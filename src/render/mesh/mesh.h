/*  mesh.h -- the terrain as geometry, replicating the sprites.  Each land
 *  tile becomes the faces its sprite draws: the top with the four corners
 *  at ALTM's ground plus the lift of the slope code's corner bits, cut
 *  along the diagonal the sprite is cut along, flat-shaded.  A corner's
 *  height is shared with the tiles meeting there, which is what the grid
 *  says on 97% of the shipped cities' corners; where two tiles disagree,
 *  the mean joins them, so the dark wedge of wall the higher tile's sprite
 *  shows above a lower neighbour does not appear.  The result is one
 *  connected surface with the sprites' faces and no walls but the map's
 *  edge.  Water tiles are flat ground at their table; their sprites' water
 *  pixels cover it, and with the mesh on the sprites drop their sand rim so
 *  the ground runs up to the water.  The mesh has no state of its own; it
 *  is rebuilt when the grid changes. */
#ifndef R_MESH_H
#define R_MESH_H

#include <stddef.h>
#include <stdint.h>

#include "atlas/atlas.h"
#include "city.h"
#include "mesh/shape.h"
#include "mesh/vert.h"

/*  The road network the strips were lofted along, kept for the traffic:
 *  every road segment's stations, x, y, the band's height, the direction
 *  and the distance along, and per segment its class, its two node tiles
 *  and their kinds (2 a junction, 1 an end, 0 a map edge or a carrier),
 *  and the lane centres of its class, tiles from the centreline. */
#define MESH_CHUNK  32                                            /* tiles on a chunk's side   */
#define MESH_CHUNKS ((R_MAP / MESH_CHUNK) * (R_MAP / MESH_CHUNK)) /* chunks on a map: 16       */

typedef struct
{
    float x, y, z, dx, dy, s;
} RNetPt;

typedef struct
{
    uint32_t first, count;
    float    total;
    int      cls;
    int32_t  node[2][2]; /* column, row of the start and end node tiles */
    int      kind[2];
    int      ctrl[2]; /* the control of the arm at each end: 0 none, 1 stop, 2 signal */
    float    lane_out, lane_in;
} RNetSeg;

typedef struct
{
    RNetPt  *pts;
    uint32_t n_pts, cap_pts;
    RNetSeg *segs;
    uint32_t n_segs, cap_segs;
} RRoadNet;

/*  A wayside signal: where it stands, which way its aspect faces, and the
 *  rail segment, distance along it and direction of travel it governs. */
typedef struct
{
    float   x, y, fx, fy, s;
    int32_t seg;
    int     dir, absolute;
} RRailSig;

/*  A level crossing: its tile and the rail's axis. */
typedef struct
{
    int32_t col, row;
    int     ns; /* the rail runs north-south */
} RXing;

typedef struct
{
    RMeshVert *land;
    uint32_t   n_land, cap_land;
    /*  Which component made each land triangle, one entry a triangle,
     *  carried through the chunk sort with the geometry: the inspector
     *  points at a triangle and asks what drew it. */
    uint32_t  *tri_comp;
    uint32_t   cap_tri_comp;
    uint32_t  *tri_comp_old; /* ... and the last build's, for the chunks an edit leaves standing */
    uint32_t   cap_tri_comp_old, n_tri_comp_old;
    uint32_t   n_terrain; /* the land list's count when the networks begin: the road range is [n_terrain, n_land), drawn once per pass */
    /*  The opaque list bucketed by CHUNK after the build -- 32 by 32 tiles,
     *  16 of them on a map -- and within a chunk by terrain then network,
     *  so the frame draws only the chunks in view and the passes only the
     *  network ranges of those.  Range 2k is chunk k's terrain, 2k+1 its
     *  networks; `bucket` is the scratch the partition swaps with. */
    uint32_t   range_start[2 * MESH_CHUNKS], range_count[2 * MESH_CHUNKS];
    int        ranged;
    uint8_t    chunk_changed[MESH_CHUNKS]; /* which chunks this build changed: every one after a full build, the wanted ones after an edit's -- the GPU uploads those */
    RMeshVert *bucket;
    uint32_t   cap_bucket;
    RMeshVert *wbucket; /* the water list's scratch, and its ranges by chunk */
    uint32_t   cap_wbucket;
    uint32_t   wrange_start[MESH_CHUNKS], wrange_count[MESH_CHUNKS];
    /*  The city the last build saw and the key it was built under
     *  (mesh/incr.c): the next build diffs against them and, for a
     *  small edit under the same key, rebuilds only the chunks it
     *  touched.  The snapshot is one RCity, malloc'd once. */
    void      *snap;
    int        snap_ok;
    uint8_t key[256];
    size_t     key_len;
    RMeshVert *water; /* the water column faces, drawn blended after */
    uint32_t   n_water, cap_water;
    int        to_water;    /* while set, triangles go to the water list */
    float      strip_class; /* the class of the road strip being emitted: its vertices carry it in the normal's fourth component */
    uint32_t   n_walls;     /* retaining walls emitted, for the report */
    RRoadNet   net;         /* the road segments, for the traffic */
    RRoadNet   railnet;     /* the rail segments, for the trains */
    RXing     *xings;       /* the level crossings, for their gates */
    uint32_t   n_xings, cap_xings;
    RRailSig  *rsigs; /* the rail signals, for their aspects */
    uint32_t   n_rsigs, cap_rsigs;
} RMesh;

/*  The traffic: cars on the network, each on one segment at a distance
 *  along it, travelling forward or back, in the outer or the inner lane of
 *  its class, holding at a red signal and behind the car ahead, crossing
 *  the junction box to an onward arm, turning back at a dead end.  Built
 *  into `scratch` each frame. */
typedef struct
{
    int32_t  seg;
    float    s;
    int      dir, lane;
    float    speed, paint;
    int      in_box, next_seg, next_dir;
    float    bx0, by0, bz0, bx1, by1, bz1, bt, blen;
    float    bcx, bcy; /* the corner the arms' lines meet at: a turn's path bends through it */
    int      bcurve;
    float    hx, hy; /* the heading last drawn */
    float    hold;   /* seconds stood at a stop sign */
    uint32_t rng;
} RCar;

/*  A point of a train's path: where the engine was, how far along its
 *  run, and on which segment, so the cars trail it by arc length. */
typedef struct
{
    float   x, y, z, hx, hy, d, s;
    int32_t seg;
    int     dir;
} RTrailPt;

/*  A train: the engine on a rail segment and its cars behind it along
 *  the engine's own path, one train per run of consecutive records on
 *  adjacent tiles in the save. */
typedef struct
{
    int32_t   seg;
    float     s, speed, d; /* d: the distance run */
    int       dir, n_cars;
    float     paint[32];
    RTrailPt *trail;
    uint32_t  trail_n, trail_cap, trail_head; /* a ring; head is the newest */
    uint32_t  rng;
} RTrain;

typedef struct
{
    RCar    *cars;
    uint32_t n, cap;
    RTrain  *trains;
    uint32_t n_trains;
    float   *gate; /* per crossing, the arm's angle in degrees, 0 down to 88 up */
    int32_t *xseg; /* per crossing, the road segment through it, -1 if none  */
    float   *xs;   /* and the distance along that segment of its centre     */
    RMesh    scratch;
} RTraffic;

int  traffic_init(RTraffic *t, const RMesh *m, const RCity *c);
void traffic_free(RTraffic *t);
void traffic_step(RTraffic *t, const RMesh *m, float dt, float time);
int  traffic_build(RTraffic *t, const RMesh *m, const RCity *c);

/*  Build the land list for `c`.  Returns 0, or -1 when out of memory. */
/*  `underground` builds the underground view's ground instead: every
 *  tile at the ground field, the seabed under water, no water, no walls,
 *  no plinths, for the white ground with the hairline grid. */
/*  `rotated` builds for the free rotation: every water surface as a
 *  mesh face and the cut on all four edges, since any side may face the
 *  camera. */
/*  `roads` adds the road strips on the surface: one constant-width band
 *  per road tile along the piece's connections, bent on a quarter circle
 *  at a corner, in place of the road sprites. */
int  mesh_build(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int roads);
void mesh_free(RMesh *m);
/*  The incremental rebuild's own scratch, kept between builds: given
 *  back when the program is done with meshes altogether. */
void mesh_incr_free(void);
/*  The emitter's own scratch: the triangle index by tile and the
 *  coplanar check's hashes, kept between builds. */
void mesh_emit_free(void);
void net_table_free(void); /* net/table.c: the segment table's sample arenas, once, at exit */

/*  What the mesh made of a tile, for the query tool: its kind, the corners
 *  of the face it draws and the shared terrain around it.  Valid after a
 *  build, which fills the fields it reads. */
int mesh_query(const RCity *c, int32_t col, int32_t row, char *buf, size_t n);
/*  Who drew the mesh on a tile: the name of each function that emitted a
 *  land triangle there, with the material it drew in and how many triangles
 *  it made, one to a line.  The inspector shows it.  Returns how many
 *  entries the tile has, -1 if the tile is off the map. */
int mesh_origins(int32_t col, int32_t row, char *out, size_t n);
/*  The topmost thing a tile carries at a world point: who drew it, in
 *  what material, how many triangles, and the box it occupies. */
int mesh_origin_pick(int32_t col, int32_t row, float wx, float wy, const char **who, const char **where, float *mat, uint32_t *tris, float box[6]);
/*  Do two triangles belong to one thing?  A shape's own triangles and
 *  every descendant's do (mesh/shape.h), which is what an outline is
 *  taken over. */
int mesh_comp_edges_key(uint32_t a, uint32_t b);

/*  Pointing at the mesh: the land triangles whose middle lies in a tile,
 *  one triangle's corners and its component, and the outline of the thing
 *  a component belongs to -- the whole of its generating call's output
 *  when it has one, the component alone otherwise -- as pairs of world
 *  points, SIX floats to a segment, in no order round it, thinned to the
 *  points that keep each run within a fiftieth of a tile of itself.  The
 *  outline carries its own height, so a deck's draws on the deck and not
 *  on the ground under it. */
int  mesh_tris_at(const RMesh *m, int32_t col, int32_t row, uint32_t *out, int max);
void mesh_tri_get(const RMesh *m, uint32_t t, float p[3][3], uint32_t *comp);
int  mesh_comp_edges(const RMesh *m, uint32_t comp, float *segs, int max_seg);
/*  The heights of the four corners of the face the mesh draws for a tile,
 *  NW, NE, SE, SW, for the query highlight; with `underground` the
 *  ground the underground view draws there, the seabed under water. */
int mesh_tile_corners(const RCity *c, int32_t col, int32_t row, int underground, float z[4]);

/*  The watertight check: every edge of every triangle must belong to at
 *  least one other triangle, so the surface has no crack.  Returns the
 *  number of free edges found (0 when the mesh is closed), -1 when out
 *  of memory; with `verbose` it prints the first forty and a summary by
 *  material.  Meant for a build with `rotated` set, which cuts all four
 *  map edges; the base of the cut is the floor and counts as closed. */
int mesh_check(const RMesh *m, int verbose);
/*  Every triangle of every chunk range keys to its chunk and the ranges
 *  tile the lists (mesh/incr.c): what a spliced build must keep true. */
int mesh_ranges_check(const RMesh *m, int verbose);
/*  The clipping check: samples every road and sidewalk face and counts
 *  the samples a terrain top face rises above; with `verbose` lists the
 *  tiles, the deepest first.  0 when no road is cut by the ground. */
int mesh_check_roads(const RMesh *m, int verbose);
/*  Two faces of DIFFERENT shapes covering the same ground at the same
 *  height.  Nothing separates such a pair but the painter's slot each
 *  carries, and where the slots agree too, nothing at all does.  Returns
 *  the count of ROAD WORKS BURIED UNDER A FOOTWAY, which a build must
 *  never have; the rest is reported. */
int mesh_check_overlap(const RMesh *m, int verbose);
/*  Every surface over one point of the map, highest first: its height
 *  there, its painter's slot, its material and the shape it belongs to.
 *  What to reach for when a surface is hidden or two of them flicker. */
void mesh_probe(const RMesh *m, float px, float py);
/*  The mesh the last build wrote into, or NULL before the first one: what
 *  a query with no mesh of its own asks (src/script). */
const RMesh *mesh_built(void);
/*  A junction's outline must be a simple ring: no vertex where it doubles
 *  back, no two edges crossing.  Nonzero when the last build laid one
 *  that is not (net/junction.c). */
int junction_outline_faults(void);
/*  A crossing must reach a pavement at both of its ends.  Nonzero when
 *  the last build marked one that does not (walk/walkway.c). */
int walk_net_faults(void);

/*  The road geometry's live knobs, in tiles: the carriageway widths, the
 *  tightest and widest curve each family may be drawn with, the straight
 *  run reserved at a node, how far inside its corridor the band is held,
 *  and how far out a junction may cut its arms back.  Nine floats in the
 *  order the tuning window shows them.  Writing them and rebuilding the
 *  mesh is all that is needed to see the change. */
float *mesh_tune(void);

/*  The road-marking and street-furniture passes (marking.c, furniture.c):
 *  each switched as one, from the View menu or --no-markings /
 *  --no-furniture; a change rebuilds the mesh. */
void marking_enable(int on);
void furniture_enable(int on);
void sidewalk_enable(int on); /* the sidewalk pass */
int  furniture_on(void);      /* the furniture pass's state, to know when a toggle needs a rebuild */

#endif /* R_MESH_H */
