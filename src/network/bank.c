/*  network/bank.c: the ground drawn beside a street (plans/terrain.md, "The
 *  ground beside a road").
 *
 *  Each street is sampled along its line every SAMPLE tiles.  The ground on
 *  both sides of a street is graded to meet it.  A point's drawn ground is
 *  read from the nearest sample within reach, found through a list of
 *  samples per tile.
 *
 *  Every number the bank is tuned by is in the block below. */
#include "network/bank.h"
#include "grow.h"
#include "network/state.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "dump.h"
#include "log.h"
#include "network/network.h"
#include "network/shape.h"
#include "network/topology.h"

/* ---- the knobs ----------------------------------------------------------- */

/*  How far past a street's edge the ground is graded to meet it, in
 *  tiles. */
#define BANK_REACH 0.25
/*  The most a graded bank may fall or rise over the reach, in levels.  It is
 *  a slope of one to one, since a level is half a tile.  Steeper, a wall
 *  stands at the street's edge instead. */
#define BANK_STEEPEST 0.5
/*  The ground within this many tiles of a street's end at a junction is the
 *  junction's. */
#define BANK_END 0.5
/*  How far past the reach a street is still found, in tiles.  It gives the
 *  drawn ground room to start its grade a little past the street's edge. */
#define BANK_FIND 0.25
/*  The widest street's half width, in tiles: how far a sample's reach can
 *  run past its line (network/shape.h, shape_width). */
#define MOST_HALF 0.45
/*  The spacing of the samples along a street, in tiles. */
#define SAMPLE 0.05
/*  A street and its land closer than this, in levels, need no bank. */
#define SAME 0.02
/*  The foot of a bank, in tiles from a street's line: just inside the end
 *  of the reach.  The land can step at a tile edge, and the reach can end
 *  on one.  The foot reads the land on the street's side of that step. */
#define FOOT_PAST (BANK_REACH - 1e-4)

/* ---- the store ----------------------------------------------------------- */

typedef struct
{
    int     road;
    float   s, x, y, hdg, z;
    float   half;    /* the street's half width there */
    uint8_t end;     /* 1 within BANK_END of the street's end at a junction */
    uint8_t wall[2]; /* per side, right then left: 1 where a wall stands at the edge */
} Sample;

static Sample      *s_sample;
static int          s_nsample, s_capsample;
static int         *s_head, *s_next; /* per tile, its first link, and per link, the next */
static int         *s_link;          /* the sample of each link */
static int          s_nlink, s_caplink;
static int32_t      s_side;
static const RCity *s_city;

static void bank_free(void)
{
    free(s_sample), free(s_head), free(s_next), free(s_link);
    s_sample = NULL, s_head = NULL, s_next = NULL, s_link = NULL;
    s_nsample = s_capsample = s_nlink = s_caplink = 0;
}

/*  Whether a street's start (contact 0) or end (1) is at a junction. */
static int at_junction(const NRoad *nr, int contact)
{
    return (contact ? nr->succ : nr->pred).kind == NLINK_JUNCTION;
}

/*  Whether a wall stands on one side of a street at a point of its line.
 *  Side 1 is left and -1 right, z is the street's height, and half its half
 *  width. It does where the land at the end of the reach is more than a slope
 *  of one to one from the street.  The land at the street's edge goes to
 *  `land`, unless it is NULL. */
static int bank_wall_at(double x, double y, double hdg, int side, double z, double half, double *land)
{
    double nx = -sin(hdg) * side, ny = cos(hdg) * side;
    double far = shape_land_at(x + nx * (half + FOOT_PAST), y + ny * (half + FOOT_PAST));
    if (land)
        *land = shape_land_at(x + nx * half, y + ny * half);
    return fabs(far - z) > BANK_STEEPEST;
}

int bank_build(const RCity *c)
{
    int    i, k;
    double reach = MOST_HALF + BANK_REACH + BANK_FIND;
    bank_free();
    s_city = c;
    s_side = topology_side();
    for (i = 0; i < network_roads(); i++)
    {
        const NRoad *nr = network_road(i);
        const SRoad *sr = shape_road(i);
        /*  A street's and a railway's line.  A junction's own roads lie
         *  inside its fill, which meets the ground itself.  A level meet has
         *  no fill: the road goes straight on across it. */
        if ((nr->net != RNET_ROAD && nr->net != RNET_RAIL) || !sr->ngeom ||
            (nr->junction >= 0 && topology_end(network_junction(nr->junction)->end)->kind != END_MEET))
            continue;
        int n = (int)(sr->length / SAMPLE) + 1;
        for (k = 0; k <= n; k++)
        {
            float s = sr->length * (float)k / (float)(n > 0 ? n : 1), hdg;
            if (arc_grow((void **)&s_sample, &s_capsample, s_nsample + 1, sizeof *s_sample) != 0)
                return -1;
            Sample *p = &s_sample[s_nsample];
            p->road   = i;
            p->s      = s;
            shape_at(i, s, &p->x, &p->y, &hdg);
            p->hdg     = hdg;
            p->z       = shape_height(i, s);
            p->half    = shape_width(i, s) * 0.5f;
            p->wall[0] = (uint8_t)bank_wall_at(p->x, p->y, hdg, -1, p->z, p->half, NULL);
            p->wall[1] = (uint8_t)bank_wall_at(p->x, p->y, hdg, 1, p->z, p->half, NULL);
            p->end     = (at_junction(nr, 0) && s < BANK_END) || (at_junction(nr, 1) && s > sr->length - BANK_END);
            s_nsample++;
        }
    }
    s_head = (int *)malloc(sizeof *s_head * (size_t)s_side * (size_t)s_side);
    if (!s_head)
        return -1;
    for (i = 0; i < s_side * s_side; i++)
        s_head[i] = -1;
    /*  Each sample is listed on every tile its reach touches. */
    for (i = 0; i < s_nsample; i++)
    {
        int32_t c0 = (int32_t)floor(s_sample[i].x - reach), c1 = (int32_t)floor(s_sample[i].x + reach);
        int32_t r0 = (int32_t)floor(s_sample[i].y - reach), r1 = (int32_t)floor(s_sample[i].y + reach);
        int32_t cc, rr;
        for (rr = r0; rr <= r1; rr++)
            for (cc = c0; cc <= c1; cc++)
            {
                if (cc < 0 || rr < 0 || cc >= s_side || rr >= s_side)
                    continue;
                if (arc_grow((void **)&s_link, &s_caplink, s_nlink + 1, sizeof *s_link) != 0)
                    return -1;
                s_next = (int *)realloc(s_next, sizeof *s_next * (size_t)s_caplink);
                if (!s_next)
                    return -1;
                s_link[s_nlink]          = i;
                s_next[s_nlink]          = s_head[rr * s_side + cc];
                s_head[rr * s_side + cc] = s_nlink++;
            }
    }
    return 0;
}

/*  The nearest sample to a point within a street's reach, or -1, with its
 *  distance in d. */
static int nearest(double x, double y, double *d)
{
    int32_t c = (int32_t)floor(x), r = (int32_t)floor(y);
    int     best  = -1, l;
    double  reach = MOST_HALF + BANK_REACH + BANK_FIND;
    *d            = 1e18;
    if (c < 0 || r < 0 || c >= s_side || r >= s_side || !s_head)
        return -1;
    for (l = s_head[r * s_side + c]; l >= 0; l = s_next[l])
    {
        const Sample *p = &s_sample[s_link[l]];
        double        e = hypot(x - p->x, y - p->y);
        if (e < *d)
            *d = e, best = s_link[l];
    }
    return *d <= reach ? best : -1;
}

/*  Which side of a sample's street a point lies on: 1 left of its line,
 *  0 right. */
static int side_of(const Sample *p, double x, double y)
{
    return (-sin(p->hdg) * (x - p->x) + cos(p->hdg) * (y - p->y)) >= 0 ? 1 : 0;
}

static void bank_each(double x, double y, void (*fn)(void *ud, double street, double d, int wall), void *ud)
{
    enum
    {
        MOST = 16
    };
    int32_t c = (int32_t)floor(x), r = (int32_t)floor(y);
    int     road[MOST], best[MOST], n = 0, l, k;
    double  dist[MOST], reach = MOST_HALF + BANK_REACH + BANK_FIND;
    if (c < 0 || r < 0 || c >= s_side || r >= s_side || !s_head)
        return;
    /*  The nearest sample of each street within reach. */
    for (l = s_head[r * s_side + c]; l >= 0; l = s_next[l])
    {
        const Sample *p = &s_sample[s_link[l]];
        double        e = hypot(x - p->x, y - p->y);
        if (e > reach)
            continue;
        for (k = 0; k < n && road[k] != p->road; ++k)
            ;
        if (k == n)
        {
            if (n == MOST)
                continue;
            road[n] = p->road, dist[n] = 1e18, ++n;
        }
        if (e < dist[k])
            dist[k] = e, best[k] = s_link[l];
    }
    for (k = 0; k < n; ++k)
    {
        const Sample *p = &s_sample[best[k]];
        fn(ud, (double)p->z, dist[k] - p->half, p->wall[side_of(p, x, y)]);
    }
}

double bank_grade(double z, double e, double ground, double margin, double sink)
{
    /*  In single precision, as the drawn ground is. */
    const float zf = (float)z, ef = (float)e, gf = (float)ground, mf = (float)margin, sf = (float)sink, reach = (float)BANK_REACH;
    if (ef <= mf)
        return zf - sf;
    if (ef < mf + reach)
    {
        float f = (ef - mf) / reach;
        f       = f * f * (3.0f - 2.0f * f);
        return zf - sf + (gf - (zf - sf)) * f;
    }
    return gf;
}

/*  The streets' grades at a point, the lowest kept. */
typedef struct
{
    double ground, out, margin, sink;
} Grade;

static void grade_street(void *ud, double street, double d, int wall)
{
    Grade *g = (Grade *)ud;
    double z;
    /*  Behind a wall the ground keeps its own height past the margin: it
     *  steps there in one part of the grid, and is not graded. */
    if (wall && d > g->margin)
        return;
    z      = bank_grade(street, d, g->ground, g->margin, g->sink);
    g->out = z < g->out ? z : g->out;
}

double bank_ground(double x, double y, double ground, double margin, double sink)
{
    Grade g;
    g.ground = g.out = ground, g.margin = margin, g.sink = sink;
    bank_each(x, y, grade_street, &g);
    return g.out;
}

/*  One street's own grade at a point, or the ground where it does not
 *  reach. */
static double own_grade(const Sample *p, double x, double y, double ground, double margin, double sink)
{
    double e = hypot(x - p->x, y - p->y) - p->half;
    if (p->wall[side_of(p, x, y)] && e > margin)
        return ground;
    return bank_grade(p->z, e, ground, margin, sink);
}

double bank_width(void)
{
    return 2.0 * MOST_HALF;
}

double bank_reach(void)
{
    return BANK_REACH;
}

/*  The rule, on the ground the streets leave (bank_ground), on every
 *  sample of every street but its ends at a junction, on each side where
 *  this street is the nearest:
 *   - at the street's edge it is the street's own grade there;
 *   - at the end of the reach it is the land's own height, where no
 *     other bank lies lower there.
 *  The count of faults. */
int bank_check(int say, double margin, double sink)
{
    int i, side, faults = 0, graded = 0, same = 0, walls = 0;
    for (i = 0; i < s_nsample; i++)
    {
        const Sample *p = &s_sample[i];
        if (p->end)
            continue;
        for (side = 0; side < 2; side++)
        {
            int    sd = side ? 1 : -1;
            double nx = -sin(p->hdg) * sd, ny = cos(p->hdg) * sd, d;
            double ex = p->x + nx * (p->half + 1e-4), ey = p->y + ny * (p->half + 1e-4);
            double fx = p->x + nx * (p->half + margin + FOOT_PAST), fy = p->y + ny * (p->half + margin + FOOT_PAST);
            double land_e = shape_land_at(ex, ey), land_f = shape_land_at(fx, fy);
            if (p->wall[side])
            {
                walls++;
                continue;
            }
            if (fabs(land_e - p->z) <= SAME)
            {
                same++;
                continue;
            }
            graded++;
            if (nearest(ex, ey, &d) != i)
                continue; /* another street is nearer there */
            const double z_edge = bank_ground(ex, ey, land_e, margin, sink), z_foot = bank_ground(fx, fy, land_f, margin, sink);
            const char  *why = NULL;
            if (fabs(z_edge - own_grade(p, ex, ey, land_e, margin, sink)) > 0.01)
                why = fabs(z_edge - land_e) <= 0.01 ? "stays at the land under a street above it" : "is cut under the street's edge by another bank";
            else if (z_foot >= own_grade(p, fx, fy, land_f, margin, sink) - 0.01 && fabs(z_foot - land_f) > 0.01)
                why = "does not meet the land at the end of the reach";
            if (why)
            {
                faults++;
                if (say)
                    dumpf("bank fault: the ground beside road %016llx at s %.2f, tile %d,%d, %s (street %.2f, "
                          "edge %.2f, foot %.2f, land %.2f)\n",
                          (unsigned long long)network_road(p->road)->id,
                          (double)p->s,
                          (int)floor(p->x),
                          (int)floor(p->y),
                          why,
                          (double)p->z,
                          z_edge,
                          z_foot,
                          land_f);
            }
        }
    }
    if (say)
    {
        /*  How far the streets stand off their own land along their
         *  lines, in levels: what the ground beside them is moved by. */
        double *off = (double *)malloc(sizeof *off * (size_t)(s_nsample + 1));
        int     n   = 0;
        for (i = 0; off && i < s_nsample; i++)
            if (!s_sample[i].end)
                off[n++] = fabs((double)s_sample[i].z - shape_land_at(s_sample[i].x, s_sample[i].y));
        if (off && n)
        {
            int a, b;
            for (a = 1; a < n; a++)
                for (b = a; b > 0 && off[b] < off[b - 1]; b--)
                {
                    double t = off[b];
                    off[b] = off[b - 1], off[b - 1] = t;
                }
            dumpf("bank: a street off its land by %.2f levels at the median, %.2f at the 95th centile, %.2f at most, "
                  "over %d samples\n",
                  off[n / 2],
                  off[(int)(n * 0.95)],
                  off[n - 1],
                  n);
        }
        free(off);
        dumpf("bank: %d edge samples; %d need nothing, %d graded, %d walls; %d faults\n", 2 * s_nsample, same, graded, walls, faults);
    }
    return faults;
}

/* ---- the state ----------------------------------------------------------- */

/*  Every variable of this file, as the store saves and loads it whole
 *  (store.h).  The release frees everything the variables own. */
/* clang-format off */
#define BANK_STATE(X) \
    X(s_sample) X(s_nsample) X(s_capsample) X(s_head) X(s_next) X(s_link) X(s_nlink) X(s_caplink) \
    X(s_side) X(s_city)
/* clang-format on */
NET_STATE(bank, BANK_STATE)

void bank_release(void)
{
    bank_free();
}
