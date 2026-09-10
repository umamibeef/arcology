/*  band.c: THE BAND'S STORES, AND WHAT IT READS THE MAP WITH.
 *
 *  Which building bytes are a band, and which way each band runs, as
 *  arc.rules.band_tiles says.  The free-air corridor a slab may sweep
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
 *  arc.rules.spur_arm's and arc.rules.spur_target's. */
#include <string.h>

#include "mesh/internal.h"

static int gix_slab_ground_margin = -1;
#include "dump.h"
#include "opt.h"
#include "mesh/model.h"
#include "pipeline.h"

HwSpur s_hw_spurs[MAX_SPURS];
int    s_hw_nspurs;

#define HWB_MAX 1024
static int32_t s_hwb_tiles[64 * 1024];
static int     s_hwb_first[HWB_MAX], s_hwb_n[HWB_MAX], s_hwb_count, s_hwb_fill;

void net_bands_reset(void)
{
    s_hwb_count = s_hwb_fill = 0;
}

void net_band_record(const int32_t *own, int n)
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

int band_count(void)
{
    return s_hwb_count;
}

int band_get(int i, const int32_t **tiles, int *n)
{
    if (i < 0 || i >= s_hwb_count)
        return -1;
    *tiles = &s_hwb_tiles[s_hwb_first[i]];
    *n     = s_hwb_n[i];
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


/*  The lane drop (spec 7.3): what a spur took from the slab at each
 *  station.  The table is by tile and map side.  A station's right and
 *  left are read off its heading.  Within a tile the taper's progress is
 *  the station's distance from the tile's edge nearest the spur.  It
 *  runs along the taper's direction.  So a gore narrows linearly across
 *  its tile.  The strip comes down in a straight line over the descent,
 *  from the slab's height at the gore to the spur tile's ground. */
/*  The lane drop (spec 7.3) at each station of a slab.  For every spur
 *  whose point on the centerline lies on this band the slab narrows on
 *  the spur's side.  It narrows by ARC LENGTH from that point.  It goes
 *  through curve blocks as easily as along a straight, over the taper.
 *  The slab tile itself and the descent come first.  Then comes the
 *  gore, the taper's last tile, where the slab widens back and the
 *  lane's own sliver sits level beside it.  Toward the line the slab
 *  stays two lanes as far as a partner spur's point within seven tiles,
 *  else widens back over one.  A spur beside a curve drops its lane from
 *  the curve like any other. */
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
    *c0    = s_hw_spurs[r].c0;
    *tile  = (V2){(float)s_hw_spurs[r].rc + 0.5f, (float)s_hw_spurs[r].rr + 0.5f};
    *along = s_hw_spurs[r].along;
    *len   = s_hw_spurs[r].len;
    *off   = s_hw_spurs[r].off;
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

/*  The width a station is left with on one side: the narrowest any spur
 *  asks for. */
void band_drop_width(DropFan *d, int i, int side, float w)
{
    Sample *smp = (Sample *)d->smp;
    if (i < 0 || i >= d->n)
        return;
    if (side)
        smp[i].wl = w < smp[i].wl ? w : smp[i].wl;
    else
        smp[i].wr = w < smp[i].wr ? w : smp[i].wr;
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
    d.nspurs = s_hw_nspurs;
    d.reach  = s_tune.band_reach;
    d.narrow = BAND_LANE_IN;
    s_drop   = d;
    net_stage_hand(&s_drop, "drop");
}

/*  One loft's working state, handed to the stages below so each can be
 *  read on its own.  It holds the strip as asked for (s_ld), its
 *  stations and their raw ground.  The widths and ends the callers gave. */

/* the segment table's sample cache, below with the table */


/*  A slab's works beside the slab: its piers and what stands with them.
 *  The family's works hook is EMPTY.  The bents' caps and columns every
 *  BAND_BENT all take their offsets in whole tiles.  This stands them
 *  clear of a slab whose width is s_tune.band_w.  Drawing them again
 *  means measuring those offsets from the slab's own width instead. */

/* ---- raised bands (the line spec, part 7) --------------------------- */

/*  A band is not one tile wide.  Its slab is a TWO-TILE BAND, the spec's
 *  2x2 segment.  So its centerline runs along the seam between two rows
 *  or two columns, never through a tile center.  The whole segment
 *  pipeline above (which walks tile to tile) cannot express it.  This
 *  walks the band instead and hands the ordinary loft a spine that is
 *  offset half a tile across.  Which ids are which was read off the
 *  shipped cities, not the sprite sheet: see the Part 7 section of
 *  docs/future.rst.  Every one of these is ALWAYS part of a 2x2 square
 *  of band (6905 of 6905 for 0x49).  The id alone says which way the
 *  band runs, because an interior tile of a two-wide band has two
 *  neighbors along it and one across.  The six meets are SLAB tiles too:
 *  0x4D is a slab in an east-west band with a second family underneath.
 *  The line below it is drawn by the thread family, not this one. */
/*  Is this building byte a band.  Which way does its band run? 1 for a
 *  slab tile, 2 for a spur of the band, 3 for an on-spur, 0 for anything
 *  else.  Which bytes are which is arc.rules.band_tiles, read once a
 *  generation into a table, since every tile of the map is looked up in
 *  it. */
static int band_kind(uint8_t b, int *east_west)
{
    const unsigned char *kind, *ew;
    script_bandtiles(&kind, &ew);
    *east_west = ew[b];
    return kind[b];
}

/*  The band itself: a slab tile, or a spur of the band.  An on-spur, a
 *  curve block and the interchange are band and are not the band. */
int band_slab(uint8_t b, int *east_west)
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

/*  A curve block: four tiles of one id carrying a band through a right
 *  angle. */
int net_band_curve(uint8_t b)
{
    int ew;
    return band_kind(b, &ew) == 4;
}

/*  The interchange: a 2x2 where four bands meet. */
int net_band_interchange(uint8_t b)
{
    int ew;
    return band_kind(b, &ew) == 5;
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
static float   s_band_top[R_MAP * R_MAP]; /* a structure's top, in levels of altitude.  0 where none */
static uint8_t s_rise_said[256];           /* --band-dump: each building's rise, once */
static int     s_corr_reach, s_corr_climb, s_corr_spurs; /* the corridor the script pushed */

void band_free_air(const RCity *c, const RAtlasLevel *l)
{
    static const char *const CORR[3] = {"reach", "climb", "spurs"};
    const unsigned char     *air     = script_bytes("slab_air");
    float                    ans[3]  = {0.0f, 0.0f, 0.0f};
    int32_t                  i;
    /*  Nothing pushed and a slab may sweep over its own cells and no
     *  others, which is a band that cannot be fitted at all. */
    script_numbers("corridor", CORR, ans, 3);
    s_corr_reach = (int)ans[0], s_corr_climb = (int)ans[1], s_corr_spurs = ans[2] > 0.5f;
    for (i = 0; i < R_MAP * R_MAP; ++i)
    {
        /*  A surface network is not in the way of a viaduct: the slab
         *  crosses over it on a straddle bent (spec 7.2).  Another slab
         *  is, being at the same height.  A structure, a building, a
         *  park, anything from 0x69, is not in the way at all: the slab
         *  is raised.  Would rather the slab take the air and the
         *  building renderer keep buildings off it later.  How high each
         *  stands is still measured, for --band-dump, in case that
         *  changes.  The height is the sprite's own.  The art rises `ay`
         *  above the left corner of its diamond.  The top corner of that
         *  diamond is half the footprint's diamond above that.  The rest
         *  is building, alt_step pixels to the level.  Not the bounding
         *  box, though.  A chimney, an antenna or a flag would make a
         *  shed a tower.  But the bulk: the first row down from the top
         *  with paint across two fifths of the diamond's width.  A tree
         *  stands about a level and a house one.  A warehouse stands
         *  about two levels and a third to its box, and less to its
         *  roof.  One the art set cannot show is a wall. */
        uint8_t      b = c->xbld[i];
        const RTile *t;
        int32_t      ybulk;
        s_band_top[i] = 0.0f;
        if (air[b] < 2)
        {
            s_free[i] = air[b] ? 1 : 0;
            continue;
        }
        /*  Free air whose height is worth measuring.  A sprite the art
         *  set cannot show leaves nothing to measure, and a tile no
         *  measurement can be taken of is out of the corridor. */
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
            s_band_top[i] = (float)rcity_alt_ground(c->altm[i]) + rise;
            s_free[i]      = 1;
            if (g_dev.band_dump && !s_rise_said[b])
            {
                s_rise_said[b] = 1;
                dumpf("band rise: xbld %02x foot %d box %.2f bulk %.2f levels\n", (unsigned)b, (int)t->foot, (double)(((float)t->ay - 0.5f * (float)t->foot * (float)l->tile_h) / (float)l->alt_step), (double)rise);
            }
        }
    }
}

const uint8_t *band_corridor(const RCity *c, const int32_t *own, int n_own, int spurs)
{
    int k;
    /*  The reach beside the band and the climb it allows are
     *  arc.rules.corridor's, read once a build.  The bulk measured above
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

/*  Walk one band from an end and loft its slab.  The spine runs along
 *  the seam: half a tile across from the primary tile's center. */

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
    V2          *pts; /* the spine's points: seam points and block centers */
    uint8_t     *spur;
    uint8_t     *block; /* per point: a curve block's center */
    uint8_t     *stair; /* per point: the collapsed staircase it belongs to, 1-based, or 0 (hw_chain) */
    Piece       *pieces;
    int32_t     *own; /* the band's own tiles */
    V2          *q;   /* the fit's nodes, their radii and tangent budgets */
    float       *rad, *tlim;
    int          n, n_own, np, nk;
    int          cut;          /* where the drive's pieces for this band are */
    int          table;        /* the band's entry in the segment table, -1 for none: the loft's cache key */
    V2           band0, band1; /* the ends, run out to the edge of the end cells */
    int32_t      cc, cr, dx, dy;
    int          cew;
    float        total, spur0, spur1;
    HwRun       *run; /* the cells the band is made of, before anything is built from them */
} HwWalk;



/*  Through a curve block, and on through every block that follows it.
 *  Its center lies on both seams.  So the spine turns there, and the
 *  fillet carries the arc back into the tile before and on into the one
 *  after.  The art draws a diagonal band as a chain of these blocks
 *  touching at their corners.  So the walk follows the chain by whatever
 *  step joins one to the next.  The straightener makes one line of the
 *  centers. 1 when a chain was taken (the walk goes on from where it
 *  left the chain, or ends if nothing continued it). */





/*  The tiles: from the first cell along the band wherever it goes.
 *  Straight on while it can, through a curve block at a right angle, or
 *  a step sideways.  Until nothing continues it. */
/*  And the band BUILT from a run of cells: the spine's points, the tiles
 *  the band owns, and which of its cells carry a spur.  Everything here
 *  follows from the run.  Nothing decides which cells are in it.  A
 *  cell's point sits on the seam, half a tile across from its own
 *  center.  A curve block's is its corner, which lies on both seams at
 *  once. */
static void hw_run_take(HwWalk *x)
{
    const HwRun *r = x->run;
    int          i;
    x->n = x->n_own = 0;
    for (i = 0; i < r->n; ++i)
    {
        const int32_t cc = r->cell[i] % R_MAP, cr = r->cell[i] / R_MAP;
        if (r->block[i])
        {
            if (x->n_own + 4 <= 4 * MAX_PTS)
            {
                x->own[x->n_own++] = cr * R_MAP + cc;
                x->own[x->n_own++] = cr * R_MAP + cc + 1;
                x->own[x->n_own++] = (cr + 1) * R_MAP + cc;
                x->own[x->n_own++] = (cr + 1) * R_MAP + cc + 1;
            }
            x->spur[x->n]  = 0;
            x->block[x->n] = 1;
            x->pts[x->n]   = (V2){(float)cc + 1.0f, (float)cr + 1.0f};
            ++x->n;
            continue;
        }
        if (x->n_own + 2 <= 4 * MAX_PTS)
        {
            x->own[x->n_own++] = cr * R_MAP + cc;
            x->own[x->n_own++] = r->ew[i] ? (cr + 1) * R_MAP + cc : cr * R_MAP + cc + 1;
        }
        x->spur[x->n]  = (uint8_t)band_is_incline(x->c->xbld[cr * R_MAP + cc]);
        x->block[x->n] = 0;
        x->pts[x->n]   = (V2){(float)cc + (r->ew[i] ? 0.5f : 1.0f), (float)cr + (r->ew[i] ? 1.0f : 0.5f)};
        ++x->n;
    }
}


/*  Run the spine to the outer edge of the end cells.  The slab then
 *  covers its whole first and last segment, rather than stopping at
 *  their centers.  The direction at each end is the polyline's own,
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

/*The band's tiles as a mask, for the fit's coverage rule.  The slab must
 *pass over every one of them.  Take away the tiles of the curve blocks.
 *A corner's sweep or a staircase's diagonal rightly cuts inside them.
 *The straight cells beside an on-spur are in it like any other.  The fit
 *holds its runs and its joins to them.  So a staircase may collapse next
 *to a spur and the slab still passes the spur's cell. */
static uint8_t        s_own_mask[R_MAP * R_MAP];
static const uint8_t *band_own_mask(const HwWalk *x)
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

/*  Which way the band turns at point j.  It is the sign of the cross
 *  product of the steps in and out.  It is 0 at an end, or straight
 *  through. */
static int hw_turn(const V2 *pts, int n, int j)
{
    float cross;
    if (j <= 0 || j + 1 >= n)
        return 0;
    cross = (pts[j].x - pts[j - 1].x) * (pts[j + 1].y - pts[j].y) - (pts[j].y - pts[j - 1].y) * (pts[j + 1].x - pts[j].x);
    return cross > 1e-4f ? 1 : cross < -1e-4f ? -1
                                              : 0;
}

/*  Is a straight cell's point pinned by an on-spur beside it?  An
 *  on-spur joins the slab cell it touches.  So the slab must pass over
 *  that cell: a staircase breaks at such a cell rather than sliding its
 *  diagonal off it. */
static int hw_pinned(const RCity *c, V2 p)
{
    int32_t fx = (int32_t)floorf(p.x), fy = (int32_t)floorf(p.y);
    int32_t t[2][2], k, d;
    static int gix_cell_axis = -1;
    /*  How far into a tile the point has to lie before the cell reads as
     *  east-west rather than north-south is the script's. */
    if (p.x - (float)fx > net_geo(&gix_cell_axis, "slab_cell_axis")) /* east-west: the pair is the rows above and below */
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
            if (net_band_spur(b))
                return 1;
        }
    return 0;
}

/*  The points the fit is given: the straight cells', and a curve block's
 *  center only where the block is a corner of its own.  A STAIRCASE is
 *  blocks turning alternately, with at most two straight cells between.
 *  It is the game's way of laying a diagonal.  It is given as ONE point,
 *  the center of the blocks and short straights it is made of.  So the
 *  run before it, the diagonal through its middle and the run after it
 *  are three legs the fit fillets at their two bends.  The coverage rule
 *  keeps the slab over the short straights.  With every block a point, a
 *  staircase was a polyline of right-angle corners two tiles apart, each
 *  filleted at a tile's radius: a serpentine.  With nothing of it, the
 *  fit joined the runs at its ends by an L across the block beside it. */
/*  Which way the chain turns at cell i: 1, -1, or 0 for straight on. */
int band_stair_turn(const StairFan *s, int i)
{
    return hw_turn(s->pts, s->n, i);
}

/*  Is the cell pinned by an on-spur beside it? */
int band_stair_pinned(const StairFan *s, int i)
{
    return hw_pinned((const RCity *)s->c, s->pts[i]);
}

/*  A cell as a point of the chain, and a stair as the one point at the
 *  center of the cells it is made of. */
void band_stair_point(StairFan *s, int i)
{
    s->chain[s->nc++] = s->pts[i];
}

void band_stair_centre(StairFan *s, int i, int j)
{
    V2  mid = {0.0f, 0.0f};
    int k, m = 0;
    for (k = i; k <= j; ++k, ++m)
        mid.x += s->pts[k].x, mid.y += s->pts[k].y;
    s->chain[s->nc++] = (V2){mid.x / (float)m, mid.y / (float)m};
}

/*  The points the fit is given, as arc.rules.stair picks them: the band
 *  is read here and the rule picks from it.  So the chain stands ready
 *  before the fit is set up. */
static V2       s_hw_chain[MAX_PTS];
static StairFan s_hw_stair;

static void hw_chain_ask(const HwWalk *x)
{
    memset(&s_hw_stair, 0, sizeof s_hw_stair);
    s_hw_stair.c     = x->c;
    s_hw_stair.pts   = x->pts;
    s_hw_stair.block = x->block;
    s_hw_stair.n     = x->n;
    s_hw_stair.gap   = (int)s_tune.band_stair;
    s_hw_stair.chain = s_hw_chain;
}

StairFan *net_hw_chain(void)
{
    return s_hw_stair.n > 0 ? &s_hw_stair : NULL;
}

/*  The fit two ways, the better kept.  One way has free lines and the
 *  band's spur tiles in its corridor.  The slab runs as straight as its
 *  corridor allows, and the plain fit.  Which of the two is better is
 *  arc.rules.fit_choice's. */
/*  The fit two ways, the better kept.  One way has free lines and the
 *  band's spur tiles in its corridor.  The slab runs as straight as its
 *  corridor allows, and the plain fit.  Which of the two is better is
 *  arc.rules.fit_choice's.  So the band reads what each fit needs, the
 *  drive runs both and settles the choice, and the walk takes the one
 *  that was kept. */
static struct
{
    const RCity   *c;
    const HwWalk  *x;
    const int32_t *own;
    const V2      *chain;
    int            n_own, nc, live;
    V2             band0, band1;
} s_hwfit;
static V2    s_hwfit_q[2][MAX_PTS];
static float s_hwfit_rad[2][MAX_PTS], s_hwfit_tlim[2][MAX_PTS];
static int   s_hwfit_nk[2], s_hwfit_sc[2][3];
static char  s_hwfit_tally[2][512], s_hwfit_before[512];
static int   s_hwfit_kept = -1;

static void hw_fit_ask(const RCity *c, const HwWalk *x, const int32_t *own, int n_own, const V2 *chain, int nc, V2 band0, V2 band1)
{
    s_hwfit.c     = c;
    s_hwfit.x     = x;
    s_hwfit.own   = own;
    s_hwfit.n_own = n_own;
    s_hwfit.chain = chain;
    s_hwfit.nc    = nc;
    s_hwfit.band0 = band0;
    s_hwfit.band1 = band1;
    s_hwfit.live  = 1;
    s_hwfit_nk[0] = s_hwfit_nk[1] = 0;
    fit_tally_get(2, s_hwfit_before, sizeof s_hwfit_before);
}

int net_hw_fits(void)
{
    return s_hwfit.live ? 2 : 0;
}

int net_hw_fit_begin(int w)
{
    if (!s_hwfit.live || w < 0 || w > 1)
        return 0;
    fit_tally_set(2, s_hwfit_before, sizeof s_hwfit_before);
    return path_fit_points_begin(band_corridor(s_hwfit.c, s_hwfit.own, s_hwfit.n_own, w),
                                 band_own_mask(s_hwfit.x), s_hwfit.chain, s_hwfit.nc,
                                 s_tune.band_w, s_hwfit.band0, s_hwfit.band1,
                                 s_tune.band_rmax, s_tune.band_rmin, 1.0f, -1, -1, w,
                                 s_hwfit_q[w], s_hwfit_rad[w], s_hwfit_tlim[w], MAX_PTS);
}

void net_hw_fit_done(int w)
{
    int k;
    if (!s_hwfit.live || w < 0 || w > 1)
        return;
    s_hwfit_nk[w]    = path_fit_points_end();
    s_hwfit_sc[w][0] = s_hwfit_sc[w][1] = 0;
    s_hwfit_sc[w][2] = s_hwfit_nk[w];
    for (k = 1; k + 1 < s_hwfit_nk[w]; ++k)
        if (s_hwfit_rad[w][k] < 0.01f)
            ++s_hwfit_sc[w][0];
        else if (s_hwfit_rad[w][k] < s_tune.band_rmin)
            ++s_hwfit_sc[w][1];
    fit_tally_get(2, s_hwfit_tally[w], sizeof s_hwfit_tally[w]);
}

const char *net_hw_fit_choice(const int **free_, const int **held)
{
    if (!s_hwfit.live)
        return NULL;
    *free_ = s_hwfit_sc[1];
    *held  = s_hwfit_sc[0];
    return "band";
}

/*  The one the script kept, into the band's own arrays, and the tally
 *  that goes with it. */
void net_hw_fit_choice_is(int keep_free)
{
    const int w = keep_free ? 1 : 0;
    if (!s_hwfit.live)
        return;
    s_hwfit.live = 0;
    s_hwfit_kept = w;
    if (g_dev.path_dump)
        dumpf("FIT %s kept: free %d corners %d tight %d nodes, plain %d corners %d tight %d nodes\n",
              keep_free ? "free" : "plain",
              s_hwfit_sc[1][0], s_hwfit_sc[1][1], s_hwfit_sc[1][2],
              s_hwfit_sc[0][0], s_hwfit_sc[0][1], s_hwfit_sc[0][2]);
    fit_tally_set(2, s_hwfit_tally[w], sizeof s_hwfit_tally[w]); /* the kept fit's tallies alone */
}

int net_hw_fit_take(V2 *q, float *rad, float *tlim)
{
    const int w = s_hwfit_kept;
    int       k;
    if (w < 0)
        return 0;
    for (k = 0; k < s_hwfit_nk[w]; ++k)
        q[k] = s_hwfit_q[w][k], rad[k] = s_hwfit_rad[w][k], tlim[k] = s_hwfit_tlim[w][k];
    return s_hwfit_nk[w];
}

/*  The corridor fit, straightened as a line is and filleted with the
 *  wide radius a band wants.  1 when there is nothing to draw. */
static int hw_fit(HwWalk *x)
{
    const RCity *c      = x->c;
    int32_t      col    = x->col;
    int32_t      row    = x->row;
    V2          *pts    = x->pts;
    uint8_t     *spur   = x->spur;
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
    float        spur0;
    float        spur1;
    int          nk;
    /*  Straightened as a line is: a staircase of cells becomes one
     *  diagonal.  Then every bend is filleted, with the wide radius a
     *  band wants so the curve begins well before the corner.  The spur
     *  lengths come first, measured along the raw polyline in TILES: the
     *  taper below compares them against arc length.  A count of points
     *  is no length at all once the straightener has collapsed a run.  A
     *  slab is raised end to end: the 0x61-0x64 cells at a band's ends
     *  are the band's own, the slab runs over them.  The taper is the
     *  on-spurs' alone. */
    spur0 = spur1 = 0.0f;
    (void)spur;
    /*  The slab on the corridor fit, as a chain of its own points: the
     *  seam points and the block centers.  The corridor is not the
     *  band's tiles alone: a slab is RAISED, and may sweep over any tile
     *  that is free air: ground, trees, water.  Only what stands in the
     *  way bounds it: a building, another network, another slab.  So a
     *  jog of two tiles becomes one long S over the ground beside it.  A
     *  corner block's arc takes the radius a band wants.  Nothing has to
     *  be covered: where the arc cuts inside a block, the ground shows
     *  under the viaduct, as it should. */
    fit_tally_into(net_band->fit_fam);
    /*  Every one of the band's own tiles must end up under the slab, as
     *  a line's must under its strip.  The corridor says where the slab
     *  MAY sweep, the own tiles where it MUST pass.  Without that rule a
     *  long band's jogs collapse into a line across the block beside
     *  them.  That line lies tiles off the band's cells, and over lines
     *  and buildings.  The fit is given the chain hw_chain makes of the
     *  points: the straight cells', a lone block's corner, and nothing
     *  of a staircase.  So the runs at its ends meet across it.  The
     *  band the fit holds to the corridor is the slab's own width and no
     *  wider.  At a full tile each side its inner edge at a corner
     *  samples the on-spur tile beside the slab, which is not free air.
     *  Every fillet is refused down to a kink. */
    hw_fit_ask(c, x, own, n_own, s_hw_chain, s_hw_stair.nc, band0, band1);
    (void)pts, (void)pieces, (void)q, (void)rad, (void)tlim, (void)np, (void)col, (void)row, (void)n, (void)nk;
    x->spur0 = spur0;
    x->spur1 = spur1;
    return 0;
}

/*  And the fit the drive kept, into the band.  It holds its pieces, its
 *  nodes and its own tiles, which the building pass replays from rather
 *  than walking and fitting it again. */
static int hw_fit_take(HwWalk *x)
{
    const RCity *c      = x->c;
    int32_t      col    = x->col;
    int32_t      row    = x->row;
    V2          *pts    = x->pts;
    int32_t     *own    = x->own;
    V2          *q      = x->q;
    float       *rad    = x->rad;
    float       *tlim   = x->tlim;
    int          n_own  = x->n_own;
    int          n      = x->n;
    int          nk     = net_hw_fit_take(q, rad, tlim);
    (void)c;
    if (nk < 2)
    {
        x->nk = nk;
        return 1;
    }
    if (g_dev.band_dump)
    {
        int q2;
        dumpf("band band from c%d r%d: %d points ->", (int)col, (int)row, n);
        for (q2 = 0; q2 < n && q2 < 80; ++q2)
            dumpf(" (%.2f,%.2f)", (double)pts[q2].x, (double)pts[q2].y);
        dumpf("\n   fitted %d ->", nk);
        for (q2 = 0; q2 < nk && q2 < 14; ++q2)
            dumpf(" (%.2f,%.2f) r%.2f", (double)q[q2].x, (double)q[q2].y, (double)rad[q2]);
        dumpf("\n");
    }
    if (g_dev.path_dump)
    {
        /*  The same lines the line fit prints.  So tools/plan.py draws a
         *  slab the way it draws a line: its tiles, the fitted line, its
         *  radii and budgets, and the runs it found. */
        int d;
        dumpf("PATH hw=%.3f\nTILES", (double)s_tune.band_w);
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
    /*  The band's path queued to be cut.  The cut is arc.rules.pieces's
     *  and the drive makes it, so this stops here and band_band_cut
     *  takes the answer.  A band queues alone: every segment's pieces
     *  have been drawn by the time the bands are walked.  So the queue
     *  is emptied for each band rather than growing through the pass. */
    net_cut_reset();
    x->cut = net_cut_add(q, nk, rad, tlim);
    x->nk  = nk;
    x->np  = 0;
    return 0;
}

/*  And the band built from the pieces the drive cut. */
static int hw_fit_cut(HwWalk *x)
{
    Piece *pieces = x->pieces;
    int    np     = 0;
    if (net_cut_pieces(x->cut, pieces, MAX_PIECES, &np) != 0 || np == 0)
    {
        x->np = 0;
        return 1;
    }
    /*  The band kept for the building pass and the next build: its
     *  pieces, its fit's nodes, its own tiles.  The building pass replays
     *  it from here rather than walking and fitting it again. */
    if (s_pass != 2)
        x->table = seg_store_band(x->col, x->row, x->ew, x->sign, pieces, np,
                                  x->q, x->rad, x->tlim, x->nk, x->own, x->n_own);
    x->np = np;
    return 0;
}

/*  The fit's nodes and corridors as ground highlights, when the overlay
 *  is on.  1 to stop, as a failed highlight always has. */
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
    /*  The corridor the fit had to work with, asked for again: the walk
     *  stamped it per band and the highlight reads the same tiles. */
    const uint8_t *corr = band_corridor(c, own, n_own, 1);
    /*  The fit's nodes, when the overlay is on: the same marks the line
     *  walk draws, on the slab rather than the ground.  And under them
     *  what the fit had to work with: the band's own tiles outlined in
     *  tan, the free air beside them in blue.  Ground highlights,
     *  blended tints under everything that stands on the tile.  The
     *  outlines before them, and the slabs before those, hid the town. */
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
            if (!isown && !corr[ti])
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
            z    = surface_at_world(c, mask_bit, q[k3].x, q[k3].y) + BAND_LIFT;
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

/*  The band the walk lofted, held for the drive: it composes the slab,
 *  and build_band_done lays the lanes under it. */
static HwWalk s_hw_hold;
static int    s_hw_live;
static int    s_hw_band_of_hold;

/*  The slab lofted on the pieces.  Its lanes follow the composition. */
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
    float        spur0    = x->spur0;
    float        spur1    = x->spur1;
    int          guard;
    for (guard = 0; guard < np; ++guard)
        total += pieces[guard].len;
    /*  The lift tapers over the spur cells at each end.  It runs from
     *  the slab's height where the elevated tiles begin, down to the
     *  ground at the band's outer end.  So a spur is a spur. */
    int   rc;
    RLoft d = {0};
    if (spur0 >= total)
        spur1 = 0.0f; /* all spur: one slope, not two */
    d.f             = net_line->f;
    d.fam           = net_band;
    d.hw            = s_tune.band_w; /* the slab's half width */
    d.ground_margin = net_geo(&gix_slab_ground_margin, "slab_ground_margin"); /* it reads the ground a hair beyond its edges */
    d.mat           = MAT_BAND;
    d.kind          = LOFT_SLAB;
    d.spur0         = spur0;
    d.spur1         = spur1;
    d.pin0 = d.pin1 = 1;
    net_band_record(own, n_own);
    d.band = ++s_hw_band; /* the band the loft records its stations under */
    d.cls  = -1.0f;
    /* the stations from the table's cache, as a line's: this build's, or the last one's when no edit came near */
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
    if (rc != 0)
        return rc;
    s_hw_hold = *x;
    s_hw_band_of_hold = s_hw_band;
    s_hw_live = 1;
    return 0;
}

/*  The slab composed, and its six lanes laid under it (lane.c) at
 *  fractions of the slab's half width.  The drive composes the slab
 *  between the loft and this. */
int build_band_done(void)
{
    int rc = net_loft_close();
    if (rc != 0 || !s_hw_live)
        return rc;
    s_hw_live = 0;
    return lane_slab(s_hw_hold.m, s_hw_hold.c, s_hw_hold.mask_bit, s_hw_hold.pieces, s_hw_hold.np, s_tune.band_w, s_hw_band_of_hold);
}

/*  The band the walk fitted, held while the drive runs its fit. */
static HwWalk s_hw_pend;
static int    s_hw_pend_live;

static int walk_band(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, int32_t col, int32_t row, int ew, int sign, const HwRun *given)
{
    static V2      pts[MAX_PTS];
    static uint8_t spur[MAX_PTS];
    static uint8_t block[MAX_PTS];
    static uint8_t stair[MAX_PTS];
    static Piece   pieces[MAX_PIECES];
    static int32_t own[4 * MAX_PTS]; /* the band's own tiles, both of each cell and all four of a block */
    static V2      q[MAX_PTS];
    static float   rad[MAX_PTS], tlim[MAX_PTS];
    static HwRun   run;
    HwWalk         x;
    memset(&x, 0, sizeof x);
    x.m = m, x.c = c, x.mask_bit = mask_bit, x.comp = comp, x.col = col, x.row = row, x.ew = ew, x.sign = sign;
    x.pts = pts, x.spur = spur, x.block = block, x.stair = stair, x.pieces = pieces, x.own = own, x.q = q, x.rad = rad, x.tlim = tlim;
    x.cc = col, x.cr = row, x.cew = ew;
    x.dx = ew ? sign : 0, x.dy = ew ? 0 : sign;
    x.table = -1;
    x.run   = &run;
    run     = *given;
    /*  The stages: what is built from the cells the script found, the
     *  ends, and the fit: which the drive runs.  So this stops here and
     *  build_band_fitted takes up the overlay and the loft. */
    hw_run_take(&x);
    if (x.n < 2)
        return 0;
    hw_ends(&x);
    /*  The band is held HERE, before the fit reads it: the fit keeps a
     *  handle on the band it is fitting.  The drive runs it after this
     *  walk has returned. */
    s_hw_pend      = x;
    s_hw_pend_live = 1;
    hw_chain_ask(&s_hw_pend);
    return 0;
}

/*  And the fit set up from the chain the rule picked. */
int build_band_chained(void)
{
    if (!s_hw_pend_live)
        return 0; /* a band the building pass replayed: it was never walked */
    if (hw_fit(&s_hw_pend) != 0)
        s_hw_pend_live = 0;
    return 0;
}

int build_band_fitted(void)
{
    if (!s_hw_pend_live)
        return 0; /* a band the building pass replayed: it was never fitted */
    if (hw_fit_take(&s_hw_pend) != 0)
        s_hw_pend_live = 0;
    return 0;
}

/*  And the band drawn from the pieces the drive cut for it. */
int build_band_cut(void)
{
    if (!s_hw_pend_live)
        return 0;
    s_hw_pend_live = 0;
    if (hw_fit_cut(&s_hw_pend) != 0)
        return 0;
    if (hw_overlay(&s_hw_pend) != 0)
        return 0;
    return hw_loft(&s_hw_pend);
}

/*  A band from the table: what the walk and the fit found, drawn again:
 *  its overlay, its loft, its lanes. */
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

static double s_hw_tp; /* where the spurs ended, so the links time from there */

/*  ------------------------------------------------------------------
 *  The bands, one at a time
 *
 *  The building pass reads every band the grading pass kept, in the
 *  order it walked them, since the spurs find a band's stations by its
 *  number.  A grading pass has nothing kept, and reads the bands the
 *  SCRIPT discovered instead (scripts/compose/bands.lua): in the order
 *  it found them, which is the order everything downstream numbers them
 *  by.  The drive composes each slab between its loft and the lanes laid
 *  under it.
 *  ------------------------------------------------------------------ */
static struct
{
    RMesh             *m;
    const RCity       *c;
    const RAtlasLevel *l;
    uint8_t            mask_bit;
    int                comp;
    int                phase; /* 0 the table's bands, 1 the script's, 3 nothing left */
    int                i;     /* how far down that list the cursor has come */
} s_hwb;



/*  Whether this build has bands to DISCOVER.  A building pass with a
 *  segment table replays the grading pass's fits.  And there is nothing
 *  for a script to find. */
int net_hw_replaying(void)
{
    return !(s_pass == 2 && seg_table_count() > 0 && !g_dev.no_replay);
}

void build_bands_begin(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp)
{
    memset(&s_hwb, 0, sizeof s_hwb);
    s_hwb.m = m, s_hwb.c = c, s_hwb.l = l, s_hwb.mask_bit = mask_bit, s_hwb.comp = comp;
    s_hwb.phase = net_hw_replaying() ? 1 : 0;
}


int build_band_next(void)
{
    RMesh             *m        = s_hwb.m;
    const RCity       *c        = s_hwb.c;
    const uint8_t      mask_bit = s_hwb.mask_bit;
    const int          comp     = s_hwb.comp;
    while (s_hwb.phase < 3)
    {
        if (s_hwb.phase == 0)
        {
            const RSeg *r;
            int         i = s_hwb.i;
            if (i >= seg_table_count())
            {
                s_hwb.phase = 3;
                continue;
            }
            ++s_hwb.i;
            r = seg_table_entry(i);
            if (!r || !r->band)
                continue;
            if (hw_replay(m, c, mask_bit, comp, i, r) != 0)
                return -1;
            return 1;
        }
        /*  The bands the script found, in the order it found them.
         *  Nothing is walked here: which cells make a band is
         *  scripts/compose/bands.lua's. */
        {
            const HwRun *run;
            int32_t      col, row;
            int          ew, sign;
            if (!net_hw_disc_get(s_hwb.i, &run, &col, &row, &ew, &sign))
            {
                s_hwb.phase = 3;
                continue;
            }
            ++s_hwb.i;
            if (walk_band(m, c, mask_bit, comp, col, row, ew, sign, run) != 0)
                return -1;
            return 1;
        }
    }
    return 0;
}

int build_bands(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp)
{
    int32_t col, row;
    double  tp;
    tp = prof_now();
    band_free_air(c, l);
    net_prof_add(NET_PROF_HW_AIR, prof_now() - tp), tp = prof_now();
    /* band_lanes ran before the junctions (mesh.c), so a junction knows the spur beside it */
    net_station_reset();
    net_bands_reset();
    build_bands_begin(m, c, l, mask_bit, comp);
    (void)col, (void)row;
    s_hw_tp = tp;
    return 0;
}

/*  And what stands on the bands the drive has just had composed: every
 *  network tile tinted for outline mode, and the spurs. */
int build_band_spurs(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp)
{
    double tp = s_hw_tp;
    net_prof_add(NET_PROF_BANDS, prof_now() - tp), tp = prof_now();
    /*  Every spur tile marked orange on the ground, for outline mode:
     *  the corridor markers' ground-highlight pipeline.  The shader
     *  shows it only while the grid is on. */
    {
        /*  And every network tile tinted with it.  Which byte takes
         *  which paint is the script's (`outline_tint`).  A spur tile
         *  keeps its orange, and a spur is one the pipeline found rather
         *  than a byte. */
        const unsigned char *tint = script_bytes("outline_tint");
        int32_t              i;
        for (i = 0; i < R_MAP * R_MAP; ++i)
        {
            float paint = lane_spur_tile(i % R_MAP, i / R_MAP) ? 8.0f : (float)tint[c->xbld[i]];
            if (paint > 0.0f && tile_highlight(m, c, mask_bit, i % R_MAP, i / R_MAP, paint) != 0)
                return -1;
        }
    }
    net_prof_add(NET_PROF_BAND_TINT, prof_now() - tp);
    build_spurs_begin(m, c, l, mask_bit, comp);
    s_hw_tp = prof_now();
    return 0;
}

/*  And the bands' ends into the lines they become (lane.c), once the
 *  drive has carried the open ends across the meets. */
void *build_band_links(RMesh *m, const RCity *c, uint8_t mask_bit)
{
    return net_links_fan(m, c, mask_bit);
}

/*  And what follows the script joining them: every lane end goes
 *  somewhere and every start has something arriving. */
int build_band_links_done(void)
{
    double tp = s_hw_tp;
    net_prof_add(NET_PROF_HW_TRANS, prof_now() - tp), tp = prof_now();
    lane_check_ends();
    net_prof_add(NET_PROF_HW_CHECK, prof_now() - tp);
    return 0;
}

/* ---- the loft's stages a slab or a spur supplies ---------------------------- */


/*  The slab stands clear (spec 7.2): 5 m under the soffit plus the
 *  girder is about 7.5 m to the line surface.  The vertical unit here is
 *  the altitude level, seven to eight meters.  So a little over one
 *  level, applied after the profile is settled so the slab follows the
 *  ground's shape while riding above it.  A slab is a structure, not a
 *  carpet: its support line may rise or fall no faster than a sixth of a
 *  level a tile.  So it runs straight over what the ground does under it
 *  and the columns take up the difference. */

/*  Station i: how far along it is, the height it stands at, and the
 *  ground under it. */
void band_prof_at(const ProfFan *p, int i, float *s_at, float *z, float *ground)
{
    const Sample *smp = (const Sample *)p->smp;
    *s_at             = smp[i].s;
    *z                = smp[i].z;
    *ground           = smp[i].z;
}

void band_prof_set(ProfFan *p, int i, float z)
{
    ((Sample *)p->smp)[i].z = z;
}

/*  A band strip's elevation: arc.rules.profile lays it out. */
static int band_profile(Loft *x)
{
    static ProfFan s_prof;
    ProfFan        p;
    int            ns = x->ns;
    memset(&p, 0, sizeof p);
    p.smp        = x->smp;
    p.n          = ns;
    p.total      = x->total;
    p.spur       = x->d->struct_ != 0;
    p.lane_piece = x->d->lane_piece != 0;
    p.lane_off   = x->d->lane_off != 0;
    p.flat       = x->d->flat != 0;
    p.z0         = x->d->z0;
    p.spur0      = x->d->spur0;
    p.spur1      = x->d->spur1;
    p.grade      = s_tune.band_grade;
    p.stiff      = s_tune.band_stiff;
    p.lift       = BAND_LIFT;
    s_prof       = p;
    net_stage_hand(&s_prof, "profile");
    return 0;
}

/*  The lane drop: what a spur took from the slab, station by station.
 *  And the stations themselves, for the spurs built after the bands.
 *  Both read the slab's heights, so both wait until the rule that shapes
 *  them has answered. */
static int band_profile_done(Loft *x)
{
    if (x->d->struct_)
        return 0;
    band_lane_stations(x->smp, x->ns);
    return 0;
}

/* ---- the band as a family --------------------------------------------- */

/*  A slab records its edge in the traffic's graph under a class of its
 *  own.  It never takes whatever the last line walked left behind, so
 *  its cars use both lanes.  It has no margin and no stripe. */
/*  What this file lends the declarations: the slab's stages.  The band
 *  is walked by its own bands, not by tile family (its tiles are the
 *  line's).  So only what the loft asks a family for is here. */
void band_primitives(void)
{
    net_hook_add_split2(NH_PROFILE, "slab_profile", (NetHookFn)band_profile, (NetHookFn)band_profile_done, "profile", "drop");
}
