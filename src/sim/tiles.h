/*  tiles.h: the store every tile of the world lives in, and the one way
 *  to reach a tile.
 *
 *  The map is held in CHUNKS of TILE_CHUNK tiles a side, made where
 *  anything is.  A chunk holds every layer of its tiles, each layer one
 *  run of memory, row by row, 64 bytes aligned.  All the layers of a
 *  chunk take about 12 KB, so a pass that works one chunk works in the
 *  first cache.  A pass that reads one layer reads one run, and one that
 *  reads four reads four runs side by side.
 *
 *  A DIRECTORY finds the chunk of a tile.  It is a flat array of chunk
 *  pointers over the chunks the map spans, with a bias for coordinates
 *  below nought.  A tile is two loads away, by shifts and masks, with no
 *  hash and no branch.  A slot with no chunk of its own points at one
 *  bare chunk that the store keeps.  So a read of open land needs no
 *  test.  Nothing writes to that chunk: the tiles a city works are made
 *  before it works them (tilemap_reserve).
 *
 *  The half and quarter resolution layers lie in the same chunks: a
 *  chunk holds the half cells and the quarter cells of its own tiles. */
#ifndef TILES_H
#define TILES_H

#include <stddef.h>
#include <stdint.h>

#define TILE_CHUNK      32
#define TILE_SHIFT      5
#define TILE_MASK       (TILE_CHUNK - 1)
#define TILE_HALF_CHUNK (TILE_CHUNK / 2)
#define TILE_QTR_CHUNK  (TILE_CHUNK / 4)

#define TILE_ALIGN __attribute__((aligned(64)))

typedef struct TileChunk
{
    uint16_t altm[TILE_CHUNK * TILE_CHUNK] TILE_ALIGN;
    uint8_t  xbld[TILE_CHUNK * TILE_CHUNK] TILE_ALIGN;
    uint8_t  xzon[TILE_CHUNK * TILE_CHUNK] TILE_ALIGN;
    uint8_t  xter[TILE_CHUNK * TILE_CHUNK] TILE_ALIGN;
    uint8_t  xbit[TILE_CHUNK * TILE_CHUNK] TILE_ALIGN;
    uint8_t  xund[TILE_CHUNK * TILE_CHUNK] TILE_ALIGN;
    uint8_t  xtxt[TILE_CHUNK * TILE_CHUNK] TILE_ALIGN;
    /*  0 for a tile nobody owns, else the owning city's slot plus one. */
    uint8_t owner[TILE_CHUNK * TILE_CHUNK] TILE_ALIGN;
    /*  The player's street data, which the original does not hold.  The
     *  low four bits are the tile's east edge and the high four its south
     *  edge (STREET_* in sim.h).  The simulation never reads it. */
    uint8_t street[TILE_CHUNK * TILE_CHUNK] TILE_ALIGN;
    uint8_t xtrf[TILE_HALF_CHUNK * TILE_HALF_CHUNK] TILE_ALIGN;
    uint8_t xplt[TILE_HALF_CHUNK * TILE_HALF_CHUNK] TILE_ALIGN;
    uint8_t xval[TILE_HALF_CHUNK * TILE_HALF_CHUNK] TILE_ALIGN;
    uint8_t xcrm[TILE_HALF_CHUNK * TILE_HALF_CHUNK] TILE_ALIGN;
    uint8_t xplc[TILE_QTR_CHUNK * TILE_QTR_CHUNK] TILE_ALIGN;
    uint8_t xfir[TILE_QTR_CHUNK * TILE_QTR_CHUNK] TILE_ALIGN;
    uint8_t xpop[TILE_QTR_CHUNK * TILE_QTR_CHUNK] TILE_ALIGN;
    uint8_t xrog[TILE_QTR_CHUNK * TILE_QTR_CHUNK] TILE_ALIGN;
    int32_t cx, cy; /* the chunk's place, in chunks.  Either may be below nought */
} TileChunk;

typedef struct TileMap
{
    TileChunk **dir;      /* w by h slots, row by row */
    int32_t     cx0, cy0; /* the chunk of the first slot */
    int32_t     w, h;
    TileChunk **all; /* every chunk made, in the order it was made */
    int32_t     n, cap;
    TileChunk  *bare; /* what an empty slot points at */
    /*  1 where the street layer was read from a file or worked out.  0
     *  where the tiles came from a file that does not carry it, so it is
     *  still to be worked out from the city. */
    int has_streets;
    /*  1 where the world drives on the left, 0 on the right.  The world's
     *  own setting, saved in its manifest.  The network reads it for the
     *  way a ramp's lane runs. */
    int drive_left;
} TileMap;

/*  The chunk a tile lies in.  The tile must lie in the directory. */
static inline TileChunk *tile_chunk(const TileMap *m, int32_t x, int32_t y)
{
    return m->dir[((y >> TILE_SHIFT) - m->cy0) * m->w + ((x >> TILE_SHIFT) - m->cx0)];
}

/*  A tile's index inside its chunk, and a half or quarter cell's. */
static inline int32_t tile_index(int32_t x, int32_t y)
{
    return ((y & TILE_MASK) << TILE_SHIFT) | (x & TILE_MASK);
}

static inline int32_t tile_half_index(int32_t hx, int32_t hy)
{
    return ((hy & (TILE_HALF_CHUNK - 1)) << (TILE_SHIFT - 1)) | (hx & (TILE_HALF_CHUNK - 1));
}

static inline int32_t tile_qtr_index(int32_t qx, int32_t qy)
{
    return ((qy & (TILE_QTR_CHUNK - 1)) << (TILE_SHIFT - 2)) | (qx & (TILE_QTR_CHUNK - 1));
}

/*  0 on success.  A new map spans no chunk. */
int  tilemap_init(TileMap *m);
void tilemap_free(TileMap *m);
/*  Make every chunk the tiles x0..x1-1, y0..y1-1 lie in, and widen the
 *  directory to them.  0 on success. */
int tilemap_reserve(TileMap *m, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
/*  Widen the directory to the tiles x0..x1-1, y0..y1-1 without making
 *  their chunks: a slot it adds points at the bare chunk.  0 on
 *  success. */
int tilemap_cover(TileMap *m, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
/*  The chunk at a chunk's place: NULL where none is made, unless `make`. */
TileChunk *tilemap_chunk(TileMap *m, int32_t cx, int32_t cy, int make);
/*  Make `dst` hold what `src` holds, chunk for chunk.  0 on success. */
int tilemap_copy(TileMap *dst, const TileMap *src);

#endif
