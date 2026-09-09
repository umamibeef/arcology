/*  mesh/internal.h -- what the mesh's own translation units share.
 *
 *  mesh.c grew to 5600 lines and was split along the section banners
 *  it already carried.  Nothing here is public: mesh.h is the
 *  interface, this is the seam between the pieces behind it.  A name
 *  appears below only because it is used in more than one of them;
 *  everything else keeps internal linkage where it is defined.
 */
#ifndef R_MESH_INT_H
#define R_MESH_INT_H

#include "mesh/mesh.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  XTER's low nibble is one of fourteen slope codes, and each code is a
 *  corner mask -- bit 0 NW, 1 SW, 2 SE, 3 NE -- read off the sprites by
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
/*  The neighbour's corners that coincide with ours across each edge. */

/*  World units per altitude level, for the normals only: the projection
 *  draws a level as 0.75 of a tile height whatever this says. */
#define LEVEL_H 0.5f

/*  The alpha channel carries a palette index for the resolve pass's
 *  shadow rule: a dirt-ramp index, so a flying thing darkens the ground
 *  as $19B76 does. */
#define LAND_INDEX (105.0f / 255.0f)

#define GRID (R_MAP + 1)
/* touches water, the ground elsewhere    */

#define MAT_GROUND   0.0f
#define MAT_ENG_WALL 1.0f    /* a retaining wall of coursed blocks          */
#define MAT_SEDIMENT 2.0f    /* the map edge's cut, layers of sediment      */
#define MAT_WATER    3.0f    /* the water column in that cut, an aquarium   */
#define MAT_SEABED   4.0f    /* the floor under the water, seen through it  */
#define MAT_EARTH    5.0f    /* a natural bank                              */
#define MAT_SURFACE  6.0f    /* the water's surface; vertical, a cascade    */
#define MAT_ROAD     7.0f    /* a road strip on the surface: col.r across,  */
                             /* -1..1, col.g along, in tiles                 */
#define MAT_PROP  8.0f       /* street furniture: a traffic light's pole     */
#define MAT_LAMP  9.0f       /* its lamp: col.r the junction's phase         */
#define MAT_ZEBRA 10.0f      /* a crosswalk across a junction's arm           */
#define MAT_RAIL  11.0f      /* a railway: two rails on ties, col.r across    */
#define MAT_WALK  13.0f      /* the sidewalk: a road tile paved to its edges  */
#define MAT_SKIRT 12.0f      /* a raised road's works: its embankment, and a  */
                             /* viaduct's fascia, parapet and bents; blocks   */
#define MAT_RAIL_X     14.0f /* a rail across a road: the rails alone, flush in the crossing surface */
#define MAT_VEHICLE    15.0f /* a train car or a road car: col.r the paint, col.g the shade  */
#define MAT_XPANEL     16.0f /* a level crossing's surface: rubber panels across both tracks     */
#define MAT_ZONE       20.0f /* the map view's zone tint, drawn only looking down */
#define MAT_HILITE     20.5f /* a show-curves ground highlight: col.r the tint, blended, any view */
#define MAT_HIWAY      19.0f
#define MAT_HIWAY_LANE 19.3f /* a ramp lane: drawn as the deck's outer lane, pulled a hair nearer, so it wins over the road it forks from */ /* a freeway deck: two carriageways either side of a barrier */
#define MAT_PIER       18.0f                                                                                                                 /* a viaduct's bent: behind the deck it carries, in */
                                                                                                                                             /* front of the ground it stands on                 */
#define MAT_XAPPROACH 17.0f                                                                                                                  /* the road approaching a crossing: solid lines and the RXR stencil */
                                                                                                                                             /* depth, and no part of the surface              */

/*  The tables and the field, defined in mesh/tile.c. */
extern const uint8_t CODE_MASK[14];
/*  One tile of the ground as the composing script is handed it: what the
 *  map says about it, how high its four corners stand, what lies under
 *  the water on it, and what is over each of its four edges.  The
 *  heightfield, the slope codes and the pads a network cut are the
 *  pipeline's; what is DRAWN over them is arc.rules.tile's. */
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
    float        pad[4];  /* the levelled height at each corner: a pad's own */
    float        land[3]; /* the colour the atlas gives this tile */
    struct
    {
        int   there;    /* a tile over that edge at all */
        int   water;    /* it carries a body of water under its top */
        int   sea;      /* its terrain is water, whatever its top does */
        int   corridor; /* a network's corridor runs over it */
        int   rim;      /* this edge is the map's own */
        int   xbld;
        int   ia, ib;   /* this tile's two corners on that edge */
        float za, zb;   /* the neighbour's ground at those two corners */
        float nx, ny;   /* the edge's outward normal */
    } nbr[4];
} TileFan;

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
int32_t corner_gi(int32_t col, int32_t row, int k);
int     saddle_lift(const RCity *c, int32_t idx);
Kind    tile_top(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float z[4]);
int     water_top(const RCity *c, int32_t idx, Kind k);
float   tile_order(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit);
void    build_field(const RCity *c);
/*  An argument a function will read and never test.  The compiler warns
 *  on a null passed to one, and the analyser stops asking whether the
 *  body should have tested it.  A face's colour is the standing case:
 *  `col` is r, g and the MATERIAL, and the material is what the whole
 *  pipeline reads a face by, so a face without one is not a face. */
#if defined(__GNUC__) || defined(__clang__)
#define R_NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
#else
#define R_NONNULL(...)
#endif

#define R_STR2(x) #x
#define R_STR(x) R_STR2(x)
int     put_tri_r2_at(const char *where, const char *who, RMesh *m, const float p[3][3], const float *nrm, float order, const float col[3], const float *ref, const float *ref2, int flat);
/*  Every land triangle goes through here, and each carries the name of the
 *  function that made it: the macro hands the caller's own __func__ to the
 *  emitter, so no call site says anything.  The names are kept per tile, by
 *  material, and the inspector reads them back (mesh_origins). */
#define put_tri_r2(...) put_tri_r2_at(__FILE__ ":" R_STR(__LINE__) " " , __func__, __VA_ARGS__)
/*  The record of who drew what: the per-tile origins and the component
 *  table.  Written only while a build records (mesh_record), so the
 *  traffic's prisms, drawn through the same emitter every frame, add
 *  nothing to it.  A full build starts it again; an edit's build keeps
 *  the components of the chunks that stand, whose triangles still index
 *  them, and clears the origins of the tiles it builds once the closure
 *  has named them. */
void    mesh_record(int on);
void    mesh_origins_reset(void);
void    mesh_origins_clear_wanted(void);
int     put_wall_r2(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3], float r0, float r1, float s0, float s1);
int     put_wall_r(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3], float r0, float r1);
int     cut_ne_sw(int32_t code);
int     put_top(RMesh *m, const float p[4][3], int32_t code, float order, const float col[3], int flat) R_NONNULL(1, 2, 5);
void    tile_colour(const RAtlas *a, const RAtlasLevel *l, int32_t tile, float out[3], const float fallback[3]);
void    grid_point(int32_t col, int32_t row, int k, float z, float out[3]);
void    mesh_free(RMesh *m);
/*  The incremental rebuild (mesh/incr.c): the partition by chunk after a
 *  full build, and the edit's build that emits into the chunks an edit
 *  touched and splices them into the previous mesh. */
extern int s_incr_on; /* an edit's build is under way */
int        mesh_chunk_of(int32_t col, int32_t row);
int        mesh_want_xy(float x, float y);           /* the emitter: does this triangle's chunk build */
int        mesh_want_tile(int32_t col, int32_t row); /* the terrain loop: does this tile's chunk build */
int        mesh_incr_near(int32_t col, int32_t row); /* did the edit come within a tile of this one (never in a full build) */
int        mesh_partition(RMesh *m);
int        mesh_incr_begin(RMesh *m, const RCity *c, const void *key, size_t keylen); /* 1 an edit, 0 full, -1 nothing changed */
void       mesh_incr_closure(int roads);                                              /* after the grading pass: which chunks */
int        mesh_incr_end(RMesh *m);                                                   /* after the building pass: the splice */
void       mesh_incr_abort(RMesh *m);                                                 /* a failed edit's build: the old mesh stands */
int        mesh_incr_snapshot(RMesh *m, const RCity *c, const void *key, size_t keylen);
#endif
