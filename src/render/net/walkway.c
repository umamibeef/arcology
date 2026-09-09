/*  walkway.c -- the sidewalk network: the paths, their ports, and whether
 *  the ends meet.  See net/walkway.h.  Nothing here draws. */
#include <math.h>
#include <string.h>

#include <stdio.h>

#include "dump.h"
#include "net/internal.h"
#include "opt.h"

#define WALKNET_MAX 32768

#define WALKST_MAX 400000

static WalkPath     s_path[WALKNET_MAX];
static int          s_np;
static WalkSt       s_st[WALKST_MAX];
static int          s_nst;
static const RCity *s_city;
static uint8_t      s_offer[R_MAP * R_MAP]; /* a bit per arm: the junction has a pavement each side of that mouth */
static int          s_tally[6];             /* mouths, the four reasons one carries no crossing, and how many were narrowed */
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

void walk_cross_tally(int mouths, int uncontrolled, int no_pavement, int not_parallel, int no_road, int narrowed)
{
    s_tally[0] += mouths;
    s_tally[1] += uncontrolled;
    s_tally[2] += no_pavement;
    s_tally[3] += not_parallel;
    s_tally[4] += no_road;
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
 *  answers: a tile the footway cannot cross, the edge of the world, or
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
            return "a carrier";      /* a highway piece: the walk stops under it */
        if (b == 0x00u)
            return "open ground";
    }
    return "nothing";
}

/*  The ports, and what names them.  A port two footways name is a join; a
 *  port one names is an end that meets nothing, and each of those is
 *  reported with the reason the map gives. */
void walk_net_check(void)
{
    static int32_t seen[R_MAP * R_MAP * 8]; /* how many footways name each port */
    static int32_t owner[R_MAP * R_MAP * 8];
    int            i, e, kind_n[4] = {0, 0, 0, 0};
    int            joins = 0, open = 0, named = 0, unnamed = 0;
    int            dump = g_dev.sidewalk_dump;
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
                                                                    : s_path[i].kind == WALK_CROSS   ? "a crossing"
                                                                                                     : "a cap",
                      pt, open_reason(s_path[i].end[e], s_path[i].out[e]));
        }
    for (i = 0; i < R_MAP * R_MAP * 8; ++i)
        if (seen[i] >= 2)
            ++joins;
    (void)owner;
    dumpf("walkways  %d paths (%d sides, %d corners, %d crossings, %d caps); %d ports named, %d joined, %d named by one footway alone; %d ends name no port\n",
          s_np, kind_n[WALK_SIDE], kind_n[WALK_CORNER], kind_n[WALK_CROSS], kind_n[WALK_CAP], named, joins, open, unnamed);
    /*  A crossing is only a crossing if there is a pavement at either end
     *  of it: both its ports must be named by a footway that is not the
     *  crossing itself, or the bars are painted across a road nobody can
     *  step off.  That is a FAULT, not a shortfall -- the junction offers
     *  a crossing only where it has a pavement each side. */
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
            dumpf("walkway  crossing at %d,%d arm %d reaches no pavement at %d of its two ends\n",
                  (int)s_path[i].col, (int)s_path[i].row, s_path[i].e, bad);
    }
    dumpf("crossings  %d junction mouths; %d uncontrolled, %d with no pavement one side, %d whose two footways are not parallel, %d whose road cannot carry the band; %d marked, %d of them shallower than a full band, %d reaching no pavement\n",
          s_tally[0], s_tally[1], s_tally[2], s_tally[3], s_tally[4], kind_n[WALK_CROSS], s_tally[5], s_faults);
}
