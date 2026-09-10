/*  pipeline.h: what the renderer's three halves say to each other.
 *
 *  This header is no one directory's own.  It declares about five
 *  hundred things.  Two in three of them are defined in mesh/: the fit,
 *  the loft, the lane model, the node outline, the families' geometry.
 *  A fifth is defined in net/, and the rest in walk/.  The file sits
 *  above all three because all three share it.  A reader who looks for
 *  the lane router must not have to find it under the network's own
 *  internals.
 *
 *  Each directory holds one kind of thing.
 *
 *      mesh/   the shape primitives
 *      net/    the stores, and the plumbing that turns a Lua
 *              declaration into a family
 *      walk/   a segment's stages
 *
 *  A declaration here belongs to whichever of the three defines it.
 *  This file is where they meet until each one is filed with its own.
 *
 *  THE STAGES, in the order they run.
 *
 *      walk/walk.c    the pass over the map, and the walk from node to
 *                     node.  It fits a segment, measures its arms and
 *                     its laps, trims it, overlays it and caps it.  It
 *                     also sets the context every later stage reads.
 *      net/table.c    the segment table.  The grading pass fills it and
 *                     the building pass replays it.  It holds the
 *                     loft's station cache.
 *      mesh/fit.c     the path fit.  A segment's cells become pieces:
 *                     runs, arcs and biarcs, inside the corridor the
 *                     family allows.
 *      net/shelf.c    the corridor field.  It holds what a strip asks
 *                     of the ground under it, and the graded surface
 *                     every later stage reads.
 *      mesh/loft.c    the loft.  It sweeps a cross-section along the
 *                     pieces and answers one strip.
 *      net/node.c     a node's outline and its arms' trims, and the box
 *                     the family draws on it.
 *
 *  THE ORDER a build runs in.
 *
 *      1.  The corridor a segment occupies, and the gate on each shared
 *          edge.
 *      2.  The path through them.
 *      3.  The nodes.  Each takes its shape from the arms that reach
 *          it, and hands each arm back where to start.
 *      4.  The loft.  The cross-section runs along the pieces, and the
 *          profile eases between node altitudes.
 *      5.  The records the traffic and the passes read, and the
 *          corridor's own shelf.
 *
 *  The families that answer the stages are declared in Lua.  net/box.c,
 *  net/meet.c and net/band.c hold what those declarations lend them.
 *  The piece tables (walk/piece.c) say what a cell carries.  The shape
 *  primitives (mesh/shapes.c) draw over the mesh's triangle emitter.
 *  The passes over the finished network are declared here too: the
 *  lanes, the margins, the markings and the furniture. */
#ifndef R_NET_INT_H
#define R_NET_INT_H

#include "mesh/internal.h"
#include "script.h"
/* ---- the types, knobs and state the pipeline shares --------------------- */

/*  The context walk_segment sets for the segment it is drawing, read by
 *  the loft, the junction box and the passes (net/walk.c owns them). */
extern uint8_t        s_junc_ctrl[R_MAP * R_MAP]; /* net/node.c: per junction tile, two bits per arm: 0 none, 1 stop, 2 signal */
extern const uint8_t *s_check_xbld;               /* piece.c: the last built city's XBLD, for the piece scan */
/* the line art's anchors per edge, read by the piece tables and the meets */
extern const float SIDE_MU[4];
extern const float SIDE_MV[4];
extern const float SIDE_DU[4];
extern const float SIDE_DV[4];
/*  The corridor field (net/grade.c): per grid corner, what the strips
 *  passing over it ask of the ground. */
extern float   s_zcap[GRID * GRID];
extern uint8_t s_corr[GRID * GRID];
extern float   s_zdist[GRID * GRID];
/*  The lowest line that passes over each corridor corner.  The shelf is
 *  shaped by the nearest one and smoothed across the field.  This is the
 *  ceiling that smoothing may not break: ground that rises above the
 *  band it carries is ground the line is buried in. */
extern float s_zlow[GRID * GRID];
/*  A corridor tile's OWN four corner heights, flat across the band and
 *  graded along it.  The corridor is not bound by the height field's
 *  rule that neighboring tiles share a corner.  Two families that run
 *  side by side at different heights are two shelves with a wall between
 *  them.  They are not one warped quad. 1e9 where a tile has none. */
extern float s_tilez[R_MAP * R_MAP * 4];
void         s_tile_reset(int32_t i);
void         shelf_node(int32_t col, int32_t row); /* a tile where edges meet: they share one level there (grade.c) */
void         shelf_steps(int *n, float *worst); /* corridor tiles disagreeing at a shared corner (grade.c) */

/*  The structure's proportions, measured off the original's own
 *  rendering of Four Cities' viaduct rather than the specification.  The
 *  art draws a slim ribbon on thin columns.  Its edge is a dark shadow a
 *  quarter of a level deep.  No parapet stands over the way, and a
 *  column about a meter across carries it every two tiles.  Built to the
 *  specification's concrete sections it read as a viaduct of walls. */
#define BAND_LIFT      1.0f
#define BAND_UNDERSIDE 0 /* the slab's soffit, fascias and end walls: off until they are a pass of their own */
/*  How far inside the slab's edge the fit samples the corridor, in
 *  tiles.  A line samples a hair inside (0.002).  A slab two tiles wide
 *  has its edges on tile boundaries.  An S of two R3 arcs can overhang
 *  the building beside its exit by 0.04 of a tile at the very end.  That
 *  is the parapet's width, not a lane. */
/*  Stations a loft may hold.  8190 is one whole ring band's worth.  A
 *  ring that runs out of stations on its last stretch leaves its spurs
 *  with no slab beside them.  The count moves with the slab's width
 *  through the meets sampled at the tile edges. */
#define LOFT_MAX_ST      32768
#define SPUR_HW          (0.15f * s_tune.band_w) /* a spur lane: the slab's outer lane, 0.70 to 1.0 across, on its own */
#define BAND_LANE_IN    0.70f  /* the outer lane's inner edge, across the band */
#define BAND_LANE_TAPER 4      /* tiles: one gore, then the descent */
float ease_smooth(float f); /* a smoothstep over 0..1, level at both ends */
#define BAND_GIRDER  0.11f     /* the slab's edge: 0.9 m of slab and girder */
#define BAND_PARAPET 0.045f    /* a barrier, not a wall: 0.35 m over the slab */
#define BAND_BENT    1.0f
#define BAND_CAP     0.05f
#define BAND_CAP_D   0.07f
#define BAND_COL     0.09f
/*  The knobs the look is tuned with, live.  They were constants.  The
 *  judgment they encode is aesthetic and belongs to the person looking
 *  at the city, not to a number I picked.  The macros still stand so
 *  every use site reads the current value, and the UI's tuning window
 *  writes them and rebuilds the mesh. */
typedef struct
{
    float line_w;    /* the way, across, in tiles                 */
    float thread_w;    /* a double thread's right of way                     */
    float line_rmin; /* the tightest curve each may be drawn with         */
    float thread_rmin;
    float line_rmax; /* and the widest sweep to look for                  */
    float thread_rmax;
    float approach;    /* straight run reserved at every node               */
    float margin;      /* how far inside its corridor the band is held      */
    float trim_cap;    /* how far out a junction may cut its arms back      */
    float show_curves; /* draw the fitted centerline over the world       */
    /*  The band's, live too.  The slab's half width across, in tiles,
     *  and what sits on it follows.  The radii its corners may take.
     *  How far an on-spur reaches for the slab.  How many straight cells
     *  may lie between two curve blocks for them to be one staircase.
     *  And the share of an edge a corner may take for its fillet.  The
     *  half rule holds it at 0.5.  It is the same for every family,
     *  because the fit is one. */
    float band_w;
    float band_rmin, band_rmax;
    float band_reach;
    float band_stair;
    float corner_share;
    float spur_merge; /* how far along the line an on-spur's join may slide from the point it aimed at, tiles */
    float band_grade; /* the slab's steepest rise or fall, levels per tile */
    float band_stiff; /* the slab's stiffness: the window, tiles, over which it holds level across dips and rounds its crests */
} RTune;
extern RTune s_tune;

#define LINE_W    (s_tune.line_w)
#define THREAD_W    (s_tune.thread_w)
#define LINE_RMIN (s_tune.line_rmin)
#define THREAD_RMIN (s_tune.thread_rmin)

#define LINE_GRADE 1.0f /* the profile's steepest rise, levels per tile of line */
/*  The tightest curve each family may be drawn with, in tiles of radius.
 *  It is a constraint and not a preference, and it outranks the
 *  corridor.  A one family may be laid round a tighter corner than
 *  another, and one cannot, whatever room the tiles leave.  Where the
 *  two disagree the curve wins and the band may overhang a neighboring
 *  tile, which is what a real alignment does.  A staircase of thread
 *  tiles is therefore ONE straight diagonal and not a weave. */

enum
{
    L_N = 1,
    L_E = 2,
    L_S = 4,
    L_W = 8
};
#define MAX_PIECES  1200 /* a joint at every gate: two pieces a point */
#define MAX_PTS     512
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
        if (put_tri_line_n(m, c, mask_bit, order + 0.05f, (const float (*)[3])t3, (N), col, ref, ref2) != 0) \
            return -1;                                                                                       \
    } while (0)
/*  A tile family is a SLOT, nothing more.  Which name sits at which slot
 *  is the script's.  Net/family.c reads the declaration's `tiles` name
 *  against the list the scripts themselves settled.  And nothing here
 *  knows what any of them draws.  The pipeline reaches for one only
 *  through the pointers the declarations filled in: net_line,
 *  net_thread, net_band, net_power. */
typedef int Family;
#define NET_TILE_FAMS 3
typedef struct
{
    float x, y;
} V2;
typedef struct
{
    int   arc; /* 0 a straight from a to b.  1 an arc about c        */
    V2    a, b, c;
    float r, t0, t1; /* the arc's radius and its angles, t0 to t1, signed */
    float len;
} Piece;

/*  A spur (spec 7.3, the lane drop), as the scan before the band walks
 *  lists it.  It holds the spur tile, and the point on the slab's
 *  centerline beside it.  It also holds the lane's direction of travel
 *  (the viewer's right hand) and the side the spur lies on is read off
 *  the stations.  `off` says the taper runs upstream, which is the lane
 *  coming down to the line.  `len` says how many tiles of it there are,
 *  counted through curve blocks.  The loft narrows the slab by arc
 *  length from that point, records its stations, and the spur is built
 *  from them afterwards. */
typedef struct
{
    int32_t rc, rr; /* the spur tile */
    V2      c0;     /* on the slab's centerline beside it */
    V2      along;  /* the lane's direction of travel */
    V2      toward; /* from the spur tile toward the slab */
    int     off;    /* the taper runs upstream: the lane comes down to the line */
    int     len;    /* the taper's tiles, 0 for none */
    int     opp;    /* the line lies opposite the slab: the strip crosses the spur tile */
    int  arm;  /* how it meets the lap beside it.  0 is not through the box, and 1 an arm every outermost lane may use.  2 is one only the lane opposite may */
} HwSpur;
#define MAX_SPURS 2048
extern HwSpur s_hw_spurs[MAX_SPURS];
extern int    s_hw_nspurs;
/*  A slab's stations, every band's, recorded by the loft for the spurs. */
typedef struct
{
    V2    pos, dir;
    float s, z;
    int   band;
} HwSt;
#define HW_MAX_ST 120000
extern HwSt s_hw_st[HW_MAX_ST];
extern int  s_hw_nst, s_hw_band;

/*  Vector helpers small enough to share: both the line algorithm and the
 *  band path want them.  A copy in each is a copy to get wrong. */
static inline float v2len(V2 v)
{
    return sqrtf(v.x * v.x + v.y * v.y);
}

static inline float v2cross(V2 a, V2 b)
{
    return a.x * b.y - a.y * b.x;
}
int   put_wall(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3]);
float line_class(const RCity *c, int32_t col, int32_t row);
int   piece_family(uint8_t b, Family *f);
/*  What line a building byte carries, as arc.rules.line_tiles reads the
 *  city: on the tile itself, or under a slab standing on it. */
/*  A carrier: a tile a line runs on into rather than stopping at, as
 *  arc.rules.carrier_tiles names it. */
int net_carrier(uint8_t b);
int net_line_lapped(uint8_t b);
int net_thread_lap(uint8_t b);  /* a lap the second family is part of, on either axis (walk/piece.c) */
int net_line_on(uint8_t b);
int net_line_near(uint8_t b);
/*  The band tiles, as arc.rules.band_tiles names them. */
int net_band_spur(uint8_t b);
/*  The slab's reading of the map (net/band.c): which cells make a band,
 *  which are free air, and how far beside a band the corridor reaches. */
int                  band_slab(uint8_t b, int *east_west);
int                  band_is_incline(uint8_t b);
void                 band_free_air(const RCity *c, const RAtlasLevel *l);
const uint8_t       *band_corridor(const RCity *c, const int32_t *own, int n_own, int spurs);
void                 band_spur_lost_is(int32_t col, int32_t row, int why);

int net_band_curve(uint8_t b);
int net_band_interchange(uint8_t b);
int   piece_second(uint8_t b, Family *f);
float width_factor(float dx, float dy, int compensate);
float surface_at_world(const RCity *c, uint8_t mask_bit, float x, float y);
float surface_at_tile(const RCity *c, uint8_t mask_bit, int32_t col, int32_t row, float x, float y); /* that tile's top, clamped to it */
int   tile_top_planar(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit);                   /* its two triangles are coplanar */
/*  The artwork the piece tables are read from: always the atlas's finest
 *  level, so a piece's links do not change with the zoom being drawn. */
void  net_piece_art(const RAtlas *a);
int   piece_links(const RAtlasLevel *l, int piece, uint8_t xter);
int   tile_links(const RCity *c, const RAtlasLevel *l, int32_t col, int32_t row, Family want);
int   link_count(int links);
int   eff_links(const RCity *c, const RAtlasLevel *l, int32_t col, int32_t row, Family f);
/*  The line and thread emitters take, invisibly, the name of the
 *  function that called them: the inspector reports who drew each
 *  material on a tile.  The innermost emitter's own name says nothing
 *  useful. */
int   put_tri_draped_n_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float *nrm, const float col[3], const float ref[3], const float ref2[3]);
#define put_tri_line_n(...) put_tri_draped_n_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
int   put_tri_ground_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float *nrm, const float col[3], const float ref[3], const float ref2[3]); /* ... laid on the drawn surface */
#define put_tri_ground(...) put_tri_ground_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
int   strip_quad_z_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float a0[2], const float a1[2], const float b0[2], const float b1[2], float za, float zb, float across0, float across1, float along_a, float along_b, float mat);
#define strip_quad_z(...) strip_quad_z_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
int   strip_quad_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float a0[2], const float a1[2], const float b0[2], const float b1[2], float across0, float across1, float along_a, float along_b, float mat);
#define strip_quad(...) strip_quad_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
int   strip_fan_z(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float t0, float t1, float r0, float r1, float across_c, float mat, int n, float lift);
/*  A tile's outline on the ground, for the curve overlay.  It is four
 *  thin bars in a vehicle paint.  The tile's own ground and sprites stay
 *  visible inside them. */
int tile_highlight(RMesh *m, const RCity *c, uint8_t mask_bit, int32_t col, int32_t row, float paint);
int put_box(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float w, float d, float z0, float z1, float mat, float phase);
int put_cyl(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float r, float z0, float z1, float mat);
int curves_hidden(float mat); /* show curves: the network materials it hides */
int put_wire(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x0, float y0, float z0, float x1, float y1, float z1, float sag);
int put_wire_paint(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x0, float y0, float z0, float x1, float y1, float z1, float sag, float paint);
int put_bar(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x0, float y0, float z0, float x1, float y1, float z1, float fx, float fy, float w, float d, float mat, float code, float phase);
int put_lamp_face(RMesh *m, float order, float x, float y, float g, float z, float fx, float fy, float sz, float phase, float code, int uv);
/*  A LEVEL MEET, in three: the ask gathers it and opens its shape, the
 *  script measures it.  The draw lays the panel and the approaches.
 *  Every measurement follows from the angle the line and the line cross
 *  at.  This is why the script sits between the two. */
int  build_lap(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int32_t col, int32_t row, int second);
void net_lap_ask(int32_t *col, int32_t *row, float *sine, float *line, float *thread);
void net_lap_frame(const ScriptLap *fr);
/*  ---- the network, as the script discovers it
 *  --------------------------
 *
 *  A segment is a run of CELLS: from one node out along an edge, tile by
 *  tile, to the next node.  Which cells those are is the script's
 *  (scripts/compose/network.lua): the pipeline offers the links and the
 *  node kinds and takes the runs back.  Everything after this reads the
 *  cells and never the map: the points, the corridor and the fit are all
 *  derived from the run.
 *
 *  `stop` says why the run ended.  The four answers are not the same
 *  shape.  A node's cell carries a point and the far end's kind.  The
 *  map's edge carries a point half a tile past the last cell.  A run the
 *  guards cut short leaves a cell marked and unkept.  A loop's last cell
 *  is the run's first. */
typedef enum
{
    NET_STOP_NODE = 0, /* the next node: the last cell is it */
    NET_STOP_EDGE,     /* the map's edge, leaving the last cell by `exit` */
    NET_STOP_CUT,      /* the run outgrew the point or step guard */
    NET_STOP_STUCK,    /* the next cell does not return the link */
    NET_STOP_LOOP      /* back to the cell it started from */
} NetStop;
typedef struct
{
    int32_t cell[MAX_PTS]; /* the cells kept, in order, as row * R_MAP + col */
    int     n;
    NetStop stop;
    int     exit; /* the edge the last cell was left by, where the run left it */
} NetRun;

/*  A BAND BAND's run, which is the same question asked of a different
 *  network.  Each entry is a cell of the band, with the axis it lies on.
 *  Or it is a curve BLOCK the band turns through, whose center is its
 *  own corner.  The spine's points, the tiles the band owns and which of
 *  them carry a spur all follow from this.  Nothing else decides which
 *  cells a band is made of. */
typedef struct
{
    int32_t cell[MAX_PTS]; /* row * R_MAP + col */
    uint8_t block[MAX_PTS];
    uint8_t ew[MAX_PTS]; /* a cell's axis.  A block lies on neither */
    int     n;
} HwRun;
/*  The band's bands, as the script discovered them (net/network.c),
 *  and the five planes it reads them off (net/band.c). */
int  net_hw_replaying(void); /* 1 where this build finds its bands rather than replaying them */
void net_hw_disc_reset(void);
int  net_hw_disc_add(const HwRun *r, int32_t col, int32_t row, int ew, int sign);
int  net_hw_disc_get(int i, const HwRun **r, int32_t *col, int32_t *row, int *ew, int *sign);
/*  What the script hands its bands back through: it reads the cells off
 *  the map itself. */
typedef struct
{
    int full;
} HwDiscFan;

/*  The store the script hands its network to, and what everything
 *  downstream reads it back through (net/network.c).  One ORDERED list a
 *  family, of the runs it found and the lone pieces it found no run for.
 *  `net_disc_planes` is the offering the script reads it all off. */
typedef enum
{
    NET_DISC_RUN = 0, /* a segment: a run of cells */
    NET_DISC_LONE,    /* a piece with no links at all: always its own band */
    NET_DISC_EDGE,    /* a piece whose every link leaves the map: a band where no run covered it */
    NET_DISC_JUNCTION /* a cell where three or more ways meet */
} NetDiscKind;
void    net_disc_reset(void);
int     net_disc_run_add(int fk, const int32_t *cells, int n, int stop, int exit);
int     net_disc_island_add(int fk, int32_t cell, int edge);
int     net_disc_junction_add(int fk, int32_t cell);
int     net_disc_junctions(int fk);
int32_t net_disc_junction(int fk, int k);
int     net_disc_count(int fk);
int     net_disc_kind(int fk, int i);
int32_t net_disc_cell(int fk, int i);
int     net_disc_run_get(int fk, int i, NetRun *out);
void    net_disc_planes(const RCity *c, const RAtlasLevel *l, Family f, uint8_t *links, uint8_t *art);
/*  What counts as a NODE says where a segment ends.  The script works it
 *  out off the two planes and hands the whole plane back.  Node_kind
 *  reads that and nothing else. */
void    net_disc_nodes_set(Family f, const uint8_t *plane);
/*  The edge a run leaves its first cell by, which with that cell names
 *  the segment for the rest of the build.  A run of one cell left the map
 *  or was cut, and leaves by the edge it was heading for. */
int     net_run_edge(const NetRun *r);
/*  One family's map, GATHERED for the script that discovers its network,
 *  and the list it hands back. */
typedef struct
{
    int         fk;
    Family      f;
    const char *family;
    uint8_t     links[R_MAP * R_MAP];
    uint8_t     art[R_MAP * R_MAP];
    int         full; /* the store would take no more: reported, never quietly dropped */
} NetDiscFan;

/*  The path in stages (net/fit.c): the corridor's gates, the taut
 *  string through them, and the radius each corner may sweep. */
void net_serve_tiles(const int32_t *tcol, const int32_t *trow, int nt); /* a drawn segment's tiles (walk.c) */
int  net_tile_served(int32_t i);                                        /* for the check: a tile with a line, wherever it runs */
float arm_cut(Family f, int col, int row, int e); /* how far from its mouth an arm's strip is cut, as the segment is cut (lane.c) */
int  path_fit_begin(const RCity *c, const int32_t *tcol, const int32_t *trow, int nt, float hw, V2 start, V2 goal, float rmax, float rmin, float gro, float reserve, int32_t ex0, int32_t ex1, int free_reach, V2 *out, float *rad, float *tlim, int cap);
int  path_fit_end(void);
/*  The same with a tangent budget per vertex, which is what the tangent
 *  fit hands over.  Fillet_r is this with half of each edge. */
/*  One boundary of the tangent fit, as the scripts read it.  It says
 *  whether the two lines meet, and whether either was let leave its own
 *  cells.  It also says how far ahead the far line's next meet and its
 *  own end lie. */
typedef struct
{
    int   has_after, met, free, cross, free_join;
    float ahead, reach;
} PathPair;

/*  The path a script settled itself, in place of the stages: the points,
 *  the radius each corner may sweep and the tangent each may spend.
 *  Answers how many were taken, or 0 outside a fit. */
int   path_answer(const V2 *q, const float *rad, const float *tlim, int n);
/*  And the corridor it is offered: the cells, the two ends and the
 *  band's half width, for a script that sweeps its own line through. */
int   path_corridor(const int32_t **tcol, const int32_t **trow, int *nt, V2 *start, V2 *goal, float *hw);
void *path_handle(void);
int  path_lined(void);
int  path_pairs(void);
int  path_pair(int k, PathPair *out);
void path_after_is(int meet);
const char *path_try(const char *how, void **obj);
int  path_held(void);
int  path_finish(void);
/*  The cut the lane router still makes for itself.  Every other path is
 *  cut through the queue below. */
/*  THE CUT QUEUE (mesh/fit.c).  Cutting a path into pieces is
 *  arc.rules.pieces's and only the drive may ask for it.  So a pass that
 *  needs one queues the chain and reads the pieces back after the drive
 *  has been round. */
void net_cut_reset(void);
int  net_cut_add(const V2 *q, int n, const float *rad, const float *tlim);
int  net_cuts(void);
int  net_cut_full(void);
void *net_cut_at(int i);
void net_cut_done(int i);
int  net_cut_pieces(int i, Piece *out, int cap, int *count);
void tlim_half(const V2 *q, int n, float *tlim);
/*  The same fit on a chain of points with a corridor of the caller's
 *  choosing, for the band walk.  And its runs, for the caller's dump. */
void path_fit_probes(void);
/*  The fit's own arithmetic, which the composition asks for by name.  It
 *  holds the corridor sweep at a corner, and a corner's demand for
 *  tangent.  It also holds what an end may spare, and the tally a
 *  finished corner falls in. */
float path_fit_demand(V2 a, V2 b, V2 c);
float path_fit_need(V2 a, V2 b, V2 c, float rmin);
void  path_fit_count(const char *what);
/*  The run cut's primitives, as the composing script asks for them.
 *  They are the step comparison, the candidate a span would be and
 *  whether it stands, and the winner of a prefix.  The runs named on the
 *  way back. */
typedef struct RunFan   RunFan;
typedef struct ChainFan ChainFan;
typedef struct JoinFan   JoinFan;
typedef struct BridgeFan BridgeFan;
typedef struct StepFan   StepFan;
typedef struct SweepFan  SweepFan;
typedef struct PieceFan  PieceFan;
typedef struct StairFan  StairFan;
typedef struct ProfFan   ProfFan;
typedef struct SlideFan  SlideFan;
typedef struct DropFan   DropFan;
typedef struct GroundFan GroundFan;
typedef struct OrientFan OrientFan;
typedef struct ShelfFan  ShelfFan;
typedef struct XLaneFan  XLaneFan;
RunFan   *path_runs(void);
ChainFan *path_chain(void);
int   path_run_perp(const RunFan *x, int a, int b);
int   path_run_spread(const RunFan *x, int i, int j, float *lo, float *hi);
void  path_run_chord(RunFan *x, int i, int j, float off);
void  path_run_note(const RunFan *x, int i, int j, const char *why);
void  path_run_slope(RunFan *x, int i, int len, int period, int major, int minor);
int   path_run_try(RunFan *x, int i, int j, int kind);
void  path_run_keep(RunFan *x, int j);
void  path_run_emit(RunFan *x, int i, int j, int kind);
void  path_run_order(RunFan *x);
/*  The chain the runs make: an end pulled onto a run's own angle,
 *  whether an end already lies on that line, and the lines appended. */
void  path_chain_aim(ChainFan *c, int which);
int   path_chain_on_line(const ChainFan *c, int which);
void  path_chain_end(ChainFan *c, int which);
void  path_chain_run(ChainFan *c, int i);
/*  The arc at a meet, as the composing script decides it.
 *
 *      Whether a leg's extension to the meet holds.
 *      Whether the gap it spans stays covered.
 *      What radius the corridor allows there.
 *      Whether the straights that reach the arc hold.
 *      The vertex placed. */
int   path_join_holds(const JoinFan *j, int leg);
int   path_join_covers(const JoinFan *j);
SweepFan *path_join_arc(const JoinFan *j, float tl);
int   path_join_legs(const JoinFan *j, float r);
void  path_join_place(JoinFan *j);
/*  The biarc between two parallel lines: one placing of the S, the
 *  placing kept as the best so far, and the two vertices it leaves. */
int   path_bridge_solve(BridgeFan *b, float a, float c, int f, int shift);
void  path_bridge_refuse(const BridgeFan *b, int out, float room, float len);
float path_bridge_holds(BridgeFan *b);
void  path_bridge_result(const BridgeFan *b, float r);
void  path_bridge_note(const BridgeFan *b, float ba, float bc);
void  path_bridge_keep(BridgeFan *b);
void  path_bridge_place(BridgeFan *b);
/*  The join walked point by point.  It says whether an end lies in line
 *  with the gap beside it, and whether the gap is a sideways step.  It
 *  also gives the diagonal that step can be drawn as, and the points
 *  placed. */
int   path_step_inline(const StepFan *w, int side);
int   path_step_jog(const StepFan *w);
int   path_step_diagonal(StepFan *w, float keep, float cap);
void  path_step_end(StepFan *w, int side);
void  path_step_point(StepFan *w, int t);
/*  The corridor sweep at one corner: whether an arc of this radius holds
 *  there and leaves nothing bare, and the radius settled on. */
SweepFan *path_sweep_ask(const void *mark, V2 a, V2 b, V2 c, float tlim, float rmax, float rmin, float hw);
float     path_sweep_take(int *tight);
int   path_sweep_holds(SweepFan *s, float r);
void  path_sweep_answer(SweepFan *s, float r, int tight);
/*  A fitted path cut into pieces: one corner read, a corner left as a
 *  corner, a fillet swept into one.  The run out to the far end. */
int   path_piece_corner(PieceFan *p, int i);
void  path_piece_straight(PieceFan *p, int i);
void  path_piece_arc(PieceFan *p, int i, float r);
void  path_piece_tail(PieceFan *p);
/*  A band band's chain of fit points: which of its cells turn, which
 *  are pinned by a spur, and the points the chain is given. */
int   band_stair_turn(const StairFan *s, int i);
int   band_stair_pinned(const StairFan *s, int i);
void  band_stair_point(StairFan *s, int i);
void  band_stair_centre(StairFan *s, int i, int j);
/*  A band strip's elevation, station by station: what the script
 *  reads and writes, and the easing curve a lane drop follows. */
void  band_prof_at(const ProfFan *p, int i, float *s_at, float *z, float *ground);
void  band_prof_set(ProfFan *p, int i, float z);
/*  Sliding a spur's join along the slab and along the line.  It routes
 *  one placing, says whether it leaves by the spur tile's line edge, and
 *  keeps the placing. */
/*  One placing of a spur's join, as far as the CHAIN it is cut from,
 *  and the placing built from the pieces the script cut. */
const char *band_slide_chain(SlideFan *s, float u, float at, V2 *q, float *rad, float *tlim, int *n);
float       band_slide_routed(SlideFan *s, const Piece *pc, int np);
int         band_slide_exits(SlideFan *s);
void        band_slide_keep(SlideFan *s, float taper);
void        band_slide_note(const SlideFan *s, int tried, int off, int unroutable, int missed);
/*  The lane a spur drops from a slab: the stations and the spurs, and
 *  the width each station is left with. */
int   band_drop_station(const DropFan *d, int i, float *at, V2 *pos, V2 *dir);
int   band_drop_spur(const DropFan *d, int r, V2 *c0, V2 *tile, V2 *along, int *len, int *off);
void  band_drop_clear(DropFan *d);
void  band_drop_width(DropFan *d, int i, int side, float w);
void  band_drop_gore(DropFan *d, int i, int side);
/*  A strip's elevation over the ground: its stations, the altitude an
 *  end or a level meet pins it to, and the height each is given. */
int   loft_ground_at(const GroundFan *g, int i, float *at, float *z);
int   loft_ground_node(const GroundFan *g, int which, float *z);
int   loft_ground_lap(const GroundFan *g, int i, float *z);
void  loft_ground_set(GroundFan *g, int i, float z);
/*  How an on-spur reads the four sides around it: what each neighbor is,
 *  and the sides it settles on. */
int   band_orient_links(const OrientFan *o, int32_t col, int32_t row);
void  band_orient_answer(OrientFan *o, int kind, int dside, int rside, int eside, int off, int lines);
void  band_orient_spur(OrientFan *o, const OrientFan *r);
/*  Reconciling the shelf: the copies of one corner, the copies round a
 *  node's tile, and the level they are all given. */
int   shelf_ask(ShelfFan *s); /* the corridor corners a shelf rule reconciles (grade.c) */
int   shelf_copies(const ShelfFan *s, int gx, int gy, int *owner, float *dist, float *z, int max);
void  shelf_set(ShelfFan *s, int gx, int gy, int owner, float z);
int   shelf_node_at(const ShelfFan *s, int i, int32_t *col, int32_t *row);
int   shelf_node_heights(const ShelfFan *s, int32_t col, int32_t row, float *z, int max);
void  shelf_node_set(ShelfFan *s, int32_t col, int32_t row, float z);
/*  A lane's open end carried on into the facing lane across a meet.  The
 *  ends that could take one, how a candidate lies, and the join. */
int   xlane_end(const XLaneFan *x, int i);
int   xlane_measure(const XLaneFan *x, int la, int lb, float *off, float *dot, float *ahead, float *aside, float *dist);
void  xlane_merge(XLaneFan *x, int la, int lb);
int   xlane_link(XLaneFan *x, int la, int lb);
void fit_tally_get(int bucket, void *dst, size_t cap); /* the fit's tallies, around a fit that may be discarded */
void fit_tally_set(int bucket, const void *src, size_t cap);
int  path_fit_points_begin(const uint8_t *mark, const uint8_t *own, const V2 *pts, int n, float hw, V2 start, V2 goal, float rmax, float rmin, float gro, int32_t ex0, int32_t ex1, int free_lines, V2 *out, float *rad, float *tlim, int cap);
int  path_fit_points_end(void);
void path_fit_prims(void);
void fit_tally_into(int bucket); /* which tally bucket the next fit counts into */
/*  What the tangent fit did over a build, for --mesh-check. */
void fit_stats(void);
void spur_stats(void);
void fit_stats_reset(void);
/*  What a loft is asked to draw: one strip along some pieces, described
 *  outright.  A slab is a slab because the description says so.  Nothing
 *  is steered by globals set around the call, so a reader of the loft
 *  can see what it will draw from this struct alone. */
typedef enum
{
    LOFT_LINE = 0, /* a line strip: the class's markings, margins, furniture */
    LOFT_THREAD,     /* two threads on ties */
    LOFT_SLAB,     /* a raised slab on its columns, three lanes each way */
    LOFT_SPUR      /* a spur: one lane from a slab down to a line */
} LoftKind;
struct NetFamily;
typedef struct
{
    Family                  f;       /* the family whose width and material apply */
    const struct NetFamily *fam;     /* the family answering for this strip: its material, its record, its markings */
    float                   hw, mat; /* the strip's half width and material, the family's or a slab's */
    LoftKind                kind;
    int                     struct_;       /* a spur: concrete from the line to the slab, it grades nothing */
    int                     flat;          /* lying on the ground: no lift, no soffit */
    int                     lane_piece;    /* one lane wide, drawn as the slab's outer lane */
    int                     lane_off;      /* an OFF spur's lane: the descent ends at its far (line) end */
    float                   z0;            /* how far above the ground the turn-out's slab end sits */
    float                   spur0, spur1;  /* the lift's taper at either end, tiles */
    float                   xw0, xw1;      /* the meet band each end's junction lays over this strip's, tiles.  0 for none */
    int                     pin0, pin1;    /* the ends pinned: a junction end, or a slab's */
    int                     band;          /* the slab band the stations are recorded under */
    float                   cls;           /* a line's class, or -1 to read it off the tiles */
    float                   taper;         /* a spur: the length over which the strip narrows to hw_end, 0 for none */
    float                   hw_end;        /* ... to this half width, the line lane's */
    int                     taper_start;   /* ... at the strip's start (an ON spur's line end) rather than its end */
    float                   ground_margin; /* how far past its own edges the strip reads the ground: a slab a hair */
    float                   raise;         /* a hair over the strip's usual seat: a turnout's through thread over its wye's curves, the second curve under the first */
    /*  The segment the strip belongs to, for the traffic's record, the
     *  profile's end heights and the table's station cache.  A slab or a
     *  spur leaves them zero. */
    int32_t  node[2][2];   /* the node tiles at either end */
    int      arm[2];       /* the arm each end leaves its node by, 0..3.  -1 for an end that leaves by none */
    int      nkind[2];     /* each end's node kind: 0 open, 1 a dead end, 2 a junction */
    int      ctrl[2];      /* the control at each end's arm: 0 none, 1 stop, 2 signal */
    int      cache;        /* 1 + the segment table's index the stations may come from.  0 for none */
    int      hot;          /* an edit came within a tile: the previous build's stations do not apply */
    uint64_t hash;         /* the trimmed pieces, keying the cache */
    int      records_only; /* the strip reaches no chunk this build draws: its stations, ground, profile and records, no slab */
} RLoft;
int loft_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, int comp, const RLoft *desc, const Piece *pc, int np, float total);
/*  ---- the loft as a service --------------------------------------------
 *
 *  A cross-section the SCRIPT defined, swept along pieces the script
 *  chose.  Nothing here knows what a line is.  The section is a list of
 *  points across the centerline.  The face between one point and the
 *  next is drawn in that point's material.  What a strip looks like is
 *  therefore a list of numbers a script can rewrite, and not a kind the
 *  loft has a branch for. */
typedef struct
{
    float across; /* tiles from the centerline, left positive */
    float up;     /* tiles above the seat                     */
    float mat;    /* the face from here to the next point     */
} LoftRung;
typedef struct
{
    float step_run, step_arc; /* how finely a straight and an arc are stationed.  Nought puts a station only at each piece's ends */
    float lift;               /* the seat, that far over the ground under the centerline */
    float z;                  /* ... or at this height outright, where `pinned` */
    int   pinned;
    float slot;               /* the painter's slot the faces take inside their own tile */
    int   closed;             /* the section's last point joins its first: a tube rather than a ribbon */
} LoftSweep;
/*  Answers how many faces it laid, or -1 where the mesh would not take
 *  them. */
int loft_sweep(RMesh *m, const RCity *c, uint8_t mask_bit, const Piece *pc, int np, const LoftRung *sec, int nsec, const LoftSweep *how);
#define loft(...) loft_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
int line_meet(V2 a, V2 da, V2 b, V2 db, V2 *out);
int build_island(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, int32_t col, int32_t row);
int node_kind(const RCity *c, const RAtlasLevel *l, Family f, int32_t col, int32_t row);
/*  Stage three: the junction takes its shape from the segments that
 *  reach it.  A segment records which way it leaves each junction it
 *  touches (s_arm).  The junction then hands back the distance at which
 *  the strip should start (s_trim).  So an intersection is the polygon
 *  its arms cut out and not a square.  The segments are therefore walked
 *  twice: once to measure, once to draw. */
typedef struct
{
    float   ax, ay;     /* where the arm's own path starts            */
    float   dx, dy;     /* and the way it leaves, a unit vector       */
    float   len;        /* the segment's fitted length, untrimmed     */
    int32_t fcol, frow; /* the segment's far node, and its edge    */
    int8_t  fe, fkind;  /* there.  Fkind 2 when it is a junction    */
    int8_t  cls;        /* the segment's class: 0 line, 1 avenue, 2 boulevard.  -1 thread */
    uint8_t have;
} RArm;
extern RArm  s_arm[2][R_MAP * R_MAP * 4]; /* [0] line, [1] thread */
extern float s_trim[2][R_MAP * R_MAP * 4];
extern float s_xwalk[2][R_MAP * R_MAP * 4]; /* the meet band each arm gave up line for, tiles.  0 for none */

/*  Where a family's fitted centerline passes a tile, and which way it is
 *  going there.  A level meet is built from these two, the line's and
 *  the thread's.  So the panel, the gates and the stop lines lie across
 *  the lines and threads that actually cross, at whatever angle they
 *  cross at. */
typedef struct
{
    float   x, y, dx, dy;
    uint8_t have;
} RCross;
extern RCross s_cross[2][R_MAP * R_MAP];
float         node_altitude(const RCity *c, int32_t col, int32_t row);
#include "walk/walkway.h" /* the margin network: where the margins run and what they join */

/*  The line surface's STACK, as fractions of a tile's painter's slot.
 *  Everything laid on the line lies at one height on the graded ground.
 *  So the slot is the only thing that orders it.  Two pieces given the
 *  same slot are ordered by nothing at all.  Whichever the build drew
 *  last wins the pixel, which is neither stable nor meant.  So each
 *  piece of the surface names its own place here, lowest first. */
/*  The line works' numbers.  They are the SCRIPTS': scripts/geo.lua sets
 *  every one and a script may invent more.  So there is no struct of
 *  fields here and nothing to declare before one can exist.  This is the
 *  store the scripts keep them in and read them back from.  The pipeline
 *  reads none of them by name.  Every number reaches the mesh through a
 *  rule's answer or a model's parts.  So the C cannot hold an opinion
 *  about a measurement that the scripts do not.
 *
 *  A name nothing sets reads as zero. */
int         net_geo_set(const char *name, float v);
const char *net_geo_name(int i, float *v);
/*  Where a name sits in that listing, and the value there.  A model
 *  holds the index rather than a place in the struct.  So a number a
 *  script made is named the same way the pipeline's own are. */
int         net_geo_index(const char *name);
/*  A number by name, remembering where it was found.  The cache is the
 *  reader's own and starts at -1.  For a tool or a check that wants one
 *  number: the pipeline itself asks a rule instead. */
float       net_geo(int *cache, const char *name);
/*  The look's knobs, the same way (net/box.c). */
int         net_tune_set(const char *name, float v);
/*  A knob by name, to keep: a family holds pointers at its width and
 *  its radii so every use reads the live value. */
const float *net_tune_at(const char *name);
const char *net_tune_name(int i, float *v);

/*  Outline points: four mouths' two corners each, and four returns of
 *  one segment more than the smoothness asks for.  A bound, not a knob:
 *  net_geo_set holds junc_arc to what this leaves room for. */
#define JUNC_MAX 96
/*  Where an arm meets the outline: the middle of its mouth and the way
 *  it leaves, one entry per edge.  Anything that has to tell a mouth
 *  from a free side asks for THIS rather than reading the outline's own
 *  tags.  The hull can drop one of a mouth's two points or slip a lip
 *  return's tangent between them.  A mouth read off adjacent tags then
 *  vanishes: which lays a margin straight across the line. */
typedef struct
{
    V2  mid, dir; /* the middle of the cut, and the way the arm leaves */
    V2  a, b;     /* its two corners, the right hand first */
    int have;
} JuncArm;
/*  A junction's outline must be a simple ring: no vertex where it
 *  doubles back, no two edges meet.  Counted for every junction a build
 *  lays and reported by the mesh check.  Neither fault may exist. */
void junction_outline_reset(void);
/*  A corner the outline pulled in for standing too far out, counted. */
void junction_outline_clamped(float len);
void junction_outline_print(void);
int  junction_outline_faults(void);
int junction_poly(const RCity *c, Family f, int32_t col, int32_t row, int links, V2 *out, uint8_t *mouth, int max, float trim[4], JuncArm arms[4]);
#define FAMX(x) ((x) == net_thread->f ? 1 : 0)

int walk_segment(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, const NetRun *run, uint8_t *visited);
int build_junction_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, Family f, int32_t col, int32_t row, int links, float order);
int            build_junction_done(void);
#define build_junction(...) build_junction_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
int net_compensate(void); /* the width compensation this build lofts with (walk.c) */
int put_prism_clip_m(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float dx, float dy, float len, float wid, float zb, float zf, float z0, float z1, float paint, float mat);
int build_bands(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp);
void build_bands_begin(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp);
int  build_band_next(void);
int   net_spurs_begin(const RCity *c, const RAtlasLevel *l);
int   net_spur_at(int32_t col, int32_t row);
OrientFan *net_spur_current(void);
int  net_spur_sides(const RCity *c);
int  net_spur_side_at(int i, int *free_side, int *room, int *back);
void net_spur_side_is(int i, int keep);
int  net_spur_spans(void);
int  net_spur_span_at(int i, float *at, int *len, int *leaves, int *sgn);
void net_spur_span_is(int i, int have, float top, float foot, float total, float ds);
int  net_spur_shares(void);
int  net_spur_share_at(int k, float *gap, int *cap);
void net_spur_share_is(int k, int half);
OrientFan *net_orient_at(int i);
int  build_band_chained(void);
StairFan *net_hw_chain(void);
int  build_band_fitted(void);
int  build_band_cut(void);
int  net_hw_fits(void);
int  net_hw_fit_begin(int w);
void net_hw_fit_done(int w);
const char *net_hw_fit_choice(const int **free_, const int **held);
void net_hw_fit_choice_is(int keep_free);
int  net_hw_fit_take(V2 *q, float *rad, float *tlim);
int  build_band_done(void);
int  build_band_spurs(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp);
void build_spurs_begin(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp);
int  build_spur_next(void);
int  build_spur_routed(void);
int  build_spur_joined(void);
int  build_spur_slid(void);
int  build_spur_done(void);
SlideFan *net_spur_slide(void);
int  build_spur_lofts(void);
int  build_spur_loft(int i);
void *build_band_links(RMesh *m, const RCity *c, uint8_t mask_bit);
int   build_band_links_done(void);
/*  THE BAND'S LANE ENDS, as the script that joins them sees them
 *  (mesh/lane.c).  Which slab lane goes on to which is the script's.
 *  This offers the lanes, their end poses, a station back along one, the
 *  biarc between two poses, and the laying of a link. */
void *net_links_fan(RMesh *m, const RCity *c, uint8_t mask_bit);
int   net_links_count(void);
int   net_links_lane(int i, int *slab, int *line, int *band, float *off, float *w, float *len, int *open0, int *open1);
int   net_links_pose(int i, int which, float *x, float *y, float *dx, float *dy);
int   net_links_station(int i, int which, float back, float *x, float *y, float *dx, float *dy);
int   net_links_route(float ax, float ay, float adx, float ady, float bx, float by, float bdx, float bdy, V2 *q, float *rad, float *tlim);
int   net_links_add(const Piece *pc, int np, float w, int from, int to, int band);
void  net_links_note(const char *what, int n);

/*  lane.c: lanes as primitives: the router, and the lanes at an
 *  intersection (docs/future.rst, "Lanes as primitives"). */
void lane_reset(void);
void seg_table_reset(void); /* net/table.c: the segment table, kept across the two passes */
void net_prof_reset(void);
/*  THE ONE DOOR (net/drive.c): the only place the renderer calls up into
 *  a script.  A build hands its passes out one at a time and a move
 *  hands out the world that moves.  Which of the two a turn is, the
 *  script reads off the handle. */
enum
{
    DRIVE_BUILD = 1,
    DRIVE_MOVE  = 2
};
int  net_drive_build(void);
int  net_drive_move(void);
int  net_drive_what(void);
/*  THE BEAT the moving world runs on, as the script drives it.  It holds
 *  the gates to swing, the cars to move, and what each of them sees.  It
 *  also holds the move itself.  net_gate_rest_is takes the one angle the
 *  build's drive asks for: where a gate settles with nothing near it. */
void  net_gate_rest_is(float angle);
void *net_moving_fan(void);
int   net_beat_owed(void);
int   net_beat_draws(void);
/*  One beat, or as much of it as runs before a train reaches a junction.
 *  Answers 1 where it stopped for an arm to be chosen.  Is entered again
 *  once the rule has answered. */
int   net_beat_run(void);
void  net_beat_step_reset(void);
int   net_beat_arms(int *n, float *hx, float *hy);
int   net_beat_arm_at(int k, float *dx, float *dy);
void  net_beat_arm_is(int k);
int   net_beat_build(void);
int   net_beat_gates(void);
int   net_beat_gate(int i, float *angle, float *near, float *dt);
void  net_beat_gate_is(int i, float angle);
int   net_beat_cars(void);
int   net_beat_car(int i);
/*  The arms at the junction the car in hand is about to reach, for
 *  arc.rules.car_turn.  How many, the draw the world made, each one's
 *  heading away from the node, and the one the rule chose. */
/*  The signal at the junction the car in hand faces, for arc.rules.signal. */
/*  A junction's stagger, and what a script makes of it.  It gives which
 *  phase of the cycle it starts at, and which group of arms an edge
 *  belongs to. */
/*  A junction's arms, for the rule that stands its signs. */
int   net_junction_signs_ask(void);
int   net_junction_signs_at(int e, int *ctrl, float *h, int32_t *col, int32_t *row);
float net_junction_signs_order(void);
void  net_junction_signs_taken(void);
void  net_junction_signs_enter(void);
void  net_junction_signs_leave(void);
int   net_signal_stagger(int32_t col, int32_t row);
void  net_signal_phase_is(int k, float phase);
void  net_signal_group_is(int e, float group);
float net_signal_phase(int k);
float net_signal_group(int e);
int  net_beat_signal(int32_t *col, int32_t *row, float *hx, float *hy, float *time, int *stagger);
/*  The thread signals, for arc.rules.thread_signal: how near the nearest car
 *  is each way along the block, and the aspect the rule answered with. */
/*  How many cars a tile's traffic is worth, for every byte there is. */
void net_line_class_is(int tv, int cls);
void net_car_density_is(int tv, int cars);
int  net_signals(void);
int  net_signal_at(int i, float *ahead, float *back);
void net_signal_is(int i, const char *model);
int  net_beat_turn(int *n, unsigned *draw);
int  net_beat_turn_at(int k, float *dx, float *dy);
void net_beat_turn_is(int k);
int   net_beat_reading(float *speed, int *have_gap, float *gap, int *have_ctrl, float *ahead, int *held, int *nx,
                       float *stop, float *free_, float *line, float *creep, float *step);
float net_beat_lap(int k);
void  net_beat_car_is(int i, float v);
/*  And the moving world DRAWN: the gate arms the drive lays, and the
 *  mesh they go into. */
int   net_movers_gates(void);
int   net_movers_gate(int i, float *x, float *y, float *fx, float *fy, float *angle, float *len, float *order);
void *net_movers_mesh(const void **city, uint8_t *mask_bit);  /* net/walk.c: where a pass's time goes, stage by stage, under --times */
void net_prof_print(void);
void net_table_free(void); /* net/table.c: the sample arenas, once, at exit */
int  seg_table_count(void);
int  grade_only(int allowed); /* mesh.c: the grading pass, skipping what only the drawing needs */
int  seg_table_replay(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, uint8_t *visited);
int  seg_table_get(int i, int32_t *col, int32_t *row, int32_t *cc, int32_t *cr, const int32_t **tcol, const int32_t **trow, int *nt);
int  seg_table_nodes(int i, const V2 **q, const float **rad, int *nk); /* net/table.c: a segment's or band's fitted nodes and radii */
const char *band_spur_lost(int32_t col, int32_t row);             /* band.c: why the on-spur at a tile was not built this pass, or NULL */
int  band_count(void);                               /* band.c: the bands' own tiles, -1 past its table */
int  band_get(int i, const int32_t **tiles, int *n); /* ... as row * R_MAP + col */
/*  A junction's connectors, in the drive's two halves.  The chains run
 *  from every inbound lane to every outbound lane of the other arms,
 *  queued for the cut.  Laid once the script has cut them. */
void lane_junction_ask(RMesh *m, const RCity *c, uint8_t mask_bit, Family f, int32_t col, int32_t row, int links);
int  lane_junction_take(void);
/*  The arms of the junction in hand, for arc.rules.turns: which of them
 *  joins which is the rule's.  There is no matcher in C behind it. */
int  lane_turns_ask(void);
void lane_turns_info(int *col, int *row, int *arms, const char **fam);
int  lane_turns_arm(int e, int *lanes, int *into, int *out, int *spur);
int  lane_turns_want(int e, int k, int e2, int k2);
/*  A segment's dead ends' caps, once the drive has cut the chains
 *  lane_segment queued for them. */
int  lane_segment_caps(void);
/*  A segment's dead ends, for arc.rules.cap: the lane arriving at each
 *  and the lane leaving it, and the two ways they may be joined. */
int  lane_caps_ask(void);
void lane_caps_info(int *n, const char **ends, const char **fam);
int  lane_caps_at(int i, int *end, int *lane, int *from, int *to);
int  lane_caps_merge(int i);
int  lane_caps_link(int i);
int  lane_segment(RMesh *m, const RCity *c, uint8_t mask_bit, Family f, const Piece *pc, int np, int32_t col, int32_t row, int e, int kind0, int32_t cc, int32_t cr, int back, int kind1, float hw, int cls);
void lane_stats_print(void);
void lane_dump_pieces(const Piece *pc, int np);
int  lane_slab(RMesh *m, const RCity *c, uint8_t mask_bit, const Piece *pc, int np, float hw, int band);
/*  THE LANES WITHIN REACH OF A POINT (mesh/lane.c), measured and offered
 *  for the script to pick one.  Which lane a spur's end fastens to is
 *  arc.rules.spur_lane's.  One candidate per piece of every lane within
 *  reach.  The pick is an index into them, or -1 for none. */
void net_spur_snap_what(const char **what, int *band);
void net_spur_snap_is(const char *what, int band); /* which snap the drive is being asked about */
/*  The chain the router lays between two poses.  Two points where B is
 *  dead ahead of A, else the equal-tangent biarc's four.  Cutting it
 *  into pieces is arc.rules.pieces's and happens elsewhere. */
/*  ---- THE PIECE (mesh/piece.c) ------------------------------------------
 *
 *  A straight or an arc, and the general arithmetic over one.  Nothing
 *  here knows what a piece is for. */
V2   l_right(V2 d);
void l_piece_at(const Piece *p, float t, V2 *pos, V2 *dir);
void piece_offset(const Piece *p, float off, Piece *o);
void pieces_reverse(Piece *p, int np);
void extent_add(float x, float y, float *x0, float *y0, float *x1, float *y1);
void piece_extent(const Piece *pc, float *x0, float *y0, float *x1, float *y1);
void piece_near(const Piece *pc, V2 p, V2 *q, V2 *dq);
int  pose_chain(V2 A, V2 tA, V2 B, V2 tB, V2 *q, float *rad, float *tlim);
int  lane_snap_ask(V2 p, V2 dir, float maxd);
int  lane_snap_count(void);
int  lane_snap_at(int i, int *lane, int *cls, int *band, float *off, float *dist, float *dot, float *x, float *y, float *dx, float *dy);
void lane_snap_is(int i);
int  lane_snap_take(V2 *pos, V2 *odir, float *dist);
int  band_slide_snap(SlideFan *s, float at);
/*  How a spur meets the meet beside it, the rule's answer for the
 *  reading in hand.  And the drive settling all sixty-four readings. */
/*  Where a spur aims on the line it comes down to, for arc.rules.spur_target:
 *  the reading, and the point, tangent and travel it answers with. */
int  build_spur_target(void);
int  net_spur_target_at(int *fork, int *off, float *rdx, float *rdy, float *mdx, float *mdy,
                        float *fx, float *fy, float *lane_off, float *merge_along);
void net_spur_target_is(float bx, float by, float tbx, float tby, float tvx, float tvy);
/*  Margins as a primitive (margin.c): every margin is registered by its
 *  two ends, and the check counts the ends that meet nothing. */
enum
{
    MARGIN_STRIP = 0, /* a line strip's margin, one each side           */
    MARGIN_LINK  = 1, /* a junction's, from one mouth's end to the next's  */
    MARGIN_CAP   = 2  /* round a dead end's turning head                    */
};
void                margin_enable(int on);
int                 margin_on(void);
/*  What is true of every strip and junction of the family.
 *
 *      The margin's share of the band.
 *      The junction box.
 *      A family's threads and a level meet's approach.
 *
 *  The script's answer, kept for a build. */
const ScriptFamily *net_family_rules(Family f);
/*  The knobs of the family the scripts named `line`: the lane model's,
 *  the spur's and the margin's reach are filed there. */
const ScriptFamily *net_line_rules(void);
/*  Where a family's lanes run at that class, from the centerline, inner
 *  first: arc.rules.lanes's answer.  Answers how many were written. */
int net_lane_offsets(Family f, int cls, float *off, int max);
void net_lane_runs_reset(void);
int  net_lane_runs(void);
void net_lane_run_at(int i, const char **fam, int *cls);
void net_lane_run_is(int i, const float *off, int n);
void                margin_reset(const RCity *c);
int                 margin_add(int kind, V2 a, V2 b, V2 oa, V2 ob); /* oa, ob: the way out past each end, or zero */
void                margin_stats_print(void);
typedef struct JBox JBox; /* the junction's box (below, with the working structs): the margin round it takes the box whole */
/*  A junction's fill as the composing script is handed it: the polygon
 *  the fan is laid over.  The outline the arms cut out, inset by the
 *  margin's band where there is one.  And where its middle sits.  The
 *  outline itself is the pipeline's: arms, corners, trims and lip
 *  returns are a solver's work.  What is drawn over it is not. */
/*  A margin as the composing script is handed it.
 *
 *      Which kind of band it is.
 *      The stations the network holds for it.
 *      Where it sits.
 *
 *  The network's own work.  Which ports it names, what it joins, how
 *  deep a meet the arm could spare.  Is the pipeline's.  The band drawn
 *  over those stations is not. */
typedef struct
{
    void *m;
    const void *c;
    uint8_t     mask_bit;
    const void *w;   /* the WalkPath */
    const void *st;  /* its WalkSt stations */
} WalkFan;

/*  The margins, from the network once it is complete.  The PASS is the
 *  script's (scripts/compose/world.lua): these gather one path at a time
 *  and it composes them.  `outline` says which rule to ask: in outline
 *  the bands stand aside and the network is drawn in their place. */
int                 margin_count(void);
int                 margin_outline(void);
int                 margin_gather(RMesh *m, const RCity *c, uint8_t mask_bit, int i, WalkFan *out, ShapeId *sh);

/*  One lane, connector or band edge as the outline view draws it.  It
 *  holds the pieces the fit produced, the paint they are drawn in.  How
 *  far over the surface they float and which slab band their height
 *  comes from.  Where the line runs is the fit's.  The hairline over it
 *  is not. */
typedef struct
{
    RMesh       *m;
    const RCity *c;
    uint8_t      mask_bit;
    const Piece *pc;
    int          np;
    float        lift, paint;
    int          band;
    /*  A spur's lane climbs to the slab as it goes.  So its height is
     *  eased along the whole line rather than taken from the ground.
     *  `spur` says so, and `off` says which end the gore is at. */
    int          spur, off;
    float        step;
} LaneFan;
/*  A fitted path as the composing script finishes it.  It holds the
 *  vertices the join stage produced.  It also holds the tangent length a
 *  biarc vertex was built with, which is -1 where the vertex was
 *  searched.  It holds the radius and tangent limit each corner is to be
 *  given.  Which vertices are idle and what radius a corner gets are
 *  decisions.  The corridor sweep that answers what a radius may be is a
 *  primitive and stays in C. */
typedef struct
{
    V2    *out;
    float *fixed, *rad, *tlim;
    int    n;
    float  res0, res1, rmax, rmin, band, share, trim_cap;
    const void *mark; /* the corridor, for the sweep */
} FitFan;
FitFan   *path_ending(void);

/*  A junction's ring as the margin reads it.  Which of its edges carry a
 *  band, and which are a line's mouth.  It also gives which way each
 *  faces into the junction, and the ring moved in by the margin's width.
 *  The ring itself is arc.rules.outline's.  How the margin sits on it is
 *  arc.rules.junction_band's. */
typedef struct
{
    const JBox    *jb;
    const V2      *poly;
    const JuncArm *arms;
    int            np;
    float          lw; /* the margin's width round this junction */
    /*  What the script answers: a flag and an inward normal an edge, the
     *  arm each edge is the mouth of, and the ring moved in. */
    uint8_t *band;
    V2      *nrm;
    int8_t  *edge_arm;
    /*  The offset direction at each end of each edge.  It is mitred
     *  where the band turns a corner of the ring.  So a lip round a
     *  return is one smooth band and not a row of quads each square to
     *  its own edge. */
    V2      *mitre0, *mitre1;
    V2      *inset;
    int      inset_max, inset_n;
} BandFan;

/*  A junction's outline as the composing script works it out: the arms
 *  that leave it.  Where each path starts, the way it goes, which edge
 *  it belongs to.  And the numbers the junction is sized by.  The script
 *  sorts them, finds the corner between each pair, cuts each arm's mouth
 *  back and walks the ring.  What the pipeline keeps is the arm table
 *  those rays come from and the check on the finished ring. */
typedef struct
{
    int     f;
    int32_t col, row;
    float   cx, cy, w, far, gro, cap;
    int     lips;
    /*  The arms, in the order the tile's edges gave them. */
    struct
    {
        float ox, oy, dx, dy, ang;
        int   e;
    } arm[4];
    int na;
    /*  What the script answers: the ring, the mouth tag at each point,
     *  and how far along each edge's arm its strip starts. */
    V2      *out;
    uint8_t *mouth;
    float   *trim;
    int      max, n;
} OutlineFan;

/*  A level meet's panel as the composing script is handed it.  It holds
 *  the four corners where the line's edges meet the thread bed's, which
 *  the two paths' own lines settle, and the surface under each.  What is
 *  laid over them.  The panel, and the stop line before it, are not the
 *  solver's. */
typedef struct
{
    RMesh       *m;
    const RCity *c;
    uint8_t      mask_bit;
    float        q[4][2];
    float        ground[4];
    float        order, lift, slot;
} LapFan;

const LapFan *net_lap_panel(void);
int            net_lap_approaches(void);
int            net_lap_approach(int i, ScriptApproachAsk *out);
float net_lap_order(void);
int            net_lap_place(const ScriptApproach *mk);

/*  The slab's own surface near a point, for a line that belongs to a
 *  band rather than to the ground (net/lane.c). */
float slab_z_near(const RCity *c, uint8_t mask_bit, int band, V2 p);

typedef struct
{
    const JBox *jb;
    const V2   *poly;
    int         np;
    float       cx, cy, zj;
    float       mat, order;
    /*  Where the box's quad goes when the OUTLINE came to nothing: the
     *  square it always was.  The fan is laid over whatever polygon
     *  there is either way, so a box can have both. */
    int          square;
    const float *a0, *a1, *b0, *b1;
} JuncFan;
int                 margin_junction(const JBox *jb, const V2 *poly, const JuncArm *arms, int np);
/*  The outline the junction's FILL is laid on.  It is the box's outline,
 *  with every side that carries a margin drawn in to the margin's own
 *  inner edge.  So the two meet along it instead of the fill being laid
 *  under the band and hiding it.  A mouth keeps the outline, since the
 *  line runs on through.  Up to 2*np points. 0 when the box carries no
 *  margin at all, and the fill then reaches the outline. */
int margin_junction_inset(const JBox *jb, const V2 *poly, const JuncArm *arms, int np, V2 *out, int max);
/*  Which of a junction's arms may carry a meet, from its outline alone:
 *  `want[e]` is the band a stripe asks for there, 0 for none.  How much
 *  of it the arm can actually spare is walk.c's.  This knows how long
 *  the line beyond the mouth is. */
int margin_junction_wants(const RCity *c, Family f, int32_t col, int32_t row, const V2 *poly, const JuncArm *arms, int np, float lw, float *want);
/*  The margin round one junction, as the DRIVE has it answered: the ring
 *  handed to arc.rules.band, the answer taken.  Then each mouth read and
 *  answered by arc.rules.lap_at.  Nothing in the margin calls up.  A
 *  script that answers neither leaves the junction bare. */
void *margin_band_ask(const JBox *jb, const V2 *poly, const JuncArm *arms, int np, float lw);
/*  The same, for the box the drive is drawing: the ring, the mouths off
 *  it, and the fill laid inside the answer. */
void *net_junction_band(void);
int   net_junction_mouths(void);
int   net_junction_band_done(void);
void  margin_band_answered(void);
int   margin_mouths_ask(const V2 *poly, int np, float lw);
int   margin_mouth_at(int k, int32_t *col, int32_t *row, int *arm, int *ctrl, int *pave, float *cs, float *span);
void  margin_mouth_is(int k, int marked, float deep);
/*  And the drive's own walk over the junctions stage three measures. */
int   net_trim_junctions(const RCity *c, const RAtlasLevel *l);
void *net_trim_band(int i);
int   net_trim_mouths(void);
void  net_trim_done(int i);
/*  The band one arm gave up line for, 0 for none (walk.c s_xwalk). */
float net_cross_depth(Family f, int32_t col, int32_t row, int e);
float               junc_surface(Family f, const RCity *c, uint8_t mask_bit, int col, int row, float x, float y, float zj);

typedef struct
{
    V2    pos, dir;
    float s, z;   /* z: the section's height, the highest ground it spans */
    float wr, wl; /* a slab's half width either side, as a fraction of hw: 1, or 0.70 where a spur took the outer lane */
    float xd;     /* the distance along to the nearest level meet on the segment */
    float zr[2];  /* the spur lane's height on the right and the left, where `lane` says there is one */
    int   lane;   /* bit 1: a spur lane strip on the right.  Bit 2: on the left */
    int   split;  /* a station where two tiles meet: the ground is the higher side's */
} Sample;

void band_lane_stations(Sample *smp, int ns); /* the slab's lane drop, per station (net/band.c) */

/*  A shelf's corner over the drawn surface (mesh/surface.c). */
float surface_corner_height(const Sample *smp, int ns, int i, float dx, float dy, float shelf_grade, Family f);

/*  The street furniture pass (furniture.c) and the line-marking pass
 *  (marking.c): each switched as one. */
void          furniture_enable(int on);
int           furniture_on(void);
int           marking_near_lap(const RCity *c, V2 pos);
const HwSpur *lane_spur_tile(int32_t col, int32_t row); /* the spur at a tile, or NULL */
int           lane_spur(RMesh *m, const RCity *c, uint8_t mask_bit, const Piece *pc, int np, float hw, int slab_lane, int line_lane, int line_port, int off);
int           lane_port_id(int col, int row, int e, int out, int k);
int           lane_table_count(void);                                                       /* the lanes as built: spurs, connectors, slab lanes */
int           lane_table_get(int i, int *cls, int *fam, const Piece **pc, int *np, float *w);
void          lane_check_ends(void);
void          band_lanes(const RCity *c);
XLaneFan     *lane_cross_ask(RMesh *m, const RCity *c, uint8_t mask_bit);
int           lane_cross_take(void);
void          net_wires_reset(void);
int           net_wires(void);
LaneFan      *net_wire_at(int i);
void          net_wire_done(int i);
#define LANE_CLS_LINE 0
#define LANE_CLS_TURN 1 /* a junction's connector: the near lane inside a meet piece's box */
#define LANE_CLS_SLAB 2
#define LANE_CLS_SPUR 3 /* a lane dropped off a slab to a line */
#define LANE_CLS_LINK 4 /* a band's lane on to the next band's, or into its own inner lane */

/* ---- the working structs the stages hand each other ---------------------- */
typedef struct
{
    int32_t  col, row, cc, cr;
    int8_t   e, back, kind0, kind1, square0, square1;
    Family   f;
    float    cls, hw, total;
    int      first, np;  /* pieces, in s_segp */
    int      qfirst, nk; /* the fit's nodes, in s_segq / s_segrad */
    int      tfirst, nt; /* the corridor tiles, in s_segt */
    int      mfirst, nm; /* the visited marks, in s_segm */
    int      sfirst, ns; /* the loft's stations, in the build's sample arena.  Ns 0 for none yet */
    uint64_t phash;      /* the trimmed pieces they were sampled from */
    int      band;       /* a band band: walked by band.c, no arms, no meets.  Replayed by band.c */
} RSeg;
/*  A band in the table.  The grading pass stores it from its walk and
 *  fit.  Its own tiles are its corridor, and its start cell and way its
 *  key.  It is replayed by the building pass (band.c). */
int         seg_store_band(int32_t col, int32_t row, int ew, int sign, const Piece *pc, int np, const V2 *q, const float *rad, const float *tlim, int nk, const int32_t *own, int n_own);
const RSeg *seg_table_entry(int i);
int         seg_table_unchanged(int i);   /* its pieces hash as the previous build's did */
void        seg_table_misses_print(void); /* the station cache's misses this pass, by reason, under --times */
int         seg_table_band_index(int k);  /* the k-th band's entry, in walk order */
void        seg_table_arenas(const RSeg *r, const Piece **pc, const V2 **q, const float **rad, const float **tlim, const int32_t **tcol, const int32_t **trow);
typedef struct
{
    RMesh             *m;
    const RCity       *c;
    const RAtlasLevel *l;
    uint8_t            mask_bit;
    int                comp;
    Family             f;
    int32_t            col, row;
    int                e;
    uint8_t           *visited;
    V2                *pts, *q;
    float             *rad, *tlim;
    int32_t           *tcol, *trow;
    Piece             *pieces;
    int                nt, n, k, nk, np, kind0, kind1, square0, square1, ee;
    float              hw, total;
    float              cls; /* a line's class from seg_class (line.c).  The arms, the table and the loft read it */
    int32_t            cc, cr, back, guard;
    int32_t           *marks; /* every (tile, edge) the walk marked visited */
    int                nm;
    int                records_only; /* as the loft's: the cap's fan is not drawn either */
} Seg;

typedef struct
{
    int i0, i1; /* the steps it covers, inclusive                        */
    int kind;   /* 0 a straight, 1 a slope                               */
    V2  da, db; /* its steps: the majority, and the minority             */
    int period; /* a slope's steps per period: 2 at 45 degrees, 3 at 2:1 */
    V2  p, d;   /* the line: a point on it, and its unit direction       */
    int ta, tb; /* the points it spans, first and last                   */
} Run;

/*  A path's steps as the composing script cuts them into runs.  The
 *  score of each prefix is the script's business and so is which span
 *  wins it.  The geometry of a candidate.  The line a slope lies on, the
 *  chord a free span is given, whether either holds on the corridor and
 *  keeps its covered tiles.  Is the pipeline's, asked for through `try`.
 *  The script keeps the winner of each prefix, then walks the chain back
 *  and names the runs. */
struct RunFan
{
    const V2      *st, *pts; /* the steps, and the points they join */
    const uint8_t *mark;     /* the corridor */
    int            ns, cap;
    float          band;
    int            free_lines; /* a span may be any chord that holds and covers */
    int            ex0, ex1;   /* a junction tile at that end of the chain */
    int           *slen, *sper; /* the slope reaching from each step, and its period */
    V2            *sda, *sdb;   /* that slope's majority and minority steps */
    const int     *code;        /* each step, as the first step it is the same as */
    const int     *moves;       /* and whether it goes anywhere at all */
    Run            cand;        /* the candidate under test */
    Run           *won;         /* the candidate that won each prefix */
    Run           *runs;
    int            nr;
};

/*  A path's runs as the composing script makes the chain of lines from
 *  them.  The chain is the start's line, every run, and the goal's.
 *  What the script decides is whether either end needs a line of its
 *  own.  Whether a slope that owns a junction's tile pulls the end onto
 *  its own angle.  The lines themselves are the pipeline's arithmetic. */
/*  One meet of two lines, as the composing script judges it.  The meet
 *  has to be ahead of the line behind and behind the line ahead.  An arc
 *  that reads as an arc has to fit there.  How far ahead and how far
 *  behind are measured here.  How much of either is enough is the
 *  script's. */
struct JoinFan
{
    void *fit, *pair; /* the fit and the boundary, the pipeline's own */
    V2    at;         /* where the lines cross */
    int   free_line;  /* a free line at one end or the other */
    float ahead, behind;  /* the meet past the line behind, and short of the one ahead */
    float reach, reach_on; /* how much line there is either side of it */
    float len_in, len_out; /* the edge either side of the meet */
    float fixed_prev;      /* the tangent the vertex behind was built with, -1 for a searched one */
    float res0, res1;      /* what the chain's own ends must be left */
    float need;            /* the tangent an arc of the smallest legal radius needs here, -1 for a corner with no turn */
    float share, trim_cap; /* how a corner splits an edge, and how far a junction may cut an arm back */
    int   first, last;     /* the chain's own ends, which spare a reserve rather than a share */
    int   placed;
};

/*  Two of a path's lines that do not cross, as the composing script
 *  bridges them.  An S is drawn between them, its tangent points pulled
 *  back along each line: the farther back, the wider its arcs.  How far
 *  back either may go, and in what order the placings are tried, are the
 *  script's.  Whether a placing yields a biarc the band holds is the
 *  pipeline's, asked through `try`. */
/*  Two of a path's lines that neither cross nor take a biarc, as the
 *  composing script walks between them.  The line's end, the points of
 *  the gap, the next line's start.  Which of those are worth a vertex,
 *  and whether the gap is a sideways step to be drawn as one diagonal,
 *  are the script's. */
/*  One corner of a fitted path, as the composing script sweeps a fillet
 *  into it.  How wide an arc the corner could take, how narrow one may
 *  be before it stops reading as an arc.  How the search walks between
 *  the two are the script's.  Whether an arc of a given radius stays on
 *  the corridor is the pipeline's.  So is whether it still covers the
 *  tiles the corner was drawn for.  This is the pipeline's, asked
 *  through `holds`. */
/*  A fitted path's vertices as the composing script cuts them into the
 *  pieces a strip is lofted from: straights and arcs, end to end.  What
 *  radius a corner is finally given.  Clamped by its budget, by the
 *  piece already laid on the way in, and by the edge it leaves along.
 *  Is the script's.  Where the tangent points and the arc's center fall
 *  is the pipeline's. */
/*  A band band's cells as the composing script picks the points the fit
 *  is given.  A STAIRCASE is curve blocks turning alternately, with at
 *  most a few straight cells between.  It is the game's way of laying a
 *  diagonal, is one point, the center of the blocks and short straights
 *  it is made of.  So the run before it, the diagonal through its middle
 *  and the run after it are three legs the fit fillets at their two
 *  bends.  Every other cell is a point of its own. */
/*  A band strip's elevation as the composing script lays it out.  A SPUR
 *  is one straight line from the ground at its line end to the ground at
 *  its slab end.  A SLAB is stiff, held to a grade and rounded over a
 *  window so it neither follows every bump nor dips into a hollow.  Both
 *  then take the lift, tapered over the spur cells at each end.  The
 *  stations and the ground under them are the pipeline's. */
/*  A spur's join, as the composing script slides it.  The descent may
 *  start further along the slab and the join may sit further along the
 *  line.  Every pairing of the two is routed and the widest that holds
 *  wins.  Whether a pairing routes at all, and whether it leaves by the
 *  spur tile's own line edge, are the pipeline's. */
/*  A slab's stations as the composing script narrows them for each spur
 *  that leaves it.  The slab gives up its outer lane over the spur's
 *  taper and takes it back afterwards.  Where it does so, and how far it
 *  stays narrow toward a partner spur, is the script's. */
/*  A strip's elevation as the composing script spurs it between the
 *  nodes at its ends.  A node is a junction, a dead end or a level lap.
 *  It stands at its own tile's leveled height.  Every corridor that
 *  reaches it spurs to that one number, so two segments meeting at a
 *  junction agree without anything being solved between them.  Which
 *  stations are anchors, and how the spur runs between them, are the
 *  script's. */
/*  An on-spur's four sides, as the composing script reads them.  One is
 *  the slab it climbs to, one the line it comes down onto, and which is
 *  which decides everything the spur is afterwards.  What each neighbor
 *  IS is the pipeline's.  Which side is the slab's and which the line's
 *  is the script's. */
/*  The shelf's copies of each corner, as the composing script reconciles
 *  them.  Every tile keeps its own copy of a corner.  Which is what lets
 *  two corridors lie side by side as two shelves with a wall between
 *  them.  And the copies were written by whichever station was nearest,
 *  so one corridor's tiles can take the corner they share from different
 *  stations and disagree.  Which copies must agree, and on what, is the
 *  script's. */
/*  A line's lanes as the composing script carries them across a meet.  A
 *  thread across a line, or a line under a slab, ends the segments on
 *  both sides of the lap tile.  Their lanes then face each other open.
 *  Which open end goes on into which is the script's.  Where each lane's
 *  end lies and how one is drawn to the other is the pipeline's. */
struct XLaneFan
{
    void       *m;
    const void *c;
    uint8_t     mask_bit;
    int         n;    /* the lanes as they stand.  Links are appended after */
    int         fail; /* a link the router could not draw */
};

struct ShelfFan
{
    int nodes; /* how many node tiles there are */
};

struct OrientFan
{
    const void *c;
    const void *l; /* the art, for the links a tile's piece claims */
    int32_t     col, row;
    int         kind;  /* 0 nothing here, 1 a spur beside a slab, 2 a slab end-on */
    int         dside, rside, eside, off, lines;
    /*  THE SPUR THE RULE MADE OF IT: which way it lies along the slab.
     *  This way the slab is, and which side its taper falls on.  How
     *  many tiles it reaches, what the line it comes down to is and how
     *  it meets the meet there.  `has` says the rule answered one at
     *  all.  A cell it answered none for carries no spur.
     *
     *  `off` above is the TILE'S own reading.  Which way round its line
     *  puts it.  And `r_off` is the SPUR'S, after the taper's side was
     *  chosen.  The two differ whenever the taper went the other way,
     *  and both are used: the spur is built on r_off, the join reads
     *  off. */
    int   has, r_off, len, opp;
    float ax, ay, tx, ty;
    /*  And the LINE it comes down to, as the same rule read it.  The way
     *  to the line tile, the way the lane leaves along the line.  What
     *  the line tile is.  0 is no junction, and 1 a stub it ends on.  2
     *  is a through line it forks onto, and 3 a line carrying straight
     *  through. */
    int   fork, arm;
    float rdx, rdy, mdx, mdy;
};

struct GroundFan
{
    void  *smp;
    const void *city;
    int    n;
    float  total;
    int    pin0, pin1;   /* the end reaches its node's own tile */
    int    dead0, dead1; /* and is a dead end, which is pinned whatever the family */
    int    pin_node;     /* the family's junction is not a turnout, so an arm reaches its node */
    float  lift;
};

struct DropFan
{
    void  *smp;
    int    n, nspurs;
    float  reach;  /* how far a spur reaches for the station it drops from */
    float  narrow; /* the outer lane's inner edge, across the band */
};

struct SlideFan
{
    void  *spur;             /* the Spur being built */
    int    lane;             /* the line lane the join must land on */
    V2     B0, tB0, trav;    /* where the join aims, and the lane's own way */
    float  reach;            /* how far along the slab the descent may start, to the lane line */
    int    parallel;         /* the slab and the lane never meet: no sliding along the slab */
    float  merge;            /* how far along the line the join may slide */
    float  snap;             /* how near a recorded lane counts as on it */
    float  taper;            /* how far past the line edge the taper runs */
    /*  The placing under test, and the best kept. */
    Piece *tmp;
    int    n;
    float  r, beyond;
    V2     pos, dir;  /* where the placing under test lands on the lane */
    float  at;        /* and how far along the line it sits */
    V2     Q;         /* where the descent leaves the slab, `lead` along it */
    float  lead;
    float  best;
    /*  Where the answer goes. */
    Piece *pc;
    int   *np;
    float *rmin, *out_merge, *out_taper;
    V2    *B, *tB;
};

struct ProfFan
{
    void *smp;
    int   n;
    float total;
    int   spur;       /* the strip is a structure: a spur or a lane drop */
    int   lane_piece; /* a lane drop's turn-out, which eases rather than runs straight */
    int   lane_off;   /* and which end of it the gore is at */
    int   flat;       /* the strip takes no lift at all */
    float z0;         /* how far above the ground its slab end sits */
    float spur0, spur1; /* how far the lift is tapered in at each end */
    float grade, stiff; /* the slab's steepest rise, and the window it is rounded over */
    float lift;         /* how far a slab rides above the ground */
};

struct StairFan
{
    const void    *c;     /* the city, for the pin test */
    const V2      *pts;
    const uint8_t *block; /* the cell is a curve block */
    int            n, gap; /* and how many straight cells a stair may step over */
    V2            *chain;
    int            nc;
};

struct PieceFan
{
    const V2    *q;
    const float *rad, *tlim;
    int          n;
    Piece       *out;
    int          np;
    int          over; /* the piece pool filled: nothing more goes in */
    V2           cur;  /* where the last piece left off */
    /*  The corner just read, kept so the fillet is swept with the same
     *  numbers the script was shown. */
    V2           ui, uo;
    float        cross, theta, tan_half;
};

struct SweepFan
{
    const uint8_t *mark;
    V2             b, ui, uo;   /* the corner and its two unit legs */
    float          theta, tan_half, cross;
    float          tlim;        /* the tangent the corner has been given */
    float          rmax, rmin, hw, margin;
    int            padded;      /* the corridor is the line's own tiles, so the band carries the margin */
    int            straight;    /* the legs are in line, or one has no length: nothing to sweep */
    int            probe;       /* --sweep-probe names this corner */
    float          r;           /* what the script settled on */
    int            tight;
};

struct StepFan
{
    void *fit, *pair;
    int   gap;        /* how many of the chain's own points lie between the lines */
    int   head, tail; /* the line either side is a run, not the chain's own end line */
    float hw;         /* the band's half width, which decides how wide a step may be stepped */
    int   placed;     /* the diagonal went in, so the walk is done */
};

struct BridgeFan
{
    void *fit, *pair;
    float len_in, len_out; /* the line either side of the pair */
    float fixed_prev;      /* the tangent the vertex behind was built with, -1 for a searched one */
    float gap;             /* how far along P the two lines' ends lie apart */
    float res0, res1;      /* what the chain's own ends must be left */
    float share;           /* how much of an edge a corner takes of its own */
    float band, margin;    /* the band, and the padding a line's corridor carries */
    int   padded;          /* the corridor is the line's own tiles */
    int   head, tail;      /* the line is the chain's own end line, which draws nothing back */
    int   first, last;     /* the vertex either side is the chain's own end */
    int   probe;           /* --sweep-probe names this pair */
    V2    c0, c1, b0, b1;  /* the placing just tried, and the best kept */
    float d, best_d;
    float solve_in, solve_out; /* the edge outside the placing just solved */
    float try_a, try_c;    /* what it was drawn back by, for the probe */
    int   try_f, try_shift;
    int   placed;
};

struct ChainFan
{
    const V2 *pts;
    Run      *runs, *lines;
    int       nr, nl, nt, ns;
    int       ex0, ex1;       /* a junction's tile at that end of the chain */
    V2        start, goal;    /* the ends, which `aim` may move */
    V2        st0, st1;       /* the first and last steps, which their lines run along */
};

typedef struct
{
    RMesh       *m;
    const RCity *c;
    uint8_t      mask_bit;
    int          comp;
    Family       f;
    int          pin0, pin1;
    Sample      *smp;
    float       *zraw;
    float        hw, mat, total;
    const Piece *pc;
    int          np;
    int          ns;
    const RLoft *d; /* what the loft was asked to draw, for the stages other modules supply */
} Loft;


/*  The strip the loft worked out, and the slab laid over it.  The loft
 *  stops before the slab, so what is drawn over the stations is settled
 *  outside it.  Net_loft_compose is that, for the callers still in C. */
Loft       *net_loft_strip(void);
void        net_loft_slab_done(double tp);
int         net_loft_draws(void);
const char *net_loft_taper(void);
/*  THE STRIP IN FLIGHT and the one whose slab is still to be laid.  The
 *  loft (mesh/loft.c) fills them, the drive (net/loft.c) stops at each
 *  of its stages over them. */
extern RLoft   s_ldv;
extern Loft    s_lx;
extern int     s_lx_live;
extern Loft    s_slab_x;
extern int     s_slab_ready;
extern uint32_t s_slab_sh;
extern double  s_slab_tp;
extern int     s_slab_records_only;
extern const RLoft *s_ld;              /* the strip being lofted, for both halves */
extern float   s_zorig[];              /* the ground's own line under each station */
extern double  s_lx_tp;                /* when the stage in hand started, for the profile */
void note_add(char *buf, size_t cap, size_t *n, const char *fmt, ...);

void  net_stage_hand(void *obj, const char *kind);
void *net_stage_taken(const char **kind);
const char *net_loft_profile(GroundFan **g);
const char *net_loft_dropped(void);
const char *net_loft_works(void);
const char *net_loft_record(void);
const char *net_loft_furniture(void);
const char *net_loft_furniture_rule(void);
int         net_loft_recorded(void);
Loft       *net_loft_working(void);
Loft       *net_loft_curves(void);
int         net_loft_close(void);
struct JBox /* JBox, declared above with the margin's API */
{
    RMesh       *m;
    const RCity *c;
    uint8_t      mask_bit;
    Family       f;
    int32_t      col, row;
    int          links;
    float        order;
    float        hw, mat, cx, cy, h, sw, lw, zj;
    float        a0[2], a1[2], b0[2], b1[2];
    int          e;
    int          records_only; /* the box reaches no chunk this build draws: its lanes' and its margin's records, no drawing (the loft's rule) */
    int          comp;         /* the width compensation the build lofts with (walk.c net_compensate), for the box's own lofts */
};

/*  One station pair of a strip, as the slab lays it.  What the family
 *  may restyle.  That is the across range, the along offset, the class,
 *  and the material and along of its markings.  And what it may build
 *  beside. */
typedef struct
{
    int           i; /* the pair's index: the quad from station i - 1 to i */
    const Sample *pv, *cu;
    float         ha, hb;                     /* the half widths at either station, compensated */
    float         a0[2], a1[2], b0[2], b1[2]; /* the quad: a right, a1/b1 left */
    int32_t       tc, tr;                     /* the tile under the pair's midpoint */
    float         order;
    float         acr, acl, aoff; /* the across range and the along offset the quad is painted with */
    float         cls;            /* the strip's class here */
    float         ma, al_a, al_b; /* the material and the along at either station */
} LoftPair;

/*  A FAMILY: how one kind of line is drawn.  How wide it is, which
 *  material it wears, which of the loft's stages it supplies and what it
 *  builds at a junction.  So no generic stage has to branch on which
 *  family it is working for.
 *
 *  None of it is a C table.  A SCRIPT declares a family,
 *  scripts/families/line.lua and its neighbors, and net/family.c builds
 *  this from the declaration.  So another way to draw a band is a file
 *  in scripts/families rather than a change here.  The knobs are
 *  pointers into the live tuning, by name.
 *
 *  Each STAGE is NAMED rather than pointed at.  A name net/family.c has
 *  registered as a primitive binds to that C function.  Any other name
 *  binds to the rule `arc.rules.<name>`, which is handed the thing the
 *  stage works on.  Ask net_family_has whether a family supplies a
 *  stage.  Call it through net_family_<stage>.  Which of the two answers
 *  is the registry's business and no call site's. */
typedef enum
{
    NH_CONTROL = 0, /* a junction's control */
    NH_RECORD,      /* what a strip records for the traffic and the passes */
    NH_FLIES,       /* the grading: 1 where the strip stands clear of the ground and notches nothing */
    NH_TAPER,       /* the stations' widths: a spur's narrowing */
    NH_PROFILE,     /* the heights along the strip, in place of the spur between nodes */
    NH_WORKS,       /* what stands under or beside the strip, before the slab: piers */
    NH_TRAFFIC,     /* where the traffic runs across the strip, as fractions of a tile */
    NH_FURNITURE,   /* the strip's furniture: a line's lamps, a thread's signals */
    NET_HOOKS
} NetHook;
extern const char *const NET_HOOK_NAME[NET_HOOKS]; /* the stage names a declaration uses (net/family.c) */

typedef struct NetFamily
{
    const char  *name;
    Family       f;         /* the tile family it answers for.  The band's is the line's */
    const float *width;     /* the strip's width across, the live knob */
    const float *rmin, *rmax; /* the fit's tightest and widest radius */
    float        ref_width; /* the width the junction outline's numbers were tuned at */
    float        mat;       /* the strip's and the box's material */
    LoftKind     loft;      /* the loft kind a segment of it is drawn as */
    int          fit_fam;   /* the tangent fit's family code */
    float        junc_lift; /* the box's order over the ground's: a thread's a hair over a line's */
    float        shelf_grade; /* the grading's ceiling on the profile's own grade, levels per tile */
    int          lips;     /* the outline has lip returns and hands trims back.  A thread's has none */
    int          spurs;     /* a spur may attach beside a junction */
    int          ends_at_buildings; /* a building tile ends a segment with a turning head */
    int          caps;      /* a dead end gets a round cap */
    int          classed;   /* segments carry a class from their tiles: lanes, lamps */
    /*  WHERE A STRIP RECORDS ITSELF for the traffic: which graph, and
     *  under which class.  A family that declares these needs no record
     *  stage of its own, the pipeline files the strip itself.  One that
     *  has more to do than file it names a stage as well.  A class of -1
     *  files the strip under its own. */
    int          graphed;      /* the family declared a graph, so the pipeline files its strips */
    int          stations;     /* ... and its lofts file their stations */
    int          meets;    /* a tile whose second piece is this family's gets a level meet */
    int          paved;        /* its junction is a paved box, laid by arc.rules.junction */
    int          crossed;      /* its strips are crossed at grade: every station measures the nearest one */
    const char  *margin;      /* ... and the rule that lays the margin each side of them */
    int          threads;       /* its junction is a set of threads, named by arc.rules.node_threads */
    const char  *props;        /* ... and the rule that stands what goes beside it */
    int          graph;        /* 0 the line network, 1 the thread */
    int          record_class; /* the class to file under, or -1 for the strip's own */
    /*  The stages, as the declaration's names resolved: the primitive,
     *  or NULL where `rule` holds a rule's name instead. */
    int (*control)(const RCity *c, int32_t col, int32_t row, int links);
    const char *ask[NET_HOOKS]; /* the rule a primitive of two halves asks for in between them */
    const char *ask_after[NET_HOOKS]; /* ... and the one its second half asks in turn */
    int (*record)(Loft *x);
    int (*record_done)(Loft *x);
    int (*flies)(const RLoft *d, float over);
    void (*taper)(Loft *x);
    int (*profile)(Loft *x);
    int (*profile_done)(Loft *x);
    int (*works)(Loft *x);
    void (*traffic)(const RLoft *d, int cls, float *lane_in, float *lane_out);
    int (*furniture)(Loft *x);
    int (*furniture_done)(Loft *x);
    const char *rule[NET_HOOKS]; /* the rule each stage answers with where no primitive does */
    /*  The lane pass: the paint its lane wires are drawn in, and what its
     *  lanes do at a dead end.  Where the lanes lie is arc.rules.lanes's,
     *  through net_lane_offsets. */
    float lane_paint;
    int   lane_ends; /* NET_LANE_ENDS_* */
    /*  How far beside its own tiles, in tiles of free ground, a line may
     *  be fitted.  One family sweeps its corners across the field,
     *  another keeps to its tiles (0). */
    int free_reach;
    /*  A junction's reach along each arm from its center, in tiles, for
     *  a family whose junction is a turnout.  The arms' strips start
     *  there and the box draws across it (thread.c node_threads).  0 for
     *  a family whose junctions hand back lip trims. */
    float turnout;
    /*  The painter's slot its strip's way draws at, NAMED.  The numbers
     *  are the scripts'.  A family says which of them it wants rather
     *  than pointing at a field. */
    const char *slot;
    /*  A raised slab: its quads carry a gore where a spur takes the
     *  outer lane, and an underside of soffit, fascias and end walls.
     *  Both are the composing script's (scripts/compose/slab.lua). */
    int slab;
} NetFamily;
enum
{
    NET_LANE_ENDS_OPEN,    /* the lanes stop */
    NET_LANE_ENDS_CAP,     /* a line: round the cap, lane for lane */
    NET_LANE_ENDS_REVERSE, /* a thread: the train reverses.  The arriving thread names the leaving one */
};

/*  A family as a script declares it, before the names are resolved.  The
 *  knobs, the loft kind, the tile family and the lane ending arrive as
 *  NAMES.  So does every stage.  A declaration names a thing the C has
 *  never heard of. */
#define NET_FAM_MAX 8
typedef struct NetFamilyDecl
{
    const char *name;
    const char *tiles;   /* the tile family it answers for: "line", "thread", "power" */
    int         answers; /* it is the family that tile family means */
    int         walk;    /* the walk visits it, at this place in the order.  -1 for a family the walk never reaches */
    const char *width, *rmin, *rmax; /* the live knobs, by name */
    float       ref_width, mat;
    const char *loft; /* "line", "thread", "slab", "spur" */
    int         fit;
    float       junc_lift, shelf_grade;
    int         lips, spurs, ends_at_buildings, caps, classed;
    int         stations; /* its lofts file their stations for the spurs to find */
    int         meets; /* a tile the family crosses another on carries a level meet */
    int         paved;     /* its junction is a paved box */
    int         crossed;   /* its stations measure the nearest level meet */
    const char *margin;   /* the rule that lays a margin each side of its strips */
    int         threads;    /* its junction is a set of threads */
    const char *props;    /* the rule that stands what goes beside a strip of it */
    const char *graph;        /* the traffic graph it files itself in: "line" or "thread" */
    int         record_class; /* the class to file under, or -1 for the strip's own */
    const char *stage[NET_HOOKS];
    float       lane_paint;
    const char *lane_ends; /* "open", "cap", "reverse" */
    int         free_reach;
    float       turnout;
    const char *slot;
    int         slab;
} NetFamilyDecl;
/*  Declare one, replacing any of the same name.  Answers 0, or -1 with
 *  the reason logged.  A name no knob, loft kind, tile family or stage
 *  answers to is a fault the script has to hear about. */
int  net_family_define(const NetFamilyDecl *d);
/*  The same reading, declaring nothing: what the LINT does.  `rule`
 *  takes 1 at each stage the declaration answered with a rule rather
 *  than a primitive.  So the lint knows which rule names a family
 *  invented and can stop calling them unknown. */
int  net_family_check(const NetFamilyDecl *d, int *rule);
void net_family_reset(void); /* before a reading of the scripts: what stands is what this reading declares */
int  net_family_count(void);
const NetFamily *net_family_at(int i);
/*  Whether a family supplies a stage at all, and the stages themselves.
 *  A stage a script answers is called through the same door as one the C
 *  answers. */
int  net_family_has(const NetFamily *fam, NetHook h);
void net_family_control_ask(const NetFamily *fam, const RCity *c, int32_t col, int32_t row, int links);
const char *net_family_stage_rule(const NetFamily *fam, NetHook h);
int  net_family_stage_primitive(const NetFamily *fam, NetHook h);
int  net_family_record_done(const NetFamily *fam, Loft *x);
int  net_family_furniture_done(const NetFamily *fam, Loft *x);
int  net_family_profile_done(const NetFamily *fam, Loft *x);
const char *net_family_stage_rule_after(const NetFamily *fam, NetHook h);
int  mesh_signal_add(RMesh *m, float x, float y, float fx, float fy, float s_along, int dir, int absolute);
void net_strip_margin_drew(int drew);
void net_strip_laps(Loft *x);
int  net_strip_margin(Loft *x);
void net_thread_cross_ask(const float **cross, int *n);
void net_thread_marks_are(const ScriptMark *mk, int n);
JuncFan *net_junction_fan(void); /* the outline the drive lays a junction's fill on (line.c) */
void           net_box_lofts_reset(void);
int            net_box_loft_add(const JBox *jb, const RLoft *d, const Piece *pc, int np, float total);
int            net_box_lofts(void);
int            net_box_loft(int i);
/*  The threads a thread junction has, for arc.rules.node_threads: which arm
 *  runs into which, and how far each is raised over the last. */
int  net_threads_ask(void);
void net_threads_info(int *col, int *row, int *links);
int  net_thread_want(int from, int to, float raise);
void net_station_reset(void);
int  net_station_record(const Loft *x);
void net_bands_reset(void);
void net_band_record(const int32_t *own, int n);
const char *net_family_props(const NetFamily *fam);
int  net_family_stations(const NetFamily *fam);
int  net_family_laps(const NetFamily *fam);
int  net_family_paved(const NetFamily *fam);
int  net_family_threads(const NetFamily *fam);
JBox *net_junction_box_now(void);
int  net_junction_composing(void);
int  net_junction_finishing(void);
int  net_box_paving_ask(void);
int  net_box_paving_done(void);
int  net_threads_done(void);
int  net_family_graphed(const NetFamily *fam);
int  net_family_record(const NetFamily *fam, Loft *x);
/*  The height a strip stands clear of the ground past, the family's own
 *  answer, settled before anything is graded (net_flies_run_*). */
float net_family_flies_over(const NetFamily *fam, const RLoft *d);
void  net_flies_runs_reset(void);
int   net_flies_runs(void);
const char *net_flies_run_at(int i, int *structure);
void net_flies_run_is(int i, float over);
void net_family_taper(const NetFamily *fam, Loft *x);
int  net_family_profile(const NetFamily *fam, Loft *x);
int  net_family_works(const NetFamily *fam, Loft *x);
void net_family_traffic(const NetFamily *fam, const RLoft *d, int cls, float *lane_in, float *lane_out);
void        net_traffic_runs_reset(void);
int         net_traffic_runs(void);
const char *net_traffic_run_at(int i, int *cls);
void        net_traffic_run_is(int i, float in, float out);
int  net_family_furniture(const NetFamily *fam, Loft *x);
/*  A stage's C primitive, registered by the module that holds it, under
 *  the name a declaration reaches it by. */
typedef void (*NetHookFn)(void);
void net_hook_add(NetHook h, const char *name, NetHookFn fn);
void net_hook_add_split(NetHook h, const char *name, NetHookFn fn, NetHookFn after, const char *ask);
void net_hook_add_split2(NetHook h, const char *name, NetHookFn fn, NetHookFn after, const char *ask, const char *ask_after);
extern const NetFamily *net_walked[NET_FAM_MAX]; /* the families the walk visits, in the order their declarations asked for */
extern int              net_n_walked;
int                     loft_furniture(Loft *x); /* the furniture pass: the family's, under the switch (net/furniture.c) */
extern const NetFamily *net_line, *net_thread, *net_band, *net_power;
const NetFamily        *net_family(Family f); /* by tile family (net/family.c) */

/* ---- net/walk.c: the walk over the map, a segment's stages, the per-segment context the stages share, the profile */
/*  Where a building pass's time goes, stage by stage (--times): the
 *  stages add to these, net/walk.c prints them. */
enum
{
    NET_PROF_WALK_FIT,
    NET_PROF_TRIMS,
    NET_PROF_LANES,
    NET_PROF_OVERLAY,
    NET_PROF_CAPS,
    NET_PROF_SAMPLE,
    NET_PROF_PROFILE,
    NET_PROF_SLAB_WORKS,
    NET_PROF_RECORD,
    NET_PROF_SLAB,
    NET_PROF_SLAB_SLAB,
    NET_PROF_JUNC_LANES,
    NET_PROF_JUNC_BOX,
    NET_PROF_STATIONS,
    NET_PROF_CACHED,
    NET_PROF_SAMPLED,
    NET_PROF_GROUND,
    NET_PROF_HW_AIR, /* build_bands (band.c): the free-air scan, the bands, the tints, the spurs, the transitions, the lane check */
    NET_PROF_BANDS,
    NET_PROF_BAND_TINT,
    NET_PROF_BAND_SPURS,
    NET_PROF_HW_TRANS,
    NET_PROF_HW_CHECK,
    NET_PROF_SPUR_LOFT, /* of the spurs: their lofts (the rest is finding, posing and routing them) */
    NET_PROF_N
};
double         prof_now(void);
void           net_prof_add(int stage, double amount);
extern V2      s_wk_pts[], s_wk_q[MAX_PTS];
extern float   s_wk_rad[], s_wk_tlim[MAX_PTS];
extern int32_t s_wk_tcol[], s_wk_trow[MAX_PTS], s_wk_marks[2 * MAX_PTS];
extern Piece   s_wk_pieces[];
int            seg_measure_arms(Seg *x);
/*  The networks in two passes, with the script's own between them: the
 *  measure fits every segment and settles the trims and the meets'
 *  paths.  The draw lays the junctions and the strips.  The level meets
 *  and the power lines fall in the gap, because a meet is built from the
 *  two paths the measure fitted. */
int            build_networks(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, uint8_t mask_bit, int comp);
int            build_networks_draw(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp);
int            build_draw_families(void);
void           build_draw_boxes_begin(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int fk);
int            build_draw_box_next(void);
int            build_junction_lanes(void);
void           build_draw_begin(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, int fk);
int            build_draw_next(void);
int            build_draw_done(void);
int            build_networks_drawn(void);
int            build_networks_controls(const RCity *c, const RAtlasLevel *l);
int            build_networks_trims(const RCity *c, const RAtlasLevel *l);
/*  Every junction the walk visits, so the script can be asked for each
 *  one's ring between the fit and the trims. */
int            build_junction_count(const RCity *c, const RAtlasLevel *l);
int            build_junction_nth(const RCity *c, const RAtlasLevel *l, int i, Family *f, int32_t *col, int32_t *row, int *links);
void           junction_rings_reset(void);
void           junction_ring_keep(const OutlineFan *o);
int            junction_ask(const RCity *c, Family f, int32_t col, int32_t row, int links, OutlineFan *o, V2 *out, uint8_t *mouth, float *trim);

/*  What stage three measured and left for the drive to have answered:
 *  each junction's control, and each mouth's stripe depth. */
void           net_control_asks_reset(void);
void           net_xwalk_asks_reset(void);
void           net_control_ask(const char *rule, int32_t col, int32_t row, int links, const int *cls, const int *traf, int busy);
int            net_control_asked(void);
const char    *net_control_ask_at(int i, int32_t *col, int32_t *row, int *links, const int **cls, const int **traf, int *busy);
void           net_control_is(int i, int ctrl);
void           net_xwalk_ask(int32_t col, int32_t row, int e, int fx, int ctrl, float want, float room, float straight, float cap);
int            net_xwalk_asked(void);
int            net_xwalk_ask_at(int i, int32_t *col, int32_t *row, int *e, int *ctrl, float *want, float *room, float *straight);
void           net_xwalk_deep(int i, float d);

/* ---- net/table.c: the segment table and the sample cache */
extern RSeg    s_segs[];
extern int32_t s_seg_at[];
uint64_t       pieces_hash(const Piece *pc, int np, float total);
int            loft_cached(Loft *x);
void           loft_keep(const Loft *x);
int            seg_store(const Seg *x);
void           seg_load(Seg *x, const RSeg *r);

/* ---- net/fit.c: the path fit */
int tf_biarc(V2 A, V2 t0, V2 B, V2 t1, V2 *c0, V2 *c1, float *dout);

/* ---- net/grade.c: the corridor grading and the graded surface */
int loft_surface(const RCity *c, uint8_t mask_bit, Sample *smp, int ns, float hw, const RLoft *d);
int corridor_tile(int32_t col, int32_t row);

/* ---- net/loft.c: the loft */
float section_height(const RCity *c, uint8_t mask_bit, V2 pos, V2 dir, float h);
int   net_record(RNet *net, const Sample *smp, int ns, float total, int cls, const RLoft *d);
void  piece_at(const Piece *p, float t, V2 *pos, V2 *dir);
void  pieces_at(const Piece *pc, int np, float t, V2 *pos, V2 *dir); /* along a chain, clamped */
int   seg_table_pieces_from(int32_t col, int32_t row, int e, Piece *out, int cap, int *np); /* a segment's fitted pieces from one end (table.c) */
int   lane_port(Family f, int col, int row, int e, int out, int k, V2 *pos, V2 *dir);      /* a junction port's pose (lane.c port_pose) */
float profile_at(const Sample *smp, int ns, float at);

/* ---- net/node.c: the junction outline and box */
void arm_heading(const Piece *pc, int np, float total, int from_end, V2 *pos, V2 *dir);

/* ---- line.c: the line family */
float *mesh_tune(void);
int    seg_class(Seg *x);
void   seg_class_counts(const Seg *x, int cnt[3]);
int    net_seg_class_of(int32_t col, int32_t row, int e);
int    build_networks_classes(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp);
int    net_fits(void);
int    net_fit_begin(const RCity *c, int i);
int    net_fit_done(int i);
const char *net_fit_choice(const int **free_, const int **held);
void   net_fit_choice_is(int keep_free);
int    net_seg_fit_of(Family f, int32_t col, int32_t row, int e, V2 *q, float *rad, float *tlim, int cap);
/*  And the pieces the drive cut from that path. */
int    net_seg_pieces_of(Family f, int32_t col, int32_t row, int e, Piece *out, int cap, int *count);
/*  One segment's reading, for the rule that says what it carries: how
 *  many of its tiles wear each class, and the tiles themselves.  So a
 *  rule may go and read the density and the neighborhood at them. */
int    net_seg_class_at(int i, int cnt[3], const int32_t **cells, int *nt);
void   net_seg_class_is(int i, int cls);

/* ---- thread.c: the thread family */
int seg_measure_laps(Seg *x);
int on_lap_panel(const RCity *c, int32_t tc, int32_t tr, float x, float y);

/* ---- band.c: the band family */
int put_fascia(RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float ea[2], const float eb[2], float za, float zb, const float nrm[3], float girder, int parapet);

/* ---- mesh.c: the build's clock, read by the stages' timing lines */
extern int s_pass;
double     tms(void);
void       tnote(const char *what, double t0);
int        mesh_build(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines);
int        mesh_tile_corners(const RCity *c, int32_t col, int32_t row, int underground, float z[4]);
int        mesh_query(const RCity *c, int32_t col, int32_t row, char *buf, size_t n);

#endif
