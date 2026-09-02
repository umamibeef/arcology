/*  r_atlasdump -- load the extracted atlases and report what is in them.
 *
 *  This is the C side of the round-trip that tools/sc2kpack.py verifies on
 *  the Python side.  It prints a CRC32 over each atlas's index plane; the
 *  same number computed from the same PNG in Python is what proves the two
 *  readers agree.  Give it an id to also write that tile back out as a PNG.
 *
 *      sc2katlas <assets dir> [shap id [out.png]]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lodepng.h"
#include "r_atlas.h"

static int dump_tile(const RAtlas *a, const RAtlasLevel *l, int32_t id,
                     const char *out)
{
    const RTile   *t = r_atlas_tile(l, id);
    unsigned char *px;
    unsigned       err;
    int32_t        y, x;

    if (!t)
    {
        fprintf(stderr, "shape %d is not in the %d px set\n", (int) id,
                (int) l->zoom);
        return 1;
    }
    px = (unsigned char *) malloc((size_t) t->w * (size_t) t->h * 4u);
    if (!px)
        return 1;
    for (y = 0; y < (int32_t) t->h; ++y)
        for (x = 0; x < (int32_t) t->w; ++x)
        {
            size_t         s = ((size_t) (t->y + y) * (size_t) l->w + (size_t) (t->x + x));
            const uint8_t *c = a->palette[l->indices[s]];
            size_t         d = ((size_t) y * (size_t) t->w + (size_t) x) * 4u;
            px[d + 0] = c[0];
            px[d + 1] = c[1];
            px[d + 2] = c[2];
            px[d + 3] = c[3];
        }
    err = lodepng_encode32_file(out, px, (unsigned) t->w, (unsigned) t->h);
    free(px);
    if (err)
    {
        fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    printf("wrote %s  (shape %d, %ux%u, footprint %ux%u, rises %d px)\n", out,
           (int) id, t->w, t->h, t->foot, t->foot, (int) t->ay);
    return 0;
}

int r_atlas_main(int argc, char **argv)
{
    RAtlas  a;
    int     i;
    int32_t total = 0;

    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <assets dir> [shap id [out.png]]\n", argv[0]);
        return 2;
    }
    if (r_atlas_load(&a, argv[1]) != 0)
    {
        fprintf(stderr, "%s\n", a.err);
        return 1;
    }

    printf("%-6s %-11s %6s  %-10s  %-10s  %s\n", "zoom", "atlas", "tiles",
           "crc(index)", "crc(rgba)", "tile/alt");
    for (i = 0; i < a.n_levels; ++i)
    {
        const RAtlasLevel *l = &a.level[i];
        size_t             n = (size_t) l->w * (size_t) l->h;
        printf("%-6d %5dx%-5d %6d  %#010x  %#010x  %dx%d / %d\n", (int) l->zoom,
               (int) l->w, (int) l->h, (int) l->n_tiles,
               lodepng_crc32(l->indices, n),
               lodepng_crc32(l->rgba, n * 4u), (int) l->tile_w,
               (int) l->tile_h, (int) l->alt_step);
        total += l->n_tiles;
    }
    printf("%d tiles across %d levels\n", (int) total, a.n_levels);

    /*  The LOD ladder the renderer will use, printed so it can be eyeballed
     *  against the design before anything depends on it. */
    printf("\nscale -> art set\n");
    {
        static const float probe[] = {0.15f, 0.25f, 0.35f, 0.36f, 0.5f,
                                      0.70f, 0.71f, 1.0f,  2.0f,  4.0f};
        size_t             k;
        for (k = 0; k < sizeof probe / sizeof probe[0]; ++k)
        {
            const RAtlasLevel *l = r_atlas_level_for_scale(&a, probe[k]);
            printf("  %.2fx -> %d px%s\n", (double) probe[k],
                   l ? (int) l->zoom : 0,
                   (l && (double) probe[k] == (double) l->zoom / 32.0)
                       ? "  (native)"
                       : "");
        }
    }

    if (argc >= 3)
    {
        const RAtlasLevel *l = &a.level[a.n_levels - 1]; /* the 32 px set */
        int rc = dump_tile(&a, l, (int32_t) atoi(argv[2]),
                           argc >= 4 ? argv[3] : "tile.png");
        r_atlas_free(&a);
        return rc;
    }
    r_atlas_free(&a);
    return 0;
}
