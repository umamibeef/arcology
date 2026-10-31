/*  network/network.c: layer 2, the road network, built from the topology
 *  (network/topology.c).  What each network's roads carry, and what each
 *  junction joins, are plans/network.md's ("what each network's roads
 *  carry").
 *
 *  A street's lanes follow the street layer: its class and direction
 *  over each stretch of it. */
#include "network/network.h"
#include "grow.h"
#include "network/state.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "city.h"
#include "dump.h"
#include "log.h"
#include "network/topology.h"

static NRoad       *s_road;
static NSection    *s_sec;
static NLane       *s_lane;
static NJunction   *s_junc;
static NConnection *s_conn;
static NLaneLink   *s_link;
static int          s_nroad, s_croad, s_nsec, s_csec, s_nlane, s_clane, s_njunc, s_cjunc, s_nconn, s_cconn, s_nlink, s_clink;
/*  The width each lane's derivation works with, beside its lane.  It is
 *  w0 at its section's start, easing to w1 over `ease`, and to w2 over
 *  `ease_end` at its end.  It is filed as the lane's width records (file_widths). */
typedef struct
{
    float w0, w1, ease, w2, ease_end;
} LaneEase;
static LaneEase *s_lw;
static int       s_clw;
#define LW(l) (&s_lw[(l) - s_lane])
/*  Every lane's width, as OpenDRIVE's records: NLane.width0 and nwidth. */
static NWidth *s_width;
static int     s_nwidth, s_cwidth;
static int    *s_junc_of_end; /* per topology end, its junction, else -1 */
/*  Per topology end, the roads that reach it: road * 2 + the contact
 *  they reach it at, from s_at[s_at0[end]] to s_at[s_at0[end + 1]]. */
static int *s_at0, *s_at;

static int index_ends(void)
{
    int i, c, ne = topology_ends(), nt = topology_roads();
    free(s_at0), free(s_at);
    s_at0 = (int *)calloc((size_t)ne + 1, sizeof(int));
    s_at  = (int *)malloc(sizeof(int) * (size_t)(2 * nt + 1));
    if (!s_at0 || !s_at)
        return -1;
    for (i = 0; i < nt; i++)
        for (c = 0; c < 2; c++)
            s_at0[topology_road(i)->end[c] + 1]++;
    for (i = 0; i < ne; i++)
        s_at0[i + 1] += s_at0[i];
    {
        int *fill = (int *)malloc(sizeof(int) * (size_t)(ne > 0 ? ne : 1));
        if (!fill)
            return -1;
        memcpy(fill, s_at0, sizeof(int) * (size_t)ne);
        for (i = 0; i < nt; i++)
            for (c = 0; c < 2; c++)
                s_at[fill[topology_road(i)->end[c]]++] = i * 2 + c;
        free(fill);
    }
    return 0;
}

#define PUSH(arr, n, cap) (arc_grow((void **)&(arr), &(cap), (n) + 1, sizeof *(arr)) != 0 ? -1 : (n)++)

static uint64_t mix(uint64_t h, uint64_t v)
{
    int i;
    for (i = 0; i < 8; i++)
    {
        h ^= (v >> (8 * i)) & 0xFF;
        h *= 1099511628211ull;
    }
    return h;
}

/*  ---- what a road carries ------------------------------------------- */

static uint8_t *s_fault; /* per lane, why the check fails it: 0 it holds */

static int8_t  *s_oneway; /* per topology road: 0 two-way, 1 with s, -1 against s, 2 two ramps disagree */
static uint8_t *s_aux;    /* per topology road: 1 where it carries an auxiliary lane left of its line, 2 right */
/*  Per topology road: 1 where it carries one driving lane fewer left of its
 *  line, 2 right.  A ramp's lane has left the band there (plans/network.md,
 *  "A ramp IS the highway's outermost lane"). */
static uint8_t *s_drop;
/*  Per topology road and contact: 1 left of its line, 2 right.  There its
 *  outermost driving lane comes back from nothing or goes to nothing. */
static uint8_t *s_back;
static int     *s_cell_region;    /* per cell, the end of the interchange's region its band unit is in, else -1 */
static int      s_band_lanes = 3; /* the traffic lanes each way of a band road new_road makes */
/*  The length of a ramp's taper along the band, in tiles (plans/network.md,
 *  "An exit and an entrance are OpenDRIVE's"). */
#define TAPER 3.0f
/*  1 where the world drives on the right, -1 on the left.  A lane whose
 *  id times this is negative travels with `s`.  That is a lane right of
 *  the line on the right and left of it on the left, as OpenDRIVE's two
 *  traffic rules have it. */
static int          s_side = 1, s_drive_left;
static const RCity *s_city; /* the city the network is built from, while it is built */
static NControl    *s_ctl;
static int          s_nctl, s_cctl;
static NObject     *s_obj; /* the roads' objects (lamps) */
static int          s_nobj, s_cobj;
static int          s_oneway_clash, s_ramps_built, s_ramps_no_band, s_ramps_no_road;

/*  A STREET RUN: a stretch of a road of the road network over which the
 *  street layer gives one class and one direction.  `dir` is 1 where the
 *  street is one way with `s`, -1 against it, and 0 two-way. */
typedef struct
{
    int   road;
    float s0, s1;
    int   cls, dir;
} Run;

static Run *s_run;
static int  s_nrun, s_crun, s_street_clash;

/*  Whether traffic travels a lane of a type: not a sidewalk, which is
 *  walked, and not a median or a border, which nothing travels. */
static int traveled(int type)
{
    return type != NLANE_SIDEWALK && type != NLANE_MEDIAN && type != NLANE_BORDER && type != NLANE_SHOULDER;
}

static void  lane_widths(void);
static void  back_tapers(void);
static void  ramp_lane_types(void);
static float section_length(int sec);
static float highway_lane(void);

/*  The base lanes of a road, from the leftmost id down to the rightmost,
 *  with their types.  A ramp's one-way road carries one lane and no
 *  sidewalk.  A connecting road carries its movement alone: `n` traffic
 *  lanes right of its line.  A street carries one lane each way for its
 *  class 0, and two for an avenue or a boulevard.  It has a sidewalk
 *  outermost on each side, and a boulevard a median lane next to its line
 *  on each side.  A railway carries a track each way and its gravel, a
 *  border lane, outside each.  A one-way street carries its lanes on the
 *  side that travels its way, and the other side holds its sidewalk
 *  alone. */
static int base_lanes(int net, int oneway, int connecting, int cls, int dir, const int extra[2], const int less[2], int8_t *id, uint8_t *type)
{
    int n = 0, k;
    if (connecting)
    {
        for (k = 1; k <= connecting; k++)
            id[n] = (int8_t)(-k * s_side), type[n++] = net == RNET_RAIL ? NLANE_RAIL : NLANE_DRIVING;
        return n;
    }
    if (oneway == 1 || oneway == -1)
    {
        id[0]   = (int8_t)(oneway == 1 ? -s_side : s_side);
        type[0] = NLANE_DRIVING;
        return 1;
    }
    if (net == RNET_ROAD)
    {
        int each = cls == 0 ? 1 : 2, side;
        for (side = 1; side >= -1; side -= 2)
        {
            /*  The lanes of this side travel with `s` where its sign is
             *  -s_side.  A one-way street has lanes on its travel side
             *  alone. */
            int     with_s = side == -s_side, lanes = dir == 0 ? each : (dir == 1) == with_s ? each
                                                                                             : 0,
                    m = 0;
            int8_t  sid[8];
            uint8_t sty[8];
            /*  A boulevard's median is lane 1 of each side. */
            int inner = cls == 2 ? 2 : 1;
            if (cls == 2)
                sid[m] = (int8_t)side, sty[m++] = NLANE_MEDIAN;
            for (k = 0; k < lanes; k++)
                sid[m] = (int8_t)((inner + k) * side), sty[m++] = NLANE_DRIVING;
            sid[m] = (int8_t)((inner + lanes) * side), sty[m++] = NLANE_SIDEWALK;
            /*  From the leftmost id down: the left side outermost first. */
            for (k = 0; k < m; k++)
            {
                int at    = side > 0 ? m - 1 - k : k;
                id[n]     = sid[at];
                type[n++] = sty[at];
            }
        }
        return n;
    }
    if (net == RNET_RAIL)
    {
        id[n] = 2, type[n++] = NLANE_BORDER;
        id[n] = 1, type[n++] = NLANE_RAIL;
        id[n] = -1, type[n++] = NLANE_RAIL;
        id[n] = -2, type[n++] = NLANE_BORDER;
        return n;
    }
    /*  A highway, from its line out: a median lane, three traffic lanes,
     *  an exit, entry or auxiliary lane where one runs, and a shoulder.
     *  `extra` holds that lane's type on the left side and on the right,
     *  or -1.  `less` holds 1 on a side whose outermost traffic lane a
     *  ramp has taken, else 0. */
    {
        int side;
        for (side = 1; side >= -1; side -= 2)
        {
            int8_t  sid[8];
            uint8_t sty[8];
            int     m = 0, x = extra[side > 0 ? 0 : 1], lanes = s_band_lanes - less[side > 0 ? 0 : 1];
            sid[m] = (int8_t)side, sty[m++] = NLANE_MEDIAN;
            for (k = 2; k <= 1 + lanes; k++)
                sid[m] = (int8_t)(k * side), sty[m++] = NLANE_DRIVING;
            if (x >= 0)
                sid[m] = (int8_t)((2 + lanes) * side), sty[m++] = (uint8_t)x;
            /*  A side a ramp's lane has left has no shoulder: the ramp's lane
             *  runs where the lane and the shoulder were. */
            if (!less[side > 0 ? 0 : 1])
                sid[m] = (int8_t)((2 + lanes + (x >= 0)) * side), sty[m++] = NLANE_SHOULDER;
            for (k = 0; k < m; k++)
            {
                int at    = side > 0 ? m - 1 - k : k;
                id[n]     = sid[at];
                type[n++] = sty[at];
            }
        }
    }
    return n;
}

static int by_s(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

/*  The street run of a topology road over [a, b], else NULL. */
static const Run *run_over(int topo, float a, float b)
{
    int i;
    for (i = 0; i < s_nrun; i++)
        if (s_run[i].road == topo && s_run[i].s0 <= a && s_run[i].s1 >= b)
            return &s_run[i];
    return NULL;
}

/*  A road and its sections.  Each street run on it cuts a section where
 *  it begins.  A section holds the lanes of its run, from the leftmost id
 *  down. */
static int new_road(uint64_t id, int net, int topo, int junction, float length, int oneway, int connecting)
{
    float cut[96];
    int   r = PUSH(s_road, s_nroad, s_croad), ncut = 0, i, x;
    if (r < 0)
        return -1;
    memset(&s_road[r], 0, sizeof s_road[r]);
    s_road[r].id       = id;
    s_road[r].net      = net;
    s_road[r].topo     = topo;
    s_road[r].junction = junction;
    s_road[r].length   = length;
    s_road[r].sec0     = s_nsec;
    cut[ncut++]        = 0.0f;
    if (topo >= 0)
    {
        for (x = 0; x < s_nrun && ncut < 94; x++)
            if (s_run[x].road == topo && s_run[x].s0 > 0.0f && s_run[x].s0 < length)
                cut[ncut++] = s_run[x].s0;
    }
    qsort(cut, (size_t)ncut, sizeof *cut, by_s);
    for (i = x = 1; i < ncut; i++)
        if (cut[i] > cut[x - 1])
            cut[x++] = cut[i];
    ncut = x;
    for (i = 0; i < ncut; i++)
    {
        float      a = cut[i], b = i + 1 < ncut ? cut[i + 1] : length;
        const Run *run = topo >= 0 ? run_over(topo, a, b) : NULL;
        int8_t     ids[24];
        uint8_t    types[24];
        int        sec, j, k, nb;
        {
            int extra[2] = {-1, -1}, less[2] = {0, 0}, q;
            for (q = 0; q < 2; q++)
            {
                if (topo >= 0 && s_aux && (s_aux[topo] & (q ? 2 : 1)))
                    extra[q] = NLANE_ENTRY;
                less[q] = topo >= 0 && s_drop && (s_drop[topo] & (q ? 2 : 1)) ? 1 : 0;
            }
            nb = base_lanes(net, oneway, connecting, run ? run->cls : 0, run ? run->dir : 0, extra, less, ids, types);
        }
        sec = PUSH(s_sec, s_nsec, s_csec);
        if (sec < 0)
            return -1;
        s_sec[sec].s     = a;
        s_sec[sec].cls   = (uint8_t)(run ? run->cls : 0);
        s_sec[sec].lane0 = s_nlane;
        s_sec[sec].nlane = 0;
        s_road[r].nsec++;
        /*  The ids from +16 down to -16, each once, a base lane or the
         *  road's auxiliary lane. */
        for (k = 16; k >= -16; k--)
        {
            int type = -1, at;
            for (j = 0; j < nb; j++)
                if (ids[j] == k)
                    type = types[j];
            if (type < 0)
                continue;
            at = PUSH(s_lane, s_nlane, s_clane);
            if (at < 0 || arc_grow((void **)&s_lw, &s_clw, at + 1, sizeof *s_lw) != 0)
                return -1;
            memset(&s_lw[at], 0, sizeof s_lw[at]);
            s_lane[at].road    = r;
            s_lane[at].section = sec;
            s_lane[at].id      = (int8_t)k;
            s_lane[at].type    = (uint8_t)type;
            s_lane[at].change  = 0;
            s_lane[at].width0 = 0, s_lane[at].nwidth = 0;
            s_sec[sec].nlane++;
        }
        /*  Along a road, a traffic lane may change into the traffic lane
         *  beside it on its own side. */
        for (j = 0; topo >= 0 && j < s_sec[sec].nlane; j++)
        {
            NLane *l    = &s_lane[s_sec[sec].lane0 + j];
            int    sign = l->id > 0 ? 1 : -1, n;
            if (!traveled(l->type))
                continue;
            for (n = 0; n < s_sec[sec].nlane; n++)
            {
                const NLane *m = &s_lane[s_sec[sec].lane0 + n];
                if (!traveled(m->type))
                    continue;
                if (abs(l->id) > 1 && m->id == l->id - sign)
                    l->change |= NCHANGE_INNER;
                if (m->id == l->id + sign)
                    l->change |= NCHANGE_OUTER;
            }
        }
    }
    return r;
}

/*  How many links leave each lane and how many reach it, kept as they
 *  are laid. */
static int *s_lout, *s_lin, s_lcap;

static int link_at(int from, int from_end, int to, int to_end)
{
    int at;
    if (from < 0 || to < 0)
        return 0;
    if ((from >= s_lcap || to >= s_lcap))
    {
        int nc = s_lcap ? s_lcap : 4096, *no, *ni;
        while (nc <= from || nc <= to)
            nc *= 2;
        no = (int *)realloc(s_lout, sizeof *no * (size_t)nc);
        ni = no ? (int *)realloc(s_lin, sizeof *ni * (size_t)nc) : NULL;
        if (!no || !ni)
        {
            s_lout = no ? no : s_lout;
            return -1;
        }
        memset(no + s_lcap, 0, sizeof *no * (size_t)(nc - s_lcap));
        memset(ni + s_lcap, 0, sizeof *ni * (size_t)(nc - s_lcap));
        s_lout = no, s_lin = ni, s_lcap = nc;
    }
    s_lout[from]++, s_lin[to]++;
    at = PUSH(s_link, s_nlink, s_clink);
    if (at < 0)
        return -1;
    s_link[at].from     = from;
    s_link[at].to       = to;
    s_link[at].from_end = (uint8_t)from_end;
    s_link[at].to_end   = (uint8_t)to_end;
    return 0;
}

/*  1 where a lane travels with `s`. */
static int with_s(int lane)
{
    return s_lane[lane].id * s_side < 0;
}

/*  A traffic lane goes on into another: from the end it runs out at, to
 *  the end the next runs in at. */
static int add_link(int from, int to)
{
    if (from < 0 || to < 0)
        return 0;
    return link_at(from, with_s(from), to, !with_s(to));
}

/*  How far out a traffic lane lies among the traffic lanes of its side:
 *  1 for the one nearest the reference line.  A median nearer the line
 *  does not count. */
static int rank_of(int lane)
{
    const NLane *l = &s_lane[lane];
    int          i, k = 0;
    for (i = 0; i < s_sec[l->section].nlane; i++)
    {
        const NLane *m = &s_lane[s_sec[l->section].lane0 + i];
        if (traveled(m->type) && (m->id > 0) == (l->id > 0) && abs(m->id) <= abs(l->id))
            k++;
    }
    return k;
}

/*  The traffic lane of a section `abs(k)` out on the side of k's sign, else
 *  -1. */
static int lane_in_section(int sec, int k)
{
    int i;
    for (i = 0; k != 0 && i < s_sec[sec].nlane; i++)
    {
        int lane = s_sec[sec].lane0 + i;
        if (traveled(s_lane[lane].type) && (s_lane[lane].id > 0) == (k > 0) && rank_of(lane) == abs(k))
            return lane;
    }
    return -1;
}

/*  The traffic lane of a road `abs(k)` out on the side of k's sign, in the
 *  section at a contact.  That is the first section at its start and the
 *  last at its end.  -1 for none. */
static int lane_at(int road, int contact, int k)
{
    const NRoad *r = &s_road[road];
    return lane_in_section(contact ? r->sec0 + r->nsec - 1 : r->sec0, k);
}

/*  Two lanes k out from the reference line at a contact.  The first
 *  travels INTO the contact and leaves the road there.  The second
 *  travels OUT of it, into the road.  A lane that travels with `s`
 *  arrives at the road's end. */
static int lane_in(int road, int contact, int k)
{
    return lane_at(road, contact, (contact ? -k : k) * s_side);
}

static int lane_out(int road, int contact, int k)
{
    return lane_at(road, contact, (contact ? k : -k) * s_side);
}

static int is_travel(int lane)
{
    return lane >= 0 && traveled(s_lane[lane].type);
}

/*  The traffic lane of a section `abs(k)` out on the side of k's sign,
 *  else -1. */
static int travel_in_section(int sec, int k)
{
    return lane_in_section(sec, k);
}

/*  The outermost sidewalk lane of a section on one side, else -1. */
static int sidewalk_in_section(int sec, int side)
{
    int i, best = -1;
    for (i = 0; i < s_sec[sec].nlane; i++)
    {
        const NLane *l = &s_lane[s_sec[sec].lane0 + i];
        if (l->type == NLANE_SIDEWALK && (l->id > 0 ? 1 : -1) == side &&
            (best < 0 || abs(l->id) > abs(s_lane[best].id)))
            best = s_sec[sec].lane0 + i;
    }
    return best;
}

/*  Sidewalk `a` at its end `ea` goes on into sidewalk `b` at its end
 *  `eb`, walked both ways. */
static int sidewalk_on(int a, int ea, int b, int eb)
{
    if (a < 0 || b < 0)
        return 0;
    return link_at(a, ea, b, eb) != 0 || link_at(b, eb, a, ea) != 0 ? -1 : 0;
}

/*  The links inside a road, across each section boundary.  A traffic
 *  lane in both sections goes on into itself.  A traffic lane that ends
 *  goes on into the traffic lane beside it, nearer the line.  Two lanes
 *  into one is a MERGE.  A traffic lane that begins comes from the
 *  traffic lane beside it.  One lane into two is a DIVERGE.  A sidewalk
 *  goes on into the sidewalk of its side. */
static int section_links(int road)
{
    const NRoad *r = &s_road[road];
    int          i, j;
    for (i = 0; i + 1 < r->nsec; i++)
    {
        int a = r->sec0 + i, b = a + 1, pass;
        for (pass = 0; pass < 2; pass++)
        {
            /*  On the side that travels with `s` traffic runs from a to b,
             *  and on the other from b to a. */
            int from = pass ? b : a, to = pass ? a : b, sign = (pass ? 1 : -1) * s_side;
            for (j = 0; j < s_sec[from].nlane; j++)
            {
                const NLane *l = &s_lane[s_sec[from].lane0 + j];
                int          there, k;
                if ((l->id > 0 ? 1 : -1) != sign || !is_travel(s_sec[from].lane0 + j))
                    continue;
                k     = rank_of(s_sec[from].lane0 + j) * sign;
                there = travel_in_section(to, k);
                /*  An entry lane that ends narrows to nought: its traffic
                 *  merges by a lane change, and it links on to nothing. */
                if (there < 0 && l->type == NLANE_ENTRY)
                    continue;
                if (there < 0)
                    there = travel_in_section(to, k - sign);
                if (add_link(s_sec[from].lane0 + j, there) != 0)
                    return -1;
            }
            for (j = 0; j < s_sec[to].nlane; j++)
            {
                const NLane *l = &s_lane[s_sec[to].lane0 + j];
                int          k = rank_of(s_sec[to].lane0 + j) * sign;
                /*  An exit lane that appears widens from nought, fed by
                 *  nothing. */
                if ((l->id > 0 ? 1 : -1) != sign || !is_travel(s_sec[to].lane0 + j) || travel_in_section(from, k) >= 0 ||
                    l->type == NLANE_EXIT)
                    continue;
                if (add_link(travel_in_section(from, k - sign), s_sec[to].lane0 + j) != 0)
                    return -1;
            }
        }
        if (sidewalk_on(sidewalk_in_section(a, 1), 1, sidewalk_in_section(b, 1), 0) != 0 ||
            sidewalk_on(sidewalk_in_section(a, -1), 1, sidewalk_in_section(b, -1), 0) != 0)
            return -1;
    }
    return 0;
}

static int carry_lanes(int a, int ca, int b, int cb, int *linked, int gore);
static int carry_skip(int a, int ca, int b, int cb, int *linked, int gore, const int *skip, int nskip);

/*  Traffic leaving road `a` at a contact goes on into road `b` at its
 *  contact.  Each traffic lane goes into the traffic lane of the same
 *  place.  A lane with none of its place across goes on into the lane
 *  beside it, nearer the line.  A lane fed by none comes from the lane
 *  beside it, as at a section boundary. */
static int carry(int a, int ca, int b, int cb, int *linked)
{
    return carry_lanes(a, ca, b, cb, linked, 0);
}

static int carry_lanes(int a, int ca, int b, int cb, int *linked, int gore)
{
    return carry_skip(a, ca, b, cb, linked, gore, NULL, 0);
}

/*  As carry_lanes, passing over the `nskip` lanes in `skip`.  Each is a lane
 *  a ramp's lane takes or feeds at a gore, and it links with the ramp alone.
 *  A place where either lane is passed over links nothing. */
static int carry_skip(int a, int ca, int b, int cb, int *linked, int gore, const int *skip, int nskip)
{
    int k, q;
    for (k = 1; k <= 8; k++)
    {
        int in = lane_in(a, ca, k), out = lane_out(b, cb, k), passed = 0;
        for (q = 0; q < nskip; q++)
            passed |= (in >= 0 && in == skip[q]) || (out >= 0 && out == skip[q]);
        if (passed)
            continue;
        in  = is_travel(in) && !(gore && nlane_is_change(s_lane[in].type)) ? in : -1;
        out = is_travel(out) && !(gore && nlane_is_change(s_lane[out].type)) ? out : -1;
        if (in >= 0 && out < 0 && k > 1)
            out = lane_out(b, cb, k - 1), out = is_travel(out) ? out : -1;
        else if (in < 0 && out >= 0 && k > 1)
            in = lane_in(a, ca, k - 1), in = is_travel(in) ? in : -1;
        if (in >= 0 && out >= 0 && linked)
            *linked = 1;
        if (add_link(in, out) != 0)
            return -1;
    }
    return 0;
}

/*  A road's section at a contact. */
static int section_at(int road, int contact)
{
    const NRoad *r = &s_road[road];
    return contact ? r->sec0 + r->nsec - 1 : r->sec0;
}

/*  A seam's two roads carry their traffic lanes across it, both ways,
 *  and each side's sidewalk goes on into the sidewalk of the same side.
 *  The two roads run the same way across the seam where one ends there
 *  and the other starts.  They run against each other where both end or
 *  both start. */
static int seam_links(int a, int ca, int b, int cb)
{
    int same = ca != cb, side;
    if (carry(a, ca, b, cb, NULL) != 0 || carry(b, cb, a, ca, NULL) != 0)
        return -1;
    for (side = 1; side >= -1; side -= 2)
        if (sidewalk_on(sidewalk_in_section(section_at(a, ca), side), ca, sidewalk_in_section(section_at(b, cb), same ? side : -side), cb) != 0)
            return -1;
    return 0;
}

/*  The road across a seam from a road's contact, and its own contact
 *  there.  -1 where the contact is no seam. */
static int across_seam(int road, int contact, int *other)
{
    const TopoRoad *t = topology_road(road);
    int             e = t->end[contact], j;
    if (topology_end(e)->kind != END_SEAM && !(topology_end(e)->kind == END_GORE && s_junc_of_end[e] < 0))
        return -1;
    for (j = s_at0[e]; j < s_at0[e + 1]; j++)
        if (s_at[j] != road * 2 + contact)
        {
            *other = s_at[j] % 2;
            return s_at[j] / 2;
        }
    return -1;
}

/*  What each topology road's two contacts are linked to: a junction, the
 *  road across a seam, or nothing at the map's edge.  A seam's two roads
 *  link their lanes one for one, once for each seam. */
static int road_links(void)
{
    int i, n = topology_roads();
    for (i = 0; i < n; i++)
    {
        const TopoRoad *t = topology_road(i);
        int             c;
        for (c = 0; c < 2; c++)
        {
            NLink *l = c ? &s_road[i].succ : &s_road[i].pred;
            int    r, cj;
            l->kind = NLINK_NONE, l->index = -1, l->contact = 0;
            if (s_junc_of_end[t->end[c]] >= 0)
            {
                l->kind  = NLINK_JUNCTION;
                l->index = s_junc_of_end[t->end[c]];
                continue;
            }
            r = across_seam(i, c, &cj);
            if (r < 0)
                continue;
            l->kind    = NLINK_ROAD;
            l->index   = r;
            l->contact = cj;
            if (r * 2 + cj > i * 2 + c && seam_links(i, c, r, cj) != 0)
                return -1;
        }
        if (section_links(i) != 0)
            return -1;
    }
    return 0;
}

static float road_len(int road);

/*  ---- the street layer ------------------------------------------------- */

/*  The street layer's edge between two cells side by side.  It answers
 *  its class and its direction seen from `p` toward `q`: 1 that way, -1
 *  the other, 0 two-way.  An edge is kept by the cell with the lower
 *  column or row (sim.h, STREET_*). */
static void street_edge(int32_t p, int32_t q, int *cls, int *dir)
{
    int32_t n = topology_side();
    int     keeper_is_p, e, d;
    *cls = 0, *dir = 0;
    if (!s_city || !s_city->street || p < 0 || q < 0)
        return;
    if (q == p + 1 || q == p - 1)
        keeper_is_p = q == p + 1, e = s_city->street[keeper_is_p ? p : q] & 0x0F;
    else if (q == p + n || q == p - n)
        keeper_is_p = q == p + n, e = (s_city->street[keeper_is_p ? p : q] >> 4) & 0x0F;
    else
        return;
    *cls = e & 0x03;
    d    = (e >> 2) & 0x03;
    if (d == 1)
        *dir = keeper_is_p ? 1 : -1;
    else if (d == 2)
        *dir = keeper_is_p ? -1 : 1;
}

/*  The street runs of each road of the road network.  Its edges are read
 *  in order: the edge to its first end, the edges between its units, and
 *  the edge to its last end.  Where two edges side by side differ, a run
 *  ends in the middle of the unit between them.  Two edges one way
 *  against each other are a fault of the data, counted.  A ramp's
 *  one-way stretch has no runs: it carries the ramp's one lane. */
static int street_runs(void)
{
    int i, nt = topology_roads();
    for (i = 0; i < nt; i++)
    {
        const TopoRoad *t = topology_road(i);
        float           pos[600];
        int             cls[600], dir[600], m = 0, k, start;
        if (t->net != RNET_ROAD || s_oneway[i] == 1 || s_oneway[i] == -1)
            continue;
        if (t->at[0] >= 0 && t->nunit)
            street_edge(t->at[0], topology_cell(t->unit0), &cls[m], &dir[m]), pos[m++] = 0.0f;
        for (k = 1; k < t->nunit && m < 598; k++)
            street_edge(topology_cell(t->unit0 + k - 1), topology_cell(t->unit0 + k), &cls[m], &dir[m]), pos[m++] = (float)k;
        if (t->at[1] >= 0 && t->nunit && m < 599)
            street_edge(topology_cell(t->unit0 + t->nunit - 1), t->at[1], &cls[m], &dir[m]), pos[m++] = (float)t->nunit;
        if (!t->nunit && t->at[0] >= 0 && t->at[1] >= 0)
            street_edge(t->at[0], t->at[1], &cls[m], &dir[m]), pos[m++] = 0.0f;
        if (!m)
            continue;
        for (start = 0, k = 1; k <= m; k++)
            if (k == m || cls[k] != cls[start] || dir[k] != dir[start])
            {
                int   at = PUSH(s_run, s_nrun, s_crun);
                float s0 = start == 0 ? 0.0f : 0.5f * (pos[start - 1] + pos[start]);
                float s1 = k == m ? road_len(i) : 0.5f * (pos[k - 1] + pos[k]);
                if (at < 0)
                    return -1;
                s_run[at].road = i, s_run[at].s0 = s0, s_run[at].s1 = s1;
                s_run[at].cls = cls[start], s_run[at].dir = dir[start];
                if (k < m && dir[k] && dir[start] && dir[k] != dir[start])
                    s_street_clash++;
                start = k;
            }
    }
    return 0;
}

/*  ---- junctions ------------------------------------------------------ */

typedef struct
{
    int road, contact, dir; /* dir: the edge the arm leaves the node by */
} Arm;

enum
{
    MAX_ARMS = 64
};

/*  The arms of a topology end: each road that reaches it, by the contact
 *  it reaches it at.  A road with both ends there is two arms. */
static int arms_of_end(int end, Arm *out, int max)
{
    int j, n = 0;
    for (j = s_at0[end]; j < s_at0[end + 1] && n < max; j++)
    {
        const TopoRoad *t = topology_road(s_at[j] / 2);
        int             c = s_at[j] % 2;
        out[n].road       = s_at[j] / 2;
        out[n].contact    = c;
        out[n].dir        = t->nunit ? rcity_edge_back(t->edge[c]) : t->edge[c];
        n++;
    }
    return n;
}

/*  Where an arm's mouth is, in half cells: the edge of its junction cell
 *  it leaves by. */
static void mouth(const Arm *a, int *x2, int *y2)
{
    int32_t side = topology_side(), at = topology_road(a->road)->at[a->contact];
    int     dc, dr;
    dc = rcity_edge_dc(a->dir), dr = rcity_edge_dr(a->dir);
    *x2 = 2 * (at % side) + 1 + dc;
    *y2 = 2 * (at / side) + 1 + dr;
}

/*  Whether a movement from arm `a` to arm `b` runs back on itself.  It
 *  does where the mouth it leaves by lies behind the way its traffic
 *  arrives.  It does where it leaves straight back along the line it
 *  came in on.  No line between the two lanes can be fitted, so the
 *  movement is not made.  A turn back into an arm beside it is made.  At
 *  a junction of one cell no ordinary movement runs back on itself. */
static int behind(const Arm *a, const Arm *b)
{
    int ax, ay, bx, by, adc, adr, bdc, bdr;
    if (topology_road(a->road)->at[a->contact] < 0 || topology_road(b->road)->at[b->contact] < 0)
        return 0;
    mouth(a, &ax, &ay);
    mouth(b, &bx, &by);
    adc = rcity_edge_dc(a->dir), adr = rcity_edge_dr(a->dir);
    bdc = rcity_edge_dc(b->dir), bdr = rcity_edge_dr(b->dir);
    /*  Arm a's traffic arrives heading against a's own direction. */
    if ((bx - ax) * -adc + (by - ay) * -adr < 0)
        return 1;
    /*  Leaving the way it came, on the line it came in on. */
    return bdc == adc && bdr == adr && (bx - ax) * adr - (by - ay) * adc == 0;
}

/*  Whether traffic arriving by arm `a` goes on by arm `b` (plans/network.md,
 *  "what each network's roads carry").  The turn back by the same arm is
 *  made where nothing else takes the arm's traffic (strand_turns). */
static int moves(const TopoEnd *e, const Arm *arms, int n, int a, int b)
{
    int net = topology_road(arms[a].road)->net, straight = arms[a].dir == rcity_edge_back(arms[b].dir);
    if (a == b || net != topology_road(arms[b].road)->net)
        return 0;
    /*  Each network goes straight across a level meet. */
    if (e->kind == END_MEET)
        return straight;
    if (net == RNET_RAIL && n >= 4 && e->ncell == 1)
        return straight;
    return !behind(&arms[a], &arms[b]);
}

/*  Each connection's junction, in filing order.  Connections are filed
 *  while the junctions are joined and again as their sidewalks are, so
 *  once all are filed they are grouped by junction (group_connections). */
static int *s_conn_junc, s_conn_jcap;

static int add_connection(int junction, int incoming, int connecting, int outgoing)
{
    int at = PUSH(s_conn, s_nconn, s_cconn);
    if (at < 0 || arc_grow((void **)&s_conn_junc, &s_conn_jcap, at + 1, sizeof *s_conn_junc) != 0)
        return -1;
    s_conn_junc[at]       = junction;
    s_conn[at].incoming   = incoming;
    s_conn[at].connecting = connecting;
    s_conn[at].outgoing   = outgoing;
    s_junc[junction].nconn++;
    return 0;
}

/*  A LANE INSIDE A JUNCTION: a road of the junction with one lane of the
 *  type given.  `pred` and `succ` are what its two ends are linked to.
 *  That is an arm's road, or the junction itself where the lane begins or
 *  ends at a node inside it.  Answers the lane, or -1. */
static int inner_lane(int junction, int net, int type, NLink pred, NLink succ, uint64_t tag)
{
    uint64_t id = mix(mix(14695981039346656037ull, s_junc[junction].id), tag);
    int      c  = new_road(id, net, -1, junction, 1.0f, 0, 1), lane;
    if (c < 0)
        return -1;
    s_road[c].pred    = pred;
    s_road[c].succ    = succ;
    lane              = s_sec[s_road[c].sec0].lane0;
    s_lane[lane].type = (uint8_t)type;
    return lane;
}

/*  One movement, as a connecting road of the junction.  Take each
 *  traffic lane k that arrives by arm `a` where arm `b` has a lane k
 *  leaving.  It goes on through a lane of the connecting road into that
 *  lane.  A movement with no such lane is not made. */
static int connect_pairs(int junction, const Arm *a, const Arm *b, int pair[][2], int n);
static int connect_turn(int junction, const Arm *a, const Arm *b, int pair[][2], int n, int form);

/*  How traffic arriving by arm `a` turns to leave by arm `b`.  0 is
 *  straight on, and 1 is toward the side the world drives on.  2 is away
 *  from it, and 3 is anything else. */
static int turn_of(const Arm *a, const Arm *b)
{
    int in = rcity_edge_back(a->dir), near = s_drive_left ? rcity_edge_back(rcity_edge_next(in)) : rcity_edge_next(in);
    return b->dir == in ? 0 : b->dir == near                ? 1
                          : b->dir == rcity_edge_back(near) ? 2
                                                            : 3;
}

/*  The lanes of travel arriving by arm `a`, and leaving by arm `b`. */
static int lanes_arriving(const Arm *a)
{
    int n = 0;
    while (n < 8 && is_travel(lane_in(a->road, a->contact, n + 1)))
        n++;
    return n;
}

static int lanes_leaving(const Arm *b)
{
    int n = 0;
    while (n < 8 && is_travel(lane_out(b->road, b->contact, n + 1)))
        n++;
    return n;
}

/*  Whether arriving lane `k` of `nin` makes the movement into an arm of
 *  `nout` lanes that turns as `turn` says. */
static int makes(int k, int nin, int nout, int turn)
{
    int m = nin < nout ? nin : nout;
    return turn == 1 ? k > nin - m : k <= m;
}

/*  An ordinary movement (plans/network.md, "A movement at a street's node
 *  takes as many lanes as the narrower of its two arms holds").  Straight
 *  on, arriving lane k goes on into leaving lane k, and the outermost
 *  arriving lane also feeds every leaving lane past the arriving ones.  A
 *  turn toward the driving side is made from the outermost lanes into the
 *  outermost.  A turn away from it is made from the innermost into the
 *  innermost.  At a street's node, given its arms, an arriving lane can
 *  make no movement.  It goes on with the movement that turns least, into
 *  the leaving lane of its own place or the nearest. */
static int connect_at(int junction, const Arm *a, const Arm *b, const TopoEnd *e, const Arm *arms, int narm)
{
    int k, n = 0, nin = lanes_arriving(a), nout = lanes_leaving(b), turn = turn_of(a, b), pair[8][2], least = 9, q;
    if (!nin || !nout)
        return 0;
    if (arms)
        for (q = 0; q < narm; q++)
            if (&arms[q] != a && moves(e, arms, narm, (int)(a - arms), q) && turn_of(a, &arms[q]) < least)
                least = turn_of(a, &arms[q]);
    for (k = 1; k <= nin && n < 8; k++)
    {
        int out = -1, other = 0;
        if (makes(k, nin, nout, turn))
            out = turn == 1 ? nout - nin + k : k;
        else if (arms && turn == least)
        {
            /*  The lane makes no movement at all: it goes on here. */
            for (q = 0; q < narm; q++)
                if (&arms[q] != a && moves(e, arms, narm, (int)(a - arms), q) &&
                    makes(k, nin, lanes_leaving(&arms[q]), turn_of(a, &arms[q])))
                    other = 1;
            if (!other)
                out = k < nout ? k : nout;
        }
        if (out >= 1)
            pair[n][0] = lane_in(a->road, a->contact, k), pair[n][1] = lane_out(b->road, b->contact, out), n++;
    }
    for (k = nin + 1; turn == 0 && k <= nout && n < 8; k++)
        pair[n][0] = lane_in(a->road, a->contact, nin), pair[n][1] = lane_out(b->road, b->contact, k), n++;
    return connect_pairs(junction, a, b, pair, n);
}

static int connect(int junction, const Arm *a, const Arm *b)
{
    return connect_at(junction, a, b, NULL, NULL, 0);
}

/*  A movement through a connecting road with one lane for each pair.
 *  The pair's first lane goes on into that lane, and it goes on into the
 *  pair's second.  A movement with no pair is not made. */
static int connect_pairs(int junction, const Arm *a, const Arm *b, int pair[][2], int n)
{
    return connect_turn(junction, a, b, pair, n, 0);
}

/*  A ramp's movement.  Its connecting road is an EXIT lane where `form`
 *  holds 1, which diverges from the lane it leaves inside the junction.
 *  It is an ENTRY lane where `form` holds 2, which merges into the lane it
 *  reaches inside the junction. */
static int connect_turn(int junction, const Arm *a, const Arm *b, int pair[][2], int n, int form)
{
    int      net = s_road[a->road].net, k, c;
    uint64_t id  = mix(mix(mix(14695981039346656037ull, s_junc[junction].id), s_road[a->road].id * 2u + (uint64_t)a->contact),
                       s_road[b->road].id * 2u + (uint64_t)b->contact);
    if (!n)
        return 0;
    c = new_road(id, net, -1, junction, 1.0f, 0, n);
    if (c < 0)
        return -1;
    s_road[c].pred = (NLink){NLINK_ROAD, a->road, a->contact};
    s_road[c].succ = (NLink){NLINK_ROAD, b->road, b->contact};
    if (form)
    {
        const NSection *x0 = &s_sec[s_road[c].sec0];
        int             j;
        for (j = 0; j < x0->nlane; j++)
            s_lane[x0->lane0 + j].type = (uint8_t)(form == 1 ? NLANE_EXIT : NLANE_ENTRY);
    }
    for (k = 0; k < n; k++)
        if (add_link(pair[k][0], lane_at(c, 0, -(k + 1) * s_side)) != 0 ||
            add_link(lane_at(c, 1, -(k + 1) * s_side), pair[k][1]) != 0)
            return -1;
    return add_connection(junction, a->road, c, b->road);
}

/*  A topology road's provisional length is its units. */
static float road_len(int road)
{
    return (float)topology_road(road)->nunit;
}

/*  ---- the ramps ------------------------------------------------------ */

static NRamp *s_ramp;
static int    s_nramp, s_cramp;

/*  Mark the one-way stretch of a ramp: every road from the ramp's cell to
 *  the first junction its line reaches, through seams and level meets.
 *  `toward` is 1 where the ramp's traffic runs toward its junction. */
static void mark_oneway(int road, int contact, int toward)
{
    int guard = 0;
    while (road >= 0 && guard++ < 100000)
    {
        int            dir = (contact == 1) == (toward != 0) ? 1 : -1, oc = 1 - contact, e, next = -1, nc = 0;
        const TopoEnd *end;
        if (s_oneway[road] == 0)
            s_oneway[road] = (int8_t)dir;
        else if (s_oneway[road] != dir)
        {
            if (s_oneway[road] != 2)
                s_oneway_clash++;
            s_oneway[road] = 2;
        }
        e   = topology_road(road)->end[oc];
        end = topology_end(e);
        if (end->kind == END_SEAM)
            next = across_seam(road, oc, &nc);
        else if (end->kind == END_MEET)
        {
            Arm arms[MAX_ARMS];
            int n = arms_of_end(e, arms, MAX_ARMS), a, me = -1;
            for (a = 0; a < n; a++)
                if (arms[a].road == road && arms[a].contact == oc)
                    me = a;
            for (a = 0; a < n && me >= 0; a++)
                if (a != me && topology_road(arms[a].road)->net == RNET_ROAD && arms[a].dir == rcity_edge_back(arms[me].dir))
                    next = arms[a].road, nc = arms[a].contact;
        }
        road = next, contact = nc;
    }
}

/*  The direct junction at gore `g`, made where it has none. */
static int direct_junction(int g)
{
    int j;
    if (s_junc_of_end[g] >= 0)
        return s_junc_of_end[g];
    j = PUSH(s_junc, s_njunc, s_cjunc);
    if (j < 0)
        return -1;
    memset(&s_junc[j], 0, sizeof s_junc[j]);
    s_junc[j].id     = topology_end(g)->id;
    s_junc[j].type   = NJUNCTION_DIRECT;
    s_junc[j].end    = g;
    s_junc_of_end[g] = j;
    return j;
}

/*  A RAMP THAT JOINS THE BAND AT A GORE (plans/network.md, "A ramp joins
 *  the band at its gore").  Road `g` reaches a gore by its contact `c`,
 *  across the edge between its ramp's cell and a plain band unit.  The
 *  ramp's gore on the band lies a taper along it (layer 1).  A direct
 *  junction there joins the band's road arriving, the band's road leaving
 *  and the ramp's road.  0, or -1 out of memory. */
static int gore_ramp(int g, int c)
{
    const TopoRoad *t    = topology_road(g);
    int32_t         side = topology_side(), cell = topology_cell(t->unit0 + (c ? t->nunit - 1 : 0)), b, v;
    int             er, eb, h = 0, on = 0, k, at, dc, dr, ge = -1, a_road = -1, a_c = 0, b_road = -1, b_c = 0, j, edge;
    NRamp           rp;
    if (!t->nunit || !topology_ramp_edges(cell, &er, &eb))
        return 0;
    dc = rcity_edge_dc(eb), dr = rcity_edge_dr(eb);
    b = cell + dr * side + dc;
    b = ((b / side) & ~1) * side + ((b % side) & ~1);
    if (!topology_ramp_way(cell, &h, &on))
    {
        s_ramps_no_road++;
        return 0;
    }
    memset(&rp, 0, sizeof rp);
    rp.on     = on;
    rp.region = -1, rp.cell_band = b;
    /*  A ramp beside a region joins at its own gore, a node of the region. */
    if (s_cell_region && s_cell_region[b] >= 0)
    {
        if ((j = direct_junction(t->end[c])) < 0)
            return -1;
        rp.cell = cell, rp.road = g, rp.road_contact = c, rp.band_junction = j, rp.heading = h;
        rp.gore = t->end[c], rp.region = s_cell_region[b], rp.band = -1;
        rp.in_road = rp.out_road = -1;
        rp.lane_in = rp.lane_ex = rp.lane_en = rp.lane_ramp = rp.lane_out = rp.aux_first = rp.aux_last = -1;
        at                                                                                             = PUSH(s_ramp, s_nramp, s_cramp);
        if (at < 0)
            return -1;
        s_ramp[at] = rp;
        return 0;
    }
    /*  The band's gore: an end of two units. */
    if ((ge = topology_ramp_gore(cell)) < 0)
    {
        s_ramps_no_band++;
        return 0;
    }
    /*  A gore at the map's edge holds one unit, the band's on the map
     *  side.  The band beyond it is off the map. */
    edge = topology_end(ge)->ncell == 1;
    b = topology_cell(topology_end(ge)->cell0), v = edge ? -1 : topology_cell(topology_end(ge)->cell0 + 1);
    /*  Another ramp's gore can be this one's, its first cell nearer that
     *  ramp's unit: the nearer of the two to this ramp's unit is `b`. */
    if (!edge && abs(v % side - rp.cell_band % side) + abs(v / side - rp.cell_band / side) <
                     abs(b % side - rp.cell_band % side) + abs(b / side - rp.cell_band / side))
        k = b, b = v, v = k;
    /*  The band's road arriving at the gore and the road leaving it.  The
     *  gore lies downstream of an on ramp's unit, so the arriving one holds
     *  the nearer unit there.  The gore lies upstream of an off ramp's unit,
     *  so the arriving one holds the farther unit. */
    for (k = s_at0[ge]; k < s_at0[ge + 1]; k++)
    {
        const TopoRoad *q    = topology_road(s_at[k] / 2);
        int             qc   = s_at[k] % 2;
        int32_t         unit = q->nunit ? topology_cell(q->unit0 + (qc ? q->nunit - 1 : 0)) : -1;
        int             up   = edge ? rp.on : rp.on ? unit == b
                                                    : unit == v;
        if (up)
            a_road = s_at[k] / 2, a_c = qc;
        else
            b_road = s_at[k] / 2, b_c = qc;
    }
    if (edge ? a_road < 0 && b_road < 0 : a_road < 0 || b_road < 0)
    {
        s_ramps_no_band++;
        return 0;
    }
    j = direct_junction(ge);
    if (j < 0)
        return -1;
    s_junc_of_end[t->end[c]] = j;
    rp.cell                  = cell;
    rp.road                  = g;
    rp.road_contact          = c;
    rp.band_junction         = j;
    rp.heading               = h;
    rp.gore                  = ge;
    rp.in_road = a_road, rp.in_contact = a_c, rp.out_road = b_road, rp.out_contact = b_c;
    rp.band         = rp.on ? b_road : a_road;
    rp.band_contact = rp.on ? b_c : a_c;
    if (rp.band < 0)
        rp.band = rp.on ? a_road : b_road, rp.band_contact = rp.on ? a_c : b_c;
    rp.lane_in = rp.lane_ex = rp.lane_en = rp.lane_ramp = rp.lane_out = rp.aux_first = rp.aux_last = -1;
    at                                                                                             = PUSH(s_ramp, s_nramp, s_cramp);
    if (at < 0)
        return -1;
    s_ramp[at] = rp;
    return 0;
}

/*  Each ramp, from its road: the road of the road network whose end
 *  crosses the edge between a ramp's cell and a band junction.  The band
 *  road its lane runs with, whether its traffic enters the band or leaves
 *  it, and its one-way stretch. */
static int find_ramps(void)
{
    int nt = topology_roads(), g, c;
    for (g = 0; g < nt; g++)
    {
        const TopoRoad *t = topology_road(g);
        for (c = 0; c < 2; c++)
        {
            int32_t cell, side = topology_side();
            int     er, eb, h = 0, a, n, at, bands = 0, only = -1;
            Arm     arms[MAX_ARMS];
            NRamp   rp;
            if (t->net == RNET_ROAD && topology_end(t->end[c])->kind == END_GORE)
            {
                if (gore_ramp(g, c) != 0)
                    return -1;
                continue;
            }
            if (t->net != RNET_ROAD || topology_end(t->end[c])->net != RNET_BAND)
                continue;
            cell = t->nunit ? topology_cell(t->unit0 + (c ? t->nunit - 1 : 0)) : t->at[1 - c];
            if (cell < 0 || !topology_ramp_edges(cell, &er, &eb) || t->edge[c] != (t->nunit ? eb : rcity_edge_back(eb)))
                continue;
            memset(&rp, 0, sizeof rp);
            rp.cell          = cell;
            rp.road          = g;
            rp.road_contact  = c;
            rp.band_junction = s_junc_of_end[t->end[c]];
            rp.band          = -1;
            /*  The heading of the band's traffic on the ramp's side: the
             *  side is right of it where the world drives on the right. */
            {
                int toward_ramp = rcity_edge_back(eb), cand[2], k;
                cand[0]         = (eb == 1 || eb == 4) ? 2 : 1;
                cand[1]         = rcity_edge_back(cand[0]);
                for (k = 0; k < 2; k++)
                {
                    int r = rcity_edge_next(cand[k]);
                    if ((s_drive_left ? rcity_edge_back(r) : r) == toward_ramp)
                        h = cand[k];
                }
            }
            rp.heading = h;
            rp.on      = er == rcity_edge_back(h);
            n          = arms_of_end(t->end[c], arms, MAX_ARMS);
            for (a = 0; a < n; a++)
                if (topology_road(arms[a].road)->net == RNET_BAND)
                {
                    bands++, only = a;
                    /*  The band road traffic leaves the junction by, for
                     *  an entry lane, or arrives by, for an exit lane. */
                    if (arms[a].dir == (rp.on ? h : rcity_edge_back(h)))
                        rp.band = arms[a].road, rp.band_contact = arms[a].contact;
                }
            /*  At the band's END the band has one arm here, and the ramp
             *  takes the whole carriageway.  A ramp in line beyond the end
             *  faces the band's one arm.  The half of the band it lines up
             *  with makes it an on or an off ramp. */
            if (bands == 1)
            {
                rp.band         = arms[only].road;
                rp.band_contact = arms[only].contact;
                rp.endon        = 1;
                if (arms[only].dir == eb)
                {
                    int32_t u = t->at[c], nb;
                    int     dc, dr, half, r = rcity_edge_next(rcity_edge_back(eb));
                    dc = rcity_edge_dc(eb), dr = rcity_edge_dr(eb);
                    nb = cell + dr * side + dc;
                    if (eb == 2 || eb == 8)
                        half = (nb % side) - (u % side) ? 4 : 1;
                    else
                        half = (nb / side) - (u / side) ? 2 : 8;
                    rp.on = half != (s_drive_left ? rcity_edge_back(r) : r);
                }
                /*  A ramp beside the end is read the way that has deck
                 *  behind it (appendix-road-spec, 7.3, "Termini").  On the
                 *  side whose traffic leaves the end it is an on ramp, and
                 *  on the side whose traffic arrives an off ramp.  So no
                 *  ramp's lanes cross the band to reach it. */
                else
                    rp.on = h == arms[only].dir;
            }
            if (er != h && er != rcity_edge_back(h) && !rp.endon)
            {
                s_ramps_no_road++;
                continue;
            }
            at = PUSH(s_ramp, s_nramp, s_cramp);
            if (at < 0)
                return -1;
            rp.lane_in = rp.lane_ex = rp.lane_en = rp.lane_ramp = rp.lane_out = rp.aux_first = rp.aux_last = -1;
            rp.in_road = rp.out_road = -1, rp.in_contact = rp.out_contact = 0, rp.gore = -1, rp.region = -1, rp.cell_band = -1;
            s_ramp[at] = rp;
        }
    }
    return 0;
}

/*  THE AUXILIARY LANE between an on ramp and the next off ramp on its
 *  side (plans/network.md).  The band road the on ramp's traffic leaves
 *  by is followed through seams alone.  It can reach an off ramp's
 *  junction by the road that ramp's traffic arrives by.  Then every road
 *  followed carries one more lane outside its own, on that side.  The on
 *  ramp's entry lane goes on into it, and the off ramp's exit lane takes
 *  it.  Nothing about it is measured: which roads carry it follows from
 *  the junctions alone. */
static int aux_lanes(void)
{
    int i, j, path[64], np;
    for (i = 0; i < s_nramp; i++)
    {
        const NRamp *rp = &s_ramp[i];
        int          road, contact, guard = 0;
        if (!rp->built || !rp->on || rp->endon || rp->gore >= 0)
            continue;
        road = rp->band, contact = rp->band_contact, np = 0;
        while (road >= 0 && np < 64 && guard++ < 64)
        {
            int oc = 1 - contact, nc = 0, next;
            path[np++] = road * 2 + contact;
            next       = across_seam(road, oc, &nc);
            if (next >= 0)
            {
                road = next, contact = nc;
                continue;
            }
            for (j = 0; j < s_nramp; j++)
                if (s_ramp[j].built && !s_ramp[j].on && !s_ramp[j].endon && s_ramp[j].gore < 0 && s_ramp[j].band == road &&
                    s_ramp[j].band_contact == oc)
                {
                    int q;
                    for (q = 0; q < np; q++)
                    {
                        int r = path[q] / 2, c = path[q] % 2;
                        /*  Traffic on it runs away from the contact it was
                         *  entered at: with `s` from the start.  Each side
                         *  of a road carries its own. */
                        s_aux[r] = (uint8_t)(s_aux[r] | ((c == 0 ? -1 : 1) * s_side > 0 ? 1 : 2));
                    }
                }
            break;
        }
    }
    return 0;
}

/*  Every ramp with a band road beside it is joined, inside the band
 *  junction its road reaches (ramp_lanes).  The band's roads carry
 *  no lane of the ramp's.  Such a lane would be a length, and a
 *  length is the shape's. */
static void ramps_built(void)
{
    int i;
    for (i = 0; i < s_nramp; i++)
        if (s_ramp[i].region >= 0)
            s_ramp[i].built = 1, s_ramps_built++;
        else if (s_ramp[i].band < 0)
            s_ramps_no_band++;
        else
            s_ramp[i].built = 1, s_ramps_built++;
}

/*  Whether topology road `road` holds unit `u`. */
static int holds_unit(int road, int32_t u)
{
    const TopoRoad *t = topology_road(road);
    int             k;
    for (k = 0; k < t->nunit; k++)
        if (topology_cell(t->unit0 + k) == u)
            return 1;
    return 0;
}

/*  The ramp at band junction `j` whose lane leaves the band (`on` 0) or
 *  joins it (`on` 1).  Its band's traffic heads `h`, and the junction is
 *  reached from band road `road`.  Else -1. */
static int partner_at(int j, int on, int h, int road)
{
    int i;
    for (i = 0; i < s_nramp; i++)
    {
        const NRamp *q = &s_ramp[i];
        if (q->built && q->gore >= 0 && q->region < 0 && q->band_junction == j && q->on == on && q->heading == h &&
            road == (on ? q->in_road : q->out_road))
            return i;
    }
    return -1;
}

/*  The band's road beyond gore junction `j` from band road `road`, and its
 *  contact there.  -1 where `j` is no gore's. */
static int gore_beyond(int j, int road, int *contact)
{
    int i;
    for (i = 0; i < s_nramp; i++)
    {
        const NRamp *q = &s_ramp[i];
        if (q->gore < 0 || q->region >= 0 || q->band_junction != j || q->in_road < 0 || q->out_road < 0)
            continue;
        if (road == q->in_road)
            return *contact = q->out_contact, q->out_road;
        if (road == q->out_road)
            return *contact = q->in_contact, q->in_road;
    }
    return -1;
}

/*  THE STRETCH A RAMP'S LANE IS AWAY (plans/network.md, "The lane comes
 *  back").  From an off ramp's gore it is walked with the band's traffic.
 *  From an on ramp's gore it is walked against it.  It goes through seams,
 *  the borders the lane comes back at, and other ramps' gores.  It can
 *  reach the gore of a ramp of the other kind on its side, the partner.
 *  Then every road walked carries one driving lane fewer on that side.
 *  Else the lane is away to the ramp's own unit and its border beyond.  The
 *  lane comes back or goes there, as the partner is no nearer than
 *  PARTNER_UNITS beyond that unit.
 *  A walk that ends before the ramp's unit carries the drop on every road
 *  it walked. */
#define PARTNER_UNITS 3
static void ramp_drops(void)
{
    int i;
    for (i = 0; i < s_nramp; i++)
    {
        const NRamp *rp = &s_ramp[i];
        int          path[64], pc[64], np = 0, road, from, past = -1, beyond = 0, mine = -1, partner = 0, guard = 0;
        if (!rp->built || rp->gore < 0 || rp->region >= 0 || (rp->on ? rp->in_road : rp->out_road) < 0)
            continue;
        road = rp->on ? rp->in_road : rp->out_road, from = rp->on ? rp->in_contact : rp->out_contact;
        while (road >= 0 && np < 64 && guard++ < 64)
        {
            int oc = 1 - from, nc = 0, next, e = topology_road(road)->end[oc];
            path[np] = road, pc[np++] = from;
            if (past >= 0)
                beyond += topology_road(road)->nunit;
            else if (holds_unit(road, rp->cell_band))
                past = np, mine = np;
            if (past >= 0 && beyond > PARTNER_UNITS)
                break;
            if (s_junc_of_end[e] >= 0)
            {
                /*  Another ramp's gore is passed through, to the band's road
                 *  beyond it. */
                if ((partner = partner_at(s_junc_of_end[e], !rp->on, rp->heading, road) >= 0) != 0)
                    break;
                next = gore_beyond(s_junc_of_end[e], road, &nc);
            }
            else
                next = across_seam(road, oc, &nc);
            road = next, from = nc;
        }
        {
            /*  Traffic travels with `s` on a road it enters at its start. */
            int n = partner || mine < 0 ? np : mine, k;
            for (k = 0; k < n; k++)
            {
                int entered     = rp->on ? 1 - pc[k] : pc[k];
                s_drop[path[k]] = (uint8_t)(s_drop[path[k]] | ((entered == 0 ? -1 : 1) * s_side > 0 ? 1 : 2));
            }
            /*  The road beyond the ramp's unit has the lane back, from
             *  nothing at its contact there, or loses it to nothing. */
            if (!partner && mine > 0 && mine < np)
            {
                int entered = rp->on ? 1 - pc[mine] : pc[mine];
                s_back[2 * path[mine] + pc[mine]] =
                    (uint8_t)(s_back[2 * path[mine] + pc[mine]] | ((entered == 0 ? -1 : 1) * s_side > 0 ? 1 : 2));
            }
        }
    }
}

/*  ---- an interchange's region (plans/interchanges.md, "The splicing") ---- */

enum
{
    IX_MOST  = 8,  /* the most arms of an interchange's own that its junction joins */
    RG_ARMS  = 12, /* the most highway arms of a region */
    RG_LINKS = 24  /* the most lane links of a region's node */
};

/*  A node of a region: a direct junction, and the lane links it makes. */
typedef struct
{
    int junction;
    int nl, from[RG_LINKS], to[RG_LINKS];
} RNode;

/*  Where an outermost lane's traffic stands as its chain of strands is
 *  filed.  That is the node it has reached, and the lanes that end there
 *  and go on into the next strand.  Two at most, so no node joins more
 *  than two. */
typedef struct
{
    int node, n, lane[2];
} RChain;

static RNode *s_rn;
static int    s_nrn, s_crn;

/*  The chain a region's nodes are made on, while it is made: its arm's road
 *  and contact, and whether it is the lane leaving.  Its nodes in the order
 *  they are made. */
static int s_ch_road = -1, s_ch_contact, s_ch_leaving, s_ch_node[64], s_ch_part[64], s_ch_n, s_ch_next = NNODE_TURN;
/*  Per junction, its chain, its part and its rank among the chain's nodes
 *  of that part from the gore, else road -1. */
static int *s_jch_road, *s_jch_contact, *s_jch_leaving, *s_jch_part, *s_jch_rank, *s_jch_order;
static int  s_cjch;

static void chain_begin(int road, int contact, int leaving)
{
    s_ch_road = road, s_ch_contact = contact, s_ch_leaving = leaving, s_ch_n = 0;
}

/*  The chain's nodes take their rank from the gore among the nodes of
 *  their part.  The arriving lane's rank in the order they are made, the
 *  leaving lane's the other way. 0, or -1 out of memory. */
static int chain_end(void)
{
    int k, q, j2;
    if (s_njunc > s_cjch)
    {
        int   n      = s_njunc + 256, *p[6];
        int **arr[6] = {&s_jch_road, &s_jch_contact, &s_jch_leaving, &s_jch_part, &s_jch_rank, &s_jch_order};
        for (q = 0; q < 6; q++)
        {
            p[q] = (int *)realloc(*arr[q], sizeof(int) * (size_t)n);
            if (!p[q])
                return -1;
            *arr[q] = p[q];
        }
        for (q = s_cjch; q < n; q++)
            s_jch_road[q] = -1;
        s_cjch = n;
    }
    for (k = 0; k < s_ch_n; k++)
    {
        int j = s_rn[s_ch_node[k]].junction, rank = 0;
        for (j2 = 0; j2 < s_ch_n; j2++)
            rank += s_ch_part[j2] == s_ch_part[k] && (s_ch_leaving ? j2 > k : j2 < k);
        s_jch_road[j] = s_ch_road, s_jch_contact[j] = s_ch_contact, s_jch_leaving[j] = s_ch_leaving;
        s_jch_part[j] = s_ch_part[k], s_jch_rank[j] = rank, s_jch_order[j] = s_ch_leaving ? s_ch_n - k : k + 1;
    }
    s_ch_road = -1, s_ch_n = 0;
    return 0;
}

int network_cell_region(int32_t cell)
{
    int32_t side = topology_side();
    return s_cell_region && cell >= 0 && cell < side * side ? s_cell_region[cell] : -1;
}

int network_node_chain(int j, int *road, int *contact, int *leaving, int *part, int *rank, int *order)
{
    if (j < 0 || j >= s_cjch || !s_jch_road || s_jch_road[j] < 0 || j >= s_njunc)
        return 0;
    *road = s_jch_road[j], *contact = s_jch_contact[j], *leaving = s_jch_leaving[j], *part = s_jch_part[j], *rank = s_jch_rank[j];
    *order = s_jch_order[j];
    return 1;
}

/*  The arms of an interchange that are band roads, and whether each has a
 *  way straight on.  0 where the end is no interchange. */
static int ix_arms(int end, Arm *arms, int *straight)
{
    Arm all[MAX_ARMS];
    int n = arms_of_end(end, all, MAX_ARMS), a, b, m = 0;
    if (topology_end(end)->kind != END_NODE || topology_end(end)->net != RNET_BAND)
        return 0;
    for (a = 0; a < n && m < IX_MOST; a++)
        if (topology_road(all[a].road)->net == RNET_BAND)
            arms[m++] = all[a];
    if (m < 3)
        return 0;
    for (a = 0; a < m; a++)
        for (straight[a] = 0, b = 0; b < m; b++)
            straight[a] |= b != a && arms[b].dir == rcity_edge_back(arms[a].dir);
    return m;
}

/*  A strand of a region: one connectingRamp lane, with `s`. */
static int ix_road(uint64_t id)
{
    int r = new_road(id, RNET_BAND, -1, -1, 1.0f, 0, 1);
    if (r >= 0)
        s_lane[s_sec[s_road[r].sec0].lane0].type = NLANE_CONNRAMP;
    return r;
}

/*  The outermost driving lane of road `road` at its contact `c`: of the
 *  lanes leaving it there where `leaving`, else of those arriving.  -1
 *  for none. */
static int outer_driving(int road, int c, int leaving)
{
    int k;
    for (k = 8; k >= 1; k--)
    {
        int l = leaving ? lane_out(road, c, k) : lane_in(road, c, k);
        if (l >= 0 && s_lane[l].type == NLANE_DRIVING)
            return l;
    }
    return -1;
}

/*  Each region's end, by the cells of its units.  A region is an end of
 *  the band's nodes that no road reaches: layer 1 cuts every road at its
 *  border.  0, or -1 out of memory. */
static int region_cells(void)
{
    int32_t side = topology_side(), n2 = side * side, q;
    int     i, j;
    free(s_cell_region);
    s_cell_region = (int *)malloc(sizeof(int) * (size_t)(n2 > 0 ? n2 : 1));
    if (!s_cell_region)
        return -1;
    for (q = 0; q < n2; q++)
        s_cell_region[q] = -1;
    for (i = 0; i < topology_ends(); i++)
    {
        const TopoEnd *e = topology_end(i);
        if (e->kind != END_NODE || e->net != RNET_BAND || s_at0[i + 1] != s_at0[i])
            continue;
        for (j = 0; j < e->ncell; j++)
            s_cell_region[topology_cell(e->cell0 + j)] = i;
    }
    return 0;
}

/*  The region a band gore lies on the border of, else -1.  Its second cell
 *  is the unit inside. */
static int gore_region(int g)
{
    const TopoEnd *e = topology_end(g);
    return e->kind == END_GORE && e->net == RNET_BAND && e->ncell == 2 && s_cell_region ? s_cell_region[topology_cell(e->cell0 + 1)]
                                                                                        : -1;
}

/*  A node of a region at junction `j`, made where it has none. */
static int rg_node_at(int j)
{
    int i;
    for (i = 0; i < s_nrn; i++)
        if (s_rn[i].junction == j)
            return i;
    i = PUSH(s_rn, s_nrn, s_crn);
    if (i < 0)
        return -1;
    memset(&s_rn[i], 0, sizeof s_rn[i]);
    s_rn[i].junction = j;
    return i;
}

/*  A new node of a region: a direct junction of its own, at the end
 *  `end` for a place. */
static int rg_node(uint64_t id, int end)
{
    int j = PUSH(s_junc, s_njunc, s_cjunc), n;
    if (j < 0)
        return -1;
    memset(&s_junc[j], 0, sizeof s_junc[j]);
    s_junc[j].id = id, s_junc[j].type = NJUNCTION_DIRECT, s_junc[j].end = end;
    n = rg_node_at(j);
    if (n >= 0 && s_ch_road >= 0 && s_ch_n < 64)
        s_ch_part[s_ch_n] = s_ch_next, s_ch_node[s_ch_n++] = n;
    s_ch_next = NNODE_TURN;
    return n;
}

static int rg_link(int node, int from, int to)
{
    RNode *r = &s_rn[node];
    if (from < 0 || to < 0)
        return 0;
    if (r->nl == RG_LINKS)
        return -1;
    r->from[r->nl] = from, r->to[r->nl++] = to;
    return 0;
}

/*  A strand from node `a` to node `b`.  Answers its lane, or -1. */
static int rg_strand(uint64_t id, int a, int b)
{
    int r = ix_road(id);
    if (r < 0)
        return -1;
    s_road[r].pred = (NLink){NLINK_JUNCTION, s_rn[a].junction, 0};
    s_road[r].succ = (NLink){NLINK_JUNCTION, s_rn[b].junction, 0};
    return s_sec[s_road[r].sec0].lane0;
}

/*  The lanes of a region's strands that narrow to nothing after a join
 *  or widen from nothing before a split.  Their widths ease along their
 *  road (region_tapers). */
static int *s_rg_taper;
static int  s_nrg_taper, s_crg_taper;

/*  A PAIR of a region: a strand of two lanes from node `a` to node `b`.
 *  It holds the strand's own lane and beside it a lane of type `type`.
 *  That is an entry lane after a join, which narrows to nothing, or an
 *  exit lane before a split, which widens from nothing.  `main` and
 *  `side` get the two lanes.  0, or -1. */
static int rg_pair(uint64_t id, int a, int b, int type, int *main, int *side)
{
    int r = new_road(id, RNET_BAND, -1, -1, 1.0f, 0, 2), l0, at;
    if (r < 0)
        return -1;
    s_road[r].pred = (NLink){NLINK_JUNCTION, s_rn[a].junction, 0};
    s_road[r].succ = (NLink){NLINK_JUNCTION, s_rn[b].junction, 0};
    l0             = s_sec[s_road[r].sec0].lane0;
    /*  The lanes run from the leftmost id down.  The strand's own lane
     *  is the one nearer the line, and the side lane lies outside it.
     *  So the strand's lane keeps its place as the side lane eases. */
    *main              = abs(s_lane[l0].id) < abs(s_lane[l0 + 1].id) ? l0 : l0 + 1;
    *side              = *main == l0 ? l0 + 1 : l0;
    s_lane[*main].type = NLANE_CONNRAMP, s_lane[*side].type = (uint8_t)type;
    at = PUSH(s_rg_taper, s_nrg_taper, s_crg_taper);
    if (at < 0)
        return -1;
    s_rg_taper[at] = *side;
    return 0;
}

/*  A WEAVE of a cloverleaf: a road of two connectingRamp lanes from node `a`
 *  to node `b`, whose traffic may change from either lane into the other.
 *  `inner` gets the lane nearer the line, `outer` the one outside it.  0, or
 *  -1 out of memory. */
static int rg_weave(uint64_t id, int a, int b, int *inner, int *outer)
{
    int r = new_road(id, RNET_BAND, -1, -1, 1.0f, 0, 2), l0;
    if (r < 0)
        return -1;
    s_road[r].pred      = (NLink){NLINK_JUNCTION, s_rn[a].junction, 0};
    s_road[r].succ      = (NLink){NLINK_JUNCTION, s_rn[b].junction, 0};
    l0                  = s_sec[s_road[r].sec0].lane0;
    *inner              = abs(s_lane[l0].id) < abs(s_lane[l0 + 1].id) ? l0 : l0 + 1;
    *outer              = *inner == l0 ? l0 + 1 : l0;
    s_lane[*inner].type = s_lane[*outer].type = NLANE_CONNRAMP;
    s_lane[*inner].change = NCHANGE_OUTER, s_lane[*outer].change = NCHANGE_INNER;
    return 0;
}

/*  A chain goes on to node `to`: a strand from where it stands, which the
 *  lanes that end there go on into.  Two such lanes are a join: a pair,
 *  the second lane narrowing to nothing beside the first. */
static int rg_advance(RChain *c, uint64_t id, int to)
{
    int l, side, k;
    if (c->n == 2)
    {
        if (rg_pair(id, c->node, to, NLANE_ENTRY, &l, &side) != 0 || rg_link(c->node, c->lane[0], l) != 0 ||
            rg_link(c->node, c->lane[1], side) != 0)
            return -1;
        c->node = to, c->n = 1, c->lane[0] = l;
        return 0;
    }
    if ((l = rg_strand(id, c->node, to)) < 0)
        return -1;
    for (k = 0; k < c->n; k++)
        if (rg_link(c->node, c->lane[k], l) != 0)
            return -1;
    c->node = to, c->n = 1, c->lane[0] = l;
    return 0;
}

/*  A chain splits at node `to`: a pair from where it stands, a lane
 *  widening from nothing beside its own.  The
 *  chain goes on in its own lane, and `branch` gets the new lane, which
 *  ends at `to`.  Two lanes waiting where it stands join first. */
static int rg_split(RChain *c, uint64_t id, int to, int *branch)
{
    int l, k;
    if (c->n == 2)
    {
        int mid, w;
        s_ch_next = NNODE_BETWEEN;
        mid       = rg_node(mix(id, 0x51u), s_junc[s_rn[c->node].junction].end);
        if (mid < 0 || rg_advance(c, mix(id, 0x52u), mid) != 0)
            return -1;
        /*  The join lies before the split node `to`, made before it: the two
         *  take their places on the lane in that order. */
        for (w = 0; w + 1 < s_ch_n; w++)
            if (s_ch_node[w] == to && s_ch_node[s_ch_n - 1] == mid)
            {
                int q, part = s_ch_part[s_ch_n - 1];
                for (q = s_ch_n - 1; q > w; q--)
                    s_ch_node[q] = s_ch_node[q - 1], s_ch_part[q] = s_ch_part[q - 1];
                s_ch_node[w] = mid, s_ch_part[w] = part;
                break;
            }
    }
    if (rg_pair(id, c->node, to, NLANE_EXIT, &l, branch) != 0)
        return -1;
    for (k = 0; k < c->n; k++)
        if (rg_link(c->node, c->lane[k], l) != 0)
            return -1;
    c->node = to, c->n = 1, c->lane[0] = l;
    return 0;
}

/*  A city ramp on a chain: a node of the chain beside the highway.  A
 *  strand runs between it and the ramp's gore, node `rn`, whose lane is
 *  `x`.  An on ramp's strand joins the chain there, and an off ramp's
 *  splits from it.  `end` is the end that names the node's place. */
static int rg_ramp(RChain *c, uint64_t id, int rn, int x, int on, int end)
{
    int n, l, br;
    s_ch_next = NNODE_RAMP;
    n         = rg_node(mix(id, 0x61u), end);
    if (n < 0)
        return -1;
    if (on)
    {
        if (rg_advance(c, mix(id, 0x62u), n) != 0 || (l = rg_strand(mix(id, 0x63u), rn, n)) < 0 || rg_link(rn, x, l) != 0)
            return -1;
        c->lane[c->n++] = l;
        return 0;
    }
    if (rg_split(c, mix(id, 0x62u), n, &br) != 0 || (l = rg_strand(mix(id, 0x63u), n, rn)) < 0 || rg_link(n, br, l) != 0 ||
        rg_link(rn, l, x) != 0)
        return -1;
    return 0;
}

/*  Each pair's lane eases between nothing and a highway lane's width along
 *  its road: an entry lane narrows, an exit lane widens. */
static void region_tapers(void)
{
    int i;
    for (i = 0; i < s_nrg_taper; i++)
    {
        NLane *l  = &s_lane[s_rg_taper[i]];
        float  w  = highway_lane();
        LW(l)->w0 = l->type == NLANE_ENTRY ? w : 0.0f, LW(l)->w1 = l->type == NLANE_ENTRY ? 0.0f : w;
        LW(l)->ease = section_length(l->section), LW(l)->w2 = 0.0f, LW(l)->ease_end = 0.0f;
    }
}

/*  How near a turn is: toward the side the world drives on first. */
static int nearness(const Arm *a, const Arm *b)
{
    int t = turn_of(a, b);
    return t == 1 ? 0 : t == 3 ? 1
                               : 2;
}

/*  The driving lanes of a road at a contact, from its line out: of those
 *  leaving where `leaving`, else of those arriving.  The last is the
 *  outermost. */
static int driving_lanes(int road, int c, int leaving, int *out, int max)
{
    int k, n = 0;
    for (k = 1; k <= 8 && n < max; k++)
    {
        int l = leaving ? lane_out(road, c, k) : lane_in(road, c, k);
        if (l >= 0 && s_lane[l].type == NLANE_DRIVING)
            out[n++] = l;
    }
    return n;
}

/*  THE SPLICING OF EACH REGION (plans/interchanges.md, "The splicing").
 *  Each highway arm reaches the region at its gore, a node.  Its inner
 *  lanes straight on are one BUNDLE, a road to the gore of the arm
 *  across.  Its outermost lane is a chain of strands.  On the way in the
 *  chain takes the city's ramps, off ramps first, then splits into the
 *  turns, nearest first.  On the way out the turns join into it,
 *  farthest first: each nearer turn joins from outside.  Then it takes the
 *  city's ramps and reaches the gore.  An arm
 *  with no way straight on has no bundle.  Its inner lanes join the
 *  chain, and on the way out the chain splits into them.  Every node
 *  splits or joins two lanes at most. 0, or -1 out of memory. */
static int regions_build(void)
{
    int e;
    for (e = 0; e < topology_ends(); e++)
    {
        Arm arm[RG_ARMS];
        int node[RG_ARMS], partner[RG_ARMS], na = 0, a, b, k, q, g, clover;
        /*  A cloverleaf's weaves, by the arm whose carriageway arrives
         *  on each.  Each has its nodes before and beyond the crossing,
         *  the lane that feeds its inner lane, and its two lanes. */
        int            ws[RG_ARMS], we[RG_ARMS], wfeed[RG_ARMS], win[RG_ARMS], wout[RG_ARMS];
        RChain         src[RG_ARMS][RG_ARMS];
        uint64_t       id;
        const TopoEnd *te = topology_end(e);
        if (te->kind != END_NODE || te->net != RNET_BAND || s_at0[e + 1] != s_at0[e] || !s_cell_region ||
            s_cell_region[topology_cell(te->cell0)] != e)
            continue;
        id = mix(14695981039346656037ull, te->id);
        /*  The highway arms, at their gores. */
        for (g = 0; g < topology_ends() && na < RG_ARMS; g++)
        {
            if (gore_region(g) != e || s_at0[g + 1] - s_at0[g] != 1 || s_junc_of_end[g] < 0)
                continue;
            arm[na].road    = s_at[s_at0[g]] / 2;
            arm[na].contact = s_at[s_at0[g]] % 2;
            arm[na].dir     = rcity_edge_back(topology_end(g)->edge);
            if ((node[na] = rg_node_at(s_junc_of_end[g])) < 0)
                return -1;
            na++;
        }
        for (a = 0; a < na; a++)
            for (partner[a] = -1, b = 0; b < na; b++)
                if (b != a && arm[b].dir == rcity_edge_back(arm[a].dir))
                    partner[a] = b;
        /*  Two highways that cross: four arms, each with a way straight on. */
        clover = na == 4;
        for (a = 0; a < na; a++)
            clover &= partner[a] >= 0, ws[a] = we[a] = wfeed[a] = win[a] = wout[a] = -1;
        /*  The bundles straight on. */
        for (a = 0; a < na; a++)
        {
            int in[8], out[8], ni, no, r, d = partner[a];
            if (d < a)
                continue;
            ni           = driving_lanes(arm[a].road, arm[a].contact, 0, in, 8) - 1;
            no           = driving_lanes(arm[a].road, arm[a].contact, 1, out, 8) - 1;
            s_band_lanes = ni < no ? ni : no;
            r            = s_band_lanes > 0 ? new_road(mix(id, ((uint64_t)0x1 << 20) + (uint64_t)(a * 16 + d)), RNET_BAND, -1, -1, 1.0f, 0, 0) : -2;
            s_band_lanes = 3;
            if (r == -1)
                return -1;
            if (r < 0)
                continue;
            s_road[r].pred = (NLink){NLINK_JUNCTION, s_rn[node[a]].junction, 0};
            s_road[r].succ = (NLink){NLINK_JUNCTION, s_rn[node[d]].junction, 0};
            for (q = 0; q < 2; q++)
            {
                int x = q ? d : a, xi[8], xo[8], nxi, nxo, c = q;
                nxi = driving_lanes(arm[x].road, arm[x].contact, 0, xi, 8);
                nxo = driving_lanes(arm[x].road, arm[x].contact, 1, xo, 8);
                for (k = 0; k + 1 < nxi && k + 1 < nxo && k < 8; k++)
                    if (rg_link(node[x], xi[k], lane_out(r, c, k + 1)) != 0 || rg_link(node[x], lane_in(r, c, k + 1), xo[k]) != 0)
                        return -1;
            }
        }
        /*  The chains in: each arm's outermost lane, its ramps, its turns. */
        for (a = 0; a < na; a++)
        {
            int    lanes[8], nl = driving_lanes(arm[a].road, arm[a].contact, 0, lanes, 8), turn[RG_ARMS], nt = 0, pass, t;
            RChain c = {
                node[a], 1, {lanes[nl - 1], -1}
            };
            for (b = 0; b < na; b++)
                src[a][b].node = -1;
            if (nl < 1)
                continue;
            chain_begin(arm[a].road, arm[a].contact, 0);
            /*  An arm with no way straight on: its inner lanes join the chain. */
            for (k = nl - 2; partner[a] < 0 && k >= 0; k--)
            {
                int n2 = (s_ch_next = NNODE_INNER, rg_node(mix(id, ((uint64_t)0x2 << 20) + (uint64_t)(a * 16 + k)), s_junc[s_rn[node[a]].junction].end)), x;
                if (n2 < 0 || rg_advance(&c, mix(id, ((uint64_t)0x3 << 20) + (uint64_t)(a * 16 + k)), n2) != 0)
                    return -1;
                if ((x = rg_strand(mix(id, ((uint64_t)0x4 << 20) + (uint64_t)(a * 16 + k)), node[a], n2)) < 0 || rg_link(node[a], lanes[k], x) != 0)
                    return -1;
                c.lane[c.n++] = x;
            }
            /*  The city's ramps on the way in: off ramps first, then on ramps. */
            for (pass = 0; pass < 2; pass++)
                for (q = 0; q < s_nramp; q++)
                {
                    const NRamp *rp   = &s_ramp[q];
                    int          near = -1, best = 1 << 30, rn, x;
                    if (rp->region != e || rp->on != pass || rp->heading != rcity_edge_back(arm[a].dir))
                        continue;
                    /*  The arm whose gore's unit is nearest the ramp's band unit. */
                    for (b = 0; b < na; b++)
                    {
                        int32_t side = topology_side(), u = topology_cell(topology_end(topology_road(arm[b].road)->end[arm[b].contact])->cell0 + 1);
                        int     dd = abs((int)(u % side) - (int)(rp->cell_band % side)) + abs((int)(u / side) - (int)(rp->cell_band / side));
                        if (dd < best)
                            best = dd, near = b;
                    }
                    if (near != a || (rn = rg_node_at(rp->band_junction)) < 0)
                        continue;
                    x = rp->on ? lane_in(rp->road, rp->road_contact, 1) : lane_out(rp->road, rp->road_contact, 1);
                    if (rg_ramp(&c, mix(id, ((uint64_t)0x5 << 20) + (uint64_t)q), rn, x, rp->on, s_junc[s_rn[node[0]].junction].end) != 0)
                        return -1;
                }
            /*  The turns, nearest first: a split for each but the last. */
            for (b = 0; b < na; b++)
                if (b != a && b != partner[a])
                {
                    for (t = nt++; t > 0 && nearness(&arm[a], &arm[b]) < nearness(&arm[a], &arm[turn[t - 1]]); t--)
                        turn[t] = turn[t - 1];
                    turn[t] = b;
                }
            for (t = 0; t < nt; t++)
            {
                if (t + 1 < nt)
                {
                    int n2 = rg_node(mix(id, ((uint64_t)0x6 << 20) + (uint64_t)(a * 16 + t)), s_junc[s_rn[node[a]].junction].end), br;
                    if (n2 < 0 || rg_split(&c, mix(id, ((uint64_t)0x7 << 20) + (uint64_t)(a * 16 + t)), n2, &br) != 0)
                        return -1;
                    src[a][turn[t]].node = n2, src[a][turn[t]].n = 1, src[a][turn[t]].lane[0] = br;
                    continue;
                }
                if (!clover)
                {
                    src[a][turn[t]] = c;
                    continue;
                }
                /*  A cloverleaf's left turn: the lane goes on to the weave of its
                 *  own carriageway, and the loop takes it from there. */
                s_ch_next = NNODE_WEAVE;
                if ((ws[a] = rg_node(mix(id, ((uint64_t)0x10 << 20) + (uint64_t)a), s_junc[s_rn[node[a]].junction].end)) < 0 ||
                    rg_advance(&c, mix(id, ((uint64_t)0x11 << 20) + (uint64_t)a), ws[a]) != 0)
                    return -1;
                wfeed[a] = c.lane[0];
            }
            if (chain_end() != 0)
                return -1;
        }
        /*  The chains out: the turns join, the city's ramps, the gore. */
        for (b = 0; b < na; b++)
        {
            int    lanes[8], nl = driving_lanes(arm[b].road, arm[b].contact, 1, lanes, 8), from[RG_ARMS], nf = 0, t, pass;
            RChain c = {
                -1, 0, {-1, -1}
            };
            if (nl < 1)
                continue;
            chain_begin(arm[b].road, arm[b].contact, 1);
            /*  A cloverleaf's leaving lane begins at the inner lane of the weave
             *  of its carriageway, from the arm across. */
            if (clover && ws[partner[b]] >= 0)
            {
                int pa    = partner[b];
                s_ch_next = NNODE_WEAVE;
                if ((we[b] = rg_node(mix(id, ((uint64_t)0x12 << 20) + (uint64_t)b), s_junc[s_rn[node[b]].junction].end)) < 0 ||
                    rg_weave(mix(id, ((uint64_t)0x13 << 20) + (uint64_t)pa), ws[pa], we[b], &win[pa], &wout[pa]) != 0 ||
                    rg_link(ws[pa], wfeed[pa], win[pa]) != 0)
                    return -1;
                c.node = we[b], c.n = 1, c.lane[0] = win[pa];
            }
            for (a = 0; a < na; a++)
                if (src[a][b].node >= 0)
                {
                    for (t = nf++; t > 0 && nearness(&arm[a], &arm[b]) > nearness(&arm[from[t - 1]], &arm[b]); t--)
                        from[t] = from[t - 1];
                    from[t] = a;
                }
            for (t = 0; t < nf; t++)
            {
                RChain s = src[from[t]][b];
                if (t == 0 && c.node < 0)
                {
                    c = s;
                    continue;
                }
                {
                    int n2 = rg_node(mix(id, ((uint64_t)0x8 << 20) + (uint64_t)(b * 16 + t)), s_junc[s_rn[node[b]].junction].end), x;
                    if (n2 < 0 || rg_advance(&c, mix(id, ((uint64_t)0x9 << 20) + (uint64_t)(from[0] * 256 + b * 16 + t)), n2) != 0)
                        return -1;
                    if ((x = rg_strand(mix(id, ((uint64_t)0xA << 20) + (uint64_t)(from[t] * 16 + b)), s.node, n2)) < 0)
                        return -1;
                    for (k = 0; k < s.n; k++)
                        if (rg_link(s.node, s.lane[k], x) != 0)
                            return -1;
                    c.lane[c.n++] = x;
                }
            }
            if (c.node < 0)
            {
                s_ch_road = -1;
                continue;
            }
            for (pass = 0; pass < 2; pass++)
                for (q = 0; q < s_nramp; q++)
                {
                    const NRamp *rp   = &s_ramp[q];
                    int          near = -1, best = 1 << 30, rn, x;
                    if (rp->region != e || rp->on != pass || rp->heading != arm[b].dir)
                        continue;
                    for (a = 0; a < na; a++)
                    {
                        int32_t side = topology_side(), u = topology_cell(topology_end(topology_road(arm[a].road)->end[arm[a].contact])->cell0 + 1);
                        int     dd = abs((int)(u % side) - (int)(rp->cell_band % side)) + abs((int)(u / side) - (int)(rp->cell_band / side));
                        if (dd < best)
                            best = dd, near = a;
                    }
                    if (near != b || (rn = rg_node_at(rp->band_junction)) < 0)
                        continue;
                    x = rp->on ? lane_in(rp->road, rp->road_contact, 1) : lane_out(rp->road, rp->road_contact, 1);
                    if (rg_ramp(&c, mix(id, ((uint64_t)0xB << 20) + (uint64_t)q), rn, x, rp->on, s_junc[s_rn[node[0]].junction].end) != 0)
                        return -1;
                }
            /*  An arm with no way straight on: the chain splits into its
             *  inner lanes. */
            for (k = 0; partner[b] < 0 && k + 1 < nl; k++)
            {
                int n2 = (s_ch_next = NNODE_INNER, rg_node(mix(id, ((uint64_t)0xC << 20) + (uint64_t)(b * 16 + k)), s_junc[s_rn[node[b]].junction].end)), x, br;
                if (n2 < 0 || rg_split(&c, mix(id, ((uint64_t)0xD << 20) + (uint64_t)(b * 16 + k)), n2, &br) != 0)
                    return -1;
                if ((x = rg_strand(mix(id, ((uint64_t)0xE << 20) + (uint64_t)(b * 16 + k)), n2, node[b])) < 0 || rg_link(n2, br, x) != 0 ||
                    rg_link(node[b], x, lanes[k]) != 0)
                    return -1;
            }
            if (rg_advance(&c, mix(id, ((uint64_t)0xF << 20) + (uint64_t)b), node[b]) != 0 || rg_link(node[b], c.lane[0], lanes[nl - 1]) != 0 ||
                chain_end() != 0)
                return -1;
        }
        /*  A cloverleaf's loops.  Each left turn runs from the outer
         *  lane at the end of its own carriageway's weave.  It runs to
         *  the outer lane at the start of the next weave. */
        for (a = 0; clover && a < na; a++)
        {
            int f = -1, pf, x;
            for (b = 0; b < na; b++)
                if (b != a && b != partner[a] && nearness(&arm[a], &arm[b]) == 2)
                    f = b;
            if (f < 0 || (pf = partner[f]) < 0 || wout[a] < 0 || wout[pf] < 0 || we[partner[a]] < 0)
                continue;
            if ((x = rg_strand(mix(id, ((uint64_t)0x14 << 20) + (uint64_t)(a * 16 + f)), we[partner[a]], ws[pf])) < 0 ||
                rg_link(we[partner[a]], wout[a], x) != 0 || rg_link(ws[pf], x, wout[pf]) != 0)
                return -1;
        }
    }
    return 0;
}

/*  The gores of every region, made direct junctions before the roads are
 *  linked.  0, or -1 out of memory. */
static int region_gores(void)
{
    int g;
    for (g = 0; g < topology_ends(); g++)
        if (gore_region(g) >= 0 && direct_junction(g) < 0)
            return -1;
    return 0;
}

/*  The links of a region's node.  1 where `junction` is one, 0 where it
 *  is not, -1 out of memory. */
static int region_links(int junction)
{
    int i, k, q;
    for (i = 0; i < s_nrn; i++)
    {
        const RNode *r = &s_rn[i];
        if (r->junction != junction)
            continue;
        for (k = 0; k < r->nl; k++)
        {
            int seen = 0;
            if (add_link(r->from[k], r->to[k]) != 0)
                return -1;
            for (q = 0; q < k; q++)
                seen |= s_lane[r->from[q]].road == s_lane[r->from[k]].road && s_lane[r->to[q]].road == s_lane[r->to[k]].road;
            if (!seen && add_connection(junction, s_lane[r->from[k]].road, -1, s_lane[r->to[k]].road) != 0)
                return -1;
        }
        return 1;
    }
    return 0;
}

/*  AN INTERCHANGE of three arms or more (plans/interchanges.md, "The
 *  records").  Its own junction carries the highway straight on, each
 *  lane into its own.  A turn between two arms with a way straight on
 *  runs on its region's strands (regions_build).  Any other turn is a
 *  connecting road of it.  An arm with no way straight on divides its
 *  own lanes.  The inner half turns to the far side and the rest to the
 *  near side, each lane into the lane of its place.  An arm with a way
 *  straight on turns so from its outermost lane, and takes such a turn
 *  into its outermost lane. */
static int interchange(int junction)
{
    enum
    {
        BAND_LANES = 3
    };
    Arm arms[IX_MOST];
    int straight[IX_MOST], n = ix_arms(s_junc[junction].end, arms, straight), a, b, k;
    for (a = 0; a < n; a++)
        for (b = 0; b < n; b++)
        {
            int pair[8][2], np = 0, near, t;
            if (a == b)
                continue;
            if (arms[a].dir == rcity_edge_back(arms[b].dir))
            {
                for (k = 1; k <= BAND_LANES; k++)
                {
                    int in = lane_in(arms[a].road, arms[a].contact, k), out = lane_out(arms[b].road, arms[b].contact, k);
                    if (in >= 0 && out >= 0)
                        pair[np][0] = in, pair[np][1] = out, np++;
                }
                if (connect_pairs(junction, &arms[a], &arms[b], pair, np) != 0)
                    return -1;
                continue;
            }
            {
                int r = rcity_edge_next(rcity_edge_back(arms[a].dir));
                near  = arms[b].dir == (s_drive_left ? rcity_edge_back(r) : r);
            }
            for (k = 1; k <= BAND_LANES; k++)
            {
                /*  The lane k of an arm with no way straight on, where the
                 *  turn's side holds it.  That is the outer lanes for a turn
                 *  to the near side, and the inner half for a turn to the
                 *  far side. */
                int inner = k <= BAND_LANES / 2, from, to, dup = 0;
                if (near == inner)
                    continue;
                from = lane_in(arms[a].road, arms[a].contact, straight[a] ? BAND_LANES : k);
                to   = lane_out(arms[b].road, arms[b].contact, straight[b] ? BAND_LANES : k);
                for (t = 0; t < np; t++)
                    dup |= pair[t][0] == from && pair[t][1] == to;
                if (from >= 0 && to >= 0 && np < 8 && !dup)
                    pair[np][0] = from, pair[np][1] = to, np++;
            }
            /*  Each turn is a connecting road of its own. */
            for (t = 0; t < np; t++)
            {
                int turn = inner_lane(junction, RNET_BAND, NLANE_DRIVING, (NLink){NLINK_ROAD, arms[a].road, arms[a].contact}, (NLink){NLINK_ROAD, arms[b].road, arms[b].contact}, 0x1000u + (uint64_t)(a * 64 + b * 8 + t));
                if (turn < 0 || add_link(pair[t][0], turn) != 0 || add_link(turn, pair[t][1]) != 0 ||
                    add_connection(junction, arms[a].road, s_lane[turn].road, arms[b].road) != 0)
                    return -1;
            }
        }
    return 0;
}

/*  Whether a lane is an exit or entry lane on a road of no units. */
static int untapered(int lane)
{
    return nlane_is_change(s_lane[lane].type) && s_road[s_lane[lane].road].length <= 0.0f;
}

/*  Whether junction j holds a ramp beside the band, an on ramp where
 *  `on`, for the band's traffic heading `h`. */
static int ramp_beside(int j, int h, int on)
{
    int i;
    for (i = 0; i < s_nramp; i++)
        if (s_ramp[i].band_junction == j && s_ramp[i].built && !s_ramp[i].endon && s_ramp[i].gore < 0 && s_ramp[i].heading == h && s_ramp[i].on == on)
            return 1;
    return 0;
}

/*  A band junction's roads straight through, where it is no interchange:
 *  each band lane goes on into its own by a connecting road.  Two band
 *  arms that are not in line make a bend, an ordinary movement. */
static int band_through(int junction, const Arm *arms, int n)
{
    int a, b, k;
    for (a = 0; a < n; a++)
        for (b = 0; b < n; b++)
        {
            int pair[8][2], np = 0;
            if (a == b || topology_road(arms[a].road)->net != RNET_BAND || topology_road(arms[b].road)->net != RNET_BAND)
                continue;
            if (arms[a].dir != rcity_edge_back(arms[b].dir))
            {
                if (!behind(&arms[a], &arms[b]) && connect(junction, &arms[a], &arms[b]) != 0)
                    return -1;
                continue;
            }
            const int off_here = ramp_beside(junction, arms[b].dir, 0), on_here = ramp_beside(junction, arms[b].dir, 1);
            for (k = 1; k <= 8; k++)
            {
                int in = lane_in(arms[a].road, arms[a].contact, k), out = lane_out(arms[b].road, arms[b].contact, k);
                /*  A road of no units has no taper.  Its exit lane is fed
                 *  by the lane beside it, and its entry lane goes on into
                 *  the lane beside it. */
                if (!is_travel(in) && is_travel(out) && untapered(out))
                    in = lane_in(arms[a].road, arms[a].contact, k - 1);
                if (is_travel(in) && !is_travel(out) && untapered(in))
                    out = lane_out(arms[b].road, arms[b].contact, k - 1);
                /*  A lane with none of its place across goes on into the
                 *  lane beside it, nearer the line.  A lane fed by none
                 *  comes from the lane beside it.  Where an off ramp here
                 *  takes the lane, it goes into the ramp alone.  Where an
                 *  on ramp here gives the lane, the ramp alone feeds it.
                 *  That is a lane drop and a lane gain (appendix-road-spec,
                 *  7.3). */
                if (is_travel(in) && !is_travel(out) && k > 1)
                    out = off_here ? -1 : lane_out(arms[b].road, arms[b].contact, k - 1);
                else if (!is_travel(in) && is_travel(out) && k > 1)
                    in = on_here ? -1 : lane_in(arms[a].road, arms[a].contact, k - 1);
                if (is_travel(in) && is_travel(out) && np < 8)
                    pair[np][0] = in, pair[np][1] = out, np++;
            }
            if (connect_pairs(junction, &arms[a], &arms[b], pair, np) != 0)
                return -1;
        }
    return 0;
}

/*  Whether ramp `g` of `ng` takes one of the `odd` lanes left over where
 *  the lanes do not divide evenly.  `near` ranks each ramp: 0 beside the
 *  carriageway on its own side, 1 in line with it, 2 across the band.
 *  The ramps in line take the odd lanes first, then those beside, then
 *  those across. */
static int extra(const int *near, int ng, int g, int odd)
{
    static const int ORDER[3] = {1, 0, 2};
    int              r, k, n = 0;
    for (r = 0; r < 3; r++)
        for (k = 0; k < ng; k++)
            if (near[k] == ORDER[r])
            {
                if (k == g)
                    return n < odd;
                n++;
            }
    return 0;
}

/*  THE RAMPS OF A BAND JUNCTION: each ramp's road is an arm of it
 *  (plans/network.md, layer 1, the ramp's band junction).  A ramp whose
 *  taper found no unit joins the band here.  Its lane leaves the band's
 *  outermost arriving lane on its side, or joins the outermost leaving
 *  lane, by a connecting road of one lane.  At the band's end, the off
 *  ramps share the arriving lanes and the on ramps the leaving ones.  So
 *  do ramps where the band has no lane to split or join on their side. */
static int ramp_lanes(int junction, const Arm *arms, int n)
{
    int a, i, k, h, j;
    for (h = 1; h < 16; h <<= 1)
    {
        int idx[16], m = 0, arr = -1, lea = -1, in = -1, out = -1;
        for (i = 0; i < s_nramp && m < 16; i++)
            if (s_ramp[i].band_junction == junction && s_ramp[i].built && !s_ramp[i].endon && s_ramp[i].heading == h)
                idx[m++] = i;
        if (!m)
            continue;
        for (a = 0; a < n; a++)
            if (topology_road(arms[a].road)->net == RNET_BAND)
            {
                if (arms[a].dir == rcity_edge_back(h))
                    arr = a;
                if (arms[a].dir == h)
                    lea = a;
            }
        for (k = 8; arr >= 0 && k >= 1 && in < 0; k--)
            if (is_travel(lane_in(arms[arr].road, arms[arr].contact, k)))
                in = lane_in(arms[arr].road, arms[arr].contact, k);
        for (k = 8; lea >= 0 && k >= 1 && out < 0; k--)
            if (is_travel(lane_out(arms[lea].road, arms[lea].contact, k)))
                out = lane_out(arms[lea].road, arms[lea].contact, k);
        if (in < 0 || out < 0)
            continue;
        /*  Each ramp's lane leaves the band's outermost arriving lane or
         *  joins its outermost leaving lane, by a connecting road of one
         *  lane.  No auxiliary lane is kept (appendix-road-spec, 7.3). */
        for (j = 0; j < m; j++)
        {
            NRamp *rp   = &s_ramp[idx[j]];
            Arm    ramp = (Arm){rp->road, rp->road_contact, rcity_edge_back(rp->heading)};
            int    lane = rp->on ? lane_in(rp->road, rp->road_contact, 1) : lane_out(rp->road, rp->road_contact, 1), pair[1][2];
            for (a = 0; a < n; a++)
                if (arms[a].road == rp->road && arms[a].contact == rp->road_contact)
                    ramp = arms[a];
            if (!is_travel(lane))
                continue;
            pair[0][0] = rp->on ? lane : in, pair[0][1] = rp->on ? out : lane;
            rp->lane_in = in, rp->lane_out = out;
            if (connect_turn(junction, rp->on ? &ramp : &arms[arr], rp->on ? &arms[lea] : &ramp, pair, 1, rp->on ? 2 : 1) != 0)
                return -1;
        }
    }
    /*  The ramps that take or feed the whole carriageway.  They are at a
     *  band's end, or where the band has no lane to split or join on their
     *  side.  The ramps of one kind on one carriageway SHARE its lanes, so
     *  each lane links into one ramp or is fed by one.  A ramp beside the
     *  carriageway on its own side takes the outer lanes.  A ramp in line
     *  with it takes the next, and a ramp across the band the inner ones.  An odd
     *  lane goes to a ramp in line, which goes straight on.  So no lane
     *  crosses another to reach its ramp. */
    for (i = 0; i < s_nramp; i++)
    {
        NRamp *rp = &s_ramp[i];
        int    grp[16], near[16], ng = 0, lanes[8], nl = 0, g, q, from, seen = 0;
        if (rp->band_junction != junction || !rp->built || rp->lane_ramp >= 0 || rp->lane_in >= 0)
            continue;
        /*  A ramp shared out with one before it is done. */
        for (j = 0; j < i && !seen; j++)
            seen = s_ramp[j].band_junction == junction && s_ramp[j].built && s_ramp[j].lane_ramp < 0 && s_ramp[j].lane_in < 0 && s_ramp[j].on == rp->on &&
                   s_ramp[j].band == rp->band && s_ramp[j].band_contact == rp->band_contact;
        if (seen)
            continue;
        /*  The ramps sharing this one's carriageway, in the order they lie
         *  from its outer edge.  Those beside it on its own side come first,
         *  then those in line with it, then those across the band.  A ramp on its own
         *  side has the heading of the carriageway's traffic. */
        for (j = i; j < s_nramp && ng < 16; j++)
        {
            NRamp *o   = &s_ramp[j];
            int    dir = 0;
            if (o->band_junction != junction || !o->built || o->lane_ramp >= 0 || o->lane_in >= 0 || o->on != rp->on || o->band != rp->band ||
                o->band_contact != rp->band_contact)
                continue;
            for (a = 0; a < n; a++)
                if (arms[a].road == o->band && arms[a].contact == o->band_contact)
                    dir = arms[a].dir;
            grp[ng]  = j;
            near[ng] = dir && o->heading == (o->on ? dir : rcity_edge_back(dir)) ? 0 : o->endon || !dir ? 1
                                                                                                        : 2;
            ng++;
        }
        for (g = 1; g < ng; g++)
            for (q = g; q > 0 && near[q] < near[q - 1]; q--)
            {
                int t  = grp[q];
                grp[q] = grp[q - 1], grp[q - 1] = t;
                t = near[q], near[q] = near[q - 1], near[q - 1] = t;
            }
        /*  The carriageway's lanes, from the line out. */
        for (k = 1; k <= 8; k++)
        {
            int l = rp->on ? lane_out(rp->band, rp->band_contact, k) : lane_in(rp->band, rp->band_contact, k);
            if (is_travel(l))
                lanes[nl++] = l;
        }
        /*  From the outermost lane in: each ramp its share.  Where the lanes
         *  do not divide evenly, the ramps in line take a lane more first. */
        from = nl;
        for (g = 0; g < ng; g++)
        {
            NRamp *o    = &s_ramp[grp[g]];
            Arm    ramp = (Arm){o->road, o->road_contact, rcity_edge_back(o->heading)};
            Arm    band = (Arm){o->band, o->band_contact, 0};
            int    take = nl / ng + (extra(near, ng, g, nl % ng) ? 1 : 0), pair[8][2], np = 0, rl;
            for (a = 0; a < n; a++)
            {
                if (arms[a].road == o->road && arms[a].contact == o->road_contact)
                    ramp = arms[a];
                if (arms[a].road == o->band && arms[a].contact == o->band_contact)
                    band = arms[a];
            }
            rl = o->on ? lane_in(ramp.road, ramp.contact, 1) : lane_out(ramp.road, ramp.contact, 1);
            if (!is_travel(rl) || take <= 0)
                continue;
            for (k = from - take; k < from; k++)
                pair[np][0] = o->on ? rl : lanes[k], pair[np][1] = o->on ? lanes[k] : rl, np++;
            for (k = from - take; k < from; k++)
                from -= take;
            if (connect_turn(junction, o->on ? &ramp : &band, o->on ? &band : &ramp, pair, np, o->on ? 2 : 1) != 0)
                return -1;
        }
    }
    return 0;
}

/*  Whether a network's traffic may turn round.  A street's may.  A
 *  highway's and a railway's may not, so their lanes stop at a dead end. */
static int turns_round(int net)
{
    return net == RNET_ROAD;
}

/*  Whether a lane of travel goes on into a lane, and whether a lane feeds
 *  it. */
static int goes_on(int lane)
{
    return lane < s_lcap && s_lout[lane];
}

static int is_fed(int lane)
{
    return lane < s_lcap && s_lin[lane];
}

/*  NO LANE IS STRANDED where its traffic can turn round.  An arm's
 *  arriving lane can go on into nothing, or its leaving lane can be fed by
 *  nothing.  That is so at a dead end and where a network reaches a
 *  junction by one arm alone.  It is also so where every other arm of its
 *  network is one way out.  Then the arm's own traffic turns round where
 *  its network lets it: its arriving lanes go on into its leaving ones.
 *  Elsewhere its lanes end at the junction, and each end is a dead end of
 *  the lane graph. */
static int strand_turns(int junction, const Arm *arms, int n)
{
    int a, k, m, stranded;
    for (a = 0; a < n; a++)
    {
        if (!turns_round(topology_road(arms[a].road)->net))
            continue;
        stranded = 0;
        for (k = 1; k <= 8 && !stranded; k++)
        {
            int in = lane_in(arms[a].road, arms[a].contact, k), out = lane_out(arms[a].road, arms[a].contact, k);
            if (!is_travel(in) && !is_travel(out))
                break;
            stranded = (is_travel(in) && !goes_on(in)) || (is_travel(out) && !is_fed(out));
        }
        if (!stranded)
            continue;
        /*  Not twice: an arm that turns round already. */
        for (m = s_junc[junction].conn0; m < s_nconn; m++)
            if (s_conn[m].incoming == arms[a].road && s_conn[m].outgoing == arms[a].road)
                stranded = 0;
        if (stranded && connect(junction, &arms[a], &arms[a]) != 0)
            return -1;
    }
    return 0;
}

static int make_junctions(void)
{
    int i, ne = topology_ends();
    for (i = 0; i < ne; i++)
    {
        const TopoEnd *e = topology_end(i);
        int            j;
        s_junc_of_end[i] = -1;
        /*  A seam, a gore and the map's edge are plain road ends, and no
         *  junction.  A gore a ramp joins at becomes a direct junction
         *  (gore_ramp), and so does a region's (region_gores).  An end no
         *  road reaches, a region, is no junction. */
        if (e->kind == END_SEAM || e->kind == END_MAP_EDGE || e->kind == END_GORE || s_at0[i + 1] == s_at0[i])
            continue;
        j = PUSH(s_junc, s_njunc, s_cjunc);
        if (j < 0)
            return -1;
        s_junc[j].id     = e->id;
        s_junc[j].type   = NJUNCTION_DEFAULT;
        s_junc[j].end    = i;
        s_junc[j].conn0  = 0;
        s_junc[j].nconn  = 0;
        s_junc_of_end[i] = j;
    }
    return 0;
}

/*  How many driving lanes arrive at road `road`'s contact `c`, or leave it
 *  there where `leaving`. */
static int driving_count(int road, int c, int leaving)
{
    int k, n = 0;
    for (k = 1; k <= 8; k++)
    {
        int l = leaving ? lane_out(road, c, k) : lane_in(road, c, k);
        n += l >= 0 && s_lane[l].type == NLANE_DRIVING;
    }
    return n;
}

/*  A GORE'S DIRECT JUNCTION (plans/network.md, "A ramp IS the highway's
 *  outermost lane").  Each lane of the band's road arriving goes on into
 *  its own lane of the road leaving, both ways.  The lane a ramp takes
 *  or feeds is passed over.  An off ramp's lane is the arriving road's
 *  outermost driving lane on its side going on, and nothing else is.
 *  The band leaving has one lane fewer there.  An on ramp's lane goes on
 *  into the leaving road's outermost driving lane, and nothing else
 *  does.  A ramp beside a region has no band roads here: region_links
 *  joins it. */
static int direct_links(int junction)
{
    int i, skip[8], ns = 0, first = -1;
    /*  The lane each ramp here takes or feeds.  There the band's roads
     *  say so.  An off ramp's is taken where the band leaving has one
     *  driving lane fewer on its side.  An on ramp's is fed where it has
     *  one more.  Where the two carry as many, the ramp's lane divides
     *  from the outermost driving lane or merges into it.  The lane
     *  comes back and goes again at one place.  A ramp at the map's edge
     *  has the band on one side alone, and nothing links there: the lanes
     *  leave the map. */
    for (i = 0; i < s_nramp && ns < 8; i++)
    {
        const NRamp *rp = &s_ramp[i];
        int          lane, din, dout;
        if (rp->band_junction != junction || rp->gore < 0 || rp->region >= 0 || rp->in_road < 0 || rp->out_road < 0)
            continue;
        first = first < 0 ? i : first;
        din = driving_count(rp->in_road, rp->in_contact, 0), dout = driving_count(rp->out_road, rp->out_contact, 1);
        lane = rp->on ? outer_driving(rp->out_road, rp->out_contact, 1) : outer_driving(rp->in_road, rp->in_contact, 0);
        if (lane >= 0 && (rp->on ? dout > din : dout < din))
            skip[ns++] = lane;
    }
    if (first < 0)
        return 0;
    {
        const NRamp *rp = &s_ramp[first];
        if (carry_skip(rp->in_road, rp->in_contact, rp->out_road, rp->out_contact, NULL, 1, skip, ns) != 0 ||
            carry_skip(rp->out_road, rp->out_contact, rp->in_road, rp->in_contact, NULL, 1, skip, ns) != 0 ||
            add_connection(junction, rp->in_road, -1, rp->out_road) != 0 || add_connection(junction, rp->out_road, -1, rp->in_road) != 0)
            return -1;
    }
    /*  An off ramp's lane is the outermost driving lane arriving, and an on
     *  ramp's lane becomes the outermost driving lane leaving. */
    for (i = 0; i < s_nramp; i++)
    {
        const NRamp *rp = &s_ramp[i];
        int          lane, ramp;
        if (rp->band_junction != junction || rp->gore < 0 || rp->region >= 0 || rp->in_road < 0 || rp->out_road < 0)
            continue;
        if (rp->on)
        {
            ramp = lane_in(rp->road, rp->road_contact, 1);
            lane = outer_driving(rp->out_road, rp->out_contact, 1);
            if (!is_travel(ramp) || lane < 0 || add_link(ramp, lane) != 0 || add_connection(junction, rp->road, -1, rp->out_road) != 0)
                return -1;
        }
        else
        {
            ramp = lane_out(rp->road, rp->road_contact, 1);
            lane = outer_driving(rp->in_road, rp->in_contact, 0);
            if (!is_travel(ramp) || lane < 0 || add_link(lane, ramp) != 0 || add_connection(junction, rp->in_road, -1, rp->road) != 0)
                return -1;
        }
    }
    return 0;
}

static int join_junctions(void)
{
    int i;
    for (i = 0; i < s_njunc; i++)
    {
        Arm            arms[MAX_ARMS];
        const TopoEnd *e = topology_end(s_junc[i].end);
        int            n, a, b;
        s_junc[i].conn0 = s_nconn;
        if (s_junc[i].type == NJUNCTION_DIRECT)
        {
            int rg = region_links(i);
            if (rg < 0 || (!rg && direct_links(i) != 0))
                return -1;
            continue;
        }
        n = arms_of_end(s_junc[i].end, arms, MAX_ARMS);
        if (n == MAX_ARMS)
            R_WARN("net", "junction %016llx has more than %d arms, and the rest are not joined", (unsigned long long)s_junc[i].id, MAX_ARMS);
        /*  A band junction joins its band roads, as an interchange where
         *  three or more reach it, and each ramp's road. */
        if (e->net == RNET_BAND)
        {
            for (a = b = 0; a < n; a++)
                b += topology_road(arms[a].road)->net == RNET_BAND;
            if ((b >= 3 ? interchange(i) : band_through(i, arms, n)) != 0 || ramp_lanes(i, arms, n) != 0)
                return -1;
        }
        else
        {
            for (a = 0; a < n; a++)
                for (b = 0; b < n; b++)
                    if (moves(e, arms, n, a, b) && connect_at(i, &arms[a], &arms[b], e, arms, n) != 0)
                        return -1;
        }
        if (strand_turns(i, arms, n) != 0)
            return -1;
    }
    return 0;
}

/*  ---- control and sidewalks ------------------------------------------- */

/*  The traffic byte of a cell, from the city's half-resolution layer. */
static int traffic_at(int32_t cell)
{
    int32_t n = topology_side(), half = n / 2;
    if (!s_city || !s_city->xtrf || cell < 0)
        return 0;
    return s_city->xtrf[((cell / n) / 2) * half + (cell % n) / 2];
}

/*  The unit of a road next to the junction at its contact.  It is the
 *  first unit at the road's start and the last at its end, else the unit
 *  of the end itself. */
static int32_t unit_next_to(int road, int contact)
{
    const TopoRoad *t = topology_road(road);
    if (!t->nunit)
        return t->at[contact];
    return topology_cell(contact ? t->unit0 + t->nunit - 1 : t->unit0);
}

static int add_control(int junction, const Arm *a, int code)
{
    int at = PUSH(s_ctl, s_nctl, s_cctl);
    if (at < 0)
        return -1;
    s_ctl[at].junction = junction;
    s_ctl[at].road     = a->road;
    s_ctl[at].contact  = a->contact;
    s_ctl[at].control  = (uint8_t)code;
    return 0;
}

static int edge_index(int e)
{
    return e == 1 ? 0 : e == 2 ? 1
                    : e == 4   ? 2
                               : 3;
}

/*  A junction is BUSY, and its control a signal rather than an all-way
 *  stop, where the traffic on its own cell passes this.  The traffic is
 *  the simulation's own byte, 0 to 255.  It is the network's, so it is
 *  a constant here and not tuned. */
#define JUNCTION_BUSY 170

/*  A street junction's control, one an arm, by the ladder of
 *  plans/network.md.  An arm's class is its road's at the junction.  The
 *  arms are grouped by the edge they leave by. */
static int controls(int junction)
{
    Arm            arms[MAX_ARMS];
    const TopoEnd *e = topology_end(s_junc[junction].end);
    int            n, a, d, ctl[4] = {0}, has[4] = {0}, cls[4] = {0}, traffic[4] = {0}, hi = 0, lo = 9, legs = 0, busy;
    /*  A LEVEL CROSSING: the railway has priority, and each road arm
     *  yields to it.  How the crossing is guarded is the look's. */
    if (e->kind == END_MEET)
    {
        int rail = 0;
        n        = arms_of_end(s_junc[junction].end, arms, MAX_ARMS);
        for (a = 0; a < n; a++)
            rail |= topology_road(arms[a].road)->net == RNET_RAIL;
        for (a = 0; a < n && rail; a++)
            if (topology_road(arms[a].road)->net == RNET_ROAD && add_control(junction, &arms[a], NCONTROL_RAILWAY) != 0)
                return -1;
        return 0;
    }
    if (s_junc[junction].type != NJUNCTION_DEFAULT || e->kind != END_NODE || e->net != RNET_ROAD)
        return 0;
    n = arms_of_end(s_junc[junction].end, arms, MAX_ARMS);
    for (a = 0; a < n; a++)
    {
        int t;
        d      = edge_index(arms[a].dir);
        has[d] = 1;
        if (s_sec[section_at(arms[a].road, arms[a].contact)].cls > cls[d])
            cls[d] = s_sec[section_at(arms[a].road, arms[a].contact)].cls;
        t = traffic_at(unit_next_to(arms[a].road, arms[a].contact));
        if (t > traffic[d])
            traffic[d] = t;
    }
    for (d = 0; d < 4; d++)
        if (has[d])
        {
            legs++;
            if (cls[d] > hi)
                hi = cls[d];
            if (cls[d] < lo)
                lo = cls[d];
        }
    if (legs < 2)
        return 0;
    busy = traffic_at(topology_cell(e->cell0)) > JUNCTION_BUSY;
    for (d = 0; d < 4; d++)
    {
        if (!has[d])
            continue;
        if (hi >= 2)
            ctl[d] = 2;
        else if (hi == 1 && lo == 1)
            ctl[d] = busy ? 2 : 1;
        else if (hi == 1)
            ctl[d] = cls[d] == 0;
        else if (legs == 3)
            ctl[d] = !has[d ^ 2];
        else
        {
            /*  Edges 0 and 2 are the two across columns, 1 and 3 across
             *  rows.  The stop goes on the axis whose busiest arm carries
             *  less, the rows' axis where they tie. */
            int cols = traffic[0] > traffic[2] ? traffic[0] : traffic[2];
            int rows = traffic[1] > traffic[3] ? traffic[1] : traffic[3];
            ctl[d]   = (d & 1) ? rows <= cols : cols < rows;
        }
    }
    for (a = 0; a < n; a++)
        if (ctl[edge_index(arms[a].dir)] && add_control(junction, &arms[a], ctl[edge_index(arms[a].dir)]) != 0)
            return -1;
    return 0;
}

static int control_of(int junction, const Arm *a)
{
    int i;
    for (i = 0; i < s_nctl; i++)
        if (s_ctl[i].junction == junction && s_ctl[i].road == a->road && s_ctl[i].contact == a->contact)
            return s_ctl[i].control;
    return 0;
}

/*  An arm's sidewalk on its left or its right, seen leaving the junction.
 *  Leaving by the road's start runs with `s`, so its left is the positive
 *  side.  -1 where the arm has none. */
static int sidewalk(const Arm *a, int left)
{
    return sidewalk_in_section(section_at(a->road, a->contact), (left != 0) == (a->contact == 0) ? 1 : -1);
}

/*  A connecting road of one sidewalk lane between two sidewalk lanes,
 *  linked both ways. */
static int walk_between(int junction, const Arm *a, const Arm *b, int from, int to, uint64_t salt)
{
    uint64_t id = mix(mix(mix(mix(14695981039346656037ull, s_junc[junction].id), s_road[a->road].id * 2u + (uint64_t)a->contact),
                          s_road[b->road].id * 2u + (uint64_t)b->contact),
                      salt);
    int      c  = new_road(id, RNET_ROAD, -1, junction, 1.0f, 0, 1), l;
    if (c < 0)
        return -1;
    l              = s_sec[s_road[c].sec0].lane0;
    s_lane[l].type = NLANE_SIDEWALK;
    s_road[c].pred = (NLink){NLINK_ROAD, a->road, a->contact};
    s_road[c].succ = (NLink){NLINK_ROAD, b->road, b->contact};
    if (sidewalk_on(from, a->contact, l, 0) != 0 || sidewalk_on(l, 1, to, b->contact) != 0)
        return -1;
    return add_connection(junction, a->road, c, b->road);
}

typedef struct
{
    Arm   arm;
    float angle;
} Around;

static int by_angle(const void *x, const void *y)
{
    float a = ((const Around *)x)->angle, b = ((const Around *)y)->angle;
    return a < b ? -1 : a > b;
}

/*  A junction's sidewalks.  Its arms that have sidewalks are taken in
 *  their order around it.  A CORNER joins each arm's sidewalk on the side
 *  of the next arm to that arm's sidewalk facing it.  One arm alone is a
 *  dead end, and its corner runs round the cap from its left to its right.
 *  A CROSSWALK joins an arm's two sidewalks across its mouth where the arm
 *  has a control. */
static int sidewalks(int junction)
{
    Arm            arms[MAX_ARMS];
    Around         ring[MAX_ARMS];
    const TopoEnd *e    = topology_end(s_junc[junction].end);
    int32_t        side = topology_side();
    float          cx = 0.0f, cy = 0.0f;
    int            n = arms_of_end(s_junc[junction].end, arms, MAX_ARMS), m = 0, a, k;
    for (k = 0; k < e->ncell; k++)
    {
        int32_t cell = topology_cell(e->cell0 + k);
        cx += (float)(cell % side) + 0.5f;
        cy += (float)(cell / side) + 0.5f;
    }
    cx /= (float)(e->ncell > 0 ? e->ncell : 1);
    cy /= (float)(e->ncell > 0 ? e->ncell : 1);
    for (a = 0; a < n; a++)
    {
        int32_t at = topology_road(arms[a].road)->at[arms[a].contact];
        int     dc, dr;
        if (sidewalk(&arms[a], 1) < 0 || sidewalk(&arms[a], 0) < 0 || at < 0)
            continue;
        dc = rcity_edge_dc(arms[a].dir), dr = rcity_edge_dr(arms[a].dir);
        ring[m].arm   = arms[a];
        ring[m].angle = atan2f((float)(at / side) + 0.5f + 0.5f * (float)dr - cy, (float)(at % side) + 0.5f + 0.5f * (float)dc - cx);
        m++;
    }
    qsort(ring, (size_t)m, sizeof *ring, by_angle);
    /*  The next arm by angle lies to the left of an arm leaving the
     *  junction, in the frame l_right turns right in. */
    for (a = 0; a < m; a++)
    {
        const Arm *A = &ring[a].arm, *B = &ring[(a + 1) % m].arm;
        /*  Across a level meet a sidewalk goes straight on to the one in
         *  line with it across, as the traffic does.  No corner joins two
         *  arms that are not in line.  An arm with no arm in line ends
         *  there, and its sidewalks join round its end as at a dead end. */
        if (e->kind == END_MEET)
        {
            int b, across = -1, ax, ay, bx, by;
            mouth(A, &ax, &ay);
            for (b = 0; b < m; b++)
            {
                mouth(&ring[b].arm, &bx, &by);
                if (ring[b].arm.dir == rcity_edge_back(A->dir) && ((A->dir == 1 || A->dir == 4) ? ay == by : ax == bx))
                    across = b;
            }
            if (across >= 0 ? walk_between(junction, A, &ring[across].arm, sidewalk(A, 1), sidewalk(&ring[across].arm, 0), 1u) != 0
                            : walk_between(junction, A, A, sidewalk(A, 1), sidewalk(A, 0), 1u) != 0)
                return -1;
            continue;
        }
        if (walk_between(junction, A, B, sidewalk(A, 1), sidewalk(B, 0), 1u) != 0)
            return -1;
        /*  A crosswalk across each mouth a street junction controls. */
        if ((control_of(junction, A) == NCONTROL_STOP || control_of(junction, A) == NCONTROL_SIGNAL) &&
            walk_between(junction, A, A, sidewalk(A, 1), sidewalk(A, 0), 2u) != 0)
            return -1;
    }
    return 0;
}

/*  THE LANE GRAPH'S NODES.  A lane has two ends.  End 2 * lane + 0 is at
 *  the start of its section in `s`, and 2 * lane + 1 at its end.  Two ends
 *  a link joins are one node, so the nodes are the classes of ends the
 *  links join.  Each node says where it lies.  That is at a contact of a
 *  road the topology found, or at a section boundary inside one.  Else it
 *  lies inside the junction whose connecting roads alone meet there. */
static NNode *s_node;
static int    s_nnode, s_cnode;
static int   *s_lnode; /* per lane end, its node */

static int root_of(int *up, int e)
{
    while (up[e] != e)
        e = up[e] = up[up[e]];
    return e;
}

static int lane_nodes(void)
{
    int *up = (int *)malloc(sizeof *up * (size_t)(2 * s_nlane + 1)), i, e;
    s_nnode = 0;
    free(s_lnode);
    s_lnode = (int *)malloc(sizeof *s_lnode * (size_t)(2 * s_nlane + 1));
    if (!up || !s_lnode)
    {
        free(up);
        return -1;
    }
    for (e = 0; e < 2 * s_nlane; e++)
        up[e] = e, s_lnode[e] = -1;
    for (i = 0; i < s_nlink; i++)
    {
        int a = root_of(up, 2 * s_link[i].from + s_link[i].from_end), b = root_of(up, 2 * s_link[i].to + s_link[i].to_end);
        if (a != b)
            up[a < b ? b : a] = a < b ? a : b;
    }
    for (e = 0; e < 2 * s_nlane; e++)
    {
        int          r = root_of(up, e), at = s_lnode[r];
        const NLane *l     = &s_lane[e / 2];
        const NRoad *rd    = &s_road[l->road];
        int          first = l->section == rd->sec0, last = l->section == rd->sec0 + rd->nsec - 1;
        NNode       *n;
        /*  A median and a border are in no node. */
        if (!traveled(l->type) && l->type != NLANE_SIDEWALK)
            continue;
        if (at < 0)
        {
            at = PUSH(s_node, s_nnode, s_cnode);
            if (at < 0)
            {
                free(up);
                return -1;
            }
            s_node[at] = (NNode){-1, -1, 0.0f, -1, 0, 0};
            s_lnode[r] = at;
        }
        s_lnode[e] = at;
        n          = &s_node[at];
        /*  A road's contact comes before a section boundary, and either
         *  before a junction's inside.  A road is any road of no junction:
         *  a topology road, or an interchange's ramp road. */
        if (rd->junction < 0 && ((e & 1) ? last : first))
            n->road = l->road, n->contact = e & 1, n->junction = -1;
        else if (rd->junction < 0 && n->contact < 0)
        {
            n->road = l->road, n->junction = -1;
            n->s = (e & 1) ? s_sec[l->section + 1].s : s_sec[l->section].s;
        }
        else if (n->road < 0)
            n->junction = rd->junction;
        if (traveled(l->type))
        {
            if ((e & 1) == with_s(e / 2))
                n->nin++;
            else
                n->nout++;
        }
    }
    free(up);
    return 0;
}

int network_nodes(void)
{
    return s_nnode;
}

const NNode *network_node(int i)
{
    return i >= 0 && i < s_nnode ? &s_node[i] : NULL;
}

/*  Every junction's connections, one run each: conn0 and nconn.  The
 *  order within a junction is the order they were filed. */
static int group_connections(void)
{
    NConnection *sorted = (NConnection *)malloc(sizeof *sorted * ((size_t)s_nconn + 1));
    int         *at     = (int *)calloc((size_t)s_njunc + 1, sizeof *at);
    int          j, k, n = 0;
    if (!sorted || !at)
    {
        free(sorted), free(at);
        return -1;
    }
    for (j = 0; j < s_njunc; j++)
        s_junc[j].nconn = 0;
    for (k = 0; k < s_nconn; k++)
        s_junc[s_conn_junc[k]].nconn++;
    for (j = 0; j < s_njunc; j++)
        s_junc[j].conn0 = at[j] = n, n += s_junc[j].nconn;
    for (k = 0; k < s_nconn; k++)
        sorted[at[s_conn_junc[k]]++] = s_conn[k];
    memcpy(s_conn, sorted, sizeof *sorted * (size_t)s_nconn);
    for (j = 0; j < s_njunc; j++)
        for (k = s_junc[j].conn0; k < s_junc[j].conn0 + s_junc[j].nconn; k++)
            s_conn_junc[k] = j;
    free(sorted), free(at);
    return 0;
}

/*  The links at each lane's end, indexed once every link is filed:
 *  s_efirst[2 * lane + end] is the first entry, s_enext the next, and an
 *  entry 2 * link + side is that link seen from its `from` (0) or its
 *  `to` (1). */
static int *s_efirst, *s_enext, s_ecap_lane, s_ecap_link;

static int end_links_index(void)
{
    int k, *last;
    if (2 * s_nlane > s_ecap_lane)
    {
        int *p = (int *)realloc(s_efirst, sizeof *p * (size_t)(2 * s_nlane));
        if (!p)
            return -1;
        s_efirst = p, s_ecap_lane = 2 * s_nlane;
    }
    if (2 * s_nlink > s_ecap_link)
    {
        int *p = (int *)realloc(s_enext, sizeof *p * (size_t)(2 * s_nlink));
        if (!p)
            return -1;
        s_enext = p, s_ecap_link = 2 * s_nlink;
    }
    last = (int *)malloc(sizeof *last * (size_t)(2 * s_nlane + 1));
    if (!last)
        return -1;
    for (k = 0; k < 2 * s_nlane; k++)
        s_efirst[k] = last[k] = -1;
    /*  Each end's entries in the order the links were filed: a link's
     *  `from` side before its `to` side. */
    for (k = 0; k < 2 * s_nlink; k++)
    {
        const NLaneLink *lk = &s_link[k / 2];
        int              e  = k & 1 ? 2 * lk->to + lk->to_end : 2 * lk->from + lk->from_end;
        s_enext[k]          = -1;
        if (last[e] < 0)
            s_efirst[e] = k;
        else
            s_enext[last[e]] = k;
        last[e] = k;
    }
    free(last);
    return 0;
}

int network_end_link(int lane, int end, int it, int *other, int *other_end, int *into)
{
    const NLaneLink *lk;
    if (lane < 0 || lane >= s_nlane || !s_efirst)
        return -1;
    it = it < 0 ? s_efirst[2 * lane + (end != 0)] : s_enext[it];
    if (it < 0)
        return -1;
    lk = &s_link[it / 2];
    if (other)
        *other = it & 1 ? lk->from : lk->to;
    if (other_end)
        *other_end = it & 1 ? lk->from_end : lk->to_end;
    if (into)
        *into = it & 1;
    return it;
}

int network_lane_node(int lane, int end)
{
    return s_lnode && lane >= 0 && lane < s_nlane ? s_lnode[2 * lane + (end != 0)] : -1;
}

static int   file_widths(void);
static int   lamps(void);
static int   bridges(void);
static float work_width(int lane, float ds);
static void  lane_t(int lane, float ds, float (*width)(int lane, float ds), float *t_in, float *t_out);

int network_build(const RCity *c)
{
    int i, nt = topology_roads(), ne = topology_ends();
    s_city       = c;
    s_nctl       = 0;
    s_nobj       = 0;
    s_drive_left = c->drive_left != 0;
    s_side       = s_drive_left ? -1 : 1;
    s_nroad = s_nsec = s_nlane = s_njunc = s_nconn = s_nlink = s_nramp = s_nrn = s_nrg_taper = 0;
    s_ch_road = -1, s_ch_n = 0;
    if (s_jch_road)
        memset(s_jch_road, 0xFF, sizeof(int) * (size_t)s_cjch);
    s_nrun = s_street_clash = 0;
    if (s_lcap)
        memset(s_lout, 0, sizeof *s_lout * (size_t)s_lcap), memset(s_lin, 0, sizeof *s_lin * (size_t)s_lcap);
    s_oneway_clash = s_ramps_built = s_ramps_no_band = s_ramps_no_road = 0;
    free(s_junc_of_end), free(s_oneway), free(s_aux), free(s_drop), free(s_back);
    s_junc_of_end = (int *)malloc(sizeof(int) * (size_t)(ne > 0 ? ne : 1));
    s_oneway      = (int8_t *)calloc((size_t)(nt > 0 ? nt : 1), 1);
    s_aux         = (uint8_t *)calloc((size_t)(nt > 0 ? nt : 1), 1);
    s_drop        = (uint8_t *)calloc((size_t)(nt > 0 ? nt : 1), 1);
    s_back        = (uint8_t *)calloc((size_t)(nt > 0 ? 2 * nt : 1), 1);
    if (!s_junc_of_end || !s_oneway || !s_aux || !s_drop || !s_back || index_ends() != 0 || make_junctions() != 0 || region_cells() != 0 ||
        find_ramps() != 0 || region_gores() != 0)
        goto fail;
    ramps_built();
    ramp_drops();
    if (aux_lanes() != 0)
        goto fail;
    /*  A built ramp makes its road one way.  A ramp
     *  with no band road to join is no ramp, and its road ends there
     *  two-way. */
    for (i = 0; i < s_nramp; i++)
        if (s_ramp[i].built)
        {
            mark_oneway(s_ramp[i].road, s_ramp[i].road_contact, s_ramp[i].on);
        }
    if (street_runs() != 0)
        goto fail;
    for (i = 0; i < nt; i++)
    {
        const TopoRoad *t = topology_road(i);
        if (new_road(t->id, t->net, i, -1, road_len(i), s_oneway[i] == 2 ? 0 : s_oneway[i], 0) != i)
            goto fail;
    }
    if (regions_build() != 0)
        goto fail;
    if (road_links() != 0 || join_junctions() != 0)
        goto fail;
    for (i = 0; i < s_njunc; i++)
        if (controls(i) != 0 || sidewalks(i) != 0)
            goto fail;
    if (bridges() != 0)
        goto fail;
    s_city = NULL;
    lane_widths();
    back_tapers();
    ramp_lane_types();
    region_tapers();
    if (file_widths() != 0 || lane_nodes() != 0 || end_links_index() != 0 || group_connections() != 0 || lamps() != 0)
        goto fail;
    {
        int bad = network_check(0);
        if (bad > 0)
            R_WARN("net", "%d lanes of the road network break the rule that every lane goes on and is fed (--net-check names them)", bad);
    }
    return 0;
fail:
    R_ERR("net", "the road network could not be built: out of memory");
    s_nroad = s_nsec = s_nlane = s_njunc = s_nconn = s_nlink = s_nnode = 0;
    return -1;
}

/*  THE STREET LAMPS (plans/network.md, "the street lamps").  A row of
 *  lamps is one object with a repeat, along each lane section of a road.
 *  A street's and an avenue's stand in their sidewalks at the road's
 *  edge, the two sides staggered by half the distance.  A boulevard's stand in its median, and a band's
 *  between its two ways.  A ramp's road carries none.  Each row starts
 *  half its distance in from its section's ends, so no lamp stands in a
 *  junction.  The distance is in units, a tile a unit. */
static const struct
{
    uint8_t type;
    float   height, distance;
} LAMP_OF[4] = {
    {NOBJ_LAMP_COBRA, 9.0f,  1.5f}, /* a street */
    {NOBJ_LAMP_COBRA, 10.0f, 1.0f}, /* an avenue */
    {NOBJ_LAMP_TWIN,  10.0f, 1.0f}, /* a boulevard */
    {NOBJ_LAMP_TWIN,  12.0f, 1.5f}, /* a band */
};

static int lamp_row(int road, uint8_t type, int8_t facing, float s0, float s1, float distance, float shift, float t, float height)
{
    float from = s0 + 0.5f * distance + shift;
    int   at;
    if (from > s1 - 0.25f * distance)
        return 0;
    at = PUSH(s_obj, s_nobj, s_cobj);
    if (at < 0)
        return -1;
    s_obj[at].road = road, s_obj[at].type = type, s_obj[at].facing = facing;
    s_obj[at].s = from, s_obj[at].length = s1 - 0.5f * distance - from, s_obj[at].distance = distance;
    s_obj[at].t = t, s_obj[at].height = height;
    if (s_obj[at].length < 0.0f)
        s_obj[at].length = 0.0f;
    return 0;
}

/*  A ramp's or an interchange strand's lamps: one row of cobra heads on
 *  the outer edge of its outermost lane, facing in over it.  They stand
 *  every LAMP_RAMP_DISTANCE units. */
#define LAMP_RAMP_DISTANCE 1.5f
static int ramp_lamps(int r)
{
    const NRoad *rd = &s_road[r];
    int          q, k;
    for (q = 0; q < rd->nsec; q++)
    {
        const NSection *sc = &s_sec[rd->sec0 + q];
        float           s0 = sc->s, s1 = q + 1 < rd->nsec ? s_sec[rd->sec0 + q + 1].s : rd->length, best = 0.0f;
        int             have = 0;
        for (k = 0; k < sc->nlane; k++)
        {
            float t_in, t_out;
            if (!traveled(s_lane[sc->lane0 + k].type))
                continue;
            network_lane_t(sc->lane0 + k, 0.0f, &t_in, &t_out);
            if (!have || fabsf(t_out) > fabsf(best))
                best = t_out, have = 1;
        }
        if (!have)
            continue;
        /*  A piece shorter than a row's start and end, as each strand of
         *  an interchange is (a unit long), takes one lamp at its middle. */
        if (s1 - s0 < 1.25f * LAMP_RAMP_DISTANCE)
        {
            if (lamp_row(r, NOBJ_LAMP_COBRA, (int8_t)(best > 0.0f ? -1 : 1), s0, s1, 2.0f * (0.5f * (s1 - s0)), 0.0f, best, 10.0f) != 0)
                return -1;
        }
        else if (lamp_row(r, NOBJ_LAMP_COBRA, (int8_t)(best > 0.0f ? -1 : 1), s0, s1, LAMP_RAMP_DISTANCE, 0.0f, best, 10.0f) != 0)
            return -1;
    }
    return 0;
}

/*  One lamp, on its own: an object of no length. */
static int lamp_one(int road, uint8_t type, int8_t facing, float s, float t, float height)
{
    int at = PUSH(s_obj, s_nobj, s_cobj);
    if (at < 0)
        return -1;
    s_obj[at].road = road, s_obj[at].type = type, s_obj[at].facing = facing;
    s_obj[at].s = s, s_obj[at].length = 0.0f, s_obj[at].distance = 1.0f, s_obj[at].t = t, s_obj[at].height = height;
    return 0;
}

/*  A street's or an avenue's lamp at each junction it reaches.  It
 *  stands at the road's end, in the sidewalk on the right as traffic
 *  leaves the junction, at its border with the road.  Every arm has one,
 *  so each corner of a junction has one. */
static int corner_lamps(int r, int cls)
{
    const NRoad *rd = &s_road[r];
    int          c, k;
    for (c = 0; c < 2; c++)
    {
        const NLink    *ln = c ? &rd->succ : &rd->pred;
        const NSection *sc = &s_sec[rd->sec0 + (c ? rd->nsec - 1 : 0)];
        float           s  = c ? rd->length - 0.1f : 0.1f, t_in, t_out;
        if (ln->kind != NLINK_JUNCTION || s_junc[ln->index].type != NJUNCTION_DEFAULT)
            continue;
        for (k = 0; k < sc->nlane; k++)
        {
            const NLane *l = &s_lane[sc->lane0 + k];
            if (l->type != NLANE_SIDEWALK || (l->id > 0) != (c == 1))
                continue;
            network_lane_t(sc->lane0 + k, s - sc->s, &t_in, &t_out);
            if (lamp_one(r, LAMP_OF[cls].type, (int8_t)(l->id > 0 ? -1 : 1), s, t_in, LAMP_OF[cls].height) != 0)
                return -1;
        }
    }
    return 0;
}

static int lamps(void)
{
    int r, q, k;
    for (r = 0; r < s_nroad; r++)
    {
        const NRoad *rd   = &s_road[r];
        int          ramp = 0;
        if (rd->net != RNET_ROAD && rd->net != RNET_BAND)
            continue;
        /*  A street junction's own connecting roads carry none: its
         *  corners do.  An interchange's strands and every ramp are lit. */
        if (rd->junction >= 0 && rd->net == RNET_ROAD)
            continue;
        for (q = 0; q < rd->nsec; q++)
            for (k = 0; k < s_sec[rd->sec0 + q].nlane; k++)
            {
                int ty = s_lane[s_sec[rd->sec0 + q].lane0 + k].type;
                ramp |= ty == NLANE_ONRAMP || ty == NLANE_OFFRAMP || ty == NLANE_CONNRAMP;
            }
        if (ramp || rd->topo < 0)
        {
            if (ramp_lamps(r) != 0)
                return -1;
            continue;
        }
        if (rd->net == RNET_ROAD && s_sec[rd->sec0].cls < 2 && corner_lamps(r, s_sec[rd->sec0].cls) != 0)
            return -1;
        for (q = 0; q < rd->nsec; q++)
        {
            const NSection *sc = &s_sec[rd->sec0 + q];
            float           s0 = sc->s, s1 = q + 1 < rd->nsec ? s_sec[rd->sec0 + q + 1].s : rd->length;
            int             cls = rd->net == RNET_BAND ? 3 : (sc->cls > 2 ? 2 : sc->cls);
            float           d = LAMP_OF[cls].distance, h = LAMP_OF[cls].height;
            if (cls >= 2)
            {
                if (lamp_row(r, LAMP_OF[cls].type, 0, s0, s1, d, 0.0f, sc->offset, h) != 0)
                    return -1;
                continue;
            }
            /*  The sidewalks' borders with the road, each side's lamps
             *  reaching over the road. */
            for (k = 0; k < sc->nlane; k++)
            {
                int   ln = sc->lane0 + k;
                float t_in, t_out;
                if (s_lane[ln].type != NLANE_SIDEWALK)
                    continue;
                network_lane_t(ln, 0.0f, &t_in, &t_out);
                if (lamp_row(r, LAMP_OF[cls].type, (int8_t)(s_lane[ln].id > 0 ? -1 : 1), s0, s1, d, s_lane[ln].id > 0 ? 0.5f * d : 0.0f, t_in, h) != 0)
                    return -1;
            }
        }
    }
    return 0;
}

/*  THE BRIDGES (plans/network.md, "the bridges").  A bridge object is
 *  one run of a road's units whose cells carry a bridge piece of the
 *  road's own network.  It starts at the run's first unit and spans the
 *  run.  The cells say where a bridge is.  Nothing else does. */
static int bridges(void)
{
    int r, k;
    for (r = 0; r < s_nroad; r++)
    {
        const NRoad    *rd = &s_road[r];
        const TopoRoad *t;
        int             from = -1;
        if (rd->topo < 0)
            continue;
        t = topology_road(rd->topo);
        for (k = 0; k <= t->nunit; k++)
        {
            int on = 0;
            if (k < t->nunit)
            {
                int32_t cell = topology_cell(t->unit0 + k);
                RPart   p[2];
                int     n = rcity_parts(s_city->xbld[cell], s_city->xbit[cell], p), j;
                for (j = 0; j < n; j++)
                    on |= p[j].kind == RPART_BRIDGE && p[j].net == rd->net;
            }
            if (on && from < 0)
                from = k;
            else if (!on && from >= 0)
            {
                int at = PUSH(s_obj, s_nobj, s_cobj);
                if (at < 0)
                    return -1;
                memset(&s_obj[at], 0, sizeof s_obj[at]);
                s_obj[at].road = r, s_obj[at].type = NOBJ_BRIDGE;
                s_obj[at].s = (float)from, s_obj[at].length = (float)(k - from);
                from = -1;
            }
        }
    }
    return 0;
}

int network_objects(void)
{
    return s_nobj;
}

const NObject *network_object(int i)
{
    return i >= 0 && i < s_nobj ? &s_obj[i] : NULL;
}

/*  THE WIDTHS of the lanes, in tiles (plans/network.md, "A lane's
 *  width").  A road's family and class give its whole width.  A
 *  sidewalk, a median, a shoulder and a railway's track each take a fixed
 *  width.
 *  The lanes of the type that fills its road share what is left.  That
 *  is the traffic lanes of a street or a highway, and the borders of a
 *  railway. */
static const float STREET_WIDTH[3] = {0.5f, 0.7f, 0.9f}; /* a street, an avenue and a boulevard */
#define RAIL_WIDTH     0.2f                              /* a railway: its two tracks and its gravel */
#define BAND_WIDTH     1.5f                              /* a highway */
#define SIDEWALK_WIDTH 0.07f
#define MEDIAN_WIDTH   0.04f  /* each of a boulevard's two median lanes */
#define BAND_MEDIAN    0.05f  /* each of a highway's two median lanes */
#define SHOULDER_WIDTH 0.03f  /* a highway's shoulder */
#define TRACK_WIDTH    0.056f /* a rail lane: its track lies halfway across it */
#define EASE           1.0f   /* the units over which a lane eases to a new width */

static float road_width(int net, int cls)
{
    if (net == RNET_RAIL)
        return RAIL_WIDTH;
    if (net == RNET_BAND)
        return BAND_WIDTH;
    return STREET_WIDTH[cls < 3 ? cls : 0];
}

/*  A lane type's fixed width, or 0 for the type that fills its road.  A
 *  highway's exit, entry and auxiliary lanes are a lane's width of their
 *  own, outside its lanes of travel. */
static float fixed_width(int net, int type)
{
    if (net == RNET_BAND && nlane_is_change(type))
        return (BAND_WIDTH - 2.0f * BAND_MEDIAN - 2.0f * SHOULDER_WIDTH) / 6.0f;
    if (type == NLANE_SIDEWALK)
        return SIDEWALK_WIDTH;
    if (type == NLANE_MEDIAN)
        return net == RNET_BAND ? BAND_MEDIAN : MEDIAN_WIDTH;
    if (type == NLANE_SHOULDER)
        return SHOULDER_WIDTH;
    return type == NLANE_RAIL ? TRACK_WIDTH : 0.0f;
}

/*  A highway lane's width, where no auxiliary lane runs.  It is what is
 *  left of a highway after its medians and shoulders, shared by its
 *  three lanes each way. */
static float highway_lane(void)
{
    return (BAND_WIDTH - 2.0f * BAND_MEDIAN - 2.0f * SHOULDER_WIDTH) / 6.0f;
}

/*  A section's whole width.  A ramp's road carries its one lane as wide as
 *  a highway lane (plans/network.md, "A ramp's road"). */
static float section_road_width(int sec, int net, int cls)
{
    const NRoad *r = &s_road[s_lane[s_sec[sec].lane0].road];
    if (r->topo < 0 && r->junction < 0)
    {
        /*  A region's bundle, strand or pair: its lanes each a highway lane
         *  wide, its medians, shoulders, entry and exit lanes their own. */
        float w = 0.0f;
        int   i;
        for (i = 0; i < s_sec[sec].nlane; i++)
        {
            int type = s_lane[s_sec[sec].lane0 + i].type;
            w += type == NLANE_DRIVING || type == NLANE_CONNRAMP ? highway_lane() : fixed_width(net, type);
        }
        return w;
    }
    if (net == RNET_ROAD && r->topo >= 0 && s_oneway && (s_oneway[r->topo] == 1 || s_oneway[r->topo] == -1))
        return highway_lane();
    if (net == RNET_BAND && r->topo >= 0)
    {
        /*  A side whose outermost lane a ramp has taken is a lane and a
         *  shoulder narrower: the lanes that stay keep their width. */
        float w = road_width(net, cls);
        int   i;
        for (i = 0; i < s_sec[sec].nlane; i++)
        {
            int type = s_lane[s_sec[sec].lane0 + i].type;
            if (nlane_is_change(type))
                w += fixed_width(net, type);
        }
        if (s_drop)
            w -= (highway_lane() + SHOULDER_WIDTH) * (float)(((s_drop[r->topo] & 1) != 0) + ((s_drop[r->topo] & 2) != 0));
        return w;
    }
    return road_width(net, cls);
}

/*  The width each filling lane of a section takes. */
static float fill_width(int sec, int net, int cls)
{
    float fixed = 0.0f;
    int   n     = 0, i;
    for (i = 0; i < s_sec[sec].nlane; i++)
    {
        float w = fixed_width(net, s_lane[s_sec[sec].lane0 + i].type);
        fixed += w, n += w <= 0.0f;
    }
    return n ? (section_road_width(sec, net, cls) - fixed) / (float)n : 0.0f;
}

/*  The class a section's lanes are laid by.  A connecting road's lanes
 *  are laid by the class of the road it leaves, where it leaves it. */
static int lane_class(const NRoad *r, int sec)
{
    const NRoad *p;
    if (r->junction < 0 || r->pred.kind != NLINK_ROAD)
        return s_sec[sec].cls;
    p = &s_road[r->pred.index];
    if (p->junction >= 0)
        return 0;
    return s_sec[r->pred.contact ? p->sec0 + p->nsec - 1 : p->sec0].cls;
}

/*  A lane linked either way with `lane` in the section `sec`, by the
 *  links laid: the next one after `from` in the list, or -1. */
static int linked_in(int lane, int sec, int *from)
{
    for (; *from < s_nlink; ++*from)
    {
        const NLaneLink *k = &s_link[*from];
        int              o = k->from == lane ? k->to : k->to == lane ? k->from
                                                                     : -1;
        if (o >= 0 && s_lane[o].section == sec)
        {
            ++*from;
            return o;
        }
    }
    return -1;
}

/*  Each lane's width and each section's offset.  A road's sections are
 *  centered on its line.  A lane of a section after the first eases from
 *  the widths of the lanes it links with in the section before.  Each of
 *  those gives it a share, split among the lanes it links with here.  A
 *  connecting road's lanes ease from the width of the lane each links
 *  from to the width of the lane it links into.  They lie as far from
 *  its line as those lanes lie from their roads', easing the same way. */
static void lane_widths(void)
{
    int r, q, i, pass;
    /*  No lane eases at its end but by a ramp's taper (ramp_tapers). */
    for (i = 0; i < s_nlane; i++)
        s_lw[i].w2 = 0.0f, s_lw[i].ease_end = 0.0f;
    for (pass = 0; pass < 3; pass++)
        for (r = 0; r < s_nroad; r++)
        {
            NRoad *rd = &s_road[r];
            if ((pass == 0) != (rd->junction < 0))
                continue;
            for (q = 0; q < rd->nsec; q++)
            {
                int       sec = rd->sec0 + q, cls = lane_class(rd, sec);
                NSection *x    = &s_sec[sec];
                float     fill = fill_width(sec, rd->net, cls), left = 0.0f, right = 0.0f;
                for (i = 0; i < x->nlane; i++)
                {
                    NLane *l  = &s_lane[x->lane0 + i];
                    float  w  = fixed_width(rd->net, l->type);
                    LW(l)->w1 = w > 0.0f ? w : fill, LW(l)->w0 = LW(l)->w1, LW(l)->ease = 0.0f;
                }
                x->offset = x->offset1 = 0.0f, x->ease = 0.0f;
                if (rd->junction >= 0)
                {
                    /*  Each lane eases along the whole road, from its
                     *  share of the lanes it links from to its share of
                     *  the lanes it links into.  The offset eases from
                     *  where the first lane it links from lies to where
                     *  the lane it links into lies. */
                    float lo[2] = {1e9f, 1e9f}, hi[2] = {-1e9f, -1e9f};
                    x->ease = rd->length;
                    for (i = 0; i < x->nlane; i++)
                    {
                        NLane *l = &s_lane[x->lane0 + i];
                        int    k, e, started = 0;
                        /*  A lane of a fixed width keeps it.  A connecting
                         *  road's exit or entry lane is a lane of travel and
                         *  eases as one. */
                        if (fixed_width(rd->net, l->type) > 0.0f && !nlane_is_change(l->type))
                            continue;
                        for (e = 0; e < 2; e++)
                        {
                            float w   = 0.0f;
                            int   got = 0;
                            /*  At a node inside the junction, a turn or
                             *  a gore's lane takes the width and place
                             *  of the exit or entry lane there.  An
                             *  entry lane that goes on from an exit lane
                             *  takes the exit lane's, and eases to its
                             *  arm's.  An exit lane keeps its arm's to
                             *  its node. */
                            int at_node = (e ? rd->succ.kind : rd->pred.kind) == NLINK_JUNCTION && l->type != NLANE_EXIT;
                            for (k = 0; k < s_nlink; k++)
                            {
                                int   me = e ? s_link[k].from : s_link[k].to, o = e ? s_link[k].to : s_link[k].from, j, n = 0;
                                float t_in, t_out;
                                if (me != x->lane0 + i || s_lw[o].w1 <= 0.0f || s_lane[o].road == r)
                                    continue;
                                if (s_road[s_lane[o].road].junction == rd->junction &&
                                    !(at_node && (s_lane[o].type == NLANE_EXIT ||
                                                  (s_lane[o].type == NLANE_ENTRY && l->type != NLANE_ENTRY))))
                                    continue;
                                /*  Its share of that lane: split among the lanes of
                                 *  this road it links with. */
                                for (j = 0; j < s_nlink; j++)
                                {
                                    int mj = e ? s_link[j].from : s_link[j].to, oj = e ? s_link[j].to : s_link[j].from;
                                    n += oj == o && s_lane[mj].road == r;
                                }
                                w += work_width(o, e ? 0.0f : 1e9f) / (float)(n > 0 ? n : 1), got = 1;
                                if (traveled(l->type))
                                {
                                    /*  The span across of the lanes its
                                     *  lanes link with.  This road runs the
                                     *  way their traffic does: against their
                                     *  road's `s`, that road's `t` turns
                                     *  over. */
                                    float a, b;
                                    lane_t(o, e ? 0.0f : 1e9f, work_width, &t_in, &t_out);
                                    a = with_s(o) ? t_in : -t_in, b = with_s(o) ? t_out : -t_out;
                                    lo[e] = fminf(lo[e], fminf(a, b)), hi[e] = fmaxf(hi[e], fmaxf(a, b));
                                }
                            }
                            if (got && e)
                                LW(l)->w1 = w;
                            else if (got)
                                LW(l)->w0 = LW(l)->w1 = w;
                            if (!e)
                                started = got;
                        }
                        /*  A lane with nothing to read at its start keeps
                         *  the width of its end. */
                        if (!started)
                            LW(l)->w0 = LW(l)->w1;
                        LW(l)->ease = fabsf(LW(l)->w1 - LW(l)->w0) > 1e-6f ? rd->length : 0.0f;
                    }
                    /*  Its line runs along its lanes (layer 3), so lane
                     *  0 lies on its line.  A level meet's connecting
                     *  roads run along the road's own line across the
                     *  meet.  Their lanes lie where the lanes they link
                     *  with lie.  Lane 0 lies at the edge of those
                     *  lanes' span nearer the line, on the side its
                     *  lanes lie. */
                    if (topology_end(s_junc[rd->junction].end)->kind == END_MEET)
                    {
                        int on_left = x->nlane > 0 && s_lane[x->lane0].id > 0, end;
                        for (end = 0; end < 2; end++)
                            if (lo[end] <= hi[end])
                            {
                                float t = on_left ? lo[end] : hi[end];
                                if (end)
                                    x->offset1 = t;
                                else
                                    x->offset = x->offset1 = t;
                            }
                        if (lo[0] > hi[0] && lo[1] <= hi[1])
                            x->offset = x->offset1;
                    }
                    else
                        x->offset = x->offset1 = 0.0f, x->ease = 0.0f;
                    continue;
                }
                /*  A highway's exit, entry and auxiliary lanes lie
                 *  outside its lanes and move none of them: they are
                 *  left out of the balance.  A lane and a shoulder a
                 *  ramp has taken are counted where they were, so the
                 *  lanes that stay keep their places. */
                for (i = 0; i < x->nlane; i++)
                {
                    const NLane *l = &s_lane[x->lane0 + i];
                    if (rd->net == RNET_BAND && nlane_is_change(l->type))
                        continue;
                    if (l->id > 0)
                        left += LW(l)->w1;
                    else
                        right += LW(l)->w1;
                }
                if (rd->net == RNET_BAND && rd->topo >= 0 && s_drop)
                    left += (s_drop[rd->topo] & 1) ? highway_lane() + SHOULDER_WIDTH : 0.0f,
                        right += (s_drop[rd->topo] & 2) ? highway_lane() + SHOULDER_WIDTH : 0.0f;
                x->offset = x->offset1 = 0.5f * (right - left);
                if (q == 0)
                    continue;
                /*  The ease from the section before. */
                for (i = 0; i < x->nlane; i++)
                {
                    NLane *l  = &s_lane[x->lane0 + i];
                    float  w0 = 0.0f, len = (q + 1 < rd->nsec ? s_sec[sec + 1].s : rd->length) - x->s;
                    int    at = 0, p;
                    while ((p = linked_in(x->lane0 + i, sec - 1, &at)) >= 0)
                    {
                        int at2 = 0, n = 0;
                        while (linked_in(p, sec, &at2) >= 0)
                            n++;
                        w0 += s_lw[p].w1 / (float)(n > 0 ? n : 1);
                    }
                    /*  A lane of travel that links with no lane before
                     *  it, where a one-way street turns the other way,
                     *  starts at its own width.  A median or a shoulder
                     *  links with nothing.  It keeps its width where the
                     *  section before has one on its side, and else it
                     *  eases in from none. */
                    if (w0 <= 0.0f && traveled(l->type))
                        w0 = LW(l)->w1;
                    if (w0 <= 0.0f && !traveled(l->type))
                    {
                        int j;
                        for (j = 0; j < s_sec[sec - 1].nlane; j++)
                        {
                            const NLane *m = &s_lane[s_sec[sec - 1].lane0 + j];
                            if (m->type == l->type && (m->id > 0) == (l->id > 0))
                                w0 = LW(l)->w1;
                        }
                    }
                    if (fabsf(w0 - LW(l)->w1) > 1e-6f)
                        LW(l)->w0 = w0, LW(l)->ease = len < EASE ? len : EASE;
                }
            }
        }
}

/*  Where a ramp's lane comes back to the band, the band's outermost
 *  driving lane widens from nothing over EASE, fed by the lane beside
 *  it.  Where an on ramp's lane is to take its place, it narrows to
 *  nothing over EASE before the border.  It goes on into the lane beside
 *  it (plans/network.md, "The lane comes back"). */
static void back_tapers(void)
{
    int i, c, k;
    for (i = 0; i < s_nroad; i++)
        for (c = 0; c < 2 && s_road[i].topo >= 0; c++)
        {
            int bits = s_back[2 * s_road[i].topo + c], sec = c ? s_road[i].sec0 + s_road[i].nsec - 1 : s_road[i].sec0;
            for (k = 0; bits && k < s_sec[sec].nlane; k++)
            {
                NLane *l   = &s_lane[s_sec[sec].lane0 + k], *o;
                float  len = section_length(sec), e = len < EASE ? len : EASE;
                int    j, outer                     = 1;
                if (l->type != NLANE_DRIVING || !(bits & (l->id > 0 ? 1 : 2)))
                    continue;
                for (j = 0; j < s_sec[sec].nlane; j++)
                    o = &s_lane[s_sec[sec].lane0 + j], outer &= !(o->type == NLANE_DRIVING && (o->id > 0) == (l->id > 0) && abs(o->id) > abs(l->id));
                if (!outer)
                    continue;
                if (c)
                    LW(l)->w2 = 0.0f, LW(l)->ease_end = e;
                else
                    LW(l)->w0 = 0.0f, LW(l)->ease = e;
            }
        }
}

/*  A ramp that joins the band at a gore carries an offRamp or an onRamp
 *  lane (plans/network.md, "A ramp's road"). */
static void ramp_lane_types(void)
{
    int i, k;
    for (i = 0; i < s_nramp; i++)
    {
        const NRamp *rp = &s_ramp[i];
        if (rp->gore < 0 || !rp->built)
            continue;
        for (k = s_road[rp->road].sec0; k < s_road[rp->road].sec0 + s_road[rp->road].nsec; k++)
        {
            int j;
            for (j = 0; j < s_sec[k].nlane; j++)
                if (traveled(s_lane[s_sec[k].lane0 + j].type))
                    s_lane[s_sec[k].lane0 + j].type = (uint8_t)(rp->on ? NLANE_ONRAMP : NLANE_OFFRAMP);
        }
    }
}

int network_lane_with_s(int lane)
{
    return lane >= 0 && lane < s_nlane && with_s(lane);
}

int network_lane_in_graph(int lane)
{
    return lane >= 0 && lane < s_nlane && (traveled(s_lane[lane].type) || s_lane[lane].type == NLANE_SIDEWALK);
}

/*  How long a section runs along its road, in units. */
static float section_length(int sec)
{
    const NRoad *r = &s_road[s_lane[s_sec[sec].lane0].road];
    return (sec + 1 < r->sec0 + r->nsec ? s_sec[sec + 1].s : r->length) - s_sec[sec].s;
}

/*  A lane's working width `ds` units into its section, while the network
 *  is derived and before its width is filed. */
static float work_width(int lane, float ds)
{
    const LaneEase *l;
    float           f, len;
    if (lane < 0 || lane >= s_nlane)
        return 0.0f;
    l = &s_lw[lane];
    if (l->ease_end > 0.0f)
    {
        len = section_length(s_lane[lane].section);
        if (ds > len - l->ease_end)
        {
            f = ds < len ? (ds - (len - l->ease_end)) / l->ease_end : 1.0f;
            return l->w1 + (l->w2 - l->w1) * f * f * (3.0f - 2.0f * f);
        }
    }
    if (l->ease <= 0.0f || ds >= l->ease)
        return l->w1;
    f = ds > 0.0f ? ds / l->ease : 0.0f;
    return l->w0 + (l->w1 - l->w0) * f * f * (3.0f - 2.0f * f);
}

float network_lane_width(int lane, float ds)
{
    const NLane *l;
    int          k;
    float        len;
    if (lane < 0 || lane >= s_nlane || !s_lane[lane].nwidth)
        return 0.0f;
    l   = &s_lane[lane];
    len = section_length(l->section);
    ds  = ds < 0.0f ? 0.0f : ds > len ? len
                                      : ds;
    /*  The record a `ds` falls in: the last that starts at or before it. */
    for (k = l->nwidth - 1; k > 0 && s_width[l->width0 + k].s > ds; k--)
        ;
    {
        /*  A value within the sum's own round-off of nought is nought: an
         *  ease to nought ends there, not a hair either side of it. */
        const NWidth *w = &s_width[l->width0 + k];
        double        d = (double)ds - (double)w->s, v = w->a + d * (w->b + d * (w->c + d * w->d));
        double        bound = 8.0 * DBL_EPSILON * (fabs(w->a) + fabs(w->b * d) + fabs(w->c * d * d) + fabs(w->d * d * d));
        return fabs(v) <= bound ? 0.0f : (float)v;
    }
}

const NWidth *network_lane_widths(int lane, int *n)
{
    *n = lane >= 0 && lane < s_nlane ? s_lane[lane].nwidth : 0;
    return *n ? &s_width[s_lane[lane].width0] : NULL;
}

int network_lane_wide(int lane)
{
    int           n, k;
    const NWidth *w = network_lane_widths(lane, &n);
    for (k = 0; k < n; k++)
        if (w[k].a > 0.0)
            return 1;
    return n && network_lane_width(lane, section_length(s_lane[lane].section)) > 0.0f;
}

/*  A lane's full width: the width its first record eases to.  An ease
 *  a + c ds^2 + d ds^3 over `e` has e = -2c / 3d.  Its width at `e` is the
 *  full width, also where a later record starts before `e`. */
static float width_settled(int lane)
{
    int           n;
    double        e;
    const NWidth *w = network_lane_widths(lane, &n);
    if (!w)
        return 0.0f;
    if (w[0].d == 0.0)
        return (float)w[0].a;
    e = -2.0 * w[0].c / (3.0 * w[0].d);
    return (float)(w[0].a + w[0].c * e * e + w[0].d * e * e * e);
}

/*  THE WIDTHS FILED: each lane's working width as OpenDRIVE's records, a
 *  cubic in `ds` from each record's own `sOffset`.  An ease from w0 to w1
 *  over `e` has no slope at either end: a + c ds^2 + d ds^3, with a = w0,
 *  c = 3 (w1 - w0) / e^2 and d = -2 (w1 - w0) / e^3.  A lane files its
 *  ease from its section's start, the width it settles at, and its ease
 *  to its section's end.  Where the ease to the end starts before the
 *  first ease is over, it takes over from the width the lane has there. */
static int width_ease(int *at, float s, float from, float to, float e)
{
    NWidth *w;
    if (arc_grow((void **)&s_width, &s_cwidth, s_nwidth + 1, sizeof *s_width) != 0)
        return -1;
    w    = &s_width[s_nwidth++];
    w->s = s, w->a = from, w->b = 0.0;
    w->c = e > 0.0f ? 3.0 * ((double)to - from) / ((double)e * e) : 0.0;
    w->d = e > 0.0f ? -2.0 * ((double)to - from) / ((double)e * e * e) : 0.0;
    ++*at;
    return 0;
}

static int file_widths(void)
{
    int i;
    s_nwidth = 0;
    for (i = 0; i < s_nlane; i++)
    {
        const LaneEase *e   = &s_lw[i];
        float           len = section_length(s_lane[i].section), end = e->ease_end > 0.0f ? len - e->ease_end : 1e30f;
        int             n = 0;
        s_lane[i].width0  = s_nwidth;
        if (e->ease > 0.0f && end > 0.0f && width_ease(&n, 0.0f, e->w0, e->w1, e->ease) != 0)
            return -1;
        if ((e->ease <= 0.0f || e->ease < end) && width_ease(&n, e->ease > 0.0f ? e->ease : 0.0f, e->w1, e->w1, 0.0f) != 0)
            return -1;
        /*  The ease to the end starts from the width the lane has there, so
         *  a lane whose eases overlap runs on unbroken. */
        if (e->ease_end > 0.0f)
        {
            float from = e->w1;
            if (e->ease > 0.0f && end < e->ease)
            {
                float f = end > 0.0f ? end / e->ease : 0.0f;
                from    = e->w0 + (e->w1 - e->w0) * f * f * (3.0f - 2.0f * f);
            }
            if (width_ease(&n, end > 0.0f ? end : 0.0f, from, e->w2, e->ease_end) != 0)
                return -1;
        }
        s_lane[i].nwidth = n;
    }
    return 0;
}

/*  Where a lane's two borders lie across its road `ds` units into its
 *  section, with each lane's width read by `width`. */
static void lane_t(int lane, float ds, float (*width)(int lane, float ds), float *t_in, float *t_out)
{
    const NLane    *l;
    const NSection *x;
    float           side, t = 0.0f;
    int             i;
    *t_in = *t_out = 0.0f;
    if (lane < 0 || lane >= s_nlane)
        return;
    l    = &s_lane[lane];
    x    = &s_sec[l->section];
    side = l->id > 0 ? 1.0f : -1.0f;
    for (i = 0; i < x->nlane; i++)
    {
        const NLane *m = &s_lane[x->lane0 + i];
        if ((m->id > 0) == (l->id > 0) && abs(m->id) < abs(l->id))
            t += width(x->lane0 + i, ds);
    }
    {
        float off = x->offset, f;
        if (x->ease > 0.0f && ds < x->ease)
        {
            f   = ds > 0.0f ? ds / x->ease : 0.0f;
            off = x->offset + (x->offset1 - x->offset) * f * f * (3.0f - 2.0f * f);
        }
        else
            off = x->offset1;
        *t_in = off + side * t;
    }
    *t_out = *t_in + side * width(lane, ds);
}

void network_lane_t(int lane, float ds, float *t_in, float *t_out)
{
    lane_t(lane, ds, network_lane_width, t_in, t_out);
}

float network_width(int net, int cls)
{
    return road_width(net, cls);
}

float network_type_width(int net, int type)
{
    return fixed_width(net, type);
}

int network_section_at(int road, float s)
{
    const NRoad *r;
    int          q, sec;
    if (road < 0 || road >= s_nroad || s_road[road].nsec < 1)
        return -1;
    r   = &s_road[road];
    sec = r->sec0;
    for (q = 1; q < r->nsec; q++)
        if (s_sec[r->sec0 + q].s <= s)
            sec = r->sec0 + q;
    return sec;
}

float network_road_width(int road, float s)
{
    int sec = network_section_at(road, s);
    return sec < 0 ? 0.0f : network_section_width(sec, s - s_sec[sec].s);
}

float network_section_width(int sec, float ds)
{
    float w = 0.0f;
    int   i;
    if (sec < 0 || sec >= s_nsec)
        return 0.0f;
    for (i = 0; i < s_sec[sec].nlane; i++)
        w += network_lane_width(s_sec[sec].lane0 + i, ds);
    return w;
}

int network_lane_fault(int i)
{
    return s_fault && i >= 0 && i < s_nlane ? s_fault[i] : 0;
}

int network_roads(void)
{
    return s_nroad;
}

const NRoad *network_road(int i)
{
    return i >= 0 && i < s_nroad ? &s_road[i] : NULL;
}

const NSection *network_section(int i)
{
    return i >= 0 && i < s_nsec ? &s_sec[i] : NULL;
}

int network_lanes(void)
{
    return s_nlane;
}

const NLane *network_lane(int i)
{
    return i >= 0 && i < s_nlane ? &s_lane[i] : NULL;
}

int network_junctions(void)
{
    return s_njunc;
}

const NJunction *network_junction(int i)
{
    return i >= 0 && i < s_njunc ? &s_junc[i] : NULL;
}

const NConnection *network_connection(int i)
{
    return i >= 0 && i < s_nconn ? &s_conn[i] : NULL;
}

int network_ramps(void)
{
    return s_nramp;
}

const NRamp *network_ramp(int i)
{
    return i >= 0 && i < s_nramp ? &s_ramp[i] : NULL;
}

int network_side(void)
{
    return s_side;
}

int network_controls(void)
{
    return s_nctl;
}

const NControl *network_control(int i)
{
    return i >= 0 && i < s_nctl ? &s_ctl[i] : NULL;
}

int network_links(void)
{
    return s_nlink;
}

const NLaneLink *network_link(int i)
{
    return i >= 0 && i < s_nlink ? &s_link[i] : NULL;
}

/*  Whether junction j is a gore at the map's edge.  The band beyond it
 *  is off the map, so what reaches it leaves the map there. */
static int at_map_edge(int j)
{
    const TopoEnd *e = j >= 0 && j < s_njunc ? topology_end(s_junc[j].end) : NULL;
    return e && e->kind == END_GORE && e->ncell == 1;
}

/*  Whether a road's end is a STOP, where a lane needs no link.  `ahead`
 *  is 1 for a lane that runs to that end, and 0 for one that runs from
 *  it.  The map's edge is a stop.  A junction is a stop where no lane of
 *  the same way links on there.  Its traffic must also be unable to turn
 *  round, or have no lane the other way to turn into. */
static int lane_stop(const NRoad *r, int at_end, int ahead)
{
    const NLink    *l = at_end ? &r->succ : &r->pred;
    const NSection *x = &s_sec[at_end ? r->sec0 + r->nsec - 1 : r->sec0];
    int             j, with = 0, against = 0, way = 0;
    if (l->kind == NLINK_NONE || (l->kind == NLINK_JUNCTION && at_map_edge(l->index)))
        return 1;
    if (l->kind != NLINK_JUNCTION || r->junction >= 0)
        return 0;
    for (j = 0; j < x->nlane; j++)
    {
        int lane = x->lane0 + j, toward = with_s(lane) == (at_end != 0);
        if (!traveled(s_lane[lane].type))
            continue;
        with += with_s(lane), against += s_lane[lane].id * s_side > 0;
        /*  A lane of the same way that links on: the road is not
         *  stranded there. */
        if (toward == (ahead != 0) && (ahead ? goes_on(lane) : is_fed(lane)))
            way = 1;
    }
    return !way && (!turns_round(r->net) || !with || !against);
}

/*  A TERMINAL inside a road.  A lane of travel reaches a section boundary,
 *  `ahead` the way it runs or else behind it.  The section across carries
 *  no lane of travel its way.  That is where the street layer's one-way
 *  stretches meet head on, or part tail to tail.  Each stretch's lanes end
 *  there, at nodes of their own, and only the sidewalks go on across. */
static int lane_terminal(int lane, int ahead)
{
    const NLane *l   = &s_lane[lane];
    const NRoad *r   = &s_road[l->road];
    int          way = with_s(lane), sec = l->section + ((way == ahead) ? 1 : -1), j, any = 0;
    if (sec < r->sec0 || sec >= r->sec0 + r->nsec)
        return 0;
    for (j = 0; j < s_sec[sec].nlane; j++)
    {
        int k = s_sec[sec].lane0 + j;
        if (!is_travel(k))
            continue;
        any = 1;
        if (with_s(k) == way)
            return 0;
    }
    return any;
}

const char *network_lane_type_name(int type)
{
    static const char *const NAME[NLANE_TYPES] = {"driving", "entry", "exit", "sidewalk", "rail", "median", "border", "shoulder", "onRamp", "offRamp", "connectingRamp"};
    return type >= 0 && type < NLANE_TYPES ? NAME[type] : "?";
}

const char *network_junction_type_name(int type)
{
    return type == NJUNCTION_DIRECT ? "direct" : "default";
}

/*  What a road's end reaches, for the check to say, into `out`. */
static const char *lane_reach(const NRoad *r, int at_end, char out[32])
{
    const NLink *l = at_end ? &r->succ : &r->pred;
    int          kind;
    if (l->kind == NLINK_NONE)
        return "the map's edge";
    if (l->kind == NLINK_ROAD)
        return "a road";
    kind = topology_end(s_junc[l->index].end)->kind;
    if (kind == END_MAP_EDGE)
        return "the map's edge";
    snprintf(out, 32, "a %s", topology_end_name(kind));
    return out;
}

/*  Each lane's first two lanes linked with it, at either end, as
 *  nb[2 * lane] and nb[2 * lane + 1], -1 for none.  NULL where there is no
 *  room. */
static int *lane_neighbors(void)
{
    int *nb = (int *)malloc(sizeof(int) * 2 * ((size_t)s_nlane + 1)), i;
    if (!nb)
        return NULL;
    for (i = 0; i < 2 * s_nlane; i++)
        nb[i] = -1;
    for (i = 0; i < s_nlink; i++)
    {
        int pass;
        for (pass = 0; pass < 2; pass++)
        {
            int me = pass ? s_link[i].to : s_link[i].from, other = pass ? s_link[i].from : s_link[i].to;
            if (nb[2 * me] < 0 || nb[2 * me] == other)
                nb[2 * me] = other;
            else if (nb[2 * me + 1] < 0)
                nb[2 * me + 1] = other;
        }
    }
    return nb;
}

/*  Whether sidewalk lane `i` has an end linked to no lane.  A sidewalk is
 *  walked both ways, so it needs a lane at each of its ends.  That is two
 *  other lanes linked with it, less one for each end at the map's edge.  A
 *  corner between a loop road's two ends joins one lane at both its
 *  ends.  Two lanes that are each other's only neighbor close a ring. */
static int sidewalk_lone(int i, const int *nb)
{
    const NLane *l    = &s_lane[i];
    const NRoad *r    = &s_road[l->road];
    int          edge = (l->section == r->sec0 && r->pred.kind == NLINK_NONE) + (l->section == r->sec0 + r->nsec - 1 && r->succ.kind == NLINK_NONE);
    int          have = (nb[2 * i] >= 0) + (nb[2 * i + 1] >= 0);
    if (have == 1 && r->junction >= 0 && r->pred.index == r->succ.index)
        return 0;
    if (have == 1 && nb[2 * nb[2 * i]] == i && nb[2 * nb[2 * i] + 1] < 0)
        return 0;
    return have < 2 - edge;
}

/*  EVERY LANE GOES ON AND IS FED (plans/network.md, layer 2), held on the
 *  records before anything is fitted.  A lane of travel links on into a
 *  lane at the end it runs to.  A lane links into it at the end it comes
 *  from.  An end that is a STOP needs no link (lane_stop).  A sidewalk is
 *  walked both ways: it needs a lane at each of its ends that is no map
 *  edge.  With `say`, each fault is said to the dump by its road, its lane
 *  and what its road reaches there.  Answers how many lanes fail. */
int network_check(int say)
{
    int     *nb = lane_neighbors();
    int      i, no_on = 0, no_feed = 0, lone = 0, place = 0, said = 0;
    uint8_t *inside = (uint8_t *)calloc((size_t)s_nnode + 1, 1);
    free(s_fault);
    s_fault = (uint8_t *)calloc((size_t)s_nlane + 1, 1);
    if (!nb || !s_fault || !inside)
    {
        free(nb), free(inside);
        return -1;
    }
    for (i = 0; i < 2 * s_nlane; i++)
    {
        const NRoad *r = &s_road[s_lane[i / 2].road];
        /*  The end of a connecting road's lane at a node inside its
         *  junction. */
        if (r->junction >= 0 && ((i & 1) ? r->succ : r->pred).kind == NLINK_JUNCTION && s_lnode[i] >= 0)
            inside[s_lnode[i]] = 1;
    }
    for (i = 0; i < s_nlane; i++)
    {
        const NLane *l     = &s_lane[i];
        const NRoad *r     = &s_road[l->road];
        int          first = l->section == r->sec0, last = l->section == r->sec0 + r->nsec - 1;
        const char  *fault = NULL;
        int          at_end;
        if (!network_lane_in_graph(i))
            continue;
        /*  A NODE LIES IN ONE PLACE.  A road's contact is a node on the
         *  junction's border.  A node inside the junction joins that
         *  junction's connecting roads alone. */
        if (r->junction < 0 && ((first && inside[s_lnode[2 * i]]) || (last && inside[s_lnode[2 * i + 1]])))
            fault = "is one node at its road's end with a node inside the junction", place++, s_fault[i] = NFAULT_PLACE;
        else if (l->type == NLANE_SIDEWALK)
        {
            if (sidewalk_lone(i, nb))
                fault = "has an end linked to no lane", lone++, s_fault[i] = NFAULT_LONE;
        }
        else
        {
            /*  A lane of travel runs with `s` where its id and the side
             *  the world drives on have opposite signs. */
            at_end = with_s(i);
            /*  A lane that narrows to nought where it runs to merges by a
             *  lane change.  A lane that widens from nought where it comes
             *  from is fed by one (OpenDRIVE 1.8.1 section 11.5). */
            if (!goes_on(i) && !((at_end ? last : first) && lane_stop(r, at_end, 1)) && !lane_terminal(i, 1) &&
                network_lane_width(i, at_end ? 1e9f : 0.0f) > 1e-4f)
                fault = "goes on into nothing", no_on++, s_fault[i] = NFAULT_NO_ON;
            else if (!is_fed(i) && !((at_end ? first : last) && lane_stop(r, !at_end, 0)) && !lane_terminal(i, 0) &&
                     network_lane_width(i, at_end ? 0.0f : 1e9f) > 1e-4f)
                fault = "is fed by nothing", no_feed++, s_fault[i] = NFAULT_NO_FEED;
        }
        if (fault && say && said++ < 40)
        {
            char from[32], to[32];
            at_end = l->type == NLANE_SIDEWALK ? 1 : with_s(i);
            dumpf("network check  %s road %016llx lane %d (%s) %s, where its road reaches %s and %s\n",
                  rcity_net_name(r->net),
                  (unsigned long long)r->id,
                  l->id,
                  network_lane_type_name(l->type),
                  fault,
                  lane_reach(r, !at_end, from),
                  lane_reach(r, at_end, to));
        }
    }
    if (say)
        dumpf("network check  %d lanes: %d go on into nothing, %d are fed by nothing, %d sidewalks have an end linked to no lane, "
              "%d meet a node inside a junction at their road's end\n",
              s_nlane,
              no_on,
              no_feed,
              lone,
              place);
    free(nb), free(inside);
    /*  Each section of a road the topology found: its lanes, at their full
     *  widths, add up to its road's width.  An exit or an entry lane's full
     *  width is its own, whichever end of its taper it reaches. */
    {
        int wide = 0;
        for (i = 0; i < s_nsec; i++)
        {
            float w = 0.0f, want;
            int   j, road = s_lane[s_sec[i].lane0].road;
            if (s_sec[i].nlane < 1 || s_road[road].junction >= 0)
                continue;
            for (j = 0; j < s_sec[i].nlane; j++)
            {
                const NLane *l = &s_lane[s_sec[i].lane0 + j];
                w += s_road[road].net == RNET_BAND && nlane_is_change(l->type) ? fixed_width(RNET_BAND, l->type) : width_settled(s_sec[i].lane0 + j);
            }
            want = section_road_width(i, s_road[road].net, s_sec[i].cls);
            if (fabsf(w - want) > 1e-4f && ++wide <= 40 && say)
                dumpf("network check  section at s %.2f of road %016llx: its lanes add up to %.3f, not its road's width %.3f\n",
                      (double)s_sec[i].s,
                      (unsigned long long)s_road[road].id,
                      (double)w,
                      (double)want);
        }
        if (say)
            dumpf("network check  %d sections whose lanes do not add up to their road's width\n", wide);
        /*  Each connection filed under a junction is that junction's: its
         *  connecting road, where it has one, is a road of that junction. */
        {
            int j, k, alien = 0;
            for (j = 0; j < s_njunc; j++)
                for (k = s_junc[j].conn0; k < s_junc[j].conn0 + s_junc[j].nconn; k++)
                    if (k >= s_nconn || (s_conn[k].connecting >= 0 && s_road[s_conn[k].connecting].junction != j))
                    {
                        if (say && ++alien <= 40)
                            dumpf("network check  junction %016llx files a connection that is not its own\n", (unsigned long long)s_junc[j].id);
                        else if (!say)
                            ++alien;
                    }
            if (say)
                dumpf("network check  %d connections filed under a junction that is not theirs\n", alien);
            return no_on + no_feed + lone + place + wide + alien;
        }
    }
}

void network_dump(void)
{
    int i, j, direct = 0, connecting = 0;
    for (i = 0; i < s_njunc; i++)
        direct += s_junc[i].type == NJUNCTION_DIRECT;
    for (i = 0; i < s_nroad; i++)
        connecting += s_road[i].junction >= 0;
    dumpf("network  %d roads (%d connecting)  %d sections  %d lanes  %d junctions (%d direct)  %d connections  %d links\n",
          s_nroad,
          connecting,
          s_nsec,
          s_nlane,
          s_njunc,
          direct,
          s_nconn,
          s_nlink);
    {
        /*  The lane graph's nodes: where each lies, and how many merge
         *  two traffic lanes or more into one, or diverge one into more. */
        int at_contact = 0, at_section = 0, inside = 0, merge = 0, diverge = 0;
        for (i = 0; i < s_nnode; i++)
        {
            const NNode *n = &s_node[i];
            at_contact += n->road >= 0 && n->contact >= 0;
            at_section += n->road >= 0 && n->contact < 0;
            inside += n->road < 0;
            merge += n->nin > 1 && n->nout == 1;
            diverge += n->nin == 1 && n->nout > 1;
        }
        dumpf("lane graph  %d nodes: %d at a road's contact, %d at a section boundary, %d inside a junction; %d merges, %d diverges\n",
              s_nnode,
              at_contact,
              at_section,
              inside,
              merge,
              diverge);
    }
    {
        /*  The ramps, by which way their traffic runs. */
        int on = 0, off = 0, endon = 0, unbuilt = 0;
        for (i = 0; i < s_nramp; i++)
        {
            on += s_ramp[i].built && s_ramp[i].on;
            off += s_ramp[i].built && !s_ramp[i].on;
            endon += s_ramp[i].built && s_ramp[i].endon;
            unbuilt += !s_ramp[i].built;
        }
        dumpf("ramps  %d on, %d off (%d of them at a band's end), %d with no band road to join\n", on, off, endon, unbuilt);
        /*  Each ramp: its cell, its way, and where it joins the band. */
        for (i = 0; i < s_nramp; i++)
        {
            const NRamp *rp   = &s_ramp[i];
            int32_t      side = topology_side();
            dumpf("nramp  %d,%d %s %s\n", rp->cell % side, rp->cell / side, rp->on ? "on" : "off", !rp->built ? "unbuilt" : rp->endon   ? "end"
                                                                                                                        : rp->gore >= 0 ? "gore"
                                                                                                                                        : "junction");
        }
    }
    {
        /*  Where the traffic lanes stop: a lane with no lane after it, or
         *  none before it, counted by what its road reaches there. */
        static const char *const AT[3]      = {"the map's edge", "a road", "a junction"};
        int                      none[2][3] = {{0}}, kind[END_KINDS][4] = {{0}};
        {
            for (i = 0; i < s_nlane; i++)
            {
                const NLane *l = &s_lane[i];
                const NRoad *r = &s_road[l->road];
                if (!traveled(l->type))
                    continue;
                /*  A lane that travels with `s` leaves by the road's end
                 *  and is fed at its start. */
                if (!goes_on(i))
                {
                    const NLink *at = with_s(i) ? &r->succ : &r->pred;
                    none[0][at->kind]++;
                    if (at->kind == NLINK_JUNCTION)
                        kind[topology_end(s_junc[at->index].end)->kind][r->net < 4 ? r->net : 0]++;
                }
                if (!is_fed(i))
                    none[1][(with_s(i) ? r->pred : r->succ).kind]++;
            }
            for (j = 0; j < 3; j++)
                dumpf("network  traffic lanes at %s: %d go on into nothing, %d are fed by nothing\n", AT[j], none[0][j], none[1][j]);
            {
                /*  The sidewalks with an end linked to no lane
                 *  (sidewalk_lone), on a road or at a junction. */
                int  lone[3] = {0}, joined = 0, ctl[4] = {0};
                int *nb = lane_neighbors();
                if (nb)
                {
                    for (i = 0; i < s_nlane; i++)
                    {
                        const NRoad *r = &s_road[s_lane[i].road];
                        if (s_lane[i].type != NLANE_SIDEWALK)
                            continue;
                        joined++;
                        if (sidewalk_lone(i, nb))
                            lone[r->junction >= 0 ? 2 : r->pred.kind == NLINK_JUNCTION || r->succ.kind == NLINK_JUNCTION ? 2
                                                                                                                         : 1]++;
                    }
                    free(nb);
                }
                for (i = 0; i < s_nctl; i++)
                    if (s_ctl[i].control < 4)
                        ctl[s_ctl[i].control]++;
                dumpf("network  %d sidewalk lanes: %d on a road and %d at a junction with an end linked to nothing\n", joined, lone[1], lone[2]);
                dumpf("network  controls: %d stops, %d signals, %d road arms yielding to a railway\n", ctl[NCONTROL_STOP], ctl[NCONTROL_SIGNAL], ctl[NCONTROL_RAILWAY]);
            }
            {
                int ek, nk;
                for (ek = 0; ek < END_KINDS; ek++)
                    for (nk = 0; nk < 4; nk++)
                        if (kind[ek][nk])
                            dumpf("network  traffic lanes of the %s network going on into nothing at a %s: %d\n", rcity_net_name(nk), topology_end_name(ek), kind[ek][nk]);
            }
            dumpf("network  ramps: %d built, %d with no band road to join, %d with no road\n",
                  s_ramps_built,
                  s_ramps_no_band,
                  s_ramps_no_road);
            dumpf("network  %d roads two ramps want one way each, in opposite ways: left two-way\n", s_oneway_clash);
            dumpf("network  %d street runs, %d places where one-way stretches meet head on or part tail to tail\n",
                  s_nrun,
                  s_street_clash);
        }
    }
    for (i = 0; i < s_njunc; i++)
        dumpf("junction %016llx  %s  %d connections\n", (unsigned long long)s_junc[i].id, network_junction_type_name(s_junc[i].type), s_junc[i].nconn);
    for (i = 0; i < s_nroad; i++)
    {
        const NRoad *r = &s_road[i];
        int          q;
        dumpf("nroad   %016llx  length %.2f  pred %d:%d  succ %d:%d", (unsigned long long)r->id, (double)r->length, r->pred.kind, r->pred.index, r->succ.kind, r->succ.index);
        for (q = 0; q < r->nsec; q++)
        {
            const NSection *x = &s_sec[r->sec0 + q];
            /*  Each lane's width records, each by the width it starts at,
             *  with `~` where it eases on from there.  And the section's
             *  offset, and where it eases to. */
            dumpf("  | s %.2f class %d offset %.3f", (double)x->s, x->cls, (double)x->offset);
            if (x->ease > 0.0f && x->offset1 != x->offset)
                dumpf(">%.3f", (double)x->offset1);
            dumpf(":");
            for (j = 0; j < x->nlane; j++)
            {
                const NLane *l = &s_lane[x->lane0 + j];
                dumpf(" %d:%s=", l->id, network_lane_type_name(l->type));
                {
                    int           n, k;
                    const NWidth *w = network_lane_widths(x->lane0 + j, &n);
                    for (k = 0; k < n; k++)
                        dumpf("%s%.3f%s", k ? ">" : "", w[k].a, w[k].c != 0.0 || w[k].d != 0.0 ? "~" : "");
                }
            }
        }
        dumpf("\n");
    }
}

/* ---- the state ----------------------------------------------------------- */

/*  Every variable of this file, as the store saves and loads it whole
 *  (store.h).  The release frees everything the variables own. */
/* clang-format off */
#define NETWORK_STATE(X) \
    X(s_road) X(s_sec) X(s_lane) X(s_junc) X(s_conn) X(s_link) X(s_nroad) X(s_croad) X(s_nsec) \
    X(s_csec) X(s_nlane) X(s_clane) X(s_njunc) X(s_cjunc) X(s_nconn) X(s_cconn) X(s_nlink) \
    X(s_clink) X(s_lw) X(s_clw) X(s_width) X(s_nwidth) X(s_cwidth) X(s_junc_of_end) X(s_at0) X(s_at) X(s_fault) X(s_oneway) X(s_aux) X(s_drop) X(s_back) \
    X(s_cell_region) X(s_band_lanes) X(s_side) X(s_drive_left) X(s_city) X(s_ctl) X(s_nctl) X(s_obj) X(s_nobj) X(s_cobj) \
    X(s_cctl) X(s_oneway_clash) X(s_ramps_built) X(s_ramps_no_band) X(s_ramps_no_road) X(s_run) \
    X(s_nrun) X(s_crun) X(s_street_clash) X(s_lout) X(s_lin) X(s_lcap) X(s_conn_junc) X(s_conn_jcap) \
    X(s_ramp) X(s_nramp) X(s_cramp) X(s_rn) X(s_nrn) X(s_crn) X(s_ch_road) X(s_ch_contact) \
    X(s_ch_leaving) X(s_ch_node) X(s_ch_part) X(s_ch_n) X(s_ch_next) X(s_jch_road) X(s_jch_contact) \
    X(s_jch_leaving) X(s_jch_part) X(s_jch_rank) X(s_jch_order) X(s_cjch) X(s_rg_taper) \
    X(s_nrg_taper) X(s_crg_taper) X(s_node) X(s_nnode) X(s_cnode) X(s_lnode) X(s_efirst) X(s_enext) \
    X(s_ecap_lane) X(s_ecap_link)
/* clang-format on */
NET_STATE(network, NETWORK_STATE)

void network_release(void)
{
    void **own[] = {(void **)&s_road, (void **)&s_sec, (void **)&s_lane, (void **)&s_junc, (void **)&s_conn, (void **)&s_link, (void **)&s_junc_of_end, (void **)&s_at0, (void **)&s_at, (void **)&s_fault, (void **)&s_oneway, (void **)&s_aux, (void **)&s_drop, (void **)&s_back, (void **)&s_cell_region, (void **)&s_ctl, (void **)&s_obj, (void **)&s_run, (void **)&s_lout, (void **)&s_lin, (void **)&s_conn_junc, (void **)&s_ramp, (void **)&s_rn, (void **)&s_jch_road, (void **)&s_jch_contact, (void **)&s_jch_leaving, (void **)&s_jch_part, (void **)&s_jch_rank, (void **)&s_jch_order, (void **)&s_rg_taper, (void **)&s_node, (void **)&s_lnode, (void **)&s_efirst, (void **)&s_enext};
    size_t k;
    for (k = 0; k < sizeof own / sizeof own[0]; ++k)
        free(*own[k]), *own[k] = NULL;
}
