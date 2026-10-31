/*  mesh/ground_bytes.c: what the building byte of a tile means to the
 *  ground.
 *  See mesh/ground_bytes.h.
 *
 *  Each answer is a range of bytes or a short table, so a test of the
 *  byte gives it.  A byte that no range names answers nought. */
#include "mesh/ground_bytes.h"

/*  A PIECE THE SADDLE LIFT APPLIES TO: a network piece on terrain code 13
 *  is drawn one step up, as the original draws it.  An elevated piece
 *  never is. */
int saddle_tile(uint8_t xbld)
{
    return xbld >= 0x0E && xbld <= 0x60;
}

/*  SOMETHING STANDS HERE: anything but bare ground and trees.  A tile that
 *  carries a structure gets a level pad, not the surface of the terrain. */
int tile_built(uint8_t xbld)
{
    return xbld >= 0x0E;
}

/*  A PIECE DRAWN BY A SLOPED SPRITE.  In the shipped cities, these pieces
 *  always stand on a sloped terrain code.  The others stand on flat ground.
 *  So the ground under one keeps its slope and is not leveled.  They are
 *  the power lines, roads, railways, highways and the elevated pieces. */
int tile_sloped(uint8_t xbld)
{
    return (xbld >= 0x10 && xbld <= 0x13) || (xbld >= 0x1F && xbld <= 0x22) ||
           (xbld >= 0x2E && xbld <= 0x31) || (xbld >= 0x3F && xbld <= 0x42) ||
           (xbld >= 0x61 && xbld <= 0x64);
}

/*  A BUILDING PROPER: it occupies a footprint and hangs its whole order on
 *  the anchor tile.  A network piece is drawn tile by tile.  The buildings
 *  begin where the networks and the structures end. */
int tile_building(uint8_t xbld)
{
    return xbld >= 0x70;
}

/*  AN ELEVATED PIECE.  It takes its order from the neighbor that owns the
 *  span, not from its own tile.  So a raised slab and the tile it crosses
 *  do not each claim the place of the painter. */
int tile_elevated(uint8_t xbld)
{
    return xbld >= 0x61 && xbld <= 0x6B;
}

/*  A TILE SOMETHING WAS PLACED ON, and not one that a zone grew.  The
 *  frame keeps its art in the map view, where the tint replaces every
 *  other sprite.  Everything from the power plants up is placed by the
 *  player. */
int tile_placed(uint8_t xbld)
{
    return xbld >= 0xC6;
}

/*  A TILE WHOSE ART THE MESH REPLACES by its byte alone.  No byte names
 *  one, because whether a road's tile is drawn depends on its whole
 *  road.  The drawing marks the tiles it draws (roads_drawn), and every
 *  other tile keeps its original sprite. */
int tile_meshed(uint8_t xbld)
{
    (void)xbld;
    return 0;
}

/*  THE TINT OF THE MAP VIEW for a tile something was placed on.  It is 11
 *  for the ten power plants, 12 for a park and 10 for the rest.  The
 *  value 0 leaves the tile to its zone. */
int structure_tint(uint8_t xbld)
{
    if (xbld >= 0xC6 && xbld <= 0xCF)
        return 11;
    if (xbld == 0xD5)
        return 12;
    return xbld >= 0xC6 ? 10 : 0;
}

/*  THE ZONE a zoning byte carries.  The low half is the zone the tile is
 *  zoned for, and the map view tints it.  The high half marks the corners
 *  of a footprint, and the tint does not read it. */
int zone_of(uint8_t xzon)
{
    return xzon & 15;
}
