/*  pipeline.h: the vocabulary the renderer's three halves share.
 *
 *  This header is no one directory's own.  What is left in it is the
 *  small set of things all three name.  A point, a piece, a sample, a
 *  family slot, and what a loft is asked to draw.  With them go the
 *  declarations mesh/ and walk/ answer.
 *
 *  net/ answers none of them.  Its stores and its plumbing are declared
 *  in net/net.h, which includes this file for the vocabulary above.  So
 *  a reader who wants to know what the network offers reads one header
 *  in the directory that answers it.
 *
 *  Each directory holds one kind of thing.
 *
 *      mesh/   the shape primitives
 *      net/    the stores, and the plumbing that turns a Lua
 *              declaration into a family
 *      walk/   a segment's stages
 *
 *  A declaration here belongs to whichever of the three defines it.
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
 *  The piece tables (walk/cell.c) say what a cell carries.  The shape
 *  primitives (mesh/shapes.c) draw over the mesh's triangle emitter. */
#ifndef R_NET_INT_H
#define R_NET_INT_H

#include "geo.h"
#include "mesh/internal.h"
#include "script.h"
/* ---- the types, knobs and state the pipeline shares --------------------- */

extern const uint8_t *s_check_xbld;               /* piece.c: the last built city's XBLD, for the piece scan */
/* the line art's anchors per edge, read by the piece tables and the meets */
extern const float SIDE_MU[4];
extern const float SIDE_MV[4];
extern const float SIDE_DU[4];
extern const float SIDE_DV[4];

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
float ease_smooth(float f); /* a smoothstep over 0..1, level at both ends */
#define BAND_PARAPET 0.045f    /* a barrier, not a wall: 0.35 m over the slab */
#define BAND_CAP     0.05f
#define BAND_CAP_D   0.07f
#define BAND_COL     0.09f

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
#define TRAIN_LEN   0.42f
#define TRAIN_WID   0.10f
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
int   piece_family(uint8_t b, Family *f);
/*  What line a building byte carries, as arc.rules.line_tiles reads the
 *  city: on the tile itself, or under a slab standing on it. */
/*  A carrier: a tile a line runs on into rather than stopping at, as
 *  arc.rules.carrier_tiles names it. */
int net_carrier(uint8_t b);
int net_line_lapped(uint8_t b);
int net_thread_lap(uint8_t b);  /* a lap the second family is part of, on either axis (walk/cell.c) */
int net_line_on(uint8_t b);
int net_line_near(uint8_t b);

int   piece_second(uint8_t b, Family *f);
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
int   put_tri_ground_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float col[3], const float ref[3], const float ref2[3]); /* ... laid on the drawn surface */
int   strip_quad_z_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float a0[2], const float a1[2], const float b0[2], const float b1[2], float za, float zb, float across0, float across1, float along_a, float along_b, float mat);
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

/*  The path in stages (mesh/fit.c): the corridor's gates, the taut
 *  string through them, and the radius each corner may sweep. */
void net_serve_tiles(const int32_t *tcol, const int32_t *trow, int nt); /* a drawn segment's tiles (walk.c) */
int  net_tile_served(int32_t i);                                        /* for the check: a tile with a line, wherever it runs */
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
typedef struct GroundFan GroundFan;
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
/*  Sliding a spur's join along the slab and along the line.  It routes
 *  one placing, says whether it leaves by the spur tile's line edge, and
 *  keeps the placing. */
/*  A strip's elevation over the ground: its stations, the altitude an
 *  end or a level meet pins it to, and the height each is given. */
int   loft_ground_at(const GroundFan *g, int i, float *at, float *z);
int   loft_ground_node(const GroundFan *g, int which, float *z);
int   loft_ground_lap(const GroundFan *g, int i, float *z);
void  loft_ground_set(GroundFan *g, int i, float z);
void fit_tally_get(int bucket, void *dst, size_t cap); /* the fit's tallies, around a fit that may be discarded */
void fit_tally_set(int bucket, const void *src, size_t cap);
int  path_fit_points_begin(const uint8_t *mark, const uint8_t *own, const V2 *pts, int n, float hw, V2 start, V2 goal, float rmax, float rmin, float gro, int32_t ex0, int32_t ex1, int free_lines, V2 *out, float *rad, float *tlim, int cap);
int  path_fit_points_end(void);
void path_fit_prims(void);
void fit_tally_into(int bucket); /* which tally bucket the next fit counts into */
/*  What the tangent fit did over a build, for --mesh-check. */
void fit_stats(void);
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
#define loft(...) loft_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
int line_meet(V2 a, V2 da, V2 b, V2 db, V2 *out);
int build_island(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, int32_t col, int32_t row);
extern float s_trim[2][R_MAP * R_MAP * 4];
extern float s_xwalk[2][R_MAP * R_MAP * 4]; /* the meet band each arm gave up line for, tiles.  0 for none */

float         node_altitude(const RCity *c, int32_t col, int32_t row);
#include "walk/walkway.h" /* the margin network: where the margins run and what they join */

/*  The line surface's STACK, as fractions of a tile's painter's slot.
 *  Everything laid on the line lies at one height on the graded ground.
 *  So the slot is the only thing that orders it.  Two pieces given the
 *  same slot are ordered by nothing at all.  Whichever the build drew
 *  last wins the pixel, which is neither stable nor meant.  So each
 *  piece of the surface names its own place here, lowest first. */
/*  Where a name sits in that listing, and the value there.  A model
 *  holds the index rather than a place in the struct.  So a number a
 *  script made is named the same way the pipeline's own are. */

/*  Outline points: four mouths' two corners each, and four returns of
 *  one segment more than the smoothness asks for.  A bound, not a knob:
 *  geo_set holds junc_arc to what this leaves room for. */
#define JUNC_MAX 96
#define FAMX(x) ((x) == net_thread->f ? 1 : 0)

int net_compensate(void); /* the width compensation this build lofts with (walk.c) */
int put_prism_clip_m(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float dx, float dy, float len, float wid, float zb, float zf, float z0, float z1, float paint, float mat);
int  net_spur_sides(const RCity *c);
int  net_spur_side_at(int i, int *free_side, int *room, int *back);
void net_spur_side_is(int i, int keep);

void net_prof_reset(void);
/*  The arms at the junction the car in hand is about to reach, for
 *  arc.rules.car_turn.  How many, the draw the world made, each one's
 *  heading away from the node, and the one the rule chose. */
/*  The signal at the junction the car in hand faces, for arc.rules.signal. */
/*  A junction's stagger, and what a script makes of it.  It gives which
 *  phase of the cycle it starts at, and which group of arms an edge
 *  belongs to. */
/*  The thread signals, for arc.rules.thread_signal: how near the nearest car
 *  is each way along the block, and the aspect the rule answered with. */
void net_prof_print(void);
int  grade_only(int allowed); /* mesh.c: the grading pass, skipping what only the drawing needs */
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
/*  How a spur meets the meet beside it, the rule's answer for the
 *  reading in hand.  And the drive settling all sixty-four readings. */
/*  A junction's fill as the composing script is handed it: the polygon
 *  the fan is laid over.  The outline the arms cut out, inset by the
 *  margin's band where there is one.  And where its middle sits.  The
 *  outline itself is the pipeline's: arms, corners, trims and lip
 *  returns are a solver's work.  What is drawn over it is not. */

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

/*  And the drive's own walk over the junctions stage three measures. */
int   net_trim_junctions(const RCity *c, const RAtlasLevel *l);
void *net_trim_band(int i);
int   net_trim_mouths(void);
void  net_trim_done(int i);
/*  The band one arm gave up line for, 0 for none (walk.c s_xwalk). */
float net_cross_depth(Family f, int32_t col, int32_t row, int e);

/*  A STATION: one cross-section of a strip or a slab.  It holds where
 *  the section is, which way it faces, how far along it sits and how
 *  high it stands.  The loft takes one every fraction of a tile, as the
 *  family's step_run and step_arc ask.  Everything the loft carries is
 *  placed by reading stations rather than cells. */
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

/*  A shelf's corner over the drawn surface (mesh/surface.c). */
float surface_corner_height(const Sample *smp, int ns, int i, float dx, float dy, float shelf_grade, Family f);

/*  The street furniture pass (furniture.c) and the line-marking pass
 *  (marking.c): each switched as one. */
void          furniture_enable(int on);
int           furniture_on(void);
#define LANE_CLS_LINE 0
#define LANE_CLS_TURN 1 /* a junction's connector: the near lane inside a meet piece's box */
#define LANE_CLS_SLAB 2
#define LANE_CLS_LINK 4 /* a band's lane on to the next band's, or into its own inner lane */

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
/*  A band's cells as the composing script picks the points the fit
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

struct PieceFan
{
    const V2    *q;
    const float *rad, *tlim;
    int          n;
    /*  A chain queued as its two POSES and no points: the script builds
     *  the points between them (arc.rules.chain) before it cuts. */
    int          posed;
    V2           A, tA, B, tB;
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

/*  THE STRIP IN FLIGHT and the one whose slab is still to be laid.  The
 *  loft (mesh/loft.c) fills them, the drive (net/strip.c) stops at each
 *  of its stages over them. */
extern RLoft   s_ldv;
extern Loft    s_lx;
extern int     s_lx_live;
extern const RLoft *s_ld;              /* the strip being lofted, for both halves */
extern float   s_zorig[];              /* the ground's own line under each station */
extern double  s_lx_tp;                /* when the stage in hand started, for the profile */
void note_add(char *buf, size_t cap, size_t *n, const char *fmt, ...);

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

enum
{
    NET_LANE_ENDS_OPEN,    /* the lanes stop */
    NET_LANE_ENDS_CAP,     /* a line: round the cap, lane for lane */
    NET_LANE_ENDS_REVERSE, /* a thread: the train reverses.  The arriving thread names the leaving one */
};

void net_strip_margin_drew(int drew);
int  net_strip_margin(Loft *x);
void net_thread_cross_ask(const float **cross, int *n);
void net_thread_marks_are(const ScriptMark *mk, int n);
/*  A stage's C primitive, registered by the module that holds it, under
 *  the name a declaration reaches it by. */
typedef void (*NetHookFn)(void);

/* ---- walk/walk.c: the walk over the map, a segment's stages, the per-segment context the stages share, the profile */
double         prof_now(void);
void           net_prof_add(int stage, double amount);
extern V2      s_wk_pts[], s_wk_q[MAX_PTS];
extern float   s_wk_rad[], s_wk_tlim[MAX_PTS];
extern int32_t s_wk_tcol[], s_wk_trow[MAX_PTS], s_wk_marks[2 * MAX_PTS];
extern Piece   s_wk_pieces[];
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

/* ---- mesh/fit.c: the path fit */
int tf_biarc(V2 A, V2 t0, V2 B, V2 t1, V2 *c0, V2 *c1, float *dout);

/* ---- mesh/loft.c: the loft */
float section_height(const RCity *c, uint8_t mask_bit, V2 pos, V2 dir, float h);
int   net_record(RNet *net, const Sample *smp, int ns, float total, int cls, const RLoft *d);
void  piece_at(const Piece *p, float t, V2 *pos, V2 *dir);
void  pieces_at(const Piece *pc, int np, float t, V2 *pos, V2 *dir); /* along a chain, clamped */
float profile_at(const Sample *smp, int ns, float at);

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

/* ---- band.c: the band family */
int put_fascia(RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float ea[2], const float eb[2], float za, float zb, const float nrm[3], float girder, int parapet);

/* ---- mesh.c: the build's clock, read by the stages' timing lines */
double     tms(void);
void       tnote(const char *what, double t0);
int        mesh_tile_corners(const RCity *c, int32_t col, int32_t row, int underground, float z[4]);
int        mesh_query(const RCity *c, int32_t col, int32_t row, char *buf, size_t n);

#endif
