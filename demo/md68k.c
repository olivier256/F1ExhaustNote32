/* =========================================================================
   Programme 68000 (Mega Drive) de la démo.

   À chaque VBlank, il fait avancer la voiture sur le circuit et écrit sa
   position dans les registres COMM 4 à 6 (voir race/shared.h) : pour lui,
   la 32X est un VDP qui sait dessiner une route. Les SH2 ne font que
   dessiner.

   Le 68000 n'est pas émulé : ce programme est en C, appelé par la
   plate-forme au début de chaque VBlank (voir pixel/game.h). Sur la
   console, ce serait du code 68000 : lecture du port $A10003, calculs
   (additions, décalages, et des produits 16 x 16 : MULS), puis écritures
   en $A15120 + 2 x n.

   Au début de chaque VBlank, si les SH2 ont fini une image (COMM_READY),
   il prend le VDP 32X (bit FM), transfère par DMA les données qu'ils ont
   préparées pour la Mega Drive (tiles et entrées de la table des sprites,
   voir race/shared.h), attend la fin du DMA (md_vblank_line), rend le VDP
   32X et le signale (COMM_DMA_DONE) : le Master peut alors échanger les
   frame buffers. Les entrées 0 à 9 de la table des sprites restent au
   68000 (vides pour l'instant) ; la dixième renvoie aux entrées des SH2.

   Trajectoire scriptée : la voiture accélère jusqu'à sa vitesse de
   pointe, et se place d'elle-même à l'intérieur des virages qui arrivent
   (elle regarde LOOKAHEAD segments devant elle).

   Commandes :
   - Gauche / Droite : la voiture s'écarte (le script la ramène quand on
     lâche) ;
   - B maintenu : frein ;
   - Start maintenu : pause (rien n'avance ; sert aux tests).
   ========================================================================= */

#include "game.h"
#include "interface.h"
#include "mdvdp.h"
#include "platform.h"
#include "shared.h"
#include "world.h"

#include "demo_track.h"
#include "hud.h"

/* Valeurs en 1/256 d'unité (24.8), pour avancer de fractions d'unité */
#define MAX_SPEED (192 << 8) /* unités par image : 3/4 de segment */
#define ACCELERATION 200     /* 0 à la vitesse de pointe en 4 s */
#define BRAKING 800
#define STEER (12 << 8) /* écart par image, à la main */
#define LOOKAHEAD 16    /* segments */
#define LINE_LIMIT (ROAD_HALF_WIDTH * 5 / 8)
#define PLAYER_LIMIT (ROAD_HALF_WIDTH + 384)

static uint32_t position; /* distance de la caméra, 24.8 */
static int32_t speed;     /* 24.8 par image */
static int32_t playerX;   /* écart latéral, 24.8 */

static int32_t clamp(int32_t v, int32_t min, int32_t max) {
    return v < min ? min : (v > max ? max : v);
}

static void advance(unsigned pad) {
    const Track *t = &demo_track;
    const uint32_t length = track_length(t) << 8;

    /* Vitesse et position */
    if (pad & PAD_B)
        speed = speed > BRAKING ? speed - BRAKING : 0;
    else
        speed =
            speed + ACCELERATION < MAX_SPEED ? speed + ACCELERATION : MAX_SPEED;
    position += (uint32_t)speed;
    if (position >= length)
        position -= length;

    /* Le segment sous la voiture, et celui qu'elle regarde */
    uint32_t z = (position >> 8) + PLAYER_Z;
    if (z >= track_length(t))
        z -= track_length(t);
    const unsigned here = z >> SEGMENT_SHIFT;
    unsigned ahead = here + LOOKAHEAD;
    if (ahead >= t->count)
        ahead -= t->count;

    /* Écart latéral : à la main, ou vers l'intérieur du virage qui vient
       (le quart de sa courbure, en unités), sans à-coups */
    const int dx = ((pad & PAD_RIGHT) ? 1 : 0) - ((pad & PAD_LEFT) ? 1 : 0);
    if (dx != 0) {
        playerX += dx * STEER;
    } else {
        const int32_t target =
            clamp(t->segments[ahead].curve >> 2, -LINE_LIMIT, LINE_LIMIT) * 256;
        playerX += (target - playerX) >> 5;
    }
    playerX = clamp(playerX, -(PLAYER_LIMIT * 256), PLAYER_LIMIT * 256);
}

/* ---- VDP Mega Drive -------------------------------------------------------
 */

static int vdpReady = 0;
static uint16_t transferring = 0; /* image en cours de transfert, ou 0 */
static uint16_t transferred = 0;  /* dernière image transférée */

static void initVDP(void) {
    md_vdp_set_sprite_table(MD_SPRITE_TABLE);
    md_vdp_write_cram(HUD_COLOR_DIGIT, HUD_CRAM_DIGIT); /* compteur */
    md_vdp_write_cram(HUD_COLOR_BACK, HUD_CRAM_BACK);
    /* Entrées réservées : hors de l'écran, chaînées jusqu'à la première
       entrée des SH2 */
    for (unsigned i = 0; i < MD_RESERVED_SPRITES; i++) {
        const uint16_t base = (uint16_t)(MD_SPRITE_TABLE + i * 8);
        md_vdp_write_vram(base, 0); /* y = -128 : jamais affichée */
        md_vdp_write_vram(base + 2, (uint16_t)(i + 1));
        md_vdp_write_vram(base + 4, 0);
        md_vdp_write_vram(base + 6, 0);
    }
    vdpReady = 1;
}

/* Début du VBlank : transfert de l'image prête, s'il y en a une */
static void startTransfer(void) {
    const uint16_t ready = comm[COMM_READY];
    if (ready == transferred)
        return; /* pas de nouvelle image : celle affichée reste */

    interface_write_fm(FM_68000);
    const uint16_t tileBytes = md_read_32x_fb16(MD_STAGE_OFFSET);
    const uint16_t sprites = md_read_32x_fb16(MD_STAGE_OFFSET + 2);
    const uint32_t tiles = MD_STAGE_OFFSET + MD_STAGE_HEADER;
    md_vdp_dma_from_32x(tiles, (uint16_t)(MD_SH2_FIRST_TILE * MD_TILE_BYTES),
                        tileBytes);
    md_vdp_dma_from_32x(tiles + tileBytes,
                        (uint16_t)(MD_SPRITE_TABLE + MD_RESERVED_SPRITES * 8),
                        (uint32_t)sprites * 8);
    transferring = ready;
}

/* Fin du DMA : le VDP 32X retourne aux SH2 */
static void finishTransfer(void) {
    if (transferring == 0 || md_vdp_dma_busy())
        return;
    interface_write_fm(FM_SH2);
    transferred = transferring;
    transferring = 0;
    comm[COMM_DMA_DONE] = transferred;
}

void md_vblank_line(int vblankLine) {
    (void)vblankLine;
    finishTransfer();
}

void md_vint(void) {
    if (!vdpReady)
        initVDP();

    const unsigned pad = platform_read_pad();
    if (!(pad & PAD_START))
        advance(pad);

    const uint32_t p = position >> 8;
    comm[COMM_POSITION_HI] = (uint16_t)(p >> 16);
    comm[COMM_POSITION_LO] = (uint16_t)p;
    comm[COMM_PLAYER_X] = (uint16_t)(int16_t)(playerX >> 8);

    startTransfer();
}
