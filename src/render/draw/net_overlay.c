/*  draw/net_overlay.c: THE LANE GRAPH IN THE WORLD, laid over the bare
 *  ground while the outline view is on.  The picture is the lane graph's
 *  layout (net_graph.c), the same one --net-svg writes.  Each line is a
 *  flat ribbon and each arrow a flat triangle.
 *
 *  THE LAYERS.  Two faces in one plane at one painter's slot fight for the
 *  same pixels.  So each mark takes the lowest layer that no other mark on
 *  its cells holds yet.  Two marks that share a cell never share a layer.  A
 *  face's slot is a float that holds the tile's painter order and the
 *  fraction both.  The order is about the map's side times the tile's column
 *  and row.  A float keeps the fraction there in steps of a power of two.  A
 *  face also takes a marking's step (decal_slot) over its slot.  So a layer
 *  is a whole number of those float steps, and two marking steps at least.
 *  Then no face of one layer can reach the next.  The layers fill a tile's
 *  fraction, and the first round of them lies LIFT over the drawn surface.  A
 *  city can need more layers than that.  Then each further round of them is
 *  lifted clear of the last, by more than two faces count as one plane by
 *  (coplanar_z). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "geo.h"
#include "log.h"
#include "material.h"
#include "mesh/shapes.h"
#include "mesh/surface.h"
#include "net_graph.h"
#include "net_overlay.h"
#include "network/network.h"

#define LIFT   0.02f /* how far over the drawn surface the lowest round of layers lies */
#define DRAPE  0.25f /* the longest straight laid without taking the ground again */
#define REACH  0.05f /* how far past its line a mark claims cells */
#define DENSE  0.1f  /* the step a mark's cells are found at */
#define MITER  2.0f  /* the most a corner widens a ribbon by */
#define LAYERS 256

typedef struct
{
    int32_t  cell;
    int      stamp; /* the mark that claimed it last */
    uint64_t held[LAYERS / 64];
} Held;

typedef struct
{
    RMesh       *m;
    const RCity *c;
    uint8_t      mask_bit;
    float        mat[NG_COLORS];
    float       *slots;
    int          nslot, most;
    float        coplanar;
    Held        *held;
    int          nheld, cap, stamp;
    float        far;
    int32_t     *claimed;
    int          nclaimed, cclaimed;
} Overlay;

/*  The cells each mark claims, in an open table: the layers held there.
 *  The table grows only to take a cell it does not hold, and a grown
 *  table moves.  So a mark keeps its cells by their number. */
static uint32_t cell_hash(int32_t cell)
{
    uint32_t h = (uint32_t)cell;
    h ^= h >> 16, h *= 0x7feb352du;
    h ^= h >> 15, h *= 0x846ca68bu;
    return h ^ (h >> 16);
}

static Held *held_find(const Overlay *o, int32_t cell, uint32_t *slot)
{
    uint32_t mask = (uint32_t)(o->cap - 1), h;
    for (h = cell_hash(cell) & mask; o->held[h].cell >= 0; h = (h + 1) & mask)
        if (o->held[h].cell == cell)
            return &o->held[h];
    *slot = h;
    return NULL;
}

static Held *held_at(Overlay *o, int32_t cell)
{
    uint32_t at = 0;
    Held    *h  = o->cap ? held_find(o, cell, &at) : NULL;
    if (h)
        return h;
    if (o->nheld * 2 >= o->cap)
    {
        int   nc = o->cap ? o->cap * 2 : 4096, oc = o->cap, i;
        Held *old = o->held, *nh = (Held *)calloc((size_t)nc, sizeof *nh);
        if (!nh)
            return NULL;
        for (i = 0; i < nc; i++)
            nh[i].cell = -1;
        o->held = nh, o->cap = nc;
        for (i = 0; i < oc; i++)
            if (old[i].cell >= 0)
            {
                uint32_t k = 0;
                held_find(o, old[i].cell, &k);
                o->held[k] = old[i];
            }
        free(old);
        held_find(o, cell, &at);
    }
    o->held[at].cell  = cell;
    o->held[at].stamp = 0;
    memset(o->held[at].held, 0, sizeof o->held[at].held);
    o->nheld++;
    return &o->held[at];
}

/*  The cells a mark of `n` points claims, `half` wide: every cell its line
 *  or its width reaches, each once.  0, or -1 out of memory. */
static int claim(Overlay *o, const NgP *p, int n, float half)
{
    float reach = half + REACH;
    int   i, s, d;
    o->stamp++, o->nclaimed = 0;
    for (i = 0; i < n; i++)
    {
        NgP a = p[i], b = i + 1 < n ? p[i + 1] : p[i];
        int m = (int)ceilf(hypotf(b.x - a.x, b.y - a.y) / DENSE);
        if (m < 1)
            m = 1;
        for (s = 0; s <= m; s++)
        {
            float x = a.x + (b.x - a.x) * (float)s / (float)m, y = a.y + (b.y - a.y) * (float)s / (float)m;
            for (d = 0; d < 5; d++)
            {
                float dx = d == 1 ? reach : d == 2 ? -reach
                                                   : 0.0f,
                      dy = d == 3 ? reach : d == 4 ? -reach
                                                   : 0.0f;
                Held *h  = held_at(o, (int32_t)floorf(y + dy) * 65536 + (int32_t)floorf(x + dx));
                if (!h)
                    return -1;
                if (h->stamp == o->stamp)
                    continue;
                if (o->nclaimed >= o->cclaimed)
                {
                    int      nc = o->cclaimed ? o->cclaimed * 2 : 1024;
                    int32_t *nh = (int32_t *)realloc(o->claimed, sizeof *nh * (size_t)nc);
                    if (!nh)
                        return -1;
                    o->claimed = nh, o->cclaimed = nc;
                }
                h->stamp                  = o->stamp;
                o->claimed[o->nclaimed++] = h->cell;
            }
        }
    }
    return 0;
}

/*  The lowest layer no mark on these cells holds, taken.  Answers its
 *  slot and its lift, or -1 out of memory. */
static int layer(Overlay *o, const NgP *p, int n, float half, float *slot, float *lift)
{
    int      i, k, w;
    uint32_t at;
    uint64_t used[LAYERS / 64] = {0};
    if (claim(o, p, n, half) != 0)
        return -1;
    /*  The layers any of its cells holds, and the lowest free of them. */
    for (i = 0; i < o->nclaimed; i++)
    {
        const Held *h = held_find(o, o->claimed[i], &at);
        for (w = 0; w < LAYERS / 64; w++)
            used[w] |= h->held[w];
    }
    for (k = 0; k < LAYERS - 1 && (used[k / 64] >> (k % 64) & 1u); k++)
        ;
    for (i = 0; i < o->nclaimed; i++)
        held_find(o, o->claimed[i], &at)->held[k / 64] |= (uint64_t)1 << (k % 64);
    if (k + 1 > o->most)
        o->most = k + 1;
    *slot = o->slots[k % o->nslot];
    *lift = LIFT + (float)(k / o->nslot) * 2.0f * o->coplanar;
    return 0;
}

/*  One flat triangle, its face turned up, `lift` over the drawn surface.
 *  It is cut where the ground folds, so it never passes through it. */
static int tri(Overlay *o, float slot, float lift, int color, const float a[3], const float b[3], const float c[3])
{
    float t[3][3], col[3] = {0.0f, 0.0f, o->mat[color]};
    int   up = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]) > 0.0f;
    memcpy(t[0], a, sizeof t[0]);
    memcpy(t[1], up ? b : c, sizeof t[1]);
    memcpy(t[2], up ? c : b, sizeof t[2]);
    return put_tri_over_ground_at(__FILE__, __func__, o->m, o->c, o->mask_bit, slot, (const float (*)[3])t, col, lift);
}

static void ground(Overlay *o, NgP p, float lift, float out[3])
{
    out[0] = p.x, out[1] = p.y;
    out[2] = surface_at_world(o->c, o->mask_bit, p.x, p.y) + lift;
}

/*  The side of a ribbon at point i.  It is the miter of the two straights
 *  that meet there, and MITER is the most it widens.  A line that ends
 *  where it begins is mitered there too. */
static NgP side_at(const NgP *p, int n, int i)
{
    int   closed = n > 2 && p[0].x == p[n - 1].x && p[0].y == p[n - 1].y;
    int   pi     = i > 0 ? i - 1 : closed ? n - 2
                                          : -1,
          ni     = i + 1 < n ? i + 1 : closed ? 1
                                              : -1;
    NgP   a = {0, 0}, b = {0, 0}, s;
    float l, k;
    if (pi >= 0)
    {
        float dx = p[i].x - p[pi].x, dy = p[i].y - p[pi].y;
        l = hypotf(dx, dy), a = l > 1e-9f ? (NgP){dy / l, -dx / l} : a;
    }
    if (ni >= 0)
    {
        float dx = p[ni].x - p[i].x, dy = p[ni].y - p[i].y;
        l = hypotf(dx, dy), b = l > 1e-9f ? (NgP){dy / l, -dx / l} : b;
    }
    if (pi < 0)
        return b;
    if (ni < 0)
        return a;
    k = 1.0f + a.x * b.x + a.y * b.y;
    if (k < 1.0f / (MITER * MITER) * 2.0f)
        k = 1.0f / (MITER * MITER) * 2.0f;
    s = (NgP){(a.x + b.x) / k, (a.y + b.y) / k};
    return s;
}

static int put_line(void *u, const char *what, const NgP *p, int n, float half, int color)
{
    Overlay *o = (Overlay *)u;
    float    slot, lift;
    int      i, s;
    ShapeId  id;
    if (n < 2 || layer(o, p, n, half, &slot, &lift) != 0)
        return n < 2 ? 0 : -1;
    id = shape_open("%s at %d,%d", what, (int)floorf(p[0].x), (int)floorf(p[0].y));
    shape_unmarked();
    for (i = 0; i + 1 < n; i++)
    {
        NgP sa = side_at(p, n, i), sb = side_at(p, n, i + 1);
        int m = (int)ceilf(hypotf(p[i + 1].x - p[i].x, p[i + 1].y - p[i].y) / DRAPE);
        if (m < 1)
            m = 1;
        for (s = 0; s < m; s++)
        {
            float t0 = (float)s / (float)m, t1 = (float)(s + 1) / (float)m;
            NgP   c0 = {p[i].x + (p[i + 1].x - p[i].x) * t0, p[i].y + (p[i + 1].y - p[i].y) * t0};
            NgP   c1 = {p[i].x + (p[i + 1].x - p[i].x) * t1, p[i].y + (p[i + 1].y - p[i].y) * t1};
            NgP   d0 = {sa.x + (sb.x - sa.x) * t0, sa.y + (sb.y - sa.y) * t0}, d1 = {sa.x + (sb.x - sa.x) * t1, sa.y + (sb.y - sa.y) * t1};
            float l0[3], r0[3], l1[3], r1[3];
            ground(o, (NgP){c0.x - d0.x * half, c0.y - d0.y * half}, lift, l0);
            ground(o, (NgP){c0.x + d0.x * half, c0.y + d0.y * half}, lift, r0);
            ground(o, (NgP){c1.x - d1.x * half, c1.y - d1.y * half}, lift, l1);
            ground(o, (NgP){c1.x + d1.x * half, c1.y + d1.y * half}, lift, r1);
            if (tri(o, slot, lift, color, l0, r0, r1) != 0 || tri(o, slot, lift, color, l0, r1, l1) != 0)
            {
                shape_close(id);
                return -1;
            }
        }
    }
    shape_close(id);
    return 0;
}

/*  An arrow: a triangle from its base, across the line, to its point. */
static int put_arrow(void *u, NgP at, NgP d, float size, int color)
{
    Overlay *o = (Overlay *)u;
    float    slot, lift, h = size * 0.5f, w = size * 0.6f, a[3], b[3], c[3];
    int      rc;
    ShapeId  id;
    if (layer(o, &at, 1, size, &slot, &lift) != 0)
        return -1;
    id = shape_open("lane graph arrow at %d,%d", (int)floorf(at.x), (int)floorf(at.y));
    shape_unmarked();
    ground(o, (NgP){at.x - d.x * h + d.y * w, at.y - d.y * h - d.x * w}, lift, a);
    ground(o, (NgP){at.x - d.x * h - d.y * w, at.y - d.y * h + d.x * w}, lift, b);
    ground(o, (NgP){at.x + d.x * h, at.y + d.y * h}, lift, c);
    rc = tri(o, slot, lift, color, a, b, c);
    shape_close(id);
    return rc;
}

static int far_line(void *u, const char *what, const NgP *p, int n, float half, int color)
{
    Overlay *o = (Overlay *)u;
    int      i;
    (void)what, (void)half, (void)color;
    for (i = 0; i < n; i++)
        o->far = fmaxf(o->far, p[i].x + p[i].y);
    return 0;
}

static int far_arrow(void *u, NgP at, NgP dir, float size, int color)
{
    (void)dir, (void)size, (void)color;
    ((Overlay *)u)->far = fmaxf(((Overlay *)u)->far, at.x + at.y);
    return 0;
}

/*  The slots one round of layers takes in a tile's fraction. */
static int slots(Overlay *o)
{
    static int cz = -1, cd = -1;
    float      decal = geo_num(&cd, "decal_slot"), ulp, step, s;
    o->coplanar      = geo_num(&cz, "coplanar_z");
    ulp              = ldexpf(1.0f, (int)ceilf(log2f((float)r_map * (o->far + 4.0f))) - 23);
    step             = ulp * fmaxf(2.0f, ceilf(2.0f * decal / ulp));
    o->slots         = (float *)malloc(sizeof *o->slots * (size_t)(1.0f / step + 2.0f));
    if (!o->slots)
        return -1;
    for (s = step; s <= 1.0f - 2.0f * step; s += step)
        o->slots[o->nslot++] = s;
    return o->nslot > 0 ? 0 : -1;
}

int net_overlay(RMesh *m, const RCity *c, uint8_t mask_bit)
{
    Overlay o;
    NgSink  k = {&o, far_line, far_arrow, NULL, 0};
    int     i, unplaced = -1;
    memset(&o, 0, sizeof o);
    o.m = m, o.c = c, o.mask_bit = mask_bit;
    for (i = 0; i < NG_COLORS; i++)
    {
        char  name[64];
        float rgb[3] = {(float)(NG_RGB[i] >> 16 & 0xff) / 255.0f, (float)(NG_RGB[i] >> 8 & 0xff) / 255.0f, (float)(NG_RGB[i] & 0xff) / 255.0f};
        snprintf(name, sizeof name, "lane graph %s", NG_NAME[i]);
        if ((o.mat[i] = material_define(name, rgb, 0.9f)) < 0.0f)
        {
            R_ERR("net", "the lane graph could not be drawn: no room for its materials");
            return -1;
        }
    }
    if (net_graph_draw(&k) >= 0 && slots(&o) == 0)
    {
        k.line = put_line, k.arrow = put_arrow;
        unplaced = net_graph_draw(&k);
    }
    free(o.slots), free(o.held), free(o.claimed);
    if (unplaced < 0)
    {
        R_ERR("net", "the lane graph could not be drawn: out of memory");
        return -1;
    }
    R_DBG("net", "lane graph: %d lanes not placed, %d layers, %d slots each", unplaced, o.most, o.nslot);
    return 0;
}
