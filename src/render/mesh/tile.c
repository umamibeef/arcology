/*  mesh/tile.c -- what a tile is, and the ground field.  Split out of
 *  mesh.c; see mesh/internal.h. */
#include <string.h>

#include "log.h"
#include "mesh/internal.h"
#include "net/internal.h"
#include "script.h"

/*  The tables and the field the other pieces read; the state the
 *  segment pipeline carries across its stages. */
const uint8_t CODE_MASK[14] = {0, 9, 3, 6, 12, 11, 7, 14, 13, 1, 2, 4, 8, 5};
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
 *  bed at ALTM's level; streams, canals and the waterfall have their
 *  table at their level.  The original draws every one flat at its table
 *  (tile_alt in soft.c): the low nibble says where the art puts its rim,
 *  not a height, and a stream with a slope nibble among flat neighbours
 *  read as a height draws as a bump. */
int is_water(uint8_t xter)
{
    return script_bytes("water_tiles")[xter];
}

/*  A structure: anything but bare ground and trees. */
static int is_structure(uint8_t xbld)
{
    return script_bytes("built_tiles")[xbld];
}

/*  A network piece drawn by a sprite that fits a slope, so the ground
 *  under it keeps its slope rather than being levelled. */
static int sloped_piece(uint8_t b)
{
    return script_bytes("sloped_tiles")[b];
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

/*  An elevated piece, which takes its order from the neighbour that owns
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

/*  A network piece on a saddle, terrain code 13, is drawn one step up
 *  by the original ($17528: `cmpi.w #$d`, then -12), and only there,
 *  and never an elevated piece. */
int saddle_lift(const RCity *c, int32_t idx)
{
    uint8_t xter = c->xter[idx], xbld = c->xbld[idx];
    return xter < 0x10u && (xter & 0x0F) == 13 && script_bytes("saddle_tiles")[xbld];
}

/*  The tile's own plane: ALTM's level plus the slope code's lifts.  For
 *  a water body this is the bed. */
static void own_plane(const RCity *c, int32_t idx, float z[4])
{
    uint8_t mask = CODE_MASK[slope_code(c->xter[idx])];
    float   base = ground_of(c, idx);
    int     k;
    for (k = 0; k < 4; ++k)
        z[k] = base + (((mask >> k) & 1u) ? 1.0f : 0.0f);
}

/*  The anchor of a building's footprint: the tile carrying the
 *  rotation's corner bit, up to three tiles north and east. */
static int32_t anchor_of(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit)
{
    int32_t idx = row * R_MAP + col, best = 99, ai = idx;
    uint8_t b = c->xbld[idx];
    int     dr, dc;
    if (!building_tile(b) || (c->xzon[idx] & mask_bit))
        return idx;
    for (dr = 0; dr >= -3; --dr)
        for (dc = 0; dc <= 3; ++dc)
        {
            int32_t ar = row + dr, ac = col + dc, i;
            if (ar < 0 || ac >= R_MAP || -dr + dc >= best)
                continue;
            i = ar * R_MAP + ac;
            if (c->xbld[i] == b && (c->xzon[i] & mask_bit))
            {
                best = -dr + dc;
                ai   = i;
            }
        }
    return ai;
}

/*  The level a flat pad sits at: on the water the water's surface (a
 *  marina), on a saddle one step up, else its ground. */
static float pad_level(const RCity *c, int32_t idx);

float node_altitude(const RCity *c, int32_t col, int32_t row)
{
    /*  What a node stands at: the levelled height of its own tile.  Every
     *  corridor that reaches this node ramps to this one number, so two
     *  segments meeting here agree by construction and there is nothing
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
/*  Does a corridor level this tile?  Which bytes it may is the script's
 *  (scripts/ground_tiles.lua): every surface network piece, and the
 *  pieces a viaduct flies over, which carry a road or a line under the
 *  deck.  A tile shut out of this draws a flat pad instead, and a road
 *  climbing under a viaduct then steps where its shelf asked for a
 *  slope.  The deck itself levels nothing: it stands clear and its
 *  columns take up the difference. */
static int corridor_levels(uint8_t xbld)
{
    return script_bytes("levelling_tiles")[xbld];
}

Kind tile_top(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float z[4])
{
    int32_t idx  = row * R_MAP + col;
    uint8_t xter = c->xter[idx], xbld = c->xbld[idx];
    int     k;
    if (building_tile(xbld))
    {
        float lv = pad_level(c, anchor_of(c, col, row, mask_bit));
        for (k = 0; k < 4; ++k)
            z[k] = lv;
        return T_PAD;
    }
    if (is_water(xter))
    {
        float t = table_of(c, idx);
        for (k = 0; k < 4; ++k)
            z[k] = t;
        return T_WATER;
    }
    /*  A corridor's tile IS the corridor's surface: its four corners were
     *  graded in the first pass, so it draws the field like open land and
     *  the corridor comes out as one continuous shelf.  The terrain field
     *  is never touched: a corner there is shared with the tile next door,
     *  so notching through it would drag the ground outside the corridor
     *  down with it.  The step between the notch and the ground it was cut
     *  from is closed by the wall rule, which is what a notch looks like. */
    if (is_structure(xbld) && corridor_levels(xbld) && s_pass == 2 &&
        s_tilez[(row * R_MAP + col) * 4] < 1e8f)
    {
        for (k = 0; k < 4; ++k)
            z[k] = s_tilez[(row * R_MAP + col) * 4 + k];
        return T_LAND;
    }
    if (is_structure(xbld) && corridor_levels(xbld) && s_pass == 2 && s_corr[row * GRID + col] == 1 &&
        s_corr[row * GRID + col + 1] == 1 && s_corr[(row + 1) * GRID + col] == 1 && s_corr[(row + 1) * GRID + col + 1] == 1)
    {
        for (k = 0; k < 4; ++k)
            z[k] = s_h[corner_gi(col, row, k)];
        return T_LAND;
    }
    if (is_structure(xbld))
    {
        /*  A highway ramp carries its own deck, lofted down to the
         *  ground by the band walk, so the tile under it is flat.  Given
         *  its own plane on a saddle it draws a pair of twisted brown
         *  wedges at the foot of the elevated highway instead. */
        if (slope_code(xter) != 0 && sloped_piece(xbld) && !elevated_tile(xbld))
        {
            own_plane(c, idx, z);
            return T_PLANE;
        }
        {
            float lv = pad_level(c, idx);
            for (k = 0; k < 4; ++k)
                z[k] = lv;
        }
        return T_PAD;
    }
    for (k = 0; k < 4; ++k)
        z[k] = s_h[corner_gi(col, row, k)];
    return T_LAND;
}

/*  A tile's surface is water: a water tile, or a pad standing on the
 *  water (a marina stands on the water's surface). */
int water_top(const RCity *c, int32_t idx, Kind k)
{
    return k == T_WATER || (k == T_PAD && is_water(c->xter[idx]));
}

/*  The painter's index of the sweep, so the mesh composes with the
 *  sprites exactly as the software terrain pass does.  A building's
 *  footprint takes its anchor's, as the elevated 2x2 pieces do
 *  ($173B8), so the art stays in front of its own pad. */
float tile_order(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit)
{
    int32_t idx = row * R_MAP + col;
    uint8_t b   = c->xbld[idx];
    if (building_tile(b))
    {
        int32_t ai = anchor_of(c, col, row, mask_bit);
        int32_t ar = ai / R_MAP, ac = ai % R_MAP;
        return (float)((ac + ar) * R_MAP + ar + 1);
    }
    if (elevated_tile(b) && !(c->xzon[idx] & mask_bit))
    {
        static const int dr[3] = {0, -1, -1};
        static const int dc[3] = {1, 1, 0};
        int              q;
        for (q = 0; q < 3; ++q)
        {
            int32_t ar = row + dr[q], ac = col + dc[q], ai;
            if (ar < 0 || ac >= R_MAP)
                continue;
            ai = ar * R_MAP + ac;
            if (elevated_tile(c->xbld[ai]) && (c->xzon[ai] & mask_bit))
                return (float)((ac + ar) * R_MAP + ar + 1);
        }
    }
    return (float)((col + row) * R_MAP + row + 1);
}

/* ---- the field --------------------------------------------------------- */

/*  Rule 1.  One height per corner from the land's votes, never below the
 *  water the corner touches; a corner with no land is the water's table
 *  there.  The bed at a corner that touches
 *  water is the mean of the water tiles' beds there, never above the
 *  surface drawn over it; elsewhere it is the ground. */
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
            uint8_t xter = c->xter[idx];
            float   z[4], top = 0.0f;
            int     structure = !is_water(xter) && is_structure(c->xbld[idx]);
            own_plane(c, idx, z);
            if (is_water(xter))
            {
                /*  The surface drawn over this bed: the table, or the pad
                 *  of a building standing on the water, which can lie
                 *  below the table when its anchor is land a level down
                 *  (The Bahamas, column 46, row 28); the bed never rises
                 *  above what is drawn over it. */
                float zt[4];
                tile_top(c, col, row, mask_bit, zt);
                top = zt[0];
            }
            for (k = 0; k < 4; ++k)
            {
                int32_t gi = corner_gi(col, row, k);
                if (is_water(xter))
                {
                    bsum[gi] += z[k];
                    bcnt[gi]++;
                    if (top < bmin[gi])
                        bmin[gi] = top;
                }
                else if (structure)
                {
                    /*  A structure draws its own pad or plane, not the
                     *  field, so it does not vote -- and where it must, it
                     *  votes with its BASE and not its plane: a corner
                     *  every tile around is a structure falls back to this
                     *  average.  The land keeps its shape and the wall rule
                     *  closes the whole difference (Oakland, column 104,
                     *  row 44, is a saddle at the foot of an elevated
                     *  highway). */
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
    /*  The shoreline is at the water level.  ALTM keeps one level per tile
     *  and the slope code's lifts, and along a shore the land's low corners
     *  lie at the bed's level, one below the water they touch: 725 corners
     *  across the shipped cities.  The original never shows them, since the
     *  water tile's flat diamond at the table, with its sand rim, is
     *  painted over them; the mesh showed a pit at every one.  So a land
     *  corner that touches water is never below that water, and a corner no
     *  land reaches is the water's table. */
    for (row = 0; row < R_MAP; ++row)
        for (col = 0; col < R_MAP; ++col)
        {
            int32_t idx = row * R_MAP + col;
            float   lv;
            if (!is_water(c->xter[idx]))
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
    /*  Curvature: the Laplacian of the height field, positive where the
     *  corner sits below the mean of its four neighbours, a hollow, and
     *  negative on a ridge.  The ground shader reads it as moisture. */
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
