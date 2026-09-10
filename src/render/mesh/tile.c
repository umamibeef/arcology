/*  mesh/tile.c: what a tile is, and the ground field.  See
 *  mesh/internal.h. */
#include <string.h>

#include "log.h"
#include "mesh/internal.h"
#include "pipeline.h"
#include "script.h"

/*  The tables and the field the other pieces read.  The state the
 *  segment pipeline carries across its stages. */
const int     EDGE_A[4]     = {NW, NE, SW, SW};
const int     EDGE_B[4]     = {NE, SE, SE, NW};
const int     NBR_A[4]      = {SW, NW, NW, SE};
const int     NBR_B[4]      = {SE, SW, NE, NE};
const int     EDGE_DR[4]    = {-1, 0, 1, 0};
const int     EDGE_DC[4]    = {0, 1, 0, -1};
const float   EDGE_N[4][3]  = {
    {0.0f,  -1.0f, 0.0f},
    {1.0f,  0.0f,  0.0f},
    {0.0f,  1.0f,  0.0f},
    {-1.0f, 0.0f,  0.0f}
};
float s_h[GRID * GRID]; /* the ground: one height per corner      */
float s_k[GRID * GRID]; /* curvature: positive in a hollow        */
float s_b[GRID * GRID]; /* the bed: the seabed at a corner that   */

/* ---- what a tile is ---------------------------------------------------- */

/*  What a tile's two bytes mean to the ground, all four the SCRIPT'S
 *  (scripts/ground_tiles.lua) and all four looked up at every tile of
 *  the map.  Read once a generation into tables of their own: the
 *  question is asked millions of times a build and answered by the byte
 *  alone.
 *
 *  With no rule nothing is water, nothing is built and nothing slopes,
 *  which is what a run that cannot find the scripts draws. */
/*  What a tile's two bytes mean to the ground, all of it the SCRIPT'S
 *  and pushed down by it (scripts/ground_tiles.lua).  A table of 256,
 *  read straight: the question is asked millions of times a build and
 *  answered by the byte alone.
 *
 *  With no table nothing is water, nothing is built and nothing slopes,
 *  which is what a run that cannot find the scripts draws. */

/*  Water.  Submerged and shore slopes are a body at ALTM's table over a
 *  bed at ALTM's level.  Streams, canals and the waterfall have their
 *  table at their level.  The original draws every one flat at its table
 *  (tile_alt in soft.c).  The low nibble says where the art puts its
 *  rim, not a height.  A stream with a slope nibble among flat neighbors
 *  read as a height draws as a bump. */
int is_water(uint8_t xter)
{
    return script_bytes("water_tiles")[xter];
}

/*  The slope a terrain byte carries, 0 flat. */
int32_t slope_code(uint8_t xter)
{
    return script_bytes("slope_codes")[xter];
}

/*  The map view's tint for a tile something was placed on, or 0 to leave
 *  it to the zone under it. */
int structure_tint(uint8_t xbld)
{
    return script_bytes("structure_tints")[xbld];
}

/*  A building proper: one with a footprint and an anchor, as against a
 *  network piece drawn tile by tile. */
int building_tile(uint8_t xbld)
{
    return script_bytes("building_tiles")[xbld];
}

/*  An elevated piece, which takes its order from the neighbor that owns
 *  the span rather than from its own tile. */
int elevated_tile(uint8_t xbld)
{
    return script_bytes("elevated_tiles")[xbld];
}

static float ground_of(const RCity *c, int32_t idx)
{
    return (float)rcity_alt_ground(c->altm[idx]);
}

static float table_of(const RCity *c, int32_t idx)
{
    return (float)rcity_alt_table(c->altm[idx]);
}

/*  The grid index of a tile's corner k. */
int32_t corner_gi(int32_t col, int32_t row, int k)
{
    return (row + ((k == SW || k == SE) ? 1 : 0)) * GRID + col +
           ((k == NE || k == SE) ? 1 : 0);
}

/*  A network piece on a saddle is drawn one step up.  Which ground
 *  counts as a saddle and which pieces take the lift are both the
 *  script's: `saddle_terrain` and `saddle_tiles`. */
int saddle_lift(const RCity *c, int32_t idx)
{
    return script_bytes("saddle_terrain")[c->xter[idx]] && script_bytes("saddle_tiles")[c->xbld[idx]];
}

/*  The tile's own plane: ALTM's level plus the slope code's lifts.  For
 *  a water body this is the bed. */
static void own_plane(const RCity *c, int32_t idx, float z[4])
{
    uint8_t mask = script_bytes("corner_lifts")[slope_code(c->xter[idx])];
    float   base = ground_of(c, idx);
    int     k;
    for (k = 0; k < 4; ++k)
        z[k] = base + (((mask >> k) & 1u) ? 1.0f : 0.0f);
}

/*  ---- WHERE EVERY TILE'S TOP COMES FROM -------------------------------
 *
 *  Six places, and which of them a tile draws from is
 *  arc.rules.terrain's: it walks the map itself, once a pass, before
 *  anything reads a tile.  Nothing here classifies a cell: this only
 *  holds the answer and fetches the height the answer names.
 *
 *  With no rule every tile falls to the field, which is a city with no
 *  water, no pads and no shelves in it. */
static uint8_t s_top[R_MAP * R_MAP];
static int32_t s_anchor[R_MAP * R_MAP]; /* the tile whose pad a footprint stands on */
static int32_t s_order[R_MAP * R_MAP];  /* the tile whose place in the painter's stack it takes */
static uint8_t s_vote[R_MAP * R_MAP];   /* how it votes for the corners it shares: land, base only, or water */
static uint8_t s_rest[R_MAP * R_MAP];   /* and where its top comes from once no pass is running */

void mesh_tops_reset(void)
{
    int32_t i;
    memset(s_top, TOP_FIELD, sizeof s_top);
    memset(s_rest, TOP_FIELD, sizeof s_rest);
    memset(s_vote, VOTE_LAND, sizeof s_vote);
    for (i = 0; i < R_MAP * R_MAP; ++i)
        s_anchor[i] = s_order[i] = i;
}

void mesh_top_is(int32_t at, int top, int32_t anchor, int32_t order, int vote, int rest)
{
    if (at < 0 || at >= R_MAP * R_MAP)
        return;
    s_vote[at]   = (uint8_t)(vote >= 0 && vote <= VOTE_WATER ? vote : VOTE_LAND);
    s_rest[at]   = (uint8_t)(rest >= 0 && rest <= TOP_PAD ? rest : TOP_FIELD);
    s_top[at]    = (uint8_t)(top >= 0 && top <= TOP_PAD ? top : TOP_FIELD);
    s_anchor[at] = anchor >= 0 && anchor < R_MAP * R_MAP ? anchor : at;
    s_order[at]  = order >= 0 && order < R_MAP * R_MAP ? order : at;
}

/*  What the corridors left on a tile, for the rule that has to choose
 *  between them.  1 says the corridor wrote the tile's own shelf, 2 it
 *  covers all four of the tile's corners, 0 neither.  This is the
 *  pipeline's own state offered to be read, not a reading of the city. */
int mesh_top_graded(int32_t at)
{
    int32_t col, row;
    if (at < 0 || at >= R_MAP * R_MAP)
        return 0;
    if (s_tilez[at * 4] < 1e8f)
        return 1;
    col = at % R_MAP, row = at / R_MAP;
    return s_corr[row * GRID + col] == 1 && s_corr[row * GRID + col + 1] == 1 &&
                   s_corr[(row + 1) * GRID + col] == 1 && s_corr[(row + 1) * GRID + col + 1] == 1
               ? 2
               : 0;
}

/*  The level a flat pad sits at: on the water the water's surface (a
 *  marina), on a saddle one step up, else its ground. */
static float pad_level(const RCity *c, int32_t idx);

float node_altitude(const RCity *c, int32_t col, int32_t row)
{
    /*  What a node stands at: the leveled height of its own tile.  Every
     *  corridor that reaches this node spurs to this one number.  So two
     *  segments meeting here agree by construction, and there is nothing
     *  global to solve. */
    int32_t idx = row * R_MAP + col;
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return 0.0f;
    return pad_level(c, idx);
}

static float pad_level(const RCity *c, int32_t idx)
{
    if (is_water(c->xter[idx]))
        return table_of(c, idx);
    return ground_of(c, idx) + (saddle_lift(c, idx) ? 1.0f : 0.0f);
}

/*  Rule 2: what a tile draws, its four corner heights in the enum's
 *  order.  Both sides of every edge go through here. */
Kind tile_top(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float z[4])
{
    int32_t idx = row * R_MAP + col;
    int     k;
    (void)mask_bit;
    /*  Between builds nothing is composing, and a tile answers where its
     *  top stands at rest.  Which is not where the building pass drew it
     *  from.  A corridor's shelf is the pass's own reading of the tile,
     *  and what a car or the inspector meets there is the tile itself. */
    switch (s_pass == 0 ? s_rest[idx] : s_top[idx])
    {
    case TOP_PAD_ANCHOR:
    {
        float lv = pad_level(c, s_anchor[idx]);
        for (k = 0; k < 4; ++k)
            z[k] = lv;
        return T_PAD;
    }
    case TOP_WATER:
    {
        float t = table_of(c, idx);
        for (k = 0; k < 4; ++k)
            z[k] = t;
        return T_WATER;
    }
    case TOP_SHELF:
        /*  A corridor's tile IS the corridor's surface: its four corners
         *  were graded in the first pass.  So it draws its own shelf and
         *  the corridor comes out as one continuous run.  The terrain
         *  field is never touched: a corner there is shared with the
         *  tile next door.  So notching through it would drag the ground
         *  outside the corridor down with it.  The step between the
         *  notch and the ground it was cut from is closed by the wall
         *  rule, which is what a notch looks like. */
        for (k = 0; k < 4; ++k)
            z[k] = s_tilez[idx * 4 + k];
        return T_LAND;
    case TOP_PLANE:
        own_plane(c, idx, z);
        return T_PLANE;
    case TOP_PAD:
    {
        float lv = pad_level(c, idx);
        for (k = 0; k < 4; ++k)
            z[k] = lv;
        return T_PAD;
    }
    default:
        for (k = 0; k < 4; ++k)
            z[k] = s_h[corner_gi(col, row, k)];
        return T_LAND;
    }
}

/*  A tile's surface is water: a water tile, or a pad standing on the
 *  water (a marina stands on the water's surface). */
int water_top(const RCity *c, int32_t idx, Kind k)
{
    return k == T_WATER || (k == T_PAD && is_water(c->xter[idx]));
}

/*  The painter's index of the sweep, so the mesh composes with the
 *  sprites exactly as the software terrain pass does.  WHOSE place a
 *  tile takes is arc.rules.terrain's.  A building's footprint takes its
 *  anchor's and an elevated piece the neighbor's that owns its span
 *  ($173B8).  And this only turns the answer into an index. */
float tile_order(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit)
{
    int32_t at = s_order[row * R_MAP + col], ar = at / R_MAP, ac = at % R_MAP;
    (void)c, (void)mask_bit;
    return (float)((ac + ar) * R_MAP + ar + 1);
}

/* ---- the field --------------------------------------------------------- */

/*  Rule 1.  One height per corner from the land's votes, never below the
 *  water the corner touches.  A corner with no land is the water's table
 *  there.  The bed at a corner that touches water is the mean of the
 *  water tiles' beds there, never above the surface drawn over it.
 *  Elsewhere it is the ground.
 *
 *  HOW EACH TILE VOTES is arc.rules.terrain's: a tile that draws its own
 *  pad or plane does not vote with it.  Where it must vote it votes with
 *  its BASE, so a corner every tile around is a structure falls back to
 *  that average.  The land keeps its shape and the wall rule closes the
 *  whole difference.  Nothing here reads a cell to decide which is
 *  which. */
void build_field(const RCity *c)
{
    static float   sum[GRID * GRID], bsum[GRID * GRID], bmin[GRID * GRID], ssum[GRID * GRID];
    static uint8_t cnt[GRID * GRID], bcnt[GRID * GRID], scnt[GRID * GRID];
    const uint8_t  mask_bit = city_corner_mask(c->rotation);
    int32_t        col, row, g;
    int            k;
    memset(sum, 0, sizeof sum);
    memset(cnt, 0, sizeof cnt);
    memset(bsum, 0, sizeof bsum);
    memset(bcnt, 0, sizeof bcnt);
    memset(ssum, 0, sizeof ssum);
    memset(scnt, 0, sizeof scnt);
    for (g = 0; g < GRID * GRID; ++g)
    {
        s_h[g]  = -1.0f;
        bmin[g] = 1e9f;
    }
    for (row = 0; row < R_MAP; ++row)
        for (col = 0; col < R_MAP; ++col)
        {
            int32_t idx  = row * R_MAP + col;
            int     vote = s_vote[idx];
            float   z[4], top = 0.0f;
            own_plane(c, idx, z);
            if (vote == VOTE_WATER)
            {
                /*  The surface drawn over this bed: the table, or the
                 *  pad of a building standing on the water.  This can
                 *  lie below the table when its anchor is land a level
                 *  down.  The bed never
                 *  rises above what is drawn over it. */
                float zt[4];
                tile_top(c, col, row, mask_bit, zt);
                top = zt[0];
            }
            for (k = 0; k < 4; ++k)
            {
                int32_t gi = corner_gi(col, row, k);
                if (vote == VOTE_WATER)
                {
                    bsum[gi] += z[k];
                    bcnt[gi]++;
                    if (top < bmin[gi])
                        bmin[gi] = top;
                }
                else if (vote == VOTE_BASE)
                {
                    ssum[gi] += ground_of(c, idx);
                    scnt[gi]++;
                }
                else
                {
                    sum[gi] += z[k];
                    cnt[gi]++;
                }
            }
        }
    for (g = 0; g < GRID * GRID; ++g)
        if (cnt[g])
            s_h[g] = sum[g] / (float)cnt[g];
        else if (scnt[g])
            s_h[g] = ssum[g] / (float)scnt[g]; /* a corner among structures only */
    /*  The shoreline is at the water level.  ALTM keeps one level per
     *  tile, and the slope code's lifts.  Along a shore the land's low
     *  corners lie at the bed's level, one below the water they touch:
     *  725 corners across the shipped cities.  The original never shows
     *  them, since the water tile's flat diamond at the table, with its
     *  sand rim, is painted over them.  The mesh showed a pit at every
     *  one.  So a land corner that touches water is never below that
     *  water, and a corner no land reaches is the water's table. */
    for (row = 0; row < R_MAP; ++row)
        for (col = 0; col < R_MAP; ++col)
        {
            int32_t idx = row * R_MAP + col;
            float   lv;
            if (s_vote[idx] != VOTE_WATER)
                continue;
            lv = table_of(c, idx);
            for (k = 0; k < 4; ++k)
                if (s_h[corner_gi(col, row, k)] < lv)
                    s_h[corner_gi(col, row, k)] = lv;
        }
    for (g = 0; g < GRID * GRID; ++g)
    {
        if (s_h[g] < 0.0f)
            s_h[g] = 0.0f;
        if (bcnt[g])
        {
            s_b[g] = bsum[g] / (float)bcnt[g];
            if (s_b[g] > bmin[g])
                s_b[g] = bmin[g];
        }
        else
            s_b[g] = s_h[g];
    }
    /*  Curvature is the Laplacian of the height field.  It is positive
     *  where the corner sits below the mean of its four neighbors, which
     *  is a hollow, and negative on a ridge.  The ground shader reads it
     *  as moisture. */
    for (row = 0; row < GRID; ++row)
        for (col = 0; col < GRID; ++col)
        {
            int32_t c0 = col > 0 ? col - 1 : col, c1 = col < GRID - 1 ? col + 1 : col;
            int32_t r0 = row > 0 ? row - 1 : row, r1 = row < GRID - 1 ? row + 1 : row;
            float   mean          = 0.25f * (s_h[row * GRID + c0] + s_h[row * GRID + c1] +
                                             s_h[r0 * GRID + col] + s_h[r1 * GRID + col]);
            s_k[row * GRID + col] = mean - s_h[row * GRID + col];
        }
}
