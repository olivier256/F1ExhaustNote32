# =========================================================================
# Conteneur Linux du banc sh2cycles, pour le lancer depuis un Mac (qemu en
# mode utilisateur n'existe que sous Linux). Voir docker.sh.
# La construction de l'image refait la chaîne sh-elf et qemu (environ
# 40 minutes, une seule fois) ; le dépôt est ensuite monté dans /src.
# =========================================================================
FROM ubuntu:24.04

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
        build-essential curl xz-utils libgmp-dev libmpfr-dev libmpc-dev \
        ninja-build libglib2.0-dev pkg-config python3 python3-venv \
        python3-distlib python3-numpy flex bison texinfo \
    && rm -rf /var/lib/apt/lists/*

ENV PREFIX=/opt
COPY build-tools.sh /tmp/build-tools.sh
RUN WORK=/tmp/build /tmp/build-tools.sh && rm -rf /tmp/build

WORKDIR /src
CMD ["tools/sh2cycles/run.sh"]
