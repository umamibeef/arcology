/*  world.h: the world, a map of any size with any number of cities on it.
 *
 *  The map is one chunk store (tiles.h), made where anything is.  So the
 *  world has no size of its own.  It is as large as the chunks it holds,
 *  and it grows when something is put down past them.
 *
 *  A CITY is a City the simulation runs, placed at an origin in the
 *  world.  Its tiles are the world's: the city reads and writes the
 *  world's store through the tile accessors (sim.h), counting its own
 *  window from the origin.  Nothing is copied in or out.
 *
 *  A tile is OWNED by the city that zoned it or built on it, and by no
 *  city when it is bare.  The owner rides in the store with the tile. */
#ifndef WORLD_H
#define WORLD_H

#include "sim.h"

/*  A city on the map.  Its window's corner is the City's own (ox, oy), a
 *  multiple of TILE_CHUNK tiles, so its window lies on whole chunks. */
typedef struct WorldCity
{
    City *city;
} WorldCity;

/*  The most cities a world holds: a tile's owner is a byte. */
#define WORLD_MAX_CITIES 254

typedef struct World
{
    TileMap    map;
    WorldCity *city;
    int32_t    n_city, cap_city;
} World;

/*  0 on success. */
int world_init(World *w);
/*  Every chunk and every city, the cities' own memory among them.  A
 *  city slot set to NULL is left to whoever took it. */
void world_free(World *w);

/*  The chunk at a chunk's place, NULL when there is none and `make` is
 *  0.  With `make` the chunk is made, every tile bare land at altitude
 *  0. */
TileChunk *world_chunk(World *w, int32_t cx, int32_t cy, int make);
/*  The chunk a tile lies in, and the tile's index inside it. */
TileChunk *world_at(World *w, int32_t x, int32_t y, int make, int32_t *i);

/*  Place a city on the map with its window's corner at x0, y0.  The
 *  world takes the City, which was allocated with calloc and holds its
 *  tiles in a store of its own (city_load).  Every tile of the window is
 *  copied onto the map as it is, and the city owns each one it zoned or
 *  built on.  The answer is the slot.  It is -1 for an origin that is not
 *  a multiple of TILE_CHUNK, a window with a tile owned already, or a full
 *  world. */
int world_add_city(World *w, City *c, int32_t x0, int32_t y0);

/*  Place a city whose tiles the map holds already, as a world read from
 *  a file does.  The slot, or -1. */
int world_place_city(World *w, City *c, int32_t x0, int32_t y0);

/*  Widen city `i`'s window to w by h tiles (city_grow), and bind every
 *  city on the map to the store as it then stands.  0 on success. */
int world_city_grow(World *w, int i, int32_t cw, int32_t ch);

/*  Set the street layer's edge between two tiles side by side, each by its
 *  place in the world.  The edge is kept by the tile with the lower column
 *  or row (sim.h, STREET_*).  0 on success. */
int world_street_edge(World *w, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t edge);

/*  Whether a tile is something a city owns: zoned, or built on with
 *  more than trees. */
int world_claims(uint8_t xzon, uint8_t xbld);

/*  The tiles the chunks cover, x0 and y0 inclusive, x1 and y1 not.  All
 *  four are 0 for a world with no chunk. */
void world_bounds(const World *w, int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1);

#endif
