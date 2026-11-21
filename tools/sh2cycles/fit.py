#!/usr/bin/env python3
"""
Banc sh2cycles : relie le temps mesuré sur le code SH2 (greffon qemu) aux
compteurs du modèle de coûts (supervdp/cost.h), et en déduit les coûts
unitaires par moindres carrés.

    python3 fit.py DOSSIER

DOSSIER contient, pour CPU = master et slave :
    costs_CPU.csv   compteurs du modèle (programme hôte costs)
    cycles_CPU.txt  temps mesurés (greffon : lignes « sh2cycles;... »)
    prints_CPU.txt  empreintes des images dessinées par le code SH2

Chaque ligne d'écran écrit exactement 160 mots : le coût d'un mot copié
depuis une source paire ne se distingue pas du coût fixe d'une ligne. Il
est donc compté dans CYCLES_LINE, et les autres coûts par mot sont des
surcoûts par rapport à lui (CYCLES_WORD_EVEN = 0).
"""

import sys
import numpy as np

FIELDS = ["lines", "aTiles", "runs", "tileChunks", "wordsEven", "wordsOdd",
          "wordsMerged"]
# Coefficients ajustés, et la constante de cost.h correspondante
FITTED = [("lines", "CYCLES_LINE"), ("aTiles", "CYCLES_A_TILE"),
          ("runs", "CYCLES_RUN"), ("tileChunks", "CYCLES_TILE_CHUNK"),
          ("wordsOdd", "CYCLES_WORD_ODD"), ("wordsMerged", "CYCLES_WORD_MERGED")]

FB_WRITES_PER_CPU = 17920
FRAME_CYCLES = 384000


def load(folder, cpu):
    costs = []
    for line in open("%s/costs_%s.csv" % (folder, cpu)):
        p = line.strip().split(";")
        row = dict(zip(FIELDS, map(int, p[1:8])))
        row["model"] = int(p[8])
        row["print"] = p[9]
        row["shifted"] = int(p[10]) if len(p) > 10 else 0
        costs.append(row)
    measured = []
    for line in open("%s/cycles_%s.txt" % (folder, cpu)):
        if line.startswith("sh2cycles;"):
            p = line.strip().split(";")
            measured.append({"cycles": int(p[2]), "insns": int(p[3]),
                             "misses": int(p[4]), "fb": int(p[5]),
                             "codeMisses": int(p[6]) if len(p) > 6 else 0})
    prints = [l.strip() for l in open("%s/prints_%s.txt" % (folder, cpu))]
    if not (len(costs) == len(measured) == len(prints)):
        sys.exit("%s : nombres d'images differents (%d, %d, %d)"
                 % (cpu, len(costs), len(measured), len(prints)))
    bad = [i for i in range(len(costs)) if costs[i]["print"] != prints[i]]
    if bad:
        sys.exit("%s : le code SH2 ne dessine pas comme l'hote (images %s)"
                 % (cpu, bad[:10]))
    # 160 mots par ligne, 161 pour une ligne décalée (SFT)
    for i, m in enumerate(measured):
        expected = FB_WRITES_PER_CPU + costs[i]["shifted"]
        if m["fb"] != expected:
            sys.exit("%s : image %d, %d ecritures FB au lieu de %d"
                     % (cpu, i, m["fb"], expected))
    return costs, measured


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "."
    rows, ys = [], []
    for cpu in ("master", "slave"):
        costs, measured = load(folder, cpu)
        shifted = sum(c["shifted"] for c in costs) / len(costs)
        print("%s : %d images, empreintes identiques a l'hote, %d a %d ecritures "
              "FB par image (160 ou 161 par ligne ; %.0f lignes decalees en "
              "moyenne)" % (cpu, len(costs), FB_WRITES_PER_CPU,
                            FB_WRITES_PER_CPU + 112, shifted))
        for c, m in zip(costs, measured):
            rows.append(c)
            ys.append(m)

    cycles = np.array([m["cycles"] for m in ys], dtype=float)
    insns = np.array([m["insns"] for m in ys], dtype=float)
    misses = np.array([m["misses"] for m in ys], dtype=float)
    oldModel = np.array([r["model"] for r in rows], dtype=float)

    print()
    print("Mesure SH2 (code compile, cache simule), par CPU et par image,")
    print("hors ecritures dans le frame buffer :")
    print("  cycles       : min %d, moyenne %d, max %d"
          % (cycles.min(), cycles.mean(), cycles.max()))
    print("  instructions : min %d, moyenne %d, max %d"
          % (insns.min(), insns.mean(), insns.max()))
    codeMisses = np.array([m["codeMisses"] for m in ys], dtype=float)
    print("  defauts de cache : moyenne %d (%d cycles), dont instructions %d"
          % (misses.mean(), misses.mean() * 12, codeMisses.mean()))
    fb = np.array([m["fb"] for m in ys], dtype=float)
    for label, per in (("3 cycles", 3), ("5 cycles", 5)):
        total = (cycles + fb * per).max()  # écritures réelles de chaque image
        print("  pire image + ecritures FB a %s : %d / %d = %.1f %%"
              % (label, total, FRAME_CYCLES, 100.0 * total / FRAME_CYCLES))

    print()
    err = (oldModel - cycles) / cycles
    print("Modele actuel (cost.h) : ecart moyen %+.1f %%, de %+.1f %% a "
          "%+.1f %%" % (100 * err.mean(), 100 * err.min(), 100 * err.max()))

    names = [f for f, _ in FITTED]
    A = np.array([[r[f] for f in names] for r in rows], dtype=float)
    # Moindres carrés à coûts positifs ou nuls : certains compteurs varient
    # presque ensemble (112 lignes par CPU et par image, une vingtaine de
    # tiles par ligne), et l'ajustement libre peut donner un coût négatif.
    # Un coût négatif est fixé à 0 et l'on ajuste à nouveau les autres.
    active = list(range(len(names)))
    while True:
        sub, *_ = np.linalg.lstsq(A[:, active], cycles, rcond=None)
        if (sub >= 0).all():
            break
        active = [j for j, k in zip(active, sub) if k >= 0]
    coef = np.zeros(len(names))
    coef[active] = sub
    pred = A @ coef
    err = (pred - cycles) / cycles
    print("Nouveau modele (moindres carres, couts >= 0) : ecart moyen %+.2f %%, de %+.2f %% a "
          "%+.2f %%, ecart-type %.2f %%"
          % (100 * err.mean(), 100 * err.min(), 100 * err.max(), 100 * err.std()))
    print()
    print("Couts unitaires ajustes (cycles) :")
    for (field, const), k in zip(FITTED, coef):
        print("  #define %-20s %d   /* ajuste : %.2f */" % (const, round(k), k))
    rounded = np.round(coef)
    err = (A @ rounded - cycles) / cycles
    print("Avec les valeurs arrondies : ecart de %+.2f %% a %+.2f %%"
          % (100 * err.min(), 100 * err.max()))


if __name__ == "__main__":
    main()
