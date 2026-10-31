/*  draw/rails.c: the railways drawn from the road network's records
 *  (rails.h).
 *
 *  A drawn railway road is swept along its line as a gravel pile.  The
 *  pile has a flat bed on top and a slope down each side to its foot,
 *  and is closed underneath.  Each of its two rail lanes is a track on
 *  the bed.  A track is a sleeper across it every so often, and two
 *  rails along it on the sleepers.  A rail lane lies on its side of the
 *  line by the sign of its id.  A railway's junction is filled by
 *  roads.c as a gravel pile.  Each of its connecting roads carries the
 *  track of its one lane on it.  A level crossing's connecting roads
 *  carry their rails on the street drawn across it.  Every number is a
 *  knob in geo_defaults. */
#include "rails.h"
#include "rail_shape.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "geo.h"
#include "grow.h"
#include "laid.h"
#include "material.h"
#include "mesh/poly.h"
#include "mesh/slab.h"
#include "network/network.h"
#include "network/shape.h"
#include "network/topology.h"

/*  The gravel's material. */
static float rails_ballast(void);

/*  The most cross-sections one sweep lays at once, and the spacing of a
 *  junction's rails, fine enough for the gap where two rails cross. */
#define JUNCTION_STEP 0.002f

static const float GRAVEL[3] = {0.36f, 0.32f, 0.28f};

static float rails_ballast(void)
{
    return material_define("ballast", GRAVEL, 0.98f);
}

/*  The darker gravel under a track, `dark` of the way to black. */
static float stained(float dark)
{
    float rgb[3] = {GRAVEL[0] * (1.0f - dark), GRAVEL[1] * (1.0f - dark), GRAVEL[2] * (1.0f - dark)};
    return material_define("ballast under a track", rgb, 0.98f);
}

static float s_sec[SWEEP_MOST * 8 * 3], s_st[SWEEP_MOST];

/*  The gravel pile along a road.  Across it are its foot, the edge of
 *  its bed, the darker gravel under each track, the other edge of its
 *  bed and its foot. */
static void pile(RMesh *m, const RCity *c, uint8_t mask_bit, int road, const RailKnobs *k, float mat, float dark)
{
    const float mats[7] = {mat, mat, dark, mat, dark, mat, mat};
    const float t[8]    = {-k->foot, -k->bed, -k->offset - k->stain, -k->offset + k->stain, k->offset - k->stain, k->offset + k->stain, k->bed, k->foot};
    int         n       = rail_stations(rail_way_from(road), rail_way_to(road), k->step, s_st), q, e;
    for (q = 0; q < n; ++q)
        for (e = 0; e < 8; ++e)
            rail_line_at(road, s_st[q], t[e], e == 0 || e == 7 ? 0.0f : k->height, &s_sec[24 * q + 3 * e]);
    slab_sweep(m, c, mask_bit, s_sec, n, 8, k->sink * 0.5f, mats);
}

/*  What a junction's tracks have laid so far.  The sleepers are each a
 *  rectangle by its middle, its way along the track and its two half
 *  sizes.  The rails are each a run of points.  A track laid later gives
 *  way to them.  A railway away from its junctions lays into an empty
 *  set.  Each sleeper and each piece of rail is filed under the tiles of
 *  the box it reaches over (Cells).  So a test reads only what lies near. */
typedef struct
{
    int64_t *key;  /* per slot, a tile as (col << 32) + row, or CELL_FREE */
    int     *head; /* per slot, its first entry */
    int      slots, used;
    int     *item, *next; /* per entry, what is filed and the next entry of its tile */
    int      nent, capitem, capnext;
} Cells;
#define CELL_FREE INT64_MIN

typedef struct
{
    float (*tie)[6]; /* x, y, the way along it, half along, half across */
    int nties, capties;
    float (*pt)[2]; /* each rail's points in turn, a break between rails */
    int  *brk;      /* 1 where a rail starts at that point */
    int   npt, cappt, capbrk;
    Cells ties, segs; /* each sleeper by the tiles it reaches over, and each piece of rail */
} Laid;

static Laid s_laid;

static void cells_clear(Cells *c)
{
    int k;
    for (k = 0; k < c->slots; ++k)
        c->key[k] = CELL_FREE;
    c->used = c->nent = 0;
}

static int cells_slot(const Cells *c, int64_t key)
{
    uint64_t h = (uint64_t)key * 0x9E3779B97F4A7C15ull;
    int      k = (int)(h >> 40) & (c->slots - 1);
    while (c->key[k] != CELL_FREE && c->key[k] != key)
        k = (k + 1) & (c->slots - 1);
    return k;
}

/*  The first entry of the tile at col, row, or -1. */
static int cells_first(const Cells *c, int32_t col, int32_t row)
{
    int k;
    if (!c->slots)
        return -1;
    k = cells_slot(c, ((int64_t)col << 32) + row);
    return c->key[k] == CELL_FREE ? -1 : c->head[k];
}

static int cells_grow(Cells *c)
{
    int64_t *ok = c->key;
    int     *oh = c->head, os = c->slots, k;
    int      ns = os ? 2 * os : 256;
    c->key      = (int64_t *)malloc(sizeof *c->key * (size_t)ns);
    c->head     = (int *)malloc(sizeof *c->head * (size_t)ns);
    if (!c->key || !c->head)
    {
        free(c->key), free(c->head);
        c->key = ok, c->head = oh;
        return -1;
    }
    c->slots = ns;
    for (k = 0; k < ns; ++k)
        c->key[k] = CELL_FREE;
    for (k = 0; k < os; ++k)
        if (ok[k] != CELL_FREE)
        {
            int at      = cells_slot(c, ok[k]);
            c->key[at]  = ok[k];
            c->head[at] = oh[k];
        }
    free(ok), free(oh);
    return 0;
}

/*  File `item` under every tile of the box x0, y0 to x1, y1. */
static int cells_add(Cells *c, float x0, float y0, float x1, float y1, int item)
{
    int32_t col, row;
    for (row = (int32_t)floorf(y0); row <= (int32_t)floorf(y1); ++row)
        for (col = (int32_t)floorf(x0); col <= (int32_t)floorf(x1); ++col)
        {
            int64_t key = ((int64_t)col << 32) + row;
            int     k;
            if (2 * (c->used + 1) > c->slots && cells_grow(c) != 0)
                return -1;
            if (arc_grow((void **)&c->item, &c->capitem, c->nent + 1, sizeof *c->item) != 0 ||
                arc_grow((void **)&c->next, &c->capnext, c->nent + 1, sizeof *c->next) != 0)
                return -1;
            k = cells_slot(c, key);
            if (c->key[k] == CELL_FREE)
                c->key[k] = key, c->head[k] = -1, c->used++;
            c->item[c->nent] = item, c->next[c->nent] = c->head[k], c->head[k] = c->nent++;
        }
    return 0;
}

static void laid_clear(void)
{
    s_laid.nties = s_laid.npt = 0;
    cells_clear(&s_laid.ties);
    cells_clear(&s_laid.segs);
}

/*  The gravel of the junction being drawn (rail_shape.h). */
static RailGravel *s_gr;

/*  Whether two sleepers overlap: the axes of both rectangles separate
 *  them or not. */
static int ties_meet(const float *a, const float *b)
{
    const float *r[2] = {a, b};
    int          q, e;
    for (q = 0; q < 2; ++q)
        for (e = 0; e < 2; ++e)
        {
            float ax = e ? -r[q][3] : r[q][2], ay = e ? r[q][2] : r[q][3], lo[2], hi[2];
            int   w;
            for (w = 0; w < 2; ++w)
            {
                const float *t  = r[w];
                float        c  = t[0] * ax + t[1] * ay;
                float        ex = fabsf(t[2] * ax + t[3] * ay) * t[4] + fabsf(-t[3] * ax + t[2] * ay) * t[5];
                lo[w] = c - ex, hi[w] = c + ex;
            }
            if (hi[0] < lo[1] || hi[1] < lo[0])
                return 0;
        }
    return 1;
}

/*  Whether a point lies within `reach` of a rail already laid.  Each
 *  piece of rail is filed under the tiles its box widened by that reach
 *  covers, so only the point's own tile is read. */
static int rail_within(float x, float y, float reach)
{
    int e;
    for (e = cells_first(&s_laid.segs, (int32_t)floorf(x), (int32_t)floorf(y)); e >= 0; e = s_laid.segs.next[e])
    {
        int          k = s_laid.segs.item[e];
        const float *a = s_laid.pt[k - 1], *b = s_laid.pt[k];
        float        ex = b[0] - a[0], ey = b[1] - a[1], l2 = ex * ex + ey * ey, t;
        t        = l2 > 1e-12f ? ((x - a[0]) * ex + (y - a[1]) * ey) / l2 : 0.0f;
        t        = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f
                                              : t;
        float dx = x - a[0] - ex * t, dy = y - a[1] - ey * t;
        if (dx * dx + dy * dy <= reach * reach)
            return 1;
    }
    return 0;
}

/*  One track along a road, t across its line, on a bed `base` over its
 *  line: sleepers across it, then its two rails.  A
 *  sleeper that would overlap one laid already is left out: the one laid
 *  carries this track too.  A stretch of rail within a rail's width of a
 *  rail laid already is left out.  At a turnout's points the two are one
 *  rail.  Where two rails cross, it is the gap a wheel's flange runs
 *  through. */
static void track(RMesh *m, const RCity *c, uint8_t mask_bit, int road, float lo, float hi, float t, float base, float step, const RailKnobs *k, float wood, float steel)
{
    const float w2 = k->sleeper_w * 0.5f, mats[1] = {wood};
    const float rail_base = base + k->sleeper_h, steel1[1] = {steel};
    float       s, a[2][3], b[2][3];
    int         side, n, q;
    for (s = lo + k->gap * 0.5f; s < hi; s += k->gap)
    {
        float s0 = s - w2 > lo ? s - w2 : lo, s1 = s + w2 < hi ? s + w2 : hi, mid[3], x, y, h, z;
        float tie[6], reach;
        int   q2, clear = 1;
        rail_line_at(road, s, t, 0.0f, mid);
        rail_way_at(road, s, &x, &y, &h, &z);
        tie[0] = mid[0], tie[1] = mid[1], tie[2] = cosf(h), tie[3] = sinf(h), tie[4] = w2, tie[5] = k->sleeper_half;
        reach = hypotf(tie[4], tie[5]);
        {
            int32_t col, row;
            for (row = (int32_t)floorf(tie[1] - reach); clear && row <= (int32_t)floorf(tie[1] + reach); ++row)
                for (col = (int32_t)floorf(tie[0] - reach); clear && col <= (int32_t)floorf(tie[0] + reach); ++col)
                    for (q2 = cells_first(&s_laid.ties, col, row); clear && q2 >= 0; q2 = s_laid.ties.next[q2])
                        clear = !ties_meet(tie, s_laid.tie[s_laid.ties.item[q2]]);
        }
        if (!clear)
            continue;
        if (arc_grow((void **)&s_laid.tie, &s_laid.capties, s_laid.nties + 1, sizeof *s_laid.tie) == 0)
        {
            memcpy(s_laid.tie[s_laid.nties], tie, sizeof tie);
            cells_add(&s_laid.ties, tie[0] - reach, tie[1] - reach, tie[0] + reach, tie[1] + reach, s_laid.nties++);
        }
        rail_line_at(road, s0, t - k->sleeper_half, base + k->sleeper_h, a[0]);
        rail_line_at(road, s0, t + k->sleeper_half, base + k->sleeper_h, a[1]);
        rail_line_at(road, s1, t - k->sleeper_half, base + k->sleeper_h, b[0]);
        rail_line_at(road, s1, t + k->sleeper_half, base + k->sleeper_h, b[1]);
        slab_strips(m, c, mask_bit, (const float (*)[3])a, (const float (*)[3])b, 2, k->sleeper_h, mats);
    }
    n = rail_stations(lo, hi, step, s_st);
    for (side = -1; side <= 1; side += 2)
    {
        float r = t + (float)side * k->gauge * 0.5f, keep_from = 0;
        int   kept = 0, first = s_laid.npt;
        for (q = 0; q < n; ++q)
        {
            float mid[3];
            int   free_here;
            rail_line_at(road, s_st[q], r, 0.0f, mid);
            free_here = !rail_within(mid[0], mid[1], k->rail_w * 1.5f);
            if (free_here)
            {
                rail_line_at(road, s_st[q], r - k->rail_w * 0.5f, rail_base + k->rail_h, &s_sec[6 * kept]);
                rail_line_at(road, s_st[q], r + k->rail_w * 0.5f, rail_base + k->rail_h, &s_sec[6 * kept + 3]);
                ++kept;
            }
            if ((!free_here || q == n - 1) && kept >= 2)
                slab_sweep(m, c, mask_bit, s_sec, kept, 2, k->rail_h, steel1);
            if (!free_here || q == n - 1)
                kept = 0;
            (void)keep_from;
        }
        /*  The rail, laid, for the tracks after it. */
        for (q = 0; q < n; ++q)
        {
            float mid[3], reach = k->rail_w * 1.5f;
            if (arc_grow((void **)&s_laid.pt, &s_laid.cappt, s_laid.npt + 1, sizeof *s_laid.pt) != 0 ||
                arc_grow((void **)&s_laid.brk, &s_laid.capbrk, s_laid.npt + 1, sizeof *s_laid.brk) != 0)
                break;
            rail_line_at(road, s_st[q], r, 0.0f, mid);
            s_laid.pt[s_laid.npt][0] = mid[0], s_laid.pt[s_laid.npt][1] = mid[1];
            s_laid.brk[s_laid.npt] = s_laid.npt == first;
            /*  The piece of rail from the point before, filed for the
             *  tracks after it. */
            if (!s_laid.brk[s_laid.npt] && s_laid.npt > 0)
            {
                const float *p0 = s_laid.pt[s_laid.npt - 1], *p1 = s_laid.pt[s_laid.npt];
                cells_add(&s_laid.segs, fminf(p0[0], p1[0]) - reach, fminf(p0[1], p1[1]) - reach, fmaxf(p0[0], p1[0]) + reach, fmaxf(p0[1], p1[1]) + reach, s_laid.npt);
            }
            s_laid.npt++;
        }
    }
}

/*  How straight a road's line is: its chord over its length, 1 for a
 *  straight. */
static float straightness(int road)
{
    float L = shape_road(road)->length, x0, y0, x1, y1, h;
    shape_at(road, 0.0f, &x0, &y0, &h);
    shape_at(road, L, &x1, &y1, &h);
    return L > 1e-6f ? hypotf(x1 - x0, y1 - y0) / L : 1.0f;
}

/*  Put into each edge of a's rings every point of b's rings that lies on
 *  it, in order along it.  So the two meet point for point, and no edge
 *  of one ends partway along an edge of the other. */
static PolySet s_welded;
static void    weld(PolySet *a, const PolySet *b)
{
    int r, q, k;
    s_welded.nring = s_welded.npt = 0;
    for (r = 0; r < a->nring; ++r)
    {
        s_welded.start[s_welded.nring] = s_welded.npt, s_welded.hole[s_welded.nring] = a->hole[r];
        s_welded.poly[s_welded.nring] = a->poly[r];
        for (q = 0; q < a->count[r]; ++q)
        {
            const float *p0 = &a->xy[2 * (a->start[r] + q)], *p1 = &a->xy[2 * (a->start[r] + (q + 1) % a->count[r])];
            float        ex = p1[0] - p0[0], ey = p1[1] - p0[1], l2 = ex * ex + ey * ey, ts[64];
            int          nt = 0, i, j;
            if (s_welded.npt < POLY_POINTS)
                s_welded.xy[2 * s_welded.npt] = p0[0], s_welded.xy[2 * s_welded.npt + 1] = p0[1], s_welded.npt++;
            if (l2 < 1e-14f)
                continue;
            for (k = 0; k < b->npt && nt < 64; ++k)
            {
                const float *v = &b->xy[2 * k];
                float        t = ((v[0] - p0[0]) * ex + (v[1] - p0[1]) * ey) / l2;
                if (t <= 1e-4f || t >= 1.0f - 1e-4f)
                    continue;
                if (fabsf((v[0] - p0[0]) * ey - (v[1] - p0[1]) * ex) / sqrtf(l2) > 1e-5f)
                    continue;
                ts[nt++] = t;
            }
            for (i = 1; i < nt; ++i)
                for (j = i; j > 0 && ts[j] < ts[j - 1]; --j)
                {
                    float t = ts[j];
                    ts[j] = ts[j - 1], ts[j - 1] = t;
                }
            for (i = 0; i < nt && s_welded.npt < POLY_POINTS; ++i)
                if (i == 0 || ts[i] - ts[i - 1] > 1e-4f)
                {
                    s_welded.xy[2 * s_welded.npt]     = p0[0] + ex * ts[i];
                    s_welded.xy[2 * s_welded.npt + 1] = p0[1] + ey * ts[i];
                    s_welded.npt++;
                }
        }
        s_welded.count[s_welded.nring] = s_welded.npt - s_welded.start[s_welded.nring];
        s_welded.nring++;
    }
    memcpy(a, &s_welded, sizeof *a);
}

/*  Whether a point lies on a ring of a set. */
static int on_rings(const PolySet *set, float x, float y)
{
    int r, q;
    for (r = 0; r < set->nring; ++r)
        for (q = 0; q < set->count[r]; ++q)
        {
            const float *a = &set->xy[2 * (set->start[r] + q)], *b = &set->xy[2 * (set->start[r] + (q + 1) % set->count[r])];
            float        ex = b[0] - a[0], ey = b[1] - a[1], l2 = ex * ex + ey * ey, t;
            t = l2 > 1e-12f ? ((x - a[0]) * ex + (y - a[1]) * ey) / l2 : 0.0f;
            t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f
                                           : t;
            if (hypotf(x - a[0] - ex * t, y - a[1] - ey * t) < 1e-4f)
                return 1;
        }
    return 0;
}

static int s_tri[3 * POLY_POINTS];

/*  Each polygon of a set, as triangles, every point raised by lift(x, y). */
static void lay_set(RMesh *m, const RCity *c, uint8_t mask_bit, const PolySet *set, float mat, int down, float (*lift)(float, float, void *), void *ud)
{
    int r = 0;
    while (r < set->nring)
    {
        int r1 = r + 1, nt, q, e;
        while (r1 < set->nring && set->poly[r1] == set->poly[r])
            ++r1;
        nt = poly_triangulate_holes(set->xy, 2, &set->start[r], &set->count[r], r1 - r, s_tri);
        for (q = 0; q < nt; ++q)
        {
            float t[3][3];
            for (e = 0; e < 3; ++e)
            {
                int i   = s_tri[3 * q + e];
                t[e][0] = set->xy[2 * i], t[e][1] = set->xy[2 * i + 1], t[e][2] = lift(t[e][0], t[e][1], ud);
            }
            slab_tri_faced(m, c, mask_bit, t[0], t[1], t[2], mat, down);
        }
        r = r1;
    }
}

static float lift_top(float x, float y, void *ud)
{
    return rail_gravel_z(s_gr, x, y) + ((const RailKnobs *)ud)->height;
}
static float lift_band(float x, float y, void *ud)
{
    return rail_gravel_z(s_gr, x, y) + (on_rings(&s_gr->bed, x, y) ? ((const RailKnobs *)ud)->height : 0.0f);
}
static float lift_bottom(float x, float y, void *ud)
{
    return rail_gravel_z(s_gr, x, y) - ((const RailKnobs *)ud)->sink * 0.5f;
}

/*  A junction's gravel pile: its top, its slope, its bottom, and the wall
 *  round its foot down to its bottom. */
static void gravel_draw(RMesh *m, const RCity *c, uint8_t mask_bit, const RailKnobs *k, float mat, float dark, int stained)
{
    int r, q;
    /*  Every outline meets every other point for point. */
    weld(&s_gr->band, &s_gr->foot);
    if (stained)
    {
        weld(&s_gr->light, &s_gr->dark), weld(&s_gr->dark, &s_gr->light);
        weld(&s_gr->light, &s_gr->band), weld(&s_gr->dark, &s_gr->band);
        weld(&s_gr->band, &s_gr->light), weld(&s_gr->band, &s_gr->dark);
    }
    else
        weld(&s_gr->bed, &s_gr->band), weld(&s_gr->band, &s_gr->bed);
    weld(&s_gr->foot, &s_gr->band);
    if (stained)
    {
        lay_set(m, c, mask_bit, &s_gr->light, mat, 0, lift_top, (void *)k);
        lay_set(m, c, mask_bit, &s_gr->dark, dark, 0, lift_top, (void *)k);
    }
    else
        lay_set(m, c, mask_bit, &s_gr->bed, mat, 0, lift_top, (void *)k);
    lay_set(m, c, mask_bit, &s_gr->band, mat, 0, lift_band, (void *)k);
    lay_set(m, c, mask_bit, &s_gr->foot, mat, 1, lift_bottom, (void *)k);
    for (r = 0; r < s_gr->foot.nring; ++r)
        for (q = 0; q < s_gr->foot.count[r]; ++q)
        {
            const float *a = &s_gr->foot.xy[2 * (s_gr->foot.start[r] + q)], *b = &s_gr->foot.xy[2 * (s_gr->foot.start[r] + (q + 1) % s_gr->foot.count[r])];
            float        ta[3] = {a[0], a[1], rail_gravel_z(s_gr, a[0], a[1])}, tb[3] = {b[0], b[1], rail_gravel_z(s_gr, b[0], b[1])};
            float        ba[3] = {a[0], a[1], ta[2] - k->sink * 0.5f}, bb[3] = {b[0], b[1], tb[2] - k->sink * 0.5f};
            slab_quad(m, c, mask_bit, ta, tb, bb, ba, mat);
        }
}

/*  A buffer stop: a concrete block across a track at the end of a
 *  railway's cap, on its gravel bed.  Its far face lies on the cap's end, at
 *  s `end`, and it stands toward s `toward`. */
static void buffer_stop(RMesh *m, const RCity *c, uint8_t mask_bit, int road, float end, float toward, float t, const RailKnobs *k, float mat)
{
    const float mats[1] = {mat};
    float       a[2][3], b[2][3], near = end + (toward > end ? k->buf_depth : -k->buf_depth);
    rail_line_at(road, near, t - k->buf_half, k->height + k->buf_h, a[0]);
    rail_line_at(road, near, t + k->buf_half, k->height + k->buf_h, a[1]);
    rail_line_at(road, end, t - k->buf_half, k->height + k->buf_h, b[0]);
    rail_line_at(road, end, t + k->buf_half, k->height + k->buf_h, b[1]);
    slab_strips(m, c, mask_bit, (const float (*)[3])a, (const float (*)[3])b, 2, k->buf_h, mats);
}

void rails_draw(RMesh *m, const RCity *c, uint8_t mask_bit)
{
    static const float WOOD[3] = {0.10f, 0.07f, 0.05f}, STEEL[3] = {0.90f, 0.91f, 0.93f};
    float              ballast = rails_ballast(), wood = material_define("sleeper", WOOD, 0.9f), dark;
    static const float CONCRETE[3] = {0.66f, 0.64f, 0.60f};
    float              steel = material_define("rail", STEEL, 0.2f), concrete = material_define("buffer stop", CONCRETE, 0.95f);
    RailKnobs          k;
    int                i, j, w;
    rail_knobs(&k);
    /*  The sleepers and the rails are far thinner than a tile.  Too thin
     *  for a pixel, they are left out, and the bed and its stain carry the
     *  track. */
    material_fine(wood, k.sleeper_w);
    material_fine(steel, k.rail_w);
    dark = stained(k.dark);
    for (i = 0; i < network_roads(); ++i)
    {
        const NRoad *r = network_road(i);
        ShapeId      id;
        float        x, y, h;
        /*  A railway: its pile, and a track either side of its line.  A
         *  railway's way across a level meet is one of its two connecting
         *  roads there, drawn as the railway (roads.c), both its tracks too. */
        if (r->net != RNET_RAIL || !shape_road(i)->ngeom || !laid_road(i) || !laid_near(i))
            continue;
        shape_at(i, 0.0f, &x, &y, &h);
        id = shape_open("railway, road %016llx from %d,%d", (unsigned long long)network_road(i)->id, (int)floorf(x), (int)floorf(y));
        laid_ref_road(i);
        pile(m, c, mask_bit, i, &k, ballast, dark);
        /*  Each track runs over the caps, and stops at a buffer stop at the
         *  end of each. */
        for (w = -1; w <= 1; w += 2)
        {
            float lo = rail_way_from(i), hi = rail_way_to(i), t = (float)w * k.offset;
            if (laid_cap(i, 0) > 0.0f)
                buffer_stop(m, c, mask_bit, i, lo, hi, t, &k, concrete), lo += k.buf_depth;
            if (laid_cap(i, 1) > 0.0f)
                buffer_stop(m, c, mask_bit, i, hi, lo, t, &k, concrete), hi -= k.buf_depth;
            laid_clear();
            track(m, c, mask_bit, i, lo, hi, t, k.height, k.step, &k, wood, steel);
        }
        shape_close(id);
    }
    /*  Each junction's tracks together, straightest first, so a turnout's
     *  branches give way to its through line.  On the junction's pile, or
     *  in the street across a level crossing. */
    for (j = 0; j < network_junctions(); ++j)
    {
        int     order[64], n = 0, a, b;
        ShapeId id;
        int     near = 0;
        for (i = 0; i < network_roads() && n < 64; ++i)
            if (network_road(i)->junction == j && rail_connection_drawn(i))
                order[n++] = i, near |= laid_near(i);
        if (!n || !near)
            continue;
        for (a = 1; a < n; ++a)
            for (b = a; b > 0 && straightness(order[b]) > straightness(order[b - 1]); --b)
            {
                int t    = order[b];
                order[b] = order[b - 1], order[b - 1] = t;
            }
        laid_clear();
        {
            float x, y, h;
            shape_at(order[0], 0.0f, &x, &y, &h);
            id = shape_open("tracks, junction %016llx at %d,%d", (unsigned long long)network_junction(j)->id, (int)floorf(x), (int)floorf(y));
            laid_ref_junction(j, RNET_RAIL);
        }
        {
            int g;
            s_gr = rail_gravel(j, &k, &g);
            if (g)
                gravel_draw(m, c, mask_bit, &k, ballast, dark, g == 2);
        }
        for (a = 0; a < n; ++a)
            track(m, c, mask_bit, order[a], 0.0f, shape_road(order[a])->length, rail_lane_side(network_road(order[a]), 0) * k.offset, k.height, JUNCTION_STEP, &k, wood, steel);
        shape_close(id);
    }
}
