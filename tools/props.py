#!/usr/bin/env python3
"""Fabrique les sprites des objets du bord de la route (images originales,
calculées) : demo/assets/props/*.bmp.

Usage : python3 tools/props.py demo/assets/props

Trois objets, chacun en plusieurs tailles (« mipmaps ») : la taille
pleine, puis la moitié, puis le quart. Le moteur part de la plus petite
taille au moins aussi grande que ce qu'il doit afficher, et la réduit en
omettant des lignes et des colonnes (ou l'agrandit en les répétant, pour
la taille pleine).
- pneus : une pile de pneus, 32 x 32 ;
- panneau : un panneau publicitaire sur deux poteaux, 96 x 48 ;
- palmier : 48 x 96.

Format Mega Drive : 16 couleurs par palette, l'index 0 transparent. Les
trois objets partagent une seule palette de 15 couleurs (une ligne de
palette de la Mega Drive), aux niveaux que la Mega Drive sait afficher
(3 bits par composante). Les BMP sont en 8 bits indexés (index 0 à 15)
pour tools/bmp2c ; l'index 0 y est en magenta, pour le voir.
Les tailles réduites sont calculées depuis la taille pleine, par blocs
de 2 x 2 : la couleur la plus fréquente du bloc ; un bloc à moitié
transparent ou plus devient transparent.
"""

import math
import os
import sys

from PIL import Image

# ---- Palette (niveaux de la Mega Drive : 0, 36, 73, 109, 146, 182, 219, 255)

L = [0, 36, 73, 109, 146, 182, 219, 255]


def md(r, g, b):
    return (L[r], L[g], L[b])


PALETTE = [
    (255, 0, 255),   # 0 transparent
    md(0, 0, 0),     # 1 noir
    md(2, 2, 2),     # 2 gris foncé
    md(4, 4, 4),     # 3 gris
    md(6, 6, 6),     # 4 gris clair
    md(7, 7, 7),     # 5 blanc
    md(7, 1, 1),     # 6 rouge
    md(7, 6, 1),     # 7 jaune
    md(1, 3, 7),     # 8 bleu
    md(0, 1, 4),     # 9 bleu nuit
    md(2, 1, 0),     # 10 brun foncé
    md(4, 2, 1),     # 11 brun
    md(6, 5, 3),     # 12 sable
    md(0, 2, 0),     # 13 vert foncé
    md(1, 4, 1),     # 14 vert
    md(3, 6, 2),     # 15 vert clair
]
T, BLACK, DGREY, GREY, LGREY, WHITE, RED, YELLOW, BLUE, NAVY, DBROWN, BROWN, \
    SAND, DGREEN, GREEN, LGREEN = range(16)


def new(w, h):
    return [[T] * w for _ in range(h)]


def put(img, x, y, c):
    if 0 <= y < len(img) and 0 <= x < len(img[0]):
        img[y][x] = c


# ---- Pile de pneus : vue un peu plongeante, le dessus du pneu du haut, puis
# quatre pneus ; le deuxième et le quatrième peints (rouge, blanc) ---------

def tires():
    w, h = 32, 32
    img = new(w, h)
    cx = 15.5
    # dessus du pneu du haut : un anneau gris, et le trou noir au milieu
    for y in range(0, 8):
        for x in range(w):
            dx, dy = (x - cx) / 14.5, (y - 3.5) / 3.8
            if dx * dx + dy * dy <= 1:
                hx, hy = (x - cx) / 7.5, (y - 3.8) / 2.0
                if hx * hx + hy * hy <= 1:
                    c = BLACK
                else:
                    c = LGREY if (y <= 2 and x < cx) else (GREY if y <= 4 else DGREY)
                img[y][x] = c
    # quatre pneus de 6 pixels de haut, le deuxième et le quatrième peints ;
    # leurs bords suivent la courbure du dessus (vue plongeante)
    band = 6
    paints = [None, RED, None, WHITE]
    for x in range(1, w - 1):
        dx = (x - cx) / 14.5
        curve = int(round(3 * math.sqrt(max(0.0, 1 - dx * dx))))
        for t in range(4):
            top = 4 + curve + t * band
            for y in range(band):
                if y == band - 1 and x in (1, w - 2):
                    continue                       # coins arrondis
                c = BLACK
                if y == 0:
                    c = DGREY                      # arête du dessus
                elif y == 2 and x % 3 == 0 and 4 < x < w - 4:
                    c = DGREY                      # sculptures
                if x <= 3 and c == BLACK:
                    c = DGREY                      # côté éclairé
                paint = paints[t]
                if paint and y in (2, 3):
                    c = paint
                    if paint == WHITE and x >= w - 5:
                        c = LGREY                  # côté à l'ombre
                put(img, x, top + y, c)
    return img


# ---- Panneau publicitaire : cadre, fond bleu, bande jaune, « TURBO » ------

FONT5x7 = {
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
}


def billboard():
    w, h = 96, 48
    img = new(w, h)
    panel_h = 36
    # poteaux
    for px in (14, w - 18):
        for y in range(panel_h, h):
            for x in range(px, px + 4):
                put(img, x, y, GREY if x == px else DGREY)
    # cadre et fond
    for y in range(panel_h):
        for x in range(w):
            edge = y in (0, panel_h - 1) or x in (0, w - 1)
            inner = y in (1, panel_h - 2) or x in (1, w - 2)
            c = LGREY if edge else (GREY if inner else BLUE)
            if not edge and not inner and y > panel_h - 9:
                c = NAVY                       # bas du fond, plus sombre
            img[y][x] = c
    # bande jaune en diagonale à droite
    for y in range(2, panel_h - 2):
        for x in range(w - 30 + y // 2, w - 18 + y // 2):
            if 2 <= x < w - 2:
                img[y][x] = YELLOW
    # texte, en lettres de 5 x 7 doublées, avec une ombre
    text = "TURBO"
    x0, y0 = 8, 11
    for i, ch in enumerate(text):
        for gy, row in enumerate(FONT5x7[ch]):
            for gx, bit in enumerate(row):
                if bit == "1":
                    for dy in range(2):
                        for dx in range(2):
                            x = x0 + i * 12 + gx * 2 + dx
                            y = y0 + gy * 2 + dy
                            put(img, x + 1, y + 1, NAVY)
                            put(img, x, y, WHITE)
    # liseré rouge sous le texte
    for x in range(8, 64):
        put(img, x, 28, RED)
        put(img, x, 29, RED)
    return img


# ---- Palmier : tronc courbe annelé, palmes retombantes, noix de coco ------

def palm():
    w, h = 48, 96
    img = new(w, h)
    top_y = 30

    def trunk_x(y):  # le tronc penche un peu vers la droite en montant
        t = (h - 1 - y) / (h - 1 - top_y)
        return 20 + 6 * t * t

    for y in range(top_y, h):
        t = (h - 1 - y) / (h - 1 - top_y)
        half = 3.5 - 1.5 * t            # plus fin en haut
        cx = trunk_x(y)
        for x in range(int(cx - half), int(math.ceil(cx + half)) + 1):
            if abs(x - cx) <= half:
                ring = (y // 3) % 2 == 0
                rel = (x - (cx - half)) / (2 * half)
                c = SAND if rel < 0.35 else (BROWN if rel < 0.8 else DBROWN)
                if ring and c != DBROWN:
                    c = BROWN if c == SAND else DBROWN
                put(img, x, y, c)

    # palmes : depuis le sommet, des courbes qui montent puis retombent
    cx, cy = trunk_x(top_y), top_y
    fronds = [(-170, 23), (-140, 24), (-110, 18), (-75, 18), (-45, 24),
              (-12, 23), (-200, 20), (22, 19)]
    for angle, length in fronds:
        a = math.radians(angle)
        for i in range(length * 3):
            s = i / (length * 3)
            # direction de départ, puis la gravité courbe la palme
            x = cx + math.cos(a) * s * length
            y = cy + math.sin(a) * s * length + 17 * s * s
            thick = 2.4 * (1 - s) + 0.6
            for k in range(-3, 4):
                ky = y + k * 0.5
                if abs(k * 0.5) <= thick:
                    c = GREEN if k < 0 else DGREEN
                    if k <= -2:
                        c = LGREEN
                    put(img, int(round(x)), int(round(ky)), c)
            # folioles : de petits traits vers le bas, une fois sur deux
            if i % 4 == 0 and 0.15 < s < 0.95:
                for d in range(1, 4):
                    put(img, int(round(x)) + (1 if math.cos(a) > 0 else -1) * (d // 2),
                        int(round(y + thick + d)), DGREEN)
    # noix de coco sous la couronne
    for ox, oy in ((-3, 3), (1, 4), (4, 2), (-1, 6)):
        for dy in range(3):
            for dx in range(3):
                if (dx, dy) not in ((0, 0), (2, 0), (0, 2), (2, 2)):
                    put(img, int(cx) + ox + dx, cy + oy + dy,
                        BROWN if (dx, dy) == (1, 0) else DBROWN)
    return img


# ---- Tailles réduites -------------------------------------------------------

def half(img):
    h, w = len(img), len(img[0])
    out = new(w // 2, h // 2)
    for y in range(h // 2):
        for x in range(w // 2):
            block = [img[2 * y + j][2 * x + i] for j in range(2) for i in range(2)]
            opaque = [c for c in block if c != T]
            if len(opaque) < 3:   # à moitié transparent ou plus
                continue
            # la couleur la plus fréquente du bloc (en pixel art, une
            # moyenne inventerait des teintes, comme du brun entre le
            # rouge et le noir) ; à égalité, la plus proche de la moyenne
            counts = {c: opaque.count(c) for c in opaque}
            top = max(counts.values())
            ties = [c for c in counts if counts[c] == top]
            rgb = [sum(PALETTE[c][k] for c in opaque) / len(opaque) for k in range(3)]
            out[y][x] = min(ties, key=lambda c: sum((PALETTE[c][k] - rgb[k]) ** 2
                                                     for k in range(3)))
    return out


def save(img, path):
    h, w = len(img), len(img[0])
    im = Image.new("P", (w, h))
    im.putdata([c for row in img for c in row])
    flat = []
    for rgb in PALETTE + [(0, 0, 0)] * (256 - len(PALETTE)):
        flat.extend(rgb)
    im.putpalette(flat)
    im.save(path)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    made = []
    for name, maker, levels in (("tires", tires, 3), ("billboard", billboard, 3),
                                ("palm", palm, 3)):
        img = maker()
        for lv in range(levels):
            h, w = len(img), len(img[0])
            path = os.path.join(out, "%s_%d.bmp" % (name, lv))
            save(img, path)
            made.append((path, w, h))
            img = half(img)
    for path, w, h in made:
        print("%s : %d x %d" % (path, w, h))


if __name__ == "__main__":
    main()
