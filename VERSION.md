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
