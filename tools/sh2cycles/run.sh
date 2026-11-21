#!/bin/sh
# =========================================================================
# Banc sh2cycles : mesure le temps de compose_line compilé pour SH2, et
# étalonne le modèle de coûts du simulateur (supervdp/cost.h).
#
# 1. Compile supervdp/compose.c pour SH2 (sh-elf-gcc -m2, -O2, sans les
#    compteurs), avec le banc bench.c, aux adresses de la 32X (bench.ld).
# 2. L'exécute sous qemu-sh4eb avec le greffon sh2cycles, qui applique le
#    modèle de temps du SH2 (cycles par instruction, branchements,
#    dépendances, cache de 4 Ko et SDRAM). Un programme par CPU.
# 3. Calcule sur l'hôte les compteurs du modèle pour les mêmes images,
#    vérifie que le code SH2 dessine les mêmes pixels, et ajuste les
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

# Images de la démo, converties par bmp2c compilé pour l'hôte
cc -std=c99 -O2 -Isupervdp -o "$OUT/bmp2c" tools/bmp2c.c
"$OUT/bmp2c" demo/assets/assets.txt "$OUT/demo_assets" 2>/dev/null

INC="-Ipixel -Isupervdp -I$T -I$OUT"

# ---- Hôte : compteurs du modèle -------------------------------------------
cc -std=c99 -O2 -Wall $INC -o "$OUT/costs" $T/costs.c supervdp/compose.c \
  "$OUT/demo_assets.c"
"$OUT/costs" 0 >"$OUT/costs_master.csv"
"$OUT/costs" 1 >"$OUT/costs_slave.csv"

# ---- SH2 : le banc ----------------------------------------------------------
CFLAGS="-m2 -mb -O2 -std=c99 -ffreestanding -fno-builtin -Wall $INC"
"${SH_ELF}gcc" $CFLAGS -c supervdp/compose.c -o "$OUT/compose.o"
"${SH_ELF}gcc" $CFLAGS -c "$OUT/demo_assets.c" -o "$OUT/assets.o"
"${SH_ELF}gcc" -m2 -mb -Wa,--isa=sh2 -c $T/start.S -o "$OUT/start.o"
for cpu in master slave; do
  first=0
  [ $cpu = slave ] && first=1
  "${SH_ELF}gcc" $CFLAGS -DBENCH_FIRST_LINE=$first -c $T/bench.c \
    -o "$OUT/bench_$cpu.o"
  # Segments alignés sur 4 Ko : qemu (mode utilisateur Linux) les projette
  # en mémoire page par page.
  "${SH_ELF}gcc" -m2 -mb -nostdlib -T $T/bench.ld -Wl,-z,max-page-size=0x1000 \
    -Wl,--no-warn-rwx-segments -o "$OUT/bench_$cpu.elf" \
    "$OUT/start.o" "$OUT/bench_$cpu.o" "$OUT/compose.o" "$OUT/assets.o" -lgcc
done

# Taille en SDRAM (256 Ko sur 32X) : code + données + tiles + pile
"${SH_ELF}size" -A "$OUT/bench_master.elf" | awk '
  $1 ~ /^\.(text|rodata|data|bss|stack)$/ { n += $2 }
  END { printf "SDRAM occupee par le banc : %d octets sur 262144 (%.0f %%)\n", n, 100*n/262144 }'

# ---- Greffon qemu -----------------------------------------------------------
cc -O2 -shared -fPIC -Wall -I"$QEMU_INCLUDE" $(pkg-config --cflags glib-2.0) \
  -o "$OUT/libsh2cycles.so" $T/sh2cycles_plugin.c

# ---- Mesure -----------------------------------------------------------------
for cpu in master slave; do
  if ! "$QEMU" -plugin "$OUT/libsh2cycles.so" "$OUT/bench_$cpu.elf" \
    >"$OUT/prints_$cpu.txt" 2>"$OUT/cycles_$cpu.txt"; then
    echo "qemu : echec du banc $cpu :"
    grep -v '^sh2cycles;' "$OUT/cycles_$cpu.txt" | head -5
    exit 1
  fi
done

python3 $T/fit.py "$OUT"
