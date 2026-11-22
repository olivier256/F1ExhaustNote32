# F1 Exhaust Note 32

Moteur de course pseudo-3D pour la Sega 32X, dans l'esprit d'Out Run,
Super Hang-On ou F1 Exhaust Note : une route dessinée ligne par ligne,
avec virages et côtes, sous un décor de fond qui défile. La 32X se
conduit comme un VDP spécialisé pour le 68000 : le 68000 fait avancer la
voiture et écrit sa position dans les registres COMM, les deux SH2
dessinent.

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

Il n'y a pas encore de sprites : ni la voiture, ni les adversaires, ni le
décor du bord de la route.

## Registres COMM

| COMM | Sens            | Rôle                                         |
|------|-----------------|----------------------------------------------|
| 1    | Master → Slave  | numéro de l'image à dessiner                 |
| 2    | Slave → Master  | dernière image terminée                      |
| 4, 5 | 68000 → SH2     | distance de la caméra sur le circuit (32 bits) |
| 6    | 68000 → SH2     | écart latéral de la voiture (signé)          |
| 7    | 68000 → SH2     | défilement horizontal du décor de fond       |

Le Master relève les registres 4 à 7 une fois par image (deux lectures
identiques de suite), puis publie la scène pour le Slave. Le circuit
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
(centre, demi-largeur, bandes claires ou sombres), soit le décor.

**Le dessin** (`race/render.c`). Chaque mot de la ligne est écrit une
fois, en 16 bits : 160 écritures par ligne, **35 840 par image** (46,7 %
du plafond de 76 800), quel que soit le paysage.
- Route : neuf plages de couleur unie (herbe, bordure, bitume, ligne,
  bitume, ligne, bitume, bordure, herbe) ; un mot à cheval sur deux
  plages réunit leurs couleurs. Les couleurs de la route ont des index
  réservés (240 et au-delà), écrits à l'initialisation.
- Décor : la ligne y d'une image de 512 pixels de large qui boucle,
  copiée par mots. Une colonne de départ impaire est copiée depuis la
  colonne paire précédente et affichée décalée d'un point (registre SFT,
  écrit ligne par ligne par le Master en H_INT, comme dans SuperVDP) :
  161 mots. Sous le décor (au-delà d'une crête), on prolonge sa couleur
  de sol.

**Le partage du travail** (`race/race.c`). Chaque CPU projette lui-même
le circuit pour ses seules lignes (paires pour le Master, impaires pour
le Slave), et obtient exactement les valeurs d'une projection de tout
l'écran : aucun n'attend l'autre ni ne lit ce que l'autre a calculé.

**La division.** Le SH2 n'en a pas d'instruction : toutes passent par
`div_u32` (`race/fixed.h`), une par point projeté et une par segment
dessiné, soit environ 160 + 40 par image et par CPU. Elle appelle libgcc
pour l'instant ; l'unité DIVU du SH7604 la remplacera si le banc le
justifie.

## Mesurer : `VDP_STATS=1`

Comme dans SuperVDP : toutes les 5 secondes dans la fenêtre, et à chaque
capture sans fenêtre, les écritures dans le frame buffer (comptées
exactement), les cycles du bus du VDP, et les cycles CPU **estimés** par
le modèle de coûts du moteur (`race/cost.h`), étalonné par le banc
`tools/sh2cycles` (le vrai code compilé pour SH2, exécuté sous qemu avec
un modèle de temps du SH2). La dernière ligne donne la durée de l'image
en images de 1/60 s : au-delà de 1, on ne tient pas 60 images/s.

La fenêtre tourne toujours à 60 images/s sur l'hôte : seules les
statistiques disent ce que ferait la vraie machine.

## Vérifier

`tests/run_tests.sh` (ou `make test`) :
1. `test_race` : `render_line` contre un rendu naïf pixel par pixel, sur
   200 000 lignes de route au hasard (dont hors de l'écran) et toutes les
   colonnes de départ du décor ; 160 ou 161 écritures par ligne, chaque
   mot écrit une fois. Puis 5 000 images (plus d'un tour de la démo, et
   des vues au hasard) : la projection de chaque CPU (une ligne sur deux)
   doit donner exactement celle de tout l'écran, l'image des deux CPU être
   identique au rendu naïf, et la route toujours visible en bas de
   l'écran.
2. Le simulateur complet sans fenêtre : la manette suit un scénario qui
   met la démo en pause avant chaque capture, et l'image capturée doit
   être identique, octet pour octet, à celle que `test_race` calcule pour
   le même scénario (le programme 68000 rejoué image par image).

## Les données

- Le décor : `demo/assets/sky.bmp`, 512 × 112, produit par
  `tools/sky.py` (Pillow ; une image originale, calculée), converti en C
  par `tools/bmp2c` (type `bitmap`). Index 240 et au-delà réservés à la
  route.
- Le circuit : `demo/track.txt`, converti par `tools/track2c`.

## Organisation

| Dossier / fichier       | Rôle                                                    |
|-------------------------|---------------------------------------------------------|
| `pixel/`                | simulateur 32X : VDP, interface (COMM, V_INT, H_INT), plates-formes |
| `race/race.c`           | boucles Master et Slave, relevé des registres, interruptions, échange de frame buffer |
| `race/world.h`          | circuit, caméra, constantes de projection              |
| `race/project.c`        | projection : description de chaque ligne d'écran       |
| `race/render.c`         | dessin d'une ligne (route ou décor)                    |
| `race/cost.h`           | modèle de coûts (cycles estimés)                       |
| `race/shared.h`         | registres COMM, scène partagée, accès sans cache       |
| `demo/`                 | circuit, décor et programme 68000 de la démo           |
| `tools/bmp2c.c`         | convertisseur BMP → C                                  |
| `tools/track2c.c`       | convertisseur circuit → C                              |
| `tools/sky.py`          | fabrique le décor de la démo                           |
| `tests/`                | tests                                                  |
| `tools/sh2cycles/`      | banc SH2 sous qemu : temps du code compilé, étalonnage de `cost.h` |

## Licence

F1 Exhaust Note 32 est publié sous licence MIT. Copyright © 2026 Olivier
BROSSERON. Si vous utilisez ce projet, merci de citer le projet et son
auteur.
