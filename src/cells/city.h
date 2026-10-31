/*  city.h: the city's cells, as the road network and the renderer are
 *  allowed to see them.  The network and the renderer read the cells and
 *  never write them.  Their own read-only view type, RCity, holds that
 *  line, so this file does not include the simulation's header.  The
 *  adapter, adapt.c, fills an RCity from a `const City *`.  Nothing that
 *  reads the view can reach the simulation's state. */
#ifndef R_CITY_H
#define R_CITY_H

#include <stddef.h>
#include <stdint.h>

#define R_MAX_THINGS 64 /* the fullest shipped city has 40 */

/*  THE MAP'S SIZE, in tiles a side.  It is a value of the run, not of
 *  the build.  The renderer draws one map at a time, and r_map_set
 *  changes the size before a map of another size is drawn.  A city from
 *  a save file is 128 tiles a side.  The half and quarter layers hold
 *  one cell for each two and four tiles a side. */
extern int32_t r_map;
/*  The world is kept in chunks of this many tiles a side.  The mesh, the
 *  stores and the road network's seams are cut at their borders. */
#define MESH_CHUNK 32
#define r_half     (r_map / 2)
#define r_qtr      (r_map / 4)

/*  A BUFFER THE SIZE OF THE MAP: `per` elements of `type` for each tile.
 *  It is allocated, all zero, when the program starts, and again each
 *  time r_map_set changes the size.  So it reads as a file-scope array
 *  would, at every size.  `store` is `static` for a buffer of one file
 *  and empty for one a header declares `extern`. */
#define R_MAP_BUF(store, type, name, per)                             \
    store type *name;                                                 \
    static void name##_map_reg(void) __attribute__((constructor));    \
    static void name##_map_reg(void)                                  \
    {                                                                 \
        r_map_register((void **)&name, sizeof(type) * (size_t)(per)); \
    }

/*  Two buffers of one name, one for each of the two families a store
 *  keeps apart: `name[0]` and `name[1]`. */
#define R_MAP_BUF2(store, type, name, per)                               \
    store type *name[2];                                                 \
    static void name##_map_reg(void) __attribute__((constructor));       \
    static void name##_map_reg(void)                                     \
    {                                                                    \
        r_map_register((void **)&name[0], sizeof(type) * (size_t)(per)); \
        r_map_register((void **)&name[1], sizeof(type) * (size_t)(per)); \
    }

/*  A BUFFER ON THE MAP'S CORNERS: `per` elements of `type` for each
 *  corner the tiles share, (n + 1) x (n + 1). */
#define R_GRID_BUF(store, type, name, per)                             \
    store type *name;                                                  \
    static void name##_map_reg(void) __attribute__((constructor));     \
    static void name##_map_reg(void)                                   \
    {                                                                  \
        r_grid_register((void **)&name, sizeof(type) * (size_t)(per)); \
    }

/*  A STORE THAT GROWS WITH THE MAP: `count` elements of `type` at 128
 *  tiles a side, and more in proportion to the map's tiles.  The count
 *  at the map's own size is r_scaled(count). */
#define R_SCALED_BUF(store, type, name, count)                            \
    store type *name;                                                     \
    static void name##_map_reg(void) __attribute__((constructor));        \
    static void name##_map_reg(void)                                      \
    {                                                                     \
        r_scaled_register((void **)&name, sizeof(type), (size_t)(count)); \
    }
#define r_scaled(count) ((int)(((int64_t)(count) * r_map * r_map + 128 * 128 - 1) / (128 * 128)))

void r_map_register(void **buf, size_t bytes_per_tile);
void r_grid_register(void **buf, size_t bytes_per_corner);
void r_scaled_register(void **buf, size_t bytes, size_t count);

/*  The bytes a buffer the size of the map holds, for a memset or a
 *  memcpy over the whole of it.  0 for a pointer that is not one. */
size_t r_map_bytes(const void *buf);

/*  Set the map's size and allocate every buffer again, all zero.  The
 *  same size keeps every buffer as it is.  Answers 0, or -1 when the
 *  memory is not there.  Call it only when no build runs. */
int r_map_set(int32_t n);

/*  Altitude, out of ALTM.  The word holds two heights: the land in bits
 *  0 to 4 and the water table in bits 5 to 9.  Which one a tile stands
 *  at is XTER's business.  Below 0x10 a tile is dry and reads the land.
 *  At or above it the surface is water and reads the table.
 *
 *  That split is the SAVE FORMAT'S own.  It is written here beside the
 *  type it decodes.  The ground's
 *  own is_water (terrain.c) names the same boundary for the
 *  ground the mesh draws.  The two say one thing about the format, not two
 *  things about the world. */
static inline int rcity_alt_ground(unsigned altm)
{
    return (int)(altm & 0x1Fu);
}

static inline int rcity_alt_table(unsigned altm)
{
    return (int)((altm >> 5) & 0x1Fu);
}

static inline int rcity_alt_surface(unsigned altm, unsigned xter)
{
    return xter < 0x10u ? rcity_alt_ground(altm) : rcity_alt_table(altm);
}

/*  THE MAP: the land the city stands on.  Altitude and water, and
 *  nothing anybody built.  The city reads it and never writes it. */
typedef struct
{
    uint16_t *altm; /* low 5 bits are the altitude level */
    uint8_t  *xter; /* the terrain and the water over it */
} RMap;

/*  The renderer's view of the world: the map, and the CITY on it, which
 *  is everything built there. */
typedef struct
{
    int32_t n;          /* tiles a side */
    int32_t drive_left; /* 1 where the world drives on the left */
    int32_t x0, y0;     /* the world's tile at the view's first */
    RMap    map;

    /* full resolution, n x n */
    uint8_t *xbld;
    uint8_t *xzon;
    uint8_t *xund;
    uint8_t *xtxt;
    uint8_t *xbit;
    uint8_t *street; /* the player's street layer (sim.h, STREET_*): each tile's edges to the next column and row */

    /* half resolution, n/2 x n/2 */
    uint8_t *xtrf;
    uint8_t *xplt;
    uint8_t *xval;
    uint8_t *xcrm;

    /* quarter resolution, n/4 x n/4 */
    uint8_t *xplc;
    uint8_t *xfir;
    uint8_t *xpop;
    uint8_t *xrog;

    void *block; /* the one allocation every layer above is part of */

    /*  XTHG holds twelve bytes per record.  They are +0 type, +1
     *  heading, +3 y and +4 x.  The field order is settled by which
     *  reading puts things on real infrastructure (98.5% vs 93.6% across
     *  the shipped cities). */
    uint8_t xthg[R_MAX_THINGS * 12];
    int32_t n_things;

    int32_t misc[1200];
    int32_t view_quarter; /* the quarter turns the view is seen from: 0 but in adapt_view_turned */
    char    name[64];
    char    err[192];
} RCity;

/*  Give a view the layers for a map `n` tiles a side, all zero.  A view
 *  of that size already keeps its layers.  Answers 0, or -1 when the
 *  memory is not there.  A view starts all zero, from calloc. */
int rcity_size(RCity *c, int32_t n);

/*  Make `dst` the same as `src`, layers and all.  Answers 0 or -1. */
int rcity_copy(RCity *dst, const RCity *src);

/*  One number for everything a view holds of its map and its city: a
 *  view whose cells are the same gives the same number. */
uint64_t rcity_hash(const RCity *c);

/*  Make `dst` the city `src` laid down k x k times, side by side: a map
 *  k times as wide.  The things on the map are the first copy's alone.
 *  Answers 0, or -1 when the memory is not there. */
int rcity_tile(RCity *dst, const RCity *src, int32_t k);

/*  Release a view's layers.  The view itself is the caller's. */
void rcity_free(RCity *c);

/*  The view read straight from a city file, through the simulation's own
 *  reader (adapt.c).  Answers 0, or -1 with a reason in `err`.  `c` is
 *  the caller's, all zero or sized before. */
int rcity_load(RCity *c, const char *path);

/*  The XZON corner bit that is nearest the viewer of a view turned
 *  `quarter` quarters.  A multi-tile building is drawn once, at the tile
 *  carrying this bit.  The grid's own is quarter 0. */
uint8_t city_corner_mask(int32_t quarter);

/*  LAYER 0 of the road network (plans/network.md): what a cell's bytes
 *  mean to the networks, as the original reads them.  A cell carries up
 *  to two PARTS, one for each network on it.  The adapter answers it
 *  (adapt.c), because the reading is the simulation's own. */
enum
{
    RNET_POWER = 0,
    RNET_ROAD,
    RNET_RAIL,
    RNET_BAND, /* the highway: a raised way two cells wide */
    RNET_SUBWAY,
    RNET_PIPE,
    RNET_COUNT
};
/*  A network's name, "?" for no network. */
const char *rcity_net_name(int net);

/*  THE EDGES of a cell are bits.  1 is toward col - 1, 2 toward row + 1,
 *  4 toward col + 1 and 8 toward row - 1.  An edge's step across the grid,
 *  the edge behind it, and the next edge of the four in that order. */
static inline int rcity_edge_dc(int e)
{
    return e == 1 ? -1 : e == 4 ? 1
                                : 0;
}
static inline int rcity_edge_dr(int e)
{
    return e == 2 ? 1 : e == 8 ? -1
                               : 0;
}
static inline int rcity_edge_back(int e)
{
    return e == 1 ? 4 : e == 4 ? 1
                    : e == 2   ? 8
                               : 2;
}
static inline int rcity_edge_next(int e)
{
    return e == 1 ? 2 : e == 2 ? 4
                    : e == 4   ? 8
                               : 1;
}

/*  The kinds of part a cell carries. */
enum
{
    RPART_FITTED = 0,  /* a fitter chose it from its neighbors or its slope */
    RPART_CROSSING,    /* two networks across each other on one cell */
    RPART_SLOPE_FOOT,  /* the cell at the foot of a slope piece */
    RPART_MOUTH,       /* a tunnel mouth: `toward` is the edge into the hill */
    RPART_BRIDGE,      /* a bridge of a road, a railway or a power line */
    RPART_RAMP,        /* the one piece that joins a road and a highway */
    RPART_TO_SUBWAY,   /* a rail end over a subway station */
    RPART_STATION,     /* a subway joins it from every side */
    RPART_SLAB,        /* a band on flat ground */
    RPART_INCLINE,     /* a band on a hill slope: `toward` is the high edge */
    RPART_CURVE,       /* a band through a right angle */
    RPART_INTERCHANGE, /* a band unit whose arms are not in its byte */
    RPART_SPAN         /* a band over water */
};

typedef struct
{
    uint8_t net, edges, kind, toward;
} RPart;

/*  The parts on a cell of the surface, from its XBLD and XBIT bytes: how
 *  many, 0 to 2.  For a band's span, `xbit` must be the XBIT of its
 *  unit's cell at (row, col + 1). */
int rcity_parts(uint8_t xbld, uint8_t xbit, RPart out[2]);

/*  The population of a lot's building, as the original's density pass
 *  counts it (A5-0x3982), from its XBLD byte.  The adapter answers it. */
int rcity_lot_population(uint8_t xbld);

#endif /* R_CITY_H */
