/* =========================================================================
   Moteur de course : boucles des deux SH2.

   Déroulement d'une image :

     68000 (VBlank)  Master                       Slave
     --------------  ------                       -----
     COMM 4-7 =      relève COMM 4-7 (scène)
     position,       publie la scène,
     écart, décor    COMM_FRAME = n          -->  lit la scène
                     projette le circuit          projette le circuit
                     (ses lignes) et dessine      (ses lignes) et dessine
                     les lignes paires            les lignes impaires
                     attend                  <--  COMM_SLAVE_DONE = n
                     COMM_SLAVE_DONE = n
                     demande l'échange de
                     frame buffer, attend
                     qu'il soit fait (VBlank)
                     image suivante               attend COMM_FRAME = n + 1

   Le Slave ne commence jamais avant l'échange : il attend le COMM_FRAME
   suivant, que le Master n'écrit qu'une fois l'échange fait.

   Chaque CPU projette lui-même le circuit, pour ses seules lignes : la
   projection est bon marché (quelques centaines de points), et ainsi
   aucun CPU n'attend l'autre au milieu de l'image ni ne lit ce que
   l'autre a calculé. Une ligne sur deux plutôt qu'une moitié d'écran
   chacun : le haut (décor) et le bas (route) ne coûtent pas pareil.

   Décalage d'écran (SFT) ligne par ligne, comme dans SuperVDP : une ligne
   de décor dont la colonne de départ est impaire est copiée depuis la
   colonne paire précédente et affichée décalée d'un point. Chaque CPU
   range le décalage de ses lignes dans la table du frame buffer qu'il
   dessine ; pendant l'affichage, le Master écrit SFT ligne par ligne
   depuis la table du frame buffer affiché : sa routine V_INT pour la
   ligne 0, sa routine H_INT (au HBlank de chaque ligne) pour la suivante.
   C'est le Master parce qu'il a la priorité sur le bus.
   ========================================================================= */

#include "race.h"
#include "cost.h"
#include "framebuffer.h"
#include "game.h"
#include "interface.h"
#include "platform.h"
#include "project.h"
#include "render.h"
#include "shared.h"
#include "vdp.h"

/* Scène de l'image en cours, publiée par le Master (voir shared.h) */
static Scene sharedScene;

/* Décalage d'écran de chaque ligne, une table par frame buffer (indice :
   valeur de FS quand il est affiché). Écrite par les deux CPU (chacun ses
   lignes), lue par les interruptions du Master : accès sans cache. */
#define MAX_LINES 240
static uint8_t lineShift[2][MAX_LINES];

/* Description des lignes, une table par CPU (chacun n'y lit et n'y écrit
   que ses lignes) */
static RoadLine roadLines[PLATFORM_CPU_COUNT][MAX_LINES];

/* État des interruptions du Master */
static int screenLines;                    /* lignes affichées */
static volatile const uint8_t *shownShift; /* table du frame buffer affiché */
static volatile int nextShiftLine;         /* ligne dont H_INT écrit SFT */

static void initVDP(int screenHeight);
static Scene readCommRegisters(void);
static void drawScene(const Scene *scene, int cpu, int screenHeight);
static int flipFrameBuffer(void);

/* ---- Interruptions ----------------------------------------------------------
   Master : V_INT et H_INT, pour le décalage d'écran. Slave : aucune
   (masquées). */

/* Début du VBlank : l'échange de frame buffer demandé a eu lieu (ou pas,
   si l'image n'était pas finie) ; FS dit lequel s'affiche. On prend sa
   table, et l'on écrit le décalage de la ligne 0, valable dès la fin du
   VBlank. */
void master_vint(void) {
    interface_clear_vint();
    const int shown = (vdp_read_fbcr() & FBCR_FS) ? 1 : 0;
    shownShift = SHARED(&lineShift[shown][0]);
    *screenShiftRegister = shownShift[0];
    nextShiftLine = 1;
}

/* HBlank de chaque ligne affichée : le décalage de la ligne suivante */
void master_hint(void) {
    interface_clear_hint();
    const int line = nextShiftLine;
    if (line < screenLines) {
        *screenShiftRegister = shownShift[line];
        nextShiftLine = line + 1;
    }
}

void slave_vint(void) { interface_clear_vint(); }
void slave_hint(void) { interface_clear_hint(); }

/* ---- Master ------------------------------------------------------------- */

void master_main(void) {
    const int screenHeight = vdp_screen_height();

    initVDP(screenHeight);

    /* Interruptions du Master : V_INT, et H_INT à chaque ligne affichée */
    screenLines = screenHeight < MAX_LINES ? screenHeight : MAX_LINES;
    shownShift = SHARED(&lineShift[0][0]);
    interface_write_hcount(0);
    interface_clear_vint();
    interface_clear_hint();
    interface_write_intmask(INTMASK_V | INTMASK_H);

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

        if (!flipFrameBuffer())
            return;
    }
}

/* ---- Slave -------------------------------------------------------------- */

void slave_main(void) {
    const int screenHeight = vdp_screen_height();
    uint16_t lastFrame = 0;

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

    /* Une table de lignes dans chacun des deux frame buffers */
    fb_init_line_table(screenHeight);
    vdp_write_fbcr(vdp_read_fbcr() ^ FBCR_FS);
    fb_init_line_table(screenHeight);

    for (int i = 0; i < PALETTE_COLORS; i++)
        vdp_palette[i] = race_content.palette[i];
    for (int i = 0; i < ROAD_COLOR_COUNT; i++)
        vdp_palette[ROAD_COLOR_FIRST + i] = road_palette[i];

    /* Mode Packed Pixel */
    *bitmapModeRegister = (*bitmapModeRegister & 0xFFFC) | 0x0001;
}

/* ---- Registres COMM -------------------------------------------------------
   Le 68000 peut les écrire à tout moment. D'après le manuel, un registre
   COMM lu pendant que l'autre côté l'écrit donne une valeur indéfinie, et
   le 68000 peut n'avoir écrit qu'une partie des quatre. On relit donc les
   quatre registres jusqu'à obtenir deux lectures identiques de suite. */

static View readOnce(void) {
    View v;
    v.position = (uint32_t)comm[COMM_POSITION_HI] << 16 |
                 (uint32_t)comm[COMM_POSITION_LO];
    v.playerX = (int16_t)comm[COMM_PLAYER_X];
    v.skyX = comm[COMM_SKY_X];
    return v;
}

static int sameView(const View *a, const View *b) {
    return a->position == b->position && a->playerX == b->playerX &&
           a->skyX == b->skyX;
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
    uint8_t *const shifts = SHARED(&lineShift[scene->drawBuffer][0]);

    /* Master : lignes paires, Slave : impaires */
    project_lines(c->track, &scene->view, lines, height, cpu,
                  PLATFORM_CPU_COUNT, &projectCost);
    for (int y = cpu; y < height; y += PLATFORM_CPU_COUNT)
        shifts[y] = (uint8_t)render_line(fb_offset(0, y), &lines[y], c->sky,
                                         scene->view.skyX, y, &renderCost);

#if RACE_COST_ENABLED && !defined(__sh__)
    /* Outil du simulateur : estimation déclarée pour cette image. Le
       Master paie en plus ses interruptions (une H_INT par ligne et une
       V_INT), estimées. */
    uint32_t cycles = cost_cycles(&projectCost, &renderCost);
    if (cpu == PLATFORM_CPU_MASTER)
        cycles += (uint32_t)(screenHeight + 1) * CYCLES_INTERRUPT;
    vdp_count_cpu_cycles(cycles);
#else
    (void)projectCost;
    (void)renderCost;
#endif
}

/* Demande l'échange des frame buffers et attend qu'il soit fait.

   Sur 32X comme dans le simulateur, une demande faite pendant l'affichage
   prend effet au début du VBlank suivant, mais une demande faite PENDANT
   le VBlank prend effet aussitôt. Si une image est dessinée plus vite que
   le VBlank (c'est le cas sur l'hôte), plusieurs échanges auraient lieu
   dans le même VBlank : on tournerait à plus de 60 images/s.
   On attend donc la fin du VBlank avant de demander l'échange : il a
   lieu au VBlank suivant, une fois par image au plus. */
static int flipFrameBuffer(void) {
    while (vdp_read_fbcr() & FBCR_VBLK)
        if (!platform_running())
            return 0;

    const uint16_t wanted = (vdp_read_fbcr() ^ FBCR_FS) & FBCR_FS;
    vdp_write_fbcr(wanted);

    while ((vdp_read_fbcr() & FBCR_FS) != wanted)
        if (!platform_running())
            return 0;
    return 1;
}
