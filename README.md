# F1 Exhaust Note 32

Moteur de course pseudo-3D pour la Sega 32X, dans l'esprit d'Out Run,
Super Hang-On ou F1 Exhaust Note : une route avec virages et côtes,
affichée ligne par ligne. La 32X se conduit comme un VDP spécialisé pour
le 68000 : le 68000 fait avancer la voiture et écrit sa position dans
les registres COMM, les deux SH2 calculent ce que montre chaque ligne.

Le principe : **c'est le VDP 32X qui dessine**. Toutes les lignes de
route dont l'écran peut avoir besoin sont préparées une fois pour toutes
dans le frame buffer ; à chaque image, les SH2 ne réécrivent que la
table de lignes (une adresse par ligne d'écran : quelle largeur de
route, décalée de combien), et le Master change au HBlank le décalage
d'un pixel (SFT) et les couleurs des bandes claires et sombres.

Les CPU ainsi libérés préparent aussi des sprites pour la Mega Drive :
tiles et table des sprites, écrites dans le frame buffer 32X, que le
68000 transfère par DMA vers la VRAM pendant le VBlank. Pour l'instant,
un compteur ; bientôt, les objets du bord de la route, mis à l'échelle.

Le projet fait suite à SuperVDP (deux plans de tiles 16 × 16) et en
garde le simulateur **pixel** (C, MiniFB, deux threads Master et Slave,
VBlank à 60 Hz, H_INT, API qui imite les registres : `vdp_dram`,
`vdp_palette`, FBCR, SFT, `comm[]`), les règles d'écriture dans le frame
buffer et le banc de mesure SH2.

## Compiler et lancer

```sh
git submodule update --init   # MiniFB, pour la fenêtre (voir ci-dessous)
make run                      # compile et lance la démo
make stats                    # la même, avec VDP_STATS=1
make test                     # tous les tests (environ 10 s)
```

À la création du dépôt, MiniFB s'ajoute une fois pour toutes par
`git submodule add https://github.com/emoon/minifb.git minifb`.

Sans MiniFB, seules les cibles sans fenêtre sont construites
(`f1en32_headless`, `test_race`). Sous Linux, MiniFB demande les en-têtes
X11 ; `-DMINIFB_USE_OPENGL_API=OFF` évite OpenGL.

Dans VS Code : ouvrir `F1ExhaustNote32.code-workspace` ; tâches
« Compiler », « Lancer la démo », « Lancer la démo avec VDP_STATS »,
« Tests ».

## La démo

Un circuit d'un peu plus de 1 000 segments (`demo/track.txt`) : ligne
droite, virages larges et serrés, enchaînement en S, crête suivie d'une
descente en courbe, montagnes russes, épingle. La voiture accélère seule
et se place à l'intérieur des virages qui arrivent.

| Manette          | Clavier  | Effet                                   |
|------------------|----------|-----------------------------------------|
| Gauche / Droite  | flèches  | écarte la voiture (le script la ramène) |
| B maintenu       | K        | frein                                   |
| Start maintenu   | Entrée   | pause                                   |

En haut à gauche, un compteur en sprites Mega Drive : la distance
parcourue. Au bord de la route, des piles de pneus, des palmiers et des
panneaux publicitaires (182 objets sur le tour), mis à l'échelle par les
SH2 et affichés en sprites Mega Drive. Pas encore de voiture ni
d'adversaires, et le haut de l'écran n'est qu'un ciel en dégradé : ils
viendront des sprites et des plans de la Mega Drive.

## Registres COMM

| COMM | Sens            | Rôle                                         |
|------|-----------------|----------------------------------------------|
| 1    | Master → Slave  | numéro de l'image à dessiner                 |
| 2    | Slave → Master  | dernière image terminée                      |
| 3    | Master → 68000  | image prête : le 68000 peut prendre le VDP 32X et transférer |
| 0    | 68000 → Master  | image transférée, VDP 32X rendu aux SH2      |
| 4, 5 | 68000 → SH2     | distance de la caméra sur le circuit (32 bits) |
| 6    | 68000 → SH2     | écart latéral de la voiture (signé)          |

Le Master relève les registres 4 à 6 une fois par image (deux lectures
identiques de suite : le manuel prévient qu'un registre lu pendant que
l'autre côté l'écrit donne une valeur indéfinie), puis publie la scène
pour le Slave. Le circuit
lui-même est une donnée constante (en ROM sur la console), lue par le
68000 comme par les SH2.

## Le moteur

**Le monde** (`race/world.h`). Le circuit est une suite de segments de
256 unités, chacun avec sa hauteur et sa courbure. `tools/track2c` les
calcule à la compilation depuis `demo/track.txt` (méthode de Jake
Gordon : la courbure monte, tient, redescend ; le dénivelé est réparti en
cosinus). La caméra est à 640 unités au-dessus de la route, 720 unités
derrière la voiture, sans inclinaison : l'horizon reste sur la ligne 96.

**La projection** (`race/project.c`). On projette le début de chacun des
160 segments devant la caméra, du plus proche au plus lointain (une
division par point : FOCAL / z). Un segment couvre les lignes d'écran
entre la projection de son début et celle de sa fin ; centre et
demi-largeur de la route y sont interpolés linéairement. Une ligne déjà
couverte par un segment plus proche le reste : c'est ce qui cache la
route derrière une crête. Les virages font glisser la route sur le côté :
la somme des courbures, sommée deux fois.

Chaque ligne d'écran est ainsi décrite **une seule fois** : soit la route
(centre, demi-largeur, bande claire ou sombre), soit le ciel.

**Les lignes préparées** (`race/render.c`, `render_prepare`). Sur une
route plate, la demi-largeur grandit de 1,2 pixel par ligne d'écran
(768 × (y − 96) / 640). Le moteur prépare donc 128 routes, de
demi-largeur w_k = 1,2 × k (k de 0 à 127, jusqu'à 152 pixels), écrites
une fois au démarrage dans chacun des deux frame buffers (le CPU n'écrit
que dans celui qui n'est pas affiché, et chaque table de lignes désigne
des lignes de son frame buffer). Les routes se suivent en mémoire,
séparées par 320 pixels d'herbe : une fenêtre de 320 pixels autour
d'une route ne voit jamais la suivante, quel que soit le décalage. Puis
douze lignes de ciel unies (une par bande de 8 lignes d'écran) et une
ligne de sol, pour ce qu'on voit au-delà d'une crête. En tout 67 890
octets : il reste 63 Ko libres par frame buffer.

La route n'utilise que quatre index de couleur (herbe, bordure, bitume,
ligne blanche) : les bandes claires et sombres ne sont pas dans les
pixels, mais dans la palette, changée ligne par ligne (sur les bandes
sombres, la ligne blanche prend la couleur du bitume).

**Le choix des lignes** (`render_line`). Pour une ligne de route : k = la
demi-largeur arrondie à un multiple de 1,2 pixel (un produit, pas de
division), puis l'octet de la route k qui doit tomber sur le pixel 0 de
l'écran (centre de la route k − centre à l'écran). La table ne donne que
des adresses de mots : si cet octet est impair, on pointe l'octet
d'avant, et la ligne est affichée décalée d'un pixel (SFT). Une route
entièrement hors de l'écran montre de l'herbe seule. Une ligne de ciel
pointe sur la ligne de sa bande. **Une écriture par ligne d'écran : 224
par image**.

**Le HBlank** (`race/race.c`). Chaque CPU range, pour ses lignes, le bit
SFT et la bande (claire ou sombre) dans une table par frame buffer.
Pendant l'affichage, le Master applique ceux de la ligne suivante :
depuis sa routine V_INT pour la ligne 0, puis depuis sa routine H_INT à
chaque HBlank. Il écrit SFT, et les quatre couleurs de la route quand la
bande change (16,5 fois par image en moyenne sur la démo). C'est le
Master parce qu'il a la priorité sur le bus ; la palette n'est
accessible que pendant les blancs. Le Slave n'a pas d'interruption.

**La Mega Drive, le bit FM et l'échange retardé** (`race/race.c`,
`race/hud.c`, `demo/md68k.c`). Le frame buffer, la palette et les
registres du VDP 32X n'appartiennent qu'à un côté à la fois, selon le
bit FM : aux SH2 pendant l'affichage (table de lignes, SFT et palette en
H_INT), au 68000 pendant le VBlank. Une image se termine ainsi :
1. les SH2 ont écrit la table de lignes et, dans la zone de transfert du
   frame buffer de dessin (`MD_STAGE_OFFSET`, `race/shared.h`), les tiles
   et les entrées de la table des sprites Mega Drive ; le Master écrit
   `COMM_READY` et ne touche plus au VDP 32X ;
2. au début du VBlank, le 68000 prend le VDP 32X (FM = 0), transfère par
   DMA la zone de transfert vers la VRAM, le rend (FM = 1) à la fin du
   DMA et écrit `COMM_DMA_DONE` ;
3. le Master échange alors les frame buffers : pendant le VBlank,
   l'échange est immédiat. Il est retardé jusque-là parce que le 68000 ne
   voit que le frame buffer qui n'est pas affiché : la route 32X et les
   sprites Mega Drive de la même image s'affichent ensemble ;
4. le Master prépare la ligne 0 de la nouvelle image (SFT, couleurs). Sa
   routine H_INT de la dernière ligne affichée l'a déjà fait pour l'image
   en cours, au cas où aucune nouvelle image ne serait prête.

Les entrées 0 à 9 de la table des sprites restent au 68000 ; celles des
SH2 commencent à la dixième. Les sprites Mega Drive sont devant la route
(bit PRI du 32X). Le compteur (`hud.c`) est le premier client de ce
chemin : cinq chiffres refaits à chaque image, 336 octets de DMA.

**Les objets du circuit** (`race/sprites.c`, preuve de concept). Le
circuit porte des objets (`object TYPE X PREMIER DERNIER PAS` dans
`demo/track.txt`) ; chaque type existe en trois tailles (pleine, moitié,
quart), faites par `tools/props.py`. La projection rend, pour chaque
segment, sa position, son échelle et la ligne de crête déjà couverte
devant lui (`SegmentView`). À chaque image :
1. le plan (`sprites_plan`), calculé à l'identique par les deux CPU :
   les objets visibles du plus proche au plus lointain (12 au plus), leur
   rectangle à l'écran, la crête qui les coupe, la plus petite taille
   source au moins aussi large que l'objet à l'écran, leurs tiles et
   leurs entrées dans la zone de transfert. Un objet n'est pris que si
   tout tient : 168 tiles et 68 entrées (le DMA du VBlank, 6 Ko), et
   20 sprites et 320 pixels de sprites par ligne ;
2. la mise à l'échelle (`sprites_draw`) : le Master fabrique les objets
   de rang pair, le Slave ceux de rang impair, en tiles Mega Drive
   (cellules de 8 x 8, rangées en colonnes), par colonnes et lignes
   répétées ou omises (pas en 16.16), comme sur Neo Geo ;
3. le Master écrit le compteur et l'en-tête de la zone de transfert.

Les images sources sont recopiées de la ROM vers la SDRAM au démarrage
(16 Ko au plus). Les objets utilisent la palette 1 de la Mega Drive,
écrite par le 68000 depuis la palette des BMP. Le coût dans `cost.h`
(`CYCLES_SPRITE_OBJECT`, `CYCLES_SPRITE_TILE`) est encore une
estimation : la mesure, et le nombre d'objets que l'on peut mettre à
l'échelle, sont l'étape suivante. Pas encore de cache des tiles en VRAM :
tout est refait à chaque image.

**Le partage du travail.** Chaque CPU projette lui-même le circuit pour
ses seules lignes (paires pour le Master, impaires pour le Slave), et
obtient exactement les valeurs d'une projection de tout l'écran : aucun
n'attend l'autre ni ne lit ce que l'autre a calculé. C'est maintenant
l'essentiel du travail des CPU.

**La division.** Le SH2 n'en a pas d'instruction : toutes passent par
`div_u32` (`race/fixed.h`), une par point projeté et une par segment
dessiné, soit environ 160 + 40 par image et par CPU. Elle appelle libgcc
pour l'instant ; l'unité DIVU du SH7604 la remplacera si le banc le
justifie.

**Code et cache.** Le moteur suppose le cache du SH2 en mode « 2 voies +
RAM » : le code de chaque image (`project.c`, `render_line`, `hud.c`,
2 036 octets) dans les 2 Ko de RAM interne ; le code froid (préparation
des lignes, fonctions des tests ; macro `COLD_CODE` de `fixed.h`) en
SDRAM (section `.text.sdram`). La RAM interne est pleine : `sprites.c`
reste en SDRAM pour l'instant.

## Le simulateur

`pixel/` modélise la 32X sans le 68000 : deux threads Master et Slave, le
VDP 32X (`vdp.c`), l'interface (`interface.c` : COMM, V_INT, H_INT, bit
FM) et, depuis cette version, une couche Mega Drive réduite aux sprites
(`mdvdp.c`) :
- VRAM, CRAM, table des sprites (80 entrées, liste chaînée), affichage
  avec les limites du mode H40 (20 sprites et 320 pixels par ligne,
  comptés quand on les dépasse) ; pas encore de plans ;
- superposition avec l'image 32X selon le bit PRI (lu ainsi : à 1, la
  Mega Drive devant ; sens à confirmer dans le manuel) et le bit de
  transparence des couleurs 32X ;
- DMA depuis le frame buffer 32X, chronométré : environ 205 octets par
  ligne de VBlank, 18 pendant l'affichage (Genesis Technical Overview ;
  à confirmer avec la source 32X) ; le 68000 attend la fin ;
- **bit FM** : tout accès au VDP 32X par un CPU qui ne l'a pas (frame
  buffer, palette, SFT, FBCR) arrête le simulateur avec un message.

Le programme 68000 (`demo/md68k.c`) est appelé au début du VBlank
(`md_vint`), puis à chaque ligne du VBlank (`md_vblank_line`).

## Mesurer : `VDP_STATS=1`

Toutes les 5 secondes dans la fenêtre, et à chaque capture sans fenêtre :

```
acces bus par image : master derniere 282 ... | total derniere 394 = 0.5 % du plafond 76800
cycles bus VDP (partages) : 394 ecritures x 5 = 1970 / 384000 (0.5 %)
cycles CPU estimes (modele du jeu + ecritures 3..5 c) : master 97649 + ecritures = 98495..99059 / 384000 ...
DMA Mega Drive : dernier VBlank 336 octets en 2 lignes, ... | echanges de frame buffer 230, dans le VBlank de leur DMA 228 | ...
derniere image : 0.26 image(s) de 1/60 s au mieux -> 60 images/s
```

- **Écritures** : comptées exactement par le simulateur (toutes passent
  par `vdp_dram_write16`). Le « max (période) » de la première période
  compte aussi les lignes préparées au démarrage.
- **Cycles bus VDP** : d'après le *32X Hardware Manual*, le VDP vide
  environ un mot toutes les 5 cycles, pour les deux CPU ensemble ; une
  image dure environ 384 000 cycles SH2 (23,01 MHz), d'où le plafond de
  76 800 écritures.
- **Cycles CPU** : le simulateur exécute du code natif et ne mesure pas
  le temps SH2. Le moteur compte ce qu'il fait et le multiplie par des
  coûts unitaires (`race/cost.h`) ; le simulateur ajoute 3 à 5 cycles
  par écriture du CPU. Les coûts unitaires sont **étalonnés** par le banc
  `tools/sh2cycles` : le vrai code compilé pour SH2, exécuté sous qemu
  avec un modèle de temps du SH2 (voir `tools/sh2cycles/README.md`).
  C'est un modèle, pas une 32X réelle, mais le nombre d'instructions est
  exact. Les interruptions du Master (H_INT, changements de couleurs)
  sont estimées.
- **DMA Mega Drive** : les octets transférés au dernier VBlank et les
  lignes qu'ils ont prises, le pire de la période, les DMA qui ont
  débordé sur l'affichage ; les échanges de frame buffer, et combien ont
  eu lieu dans le VBlank de leur DMA (les autres : le Master a vu la fin
  du DMA après la fin du VBlank, ses sprites ont précédé la route d'une
  image) ; les lignes à plus de 20 sprites ou 320 pixels.
- **Verdict** : la dernière ligne donne la durée de l'image en images de
  1/60 s (le CPU le plus lent, ou le bus du VDP). Au-delà de 1, on ne
  tient pas 60 images/s.

La fenêtre tourne toujours à 60 images/s sur l'hôte : seules les
statistiques disent ce que ferait la vraie machine.

## Vérifier

`tests/run_tests.sh` (ou `make test`) :
1. `test_race` : les lignes sont préparées dans un frame buffer de test,
   puis le test fait ce que fera le VDP (lire la table de lignes,
   appliquer SFT et la bande claire ou sombre) et compare au rendu naïf,
   écrit indépendamment : 300 000 lignes de route au hasard (dont très
   larges ou loin hors de l'écran) et toutes les lignes de ciel, une
   écriture par ligne, fenêtre toujours dans les lignes préparées. Puis
   5 000 images (plus d'un tour de la démo, et des vues au hasard) : la
   projection de chaque CPU (une ligne sur deux) doit donner exactement
   celle de tout l'écran, l'image être identique au rendu naïf, 224
   écritures par image, et la route toujours visible en bas de l'écran.
   Puis le compteur : 20 000 valeurs, tiles et table des sprites relues
   comme le VDP Mega Drive, comparées aux chiffres dessinés directement.
   Enfin les objets : 5 000 images (trajectoire de la démo et vues au
   hasard). Le plan doit être le même pour les deux CPU ; tiles et table
   des sprites, fabriquées par les deux, relues comme le VDP Mega Drive,
   doivent donner pixel pour pixel un rendu naïf de chaque objet (le plus
   proche devant) ; le transfert doit tenir dans son budget, et chaque
   ligne dans les limites du VDP (20 sprites, 320 pixels).
2. Le simulateur complet sans fenêtre (H_INT comprises) : la manette suit
   un scénario qui met la démo en pause avant chaque capture, et l'image
   capturée doit être identique, octet pour octet, à celle que
   `test_race` calcule pour le même scénario, compteur compris : tout le
   chemin SH2 → frame buffer → DMA → VRAM → sprites est vérifié. Une image qui diffère fait
   relancer le scénario une fois : sur une machine chargée, une H_INT
   servie trop tard décale les couleurs des lignes suivantes (compté « en
   retard » dans le journal).

## Les données

- Le circuit : `demo/track.txt`, converti par `tools/track2c`.
- Les couleurs (ciel, route claire et sombre) : `race/render.c`.
- Les objets : `demo/assets/props/*.bmp` (trois tailles par type),
  produits par `tools/props.py` (Python 3, sans dépendance), listés dans
  `demo/assets/assets.txt` et convertis par `tools/bmp2c` ; leurs
  tailles dans le monde dans `demo/content.c`. 15 couleurs communes,
  l'index 0 transparent (magenta dans les BMP).

## Organisation

| Dossier / fichier       | Rôle                                                    |
|-------------------------|---------------------------------------------------------|
| `pixel/`                | simulateur 32X : VDP, interface (COMM, V_INT, H_INT, FM), sprites Mega Drive, plates-formes |
| `race/race.c`           | boucles Master et Slave, relevé des registres, interruptions, échange de frame buffer |
| `race/world.h`          | circuit, caméra, constantes de projection              |
| `race/project.c`        | projection : description de chaque ligne d'écran       |
| `race/render.c`         | lignes préparées, choix d'une ligne (table, SFT, bande) |
| `race/hud.c`            | compteur en sprites Mega Drive (tiles, table des sprites) |
| `race/sprites.c`        | objets du circuit : plan, mise à l'échelle en sprites Mega Drive |
| `race/cost.h`           | modèle de coûts (cycles estimés)                       |
| `race/shared.h`         | registres COMM, scène partagée, accès sans cache       |
| `demo/`                 | circuit et programme 68000 de la démo                  |
| `tools/bmp2c.c`         | convertisseur BMP → C                                  |
| `tools/track2c.c`       | convertisseur circuit (segments, objets) → C           |
| `tools/props.py`        | générateur des images des objets et de leurs tailles réduites |
| `tests/`                | tests                                                  |
| `tools/sh2cycles/`      | banc SH2 sous qemu : temps du code compilé, étalonnage de `cost.h` |

## Vers une vraie 32X

- Les lignes préparées (68 Ko) sont écrites une fois par frame buffer au
  démarrage, par le Master ; ensuite, 224 écritures par image.
- `vdp_dram_write16()` devient une écriture directe en 2400 0000h +
  offset ; `SHARED()` donne déjà l'adresse cache-through sur SH2.
- **À vérifier sur un émulateur 32X, puis sur la console** : que la
  routine H_INT du Master écrive SFT et quatre couleurs de la palette
  dans le HBlank (le simulateur ne mesure pas ce temps). Sinon, le
  principe des bandes par la palette ne tient pas.
- **À vérifier aussi** : que le DMA du VDP Mega Drive puisse lire le
  frame buffer 32X ($840000 ; hors des sources documentées, mais
  l'adresse se code avec DMD0 = SA23), et à quel débit ; le sens du bit
  PRI.
- Programme de démarrage SH2 à écrire : cache en 2 voies + RAM, code de
  chaque image en C000 0000h, V_INT et H_INT démasquées sur le Master.
- `tools/sh2cycles/build-tools.sh` construit la chaîne `sh-elf` (gcc
  13.2) qui servira aussi pour la console.

## Licence

F1 Exhaust Note 32 est publié sous licence MIT. Copyright © 2026 Olivier
BROSSERON. Si vous utilisez ce projet, merci de citer le projet et son
auteur.
