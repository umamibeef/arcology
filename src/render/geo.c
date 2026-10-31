/*  geo.c: the numbers the world is built and tuned with, and the one
 *  place they are written.
 *
 *  geo_defaults, at the end of this file, sets every number.  The store
 *  has no struct of fields: a name exists from the moment something
 *  sets it.  The C that reads one names it and keeps the place it was
 *  given.
 *
 *  A read is an index into this store, cached at the site that reads it.
 *  So a number costs a bounds test and a load even in a per-frame loop.
 *  That is what lets every reader keep its numbers here rather than in
 *  fields of its own. */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "geo.h"
#include "log.h"

/*  Bind the list above to the store.  Answers whether geo_defaults
 *  declared every one.  A name it did not is a fault: the renderer
 *  would draw with a nought where a width belongs. */
static int tune_bind(void);

/*  A reader may hold a pointer at a knob, so every use reads the live
 *  value. */
static const float *tune_at(const char *name);

/*  The knobs.  A declaration that names a knob the store already holds
 *  keeps its value and moves its range.  So a second declaration while
 *  the city runs does not throw a tuning session away. */
static int tune_declare(const RKnob *k);

static int geo_set(const char *name, float v);
#define GEO_MAX 512
/*  Where each name sits, by its hash.  A reader finds a number by name,
 *  and a stage that runs per tile reads several of
 *  them.  A walk of the whole store at each read would cost more than
 *  the number is worth.  The table is twice the store, so a probe ends within a few
 *  steps.  A slot holds ONE PAST the place, since nought is empty.  A
 *  name is never taken away, so a probe needs no mark for a hole. */
#define GEO_SLOTS 1024
/*  A probe walks until it finds an empty slot, so the table must always
 *  hold one.  This says so at compile time: raising GEO_MAX past the
 *  slots would turn a miss into a loop that never ends. */
typedef char geo_slots_hold_the_store[GEO_SLOTS > GEO_MAX ? 1 : -1];

static char     s_name[GEO_MAX][32];
static float    s_val[GEO_MAX];
static uint16_t s_slot[GEO_SLOTS];
static int      s_n;

/*  FNV-1a over the name.  Any spread does: the names are a few hundred
 *  words a person wrote, and the table is half empty. */
static uint32_t geo_hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s)
        h = (h ^ (uint32_t)(unsigned char)*s++) * 16777619u;
    return h;
}

/*  The slot a name holds, or the empty slot it would take.  Answers
 *  where, and puts the place it found in `at`, which is -1 for a name
 *  the store does not hold. */
static uint32_t geo_probe(const char *name, int *at)
{
    uint32_t h = geo_hash(name) & (GEO_SLOTS - 1u);
    for (;; h = (h + 1u) & (GEO_SLOTS - 1u))
    {
        int k = (int)s_slot[h] - 1;
        if (k < 0)
            return *at = -1, h;
        if (strcmp(s_name[k], name) == 0)
            return *at = k, h;
    }
}

static int geo_index(const char *name)
{
    int at;
    if (!name)
        return -1;
    geo_probe(name, &at);
    return at;
}

int geo_find(const char *name, float *v)
{
    int i = geo_index(name);
    if (i < 0)
        return 0;
    if (v)
        *v = s_val[i];
    return 1;
}

static int geo_set(const char *name, float v);

int geo_put(const char *name, float v)
{
    return geo_set(name, v);
}

float geo_num(int *cache, const char *name)
{
    if (*cache < 0)
        *cache = geo_index(name);
    return *cache >= 0 ? s_val[*cache] : 0.0f;
}

static int geo_set(const char *name, float v)
{
    int      at;
    uint32_t h;
    if (!name || !*name)
        return 0;
    h = geo_probe(name, &at);
    if (at >= 0)
    {
        s_val[at] = v;
        return 1;
    }
    if (s_n >= GEO_MAX)
        return 0;
    /*  A name the store cannot hold whole would never be found again,
     *  and would take a fresh place at every reading. */
    if (strlen(name) >= sizeof s_name[0])
    {
        R_WARN("geo", "the name %s is longer than %d characters and cannot be kept", name, (int)sizeof s_name[0] - 1);
        return 0;
    }
    snprintf(s_name[s_n], sizeof s_name[0], "%s", name);
    s_val[s_n] = v;
    s_slot[h]  = (uint16_t)(s_n + 1);
    ++s_n;
    return 1;
}

/*  ---- THE LOOK'S KNOBS ---------------------------------------------------
 *
 *  The store above is the look's numbers, set by name.  A KNOB is one of
 *  the handful the TUNING WINDOW moves while the city is running.  It
 *  carries more than a value: a range to hold
 *  it to, the value it starts at, and what the window calls it.
 *
 *  geo_defaults declares every one.  The window keeps no list of them,
 *  so a knob is added in one place.
 *
 *  A knob keeps its own float at an address that never moves, because a
 *  reader may hold a pointer at it.  So what is drawn later reads the
 *  value the window is showing rather than the one the declaration was
 *  read at. */
static struct
{
    char  name[24];
    char  label[32];
    char  group[24];
    float v, lo, hi, def;
} s_knob[ARC_TUNE_MAX];
static int s_nknob;

static float clamp_to(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int knob_at(const char *name)
{
    int i;
    for (i = 0; name && i < s_nknob; ++i)
        if (strcmp(s_knob[i].name, name) == 0)
            return i;
    return -1;
}

static int tune_declare(const RKnob *k)
{
    int at;
    if (!k || !k->name || !*k->name)
        return 0;
    if (strlen(k->name) >= sizeof s_knob[0].name)
    {
        R_WARN("geo", "the knob %s is longer than %d characters and cannot be kept", k->name, (int)sizeof s_knob[0].name - 1);
        return 0;
    }
    at = knob_at(k->name);
    if (at < 0)
    {
        if (s_nknob >= ARC_TUNE_MAX)
        {
            R_WARN("geo", "the knob %s: the store holds only %d", k->name, ARC_TUNE_MAX);
            return 0;
        }
        at           = s_nknob++;
        s_knob[at].v = k->def; /* only the first reading sets the value */
        snprintf(s_knob[at].name, sizeof s_knob[0].name, "%s", k->name);
    }
    snprintf(s_knob[at].label, sizeof s_knob[0].label, "%s", k->label ? k->label : "");
    snprintf(s_knob[at].group, sizeof s_knob[0].group, "%s", k->group ? k->group : "");
    s_knob[at].lo  = k->lo;
    s_knob[at].hi  = k->hi;
    s_knob[at].def = clamp_to(k->def, k->lo, k->hi);
    s_knob[at].v   = clamp_to(s_knob[at].v, k->lo, k->hi);
    return 1;
}

int tune_count(void)
{
    return s_nknob;
}

int tune_knob(int i, RKnob *out)
{
    if (i < 0 || i >= s_nknob || !out)
        return 0;
    out->name  = s_knob[i].name;
    out->label = s_knob[i].label[0] ? s_knob[i].label : NULL;
    out->group = s_knob[i].group;
    out->v     = s_knob[i].v;
    out->lo    = s_knob[i].lo;
    out->hi    = s_knob[i].hi;
    out->def   = s_knob[i].def;
    return 1;
}

int tune_set_at(int i, float v)
{
    if (i < 0 || i >= s_nknob)
        return 0;
    s_knob[i].v = clamp_to(v, s_knob[i].lo, s_knob[i].hi);
    return 1;
}

int tune_set(const char *name, float v)
{
    return tune_set_at(knob_at(name), v);
}

static const float *tune_at(const char *name)
{
    int i = knob_at(name);
    return i < 0 ? NULL : &s_knob[i].v;
}

int tune_find(const char *name, float *v)
{
    const float *p = tune_at(name);
    if (!p)
        return 0;
    if (v)
        *v = *p;
    return 1;
}

int tune_values(float *out, int max)
{
    int i, n = s_nknob < max ? s_nknob : max;
    for (i = 0; i < n; ++i)
        out[i] = s_knob[i].v;
    return n;
}

/*  What an unbound knob reads as.  A read before tune_bind gets nought,
 *  and this keeps such a read from being a crash. */
static const float s_nought = 0.0f;

/*  Every pointer is the same value here, so nothing is ordered by
 *  position: tune_bind names each field beside the knob it wants. */
RTuned s_tuned = {
    &s_nought,
    &s_nought,
};

static int tune_bind(void)
{
    int missing = 0;
#define BIND(k)                                                                       \
    do                                                                                \
    {                                                                                 \
        const float *p = tune_at(#k);                                                 \
        if (!p)                                                                       \
        {                                                                             \
            R_ERR("geo", "there is no knob named %s, and the renderer reads it", #k); \
            ++missing;                                                                \
            p = &s_nought;                                                            \
        }                                                                             \
        s_tuned.k = p;                                                                \
    } while (0)
    BIND(show_curves);
    BIND(corner_share);
#undef BIND
    return missing == 0;
}

/*  EVERY NUMBER THE WORLD IS BUILT FROM, and the knobs the tuning window
 *  moves, set once as the program starts.  What each one does is said
 *  where the C reads it. */
void geo_defaults(void)
{
    static const struct
    {
        const char *name;
        float       v;
    } NUM[] = {
        /*  A zoned lot drawn as a tint over the ground at every pitch, and
         *  how its tint and its border look (compose.c). */
        {"zones_drawn",           1.0f   },
        {"zone_fill",             0.40f  },
        {"zone_lighten",          0.55f  },
        {"zone_edge",             0.95f  },
        {"zone_edge_w",           0.10f  },
        {"zone_round",            0.35f  },
        /*  How far round its chunks a window's build reads the map, and how
         *  many chunks a side the live window builds at a time. */
        {"net_halo",              64.0f  },
        /*  How near the window a build lays a road's line passes for the
         *  road to be drawn, in tiles. */
        {"draw_margin",           4.0f   },
        {"build_block",           1.0f   },
        /*  A drawn street.  First how far the ground lies under its
         *  surface in levels, then the spacing of the points its surface
         *  is laid through, in tiles.  Its widths are its lanes' (layer
         *  2). */
        {"road_sink",             0.02f  },
        {"road_step",             0.1f   },
        /*  The parts a side of the grid the ground near a drawn street is
         *  drawn on.  It is even. */
        {"road_ground_grid",      8.0f   },
        /*  A drawn highway's deck: how far its bottom lies under its top,
         *  in levels.  Its widths are its lanes' (layer 2). */
        {"deck_thick",            0.12f  },
        /*  A highway's columns.  A column's half width in tiles, and how
         *  far apart they stand along a road in tiles.  Then how far
         *  over the ground a deck must stand to have one, in levels.
         *  Then how far a column stands clear of a lower road's edge, in
         *  tiles. */
        {"pier_column",           0.05f  },
        {"pier_step",             1.0f   },
        {"pier_least",            0.6f   },
        {"pier_clear",            0.05f  },
        /*  A railway at a tile of 100 m and a level of about 60 m.  The
         *  bed's height over its line, then its tracks' gauge, and their
         *  rails and sleepers.  Where its tracks and its gravel's foot lie
         *  is its lanes' (layer 2). */
        {"rail_bed_height",       0.01f  },
        {"rail_gauge",            0.0144f},
        {"rail_height",           0.005f },
        {"rail_width",            0.003f },
        {"rail_sleeper_gap",      0.02f  },
        {"rail_sleeper_half",     0.013f },
        {"rail_sleeper_width",    0.0025f},
        {"rail_sleeper_height",   0.005f },
        /*  A map no wider than this many tiles is built whole, at once.
         *  A wider one is built a block at a time, nearest the camera
         *  first. */
        {"build_whole_side",      128.0f },
        /*  The land settling as the world is built.  A chunk takes
         *  settle_seconds to settle, once it is built and once a sweep
         *  out from the camera at settle_sweep tiles a second reaches it.  The
         *  wave on the sheet has a height in levels, a length in tiles and
         *  a speed in tiles a second. */
        {"settle_seconds",        0.5f   },
        {"settle_sweep",          60.0f  },
        {"settle_wave_height",    1.2f   },
        {"settle_wave_length",    16.0f  },
        {"settle_wave_speed",     25.0f  },
        /*  The levels a piece of art falls from, and the seconds it takes.
         *  Each piece falls when the sweep reaches its own tile. */
        {"settle_drop",           8.0f   },
        {"settle_fall",           0.35f  },
        /*  How much darker the land still being built is drawn, and the
         *  seconds that darkening takes to come in.  A chunk brightens
         *  again as it settles. */
        {"settle_dim",            0.45f  },
        {"settle_dim_in",         0.6f   },
        /*  A power line's wire. */
        {"wire_w",                0.02f  },
        {"wire_span_sag",         0.1f   },
        {"wire_hang",             1.35f  },
        {"wire_sag",              0.05f  },
        /*  How close two faces lie before the check calls them one
         *  surface, and how far ahead of its surface a marking sits. */
        {"coplanar_z",            0.004f },
        {"decal_slot",            0.004f },
        /*  The painter's slot the zone tints take over a tile's ground. */
        {"zone_slot",             0.4f   },
        /*  Which sprite the ground's mean color comes from, and the color
         *  where the art has no such sprite. */
        {"ground_art",            256.0f },
        {"ground_red",            0.45f  },
        {"ground_green",          0.62f  },
        {"ground_blue",           0.30f  },
        /*  The height the map's cut edges go down to, in levels. */
        {"map_floor",             -2.0f  },
        /*  A cliff's lean at its lip and its foot (plans/terrain.md). */
        {"cliff_lip",             0.2f   },
        {"cliff_toe",             0.12f  },
        /*  What an edit shows (show.c). */
        {"edit_show_reach",       3.0f   },
        {"edit_show_come",        6.0f   },
        {"edit_show_go_speed",    12.0f  },
        {"edit_show_go_time",     0.35f  },
        {"edit_show_come_speed",  15.0f  },
        {"edit_show_come_time",   0.35f  },
        {"edit_show_come_inward", 1.0f   },
        {"edit_show_piece",       0.4f   },
        {"edit_show_jitter",      0.2f   },
        {"edit_show_scatter",     0.3f   },
        {"edit_show_dissolve",    0.0f   },
        {"edit_show_hop",         0.5f   },
        {"edit_show_depth",       1.0f   },
        {"edit_show_drop",        1.5f   },
        /*  The air the camera looks through (air.frag, View, Air).  The
         *  meters a tile spans, the nominal tile of
         *  docs/appendix-road-spec.rst, and the meters a level rises.
         *  The camera's distance from what it looks at, as a share of
         *  the width the view shows.  It is a long lens of about five
         *  degrees, since the flat view is a lens with no spread at all.  The
         *  haze over clean air, the share of the light that the ground
         *  gives back, and the steps along each ray.  The sun's light,
         *  where one lights a white surface that faces it to one. */
        {"air_tile_m",            15.0f  },
        {"air_level_m",           5.0f   },
        {"air_cam_k",             12.0f  },
        {"air_haze",              1.5f   },
        {"air_albedo",            0.2f   },
        {"air_steps",             12.0f  },
        {"air_sun",               1.0f   },
        /*  The street lamps (draw/lamps.c).  The meters a level of the art
         *  stands for when a lamp is put among it.  How wide the pool of
         *  light under a lamp's head is, in tiles. */
        {"lamp_level_m",          14.0f  },
        {"lamp_pool_tiles",       0.9f   },
        /*  The hour of the day the sun stands at (frame.c, sun_clock).  At
         *  this hour it stands where the light is fitted to the art. */
        {"sun_hour",              13.052f},
        /*  The sun's shadow on the mesh (frame.c, sun_frame).  The share
         *  of the sun a shadow takes, 0 for none.  The map's texels a
         *  side.  How much wider than the view the map is drawn, so a
         *  pan reuses it. */
        {"sun_shadow",            0.85f  },
        {"sun_shadow_res",        2048.0f},
        {"sun_shadow_margin",     1.5f   },
        /*  The ground's large bumps, which tilt its light only: their
         *  steepest slope, and the width of the wider of their two
         *  sizes in tiles. */
        {"ground_bump",           0.15f  },
        {"ground_bump_tiles",     5.0f   },
        /*  The land's cover (terrain_body.frag, land_cover).  How strongly
         *  the patches of meadow, scrub, woods and bare earth show, 0 for
         *  none.  The tiles a cell of their noise spans. */
        {"cover",                 1.0f   },
        {"cover_tiles",           40.0f  },
        /*  The occlusion of the sky by what stands round a point of the
         *  mesh (air_body.frag, occlusion).  Its strength, 0 for none.
         *  Its reach in tiles.  The share of a tap's way it ignores.  The
         *  frame's pixels a side that one of its pixels covers. */
        {"ao_strength",           1.0f   },
        {"ao_radius",             0.75f  },
        {"ao_bias",               0.1f   },
        {"ao_res",                2.0f   },
        /*  The water (terrain_body.frag, water_surface): how clear it is,
         *  1 for the sea's own, and its swells' steepest slope and length
         *  in tiles. */
        {"water_clarity",         1.0f   },
        {"water_wave",            0.15f  },
        {"water_wave_tiles",      6.0f   },
        /*  The trees' levels of detail (trees.c), in the pixels a tile
         *  spans on the screen.  Above the first every tree draws whole.
         *  Below the second no tree draws, and the ground takes the
         *  canopy.  Between the two each tree draws as a simple crown.  Each change of
         *  level is a dithered band of this share either side. */
        {"trees_near_px",         48.0f  },
        {"trees_far_px",          12.0f  },
        {"trees_band",            0.15f  },
        /*  How far a tree's crown sways on the wind, in tiles. */
        {"trees_sway",            0.012f },
        /*  The clouds the air pass marches through (air.frag).  The
         *  share of the sky under cloud on the clearest day.  The tiles
         *  the sky field's lowest octave spans, the wind's speed in
         *  tiles a second and its direction.  The clouds' base and their
         *  tallest above it, in levels.  The tiles round the pointer
         *  kept clear, and the tiles past the map's edge they thin out
         *  over, from and to.  The share of the field past the cover
         *  that a cloud's wisps reach.  The gain of a cloud's density
         *  over the cut its shape clears.  The share of a cloud's shadow
         *  that the ground under it takes. */
        {"sky_cover_least",       0.0f   },
        {"sky_scale",             24.0f  },
        {"sky_speed",             0.7f   },
        {"sky_wind_x",            0.8f   },
        {"sky_wind_y",            0.6f   },
        {"sky_altitude",          48.0f  },
        {"sky_thickness",         16.0f  },
        {"sky_cursor_hole",       6.0f   },
        {"sky_fade_from",         0.0f   },
        {"sky_fade_to",           16.0f  },
        {"sky_reach",             0.37f  },
        {"sky_wisp",              1.5f   },
        {"sky_shadow",            0.5f   },
        /*  The clouds (air.frag).  Their extinction per meter at their
         *  thickest.  The sun's light on them, which stands for the light
         *  scattered many times in a thick cloud.  The meters over
         *  which their shape and the detail that wears their edges
         *  repeat, and how hard the detail wears them.  The frame's
         *  pixels a side that one pixel of the clouds' march covers. */
        {"cloud_ext",             0.04f  },
        {"cloud_res",             2.0f   },
        {"cloud_light",           1.0f   },
        {"cloud_shape_m",         320.0f },
        {"cloud_detail_m",        60.0f  },
        {"cloud_erode",           0.2f   },
        /*  How the clouds scatter (Hillaire, 2016).  Two Henyey and
         *  Greenstein lobes, forward and back, and the back lobe's share.
         *  The back lobe lets a cloud with the sun behind the camera show
         *  its shape.  The octaves of light scattered many times, each with
         *  this share of the scattering and the extinction of the one
         *  before (Wrenninge, 2013).  The share of the sky's light that
         *  reaches the bottom of the layer against its top. */
        {"cloud_g0",              0.8f   },
        {"cloud_g1",              -0.4f  },
        {"cloud_back",            0.3f   },
        {"cloud_octaves",         8.0f   },
        {"cloud_octave_a",        0.95f  },
        {"cloud_amb_floor",       0.5f   },
        /*  The zoom out to which the clouds show whole, and the zoom from
         *  which they show no more.  Their shadows stay at every zoom. */
        {"cloud_zoom_full",       0.6f   },
        {"cloud_zoom_gone",       0.8f   },
        /*  The smog over a polluted place (air.frag).  Its extinction per
         *  meter where the pollution is at its most, and the height at
         *  which it thins by e.  The share of it that scatters, and the
         *  share that absorbs each of red, green and blue, more of the
         *  blue, as nitrogen dioxide does.  Its asymmetry. */
        {"smog_ext",              0.018f },
        {"smog_h",                30.0f  },
        {"smog_scatter",          0.9f   },
        {"smog_abs_r",            0.02f  },
        {"smog_abs_g",            0.04f  },
        {"smog_abs_b",            0.30f  },
        {"smog_g",                0.0f   },
    };
    /*  The outline view, which the View menu turns on, and the share of
     *  each edge a corner may take. */
    static const RKnob KNOB[] = {
        {"show_curves",        NULL,                     "view", 0.0f,   0.0f,   1.0f,  0.0f  },
        {"corner_share",       "corner share",           "fit",  0.50f,  0.25f,  1.00f, 0.50f },
        /*  A railway's look, in tiles.  Its gravel bed's half width, then
         *  the darker gravel under each track, how wide and how dark, then
         *  a buffer stop's size. */
        {"rail_bed_half",      "gravel bed half width",  "rail", 0.06f,  0.02f,  0.20f, 0.06f },
        {"rail_stain_half",    "dark gravel half width", "rail", 0.018f, 0.0f,   0.06f, 0.018f},
        {"rail_stain_dark",    "dark gravel darkness",   "rail", 0.5f,   0.0f,   1.0f,  0.5f  },
        {"rail_buffer_half",   "buffer stop half width", "rail", 0.022f, 0.005f, 0.06f, 0.022f},
        {"rail_buffer_depth",  "buffer stop depth",      "rail", 0.03f,  0.004f, 0.08f, 0.03f },
        {"rail_buffer_height", "buffer stop height",     "rail", 0.024f, 0.004f, 0.06f, 0.024f},
    };
    size_t i;
    for (i = 0; i < sizeof NUM / sizeof NUM[0]; ++i)
        geo_set(NUM[i].name, NUM[i].v);
    for (i = 0; i < sizeof KNOB / sizeof KNOB[0]; ++i)
        tune_declare(&KNOB[i]);
    tune_bind();
}
