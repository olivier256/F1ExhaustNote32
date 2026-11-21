/* =========================================================================
   SuperVDP : boucles des deux SH2.

   Déroulement d'une image :

     68000 (VBlank)        Master                         Slave
     --------------        ------                         -----
     COMM 4-7 = scrolls
                           relève COMM 4-7 (scène)
                           publie la scène, COMM_FRAME = n --> lit la scène
                           compose les lignes paires       compose les lignes
   impaires attend COMM_SLAVE_DONE = n  <-- COMM_SLAVE_DONE = n demande
   l'échange de frame buffer attend qu'il soit fait (VBlank) image suivante
   attend COMM_FRAME = n + 1

   Le Slave ne commence jamais avant l'échange : il attend le COMM_FRAME
   suivant, que le Master n'écrit qu'une fois l'échange fait.

   Pourquoi une ligne sur deux plutôt qu'une moitié d'écran chacun : le
   coût d'une ligne dépend de son contenu (ciel transparent, sol opaque,
   lignes mixtes). En s'entrelaçant, les deux CPU ont presque le même
   travail, et chacun possède entièrement ses lignes.
   ========================================================================= */

#include "supervdp.h"
#include "compose.h"
#include "cost.h"
#include "framebuffer.h"
#include "game.h"
#include "interface.h"
#include "platform.h"
#include "shared.h"
#include "vdp.h"

/* Scène de l'image en cours, publiée par le Master (voir shared.h) */
static Scene sharedScene;

static void initVDP(int screenHeight);
static Scene readScrollRegisters(void);
static void drawScene(const Scene *scene, int cpu, int screenHeight);
static int flipFrameBuffer(void);

/* ---- Interruptions : non utilisées (V_INT reste masquée) ---------------- */

void master_vint(void) { interface_clear_vint(); }
void slave_vint(void) { interface_clear_vint(); }

/* ---- Master ------------------------------------------------------------- */

void master_main(void) {
    const int screenHeight = vdp_screen_height();

    initVDP(screenHeight);

    uint16_t frame = 0;

    for (;;) {
        if (++frame == 0) /* 0 veut dire « pas encore d'image » */
            frame = 1;

        const Scene scene = readScrollRegisters();

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
        vdp_palette[i] = supervdp_content.palette[i];

    /* Mode Packed Pixel */
    *bitmapModeRegister = (*bitmapModeRegister & 0xFFFC) | 0x0001;
}

/* ---- Registres de défilement ---------------------------------------------
   Le 68000 peut les écrire à tout moment. D'après le manuel, un registre
   COMM lu pendant que l'autre côté l'écrit donne une valeur indéfinie, et
   le 68000 peut n'avoir écrit qu'une partie des quatre. On relit donc les
   quatre registres jusqu'à obtenir deux lectures identiques de suite. */

static Scene readOnce(void) {
    Scene s;
    s.hScrollA = (int16_t)comm[COMM_HSCROLL_A];
    s.hScrollB = (int16_t)comm[COMM_HSCROLL_B];
    s.vScrollA = (int16_t)comm[COMM_VSCROLL_A];
    s.vScrollB = (int16_t)comm[COMM_VSCROLL_B];
    return s;
}

static int sameScene(const Scene *a, const Scene *b) {
    return a->hScrollA == b->hScrollA && a->hScrollB == b->hScrollB &&
           a->vScrollA == b->vScrollA && a->vScrollB == b->vScrollB;
}

static Scene readScrollRegisters(void) {
    Scene previous = readOnce();
    for (;;) {
        const Scene current = readOnce();
        if (sameScene(&previous, &current))
            return current;
        previous = current;
    }
}

/* ---- Dessin ------------------------------------------------------------- */

/* La ligne y de l'écran montre la ligne (vScroll + y) de chaque plan */
static void drawScene(const Scene *scene, int cpu, int screenHeight) {
    const SuperVdpContent *c = &supervdp_content;
    ComposeCost cost = {0};

    /* Master : lignes paires, Slave : impaires */
    for (int y = cpu; y < screenHeight; y += PLATFORM_CPU_COUNT)
        compose_line(fb_offset(0, y), c->tiles, c->planeA, scene->hScrollA,
                     scene->vScrollA + y, c->planeB, scene->hScrollB,
                     scene->vScrollB + y, &cost);

#if SUPERVDP_COST_ENABLED && !defined(__sh__)
    /* Outil du simulateur : estimation déclarée pour cette image */
    vdp_count_cpu_cycles(cost_cycles(&cost));
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
