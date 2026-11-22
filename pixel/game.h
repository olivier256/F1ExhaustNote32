#ifndef GAME_H
#define GAME_H

/* Points d'entrée des deux CPU, lancés par la plate-forme
   (comme la BootROM saute aux adresses de l'en-tête de la cartouche). */
void master_main(void);
void slave_main(void);

/* Routines d'interruption V_INT des deux CPU, appelées par la plate-forme
   (comme le SH2 saute à l'adresse lue dans sa table de vecteurs).
   Elles s'exécutent en interrompant le CPU, où qu'il en soit : elles
   doivent rester courtes, ne toucher que des variables volatile et
   n'appeler aucune fonction de la bibliothèque C (pas de printf, malloc...).
   Elles doivent acquitter l'interruption (interface_clear_vint), sinon
   elles sont rappelées indéfiniment. */
void master_vint(void);
void slave_vint(void);

/* Routines d'interruption H_INT (même règles ; acquitter par
   interface_clear_hint). V_INT passe avant H_INT, comme sur le SH2 (niveau
   12 contre 10). */
void master_hint(void);
void slave_hint(void);

/* Routine V_INT du 68000 (le programme Mega Drive de la cartouche).
   Le 68000 n'est pas émulé : la plate-forme appelle cette fonction au
   début de chaque VBlank, depuis la synchro vidéo. Elle doit être très
   courte (lire la manette, écrire les registres COMM). Elle ne touche au
   frame buffer 32X qu'après avoir pris le VDP 32X (bit FM, interface.h),
   et le rend avant la fin du VBlank. */
void md_vint(void);

/* Suite du programme 68000 pendant le VBlank : appelée au début de
   chaque ligne du VBlank après la première (vblankLine = 1, 2...). Le
   68000 y attend par exemple la fin d'un DMA (mdvdp.h) avant de rendre
   le VDP 32X aux SH2. Mêmes règles que md_vint. */
void md_vblank_line(int vblankLine);

#endif
