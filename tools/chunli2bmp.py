#!/usr/bin/env python3
"""
Découpe l'image de la démo (deux plans sur un fond magenta) en deux BMP
8 bits indexés, à palette commune, pour tools/bmp2c.

    python3 tools/chunli2bmp.py demo/assets/source/chunli_stage.png demo/assets

Il faut Pillow (pip install pillow). Le script ne sert qu'à refaire les BMP
si l'image source change : les BMP produits sont dans le dépôt.

- Plan A (haut de l'image, 768 x 197) : son ciel magenta (#FF00FF) est
  transparent. Il est calé en bas d'un plan de 224 lignes : les 27 lignes
  du haut sont transparentes.
- Plan B (bas de l'image, 672 x 224) : opaque.
- Palette : index 0 = transparent (affiché en magenta s'il apparaissait
  à l'écran, ce qui ne doit pas arriver). Les autres couleurs suivent,
  triées, communes aux deux plans.
"""

import sys
from PIL import Image

TRANSPARENT = (255, 0, 255)
PLANE_HEIGHT = 224

# Position des plans dans l'image source (pixels), mesurée sur l'image :
# un cadre de 5 pixels, et une bande de séparation entre les deux plans.
PLANE_A_BOX = (5, 5, 773, 202)    # 768 x 197
PLANE_B_BOX = (5, 207, 677, 431)  # 672 x 224


def main():
    if len(sys.argv) != 3:
        sys.exit("usage : chunli2bmp.py IMAGE.png DOSSIER_SORTIE")
    source = Image.open(sys.argv[1]).convert("RGB")
    out = sys.argv[2]

    a = source.crop(PLANE_A_BOX)
    b = source.crop(PLANE_B_BOX)
    if b.height != PLANE_HEIGHT:
        sys.exit("plan B : %d lignes au lieu de %d" % (b.height, PLANE_HEIGHT))

    b_colors = {c for _, c in b.getcolors(1 << 24)}
    if TRANSPARENT in b_colors:
        sys.exit("plan B : contient la couleur transparente")

    colors = ({c for _, c in a.getcolors(1 << 24)} | b_colors) - {TRANSPARENT}
    palette = [TRANSPARENT] + sorted(colors)
    if len(palette) > 256:
        sys.exit("plus de 256 couleurs")
    index = {c: i for i, c in enumerate(palette)}

    flat = []
    for c in palette:
        flat.extend(c)
    flat.extend([0] * (768 - len(flat)))

    def indexed(img, height, top):
        """Image indexée de 'height' lignes, img posée à la ligne 'top'."""
        result = Image.new("P", (img.width, height), 0)
        result.putpalette(flat)
        px = result.load()
        src = img.load()
        for y in range(img.height):
            for x in range(img.width):
                px[x, y + top] = index[src[x, y]]
        return result

    indexed(a, PLANE_HEIGHT, PLANE_HEIGHT - a.height).save(out + "/plane_a.bmp")
    indexed(b, PLANE_HEIGHT, 0).save(out + "/plane_b.bmp")
    print("palette : %d couleurs (dont l'index 0 transparent)" % len(palette))


if __name__ == "__main__":
    main()
