Première version : deux plans superposés, composés par segments

**Ce que fait cette version** :
- Nouveau dépôt, sur la base d'AMB96 :
  - `pixel/` : le simulateur (VDP, interface COMM/V_INT, plates-formes
    MiniFB et sans fenêtre), repris tel quel, sauf l'ajout des cycles
    estimés (ci-dessous) ;
  - `tools/bmp2c.c` : plans de dimensions quelconques (multiples de 16,
    plus de puissance de 2), et classe de chaque ligne de tile
    (transparente, opaque, mixte ; 2 bits par ligne, 32 bits par tile) ;
  - le reste d'AMB96 (sprites, assembleur, sh2test) n'est pas repris pour
    l'instant.
- Moteur `supervdp/` :
  - `compose_line` compose le plan A (index 0 transparent) sur le plan B
    **par segments, sans tampon** : copie de B sous les lignes de tile
    transparentes de A, copie de A sous les opaques (B n'est pas lu),
    fusion pixel par pixel sous les mixtes. Les tiles voisines de même
    classe forment un segment ; un mot à cheval sur deux segments (défilement de A impair)
    passe par la fusion ;
  - uniquement des écritures 16 bits à des adresses paires, chaque mot
    écrit une fois : **35 840 écritures par image**, quels que soient
    les défilements ; une décision de parité par ligne et par plan ;
  - Master et Slave composent une ligne sur deux ; le Master relève les
    registres COMM 4 à 7 (deux lectures identiques de suite) et publie la
    scène.
- Démo : décor de Chun-Li, plan A (768 × 224, calé en bas) devant plan B
  (672 × 224). Le programme 68000 (`demo/md68k.c`) lit la manette et
  écrit les défilements : xA de 0 à 448, xB = xA × 11 / 14 arrondi (de 0
  à 352).
- `VDP_STATS=1` : en plus des écritures, le pourcentage du plafond de
  76 800 écritures, les cycles du VDP pour les vider (5 par écriture,
  partagés), et les **cycles CPU estimés** par CPU : modèle de coûts du
  moteur (`supervdp/cost.h`, coûts provisoires) + 3 à 5 cycles par
  écriture. Nouvelle fonction du simulateur : `vdp_count_cpu_cycles()`.
- Vérifié :
  - `test_compose` : 1 168 576 lignes identiques au rendu de référence
    naïf (trajectoire de la démo, toutes les colonnes de A × 40 valeurs
    de B, 300 000 lignes au hasard, défilements négatifs ou au-delà du
    plan compris), 160 écritures 16 bits par ligne, chaque mot écrit une
    fois ;
  - simulateur complet sans fenêtre, 5 scénarios de manette : images
    identiques octet pour octet à la référence, 35 840 écritures par
    image (17 920 par CPU).

**Mesures (modèle provisoire)** :
- trajectoire de la démo, moyenne par image : 17 095 mots copiés source
  paire, 16 960 impaire, 1 783 fusionnés, 794 segments ;
- par CPU, hors écritures : environ 168 000 cycles (défilements pairs) à
  247 000 (A et B impairs), soit 58 à 88 % de l'image écritures
  comprises. Le chemin impair (deux `MOV.B`) est le premier poste.

**Ce qui reste pour plus tard** :
- étalonner les coûts unitaires sur le code SH2 compilé (banc qemu,
  émulateur 32X) ;
- copies en lectures `MOV.L` alignées et décalages en registres
  (4 variantes selon la source mod 4, l'idée des renderers de la
  première ébauche de SuperVDP), surtout pour le chemin impair ;
- frontières de segment : reconnaître les mots opaque + opaque et
  transparent + transparent sans passer par la fusion ;
- l'assembleur SH2 en option (même interface que le C, vérifié contre
  lui), avec le banc sh2test d'AMB96 ;
- sprites.

Message de commit proposé :

```
SuperVDP : deux plans superposés, composés par segments

Nouveau dépôt sur la base d'AMB96 : simulateur pixel, bmp2c (plans de
dimensions quelconques, classe des lignes de tile).

Le plan A (index 0 transparent) est composé sur le plan B ligne par
ligne, sans tampon : copie de B sous les lignes de tile transparentes,
copie de A sous les opaques, fusion sous les mixtes. Uniquement des
écritures 16 bits, chaque mot écrit une fois : 35 840 écritures par
image. Master et Slave composent une ligne sur deux.

Démo Chun-Li : le programme 68000 écrit les défilements dans COMM 4 à 7,
xB = xA x 11 / 14.

VDP_STATS=1 : plafond de 76 800 écritures, cycles du VDP, cycles CPU
estimés (modèle de coûts provisoire).

Tests : 1 168 576 lignes contre un rendu de référence, et 5 scénarios
du simulateur complet identiques octet pour octet.
```

---

Banc sh2cycles : temps SH2 mesuré, modèle de coûts étalonné

**Ce que fait cette version** :
- `tools/sh2cycles/` : banc d'essai qui mesure le temps du **vrai code**
  de `compose.c`, compilé pour SH2 (`sh-elf-gcc` 13.2, `-m2 -O2`, sans les
  compteurs) et lié aux adresses de la 32X (SDRAM en 0600 0000h, frame
  buffer en 2400 0000h) :
  - `sh2cycles_plugin.c` : greffon qemu qui applique un modèle du SH2 à
    chaque instruction exécutée (cycles par instruction, branchements pris
    ou non, dépendance après chargement, cache de 4 Ko 4 voies LRU commun
    aux instructions et aux données, défaut = 12 cycles, écriture en SDRAM
    = +1) ; les écritures dans le frame buffer sont comptées à part ;
    option `profile=on` : exécutions par adresse ;
  - `bench.c` compose les lignes d'un CPU pour 180 images (`frames.h` :
    trajectoire de la démo et défilements quelconques), un programme par
    CPU (chacun son cache) ; `costs.c` calcule sur l'hôte les compteurs du
    modèle pour les mêmes images ; `fit.py` vérifie les pixels (empreintes)
    et ajuste les coûts unitaires par moindres carrés ;
  - `build-tools.sh` construit binutils 2.42, gcc 13.2 pour `sh-elf`
    (SH2, gros-boutiste, libgcc) et qemu 8.2.2 avec les greffons. La même
    chaîne servira pour la console.
- `pixel/vdp.h` : sur SH2, `vdp_dram_write*()` et `vdp_overwrite_write*()`
  deviennent des écritures directes (2400 0000h, 2402 0000h + offset),
  comme sur la console.
- `supervdp/cost.h` : coûts unitaires étalonnés. Le coût d'un mot copié
  depuis une source paire est compris dans celui de la ligne (chaque ligne
  écrit 160 mots : on ne peut pas les séparer).
- `VDP_STATS=1` : nouvelle ligne, la durée de la dernière image en images
  de 1/60 s (CPU le plus lent ou bus du VDP), et le verdict 60 images/s.
- Vérifié : le code SH2 dessine les mêmes pixels que l'hôte (360 images,
  empreintes identiques) et écrit 17 920 mots par CPU et par image ; le
  modèle étalonné reproduit les temps du banc de -3 % à +6 % ;
  `test_compose` et les 5 scénarios du simulateur passent toujours.

**Mesures** (par CPU et par image, hors écritures FB) :
- 542 640 à 950 826 cycles, 717 077 en moyenne ; 508 859 instructions en
  moyenne : **le nombre d'instructions dépasse à lui seul le budget**
  (384 000 cycles) ;
- avec les écritures (3 cycles) : pire image 1 004 586 cycles, soit
  2,6 images de 1/60 s. La version actuelle tiendrait environ **30
  images/s** en moyenne, 20 dans le pire cas. L'ancien modèle provisoire
  sous-estimait d'un facteur 3,5 ;
- répartition des instructions : copie source impaire 33 %, formation
  des segments (`compose_line`) 30 %, copie source paire 18 %, fusion 9 %,
  divisions (`%`, ___sdivsi3) 7 %, décalages variables (___lshrsi3) 4 % ;
- le coût n'est pas dans les boucles de copie (4 instructions par mot en
  source paire, 11 en impaire), mais autour : environ 60 instructions par
  tile de A examinée et par morceau de tile copié. Le SH2 n'a ni décaleur
  à distance variable (appel de libgcc pour `>> (row * 2)`, 4 SHAR pour
  `x >> 4` signé), ni division (`%` à chaque ligne) ;
- SDRAM occupée par le banc (code, tiles, pile) : 169 556 octets sur
  262 144 (65 %).

**Ce qui reste pour plus tard** :
- réécrire le C pour le SH2, en mesurant chaque étape avec le banc :
  classes des lignes de tile rangées par ligne (un octet, sans décalage
  variable) ; parcours des tiles par pointeurs, décalages non signés,
  plus de `%` ni de boucle `advance()` par morceau ; chemin impair sans
  `EXTU.B` ;
- ensuite seulement, l'assembleur en option ;
- recouper le modèle de temps sur un émulateur 32X.

Message de commit proposé :

```
Banc sh2cycles : temps SH2 mesuré, modèle de coûts étalonné

tools/sh2cycles : compose.c compilé pour SH2 (sh-elf-gcc 13.2 -m2 -O2),
exécuté sous qemu avec un greffon qui applique un modèle de temps du SH2
(cycles par instruction, branchements, dépendances, cache de 4 Ko,
SDRAM). Les pixels sont vérifiés contre l'hôte, et les coûts unitaires
de supervdp/cost.h ajustés par moindres carrés (-3 % à +6 %).
build-tools.sh construit la chaîne sh-elf et qemu avec les greffons.

Sur SH2, vdp_dram_write*() écrit directement dans le frame buffer.
VDP_STATS=1 donne la durée de l'image en images de 1/60 s.

Mesure : 717 000 cycles par CPU et par image en moyenne, 1 000 000 au
pire écritures comprises : environ 30 images/s, pas 60. Le temps passe
autour des boucles de copie (segments, morceaux de tile, décalages
variables et divisions, absents du SH2).
```

---

Banc sh2cycles : sur Mac par Docker, outils dans ~/opt

**Ce que fait cette version** :
- `build-tools.sh` installe dans `$PREFIX` (par défaut `~/opt`) : plus
  besoin de `sudo`, ni de `/opt`. Il refuse de tourner hors de Linux avec
  un message clair (qemu en mode utilisateur n'existe pas sous macOS),
  ne retélécharge pas les sources déjà présentes, et configure qemu avec
  le Python du système (correction : avec un autre Python, qemu ne
  trouvait pas distlib).
- `run.sh` cherche les outils dans `$PREFIX` (même défaut) et dit quoi
  faire s'ils manquent ; il refuse aussi de tourner hors de Linux.
- `Dockerfile` et `docker.sh` : le banc sur Mac, dans un conteneur Ubuntu
  (Docker Desktop, OrbStack ou colima). L'image contient la chaîne
  `sh-elf` et qemu (environ 40 minutes à construire, une seule fois) ;
  le dépôt est monté dans le conteneur, les résultats arrivent dans
  `build/sh2cycles`.

**Ce qui reste pour plus tard** :
- inchangé : réécrire le C pour le SH2 en mesurant chaque étape.

Message de commit proposé :

```
Banc sh2cycles : sur Mac par Docker, outils dans ~/opt

build-tools.sh installe dans $PREFIX (~/opt par défaut, sans sudo),
refuse de tourner hors de Linux et configure qemu avec le Python du
système. run.sh cherche les outils au même endroit.

Dockerfile et docker.sh : le banc dans un conteneur Ubuntu, depuis un
Mac, où qemu en mode utilisateur n'existe pas.
```

---

compose.c réécrit pour le SH2, code de rendu en RAM interne

**Ce que fait cette version** :
- `supervdp/compose.c` réécrit pour le SH2, même principe (segments,
  une décision de parité par ligne et par plan, écritures 16 bits
  uniquement), chaque étape mesurée avec le banc :
  - plus de division ni de décalage à distance variable (le SH2 n'en a
    pas : appels à libgcc) : positions ramenées dans le plan par
    soustraction, classe des lignes de tile lue avec des masques préparés
    une fois par ligne, positions non signées ;
  - tiles parcourues par un pointeur sur leur numéro, qui revient au
    début de la rangée au bout du plan ;
  - tiles entières copiées sans boucle (8 mots) ; en source impaire, lues
    en quatre `MOV.L` ;
  - destination : un pointeur sur SH2 (écritures `MOV.W R0,@(disp,Rn)`),
    un offset sur l'hôte (chaque écriture passe toujours par
    `vdp_dram_write16`, qui la compte) ;
  - fusion par morceaux sans changement de tile, deux pixels à la fois ;
    mot de frontière entre segments lu directement (`mergeWord`).
- Cache en mode « 2 voies + RAM » (32X Hardware Manual, « Cache ») : 2 Ko
  de cache à 2 voies, et le code de `compose.c` (1 852 octets) dans les
  2 Ko de RAM interne du SH2, en C000 0000h. Le code de rendu ne subit
  plus de défaut de cache (le profil montrait les lignes de tile lues en
  continu chassant le code et la pile).
- Banc `tools/sh2cycles` :
  - greffon : options `ways=2` (cache 2 voies + RAM interne) et
    `ram=adresse` ; défauts de cache dus aux instructions comptés à part ;
    avec `profile=on`, défauts par ligne de 16 octets ;
  - `bench_ram.ld` place le code de `compose.c` dans la RAM interne ;
    `run.sh` mesure ce mode par défaut (`CACHE=4` : l'ancien) et donne la
    place occupée en RAM interne (2 Ko au plus, vérifié à l'édition de
    liens) ;
  - `fit.py` : coûts unitaires positifs ou nuls (lignes et tiles
    examinées varient presque ensemble).
- `supervdp/cost.h` réétalonné (-4,5 % / +5,3 %) ; `assets_types.h` :
  `tile_row()`, utilisée par la référence de `test_compose`.
- `tests/run_tests.sh` affiche aussi le verdict 60 images/s de chaque
  scénario.
- Vérifié : `test_compose` (1 168 576 lignes) et les 5 scénarios du
  simulateur ; sur SH2, pixels identiques à l'hôte et 17 920 écritures par
  CPU et par image.

**Mesures** (par CPU et par image, hors écritures FB ; pire image avec
écritures à 3 cycles) :

| Étape                                         | Moyenne | Pire    |
|-----------------------------------------------|---------|---------|
| Départ (version précédente, cache 4 voies)    | 717 077 | 950 826 |
| C sans division ni décalage variable          | 481 087 | 682 590 |
| Destination pointeur, fusion par morceaux     | 440 136 | 635 046 |
| Cache 2 voies + code en RAM interne           | 378 183 | 522 105 |
| Fusion deux pixels à la fois, mot de frontière| 358 369 | 487 021 |
| Tile entière impaire en `MOV.L`               | 336 469 | 441 039 |

- pire image écritures comprises : 494 799 cycles, soit 1,29 image de
  1/60 s (2,6 avant) ;
- sur la trajectoire de la démo, selon la parité de xA et xB :
  pair-pair 278 000 en moyenne, 290 000 au pire (**60 images/s**, 0,8
  image de 1/60 s avec les écritures) ; pair-impair 333 000 / 358 000 ;
  impair-pair 365 000 / 380 000 ; impair-impair 418 000 / 436 000 ;
- essayé et abandonné : ranger les tiles par ligne (la ligne 0 de toutes
  les tiles, puis la ligne 1...) pour répartir les lectures sur le cache.
  Plus lent dans les deux modes du cache (474 000 au lieu de 440 000 en
  4 voies, 384 000 au lieu de 378 000 en 2 voies + RAM).

**Ce qui reste pour plus tard** :
- registre de décalage d'écran (SFT, 2000 4102h : un point vers la
  gauche, prévu par la doc pour les défilements impairs en Packed
  Pixel) : xA toujours traité comme pair. Il faut 161 mots par ligne et
  la prise en charge de SFT dans le simulateur. Pire cas attendu :
  environ 358 000 cycles (B impair) ;
- il resterait environ 30 000 cycles à gagner pour tenir 60 images/s
  quand B est impair : segments et fusion (44 % des instructions),
  chemin impair (34 %), notamment les débuts et fins de segment ;
- écritures 32 bits dans le frame buffer : moitié moins d'instructions
  d'écriture, mais la doc ne cite que 8 et 16 bits ; à vérifier sur un
  émulateur 32X avant d'y toucher ;
- sur la console, le programme de démarrage SH2 (pas encore écrit) devra
  mettre le cache en mode 2 voies (CCR) et recopier le code de
  `compose.c` en C000 0000h.

Message de commit proposé :

```
compose.c réécrit pour le SH2, code de rendu en RAM interne

Même principe (segments, parité décidée par ligne et par plan,
écritures 16 bits), mais écrit pour le SH2 et mesuré pas à pas avec
tools/sh2cycles : plus de division ni de décalage variable, tiles
parcourues par pointeur et copiées sans boucle, tile impaire lue en
MOV.L, destination en pointeur sur SH2, fusion par morceaux.

Cache en mode 2 voies + RAM : le code de compose.c (1 852 octets) dans
la RAM interne du SH2. Banc : options ways=2 et ram=, bench_ram.ld,
défauts de cache par ligne, ajustement à coûts positifs. cost.h
réétalonné.

Par CPU et par image : 336 000 cycles en moyenne au lieu de 717 000,
441 000 au pire au lieu de 951 000. Défilements pairs : 60 images/s.
```

---

Décalage d'écran (SFT) choisi ligne par ligne, écrit par le Master en H_INT

**Ce que fait cette version** :
- Simulateur `pixel/` :
  - registre de décalage d'écran (`screenShiftRegister`, 2000 4102h, bit
    SFT) : lu par le VDP à chaque ligne affichée ; avec SFT, le pixel x
    de l'écran est l'octet x + 1 de la ligne ;
  - interruption H : masque (bit H), bit HEN (H_INT aussi pendant le
    VBlank), registre de compteur H (une H_INT toutes les n + 1 lignes),
    acquittement (`interface_clear_hint`), routines `master_hint` et
    `slave_hint` (V_INT passe avant H_INT, comme sur le SH2). La H_INT
    d'une ligne est levée juste après son affichage ;
  - contrôle : une H_INT encore en attente au HBlank suivant est comptée
    « en retard » (sa valeur arrive trop tard pour la ligne voulue) ;
    compteurs affichés avec les statistiques ;
  - latence des interruptions : sur la console, un SH2 prend une
    interruption en quelques cycles ; l'hôte, lui, peut tarder à donner un
    processeur au thread du CPU. Après chaque H_INT, la synchro vidéo
    attend donc que la routine ait acquitté (50 µs au plus), et
    `platform_running()`, appelée dans les boucles d'attente du jeu, cède
    le processeur. Sans cela, sur une machine à 2 cœurs, la moitié des
    H_INT étaient servies avec plus d'une ligne de retard.
- Moteur `supervdp/` :
  - `compose_line` forme les segments sur 322 pixels (de la colonne
    scrollAX - 1 à scrollAX + 320), compte les mots venant de A et de B,
    et choisit le décalage de la ligne qui rend pair le plan le plus
    coûteux en source impaire (un A impair coûte aussi ses mots de
    frontière). Elle renvoie ce décalage : ligne de 160 mots sans, 161
    avec ;
  - frame buffer : 161 mots par ligne (72 640 octets) ;
  - chaque CPU range le décalage de ses lignes dans une table par frame
    buffer ; la routine V_INT du Master prend la table du frame buffer
    affiché (d'après FS) et écrit SFT pour la ligne 0 ; sa routine H_INT
    l'écrit pour la ligne suivante. Le Master, parce qu'il a la priorité
    sur le bus ;
  - code froid : la copie depuis une source impaire, devenue rare, est
    rangée à part (section `.text.sdram`) et reste en SDRAM ; le code
    chaud (1 844 octets) tient dans la RAM interne ;
  - `cost.h` réétalonné (-7 % / +4 %) ; interruptions du Master comptées
    au modèle à 40 cycles chacune (estimation, non mesurée).
- Banc `tools/sh2cycles` : options `ccr_od=1` et `ccr_id=1` (bits OD et
  ID du CCR) ; 160 ou 161 écritures par ligne ; empreinte de ce que
  l'écran affiche.
- `tests/test_compose.c` vérifie ce que l'écran affiche, compte tenu du
  décalage renvoyé, et 160 ou 161 mots écrits une fois chacun.
- `supervdp.c` : le schéma de déroulement, aplati par clang-format,
  redessiné en moins de 80 colonnes.
- Vérifié : `test_compose`, 1 168 576 lignes dont 609 666 décalées, sans
  erreur ; les 5 scénarios du simulateur complet identiques à la
  référence ; sur SH2, pixels identiques à l'hôte.

**Mesures** (banc, par CPU et par image, hors écritures FB) :
- 309 819 cycles en moyenne (336 469 avant), 355 898 au pire (441 039) ;
  pire image avec ses écritures à 3 cycles : 409 850, soit 1,07 image de
  1/60 s (1,29 avant) ;
- trajectoire de la démo, mots copiés depuis une source impaire : 1 194
  par image au lieu de 16 960 ;
- selon la parité de xA et xB (moyenne / pire) :

| Parité xA, xB  | Version précédente  | Cette version       |
|----------------|---------------------|---------------------|
| pair, pair     | 278 000 / 290 000   | 292 000 / 305 000   |
| pair, impair   | 333 000 / 358 000   | 332 000 / 353 000   |
| impair, pair   | 365 000 / 380 000   | 343 000 / 356 000   |
| impair, impair | 418 000 / 436 000   | 299 000 / 311 000   |

  Le cas pair-pair coûte un peu plus (segments sur 322 pixels, choix par
  ligne, code froid) ; impair-impair rejoint pair-pair ; quand les
  parités diffèrent, un plan reste impair sur chaque ligne ;
- scénarios du simulateur (modèle étalonné) : 0,87 à 0,94 image de
  1/60 s, sauf parités différentes (« milieu ») : 1,05 ;
- options du cache, mesurées : OD = 1 deux fois plus lent (787 577 cycles
  en moyenne en 4 voies) ; ID = 1 en 2 voies + RAM : +22 000 cycles (le
  code resté en SDRAM n'est plus mis en cache) ; non retenues.

**Ce qui reste pour plus tard** :
- parités différentes : environ 26 000 cycles à gagner au pire. Pistes :
  segments impairs courts (environ 22 instructions par mot), mise en
  place de chaque morceau de fusion, part fixe de chaque ligne (environ
  140 instructions) ;
- le coût des interruptions du Master (estimé à 40 cycles) reste à
  mesurer ;
- sur la console, la routine H_INT dispose du seul HBlank pour écrire
  SFT (dans le simulateur, d'une ligne entière) : à vérifier sur un
  émulateur 32X ;
- sur une machine à 2 cœurs, le simulateur sert encore 1 à 3 % des H_INT
  en retard (hors des images capturées par les tests) ;
- le programme de démarrage SH2 devra mettre le cache en mode 2 voies,
  recopier le code chaud en C000 0000h, et démasquer V_INT et H_INT sur
  le Master.

Message de commit proposé :

```
Décalage d'écran (SFT) choisi ligne par ligne, écrit par le Master en H_INT

compose_line forme ses segments sur 322 pixels et choisit pour chaque
ligne le décalage d'écran qui rend pair le plan le plus coûteux ; une
ligne décalée fait 161 mots. Chaque CPU range ses décalages dans la table
du frame buffer qu'il dessine ; le Master, prioritaire sur le bus, écrit
SFT en V_INT (ligne 0) et en H_INT (ligne suivante).

Simulateur : registre SFT lu à chaque ligne, interruption H (masque, HEN,
compteur H, acquittement), H_INT en retard comptées, latence des
interruptions imitée (attente bornée, attentes du jeu qui cèdent le
processeur).

Copie depuis une source impaire en code froid (SDRAM), code chaud en RAM
interne (1 844 octets). Banc : options ccr_od et ccr_id. cost.h
réétalonné.

Par CPU et par image : 310 000 cycles en moyenne, 356 000 au pire (441 000
avant) ; impair-impair rejoint pair-pair. Mots en source impaire sur la
démo : 1 194 par image au lieu de 16 960.
```

---

Segments formés depuis les changements de classe, source impaire par mots alignés

**Ce que fait cette version** :
- Profil d'une image aux parités différentes (xA = 100, xB = 79), avant
  cette version : `compose_line` seule prenait 34 % des instructions,
  dont 27 000 par image pour la boucle qui forme les segments (12
  instructions par tile de A, 20 tiles par ligne), contre 7 % pour la
  copie depuis une source impaire. On a donc commencé par la formation
  des segments.
- `tools/bmp2c` : pour chaque plan, une table `classChanges` (champ
  ajouté à `Tilemap`, `assets_types.h`) : pour chaque ligne de pixels, un
  bit par colonne de tile, mis si la classe de la ligne y change. La
  rangée est suivie de 22 colonnes répétées (une ligne d'écran se lit
  d'un seul tenant) et d'un octet de garde : 10 octets par ligne pour A,
  9 pour B, 4,2 Ko en tout. Même préparation pour tous les plans.
- `compose_line` lit les bits des colonnes de la ligne en une fois
  (quatre octets, puis un décalage de 0 à 7 bits composé de décalages
  fixes), et ne s'arrête que sur les changements : 4 instructions par
  tile sans changement, et la boucle finit après le dernier. La classe
  du nouveau segment n'est lue qu'au changement.
- Parité d'un défilement prise directement sur scrollAX et scrollBX
  (les largeurs des plans sont paires) : deux réductions dans le plan en
  moins, et le code chaud retombe sous 2 Ko (2 028 octets).
- `copySpanOdd` (source impaire) : lecture par mots alignés ; chaque mot
  écrit réunit l'octet de poids faible d'un mot lu et l'octet de poids
  fort du suivant, le passage à la tile suivante n'est qu'un changement
  de pointeur ; tiles entières toujours par quatre MOV.L. 17 000
  instructions par image au lieu de 24 000 sur l'image profilée ; elle
  revient en RAM interne, seul le mot de frontière (`mergeWord`) reste en
  SDRAM.
- `cost.h` réétalonné (-6 % / +4 %).
- Vérifié : `test_compose`, 1 168 576 lignes dont 609 666 décalées, sans
  erreur ; les 5 scénarios du simulateur complet identiques à la
  référence ; sur SH2, pixels identiques à l'hôte.

**Mesures** (banc, par CPU et par image, hors écritures FB) :
- 276 852 cycles en moyenne (309 819 avant), 315 938 au pire (355 898) ;
  instructions : 184 439 en moyenne ;
- pire image avec ses écritures : 369 890 cycles à 3 cycles par
  écriture (96 % d'une image de 1/60 s), 405 858 à 5 cycles (106 %) ;
- selon la parité de xA et xB (moyenne / pire) :

| Parité xA, xB  | Version précédente  | Cette version       |
|----------------|---------------------|---------------------|
| pair, pair     | 292 000 / 305 000   | 265 000 / 282 000   |
| pair, impair   | 332 000 / 353 000   | 285 000 / 315 000   |
| impair, pair   | 343 000 / 356 000   | 293 000 / 316 000   |
| impair, impair | 299 000 / 311 000   | 269 000 / 287 000   |

- scénarios du simulateur (modèle étalonné, écritures à 3 cycles) : 0,80
  à 0,95 image de 1/60 s, « milieu » compris (1,05 avant).

**Ce qui reste pour plus tard** :
- écritures à 5 cycles (FIFO pleine) : le pire cas dépasse encore
  l'image de 6 %. Pistes : la fusion (24 % des instructions sur l'image
  profilée, environ 25 instructions par mot), la part fixe de chaque
  ligne (environ 230 instructions, hors formation des segments) ;
- segments impairs courts : la boucle fait 12 instructions par mot ; un
  passage en assembleur SH2 (MOV.L, deux mots par tour) n'en ferait
  gagner que 2 000 environ par image : non retenu pour l'instant ;
- le coût des interruptions du Master (estimé à 40 cycles) reste à
  mesurer ;
- sur la console, la routine H_INT dispose du seul HBlank pour écrire
  SFT : à vérifier sur un émulateur 32X ;
- le programme de démarrage SH2 devra mettre le cache en mode 2 voies,
  recopier le code chaud en C000 0000h, et démasquer V_INT et H_INT sur
  le Master.

Message de commit proposé :

```
Segments formés depuis les changements de classe, source impaire par mots alignés

bmp2c prépare pour chaque plan une table classChanges : pour chaque
ligne de pixels, un bit par colonne de tile où la classe de la ligne
change (4,2 Ko pour la démo). compose_line lit les colonnes de la ligne
en une fois et ne s'arrête que sur les changements, au lieu d'examiner
chaque tile de A. Parité des défilements prise sur les scrolls.

copySpanOdd lit le plan par mots alignés : chaque mot écrit réunit deux
octets de deux mots lus. Code chaud en RAM interne : 2 028 octets.
cost.h réétalonné.

Par CPU et par image, hors écritures FB : 277 000 cycles en moyenne,
316 000 au pire (310 000 et 356 000 avant).
```

---

Retrait de makePair, inutilisée depuis la lecture par mots alignés

**Ce que fait cette version** :
- `compose.c` : la fonction `makePair` (deux pixels lus par MOV.B
  assemblés en un mot) ne servait plus depuis que `copySpanOdd` lit le
  plan par mots alignés ; clang le signalait (`-Wunused-function`), gcc
  non. Retirée. Code produit inchangé.
- Vérifié : compilation sans avertissement avec clang et gcc
  (`-Wall -Wextra`) ; `test_compose` et les 5 scénarios sans erreur.

Message de commit proposé :

```
Retrait de makePair, inutilisée depuis la lecture par mots alignés

clang la signalait (-Wunused-function). Code produit inchangé.
```

---

Fusion : morceaux impairs corrigés, version en assembleur SH2 (option SUPERVDP_ASM)

**Ce que fait cette version** :
- `mergeSpan` (C) : après un morceau impair, tout le morceau suivant
  passait pixel par pixel (environ 22 instructions par pixel), et le
  profil en faisait plus de la moitié du temps de la fusion. Désormais,
  le premier pixel du morceau complète le mot en attente et le reste
  repart par mots entiers. Fusion : 37 700 instructions par image au lieu
  de 50 000 sur l'image profilée (xA = 100, xB = 79).
- `supervdp/merge_sh2.S` : la fusion en assembleur SH2, même interface
  et même résultat que `mergeSpan`, qui reste la référence et la version
  de l'hôte. Utilisée quand `compose.c` est compilé pour le SH2 avec
  `SUPERVDP_ASM=1` ; `compose.c` vérifie à la compilation les positions
  des champs de `PlaneRow` qu'elle lit. Ce qu'elle gagne sur gcc :
  - tout l'état dans les registres (gcc rangeait les curseurs dans la
    pile) : 48 instructions de mise en place par appel au lieu
    d'environ 70, 17 par morceau au lieu de 35 ;
  - mot fusionné en 15 instructions, plus une par pixel pris dans B : le
    pixel de droite est lu par MOV.B @(1,Rm),R0 et seul lui passe par
    EXTU.B ; les deux créneaux de retard des branchements sont remplis
    (proposition d'Olivier), sans lire un registre juste après son
    chargement.
  Fusion : 27 400 instructions par image au lieu de 37 700.
- `tools/sh2cycles` :
  - `test_merge.c` : vérifie sur SH2, sous qemu, l'assembleur contre le
    C : toutes les colonnes de A x 32 colonnes de B x longueurs de 2 à
    40 pixels, plus 100 000 cas au hasard ; 591 520 cas, gardes du
    tampon comprises, sans différence. Une faute volontaire dans
    l'assembleur donne 313 749 cas faux ;
  - `run.sh` : assembleur par défaut (`ASM=0` : tout en C), lance
    `test_merge` avant la mesure ;
  - `bench_ram.ld` : le code des fichiers `*_sh2.o` va aussi en RAM
    interne.
- `cost.h` réétalonné sur la version assembleur, celle de la console
  (-3 % / +4 %). Il ne sert qu'à l'estimation du simulateur.
- Code en RAM interne : 1 852 octets (2 028 tout en C).
- Vérifié : `test_compose`, 1 168 576 lignes sans erreur ; les 5
  scénarios du simulateur complet identiques à la référence ; sur SH2,
  pixels identiques à l'hôte, en C comme en assembleur.

**Mesures** (banc, par CPU et par image, hors écritures FB) :

| Version                   | Moyenne | Pire cas | Pire cas + écritures à 5 cycles |
|---------------------------|---------|----------|---------------------------------|
| Version précédente        | 276 852 | 315 938  | 405 858 (106 %)                 |
| C corrigé (`ASM=0`)       | 272 948 | 307 178  | 397 098 (103 %)                 |
| Assembleur (par défaut)   | 258 651 | 289 706  | 379 626 (99 %)                  |

- avec l'assembleur, le pire cas tient dans l'image même avec des
  écritures à 5 cycles (FIFO plein) ; à 3 cycles : 343 658, soit 89 % ;
- selon la parité de xA et xB (moyenne / pire, Master) :

| Parité xA, xB  | Version précédente  | Cette version       |
|----------------|---------------------|---------------------|
| pair, pair     | 265 000 / 282 000   | 251 000 / 265 000   |
| pair, impair   | 285 000 / 315 000   | 264 000 / 283 000   |
| impair, pair   | 293 000 / 316 000   | 272 000 / 290 000   |
| impair, impair | 269 000 / 287 000   | 255 000 / 269 000   |

- scénarios du simulateur (modèle étalonné, écritures à 3 cycles) : 0,77
  à 0,88 image de 1/60 s.

**Ce qui reste pour plus tard** :
- la part fixe de chaque ligne (environ 230 instructions, hors formation
  des segments) et `planeRowInit` (environ 40 instructions, deux fois par
  ligne) ;
- non modélisé par le banc : le conflit entre lecture d'instruction et
  accès aux données dans le même cycle ;
- le coût des interruptions du Master (estimé à 40 cycles) reste à
  mesurer ;
- sur la console, la routine H_INT dispose du seul HBlank pour écrire
  SFT : à vérifier sur un émulateur 32X ;
- le programme de démarrage SH2 devra mettre le cache en mode 2 voies,
  recopier le code chaud (compose.c et merge_sh2.S) en C000 0000h, et
  démasquer V_INT et H_INT sur le Master.

Message de commit proposé :

```
Fusion : morceaux impairs corrigés, version en assembleur SH2

mergeSpan ne repasse plus pixel par pixel tout un morceau après un
morceau impair : le premier pixel complète le mot en attente.

supervdp/merge_sh2.S : la fusion en assembleur SH2, même interface et
même résultat que mergeSpan (la référence), avec SUPERVDP_ASM=1. Tout
l'état en registres, mot fusionné en 15 instructions, créneaux de retard
remplis. tools/sh2cycles/test_merge.c la vérifie contre le C sur SH2
(591 520 cas) ; run.sh l'utilise par défaut (ASM=0 : tout en C).
cost.h réétalonné sur la version assembleur.

Par CPU et par image, hors écritures FB : 259 000 cycles en moyenne,
290 000 au pire (277 000 et 316 000 avant) ; le pire cas tient dans
l'image même avec des écritures à 5 cycles.
```

---

Part fixe de chaque ligne allégée ; statistiques VBlank corrigées

**Ce que fait cette version** :
- `compose_line` :
  - un segment ne garde que sa fin et ce qu'on y dessine (`kind` : copie
    de B, copie de A, fusion), décidé une fois quand il est formé : 4
    octets au lieu de 8, et plus de comparaison de classe au dessin. La
    table des segments est dans la pile, en SDRAM : moins d'écritures et
    moins de lignes de cache ;
  - la colonne de départ n'est calculée que pour le plan lu (une seule
    pour une copie, deux pour une fusion) ;
  - `PlaneRow` ne garde que ce que lisent la copie et la fusion (rangée,
    fin de rangée, ligne de tile, largeur) ; la ligne de tile et les
    changements de classe de A restent dans `compose_line` ;
  - `planeRowInit` recopiée dans `compose_line` avec la fusion en
    assembleur ; appelée tout en C, sinon le code chaud dépasserait
    2 Ko ;
  - le mot de frontière (code froid) calcule lui-même ses colonnes.
- Simulateur `pixel/` : le relevé des statistiques lisait le nombre de
  VBlank avant les VBlank vus par chaque CPU ; un VBlank commencé et vu
  entre les deux lectures donnait un nombre de « ratés » négatif
  (affiché 18446744073709551615). Relevé dans l'ordre inverse, avec une
  barrière (ARM).
- `cost.h` réétalonné (-3 % / +4 %).
- Vérifié : `test_compose`, 1 168 576 lignes sans erreur ; les 5
  scénarios du simulateur complet identiques à la référence ;
  `test_merge` sans différence ; sur SH2, pixels identiques à l'hôte, en
  C comme en assembleur.

**Mesures** (banc, par CPU et par image, hors écritures FB) :

| Version                   | Moyenne | Pire cas | Pire cas + écritures à 5 cycles |
|---------------------------|---------|----------|---------------------------------|
| Précédente, assembleur    | 258 651 | 289 706  | 379 626 (99 %)                  |
| Celle-ci, assembleur      | 247 820 | 276 774  | 366 694 (95 %)                  |
| Celle-ci, tout en C       | 261 543 | 292 469  | 382 389 (100 %)                 |

- code en RAM interne : 1 904 octets avec l'assembleur, 2 036 tout en C ;
- selon la parité de xA et xB (moyenne / pire, Master) : pair-pair
  239 000 / 252 000, pair-impair 253 000 / 271 000, impair-pair 260 000 /
  277 000, impair-impair 242 000 / 255 000 ;
- scénarios du simulateur (écritures à 3 cycles) : 0,75 à 0,85 image de
  1/60 s.

**Ce qui reste pour plus tard** :
- `compose_line` reste la deuxième dépense (32 % des instructions sur
  l'image profilée) : environ 200 instructions par ligne et 40 par
  segment. Une version en assembleur pourrait en gagner une quinzaine de
  milliers par image ; à discuter avant ;
- essai noté : la pile en RAM interne (accès sans attente) ne tient pas
  à côté du code (448 octets libres, débordement constaté) ;
- non modélisé par le banc : le conflit entre lecture d'instruction et
  accès aux données dans le même cycle ;
- le coût des interruptions du Master (estimé à 40 cycles) reste à
  mesurer ; la routine H_INT dans le seul HBlank, à vérifier sur un
  émulateur 32X ;
- le programme de démarrage SH2 (cache en 2 voies, code chaud en
  C000 0000h, V_INT et H_INT démasquées sur le Master).

Message de commit proposé :

```
Part fixe de chaque ligne allégée ; statistiques VBlank corrigées

compose_line : segments réduits à leur fin et à ce qu'on y dessine
(4 octets, décidé à la formation), colonne de départ calculée pour le
seul plan lu, PlaneRow réduite aux champs lus par la copie et la fusion,
planeRowInit recopiée avec SUPERVDP_ASM. cost.h réétalonné.

Simulateur : VBlank vus relevés avant le nombre de VBlank (le nombre de
ratés pouvait devenir négatif).

Par CPU et par image, hors écritures FB, avec l'assembleur : 248 000
cycles en moyenne, 277 000 au pire (259 000 et 290 000 avant).
```

---

F1 Exhaust Note 32, première version : la route de référence

**Ce que fait cette version** :
- Nouveau projet, sur la base de SuperVDP : moteur de course pseudo-3D
  (route ligne par ligne, virages, côtes, décor de fond qui défile).
  On garde le simulateur `pixel/`, la synchronisation Master / Slave, le
  décalage d'écran SFT ligne par ligne (Master, en V_INT et H_INT),
  `bmp2c` et le banc `tools/sh2cycles`. `supervdp/` (composition de deux
  plans de tiles, assembleur de fusion) et la démo de Chun-Li sont
  retirés ; `metriques.md` aussi (il commentait les chiffres de
  SuperVDP).
- Moteur `race/` :
  - `world.h` : le circuit (segments de 256 unités : hauteur, courbure,
    bandes claires ou sombres) et la caméra (640 unités au-dessus de la
    route, 720 derrière la voiture, focale 128, horizon sur la ligne 96) ;
  - `project.c` : projection à la Jake Gordon. Les 160 débuts de segment
    devant la caméra sont projetés une fois chacun (une division) ;
    chaque segment couvre les lignes entre ses deux bords, centre et
    demi-largeur interpolés ; une ligne déjà couverte par un segment plus
    proche le reste (crêtes). Chaque ligne d'écran est décrite une fois :
    route ou décor. Chaque CPU projette pour ses seules lignes, et obtient
    exactement les valeurs d'une projection de tout l'écran (additions
    exactes depuis la première ligne du segment) ;
  - `render.c` : chaque mot écrit une fois, en 16 bits. Route : neuf
    plages de couleur unie (herbe, bordure, bitume, deux lignes,
    bordure, herbe). Décor : une ligne de l'image de fond (512 pixels de
    large, qui boucle) copiée par mots ; colonne impaire : copie depuis
    la colonne paire précédente et ligne affichée décalée (SFT), 161
    mots. Sous le décor, sa couleur de sol. **35 840 à 36 064 écritures
    par image**, comme SuperVDP ;
  - `fixed.h` : toutes les divisions passent par `div_u32` (libgcc pour
    l'instant, DIVU plus tard si besoin) ; produits 32 × 32 -> 64 bits.
- Registres COMM : 4 et 5, la distance de la caméra (32 bits) ; 6,
  l'écart latéral ; 7, le défilement du décor. Le Master les relève
  (deux lectures identiques) et publie la scène.
- Démo : `demo/track.txt`, 1 062 segments (droite, virages larges et
  serrés, S, crête et descente en courbe, montagnes russes, épingle),
  converti par le nouvel outil `tools/track2c` (méthode addRoad de Jake
  Gordon). Décor original `demo/assets/sky.bmp` (512 × 112), produit par
  `tools/sky.py`. Programme 68000 : accélération jusqu'à 192 unités par
  image, voiture qui se place à l'intérieur des virages à venir, décor
  qui tourne avec la route ; Gauche / Droite, B (frein), Start (pause).
- `bmp2c` : type `bitmap` (image brute, comme `sprite`) ; le type C
  `Sprite` devient `Bitmap`.
- Simulateur : la table de lignes est écrite par `vdp_dram_write16`
  (mots du SH2), et relue ainsi par `vdp_render_line` ; plus d'écriture
  directe par `vdp_dram`. Prépare une table de lignes réécrite à chaque
  image.
- `cost.h` étalonné par le banc. Chaque CPU dessine toujours 112 lignes
  et projette 160 points : ces coûts fixes sont réunis dans
  `CYCLES_FRAME` (l'ajustement ne peut pas les séparer).
- Vérifié :
  - `test_race` : 232 768 lignes (200 000 lignes de route au hasard, dont
    hors de l'écran, et toutes les colonnes de départ du décor) identiques
    au rendu naïf, 160 ou 161 écritures par ligne, chaque mot écrit une
    fois ; 5 016 images (plus d'un tour de la démo, et 3 000 vues au
    hasard, dont la fin du circuit) : projection par CPU identique à
    celle de tout l'écran, image identique au rendu naïf, route toujours
    présente en bas ;
  - simulateur complet : 3 captures (départ ; premier virage, décor à une
    colonne impaire, donc lignes décalées ; descente après la crête),
    identiques octet pour octet à la référence ;
  - banc SH2 : pixels identiques à l'hôte (empreintes), 17 920 + lignes
    décalées écritures par CPU.

**Mesures** (banc, par CPU et par image, hors écritures FB) :

| Cache                           | Moyenne | Pire cas | Pire + écritures à 3 c | à 5 c         |
|---------------------------------|---------|----------|------------------------|---------------|
| 2 voies + RAM (render.c en RAM) | 222 799 | 238 421  | 292 328 (76 %)         | 328 266 (85 %) |
| 4 voies, tout en SDRAM          | 218 495 | 230 110  | 283 870 (74 %)         | 319 713 (83 %) |

- Profil (instructions, 155 000 par image) : `render_line` 71 %, dont
  46 % pour les deux boucles des mots (4 instructions par mot, copie
  comme remplissage) ; projection 19 % ; division (`__udivsi3`) 7 %
  pour environ 200 divisions ; décalages arithmétiques de libgcc 1 %.
- Défauts de cache : environ 2 300 par image (27 000 cycles), surtout
  les lignes du décor (20 lignes de cache par ligne copiée).
- Le mode 2 voies + RAM n'apporte rien ici (+2 %) : le code chaud est
  petit, les données (décor, circuit) profitent des 4 Ko de cache.
- `render.c` fait 912 octets ; avec `project.c` (1 240), le moteur
  dépasse les 2 Ko de RAM interne.
- Simulateur (écritures à 3 cycles) : 0,72 à 0,75 image de 1/60 s. On
  tient 60 images/s, avec environ 15 % de marge au pire (écritures à
  5 cycles), avant les sprites.

**Ce qui reste pour plus tard** :
- les sprites mis à l'échelle (voiture, adversaires, décor du bord de la
  route) : c'est là que se jouera le budget ;
- le bus du VDP : 35 840 écritures, c'est 179 200 cycles de vidage
  partagés par les deux CPU. Ni le simulateur ni le banc ne modélisent
  le FIFO plein quand les deux CPU écrivent en même temps : le chiffre à
  5 cycles est le plus réaliste. Réduire les écritures (table de lignes)
  comptera plus que réduire les instructions ;
- pistes mesurables contre cette référence : boucles des mots déroulées,
  DIVU, décalages non signés dans la projection, table de lignes pour
  les lignes sans sprite.

Message de commit proposé :

```
F1 Exhaust Note 32 : moteur de course pseudo-3D, route de référence

Nouveau projet sur la base de SuperVDP. race/ remplace supervdp/ :
projection du circuit à la Jake Gordon (une description par ligne
d'écran, crêtes comprises), dessin de chaque ligne en plages de couleur
(route) ou copie d'un décor qui boucle (SFT pour les colonnes impaires),
35 840 écritures par image. Circuit converti par tools/track2c, décor
fabriqué par tools/sky.py, programme 68000 à trajectoire scriptée.

Simulateur : table de lignes écrite et relue en mots du SH2.
Tests : rendu naïf, projection par CPU, simulateur complet en pause.
Banc SH2 adapté ; cost.h étalonné : 223 000 cycles par CPU et par
image en moyenne, 238 000 au pire, hors écritures FB.
```

---

La route par la table de lignes : le VDP dessine

**Ce que fait cette version** :
- Les pixels ne sont plus dessinés à chaque image. Au démarrage, le
  Master écrit dans chacun des deux frame buffers toutes les lignes dont
  l'écran peut avoir besoin (`render_prepare`) :
  - 128 routes, de demi-largeur w_k = 1,2 × k pixels (sur une route
    plate, la demi-largeur grandit de 1,2 pixel par ligne d'écran),
    séparées par 320 pixels d'herbe : une fenêtre de 320 pixels autour
    d'une route ne voit jamais la suivante, quel que soit le décalage ;
  - 12 lignes de ciel unies (dégradé, une par bande de 8 lignes) et une
    ligne de sol, pour ce qu'on voit au-delà d'une crête ;
  - 67 890 octets en tout : 63 Ko restent libres par frame buffer.
- À chaque image, chaque CPU projette le circuit pour ses lignes (comme
  avant), puis écrit leurs entrées de la table de lignes (`render_line`) :
  route k la plus proche (k = arrondi de w / 1,2 pixel, par un produit),
  octet de départ = centre de la route k − centre à l'écran ; octet
  impair : l'octet d'avant, et la ligne décalée d'un pixel (SFT). Route
  entièrement hors de l'écran : de l'herbe seule. **224 écritures par
  image** au lieu de 35 840.
- Bandes claires et sombres par la palette : la route n'utilise que
  quatre index (herbe, bordure, bitume, ligne blanche). Le Master écrit
  au HBlank, depuis V_INT pour la ligne 0 et H_INT pour les suivantes,
  le bit SFT de la ligne suivante et, quand la bande change, ses quatre
  couleurs (16,5 changements par image en moyenne). Ses routines
  n'acquittent l'interruption qu'à la fin. Le Slave n'a pas
  d'interruption.
- Le décor de fond (`sky.bmp`, `tools/sky.py`) est retiré : il ne tient
  pas dans la table de lignes (512 + 320 pixels de large pour boucler,
  93 Ko). Le haut de l'écran viendra des plans de la Mega Drive ; COMM 7
  est libre, `bmp2c` reste construit pour plus tard.
- `cost.h` réétalonné ; le code de chaque image (`project.c`,
  `render_line`, 1 472 octets) va en RAM interne, la préparation des
  lignes (code froid, section `.text.sdram`) en SDRAM.
- Simulateur (`platform_headless.c`) : la synchro vidéo attend jusqu'à
  2 ms (au lieu de 50 µs) que la H_INT soit servie. Sur une machine
  virtuelle, un signal arrivait parfois trop tard, et les couleurs de
  toutes les lignes suivantes de l'image étaient décalées.
- `tests/run_tests.sh` relance une fois un scénario dont une capture
  diffère (H_INT servie en retard sur une machine chargée ; un vrai
  défaut se reproduit), et affiche les H_INT en retard.
- README : retour de l'explication de `VDP_STATS`, de la section « Vers
  une vraie 32X » et de la commande `git submodule add`.
- Vérifié :
  - `test_race` émule l'affichage du VDP (table de lignes, SFT, bande)
    sur un frame buffer préparé : 300 224 lignes identiques au rendu
    naïf (route de demi-largeur w_k la plus proche, centrée sur le pixel
    le plus proche), une écriture par ligne, fenêtre toujours dans les
    lignes préparées ; 5 016 images, projection par CPU identique à
    celle de tout l'écran, 224 écritures par image ;
  - simulateur complet : les 3 captures identiques à la référence ;
  - banc SH2 : affichage identique à l'hôte (empreintes), 112 écritures
    par CPU et par image.

**Mesures** (banc, par CPU et par image, hors écritures FB) :

| Version                               | Moyenne | Pire cas | Pire, écritures comprises |
|---------------------------------------|---------|----------|---------------------------|
| Précédente (pixels dessinés)          | 222 799 | 238 421  | 328 266 (85 %)            |
| Table de lignes, cache 4 voies        |  75 616 |  84 766  |  85 326 (22 %)            |
| Table de lignes, 2 voies + render.c en RAM | 101 014 | 117 948 | 118 508 (31 %)      |
| **Table de lignes, 2 voies + project.c et render_line en RAM** | **66 086** | **73 787** | **74 347 (19 %)** |

- Profil (49 000 instructions par image) : projection 60 %, division
  (`__udivsi3`, environ 200 par image) 23 %, choix des lignes 10 %.
- Bus du VDP : 224 écritures par image, 0,3 % du plafond (1 120 cycles
  de vidage, contre 179 200).
- Simulateur : 0,18 à 0,20 image de 1/60 s (interruptions du Master
  estimées).

**Ce qui reste pour plus tard** :
- **à vérifier sur un émulateur 32X, puis sur la console** : que la
  H_INT du Master tienne dans le HBlank avec SFT et quatre couleurs. Si
  ce n'est pas le cas, le principe ne tient pas ;
- la projection est faite en double (chaque CPU projette les 160
  points) : la confier à un seul CPU libérerait l'autre entièrement ;
  DIVU et des décalages non signés réduiraient la division ;
- le haut de l'écran et les sprites, par la Mega Drive.

Message de commit proposé :

```
Route par la table de lignes : le VDP dessine

128 routes de demi-largeur 1,2 x k pixels, séparées par 320 pixels
d'herbe, et les lignes du ciel sont préparées une fois dans chaque frame
buffer. Chaque image ne fait qu'écrire la table de lignes (route la plus
proche, décalage, SFT pour l'octet impair) : 224 écritures au lieu de
35 840. Bandes claires et sombres par quatre couleurs de la palette,
changées par le Master au HBlank avec SFT. Décor de fond retiré (le
haut de l'écran viendra de la Mega Drive).

Simulateur : attente de la H_INT jusqu'à 2 ms sans fenêtre.
Tests : affichage du VDP émulé contre le rendu naïf. cost.h réétalonné :
66 000 cycles par CPU et par image en moyenne, 74 000 au pire (19 %).
```

---

Couche Mega Drive, bit FM, DMA du VBlank et échange retardé

**Ce que fait cette version** :
- Simulateur `pixel/` :
  - **bit FM** (`interface.c`) : le VDP 32X (frame buffer, palette, SFT,
    FBCR) n'appartient qu'à un côté à la fois. Tout accès par un CPU qui
    ne l'a pas arrête le simulateur avec un message. Les CPU écrivent
    désormais la palette et SFT par `vdp_write_palette` et
    `vdp_write_sft`, qui vérifient FM ;
  - **VDP Mega Drive réduit aux sprites** (`mdvdp.c`) : VRAM, CRAM, table
    des sprites (80 entrées chaînées), affichage avec les limites du mode
    H40 (20 sprites, 320 pixels par ligne), superposition avec l'image
    32X selon le bit PRI (sens à confirmer) et le bit de transparence des
    couleurs 32X ;
  - **DMA depuis le frame buffer 32X**, chronométré : 205 octets par
    ligne de VBlank, 18 pendant l'affichage ; le 68000 attend la fin.
    Nouveau point d'entrée du programme 68000 : `md_vblank_line`, à
    chaque ligne du VBlank ;
  - statistiques : octets et lignes de DMA par VBlank, débordements sur
    l'affichage, échanges de frame buffer faits dans le VBlank de leur
    DMA, lignes de sprites en surnombre ;
  - corrections : un VBlank n'est plus compté deux fois comme « vu »
    quand une interruption lit le FBCR au milieu d'une lecture (le
    nombre de VBlank ratés devenait négatif) ; la fenêtre attend aussi
    jusqu'à 2 ms que la H_INT soit servie.
- Moteur :
  - **échange retardé** : le Master écrit `COMM_READY` une fois l'image
    finie, n'accède plus au VDP 32X, attend `COMM_DMA_DONE`, puis échange
    les frame buffers pendant le VBlank (échange immédiat) et prépare la
    ligne 0. La H_INT de la dernière ligne affichée prépare la ligne 0 de
    l'image en cours, pour le cas où aucune nouvelle image n'est prête.
    La V_INT du Master ne fait plus rien ;
  - zone de transfert vers la Mega Drive dans le frame buffer de dessin
    (`MD_STAGE_OFFSET`, `shared.h`) : nombre d'octets de tiles et
    d'entrées, tiles, entrées de la table des sprites ;
  - **compteur** (`hud.c`) : la distance parcourue, cinq chiffres en deux
    sprites Mega Drive (4 × 2 et 1 × 2 cellules), tiles 4 bits par pixel
    refaites par le Master à chaque image : 336 octets de DMA. Rangées
    de pixels par table, chiffres par soustractions : ni division ni
    décalage variable ;
  - sprites Mega Drive devant la route (bit PRI).
- Programme 68000 : entrées 0 à 9 de la table des sprites réservées
  (hors de l'écran, chaînées vers la dixième) ; au début du VBlank, si
  une image est prête, prise du VDP 32X, deux DMA (tiles, entrées), fin
  du DMA attendue ligne par ligne, VDP 32X rendu, `COMM_DMA_DONE`.
- `cost.h` réétalonné, compteur compris ; H_INT estimée à 100 cycles
  (d'après les attentes du manuel), changement de couleurs à 30.
- Vérifié :
  - `test_race` : comme avant, plus le compteur sur 20 000 valeurs
    (tiles et table des sprites relues comme le VDP Mega Drive, contre
    les chiffres dessinés directement ; taille du transfert, chaînage
    des entrées) ;
  - simulateur complet : les 3 captures identiques à la référence,
    compteur compris (le chemin SH2 → frame buffer → DMA → VRAM →
    sprites est vérifié au pixel près) ; aucune violation du bit FM ;
  - construit et lancé aussi avec la fenêtre (MiniFB sous X11).

**Mesures** :
- banc SH2, par CPU et par image, hors écritures FB : 69 292 cycles en
  moyenne, 77 224 au pire (20 % de l'image) ; le compteur coûte 1 941
  cycles au Master (24 603 dans une première version, qui calculait
  chaque pixel par une fonction ; réécrit par tables de rangées) ; code
  de chaque image en RAM interne : 1 972 octets sur 2 048 ;
- simulateur : Master 23 à 26 % (H_INT comprises), Slave 16 à 18 % ;
  DMA de 336 octets en 2 lignes de VBlank, sur environ 7 600 possibles.

**Ce qui reste pour plus tard** :
- les objets du circuit, mis à l'échelle par les SH2 en tiles Mega
  Drive (la moitié des objets par CPU), avec cache des tiles en VRAM et
  tailles fixes pour les objets lointains ;
- à vérifier : le DMA du VDP Mega Drive depuis $840000 et son débit réel,
  le sens du bit PRI, la H_INT dans le HBlank ;
- sur cette machine virtuelle, le Master voit parfois la fin du DMA après
  la fin du VBlank (1 % des échanges) : à surveiller sur Mac ;
- les plans de la Mega Drive (le ciel), et la RAM interne, presque
  pleine.

Message de commit proposé :

```
Couche Mega Drive, bit FM, DMA du VBlank et échange retardé

Simulateur : bit FM (accès au VDP 32X vérifiés), VDP Mega Drive réduit
aux sprites (VRAM, CRAM, table des sprites, limites H40, superposition
avec la 32X), DMA depuis le frame buffer 32X chronométré ligne par
ligne, md_vblank_line. Corrections : VBlank vus comptés une fois,
attente de la H_INT jusqu'à 2 ms dans la fenêtre.

Moteur : le Master confie l'image au 68000 (COMM_READY), qui prend le
VDP 32X au VBlank, transfère tiles et table des sprites, le rend
(COMM_DMA_DONE) ; l'échange de frame buffer suit, dans le même VBlank.
Compteur de distance en sprites Mega Drive, refait à chaque image.
cost.h réétalonné : 69 000 cycles par CPU et par image, 77 000 au pire.
```
