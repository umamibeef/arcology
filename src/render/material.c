/*  material.c: the materials named at run time (material.h). */
#include "material.h"

#include <stdio.h>
#include <string.h>

#include "materials.h" /* GENERATED: the fixed material numbers, and the room after them */

static char  s_name[MAT_RUNTIME_MAX][32];
static float s_rgba[MAT_RUNTIME_MAX][4]; /* the color, and roughness in the fourth */
static float s_fine[MAT_RUNTIME_MAX];    /* the thinnest feature, in tiles, 0 for none */
static int   s_n;

int material_list(const float **out)
{
    *out = s_n ? s_rgba[0] : NULL;
    return s_n;
}

void material_fine(float mat, float width)
{
    int at = (int)(mat - MAT_RUNTIME_BASE + 0.5f);
    if (at >= 0 && at < s_n)
        s_fine[at] = width;
}

const float *material_fine_list(void)
{
    return s_fine;
}

float material_define(const char *name, const float rgb[3], float rough)
{
    int i, at = -1;
    for (i = 0; i < s_n; ++i)
        if (strcmp(s_name[i], name) == 0)
            at = i;
    if (at < 0)
    {
        if (s_n >= MAT_RUNTIME_MAX)
            return -1.0f;
        at = s_n++;
        snprintf(s_name[at], sizeof s_name[at], "%s", name);
    }
    for (i = 0; i < 3; ++i)
        s_rgba[at][i] = rgb[i];
    s_rgba[at][3] = rough;
    return MAT_RUNTIME_BASE + (float)at;
}
