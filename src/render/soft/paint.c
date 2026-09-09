/*  soft/paint.c -- putting the sweep's ops down as pixels.
 *
 *  soft.c decides what is drawn and in what order; this paints it, and
 *  owns the depth plane the 2.5D passes read and write.  Every rule here
 *  is the game's own, checked against it with tools/pixel_diff.py.
 */
#include "soft/soft.h"
#include "project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lodepng.h"
#include "tables.h"
#include "dump.h"

/*  The 2.5D passes.  g_pass is 0 for the original's single sweep, 1 while
 *  the terrain is painted (writes depth), 2 while everything else is
 *  painted (tested against it).  g_order is the painter's index of the
 *  tile that emitted the op being painted: strictly increasing along the
 *  original's sweep, so "depth" here means "drawn later in the sweep",
 *  which is the only depth the original ever had. */
static uint32_t *g_depth;
static int       g_pass;
static uint32_t  g_order;
static uint32_t  g_blocked; /* sprite pixels the depth plane rejected */

static void put(uint8_t *px, const uint8_t rgb[3])
{
    px[0] = rgb[0];
    px[1] = rgb[1];
    px[2] = rgb[2];
}

uint32_t soft_depth_blocked(void)
{
    return g_blocked;
}

static void paint_blit(RImage *im, const RAtlas *a, const RAtlasLevel *l, const ROp *op)
{
    const RTile *t = atlas_tile(l, op->shape);
    int32_t      yy, top = op->y, sx = op->x;
    int          flip = op->flip;

    if (!t)
        return;
    for (yy = 0; yy < (int32_t)t->h; ++yy)
    {
        int32_t        Y = top + yy;
        const uint8_t *src;
        uint8_t       *row;
        int32_t        xx;

        if (Y < 0 || Y >= im->h)
            continue;
        src = l->indices + ((size_t)t->y + (size_t)yy) * (size_t)l->w +
              (size_t)t->x;
        row = im->rgb + (size_t)Y * (size_t)im->w * 3u;
        for (xx = 0; xx < (int32_t)t->w; ++xx)
        {
            int32_t v = src[flip ? (int32_t)t->w - 1 - xx : xx];
            int32_t X;
            size_t  off;
            if (v == l->transparent)
                continue;
            X = sx + xx;
            if (X < 0 || X >= im->w)
                continue;
            off = (size_t)Y * (size_t)im->w + (size_t)X;
            if (op->stencil >= 0 && im->idx[off] != (uint8_t)op->stencil)
                continue;
            /*  Terrain owned by a tile later in the sweep is in front of
             *  this pixel, whichever pass is painting. */
            if (g_pass && g_depth[off] > g_order)
            {
                if (g_pass == 2)
                    ++g_blocked;
                continue;
            }
            put(row + (size_t)X * 3u, a->palette[v]);
            im->idx[off]  = (uint8_t)v;
            im->prov[off] = (uint16_t)(op->shape - l->id_base);
            if (g_pass == 1)
                g_depth[off] = g_order;
        }
    }
}

/*  $19B76: 019BF2  cmpi.w #$64, d0   ; below 100 -- leave it 019BFC  cmpi.w
 *  #$6E, d0   ; above 110 -- leave it 019C02  move.b #$6E, (a2) ; otherwise
 *  darken to 110 so it darkens open ground and does nothing to roads, water
 *  or roofs.  That is also why it is invisible to a blit-list oracle and to
 *  drawing a tile on a blank background: it depends on what is already
 *  there. */
static void paint_shadow(RImage *im, const RAtlas *a, const RAtlasLevel *l, const ROp *op)
{
    const RTile *t = atlas_tile(l, op->shape);
    int32_t      yy, top = op->y, sx = op->x;
    int          flip = op->flip;

    if (!t)
        return;
    for (yy = 0; yy < (int32_t)t->h; ++yy)
    {
        int32_t        Y = top + yy;
        const uint8_t *src;
        int32_t        xx;

        if (Y < 0 || Y >= im->h)
            continue;
        src = l->indices + ((size_t)t->y + (size_t)yy) * (size_t)l->w +
              (size_t)t->x;
        for (xx = 0; xx < (int32_t)t->w; ++xx)
        {
            size_t  off;
            int32_t X, v = src[flip ? (int32_t)t->w - 1 - xx : xx];
            if (v == l->transparent)
                continue;
            X = sx + xx;
            if (X < 0 || X >= im->w)
                continue;
            off = (size_t)Y * (size_t)im->w + (size_t)X;
            if (g_pass && g_depth[off] > g_order)
            {
                ++g_blocked;
                continue;
            }
            /*  $19BE2 tests two cases, not one: 79 becomes 84 before the
             *  dirt ramp is considered at all, and only then does
             *  100..110 collapse to 110.  Everything else is left. */
            if (im->idx[off] == 79u)
                v = 84;
            else if (im->idx[off] >= 100u && im->idx[off] <= 110u)
                v = 110;
            else
                continue;
            im->idx[off] = (uint8_t)v;
            im->prov[off] |= (uint16_t)R_PROV_SHADOW;
            put(im->rgb + off * 3u, a->palette[v]);
        }
    }
}

static void paint(RImage *im, const RAtlas *a, const RAtlasLevel *l, const ROp *op)
{
    g_order = op->order;
    if (op->kind == R_OP_SHADOW)
        paint_shadow(im, a, l, op);
    else
        paint_blit(im, a, l, op);
}

int soft_render(RImage *out, const RAtlas *a, const RCity *c, const RSoftOpts *o)
{
    int dump;
    ROpList            ops = {NULL, 0, 0};
    RSweep             sw;
    const RAtlasLevel *l;
    int32_t            W, H;
    size_t             k;
    int                mesh;

    memset(out, 0, sizeof *out);
    if (soft_sweep(a, c, o, &ops, &sw) != 0)
    {
        ops_free(&ops);
        return -1;
    }
    l = sw.level;
    W = sw.w;
    H = sw.h;

    out->w   = W;
    out->h   = H;
    out->rgb = (uint8_t *)malloc((size_t)W * (size_t)H * 3u);
    out->idx = (uint8_t *)malloc((size_t)W * (size_t)H);
    /*  calloc: 0 is "background", and the fill below paints no sprite. */
    out->prov = (uint16_t *)calloc((size_t)W * (size_t)H, sizeof(uint16_t));
    if (!out->rgb || !out->idx || !out->prov)
    {
        ops_free(&ops);
        return -1;
    }
    {
        /*  The underground view fills its rect white before drawing
         *  ($15452 _FillRect), not with the sky colour. */
        static const uint8_t white[3] = {255, 255, 255};
        const uint8_t       *bg       = o->underground ? white : o->sky;
        size_t               npx      = (size_t)W * (size_t)H;
        /*  The game's screen is 8 bits per pixel, so the background is a
         *  palette entry like everything else.  Snap it to the nearest one
         *  and paint BOTH planes from that: otherwise an indexed export
         *  disagrees with the RGB one, and the shadow pass -- which reads
         *  the index plane -- would be testing a number no pixel has. */
        int  pi, best = 0;
        long bestd = -1;
        for (pi = 0; pi < 256; ++pi)
        {
            long dr = (long)a->palette[pi][0] - bg[0];
            long dg = (long)a->palette[pi][1] - bg[1];
            long db = (long)a->palette[pi][2] - bg[2];
            long d  = dr * dr + dg * dg + db * db;
            if (bestd < 0 || d < bestd)
            {
                bestd = d;
                best  = pi;
            }
            if (!d)
                break;
        }
        memset(out->idx, (uint8_t)best, npx);
        for (k = 0; k < npx; ++k)
            put(out->rgb + k * 3u, a->palette[best]);
    }

    /*  Print the op list instead of painting it. */
    dump = o->dump_blits;
    if (dump)
    {
        /*  Report the top-left the game itself passes to $18E96, in the
         *  game's own origin, so that render_diff.py compares the anchor
         *  as well instead of having it cancel out on both sides.  A
         *  shadow is not a blit and the list records none. */
        for (k = 0; k < ops.n; ++k)
        {
            const ROp *op = &ops.v[k];
            if (op->kind == R_OP_BLIT)
                dumpf("%d %d %d %d %d %d\n", (int)op->row, (int)op->col, (int)op->shape, (int)(op->x - sw.ox), (int)(op->y - sw.oy), (int)op->flip);
        }
        ops_free(&ops);
        return 0;
    }

    /*  The underground view emits ground and buried pieces together per
     *  tile, so it has no terrain pass to split off; it keeps the single
     *  sweep. */
    mesh      = o->mesh && !o->underground;
    g_blocked = 0;
    free(g_depth);
    g_depth = NULL;
    if (!mesh)
    {
        g_pass = 0;
        for (k = 0; k < ops.n; ++k)
            paint(out, a, l, &ops.v[k]);
    }
    else
    {
        g_depth = (uint32_t *)calloc((size_t)W * (size_t)H,
                                     sizeof(uint32_t));
        if (!g_depth)
        {
            ops_free(&ops);
            return -1;
        }
        g_pass = 1;
        if (!o->mesh_reverse)
        {
            for (k = 0; k < ops.n; ++k)
                if (ops.v[k].terrain)
                    paint(out, a, l, &ops.v[k]);
        }
        else
        {
            /*  Tiles back to front, each tile's own ops in their order.
             *  The plane and not the loop carries the ordering, so this
             *  must change nothing. */
            size_t end = ops.n;
            while (end > 0)
            {
                size_t   start = end;
                uint32_t ord   = ops.v[end - 1].order;
                while (start > 0 && ops.v[start - 1].order == ord)
                    --start;
                for (k = start; k < end; ++k)
                    if (ops.v[k].terrain)
                        paint(out, a, l, &ops.v[k]);
                end = start;
            }
        }
        g_pass = 2;
        for (k = 0; k < ops.n; ++k)
            if (!ops.v[k].terrain)
                paint(out, a, l, &ops.v[k]);
    }
    g_pass = 0;
    ops_free(&ops);
    return 0;
}

/*  The depth plane of the last --mesh render as a 16-bit greyscale PNG:
 *  the painter's index of the tile owning each pixel, 0 where no terrain
 *  was drawn.  A diagnostic, not a picture. */
int soft_write_depth_png(const RImage *im, const char *path)
{
    LodePNGState   st;
    unsigned char *png = NULL, *raw;
    size_t         n = 0, npx = (size_t)im->w * (size_t)im->h, k;
    unsigned       err;

    if (!g_depth)
        return -1;
    raw = (unsigned char *)malloc(npx * 2u);
    if (!raw)
        return -1;
    for (k = 0; k < npx; ++k)
    {
        uint32_t v       = g_depth[k] > 0xFFFFu ? 0xFFFFu : g_depth[k];
        raw[k * 2u]      = (unsigned char)(v >> 8);
        raw[k * 2u + 1u] = (unsigned char)(v & 0xFFu);
    }
    lodepng_state_init(&st);
    st.info_raw.colortype       = LCT_GREY;
    st.info_raw.bitdepth        = 16;
    st.info_png.color.colortype = LCT_GREY;
    st.info_png.color.bitdepth  = 16;
    st.encoder.auto_convert     = 0;
    err                         = lodepng_encode(&png, &n, raw, (unsigned)im->w, (unsigned)im->h, &st);
    if (!err)
        err = lodepng_save_file(png, n, path);
    free(png);
    free(raw);
    lodepng_state_cleanup(&st);
    return err ? -1 : 0;
}
