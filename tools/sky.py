#!/usr/bin/env python3
"""Fabrique le décor de fond de la démo : demo/assets/sky.bmp.

Usage : python3 tools/sky.py demo/assets/sky.bmp

Une image originale, calculée (Pillow) : ciel en dégradé tramé, nuages,
montagnes lointaines, collines, et le sol jusqu'en bas. 512 x 112 pixels,
256 couleurs. Elle boucle horizontalement : toutes les formes sont
périodiques sur 512 pixels (sinus de périodes entières, nuages dessinés
aussi de l'autre côté du bord).

La ligne y de l'image est la ligne y de l'écran. L'horizon de la route
(world.h, SCREEN_CENTER_Y) est vers la ligne 96 : le sol commence à
GROUND_Y. Le moteur prolonge le sol sous l'image avec la couleur de son
premier pixel en bas à gauche.

Palette : index 1 à 63 ; l'index 0 n'est pas utilisé, et les index 240 et
au-delà sont réservés aux couleurs de la route (race/render.h).
"""

import math
import sys

from PIL import Image

WIDTH, HEIGHT = 512, 112
GROUND_Y = 98

# ---- Palette ----------------------------------------------------------------

palette = [(0, 0, 0)]


def color(rgb):
    palette.append(rgb)
    return len(palette) - 1


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


SKY_TOP, SKY_LOW = (40, 88, 200), (168, 208, 248)
SKY_BANDS = 16
sky = [color(lerp(SKY_TOP, SKY_LOW, i / (SKY_BANDS - 1))) for i in range(SKY_BANDS)]
cloud_light = color((248, 248, 248))
cloud_mid = color((224, 228, 236))
cloud_shadow = color((192, 200, 216))
far_lit = color((120, 128, 176))
far_shade = color((96, 100, 152))
snow = color((232, 236, 248))
hill_lit = color((40, 120, 56))
hill_shade = color((28, 96, 44))
ground = color((16, 112, 32))
assert len(palette) < 240

# ---- Dessin -----------------------------------------------------------------

img = Image.new("P", (WIDTH, HEIGHT), 0)
px = img.load()

# Tramage ordonné 4 x 4 (Bayer), pour fondre les bandes du ciel
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]

for y in range(GROUND_Y):
    t = y / (GROUND_Y - 1) * (SKY_BANDS - 1)
    band = int(t)
    frac = t - band
    for x in range(WIDTH):
        b = band + (1 if frac * 16 > BAYER[y & 3][x & 3] + 0.5 else 0)
        px[x, y] = sky[min(b, SKY_BANDS - 1)]


def ridge(x, base, waves):
    """Hauteur d'une crête : somme de sinus de périodes entières sur 512."""
    return base - sum(a * math.sin(2 * math.pi * k * x / WIDTH + p) for a, k, p in waves)


# Nuages : des disques réunis ; dessinés aussi décalés de +-512 (bouclage)
clouds = [(60, 30, 26), (190, 18, 20), (330, 40, 30), (450, 24, 18)]
for cx, cy, size in clouds:
    blobs = [(cx + dx * size // 10, cy + dy * size // 20, r * size // 10)
             for dx, dy, r in [(-12, 2, 6), (-5, -2, 8), (4, -3, 9), (12, 1, 6), (0, 3, 7)]]
    for x in range(WIDTH):
        for y in range(GROUND_Y):
            for shift in (-WIDTH, 0, WIDTH):
                inside = [(y - by) for bx, by, r in blobs
                          if (x - bx - shift) ** 2 + ((y - by) * 2) ** 2 <= r * r]
                if inside:
                    low = max(inside)
                    px[x, y] = cloud_shadow if low > size // 4 else (
                        cloud_mid if low > 0 else cloud_light)
                    break

far_waves = [(14, 2, 0.3), (8, 5, 1.7), (4, 11, 0.9), (2, 23, 2.2)]
hill_waves = [(5, 3, 1.1), (3, 7, 0.2), (2, 13, 2.8)]
for x in range(WIDTH):
    top = ridge(x, 72, far_waves)
    slope = ridge(x + 1, 72, far_waves) - top
    for y in range(int(math.ceil(top)), GROUND_Y):
        if y < top + 4 and top < 66:
            px[x, y] = snow
        else:
            px[x, y] = far_shade if slope > 0 else far_lit
    htop = ridge(x, 90, hill_waves)
    hslope = ridge(x + 1, 90, hill_waves) - htop
    for y in range(int(math.ceil(htop)), GROUND_Y):
        px[x, y] = hill_shade if hslope > 0 else hill_lit
    for y in range(GROUND_Y, HEIGHT):
        px[x, y] = ground

flat = []
for rgb in palette + [(0, 0, 0)] * (256 - len(palette)):
    flat.extend(rgb)
img.putpalette(flat)
img.save(sys.argv[1])
print(f"{sys.argv[1]} : {WIDTH} x {HEIGHT}, {len(palette)} couleurs")
