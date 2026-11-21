/* =========================================================================
   sh2cycles : greffon qemu (TCG plugin) qui estime le temps d'exécution
   d'un programme SH2, instruction par instruction.

   qemu-sh4eb exécute le code SH2 compilé (sous-ensemble du SH4), sans
   notion de temps. Ce greffon suit chaque instruction exécutée et chaque
   accès mémoire, et applique un modèle du SH2 de la 32X :

   Instructions (SH7604 Hardware Manual, temps d'exécution)
   - 1 cycle par instruction, sauf :
     BT/BF pris 3, non pris 1 ; BT/S, BF/S pris 2, non pris 1 ;
     BRA, BSR, BRAF, BSRF, JMP, JSR, RTS 2 ; RTE 4 ;
     MUL.L, DMULS.L, DMULU.L 2 (au mieux) ; MAC.W, MAC.L 2 ; TAS 4.
   - dépendance après chargement : si l'instruction qui suit un
     chargement lit le registre chargé, +1 cycle.

   Cache (32X Hardware Manual, « Cache ») : 4 Ko, 4 voies de 64 entrées,
   lignes de 16 octets, remplacement LRU, commun aux instructions et aux
   données, écriture simultanée (write-through) sans allocation.
   Carte mémoire, comme sur 32X :
   - 0000 0000h - 1FFF FFFFh : accès par le cache. Un défaut charge une
     ligne depuis la SDRAM : +12 cycles (8 mots en rafale). Une écriture
     part aussi en SDRAM : +1 cycle (2 cycles par mot).
   - 2400 0000h - 2403 FFFFh : frame buffer, sans cache. Les écritures
     sont comptées à part (3 cycles chacune, 5 si le FIFO est plein : le
     simulateur pixel les ajoute de son côté) ; leur coût n'est PAS dans
     les cycles CPU donnés ici.
   - 2000 0000h et 2000 0004h : marqueurs du banc d'essai. Une écriture
     en 2000 0000h ouvre une fenêtre de mesure, une écriture en
     2000 0004h la ferme et imprime sur stderr :
         sh2cycles;fenetre;cycles;instructions;defauts_cache;ecritures_fb
     (cycles hors écritures dans le frame buffer).

   Non modélisé : conflit entre lecture d'instruction et accès aux
   données, partage du bus avec l'autre SH2 (SDRAM), attente sur le FIFO.

   Construction : voir tools/sh2cycles/run.sh (il faut un qemu construit
   avec --enable-plugins, et l'en-tête qemu-plugin.h qui l'accompagne).
   ========================================================================= */

#include <qemu-plugin.h>

#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

/* ---- Modèle ----------------------------------------------------------------
 */

#define LINE_FILL_CYCLES 12  /* défaut de cache : une ligne depuis la SDRAM */
#define SDRAM_WRITE_CYCLES 1 /* écriture simultanée en SDRAM, en plus */

#define FB_START 0x24000000u
#define FB_END 0x24040000u
#define MARK_START 0x20000000u
#define MARK_STOP 0x20000004u

enum {
    KIND_PLAIN,
    KIND_BRANCH_COND,      /* BT, BF */
    KIND_BRANCH_COND_SLOT, /* BT/S, BF/S */
};

typedef struct {
    uint64_t pc;
    uint16_t op;
    uint8_t baseCycles;
    uint8_t kind;
    int8_t loadDest;   /* registre chargé, ou -1 */
    uint64_t executed; /* exécutions pendant les mesures (profil) */
} Insn;

/* Profil (option profile=on) : toutes les instructions traduites */
static int profileOn = 0;
static GPtrArray *allInsns = NULL;

/* ---- Cache SH2 : 64 entrées x 4 voies, lignes de 16 octets ------------- */

#define SETS 64
#define WAYS 4

static uint32_t tagOf[SETS][WAYS];
static uint8_t validOf[SETS][WAYS];
static uint8_t ageOf[SETS][WAYS]; /* 0 = le plus récent */

static void touch(int set, int way) {
    const uint8_t old = ageOf[set][way];
    for (int w = 0; w < WAYS; w++)
        if (ageOf[set][w] < old)
            ageOf[set][w]++;
    ageOf[set][way] = 0;
}

/* Lecture par le cache : 1 si défaut (la ligne est alors chargée) */
static int cacheRead(uint32_t addr) {
    const uint32_t line = addr >> 4;
    const int set = line & (SETS - 1);
    const uint32_t tag = line >> 6;
    for (int w = 0; w < WAYS; w++)
        if (validOf[set][w] && tagOf[set][w] == tag) {
            touch(set, w);
            return 0;
        }
    int victim = 0;
    for (int w = 0; w < WAYS; w++) {
        if (!validOf[set][w]) {
            victim = w;
            break;
        }
        if (ageOf[set][w] > ageOf[set][victim])
            victim = w;
    }
    validOf[set][victim] = 1;
    tagOf[set][victim] = tag;
    touch(set, victim);
    return 1;
}

/* Écriture : met à jour la ligne si elle est présente (pas d'allocation) */
static void cacheWrite(uint32_t addr) {
    const uint32_t line = addr >> 4;
    const int set = line & (SETS - 1);
    const uint32_t tag = line >> 6;
    for (int w = 0; w < WAYS; w++)
        if (validOf[set][w] && tagOf[set][w] == tag) {
            touch(set, w);
            return;
        }
}

/* ---- État de la mesure ----------------------------------------------------
 */

static int measuring = 0;
static int windowNo = 0;
static uint64_t cycles, insns, misses, fbWrites;

static const Insn *previous = NULL; /* instruction précédente */
static const Insn *pendingBranch = NULL;
static int pendingCountdown = 0; /* instructions avant de juger le saut */

static void startWindow(void) {
    measuring = 1;
    cycles = insns = misses = fbWrites = 0;
}

static void stopWindow(void) {
    if (!measuring)
        return;
    measuring = 0;
    fprintf(stderr,
            "sh2cycles;%d;%" PRIu64 ";%" PRIu64 ";%" PRIu64 ";%" PRIu64 "\n",
            windowNo++, cycles, insns, misses, fbWrites);
}

/* ---- Décodage --------------------------------------------------------------
 */

/* L'instruction lit-elle le registre r ? D'après les champs n (bits 11-8)
   et m (bits 7-4) de chaque famille d'instructions, plus R0 implicite. */
static int readsRegister(uint16_t op, int r) {
    const int hi = op >> 12, n = (op >> 8) & 15, m = (op >> 4) & 15,
              lo = op & 15;
    int readN = 0, readM = 0, readR0 = 0;

    switch (hi) {
    case 0x0:
        /* STC, MOVT, STS et chargements @(R0,Rm),Rn : n est la destination */
        readN =
            !(lo == 0x2 || lo == 0x9 || lo == 0xA || (lo >= 0xC && lo <= 0xE));
        readM = (lo >= 0x4 && lo <= 0x7) || (lo >= 0xC && lo <= 0xF);
        readR0 = (lo >= 0x4 && lo <= 0x6) || (lo >= 0xC && lo <= 0xE);
        break;
    case 0x1: /* MOV.L Rm,@(disp,Rn) */
    case 0x2: /* écritures, logique, TST */
    case 0x3: /* arithmétique */
        readN = readM = 1;
        break;
    case 0x4: /* décalages, LDC, JMP... */
    case 0x7: /* ADD #imm,Rn */
        readN = 1;
        break;
    case 0x5: /* MOV.L @(disp,Rm),Rn */
    case 0x6: /* MOV, chargements, EXTU, SWAP... : n est la destination */
        readM = 1;
        break;
    case 0x8:
        if (n == 0x0 || n == 0x1) /* MOV.x R0,@(disp,Rm) */
            readM = readR0 = 1;
        else if (n == 0x4 || n == 0x5) /* MOV.x @(disp,Rm),R0 */
            readM = 1;
        else if (n == 0x8) /* CMP/EQ #imm,R0 */
            readR0 = 1;
        break;
    case 0xC: /* GBR ; TST, AND, XOR, OR #imm,R0 */
        readR0 = n <= 0x2 || n >= 0x8;
        break;
    default: /* 9, A, B, D, E : aucun registre général lu */
        break;
    }
    return (readN && n == r) || (readM && m == r) || (readR0 && r == 0);
}

static void decode(Insn *in) {
    const uint16_t op = in->op;
    const int hi = op >> 12;
    const int n = (op >> 8) & 15;
    in->baseCycles = 1;
    in->kind = KIND_PLAIN;
    in->loadDest = -1;

    switch (op & 0xFF00) {
    case 0x8900: /* BT */
    case 0x8B00: /* BF */
        in->kind = KIND_BRANCH_COND;
        return;
    case 0x8D00: /* BT/S */
    case 0x8F00: /* BF/S */
        in->kind = KIND_BRANCH_COND_SLOT;
        return;
    }
    if (hi == 0xA || hi == 0xB) { /* BRA, BSR */
        in->baseCycles = 2;
        return;
    }
    if (hi == 0x0) {
        switch (op & 0xFF) {
        case 0x23: /* BRAF */
        case 0x03: /* BSRF */
            in->baseCycles = 2;
            return;
        }
        if (op == 0x000B) { /* RTS */
            in->baseCycles = 2;
            return;
        }
        if (op == 0x002B) { /* RTE */
            in->baseCycles = 4;
            return;
        }
        if ((op & 0xF) == 0x7 || (op & 0xF) == 0xF) /* MUL.L, MAC.L */
            in->baseCycles = 2;
        if ((op & 0xF) >= 0xC && (op & 0xF) <= 0xE)
            in->loadDest = (int8_t)n; /* MOV.x @(R0,Rm),Rn */
        return;
    }
    if (hi == 0x4) {
        if ((op & 0xFF) == 0x2B || (op & 0xFF) == 0x0B) { /* JMP, JSR */
            in->baseCycles = 2;
            return;
        }
        if ((op & 0xFF) == 0x1B) { /* TAS.B */
            in->baseCycles = 4;
            return;
        }
        if ((op & 0xF) == 0xF) /* MAC.W */
            in->baseCycles = 2;
        return;
    }
    if (hi == 0x3 && ((op & 0xF) == 0x5 || (op & 0xF) == 0xD)) { /* DMULx.L */
        in->baseCycles = 2;
        return;
    }
    if (hi == 0x6 &&
        ((op & 0xF) <= 0x2 || ((op & 0xF) >= 0x4 && (op & 0xF) <= 0x6))) {
        in->loadDest = (int8_t)n; /* MOV.x @Rm,Rn ; @Rm+,Rn */
        return;
    }
    if (hi == 0x5 || hi == 0x9 || hi == 0xD) {
        in->loadDest = (int8_t)n; /* @(disp,Rm) ; @(disp,PC) */
        return;
    }
    if ((op & 0xFF00) == 0x8400 || (op & 0xFF00) == 0x8500 ||
        (op & 0xFF00) == 0xC400 || (op & 0xFF00) == 0xC500 ||
        (op & 0xFF00) == 0xC600) {
        in->loadDest = 0;
        return;
    }
}

/* ---- Rappels d'exécution ---------------------------------------------------
 */

static void onInsn(unsigned int vcpu, void *udata) {
    (void)vcpu;
    const Insn *in = udata;

    /* Saut conditionnel en attente : pris si on n'est pas à la suite */
    if (pendingBranch != NULL && --pendingCountdown == 0) {
        const uint64_t fallThrough =
            pendingBranch->pc +
            (pendingBranch->kind == KIND_BRANCH_COND ? 2 : 4);
        const int taken = in->pc != fallThrough;
        if (measuring)
            cycles += pendingBranch->kind == KIND_BRANCH_COND ? (taken ? 3 : 1)
                                                              : (taken ? 2 : 1);
        pendingBranch = NULL;
    }

    if (measuring) {
        insns++;
        ((Insn *)in)->executed++;
        if (in->kind == KIND_PLAIN)
            cycles += in->baseCycles;
        if (previous != NULL && previous->loadDest >= 0 &&
            readsRegister(in->op, previous->loadDest))
            cycles++;
        /* Lecture de l'instruction par le cache */
        if (in->pc < 0x20000000u && cacheRead((uint32_t)in->pc)) {
            cycles += LINE_FILL_CYCLES;
            misses++;
        }
    } else if (in->pc < 0x20000000u) {
        cacheRead((uint32_t)in->pc); /* le cache vit aussi hors fenêtre */
    }

    if (in->kind != KIND_PLAIN) {
        pendingBranch = in;
        pendingCountdown = in->kind == KIND_BRANCH_COND ? 1 : 2;
    }
    previous = in;
}

static void onMem(unsigned int vcpu, qemu_plugin_meminfo_t info, uint64_t vaddr,
                  void *udata) {
    (void)vcpu;
    (void)udata;
    const uint32_t addr = (uint32_t)vaddr;
    const int store = qemu_plugin_mem_is_store(info);

    if (store && addr == MARK_START) {
        startWindow();
        return;
    }
    if (store && addr == MARK_STOP) {
        stopWindow();
        return;
    }

    if (addr >= FB_START && addr < FB_END) {
        if (store && measuring)
            fbWrites++;
        return;
    }
    if (addr >= 0x20000000u)
        return; /* autres accès sans cache : hors du banc */

    if (store) {
        cacheWrite(addr);
        if (measuring)
            cycles += SDRAM_WRITE_CYCLES;
    } else if (cacheRead(addr) && measuring) {
        cycles += LINE_FILL_CYCLES;
        misses++;
    }
}

static void onTranslate(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
    (void)id;
    const size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *qi = qemu_plugin_tb_get_insn(tb, i);
        const uint8_t *bytes = qemu_plugin_insn_data(qi);
        Insn *in = g_new0(Insn, 1); /* gardé pour toujours, comme le code */
        in->pc = qemu_plugin_insn_vaddr(qi);
        in->op = (uint16_t)((bytes[0] << 8) | bytes[1]); /* gros-boutiste */
        decode(in);
        if (profileOn)
            g_ptr_array_add(allInsns, in);
        qemu_plugin_register_vcpu_insn_exec_cb(qi, onInsn,
                                               QEMU_PLUGIN_CB_NO_REGS, in);
        qemu_plugin_register_vcpu_mem_cb(qi, onMem, QEMU_PLUGIN_CB_NO_REGS,
                                         QEMU_PLUGIN_MEM_RW, in);
    }
}

/* Profil : nombre d'exécutions par adresse, pendant les mesures. Une
   adresse peut avoir été traduite plusieurs fois : on additionne. */
static gint byPc(gconstpointer a, gconstpointer b) {
    const Insn *x = *(const Insn *const *)a, *y = *(const Insn *const *)b;
    return x->pc < y->pc ? -1 : x->pc > y->pc;
}

static void onExit(qemu_plugin_id_t id, void *p) {
    (void)id;
    (void)p;
    if (!profileOn)
        return;
    g_ptr_array_sort(allInsns, byPc);
    for (guint i = 0; i < allInsns->len;) {
        const Insn *in = g_ptr_array_index(allInsns, i);
        uint64_t n = 0;
        guint j = i;
        for (; j < allInsns->len &&
               ((const Insn *)g_ptr_array_index(allInsns, j))->pc == in->pc;
             j++)
            n += ((const Insn *)g_ptr_array_index(allInsns, j))->executed;
        if (n > 0)
            fprintf(stderr, "profil;%08" PRIx64 ";%04x;%" PRIu64 "\n", in->pc,
                    in->op, n);
        i = j;
    }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv) {
    (void)info;
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], "profile=on") == 0)
            profileOn = 1;
    allInsns = g_ptr_array_new();
    qemu_plugin_register_vcpu_tb_trans_cb(id, onTranslate);
    qemu_plugin_register_atexit_cb(id, onExit, NULL);
    return 0;
}
