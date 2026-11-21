#!/bin/bash
# =========================================================================
# Construit les outils du banc sh2cycles (voir README.md) :
# - binutils 2.42 et gcc 13.2 pour sh-elf (SH2 gros-boutiste, avec
#   libgcc), dans /opt/sh-elf ;
# - qemu 8.2.2, sh4eb en mode utilisateur, avec les greffons, dans
#   /opt/qemu.
# Environ 40 minutes sur 2 cœurs. Dossier de travail : /opt/build.
# =========================================================================
set -e
mkdir -p /opt/build
cd /opt/build
log() { echo "[$(date +%H:%M:%S)] $*"; }
log telechargement
curl -sSfLO https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.xz
curl -sSfLO https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.xz
curl -sSfLO https://download.qemu.org/qemu-8.2.2.tar.xz
log extraction
tar xf binutils-2.42.tar.xz; tar xf gcc-13.2.0.tar.xz; tar xf qemu-8.2.2.tar.xz
export PATH=/opt/sh-elf/bin:$PATH
log binutils
mkdir -p b-binutils && cd b-binutils
../binutils-2.42/configure --target=sh-elf --prefix=/opt/sh-elf --disable-nls --disable-werror --disable-gdb --disable-sim > ../binutils.log 2>&1
make -j2 >> ../binutils.log 2>&1 && make install >> ../binutils.log 2>&1
cd ..
log gcc
mkdir -p b-gcc && cd b-gcc
../gcc-13.2.0/configure --target=sh-elf --prefix=/opt/sh-elf --enable-languages=c --without-headers --with-newlib --disable-nls --disable-libssp --disable-multilib --with-cpu=m2 --with-endian=big --disable-shared --disable-threads --disable-libquadmath --disable-libgomp --disable-lto > ../gcc.log 2>&1
make -j2 all-gcc >> ../gcc.log 2>&1 && make install-gcc >> ../gcc.log 2>&1
make -j2 all-target-libgcc >> ../gcc.log 2>&1 && make install-target-libgcc >> ../gcc.log 2>&1
cd ..
log qemu
mkdir -p b-qemu && cd b-qemu
../qemu-8.2.2/configure --target-list=sh4eb-linux-user --enable-plugins --disable-docs --disable-werror --prefix=/opt/qemu > ../qemu.log 2>&1
make -j2 >> ../qemu.log 2>&1 && make install >> ../qemu.log 2>&1
log fini
