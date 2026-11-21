Oui, c'est bon : tous les tests passent. Les chiffres de cycles sont élevés, mais c'est attendu : depuis le patch 0002, ils reprennent les coûts mesurés sur le code SH2.

**1. Que le rendu soit juste.** Ce sont les lignes qui comptent le plus.
- `1168576 lignes verifiees, 0 echecs` : le moteur dessine exactement comme le rendu de référence.
- Les 5 `OK` : le simulateur complet (deux CPU, VBlank, 68000) produit les images attendues, octet pour octet.

**2. Le bus du VDP.** Ce chiffre est exact, et il est bon.
- 35 840 écritures par image, soit 46,7 % du plafond, quel que soit le défilement.
- Les écritures ne seront pas le problème.

**3. Le temps CPU.** C'est le chiffre à surveiller, et il est mauvais pour l'instant. Avec des écritures à 3 cycles :

| Scénario | Modèle + écritures | Images de 1/60 s |
|---|---|---|
| départ (xA = 0, pair) | 597 944 | 1,6 |
| impair (xA = 37, xB = 29) | 960 013 | 2,5 |
| pire image pendant le défilement (« max modèle », milieu) | ≈ 985 000 | 2,6 |

- Au-dessus de 100 %, on ne tient pas 60 images/s. On en est à 1,6 à 2,6 images de 1/60 s, soit 20 à 30 images/s sur une vraie 32X.
- L'écart entre les cas pair et impair montre que le chemin impair est le premier poste à réduire.
- La moyenne de `test_compose` (1 481 473 cycles pour les deux CPU, soit environ 740 000 par CPU) recoupe les 717 000 mesurés par le banc. Le modèle est cohérent.

**Un piège à éviter :** la fenêtre tourne à 60 images/s sur votre Mac, quoi qu'il arrive. Le simulateur ne ralentit pas pour imiter le SH2. Seules les statistiques disent ce que ferait la vraie machine.

**Un oubli de ma part :** la ligne de verdict (« dernière image : X image(s) de 1/60 s ») existe bien, mais `run_tests.sh` ne garde que trois lignes du journal, et elle n'en fait pas partie. Vous la trouverez dans `build/tests/*.log`, et avec `make stats`. Je corrigerai le filtre dans le prochain patch.

**Pour Docker, rien ne presse.** Le banc donnerait aujourd'hui les mêmes chiffres, puisque `cost.h` en sort. Il deviendra indispensable dès qu'on modifiera `compose.c` : à chaque optimisation, il mesure le nouveau code et recalcule les coûts. Vous pouvez lancer la construction de l'image ce soir pour qu'elle soit prête.

La suite logique est de réécrire le C pour le SH2, en commençant par le chemin impair et le découpage en segments, et de vérifier chaque étape avec le banc.
