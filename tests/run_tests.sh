#!/bin/sh
# =========================================================================
# Tous les tests, depuis la racine du dépôt :  tests/run_tests.sh
#
# 1. test_race : ce qu'afficherait le VDP (table de lignes, SFT,
#    couleurs) contre un rendu de référence naïf, ligne par ligne puis
#    image par image (voir tests/test_race.c).
# 2. Le simulateur complet, sans fenêtre (deux CPU, VBlank, H_INT qui
#    écrit SFT et les couleurs de la route, programme 68000 de la démo) :
#    la manette suit un scénario qui met la démo en pause (Start) avant
#    chaque capture ; l'image capturée doit être identique, octet pour
#    octet, à l'image de référence calculée par test_race pour le même
#    scénario.
#    Le simulateur tourne en temps réel : une dizaine de secondes.
# =========================================================================
set -e

BUILD=${BUILD:-build}
OUT=$BUILD/tests
mkdir -p "$OUT"

cmake -S . -B "$BUILD" >/dev/null
cmake --build "$BUILD" --target test_race f1en32_headless >/dev/null

echo "== moteur"
"$BUILD/test_race"

# run NOM IMAGES "MANETTE..." "CAPTURES..."
#   Lance le simulateur IMAGES images avec la manette (DEBUT-FIN:BOUTONS),
#   capture les images demandées, et compare chacune à sa référence.
#   Une image qui diffère fait relancer le scénario une fois : sur une
#   machine chargée, une H_INT servie en retard décale les couleurs ou SFT
#   des lignes suivantes (compté « en retard » dans le journal). Un vrai
#   défaut du moteur, lui, se reproduit.
simulate() {
  name=$1 frames=$2 pads=$3 shots=$4
  padopt=""
  for step in $pads; do padopt="$padopt --pad $step"; done
  shotopt=""
  for shot in $shots; do shotopt="$shotopt --shot $shot:$OUT/$name-$shot.ppm"; done
  # shellcheck disable=SC2086
  "$BUILD/f1en32_headless" --frames "$frames" $padopt $shotopt \
    2>"$OUT/$name.log"
}

same() {
  for shot in $shots; do
    cmp -s "$OUT/$name-$shot.ppm" "$OUT/$name-$shot.ref.ppm" || return 1
  done
}

run() {
  name=$1 frames=$2 pads=$3 shots=$4
  padopt=""
  for step in $pads; do padopt="$padopt --pad $step"; done
  for shot in $shots; do
    # shellcheck disable=SC2086
    "$BUILD/test_race" --ppm "$shot" $padopt "$OUT/$name-$shot.ref.ppm" \
      >"$OUT/$name-$shot.info"
  done
  simulate "$@"
  if ! same; then
    echo "      (images differentes : nouvel essai)"
    grep "H_INT" "$OUT/$name.log" | sed 's/^/      /'
    simulate "$@"
  fi
  for shot in $shots; do
    if cmp -s "$OUT/$name-$shot.ppm" "$OUT/$name-$shot.ref.ppm"; then
      echo "OK    $name, image $shot ($(cat "$OUT/$name-$shot.info"))"
    else
      echo "ECHEC $name, image $shot : $OUT/$name-$shot.ppm differe de" \
        "$OUT/$name-$shot.ref.ppm"
      exit 1
    fi
    grep -E "\[image $shot\] (acces bus|cycles CPU|derniere image|H_INT)" \
      "$OUT/$name.log" | sed 's/^/      /'
  done
}

echo "== simulateur complet"
# Au départ, à l'arrêt
run depart 40 "0-39:S" "30"
# Accélération, écart à gauche, pause dans le premier virage ; reprise,
# écart à droite, pause dans la descente après la crête
run course 470 "120-160:L 220-240:S 300-330:R 430-469:S" "232 460"

echo "Tous les tests sont passes."
