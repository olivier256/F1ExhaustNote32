# SuperVDP

Moteur de tiles pour la Sega 32X : deux plans de tiles 16 × 16
superposés, qui défilent indépendamment. Le plan A est devant (index 0
transparent), le plan B derrière. La 32X se conduit comme un VDP pour le
68000 : le 68000 écrit les défilements dans les registres COMM, les deux
SH2 dessinent.

Le développement se fait sur Mac avec **pixel**, un simulateur de 32X
sans 68000 (C, MiniFB, deux threads Master et Slave, VBlank à 60 Hz,
API qui imite les registres : `vdp_dram`, `vdp_palette`, FBCR, `comm[]`).
pixel vient du projet AMB96.

## Compiler et lancer

```sh
git submodule update --init   # MiniFB, pour la fenêtre (voir ci-dessous)
make run                      # compile et lance la démo
make stats                    # la même, avec VDP_STATS=1
make test                     # tous les tests (environ 25 s)
```

À la création du dépôt, MiniFB s'ajoute une fois pour toutes par
`git submodule add https://github.com/emoon/minifb.git minifb`.
Sans MiniFB, seules les cibles sans fenêtre sont construites
(`supervdp_headless`, `test_compose`). Sous Linux, MiniFB demande les
en-têtes X11 ; `-DMINIFB_USE_OPENGL_API=OFF` évite OpenGL.

Dans VS Code : ouvrir `SuperVDP.code-workspace` ; tâches « Compiler »,
« Lancer la démo », « Lancer la démo avec VDP_STATS », « Tests ».

## La démo

Décor de Chun-Li (Street Fighter Alpha) : la Muraille et le sol (plan A,
768 × 224, ciel transparent) devant les montagnes (plan B, 672 × 224).

| Manette   | Clavier | Effet                                 |
|-----------|---------|---------------------------------------|
| Gauche / Droite | flèches | déplace la vue                  |
| C maintenu | L      | 4 pixels par image au lieu de 1       |

Parallaxe : les deux plans arrivent ensemble au bout. xA va de 0 à
448 (768 − 320), xB de 0 à 352 (672 − 320) : xB = xA × 11 / 14, arrondi.
C'est le programme 68000 (`demo/md68k.c`, en C dans le simulateur) qui
le calcule et écrit les registres.

## Registres COMM

| COMM | Sens            | Rôle                                   |
|------|-----------------|----------------------------------------|
| 1    | Master → Slave  | numéro de l'image à dessiner           |
| 2    | Slave → Master  | dernière image terminée                |
| 4    | 68000 → SH2     | défilement horizontal du plan A        |
| 5    | 68000 → SH2     | défilement horizontal du plan B        |
| 6    | 68000 → SH2     | défilement vertical du plan A          |
| 7    | 68000 → SH2     | défilement vertical du plan B          |

Le Master relève les registres 4 à 7 une fois par image (deux lectures
identiques de suite : le manuel prévient qu'un registre lu pendant que
l'autre côté l'écrit donne une valeur indéfinie), puis publie la scène
pour le Slave. Les deux CPU dessinent donc avec les mêmes valeurs.

## Le moteur

`supervdp/compose.c` compose chaque ligne **par segments**, sans tampon :
on parcourt la ligne du plan A tile par tile ; bmp2c a donné la classe
de chaque ligne de tile (transparente, opaque, mixte). Transparente : on
copie le plan B. Opaque : on copie le plan A, et B n'est pas lu. Mixte :
on fusionne pixel par pixel. Sur la démo, 95 % des lignes de tile de A
sont entièrement transparentes ou entièrement opaques. bmp2c prépare
aussi, pour chaque ligne de pixels d'un plan, les colonnes de tile où la
classe change (un bit par colonne) : les segments d'une ligne se forment
en ne regardant que ces changements.

Règles d'écriture, reprises d'AMB96 :
- uniquement des écritures 16 bits, à des adresses paires : chaque mot
  de la ligne est écrit une seule fois, soit **35 840 à 36 064
  écritures par image** (160 mots par ligne, 161 avec décalage d'écran),
  quels que soient les défilements (le surdessin, B puis A par la zone
  overwrite, en aurait coûté 53 000 à 58 000) ;
- source impaire : lecture par mots alignés, chaque mot écrit réunit
  deux octets de deux mots lus (`MOV.L` et décalages pour une tile
  entière) ; la décision de parité est prise une fois par ligne et par
  plan ;
- tiles 16 × 16 alignées sur 16 octets : une ligne de tile = une ligne
  du cache SH2 ;
- Master et Slave composent une ligne sur deux.

**Décalage d'écran (SFT) ligne par ligne.** Une source impaire coûte
cher (pixels réassemblés). Le VDP peut décaler l'affichage d'un point
(registre SFT, 2000 4102h), mais pour toute la ligne : les deux plans
changent de parité ensemble. `compose_line` choisit donc, pour chaque
ligne, de la dessiner décalée ou non, de façon à rendre pair le plan qui
fournit le plus de mots (en haut de l'écran, B ; sur le sol, A). Une
ligne décalée fait 161 mots (le frame buffer prévoit 161 mots par
ligne). Chaque CPU range le décalage de ses lignes dans une table, une
par frame buffer ; pendant l'affichage, le **Master** écrit SFT ligne par
ligne : sa routine V_INT pour la ligne 0, sa routine H_INT (au HBlank de
chaque ligne) pour la ligne suivante. C'est le Master parce qu'il a la
priorité sur le bus.

**Code et cache.** Le moteur suppose le cache du SH2 en mode « 2 voies
+ RAM » : le code chaud de `compose.c` (2 028 octets) dans les 2 Ko de
RAM interne, son code froid (le mot de frontière, fusionné pixel par
pixel) en SDRAM. Voir `tools/sh2cycles`.

## Mesurer : `VDP_STATS=1`

Toutes les 5 secondes dans la fenêtre, et à chaque capture sans fenêtre :

```
acces bus par image : master 17970 | slave 17970 | total 35940 = 46.8 % du plafond 76800
cycles bus VDP (partages) : 35940 ecritures x 5 = 179700 / 384000 (46.8 %)
cycles CPU estimes (modele du jeu + ecritures 3..5 c) : master 203414 + ecritures = 257174..293014 / 384000 ...
```

- **Écritures** : comptées exactement par le simulateur (toutes passent
  par `vdp_dram_write16`).
- **Cycles bus VDP** : d'après le *32X Hardware Manual*, le VDP vide
  environ un mot toutes les 5 cycles, pour les deux CPU ensemble ; une
  image dure environ 384 000 cycles SH2 (23,01 MHz), d'où le plafond de
  76 800 écritures.
- **Cycles CPU** : le simulateur exécute du code natif et ne mesure pas
  le temps SH2. Le moteur compte ce qu'il fait et le multiplie par des
  coûts unitaires (`supervdp/cost.h`) ; le simulateur ajoute 3 à 5 cycles
  par écriture du CPU. Les coûts unitaires sont **étalonnés** par le banc
  `tools/sh2cycles` : le vrai code compilé pour SH2, exécuté sous qemu
  avec un modèle de temps du SH2 (voir `tools/sh2cycles/README.md`).
  C'est un modèle, pas une 32X réelle, mais le nombre d'instructions est
  exact.
- **Verdict** : la dernière ligne donne la durée de l'image en images de
  1/60 s (le CPU le plus lent, ou le bus du VDP). Au-delà de 1, on ne
  tient pas 60 images/s.

## Vérifier

`tests/run_tests.sh` (ou `make test`) :
1. `test_compose` : `compose_line` contre un rendu naïf pixel par pixel,
   sur la trajectoire de la démo, toutes les colonnes de départ de A
   contre 40 valeurs de B, et 300 000 lignes au hasard ; 160 écritures
   16 bits par ligne, chaque mot écrit une fois.
2. Le simulateur complet sans fenêtre : la manette suit un scénario, et
   l'image capturée doit être identique, octet pour octet, à l'image de
   référence.

## Les images

BMP 8 bits indexés, palette commune, index 0 = transparent, convertis en
C par `tools/bmp2c` à chaque compilation (manifeste
`demo/assets/assets.txt`). Largeur et hauteur : des multiples de 16. Les
BMP de la démo sont produits depuis `demo/assets/source/chunli_stage.png`
par `tools/chunli2bmp.py` (Pillow) : plan A calé en bas (27 lignes
transparentes au-dessus), 41 couleurs, 599 tiles.

## Organisation

| Dossier / fichier         | Rôle                                                    |
|---------------------------|---------------------------------------------------------|
| `pixel/`                  | simulateur 32X : VDP, interface (COMM, V_INT), plates-formes fenêtre et sans fenêtre |
| `supervdp/supervdp.c`     | boucles Master et Slave, relevé des registres, échange de frame buffer |
| `supervdp/compose.c`      | composition d'une ligne par segments                    |
| `supervdp/cost.h`         | modèle de coûts (cycles estimés)                        |
| `supervdp/assets_types.h` | tiles, plans, classes de lignes de tile                 |
| `supervdp/shared.h`       | registres COMM, scène partagée, accès sans cache        |
| `demo/`                   | contenu de la démo et programme 68000                   |
| `tools/bmp2c.c`           | convertisseur BMP → C                                   |
| `tests/`                  | tests                                                   |
| `tools/sh2cycles/`        | banc SH2 sous qemu : temps du code compilé, étalonnage de `cost.h` |

## Vers une vraie 32X

- 599 tiles de 256 octets = 150 Ko, en SDRAM (256 Ko en tout) : depuis
  la ROM, un chargement de cache coûterait bien plus cher.
- `vdp_dram_write16()` devient une écriture directe en 2400 0000h +
  offset ; `SHARED()` donne déjà l'adresse cache-through sur SH2.
- `tools/sh2cycles/build-tools.sh` construit la chaîne `sh-elf` (gcc
  13.2) qui servira aussi pour la console ; le temps reste à recouper
  sur un émulateur 32X.

## Licence

SuperVDP est publié sous licence MIT. Copyright © 2026 Olivier BROSSERON.
Si vous utilisez SuperVDP, merci de citer le projet et son auteur.
