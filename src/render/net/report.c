/*  The debug report of an area.  It says what the city holds on every
 *  tile of the area.  It uses the two-letter map from when the bands
 *  were argued over, and what the networks made of them.
 *
 *      The bands with their fitted nodes and stations.  The segments.
 *      The on-spurs with the stage that lost them.  The junctions'
 *      controls. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mesh/internal.h"
#include "mesh/mesh.h"
#include "pipeline.h"
#include "net/net.h"
#include "net/report.h"
#include "mesh/loft.h"
#include "mesh/piece.h"
#include "net/cell.h"
#include "script.h"

typedef struct
{
    char  *s;
    size_t n, cap;
} Text;

static int text_printf(Text *t, const char *fmt, ...)
{
    va_list ap;
    int     need;
    va_start(ap, fmt);
    need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0)
        return -1;
    if (t->n + (size_t)need + 1 > t->cap)
    {
        size_t cap = t->cap ? t->cap : 4096;
        char  *s;
        while (cap < t->n + (size_t)need + 1)
            cap *= 2;
        s = (char *)realloc(t->s, cap);
        if (!s)
            return -1;
        t->s   = s;
        t->cap = cap;
    }
    va_start(ap, fmt);
    vsnprintf(t->s + t->n, t->cap - t->n, fmt, ap);
    va_end(ap);
    t->n += (size_t)need;
    return 0;
}

/*  A tile's two-letter mark, in the order the three tables are asked.
 *  A band piece carries its own, which stands whatever the terrain is,
 *  so that one band piece can be told from another.  The terrain comes
 *  next, which is how water shows through what stands on it.  Then what
 *  stands there.  Every mark is a script's:
 *  arc.names("band_marks"), ("terrain_marks") and ("tile_marks"). */
static void tile_mark(const RCity *c, int32_t col, int32_t row, char out[3])
{
    int32_t     at = row * R_MAP + col;
    const char *m  = script_name("band_marks", c->xbld[at]);
    if (!m)
        m = script_name("terrain_marks", c->xter[at]);
    if (!m)
        m = script_name("tile_marks", c->xbld[at]);
    snprintf(out, 3, "%s", m ? m : "??");
}

/*  And the word for a byte, as arc.names("tile_kinds") gives it. */
static const char *tile_kind(uint8_t b)
{
    const char *k = script_name("tile_kinds", b);
    return k ? k : "?";
}

static int in_area(int32_t col, int32_t row, int32_t c0, int32_t r0, int32_t c1, int32_t r1)
{
    return col >= c0 && col <= c1 && row >= r0 && row <= r1;
}

int net_area_report(const RCity *c, int32_t c0, int32_t r0, int32_t c1, int32_t r1, char **out)
{
    Text    t = {NULL, 0, 0};
    int32_t col, row, tmp;
    int     i, k, n;
    char    buf[200], mark[3];
    if (!c || !out)
        return -1;
    if (c0 > c1)
        tmp = c0, c0 = c1, c1 = tmp;
    if (r0 > r1)
        tmp = r0, r0 = r1, r1 = tmp;
    c0 = c0 < 0 ? 0 : c0 > R_MAP - 1 ? R_MAP - 1
                                     : c0;
    c1 = c1 < 0 ? 0 : c1 > R_MAP - 1 ? R_MAP - 1
                                     : c1;
    r0 = r0 < 0 ? 0 : r0 > R_MAP - 1 ? R_MAP - 1
                                     : r0;
    r1 = r1 < 0 ? 0 : r1 > R_MAP - 1 ? R_MAP - 1
                                     : r1;
    text_printf(&t, "area  %s  cols %d..%d  rows %d..%d  (%d x %d tiles)  rotation %d\n", c->name[0] ? c->name : "(unnamed)", (int)c0, (int)c1, (int)r0, (int)r1, (int)(c1 - c0 + 1), (int)(r1 - r0 + 1), (int)c->rotation);
    text_printf(&t, "  north is decreasing row, east is decreasing col; a label is col,row\n");
    /*  The map: the marks used when the bands were argued over. */
    /*  The legend, built from the same tables the marks come from: each
     *  mark once, with the word of the first byte that carries it.  A
     *  list written out here would be a second opinion about what a
     *  mark stands for. */
    text_printf(&t, "tiles  (");
    {
        const char *said[16];
        int         n_said = 0, byte, seen;
        for (byte = 0; byte < 512; ++byte)
        {
            /*  The terrain's marks first, then what stands on a tile,
             *  which is the order a mark is settled in. */
            const char *m = byte < 256 ? script_name("terrain_marks", (unsigned char)byte)
                                       : script_name("tile_marks", (unsigned char)(byte - 256));
            if (!m)
                continue;
            for (seen = 0; seen < n_said; ++seen)
                if (strcmp(said[seen], m) == 0)
                    break;
            if (seen < n_said || n_said >= (int)(sizeof said / sizeof said[0]))
                continue;
            said[n_said++] = m;
            text_printf(&t, "%s%s %s", n_said > 1 ? "  " : "", m,
                        byte < 256 ? "water" : tile_kind((uint8_t)(byte - 256)));
        }
    }
    text_printf(&t, "; a band piece by its id)\n     ");
    for (col = c0; col <= c1; ++col)
        text_printf(&t, " %3d", (int)col);
    text_printf(&t, "\n");
    for (row = r0; row <= r1; ++row)
    {
        text_printf(&t, "  %3d", (int)row);
        for (col = c0; col <= c1; ++col)
        {
            tile_mark(c, col, row, mark);
            text_printf(&t, "  %s", mark);
        }
        text_printf(&t, "\n");
    }
    /*  What stands there, tile by tile, with the mesh's word on it. */
    text_printf(&t, "what stands there (every tile that is not plain ground)\n");
    for (row = r0; row <= r1; ++row)
        for (col = c0; col <= c1; ++col)
        {
            int32_t idx = row * R_MAP + col;
            uint8_t b   = c->xbld[idx];
            if (b == 0 && !is_water(c->xter[idx]))
                continue;
            buf[0] = 0;
            mesh_query(c, col, row, buf, sizeof buf);
            text_printf(&t, "  %3d,%-3d xbld 0x%02x %-30s alt %2d  xter 0x%02x  %s\n", (int)col, (int)row, (unsigned)b, tile_kind(b), (int)rcity_alt_ground(c->altm[idx]), (unsigned)c->xter[idx], buf);
        }
    /*  The bands over the area: their tiles here, their fitted
     *  nodes near here, their stations here. */
    n = band_count();
    for (i = 0; i < n; ++i)
    {
        const int32_t *bt;
        int            nb, here = 0, idx, nk, j, said = 0;
        const RSeg    *r;
        const V2      *q;
        const float   *rad;
        if (band_get(i, &bt, &nb) != 0)
            continue;
        for (k = 0; k < nb; ++k)
            if (in_area(bt[k] % R_MAP, bt[k] / R_MAP, c0, r0, c1, r1))
                ++here;
        if (!here)
            continue;
        idx = seg_table_band_index(i);
        r   = idx >= 0 ? seg_table_entry(idx) : NULL;
        text_printf(&t, "band %d%s: %d of its %d tiles are in the area\n", i, r ? "" : " (not in the table)", here, nb);
        if (r)
            text_printf(&t, "  walked from %d,%d, %d fitted nodes, %d pieces\n", (int)r->col, (int)r->row, r->nk, r->np);
        if (r && seg_table_nodes(idx, &q, &rad, &nk) == 0)
            for (j = 0; j < nk; ++j)
                if (q[j].x >= (float)c0 - 1.0f && q[j].x <= (float)c1 + 2.0f && q[j].y >= (float)r0 - 1.0f && q[j].y <= (float)r1 + 2.0f)
                {
                    if (!said)
                        text_printf(&t, "  fitted nodes near the area (x,y radius):"), said = 1;
                    text_printf(&t, " (%.2f,%.2f) r%.2f", (double)q[j].x, (double)q[j].y, (double)rad[j]);
                }
        if (said)
            text_printf(&t, "\n");
    }
    {
        int nst = 0, band = -1, first = -1, last = -1;
        for (k = 0; k < s_band_nst; ++k)
        {
            const BandSt *st = &s_band_st[k];
            if (!in_area((int32_t)floorf(st->pos.x), (int32_t)floorf(st->pos.y), c0, r0, c1, r1))
                continue;
            if (nst == 0)
                first = k, band = st->band;
            last = k;
            ++nst;
        }
        if (nst)
            text_printf(&t, "slab stations in the area: %d (the loft's band %d), first (%.2f,%.2f) s %.2f z %.2f, last (%.2f,%.2f) s %.2f z %.2f\n", nst, band, (double)s_band_st[first].pos.x, (double)s_band_st[first].pos.y, (double)s_band_st[first].s, (double)s_band_st[first].z, (double)s_band_st[last].pos.x, (double)s_band_st[last].pos.y, (double)s_band_st[last].s, (double)s_band_st[last].z);
    }
    /*  The line and thread segments over the area. */
    n = seg_table_count();
    for (i = 0; i < n; ++i)
    {
        int32_t        col0, row0, cc, cr;
        const int32_t *tc, *tr;
        int            nt, here = 0;
        const RSeg    *r = seg_table_entry(i);
        if (!r || r->band || seg_table_get(i, &col0, &row0, &cc, &cr, &tc, &tr, &nt) != 0)
            continue;
        for (k = 0; k < nt; ++k)
            if (in_area(tc[k], tr[k], c0, r0, c1, r1))
                ++here;
        if (here)
            text_printf(&t, "%s segment %d from %d,%d to %d,%d: %d of its %d tiles are in the area, %d pieces, %d fitted nodes\n", net_tile_family_name((int)r->f), i, (int)col0, (int)row0, (int)cc, (int)cr, here, nt, r->np, r->nk);
    }
    /*  The on-spurs, with the stage that lost any. */
    for (i = 0; i < s_band_nspurs; ++i)
    {
        const BandSpur *rp = &s_band_spurs[i];
        const char   *lost;
        if (!in_area(rp->rc, rp->rr, c0, r0, c1, r1))
            continue;
        lost = band_spur_lost(rp->rc, rp->rr);
        text_printf(&t, "on-spur %d,%d: %s, on the slab's centreline at %.2f,%.2f, along %.0f,%.0f, toward the slab %.0f,%.0f, %d tiles of lane drop%s%s\n", (int)rp->rc, (int)rp->rr, rp->off ? "OFF the slab" : "ON to the slab", (double)rp->c0.x, (double)rp->c0.y, (double)rp->along.x, (double)rp->along.y, (double)rp->toward.x, (double)rp->toward.y, rp->len, lost ? " -- LOST: " : "", lost ? lost : "");
    }
    /*  The junctions' controls. */
    for (row = r0; row <= r1; ++row)
        for (col = c0; col <= c1; ++col)
        {
            uint8_t                  ctl    = s_junc_ctrl[row * R_MAP + col];
            static const char *const CTL[4] = {"none", "stop", "signal", "?"};
            if (!ctl)
                continue;
            text_printf(&t, "junction %d,%d: N %s, E %s, S %s, W %s\n", (int)col, (int)row, CTL[ctl & 3], CTL[(ctl >> 2) & 3], CTL[(ctl >> 4) & 3], CTL[(ctl >> 6) & 3]);
        }
    text_printf(&t, "end of area\n");
    *out = t.s;
    return t.s ? 0 : -1;
}

/*  The component under a tile, for the inspector.  It is an
 *  intersection's own outline where the tile is a junction.  Otherwise
 *  it is the band of the line or line that runs through it.  The polygon
 *  comes back in world coordinates, closed, and `label` names the thing.
 *  Returns 0, or -1 when nothing of ours is there. */
int net_component_at(const RCity *c, int32_t col, int32_t row, float *poly_xy, int max_pts, int *n, char *label, size_t lab)
{
    V2 *poly = (V2 *)poly_xy; /* pairs of floats, laid out as the points are */
    int max  = max_pts;
    const Family fams[2] = {net_line->f, net_thread->f};
    int                 fi, i, cnt = seg_table_count();
    if (!poly_xy || max < 8 || !n || col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return -1;
    *n = 0;
    /*  A junction first: its outline is the shape its own box is drawn
     *  on, which is what the eye calls the intersection. */
    for (fi = 0; fi < 2; ++fi)
    {
        Family  f     = fams[fi];
        int     links = eff_links(c, col, row, f);
        float   trim[4];
        uint8_t mouth[JUNC_MAX];
        int     np;
        if (!tile_links(c, col, row, f) || node_kind(c, f, col, row) != 2)
            continue;
        np = junction_poly(c, f, col, row, links, poly, mouth, max < JUNC_MAX ? max : JUNC_MAX, trim, NULL);
        if (np < 3)
            continue;
        *n = np;
        snprintf(label, lab, "%s junction at %d,%d", net_tile_family_name((int)f), (int)col, (int)row);
        return 0;
    }
    /*  A spur next: it is a lane and nothing else.  No segment holds it.
     *  So the lane table is the only place to find it. */
    {
        int nl = lane_table_count();
        for (i = 0; i < nl; ++i)
        {
            int          cls, fam, np, k, side, steps;
            const Piece *pc;
            float        w, total = 0.0f, step;
            V2           pos, dir;
            if (lane_table_get(i, &cls, &fam, &pc, &np, &w) != 0 || cls != LANE_CLS_SPUR || np < 1)
                continue;
            for (k = 0; k < np; ++k)
                total += pc[k].len;
            if (total < 1e-3f)
                continue;
            /* does it run through this tile? */
            for (k = 0; k <= 32; ++k)
            {
                pieces_at(pc, np, total * (float)k / 32.0f, &pos, &dir);
                if ((int32_t)floorf(pos.x) == col && (int32_t)floorf(pos.y) == row)
                    break;
            }
            if (k > 32)
                continue;
            steps = max / 2 - 1;
            if (steps > 96)
                steps = 96;
            step = total / (float)steps;
            for (side = 0; side < 2; ++side)
                for (k = 0; k <= steps && *n < max; ++k)
                {
                    float s0 = side ? total - (float)k * step : (float)k * step;
                    pieces_at(pc, np, s0, &pos, &dir);
                    poly[*n].x = pos.x + dir.y * (side ? -w : w);
                    poly[*n].y = pos.y - dir.x * (side ? -w : w);
                    ++*n;
                }
            snprintf(label, lab, "spur lane %d", i);
            return 0;
        }
    }
    /*  Else the band that runs through the tile.
     *
     *      Its two edges.
     *      Out along one and back along the other.
     *      At the half width it was lofted at. */
    for (i = 0; i < cnt; ++i)
    {
        const RSeg    *r = seg_table_entry(i);
        const Piece   *pc;
        const V2      *q;
        const float   *rad, *tlim;
        const int32_t *tc, *tr;
        int            k, side, steps;
        float          total = 0.0f, step;
        if (!r || r->np < 1)
            continue; /* a band is walked whole and its tiles are not its own: the report skips it too */
        seg_table_arenas(r, &pc, &q, &rad, &tlim, &tc, &tr);
        for (k = 0; k < r->nt; ++k)
            if (tc[k] == col && tr[k] == row)
                break;
        if (k >= r->nt)
            continue;
        for (k = 0; k < r->np; ++k)
            total += pc[k].len;
        if (total < 1e-3f)
            continue;
        steps = max / 2 - 1;
        if (steps > 96)
            steps = 96;
        if (steps < 2)
            continue;
        step = total / (float)steps;
        for (side = 0; side < 2; ++side)
            for (k = 0; k <= steps; ++k)
            {
                float s0 = side ? total - (float)k * step : (float)k * step;
                V2    pos, dir;
                if (*n >= max)
                    break;
                pieces_at(pc, r->np, s0, &pos, &dir);
                poly[*n].x = pos.x + dir.y * (side ? -r->hw : r->hw);
                poly[*n].y = pos.y - dir.x * (side ? -r->hw : r->hw);
                ++*n;
            }
        snprintf(label, lab, "%s %d, %d,%d to %d,%d", r->band ? "band" : (r->f == net_thread->f ? "thread segment" : "line segment"), i, (int)r->col, (int)r->row, (int)r->cc, (int)r->cr);
        return 0;
    }
    return -1;
}
