# sh2cycles : temps SH2 du moteur, et étalonnage du modèle de coûts

Le simulateur pixel exécute le moteur en code natif : il compte
exactement les écritures dans le frame buffer, mais pas le temps des
SH2. Il affiche donc des cycles CPU **estimés**, d'après le modèle de
coûts de `race/cost.h`. Ce banc donne à ce modèle des valeurs mesurées.

## Ce que fait le banc

1. Le moteur (`race/project.c`, `race/render.c`, `race/hud.c`) est
   compilé **pour SH2** (`sh-elf-gcc -m2 -O2`), sans les compteurs du modèle : c'est le
   code qui tournera sur la 32X. Il est lié aux adresses de la 32X
   (`bench.ld`) : code, données (circuit, images des objets) et pile en SDRAM (0600 0000h),
   frame buffer en 2400 0000h.
2. Le programme (`bench.c`) prépare les lignes (hors mesure), puis
   projette le circuit et écrit la table de lignes pour les lignes d'un
   CPU (et le compteur pour le Master), pendant 180 images (`frames.h` :
   des positions réparties sur tout le circuit de la démo, écart latéral
   varié). Il tourne sous `qemu-sh4eb`, avec le greffon
   `sh2cycles_plugin.c` qui mesure chaque image, d'après un modèle du
   SH2 :
   - temps de chaque instruction (SH7604 Hardware Manual) : 1 cycle, sauf
     branchements (BT/BF pris 3, BT/S/BF/S pris 2, BRA, JSR, RTS... 2),
     multiplications ;
   - +1 cycle quand une instruction lit le registre que la précédente
     vient de charger ;
   - cache (lignes de 16 octets, LRU, commun aux instructions et aux
     données, écriture simultanée) : +12 cycles par défaut (une ligne
     depuis la SDRAM), +1 par écriture en SDRAM ;
   - par défaut, le cache est en mode « 2 voies + RAM » (registre CCR du
     SH2 ; 32X Hardware Manual, « Cache ») : 2 Ko de cache à 2 voies, et
     2 Ko de RAM interne en C000 0000h, sans attente, où se trouve le code
     de chaque image, `project.c`, `render_line` et `hud.c`
     (`bench_ram.ld`) ; le code froid (macro `COLD_CODE` : préparation
     des lignes, fonctions des tests) reste en SDRAM. C'est la
     configuration que suppose le moteur, comme SuperVDP. qemu
     refusant les adresses au-delà de 2 Go, la RAM interne y est placée
     en 5000 0000h ; le temps simulé est le même. `CACHE=4` : cache en
     4 voies et tout en SDRAM (`bench.ld`), pour comparer ;
   - les écritures dans le frame buffer sont comptées à part (le
     simulateur leur compte 3 à 5 cycles).
   Un programme par CPU : chacun a son propre cache.
3. Sur l'hôte, `costs.c` calcule les compteurs du modèle pour les mêmes
   images. `fit.py` vérifie que le code SH2 donne exactement le même
   affichage (empreintes de ce que le VDP montrerait : table de lignes,
   SFT, bandes) et écrit 112 mots par image (sa moitié de la table ; 280
   pour le Master, compteur compris), puis
   ajuste les coûts unitaires par moindres carrés.

Pas encore dans le banc : les objets du circuit (`race/sprites.c`) ;
leur mesure est l'étape suivante.

Non modélisé : le conflit entre lecture d'instruction et accès aux
données dans le même cycle, le partage de la SDRAM avec l'autre SH2,
l'attente quand le FIFO du frame buffer est plein. Les temps donnés sont
ceux d'un modèle, plus fin que `cost.h`, mais pas d'une 32X réelle.

## Lancer

**Sur Mac** : qemu en mode utilisateur n'existe que sous Linux. Le banc
tourne donc dans un conteneur Linux (Docker Desktop, OrbStack ou colima) :

```sh
tools/sh2cycles/docker.sh
```

La première fois, l'image est construite : chaîne `sh-elf` et qemu,
environ 40 minutes. Ensuite, le banc prend quelques secondes. Les
résultats sont dans `build/sh2cycles`.

**Sous Linux**, directement :

```sh
sudo apt install build-essential curl xz-utils libgmp-dev libmpfr-dev \
     libmpc-dev ninja-build libglib2.0-dev pkg-config python3-venv \
     python3-distlib python3-numpy flex bison texinfo
tools/sh2cycles/build-tools.sh     # une fois : ~/opt/sh-elf et ~/opt/qemu
tools/sh2cycles/run.sh
```

Ubuntu fournit `gcc-sh4-linux-gnu`, mais il ne sait produire que du SH4,
et son qemu n'a pas les greffons : d'où `build-tools.sh`. Variables :
`PREFIX` (dossier des outils, `~/opt` par défaut), ou, une par une,
`SH_ELF` (préfixe de la chaîne), `QEMU` et `QEMU_INCLUDE` (dossier de
`qemu-plugin.h`).

La même chaîne `sh-elf` servira à compiler le moteur pour la vraie 32X.

## Profil : où passe le temps

```sh
~/opt/qemu/bin/qemu-sh4eb \
    -plugin build/sh2cycles/libsh2cycles.so,ways=2,ram=0x50000000,profile=on \
    build/sh2cycles/bench_master.elf >/dev/null 2>profil.txt
```

- `profil;adresse;opcode;exécutions` : exécutions de chaque instruction
  pendant les mesures ; avec `sh-elf-objdump -d
  build/sh2cycles/bench_master.elf`, on retrouve les boucles chères ;
- `defaut;ligne;nombre` : défauts de cache par ligne de 16 octets ; avec
  `sh-elf-nm -n`, on voit quelles données (ou quel code) se chassent.

Chaque ligne `sh2cycles;...` donne aussi, en dernier champ, les défauts
de cache dus aux instructions.
