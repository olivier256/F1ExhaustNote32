#include "interface.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>

/* Registres de communication */
volatile uint16_t comm[COMM_COUNT];

/* ---- Bit FM ------------------------------------------------------------ */

static volatile int _fm = FM_SH2;

void interface_write_fm(int fm) {
    if (platform_cpu_id() >= 0) {
        fprintf(stderr, "Interface : FM ecrit par un SH2 (CPU %d)\n",
                platform_cpu_id());
        abort();
    }
    platform_sync();
    _fm = fm ? FM_SH2 : FM_68000;
    platform_sync();
}

int interface_read_fm(void) { return _fm; }

void interface_check_vdp_access(const char *what) {
    const int cpu = platform_cpu_id();
    const int sh2 = cpu >= 0;
    if (sh2 != (_fm == FM_SH2)) {
        fprintf(stderr,
                "Interface : %s par %s alors que FM donne le VDP 32X %s\n",
                what, sh2 ? (cpu == 0 ? "le Master" : "le Slave") : "le 68000",
                _fm == FM_SH2 ? "aux SH2" : "au 68000");
        abort();
    }
}

/* ---- Interruptions ------------------------------------------------------ */

/* Un masque et une V_INT en attente par CPU. La V_INT est mémorisée même
   si elle est masquée : la démasquer ensuite la déclenche aussitôt. Une
   routine qui veut l'éviter acquitte avant de démasquer. */
static volatile uint16_t _intMask[PLATFORM_CPU_COUNT];
static volatile int _vintPending[PLATFORM_CPU_COUNT];

/* H_INT : une en attente par CPU ; compteur H et HEN communs */
static volatile int _hintPending[PLATFORM_CPU_COUNT];
static volatile uint16_t _hcount = 0;
static volatile int _hen = 0;
static int _hcounter = 0; /* lignes avant la prochaine H_INT (synchro vidéo) */
static volatile unsigned long _hintRaised = 0;
static volatile unsigned long _hintHandled[PLATFORM_CPU_COUNT];
static volatile unsigned long _hintLate[PLATFORM_CPU_COUNT];

static volatile unsigned long _raised = 0;
static volatile unsigned long _handled[PLATFORM_CPU_COUNT];
static volatile unsigned long _unacked[PLATFORM_CPU_COUNT];

uint16_t interface_read_intmask(void) {
    int cpu = platform_cpu_id();
    return (uint16_t)((cpu >= 0 ? _intMask[cpu] : 0) |
                      (_hen ? INTMASK_HEN : 0));
}

void interface_write_intmask(uint16_t value) {
    int cpu = platform_cpu_id();
    if (cpu < 0)
        return;

    _intMask[cpu] = value & 0x000F;
    _hen = (value & INTMASK_HEN) != 0;
    platform_sync();

    /* Effet immédiat : une interruption déjà en attente se déclenche */
    if (interface_vint_active(cpu) || interface_hint_active(cpu))
        platform_raise_interrupt(cpu);
}

void interface_clear_vint(void) {
    int cpu = platform_cpu_id();
    if (cpu < 0)
        return;

    _vintPending[cpu] = 0;
    platform_sync();
}

void interface_raise_vint(void) {
    _raised++;

    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++) {
        _vintPending[cpu] = 1;
        platform_sync();

        if (_intMask[cpu] & INTMASK_V)
            platform_raise_interrupt(cpu);
    }
}

int interface_vint_active(int cpu) {
    return _vintPending[cpu] && (_intMask[cpu] & INTMASK_V);
}

/* ---- H_INT ----------------------------------------------------------------
 */

void interface_clear_hint(void) {
    int cpu = platform_cpu_id();
    if (cpu < 0)
        return;
    _hintPending[cpu] = 0;
    platform_sync();
}

uint16_t interface_read_hcount(void) { return _hcount; }

void interface_write_hcount(uint16_t value) {
    _hcount = value & 0x00FF;
    platform_sync();
}

int interface_hint_active(int cpu) {
    return _hintPending[cpu] && (_intMask[cpu] & INTMASK_H);
}

void interface_hblank(int inVBlank) {
    if (inVBlank && !_hen) {
        _hcounter = 0; /* le décompte repart avec la première ligne */
        return;
    }
    if (_hcounter > 0) {
        _hcounter--;
        return;
    }
    _hcounter = _hcount;
    _hintRaised++;

    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++) {
        if (!(_intMask[cpu] & INTMASK_H))
            continue;
        if (_hintPending[cpu])
            _hintLate[cpu]++; /* la précédente n'a pas été servie à temps */
        _hintPending[cpu] = 1;
        platform_sync();
        platform_raise_interrupt(cpu);
    }
}

void interface_count_hint_handled(int cpu) { _hintHandled[cpu]++; }

int interface_hint_waiting(void) {
    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++)
        if (interface_hint_active(cpu))
            return 1;
    return 0;
}

void interface_count_handled(int cpu, int acknowledged) {
    _handled[cpu]++;
    if (!acknowledged)
        _unacked[cpu]++;
}

void interface_get_stats(interface_stats *stats) {
    stats->raised = _raised;
    for (int i = 0; i < PLATFORM_CPU_COUNT; i++) {
        stats->handled[i] = _handled[i];
        stats->unacked[i] = _unacked[i];
        stats->hintHandled[i] = _hintHandled[i];
        stats->hintLate[i] = _hintLate[i];
    }
    stats->hintRaised = _hintRaised;
}
