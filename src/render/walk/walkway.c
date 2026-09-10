/*  walkway.c: the margin network: the paths, their ports, and whether
 *  the ends meet.  See walk/walkway.h.  Nothing here draws. */
#include <math.h>
#include <string.h>

#include <stdio.h>

#include "dump.h"
#include "pipeline.h"
#include "opt.h"

#define WALKNET_MAX 32768

#define WALKST_MAX 400000

static WalkPath     s_path[WALKNET_MAX];
static int          s_np;
static WalkSt       s_st[WALKST_MAX];
static int          s_nst;
static const RCity *s_city;
static uint8_t      s_offer[R_MAP * R_MAP]; /* a bit per arm: the junction has a margin each side of that mouth */
static int          s_tally[6];             /* mouths, the four reasons one carries no meet, and how many were narrowed */
static int          s_faults;

int walk_port(int32_t col, int32_t row, int e, int side)
{
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP || e < 0 || e > 3 || side < 0 || side > 1)
        return -1;
    return (int)(((row * R_MAP + col) * 4 + e) * 2 + side);
}

void walk_port_name(int port, char *out, size_t n)
{
    static const char *const ARM[4] = {"north", "east", "south", "west"};
    if (!out || !n)
        return;
    if (port < 0)
        snprintf(out, n, "nothing");
    else
        snprintf(out, n, "%d,%d %s, %s", (port >> 3) % R_MAP, (port >> 3) / R_MAP, ARM[(port >> 1) & 3], (port & 1) ? "left" : "right");
}

const char *walk_arm_name(int e)
{
    static const char *const ARM[5] = {", north arm", ", east arm", ", south arm", ", west arm", ""};
    return ARM[e >= 0 && e < 4 ? e : 4];
}

void walk_net_reset(const RCity *c)
{
    s_np   = 0;
    s_nst  = 0;
    s_city = c;
    memset(s_offer, 0, sizeof s_offer);
    memset(s_tally, 0, sizeof s_tally);
    s_faults = 0;
}

void walk_cross_offer(int32_t col, int32_t row, int e)
{
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP || e < 0 || e > 3)
        return;
    s_offer[row * R_MAP + col] |= (uint8_t)(1u << e);
}

int walk_cross_at(int32_t col, int32_t row, int e)
{
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP || e < 0 || e > 3)
        return 0;
    return (s_offer[row * R_MAP + col] >> e) & 1;
}

void walk_cross_tally(int mouths, int uncontrolled, int no_margin, int not_parallel, int no_line, int narrowed)
{
    s_tally[0] += mouths;
    s_tally[1] += uncontrolled;
    s_tally[2] += no_margin;
    s_tally[3] += not_parallel;
    s_tally[4] += no_line;
    s_tally[5] += narrowed;
}

void walk_cross_counts(int out[6])
{
    memcpy(out, s_tally, sizeof s_tally);
}

const RCity *walk_net_city(void)
{
    return s_city;
}

int walk_net_faults(void)
{
    return s_faults;
}

int walk_net_add(const WalkPath *p, const WalkSt *st, int nst)
{
    if (s_np >= WALKNET_MAX || !p)
        return -1;
    s_path[s_np]      = *p;
    s_path[s_np].st0  = s_nst;
    s_path[s_np].nst  = 0;
    if (st && nst > 0 && s_nst + nst <= WALKST_MAX)
    {
        memcpy(&s_st[s_nst], st, (size_t)nst * sizeof *st);
        s_path[s_np].nst = nst;
        s_nst += nst;
    }
    return s_np++;
}

const WalkSt *walk_net_st(const WalkPath *p)
{
    return p && p->nst > 0 ? &s_st[p->st0] : NULL;
}

int walk_net_count(void)
{
    return s_np;
}

const WalkPath *walk_net_get(int i)
{
    return i >= 0 && i < s_np ? &s_path[i] : NULL;
}

/*  Why an end names no port, or names one nothing else names.  The map
 *  answers: a tile the margin cannot cross, the edge of the world, or
 *  nothing at all, which is the one worth reporting. */
static const char *open_reason(V2 p, V2 o)
{
    int32_t tc, tr;
    if (p.x < 0.05f || p.y < 0.05f || p.x > (float)R_MAP - 0.05f || p.y > (float)R_MAP - 0.05f)
        return "the map's edge";
    if (!s_city || (o.x == 0.0f && o.y == 0.0f))
        return "nothing";
    tc = (int32_t)floorf(p.x + o.x * 0.5f);
    tr = (int32_t)floorf(p.y + o.y * 0.5f);
    if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
        return "the map's edge";
    {
        uint8_t b = s_city->xbld[tr * R_MAP + tc];
        if (b >= 0x49u)
            return "a carrier";      /* a band piece: the walk stops under it */
        if (b == 0x00u)
            return "open ground";
    }
    return "nothing";
}

/*  The ports, and what names them.  A port two margins name is a join.
 *  A port one names is an end that meets nothing.  Each of those is
 *  reported with the reason the map gives. */
void walk_net_check(void)
{
    static int32_t seen[R_MAP * R_MAP * 8]; /* how many margins name each port */
    static int32_t owner[R_MAP * R_MAP * 8];
    int            i, e, kind_n[4] = {0, 0, 0, 0};
    int            joins = 0, open = 0, named = 0, unnamed = 0;
    int            dump = g_dev.margin_dump;
    memset(seen, 0, sizeof seen);
    for (i = 0; i < s_np; ++i)
    {
        ++kind_n[s_path[i].kind & 3];
        for (e = 0; e < 2; ++e)
        {
            int pt = s_path[i].port[e];
            if (pt < 0)
            {
                ++unnamed;
                continue;
            }
            ++named;
            if (!seen[pt])
                owner[pt] = i;
            ++seen[pt];
        }
    }
    for (i = 0; i < s_np; ++i)
        for (e = 0; e < 2; ++e)
        {
            int pt = s_path[i].port[e];
            if (pt < 0 || seen[pt] != 1)
                continue;
            ++open;
            if (dump)
                dumpf("walkway  open end at %.3f,%.3f: %s, port %d, %s\n",
                      (double)s_path[i].end[e].x, (double)s_path[i].end[e].y,
                      s_path[i].kind == WALK_SIDE ? "a segment's side" : s_path[i].kind == WALK_CORNER ? "a junction's corner"
                                                                    : s_path[i].kind == WALK_CROSS   ? "a meet"
                                                                                                     : "a cap",
                      pt, open_reason(s_path[i].end[e], s_path[i].out[e]));
        }
    for (i = 0; i < R_MAP * R_MAP * 8; ++i)
        if (seen[i] >= 2)
            ++joins;
    (void)owner;
    dumpf("walkways  %d paths (%d sides, %d corners, %d meets, %d caps); %d ports named, %d joined, %d named by one margin alone; %d ends name no port\n",
          s_np, kind_n[WALK_SIDE], kind_n[WALK_CORNER], kind_n[WALK_CROSS], kind_n[WALK_CAP], named, joins, open, unnamed);
    /*  A meet is only a meet if there is a margin at either end of it.
     *  Both its ports must be named by a margin that is not the meet
     *  itself.  The bars are painted across a line nobody can step off.
     *  That is a FAULT, not a shortfall: the junction offers a meet only
     *  where it has a margin each side. */
    s_faults = 0;
    for (i = 0; i < s_np; ++i)
    {
        int bad = 0;
        if (s_path[i].kind != WALK_CROSS)
            continue;
        for (e = 0; e < 2; ++e)
            if (s_path[i].port[e] < 0 || seen[s_path[i].port[e]] < 2)
                ++bad;
        if (!bad)
            continue;
        ++s_faults;
        if (dump)
            dumpf("walkway  meet at %d,%d arm %d reaches no margin at %d of its two ends\n",
                  (int)s_path[i].col, (int)s_path[i].row, s_path[i].e, bad);
    }
    dumpf("meets  %d junction mouths; %d uncontrolled, %d with no margin one side, %d whose two margins are not parallel, %d whose line cannot carry the band; %d marked, %d of them shallower than a full band, %d reaching no margin\n",
          s_tally[0], s_tally[1], s_tally[2], s_tally[3], s_tally[4], kind_n[WALK_CROSS], s_tally[5], s_faults);
}

/*  A strip's MARGIN, either side of it.  The two bands are the SCRIPT'S.
 *  They are composed from the same stations.  The same expression lays
 *  the way's own edge, so the two meet along the band's inner line
 *  exactly.  With no rule a strip has no margin at all, which is why the
 *  walk below runs only on what the rule answered it drew. */
static int s_walks_drew;

void net_strip_margin_drew(int drew)
{
    s_walks_drew = drew;
}

/*  It filed in the walk network, from the stations the rule laid.  A
 *  family that names a `margin` rule gets it: the rule answers whether
 *  it drew one, and only what it drew is walked.  Both sides go in,
 *  named from each end's own point of view.  Side 0 is the right hand
 *  looking OUT from a node along its arm.  Because the junction's corner
 *  names the same port, and that is what makes the two one walk. */
int net_strip_margin(Loft *x)
{
    const int     drew = s_walks_drew;
    const RLoft  *d   = x->d;
    const Sample *smp = x->smp;
    V2            oa  = {-smp[0].dir.x, -smp[0].dir.y}, ob = smp[x->ns - 1].dir;
    V2            ra, rb, la, lb;
    float         e[4];
    if (!drew)
        return 0;
    script_walk_end_pts(0, e), ra = (V2){e[0], e[1]}, rb = (V2){e[2], e[3]};
    script_walk_end_pts(1, e), la = (V2){e[0], e[1]}, lb = (V2){e[2], e[3]};
    margin_add(MARGIN_STRIP, ra, rb, oa, ob);
    margin_add(MARGIN_STRIP, la, lb, oa, ob);
    /*  The two sides, as the network holds them.  Side 0 is the right
     *  hand looking OUT from a node along its arm.  So the strip's right
     *  side is side 0 at the first node, and side 1 at the far one.  It
     *  is the same ground, named from each end's own point of view.  The
     *  junction's corner names the same port, and that is what makes the
     *  two one walk. */
    {
        static WalkSt st[LOFT_MAX_ST];
        WalkPath      w;
        int           s, k, nst;
        for (s = 0; s < 2; ++s)
        {
            nst = script_walk_count(s);
            for (k = 0; k < nst && k < (int)(sizeof st / sizeof st[0]); ++k)
            {
                float v[5];
                script_walk_station(s, k, v);
                st[k].outer = (V2){v[0], v[1]};
                st[k].inner = (V2){v[2], v[3]};
                st[k].z     = v[4];
            }
            memset(&w, 0, sizeof w);
            w.kind    = WALK_SIDE;
            w.drape   = 1; /* a strip lies on the graded ground, and its margin with it */
            w.col     = d->node[0][0];
            w.row     = d->node[0][1];
            w.e       = d->arm[0];
            w.w       = x->hw * (1.0f - net_family_rules(x->d->f)->inner);
            w.order   = tile_order(x->c, d->node[0][0], d->node[0][1], x->mask_bit);
            w.owner   = shape_current(); /* the strip it runs beside */
            w.end[0]  = s ? la : ra;
            w.end[1]  = s ? lb : rb;
            w.out[0]  = oa;
            w.out[1]  = ob;
            /*  Only a node that IS one: a junction or a dead end.  A
             *  segment that runs on through a bend has no arm to name
             *  there, and its margin simply carries on. */
            w.port[0] = d->nkind[0] ? walk_port(d->node[0][0], d->node[0][1], d->arm[0], s) : -1;
            w.port[1] = d->nkind[1] ? walk_port(d->node[1][0], d->node[1][1], d->arm[1], s ? 0 : 1) : -1;
            walk_net_add(&w, st, nst);
        }
    }
    return 0;
}
