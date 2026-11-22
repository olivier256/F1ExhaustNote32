#!/bin/sh
# =========================================================================
# Banc sh2cycles : mesure le temps du moteur (race/project.c et
# race/render.c) compilé pour SH2, et étalonne le modèle de coûts du
# simulateur (race/cost.h).
#
# 1. Compile le moteur pour SH2 (sh-elf-gcc -m2, -O2, sans les
#    compteurs), avec le banc bench.c, aux adresses de la 32X.
# 2. L'exécute sous qemu-sh4eb avec le greffon sh2cycles, qui applique le
#    modèle de temps du SH2 (cycles par instruction, branchements,
#    dépendances, cache et SDRAM). Un programme par CPU.
#    Cache : par défaut en mode « 2 voies + RAM », le code de chaque image
#    (project.c, render_line, hud.c) dans la RAM interne (bench_ram.ld). CACHE=4 : cache en 4 voies, tout en
#    SDRAM (bench.ld), pour comparer.
# 3. Calcule sur l'hôte les compteurs du modèle pour les mêmes images,
#    vérifie que le code SH2 donne le même affichage, et ajuste les
#    coûts unitaires (fit.py).
#
# Il faut (Linux ; qemu en mode utilisateur n'existe pas sous macOS, où
# l'on passe par tools/sh2cycles/docker.sh) :
# - la chaîne sh-elf et qemu-sh4eb avec les greffons, construits par
#   build-tools.sh dans $PREFIX (par défaut ~/opt) ;
# - glib (en-têtes), Python 3 et numpy.
#
# Usage : tools/sh2cycles/run.sh   (depuis la racine du dépôt)
# =========================================================================
set -e

if [ "$(uname -s)" != "Linux" ]; then
  echo "run.sh : Linux uniquement. Sur Mac : tools/sh2cycles/docker.sh"
  exit 1
fi

PREFIX=${PREFIX:-$HOME/opt}
SH_ELF=${SH_ELF:-$PREFIX/sh-elf/bin/sh-elf-}
QEMU=${QEMU:-$PREFIX/qemu/bin/qemu-sh4eb}
QEMU_INCLUDE=${QEMU_INCLUDE:-$PREFIX/qemu/include}
for tool in "${SH_ELF}gcc" "$QEMU"; do
  if [ ! -x "$tool" ]; then
    echo "run.sh : $tool introuvable. Construire les outils :"
    echo "  tools/sh2cycles/build-tools.sh   (ou PREFIX=... si ailleurs)"
    exit 1
  fi
done
OUT=build/sh2cycles
T=tools/sh2cycles
mkdir -p "$OUT"

# Circuit de la démo, converti par l'outil de l'hôte
cc -std=c99 -O2 -Irace -o "$OUT/track2c" tools/track2c.c -lm
"$OUT/track2c" demo/track.txt "$OUT/demo_track" demo_track 2>/dev/null
# Objets du circuit (images des accessoires), convertis de même
cc -std=c99 -O2 -Irace -o "$OUT/bmp2c" tools/bmp2c.c
"$OUT/bmp2c" demo/assets/assets.txt "$OUT/demo_assets" >/dev/null

INC="-Ipixel -Irace -I$T -I$OUT"
ENGINE="race/project.c race/render.c race/hud.c"
DATA="$OUT/demo_track.c $OUT/demo_assets.c demo/content.c"

# ---- Hôte : compteurs du modèle -------------------------------------------
cc -std=c99 -O2 -Wall $INC -o "$OUT/costs" $T/costs.c $ENGINE $DATA
"$OUT/costs" 0 >"$OUT/costs_master.csv"
"$OUT/costs" 1 >"$OUT/costs_slave.csv"

# ---- SH2 : le banc ----------------------------------------------------------
if [ "${CACHE:-ram}" = 4 ]; then
  LDSCRIPT="-T $T/bench.ld"
  PLUGIN_OPTIONS=""
  echo "Cache : 4 voies (4 Ko), tout le programme en SDRAM"
else
  # qemu refuse les adresses au-delà de 2 Go : la RAM interne (C000 0000h
  # sur SH2) est remplacée par 5000 0000h, pour l'édition de liens comme
  # pour le greffon.
  LDSCRIPT="-T $T/bench_ram.ld -Wl,--defsym,RAM_BASE=0x50000000"
  PLUGIN_OPTIONS=",ways=2,ram=0x50000000"
  echo "Cache : 2 voies (2 Ko) + RAM interne (2 Ko) avec le code de chaque image"
fi

CFLAGS="-m2 -mb -O2 -std=c99 -ffreestanding -fno-builtin -Wall $INC"
OBJECTS="$OUT/start.o $OUT/project.o $OUT/render.o $OUT/hud.o $OUT/data.o"
"${SH_ELF}gcc" -m2 -mb -Wa,--isa=sh2 -c $T/start.S -o "$OUT/start.o"
"${SH_ELF}gcc" $CFLAGS -c race/project.c -o "$OUT/project.o"
"${SH_ELF}gcc" $CFLAGS -c race/render.c -o "$OUT/render.o"
"${SH_ELF}gcc" $CFLAGS -c race/hud.c -o "$OUT/hud.o"
for f in $DATA; do
  "${SH_ELF}gcc" $CFLAGS -c "$f" -o "$OUT/$(basename "$f" .c).o"
done
"${SH_ELF}ld" -r -o "$OUT/data.o" "$OUT/demo_track.o" "$OUT/demo_assets.o" "$OUT/content.o"
for cpu in master slave; do
  first=0
  [ $cpu = slave ] && first=1
  "${SH_ELF}gcc" $CFLAGS -DBENCH_FIRST_LINE=$first -c $T/bench.c \
    -o "$OUT/bench_$cpu.o"
  # Segments alignés sur 4 Ko : qemu (mode utilisateur Linux) les projette
  # en mémoire page par page.
  "${SH_ELF}gcc" -m2 -mb -nostdlib $LDSCRIPT -Wl,-z,max-page-size=0x1000 \
    -Wl,--no-warn-rwx-segments -o "$OUT/bench_$cpu.elf" \
    "$OUT/bench_$cpu.o" $OBJECTS -lgcc
done

# Taille en SDRAM (256 Ko sur 32X) : code + données + pile ; et code en
# RAM interne (2 Ko au plus, vérifié par bench_ram.ld)
"${SH_ELF}size" -A "$OUT/bench_master.elf" | awk '
  $1 ~ /^\.(text|rodata|data|bss|stack)$/ { n += $2 }
  $1 == ".ram" { ram = $2 }
  END { printf "SDRAM occupee par le banc : %d octets sur 262144 (%.0f %%)\n", n, 100*n/262144
        if (ram) printf "Code en RAM interne : %d octets sur 2048\n", ram }'

# ---- Greffon qemu -----------------------------------------------------------
cc -O2 -shared -fPIC -Wall -I"$QEMU_INCLUDE" $(pkg-config --cflags glib-2.0) \
  -o "$OUT/libsh2cycles.so" $T/sh2cycles_plugin.c

# ---- Mesure -----------------------------------------------------------------
for cpu in master slave; do
  if ! "$QEMU" -plugin "$OUT/libsh2cycles.so$PLUGIN_OPTIONS" "$OUT/bench_$cpu.elf" \
    >"$OUT/prints_$cpu.txt" 2>"$OUT/cycles_$cpu.txt"; then
    echo "qemu : echec du banc $cpu :"
    grep -v '^sh2cycles;' "$OUT/cycles_$cpu.txt" | head -5
    exit 1
  fi
done

python3 $T/fit.py "$OUT"
