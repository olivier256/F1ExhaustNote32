#!/bin/bash
# =========================================================================
# Construit les outils du banc sh2cycles (voir README.md) :
# - binutils 2.42 et gcc 13.2 pour sh-elf (SH2 gros-boutiste, avec
#   libgcc), dans $PREFIX/sh-elf ;
# - qemu 8.2.2, sh4eb en mode utilisateur, avec les greffons, dans
#   $PREFIX/qemu.
# PREFIX vaut ~/opt par défaut (pas besoin de sudo). Environ 40 minutes
# sur 2 cœurs. Linux uniquement : qemu en mode utilisateur n'existe pas
# sous macOS. Sur Mac, utiliser tools/sh2cycles/docker.sh.
# =========================================================================
set -e

if [ "$(uname -s)" != "Linux" ]; then
    echo "build-tools.sh : Linux uniquement (qemu en mode utilisateur n'existe"
    echo "pas sous $(uname -s)). Sur Mac : tools/sh2cycles/docker.sh"
    exit 1
fi

PREFIX=${PREFIX:-$HOME/opt}
WORK=${WORK:-$PREFIX/build}
JOBS=${JOBS:-$(nproc)}
PYTHON=${PYTHON:-/usr/bin/python3} # qemu veut le Python du système (distlib)

mkdir -p "$WORK"
cd "$WORK"
log() { echo "[$(date +%H:%M:%S)] $*"; }

log "telechargement dans $WORK"
[ -f binutils-2.42.tar.xz ] || curl -sSfLO https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.xz
[ -f gcc-13.2.0.tar.xz ] || curl -sSfLO https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.xz
[ -f qemu-8.2.2.tar.xz ] || curl -sSfLO https://download.qemu.org/qemu-8.2.2.tar.xz
log extraction
tar xf binutils-2.42.tar.xz
tar xf gcc-13.2.0.tar.xz
tar xf qemu-8.2.2.tar.xz
export PATH="$PREFIX/sh-elf/bin:$PATH"

log "binutils (journal : $WORK/binutils.log)"
mkdir -p b-binutils && cd b-binutils
../binutils-2.42/configure --target=sh-elf --prefix="$PREFIX/sh-elf" \
    --disable-nls --disable-werror --disable-gdb --disable-sim >../binutils.log 2>&1
make -j"$JOBS" >>../binutils.log 2>&1
make install >>../binutils.log 2>&1
cd ..

log "gcc (journal : $WORK/gcc.log)"
mkdir -p b-gcc && cd b-gcc
../gcc-13.2.0/configure --target=sh-elf --prefix="$PREFIX/sh-elf" \
    --enable-languages=c --without-headers --with-newlib --disable-nls \
    --disable-libssp --disable-multilib --with-cpu=m2 --with-endian=big \
    --disable-shared --disable-threads --disable-libquadmath --disable-libgomp \
    --disable-lto >../gcc.log 2>&1
make -j"$JOBS" all-gcc >>../gcc.log 2>&1
make install-gcc >>../gcc.log 2>&1
make -j"$JOBS" all-target-libgcc >>../gcc.log 2>&1
make install-target-libgcc >>../gcc.log 2>&1
cd ..

log "qemu (journal : $WORK/qemu.log)"
mkdir -p b-qemu && cd b-qemu
../qemu-8.2.2/configure --python="$PYTHON" --target-list=sh4eb-linux-user \
    --enable-plugins --disable-docs --disable-werror --prefix="$PREFIX/qemu" \
    >../qemu.log 2>&1
make -j"$JOBS" >>../qemu.log 2>&1
make install >>../qemu.log 2>&1

log "fini : $PREFIX/sh-elf, $PREFIX/qemu"
