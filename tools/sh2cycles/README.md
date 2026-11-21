# sh2cycles : temps SH2 du moteur, et étalonnage du modèle de coûts

Le simulateur pixel exécute SuperVDP en code natif : il compte exactement
les écritures dans le frame buffer, mais pas le temps des SH2. Il affiche
donc des cycles CPU **estimés**, d'après le modèle de coûts de
`supervdp/cost.h`. Ce banc donne à ce modèle des valeurs mesurées.

## Ce que fait le banc

1. `supervdp/compose.c` est compilé **pour SH2** (`sh-elf-gcc -m2 -O2`),
   sans les compteurs du modèle : c'est le code qui tournera sur la 32X.
   Il est lié aux adresses de la 32X (`bench.ld`) : code, données, tiles
   et pile en SDRAM (0600 0000h), frame buffer en 2400 0000h.
2. Le programme (`bench.c`) compose les lignes d'un CPU pour 180 images
   (`frames.h` : la trajectoire de la démo, et des défilements
   quelconques). Il tourne sous `qemu-sh4eb`, avec le greffon
   `sh2cycles_plugin.c` qui mesure chaque image, d'après un modèle du
   SH2 :
   - temps de chaque instruction (SH7604 Hardware Manual) : 1 cycle, sauf
     branchements (BT/BF pris 3, BT/S/BF/S pris 2, BRA, JSR, RTS... 2),
     multiplications ;
   - +1 cycle quand une instruction lit le registre que la précédente
     vient de charger ;
   - cache de 4 Ko (4 voies, lignes de 16 octets, LRU, commun aux
     instructions et aux données, écriture simultanée) : +12 cycles par
     défaut (une ligne depuis la SDRAM), +1 par écriture en SDRAM ;
   - les écritures dans le frame buffer sont comptées à part (le
     simulateur leur compte 3 à 5 cycles).
   Un programme par CPU : chacun a son propre cache.
3. Sur l'hôte, `costs.c` calcule les compteurs du modèle pour les mêmes
   images. `fit.py` vérifie que le code SH2 dessine exactement les mêmes
   pixels (empreintes) et écrit 17 920 mots par image, puis ajuste les
   coûts unitaires par moindres carrés.

Non modélisé : le conflit entre lecture d'instruction et accès aux
données dans le même cycle, le partage de la SDRAM avec l'autre SH2,
l'attente quand le FIFO du frame buffer est plein. Les temps donnés sont
ceux d'un modèle, plus fin que `cost.h`, mais pas d'une 32X réelle.

## Lancer

Sous Linux (qemu en mode utilisateur n'existe pas sous macOS : VM,
Docker, ou une machine Linux), depuis la racine du dépôt :

```sh
tools/sh2cycles/run.sh
```

Variables : `SH_ELF` (préfixe de la chaîne, par défaut
`/opt/sh-elf/bin/sh-elf-`), `QEMU` (par défaut `/opt/qemu/bin/qemu-sh4eb`),
`QEMU_INCLUDE` (dossier de `qemu-plugin.h`, par défaut
`/opt/qemu/include`).

## Construire la chaîne sh-elf et qemu

Ubuntu fournit `gcc-sh4-linux-gnu`, mais il ne sait produire que du SH4,
et son qemu n'a pas les greffons. Il faut donc les construire (environ
40 minutes sur 2 cœurs) ; `build-tools.sh` le fait :

```sh
sudo apt install build-essential libgmp-dev libmpfr-dev libmpc-dev \
     ninja-build libglib2.0-dev pkg-config python3-venv flex bison texinfo
sudo tools/sh2cycles/build-tools.sh     # installe dans /opt/sh-elf et /opt/qemu
```

La même chaîne `sh-elf` servira à compiler SuperVDP pour la vraie 32X.

## Profil : où passe le temps

```sh
/opt/qemu/bin/qemu-sh4eb -plugin build/sh2cycles/libsh2cycles.so,profile=on \
    build/sh2cycles/bench_master.elf >/dev/null 2>profil.txt
```

Les lignes `profil;adresse;opcode;exécutions` donnent le nombre
d'exécutions de chaque instruction pendant les mesures ; avec
`sh-elf-objdump -d build/sh2cycles/bench_master.elf`, on retrouve les
boucles chères.
