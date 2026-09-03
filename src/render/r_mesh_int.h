/*  r_mesh_int.h -- what the mesh's own translation units share.
 *
 *  r_mesh.c grew to 5600 lines and was split along the section banners
 *  it already carried.  Nothing here is public: r_mesh.h is the
 *  interface, this is the seam between the pieces behind it.  A name
 *  appears below only because it is used in more than one of them;
 *  everything else keeps internal linkage where it is defined.
 */
#ifndef R_MESH_INT_H
#define R_MESH_INT_H

#include "r_mesh.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  XTER's low nibble is one of fourteen slope codes, and each code is a
 *  corner mask -- bit 0 NW, 1 SW, 2 SE, 3 NE -- read off the sprites by
 *  tools/terrain_shapes.py into assets/terrain-shapes.json.  A set bit
 *  lifts that corner one level, and a lift is never more than one.
 *
 *  On screen NW is the diamond's top vertex, NE its left, SW its right
 *  and SE its bottom: NW is grid point (col, row), NE is (col + 1, row),
 *  SE is (col + 1, row + 1) and SW is (col, row + 1). */
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
#define MAT_ENG_WALL 1.0f   /* a retaining wall of coursed blocks          */
#define MAT_SEDIMENT 2.0f   /* the map edge's cut, layers of sediment      */
#define MAT_WATER    3.0f   /* the water column in that cut, an aquarium   */
#define MAT_SEABED   4.0f   /* the floor under the water, seen through it  */
#define MAT_EARTH    5.0f   /* a natural bank                              */
#define MAT_SURFACE  6.0f   /* the water's surface; vertical, a cascade    */
#define MAT_ROAD     7.0f   /* a road strip on the surface: col.r across,  */
                            /* -1..1, col.g along, in tiles                 */
#define MAT_PROP      8.0f  /* street furniture: a traffic light's pole     */
#define MAT_LAMP      9.0f  /* its lamp: col.r the junction's phase         */
#define MAT_ZEBRA     10.0f /* a crosswalk across a junction's arm           */
#define MAT_RAIL      11.0f /* a railway: two rails on ties, col.r across    */
#define MAT_WALK      13.0f /* the sidewalk: a road tile paved to its edges  */
#define MAT_SKIRT     12.0f /* a raised road's works: its embankment, and a  */
                            /* viaduct's fascia, parapet and bents; blocks   */
#define MAT_RAIL_X    14.0f /* a rail across a road: the rails alone, flush in the crossing surface */
#define MAT_VEHICLE   15.0f /* a train car or a road car: col.r the paint, col.g the shade  */
#define MAT_XPANEL    16.0f /* a level crossing's surface: rubber panels across both tracks     */
#define MAT_PIER      18.0f /* a viaduct's bent: behind the deck it carries, in */
                            /* front of the ground it stands on                 */
#define MAT_XAPPROACH 17.0f /* the road approaching a crossing: solid lines and the RXR stencil */
                            /* depth, and no part of the surface              */


/*  The tables and the field, defined in r_mesh_tile.c; the state the
 *  segment pipeline carries across its stages, in r_mesh_seg.c. */
extern const uint8_t CODE_MASK[14];
extern const int EDGE_A[4];
extern const int EDGE_B[4];
extern const int NBR_A[4];
extern const int NBR_B[4];
extern const int EDGE_DR[4];
extern const int EDGE_DC[4];
extern const float EDGE_N[4][3];
extern float s_h[GRID * GRID];
extern float s_k[GRID * GRID];
extern float s_b[GRID * GRID];
extern float s_road_class;
extern float s_seg_class;
extern int32_t s_seg_node[2][2];
extern int s_seg_ctrl[2];
extern uint8_t s_junc_ctrl[R_MAP * R_MAP];
extern int s_seg_kind[2];
extern const uint8_t *s_check_xbld;
extern int s_hiway;
extern const float ROAD_MU[4];
extern const float ROAD_MV[4];
extern const float ROAD_DU[4];
extern const float ROAD_DV[4];
extern float s_zcap[GRID * GRID];
extern int s_pass;

#define HIWAY_LIFT 1.0f
#define HIWAY_GIRDER  0.22f
#define HIWAY_PARAPET 0.13f
#define HIWAY_BENT    1.0f
#define HIWAY_CAP     0.10f
#define HIWAY_CAP_D   0.10f
#define HIWAY_COL     0.16f
#define ROAD_W 0.71f    /* the strip: asphalt, a curb and a sidewalk each side; one  \
                         * width in the world everywhere (the user: "max 0.71 of the \
                         * tile size by my calculations") */
#define RAIL_W 0.62f    /* a double track's right-of-way, ballast shoulders in: two tracks 0.38 of a \
                         * tile apart, the spec's ratio of gauge to track spacing, sized against the \
                         * road as the road is sized to the tile (the user: "there needs to be two   \
                         * tracks", "especially since we had to resize the roads")                  */
#define ROAD_GRADE 1.0f /* the profile's steepest rise, levels per tile of road */
enum
{
    L_N = 1,
    L_E = 2,
    L_S = 4,
    L_W = 8
};
#define MAX_PIECES 1024
#define MAX_PTS    512
#define TRAIN_PITCH 0.48f
#define TRAIN_LEN   0.42f
#define TRAIN_WID   0.10f
#define TRAIN_BOGIE 0.14f
#define ARM_TRI(A, B, C, N)                                                                                  \
    do                                                                                                       \
    {                                                                                                        \
        memcpy(t3[0], (A), sizeof t3[0]);                                                                    \
        memcpy(t3[1], (B), sizeof t3[1]);                                                                    \
        memcpy(t3[2], (C), sizeof t3[2]);                                                                    \
        if (put_tri_road_n(m, c, mask_bit, order + 0.05f, (const float (*)[3])t3, (N), col, ref, ref2) != 0) \
            return -1;                                                                                       \
    } while (0)

typedef enum
{
    T_LAND,  /* the field, cut on the sprite's diagonal              */
    T_WATER, /* flat at the table, over a seabed                     */
    T_PAD,   /* a flat pad at pad_level: a building, a flat piece    */
    T_PLANE  /* its own plane: a sloped network piece                */
} Kind;
typedef enum
{
    F_POWER = 0,
    F_ROAD  = 1,
    F_RAIL  = 2
} Family;
typedef struct
{
    float x, y;
} V2;
typedef struct
{
    int   arc; /* 0 a straight from a to b; 1 an arc about c        */
    V2    a, b, c;
    float r, t0, t1; /* the arc's radius and its angles, t0 to t1, signed */
    float len;
} Piece;

/*  The functions that cross a seam. */
int is_water(uint8_t xter);
int32_t slope_code(uint8_t xter);
int32_t corner_gi(int32_t col, int32_t row, int k);
int saddle_lift(const RCity *c, int32_t idx);
Kind tile_top(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float z[4]);
int water_top(const RCity *c, int32_t idx, Kind k);
float tile_order(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit);
void build_field(const RCity *c);
int put_tri_r2(RMesh *m, const float p[3][3], const float *nrm, float order, const float col[3], const float *ref, const float *ref2, int flat);
int put_wall_r2(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3], float r0, float r1, float s0, float s1);
int put_wall_r(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3], float r0, float r1);
int put_wall(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3]);
int cut_ne_sw(int32_t code);
int put_top(RMesh *m, const float p[4][3], int32_t code, float order, const float col[3], int flat);
void tile_colour(const RAtlas *a, const RAtlasLevel *l, int32_t tile, float out[3], const float fallback[3]);
void grid_point(int32_t col, int32_t row, int k, float z, float out[3]);
float road_class(const RCity *c, int32_t col, int32_t row);
int piece_family(uint8_t b, Family *f);
int piece_second(uint8_t b, Family *f);
float width_factor(float dx, float dy, int compensate);
float surface_at_world(const RCity *c, uint8_t mask_bit, float x, float y);
int piece_links(const RAtlasLevel *l, int piece, uint8_t xter);
int tile_links(const RCity *c, const RAtlasLevel *l, int32_t col, int32_t row, Family want);
int link_count(int links);
int eff_links(const RCity *c, const RAtlasLevel *l, int32_t col, int32_t row, Family f);
int put_tri_road_n(RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float *nrm, const float col[3], const float ref[3], const float ref2[3]);
int strip_quad_z(RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float a0[2], const float a1[2], const float b0[2], const float b1[2], float za, float zb, float across0, float across1, float along_a, float along_b, float mat);
int strip_quad(RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float a0[2], const float a1[2], const float b0[2], const float b1[2], float across0, float across1, float along_a, float along_b, float mat);
int strip_fan_z(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float t0, float t1, float r0, float r1, float across_c, float mat, int n, float lift);
int put_box(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float w, float d, float z0, float z1, float mat, float phase);
int put_wire(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x0, float y0, float z0, float x1, float y1, float z1, float sag);
int put_signal(RMesh *m, const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float order, int e, float h);
int put_lamp_face(RMesh *m, float order, float x, float y, float g, float z, float fx, float fy, float sz, float phase, float code);
int put_rail_signal(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy, int absolute, float s_along, int dir);
int put_gate(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy, int along_u);
int put_second_train_sign(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy);
int fillet(const V2 *q, int n, float rmax, Piece *out, int *count);
int loft(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, Family f, const Piece *pc, int np, float total, float zeb0, float zeb1, int pin0, int pin1);
int line_meet(V2 a, V2 da, V2 b, V2 db, V2 *out);
int build_island(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, int32_t col, int32_t row);
int node_kind(const RCity *c, const RAtlasLevel *l, Family f, int32_t col, int32_t row);
int walk_segment(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, int32_t col, int32_t row, int e, uint8_t *visited);
int build_junction(RMesh *m, const RCity *c, uint8_t mask_bit, Family f, int32_t col, int32_t row, int links, float order);
int build_power_tile(RMesh *m, const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, int links, float order, int crossing);
int put_prism_clip(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float dx, float dy, float len, float wid, float zb, float zf, float z0, float z1, float paint);
int road_under_deck(const RCity *c, float x, float y, float px, float py);
int build_highways(RMesh *m, const RCity *c, uint8_t mask_bit, int comp);
void r_mesh_free(RMesh *m);

#endif
