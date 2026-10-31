/*  mesh/tile.h: one tile of the ground, gathered.  The build composes the ground from what is here.
 *  That is the tiles' fans, the ground drawn tile by tile, the settled
 *  tops and the field (mesh/tile.c, mesh/ground.c, mesh/mesh.c). */
#ifndef ARC_MESH_TILE_H
#define ARC_MESH_TILE_H

#include "mesh/mesh.h"

/*  One tile of the ground, gathered.  What the map says about it, how
 *  high its four corners stand, what lies under the water on it.  What
 *  is over each of its four edges.  mesh_ground_tile draws it, and the
 *  zone tint of the map view reads it. */
typedef struct
{
    RMesh       *m;
    const RCity *c;
    uint8_t      mask_bit;
    int32_t      col, row;
    int32_t      code;
    float        order;
    int          kind, wet, underground;
    int          zone; /* what the map view tints it, 0 for nothing */
    int          xter, xbld;
    float        z[4];
    float        p[4][3], bed[4][3];
    float        pad[4];            /* the leveled height at each corner: a pad's own */
    int          dual;              /* its top is the dual ground */
    float        mid[4][3], ctr[3]; /* the middle of each edge, and of the tile, on its top */
    int          split[4];          /* each edge cut into this many even parts, for a fine tile over it, or 0 */
    int          fine;              /* the parts a side its own top is cut into, or 0 for eight triangles */
    float        land[3];           /* the color the atlas gives this tile */
    struct
    {
        int   there; /* a tile over that edge at all */
        int   water; /* it carries a body of water under its top */
        int   sea;   /* its terrain is water, whatever its top does */
        int   rim;   /* this edge is the map's own */
        int   xbld;
        int   ia, ib; /* this tile's two corners on that edge */
        float za, zb; /* the neighbor's ground at those two corners */
        float zm;     /* and at the middle of the edge */
        int   cliff;  /* a cliff lies between the two tiles */
        float nx, ny; /* the edge's outward normal */
    } nbr[4];
} TileFan;

/*  One tile's ground and one tile's zone, GATHERED for compose.c to
 *  compose.  0 where this build wants no such tile.  Neither draws
 *  anything and neither opens a shape: both are compose.c's. */
void mesh_build_begin(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int32_t dump_r, int32_t dump_c);
int  mesh_ground_fan(int32_t col, int32_t row, TileFan *out);
/*  The same, whether or not the tile lies in the window being built: for
 *  a decision that has to be the same in every window. */
int mesh_ground_fan_any(int32_t col, int32_t row, TileFan *out);
int mesh_tint_fan(int32_t col, int32_t row, TileFan *out);
/*  One tile of the ground, drawn (mesh/ground.c).  Then a point of its
 *  top, on the top or on the seabed, and the height of its pad there.
 *  Points 0 to 3 are its corners, 4 to 7 the middles of its edges and 8
 *  its middle. */
void mesh_ground_tile(const TileFan *t);
/*  Whether a tile's ground can be drawn as a fine grid at all.  It must be
 *  dry, above ground, and open land or a level pad. */
int mesh_ground_dry(const TileFan *t);
/*  A tile's top as an `n` by `n` grid, n even, laid on the same surface as
 *  its eight triangles.  `height` moves each point from that surface: it is
 *  given the point and the surface's height there, and answers the height to
 *  draw.  `fine` says which of its edges and corners move.  Bit (dr + 1) * 3
 *  + (dc + 1) is the edge or the corner in that way. Every tile that shares
 *  one must be told the same. */
void mesh_ground_tile_fine(const TileFan *t, int n, int fine, float (*height)(void *ud, float x, float y, float z), void *ud);
void tile_point(const TileFan *t, int k, int bed, float out[3]);

/*  Each edge's two corners, A to B, and the step to the tile over it. */
extern const int EDGE_A[4];
extern const int EDGE_B[4];
extern const int EDGE_DR[4];
extern const int EDGE_DC[4];

/*  The tiles whose piece is drawn from its own shape, one byte a tile, or
 *  NULL for none.  Such a tile's top is the land's, as if nothing stood on
 *  it.  The map is read when the tops are settled. */
void mesh_tops_shaped(const uint8_t *shaped);
/*  Every tile's top chosen from the places it can come from (TopFrom,
 *  mesh/internal.h). */
void mesh_tops_settle(const RCity *c);
/*  The draw order of a tile. */
float tile_order(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit);
/*  The terrain field, built from the map's heights. */
void build_field(const RCity *c);

#endif /* ARC_MESH_TILE_H */
