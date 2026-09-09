/*  The highway family: bands of two tiles walked as their own network,
 *  the free-air corridor a deck flies through, the deck's works and lane
 *  drop in the loft, and the ramps. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "dump.h"
#include "mesh/internal.h"
#include "net/internal.h"
#include "net/model.h"
#include "opt.h"

/*  The lane drop (spec 7.3): what a ramp took from the deck at each
 *  station.  The table is by tile and map side; a station's right and
 *  left are read off its heading.  Within a tile the taper's progress
 *  is the station's distance from the tile's edge nearest the ramp,
 *  along the taper's direction, so a gore narrows linearly across its
 *  tile and the strip comes down in a straight line over the descent,
 *  from the deck's height at the gore to the ramp tile's ground. */
/*  The descent's profile as a fraction of the drop, 0 at the road's
 *  edge to 1 at the gore: a smoothstep, level at both ends. */
float hiway_lane_ease(float f)
{
    if (f < 0.0f)
        f = 0.0f;
    if (f > 1.0f)
        f = 1.0f;
    return f * f * (3.0f - 2.0f * f);
}

/*  The lane drop (spec 7.3) at each station of a deck: for every ramp
 *  whose point on the centreline lies on this band, the deck narrows on
 *  the ramp's side by ARC LENGTH from that point -- through curve blocks
 *  as easily as along a straight -- over the taper: the deck tile itself
 *  and the descent, then the gore, the taper's last tile, where the slab
 *  widens back and the lane's own sliver sits level beside it.  Toward
 *  the road the deck stays two lanes as far as a partner ramp's point
 *  within seven tiles, else widens back over one.  A ramp beside a curve
 *  drops its lane from the curve like any other (Atlanta 70,88). */
/*  Station i of the deck: how far along it is, where it stands and which
 *  way it heads.  Ramp r: the point on the centreline it drops from, its
 *  own tile's centre, the way it runs, how long its taper is, and whether
 *  it leaves the deck or joins it. */
int hiway_drop_station(const DropFan *d, int i, float *at, V2 *pos, V2 *dir)
{
    const Sample *smp = (const Sample *)d->smp;
    if (i < 0 || i >= d->n)
        return 0;
    *at  = smp[i].s;
    *pos = smp[i].pos;
    *dir = smp[i].dir;
    return 1;
}

int hiway_drop_ramp(const DropFan *d, int r, V2 *c0, V2 *tile, V2 *along, int *len, int *off)
{
    if (r < 0 || r >= d->nramps)
        return 0;
    *c0    = s_hw_ramps[r].c0;
    *tile  = (V2){(float)s_hw_ramps[r].rc + 0.5f, (float)s_hw_ramps[r].rr + 0.5f};
    *along = s_hw_ramps[r].along;
    *len   = s_hw_ramps[r].len;
    *off   = s_hw_ramps[r].off;
    return 1;
}

/*  Every station its full width, before any ramp takes a lane. */
void hiway_drop_clear(DropFan *d)
{
    Sample *smp = (Sample *)d->smp;
    int     i;
    for (i = 0; i < d->n; ++i)
    {
        smp[i].wl = smp[i].wr = 1.0f;
        smp[i].lane           = 0;
        smp[i].zr[0] = smp[i].zr[1] = smp[i].z;
    }
}

/*  The width a station is left with on one side: the narrowest any ramp
 *  asks for. */
void hiway_drop_width(DropFan *d, int i, int side, float w)
{
    Sample *smp = (Sample *)d->smp;
    if (i < 0 || i >= d->n)
        return;
    if (side)
        smp[i].wl = w < smp[i].wl ? w : smp[i].wl;
    else
        smp[i].wr = w < smp[i].wr ? w : smp[i].wr;
}

/*  The gore: where the ramp's own sliver of lane sits beside the deck. */
void hiway_drop_gore(DropFan *d, int i, int side)
{
    Sample *smp = (Sample *)d->smp;
    if (i < 0 || i >= d->n)
        return;
    smp[i].lane |= side ? 2 : 1;
    smp[i].zr[side] = smp[i].z;
}

/*  The lane drop at each station of a deck: arc.rules.lane_drop works it
 *  out. */
static void hiway_lane_stations(Sample *smp, int ns)
{
    DropFan d;
    memset(&d, 0, sizeof d);
    d.smp    = smp;
    d.n      = ns;
    d.nramps = s_hw_nramps;
    d.reach  = s_tune.hiway_reach;
    d.narrow = HIWAY_LANE_IN;
    script_rule_object("lane_drop", "drop", &d);
}

/*  A deck's edge: the girder's fascia down from the carriageway, in the
 *  deck's own shadow, and the parapet up from it with two faces and a
 *  top (one quad showed the viewer its back along the far edge).  The
 *  ramp lane's edges are the same, shallower. */
int deck_edge(RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float ea[2], const float eb[2], float za, float zb, const float nrm[3], float girder, int parapet)
{
    static const float conc[3]  = {1.0f, 0.0f, MAT_PIER}; /* cast concrete, plain */
    static const float shade[3] = {1.0f, 1.0f, MAT_PIER};
    const float        pw       = 0.035f; /* the parapet's thickness */
    float              ia[2]    = {ea[0] - nrm[0] * pw, ea[1] - nrm[1] * pw};
    float              ib[2]    = {eb[0] - nrm[0] * pw, eb[1] - nrm[1] * pw};
    float              inn[3] = {-nrm[0], -nrm[1], 0.0f}, up[3] = {0.0f, 0.0f, 1.0f};
    float              t0[3] = {ea[0], ea[1], za}, t1[3] = {eb[0], eb[1], zb};
    float              g0[3] = {ea[0], ea[1], za - girder}, g1[3] = {eb[0], eb[1], zb - girder};
    float              p0[3] = {ea[0], ea[1], za + HIWAY_PARAPET}, p1[3] = {eb[0], eb[1], zb + HIWAY_PARAPET};
    float              k0[3] = {ia[0], ia[1], za + HIWAY_PARAPET}, k1[3] = {ib[0], ib[1], zb + HIWAY_PARAPET};
    float              n0[3] = {ia[0], ia[1], za}, n1[3] = {ib[0], ib[1], zb};
    (void)c;
    (void)mask_bit;
    if (put_wall(m, t0, t1, g0, g1, nrm, order, shade) != 0)
        return -1;
    if (!parapet)
        return 0;
    if (put_wall(m, p0, p1, t0, t1, nrm, order, conc) != 0)
        return -1;
    if (put_wall(m, k0, k1, n0, n1, inn, order, conc) != 0)
        return -1;
    return put_wall(m, p0, p1, k0, k1, up, order, conc);
}

/*  One loft's working state, handed to the stages below so each can be
 *  read on its own: the strip as asked for (s_ld), its stations and
 *  their raw ground, and the widths and ends the callers gave. */

/* the segment table's sample cache, below with the table */

/*  A ramp's taper: over the last `taper` of the strip (or the first, for an
 *  ON ramp travelled from the road) the width narrows from the deck lane's
 *  to the road lane's, so the ramp joins the road as a lane of that road
 *  and not a strip lying across it.  The stations' width fractions are what
 *  the slab draws by. */
static void hiway_taper(Loft *x)
{
    Sample *smp = x->smp;
    int     i;
    float   f1;
    if (!(x->d->taper > 0.0f) || x->hw <= 0.0f)
        return;
    f1 = x->d->hw_end / x->hw;
    for (i = 0; i < x->ns; ++i)
    {
        float from_end = x->d->taper_start ? smp[i].s : x->total - smp[i].s;
        float t        = from_end / x->d->taper; /* 0 at the road end, 1 a taper's length up the ramp */
        float f;
        if (t < 0.0f)
            t = 0.0f;
        if (t > 1.0f)
            t = 1.0f;
        f = f1 + (1.0f - f1) * t;
        if (f < smp[i].wr)
            smp[i].wr = f;
        if (f < smp[i].wl)
            smp[i].wl = f;
    }
}

/*  A deck's works beside the slab: its piers and what stands with them.
 *  The family's works hook is EMPTY.  The bents' caps and columns every
 *  HIWAY_BENT, the lane columns and a ramp's single column all take their
 *  offsets in whole tiles, which stands them clear of a deck whose width
 *  is s_tune.hiway_w.  Drawing them again means measuring those offsets
 *  from the deck's own width instead. */

/* ---- raised highways (the road spec, part 7) --------------------------- */

/*  A highway is not one tile wide.  Its deck is a TWO-TILE BAND -- the
 *  spec's 2x2 segment -- so its centreline runs along the seam between two
 *  rows or two columns, never through a tile centre, and the whole segment
 *  pipeline above (which walks tile to tile) cannot express it.  This walks
 *  the band instead and hands the ordinary loft a spine that is offset half
 *  a tile across.  Which ids are which was read off the shipped cities, not
 *  the sprite sheet -- see the Part 7 section of docs/future.rst.  Every
 *  one of these is ALWAYS part of a 2x2 square of highway (6905 of 6905 for
 *  0x49), and the id alone says which way the band runs, because an
 *  interior tile of a two-wide band has two neighbours along it and one
 *  across.  The six crossings are DECK tiles too: 0x4D is a deck in an
 *  east-west band with a railway underneath, and the rail below it is drawn
 *  by the rail family, not this one. */
/*  Is there a surface road under the deck here?  7.2 puts a two-column
 *  bent over one, its columns outside the curbs, and a hammerhead over
 *  anything else.  The two tiles the deck's own band covers are asked
 *  across it, since that is where a column would land. */
int road_under_deck(const RCity *c, float x, float y, float px, float py)
{
    int k;
    for (k = -1; k <= 1; k += 2)
    {
        int32_t tc = (int32_t)floorf(x + px * 0.5f * (float)k);
        int32_t tr = (int32_t)floorf(y + py * 0.5f * (float)k);
        uint8_t b;
        if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
            continue;
        b = c->xbld[tr * R_MAP + tc];
        if (net_road_on(b))
            return 1;
    }
    return 0;
}

/*  Is this building byte a highway, and which way does its band run?
 *  1 for a deck tile, 2 for a ramp of the band, 3 for an on-ramp, 0 for
 *  anything else.  Which bytes are which is arc.rules.hiway_tiles, read
 *  once a generation into a table, since every tile of the map is looked
 *  up in it. */
static int hiway_kind(uint8_t b, int *east_west)
{
    static uint8_t kind[256], ew[256];
    static int     gen = -1;
    if (gen != script_generation())
    {
        memset(kind, 0, sizeof kind);
        memset(ew, 0, sizeof ew);
        script_rule_hiway_tiles(kind, ew, 256);
        gen = script_generation();
    }
    *east_west = ew[b];
    return kind[b];
}

/*  The band itself: a deck tile, or a ramp of the band.  An on-ramp, a
 *  curve block and the interchange are highway and are not the band. */
static int hiway_deck(uint8_t b, int *east_west)
{
    int k = hiway_kind(b, east_west);
    return k == 1 || k == 2 ? k : 0;
}

/*  An on-ramp tile: one id per direction, one tile, standing beside the
 *  deck cell it climbs to. */
int net_hiway_onramp(uint8_t b)
{
    int ew;
    return hiway_kind(b, &ew) == 3;
}

/*  A curve block: four tiles of one id carrying a band through a right
 *  angle. */
int net_hiway_curve(uint8_t b)
{
    int ew;
    return hiway_kind(b, &ew) == 4;
}

/*  The interchange: a 2x2 where four bands meet. */
int net_hiway_interchange(uint8_t b)
{
    int ew;
    return hiway_kind(b, &ew) == 5;
}

/*  Any part of a highway at all, the interchange included. */
int net_hiway_any(uint8_t b)
{
    int ew;
    return hiway_kind(b, &ew) != 0;
}

/*  Why the on-ramp at a tile was not built this pass, for the area
 *  report: the stage that refused it, or 0. */
static uint8_t           s_ramp_lost[R_MAP * R_MAP];
static const char *const RAMP_LOST[6] = {NULL, "no ramp record for the tile", "no deck station within reach", "no deck station at the top of the descent", "the descent could not be routed", "no deck touching the tile"};
const char              *hiway_ramp_lost(int32_t col, int32_t row)
{
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return NULL;
    return RAMP_LOST[s_ramp_lost[row * R_MAP + col]];
}

static int hiway_is_ramp(uint8_t b)
{
    int ew;
    return hiway_deck(b, &ew) == 2;
}

/*  The band cell a deck tile belongs to, as its PRIMARY tile: the lower
 *  of the two across the band.  Answers 0 if the tile has no partner --
 *  a lone deck tile is malformed data and is left to the sprites. */
static int hiway_cell(const RCity *c, int32_t col, int32_t row, int32_t *pc, int32_t *pr, int *ew)
{
    uint8_t b = c->xbld[row * R_MAP + col];
    int32_t oc, orr;
    int     e2;
    if (!hiway_deck(b, ew))
        return 0;
    /*  across the band: north-south for an east-west deck */
    oc  = *ew ? col : col - 1;
    orr = *ew ? row - 1 : row;
    if (oc >= 0 && orr >= 0 && hiway_deck(c->xbld[orr * R_MAP + oc], &e2) && e2 == *ew)
    {
        *pc = oc;
        *pr = orr;
        return 1;
    }
    oc  = *ew ? col : col + 1;
    orr = *ew ? row + 1 : row;
    if (oc < R_MAP && orr < R_MAP && hiway_deck(c->xbld[orr * R_MAP + oc], &e2) && e2 == *ew)
    {
        *pc = col;
        *pr = row;
        return 1;
    }
    return 0;
}

/*  A curve block: the four tiles of one id, 0x65 to 0x68, that carry a
 *  highway through a right angle.  Answers its lowest tile and, of the
 *  four sides, which two carry the runs it joins.  The ids are not
 *  trusted for the orientation -- it is read off the runs that touch
 *  the block, as the crossing table was read off the shipped cities. */
static int hiway_block(const RCity *c, int32_t col, int32_t row, int32_t *bc, int32_t *br, int side[4])
{
    uint8_t b = c->xbld[row * R_MAP + col];
    int32_t k;
    int     e2;
    if (!net_hiway_curve(b))
        return 0;
    *bc = col;
    *br = row;
    for (k = 0; k < 2; ++k)
    {
        if (*bc > 0 && c->xbld[*br * R_MAP + *bc - 1] == b)
            --*bc;
        if (*br > 0 && c->xbld[(*br - 1) * R_MAP + *bc] == b)
            --*br;
    }
    /*  Its sides, north, east, south, west: a run of the axis that side
     *  would carry, on either of the two tiles along it. */
    side[0] = side[1] = side[2] = side[3] = 0;
    for (k = 0; k < 2; ++k)
    {
        int32_t x = *bc + k, y = *br + k;
        /*  Any deck piece on the block's side is the run it joins, the
         *  0x61-0x64 sections included: they are band cells like the plain
         *  ones, and taking only the plain ones left a block beside such a
         *  section with no side there, so the walk stopped inside it and
         *  the highway broke into pieces. */
        if (*br > 0 && hiway_deck(c->xbld[(*br - 1) * R_MAP + x], &e2) && !e2)
            side[0] = 1;
        if (*br + 2 < R_MAP && hiway_deck(c->xbld[(*br + 2) * R_MAP + x], &e2) && !e2)
            side[2] = 1;
        if (*bc + 2 < R_MAP && hiway_deck(c->xbld[y * R_MAP + *bc + 2], &e2) && e2)
            side[1] = 1;
        if (*bc > 0 && hiway_deck(c->xbld[y * R_MAP + *bc - 1], &e2) && e2)
            side[3] = 1;
    }
    return 1;
}

/*  The corridor a deck may sweep over: every tile that is free air --
 *  ground, trees, rubble, water -- plus the band's own.  What bounds it
 *  is what stands up: a building or anything else from 0x69, a surface
 *  network, or another deck.  Computed once a build, the band's own
 *  tiles stamped over it per walk. */
static uint8_t s_free[R_MAP * R_MAP];
static uint8_t s_corr_h[R_MAP * R_MAP];
static float   s_hiway_top[R_MAP * R_MAP]; /* a structure's top, in levels of altitude; 0 where none */
static uint8_t s_rise_said[256];           /* --hiway-dump: each building's rise, once */

static void hiway_free_air(const RCity *c, const RAtlasLevel *l)
{
    int32_t i;
    for (i = 0; i < R_MAP * R_MAP; ++i)
    {
        /*  A surface network is not in the way of a viaduct: the deck
         *  crosses over it on a straddle bent (spec 7.2).  Another deck is,
         *  being at the same height.  A structure -- a building, a park,
         *  anything from 0x69 -- is not in the way at all: the deck is
         *  raised, and would rather the deck take the air and the building
         *  renderer keep buildings off it later.  How high each stands is
         *  still measured, for --hiway-dump, in case that changes.  The
         *  height is the sprite's own: the art rises `ay` above the left
         *  corner of its diamond, the top corner of which is half the
         *  footprint's diamond above that, and the rest is building,
         *  alt_step pixels to the level.  Not the bounding box, though -- a
         *  chimney, an antenna or a flag would make a shed a tower -- but
         *  the bulk: the first row down from the top with paint across two
         *  fifths of the diamond's width.  A tree stands about a level, a
         *  house one, and the warehouse at Toronto 112,44 (0xA8) 2.3 to its
         *  box but less to its roof.  One the art set cannot show is a
         *  wall. */
        uint8_t      b = c->xbld[i];
        const RTile *t;
        int32_t      ybulk;
        s_hiway_top[i] = 0.0f;
        /*  The interchange is a deck like the rest: its sprite, the
         *  cloverleaf, measures 2.4 levels, and taken for a building it
         *  lifted every deck through it. */
        if (net_hiway_any(b))
        {
            s_free[i] = 0;
            continue;
        }
        if (!net_stands_up(b))
        {
            s_free[i] = 1;
            continue;
        }
        t = l ? atlas_tile(l, l->id_base + b) : NULL;
        if (!t || l->alt_step <= 0)
        {
            s_free[i] = 0;
            continue;
        }
        for (ybulk = 0; ybulk < (int32_t)t->h; ++ybulk)
        {
            int32_t x, n = 0;
            for (x = 0; x < (int32_t)t->w; ++x)
                if ((int32_t)l->indices[((size_t)t->y + (size_t)ybulk) * (size_t)l->w + (size_t)t->x + (size_t)x] != l->transparent)
                    ++n;
            if (5 * n >= 2 * (int32_t)t->foot * l->tile_w)
                break;
        }
        {
            float rise = ((float)t->ay - 0.5f * (float)t->foot * (float)l->tile_h - (float)ybulk) / (float)l->alt_step;
            if (rise < 0.0f)
                rise = 0.0f;
            s_hiway_top[i] = (float)rcity_alt_ground(c->altm[i]) + rise;
            s_free[i]      = 1;
            if (g_dev.hiway_dump && !s_rise_said[b])
            {
                s_rise_said[b] = 1;
                dumpf("hiway rise: xbld %02x foot %d box %.2f bulk %.2f levels\n", (unsigned)b, (int)t->foot, (double)(((float)t->ay - 0.5f * (float)t->foot * (float)l->tile_h) / (float)l->alt_step), (double)rise);
            }
        }
    }
}

static const uint8_t *hiway_corridor(const RCity *c, const int32_t *own, int n_own, int ramps)
{
    int k;
    /*  Free air is free in height too: a deck a level over its own ground
     *  may sweep over ground no higher than that, and a hill beside the
     *  band stands in its way as surely as a building.  So only the free
     *  tiles within reach of the band -- three tiles, the most an arc cuts
     *  inside a corner at the widest sweep -- and no higher than the band's
     *  own ground next to them, are its corridor.  A structure on such
     *  ground is in it whatever its height.  The deck stays at its lift and
     *  the building under it is the building renderer's to keep off the
     *  deck; a profile that climbed over roofs spread each climb along the
     *  grade until every deck in Toronto rode two levels up.  The bulk
     *  measured above is kept for --hiway-dump. */
    memset(s_corr_h, 0, sizeof s_corr_h);
    for (k = 0; k < n_own; ++k)
    {
        int32_t oc = own[k] % R_MAP, orr = own[k] / R_MAP, dc, dr;
        int     lvl = rcity_alt_ground(c->altm[own[k]]);
        for (dr = -3; dr <= 3; ++dr)
            for (dc = -3; dc <= 3; ++dc)
            {
                int32_t tc = oc + dc, tr = orr + dr, i;
                if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                    continue;
                i = tr * R_MAP + tc;
                if (!s_free[i] || rcity_alt_ground(c->altm[i]) > lvl)
                    continue;
                s_corr_h[i] = 1;
            }
    }
    for (k = 0; k < n_own; ++k)
        s_corr_h[own[k]] = 1;
    /*  And the band's own on-ramp tiles, beside its cells: a deck's
     *  edge over a ramp tile is exactly where the ramp's lane leaves
     *  it.  Kept out, the one straight line through Atlanta 65,81 to
     *  60,74 was refused for the third of a tile its edge took of the
     *  ramp at 59,76. */
    for (k = 0; ramps && k < n_own; ++k)
    {
        static const int32_t DC[4] = {1, -1, 0, 0}, DR[4] = {0, 0, 1, -1};
        int32_t              oc = own[k] % R_MAP, orr = own[k] / R_MAP, e;
        for (e = 0; e < 4; ++e)
        {
            int32_t tc = oc + DC[e], tr = orr + DR[e];
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            if (net_hiway_onramp(c->xbld[tr * R_MAP + tc]))
                s_corr_h[tr * R_MAP + tc] = 1;
        }
    }
    return s_corr_h;
}

/*  Each band's own tiles as it was walked, for the incremental rebuild's
 *  closure (mesh/incr.c): a band near an edit is fitted again. */
#define HWB_MAX 1024
static int32_t s_hwb_tiles[64 * 1024];
static int     s_hwb_first[HWB_MAX], s_hwb_n[HWB_MAX], s_hwb_count, s_hwb_fill;

static void hiway_band_record(const int32_t *own, int n)
{
    int k;
    if (s_hwb_count < 0 || s_hwb_count >= HWB_MAX || s_hwb_fill + n > (int)(sizeof s_hwb_tiles / sizeof *s_hwb_tiles))
    {
        s_hwb_count = -1; /* past the table: the closure rebuilds every chunk */
        return;
    }
    s_hwb_first[s_hwb_count] = s_hwb_fill;
    s_hwb_n[s_hwb_count]     = n;
    for (k = 0; k < n; ++k)
        s_hwb_tiles[s_hwb_fill++] = own[k];
    ++s_hwb_count;
}

int hiway_band_count(void)
{
    return s_hwb_count;
}

int hiway_band_get(int i, const int32_t **tiles, int *n)
{
    if (i < 0 || i >= s_hwb_count)
        return -1;
    *tiles = &s_hwb_tiles[s_hwb_first[i]];
    *n     = s_hwb_n[i];
    return 0;
}

/*  Walk one band from an end and loft its deck.  The spine runs along
 *  the seam: half a tile across from the primary tile's centre. */

/*  One band's walk, in stages: the tiles, the ends, the fit, the
 *  overlay, the loft.  Each stage reads what it needs off this and
 *  writes back what it changed. */
typedef struct
{
    RMesh       *m;
    const RCity *c;
    uint8_t      mask_bit;
    int          comp;
    int32_t      col, row; /* where the walk began */
    int          ew, sign;
    uint8_t     *seen;
    V2          *pts; /* the spine's points: seam points and block centres */
    uint8_t     *ramp;
    uint8_t     *block; /* per point: a curve block's centre */
    uint8_t     *stair; /* per point: the collapsed staircase it belongs to, 1-based, or 0 (hw_chain) */
    Piece       *pieces;
    int32_t     *own; /* the band's own tiles */
    V2          *q;   /* the fit's nodes, their radii and tangent budgets */
    float       *rad, *tlim;
    int          n, n_own, np, nk;
    int          table;        /* the band's entry in the segment table, -1 for none: the loft's cache key */
    V2           band0, band1; /* the ends, run out to the edge of the end cells */
    int32_t      cc, cr, dx, dy;
    int          cew;
    float        total, ramp0, ramp1;
} HwWalk;

/*  Take the cell the walk stands on: the primary of its pair, not seen
 *  before, its point on the spine and both of its tiles the band's own.
 *  0 where the band ends. */
static int hw_take_cell(HwWalk *x)
{
    const RCity *c     = x->c;
    uint8_t     *seen  = x->seen;
    V2          *pts   = x->pts;
    uint8_t     *ramp  = x->ramp;
    int32_t     *own   = x->own;
    int          n_own = x->n_own;
    int32_t      cc    = x->cc;
    int32_t      cr    = x->cr;
    int          n     = x->n;
    int          cew   = x->cew;
    int32_t      pcol, prow;
    int          e2;
    if (cc < 0 || cr < 0 || cc >= R_MAP || cr >= R_MAP)
        return 0;
    if (!hiway_cell(c, cc, cr, &pcol, &prow, &e2) || e2 != cew)
        return 0;
    if (pcol != cc || prow != cr)
        return 0; /* not the primary of its pair */
    if (seen[cr * R_MAP + cc])
        return 0;
    seen[cr * R_MAP + cc] = 1;
    if (n + 2 >= MAX_PTS)
        return 0;
    ramp[n]     = (uint8_t)hiway_is_ramp(c->xbld[cr * R_MAP + cc]);
    x->block[n] = 0;
    pts[n++]    = (V2){(float)cc + (cew ? 0.5f : 1.0f), (float)cr + (cew ? 1.0f : 0.5f)};
    if (n_own + 2 <= 4 * MAX_PTS)
    {
        own[n_own++] = cr * R_MAP + cc;
        own[n_own++] = cew ? (cr + 1) * R_MAP + cc : cr * R_MAP + cc + 1;
    }
    x->n     = n;
    x->n_own = n_own;
    return 1;
}

/*  Straight on: the next cell along the band, when it is one. */
static int hw_step_on(HwWalk *x, int32_t nc, int32_t nr)
{
    const RCity *c    = x->c;
    uint8_t     *seen = x->seen;
    int          cew  = x->cew;
    int32_t      pcol, prow;
    int          e2;
    if (nc >= 0 && nr >= 0 && nc < R_MAP && nr < R_MAP && hiway_cell(c, nc, nr, &pcol, &prow, &e2) && e2 == cew &&
        pcol == nc && prow == nr && !seen[nr * R_MAP + nc])
    {
        x->cc = nc;
        x->cr = nr;
        return 1;
    }
    return 0;
}

/*  Through a curve block, and on through every block that follows it:
 *  its centre lies on both seams, so the spine turns there and the
 *  fillet carries the arc back into the tile before and on into the one
 *  after.  The art draws a diagonal highway as a chain of these blocks
 *  touching at their corners, so the walk follows the chain by whatever
 *  step joins one to the next, and the straightener makes one line of
 *  the centres.  1 when a chain was taken (the walk goes on from where
 *  it left the chain, or ends if nothing continued it). */
/*  Take a curve block: its four tiles seen and the band's own, its
 *  centre a point of the spine. */
static void hw_block_take(HwWalk *x, int32_t bc, int32_t br)
{
    uint8_t *seen                   = x->seen;
    V2      *pts                    = x->pts;
    uint8_t *ramp                   = x->ramp;
    int32_t *own                    = x->own;
    int      n_own                  = x->n_own;
    int      n                      = x->n;
    seen[br * R_MAP + bc]           = 1;
    seen[br * R_MAP + bc + 1]       = 1;
    seen[(br + 1) * R_MAP + bc]     = 1;
    seen[(br + 1) * R_MAP + bc + 1] = 1;
    if (n_own + 4 <= 4 * MAX_PTS)
    {
        own[n_own++] = br * R_MAP + bc;
        own[n_own++] = br * R_MAP + bc + 1;
        own[n_own++] = (br + 1) * R_MAP + bc;
        own[n_own++] = (br + 1) * R_MAP + bc + 1;
    }
    ramp[n]     = 0;
    x->block[n] = 1;
    pts[n++]    = (V2){(float)bc + 1.0f, (float)br + 1.0f};
    x->n        = n;
    x->n_own    = n_own;
}

/*  The next block of the chain: a step that carries on the way we
 *  were going, never back; a step across is allowed and comes first, so
 *  a chain of blocks is followed block by block rather than jumping the
 *  diagonal and leaving an irregular path the straightener cannot read.
 *  1 with the block and the step taken to it. */
static int hw_block_next(const RCity *c, const uint8_t *seen, int32_t bc, int32_t br, int32_t *sx, int32_t *sy, int32_t *nbc, int32_t *nbr)
{
    static const int32_t off[8][2] = {
        {2,  0 },
        {-2, 0 },
        {0,  2 },
        {0,  -2},
        {2,  2 },
        {2,  -2},
        {-2, 2 },
        {-2, -2}
    };
    int have = 0, k;
    for (k = 0; k < 8 && !have; ++k)
    {
        int32_t qc = bc + off[k][0], qr = br + off[k][1], mc, mr;
        int     qs[4];
        /*  Never back the way we came; a step across is
         *  allowed, and comes first, so a chain of blocks is
         *  followed block by block rather than jumping the
         *  diagonal and leaving an irregular path the
         *  straightener cannot read. */
        if (off[k][0] * *sx + off[k][1] * *sy < 0)
            continue;
        if (qc < 0 || qr < 0 || qc >= R_MAP || qr >= R_MAP)
            continue;
        if (!hiway_block(c, qc, qr, &mc, &mr, qs) || seen[mr * R_MAP + mc])
            continue;
        *nbc = mc;
        *nbr = mr;
        *sx  = off[k][0];
        *sy  = off[k][1];
        have = 1;
    }
    return have;
}

/*  The chain ends here: the run this block hands the highway to, which
 *  is the side that carries one and is not the way we came in.  The
 *  walk goes on from that run's first cell; 0 when there is none. */
static int hw_block_exit(HwWalk *x, int32_t bc, int32_t br, const int *side, int32_t sx, int32_t sy)
{
    int32_t              cc, cr, dx, dy;
    int                  cew, k;
    static const int32_t odx[4] = {0, 1, 0, -1};
    static const int32_t ody[4] = {-1, 0, 1, 0};
    int                  out    = -1;
    for (k = 0; k < 4; ++k)
        if (side[k] && odx[k] * sx + ody[k] * sy > 0)
            out = k;
    if (out < 0)
        for (k = 0; k < 4; ++k)
            if (side[k] && !(odx[k] * sx + ody[k] * sy < 0))
                out = k;
    if (out < 0)
        return 0;
    cew    = (out == 1 || out == 3);
    dx     = odx[out];
    dy     = ody[out];
    cc     = bc + (out == 1 ? 2 : out == 3 ? -1
                                           : 0);
    cr     = br + (out == 2 ? 2 : out == 0 ? -1
                                           : 0);
    x->cew = cew;
    x->dx  = dx;
    x->dy  = dy;
    x->cc  = cc;
    x->cr  = cr;
    return 1;
}

static int hw_step_blocks(HwWalk *x)
{
    const RCity *c     = x->c;
    uint8_t     *seen  = x->seen;
    int          n_own = x->n_own;
    int32_t      cc    = x->cc;
    int32_t      cr    = x->cr;
    int          n     = x->n;
    int          cew   = x->cew;
    int32_t      dx    = x->dx;
    int32_t      dy    = x->dy;
    int32_t      bc, br;
    int          side[4];
    /*  Through a curve block, and on through every block that follows it:
     *  its centre lies on both seams, so the spine turns there and the
     *  fillet carries the arc back into the tile before and on into the one
     *  after.  The art draws a diagonal highway as a chain of these blocks
     *  touching at their corners, so the walk follows the chain by whatever
     *  step joins one to the next, two tiles along, across, or both, and
     *  the straightener makes one line of the centres. */
    {
        int32_t tc = cew ? (dx > 0 ? cc + 1 : cc - 1) : cc;
        int32_t tr = cew ? cr : (dy > 0 ? cr + 1 : cr - 1);
        int32_t sx = dx, sy = dy; /* the step that brought us here */
        int     chained = 0;
        while (tc >= 0 && tr >= 0 && tc < R_MAP && tr < R_MAP && hiway_block(c, tc, tr, &bc, &br, side) && n + 2 < MAX_PTS &&
               !seen[br * R_MAP + bc])
        {
            int32_t nbc = 0, nbr = 0;
            int     have;
            hw_block_take(x, bc, br);
            n       = x->n;
            n_own   = x->n_own;
            chained = 1;
            have    = hw_block_next(c, seen, bc, br, &sx, &sy, &nbc, &nbr);
            if (have)
            {
                tc = nbc;
                tr = nbr;
                continue;
            }
            if (!hw_block_exit(x, bc, br, side, sx, sy))
                break;
            cew = x->cew, dx = x->dx, dy = x->dy, cc = x->cc, cr = x->cr;
            break;
        }
        if (chained)
        {
            x->n     = n;
            x->n_own = n_own;
            x->cc    = cc;
            x->cr    = cr;
            x->cew   = cew;
            x->dx    = dx;
            x->dy    = dy;
            return 1;
        }
    }
    return 0;
}

/*  A step sideways: the staircase of a diagonal run. */
static int hw_step_aside(HwWalk *x, int32_t nc, int32_t nr)
{
    const RCity *c    = x->c;
    uint8_t     *seen = x->seen;
    int          cew  = x->cew;
    int32_t      pcol, prow;
    int          e2, k;
    {
        int     found = 0;
        int32_t sc, sr;
        for (k = -1; k <= 1 && !found; k += 2)
        {
            sc = nc + (cew ? 0 : k);
            sr = nr + (cew ? k : 0);
            if (sc < 0 || sr < 0 || sc >= R_MAP || sr >= R_MAP)
                continue;
            if (hiway_cell(c, sc, sr, &pcol, &prow, &e2) && e2 == cew && pcol == sc && prow == sr && !seen[sr * R_MAP + sc])
            {
                x->cc = sc;
                x->cr = sr;
                found = 1;
            }
        }
        if (found)
            return 1;
    }
    return 0;
}

/*  The tiles: from the first cell along the band wherever it goes --
 *  straight on while it can, through a curve block at a right angle, or
 *  a step sideways -- until nothing continues it. */
static void hw_walk(HwWalk *x)
{
    int guard = 0;
    while (guard++ < 4 * R_MAP)
    {
        int32_t nc, nr;
        if (!hw_take_cell(x))
            break;
        nc = x->cc + x->dx;
        nr = x->cr + x->dy;
        if (hw_step_on(x, nc, nr) || hw_step_blocks(x) || hw_step_aside(x, nc, nr))
            continue;
        break;
    }
}

/*  Run the spine to the outer edge of the end cells, so the deck
 *  covers its whole first and last segment rather than stopping at
 *  their centres.  The direction at each end is the polyline's own,
 *  since the band may have turned along the way. */
static void hw_ends(HwWalk *x)
{
    V2 *pts   = x->pts;
    V2  band0;
    V2  band1;
    int n     = x->n;
    {
        float d0x = pts[0].x - pts[1].x, d0y = pts[0].y - pts[1].y;
        float d1x = pts[n - 1].x - pts[n - 2].x, d1y = pts[n - 1].y - pts[n - 2].y;
        float l0 = sqrtf(d0x * d0x + d0y * d0y), l1 = sqrtf(d1x * d1x + d1y * d1y);
        band0 = pts[0];
        band1 = pts[n - 1];
        if (l0 > 1e-4f)
        {
            band0.x += d0x / l0 * 0.5f;
            band0.y += d0y / l0 * 0.5f;
        }
        if (l1 > 1e-4f)
        {
            band1.x += d1x / l1 * 0.5f;
            band1.y += d1y / l1 * 0.5f;
        }
    }
    x->band0 = band0;
    x->band1 = band1;
}

/*  The band's tiles as a mask, for the fit's coverage rule: the deck must
 *  pass over every one of them -- less the tiles of the curve blocks,
 *  which a corner's sweep or a staircase's diagonal rightly cuts inside
 *  (with them held, Babar's ring highway lost its radius-8 corners to
 *  2.3, the loft refused them and the ring drew nothing).  The straight
 *  cells beside an on-ramp are in it like any other, and the fit holds
 *  its runs and its joins to them, so a staircase may collapse next to
 *  a ramp and the deck still passes the ramp's cell. */
static uint8_t        s_own_mask[R_MAP * R_MAP];
static const uint8_t *hiway_own_mask(const HwWalk *x)
{
    int k;
    memset(s_own_mask, 0, sizeof s_own_mask);
    for (k = 0; k < x->n_own; ++k)
        s_own_mask[x->own[k]] = 1;
    for (k = 0; k < x->n; ++k)
        if (x->block[k])
        {
            int32_t bc = (int32_t)floorf(x->pts[k].x) - 1, br = (int32_t)floorf(x->pts[k].y) - 1, dc, dr;
            for (dr = 0; dr < 2; ++dr)
                for (dc = 0; dc < 2; ++dc)
                    if (bc + dc >= 0 && br + dr >= 0 && bc + dc < R_MAP && br + dr < R_MAP)
                        s_own_mask[(br + dr) * R_MAP + bc + dc] = 0;
        }
    return s_own_mask;
}

/*  Which way the band turns at point j: the sign of the cross product
 *  of the steps in and out, 0 at an end or straight through. */
static int hw_turn(const V2 *pts, int n, int j)
{
    float cross;
    if (j <= 0 || j + 1 >= n)
        return 0;
    cross = (pts[j].x - pts[j - 1].x) * (pts[j + 1].y - pts[j].y) - (pts[j].y - pts[j - 1].y) * (pts[j + 1].x - pts[j].x);
    return cross > 1e-4f ? 1 : cross < -1e-4f ? -1
                                              : 0;
}

/*  Is a straight cell's point pinned by an on-ramp beside it?  An
 *  on-ramp joins the deck cell it touches, so the deck must pass over
 *  that cell: a staircase breaks at such a cell rather than sliding its
 *  diagonal off it. */
static int hw_pinned(const RCity *c, V2 p)
{
    int32_t fx = (int32_t)floorf(p.x), fy = (int32_t)floorf(p.y);
    int32_t t[2][2], k, d;
    if (p.x - (float)fx > 0.25f) /* an east-west cell: the pair is the rows above and below the point */
        t[0][0] = fx, t[0][1] = fy - 1, t[1][0] = fx, t[1][1] = fy;
    else /* a north-south cell: the pair is the columns either side */
        t[0][0] = fx - 1, t[0][1] = fy, t[1][0] = fx, t[1][1] = fy;
    for (k = 0; k < 2; ++k)
        for (d = 0; d < 4; ++d)
        {
            static const int32_t DC[4] = {0, 1, 0, -1}, DR[4] = {-1, 0, 1, 0};
            int32_t              qc = t[k][0] + DC[d], qr = t[k][1] + DR[d];
            uint8_t              b;
            if (qc < 0 || qr < 0 || qc >= R_MAP || qr >= R_MAP)
                continue;
            b = c->xbld[qr * R_MAP + qc];
            if (net_hiway_onramp(b))
                return 1;
        }
    return 0;
}

/*  The points the fit is given: the straight cells', and a curve block's
 *  centre only where the block is a corner of its own.  A STAIRCASE --
 *  blocks turning alternately with at most two straight cells between, the
 *  game's way of laying a diagonal -- is given as ONE point, the centre of
 *  the blocks and short straights it is made of, so the run before it, the
 *  diagonal through its middle and the run after it are three legs the fit
 *  fillets at their two bends; the coverage rule keeps the deck over the
 *  short straights.  With every block a point, a staircase was a polyline
 *  of right-angle corners two tiles apart, each filleted at a tile's
 *  radius: a serpentine; with nothing of it, the fit joined the runs at its
 *  ends by an L across the block beside it. */
/*  Which way the chain turns at cell i: 1, -1, or 0 for straight on. */
int hiway_stair_turn(const StairFan *s, int i)
{
    return hw_turn(s->pts, s->n, i);
}

/*  Is the cell pinned by an on-ramp beside it? */
int hiway_stair_pinned(const StairFan *s, int i)
{
    return hw_pinned((const RCity *)s->c, s->pts[i]);
}

/*  A cell as a point of the chain, and a stair as the one point at the
 *  centre of the cells it is made of. */
void hiway_stair_point(StairFan *s, int i)
{
    s->chain[s->nc++] = s->pts[i];
}

void hiway_stair_centre(StairFan *s, int i, int j)
{
    V2  mid = {0.0f, 0.0f};
    int k, m = 0;
    for (k = i; k <= j; ++k, ++m)
        mid.x += s->pts[k].x, mid.y += s->pts[k].y;
    s->chain[s->nc++] = (V2){mid.x / (float)m, mid.y / (float)m};
}

/*  The points the fit is given, as arc.rules.stair picks them. */
static int hw_chain(HwWalk *x, V2 *chain)
{
    StairFan s;
    memset(&s, 0, sizeof s);
    s.c     = x->c;
    s.pts   = x->pts;
    s.block = x->block;
    s.n     = x->n;
    s.gap   = (int)s_tune.hiway_stair;
    s.chain = chain;
    script_rule_object("stair", "stair", &s);
    return s.nc;
}

/*  The fit two ways, the better kept: with free lines and the band's
 *  ramp tiles in its corridor -- the deck as straight as its corridor
 *  allows -- and the plain fit.  Which of the two is better is
 *  arc.rules.fit_choice's. */
static int hw_fit_best(const RCity *c, const HwWalk *x, const int32_t *own, int n_own, const V2 *chain, int nc, V2 band0, V2 band1, V2 *q, float *rad, float *tlim)
{
    static V2    q2[MAX_PTS];
    static float rad2[MAX_PTS], tlim2[MAX_PTS];
    static char  tally[2][512], before[512]; /* the fit's tallies after each way, and before both */
    int          nk[2], corners[2] = {0, 0}, tight[2] = {0, 0}, w, k, keep;
    path_fit_tally_get(2, before, sizeof before);
    for (w = 0; w < 2; ++w)
    {
        V2    *qq = w ? q : q2;
        float *rr = w ? rad : rad2, *tt = w ? tlim : tlim2;
        path_fit_tally_set(2, before, sizeof before);
        nk[w] = path_fit_points(hiway_corridor(c, own, n_own, w), hiway_own_mask(x), chain, nc, s_tune.hiway_w, band0, band1, s_tune.hiway_rmax, s_tune.hiway_rmin, 1.0f, -1, -1, w, qq, rr, tt, MAX_PTS);
        for (k = 1; k + 1 < nk[w]; ++k)
            if (rr[k] < 0.01f)
                ++corners[w];
            else if (rr[k] < s_tune.hiway_rmin)
                ++tight[w];
        path_fit_tally_get(2, tally[w], sizeof tally[w]);
    }
    {
        const int free_[3] = {corners[1], tight[1], nk[1]}, held[3] = {corners[0], tight[0], nk[0]};
        keep               = script_rule_fit_choice("hiway", free_, held);
    }
    if (g_dev.path_dump)
        dumpf("FIT %s kept: free %d corners %d tight %d nodes, plain %d corners %d tight %d nodes\n", keep ? "free" : "plain", corners[1], tight[1], nk[1], corners[0], tight[0], nk[0]);
    path_fit_tally_set(2, tally[keep], sizeof tally[keep]); /* the kept fit's tallies alone */
    if (!keep)
    {
        memcpy(q, q2, sizeof(V2) * (size_t)nk[0]);
        memcpy(rad, rad2, sizeof(float) * (size_t)nk[0]);
        memcpy(tlim, tlim2, sizeof(float) * (size_t)nk[0]);
    }
    return nk[keep];
}

/*  The corridor fit, straightened as a road is and filleted with the
 *  wide radius a highway wants; 1 when there is nothing to draw. */
static int hw_fit(HwWalk *x)
{
    const RCity *c      = x->c;
    int32_t      col    = x->col;
    int32_t      row    = x->row;
    V2          *pts    = x->pts;
    uint8_t     *ramp   = x->ramp;
    Piece       *pieces = x->pieces;
    int32_t     *own    = x->own;
    V2          *q      = x->q;
    float       *rad    = x->rad;
    float       *tlim   = x->tlim;
    int          n_own  = x->n_own;
    V2           band0  = x->band0;
    V2           band1  = x->band1;
    int          n      = x->n;
    int          np     = x->np;
    float        ramp0;
    float        ramp1;
    int          nk;
    /*  Straightened as a road is: a staircase of cells becomes one
     *  diagonal, then every bend is filleted, with the wide radius a
     *  highway wants so the curve begins well before the corner.  The ramp
     *  lengths come first, measured along the raw polyline in TILES: the
     *  taper below compares them against arc length, and a count of points
     *  is no length at all once the straightener has collapsed a run.  A
     *  deck is raised end to end -- the 0x61-0x64 cells at a band's ends
     *  are the band's own, the deck runs over them, and the taper is the
     *  on-ramps' alone. */
    ramp0 = ramp1 = 0.0f;
    (void)ramp;
    /*  The deck on the corridor fit, as a chain of its own points: the
     *  seam points and the block centres.  The corridor is not the band's
     *  tiles alone -- a deck is RAISED, and may sweep over any tile that is
     *  free air: ground, trees, water.  Only what stands in the way bounds
     *  it: a building, another network, another deck.  So a jog of two
     *  tiles becomes one long S over the verge beside it, and a corner
     *  block's arc takes the radius a highway wants.  Nothing has to be
     *  covered: where the arc cuts inside a block, the ground shows under
     *  the viaduct, as it should. */
    fit_family(net_hiway.fit_fam);
    /*  Every one of the band's own tiles must end up under the deck, as a
     *  road's must under its strip: the corridor says where the deck MAY
     *  sweep, the own tiles where it MUST pass.  Without that rule a long
     *  band's jogs collapse into a line across the block beside them, tiles
     *  off the highway's cells and over roads and buildings.  The fit is
     *  given the chain hw_chain makes of the points: the straight cells',
     *  a lone block's corner, and nothing of a staircase, so the runs at
     *  its ends meet across it.  The band the fit holds to the corridor is
     *  the deck's own width and no wider: at a full tile each side its
     *  inner edge at a corner samples the on-ramp tile beside the deck,
     *  which is not free air, and every fillet is refused down to a kink. */
    {
        static V2 chain[MAX_PTS];
        int       nc = hw_chain(x, chain);
        nk           = hw_fit_best(c, x, own, n_own, chain, nc, band0, band1, q, rad, tlim);
    }
    if (nk < 2)
    {
        x->nk = nk;
        return 1;
    }
    if (g_dev.hiway_dump)
    {
        int q2;
        dumpf("hiway band from c%d r%d: %d points ->", (int)col, (int)row, n);
        for (q2 = 0; q2 < n && q2 < 80; ++q2)
            dumpf(" (%.2f,%.2f)", (double)pts[q2].x, (double)pts[q2].y);
        dumpf("\n   fitted %d ->", nk);
        for (q2 = 0; q2 < nk && q2 < 14; ++q2)
            dumpf(" (%.2f,%.2f) r%.2f", (double)q[q2].x, (double)q[q2].y, (double)rad[q2]);
        dumpf("\n");
    }
    if (g_dev.path_dump)
    {
        /*  The same lines the road fit prints, so tools/plan.py draws
         *  a deck the way it draws a road: its tiles, the fitted line,
         *  its radii and budgets, and the runs it found. */
        int d;
        dumpf("PATH hw=%.3f\nTILES", (double)s_tune.hiway_w);
        for (d = 0; d < n_own; ++d)
            dumpf(" %d,%d", (int)(own[d] % R_MAP), (int)(own[d] / R_MAP));
        dumpf("\nGATES\nPTS");
        for (d = 0; d < nk; ++d)
            dumpf(" %.3f,%.3f", (double)q[d].x, (double)q[d].y);
        dumpf("\nRAD");
        for (d = 0; d < nk; ++d)
            dumpf(" %.3f", (double)rad[d]);
        dumpf("\nTLIM");
        for (d = 0; d < nk; ++d)
            dumpf(" %.3f", (double)tlim[d]);
        dumpf("\n");
        path_fit_prims();
    }
    if (fillet_t(q, nk, rad, tlim, pieces, &np) != 0 || np == 0)
    {
        x->np = 0;
        return 1;
    }
    /*  The band kept for the building pass and the next build: its
     *  pieces, its fit's nodes, its own tiles.  The building pass replays
     *  it from here rather than walking and fitting it again. */
    if (s_pass != 2)
        x->table = seg_store_band(x->col, x->row, x->ew, x->sign, pieces, np, q, rad, tlim, nk, own, n_own);
    x->nk    = nk;
    x->np    = np;
    x->ramp0 = ramp0;
    x->ramp1 = ramp1;
    return 0;
}

/*  The fit's nodes and corridors as ground highlights, when the overlay
 *  is on; 1 to stop, as a failed highlight always has. */
static int hw_overlay(HwWalk *x)
{
    RMesh       *m        = x->m;
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    int32_t     *own      = x->own;
    V2          *q        = x->q;
    float       *rad      = x->rad;
    int          n_own    = x->n_own;
    int          nk       = x->nk;
    /*  The fit's nodes, when the overlay is on: the same marks the road
     *  walk draws, on the deck rather than the ground.  And under them what
     *  the fit had to work with: the band's own tiles outlined in tan, the
     *  free air beside them in blue.  Ground highlights, blended tints
     *  under everything that stands on the tile; the outlines before them,
     *  and the slabs before those, hid the town. */
    if (s_tune.show_curves > 0.5f && s_pass != 1)
    {
        int     k3;
        int32_t ti;
        for (ti = 0; ti < R_MAP * R_MAP; ++ti)
        {
            int32_t tc = ti % R_MAP, tr = ti / R_MAP;
            int     isown = 0;
            for (k3 = 0; k3 < n_own && !isown; ++k3)
                isown = own[k3] == ti;
            if (!isown && !s_corr_h[ti])
                continue;
            if (tile_highlight(m, c, mask_bit, tc, tr, isown ? 3.0f : 6.0f) != 0)
                return 1;
        }
        for (k3 = 0; k3 < nk; ++k3)
        {
            int32_t tc = (int32_t)floorf(q[k3].x), tr = (int32_t)floorf(q[k3].y);
            float   paint, z, half;
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            paint = (k3 == 0 || k3 + 1 == nk) ? 7.0f : (rad[k3] > 0.001f ? 3.0f : 1.0f);
            if (k3 > 0 && k3 + 1 < nk && rad[k3] <= 0.001f)
            {
                V2    u1 = {q[k3].x - q[k3 - 1].x, q[k3].y - q[k3 - 1].y};
                V2    u2 = {q[k3 + 1].x - q[k3].x, q[k3 + 1].y - q[k3].y};
                float l1 = v2len(u1), l2 = v2len(u2);
                if (l1 > 1e-5f && l2 > 1e-5f && (u1.x * u2.x + u1.y * u2.y) / (l1 * l2) > 0.9999f)
                    paint = 7.0f;
            }
            (void)half;
            z    = surface_at_world(c, mask_bit, q[k3].x, q[k3].y) + HIWAY_LIFT;
            /*  The mark is a model (scripts/models.lua): a line's end
             *  is a smaller one than a turn. */
            if (net_model_put_on(net_model_find(k3 == 0 || k3 + 1 == nk ? "node_end" : "node_mid"),
                                 m, c, mask_bit, tile_order(c, tc, tr, mask_bit),
                                 q[k3].x, q[k3].y, 1.0f, 0.0f, 0.0f, paint, 0.0f, z, z, 1) != 0)
                return 1;
        }
    }
    return 0;
}

/*  The deck lofted on the pieces, and its lanes. */
static int hw_loft(HwWalk *x)
{
    RMesh       *m        = x->m;
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    int          comp     = x->comp;
    Piece       *pieces   = x->pieces;
    int32_t     *own      = x->own;
    int          n_own    = x->n_own;
    int          np       = x->np;
    float        total    = x->total;
    float        ramp0    = x->ramp0;
    float        ramp1    = x->ramp1;
    int          guard;
    for (guard = 0; guard < np; ++guard)
        total += pieces[guard].len;
    /*  The lift tapers over the ramp cells at each end, from the
     *  deck's height where the elevated tiles begin down to the
     *  ground at the band's outer end, so a ramp is a ramp. */
    int   rc;
    RLoft d = {0};
    if (ramp0 >= total)
        ramp1 = 0.0f; /* all ramp: one slope, not two */
    d.f             = F_ROAD;
    d.fam           = &net_hiway;
    d.hw            = s_tune.hiway_w; /* the deck's half width */
    d.ground_margin = 0.06f;          /* and it reads the ground a hair beyond its edges */
    d.mat           = MAT_HIWAY;
    d.kind          = LOFT_DECK;
    d.ramp0         = ramp0;
    d.ramp1         = ramp1;
    d.pin0 = d.pin1 = 1;
    hiway_band_record(own, n_own);
    d.band = ++s_hw_band; /* the band the loft records its stations under */
    d.cls  = -1.0f;
    /* the stations from the table's cache, as a road's: this build's, or the last one's when no edit came near */
    d.cache = x->table + 1;
    d.hash  = pieces_hash(pieces, np, total);
    for (guard = 0; guard < n_own && !d.hot; ++guard)
        if (mesh_incr_near(own[guard] % R_MAP, own[guard] / R_MAP))
            d.hot = 1;
    d.records_only = s_incr_on;
    for (guard = 0; guard < n_own && d.records_only; ++guard)
        if (mesh_want_tile(own[guard] % R_MAP, own[guard] / R_MAP))
            d.records_only = 0;
    rc = loft(m, c, mask_bit, comp, &d, pieces, np, total);
    /* the deck's six lanes, under this band (lane.c) */
    if (rc == 0 && lane_deck(m, c, mask_bit, pieces, np, s_tune.hiway_w, s_hw_band) != 0) /* the lanes at fractions of the deck's half width */
        return -1;
    return rc;
}

static int walk_hiway(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, int32_t col, int32_t row, int ew, int sign, uint8_t *seen)
{
    static V2      pts[MAX_PTS];
    static uint8_t ramp[MAX_PTS];
    static uint8_t block[MAX_PTS];
    static uint8_t stair[MAX_PTS];
    static Piece   pieces[MAX_PIECES];
    static int32_t own[4 * MAX_PTS]; /* the band's own tiles, both of each cell and all four of a block */
    static V2      q[MAX_PTS];
    static float   rad[MAX_PTS], tlim[MAX_PTS];
    HwWalk         x;
    memset(&x, 0, sizeof x);
    x.m = m, x.c = c, x.mask_bit = mask_bit, x.comp = comp, x.col = col, x.row = row, x.ew = ew, x.sign = sign, x.seen = seen;
    x.pts = pts, x.ramp = ramp, x.block = block, x.stair = stair, x.pieces = pieces, x.own = own, x.q = q, x.rad = rad, x.tlim = tlim;
    x.cc = col, x.cr = row, x.cew = ew;
    x.dx = ew ? sign : 0, x.dy = ew ? 0 : sign; /* the walk runs either way along the band */
    x.table = -1;
    /*  The stages: the tiles, the ends, the fit, the overlay, the loft. */
    hw_walk(&x);
    if (x.n < 2)
        return 0;
    hw_ends(&x);
    if (hw_fit(&x) != 0)
        return 0;
    if (hw_overlay(&x) != 0)
        return 0;
    return hw_loft(&x);
}

/*  The on-ramps: 0x5D to 0x60, one id per direction.  Each is one tile
 *  beside a deck and beside a surface road, on two adjacent sides -- the
 *  save's neighbours say so, and it is spec 7.3's "one 1x1 tile adjacent to
 *  a deck edge, connected to a surface road".  In the shipped cities they
 *  flank each road that crosses under a deck, one on either side: a
 *  diamond.  The data does not grow (7.3, the hard constraint), so the
 *  ramp's form is derived on every build from what is free beside the deck,
 *  the way 7.3 sets out: which lane  right-hand traffic: the lane nearest
 *  the ramp runs so the ramp is on its right.  North of an east-west deck
 *  that is the westbound lane; east of a north-south one, the northbound.
 *  ON or OFF   the road is upstream of the ramp on that lane: traffic comes
 *  off the road and merges downstream -- an ON ramp.  The road is
 *  downstream: traffic leaves the deck upstream, descends, and turns onto
 *  the road -- an OFF ramp.  A diamond puts the OFF before the cross street
 *  and the ON after it, which is what the pairs are.  D           the
 *  free-air tiles along the deck's edge, in the ramp's own row, downstream
 *  for an ON ramp and upstream for an OFF, capped at eight.  Free is
 *  nothing built on it, no higher than the ramp's own ground. the form    D
 *  >= 4: PARALLEL.  The ramp runs along its row beside the deck, climbing
 *  over min(D - 2, 6) tiles, then turns in over two more: an S from the
 *  row's centre onto the deck's edge, tangent to it.  Half the spec's climb
 *  at 4 or 5 is its "short parallel".  D < 4: HELIX.  A spiral of one and a
 *  quarter turns in the ramp tile, turning toward the deck so its last
 *  quarter turn is the turn-in, and a short straight to the deck's edge.
 *  The spec's hairpin (D 2-3 with an upstream tile free) is not built yet:
 *  it takes the helix's place.  An OFF ramp is an ON ramp on the reversed
 *  lane, traversed backwards: built as one, then reversed, with the lift
 *  falling at its end instead of rising at its start.  A ramp at a band's
 *  END, feeding it head-on (nine across the corpus, in pairs), goes
 *  straight across.  A ramp is concrete from the road to the deck, and
 *  grades nothing. */

/*  How a ramp tile sits: which side its deck is on, which its road, the
 *  lane it serves by the right-hand rule and the direction of travel on
 *  it, and whether it is an OFF ramp (the road downstream) or ON.
 *  Answers 0 for a ramp with no deck beside it -- left to its sprite --
 *  and sets `eside` for one at a band's end, head-on. */
/*  Neighbour k of the ramp's tile: whether it is a deck tile, whether
 *  that deck's axis is the one this side lies on, and whether it carries
 *  a road. */
int hiway_orient_side(const OrientFan *o, int k, int *deck, int *axis, int *road)
{
    static const int32_t DC[4] = {0, 1, 0, -1}, DR[4] = {-1, 0, 1, 0}; /* N E S W */
    const RCity         *c     = (const RCity *)o->c;
    int32_t              nc = o->col + DC[k], nr = o->row + DR[k];
    uint8_t              nb;
    int                  ew;
    *deck = *axis = *road = 0;
    if (nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
        return 0;
    nb    = c->xbld[nr * R_MAP + nc];
    *deck = hiway_deck(nb, &ew) == 1;
    *axis = *deck && (ew ? (k == 0 || k == 2) : (k == 1 || k == 3));
    *road = net_road_on(nb);
    return 1;
}

/*  The sides the script settled on. */
void hiway_orient_answer(OrientFan *o, int kind, int dside, int rside, int eside, int off, int roads)
{
    o->kind  = kind;
    o->dside = dside;
    o->rside = rside;
    o->eside = eside;
    o->off   = off;
    o->roads = roads;
}

/*  Which side of an on-ramp's tile is the deck's and which the road's:
 *  arc.rules.ramp_orient reads them.  1 a ramp beside a deck, 2 a deck
 *  end-on, 0 neither. */
static int ramp_orient(const RCity *c, int32_t col, int32_t row, int *dside, int *rside, int *eside, V2 *along, V2 *toward, int *off, int *roads)
{
    static const int32_t DC[4] = {0, 1, 0, -1}, DR[4] = {-1, 0, 1, 0};
    OrientFan            o;
    memset(&o, 0, sizeof o);
    o.c     = c;
    o.col   = col;
    o.row   = row;
    o.dside = o.rside = o.eside = -1;
    script_rule_object("ramp_orient", "orient", &o);
    *dside = o.dside;
    *rside = o.rside;
    *eside = o.eside;
    *off   = o.off;
    *roads = o.roads;
    if (o.kind != 1)
        return o.kind;
    *toward = (V2){(float)DC[*dside], (float)DR[*dside]};
    /*  The map reaches the screen through a reflection (col down-left,
     *  row down-right), so the viewer's right hand is the map's left:
     *  a lane on the deck's south edge heads WEST as seen. */
    *along = *dside == 0 ? (V2){-1.0f, 0.0f} : *dside == 2 ? (V2){1.0f, 0.0f}
                                           : *dside == 1   ? (V2){0.0f, -1.0f}
                                                           : (V2){0.0f, 1.0f};
    if (*rside >= 0 && *rside != *dside && *rside != ((*dside + 2) & 3))
        *off = ((float)DC[*rside] * along->x + (float)DR[*rside] * along->y) > 0.0f;
    else
        *off = 0;
    return 1;
}

HwRamp s_hw_ramps[HW_MAX_RAMPS];
int    s_hw_nramps;
HwSt   s_hw_st[HW_MAX_ST];
int    s_hw_nst, s_hw_band;

/*  Deck-family tiles from a deck tile along a direction: the taper's
 *  room on that side, counted through curve blocks (a block's two tiles
 *  along the axis stand for the arc through it), stopping at anything
 *  else or at another ramp's deck tile. */
static int taper_room(const RCity *c, int32_t d0c, int32_t d0r, int32_t tvx, int32_t tvy)
{
    int L;
    for (L = 0; L < HIWAY_LANE_TAPER; ++L)
    {
        int32_t tc = d0c + tvx * (L + 1), tr = d0r + tvy * (L + 1);
        uint8_t tb;
        if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
            break;
        tb = c->xbld[tr * R_MAP + tc];
        if (!net_hiway_any(tb) || net_hiway_interchange(tb))
            break;
    }
    return L;
}

/*  The lane drop: every ramp beside a deck is listed for the loft and the
 *  ramp builder.  Its taper lies on the side away from its road when a road
 *  lies along the deck's axis (on the road's side the strip would run over
 *  the road); a road opposite the deck, or none, or one each way, leaves
 *  both sides open -- a ramp has no direction of its own in the data -- and
 *  the longer taper wins.  However short, the ramp is a strip. */
void hiway_lanes(const RCity *c)
{
    static const int32_t DC[4] = {0, 1, 0, -1}, DR[4] = {-1, 0, 1, 0};
    int32_t              col, row;
    s_hw_nramps = 0;
    for (row = 0; row < R_MAP; ++row)
        for (col = 0; col < R_MAP && s_hw_nramps < HW_MAX_RAMPS; ++col)
        {
            uint8_t b = c->xbld[row * R_MAP + col];
            int     dside, rside, eside, off, roads, along_roads = 0, k4, free_side, best = -1, best_off;
            V2      along, toward;
            int32_t d0c, d0r;
            HwRamp *rp;
            if (!net_hiway_onramp(b))
                continue;
            if (ramp_orient(c, col, row, &dside, &rside, &eside, &along, &toward, &off, &roads) != 1)
                continue;
            d0c = col + (int32_t)toward.x;
            d0r = row + (int32_t)toward.y;
            for (k4 = 0; k4 < 4; ++k4)
            {
                int32_t nc = col + DC[k4], nr = row + DR[k4];
                uint8_t nb;
                if (k4 == dside || k4 == ((dside + 2) & 3) || nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
                    continue;
                nb = c->xbld[nr * R_MAP + nc];
                if (net_road_on(nb))
                    ++along_roads;
            }
            /*  How much deck each way has, and which way the taper lies:
             *  arc.rules.ramp_side's. */
            free_side = along_roads != 1;
            {
                int room  = taper_room(c, d0c, d0r, off ? -(int32_t)along.x : (int32_t)along.x, off ? -(int32_t)along.y : (int32_t)along.y);
                int back  = free_side ? taper_room(c, d0c, d0r, off ? (int32_t)along.x : -(int32_t)along.x, off ? (int32_t)along.y : -(int32_t)along.y) : -1;
                int keep  = script_rule_ramp_side(free_side, room, back);
                best_off  = keep ? off : !off;
                best      = keep ? room : back;
            }
            rp         = &s_hw_ramps[s_hw_nramps++];
            rp->rc     = col;
            rp->rr     = row;
            rp->c0     = (V2){(float)d0c + 0.5f + toward.x * 0.5f, (float)d0r + 0.5f + toward.y * 0.5f};
            rp->along  = along;
            rp->toward = toward;
            rp->off    = best_off;
            rp->len    = best;
            rp->opp    = rside >= 0 && rside == ((dside + 2) & 3);
        }
    /*  Two ramps on one side of a deck whose tapers face each other --
     *  each with its road on its far side -- would run their strips into
     *  one another (Atlanta 72,88 and 78,88).  They share the tiles
     *  between them: each taper takes half, so the lane rises from one,
     *  runs as the deck's outer lane, and comes down to the other, which
     *  reads as one ramp with a merge lane into the highway. */
    {
        int i, j;
        for (i = 0; i < s_hw_nramps; ++i)
            for (j = i + 1; j < s_hw_nramps; ++j)
            {
                HwRamp *p = &s_hw_ramps[i], *q = &s_hw_ramps[j];
                V2      tvp = p->off ? (V2){-p->along.x, -p->along.y} : p->along;
                V2      tvq = q->off ? (V2){-q->along.x, -q->along.y} : q->along;
                float   dx = (float)(q->rc - p->rc), dy = (float)(q->rr - p->rr), gap, half;
                if (p->toward.x != q->toward.x || p->toward.y != q->toward.y)
                    continue; /* not the same side of a deck running the same way */
                if (fabsf(dx * p->toward.x + dy * p->toward.y) > 0.5f)
                    continue; /* not on one line along the deck */
                if (tvp.x * dx + tvp.y * dy <= 0.0f || tvq.x * dx + tvq.y * dy >= 0.0f)
                    continue;                                           /* not facing */
                gap  = fabsf(dx * p->along.x + dy * p->along.y) - 1.0f; /* tiles between the two deck tiles */
                half = (float)script_rule_ramp_share(gap, HIWAY_LANE_TAPER);
                if (half < 0.0f)
                    continue;
                if ((float)p->len > half)
                    p->len = (int)half;
                if ((float)q->len > half)
                    q->len = (int)half;
            }
    }
}

static int s_ramp_forms[6]; /* head-on, -, short taper, -, orphan, lane drop */

void ramp_stats(void)
{
    if (s_ramp_forms[0] + s_ramp_forms[2] + s_ramp_forms[4] + s_ramp_forms[5] == 0)
        return;
    dumpf("on-ramps     %d lane drops (%d with a taper under two tiles), %d head-on, %d with no deck beside them\n", s_ramp_forms[5], s_ramp_forms[2], s_ramp_forms[0], s_ramp_forms[4]);
}

/*  One ramp's working state, handed to the stages below so each can be
 *  read on its own: the ramp tile, the deck beside it, the road it
 *  joins and how, the two poses, the pieces. */
typedef struct
{
    RMesh             *m;
    const RCity       *c;
    uint8_t            mask_bit;
    int                comp;
    int32_t            col, row;
    Piece             *pc;
    const HwRamp      *rp;
    const RAtlasLevel *l;
    int                road_port; /* the junction port the road end is, or -1 */
    float              taper;     /* the loft eases the lane down to the road over this much of its end: the road part and a little of the ramp tile; 0 when the ramp ends at a port */
    float              merge;     /* how far along the road the join slid, tiles; -1 for the two legs through the foot */
    int                iref, side, sgn, band, fork, np, np1, np2, deck_lane, road_lane, flat, form, off, lane_off, how, roads, k, q, r;
    int                dside, rside, eside;
    float              ds, s_top, s_foot, total_len, rmin, z0, climb, total, best;
    V2                 rd, mdir, A, tA, F, tF, B, tB, along, toward;
} Ramp;

/*  The lane drop this tile is, and the deck station beside it: none, and the tile is not a ramp of this kind. */
static int ramp_find(Ramp *x)
{
    int32_t       col  = x->col;
    int32_t       row  = x->row;
    const HwRamp *rp   = x->rp;
    int           iref = x->iref;
    int           k;
    int           r;
    float         best = x->best;
    for (r = 0; r < s_hw_nramps; ++r)
        if (s_hw_ramps[r].rc == col && s_hw_ramps[r].rr == row)
            rp = &s_hw_ramps[r];
    if (!rp)
    {
        if (s_pass != 1)
            ++s_ramp_forms[4];
        s_ramp_lost[(x->row) * R_MAP + (x->col)] = 1;
        if (g_dev.lane_dump && s_pass != 1)
            dumpf("RAMP %d,%d lost: %s\n", (int)x->col, (int)x->row, "no ramp record for the tile");
        return 1; /* not this tile */
    }
    float nearest = 1e9f; /* for the lane dump: how far the nearest station was when none was within reach */
    for (k = 0; k < s_hw_nst; ++k)
    {
        float d = v2len((V2){s_hw_st[k].pos.x - rp->c0.x, s_hw_st[k].pos.y - rp->c0.y});
        if (d < nearest)
            nearest = d;
        if (d < best)
        {
            best = d;
            iref = k;
        }
    }
    if (iref < 0)
    {
        if (s_pass != 1)
            ++s_ramp_forms[4];
        s_ramp_lost[(x->row) * R_MAP + (x->col)] = 2;
        if (g_dev.lane_dump && s_pass != 1)
            dumpf("RAMP %d,%d lost: no deck station within %.1f of c0 %.2f,%.2f (nearest %.2f of %d stations)\n", (int)x->col, (int)x->row, (double)s_tune.hiway_reach, (double)rp->c0.x, (double)rp->c0.y, (double)nearest, s_hw_nst);
        return 1; /* no deck lofted beside it after all */
    }
    x->rp   = rp;
    x->iref = iref;
    x->k    = k;
    x->r    = r;
    x->best = best;
    return 0;
}

/*  What lies beyond the road tile: straight on (3), a through road across (2), a stub (1), neither (0); and the way the road lane runs from the ramp. */
static int ramp_classify(Ramp *x)
{
    const RCity  *c      = x->c;
    int32_t       col    = x->col;
    int32_t       row    = x->row;
    const HwRamp *rp     = x->rp;
    int           side   = x->side;
    int           fork;
    int           off    = x->off;
    V2            rd;
    V2            mdir   = x->mdir;
    V2            along  = x->along;
    V2            toward = x->toward;
    /*  Does the road run across the deck's axis (the usual
     *  crossing, or a stub ending at the ramps)?  Then the ramp
     *  forks off it with a curve; a road running along the
     *  axis is met end-on at the tile's edge as before. */
    {
        static const int32_t DC2[4] = {0, 1, 0, -1}, DR2[4] = {-1, 0, 1, 0};
        V2                   pp; /* the road's axis: across the way to the road tile */
        int                  kt;
        int                  ks;
        /*  Toward the road tile: along the deck for a lane drop, and
         *  across the ramp tile, away from the deck, when the road
         *  lies opposite it -- that ramp is joined like the others
         *  (Atlanta 97,69 and 51,70 had ended on no lane). */
        rd   = rp->opp ? (V2){-rp->toward.x, -rp->toward.y} : rp->off ? rp->along
                                                                      : (V2){-rp->along.x, -rp->along.y};
        pp   = (V2){rd.y, -rd.x};
        kt   = (int)lroundf(pp.x) == 0 ? ((int)lroundf(pp.y) < 0 ? 0 : 2) : ((int)lroundf(pp.x) > 0 ? 1 : 3);
        ks   = (int)lroundf(rd.x) == 0 ? ((int)lroundf(rd.y) < 0 ? 0 : 2) : ((int)lroundf(rd.x) > 0 ? 1 : 3);
        fork = 0;
        if (col + DC2[ks] >= 0 && row + DR2[ks] >= 0 && col + DC2[ks] < R_MAP && row + DR2[ks] < R_MAP)
        {
            /*  What lies beyond the road tile, each way along the
             *  toward axis: a road or a deck-over-road crossing is
             *  road; plain deck is the end of it.  The road tile's
             *  own links cannot say -- a stub between two ramps is
             *  a four-way piece in the data. */
            int32_t tc = col + DC2[ks], tr = row + DR2[ks];
            int     in = 0, away = 0, k5;
            for (k5 = 0; k5 < 2; ++k5)
            {
                int32_t nc = tc + DC2[k5 ? (kt + 2) & 3 : kt], nr = tr + DR2[k5 ? (kt + 2) & 3 : kt];
                uint8_t nb;
                if (nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
                    continue;
                nb = c->xbld[nr * R_MAP + nc];
                if (net_road_near(nb))
                    *(k5 ? &away : &in) = 1;
            }
            {
                /*  A road in the road tile's far side along rd carries the
                 *  ramp's lane through without a turn.  What the three
                 *  answers make of it is arc.rules.ramp_fork's. */
                int32_t sc = tc + DC2[ks], sr = tr + DR2[ks];
                int     straight = 0;
                if (sc >= 0 && sr >= 0 && sc < R_MAP && sr < R_MAP)
                    straight = net_road_near(c->xbld[sr * R_MAP + sc]);
                fork = script_rule_ramp_fork(straight, in, away);
            }
            mdir = away ? (V2){-pp.x, -pp.y} : pp; /* the lane's direction away from the ramp on the road */
        }
    }
    x->rp     = rp;
    x->side   = side;
    x->fork   = fork;
    x->off    = off;
    x->rd     = rd;
    x->mdir   = mdir;
    x->along  = along;
    x->toward = toward;
    return 0;
}

/*  The band, the side the ramp lies on, and the gore and foot along the deck. */
static int ramp_geometry(Ramp *x)
{
    int32_t       col       = x->col;
    int32_t       row       = x->row;
    const HwRamp *rp        = x->rp;
    int           iref      = x->iref;
    int           side;
    int           sgn;
    int           band;
    int           off       = x->off;
    float         ds;
    float         s_top;
    float         s_foot;
    float         total_len;
    V2            along     = x->along;
    band                    = s_hw_st[iref].band;
    side                    = (((float)col + 0.5f - s_hw_st[iref].pos.x) * s_hw_st[iref].dir.y - ((float)row + 0.5f - s_hw_st[iref].pos.y) * s_hw_st[iref].dir.x) > 0.0f ? 1 : -1;
    sgn                     = s_hw_st[iref].dir.x * rp->along.x + s_hw_st[iref].dir.y * rp->along.y > 0.0f ? 1 : -1;
    /*  Where the descent runs along the band: arc.rules.ramp_span's. */
    script_rule_ramp_span(s_hw_st[iref].s, rp->len, rp->off, sgn, &s_top, &s_foot, &total_len, &ds);
    x->rp        = rp;
    x->iref      = iref;
    x->side      = side;
    x->sgn       = sgn;
    x->band      = band;
    x->off       = off;
    x->ds        = ds;
    x->s_top     = s_top;
    x->s_foot    = s_foot;
    x->total_len = total_len;
    x->along     = along;
    return 0;
}

/*  The two poses of the descent: A on the deck's outer lane at the gore, F at the ramp tile's road edge -- snapped onto the recorded deck lane. */
/*  The deck's pose at an arc length along a band: interpolated between
 *  the two recorded stations either side of it.  0 when the band has
 *  no stations at all. */
static int hiway_station_at(int band, float s_at, V2 *pos, V2 *dir)
{
    const HwSt *p0 = NULL, *p1 = NULL;
    float       u;
    int         q;
    for (q = 0; q < s_hw_nst; ++q)
    {
        const HwSt *st = &s_hw_st[q];
        if (st->band != band)
            continue;
        if (st->s <= s_at + 1e-4f && (!p0 || st->s > p0->s))
            p0 = st;
        if (st->s >= s_at - 1e-4f && (!p1 || st->s < p1->s))
            p1 = st;
    }
    if (!p0)
        p0 = p1;
    if (!p1)
        p1 = p0;
    if (!p0)
        return 0;
    u    = p1->s > p0->s + 1e-6f ? (s_at - p0->s) / (p1->s - p0->s) : 0.0f;
    *pos = (V2){p0->pos.x + (p1->pos.x - p0->pos.x) * u, p0->pos.y + (p1->pos.y - p0->pos.y) * u};
    *dir = (V2){p0->dir.x + (p1->dir.x - p0->dir.x) * u, p0->dir.y + (p1->dir.y - p0->dir.y) * u};
    {
        float dl = v2len(*dir);
        if (dl < 1e-6f)
            *dir = p0->dir;
        else
            *dir = (V2){dir->x / dl, dir->y / dl};
    }
    return 1;
}

/*  The foot F: the middle of the ramp tile's road-side edge, heading at the
 *  road -- or, with the road opposite the deck, the far edge across the
 *  tile; a through road (fork 2) is met at its side at an angle, thirty
 *  degrees off its line toward the lane the ramp merges into. */
static void ramp_foot_pose(const Ramp *x, V2 *F, V2 *tF)
{
    const HwRamp *rp   = x->rp;
    int32_t       col  = x->col;
    int32_t       row  = x->row;
    int           fork = x->fork;
    if (rp->opp)
    {
        /* across the ramp tile to the road on its far side */
        *F  = (V2){(float)col + 0.5f - rp->toward.x * 0.5f, (float)row + 0.5f - rp->toward.y * 0.5f};
        *tF = (V2){-rp->toward.x, -rp->toward.y};
    }
    else
    {
        *F  = rp->off ? (V2){(float)col + 0.5f + rp->along.x * 0.5f, (float)row + 0.5f + rp->along.y * 0.5f}
                      : (V2){(float)col + 0.5f - rp->along.x * 0.5f, (float)row + 0.5f - rp->along.y * 0.5f};
        *tF = rp->off ? rp->along : (V2){-rp->along.x, -rp->along.y}; /* at the road, square on */
        if (fork == 2)
        {
            /*  A through road is met at its side at an angle off its line,
             *  toward the lane the ramp merges into. */
            V2 dm = {tF->y, -tF->x}; /* the road direction whose right-hand lane is on the ramp's side */
            /*  Built deck-to-road: an OFF ramp's traffic runs with
             *  the lane here, an ON ramp's against it, since the
             *  ramp is reversed afterwards (Atlanta 103,114 had
             *  begun against the westbound lane's flow). */
            float ca = net_family_rules(F_ROAD)->ramp_meet_cos, sa = net_family_rules(F_ROAD)->ramp_meet_sin, tr = rp->off ? 1.0f : -1.0f;
            *tF = (V2){dm.x * ca * tr + tF->x * sa, dm.y * ca * tr + tF->y * sa};
        }
    }
}

static int ramp_poses(Ramp *x)
{
    const HwRamp *rp        = x->rp;
    int           side      = x->side;
    int           sgn       = x->sgn;
    int           band      = x->band;
    int           fork      = x->fork;
    int           np1       = x->np1;
    int           np2       = x->np2;
    int           deck_lane;
    int           road_lane = x->road_lane;
    int           off       = x->off;
    int           r         = x->r;
    float         s_top     = x->s_top;
    float         rmin      = x->rmin;
    V2            A;
    V2            tA;
    V2            F         = x->F;
    V2            tF        = x->tF;
    V2            B         = x->B;
    V2            tB        = x->tB;
    V2            along     = x->along;
    V2            toward    = x->toward;
    /*  The ramp as ONE ROUTED LANE (lane.c, stage 2 of the lane
     *  primitives), lofted below by the same loft() as a road strip.  Two
     *  legs, each the router's biarc between two poses: the descent, from
     *  the deck's outer lane at the gore's near end (A, heading the deck's
     *  way) to the foot F -- the middle of the ramp tile's road-side edge,
     *  heading at the road; and the join, from F into the road: a STUB's
     *  lane on the ramp's side, a quarter of the road's width off its
     *  centreline, out to the road tile's far edge -- the road's lanes are
     *  the ramps' -- or a THROUGH road's side, a short way along it at the
     *  merge.  A road opposite the deck: the descent runs to the ramp
     *  tile's far edge instead.  Built deck-to-road; an ON ramp is reversed
     *  afterwards. */
    {
        const float o = net_family_rules(F_ROAD)->ramp_outer * s_tune.hiway_w; /* the outer lane's centre, across the deck */
        V2          pos, dir;
        if (!hiway_station_at(band, s_top, &pos, &dir))
        {
            if (s_pass != 1)
                ++s_ramp_forms[4];
            s_ramp_lost[(x->row) * R_MAP + (x->col)] = 3;
            if (g_dev.lane_dump && s_pass != 1)
                dumpf("RAMP %d,%d lost: %s\n", (int)x->col, (int)x->row, "no deck station at the top of the descent");
            return 1; /* not this tile */
        }
        A = (V2){pos.x + (float)side * dir.y * o, pos.y - (float)side * dir.x * o};
        /*  Both legs are built deck-to-road, so the deck end heads
         *  AWAY from the gore along the deck: with the travel
         *  direction (the deck's own way) an ON ramp's biarc had
         *  to double back and hooked at the deck (r 0.26 at
         *  Atlanta 87,80).  An OFF ramp travels this way; an ON
         *  ramp is reversed below. */
        tA = rp->off ? (V2){dir.x * (float)sgn, dir.y * (float)sgn} : (V2){-dir.x * (float)sgn, -dir.y * (float)sgn};
        ramp_foot_pose(x, &F, &tF);
    }
    /*  The ends ON THE RECORDED LANES: the deck's outer lane of
     *  this band at A, the road's lane at the join's end (or at
     *  the foot when there is no join), each snapped to the
     *  nearest station running the ramp's way.  A ramp whose
     *  end finds no lane keeps its computed pose and is
     *  counted (the lanes line: "ramp ends on no lane"). */
    {
        V2    pos, d2, trav = rp->off ? tA : (V2){-tA.x, -tA.y};
        float dist;
        deck_lane = lane_nearest(A, trav, LANE_CLS_DECK, band, net_family_rules(F_ROAD)->ramp_snap, &pos, &d2, &dist);
        if (deck_lane >= 0)
        {
            A  = pos;
            tA = rp->off ? d2 : (V2){-d2.x, -d2.y};
        }
    }
    x->rp        = rp;
    x->side      = side;
    x->sgn       = sgn;
    x->band      = band;
    x->fork      = fork;
    x->np1       = np1;
    x->np2       = np2;
    x->deck_lane = deck_lane;
    x->road_lane = road_lane;
    x->off       = off;
    x->r         = r;
    x->s_top     = s_top;
    x->rmin      = rmin;
    x->A         = A;
    x->tA        = tA;
    x->F         = F;
    x->tF        = tF;
    x->B         = B;
    x->tB        = tB;
    x->along     = along;
    x->toward    = toward;
    return 0;
}

/*  The descent, routed. */
static int ramp_route(Ramp *x)
{
    Piece *pc   = x->pc;
    int    np;
    int    np1  = x->np1;
    float  rmin = x->rmin;
    V2     A    = x->A;
    V2     tA   = x->tA;
    V2     F    = x->F;
    V2     tF   = x->tF;
    if (lane_route(A, tA, F, tF, pc, &np1, &rmin) != 0 || np1 < 1)
    {
        if (s_pass != 1)
            ++s_ramp_forms[4];
        s_ramp_lost[(x->row) * R_MAP + (x->col)] = 4;
        if (g_dev.lane_dump && s_pass != 1)
            dumpf("RAMP %d,%d lost: %s\n", (int)x->col, (int)x->row, "the descent could not be routed");
        return 1; /* not this tile */
    }
    np      = np1;
    x->np   = np;
    x->np1  = np1;
    x->rmin = rmin;
    x->A    = A;
    x->tA   = tA;
    x->F    = F;
    x->tF   = tF;
    return 0;
}

/*  The join into the road: straight on, into a stub's lane, or merging into a through road's near lane -- snapped onto the recorded road lane. */
/*  Where the join meets the road, by the fork's kind: straight on
 *  (3), the road's lane on the ramp's side out to the far edge (1), or
 *  the through road's near lane at the merge (else).  The pose B/tB is
 *  the construction's, deck-to-road; `trav` the lane's own way, for the
 *  lookup of the recorded lane there. */
static void ramp_join_target(const Ramp *x, V2 *B, V2 *tB, V2 *trav)
{
    const HwRamp *rp   = x->rp;
    int           fork = x->fork;
    float         lo   = s_tune.road_w * net_family_rules(F_ROAD)->ramp_lane_off;
    V2            rd   = x->rd;
    V2            mdir = x->mdir;
    V2            F    = x->F;
    if (fork == 3)
    {
        /*  Straight on: the road's lane on the right hand of the
         *  ramp's travel, out to the road tile's far edge, no
         *  turn at all. */
        V2 cr = {F.x + rd.x * 0.5f, F.y + rd.y * 0.5f}; /* the road tile's centre */
        V2 tv = rp->off ? rd : (V2){-rd.x, -rd.y};      /* the ramp's travel on the road */
        V2 rt = {tv.y, -tv.x};                          /* its right hand */
        *B    = (V2){cr.x + rt.x * lo + rd.x * 0.5f, cr.y + rt.y * lo + rd.y * 0.5f};
        *tB   = rd; /* construction: on along the road */
        *trav = tv;
    }
    else if (fork == 1)
    {
        /*  The road's lane on the ramp's side, out to the road tile's far
         *  edge.  An OFF ramp's traffic runs away from the deck (mdir) and
         *  takes the lane on that direction's right hand; an ON ramp's runs
         *  toward it and takes the other lane: the side is the lane's own,
         *  not the ramp tile's. */
        V2 cr = {F.x + rd.x * 0.5f, F.y + rd.y * 0.5f};  /* the road tile's centre */
        V2 tv = rp->off ? mdir : (V2){-mdir.x, -mdir.y}; /* the ramp's travel on the road */
        V2 rt = {tv.y, -tv.x};                           /* its right hand */
        *B    = (V2){cr.x + rt.x * lo + mdir.x * 0.5f, cr.y + rt.y * lo + mdir.y * 0.5f};
        *tB   = mdir; /* construction: away from the deck */
        *trav = tv;
    }
    else
    {
        /*  Into the through road's NEAR lane -- the one on the ramp's side,
         *  running the way whose right hand it is (lane.c's sides: the
         *  viewer's right of d is (d.y, -d.x)) -- a quarter of the road's
         *  width off the centreline, most of half a tile along, heading
         *  with it: the merge.  A run onto the road's surface had ended on
         *  no lane at all. */
        V2 cr = {F.x + rd.x * 0.5f, F.y + rd.y * 0.5f}; /* the road tile's centre */
        /*  Downstream of the foot for an OFF ramp, which merges
         *  in; upstream for an ON ramp, which peels off, with the
         *  construction heading against its travel. */
        V2    dm = {rd.y, -rd.x};
        float tr = rp->off ? 1.0f : -1.0f;
        *tB      = (V2){dm.x * tr, dm.y * tr};
        {
            const float al = net_family_rules(F_ROAD)->ramp_merge_along;
            *B             = (V2){cr.x - rd.x * lo + dm.x * al * tr, cr.y - rd.y * lo + dm.y * al * tr};
        }
        *trav    = dm; /* the lane's own way, for the lookup */
    }
}

/*  Whether a route leaves the ramp tile across its road edge -- the
 *  edge in direction `rd` from the tile's centre -- and the length of
 *  the route beyond that crossing.  The ramp tile is the hard rule: a
 *  lane that misses that edge is not this ramp's. */
static int ramp_exits(const Piece *pc, int np, V2 centre, V2 rd, float *beyond)
{
    float total = 0.0f, run = 0.0f, ps = 0.0f, pl = 0.0f;
    int   k, first = 1;
    for (k = 0; k < np; ++k)
        total += pc[k].len;
    for (k = 0; k < np; ++k)
    {
        float t;
        for (t = 0.0f;; t += 0.05f)
        {
            V2    pos, dir;
            float side, lat;
            if (t > pc[k].len)
                t = pc[k].len;
            piece_at(&pc[k], t, &pos, &dir);
            side = (pos.x - centre.x) * rd.x + (pos.y - centre.y) * rd.y - 0.5f;
            lat  = (pos.x - centre.x) * rd.y - (pos.y - centre.y) * rd.x;
            if (!first && side > 0.0f && ps <= 0.0f)
            {
                float f = ps / (ps - side), l = pl + (lat - pl) * f;
                if (fabsf(l) > 0.5f)
                    return 0; /* out past the tile's corner, not across its road edge */
                *beyond = total - (run + t);
                return 1;
            }
            first = 0, ps = side, pl = lat;
            if (t >= pc[k].len)
                break;
        }
        run += pc[k].len;
    }
    return 0;
}

/*  The join as a slide.  The route runs from the deck pose to the road's
 *  lane, and where it meets the lane slides along it, out to `ramp_merge`
 *  tiles from the aimed point, while the lane there is still the one aimed
 *  at; and the deck pose may first run straight, a parallel lane beside the
 *  deck, up to half a tile short of the road's lane line, as a real ramp
 *  runs before it turns (Atlanta 108,35, its gore four tiles from the tile:
 *  every route that turned at once reached the road under the deck's
 *  crossing).  A candidate must leave the ramp tile across its road edge.
 *  The widest tightest arc wins, the shortest run and slide among equals:
 *  one arc where the deck crosses the road, an easy S where it runs beside
 *  it.  Returns 1 with the pieces, the tightest radius, the end pose, the
 *  slide, and the taper (the road part and a little of the ramp tile, as
 *  the two-leg join's was). */
/*  One placing: the descent starting `u` along the deck and the join
 *  sitting `at` along the road.  The radius the route holds, or why it
 *  cannot be had -- "off" the lane it aimed at, which ends the slide
 *  along the road, or "unroutable". */
const char *hiway_slide_route(SlideFan *s, float u, float at, float *r)
{
    const Ramp *x = (const Ramp *)s->ramp;
    V2          Q = {x->A.x + x->tA.x * u, x->A.y + x->tA.y * u};
    V2          P = {s->B0.x + s->tB0.x * at, s->B0.y + s->tB0.y * at}, pos, d2, tb;
    float       dist;
    int         n = 0;
    s->r          = 1e9f;
    if (lane_nearest_outer(P, s->trav, LANE_CLS_ROAD, s->snap, &pos, &d2, &dist) != s->lane)
        return "off";
    tb = x->rp->off ? d2 : (V2){-d2.x, -d2.y}; /* construction: against an ON ramp's travel */
    if (u > 0.0f)
    {
        s->tmp[0].arc = 0;
        s->tmp[0].a   = x->A;
        s->tmp[0].b   = Q;
        s->tmp[0].len = u;
    }
    if (lane_route(Q, x->tA, pos, tb, s->tmp + (u > 0.0f), &n, &s->r) != 0 || n < 1 || n + (u > 0.0f) > MAX_PIECES)
        return "unroutable";
    s->n   = n + (u > 0.0f);
    s->pos = pos;
    s->dir = tb;
    s->at  = at;
    *r     = s->r;
    return NULL;
}

/*  Does the placing leave by the ramp tile's own road edge?  The ramp
 *  tile is the hard rule: a lane that misses that edge is not this
 *  ramp's. */
int hiway_slide_exits(SlideFan *s)
{
    const Ramp *x      = (const Ramp *)s->ramp;
    V2          centre = {(float)x->col + 0.5f, (float)x->row + 0.5f};
    return ramp_exits(s->tmp, s->n, centre, x->rd, &s->beyond);
}

/*  The placing kept as the best so far. */
void hiway_slide_keep(SlideFan *s, float taper)
{
    s->best       = s->r;
    *s->np        = s->n;
    *s->rmin      = s->r;
    *s->B         = s->pos;
    *s->tB        = s->dir;
    *s->out_merge = s->at;
    *s->out_taper = s->beyond + taper;
    memcpy(s->pc, s->tmp, sizeof(Piece) * (size_t)s->n);
}

/*  Why no placing was found, for --lane-dump. */
void hiway_slide_note(const SlideFan *s, int tried, int off, int unroutable, int missed)
{
    const Ramp *x = (const Ramp *)s->ramp;
    if (g_dev.lane_dump && s_pass != 1)
        dumpf("RAMP %d,%d slide: none of %d candidates (%d off the lane, %d unroutable, %d miss the road edge)\n", (int)x->col, (int)x->row, tried, off, unroutable, missed);
}

/*  The join slid along the deck and along the road: arc.rules.ramp_slide
 *  walks the placings. */
static int ramp_slide(const Ramp *x, int lane, V2 B0, V2 tB0, V2 trav, Piece *pc, int *np, float *rmin, V2 *B, V2 *tB, float *merge, float *taper)
{
    static Piece tmp[MAX_PIECES];
    SlideFan     s;
    V2           nB  = {-tB0.y, tB0.x};
    float        den = x->tA.x * nB.x + x->tA.y * nB.y;
    memset(&s, 0, sizeof s);
    s.ramp      = (void *)x;
    s.lane      = lane;
    s.B0        = B0;
    s.tB0       = tB0;
    s.trav      = trav;
    s.parallel  = fabsf(den) <= 1e-3f;
    s.reach     = s.parallel ? 0.0f : ((B0.x - x->A.x) * nB.x + (B0.y - x->A.y) * nB.y) / den;
    s.merge     = s_tune.ramp_merge;
    s.snap      = net_family_rules(F_ROAD)->ramp_snap;
    s.taper     = net_family_rules(F_ROAD)->ramp_taper;
    s.tmp       = tmp;
    s.pc        = pc;
    s.np        = np;
    s.rmin      = rmin;
    s.B         = B;
    s.tB        = tB;
    s.out_merge = merge;
    s.out_taper = taper;
    script_rule_object("ramp_slide", "slide", &s);
    return s.best > 0.0f;
}

static int ramp_join(Ramp *x)
{
    float              taper     = 0.0f, merge = -1.0f;
    const RCity       *c         = x->c;
    const RAtlasLevel *l         = x->l;
    int                road_port = x->road_port;
    Piece             *pc        = x->pc;
    const HwRamp      *rp        = x->rp;
    int                side      = x->side;
    int                fork      = x->fork;
    int                np        = x->np;
    int                np1       = x->np1;
    int                np2       = x->np2;
    int                road_lane;
    int                off       = x->off;
    float              rmin      = x->rmin;
    V2                 rd        = x->rd;
    V2                 mdir      = x->mdir;
    V2                 A         = x->A;
    V2                 F         = x->F;
    V2                 tF        = x->tF;
    V2                 B         = x->B;
    V2                 tB        = x->tB;
    V2                 along     = x->along;
    V2                 toward    = x->toward;
    /*  The road tile is a JUNCTION: the ramp is one of its arms, with a
     *  one-way port the junction's connectors feed (an ON ramp) or drain
     *  (an OFF ramp), so the ramp ends at the foot -- the tile edge -- and
     *  no join of its own runs on into the box.  A real junction, a stub
     *  ending in the ramps, or a bend with the ramp straight ahead of one
     *  arm takes the ramp as an arm of the box (lane_ramp_arm); a road
     *  passing the ramp at its side does not, and there its near lane
     *  alone meets the ramp, below. */
    int32_t jc0 = (int32_t)floorf(F.x + rd.x * 0.5f), jr0 = (int32_t)floorf(F.y + rd.y * 0.5f), ej0;
    for (ej0 = 0; ej0 < 4; ++ej0)
        if ((int)lroundf(ROAD_DU[ej0]) == -(int)lroundf(rd.x) && (int)lroundf(ROAD_DV[ej0]) == -(int)lroundf(rd.y))
            break;
    if (node_kind(c, l, F_ROAD, jc0, jr0) == 2 && ej0 < 4 && lane_ramp_arm(c, jc0, jr0, tile_links(c, l, jc0, jr0, F_ROAD), ej0))
    {
        int32_t jc = jc0, jr = jr0, ej = ej0;
        road_port = lane_port_id(jc, jr, ej, rp->off ? 0 : 1, 0);
        fork      = 0;
    }
    if (fork)
    {
        V2 trav = {0.0f, 0.0f}; /* the road lane's own direction of travel at the join */
        ramp_join_target(x, &B, &tB, &trav);
        {
            /* the road's own lane there, travelling the join's way */
            V2    pos, d2;
            float dist;
            /*  The kerb-side lane; inside the crossing piece's box, where
             *  the road's lanes are its straight connectors, the nearest of
             *  those. */
            road_lane = lane_nearest_outer(B, trav, LANE_CLS_ROAD, net_family_rules(F_ROAD)->ramp_snap, &pos, &d2, &dist);
            if (road_lane < 0)
                road_lane = lane_nearest(B, trav, LANE_CLS_TURN, -1, net_family_rules(F_ROAD)->ramp_snap, &pos, &d2, &dist);
            if (road_lane >= 0)
            {
                B  = pos;
                tB = rp->off ? d2 : (V2){-d2.x, -d2.y}; /* construction: against an ON ramp's travel */
            }
        }
        /*  ONE LANE from the deck to the road: the route runs from the deck
         *  pose to the road's lane, and where it meets the lane slides
         *  along it to wherever the tightest arc is widest (ramp_slide).
         *  The foot stays the tile logic's anchor; the two legs through it,
         *  which turned inside half a tile, are the fallback when no slide
         *  leaves the ramp tile across its road edge. */
        if (road_lane >= 0 && ramp_slide(x, road_lane, B, tB, trav, pc, &np1, &rmin, &B, &tB, &merge, &taper))
        {
            np2 = 0;
            np  = np1;
        }
        else if (np1 < MAX_PIECES && lane_route(F, tF, B, tB, pc + np1, &np2, &rmin) == 0)
        {
            int q2;
            np    = np1 + np2;
            taper = net_family_rules(F_ROAD)->ramp_taper; /* the join and a little of the descent */
            for (q2 = np1; q2 < np; ++q2)
                taper += pc[q2].len;
        }
    }
    else
    {
        /* no join: the road's lane at the foot itself, if one runs there */
        V2    pos, d2;
        float dist;
        road_lane = lane_nearest_outer(F, tF, LANE_CLS_ROAD, net_family_rules(F_ROAD)->ramp_snap, &pos, &d2, &dist);
        if (road_lane < 0)
            road_lane = lane_nearest(F, tF, LANE_CLS_TURN, -1, net_family_rules(F_ROAD)->ramp_snap, &pos, &d2, &dist);
    }
    x->rp        = rp;
    x->side      = side;
    x->fork      = fork;
    x->np        = np;
    x->np1       = np1;
    x->np2       = np2;
    x->road_lane = road_lane;
    x->off       = off;
    x->rmin      = rmin;
    x->rd        = rd;
    x->mdir      = mdir;
    x->A         = A;
    x->F         = F;
    x->tF        = tF;
    x->B         = B;
    x->tB        = tB;
    x->along     = along;
    x->toward    = toward;
    x->road_port = road_port;
    x->taper     = taper;
    x->merge     = merge;
    return 0;
}

/*  An ON ramp was built deck-to-road: reversed into travel order. */
static int ramp_reverse(Ramp *x)
{
    Piece        *pc  = x->pc;
    const HwRamp *rp  = x->rp;
    int           np  = x->np;
    int           off = x->off;
    int           q   = x->q;
    if (!rp->off)
    {
        /* built top-down; an ON ramp is travelled from the road up: reverse */
        for (q = 0; q < np / 2; ++q)
        {
            Piece t        = pc[q];
            pc[q]          = pc[np - 1 - q];
            pc[np - 1 - q] = t;
        }
        for (q = 0; q < np; ++q)
        {
            V2 t    = pc[q].a;
            pc[q].a = pc[q].b;
            pc[q].b = t;
            if (pc[q].arc)
            {
                float t2 = pc[q].t0;
                pc[q].t0 = pc[q].t1;
                pc[q].t1 = t2;
            }
        }
    }
    x->rp  = rp;
    x->np  = np;
    x->off = off;
    x->q   = q;
    return 0;
}

/*  The ramp as a lane of its own, its dump, and what the loft is told. */
static int ramp_finish(Ramp *x)
{
    int           road_port = x->road_port;
    RMesh        *m         = x->m;
    const RCity  *c         = x->c;
    uint8_t       mask_bit  = x->mask_bit;
    int32_t       col       = x->col;
    int32_t       row       = x->row;
    Piece        *pc        = x->pc;
    const HwRamp *rp        = x->rp;
    int           side      = x->side;
    int           sgn       = x->sgn;
    int           fork      = x->fork;
    int           np        = x->np;
    int           np1       = x->np1;
    int           np2       = x->np2;
    int           deck_lane = x->deck_lane;
    int           road_lane = x->road_lane;
    int           flat;
    int           form;
    int           off;
    int           lane_off;
    float         z0;
    V2            A         = x->A;
    V2            tA        = x->tA;
    V2            F         = x->F;
    V2            tF        = x->tF;
    V2            along     = x->along;
    V2            toward    = x->toward;
    if (lane_ramp(m, c, mask_bit, pc, np, RAMP_HW, deck_lane, road_lane, road_port, rp->off) != 0)
        return -1;
    if (g_dev.lane_dump && s_pass != 1)
    {
        dumpf("LANE ramp %d,%d %s fork %d opp %d len %d: c0 %.2f,%.2f along %.2f,%.2f toward %.2f,%.2f side %d sgn %d | A %.3f,%.3f "
              "heading %.2f,%.2f | F %.3f,%.3f heading %.2f,%.2f | %d+%d pieces\n",
              (int)col,
              (int)row,
              rp->off ? "OFF" : "ON",
              fork,
              rp->opp,
              rp->len,
              (double)rp->c0.x,
              (double)rp->c0.y,
              (double)rp->along.x,
              (double)rp->along.y,
              (double)rp->toward.x,
              (double)rp->toward.y,
              side,
              sgn,
              (double)A.x,
              (double)A.y,
              (double)tA.x,
              (double)tA.y,
              (double)F.x,
              (double)F.y,
              (double)tF.x,
              (double)tF.y,
              np1,
              np2);
        dumpf("    deck lane %d, road lane %d, merge %.1f, taper %.2f\n", deck_lane, road_lane, (double)x->merge, (double)x->taper);
        lane_dump_pieces(pc, np);
    }
    flat     = 1;
    form     = 5;
    off      = rp->off;
    lane_off = off;
    z0       = HIWAY_LIFT;
    if (rp->len < 2 && s_pass != 1)
        ++s_ramp_forms[2];
    off          = 0; /* in travel order already: no lift to fall */
    x->rp        = rp;
    x->side      = side;
    x->sgn       = sgn;
    x->fork      = fork;
    x->np        = np;
    x->np1       = np1;
    x->np2       = np2;
    x->deck_lane = deck_lane;
    x->road_lane = road_lane;
    x->flat      = flat;
    x->form      = form;
    x->off       = off;
    x->lane_off  = lane_off;
    x->z0        = z0;
    x->A         = A;
    x->tA        = tA;
    x->F         = F;
    x->tF        = tF;
    x->along     = along;
    x->toward    = toward;
    return 0;
}

static int build_ramps(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp)
{
    /*  The grading pass builds the ramps too: a ramp grades no shelf, but
     *  its loft caps the terrain field under it, and the field is
     *  smoothed as a whole -- skipping them moved a road's station ninety
     *  tiles away by a hundredth (Atlanta 6,5). */
    static Piece pc[MAX_PIECES];
    int32_t      col, row;
    if (s_pass != 1)
    {
        memset(s_ramp_forms, 0, sizeof s_ramp_forms);
        memset(s_ramp_lost, 0, sizeof s_ramp_lost);
    }
    for (row = 0; row < R_MAP; ++row)
        for (col = 0; col < R_MAP; ++col)
        {
            uint8_t              b     = c->xbld[row * R_MAP + col];
            static const int32_t DC[4] = {0, 1, 0, -1}, DR[4] = {-1, 0, 1, 0}; /* N E S W */
            int                  dside, rside, eside, k, np, off, roads, form = 0, how, flat = 0;
            int                  lane_off = 0;    /* the ramp's: an OFF ramp's lane descends to its road end */
            float                taper    = 0.0f; /* the ramp's: how much of its road end narrows to the road lane's width */
            float                z0       = 0.0f; /* ... and how far up its deck end sits */
            V2                   along, toward;
            float                climb = 0.0f, total = 0.0f;
            if (!net_hiway_onramp(b))
                continue;
            how = ramp_orient(c, col, row, &dside, &rside, &eside, &along, &toward, &off, &roads);
            if (how == 2)
            {
                /*  At a band's end, head-on: straight across from the far
                 *  edge to the deck's, climbing. */
                V2 from   = {(float)col + 0.5f - (float)DC[eside] * 0.47f, (float)row + 0.5f - (float)DR[eside] * 0.47f};
                V2 to     = {(float)col + 0.5f + (float)DC[eside] * 0.5f, (float)row + 0.5f + (float)DR[eside] * 0.5f};
                pc[0].arc = 0;
                pc[0].a   = from;
                pc[0].b   = to;
                pc[0].len = v2len((V2){to.x - from.x, to.y - from.y});
                np        = 1;
                climb     = pc[0].len;
                off       = 0;
                form      = 0;
                if (g_dev.path_dump)
                    dumpf("PATH hw=%.3f\nTILES %d,%d\nGATES\nPTS %.3f,%.3f %.3f,%.3f\nRAD 0.000 0.000\nTLIM 0.000 0.000\n", (double)RAMP_HW, (int)col, (int)row, (double)from.x, (double)from.y, (double)to.x, (double)to.y);
            }
            else if (how == 0)
            {
                if (s_pass != 1)
                    ++s_ramp_forms[4];
                s_ramp_lost[(row)*R_MAP + (col)] = 5;
                if (g_dev.lane_dump && s_pass != 1)
                    dumpf("RAMP %d,%d lost: %s\n", (int)col, (int)row, "no deck touching the tile");
                continue; /* no deck touching it: left to its sprite */
            }
            else
            {
                /*  The ramp (spec 7.3), as ONE ROUTED LANE (lane.c): the stages
                 *  below find the deck beside the tile, say what the road does,
                 *  pose the descent, route it, join the road, and hand the
                 *  pieces to the loft as a strip like any other. */
                Ramp x;
                memset(&x, 0, sizeof x);
                x.m = m, x.c = c, x.mask_bit = mask_bit, x.comp = comp, x.col = col, x.row = row, x.pc = pc;
                x.iref = -1, x.best = s_tune.hiway_reach, x.deck_lane = -1, x.road_lane = -1, x.road_port = -1, x.l = l;
                x.along = along, x.toward = toward, x.off = off, x.dside = dside, x.rside = rside, x.eside = eside, x.how = how, x.roads = roads;
                if (ramp_find(&x) != 0)
                    continue;
                ramp_classify(&x);
                ramp_geometry(&x);
                if (ramp_poses(&x) != 0)
                    continue;
                if (ramp_route(&x) != 0)
                    continue;
                ramp_join(&x);
                ramp_reverse(&x);
                if (ramp_finish(&x) != 0)
                    return -1;
                np = x.np, flat = x.flat, form = x.form, off = x.off, lane_off = x.lane_off, z0 = x.z0, climb = x.climb;
                taper = x.taper;
            }
            for (k = 0; k < np; ++k)
                total += pc[k].len;
            {
                /*  A ramp: concrete from the road to the deck, it grades
                 *  nothing; a lane drop's is flat, one lane wide, drawn as
                 *  the deck's outer lane, its height eased by the loft; an
                 *  older form lifts over `climb` at the deck end. */
                RLoft d   = {0};
                d.f       = F_ROAD;
                d.fam     = &net_hiway;
                d.hw      = RAMP_HW;
                d.mat     = flat ? MAT_HIWAY_LANE : MAT_HIWAY; /* a lane piece is drawn as the deck's outer lane */
                d.kind    = LOFT_RAMP;
                d.struct_ = 1;
                d.flat = d.lane_piece = flat;
                d.lane_off            = lane_off;
                d.z0                  = flat ? z0 : 0.0f;
                d.ramp0               = flat ? 0.0f : off ? 0.0f
                                                          : climb; /* the lift climbs the first `climb` */
                d.ramp1               = flat ? 0.0f : off ? climb
                                                          : 0.0f; /* ... or falls over the end */
                d.pin0 = d.pin1 = 1;
                d.cls           = -1.0f;
                d.taper         = taper;
                d.hw_end        = ROAD_W * 0.2f;        /* a road lane's half width, where its centre lies */
                d.taper_start   = !off && taper > 0.0f; /* an ON ramp starts at the road */
                double tp       = prof_now();
                if (loft(m, c, mask_bit, comp, &d, pc, np, total) != 0)
                    return -1;
                net_prof_add(NET_PROF_RAMP_LOFT, prof_now() - tp);
            }
            if (s_pass != 1)
                ++s_ramp_forms[form];
        }
    return 0;
}

/*  Every band in the city, each walked once from an end. */
/*  A band from the table: what the walk and the fit found, drawn
 *  again -- its overlay, its loft, its lanes. */
static int hw_replay(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, int index, const RSeg *r)
{
    static Piece   pieces[MAX_PIECES];
    static int32_t own[4 * MAX_PTS];
    static V2      q[MAX_PTS];
    static float   rad[MAX_PTS], tlim[MAX_PTS];
    const Piece   *pc;
    const V2      *rq;
    const float   *rrad, *rtlim;
    const int32_t *tcol, *trow;
    HwWalk         x;
    int            k;
    seg_table_arenas(r, &pc, &rq, &rrad, &rtlim, &tcol, &trow);
    memset(&x, 0, sizeof x);
    x.m = m, x.c = c, x.mask_bit = mask_bit, x.comp = comp, x.col = r->col, x.row = r->row;
    x.pieces = pieces, x.own = own, x.q = q, x.rad = rad, x.tlim = tlim;
    x.table = index;
    for (k = 0; k < r->np && k < MAX_PIECES; ++k)
        pieces[k] = pc[k];
    x.np = k;
    for (k = 0; k < r->nk && k < MAX_PTS; ++k)
    {
        q[k]    = rq[k];
        rad[k]  = rrad[k];
        tlim[k] = rtlim[k];
    }
    x.nk = k;
    for (k = 0; k < r->nt && k < 4 * MAX_PTS; ++k)
        own[k] = trow[k] * R_MAP + tcol[k];
    x.n_own = k;
    if (hw_overlay(&x) != 0)
        return 0;
    return hw_loft(&x);
}

int build_highways(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp)
{
    static uint8_t seen[R_MAP * R_MAP];
    int32_t        col, row;
    double         tp;
    memset(seen, 0, sizeof seen);
    tp = prof_now();
    hiway_free_air(c, l);
    net_prof_add(NET_PROF_HW_AIR, prof_now() - tp), tp = prof_now();
    /* hiway_lanes ran before the junctions (mesh.c), so a junction knows the ramp beside it */
    s_hw_nst    = 0;
    s_hw_band   = 0;
    s_hwb_count = 0;
    s_hwb_fill  = 0;
    if (s_pass == 2 && seg_table_count() > 0 && !g_dev.no_replay)
    {
        /*  The building pass: every band the grading pass kept, in the
         *  order it walked them (the ramps find a band's stations by its
         *  number), lofted from the table without a walk or a fit. */
        int i;
        for (i = 0; i < seg_table_count(); ++i)
        {
            const RSeg *r = seg_table_entry(i);
            if (r && r->band && hw_replay(m, c, mask_bit, comp, i, r) != 0)
                return -1;
        }
    }
    else
    {
        for (row = 0; row < R_MAP; ++row)
            for (col = 0; col < R_MAP; ++col)
            {
                int32_t pcol, prow;
                int     ew;
                int32_t bc, br;
                if (!hiway_cell(c, col, row, &pcol, &prow, &ew))
                    continue;
                if (pcol != col || prow != row || seen[row * R_MAP + col])
                    continue;
                /*  A band is walked from an END, away from it.  Whether it
                 *  carries on either way from this cell -- a band cell of
                 *  the same axis, or a curve block, which means the band
                 *  turns a corner there into another -- is measured here;
                 *  which way to walk is arc.rules.band_start's. */
                {
                    int     back = 0, on = 0, e2, side[4], way;
                    int32_t qc, qr;
                    bc = ew ? col - 1 : col;
                    br = ew ? row : row - 1;
                    if (bc >= 0 && br >= 0 && ((hiway_cell(c, bc, br, &qc, &qr, &e2) && e2 == ew && qc == bc && qr == br) || hiway_block(c, bc, br, &qc, &qr, side)))
                        back = 1;
                    bc = ew ? col + 1 : col;
                    br = ew ? row : row + 1;
                    if (bc < R_MAP && br < R_MAP && ((hiway_cell(c, bc, br, &qc, &qr, &e2) && e2 == ew && qc == bc && qr == br) || hiway_block(c, bc, br, &qc, &qr, side)))
                        on = 1;
                    way = script_rule_band_start(back, on);
                    if (way && walk_hiway(m, c, mask_bit, comp, col, row, ew, way, seen) != 0)
                        return -1;
                }
            }
        /*  What no end reached -- a band between two blocks, or a loop --
         *  walked both ways from wherever it is found.  The start cell's own
         *  half is lofted twice, once per direction; rare enough to bear. */
        for (row = 0; row < R_MAP; ++row)
            for (col = 0; col < R_MAP; ++col)
            {
                int32_t pcol, prow;
                int     ew;
                if (!hiway_cell(c, col, row, &pcol, &prow, &ew))
                    continue;
                if (pcol != col || prow != row || seen[row * R_MAP + col])
                    continue;
                if (walk_hiway(m, c, mask_bit, comp, col, row, ew, 1, seen) != 0)
                    return -1;
                seen[row * R_MAP + col] = 0;
                if (walk_hiway(m, c, mask_bit, comp, col, row, ew, -1, seen) != 0)
                    return -1;
            }
    }
    net_prof_add(NET_PROF_HW_BANDS, prof_now() - tp), tp = prof_now();
    /*  Every ramp tile marked orange on the ground, for outline mode: the
     *  corridor markers' ground-highlight pipeline; the shader shows it
     *  only while the grid is on. */
    {
        /*  And every network tile tinted with it: roads grey, rails yellow,
         *  highways purple; a ramp tile keeps its orange. */
        int32_t i;
        for (i = 0; i < R_MAP * R_MAP; ++i)
        {
            uint8_t b     = c->xbld[i];
            float   paint = lane_ramp_tile(i % R_MAP, i / R_MAP) ? 8.0f
                            : (b >= 0x1Du && b <= 0x2Bu)         ? 9.0f
                            : (b >= 0x2Cu && b <= 0x3Au)         ? 10.0f
                            : (b >= 0x43u && b <= 0x48u)         ? 10.0f
                            : net_hiway_any(b)                   ? 11.0f
                                                                 : 0.0f;
            if (paint > 0.0f && tile_highlight(m, c, mask_bit, i % R_MAP, i / R_MAP, paint) != 0)
                return -1;
        }
    }
    net_prof_add(NET_PROF_HW_TINT, prof_now() - tp), tp = prof_now();
    if (build_ramps(m, c, l, mask_bit, comp) != 0)
        return -1;
    net_prof_add(NET_PROF_HW_RAMPS, prof_now() - tp), tp = prof_now();
    /* the bands' ends into the roads they become (lane.c) */
    if (lane_transitions(m, c, mask_bit) != 0)
        return -1;
    net_prof_add(NET_PROF_HW_TRANS, prof_now() - tp), tp = prof_now();
    /* every lane end goes somewhere, every start has something arriving */
    lane_check_ends();
    net_prof_add(NET_PROF_HW_CHECK, prof_now() - tp);
    return 0;
}

/* ---- the loft's stages a deck or a ramp supplies ---------------------------- */

/*  The grading: a deck stands clear of the ground and notches nothing;
 *  a ramp is concrete and grades nothing either.  Only where the lift has
 *  tapered toward the ground -- the ramp cells at a band's ends -- is the
 *  deck earthworks, shelved like a road (net/grade.c says why). */
static int hiway_flies(const RLoft *d, float over)
{
    return d->struct_ || over > 0.5f * HIWAY_LIFT;
}

/*  The deck stands clear (spec 7.2): 5 m under the soffit plus the girder
 *  is about 7.5 m to the road surface, and the vertical unit here is the
 *  altitude level, seven to eight metres.  So a little over one level,
 *  applied after the profile is settled so the deck follows the ground's
 *  shape while riding above it.  A deck is a structure, not a carpet: its
 *  support line may rise or fall no faster than a sixth of a level a tile,
 *  so it runs straight over what the ground does under it and the columns
 *  take up the difference. */

/*  Station i: how far along it is, the height it stands at, and the
 *  ground under it. */
void hiway_prof_at(const ProfFan *p, int i, float *s_at, float *z, float *ground)
{
    const Sample *smp = (const Sample *)p->smp;
    *s_at             = smp[i].s;
    *z                = smp[i].z;
    *ground           = smp[i].z;
}

void hiway_prof_set(ProfFan *p, int i, float z)
{
    ((Sample *)p->smp)[i].z = z;
}

/*  A highway strip's elevation: arc.rules.hiway_profile lays it out. */
static int hiway_profile(Loft *x)
{
    Sample *smp = x->smp;
    int     ns  = x->ns, i;
    if (ns > 2)
    {
        ProfFan p;
        memset(&p, 0, sizeof p);
        p.smp        = smp;
        p.n          = ns;
        p.total      = x->total;
        p.ramp       = x->d->struct_ != 0;
        p.lane_piece = x->d->lane_piece != 0;
        p.lane_off   = x->d->lane_off != 0;
        p.flat       = x->d->flat != 0;
        p.z0         = x->d->z0;
        p.ramp0      = x->d->ramp0;
        p.ramp1      = x->d->ramp1;
        p.grade      = s_tune.hiway_grade;
        p.stiff      = ns <= LOFT_MAX_ST ? s_tune.hiway_stiff : 0.0f;
        p.lift       = HIWAY_LIFT;
        script_rule_object("hiway_profile", "profile", &p);
    }
    else
    {
        /*  Too short to shape: the lift alone, tapered as it is on a
         *  strip that has stations to shape. */
        for (i = 0; i < ns; ++i)
        {
            float lift = x->d->flat ? 0.0f : 1.0f;
            if (x->d->ramp0 > 0.0f && smp[i].s < x->d->ramp0)
                lift = smp[i].s / x->d->ramp0;
            if (x->d->ramp1 > 0.0f && x->total - smp[i].s < x->d->ramp1)
            {
                float back = (x->total - smp[i].s) / x->d->ramp1;
                if (back < lift)
                    lift = back;
            }
            smp[i].z += HIWAY_LIFT * lift;
        }
    }
    /*  The lane drop: what a ramp took from the deck, station by station;
     *  and the stations themselves, for the ramps built after the bands. */
    if (!x->d->struct_)
    {
        hiway_lane_stations(smp, ns);
        for (i = 0; i < ns && s_hw_nst < HW_MAX_ST; ++i)
        {
            s_hw_st[s_hw_nst].pos  = smp[i].pos;
            s_hw_st[s_hw_nst].dir  = smp[i].dir;
            s_hw_st[s_hw_nst].s    = smp[i].s;
            s_hw_st[s_hw_nst].z    = smp[i].z;
            s_hw_st[s_hw_nst].band = x->d->band;
            ++s_hw_nst;
        }
    }
    return 0;
}

/*  The pair's across range and along offset: a turn-out at the foot of
 *  a lane drop is one lane, drawn as the deck's outer lane; a deck's
 *  narrowed sides are told apart by an along offset.  The class is the
 *  highway's own. */
static int hiway_pair(Loft *x, LoftPair *p)
{
    if (x->d->lane_piece)
    {
        p->acr  = -1.0f;
        p->acl  = -HIWAY_LANE_IN;
        p->aoff = 4000.0f;
    }
    else if (!x->d->struct_)
        p->aoff = (p->acr > -0.99f ? 1000.0f : 0.0f) + (p->acl < 0.99f ? 2000.0f : 0.0f);
    p->cls = 3.0f;
    return 0;
}

/* ---- the highway as a family --------------------------------------------- */

/*  A deck records its edge in the traffic's graph under a class of its
 *  own, never whatever the last road walked left behind, so
 *  its cars use both lanes; it has no sidewalk and no crosswalk. */
static int hiway_record(Loft *x)
{
    return net_record(&x->m->net, x->smp, x->ns, x->total, 3, 0, x->d);
}

/*  A deck's lanes, from the 7.1 section: the half deck is a tile and
 *  holds three of them, their centres 2.75, 6.45 and 10.15 m out of
 *  14.9.  Two lanes carry the traffic here, so it runs in the one
 *  against the median and the one against the shoulder. */
static void hiway_traffic_lanes(const RLoft *d, int cls, float *lane_in, float *lane_out)
{
    (void)d;
    (void)cls;
    *lane_in  = 0.185f;
    *lane_out = 0.681f;
}

/*  The highway is walked by its own bands, not by tile family (its tiles
 *  are the road's), so only what the loft asks a family for is filled. */
const NetFamily net_hiway = {
    "highway",
    F_ROAD,
    &s_tune.road_w,
    &s_tune.road_rmin,
    &s_tune.road_rmax,
    0.50f,
    MAT_HIWAY,
    LOFT_DECK,
    2, /* the fit's family code */
    0.0f,
    0.25f,
    0,
    0,
    0,
    0,
    0,
    NULL,
    NULL,
    hiway_record,
    NULL,
    hiway_flies,
    hiway_taper,
    hiway_profile,
    NULL, /* works: the deck's piers and caps, stubbed out (see above) */
    hiway_pair,
    hiway_traffic_lanes,
    NULL,
    NULL,
    NULL, /* a deck's lanes are its own (lane_deck) */
    7.0f, /* a deck's lane wires in light grey */
    NET_LANE_ENDS_OPEN,
    0, /* keeps to its tiles */
    0.0f, /* no turnout: its junctions hand back curb trims */
    "slot_strip", /* a deck carries its own height and meets nothing on the ground */
    1,            /* a deck: its quads carry a gore and an underside */
};
