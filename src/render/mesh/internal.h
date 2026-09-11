/*  mesh/internal.h: what the mesh's own translation units share.
 *
 *  This header is the seam between the pieces behind it.  A name appears below
 *  only because it is used in more than one of them.  Everything else
 *  keeps internal linkage where it is defined. */
#ifndef R_MESH_INT_H
#define R_MESH_INT_H

#include "materials.h" /* GENERATED from mesh/materials.def: the material numbers */
#include "mesh/mesh.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  XTER's low nibble is one of fourteen slope codes, and each code is a
 *  corner mask, bit 0 NW, 1 SW, 2 SE, 3 NE, read off the sprites by
 *  tools/terrain_shapes.py into assets/terrain-shapes.json.  A set bit
 *  lifts that corner one level, and a lift is never more than one.  On
 *  screen NW is the diamond's top vertex, NE its left, SW its right and SE
 *  its bottom: NW is grid point (col, row), NE is (col + 1, row), SE is
 *  (col + 1, row + 1) and SW is (col, row + 1). */
enum
{
    NW = 0,
    SW = 1,
    SE = 2,
    NE = 3
};

/*  The four edges, and the corners that bound each, A to B. */
enum
{
    E_N = 0,
    E_E = 1,
    E_S = 2,
    E_W = 3
};
/*  The neighbor's corners that coincide with ours across each edge. */

/*  World units per altitude level, for the normals only: the projection
 *  draws a level as 0.75 of a tile height whatever this says. */
#define LEVEL_H 0.5f

/*  The alpha channel carries a palette index for the resolve pass's
 *  shadow rule.  A dirt-spur index, so a flying thing darkens the ground
 *  as $19B76 does. */
#define LAND_INDEX (105.0f / 255.0f)

#define GRID (R_MAP + 1)
/* touches water, the ground elsewhere    */

                             /* -1..1, col.g along, in tiles                 */
                             /* viaduct's fascia, parapet and bents.  Blocks   */
                                                                                                                                             /* front of the ground it stands on                 */
                                                                                                                                             /* depth, and no part of the surface              */

/*  The tables and the field, defined in mesh/tile.c. */
/*  One tile of the ground as the composing script is handed it.  What
 *  the map says about it, how high its four corners stand, what lies
 *  under the water on it.  What is over each of its four edges.  The
 *  heightfield, the slope codes and the pads a network cut are the
 *  pipeline's.  What is DRAWN over them is arc.rules.tile's. */
typedef struct
{
    RMesh       *m;
    const RCity *c;
    uint8_t      mask_bit;
    int32_t      col, row;
    int32_t      code;
    float        order;
    int          kind, wet, underground;
    int          corridor; /* a network's corridor runs over this tile */
    int          zone;     /* what the map view tints it, 0 for nothing */
    int          xter, xbld;
    float        z[4];
    float        p[4][3], bed[4][3];
    float        pad[4];  /* the leveled height at each corner: a pad's own */
    float        land[3]; /* the color the atlas gives this tile */
    struct
    {
        int   there;    /* a tile over that edge at all */
        int   water;    /* it carries a body of water under its top */
        int   sea;      /* its terrain is water, whatever its top does */
        int   corridor; /* a network's corridor runs over it */
        int   rim;      /* this edge is the map's own */
        int   xbld;
        int   ia, ib;   /* this tile's two corners on that edge */
        float za, zb;   /* the neighbor's ground at those two corners */
        float nx, ny;   /* the edge's outward normal */
    } nbr[4];
} TileFan;

/*  THE WHOLE BUILD, as the script that drives it sees one.  The order
 *  the world is composed in is the script's.  Which tiles, in what
 *  order, and which of the network passes run at all.  And these are the
 *  primitives it composes from.  There is no C loop behind it. */
typedef struct
{
    RMesh             *m;
    const RCity       *c;
    const RAtlas      *a;
    const RAtlasLevel *l;
    uint8_t            mask_bit;
    int                pass;                    /* 1 the grading pass, 2 the building pass, 0 neither */
    int                lines, underground;
    int                rotated;
    int                rc;      /* what a primitive that failed left behind */
    ShapeId            shape;   /* the one the script has open, closed when it opens the next */
} WorldFan;
/*  One tile's ground and one tile's zone, GATHERED for the script that
 *  composes them.  0 where this build wants no such tile.  Neither draws
 *  anything and neither opens a shape: both are the script's. */
int mesh_ground_fan(int32_t col, int32_t row, TileFan *out);
int mesh_tint_fan(int32_t col, int32_t row, TileFan *out);
int mesh_world_nets(WorldFan *w, int what);     /* 0 the lane model, 1 the networks, 2 the bands */

extern const int     EDGE_A[4];
extern const int     EDGE_B[4];
extern const int     NBR_A[4];
extern const int     NBR_B[4];
extern const int     EDGE_DR[4];
extern const int     EDGE_DC[4];
extern const float   EDGE_N[4][3];
extern float         s_h[GRID * GRID];
extern float         s_k[GRID * GRID];
extern float         s_b[GRID * GRID];
extern int           s_pass;
/*  One pass of a build composed: the body build.c runs twice. */
int mesh_build_pass(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines);

typedef enum
{
    T_LAND,  /* the field, cut on the sprite's diagonal              */
    T_WATER, /* flat at the table, over a seabed                     */
    T_PAD,   /* a flat pad at pad_level: a building, a flat piece    */
    T_PLANE  /* its own plane: a sloped network piece                */
} Kind;

/*  The functions that cross a seam. */
int     is_water(uint8_t xter);
int32_t slope_code(uint8_t xter);
int     structure_tint(uint8_t xbld); /* the map view's tint for a placed structure, or 0 for the zone under it (mesh/tile.c) */
int     building_tile(uint8_t xbld);  /* a building proper: a footprint with an anchor (mesh/tile.c) */
int     elevated_tile(uint8_t xbld);  /* a raised piece, ordered by the neighbor that owns the span */
int32_t corner_gi(int32_t col, int32_t row, int k);
int     saddle_lift(const RCity *c, int32_t idx);
/*  THE SIX PLACES a tile's top can come from, which arc.rules.terrain
 *  chooses between.  They are the terrain field, and the pad of the
 *  footprint this tile belongs to.  They are also the water's table, the
 *  shelf a corridor graded for itself, the tile's own slope plane, and
 *  its own pad. */
typedef enum
{
    TOP_FIELD = 0,
    TOP_PAD_ANCHOR,
    TOP_WATER,
    TOP_SHELF,
    TOP_PLANE,
    TOP_PAD
} TopFrom;
/*  And how a tile VOTES for the four corners it shares with its
 *  neighbors: with its own plane, with its base alone, or as water. */
typedef enum
{
    VOTE_LAND = 0,
    VOTE_BASE,
    VOTE_WATER
} TopVote;
void    mesh_tops_reset(void);
void    mesh_top_is(int32_t at, int top, int32_t anchor, int32_t order, int vote, int rest);
int     mesh_top_graded(int32_t at);
Kind    tile_top(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float z[4]);
int     water_top(const RCity *c, int32_t idx, Kind k);
float   tile_order(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit);
void    build_field(const RCity *c);
void    mesh_field(void); /* the field, and the plan dump over it: the DRIVE says when */
/*  An argument a function will read and never test.  The compiler warns
 *  on a null passed to one, and the analyser stops asking whether the
 *  body should have tested it.  A face's color is the standing case.
 *  `col` is r, g and the MATERIAL.  The material is what the whole
 *  pipeline reads a face by.  A face without one is not a face. */
#if defined(__GNUC__) || defined(__clang__)
#define R_NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
#else
#define R_NONNULL(...)
#endif

#define R_STR2(x) #x
#define R_STR(x) R_STR2(x)
int     put_tri_r2_at(const char *where, const char *who, RMesh *m, const float p[3][3], const float *nrm, float order, const float col[3], const float *ref, const float *ref2, int flat);
/*  Every land triangle goes through here.  Each carries the name of the
 *  function that made it.  The macro hands the caller's own __func__ to
 *  the emitter, so no call site says anything.  The names are kept per
 *  tile, by material, and the inspector reads them back (mesh_origins). */
#define put_tri_r2(...) put_tri_r2_at(__FILE__ ":" R_STR(__LINE__) " " , __func__, __VA_ARGS__)
/*  The record of who drew what: the per-tile origins and the component
 *  table.  Written only while a build records (mesh_record), so the
 *  traffic's prisms, drawn through the same emitter every frame, add
 *  nothing to it.  A full build starts it again.  An edit's build keeps
 *  the components of the chunks that stand, whose triangles still index
 *  them.  Clears the origins of the tiles it builds once the closure has
 *  named them. */
void    mesh_record(int on);
int     put_wall_r2(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3], float r0, float r1, float s0, float s1);
int     put_wall_r(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3], float r0, float r1);
int     cut_ne_sw(int32_t code);
int     put_top(RMesh *m, const float p[4][3], int32_t code, float order, const float col[3], int flat) R_NONNULL(1, 2, 5);
void    tile_colour(const RAtlas *a, const RAtlasLevel *l, int32_t tile, float out[3], const float fallback[3]);
void    grid_point(int32_t col, int32_t row, int k, float z, float out[3]);
void    mesh_free(RMesh *m);
/*  The incremental rebuild (mesh/incr.c).  It holds the partition by
 *  chunk after a full build.  It also holds the edit's build that emits
 *  into the chunks an edit touched and meets them into the previous
 *  mesh. */
extern int s_incr_on; /* an edit's build is under way */
void    mesh_origins_reset(void);
void    mesh_origins_clear_wanted(void);
int        mesh_chunk_of(int32_t col, int32_t row);
int        mesh_want_xy(float x, float y);           /* the emitter: does this triangle's chunk build */
#endif
