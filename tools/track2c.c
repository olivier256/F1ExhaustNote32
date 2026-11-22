/* =========================================================================
   track2c : convertit la description d'un circuit en données C pour le
   moteur (voir race/world.h).

   Usage : track2c DESCRIPTION SORTIE NOM
     Produit SORTIE.h et SORTIE.c, qui définissent 'const Track NOM'.

   La description est un fichier texte, une section par ligne :
       # commentaire
       road  ENTREE TENUE SORTIE  COURBE  DENIVELE
   - ENTREE, TENUE, SORTIE : des nombres de segments. La courbure passe de
     0 à COURBE pendant ENTREE segments (en p^2), reste à COURBE pendant
     TENUE, puis revient à 0 pendant SORTIE (en cosinus) ;
   - COURBE : en unités du monde (décimales permises), positive vers la
     droite. Échelle : 2 doux, 4 moyen, 6 serré ;
   - DENIVELE : ce que la route gagne en hauteur sur toute la section, en
     unités du monde (négatif : elle descend), réparti en cosinus.
   C'est la méthode de Jake Gordon (« How to build a racing game »,
   addRoad). Le circuit boucle : il doit revenir à la hauteur 0, où il
   commence.

   Outil de l'hôte : il calcule en flottants (cos), le moteur ne reçoit
   que des entiers.
   ========================================================================= */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "world.h"

#define MAX_SEGMENTS 65535
#define PI 3.14159265358979323846 /* M_PI n'est pas dans C99 */

static double heights[MAX_SEGMENTS + 1]; /* début de chaque segment */
static double curves[MAX_SEGMENTS];
static int count = 0;

static void fail(const char *fmt, const char *arg, int line) {
    fprintf(stderr, "track2c : ");
    fprintf(stderr, fmt, arg);
    if (line > 0)
        fprintf(stderr, " (ligne %d)", line);
    fprintf(stderr, "\n");
    exit(1);
}

static double easeIn(double a, double b, double p) {
    return a + (b - a) * p * p;
}

static double easeInOut(double a, double b, double p) {
    return a + (b - a) * (-cos(p * PI) / 2 + 0.5);
}

static void addSegment(double curve, double endHeight, const char *path,
                       int line) {
    if (count == MAX_SEGMENTS)
        fail("%s : trop de segments", path, line);
    curves[count] = curve;
    heights[count + 1] = endHeight;
    count++;
}

static void addRoad(int enter, int hold, int leave, double curve, double rise,
                    const char *path, int line) {
    const double startY = heights[count];
    const double endY = startY + rise;
    const double total = enter + hold + leave;
    int n;
    for (n = 0; n < enter; n++)
        addSegment(easeIn(0, curve, (double)n / enter),
                   easeInOut(startY, endY, (n + 1) / total), path, line);
    for (n = 0; n < hold; n++)
        addSegment(curve, easeInOut(startY, endY, (enter + n + 1) / total),
                   path, line);
    for (n = 0; n < leave; n++)
        addSegment(easeInOut(curve, 0, (double)n / leave),
                   easeInOut(startY, endY, (enter + hold + n + 1) / total),
                   path, line);
}

static void readDescription(const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL)
        fail("impossible d'ouvrir %s", path, 0);

    char text[512];
    int line = 0;
    heights[0] = 0;
    while (fgets(text, sizeof text, f) != NULL) {
        line++;
        char *hash = strchr(text, '#');
        if (hash != NULL)
            *hash = '\0';
        char kind[16];
        int enter, hold, leave;
        double curve, rise;
        int n = sscanf(text, "%15s %d %d %d %lf %lf", kind, &enter, &hold,
                       &leave, &curve, &rise);
        if (n <= 0)
            continue;
        if (n != 6 || strcmp(kind, "road") != 0)
            fail("%s : 'road ENTREE TENUE SORTIE COURBE DENIVELE' attendu",
                 path, line);
        if (enter < 0 || hold < 0 || leave < 0 || enter + hold + leave == 0)
            fail("%s : longueurs invalides", path, line);
        addRoad(enter, hold, leave, curve, rise, path, line);
    }
    fclose(f);

    if (count <= DRAW_DISTANCE)
        fail("%s : circuit plus court que la distance de vue", path, 0);
    if (fabs(heights[count]) > 0.5)
        fail("%s : le circuit ne revient pas à la hauteur 0", path, 0);
}

static void writeOutput(const char *base, const char *path, const char *name) {
    char file[1024];

    snprintf(file, sizeof file, "%s.h", base);
    FILE *h = fopen(file, "w");
    if (h == NULL)
        fail("impossible de créer %s", file, 0);
    fprintf(h, "/* Généré par track2c depuis %s : ne pas modifier. */\n\n",
            path);
    fprintf(h, "#ifndef DEMO_TRACK_H\n#define DEMO_TRACK_H\n\n");
    fprintf(h, "#include \"world.h\"\n\nextern const Track %s;\n\n#endif\n",
            name);
    fclose(h);

    snprintf(file, sizeof file, "%s.c", base);
    FILE *c = fopen(file, "w");
    if (c == NULL)
        fail("impossible de créer %s", file, 0);
    const char *slash = strrchr(base, '/');
    fprintf(c, "/* Généré par track2c depuis %s : ne pas modifier. */\n\n",
            path);
    fprintf(c, "#include \"%s.h\"\n\n", slash ? slash + 1 : base);
    fprintf(c, "/* hauteur, courbure (1/256), flags */\n");
    fprintf(c, "static const Segment segments[%d] = {\n", count);
    for (int i = 0; i < count; i++) {
        const long y = lround(heights[i]);
        const long curve = lround(curves[i] * 256);
        const int dark = (i / RUMBLE_SEGMENTS) % 2;
        fprintf(c, "    {%ld, %ld, %s, 0},\n", y, curve,
                dark ? "SEGMENT_DARK" : "0");
    }
    fprintf(c, "};\n\nconst Track %s = {%d, segments};\n", name, count);
    fclose(c);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "usage : track2c DESCRIPTION SORTIE NOM  (produit "
                        "SORTIE.h et SORTIE.c)\n");
        return 1;
    }
    readDescription(argv[1]);
    writeOutput(argv[2], argv[1], argv[3]);
    fprintf(stderr, "track2c : %d segments (%d unités) -> %s.c / %s.h\n", count,
            count * SEGMENT_LENGTH, argv[2], argv[2]);
    return 0;
}
