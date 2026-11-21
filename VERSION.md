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
