/*  The network walk.  It runs from a node to the next node, along the
 *  pieces a family lays.  It holds the segment's stages, and the pass
 *  over the map that drives them (build_networks).  Family-specific
 *  decisions are answered by line.c, thread.c and band.c. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "dump.h"
#include "mesh/internal.h"
#include "log.h"
#include "pipeline.h"
#include "mesh/model.h"
#include "opt.h"
#include "script.h"

/*  The numbers this file reads, each remembering where the store
 *  put it (net/geo.c).  A name here is a name a script must set. */


static int s_measure; /* 1: the measuring walk: fit the paths, record the arms, draw nothing */

/*  Where a building pass's time goes, stage by stage (--times). */
static int gix_walk_cap_w = -1;
static double s_prof[NET_PROF_N];
void          net_prof_add(int stage, double amount)
{
    s_prof[stage] += amount;
}
double prof_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}
void net_prof_reset(void)
{
    memset(s_prof, 0, sizeof s_prof);
}
void net_prof_print(void)
{
    if (!g_dev.times)
        return;
    dumpf("time    segments: walk+fit %.1f, trims %.1f, lanes %.1f, overlay %.1f, caps %.1f | loft: sample %.1f (%d stations, %d lofts from the cache, %d sampled), ground %.1f, profile %.1f, slab works %.1f, record %.1f, slab %.1f (of which slabs and spurs %.1f) | junctions: lanes %.1f, box %.1f ms\n",
          s_prof[0],
          s_prof[1],
          s_prof[2],
          s_prof[3],
          s_prof[4],
          s_prof[5],
          (int)s_prof[13],
          (int)s_prof[14],
          (int)s_prof[15],
          s_prof[16],
          s_prof[6],
          s_prof[7],
          s_prof[8],
          s_prof[9],
          s_prof[10],
          s_prof[11],
          s_prof[12]);
    dumpf("time    bands: free air %.1f, bands %.1f, tints %.1f, spurs %.1f (of which their lofts %.1f), transitions %.1f, lane check %.1f ms\n",
          s_prof[NET_PROF_HW_AIR],
          s_prof[NET_PROF_BANDS],
          s_prof[NET_PROF_BAND_TINT],
          s_prof[NET_PROF_BAND_SPURS],
          s_prof[NET_PROF_SPUR_LOFT],
          s_prof[NET_PROF_HW_TRANS],
          s_prof[NET_PROF_HW_CHECK]);
}

/*  ==================================================================
 *  Walking the network
 *
 *  From a node to the next node: the tiles a run covers, the fit, the
 *  trims its junctions ask for, and the geometry.
 *  ================================================================== */
/*  A lone piece no neighbor joins.  It is a band across its own tile,
 *  along the axis its art links.  Both ends are capped, as the
 *  original's lone sprite. */
int build_island(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, int32_t col, int32_t row)
{
    Piece pc;
    int   links = tile_links(c, l, col, row, f), ns;
    float cx = (float)col + 0.5f, cy = (float)row + 0.5f;
    if (!links)
        return 0;
    ns     = (links & (L_N | L_S)) ? 1 : 0;
    pc.arc = 0;
    {
        const float in = net_family_rules(f)->tile_inset; /* a hair inside the tile, so the end stations read its surface */
        pc.a           = (V2){ns ? cx : cx - in, ns ? cy - in : cy};
        pc.b           = (V2){ns ? cx : cx + in, ns ? cy + in : cy};
    }
    pc.c   = pc.a;
    pc.len = 2.0f * net_family_rules(f)->tile_inset;
    pc.r   = 0.0f;
    pc.t0 = pc.t1 = 0.0f;
    {
        const NetFamily *fam = net_family(f);
        RLoft            d   = {0};
        d.f                  = f;
        d.fam                = fam;
        d.hw                 = *fam->width * 0.5f;
        d.mat                = fam->mat;
        d.kind               = fam->loft;
        d.cls                = -1.0f;
        d.node[0][0] = d.node[1][0] = col; /* both ends on the island's own tile */
        d.node[0][1] = d.node[1][1] = row;
        return loft(m, c, mask_bit, comp, &d, &pc, 1, 2.0f * net_family_rules(f)->tile_inset);
    }
}

/*  A node of a family's network: a junction (three or four links), an
 *  end (one), or nothing. */
/*  Where a segment ends on an end tile, and how.  The tile's art links
 *  that no neighbor returns point at the dead side.  What stands there
 *  decides: which byte is a carrier and which a building is the script's
 *  (`end_against`).  Returns 1 for a square end. */
static int end_point(const RCity *c, const RAtlasLevel *l, Family f, int32_t col, int32_t row, V2 *pt)
{
    int dead = tile_links(c, l, col, row, f) & ~eff_links(c, l, col, row, f), e;
    *pt      = (V2){(float)col + 0.5f, (float)row + 0.5f};
    for (e = 0; e < 4; ++e)
    {
        int32_t nc, nr;
        uint8_t b;
        if (!(dead & (1 << e)))
            continue;
        nc = col + (int32_t)SIDE_DU[e];
        nr = row + (int32_t)SIDE_DV[e];
        if (nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
            continue;
        b = script_bytes("end_against")[c->xbld[nr * R_MAP + nc]];
        if (b == 1) /* a carrier the line runs on into */
        {
            pt->x += SIDE_DU[e] * net_family_rules(f)->tile_inset;
            pt->y += SIDE_DV[e] * net_family_rules(f)->tile_inset;
            return 1;
        }
        if (b == 2 && net_family(f)->ends_at_buildings)
        {
            /*  A building: the line ends here.  Its turning head is
             *  drawn INSIDE its own tile.  The strip stops a cap's
             *  radius short of the edge, so the round cap lands just
             *  inside it.  The cap is a flat fan at the end's own
             *  height.  So it only goes on ground that can carry one.
             *  Which terrain bytes those are is the script's
             *  (`cap_ground`).  And anywhere else the end stays square. */
            if (script_bytes("cap_ground")[c->xter[row * R_MAP + col]])
            {
                pt->x += SIDE_DU[e] * (0.5f - LINE_W * 0.5f - 0.02f);
                pt->y += SIDE_DV[e] * (0.5f - LINE_W * 0.5f - 0.02f);
                return 0;
            }
            pt->x += SIDE_DU[e] * net_family_rules(f)->tile_inset;
            pt->y += SIDE_DV[e] * net_family_rules(f)->tile_inset;
            return 1;
        }
    }
    return 0;
}

/*  Cut `s` of length off the front of a piece chain, or off its back
 *  when `back`.  The chain keeps its shape exactly.  A line is shortened
 *  along itself.  An arc keeps its center and radius, and gives up some
 *  of its angle.  This is how a strip starts at the junction outline it
 *  meets: the same path the junction measured, cut where the junction
 *  said.  So the mouth is square and the two meet with nothing between
 *  them. */
static void pieces_trim(Piece *pc, int *np, float s, int back)
{
    while (s > 1e-4f && *np > 0)
    {
        Piece *p = back ? &pc[*np - 1] : &pc[0];
        if (p->len <= s + 1e-4f)
        {
            s -= p->len;
            if (!back)
                memmove(pc, pc + 1, (size_t)(*np - 1) * sizeof *pc);
            --*np;
            continue;
        }
        if (p->arc)
        {
            float sweep = p->t1 - p->t0;
            float dt    = (sweep >= 0.0f ? 1.0f : -1.0f) * s / p->r;
            if (back)
                p->t1 -= dt;
            else
                p->t0 += dt;
        }
        else
        {
            float dx = p->b.x - p->a.x, dy = p->b.y - p->a.y;
            float f = s / (p->len > 1e-6f ? p->len : 1.0f);
            if (back)
            {
                p->b.x -= dx * f;
                p->b.y -= dy * f;
            }
            else
            {
                p->a.x += dx * f;
                p->a.y += dy * f;
            }
        }
        p->len -= s;
        s = 0.0f;
    }
}
V2      s_wk_pts[MAX_PTS], s_wk_q[MAX_PTS];
float   s_wk_rad[MAX_PTS], s_wk_tlim[MAX_PTS];
int32_t s_wk_tcol[MAX_PTS], s_wk_trow[MAX_PTS], s_wk_marks[2 * MAX_PTS];
Piece   s_wk_pieces[MAX_PIECES];

/*  THE DISCOVERY.  From this node out along edge e, cell by cell, to the
 *  next node.  It reads the links and the node kinds and nothing else.
 *  No geometry, no mesh, no family beyond which links count: this is the
 *  whole of what "which cells form a segment" means.  It is what a
 *  script does instead (scripts/compose/network.lua). */
void net_walk_cells(const RCity *c, const RAtlasLevel *l, Family f, int32_t col, int32_t row, int e, NetRun *out)
{
    int32_t cc = col, cr = row;
    int     ee = e, back, guard = 0;
    out->n     = 0;
    out->stop  = NET_STOP_STUCK;
    out->exit  = -1;
    out->cell[out->n++] = row * R_MAP + col;
    for (;;)
    {
        int links, other;
        cc += (int32_t)SIDE_DU[ee];
        cr += (int32_t)SIDE_DV[ee];
        back = (ee + 2) & 3;
        if (cc < 0 || cr < 0 || cc >= R_MAP || cr >= R_MAP)
        {
            out->stop = NET_STOP_EDGE; /* the run leaves the map by the way it was going */
            out->exit = ee;
            return;
        }
        links = eff_links(c, l, cc, cr, f);
        if (!(links & (1 << back)))
        {
            out->stop = NET_STOP_STUCK; /* cannot happen on effective links.  Kept as a guard */
            out->exit = ee;
            return;
        }
        if (out->n + 2 >= MAX_PTS || ++guard > 4096)
        {
            out->stop = NET_STOP_CUT; /* the cell was entered and is not kept */
            out->exit = ee;
            return;
        }
        if (node_kind(c, l, f, cc, cr) != 0 || link_count(links) != 2)
        {
            out->cell[out->n++] = cr * R_MAP + cc;
            out->stop           = NET_STOP_NODE;
            return;
        }
        out->cell[out->n++] = cr * R_MAP + cc;
        other               = links & ~(1 << back);
        ee                  = other == L_N ? 0 : other == L_E ? 1
                                             : other == L_S   ? 2
                                                              : 3;
        out->exit           = ee;
        if (cc == col && cr == row)
        {
            out->stop = NET_STOP_LOOP; /* round to the cell it started from */
            return;
        }
    }
}

/*  And the segment BUILT from a run of cells.
 *
 *      The points the fit passes through.
 *      The corridor tiles.
 *      The visited marks and the far end's kind.
 *
 *  Everything here follows from the cells.  Nothing decides which they
 *  are. */
static int seg_from_cells(Seg *x, const NetRun *w)
{
    const RCity       *c    = x->c;
    const RAtlasLevel *l    = x->l;
    Family             f    = x->f;
    V2                *pts  = x->pts;
    int32_t           *tcol = x->tcol, *trow = x->trow, *marks = x->marks;
    float              hw   = x->hw;
    int                i, n = x->n, nt = x->nt, nm = x->nm;
    int32_t            cc = x->col, cr = x->row;
    int                back = (x->e + 2) & 3;
    if (w->n < 1)
        return 0;
    /*  The start.
     *
     *      Where the junction says this arm's strip begins.  Out along
     *      the arm's own direction.  At the distance its shape cleared.
     *      Or the end tile's center.
     *
     *  Before the junctions are shaped, that is the middle of the box's
     *  own side.  The same holds for a family that keeps its box. */
    if (x->kind0 == 2)
        pts[n++] = (V2){(float)x->col + 0.5f + SIDE_DU[x->e] * hw, (float)x->row + 0.5f + SIDE_DV[x->e] * hw};
    else
        x->square0 = end_point(c, l, f, x->col, x->row, &pts[n++]);
    tcol[nt]   = x->col;
    trow[nt++] = x->row;
    for (i = 1; i < w->n; ++i)
    {
        int last = i == w->n - 1;
        int ee;
        cc   = w->cell[i] % R_MAP;
        cr   = w->cell[i] / R_MAP;
        back = cc > w->cell[i - 1] % R_MAP   ? 3
               : cc < w->cell[i - 1] % R_MAP ? 1
               : cr > w->cell[i - 1] / R_MAP ? 0
                                             : 2;
        x->visited[(cr * R_MAP + cc) * 4 + back] = 1;
        if (nm < 2 * MAX_PTS)
            marks[nm++] = (cr * R_MAP + cc) * 4 + back;
        if (last && w->stop == NET_STOP_NODE)
        {
            x->kind1 = node_kind(c, l, f, cc, cr);
            if (x->kind1 == 0)
                x->kind1 = 1;
            if (x->kind1 == 2)
                pts[n++] = (V2){(float)cc + 0.5f + SIDE_DU[back] * hw, (float)cr + 0.5f + SIDE_DV[back] * hw};
            else
                x->square1 = end_point(c, l, f, cc, cr, &pts[n++]);
            if (nt < MAX_PTS)
            {
                tcol[nt]   = cc;
                trow[nt++] = cr;
            }
            break;
        }
        pts[n++] = (V2){(float)cc + 0.5f, (float)cr + 0.5f};
        if (nt < MAX_PTS)
        {
            tcol[nt]   = cc;
            trow[nt++] = cr;
        }
        /*  A cell the run passed through leaves by its other link.  That
         *  edge is marked too: the segment is then found from neither
         *  side twice. */
        ee = last ? w->exit
                  : (w->cell[i + 1] % R_MAP > cc   ? 1
                     : w->cell[i + 1] % R_MAP < cc ? 3
                     : w->cell[i + 1] / R_MAP > cr ? 2
                                                   : 0);
        if (ee >= 0)
        {
            x->visited[(cr * R_MAP + cc) * 4 + ee] = 1;
            if (nm < 2 * MAX_PTS)
                marks[nm++] = (cr * R_MAP + cc) * 4 + ee;
        }
    }
    /*  The two runs that end between cells: off the map, where the last
     *  cell's own side carries the point, and cut short.  There the cell
     *  the run reached is marked and kept out of the corridor. */
    if (w->stop == NET_STOP_EDGE)
    {
        pts[n++] = (V2){(float)cc + 0.5f + SIDE_DU[w->exit] * 0.5f,
                        (float)cr + 0.5f + SIDE_DV[w->exit] * 0.5f};
        cc += (int32_t)SIDE_DU[w->exit];
        cr += (int32_t)SIDE_DV[w->exit];
        back     = (w->exit + 2) & 3;
        x->kind1 = 0;
    }
    else if (w->stop == NET_STOP_CUT || w->stop == NET_STOP_STUCK)
    {
        cc += (int32_t)SIDE_DU[w->exit];
        cr += (int32_t)SIDE_DV[w->exit];
        back = (w->exit + 2) & 3;
        if (w->stop == NET_STOP_CUT)
        {
            x->visited[(cr * R_MAP + cc) * 4 + back] = 1;
            if (nm < 2 * MAX_PTS)
                marks[nm++] = (cr * R_MAP + cc) * 4 + back;
        }
    }
    x->nt   = nt;
    x->n    = n;
    x->nm   = nm;
    x->cc   = cc;
    x->cr   = cr;
    x->back = back;
    return 0;
}

/*  The tiles a drawn segment serves.  It is every tile of a segment
 *  whose strip was lofted, whether or not the strip runs over that tile.
 *  A family fitted across the free ground beside its tiles leaves some
 *  bare, by design.  The check (mesh/check.c) must not read those as a
 *  piece the walk could not draw. */
static uint8_t s_served[R_MAP * R_MAP];

void net_serve_tiles(const int32_t *tcol, const int32_t *trow, int nt)
{
    int i;
    for (i = 0; i < nt; ++i)
        if (tcol[i] >= 0 && trow[i] >= 0 && tcol[i] < R_MAP && trow[i] < R_MAP)
            s_served[trow[i] * R_MAP + tcol[i]] = 1;
}

int net_tile_served(int32_t i)
{
    return i >= 0 && i < R_MAP * R_MAP && s_served[i];
}

/*  Stages one and two: the corridor and the fit through it, the pieces the strip is lofted from. */
static int seg_fit(Seg *x)
{
    RMesh       *m        = x->m;
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    Family       f        = x->f;
    int32_t      col      = x->col;
    int32_t      row      = x->row;
    int          e        = x->e;
    V2          *pts      = x->pts;
    V2          *q        = x->q;
    float       *rad      = x->rad;
    float       *tlim     = x->tlim;
    int32_t     *tcol     = x->tcol;
    int32_t     *trow     = x->trow;
    Piece       *pieces   = x->pieces;
    int          nt       = x->nt;
    float        hw       = x->hw;
    int          n        = x->n;
    int          k        = x->k;
    int          nk;
    int          np       = x->np;
    int          kind0    = x->kind0;
    int          kind1    = x->kind1;
    int32_t      cc       = x->cc;
    int32_t      cr       = x->cr;
    /*  Stage one and two (net/fit.c): the corridor is the segment's own
     *  tiles, its gates the crossable part of each shared edge.  The
     *  path is the taut string through them.  It cuts every corner of a
     *  staircase into one diagonal by itself, with each corner then
     *  swept as wide as the corridor allows.  The corridor is tested
     *  against the line's own half width, not a fraction of it: what has
     *  to fit inside the corridor is the line. */
    const NetFamily *fam = net_family(f);
    (void)fam, (void)kind0, (void)kind1, (void)cc, (void)cr, (void)nt, (void)hw;
    nk = net_seg_fit_of(f, col, row, e, q, rad, tlim, MAX_PTS);
    /*  The corridor under the curve overlay: the segment's own tiles, in
     *  tan, which for a line is all the fit may use. */
    if (s_tune.show_curves > 0.5f && s_pass != 1)
    {
        int ct;
        for (ct = 0; ct < nt; ++ct)
        {
            if (tile_highlight(m, c, mask_bit, tcol[ct], trow[ct], 3.0f) != 0)
                return -1;
        }
    }
    if (nk < 2)
        return 1; /* nothing to draw: the walk found no path */
    {
        /*  --line-dump prints every segment of six tiles or more.
         *  --line-dump C,R every segment through that tile.  The switch
         *  is value-OPTIONAL, so its presence is asked separately from
         *  its value: reading the value alone made a bare --line-dump
         *  print nothing at all. */
        const int   on   = g_dev.line_dump;
        const char *dump = on ? g_dev.line_dump_at : NULL;
        int         dc = -1, dr = -1, show = 0;
        if (dump && sscanf(dump, "%d,%d", &dc, &dr) == 2)
        {
            for (k = 0; k < n; ++k)
                if ((int32_t)floorf(pts[k].x) == dc && (int32_t)floorf(pts[k].y) == dr)
                    show = 1;
        }
        else if (on && n >= 6)
            show = 1;
        if (show)
        {
            dumpf("segment f%d from c%d r%d e%d: %d points, %d kept:", (int)f, (int)col, (int)row, e, n, nk);
            for (k = 0; k < nk; ++k)
                dumpf(" (%.2f,%.2f)", (double)q[k].x, (double)q[k].y);
            dumpf("\n  raw:");
            for (k = 0; k < n; ++k)
                dumpf(" (%.2f,%.2f)", (double)pts[k].x, (double)pts[k].y);
            dumpf("\n");
        }
    }
    /*  And the pieces the drive cut from it, each corner on the radius
     *  its room allowed.  The cut is arc.rules.pieces's and was made
     *  when the path was fitted. */
    if (net_seg_pieces_of(f, col, row, e, pieces, MAX_PIECES, &np) != 0 || np == 0)
    {
        if (g_dev.path_dump)
            dumpf("  no pieces: %d points\n", nk);
        return 1; /* nothing to draw */
    }
    x->nt    = nt;
    x->hw    = hw;
    x->n     = n;
    x->k     = k;
    x->nk    = nk;
    x->np    = np;
    x->kind0 = kind0;
    x->kind1 = kind1;
    x->cc    = cc;
    x->cr    = cr;
    return 0;
}

/*  Stage three's measurement: which way this segment leaves each junction it touches. */
int seg_measure_arms(Seg *x)
{
    Family  f      = x->f;
    int32_t col    = x->col;
    int32_t row    = x->row;
    int     e      = x->e;
    Piece  *pieces = x->pieces;
    int     np     = x->np;
    int     kind0  = x->kind0;
    int     kind1  = x->kind1;
    int32_t cc     = x->cc;
    int32_t cr     = x->cr;
    int32_t back   = x->back;
    float   total  = x->total;
    /*  Stage three's measurement: which way this segment leaves each
     *  junction it touches.  The direction is the fitted path's own at
     *  that end, so a segment that leaves at an angle says so. */
    if (kind0 == 2 || kind1 == 2)
    {
        V2 pos, dir;
        if (kind0 == 2)
        {
            RArm *a = &s_arm[FAMX(f)][(row * R_MAP + col) * 4 + e];
            arm_heading(pieces, np, total, 0, &pos, &dir);
            a->ax    = pos.x;
            a->ay    = pos.y;
            a->dx    = dir.x;
            a->dy    = dir.y;
            a->len   = total;
            a->fcol  = cc;
            a->frow  = cr;
            a->fe    = (int8_t)back;
            a->fkind = (int8_t)kind1;
            a->cls   = (int8_t)(net_family(f)->classed ? (int)x->cls : -1);
            a->have  = 1;
        }
        if (kind1 == 2)
        {
            RArm *a = &s_arm[FAMX(f)][(cr * R_MAP + cc) * 4 + back];
            arm_heading(pieces, np, total, 1, &pos, &dir);
            a->ax    = pos.x;
            a->ay    = pos.y;
            a->dx    = dir.x; /* arm_heading already points away from the junction */
            a->dy    = dir.y;
            a->len   = total;
            a->fcol  = col;
            a->frow  = row;
            a->fe    = (int8_t)e;
            a->fkind = (int8_t)kind0;
            a->cls   = (int8_t)(net_family(f)->classed ? (int)x->cls : -1);
            a->have  = 1;
        }
    }
    x->np    = np;
    x->kind0 = kind0;
    x->kind1 = kind1;
    x->cc    = cc;
    x->cr    = cr;
    x->back  = back;
    x->total = total;
    return 0;
}

/*  Cut the strip back to the outline each junction gave it. */
static int seg_trim(Seg *x)
{
    Family  f      = x->f;
    int32_t col    = x->col;
    int32_t row    = x->row;
    int     e      = x->e;
    Piece  *pieces = x->pieces;
    int     k      = x->k;
    int     np     = x->np;
    int     kind0  = x->kind0;
    int     kind1  = x->kind1;
    int32_t cc     = x->cc;
    int32_t cr     = x->cr;
    int32_t back   = x->back;
    float   total  = x->total;
    /*  Cut the strip back to the outline each junction gave it.  The
     *  path itself is untouched.  The drawing pass fits exactly what the
     *  measuring pass measured, so the cut lands on the mouth the
     *  junction cut for it. */
    if (kind0 == 2 || kind1 == 2)
    {
        /*  The cut each end is given, the one the ports and a turnout's
         *  box use (lane.c arm_cut).  Never eating the segment: between
         *  two adjacent junctions a segment is about a tile long.  Two
         *  trims of 0.45 would leave a floating stub with nothing
         *  joining it to either end.  And, for a turnout, no further
         *  than the straight run from the mouth.  So the strip starts
         *  where the lane's port is.  A turnout's cut trims what the
         *  segment DRAWS.  The GRADE is never trimmed.  The grading pass
         *  lofts a whole thread segment whole, out to the mouth, so the
         *  ground under the box's strip is leveled. */
        int   cut = s_pass != 1 || net_family(f)->turnout <= 0.0f;
        float t0  = kind0 == 2 && cut ? arm_cut(f, col, row, e) : 0.0f;
        float t1  = kind1 == 2 && cut ? arm_cut(f, cc, cr, back) : 0.0f;
        if (t0 > 0.0f)
            pieces_trim(pieces, &np, t0, 0);
        if (t1 > 0.0f)
            pieces_trim(pieces, &np, t1, 1);
        if (np < 1)
            return 1; /* the trims ate it: nothing to draw */
        total = 0.0f;
        for (k = 0; k < np; ++k)
            total += pieces[k].len;
    }
    x->k     = k;
    x->np    = np;
    x->kind0 = kind0;
    x->kind1 = kind1;
    x->cc    = cc;
    x->cr    = cr;
    x->back  = back;
    x->total = total;
    return 0;
}

/*  The fit's nodes under the curve overlay. */
static int seg_overlay(Seg *x)
{
    RMesh       *m        = x->m;
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    V2          *q        = x->q;
    float       *rad      = x->rad;
    int          nk       = x->nk;
    /*  The nodes the fit produced, when the overlay is on: one mark per
     *  vertex of the polyline the pieces were built from.  So the
     *  spacing between them can be read directly and every corner that
     *  came out hard is visible as such.  Amber where the corner was
     *  swept into an arc, red where no legal radius fitted and the line
     *  simply turns.  Not under the spline fit: it has no fillets.  So
     *  every node would read as "no arc".  At that sampling the marks
     *  merge into a ribbon that hides the very curve they are there to
     *  explain. */
    if (s_tune.show_curves > 0.5f && s_pass != 1)
    {
        int k3;
        for (k3 = 0; k3 < nk; ++k3)
        {
            int32_t tc = (int32_t)floorf(q[k3].x), tr = (int32_t)floorf(q[k3].y);
            float   paint, z, half;
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            /*  An end node has no corner to sweep, so it is neither. */
            paint = (k3 == 0 || k3 + 1 == nk) ? 7.0f : (rad[k3] > 0.001f ? 3.0f : 1.0f);
            /*  Under the tangent fit a vertex with no turn is a line's
             *  end, not a corner that failed: shown as an end. */
            if (k3 > 0 && k3 + 1 < nk && rad[k3] <= 0.001f)
            {
                V2    u1 = {q[k3].x - q[k3 - 1].x, q[k3].y - q[k3 - 1].y};
                V2    u2 = {q[k3 + 1].x - q[k3].x, q[k3 + 1].y - q[k3].y};
                float l1 = v2len(u1), l2 = v2len(u2);
                if (l1 > 1e-5f && l2 > 1e-5f && (u1.x * u2.x + u1.y * u2.y) / (l1 * l2) > 0.9999f)
                    paint = 7.0f;
            }
            (void)half;
            z    = surface_at_world(c, mask_bit, q[k3].x, q[k3].y);
            /*  The mark is a model (scripts/models.lua): a line's end
             *  is a smaller one than a turn. */
            if (net_model_put_on(net_model_find(k3 == 0 || k3 + 1 == nk ? "node_end" : "node_mid"),
                                 m, c, mask_bit, tile_order(c, tc, tr, mask_bit),
                                 q[k3].x, q[k3].y, 1.0f, 0.0f, 0.0f, paint, 0.0f, z, z, 1) != 0)
                return 0;
        }
    }
    x->nk = nk;
    return 0;
}

/*  A dead end's round cap, the turning head. */
static int seg_caps(Seg *x)
{
    ShapeId      sh;
    int          records_only = x->records_only;
    RMesh       *m            = x->m;
    const RCity *c            = x->c;
    uint8_t      mask_bit     = x->mask_bit;
    int          comp         = x->comp;
    Family       f            = x->f;
    Piece       *pieces       = x->pieces;
    float        hw           = x->hw;
    int          np           = x->np;
    int          kind0        = x->kind0;
    int          kind1        = x->kind1;
    int          square0      = x->square0;
    int          square1      = x->square1;
    if (net_family(f)->caps)
    {
        int which;
        for (which = 0; which < 2; ++which)
        {
            int   at_end = which == 1;
            int   end    = at_end ? np - 1 : 0;
            V2    pos, dir;
            float h, ang;
            if (at_end ? (kind1 != 1 || square1) : (kind0 != 1 || square0))
                continue;
            /*  Every dead end gets its round cap.  Spec 3.10's step 11
             *  keeps the turning head for a local line with open land
             *  around it.  Ends an avenue square, but a square end in
             *  the middle of a tile is a raw edge, so the cap is
             *  unconditional. */
            sh = shape_open("turning head at %d,%d", (int)(at_end ? x->cc : x->col), (int)(at_end ? x->cr : x->row));
            piece_at(&pieces[end], at_end ? pieces[end].len : 0.0f, &pos, &dir);
            if (!at_end)
            {
                dir.x = -dir.x;
                dir.y = -dir.y;
            }
            h   = hw * width_factor(dir.x, dir.y, comp);
            ang = atan2f(dir.y, dir.x);
            {
                int32_t tc = (int32_t)floorf(pos.x), tr = (int32_t)floorf(pos.y);
                if (tc < 0)
                    tc = 0;
                if (tr < 0)
                    tr = 0;
                if (tc >= R_MAP)
                    tc = R_MAP - 1;
                if (tr >= R_MAP)
                    tr = R_MAP - 1;
                if (!records_only && strip_fan_z(m, c, mask_bit, tile_order(c, tc, tr, mask_bit), pos.x, pos.y, ang - 1.5707963f, ang + 1.5707963f, h, h, 0.0f, MAT_LINE, 8, 0.03f) != 0)
                    return -1;
                /* the margin round the cap, from the strip's one side to its other */
                const float ck = net_family_rules(f)->cap_lip;
                V2          c0 = {pos.x + dir.y * h * ck, pos.y - dir.x * h * ck};
                V2          c1 = {pos.x - dir.y * h * ck, pos.y + dir.x * h * ck};
                margin_add(MARGIN_CAP, c0, c1, (V2){0.0f, 0.0f}, (V2){0.0f, 0.0f});
                {
                    /*  The cap, as the network holds it.
                     *
                     *      A terminus closed round its head.
                     *      Naming both sides of the arm that ends here.
                     *      So the walk turns rather than stopping. */
                    WalkPath w;
                    int32_t  nc = at_end ? x->cc : x->col, nr = at_end ? x->cr : x->row;
                    int      ne = at_end ? (int)x->back : x->e;
                    memset(&w, 0, sizeof w);
                    w.kind    = WALK_CAP;
                    w.col     = nc;
                    w.row     = nr;
                    w.e       = ne;
                    w.w       = hw * net_geo(&gix_walk_cap_w, "walk_cap_w");
                    w.end[0]  = c0;
                    w.end[1]  = c1;
                    w.port[0] = walk_port(nc, nr, ne, 0);
                    w.port[1] = walk_port(nc, nr, ne, 1);
                    walk_net_add(&w, NULL, 0);
                }
            }
            shape_close(sh);
        }
    }
    x->hw      = hw;
    x->np      = np;
    x->kind0   = kind0;
    x->kind1   = kind1;
    x->square0 = square0;
    x->square1 = square1;
    return 0;
}

/*  The segment the walk finished, held for the drive: it composes the
 *  strip, and walk_segment_done takes up what follows it. */
static Seg s_seg_hold;
static int s_seg_live;

int walk_segment(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, const NetRun *run, uint8_t *visited)
{
    V2              *pts = s_wk_pts, *q = s_wk_q;
    float           *rad = s_wk_rad, *tlim = s_wk_tlim;
    int32_t         *tcol = s_wk_tcol, *trow = s_wk_trow, *marks = s_wk_marks;
    Piece           *pieces = s_wk_pieces;
    Seg              x;
    const NetFamily *fam = net_family(f);
    int32_t          col = run->cell[0] % R_MAP, row = run->cell[0] / R_MAP;
    int              e   = net_run_edge(run);
    int              k, kept = -1;
    if (visited[(row * R_MAP + col) * 4 + e])
        return 0;
    visited[(row * R_MAP + col) * 4 + e] = 1;
    memset(&x, 0, sizeof x);
    x.m = m, x.c = c, x.l = l, x.mask_bit = mask_bit, x.comp = comp, x.f = f, x.col = col, x.row = row, x.e = e, x.visited = visited;
    x.pts = pts, x.q = q, x.rad = rad, x.tlim = tlim, x.tcol = tcol, x.trow = trow, x.pieces = pieces, x.marks = marks;
    x.hw    = *fam->width * 0.5f;
    x.kind0 = node_kind(c, l, f, col, row);
    x.cc = col, x.cr = row, x.back = (e + 2) & 3, x.ee = e;
    /*  The drawing walk reads the segment the measuring walk kept, and
     *  only walks and fits it again if the table could not hold it. */
    if (!s_measure)
        kept = s_seg_at[(row * R_MAP + col) * 4 + e];
    if (kept >= 0)
    {
        seg_load(&x, &s_segs[kept]);
        goto trims;
    }
    /*  The stages: walk the tiles to the far node.  The class.  The fit
     *  through the corridor.  The arms for stage three, and in the
     *  measuring pass the meets and nothing more.  The trims the
     *  junctions gave.  The lanes.  The overlay.  The loft.  The caps. */
    double tp = prof_now();
    if (seg_from_cells(&x, run) != 0)
        return -1;
    if (x.n < 2)
        return 0;
    seg_class(&x);
    {
        int r = seg_fit(&x); /* 1: nothing to draw */
        if (r != 0)
            return r < 0 ? -1 : 0;
    }
    net_prof_add(NET_PROF_WALK_FIT, prof_now() - tp);
    for (k = 0; k < x.np; ++k)
        x.total += pieces[k].len;
    seg_measure_arms(&x);
    if (s_measure)
    {
        seg_store(&x); /* kept for the drawing walk */
        return seg_measure_laps(&x);
    }
trims:
    {
        double t1 = prof_now();
        int    r  = seg_trim(&x); /* 1: the trims ate the segment */
        net_prof_add(NET_PROF_TRIMS, prof_now() - t1);
        if (r != 0)
            return r < 0 ? -1 : 0;
    }
    /*  The segment's two lanes, a line's two lanes, a line's two
     *  threads, from the same trimmed pieces the strip is lofted from
     *  (lane.c). */
    double t2 = prof_now();
    if (lane_segment(m, c, mask_bit, f, pieces, x.np, col, row, e, x.kind0, x.cc, x.cr, x.back, x.kind1, x.hw, fam->classed ? (int)x.cls : -1) != 0)
        return -1;
    net_prof_add(NET_PROF_LANES, prof_now() - t2), t2 = prof_now();
    if (seg_overlay(&x) != 0)
        return -1;
    net_prof_add(NET_PROF_OVERLAY, prof_now() - t2);
    {
        /* a line or thread strip: the class's markings, a stripe at a controlled leg, pinned at a junction */
        RLoft d      = {0};
        d.f          = f;
        d.fam        = fam;
        d.hw         = *fam->width * 0.5f;
        d.mat        = fam->mat;
        d.kind       = fam->loft;
        d.node[0][0] = col;
        d.node[0][1] = row;
        d.node[1][0] = x.cc;
        d.node[1][1] = x.cr;
        d.arm[0]     = e;      /* the arm it leaves each node by, for the margin's ports */
        d.arm[1]     = x.back;
        d.nkind[0]   = x.kind0;
        d.nkind[1]   = x.kind1;
        d.ctrl[0]    = (net_family_has(fam, NH_CONTROL) && x.kind0 == 2) ? (s_junc_ctrl[row * R_MAP + col] >> (2 * e)) & 3 : 0;
        d.ctrl[1]    = (net_family_has(fam, NH_CONTROL) && x.kind1 == 2) ? (s_junc_ctrl[x.cr * R_MAP + x.cc] >> (2 * x.back)) & 3 : 0;
        /*  The band each junction takes from this strip's end for its
         *  meet: the junction lays it (net/margin.c), square to its own
         *  mouth.  The slab leaves that much of its way bare so the two
         *  meet rather than lap. */
        d.xw0        = x.kind0 == 2 ? net_cross_depth(f, col, row, e) : 0.0f;
        d.xw1        = x.kind1 == 2 ? net_cross_depth(f, x.cc, x.cr, x.back) : 0.0f;
        d.pin0       = x.kind0 == 2;
        d.pin1       = x.kind1 == 2;
        d.cls        = fam->classed ? x.cls : -1.0f;
        d.cache      = kept + 1;
        d.hash       = pieces_hash(pieces, x.np, x.total);
        /*  Does the strip reach a chunk this build draws?  In an edit's
         *  build most do not.  Their slabs would be dropped triangle by
         *  triangle: they keep their stations, ground, profile and
         *  records and draw nothing. */
        d.records_only = s_incr_on;
        for (k = 0; k < x.nt && d.records_only; ++k)
            if (mesh_want_tile(tcol[k], trow[k]))
                d.records_only = 0;
        x.records_only = d.records_only;
        for (k = 0; k < x.nt && !d.hot; ++k)
            if (mesh_incr_near(tcol[k], trow[k]))
                d.hot = 1;
        if (loft(m, c, mask_bit, comp, &d, pieces, x.np, x.total) != 0)
            return -1;
        s_seg_hold = x;
        s_seg_live = 1;
    }
    return 0;
}

/*  What follows the strip: the tiles it serves, wherever the line runs,
 *  and the caps at its dead ends.  The strip itself is composed between
 *  the two, so this is where the loft's shape is closed. */
int build_draw_done(void)
{
    double t3;
    int    rc;
    if (net_loft_close() != 0)
        return -1;
    if (lane_segment_caps() != 0)
        return -1;
    if (!s_seg_live)
        return 0;
    s_seg_live = 0;
    net_serve_tiles(s_seg_hold.tcol, s_seg_hold.trow, s_seg_hold.nt);
    t3 = prof_now();
    rc = seg_caps(&s_seg_hold);
    net_prof_add(NET_PROF_CAPS, prof_now() - t3);
    return rc;
}

/*  Bucket the opaque list by chunk.  Within a chunk the terrain comes
 *  first and the networks after.  It is a counting sort of the triangles
 *  by the tile their first vertex lies on, into the scratch, then the
 *  buffers swap.  The triangle set is untouched.  Only its order, which
 *  the depth buffer makes free.  Called once, after the building pass. */
/*  ==================================================================
 *  The driver
 *
 *  The pass over the map: both families measured, the junction shapes,
 *  the meets and power, then the junctions and segments drawn.
 *  ================================================================== */
static int s_comp; /* the width compensation this build lofts with */

/*  How far an arm's strip runs STRAIGHT from its mouth, up to `want`.  A
 *  stripe is painted between two parallel margins.  The two margins are
 *  the strip's own sides.  So the line under a band has to hold its
 *  heading over the whole of it.  An arc turns at one radius per unit
 *  length.  The band may run only as far as that turn stays inside the
 *  angle two margins may differ by. */
static float arm_straight(Family f, int32_t col, int32_t row, int e, float want)
{
    static Piece pc[MAX_PIECES];
    float        cut = arm_cut(f, col, row, e), turn = 0.0f, got = 0.0f;
    const float  tol = acosf(net_family_rules(f)->parallel);
    int          np, k;
    if (seg_table_pieces_from(col, row, e, pc, (int)(sizeof pc / sizeof pc[0]), &np) != 0)
        return 0.0f;
    if (cut > 0.0f)
        pieces_trim(pc, &np, cut, 0);
    for (k = 0; k < np && got < want; ++k)
    {
        float rate = pc[k].arc && fabsf(pc[k].r) > 1e-4f ? 1.0f / fabsf(pc[k].r) : 0.0f;
        float room = rate > 0.0f ? (tol - turn) / rate : want;
        float use  = pc[k].len;
        if (use > room)
            use = room > 0.0f ? room : 0.0f;
        got += use;
        turn += use * rate;
        if (use < pc[k].len - 1e-5f)
            break; /* the turn ran out inside this piece */
    }
    return got < want ? got : want;
}

/*  Stage three: each junction takes its shape from its arms and hands
 *  each of them back the length to start at.  It is a computation and
 *  nothing else.  No geometry comes out of it.  So it is worked out
 *  ONCE, by the grading pass, and the building pass reads the table it
 *  left.  It may: the arms it reads are measured from the fitted path
 *  BEFORE any trim.  Walk_segment runs seg_measure_arms ahead of
 *  seg_trim.  And the building pass replays the very segments the
 *  grading pass fitted, so both passes were working the same numbers.
 *  Doing it twice was also how the passes came to disagree about a
 *  thread arm's cut and left a raw slope under a junction's strip
 * . */
/*  Stage three's reading: which junctions there are, so the drive can
 *  have each one's control answered before anything that turns on it is
 *  measured.  The outline is worked out here only to know that there IS
 *  a junction.  It reads the arm table the fit left and nothing stage
 *  three writes, so the walk below arrives at the same outlines. */
static void net_stage_three_ask(const RCity *c, const RAtlasLevel *l)
{
    Family fam;
    int    fk, k;
    for (fk = 0; fk < net_n_walked; ++fk)
    {
        if (!net_walked[fk]->lips)
            continue;
        fam = net_walked[fk]->f;
        if (!net_family_has(net_family(fam), NH_CONTROL))
            continue;
        for (k = 0; k < net_disc_junctions(fk); ++k)
        {
            V2            poly[JUNC_MAX];
            JuncArm       arms[4];
            float         trm[4];
            const int32_t cell = net_disc_junction(fk, k);
            const int32_t col = cell % R_MAP, row = cell / R_MAP;
            int           links = eff_links(c, l, col, row, fam);
            if (junction_poly(c, fam, col, row, links, poly, NULL, JUNC_MAX, trm, arms) < 3)
                continue;
            net_family_control_ask(net_family(fam), c, col, row, links);
        }
    }
}


/*  ---- the junctions whose margin the drive asks for -----------------
 *
 *  Stage three's first reading is the margin round each junction: the
 *  trim it hands each arm, and which of its mouths want a meet.  Both
 *  follow from how the margin sits on the ring.  This is a rule, so the
 *  DRIVE walks these and asks, the ring, then each mouth, and this only
 *  measures.  Nothing here calls up.
 *
 *  The grading pass does it and the building pass reads what it left, so
 *  outside the grading pass there is nothing to walk. */
static struct
{
    const RCity       *c;
    const RAtlasLevel *l;
    Family             fam;
    int                fk, at;
    int32_t            col, row;
    int                np;
    V2                 poly[JUNC_MAX];
    JuncArm            arms[4];
    float              trm[4];
    float              lw;
} s_trimj;
static int s_n_trimj;

int net_trim_junctions(const RCity *c, const RAtlasLevel *l)
{
    int fk, n = 0;
    memset(&s_trimj, 0, sizeof s_trimj);
    s_trimj.c = c, s_trimj.l = l;
    s_n_trimj = 0;
    if (s_pass == 2)
        return 0;
    for (fk = 0; fk < net_n_walked; ++fk)
        if (net_walked[fk]->lips)
            n += net_disc_junctions(fk);
    s_n_trimj = n;
    return n;
}

/*  Junction i of that walk: its ring, ready to be read as a margin.
 *  Answers the handle the drive hands the rule.  Nothing where the
 *  outline is not a ring at all. */
static int trim_at(int i)
{
    int fk, n = 0;
    for (fk = 0; fk < net_n_walked; ++fk)
    {
        int k;
        if (!net_walked[fk]->lips)
            continue;
        k = net_disc_junctions(fk);
        if (i - n < k)
        {
            int32_t cell = net_disc_junction(fk, i - n);
            s_trimj.fk   = fk;
            s_trimj.fam  = net_walked[fk]->f;
            s_trimj.col  = cell % R_MAP;
            s_trimj.row  = cell / R_MAP;
            s_trimj.lw   = *net_family(s_trimj.fam)->width * 0.5f *
                         (1.0f - net_family_rules(s_trimj.fam)->inner);
            s_trimj.np = junction_poly(s_trimj.c, s_trimj.fam, s_trimj.col, s_trimj.row,
                                       eff_links(s_trimj.c, s_trimj.l, s_trimj.col, s_trimj.row, s_trimj.fam),
                                       s_trimj.poly, NULL, JUNC_MAX, s_trimj.trm, s_trimj.arms);
            return 1;
        }
        n += k;
    }
    return 0;
}

void *net_trim_band(int i)
{
    JBox jb;
    if (i < 0 || i >= s_n_trimj || !trim_at(i) || s_trimj.np < 3)
        return NULL;
    memset(&jb, 0, sizeof jb);
    jb.c = s_trimj.c, jb.f = s_trimj.fam, jb.col = s_trimj.col, jb.row = s_trimj.row, jb.lw = s_trimj.lw;
    s_trimj.at = i;
    return margin_band_ask(&jb, s_trimj.poly, s_trimj.arms, s_trimj.np, s_trimj.lw);
}

/*  The mouths of the junction in hand, off the band just answered. */
int net_trim_mouths(void)
{
    return margin_mouths_ask(s_trimj.poly, s_trimj.np, s_trimj.lw);
}

/*  And what follows: the trim each arm is handed, and how deep a band
 *  each mouth that asked for a meet wants. */
void net_trim_done(int i)
{
    float want[4];
    int   k;
    if (i < 0 || i >= s_n_trimj || s_trimj.at != i || s_trimj.np < 3)
        return;
    for (k = 0; k < 4; ++k)
        s_trim[FAMX(s_trimj.fam)][(s_trimj.row * R_MAP + s_trimj.col) * 4 + k] = s_trimj.trm[k];
    margin_junction_wants(s_trimj.c, s_trimj.fam, s_trimj.col, s_trimj.row,
                            s_trimj.poly, s_trimj.arms, s_trimj.np, s_trimj.lw, want);
    for (k = 0; k < 4; ++k)
        s_xwalk[FAMX(s_trimj.fam)][(s_trimj.row * R_MAP + s_trimj.col) * 4 + k] = want[k];
}

static void net_stage_three(const RCity *c, const RAtlasLevel *l)
{
    Family fam;
    int    fk;
    /*  What is left of stage three, once the drive has had every
     *  junction's margin answered and its trims taken.  It gives how
     *  much of the band each arm can actually spare.  A meet is line
     *  given up, and two of them must still leave a line between the
     *  junctions they belong to.  A band too shallow to read as a meet
     *  is dropped and the arm keeps the line.
     *
     *  It is worked out ONCE, in the grading pass, and the building pass
     *  reads what it left.  It may: the arms it reads are measured from
     *  the fitted path BEFORE any trim.  Walk_segment runs
     *  seg_measure_arms ahead of seg_trim.  And the building pass
     *  replays the very segments the grading pass fitted, so both passes
     *  work the same numbers.  Working it twice is how the two passes
     *  come to disagree about a thread arm's cut and leave a raw slope
     *  under a junction's strip. */
    for (fk = 0; fk < net_n_walked; ++fk)
    {
        int j;
        if (!net_walked[fk]->lips)
            continue;
        fam = net_walked[fk]->f;
        for (j = 0; j < net_disc_junctions(fk); ++j)
        {
            const int32_t cell = net_disc_junction(fk, j);
            const int32_t col = cell % R_MAP, row = cell / R_MAP;
                for (int e = 0; e < 4; ++e)
                {
                    int         ix = (row * R_MAP + col) * 4 + e, fx = FAMX(fam);
                    const RArm *a  = &s_arm[fx][ix];
                    float       room, straight, want = s_xwalk[fx][ix];
                    if (!(want > 0.0f))
                        continue;
                    room     = a->have ? a->len - s_trim[fx][ix] - (a->fkind == 2 ? s_trim[fx][(a->frow * R_MAP + a->fcol) * 4 + a->fe] : 0.0f) : 0.0f;
                    straight = arm_straight(fam, col, row, e, want);
                    /*  How deep the band runs is the SCRIPT'S
                     *  (arc.rules.meet), from the three measurements
                     *  this makes for it.  What the outline asked for,
                     *  the line there is to give up.  How much of it
                     *  runs straight from the mouth.  The mouth keeps no
                     *  band until the drive answers it. */
                    s_xwalk[fx][ix] = 0.0f;
                    net_xwalk_ask(col, row, e, fx, (s_junc_ctrl[row * R_MAP + col] >> (2 * e)) & 3,
                                  want, room, straight, net_family_rules(fam)->cross_share * room);
                }
        }
    }
    /*  A family whose junction is a turnout hands each arm the reach
     *  instead.  The box draws the through line whole across it and the
     *  branch's threads curving at its radius, so the arm's strip starts
     *  there.  A short arm scales its cut back with its far end's
     *  (lane.c arm_cut). */
    for (fk = 0; fk < net_n_walked; ++fk)
    {
        const NetFamily *tf = net_walked[fk];
        int              j;
        fam = tf->f;
        if (!(tf->turnout > 0.0f))
            continue;
        for (j = 0; j < net_disc_junctions(fk); ++j)
        {
            const int32_t cell = net_disc_junction(fk, j);
            const int32_t col = cell % R_MAP, row = cell / R_MAP;
            int           links = eff_links(c, l, col, row, fam), k;
            for (k = 0; k < 4; ++k)
                if (links & (1 << k))
                    s_trim[FAMX(fam)][(row * R_MAP + col) * 4 + k] = tf->turnout - *tf->width * 0.5f;
        }
    }
}

/*  ... for a box's own lofts (thread.c thread_loft), which loft as a segment does. */
int net_compensate(void)
{
    return s_comp;
}

int build_networks(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, uint8_t mask_bit, int comp)
{
    s_comp = comp;
    static uint8_t visited[R_MAP * R_MAP * 4];
    Family         fam;
    int            fk;
    (void)a;
    /*  Stage two, for both families at once: every segment fits its path
     *  and records what the later stages need.  Which way it leaves each
     *  junction, and where it passes each tile.  Nothing is drawn.  The
     *  meets then have both paths to build from, which is what lets a
     *  two families meet at whatever angle they actually meet at. */
    double tt = tms();
    memset(s_arm, 0, sizeof s_arm);
    if (s_pass != 2)
    {
        /* stage three's own tables: worked out once, in the grading pass, and read by the building pass */
        memset(s_trim, 0, sizeof s_trim);
        memset(s_xwalk, 0, sizeof s_xwalk);
    }
    memset(s_cross, 0, sizeof s_cross);
    lane_reset();
    memset(s_served, 0, sizeof s_served);
    margin_reset(c);
    walk_net_reset(c);
    junction_outline_reset();
    net_prof_reset();
    if (s_pass != 2)
        seg_table_reset(); /* the grading pass fits.  The building pass reads the same table */
    s_measure = 1;
    if (s_pass == 2 && seg_table_count() > 0 && !g_dev.no_replay)
    {
        /* the building pass: the arms and meets from the kept segments, nothing walked */
        memset(visited, 0, sizeof visited);
        if (seg_table_replay(m, c, l, mask_bit, comp, visited) != 0)
            return -1;
    }
    else
        for (fk = 0; fk < net_n_walked; ++fk)
        {
            /*  The runs the script found, in the order it found them.
             *  Nothing is walked here.  Which cells make a segment is
             *  scripts/compose/network.lua's, and this measures what it
             *  handed over. */
            int i, n = net_disc_count(fk);
            fam = net_walked[fk]->f;
            memset(visited, 0, sizeof visited);
            for (i = 0; i < n; ++i)
            {
                NetRun run;
                if (!net_disc_run_get(fk, i, &run))
                    continue;
                if (walk_segment(m, c, l, mask_bit, comp, fam, &run, visited) != 0)
                    return -1;
            }
        }
    s_measure = 0;
    tnote("measure: fit every segment", tt);
    /*  What follows this, the rings, the trims, the level meets and the
     *  power lines, is the composing script's own sequence
     *  (scripts/compose/world.lua).  The order it puts them in is the
     *  order they depend on each other.  A ring is walked from the arms
     *  this pass filled, the trims are read off the rings.  A meet is
     *  built from the two paths this pass fitted. */
    return 0;
}

/*  Stage three: the trims every junction hands its arms, from the rings
 *  the script walked between this pass and the one before.  The building
 *  pass reads what the grading pass left. */
/*  The reading stage three is made from, taken before it runs so the
 *  drive can settle every junction's control first. */
int build_networks_controls(const RCity *c, const RAtlasLevel *l)
{
    net_control_asks_reset();
    if (s_pass != 2)
        net_stage_three_ask(c, l);
    return 0;
}

int build_networks_trims(const RCity *c, const RAtlasLevel *l)
{
    net_xwalk_asks_reset();
    if (s_pass != 2)
        net_stage_three(c, l);
    return 0;
}

/*  Every junction on the map, in the order the walk visits them, so the
 *  script can be asked for each one's ring. */
int build_junction_count(const RCity *c, const RAtlasLevel *l)
{
    int fk, n = 0;
    (void)c;
    (void)l;
    for (fk = 0; fk < net_n_walked; ++fk)
        n += net_disc_junctions(fk);
    return n;
}

int build_junction_nth(const RCity *c, const RAtlasLevel *l, int i, Family *f, int32_t *ocol, int32_t *orow, int *olinks)
{
    int fk, k, n = 0;
    for (fk = 0; fk < net_n_walked; ++fk)
    {
        Family fam = net_walked[fk]->f;
        k          = net_disc_junctions(fk);
        if (i - n < k)
        {
            int32_t cell = net_disc_junction(fk, i - n);
            *f           = fam;
            *ocol        = cell % R_MAP;
            *orow        = cell / R_MAP;
            *olinks      = eff_links(c, l, *ocol, *orow, fam);
            return 1;
        }
        n += k;
    }
    return 0;
}
/*  ------------------------------------------------------------------
 *  The classes, read before anything is fitted
 *
 *  One class for a whole segment settles how wide it is, where its lanes
 *  run and what is painted on it.  So it has to be known before the fit.
 *  The walk here does nothing but step the tiles of every segment and
 *  count how many of them read as each class.  The drive settles each
 *  one and hands it back, and the walks that follow look the answer up.
 *
 *  It reaches the same segments in the same order as the pass that
 *  follows it, because seg_walk reads the map and its own visited marks
 *  and nothing either walk writes.
 *  ------------------------------------------------------------------ */
#define CLASSES_MAX 16384

static struct
{
    int32_t col, row;
    int     e, cnt[3];
    int     at, nt; /* the segment's own cells, in s_cls_cells */
} s_cls_ask[CLASSES_MAX];
/*  And the cells themselves.  A rule that decides what a line carries
 *  from the density and the neighborhood around it needs the tiles it
 *  runs over.  A single tile is not enough for itally of them. */
#define CLASS_CELLS 262144
static int32_t s_cls_cells[CLASS_CELLS];
static int     s_n_cls_cells;
static int   s_n_cls_ask;
static float s_seg_cls[R_MAP * R_MAP * 4];

int net_seg_class_of(int32_t col, int32_t row, int e)
{
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP || e < 0 || e > 3)
        return 0;
    return (int)s_seg_cls[(row * R_MAP + col) * 4 + e];
}

/*  ------------------------------------------------------------------
 *  And the fitted path of every segment, read at the same time
 *
 *  The fit is a pure function of the tiles a segment covers and the
 *  nodes at its ends.  Nothing it needs comes from anything the walk
 *  does afterwards: so it is worked out here, once, and the walk looks
 *  the answer up.  The points of every path lie end to end in one
 *  arena, since a path is a few points and a city is thousands of them.
 *  ------------------------------------------------------------------ */
#define FITS_MAX  16384
#define FIT_ARENA 262144

static struct
{
    int32_t col, row;
    int     e, fx, at, nk, cut; /* cut: where the drive's answer for this path is */
} s_fit_ask[FITS_MAX];
static int   s_n_fit_ask;
static V2    s_fits_q[FIT_ARENA];
static float s_fits_rad[FIT_ARENA], s_fits_tlim[FIT_ARENA];
static int   s_fit_n;
static int   s_fit_at[2][R_MAP * R_MAP * 4];

static void fit_keep(Family f, int32_t col, int32_t row, int e, const V2 *q, const float *rad, const float *tlim, int nk)
{
    int k, fx = FAMX(f);
    if (s_n_fit_ask >= FITS_MAX || s_fit_n + nk > FIT_ARENA)
    {
        R_ERR("net", "no room for the fit at %d,%d: %d paths of %d points is the most held",
              (int)col, (int)row, FITS_MAX, FIT_ARENA);
        return;
    }
    k                   = s_n_fit_ask++;
    s_fit_ask[k].col    = col;
    s_fit_ask[k].row    = row;
    s_fit_ask[k].e      = e;
    s_fit_ask[k].fx     = fx;
    s_fit_ask[k].at     = s_fit_n;
    s_fit_ask[k].nk     = nk;
    /*  And the path queued to be CUT.  Cutting it into pieces is
     *  arc.rules.pieces's.  So the drive comes round when every path is
     *  fitted and the segment reads the answer back. */
    s_fit_ask[k].cut    = net_cut_add(q, nk, rad, tlim);
    s_fit_at[fx][(row * R_MAP + col) * 4 + e] = k;
    for (int i = 0; i < nk; ++i)
    {
        s_fits_q[s_fit_n + i]    = q[i];
        s_fits_rad[s_fit_n + i]  = rad[i];
        s_fits_tlim[s_fit_n + i] = tlim[i];
    }
    s_fit_n += nk;
}

/*  The path the fit settled for this segment, into the walk's own
 *  arrays.  Answers 0 where none was kept, which is a segment with
 *  nothing to draw. */
/*  The pieces the drive cut for the segment at (col, row, e), or -1
 *  where it cut none. */
int net_seg_pieces_of(Family f, int32_t col, int32_t row, int e, Piece *out, int cap, int *count)
{
    int k = s_fit_at[FAMX(f)][(row * R_MAP + col) * 4 + e];
    if (k < 0 || k >= s_n_fit_ask)
        return -1;
    return net_cut_pieces(s_fit_ask[k].cut, out, cap, count);
}

int net_seg_fit_of(Family f, int32_t col, int32_t row, int e, V2 *q, float *rad, float *tlim, int cap)
{
    int i, k = s_fit_at[FAMX(f)][(row * R_MAP + col) * 4 + e], nk;
    if (k < 0 || k >= s_n_fit_ask)
        return 0;
    nk = s_fit_ask[k].nk < cap ? s_fit_ask[k].nk : cap;
    for (i = 0; i < nk; ++i)
    {
        q[i]    = s_fits_q[s_fit_ask[k].at + i];
        rad[i]  = s_fits_rad[s_fit_ask[k].at + i];
        tlim[i] = s_fits_tlim[s_fit_ask[k].at + i];
    }
    return nk;
}

/*  ------------------------------------------------------------------
 *  The fits, run by the drive
 *
 *  Every segment's path is a pure function of the tiles it covers and
 *  the nodes at its ends.  So the walk asks for none of them: the pass
 *  above reads what each fit needs, the drive runs each in turn.  The
 *  walk looks the answer up.
 *
 *  A family whose runs may leave its own cells: one sweeping
 *  across the field: is fitted TWICE, once held to its tiles and once
 *  free, and which of the two to keep is arc.rules.fit_choice's.
 *  ------------------------------------------------------------------ */
#define FIT_TILES 262144

static struct
{
    int32_t col, row;
    int     e, fit_fam, cand, free_reach; /* cand: 0 the only one, 1 held, 2 free */
    int     at, nt;
    float   hw, rmax, rmin, gro, reserve;
    V2      start, goal;
    int32_t ex0, ex1;
    Family  f;
} s_fitq[FITS_MAX];
static int     s_n_fitq;
static int32_t s_fitq_tc[FIT_TILES], s_fitq_tr[FIT_TILES];
static int     s_fitq_nt;

/*  The path the fit in hand is writing, and the held candidate kept
 *  aside while the free one is fitted. */
static V2    s_fit_q[MAX_PTS];
static float s_fit_rad[MAX_PTS], s_fit_tlim[MAX_PTS];
static V2    s_fit_held_q[MAX_PTS];
static float s_fit_held_rad[MAX_PTS], s_fit_held_tlim[MAX_PTS];
static V2    s_fit_free_q[MAX_PTS];
static float s_fit_free_rad[MAX_PTS], s_fit_free_tlim[MAX_PTS];
static int   s_fit_held_nk, s_fit_free_nk;
static char  s_fit_tally[2][512], s_fit_before[512];
static int   s_fit_held_sc[3], s_fit_free_sc[3]; /* corners, tight, nodes */
static int   s_fit_at_choice = -1;

static void fitq_add(const NetFamily *fam, Family f, int32_t col, int32_t row, int e,
                     const Seg *x, int cand, int free_reach)
{
    int k, i;
    if (s_n_fitq >= FITS_MAX || s_fitq_nt + x->nt > FIT_TILES)
    {
        R_ERR("net", "no room for the fit at %d,%d: %d fits of %d tiles is the most read",
              (int)col, (int)row, FITS_MAX, FIT_TILES);
        return;
    }
    k                    = s_n_fitq++;
    s_fitq[k].col        = col;
    s_fitq[k].row        = row;
    s_fitq[k].e          = e;
    s_fitq[k].f          = f;
    s_fitq[k].fit_fam    = fam->fit_fam;
    s_fitq[k].cand       = cand;
    s_fitq[k].free_reach = free_reach;
    s_fitq[k].at         = s_fitq_nt;
    s_fitq[k].nt         = x->nt;
    s_fitq[k].hw         = x->hw;
    s_fitq[k].rmax       = *fam->rmax;
    s_fitq[k].rmin       = *fam->rmin;
    s_fitq[k].gro        = x->hw / (fam->ref_width * 0.5f);
    s_fitq[k].reserve    = fam->turnout > 0.0f ? fam->turnout - x->hw + 0.05f : 0.0f;
    s_fitq[k].start      = x->pts[0];
    s_fitq[k].goal       = x->pts[x->n - 1];
    s_fitq[k].ex0        = x->kind0 == 2 ? (int32_t)(row * R_MAP + col) : -1;
    s_fitq[k].ex1        = x->kind1 == 2 ? (int32_t)(x->cr * R_MAP + x->cc) : -1;
    for (i = 0; i < x->nt && i < MAX_PTS; ++i)
        s_fitq_tc[s_fitq_nt + i] = x->tcol[i], s_fitq_tr[s_fitq_nt + i] = x->trow[i];
    s_fitq_nt += x->nt;
}

int net_fits(void)
{
    return s_n_fitq;
}

/*  One of them set up and its lines found, ready for the drive to walk
 *  its boundaries.  Answers how many boundaries there are. */
int net_fit_begin(const RCity *c, int i)
{
    if (i < 0 || i >= s_n_fitq)
        return 0;
    fit_tally_into(s_fitq[i].fit_fam);
    if (s_fitq[i].cand == 1)
        fit_tally_get(s_fitq[i].fit_fam, s_fit_before, sizeof s_fit_before);
    if (s_fitq[i].cand)
        fit_tally_set(s_fitq[i].fit_fam, s_fit_before, sizeof s_fit_before);
    return path_fit_begin(c, s_fitq_tc + s_fitq[i].at, s_fitq_tr + s_fitq[i].at, s_fitq[i].nt,
                          s_fitq[i].hw, s_fitq[i].start, s_fitq[i].goal, s_fitq[i].rmax, s_fitq[i].rmin,
                          s_fitq[i].gro, s_fitq[i].reserve, s_fitq[i].ex0, s_fitq[i].ex1,
                          s_fitq[i].cand == 2 ? s_fitq[i].free_reach : 0,
                          s_fit_q, s_fit_rad, s_fit_tlim, MAX_PTS);
}

static void fit_score(const float *rad, int nk, float rmin, int *out)
{
    int k;
    out[0] = out[1] = 0;
    out[2] = nk;
    for (k = 1; k + 1 < nk; ++k)
        if (rad[k] < 0.01f)
            ++out[0];
        else if (rad[k] < rmin)
            ++out[1];
}

/*  And the path it settled, kept: or held aside where the segment is
 *  fitted twice, until the script says which of the two to keep.
 *  Answers 1 when a choice is now waiting on that. */
int net_fit_done(int i)
{
    int nk, k;
    s_fit_at_choice = -1;
    if (i < 0 || i >= s_n_fitq)
        return 0;
    nk = path_fit_end();
    if (s_fitq[i].cand == 0)
    {
        fit_keep(s_fitq[i].f, s_fitq[i].col, s_fitq[i].row, s_fitq[i].e, s_fit_q, s_fit_rad, s_fit_tlim, nk);
        return 0;
    }
    fit_tally_get(s_fitq[i].fit_fam, s_fit_tally[s_fitq[i].cand - 1], sizeof s_fit_tally[0]);
    if (s_fitq[i].cand == 1)
    {
        for (k = 0; k < nk; ++k)
            s_fit_held_q[k] = s_fit_q[k], s_fit_held_rad[k] = s_fit_rad[k], s_fit_held_tlim[k] = s_fit_tlim[k];
        s_fit_held_nk = nk;
        fit_score(s_fit_rad, nk, s_fitq[i].rmin, s_fit_held_sc);
        return 0;
    }
    for (k = 0; k < nk; ++k)
        s_fit_free_q[k] = s_fit_q[k], s_fit_free_rad[k] = s_fit_rad[k], s_fit_free_tlim[k] = s_fit_tlim[k];
    s_fit_free_nk = nk;
    fit_score(s_fit_rad, nk, s_fitq[i].rmin, s_fit_free_sc);
    s_fit_at_choice = i;
    return 1;
}

const char *net_fit_choice(const int **free_, const int **held)
{
    if (s_fit_at_choice < 0)
        return NULL;
    *free_ = s_fit_free_sc;
    *held  = s_fit_held_sc;
    return net_family(s_fitq[s_fit_at_choice].f)->name;
}

/*  The one the script kept, and the tally that goes with it. */
void net_fit_choice_is(int keep_free)
{
    int i = s_fit_at_choice;
    if (i < 0)
        return;
    s_fit_at_choice = -1;
    if (g_dev.path_dump)
        dumpf("FIT %s %s kept: free %d corners %d tight %d nodes, own %d corners %d tight %d nodes\n",
              net_family(s_fitq[i].f)->name, keep_free ? "free" : "own",
              s_fit_free_sc[0], s_fit_free_sc[1], s_fit_free_sc[2],
              s_fit_held_sc[0], s_fit_held_sc[1], s_fit_held_sc[2]);
    fit_tally_set(s_fitq[i].fit_fam, s_fit_tally[keep_free ? 1 : 0], sizeof s_fit_tally[0]);
    if (keep_free)
        fit_keep(s_fitq[i].f, s_fitq[i].col, s_fitq[i].row, s_fitq[i].e, s_fit_free_q, s_fit_free_rad, s_fit_free_tlim, s_fit_free_nk);
    else
        fit_keep(s_fitq[i].f, s_fitq[i].col, s_fitq[i].row, s_fitq[i].e, s_fit_held_q, s_fit_held_rad, s_fit_held_tlim, s_fit_held_nk);
}

static void seg_class_ask(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, const NetRun *run, uint8_t *visited)
{
    Seg              x;
    const NetFamily *fam = net_family(f);
    int32_t          col = run->cell[0] % R_MAP, row = run->cell[0] / R_MAP;
    int              e   = net_run_edge(run);
    if (visited[(row * R_MAP + col) * 4 + e])
        return;
    visited[(row * R_MAP + col) * 4 + e] = 1;
    memset(&x, 0, sizeof x);
    x.m = m, x.c = c, x.l = l, x.mask_bit = mask_bit, x.comp = comp, x.f = f, x.col = col, x.row = row, x.e = e, x.visited = visited;
    x.pts = s_wk_pts, x.q = s_wk_q, x.rad = s_wk_rad, x.tlim = s_wk_tlim;
    x.tcol = s_wk_tcol, x.trow = s_wk_trow, x.pieces = s_wk_pieces, x.marks = s_wk_marks;
    x.hw    = *fam->width * 0.5f;
    x.kind0 = node_kind(c, l, f, col, row);
    x.cc = col, x.cr = row, x.back = (e + 2) & 3, x.ee = e;
    if (seg_from_cells(&x, run) != 0 || x.n < 2)
        return;
    if (fam->classed)
    {
        if (s_n_cls_ask >= CLASSES_MAX)
        {
            R_ERR("net", "no room for the class at %d,%d: %d segments is the most read", (int)col, (int)row, CLASSES_MAX);
            return;
        }
        s_cls_ask[s_n_cls_ask].col = col;
        s_cls_ask[s_n_cls_ask].row = row;
        s_cls_ask[s_n_cls_ask].e   = e;
        seg_class_counts(&x, s_cls_ask[s_n_cls_ask].cnt);
        s_cls_ask[s_n_cls_ask].at = s_n_cls_cells;
        s_cls_ask[s_n_cls_ask].nt = 0;
        {
            int t;
            for (t = 0; t < x.nt && s_n_cls_cells < CLASS_CELLS; ++t)
                s_cls_cells[s_n_cls_cells++] = x.trow[t] * R_MAP + x.tcol[t],
                ++s_cls_ask[s_n_cls_ask].nt;
        }
        ++s_n_cls_ask;
    }
    /*  The corridor is the segment's own tiles, its gates the crossable
     *  part of each shared edge.  The path is the taut string through
     *  them.  It cuts every corner of a staircase into one diagonal by
     *  itself, with each corner then swept as wide as the corridor
     *  allows.  The corridor is tested against the line's own half
     *  width, not a fraction of it: what has to fit inside the corridor
     *  is the line. */
    if (fam->free_reach > 0)
    {
        fitq_add(fam, f, col, row, e, &x, 1, fam->free_reach);
        fitq_add(fam, f, col, row, e, &x, 2, fam->free_reach);
    }
    else
        fitq_add(fam, f, col, row, e, &x, 0, 0);
}

int build_networks_classes(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp)
{
    static uint8_t visited[R_MAP * R_MAP * 4];
    int            fk;
    s_n_cls_ask = s_n_cls_cells = 0;
    net_cut_reset(); /* the fits queue their cuts as they are kept */
    s_n_fitq = s_fitq_nt = 0;
    s_n_fit_ask = s_fit_n = 0;
    memset(s_seg_cls, 0, sizeof s_seg_cls);
    memset(s_fit_at, -1, sizeof s_fit_at);
    for (fk = 0; fk < net_n_walked; ++fk)
    {
        /*  The same runs the measuring walk reads, in the same order:
             both step through the network the script discovered, so the
             class a segment is given and the segment it is given to
             cannot be about different runs. */
        Family fam = net_walked[fk]->f;
        int    i, n = net_disc_count(fk);
        memset(visited, 0, sizeof visited);
        for (i = 0; i < n; ++i)
        {
            NetRun run;
            if (net_disc_run_get(fk, i, &run))
                seg_class_ask(m, c, l, mask_bit, comp, fam, &run, visited);
        }
    }
    return s_n_cls_ask;
}

int net_seg_class_at(int i, int cnt[3], const int32_t **cells, int *nt)
{
    if (i < 0 || i >= s_n_cls_ask)
        return 0;
    cnt[0] = s_cls_ask[i].cnt[0], cnt[1] = s_cls_ask[i].cnt[1], cnt[2] = s_cls_ask[i].cnt[2];
    *cells = &s_cls_cells[s_cls_ask[i].at];
    *nt    = s_cls_ask[i].nt;
    return 1;
}

/*  The class the rule settled, on to every link of the segment's start:
 *  the walks that follow read it there. */
void net_seg_class_is(int i, int cls)
{
    if (i < 0 || i >= s_n_cls_ask)
        return;
    s_seg_cls[(s_cls_ask[i].row * R_MAP + s_cls_ask[i].col) * 4 + s_cls_ask[i].e] = (float)cls;
}

/*  ------------------------------------------------------------------
 *  The drawing pass, walked by the drive
 *
 *  The pass is a cursor, not a loop: the drive asks a family for its
 *  junctions, then for one thing at a time until there is nothing left.
 *  The order is the SCRIPT'S: the network it discovered, entry by
 *  entry: and the junctions come first so a leg knows whether it is
 *  signaled before it draws its stripe.
 *  ------------------------------------------------------------------ */
static struct
{
    RMesh             *m;
    const RCity       *c;
    const RAtlasLevel *l;
    uint8_t            mask_bit;
    int                comp;
    Family             fam;
    int                fk; /* which walked family: its list in the store */
    int                at; /* how far down that list the cursor has come */
} s_draw;
static uint8_t s_draw_visited[R_MAP * R_MAP * 4];
static double  s_draw_t;

int build_networks_draw(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp)
{
    double tt = tms();
    (void)m, (void)c, (void)l, (void)mask_bit, (void)comp;
    tnote("stage three: junction shapes", tt);
    s_draw_t = tms();
    return 0;
}

int build_draw_families(void)
{
    return net_n_walked;
}

/*  One family's junctions, all of them, before any of its segments.  It
 *  is a cursor like the one below, because the drive lays the fill on
 *  each outline between the two halves of its box. */
static struct
{
    RMesh             *m;
    const RCity       *c;
    const RAtlasLevel *l;
    uint8_t            mask_bit;
    Family             fam;
    int                fk, at;
} s_box;

void build_draw_boxes_begin(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int fk)
{
    memset(&s_box, 0, sizeof s_box);
    s_box.m = m, s_box.c = c, s_box.l = l, s_box.mask_bit = mask_bit;
    s_box.fam = net_walked[fk]->f;
    s_box.fk  = fk;
}

int build_draw_box_next(void)
{
    while (s_box.at < net_disc_junctions(s_box.fk))
    {
        const int32_t cell = net_disc_junction(s_box.fk, s_box.at++);
        const int32_t col = cell % R_MAP, row = cell / R_MAP;
        const int     links = eff_links(s_box.c, s_box.l, col, row, s_box.fam);
        if (build_junction(s_box.m, s_box.c, s_box.mask_bit, s_box.fam, col, row, links,
                           tile_order(s_box.c, col, row, s_box.mask_bit) + net_family(s_box.fam)->junc_lift) != 0)
            return -1;
        return 1;
    }
    return 0;
}

/*  And the cursor over what it draws after them, set to the first.  What
 *  it steps through is the network the script discovered: the runs and
 *  the lone pieces, in the order they were handed over. */
void build_draw_begin(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, int fk)
{
    memset(&s_draw, 0, sizeof s_draw);
    s_draw.m = m, s_draw.c = c, s_draw.l = l;
    s_draw.mask_bit = mask_bit, s_draw.comp = comp;
    s_draw.fam = net_walked[fk]->f;
    s_draw.fk  = fk;
    memset(s_draw_visited, 0, sizeof s_draw_visited);
}

/*  The next thing this family draws.  Answers 1 for one drawn, 0 when
 *  the family has none left, -1 with the reason already reported. */
int build_draw_next(void)
{
    RMesh             *m        = s_draw.m;
    const RCity       *c        = s_draw.c;
    const RAtlasLevel *l        = s_draw.l;
    const uint8_t      mask_bit = s_draw.mask_bit;
    const int          comp     = s_draw.comp;
    const Family       fam      = s_draw.fam;
    while (s_draw.at < net_disc_count(s_draw.fk))
    {
        const int     i    = s_draw.at++;
        const int     kind = net_disc_kind(s_draw.fk, i);
        const int32_t cell = net_disc_cell(s_draw.fk, i);
        const int32_t col = cell % R_MAP, row = cell / R_MAP;
        net_cut_reset(); /* this thing's own paths, and nothing left from the last */
        if (kind == NET_DISC_RUN)
        {
            NetRun run;
            if (!net_disc_run_get(s_draw.fk, i, &run))
                continue;
            if (walk_segment(m, c, l, mask_bit, comp, fam, &run, s_draw_visited) != 0)
                return -1;
            return 1;
        }
        /*  A junction is not drawn here: the box cursor above has
         *  already drawn every one of them.  So a leg knows whether it
         *  is signaled before it draws its stripe. */
        if (kind == NET_DISC_JUNCTION)
            continue;
        /*  A lone piece: its own short band.  The kind whose links all
         *  leave the map is drawn only where no run covered the tile
         *  after all.  A run reaches it, steps straight off the map and
         *  comes back with one cell.  Which is why the script hands
         *  those over last. */
        if (kind == NET_DISC_EDGE && net_tile_served(row * R_MAP + col))
            continue;
        if (build_island(m, c, l, mask_bit, comp, fam, col, row) != 0)
            return -1;
        return 1;
    }
    return 0;
}

/*  The margins are the composing script's own pass, run after this one
 *  (scripts/compose/world.lua).  It asks for each path the network holds
 *  and lays the band over it.  They come last because every junction has
 *  registered its corners by then.  So the bands are laid in one place
 *  instead of inside whichever box happened to make them. */
int build_networks_drawn(void)
{
    tnote("junctions and segments drawn", s_draw_t);
    return 0;
}

/*  A tile the corridor pass graded: all four of its corners belong to a
 *  corridor. */
/*  The grading pass skips what only the drawing needs: junction boxes,
 *  lanes, spurs, transitions.  --grade-all runs them anyway, for a
 *  comparison. */
float net_cross_depth(Family f, int32_t col, int32_t row, int e)
{
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP || e < 0 || e > 3)
        return 0.0f;
    return s_xwalk[FAMX(f)][(row * R_MAP + col) * 4 + e];
}

int grade_only(int allowed)
{
    return s_pass == 1 && !g_dev.grade_all && !allowed;
}


