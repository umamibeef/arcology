/*  terrain.c: what a tile's terrain byte means to the land.  It says
 *  whether the tile is water, the slope it carries, the corners it lifts,
 *  the way its top folds, and the saddle.  See terrain.h.
 *
 *  Each answer is a range of bytes or a short table, so a test of the
 *  byte gives it.  A byte that no range names answers nought. */
#include "terrain.h"

/*  WATER: terrain 0x10 and up.  Submerged and shore slopes, 0x10 to 0x2F,
 *  are a body at the table ALTM gives, over a bed at the level ALTM gives.
 *  Streams, canals and the waterfall, 0x30 and up, have their table at
 *  their level.  The original draws every one flat at its table (tile_alt
 *  in soft.c).  The low nibble says where the art puts its rim, not a
 *  height. */
int is_water(uint8_t xter)
{
    return xter >= 0x10;
}

/*  THE SLOPE a terrain byte carries, 0 flat.  The low nibble says which
 *  way the tile falls, and only below 0x40.  If not, a stream with a slope
 *  nibble among flat neighbors draws as a bump.  The art has no shape for
 *  the codes 14 and 15, so they are flat. */
int32_t slope_code(uint8_t xter)
{
    int32_t code = (int32_t)(xter & 15u);
    return xter < 0x40 && code <= 13 ? code : 0;
}

/*  WHICH CORNERS A SLOPE CODE LIFTS, as a mask.  The bits are NW, SW, SE
 *  and NE, from bit 0 up.  A set bit lifts that corner one level, and a
 *  lift is never more than one.  On screen NW is the top vertex of the
 *  diamond, NE its left, SW its right and SE its bottom.  The tool
 *  tools/terrain_shapes.py reads the fourteen masks off the sprites. */
static const unsigned char LIFTS[14] = {0, 9, 3, 6, 12, 11, 7, 14, 13, 1, 2, 4, 8, 5};

int corner_lifts(int32_t code)
{
    return code >= 0 && code < 14 ? LIFTS[code] : 0;
}

/*  THE DIAGONAL the top of a tile folds on: 1 for NE to SW, 0 for NW to
 *  SE.  It follows from the masks above.  A tile with one odd corner keeps
 *  a flat triangle on the other three, so the fold goes clear of the odd
 *  corner.  The fold of a saddle (code 13) is NE to SW whatever its
 *  corners say.  A plane is planar on the two diagonals.  So the fold is
 *  NE to SW where the odd corner is NW or SE: codes 5, 7, 9 and 11. */
static const unsigned char FOLD[14] = {0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1};

int cut_ne_sw(int32_t code)
{
    return code >= 0 && code < 14 ? FOLD[code] : 0;
}

/*  THE GROUND THE SADDLE LIFT APPLIES TO: terrain code 13 on bare land.
 *  The original draws a network piece there one step up ($17528: `cmpi.w
 *  #$d`, then -12), and only there. */
int saddle_terrain(uint8_t xter)
{
    return xter == 0x0D;
}
