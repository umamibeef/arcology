/*  band.c: THE BAND'S STORES, AND WHAT IT READS THE MAP WITH.
 *
 *  Which building bytes are a band, and which way each band runs, as
 *  arc.bands says.  The free-air corridor a slab may sweep
 *  over, bounded by the script's `slab_air` table and reaching as far as
 *  its pushed `corridor` numbers allow.  The lane drop offered to
 *  arc.rules.drop.  And:
 *
 *  The SPURS.  Every spur beside a slab, as the scan listed it.  The
 *  tile it stands on, the point on the slab's centerline beside it, and
 *  which way it runs.  The lane model reads them to know a spur tile
 *  from a line one.  The spur builder reads them to build.
 *
 *  The BANDS' TILES, as each band was walked, for the incremental
 *  rebuild's closure: a band near an edit is fitted again.  The closure
 *  has to know which tiles a band covered to say so.  A band past the
 *  table sets the count to -1, which tells the closure to rebuild every
 *  chunk rather than guess.
 *
 *  Nothing here decides anything.  Which cells form a band is
 *  scripts/compose/bands.lua's, and where a spur joins is
 *  arc.rules.spur_target's. */
#include <string.h>

#include "dump.h"
#include "mesh/internal.h"
#include "mesh/model.h"
#include "net/internal.h"
#include "net/net.h"
#include "opt.h"
#include "pipeline.h"
#include "script.h"

int gix_slab_ground_margin = -1;
BandSpur s_band_spurs[MAX_SPURS];
int    s_band_nspurs;

#define BAND_TILES_MAX 1024
static int32_t s_band_tiles[64 * 1024];
static int     s_band_tile_first[BAND_TILES_MAX], s_band_tile_n[BAND_TILES_MAX], s_band_tile_count, s_band_tile_fill;

void net_bands_reset(void)
{
    s_band_tile_count = s_band_tile_fill = 0;
}

void net_band_record(const int32_t *own, int n)
{
    int k;
    if (s_band_tile_count < 0 || s_band_tile_count >= BAND_TILES_MAX || s_band_tile_fill + n > (int)(sizeof s_band_tiles / sizeof *s_band_tiles))
    {
        s_band_tile_count = -1; /* past the table: the closure rebuilds every chunk */
        return;
    }
    s_band_tile_first[s_band_tile_count] = s_band_tile_fill;
    s_band_tile_n[s_band_tile_count]     = n;
    for (k = 0; k < n; ++k)
        s_band_tiles[s_band_tile_fill++] = own[k];
    ++s_band_tile_count;
}

int band_count(void)
{
    return s_band_tile_count;
}

int band_get(int i, const int32_t **tiles, int *n)
{
    if (i < 0 || i >= s_band_tile_count)
        return -1;
    *tiles = &s_band_tiles[s_band_tile_first[i]];
    *n     = s_band_tile_n[i];
    return 0;
}

/* ---- what the map says, as the script reads it ------------------------ */

/*  Which snap the drive is being asked about, and the band it belongs
 *  to: the reading arc.rules.spur_lane is given with the candidates. */
static const char *s_snap_what = "slab";
static int         s_snap_band = -1;

void net_spur_snap_what(const char **what, int *band)
{
    *what = s_snap_what;
    *band = s_snap_band;
}

/*  And which one the spur builder is about to ask for.  It is "slab" for
 *  a spur's top, with the band it belongs to.  It is "line" for its
 *  foot. */
void net_spur_snap_is(const char *what, int band)
{
    s_snap_what = what;
    s_snap_band = band;
}


/*  Station i of the slab: how far along it is, where it stands and which
 *  way it heads.  Spur r: the point on the centerline it drops from, its
 *  own tile's center, the way it runs, how long its taper is.  Whether
 *  it leaves the slab or joins it. */
int band_drop_station(const DropFan *d, int i, float *at, V2 *pos, V2 *dir)
{
    const Sample *smp = (const Sample *)d->smp;
    if (i < 0 || i >= d->n)
        return 0;
    *at  = smp[i].s;
    *pos = smp[i].pos;
    *dir = smp[i].dir;
    return 1;
}

int band_drop_spur(const DropFan *d, int r, V2 *c0, V2 *tile, V2 *along, int *len, int *off)
{
    if (r < 0 || r >= d->nspurs)
        return 0;
    *c0    = s_band_spurs[r].c0;
    *tile  = (V2){(float)s_band_spurs[r].rc + 0.5f, (float)s_band_spurs[r].rr + 0.5f};
    *along = s_band_spurs[r].along;
    *len   = s_band_spurs[r].len;
    *off   = s_band_spurs[r].off;
    return 1;
}

/*  Every station its full width, before any spur takes a lane. */
void band_drop_clear(DropFan *d)
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

/*  The width a station has on one side, as the rule says.  It is less
 *  than 1 where a lane is taken from the slab, and more than 1 where a
 *  lane is added beside it.  How the spurs' asks are settled against
 *  each other is the rule's own. */
void band_drop_width(DropFan *d, int i, int side, float w)
{
    Sample *smp = (Sample *)d->smp;
    if (i < 0 || i >= d->n)
        return;
    if (side)
        smp[i].wl = w;
    else
        smp[i].wr = w;
}

/*  WHERE EACH LANE OF THE SLAB RUNS, along it, as the rule says.  A
 *  lane the rule ends short of the slab's end is laid that far and no
 *  further (lane.c).  The slab's edge behind it is the rule's own width.
 *  Kept for the band in hand, from its lane drop until its lanes are
 *  laid, and cleared with them. */
#define BAND_LANES 8
static struct
{
    float s0, s1;
    int   set;
} s_band_cut[2][BAND_LANES];

void band_drop_cut(DropFan *d, int side, int k, float s0, float s1)
{
    (void)d;
    if (side < 0 || side > 1 || k < 0 || k >= BAND_LANES)
        return;
    s_band_cut[side][k].s0  = s0;
    s_band_cut[side][k].s1  = s1;
    s_band_cut[side][k].set = 1;
}

int band_lane_cut(int side, int k, float *s0, float *s1)
{
    if (side < 0 || side > 1 || k < 0 || k >= BAND_LANES || !s_band_cut[side][k].set)
        return 0;
    *s0 = s_band_cut[side][k].s0;
    *s1 = s_band_cut[side][k].s1;
    return 1;
}

void band_lane_cut_clear(void)
{
    memset(s_band_cut, 0, sizeof s_band_cut);
}

/*  The gore: where the spur's own sliver of lane sits beside the slab. */
void band_drop_gore(DropFan *d, int i, int side)
{
    Sample *smp = (Sample *)d->smp;
    if (i < 0 || i >= d->n)
        return;
    smp[i].lane |= side ? 2 : 1;
    smp[i].zr[side] = smp[i].z;
}

/*  The lane drop at each station of a slab: arc.rules.drop works it
 *  out. */
void band_lane_stations(Sample *smp, int ns)
{
    static DropFan s_drop;
    DropFan        d;
    memset(&d, 0, sizeof d);
    d.smp    = smp;
    d.n      = ns;
    d.nspurs = s_band_nspurs;
    d.reach  = TUNE(band_reach);
    d.narrow = BAND_LANE_IN;
    s_drop   = d;
    band_lane_cut_clear();
    net_stage_hand(&s_drop, "drop");
}

/*  One loft's working state, handed to the stages below so each can be
 *  read on its own.  It holds the strip as asked for (s_ld), its
 *  stations and their raw ground.  The widths and ends the callers gave. */

/* the segment table's sample cache, below with the table */


/*  A slab's works beside the slab: its piers and what stands with them.
 *  The family's works hook is EMPTY.  The bents' caps and columns every
 *  BAND_BENT all take their offsets in whole tiles.  This stands them
 *  clear of a slab whose width is the band_w knob.  Drawing them again
 *  means measuring those offsets from the slab's own width instead. */

/* ---- raised bands (the line spec, part 7) --------------------------- */

/*  Is this building byte a band.  Which way does its band run? 1 for a
 *  slab tile, 2 for a spur of the band, 3 for an on-spur, 0 for anything
 *  else.  Which bytes are which is arc.bands, read once a
 *  generation into a table, since every tile of the map is looked up in
 *  it.  Which ids are which was read off the shipped cities, not the
 *  sprite sheet: see the Part 7 section of docs/future.rst.  Every one
 *  of these is ALWAYS part of a 2x2 square of band across the whole
 *  collection.  The id alone says which way the band runs, because an
 *  interior tile of a two-wide band has two neighbors along it and one
 *  across.  A meet is a SLAB tile too: a slab in a band of one axis with
 *  a second family underneath.  The line below it is drawn by the thread
 *  family, not this one. */
static int band_kind(uint8_t b, int *east_west)
{
    const unsigned char *kind, *ew;
    script_bandtiles(&kind, &ew);
    *east_west = ew[b];
    return kind[b];
}

/*  The band itself: a slab tile, or a spur of the band.  An on-spur, a
 *  curve block and the interchange are band and are not the band. */
static int band_slab(uint8_t b, int *east_west)
{
    int k = band_kind(b, east_west);
    return k == 1 || k == 2 ? k : 0;
}

/*  An on-spur tile: one id per direction, one tile, standing beside the
 *  slab cell it climbs to. */
int net_band_spur(uint8_t b)
{
    int ew;
    return band_kind(b, &ew) == 3;
}

/*  Why the on-spur at a tile was not built this pass, for the area
 *  report: the stage that refused it, or 0. */
static uint8_t           s_spur_lost[R_MAP * R_MAP];
static const char *const SPUR_LOST[6] = {NULL, "no spur record for the tile", "no slab station within reach", "no slab station at the top of the descent", "the descent could not be routed", "no slab touching the tile"};
const char              *band_spur_lost(int32_t col, int32_t row)
{
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return NULL;
    return SPUR_LOST[s_spur_lost[row * R_MAP + col]];
}

/*  Why a spur was refused, as the stage that refused it says.  0 clears
 *  the tile.  -1 clears the whole map for a fresh build. */
void band_spur_lost_is(int32_t col, int32_t row, int why)
{
    if (why < 0)
    {
        memset(s_spur_lost, 0, sizeof s_spur_lost);
        return;
    }
    if (col >= 0 && row >= 0 && col < R_MAP && row < R_MAP)
        s_spur_lost[row * R_MAP + col] = (uint8_t)why;
}

int band_is_incline(uint8_t b)
{
    int ew;
    return band_slab(b, &ew) == 2;
}

/*  The corridor a slab may sweep over: every tile that is free air plus
 *  the band's own.  Which bytes are air, and how far beside the band the
 *  corridor reaches, is the script's: `slab_air` and `corridor`.  Both
 *  are read once a build, the band's own tiles stamped over the result
 *  per walk. */
static uint8_t s_free[R_MAP * R_MAP];
static uint8_t s_corr_h[R_MAP * R_MAP];
static uint8_t s_rise_said[256]; /* --band-dump: each structure's rise, once */
static int     s_corr_reach, s_corr_climb, s_corr_spurs; /* the corridor the script pushed */

void band_free_air(const RCity *c)
{
    static const char *const CORR[3] = {"reach", "climb", "spurs"};
    const unsigned char     *air     = script_bytes("slab_air");
    float                    ans[3]  = {0.0f, 0.0f, 0.0f};
    int32_t                  i;
    /*  Nothing pushed and a slab may sweep over its own cells and no
     *  others, which is a band that cannot be fitted at all. */
    script_numbers("corridor", CORR, ans, 3);
    s_corr_reach = (int)ans[0], s_corr_climb = (int)ans[1], s_corr_spurs = ans[2] > 0.5f;
    /*  How high a structure stands, and whether it can be measured at
     *  all, are read off the pictures by a script
     *  (scripts/art_tiles.lua).  A byte the art set cannot show leaves
     *  nothing to measure, and a tile no measurement can be taken of is
     *  out of the corridor. */
    const unsigned char *seen = script_bytes("structure_seen");
    const float         *rise = script_floats("structure_rise");
    for (i = 0; i < R_MAP * R_MAP; ++i)
    {
        /*  A surface network is not in the way of a viaduct: the slab
         *  crosses over it on a straddle bent (spec 7.2).  Another slab
         *  is, being at the same height.  A structure, a building, a
         *  park, anything a script calls raised ground, is not in the
         *  way at all: the slab is over it.  Would rather the slab take
         *  the air and the building renderer keep buildings off it
         *  later.  How high each stands is measured all the same, in
         *  case that changes. */
        uint8_t b = c->xbld[i];
        if (air[b] < 2)
        {
            s_free[i] = air[b] ? 1 : 0;
            continue;
        }
        s_free[i] = seen[b] ? 1 : 0;
        if (seen[b] && g_dev.band_dump && !s_rise_said[b])
        {
            s_rise_said[b] = 1;
            dumpf("band rise: xbld %02x  %.2f levels\n", (unsigned)b, (double)rise[b]);
        }
    }
}

const uint8_t *band_corridor(const RCity *c, const int32_t *own, int n_own, int spurs)
{
    int k;
    /*  The reach beside the band and the climb it allows are
     *  arc.numbers("corridor")'s, read once a build.  The bulk measured above
     *  is kept for --band-dump. */
    memset(s_corr_h, 0, sizeof s_corr_h);
    for (k = 0; k < n_own; ++k)
    {
        int32_t oc = own[k] % R_MAP, orr = own[k] / R_MAP, dc, dr;
        int     lvl = rcity_alt_ground(c->altm[own[k]]);
        for (dr = -s_corr_reach; dr <= s_corr_reach; ++dr)
            for (dc = -s_corr_reach; dc <= s_corr_reach; ++dc)
            {
                int32_t tc = oc + dc, tr = orr + dr, i;
                if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                    continue;
                i = tr * R_MAP + tc;
                if (!s_free[i] || rcity_alt_ground(c->altm[i]) > lvl + s_corr_climb)
                    continue;
                s_corr_h[i] = 1;
            }
    }
    for (k = 0; k < n_own; ++k)
        s_corr_h[own[k]] = 1;
    /*  And the band's own on-spur tiles beside its cells, where the rule
     *  asked for them. */
    for (k = 0; spurs && s_corr_spurs && k < n_own; ++k)
    {
        static const int32_t DC[4] = {1, -1, 0, 0}, DR[4] = {0, 0, 1, -1};
        int32_t              oc = own[k] % R_MAP, orr = own[k] / R_MAP, e;
        for (e = 0; e < 4; ++e)
        {
            int32_t tc = oc + DC[e], tr = orr + DR[e];
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            if (net_band_spur(c->xbld[tr * R_MAP + tc]))
                s_corr_h[tr * R_MAP + tc] = 1;
        }
    }
    return s_corr_h;
}
