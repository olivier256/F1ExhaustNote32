/* =========================================================================
   Moteur de course : boucles des deux SH2.

   Déroulement d'une image (n) :

     68000                  Master                     Slave
     -----                  ------                     -----
                            relève COMM 4-6 (scène),
                            la publie,
                            COMM_FRAME = n        -->  lit la scène
                            projette, écrit les        projette, écrit
                            entrées paires de la       les entrées
                            table de lignes, et le     impaires
                            compteur (sprites MD)
                            attend                <--  COMM_SLAVE_DONE = n
                            COMM_READY = n
                            attend (sans toucher
                            au VDP 32X)
     VBlank : voit
     COMM_READY = n,
     prend le VDP 32X
     (FM = 0), DMA du
     frame buffer de
     dessin vers la VRAM,
     rend le VDP (FM = 1),
     COMM_DMA_DONE = n -->  demande l'échange de
                            frame buffer : pendant
                            le VBlank, il est
                            immédiat ; prépare la
                            ligne 0 de l'image
                            affichée
                            image suivante              attend COMM_FRAME
                                                        = n + 1

   L'échange de frame buffer est retardé jusqu'après le DMA : le 68000 ne
   voit que le frame buffer qui n'est pas affiché, et il doit y lire les
   données de l'image n avant qu'elle ne s'affiche. La route 32X et les
   sprites Mega Drive de la même image apparaissent ensemble.

   Le bit FM ne donne le VDP 32X qu'à un côté à la fois : les SH2 ne s'en
   servent jamais entre COMM_READY et COMM_DMA_DONE (ni table de lignes, ni
   palette, ni SFT, ni FBCR). Rien ne leur manque : il n'y a pas de H_INT
   pendant le VBlank, et la ligne 0 est préparée avant (par la H_INT de la
   dernière ligne affichée, pour l'image en cours) ou après (par le Master,
   après l'échange, pour la nouvelle).

   Si une image n'est pas prête au début du VBlank, le 68000 ne transfère
   rien et l'image affichée reste : elle sera transférée et échangée au
   VBlank suivant.

   Chaque CPU projette lui-même le circuit, pour ses seules lignes : aucun
   CPU n'attend l'autre au milieu de l'image ni ne lit ce que l'autre a
   calculé.

   Les pixels de la route ne changent jamais : au démarrage, le Master
   écrit les lignes préparées (render.h) dans les deux frame buffers.
   Ensuite, une image, c'est la table de lignes (une entrée par ligne
   d'écran) et, pour chaque ligne, ce que le Master écrira au HBlank qui la
   précède : le décalage d'écran (SFT) et les couleurs claires ou sombres
   de la route. Chaque CPU range ces attributs de ses lignes dans la table
   du frame buffer qu'il dessine ; pendant l'affichage, le Master les
   applique depuis la table du frame buffer affiché, dans sa routine H_INT.
   C'est le Master parce qu'il a la priorité sur le bus.
   ========================================================================= */

#include "race.h"
#include "cost.h"
#include "framebuffer.h"
#include "game.h"
#include "hud.h"
#include "interface.h"
#include "platform.h"
#include "project.h"
#include "render.h"
#include "shared.h"
#include "vdp.h"

/* Scène de l'image en cours, publiée par le Master (voir shared.h) */
static Scene sharedScene;

/* Attributs de chaque ligne (LINE_SFT, LINE_DARK, voir render.h), une
   table par frame buffer (indice : valeur de FS quand il est affiché).
   Écrite par les deux CPU (chacun ses lignes), lue par les interruptions
   du Master : accès sans cache. */
#define MAX_LINES 240
static uint8_t lineAttributes[2][MAX_LINES];

/* Description des lignes, une table par CPU (chacun n'y lit et n'y écrit
   que ses lignes) */
static RoadLine roadLines[PLATFORM_CPU_COUNT][MAX_LINES];

/* Plan des lignes préparées, calculé par chaque CPU (le même) */
static RoadLayout layouts[PLATFORM_CPU_COUNT];

/* État des interruptions du Master */
static int screenLines;                         /* lignes affichées */
static volatile const uint8_t *shownAttributes; /* frame buffer affiché */
static volatile int nextLine;       /* ligne que prépare la prochaine H_INT */
static volatile unsigned shownDark; /* couleurs actuelles : LINE_DARK ou 0 */

static void initVDP(int screenHeight);
static Scene readCommRegisters(void);
static void drawScene(const Scene *scene, int cpu, int screenHeight);
static int flipAfterTransfer(uint16_t frame);

/* ---- Interruptions ----------------------------------------------------------
   Master : H_INT, pour le décalage d'écran et les couleurs de la route.
   Sa V_INT ne fait rien : au début du VBlank, le VDP 32X peut être au
   68000. Slave : aucune (masquées).

   La palette du VDP 32X n'est accessible que pendant les blancs : les
   quatre couleurs de la route s'écrivent donc au HBlank, comme SFT. On ne
   les écrit que quand la ligne suivante change de bande (claire ou
   sombre). Chaque routine n'acquitte son interruption qu'à la fin : dans
   le simulateur, la synchro vidéo attend l'acquittement pour balayer la
   ligne suivante. */

/* Outil du simulateur : changements de couleurs depuis la dernière image
   (estimation des cycles du Master) */
static volatile uint32_t paletteSwaps;

static void setRoadColors(unsigned dark) {
    const uint16_t *colors = dark ? road_dark : road_light;
    for (int i = 0; i < ROAD_COLORS; i++)
        vdp_write_palette(COLOR_ROAD_FIRST + i, colors[i]);
    shownDark = dark;
    paletteSwaps = paletteSwaps + 1;
}

/* Prépare la ligne 0 du frame buffer affiché (valeur de FS : shown) :
   SFT et couleurs, valables jusqu'à la fin du VBlank */
static void prepareLine0(int shown) {
    shownAttributes = SHARED(&lineAttributes[shown][0]);
    const unsigned a = shownAttributes[0];
    vdp_write_sft((uint16_t)(a & LINE_SFT));
    if ((a & LINE_DARK) != shownDark)
        setRoadColors(a & LINE_DARK);
    nextLine = 1;
}

void master_vint(void) { interface_clear_vint(); }

/* HBlank de chaque ligne affichée : prépare la ligne suivante. Après la
   dernière, la ligne 0 de la même image : si une nouvelle image est
   échangée pendant le VBlank, le Master préparera sa ligne 0 lui-même. */
static volatile int currentShown = 0; /* FS de l'image affichée */

void master_hint(void) {
    const int line = nextLine;
    if (line < screenLines) {
        const unsigned a = shownAttributes[line];
        vdp_write_sft((uint16_t)(a & LINE_SFT));
        if ((a & LINE_DARK) != shownDark)
            setRoadColors(a & LINE_DARK);
        nextLine = line + 1;
    } else {
        prepareLine0(currentShown);
    }
    interface_clear_hint();
}

void slave_vint(void) { interface_clear_vint(); }
void slave_hint(void) { interface_clear_hint(); }

/* ---- Master ------------------------------------------------------------- */

void master_main(void) {
    const int screenHeight = vdp_screen_height();

    initVDP(screenHeight);

    /* Interruptions du Master : V_INT, et H_INT à chaque ligne affichée */
    screenLines = screenHeight < MAX_LINES ? screenHeight : MAX_LINES;
    shownAttributes = SHARED(&lineAttributes[0][0]);
    interface_write_hcount(0);
    interface_clear_vint();
    interface_clear_hint();
    interface_write_intmask(INTMASK_V | INTMASK_H);
    currentShown = (vdp_read_fbcr() & FBCR_FS) ? 1 : 0;

    uint16_t frame = 0;

    for (;;) {
        if (++frame == 0) /* 0 veut dire « pas encore d'image » */
            frame = 1;

        Scene scene = readCommRegisters();
        scene.drawBuffer = (vdp_read_fbcr() & FBCR_FS) ? 0 : 1;

        /* Publier la scène, puis lancer le Slave */
        *SHARED(&sharedScene) = scene;
        SHARED_BARRIER();
        comm[COMM_FRAME] = frame;

        drawScene(&scene, PLATFORM_CPU_MASTER, screenHeight);

        /* Attendre que le Slave ait fini ses lignes */
        while (comm[COMM_SLAVE_DONE] != frame)
            if (!platform_running())
                return;

        if (!flipAfterTransfer(frame))
            return;
    }
}

/* ---- Slave -------------------------------------------------------------- */

void slave_main(void) {
    const int screenHeight = vdp_screen_height();
    uint16_t lastFrame = 0;

    render_layout(&layouts[PLATFORM_CPU_SLAVE]);

    for (;;) {
        uint16_t frame;
        while ((frame = comm[COMM_FRAME]) == lastFrame)
            if (!platform_running())
                return;

        SHARED_BARRIER();
        const Scene scene = *SHARED(&sharedScene);

        drawScene(&scene, PLATFORM_CPU_SLAVE, screenHeight);

        SHARED_BARRIER();
        comm[COMM_SLAVE_DONE] = frame;
        lastFrame = frame;
    }
}

/* ---- Initialisation ----------------------------------------------------- */

static void initVDP(int screenHeight) {
    /* Mode Blank : l'échange de frame buffer est immédiat */
    *bitmapModeRegister = (*bitmapModeRegister & 0xFFFC);

    /* Les lignes préparées, et une table de lignes qui montre le sol, dans
       chacun des deux frame buffers. Elles tiennent (vérifié par
       test_race) : sinon, on reste en mode Blank, écran noir. */
    const RoadLayout *layout = &layouts[PLATFORM_CPU_MASTER];
    render_layout(&layouts[PLATFORM_CPU_MASTER]);
    if (layout->end > MD_STAGE_OFFSET)
        return; /* la zone de transfert vers la Mega Drive serait écrasée */
    for (int fb = 0; fb < 2; fb++) {
        if (!render_prepare(layout))
            return;
        fb_init_line_table(screenHeight, layout->sky[SKY_BANDS]);
        vdp_write_fbcr(vdp_read_fbcr() ^ FBCR_FS);
    }

    for (int i = 0; i <= SKY_BANDS; i++)
        vdp_write_palette(COLOR_SKY_FIRST + i, sky_palette[i]);
    setRoadColors(0);

    /* Mode Packed Pixel ; l'image Mega Drive (sprites) devant */
    *bitmapModeRegister =
        (uint16_t)((*bitmapModeRegister & 0xFFFC) | 0x0001 | BITMAP_PRI);
}

/* ---- Registres COMM -------------------------------------------------------
   Le 68000 peut les écrire à tout moment. D'après le manuel, un registre
   COMM lu pendant que l'autre côté l'écrit donne une valeur indéfinie, et
   le 68000 peut n'avoir écrit qu'une partie des trois. On relit donc les
   trois registres jusqu'à obtenir deux lectures identiques de suite. */

static View readOnce(void) {
    View v;
    v.position = (uint32_t)comm[COMM_POSITION_HI] << 16 |
                 (uint32_t)comm[COMM_POSITION_LO];
    v.playerX = (int16_t)comm[COMM_PLAYER_X];
    return v;
}

static int sameView(const View *a, const View *b) {
    return a->position == b->position && a->playerX == b->playerX;
}

static Scene readCommRegisters(void) {
    View previous = readOnce();
    for (;;) {
        const View current = readOnce();
        if (sameView(&previous, &current)) {
            Scene s = {current, 0};
            return s;
        }
        previous = current;
    }
}

/* ---- Dessin ------------------------------------------------------------- */

static void drawScene(const Scene *scene, int cpu, int screenHeight) {
    const RaceContent *c = &race_content;
    ProjectCost projectCost = {0};
    RenderCost renderCost = {0};

    const int height = screenHeight < MAX_LINES ? screenHeight : MAX_LINES;
    RoadLine *lines = roadLines[cpu];
    const RoadLayout *layout = &layouts[cpu];
    uint8_t *const attributes = SHARED(&lineAttributes[scene->drawBuffer][0]);

    /* Master : lignes paires, Slave : impaires */
    project_lines(c->track, &scene->view, lines, height, cpu,
                  PLATFORM_CPU_COUNT, &projectCost);
    for (int y = cpu; y < height; y += PLATFORM_CPU_COUNT)
        attributes[y] = (uint8_t)render_line(y, &lines[y], layout, &renderCost);

    /* Les sprites Mega Drive : pour l'instant, le compteur (Master) */
    if (cpu == PLATFORM_CPU_MASTER)
        hud_build(scene->view.position);

#if RACE_COST_ENABLED && !defined(__sh__)
    /* Outil du simulateur : estimation déclarée pour cette image. Le
       Master paie en plus ses interruptions (une H_INT par ligne et une
       V_INT, et les changements de couleurs de l'image précédente),
       estimées. */
    uint32_t cycles = cost_cycles(&projectCost, &renderCost);
    if (cpu == PLATFORM_CPU_MASTER) {
        cycles += (uint32_t)(screenHeight + 1) * CYCLES_INTERRUPT;
        cycles += paletteSwaps * CYCLES_PALETTE_SWAP + CYCLES_HUD;
        paletteSwaps = 0;
    }
    vdp_count_cpu_cycles(cycles);
#else
    (void)projectCost;
    (void)renderCost;
#endif
}

/* Fin d'une image : la confie au 68000, attend son DMA, puis l'affiche.

   1. COMM_READY = frame : à partir de là, plus aucun accès au VDP 32X
      (le 68000 peut prendre le bit FM au début du VBlank).
   2. COMM_DMA_DONE = frame : le 68000 a transféré les données pour la Mega
      Drive et rendu le VDP 32X.
   3. Échange de frame buffer. Demandé pendant le VBlank, il est immédiat
      (sur 32X comme dans le simulateur). Si le DMA a débordé sur
      l'affichage (trop de données), on attend le VBlank suivant : l'image
      affichée reste une image de plus, et ses sprites Mega Drive ont déjà
      changé (compté par le simulateur : échange hors du VBlank du DMA).
   4. Ligne 0 de la nouvelle image : SFT et couleurs, encore dans le
      VBlank (pas de H_INT avant la fin du VBlank). */
static int flipAfterTransfer(uint16_t frame) {
    SHARED_BARRIER();
    comm[COMM_READY] = frame;
    while (comm[COMM_DMA_DONE] != frame)
        if (!platform_running())
            return 0;

    while (!(vdp_read_fbcr() & FBCR_VBLK))
        if (!platform_running())
            return 0;

    const uint16_t wanted = (vdp_read_fbcr() ^ FBCR_FS) & FBCR_FS;
    vdp_write_fbcr(wanted);
    while ((vdp_read_fbcr() & FBCR_FS) != wanted)
        if (!platform_running())
            return 0;

    currentShown = wanted ? 1 : 0;
    prepareLine0(currentShown);
    return 1;
}
