/*  mesh.h: the terrain as geometry, replicating the sprites.  Each land
 *  tile becomes the faces its sprite draws.  The top has its four
 *  corners at ALTM's ground, plus the lift of the slope code's corner
 *  bits.  It is cut along the diagonal the sprite is cut along,
 *  flat-shaded.  A corner's height is shared with the tiles meeting
 *  there, which is what the grid says on 97% of the shipped cities'
 *  corners.  Where two tiles disagree, the mean joins them.  So the dark
 *  wedge of wall the higher tile's sprite shows above a lower neighbor
 *  does not appear.  The result is one connected surface with the
 *  sprites' faces and no walls but the map's edge.  Water tiles are flat
 *  ground at their table.  Their sprites' water pixels cover it, and
 *  with the mesh on the sprites drop their sand rim so the ground runs
 *  up to the water.  The mesh has no state of its own.  It is rebuilt
 *  when the grid changes. */
#ifndef R_MESH_H
#define R_MESH_H

#include <stddef.h>
#include <stdint.h>

#include "atlas/atlas.h"
#include "city.h"
#include "mesh/shape.h"
#include "mesh/vert.h"

/*  The line network the strips were lofted along, kept for the traffic.
 *  It holds every line segment's stations: x, y, the band's height, the
 *  direction and the distance along.  Per segment it holds its class,
 *  and its two node tiles with their kinds.  2 is a junction, 1 an end,
 *  and 0 a map edge or a carrier.  And the lane centers of its class,
 *  tiles from the centerline. */
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
} RNet;

/*  A wayside signal: where it stands, which way its aspect faces, and the
 *  thread segment, distance along it and direction of travel it governs. */
typedef struct
{
    float   x, y, fx, fy, s;
    int32_t seg;
    int     dir, absolute;
} RSignal;

/*  A level meet: its tile and the thread's axis. */
typedef struct
{
    int32_t col, row;
    int     ns; /* the thread runs north-south */
} RLap;

typedef struct
{
    RMeshVert *land;
    uint32_t   n_land, cap_land;
    /*  Which component made each land triangle, one entry a triangle.
     *  It is carried through the chunk sort with the geometry.  The
     *  inspector points at a triangle and asks what drew it. */
    uint32_t  *tri_comp;
    uint32_t   cap_tri_comp;
    uint32_t  *tri_comp_old; /* ... and the last build's, for the chunks an edit leaves standing */
    uint32_t   cap_tri_comp_old, n_tri_comp_old;
    uint32_t   n_terrain; /* the land list's count when the networks begin: the line range is [n_terrain, n_land), drawn once per pass */
    /*  The opaque list bucketed by CHUNK after the build.  A chunk is 32
     *  by 32 tiles, and a map holds 16 of them.  Within a chunk it goes
     *  terrain then network.  So the frame draws only the chunks in view
     *  and the passes only the network ranges of those.  Range 2k is
     *  chunk k's terrain, 2k+1 its networks.  `bucket` is the scratch
     *  the partition swaps with. */
    uint32_t   range_start[2 * MESH_CHUNKS], range_count[2 * MESH_CHUNKS];
    int        ranged;
    uint8_t    chunk_changed[MESH_CHUNKS]; /* which chunks this build changed: every one after a full build, the wanted ones after an edit's: the GPU uploads those */
    RMeshVert *bucket;
    uint32_t   cap_bucket;
    RMeshVert *wbucket; /* the water list's scratch, and its ranges by chunk */
    uint32_t   cap_wbucket;
    uint32_t   wrange_start[MESH_CHUNKS], wrange_count[MESH_CHUNKS];
    /*  The city the last build saw, and the key it was built under
     *  (mesh/incr.c).  The next build diffs against them.  For a small
     *  edit under the same key, rebuilds only the chunks it touched.
     *  The snapshot is one RCity, malloc'd once. */
    void      *snap;
    int        snap_ok;
    uint8_t key[256];
    size_t     key_len;
    RMeshVert *water; /* the water column faces, drawn blended after */
    uint32_t   n_water, cap_water;
    int        to_water;    /* while set, triangles go to the water list */
    float      strip_class; /* the class of the line strip being emitted: its vertices carry it in the normal's fourth component */
    uint32_t   n_walls;     /* retaining walls emitted, for the report */
    RNet   net;         /* the line segments, for the traffic */
    RNet   threadnet;     /* the thread segments, for the trains */
    RLap     *meets;       /* the level meets, for their gates */
    uint32_t   n_laps, cap_laps;
    RSignal  *rsigs; /* the thread signals, for their aspects */
    uint32_t   n_rsigs, cap_rsigs;
} RMesh;

/*  The traffic.
 *
 *      Cars on the network.
 *      Each on one segment at a distance along it.
 *      Traveling forward or back.
 *      In the outer or the inner lane of its class.
 *      Holding at a red signal and behind the car ahead.
 *      Meet the junction box to an onward arm.
 *      Turning back at a dead end.
 *
 *  Built into `scratch` each frame. */
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
    /*  The arm it will take at the junction ahead, once the rule that
     *  chooses has been asked: the segment, the way along it.  Whether
     *  there is an answer waiting at all. */
    int      turn_seg, turn_dir, turn_have;
} RCar;

/*  A point of a train's path.
 *
 *      Where the engine was.
 *      How far along its run.
 *      On which segment.
 *      So the cars trail it by arc length. */
typedef struct
{
    float   x, y, z, hx, hy, d, s;
    int32_t seg;
    int     dir;
} RTrailPt;

/*  A train.  It is the engine on a thread segment, with its cars behind
 *  it along the engine's own path.  There is one train per run of
 *  consecutive records on adjacent tiles in the save. */
typedef struct
{
    int32_t   seg;
    float     s, speed, d; /* d: the distance run */
    int       dir, n_cars;
    /*  The arm it was given at the junction it has just reached. */
    int       turn_seg, turn_dir, turn_have;
    float     paint[32];
    RTrailPt *trail;
    uint32_t  trail_n, trail_cap, trail_head; /* a ring.  Head is the newest */
    uint32_t  rng;
} RTrain;

typedef struct
{
    RCar    *cars;
    uint32_t n, cap;
    RTrain  *trains;
    uint32_t n_trains;
    float   *gate; /* per meet, the arm's angle in degrees, 0 down to 88 up */
    int32_t *xseg; /* per meet, the line segment through it, -1 if none  */
    float   *xs;   /* and the distance along that segment of its center     */
    RMesh    scratch;
} RTraffic;

void traffic_digest(const RTraffic *t, const RMesh *m);
int  traffic_init(RTraffic *t, const RMesh *m, const RCity *c);
void traffic_free(RTraffic *t);
/*  THE MOVING WORLD'S OWN DOOR: entered whenever the world advances or
 *  is drawn.  The script runs the beats the clock owes and lays the
 *  geometry of what moves. */
int  traffic_moving(RTraffic *t, const RMesh *m, const RCity *c, float dt, float time, int draw);
int  traffic_build(RTraffic *t, const RMesh *m, const RCity *c);

/*  Build the land list for `c`.  Returns 0, or -1 when out of memory. */
/*  `underground` builds the underground view's ground instead.
 *
 *      Every tile at the ground field.
 *      The seabed under water.
 *      No water.
 *      No walls.
 *      No plinths.
 *      For the white ground with the hairline grid. */
/*  `rotated` builds for the free rotation.  Every water surface becomes
 *  a mesh face, and the cut runs on all four edges, because any side may
 *  face the camera. */
/*  `lines` adds the line strips on the surface.
 *
 *      One constant-width band per line tile along the piece's connections.
 *      Bent on a quarter circle at a corner.
 *      In place of the line sprites. */
int  mesh_build(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines);
void mesh_free(RMesh *m);
/*  The incremental rebuild's own scratch, kept between builds: given
 *  back when the program is done with meshes altogether. */
void mesh_incr_free(void);
/*  The emitter's own scratch: the triangle index by tile and the
 *  coplanar check's hashes, kept between builds. */
void mesh_emit_free(void);
void net_table_free(void); /* net/table.c: the segment table's sample arenas, once, at exit */

/*  What the mesh made of a tile, for the query tool.  It gives its kind,
 *  the corners of the face it draws, and the shared terrain around it.
 *  Valid after a build, which fills the fields it reads. */
int mesh_query(const RCity *c, int32_t col, int32_t row, char *buf, size_t n);
/*  Who drew the mesh on a tile.  It gives the name of each function that
 *  emitted a land triangle there.  It also gives the material it drew
 *  in, and how many triangles it made, one to a line.  The inspector
 *  shows it.  Returns how many entries the tile has, -1 if the tile is
 *  off the map. */
int mesh_origins(int32_t col, int32_t row, char *out, size_t n);
/*  The topmost thing a tile carries at a world point: who drew it, in
 *  what material, how many triangles, and the box it occupies. */
int mesh_origin_pick(int32_t col, int32_t row, float wx, float wy, const char **who, const char **where, float *mat, uint32_t *tris, float box[6]);
/*  Do two triangles belong to one thing?  A shape's own triangles and
 *  every descendant's do (mesh/shape.h), which is what an outline is
 *  taken over. */
int mesh_comp_edges_key(uint32_t a, uint32_t b);

/*  Pointing at the mesh.
 *
 *      The land triangles whose middle lies in a tile.  One triangle's
 *      corners and its component.  The outline of the thing a component
 *      belongs to.
 *
 *  The whole of its generating call's output when it has one, the
 *  component alone otherwise.  As pairs of world points, SIX floats to a
 *  segment, in no order round it.  They are thinned to the points that
 *  keep each run within a fiftieth of a tile of itself.  The outline
 *  carries its own height, so a slab's draws on the slab and not on the
 *  ground under it. */
int  mesh_tris_at(const RMesh *m, int32_t col, int32_t row, uint32_t *out, int max);
void mesh_tri_get(const RMesh *m, uint32_t t, float p[3][3], uint32_t *comp);
int  mesh_comp_edges(const RMesh *m, uint32_t comp, float *segs, int max_seg);
/*  The heights of the four corners of the face the mesh draws for a
 *  tile, NW, NE, SE, SW, for the query highlight.  With `underground`
 *  the ground the underground view draws there, the seabed under water. */
int mesh_tile_corners(const RCity *c, int32_t col, int32_t row, int underground, float z[4]);

/*  The watertight check: every edge of every triangle must belong to at
 *  least one other triangle, so the surface has no crack.  Returns the
 *  number of free edges found (0 when the mesh is closed), -1 when out
 *  of memory.  With `verbose` it prints the first forty and a summary by
 *  material.  Meant for a build with `rotated` set, which cuts all four
 *  map edges.  The base of the cut is the floor and counts as closed. */
int mesh_check(const RMesh *m, int verbose);
/*  Every triangle of every chunk range keys to its chunk and the ranges
 *  tile the lists (mesh/incr.c): what a spliced build must keep true. */
int mesh_ranges_check(const RMesh *m, int verbose);
/*  The clipping check: samples every line and margin face and counts the
 *  samples a terrain top face rises above.  With `verbose` lists the
 *  tiles, the deepest first.  0 when no line is cut by the ground. */
int mesh_check_clip(const RMesh *m, int verbose);
/*  Two faces of DIFFERENT shapes covering the same ground at the same
 *  height.  Nothing separates such a pair but the painter's slot each
 *  carries, and where the slots agree too, nothing at all does.  Returns
 *  the count of LINE WORKS BURIED UNDER A MARGIN, which a build must
 *  never have.  The rest is reported. */
int mesh_check_overlap(const RMesh *m, int verbose);
/*  Two faces of different shapes that pass THROUGH one another: geometry
 *  no painter's order can separate.  Answers how many pairs. */
int mesh_check_collide(const RMesh *m, int verbose);
/*  Every surface over one point of the map, highest first: its height
 *  there, its painter's slot, its material and the shape it belongs to.
 *  What to reach for when a surface is hidden or two of them flicker. */
void mesh_probe(const RMesh *m, float px, float py);
/*  The mesh the last build wrote into, or NULL before the first one: what
 *  a query with no mesh of its own asks (src/script). */
const RMesh *mesh_built(void);
/*  A junction's outline must be a simple ring: no vertex where it doubles
 *  back, no two edges meet.  Nonzero when the last build laid one
 *  that is not (net/node.c). */
int junction_outline_faults(void);
/*  A meet must reach a margin at both of its ends.  Nonzero when
 *  the last build marked one that does not (walk/walkway.c). */
int walk_net_faults(void);

/*  The line geometry's live knobs, in tiles.  They are the way widths,
 *  and the tightest and widest curve each family may be drawn with.  The
 *  straight run reserved at a node, and how far inside its corridor the
 *  band is held.  It also gives how far out a junction may cut its arms
 *  back.  Nine floats in the order the tuning window shows them.
 *  Writing them and rebuilding the mesh is all that is needed to see the
 *  change. */
float *mesh_tune(void);

/*  The street-furniture and margin passes (furniture.c, margin.c): each
 *  switched as one, from the View menu or --no-furniture / --no-margins.
 *  A change rebuilds the mesh. */
void furniture_enable(int on);
void margin_enable(int on); /* the margin pass */
int  furniture_on(void);      /* the furniture pass's state, to know when a toggle needs a rebuild */

#endif /* R_MESH_H */
