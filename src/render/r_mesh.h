/*  r_mesh.h -- the terrain as geometry, replicating the sprites.
 *
 *  Each land tile becomes the faces its sprite draws: the top with the
 *  four corners at ALTM's ground plus the lift of the slope code's corner
 *  bits, cut along the diagonal the sprite is cut along, flat-shaded.
 *  A corner's height is shared with the tiles meeting there, which is
 *  what the grid says on 97% of the shipped cities' corners; where two
 *  tiles disagree, the mean joins them, so the dark wedge of wall the
 *  higher tile's sprite shows above a lower neighbour does not appear.
 *  The result is one connected surface with the sprites' faces and no
 *  walls but the map's edge.  Water tiles are flat ground at their table;
 *  their sprites' water pixels cover it, and with the mesh on the sprites
 *  drop their sand rim so the ground runs up to the water.
 *
 *  The mesh has no state of its own; it is rebuilt when the grid changes.
 */
#ifndef R_MESH_H
#define R_MESH_H

#include <stddef.h>
#include <stdint.h>

#include "r_atlas.h"
#include "r_city.h"
#include "r_gpu.h"

/*  The road network the strips were lofted along, kept for the traffic:
 *  every road segment's stations, x, y, the band's height, the direction
 *  and the distance along, and per segment its class, its two node tiles
 *  and their kinds (2 a junction, 1 an end, 0 a map edge or a carrier),
 *  and the lane centres of its class, tiles from the centreline. */
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
    RMeshVert *water; /* the water column faces, drawn blended after */
    uint32_t   n_water, cap_water;
    int        to_water; /* while set, triangles go to the water list */
    uint32_t   n_walls;  /* retaining walls emitted, for the report */
    RRoadNet   net;      /* the road segments, for the traffic */
    RRoadNet   railnet;  /* the rail segments, for the trains */
    RXing     *xings;    /* the level crossings, for their gates */
    uint32_t   n_xings, cap_xings;
    RRailSig  *rsigs; /* the rail signals, for their aspects */
    uint32_t   n_rsigs, cap_rsigs;
} RMesh;

/*  The traffic (the user: "cars are completely wrong and unmoving...
 *  and need to respect the lanes"): cars on the network, each on one
 *  segment at a distance along it, travelling forward or back, in the
 *  outer or the inner lane of its class, holding at a red signal and
 *  behind the car ahead, crossing the junction box to an onward arm,
 *  turning back at a dead end.  Built into `scratch` each frame. */
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

int  r_traffic_init(RTraffic *t, const RMesh *m, const RCity *c);
void r_traffic_free(RTraffic *t);
void r_traffic_step(RTraffic *t, const RMesh *m, float dt, float time);
int  r_traffic_build(RTraffic *t, const RMesh *m, const RCity *c);

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
int  r_mesh_build(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int roads);
void r_mesh_free(RMesh *m);

/*  What the mesh made of a tile, for the query tool: its kind, the corners
 *  of the face it draws and the shared terrain around it.  Valid after a
 *  build, which fills the fields it reads. */
int r_mesh_query(const RCity *c, int32_t col, int32_t row, char *buf, size_t n);
/*  The heights of the four corners of the face the mesh draws for a tile,
 *  NW, NE, SE, SW, for the query highlight; with `underground` the
 *  ground the underground view draws there, the seabed under water. */
int r_mesh_tile_corners(const RCity *c, int32_t col, int32_t row, int underground, float z[4]);

/*  The watertight check: every edge of every triangle must belong to at
 *  least one other triangle, so the surface has no crack.  Returns the
 *  number of free edges found (0 when the mesh is closed), -1 when out
 *  of memory; with `verbose` it prints the first forty and a summary by
 *  material.  Meant for a build with `rotated` set, which cuts all four
 *  map edges; the base of the cut is the floor and counts as closed. */
int r_mesh_check(const RMesh *m, int verbose);
/*  The clipping check: samples every road and sidewalk face and counts
 *  the samples a terrain top face rises above; with `verbose` lists the
 *  tiles, the deepest first.  0 when no road is cut by the ground. */
int r_mesh_check_roads(const RMesh *m, int verbose);

#endif /* R_MESH_H */
