/*  mesh/internal.h: what the mesh's own translation units share.
 *
 *  This header is the seam between the pieces behind it.  A name appears below
 *  only because it is used in more than one of them.  Everything else
 *  keeps internal linkage where it is defined. */
#ifndef R_MESH_INT_H
#define R_MESH_INT_H

#include "materials.h" /* GENERATED from mesh/materials.def: the material numbers */
#include "mesh/emit.h"
#include "mesh/ground_bytes.h"
#include "mesh/mesh.h"
#include "mesh/tile.h"

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

#define LEVEL_H MESH_LEVEL_H

#define GRID (r_map + 1)

extern const int   NBR_A[4];
extern const int   NBR_B[4];
extern const float EDGE_N[4][3];
extern float      *s_h;
extern float      *s_k;
extern uint8_t    *s_split; /* a grid point a cliff splits (tile_corners) */
extern float      *s_b;

typedef enum
{
    T_LAND,  /* the field, cut on the sprite's diagonal              */
    T_WATER, /* flat at the table, over a seabed                     */
    T_PAD,   /* a flat pad at pad_level: a building, a flat piece    */
    T_PLANE  /* its own plane: a sloped network piece                */
} Kind;

/*  The functions that cross a seam. */
int32_t corner_gi(int32_t col, int32_t row, int k);
int     saddle_lift(const RCity *c, int32_t idx);
/*  THE FIVE PLACES a tile's top can come from, which mesh_tops_settle
 *  chooses between.  They are the terrain field, and the pad of the
 *  footprint this tile belongs to.  They are also the water's table, the
 *  tile's own slope plane, and its own pad. */
typedef enum
{
    TOP_FIELD = 0,
    TOP_PAD_ANCHOR,
    TOP_WATER,
    TOP_PLANE,
    TOP_PAD
} TopFrom;
/*  And how a tile VOTES for the seabed: as land, or as water whose bed
 *  it is. */
typedef enum
{
    VOTE_LAND  = 0,
    VOTE_WATER = 2
} TopVote;
Kind  tile_top(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float z[4]);
int   tile_cliff(const RCity *c, int32_t a, int32_t b);                            /* a cliff between two tiles side by side */
float tile_lean(const RCity *c, int32_t col, int32_t row, int k);                  /* how far a cliff's lean moved point k */
int   tile_dual(const RCity *c, int32_t col, int32_t row, float *zc, float zm[4]); /* open land: its middle and its edge middles */
int   put_face(RMesh *m, const float p[3][3], const float side[2], float order, const float col[3], const float ref[3]);
int   put_top_dual(RMesh *m, const float p[4][3], const float mid[4][3], const float ctr[3], float order, const float col[3], int flat, const float *lean);
int   water_top(const RCity *c, int32_t idx, Kind k);
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

/*  The same, with how far a cliff's lean moved the ground at each
 *  corner, in levels, for the ground material to read.  NULL is none. */
int put_tri_lean_at(const char *where, const char *who, RMesh *m, const float p[3][3], const float *nrm, float order, const float col[3], const float *ref, const float *ref2, const float *lean, int flat);
#define put_tri_lean(...) put_tri_lean_at(__FILE__ ":" R_STR(__LINE__) " ", __func__, __VA_ARGS__)
int  put_wall_r2(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3], float r0, float r1, float s0, float s1);
int  put_top(RMesh *m, const float p[4][3], int32_t code, float order, const float col[3], int flat) R_NONNULL(1, 2, 5);
void tile_colour(const RAtlas *a, const RAtlasLevel *l, int32_t tile, float out[3], const float fallback[3]);
void grid_point(int32_t col, int32_t row, int k, float z, float out[3]);
void mesh_free(RMesh *m);
/*  The triangles a window's build drops at its border, where no chunk
 *  stands to hold their like (mesh/emit.c).  They are filed under chunk
 *  `ch` of the window that dropped them.  Each is 13 floats: three corners of
 *  four, then its material.  A build begins its own chunks' stores
 *  again, and a whole build begins every one. */
void         mesh_beyond_reset(void);
const float *mesh_beyond(int ch, uint32_t *n);
/*  Box `b`, lowest corner then highest, grown to hold triangle `p`. */
void mesh_box_grow(float b[6], const float p[3][3]);
#endif
