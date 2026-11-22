#ifndef INTERFACE_H
#define INTERFACE_H

#include <stdint.h>

/* =========================================================================
   Interface 32X (émulée) : IC4, circuit 315-5818, « MEGA Drive I/F ».
   Module sans thread.

   Sur la vraie machine, l'IC4 regroupe les registres système
   (2000 4000h - 2000 403Fh côté SH2) : masques et acquittement des
   interruptions, compteur H, FIFO DREQ, PWM, registres de communication...
   Émulés pour l'instant : les registres de communication, V_INT et H_INT
   (avec le compteur H et le bit HEN).
   ========================================================================= */

/* ---- Registres de communication ------------------------------------------
   8 mots partagés par les CPU (2000 4020h - 2000 402Eh). Accès direct,
   comme vdp_palette.
   volatile : chaque lecture relit vraiment le registre (boucles d'attente).
   Attention à l'accès par octet via un cast : l'hôte est petit-boutiste,
   le SH2 gros-boutiste (sur 32X, 2000 4020h est l'octet de poids FORT). */

#define COMM_COUNT 8

extern volatile uint16_t comm[COMM_COUNT];

/* ---- Bit FM : qui a le VDP 32X ---------------------------------------------
   Le frame buffer, la palette et les registres du VDP 32X (FBCR, SFT,
   mode) ne sont accessibles qu'à un côté à la fois : aux SH2 si FM vaut
   1, au 68000 s'il vaut 0. C'est le 68000 qui l'écrit (registre de
   contrôle de l'adaptateur, $A15100, bit 15).
   Au démarrage du simulateur, FM vaut 1 : comme si le programme 68000
   avait donné le VDP 32X aux SH2.
   interface_check_vdp_access() est appelée par chaque accès du simulateur
   au VDP 32X : si le CPU qui accède ne l'a pas, le simulateur s'arrête
   avec un message (sur la console, l'accès serait perdu ou donnerait
   n'importe quoi). Le 68000 est le code qui tourne hors des threads des
   SH2 (platform_cpu_id() < 0). */

#define FM_SH2 1   /* les SH2 ont le VDP 32X */
#define FM_68000 0 /* le 68000 l'a */

void interface_write_fm(int fm); /* 68000 seulement */
int interface_read_fm(void);
void interface_check_vdp_access(const char *what);

/* ---- Interruptions : vu des CPU (jeu) -------------------------------------
   Interrupt Mask Register (2000 4000h, bits 3 à 0). Il en existe un par
   CPU, à la même adresse : chaque fonction agit sur le registre du CPU
   qui l'appelle. Bit à 1 = interruption transmise au CPU ; à 0 = masquée
   (valeur au démarrage).

   Le masque et l'acquittement passent par des fonctions plutôt que par un
   pointeur, pour la même raison que le FBCR (vdp_write_fbcr) : l'écriture
   a un effet immédiat. Si l'on démasque V_INT alors qu'elle est déjà en
   attente, elle doit se déclencher aussitôt ; avec une simple écriture en
   mémoire, personne ne le saurait. */

#define INTMASK_PWM 0x0001
#define INTMASK_CMD 0x0002
#define INTMASK_H 0x0004
#define INTMASK_V 0x0008
/* Bit 7 du même registre, commun aux deux CPU : H_INT aussi pendant le
   VBlank (0 : seulement sur les lignes affichées) */
#define INTMASK_HEN 0x0080

uint16_t interface_read_intmask(void);
void interface_write_intmask(uint16_t value);

/* V Interrupt Clear Register (2000 4016h) : acquitte V_INT pour le CPU
   appelant. Une V_INT reste active tant qu'elle n'est pas acquittée : la
   routine d'interruption doit l'acquitter, sinon elle est rappelée dès
   son retour, indéfiniment (comme sur 32X). */
void interface_clear_vint(void);

/* H Interrupt Clear Register (2000 4018h) : acquitte H_INT pour le CPU
   appelant (même règle que V_INT). */
void interface_clear_hint(void);

/* H Count Register (2000 4004h) : une H_INT toutes les (valeur + 1)
   lignes ; 0 = à chaque ligne (valeur au démarrage). Commun aux deux CPU.
   Le décompte repart de zéro pendant le VBlank (si HEN = 0). */
uint16_t interface_read_hcount(void);
void interface_write_hcount(uint16_t value);

/* ---- Interruptions : vu de la plate-forme ---------------------------------
 */

/* Le VDP signale le début du VBlank. V_INT est mémorisée pour chaque CPU
   et transmise à ceux qui l'ont démasquée. */
void interface_raise_vint(void);

/* 1 si une V_INT est en attente et démasquée pour ce CPU */
int interface_vint_active(int cpu);

/* Le VDP signale le HBlank de chaque ligne (inVBlank : ligne hors de
   l'image). Lève H_INT selon le compteur H et HEN.
   Contrôle : une H_INT encore en attente au HBlank suivant n'a pas été
   servie à temps. La valeur écrite par sa routine (SFT, par exemple)
   arrive alors trop tard pour la ligne voulue : compté dans hintLate. */
void interface_hblank(int inVBlank);

/* 1 si une H_INT est en attente et démasquée pour ce CPU */
int interface_hint_active(int cpu);

/* 1 si une H_INT démasquée attend encore sa routine, sur l'un des CPU */
int interface_hint_waiting(void);

/* Diagnostics */
typedef struct {
    unsigned long raised;         /* V_INT levées par le VDP */
    unsigned long handled[2];     /* routines exécutées, par CPU */
    unsigned long unacked[2];     /* routines revenues sans acquitter */
    unsigned long hintRaised;     /* H_INT levées (lignes) */
    unsigned long hintHandled[2]; /* routines H_INT exécutées, par CPU */
    unsigned long hintLate[2];    /* H_INT pas servies au HBlank suivant */
} interface_stats;

void interface_count_handled(int cpu, int acknowledged);
void interface_count_hint_handled(int cpu);
void interface_get_stats(interface_stats *stats);

#endif
