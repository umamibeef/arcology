/*  sc2ksoft -- draw a city with the reference rasteriser.
 *
 *      sc2ksoft <assets dir> <city file> [out.png] [--zoom N] [--n N]
 *
 *  Prints the CRC32 of the raw RGB buffer.  That number, not the PNG's
 *  bytes, is what the Python check compares: two deflate implementations
 *  need not agree on compressed output for the pixels to be identical.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r_atlas.h"
#include "r_city.h"
#include "r_soft.h"

/*  Crop then magnify, in place, for the preview sheets: a 128x128 city at
 *  the 32 px art set is 4224x2468, so a detail has to be cut out and blown
 *  up with nearest-neighbour before it is legible at all.  Integer scale
 *  only -- this is pixel art and any interpolation is a lie about what the
 *  renderer produced. */
static int image_crop_scale(RImage *im, int cx, int cy, int cw, int chh,
                            int scale)
{
    uint8_t  *dst, *dsti;
    uint16_t *dstp;
    int       x, y, sx, sy;

    if (cw <= 0 || chh <= 0)
    {
        cx = cy = 0;
        cw = im->w;
        chh = im->h;
    }
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx + cw > im->w) cw = im->w - cx;
    if (cy + chh > im->h) chh = im->h - cy;
    if (cw <= 0 || chh <= 0 || scale < 1)
        return -1;

    {
        size_t npx = (size_t) cw * (size_t) chh * (size_t) scale *
                     (size_t) scale;
        dst  = (uint8_t *) malloc(npx * 3u);
        /*  The index plane travels with the colour plane, or an indexed
         *  export after a crop writes whatever was there before. */
        dsti = (uint8_t *) malloc(npx);
        dstp = (uint16_t *) malloc(npx * sizeof(uint16_t));
        if (!dst || !dsti || !dstp)
        {
            free(dst);
            free(dsti);
            free(dstp);
            return -1;
        }
    }
    for (y = 0; y < chh * scale; ++y)
    {
        sy = cy + y / scale;
        for (x = 0; x < cw * scale; ++x)
        {
            sx = cx + x / scale;
            {
                size_t d = (size_t) y * (size_t) cw * (size_t) scale +
                           (size_t) x;
                size_t s = (size_t) sy * (size_t) im->w + (size_t) sx;
                memcpy(dst + d * 3u, im->rgb + s * 3u, 3u);
                dsti[d] = im->idx[s];
                dstp[d] = im->prov[s];
            }
        }
    }
    free(im->rgb);
    free(im->idx);
    free(im->prov);
    im->rgb  = dst;
    im->idx  = dsti;
    im->prov = dstp;
    im->w   = cw * scale;
    im->h   = chh * scale;
    return 0;
}

int r_soft_main(int argc, char **argv)
{
    RAtlas    a;
    RCity    *c;
    RImage    im;
    RSoftOpts o;
    const char *out = NULL, *prov_out = NULL, *depth_out = NULL;
    int         i, rc = 0;
    int         cx = 0, cy = 0, cw = 0, ch = 0, scale = 1, indexed = 0;
    int         frow = -1, fcol = -1, fpad = 160, fdx = 0, fdy = 0;
    int         phase = 0;

    if (argc < 3)
    {
        fprintf(stderr,
                "usage: %s <assets dir> <city file> [out.png] "
                "[--zoom 8|16|32] [--n tiles] [--view 0..11] "
                "[--underground]\n       [--crop x,y,w,h] "
                "[--focus row,col,pad[,dx,dy]] "
                "[--scale N] [--phase N] "
                "[--indexed] [--provenance out16.png] [--mesh] [--mesh-reverse] [--depth out16.png]\n",
                argv[0]);
        return 2;
    }
    r_soft_defaults(&o);
    for (i = 3; i < argc; ++i)
    {
        if (strcmp(argv[i], "--zoom") == 0 && i + 1 < argc)
            o.zoom = atoi(argv[++i]);
        else if (strcmp(argv[i], "--n") == 0 && i + 1 < argc)
            o.n = atoi(argv[++i]);
        else if (strcmp(argv[i], "--view") == 0 && i + 1 < argc)
            o.view = atoi(argv[++i]);
        else if (strcmp(argv[i], "--underground") == 0)
            o.underground = 1;
        else if (strcmp(argv[i], "--no-things") == 0)
            o.draw_things = 0;
        else if (strcmp(argv[i], "--dump-blits") == 0)
            o.dump_blits = 1;
        else if (strcmp(argv[i], "--no-drop") == 0)
            o.no_drop = 1;
        else if (strcmp(argv[i], "--crop") == 0 && i + 1 < argc)
        {
            if (sscanf(argv[++i], "%d,%d,%d,%d", &cx, &cy, &cw, &ch) != 4)
            {
                fprintf(stderr, "--crop wants x,y,w,h\n");
                return 2;
            }
        }
        else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (strcmp(argv[i], "--provenance") == 0 && i + 1 < argc)
            prov_out = argv[++i];
        else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc)
            depth_out = argv[++i];
        else if (strcmp(argv[i], "--mesh") == 0)
            o.mesh = 1;
        else if (strcmp(argv[i], "--mesh-reverse") == 0)
            o.mesh = o.mesh_reverse = 1;
        else if (strcmp(argv[i], "--indexed") == 0)
            indexed = 1;
        else if (strcmp(argv[i], "--phase") == 0 && i + 1 < argc)
            phase = atoi(argv[++i]);
        else if (strcmp(argv[i], "--focus") == 0 && i + 1 < argc)
        {
            /*  row,col,pad and, optionally, an offset from that tile --
             *  a sprite is not always drawn on its own tile (an aircraft
             *  goes 120 px up), so the box has to be movable. */
            int got = sscanf(argv[++i], "%d,%d,%d,%d,%d", &frow, &fcol,
                             &fpad, &fdx, &fdy);
            if (got != 3 && got != 5)
            {
                fprintf(stderr, "--focus wants row,col,pad[,dx,dy]\n");
                return 2;
            }
            o.focus_row = frow;
            o.focus_col = fcol;
        }
        else if (argv[i][0] != '-')
            out = argv[i];
    }

    if (r_atlas_load(&a, argv[1]) != 0)
    {
        fprintf(stderr, "%s\n", a.err);
        return 1;
    }
    /*  _AnimatePalette rotates two runs of the palette -- 155..203 and
     *  224..238 -- and the game touches no pixels to do it.  Rotating them
     *  here is the whole of the water shimmer and the blinking lights. */
    if (phase)
        r_atlas_animate(&a, phase);

    c = (RCity *) malloc(sizeof *c); /* ~156 KB: not a stack object */
    if (!c)
    {
        r_atlas_free(&a);
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    if (r_city_load(c, argv[2]) != 0)
    {
        fprintf(stderr, "%s\n", c->err);
        free(c);
        r_atlas_free(&a);
        return 1;
    }
    if (r_soft_render(&im, &a, c, &o) != 0)
    {
        fprintf(stderr, "no %d px art set in the atlas\n", (int) o.zoom);
        free(c);
        r_atlas_free(&a);
        return 1;
    }

    if (o.dump_blits)
    {
        r_image_free(&im);
        free(c);
        r_atlas_free(&a);
        return 0;
    }
    printf("city      %s\n", c->name[0] ? c->name : "(unnamed)");
    printf("rotation  %d\n", (int) c->rotation);
    printf("zoom      %d px\n", (int) o.zoom);
    if (o.mesh)
        printf("mode      2.5D: terrain pass with a depth plane, then the sprites%s\n"
               "occluded  %u sprite pixels behind nearer terrain\n",
               o.mesh_reverse ? " (terrain drawn back to front)" : "",
               (unsigned) r_soft_depth_blocked());
    printf("image     %dx%d\n", (int) im.w, (int) im.h);
    printf("crc       %#010x\n", r_image_crc(&im));

    if (depth_out)
    {
        if (!o.mesh || r_soft_write_depth_png(&im, depth_out) != 0)
            fprintf(stderr, "--depth needs --mesh; nothing written\n");
        else
            printf("wrote     %s (depth, full frame)\n", depth_out);
    }

    /*  --focus wins over --crop: frame the box on the tile the renderer
     *  actually placed, so a sprite that stands well above its own tile
     *  still lands inside the picture. */
    if (frow >= 0)
    {
        int32_t fx, fy;
        if (!r_soft_focus_result(&fx, &fy))
        {
            fprintf(stderr, "--focus tile %d,%d never drawn\n", frow, fcol);
            r_image_free(&im);
            free(c);
            r_atlas_free(&a);
            return 2;
        }
        cx = (int) fx + fdx - fpad;
        cy = (int) fy + fdy - fpad;
        cw = ch = fpad * 2;
        if (cx < 0) cx = 0;
        if (cy < 0) cy = 0;
        printf("focus     tile %d,%d at %d,%d\n", frow, fcol, (int) fx,
               (int) fy);
    }

    if ((cw > 0 && ch > 0) || scale > 1)
    {
        if (image_crop_scale(&im, cx, cy, cw, ch, scale) != 0)
        {
            fprintf(stderr, "bad --crop/--scale for a %dx%d image\n",
                    (int) im.w, (int) im.h);
            r_image_free(&im);
            free(c);
            r_atlas_free(&a);
            return 2;
        }
        printf("cropped   %dx%d\n", (int) im.w, (int) im.h);
    }

    if (out && (indexed ? r_image_write_png_indices(&im, &a, out)
                        : r_image_write_png(&im, out)) != 0)
    {
        fprintf(stderr, "cannot write %s\n", out);
        rc = 1;
    }
    else if (out)
        printf("wrote     %s\n", out);
    if (prov_out && r_image_write_png_provenance(&im, prov_out) != 0)
    {
        fprintf(stderr, "cannot write %s\n", prov_out);
        rc = 1;
    }
    else if (prov_out)
        printf("wrote     %s (provenance)\n", prov_out);

    r_image_free(&im);
    free(c);
    r_atlas_free(&a);
    return rc;
}
