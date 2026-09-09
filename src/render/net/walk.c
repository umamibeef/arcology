/*  The network walk: from a node to the next node along the pieces a
 *  family lays, the segment's stages, and the pass over the map that
 *  drives them (build_networks).  Family-specific decisions are answered
 *  by road.c, rail.c and hiway.c. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "dump.h"
#include "mesh/internal.h"
#include "net/internal.h"
#include "net/model.h"
#include "opt.h"
#include "script.h"

/*  The numbers this file reads, each remembering where the store
 *  put it (net/geo.c).  A name here is a name a script must set. */


static int s_measure; /* 1: the measuring walk -- fit the paths, record the arms, draw nothing */

/*  Where a building pass's time goes, stage by stage (--times). */
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
    dumpf("time    segments: walk+fit %.1f, trims %.1f, lanes %.1f, overlay %.1f, caps %.1f | loft: sample %.1f (%d stations, %d lofts from the cache, %d sampled), ground %.1f, profile %.1f, deck works %.1f, record %.1f, slab %.1f (of which decks and ramps %.1f) | junctions: lanes %.1f, box %.1f ms\n",
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
    dumpf("time    highways: free air %.1f, bands %.1f, tints %.1f, ramps %.1f (of which their lofts %.1f), transitions %.1f, lane check %.1f ms\n",
          s_prof[NET_PROF_HW_AIR],
          s_prof[NET_PROF_HW_BANDS],
          s_prof[NET_PROF_HW_TINT],
          s_prof[NET_PROF_HW_RAMPS],
          s_prof[NET_PROF_RAMP_LOFT],
          s_prof[NET_PROF_HW_TRANS],
          s_prof[NET_PROF_HW_CHECK]);
}

/*  ==================================================================
 *  Walking the network
 *
 *  From a node to the next node: the tiles a run covers, the fit, the
 *  trims its junctions ask for, and the geometry.
 *  ================================================================== */
/*  A family drawn tile by tile rather than walked -- the power lines --
 *  as a thing of its own: what asked for it is in force while it runs,
 *  and it says what it is, so the inspector names the line rather than
 *  the primitive that drew a pole. */
static int build_tile_family(RMesh *m, const RCity *c, uint8_t mask_bit, Family f, int32_t col, int32_t row, int links, float order, int second)
{
    const NetFamily *fam = net_family(f);
    ShapeId          sh;
    int              rc;
    sh = shape_open("%s line at %d,%d%s", fam->name, (int)col, (int)row, second ? ", sharing the tile" : "");
    shape_note("links\t%s%s%s%s", links & L_N ? "north " : "", links & L_E ? "east " : "", links & L_S ? "south " : "", links & L_W ? "west " : "");
    rc = fam->tile(m, c, col, row, mask_bit, links, order, second);
    shape_close(sh);
    return rc;
}

/*  A lone piece no neighbour joins: a band across its own tile along the
 *  axis its art links, both ends capped, as the original's lone sprite. */
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
        const float in = net_family_rules(F_ROAD)->tile_inset; /* a hair inside the tile, so the end stations read its surface */
        pc.a           = (V2){ns ? cx : cx - in, ns ? cy - in : cy};
        pc.b           = (V2){ns ? cx : cx + in, ns ? cy + in : cy};
    }
    pc.c   = pc.a;
    pc.len = 2.0f * net_family_rules(F_ROAD)->tile_inset;
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
        return loft(m, c, mask_bit, comp, &d, &pc, 1, 2.0f * net_family_rules(F_ROAD)->tile_inset);
    }
}

/*  A node of a family's network: a junction (three or four links), an
 *  end (one), or nothing. */
int node_kind(const RCity *c, const RAtlasLevel *l, Family f, int32_t col, int32_t row)
{
    /*  A junction piece is a node whatever its neighbours return: its
     *  box is drawn and its dangling arms end at the box (Barcelona's
     *  crossing at column 101, row 0, two arms into buildings, was
     *  walked through as a bend to the map's edge).  Otherwise one
     *  returned link makes an end. */
    int n = link_count(eff_links(c, l, col, row, f));
    if (link_count(tile_links(c, l, col, row, f)) >= 3)
        return 2;
    return n == 1 ? 1 : 0;
}

/*  Where a segment ends on an end tile, and how.  The tile's art links
 *  that no neighbour returns point at the dead side; what stands there
 *  decides: against a building, a bridge, a tunnel end or a highway
 *  the road runs square to the tile's edge, as the original draws the
 *  straight piece whole and the carrier's sprite goes on from there;
 *  against open land the segment ends at the tile's centre and a road
 *  gets its turning head.  Returns 1 for a square end. */
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
        nc = col + (int32_t)ROAD_DU[e];
        nr = row + (int32_t)ROAD_DV[e];
        if (nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
            continue;
        b = c->xbld[nr * R_MAP + nc];
        if (b >= 0x3Bu && b <= 0x69u) /* a tunnel end, a bridge, a crossing, a highway: a carrier the road runs on into */
        {
            pt->x += ROAD_DU[e] * net_family_rules(F_ROAD)->tile_inset;
            pt->y += ROAD_DV[e] * net_family_rules(F_ROAD)->tile_inset;
            return 1;
        }
        if (b >= 0x6Au && net_family(f)->ends_at_buildings)
        {
            /*  A building: the road ends here, and its turning head is
             *  drawn INSIDE its own tile, the strip stopping a cap's radius
             *  short of the edge so the round cap lands just inside it.  On
             *  a flat tile only: the cap is a flat fan at the end's own
             *  height, and on a slope it cut under the ground (Bayview
             *  116,13; the shipped cities gained clip on five tiles), so a
             *  sloped end stays square as before. */
            if (c->xter[row * R_MAP + col] == 0)
            {
                pt->x += ROAD_DU[e] * (0.5f - ROAD_W * 0.5f - 0.02f);
                pt->y += ROAD_DV[e] * (0.5f - ROAD_W * 0.5f - 0.02f);
                return 0;
            }
            pt->x += ROAD_DU[e] * net_family_rules(F_ROAD)->tile_inset;
            pt->y += ROAD_DV[e] * net_family_rules(F_ROAD)->tile_inset;
            return 1;
        }
    }
    return 0;
}

/*  Cut `s` of length off the front of a piece chain, or off its back
 *  when `back`.  The chain keeps its shape exactly: a line is shortened
 *  along itself and an arc keeps its centre and radius and gives up some
 *  of its angle.  This is how a strip starts at the junction outline it
 *  meets -- the same path the junction measured, cut where the junction
 *  said, so the mouth is square and the two meet with nothing between
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

/*  The walk: from this node out along edge e, tile by tile, to the far node, collecting the tile centres the fit will pass and what kind of node ends it. */
static int seg_walk(Seg *x)
{
    const RCity       *c       = x->c;
    const RAtlasLevel *l       = x->l;
    Family             f       = x->f;
    int32_t            col     = x->col;
    int32_t            row     = x->row;
    int                e       = x->e;
    uint8_t           *visited = x->visited;
    int32_t           *marks   = x->marks;
    int                nm      = x->nm;
    V2                *pts     = x->pts;
    int32_t           *tcol    = x->tcol;
    int32_t           *trow    = x->trow;
    int                nt      = x->nt;
    float              hw      = x->hw;
    int                n       = x->n;
    int                kind0   = x->kind0;
    int                kind1   = x->kind1;
    int                square0 = x->square0;
    int                square1 = x->square1;
    int32_t            cc      = x->cc;
    int32_t            cr      = x->cr;
    int32_t            back;
    int32_t            guard   = x->guard;
    int                ee      = x->ee;
    /*  The start: where the junction says this arm's strip begins -- out
     *  along the arm's own direction, at the distance its shape cleared
     *  -- or the end tile's centre.  Before the junctions are shaped, and
     *  for a family that keeps its box, that is the middle of the box's
     *  own side, as it always was. */
    if (kind0 == 2)
        pts[n++] = (V2){(float)col + 0.5f + ROAD_DU[e] * hw, (float)row + 0.5f + ROAD_DV[e] * hw};
    else
        square0 = end_point(c, l, f, col, row, &pts[n++]);
    tcol[nt]   = col;
    trow[nt++] = row;
    for (;;)
    {
        int links, other;
        cc += (int32_t)ROAD_DU[ee];
        cr += (int32_t)ROAD_DV[ee];
        back = (ee + 2) & 3;
        if (cc < 0 || cr < 0 || cc >= R_MAP || cr >= R_MAP)
        {
            /* off the map: the segment runs to the edge, the last tile's side */
            pts[n++] = (V2){(float)(cc - (int32_t)ROAD_DU[ee]) + 0.5f + ROAD_DU[ee] * 0.5f,
                            (float)(cr - (int32_t)ROAD_DV[ee]) + 0.5f + ROAD_DV[ee] * 0.5f};
            kind1    = 0;
            break;
        }
        links = eff_links(c, l, cc, cr, f);
        if (!(links & (1 << back)))
            break; /* cannot happen on effective links; kept as a guard */
        visited[(cr * R_MAP + cc) * 4 + back] = 1;
        if (nm < 2 * MAX_PTS)
            marks[nm++] = (cr * R_MAP + cc) * 4 + back;
        if (n + 2 >= MAX_PTS || ++guard > 4096)
            break;
        kind1 = node_kind(c, l, f, cc, cr);
        if (kind1 != 0 || link_count(links) != 2)
        {
            /* a node: the far end */
            if (kind1 == 0)
                kind1 = 1;
            if (kind1 == 2)
                pts[n++] = (V2){(float)cc + 0.5f + ROAD_DU[back] * hw, (float)cr + 0.5f + ROAD_DV[back] * hw};
            else
                square1 = end_point(c, l, f, cc, cr, &pts[n++]);
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
        other                               = links & ~(1 << back);
        ee                                  = other == L_N ? 0 : other == L_E ? 1
                                                             : other == L_S   ? 2
                                                                              : 3;
        visited[(cr * R_MAP + cc) * 4 + ee] = 1;
        if (nm < 2 * MAX_PTS)
            marks[nm++] = (cr * R_MAP + cc) * 4 + ee;
        if (cc == col && cr == row)
            break; /* a loop back to the start */
    }
    x->nt      = nt;
    x->hw      = hw;
    x->n       = n;
    x->kind0   = kind0;
    x->kind1   = kind1;
    x->square0 = square0;
    x->square1 = square1;
    x->cc      = cc;
    x->cr      = cr;
    x->back    = back;
    x->guard   = guard;
    x->nm      = nm;
    x->ee      = ee;
    return 0;
}

/*  A line allowed the free ground beside its tiles is fitted two ways,
 *  on its own tiles and with that ground, and the better kept: fewer
 *  hard corners, then fewer arcs under the minimum radius, then the
 *  straighter of equals -- so no line fits worse for the freedom (the
 *  highway's own rule, hiway.c hw_fit_best).  The fit's tallies are
 *  saved and restored around the discarded fit. */
static int seg_fit_best(const RCity *c, const NetFamily *fam, const int32_t *tcol, const int32_t *trow, int nt, float hw, V2 start, V2 goal, int32_t ex0, int32_t ex1, V2 *q, float *rad, float *tlim)
{
    static V2    q2[MAX_PTS];
    static float rad2[MAX_PTS], tlim2[MAX_PTS];
    static char  tally[2][512], before[512];
    int          nk[2], corners[2] = {0, 0}, tight[2] = {0, 0}, w, k, keep;
    path_fit_tally_get(fam->fit_fam, before, sizeof before);
    for (w = 0; w < 2; ++w)
    {
        V2    *qq = w ? q : q2;
        float *rr = w ? rad : rad2, *tt = w ? tlim : tlim2;
        path_fit_tally_set(fam->fit_fam, before, sizeof before);
        nk[w] = path_fit(c, tcol, trow, nt, hw, start, goal, *fam->rmax, *fam->rmin, hw / (fam->ref_width * 0.5f), fam->turnout > 0.0f ? fam->turnout - hw + 0.05f : 0.0f, ex0, ex1, w ? fam->free_reach : 0, qq, rr, tt, MAX_PTS);
        for (k = 1; k + 1 < nk[w]; ++k)
            if (rr[k] < 0.01f)
                ++corners[w];
            else if (rr[k] < *fam->rmin)
                ++tight[w];
        path_fit_tally_get(fam->fit_fam, tally[w], sizeof tally[w]);
    }
    {
        const int free_[3] = {corners[1], tight[1], nk[1]}, held[3] = {corners[0], tight[0], nk[0]};
        keep               = script_rule_fit_choice(fam->name, free_, held);
    }
    if (g_dev.path_dump)
        dumpf("FIT %s %s kept: free %d corners %d tight %d nodes, own %d corners %d tight %d nodes\n", fam->name, keep ? "free" : "own", corners[1], tight[1], nk[1], corners[0], tight[0], nk[0]);
    path_fit_tally_set(fam->fit_fam, tally[keep], sizeof tally[keep]);
    if (!keep)
    {
        memcpy(q, q2, sizeof(V2) * (size_t)nk[0]);
        memcpy(rad, rad2, sizeof(float) * (size_t)nk[0]);
        memcpy(tlim, tlim2, sizeof(float) * (size_t)nk[0]);
    }
    return nk[keep];
}

/*  The tiles a drawn segment serves: every tile of a segment whose strip
 *  was lofted, whether or not the strip runs over that tile -- a railway
 *  fitted across the free ground beside its tiles leaves some bare, by
 *  design, and the check (mesh/check.c) must not read those as a piece
 *  the walk could not draw. */
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
     *  tiles, its gates the crossable part of each shared edge, and the
     *  path is the taut string through them -- which cuts every corner of a
     *  staircase into one diagonal by itself -- with each corner then swept
     *  as wide as the corridor allows.  The corridor is tested against the
     *  road's own half width, not a fraction of it: what has to fit inside
     *  the corridor is the road. */
    const NetFamily *fam = net_family(f);
    fit_family(fam->fit_fam);
    if (fam->free_reach > 0)
        nk = seg_fit_best(c, fam, tcol, trow, nt, hw, pts[0], pts[n - 1], kind0 == 2 ? (int32_t)(row * R_MAP + col) : -1, kind1 == 2 ? (int32_t)(cr * R_MAP + cc) : -1, q, rad, tlim);
    else
        nk = path_fit(c, tcol, trow, nt, hw, pts[0], pts[n - 1], *fam->rmax, *fam->rmin, hw / (fam->ref_width * 0.5f), fam->turnout > 0.0f ? fam->turnout - hw + 0.05f : 0.0f, kind0 == 2 ? (int32_t)(row * R_MAP + col) : -1, kind1 == 2 ? (int32_t)(cr * R_MAP + cc) : -1, 0, q, rad, tlim, MAX_PTS);
    /*  The corridor under the curve overlay: the segment's own tiles, in
     *  tan, which for a road is all the fit may use. */
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
        /*  --road-dump prints every segment of six tiles or more;
         *  --road-dump C,R every segment through that tile.  The switch
         *  is value-OPTIONAL, so its presence is asked separately from
         *  its value: reading the value alone made a bare --road-dump
         *  print nothing at all. */
        const int   on   = g_dev.road_dump;
        const char *dump = on ? g_dev.road_dump_at : NULL;
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
    /* stage two's sweeps, each corner on the radius its room allows */
    if (fillet_t(q, nk, rad, tlim, pieces, &np) != 0 || np == 0)
    {
        if (g_dev.path_dump)
            dumpf("  fillet refused: %d points\n", nk);
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
     *  path itself is untouched -- the drawing pass fits exactly what
     *  the measuring pass measured -- so the cut lands on the mouth the
     *  junction cut for it. */
    if (kind0 == 2 || kind1 == 2)
    {
        /*  The cut each end is given, the one the ports and a turnout's
         *  box use (lane.c arm_cut): never eating the segment -- between
         *  two adjacent junctions a segment is about a tile long, and two
         *  trims of 0.45 would leave a floating stub with nothing joining
         *  it to either end -- and, for a turnout, no further than the
         *  straight run from the mouth, so the strip starts where the
         *  lane's port is.  A turnout's cut trims what the segment DRAWS,
         *  never what it GRADES: the grading pass lofts a rail segment
         *  whole, to the mouth, so the ground under the box's strip is
         *  levelled (Atlanta 51,50). */
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
     *  vertex of the polyline the pieces were built from, so the spacing
     *  between them can be read directly and every corner that came out
     *  hard is visible as such.  Amber where the corner was swept into an
     *  arc, red where no legal radius fitted and the line simply turns.
     *  Not under the spline fit: it has no fillets, so every node would
     *  read as "no arc" and at that sampling the marks merge into a ribbon
     *  that hides the very curve they are there to explain. */
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
            /*  Every dead end gets its round cap; spec 3.10's step 11 keeps
             *  the turning head for a local road with open land around it,
             *  and ends an avenue square, but a square end in the middle of
             *  a tile is a raw edge, so the cap is unconditional. */
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
                if (!records_only && strip_fan_z(m, c, mask_bit, tile_order(c, tc, tr, mask_bit), pos.x, pos.y, ang - 1.5707963f, ang + 1.5707963f, h, h, 0.0f, MAT_ROAD, 8, 0.03f) != 0)
                    return -1;
                /* the sidewalk round the cap, from the strip's one side to its other */
                const float ck = net_family_rules(F_ROAD)->cap_kerb;
                V2          c0 = {pos.x + dir.y * h * ck, pos.y - dir.x * h * ck};
                V2          c1 = {pos.x - dir.y * h * ck, pos.y + dir.x * h * ck};
                sidewalk_add(SIDEWALK_CAP, c0, c1, (V2){0.0f, 0.0f}, (V2){0.0f, 0.0f});
                {
                    /*  The cap, as the network holds it: a terminus closed
                     *  round its head, naming both sides of the arm that
                     *  ends here, so the walk turns rather than stopping. */
                    WalkPath w;
                    int32_t  nc = at_end ? x->cc : x->col, nr = at_end ? x->cr : x->row;
                    int      ne = at_end ? (int)x->back : x->e;
                    memset(&w, 0, sizeof w);
                    w.kind    = WALK_CAP;
                    w.col     = nc;
                    w.row     = nr;
                    w.e       = ne;
                    w.w       = hw * 0.2f;
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

int walk_segment(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, int32_t col, int32_t row, int e, uint8_t *visited)
{
    V2              *pts = s_wk_pts, *q = s_wk_q;
    float           *rad = s_wk_rad, *tlim = s_wk_tlim;
    int32_t         *tcol = s_wk_tcol, *trow = s_wk_trow, *marks = s_wk_marks;
    Piece           *pieces = s_wk_pieces;
    Seg              x;
    const NetFamily *fam = net_family(f);
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
    /*  The stages: walk the tiles to the far node; the class; the fit
     *  through the corridor; the arms for stage three, and in the
     *  measuring pass the crossings and nothing more; the trims the
     *  junctions gave; the lanes; the overlay; the loft; the caps. */
    double tp = prof_now();
    if (seg_walk(&x) != 0)
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
        return seg_measure_crossings(&x);
    }
trims:
    {
        double t1 = prof_now();
        int    r  = seg_trim(&x); /* 1: the trims ate the segment */
        net_prof_add(NET_PROF_TRIMS, prof_now() - t1);
        if (r != 0)
            return r < 0 ? -1 : 0;
    }
    /*  The segment's two lanes -- a road's two lanes, a line's two tracks
     *  -- from the same trimmed pieces the strip is lofted from (lane.c). */
    double t2 = prof_now();
    if (lane_segment(m, c, mask_bit, f, pieces, x.np, col, row, e, x.kind0, x.cc, x.cr, x.back, x.kind1, x.hw, fam->classed ? (int)x.cls : -1) != 0)
        return -1;
    net_prof_add(NET_PROF_LANES, prof_now() - t2), t2 = prof_now();
    if (seg_overlay(&x) != 0)
        return -1;
    net_prof_add(NET_PROF_OVERLAY, prof_now() - t2);
    {
        /* a road or rail strip: the class's markings, a crosswalk band at a controlled leg, pinned at a junction */
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
        d.arm[0]     = e;      /* the arm it leaves each node by, for the footway's ports */
        d.arm[1]     = x.back;
        d.nkind[0]   = x.kind0;
        d.nkind[1]   = x.kind1;
        d.ctrl[0]    = (fam->control && x.kind0 == 2) ? (s_junc_ctrl[row * R_MAP + col] >> (2 * e)) & 3 : 0;
        d.ctrl[1]    = (fam->control && x.kind1 == 2) ? (s_junc_ctrl[x.cr * R_MAP + x.cc] >> (2 * x.back)) & 3 : 0;
        /*  The band each junction takes from this strip's end for its
         *  crossing: the junction lays it (net/sidewalk.c), square to
         *  its own mouth, and the slab leaves that much of its
         *  carriageway bare so the two meet rather than lap. */
        d.xw0        = x.kind0 == 2 ? net_cross_depth(f, col, row, e) : 0.0f;
        d.xw1        = x.kind1 == 2 ? net_cross_depth(f, x.cc, x.cr, x.back) : 0.0f;
        d.pin0       = x.kind0 == 2;
        d.pin1       = x.kind1 == 2;
        d.cls        = fam->classed ? x.cls : -1.0f;
        d.cache      = kept + 1;
        d.hash       = pieces_hash(pieces, x.np, x.total);
        /*  Does the strip reach a chunk this build draws?  In an edit's build most
         *  do not, and their slabs would be dropped triangle by triangle: they
         *  keep their stations, ground, profile and records and draw nothing. */
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
        net_serve_tiles(x.tcol, x.trow, x.nt); /* its tiles have a line, wherever the line runs */
    }
    {
        double t3 = prof_now();
        int    rc = seg_caps(&x);
        net_prof_add(NET_PROF_CAPS, prof_now() - t3);
        return rc;
    }
}

/*  Bucket the opaque list by chunk, and within a chunk terrain first,
 *  networks after: a counting sort of the triangles by the tile their first
 *  vertex lies on, into the scratch, then the buffers swap.  The triangle
 *  set is untouched; only its order, which the depth buffer makes free.
 *  Called once, after the building pass. */
/*  ==================================================================
 *  The driver
 *
 *  The pass over the map: both families measured, the junction shapes,
 *  the crossings and power, then the junctions and segments drawn.
 *  ================================================================== */
static int s_comp; /* the width compensation this build lofts with */

/*  How far an arm's strip runs STRAIGHT from its mouth, up to `want`.
 *  A crosswalk is painted between two parallel pavements, and the two
 *  pavements are the strip's own sides, so the road under a band has to
 *  hold its heading over the whole of it: an arc turns at one radius per
 *  unit length, and the band may run only as far as that turn stays
 *  inside the angle two footways may differ by. */
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

/*  Stage three: each junction takes its shape from its arms and hands each
 *  of them back the length to start at.  It is a computation and nothing
 *  else -- no geometry comes out of it -- so it is worked out ONCE, by the
 *  grading pass, and the building pass reads the table it left.  It may:
 *  the arms it reads are measured from the fitted path BEFORE any trim --
 *  walk_segment runs seg_measure_arms ahead of seg_trim -- and the building
 *  pass replays the very segments the grading pass fitted, so both passes
 *  were working the same numbers.  Doing it twice was also how the passes
 *  came to disagree about a rail arm's cut and left a raw slope under a
 *  junction's strip (Atlanta 51,50). */
static void net_stage_three(const RCity *c, const RAtlasLevel *l)
{
    Family  fam;
    int     fk;
    int32_t row, col;
    /*  Stage three for the roads: each junction takes its shape from its
     *  arms and hands each of them back the length to start at.  It is
     *  worked out ONCE, in the grading pass, and the building pass reads
     *  what it left.  It may: the arms it reads are measured from the
     *  fitted path BEFORE any trim (walk_segment runs seg_measure_arms
     *  ahead of seg_trim), and the building pass replays the very segments
     *  the grading pass fitted, so both passes were computing the same
     *  answer from the same numbers.  Computing it twice was also how the
     *  two passes came to disagree about a rail arm's cut and left a raw
     *  slope under a junction's strip (Atlanta 51,50).  The crossings below
     *  are a drawing and stay in the building pass. */
    for (fk = 0; fk < NET_WALKED; ++fk)
    {
        if (!net_walked[fk]->curbs)
            continue;
        fam = net_walked[fk]->f;
        for (row = 0; row < R_MAP; ++row)
            for (col = 0; col < R_MAP; ++col)
            {
                V2      poly[JUNC_MAX];
                JuncArm arms[4];
                float   trm[4], want[4];
                int     links = eff_links(c, l, col, row, fam), k, np;
                if (!tile_links(c, l, col, row, fam) ||
                    node_kind(c, l, fam, col, row) != 2)
                    continue;
                np = junction_poly(c, fam, col, row, links, poly, NULL, JUNC_MAX, trm, arms);
                if (np < 3)
                    continue;
                for (k = 0; k < 4; ++k)
                    s_trim[FAMX(fam)][(row * R_MAP + col) * 4 + k] = trm[k];
                /*  Which arms want a crossing.  The control is decided
                 *  here too: it is a computation, the junction's box only
                 *  reads it, and the crossings turn on it. */
                if (net_family(fam)->control)
                    s_junc_ctrl[row * R_MAP + col] = (uint8_t)net_family(fam)->control(c, col, row, links);
                sidewalk_junction_wants(c, fam, col, row, poly, arms, np, *net_family(fam)->width * 0.5f * (1.0f - net_family_rules(fam)->inner), want);
                for (k = 0; k < 4; ++k)
                    s_xwalk[FAMX(fam)][(row * R_MAP + col) * 4 + k] = want[k];
            }
    }
    /*  How much of the band each arm can actually spare, now that every
     *  junction's trims are known: a crossing is road given up, and two
     *  of them must still leave a road between the junctions they belong
     *  to.  A band too shallow to read as a crossing is dropped, and the
     *  arm keeps the road. */
    for (fk = 0; fk < NET_WALKED; ++fk)
    {
        if (!net_walked[fk]->curbs)
            continue;
        fam = net_walked[fk]->f;
        for (row = 0; row < R_MAP; ++row)
            for (col = 0; col < R_MAP; ++col)
                for (int e = 0; e < 4; ++e)
                {
                    int         ix = (row * R_MAP + col) * 4 + e, fx = FAMX(fam);
                    const RArm *a  = &s_arm[fx][ix];
                    float       room, straight, v, want = s_xwalk[fx][ix], d;
                    if (!(want > 0.0f))
                        continue;
                    room     = a->have ? a->len - s_trim[fx][ix] - (a->fkind == 2 ? s_trim[fx][(a->frow * R_MAP + a->fcol) * 4 + a->fe] : 0.0f) : 0.0f;
                    straight = arm_straight(fam, col, row, e, want);
                    /*  How deep the band runs is the SCRIPT'S
                     *  (scripts/rules.lua), from the three measurements
                     *  this makes for it: what the outline asked for,
                     *  the road there is to give up, and how much of it
                     *  runs straight from the mouth.  No rule is no
                     *  crossing; the answer is held to the road so one
                     *  can never eat the segment. */
                    d = 0.0f;
                    if (script_rule_crossing(col, row, e, (s_junc_ctrl[row * R_MAP + col] >> (2 * e)) & 3,
                                             want, room, straight, &v))
                    {
                        const float cap = net_family_rules(fam)->cross_share * room;
                        d               = v < 0.0f ? 0.0f : v > cap ? cap : v;
                    }
                    s_xwalk[fx][ix] = d;
                }
    }
    /*  A family whose junction is a turnout hands each arm the reach
     *  instead: the box draws the through line whole across it and the
     *  branch's rails curving at its radius (rail.c rail_box), so the arm's
     *  strip starts there.  A short arm scales its cut back with its far
     *  end's (lane.c arm_cut). */
    for (fk = 0; fk < NET_WALKED; ++fk)
    {
        const NetFamily *tf = net_walked[fk];
        fam                 = tf->f;
        if (!(tf->turnout > 0.0f))
            continue;
        for (row = 0; row < R_MAP; ++row)
            for (col = 0; col < R_MAP; ++col)
            {
                int links = eff_links(c, l, col, row, fam), k;
                if (!tile_links(c, l, col, row, fam) || node_kind(c, l, fam, col, row) != 2)
                    continue;
                for (k = 0; k < 4; ++k)
                    if (links & (1 << k))
                        s_trim[FAMX(fam)][(row * R_MAP + col) * 4 + k] = tf->turnout - *tf->width * 0.5f;
            }
    }
}

/*  ... for a box's own lofts (rail.c rail_loft), which loft as a segment does. */
int net_compensate(void)
{
    return s_comp;
}

int build_networks(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, uint8_t mask_bit, int comp)
{
    s_comp = comp;
    static uint8_t visited[R_MAP * R_MAP * 4];
    int32_t        col, row;
    Family         fam;
    int            fk;
    (void)a;
    /*  Stage two, for both families at once: every segment fits its path
     *  and records what the later stages need -- which way it leaves each
     *  junction, and where it passes each tile.  Nothing is drawn.  The
     *  crossings then have both paths to build from, which is what lets a
     *  road and a railway meet at whatever angle they actually meet at. */
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
    sidewalk_reset(c);
    walk_net_reset(c);
    junction_outline_reset();
    net_prof_reset();
    if (s_pass != 2)
        seg_table_reset(); /* the grading pass fits; the building pass reads the same table */
    s_measure = 1;
    if (s_pass == 2 && seg_table_count() > 0 && !g_dev.no_replay)
    {
        /* the building pass: the arms and crossings from the kept segments, nothing walked */
        memset(visited, 0, sizeof visited);
        if (seg_table_replay(m, c, l, mask_bit, comp, visited) != 0)
            return -1;
    }
    else
        for (fk = 0; fk < NET_WALKED; ++fk)
        {
            fam = net_walked[fk]->f;
            memset(visited, 0, sizeof visited);
            for (row = 0; row < R_MAP; ++row)
                for (col = 0; col < R_MAP; ++col)
                {
                    int links = eff_links(c, l, col, row, fam), e;
                    if (!tile_links(c, l, col, row, fam) ||
                        node_kind(c, l, fam, col, row) == 0)
                        continue;
                    for (e = 0; e < 4; ++e)
                        if ((links & (1 << e)) &&
                            walk_segment(m, c, l, mask_bit, comp, fam, col, row, e, visited) != 0)
                            return -1;
                }
            for (row = 0; row < R_MAP; ++row)
                for (col = 0; col < R_MAP; ++col)
                {
                    int links = eff_links(c, l, col, row, fam), e;
                    if (link_count(links) != 2)
                        continue;
                    for (e = 0; e < 4; ++e)
                        if ((links & (1 << e)) && !visited[(row * R_MAP + col) * 4 + e] &&
                            walk_segment(m, c, l, mask_bit, comp, fam, col, row, e, visited) != 0)
                            return -1;
                }
        }
    s_measure = 0;
    tnote("measure: fit every segment", tt);
    tt = tms();
    /*  Stage three, once (net_stage_three above); the crossings below
     *  are a drawing and belong to the pass that draws. */
    if (s_pass != 2)
        net_stage_three(c, l);
    for (row = 0; row < R_MAP; ++row)
        for (col = 0; col < R_MAP; ++col)
        {
            int32_t idx   = row * R_MAP + col;
            uint8_t b     = c->xbld[idx];
            float   order = tile_order(c, col, row, mask_bit);
            Family  f, f2;
            int     piece = piece_family(b, &f), second = piece_second(b, &f2);
            /* a family drawn tile by tile, not walked: the power lines, on their own or over a road */
            if (piece >= 0 && net_family(f)->tile)
            {
                int links = piece_links(l, piece, c->xter[idx]);
                if (links && build_tile_family(m, c, mask_bit, f, col, row, links, order, 0) != 0)
                    return -1;
            }
            if (second >= 0 && net_family(f2)->tile)
            {
                if (build_tile_family(m, c, mask_bit, f2, col, row, piece_links(l, second, c->xter[idx]), order, 1) != 0)
                    return -1;
            }
            if (second >= 0 && net_family(f2)->crossing)
            {
                if (net_family(f2)->crossing(m, c, l, mask_bit, col, row, second) != 0)
                    return -1;
            }
        }
    /*  And the drawing pass: the junctions first, so a leg knows whether
     *  it is signalled before it draws its crosswalk, then the segments,
     *  each cut back to the outline its junctions gave it. */
    tnote("stage three: junction shapes", tt);
    tt = tms();
    for (fk = 0; fk < NET_WALKED; ++fk)
    {
        fam = net_walked[fk]->f;
        memset(visited, 0, sizeof visited);
        for (row = 0; row < R_MAP; ++row)
            for (col = 0; col < R_MAP; ++col)
            {
                int links = eff_links(c, l, col, row, fam);
                if (!tile_links(c, l, col, row, fam) ||
                    node_kind(c, l, fam, col, row) != 2)
                    continue;
                if (build_junction(m, c, mask_bit, fam, col, row, links, tile_order(c, col, row, mask_bit) + net_family(fam)->junc_lift) != 0)
                    return -1;
            }
        for (row = 0; row < R_MAP; ++row)
            for (col = 0; col < R_MAP; ++col)
            {
                int links = eff_links(c, l, col, row, fam), e;
                int kind;
                if (!tile_links(c, l, col, row, fam))
                    continue;
                kind = node_kind(c, l, fam, col, row);
                if (kind == 0)
                {
                    /*  A piece none of whose links a neighbour returns:
                     *  an island, drawn as its own short band. */
                    if (links == 0 && build_island(m, c, l, mask_bit, comp, fam, col, row) != 0)
                        return -1;
                    continue;
                }
                for (e = 0; e < 4; ++e)
                    if ((links & (1 << e)) &&
                        walk_segment(m, c, l, mask_bit, comp, fam, col, row, e, visited) != 0)
                        return -1;
            }
        /* the loops with no node at all: start anywhere still unvisited */
        for (row = 0; row < R_MAP; ++row)
            for (col = 0; col < R_MAP; ++col)
            {
                int links = eff_links(c, l, col, row, fam), e;
                if (link_count(links) != 2)
                    continue;
                for (e = 0; e < 4; ++e)
                    if ((links & (1 << e)) && !visited[(row * R_MAP + col) * 4 + e] &&
                        walk_segment(m, c, l, mask_bit, comp, fam, col, row, e, visited) != 0)
                        return -1;
            }
        /*  A piece whose every link leaves the map, drawn as its own
         *  short band.  The island rule above asks for a piece whose links
         *  all VANISH; this one keeps a link, so it is walked -- but the
         *  walk steps straight off the map, comes back with one tile, and
         *  nothing is drawn (Venice 0,84, a road in the corner between a
         *  building and the edge; Washington 77,0).  The test is deliberately
         *  narrow: a tile with a link to a real neighbour may be covered by
         *  that neighbour's band, and drawing an island there would lay a
         *  second road over the first. */
        for (row = 0; row < R_MAP; ++row)
            for (col = 0; col < R_MAP; ++col)
            {
                int links = eff_links(c, l, col, row, fam), e, off = 1;
                if (!tile_links(c, l, col, row, fam) || !links || node_kind(c, l, fam, col, row) == 2)
                    continue;
                for (e = 0; e < 4; ++e)
                    if (links & (1 << e))
                    {
                        int32_t nc = col + (int32_t)ROAD_DU[e], nr = row + (int32_t)ROAD_DV[e];
                        if (nc >= 0 && nr >= 0 && nc < R_MAP && nr < R_MAP)
                            off = 0;
                    }
                if (!off || net_tile_served(row * R_MAP + col))
                    continue;
                if (build_island(m, c, l, mask_bit, comp, fam, col, row) != 0)
                    return -1;
            }
    }
    tnote("junctions and segments drawn", tt);
    /*  The footways last, drawn from the network the pass above
     *  built: every junction has registered its corners by now, so
     *  the bands are laid in one place instead of inside whichever
     *  box happened to make them. */
    if (sidewalk_draw(m, c, mask_bit) != 0)
        return -1;
    return 0;
}

/*  A tile the corridor pass graded: all four of its corners belong to a
 *  corridor. */
/*  The grading pass skips what only the drawing needs -- junction boxes,
 *  lanes, ramps, transitions; --grade-all runs them anyway, for a
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

/*  The family answering for a tile family: the road's, the rail's, the
 *  power line's.  A deck's or a ramp's is set on its RLoft by hiway.c. */
const NetFamily *net_family(Family f)
{
    return f == F_RAIL ? &net_rail : f == F_POWER ? &net_power
                                                  : &net_road;
}

const NetFamily *const net_walked[NET_WALKED] = {&net_road, &net_rail};
