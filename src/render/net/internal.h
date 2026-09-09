/*  The network pipeline, inside the renderer: what its modules share.  The
 *  generic stages, one file each, in the order they run: net/walk.c the
 *  pass over the map (build_networks) and the walk from node to node:
 *  walk_segment over seg_walk, seg_class (road.c), seg_fit,
 *  seg_measure_arms, seg_measure_crossings (rail.c), seg_trim, seg_overlay,
 *  seg_caps -- one segment, tile walk to cap; the context a segment sets
 *  for the stages after it; the timing profile net/table.c the segment
 *  table the grading pass fills and the building pass replays, and the
 *  loft's station cache net/fit.c the path fit: a segment's tiles become
 *  pieces -- runs, arcs, biarcs -- inside the corridor the family allows
 *  (tangent_fit, path_fit, the fillets) net/grade.c the corridor field:
 *  what a strip asks of the ground under it, and the graded surface every
 *  later stage reads net/loft.c the loft: loft over loft_sample (or the
 *  table's cache), loft_ground, loft_taper and loft_deck_works (hiway.c),
 *  loft_profile, loft_record, loft_slab -- one strip along some pieces,
 *  described by an RLoft net/junction.c junction_poly over jp_arms,
 *  jp_sort, jp_corners, jp_trims, jp_curbs, jp_hull -- a junction's outline
 *  and its arms' trims; build_junction, the box, drawn by the family The
 *  families that answer them: road.c, rail.c, hiway.c (bands, free air, the
 *  deck's works and lane drop, build_ramps over the ramp_* stages),
 *  power.c.  The piece tables (piece.c) say what a tile carries; the shape
 *  primitives (mesh/shapes.c) draw over the mesh's triangle emitter.  The
 *  passes over the finished network -- lanes, sidewalks, markings,
 *  furniture -- are declared here too.  It runs in this order: stage
 *  one, the corridor a segment occupies and the gate on each shared edge;
 *  stage two, the path through them; stage three, the junctions, each
 *  taking its shape from the arms that reach it and handing them back where
 *  to start; then the loft, the cross-section swept along the pieces with
 *  the profile ramped between node altitudes, the records for the traffic
 *  and the passes, and the corridor's own shelf. */
#ifndef R_NET_INT_H
#define R_NET_INT_H

#include "mesh/internal.h"
#include "script.h"
/* ---- the types, knobs and state the pipeline shares --------------------- */

/*  The context walk_segment sets for the segment it is drawing, read by
 *  the loft, the junction box and the passes (net/walk.c owns them). */
extern uint8_t        s_junc_ctrl[R_MAP * R_MAP]; /* net/junction.c: per junction tile, two bits per arm: 0 none, 1 stop, 2 signal */
extern const uint8_t *s_check_xbld;               /* piece.c: the last built city's XBLD, for the piece scan */
/* the road art's anchors per edge, read by the piece tables and the crossings */
extern const float ROAD_MU[4];
extern const float ROAD_MV[4];
extern const float ROAD_DU[4];
extern const float ROAD_DV[4];
/*  The corridor field (net/grade.c): per grid corner, what the strips
 *  passing over it ask of the ground. */
extern float   s_zcap[GRID * GRID];
extern uint8_t s_corr[GRID * GRID];
extern float   s_zdist[GRID * GRID];
/*  The lowest road that passes over each corridor corner.  The shelf is
 *  shaped by the nearest one and smoothed across the field, and this is
 *  the ceiling that smoothing may not break: ground that rises above the
 *  band it carries is ground the road is buried in. */
extern float s_zlow[GRID * GRID];
/*  A corridor tile's OWN four corner heights, flat across the band and
 *  graded along it.  The corridor is not bound by the height field's rule
 *  that neighbouring tiles share a corner: a road and a railway running
 *  side by side at different heights are two shelves with a wall between
 *  them, not one warped quad. 1e9 where a tile has none. */
extern float s_tilez[R_MAP * R_MAP * 4];
void         s_tile_reset(int32_t i);
void         shelf_node(int32_t col, int32_t row); /* a tile where edges meet: they share one level there (grade.c) */
void         shelf_steps(int *n, float *worst); /* corridor tiles disagreeing at a shared corner (grade.c) */

/*  The structure's proportions, measured off the original's own
 *  rendering of Four Cities' viaduct rather than the specification: the
 *  art draws a slim ribbon on thin columns, its edge a dark shadow a
 *  quarter of a level deep, no parapet standing over the carriageway,
 *  and a column about a metre across every two tiles.  Built to the
 *  specification's concrete sections it read as a viaduct of walls. */
#define HIWAY_LIFT      1.0f
#define HIWAY_UNDERSIDE 0 /* the deck's soffit, fascias and end walls: off until they are a pass of their own */
/*  How far inside the deck's edge the fit samples the corridor, in
 *  tiles.  A road samples a hair inside (0.002); a deck two tiles wide
 *  has its edges on tile boundaries, and the S at Toronto 113,42 -- two
 *  R3 arcs -- overhangs the warehouse beside its exit by 0.04 of a tile
 *  at the very end: the parapet's width, not a lane. */
/*  Stations a loft may hold.  8190 was a whole ring highway's worth and
 *  Babar's ring ran out of it on its last stretch, leaving six ramps
 *  with no deck beside them; the count moves with the deck's width
 *  through the crossings sampled at the tile edges. */
#define LOFT_MAX_ST      32768
#define RAMP_HW          (0.15f * s_tune.hiway_w) /* a ramp lane: the deck's outer lane, 0.70 to 1.0 across, on its own */
#define HIWAY_LANE_IN    0.70f  /* the outer lane's inner edge, across the band */
#define HIWAY_LANE_TAPER 4      /* tiles: one gore, then the descent */
float hiway_lane_ease(float f); /* the descent's profile, 0 at the road to 1 at the gore */
#define HIWAY_GIRDER  0.11f     /* the deck's edge: 0.9 m of slab and girder */
#define HIWAY_PARAPET 0.045f    /* a barrier, not a wall: 0.35 m over the deck */
#define HIWAY_BENT    1.0f
#define HIWAY_CAP     0.05f
#define HIWAY_CAP_D   0.07f
#define HIWAY_COL     0.09f
/*  The knobs the look is tuned with, live.  They were constants; the
 *  judgement they encode is aesthetic and belongs to the person looking at
 *  the city, not to a number I picked.  The macros still stand so every use
 *  site reads the current value, and the UI's Road tuning window writes
 *  them and rebuilds the mesh. */
typedef struct
{
    float road_w;    /* the carriageway, across, in tiles                 */
    float rail_w;    /* a double track's right of way                     */
    float road_rmin; /* the tightest curve each may be drawn with         */
    float rail_rmin;
    float road_rmax; /* and the widest sweep to look for                  */
    float rail_rmax;
    float approach;    /* straight run reserved at every node               */
    float margin;      /* how far inside its corridor the band is held      */
    float trim_cap;    /* how far out a junction may cut its arms back      */
    float show_curves; /* draw the fitted centreline over the world       */
    /*  The highway's, live too.  The deck's half width across, in tiles,
     *  and what sits on it follows; the radii its corners may take; how far
     *  an on-ramp reaches for the deck; how many straight cells may lie
     *  between two curve blocks for them to be one staircase; and the share
     *  of an edge a corner may take for its fillet, the half rule at 0.5 --
     *  for every family, since the fit is one. */
    float hiway_w;
    float hiway_rmin, hiway_rmax;
    float hiway_reach;
    float hiway_stair;
    float corner_share;
    float ramp_merge; /* how far along the road an on-ramp's join may slide from the point it aimed at, tiles */
    float hiway_grade; /* the deck's steepest rise or fall, levels per tile */
    float hiway_stiff; /* the deck's stiffness: the window, tiles, over which it holds level across dips and rounds its crests */
} RRoadTune;
extern RRoadTune s_tune;

#define ROAD_W    (s_tune.road_w)
#define RAIL_W    (s_tune.rail_w)
#define ROAD_RMIN (s_tune.road_rmin)
#define RAIL_RMIN (s_tune.rail_rmin)

#define ROAD_GRADE 1.0f /* the profile's steepest rise, levels per tile of road */
/*  The tightest curve each family may be drawn with, in tiles of radius.
 *  It is a constraint and not a preference, and it outranks the corridor: a
 *  road can be laid round a tighter corner than a railway and a railway
 *  cannot, whatever room the tiles leave.  Where the two disagree the curve
 *  wins and the band may overhang a neighbouring tile, which is what a real
 *  alignment does.  A staircase of rail tiles is therefore ONE straight
 *  diagonal and not a weave. */

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
        if (put_tri_road_n(m, c, mask_bit, order + 0.05f, (const float (*)[3])t3, (N), col, ref, ref2) != 0) \
            return -1;                                                                                       \
    } while (0)
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

/*  A ramp (spec 7.3, the lane drop), as the scan before the band walks
 *  lists it: the ramp tile, the point on the deck's centreline beside
 *  it, the lane's direction of travel (the viewer's right hand) and the
 *  side the ramp lies on is read off the stations; `off` says the taper
 *  runs upstream (the lane comes down to the road) and `len` how many
 *  tiles of it there are, counted through curve blocks.  The loft
 *  narrows the deck by arc length from that point, records its
 *  stations, and the ramp is built from them afterwards. */
typedef struct
{
    int32_t rc, rr; /* the ramp tile */
    V2      c0;     /* on the deck's centreline beside it */
    V2      along;  /* the lane's direction of travel */
    V2      toward; /* from the ramp tile toward the deck */
    int     off;    /* the taper runs upstream: the lane comes down to the road */
    int     len;    /* the taper's tiles, 0 for none */
    int     opp;    /* the road lies opposite the deck: the strip crosses the ramp tile */
} HwRamp;
#define HW_MAX_RAMPS 2048
extern HwRamp s_hw_ramps[HW_MAX_RAMPS];
extern int    s_hw_nramps;
/*  A deck's stations, every band's, recorded by the loft for the ramps. */
typedef struct
{
    V2    pos, dir;
    float s, z;
    int   band;
} HwSt;
#define HW_MAX_ST 120000
extern HwSt s_hw_st[HW_MAX_ST];
extern int  s_hw_nst, s_hw_band;

/*  Vector helpers small enough to share: both the road algorithm and the
 *  highway path want them, and a copy in each is a copy to get wrong. */
static inline float v2len(V2 v)
{
    return sqrtf(v.x * v.x + v.y * v.y);
}

static inline float v2cross(V2 a, V2 b)
{
    return a.x * b.y - a.y * b.x;
}
int   put_wall(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3]);
float road_class(const RCity *c, int32_t col, int32_t row);
int   piece_family(uint8_t b, Family *f);
/*  What road a building byte carries, as arc.rules.road_tiles reads the
 *  city: on the tile itself, or under a deck standing on it. */
/*  A building that stands up, as arc.rules.standing_tiles names it, and
 *  a carrier a road runs on into, as arc.rules.carrier_tiles does. */
int net_stands_up(uint8_t b);
int net_carrier(uint8_t b);
int net_road_over_rail(uint8_t b);
int net_rail_crossing(uint8_t b);  /* a crossing a railway is part of, on either axis (net/piece.c) */
int net_road_on(uint8_t b);
int net_road_near(uint8_t b);
/*  The highway tiles, as arc.rules.hiway_tiles names them. */
int net_hiway_onramp(uint8_t b);
int net_hiway_curve(uint8_t b);
int net_hiway_interchange(uint8_t b);
int net_hiway_any(uint8_t b);
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
/*  The road and rail emitters take, invisibly, the name of the function
 *  that called them: the inspector reports who drew each material on a
 *  tile, and the innermost emitter's own name says nothing useful. */
int   put_tri_road_n_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float *nrm, const float col[3], const float ref[3], const float ref2[3]);
#define put_tri_road_n(...) put_tri_road_n_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
int   put_tri_ground_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float *nrm, const float col[3], const float ref[3], const float ref2[3]); /* ... laid on the drawn surface */
#define put_tri_ground(...) put_tri_ground_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
int   strip_quad_z_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float a0[2], const float a1[2], const float b0[2], const float b1[2], float za, float zb, float across0, float across1, float along_a, float along_b, float mat);
#define strip_quad_z(...) strip_quad_z_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
int   strip_quad_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float a0[2], const float a1[2], const float b0[2], const float b1[2], float across0, float across1, float along_a, float along_b, float mat);
#define strip_quad(...) strip_quad_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
int   strip_fan_z(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float t0, float t1, float r0, float r1, float across_c, float mat, int n, float lift);
/*  A tile's outline on the ground, for the curve overlay: four thin
 *  bars in a vehicle paint, the tile's own ground and sprites left
 *  visible inside them. */
int tile_highlight(RMesh *m, const RCity *c, uint8_t mask_bit, int32_t col, int32_t row, float paint);
int put_box(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float w, float d, float z0, float z1, float mat, float phase);
int put_cyl(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float r, float z0, float z1, float mat);
int curves_hidden(float mat); /* show curves: the network materials it hides */
int put_wire(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x0, float y0, float z0, float x1, float y1, float z1, float sag);
int put_wire_paint(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x0, float y0, float z0, float x1, float y1, float z1, float sag, float paint);
int put_signal(RMesh *m, const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float order, int e, float h);
int put_bar(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x0, float y0, float z0, float x1, float y1, float z1, float fx, float fy, float w, float d, float mat, float code, float phase);
int put_lamp_face(RMesh *m, float order, float x, float y, float g, float z, float fx, float fy, float sz, float phase, float code);
/*  A LEVEL CROSSING, in three: the ask gathers it and opens its shape,
 *  the script measures it, and the draw lays the panel and the
 *  approaches.  Every measurement follows from the angle the road and
 *  the line cross at, which is why the script sits between the two. */
int  build_crossing(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int32_t col, int32_t row, int second);
void net_crossing_ask(int32_t *col, int32_t *row, float *sine, float *road, float *rail);
void net_crossing_frame(const ScriptXing *fr);
int put_gate(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy, int along_u);
int put_rail_signal(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy, int absolute, float s_along, int dir);
int put_second_train_sign(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy);
int fillet(const V2 *q, int n, float rmax, Piece *out, int *count);
/*  The path in stages (net/fit.c): the corridor's gates, the taut
 *  string through them, and the radius each corner may sweep. */
void net_serve_tiles(const int32_t *tcol, const int32_t *trow, int nt); /* a drawn segment's tiles (walk.c) */
int  net_tile_served(int32_t i);                                        /* for the check: a tile with a line, wherever it runs */
float arm_cut(Family f, int col, int row, int e); /* how far from its mouth an arm's strip is cut, as the segment is cut (lane.c) */
int  path_fit_begin(const RCity *c, const int32_t *tcol, const int32_t *trow, int nt, float hw, V2 start, V2 goal, float rmax, float rmin, float gro, float reserve, int32_t ex0, int32_t ex1, int free_reach, V2 *out, float *rad, float *tlim, int cap);
int  path_fit_end(void);
int fillet_r(const V2 *q, int n, const float *rad, Piece *out, int *count);
/*  The same with a tangent budget per vertex, which is what the tangent
 *  fit hands over; fillet_r is this with half of each edge. */
/*  One boundary of the tangent fit, as the scripts read it: whether the
 *  two lines meet, whether either was let leave its own cells, and how
 *  far ahead the far line's next crossing and its own end lie. */
typedef struct
{
    int   has_after, met, free, cross, free_join;
    float ahead, reach;
} PathPair;

void *path_handle(void);
int  path_lined(void);
int  path_pairs(void);
int  path_pair(int k, PathPair *out);
void path_after_is(int crossing);
const char *path_try(const char *how, void **obj);
int  path_held(void);
int  path_finish(void);
int  fillet_t(const V2 *q, int n, const float *rad, const float *tlim, Piece *out, int *count);
void tlim_half(const V2 *q, int n, float *tlim);
/*  The same fit on a chain of points with a corridor of the caller's
 *  choosing, for the highway walk; and its runs, for the caller's dump. */
void path_fit_probes(void);
/*  The fit's own arithmetic, which the composition asks for by name: the
 *  corridor sweep at a corner, a corner's demand for tangent, what an
 *  end may spare, and the tally a finished corner falls in. */
float path_fit_demand(V2 a, V2 b, V2 c);
float path_fit_need(V2 a, V2 b, V2 c, float rmin);
void  path_fit_count(const char *what);
/*  The run cut's primitives, as the composing script asks for them: the
 *  step comparison, the candidate a span would be and whether it stands,
 *  the winner of a prefix, and the runs named on the way back. */
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
/*  The arc at a crossing, as the composing script decides it: whether a
 *  leg's extension to the crossing holds, whether the gap it spans stays
 *  covered, what radius the corridor allows there, whether the straights
 *  that reach the arc hold, and the vertex placed. */
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
/*  The join walked point by point: whether an end lies in line with the
 *  gap beside it, whether the gap is a sideways step, the diagonal that
 *  step can be drawn as, and the points placed. */
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
 *  corner, a fillet swept into one, and the run out to the far end. */
int   path_piece_corner(PieceFan *p, int i);
void  path_piece_straight(PieceFan *p, int i);
void  path_piece_arc(PieceFan *p, int i, float r);
void  path_piece_tail(PieceFan *p);
/*  A highway band's chain of fit points: which of its cells turn, which
 *  are pinned by a ramp, and the points the chain is given. */
int   hiway_stair_turn(const StairFan *s, int i);
int   hiway_stair_pinned(const StairFan *s, int i);
void  hiway_stair_point(StairFan *s, int i);
void  hiway_stair_centre(StairFan *s, int i, int j);
/*  A highway strip's elevation, station by station: what the script
 *  reads and writes, and the easing curve a lane drop follows. */
void  hiway_prof_at(const ProfFan *p, int i, float *s_at, float *z, float *ground);
void  hiway_prof_set(ProfFan *p, int i, float z);
/*  Sliding a ramp's join along the deck and along the road: one placing
 *  routed, whether it leaves by the ramp tile's road edge, and the
 *  placing kept. */
const char *hiway_slide_route(SlideFan *s, float u, float at, float *r);
int         hiway_slide_exits(SlideFan *s);
void        hiway_slide_keep(SlideFan *s, float taper);
void        hiway_slide_note(const SlideFan *s, int tried, int off, int unroutable, int missed);
/*  The lane a ramp drops from a deck: the stations and the ramps, and
 *  the width each station is left with. */
int   hiway_drop_station(const DropFan *d, int i, float *at, V2 *pos, V2 *dir);
int   hiway_drop_ramp(const DropFan *d, int r, V2 *c0, V2 *tile, V2 *along, int *len, int *off);
void  hiway_drop_clear(DropFan *d);
void  hiway_drop_width(DropFan *d, int i, int side, float w);
void  hiway_drop_gore(DropFan *d, int i, int side);
/*  A strip's elevation over the ground: its stations, the altitude an
 *  end or a level crossing pins it to, and the height each is given. */
int   loft_ground_at(const GroundFan *g, int i, float *at, float *z);
int   loft_ground_node(const GroundFan *g, int which, float *z);
int   loft_ground_crossing(const GroundFan *g, int i, float *z);
void  loft_ground_set(GroundFan *g, int i, float z);
/*  How an on-ramp reads the four sides around it: what each neighbour
 *  is, and the sides it settles on. */
int   hiway_orient_side(const OrientFan *o, int k, int *deck, int *axis, int *road);
void  hiway_orient_answer(OrientFan *o, int kind, int dside, int rside, int eside, int off, int roads);
/*  Reconciling the shelf: the copies of one corner, the copies round a
 *  node's tile, and the level they are all given. */
int   shelf_ask(ShelfFan *s); /* the corridor corners a shelf rule reconciles (grade.c) */
int   shelf_copies(const ShelfFan *s, int gx, int gy, int *owner, float *dist, float *z, int max);
void  shelf_set(ShelfFan *s, int gx, int gy, int owner, float z);
int   shelf_node_at(const ShelfFan *s, int i, int32_t *col, int32_t *row);
int   shelf_node_heights(const ShelfFan *s, int32_t col, int32_t row, float *z, int max);
void  shelf_node_set(ShelfFan *s, int32_t col, int32_t row, float z);
/*  A lane's open end carried on into the facing lane across a crossing:
 *  the ends that could take one, how a candidate lies, and the join. */
int   xlane_end(const XLaneFan *x, int i);
int   xlane_measure(const XLaneFan *x, int la, int lb, float *off, float *dot, float *ahead, float *aside, float *dist);
void  xlane_merge(XLaneFan *x, int la, int lb);
int   xlane_link(XLaneFan *x, int la, int lb);
void path_fit_tally_get(int fam, void *dst, size_t cap); /* the fit's tallies, around a fit that may be discarded */
void path_fit_tally_set(int fam, const void *src, size_t cap);
int  path_fit_points_begin(const uint8_t *mark, const uint8_t *own, const V2 *pts, int n, float hw, V2 start, V2 goal, float rmax, float rmin, float gro, int32_t ex0, int32_t ex1, int free_lines, V2 *out, float *rad, float *tlim, int cap);
int  path_fit_points_end(void);
void path_fit_prims(void);
void fit_family(int fam); /* the tangent fit's family code, set before a fit: a family's fit_fam */
int  path_biarc(V2 A, V2 t0, V2 B, V2 t1, V2 *c0, V2 *c1, float *d);
/*  What the tangent fit did over a build, for --mesh-check. */
void fit_stats(void);
void ramp_stats(void);
void fit_stats_reset(void);
/*  What a loft is asked to draw: one strip along some pieces, described
 *  outright.  A deck is a deck because the description says so: nothing
 *  is steered by globals set around the call, so a reader of the loft can
 *  see what it will draw from this struct alone. */
typedef enum
{
    LOFT_ROAD = 0, /* a road strip: the class's markings, sidewalks, furniture */
    LOFT_RAIL,     /* a railway: two rails on ties */
    LOFT_DECK,     /* a freeway deck on its columns, three lanes each way */
    LOFT_RAMP      /* a ramp: one lane from a deck down to a road */
} LoftKind;
struct NetFamily;
typedef struct
{
    Family                  f;       /* the family whose width and material apply */
    const struct NetFamily *fam;     /* the family answering for this strip: its material, its record, its markings */
    float                   hw, mat; /* the strip's half width and material, the family's or a deck's */
    LoftKind                kind;
    int                     struct_;       /* a ramp: concrete from the road to the deck, it grades nothing */
    int                     flat;          /* lying on the ground: no lift, no soffit */
    int                     lane_piece;    /* one lane wide, drawn as the deck's outer lane */
    int                     lane_off;      /* an OFF ramp's lane: the descent ends at its far (road) end */
    float                   z0;            /* how far above the ground the turn-out's deck end sits */
    float                   ramp0, ramp1;  /* the lift's taper at either end, tiles */
    float                   xw0, xw1;      /* the crossing band each end's junction lays over this strip's, tiles; 0 for none */
    int                     pin0, pin1;    /* the ends pinned: a junction end, or a deck's */
    int                     band;          /* the deck band the stations are recorded under */
    float                   cls;           /* a road's class, or -1 to read it off the tiles */
    float                   taper;         /* a ramp: the length over which the strip narrows to hw_end, 0 for none */
    float                   hw_end;        /* ... to this half width, the road lane's */
    int                     taper_start;   /* ... at the strip's start (an ON ramp's road end) rather than its end */
    float                   ground_margin; /* how far past its own edges the strip reads the ground: a deck a hair */
    float                   raise;         /* a hair over the strip's usual seat: a turnout's through track over its wye's curves, the second curve under the first */
    /*  The segment the strip belongs to, for the traffic's record, the
     *  profile's end heights and the table's station cache; a deck or a
     *  ramp leaves them zero. */
    int32_t  node[2][2];   /* the node tiles at either end */
    int      arm[2];       /* the arm each end leaves its node by, 0..3; -1 for an end that leaves by none */
    int      nkind[2];     /* each end's node kind: 0 open, 1 a dead end, 2 a junction */
    int      ctrl[2];      /* the control at each end's arm: 0 none, 1 stop, 2 signal */
    int      cache;        /* 1 + the segment table's index the stations may come from; 0 for none */
    int      hot;          /* an edit came within a tile: the previous build's stations do not apply */
    uint64_t hash;         /* the trimmed pieces, keying the cache */
    int      records_only; /* the strip reaches no chunk this build draws: its stations, ground, profile and records, no slab */
} RLoft;
int loft_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, int comp, const RLoft *desc, const Piece *pc, int np, float total);
#define loft(...) loft_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
int line_meet(V2 a, V2 da, V2 b, V2 db, V2 *out);
int build_island(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, int32_t col, int32_t row);
int node_kind(const RCity *c, const RAtlasLevel *l, Family f, int32_t col, int32_t row);
/*  Stage three: the junction takes its shape from the segments that reach
 *  it.  A segment records which way it leaves each junction it touches
 *  (s_arm) and the junction hands back the distance at which the strip
 *  should start (s_trim), so an intersection is the polygon its arms cut
 *  out and not a square.  The segments are therefore walked twice: once to
 *  measure, once to draw. */
typedef struct
{
    float   ax, ay;     /* where the arm's own path starts            */
    float   dx, dy;     /* and the way it leaves, a unit vector       */
    float   len;        /* the segment's fitted length, untrimmed     */
    int32_t fcol, frow; /* the segment's far node, and its edge    */
    int8_t  fe, fkind;  /* there; fkind 2 when it is a junction    */
    int8_t  cls;        /* the segment's class: 0 road, 1 avenue, 2 boulevard; -1 rail */
    uint8_t have;
} RArm;
extern RArm  s_arm[2][R_MAP * R_MAP * 4]; /* [0] road, [1] rail */
extern float s_trim[2][R_MAP * R_MAP * 4];
extern float s_xwalk[2][R_MAP * R_MAP * 4]; /* the crossing band each arm gave up road for, tiles; 0 for none */

/*  Where a family's fitted centreline passes a tile, and which way it is
 *  going there.  A level crossing is built from these two -- the road's and
 *  the rail's -- so the panel, the gates and the stop lines lie across the
 *  roads and tracks that actually cross, at whatever angle they cross at. */
typedef struct
{
    float   x, y, dx, dy;
    uint8_t have;
} RCross;
extern RCross s_cross[2][R_MAP * R_MAP];
float         node_altitude(const RCity *c, int32_t col, int32_t row);
#include "walk/walkway.h" /* the sidewalk network: where the footways run and what they join */

/*  The road surface's STACK, as fractions of a tile's painter's slot.
 *  Everything laid on the road lies at one height on the graded ground,
 *  so the slot is the only thing that orders it, and two pieces given the
 *  same slot are ordered by nothing at all -- whichever the build drew
 *  last wins the pixel, which is neither stable nor meant.  So each piece
 *  of the surface names its own place here, lowest first. */
/*  The road works' numbers.  They are the SCRIPTS' -- scripts/geo.lua
 *  sets every one and a script may invent more -- so there is no struct
 *  of fields here and nothing to declare before one can exist.  This is
 *  the store the scripts keep them in and read them back from; the
 *  pipeline reads none of them by name.  Every number reaches the mesh
 *  through a rule's answer or a model's parts, so the C cannot hold an
 *  opinion about a measurement that the scripts do not.
 *
 *  A name nothing sets reads as zero. */
int         net_geo_set(const char *name, float v);
const char *net_geo_name(int i, float *v);
int         net_geo_count(void);
/*  Where a name sits in that listing, and the value there.  A model
 *  holds the index rather than a place in the struct, so a number a
 *  script made is named the same way the pipeline's own are. */
int         net_geo_index(const char *name);
float       net_geo_value(int i);
/*  A number by name, remembering where it was found; the cache is the
 *  reader's own and starts at -1.  For a tool or a check that wants one
 *  number: the pipeline itself asks a rule instead. */
float       net_geo(int *cache, const char *name);
/*  The look's knobs, the same way (net/road.c). */
int         net_tune_set(const char *name, float v);
/*  A knob by name, to keep: a family holds pointers at its width and
 *  its radii so every use reads the live value. */
const float *net_tune_at(const char *name);
const char *net_tune_name(int i, float *v);

/*  Outline points: four mouths' two corners each, and four returns of one
 *  segment more than the smoothness asks for.  A bound, not a knob --
 *  net_geo_set holds junc_arc to what this leaves room for. */
#define JUNC_MAX 96
/*  Where an arm meets the outline: the middle of its mouth and the way it
 *  leaves, one entry per edge.  Anything that has to tell a mouth from a
 *  free side asks for THIS rather than reading the outline's own tags: the
 *  hull can drop one of a mouth's two points or slip a curb return's
 *  tangent between them, and a mouth read off adjacent tags then vanishes
 *  -- which lays a footway straight across the road. */
typedef struct
{
    V2  mid, dir; /* the middle of the cut, and the way the arm leaves */
    V2  a, b;     /* its two corners, the right hand first */
    int have;
} JuncArm;
/*  A junction's outline must be a simple ring: no vertex where it doubles
 *  back, no two edges crossing.  Counted for every junction a build lays
 *  and reported by the mesh check; neither fault may exist. */
void junction_outline_reset(void);
/*  A corner the outline pulled in for standing too far out, counted. */
void junction_outline_clamped(float len);
void junction_outline_print(void);
int  junction_outline_faults(void);
int junction_poly(const RCity *c, Family f, int32_t col, int32_t row, int links, V2 *out, uint8_t *mouth, int max, float trim[4], JuncArm arms[4]);
#define FAMX(f) ((f) == F_RAIL ? 1 : 0)

int walk_segment(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, int32_t col, int32_t row, int e, uint8_t *visited);
int build_junction_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, Family f, int32_t col, int32_t row, int links, float order);
int            build_junction_done(void);
#define build_junction(...) build_junction_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
int net_compensate(void); /* the width compensation this build lofts with (walk.c) */
int build_power_tile(RMesh *m, const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, int links, float order, int crossing);
int put_prism_clip_m(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float dx, float dy, float len, float wid, float zb, float zf, float z0, float z1, float paint, float mat);
int put_prism_clip(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float dx, float dy, float len, float wid, float zb, float zf, float z0, float z1, float paint);
int road_under_deck(const RCity *c, float x, float y, float px, float py);
int build_highways(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp);
void build_hiway_bands_begin(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp);
int  build_hiway_band_next(void);
int  net_orients(const RCity *c);
int  net_ramp_sides(const RCity *c);
int  net_ramp_side_at(int i, int *free_side, int *room, int *back);
void net_ramp_side_is(int i, int keep);
int  net_ramp_spans(void);
int  net_ramp_span_at(int i, float *at, int *len, int *leaves, int *sgn);
void net_ramp_span_is(int i, int have, float top, float foot, float total, float ds);
int  net_ramp_shares(void);
int  net_ramp_share_at(int k, float *gap, int *cap);
void net_ramp_share_is(int k, int half);
OrientFan *net_orient_at(int i);
void net_ramp_fork_is(int straight, int along, int against, int fork);
int  net_ramp_fork(int straight, int along, int against);
void net_band_start_is(int back, int on, int way);
int  net_band_start(int back, int on);
int  build_hiway_band_chained(void);
StairFan *net_hw_chain(void);
int  build_hiway_band_fitted(void);
int  net_hw_fits(void);
int  net_hw_fit_begin(int w);
void net_hw_fit_done(int w);
const char *net_hw_fit_choice(const int **free_, const int **held);
void net_hw_fit_choice_is(int keep_free);
int  net_hw_fit_take(V2 *q, float *rad, float *tlim);
int  build_hiway_band_done(void);
int  build_highway_ramps(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp);
void build_ramps_begin(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp);
int  build_ramp_next(void);
int  build_ramp_done(void);
SlideFan *net_ramp_slide(void);
int  build_ramp_lofts(void);
int  build_ramp_loft(int i);
int build_highway_links(RMesh *m, const RCity *c, uint8_t mask_bit);

/*  lane.c: lanes as primitives -- the router, and the lanes at an
 *  intersection (docs/future.rst, "Lanes as primitives"). */
void lane_reset(void);
void seg_table_reset(void); /* net/table.c: the segment table, kept across the two passes */
void net_prof_reset(void);  /* net/walk.c: where a pass's time goes, stage by stage, under --times */
void net_prof_print(void);
void net_table_free(void); /* net/table.c: the sample arenas, once, at exit */
int  seg_table_count(void);
int  grade_only(int allowed); /* mesh.c: the grading pass, skipping what only the drawing needs */
int  seg_table_replay(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, uint8_t *visited);
int  seg_table_get(int i, int32_t *col, int32_t *row, int32_t *cc, int32_t *cr, const int32_t **tcol, const int32_t **trow, int *nt);
int  seg_table_nodes(int i, const V2 **q, const float **rad, int *nk); /* net/table.c: a segment's or band's fitted nodes and radii */
const char *hiway_ramp_lost(int32_t col, int32_t row);             /* hiway.c: why the on-ramp at a tile was not built this pass, or NULL */
int  hiway_band_count(void);                               /* hiway.c: the bands' own tiles, -1 past its table */
int  hiway_band_get(int i, const int32_t **tiles, int *n); /* ... as row * R_MAP + col */
int  lane_route(V2 A, V2 tA, V2 B, V2 tB, Piece *out, int *np, float *rmin);
int  lane_junction(RMesh *m, const RCity *c, uint8_t mask_bit, Family f, int32_t col, int32_t row, int links);
int  lane_segment(RMesh *m, const RCity *c, uint8_t mask_bit, Family f, const Piece *pc, int np, int32_t col, int32_t row, int e, int kind0, int32_t cc, int32_t cr, int back, int kind1, float hw, int cls);
void lane_stats_print(void);
void lane_dump_pieces(const Piece *pc, int np);
int  lane_deck(RMesh *m, const RCity *c, uint8_t mask_bit, const Piece *pc, int np, float hw, int band);
int  lane_nearest(V2 p, V2 dir, int cls, int band, float maxd, V2 *pos, V2 *odir, float *dist);
int  lane_nearest_outer(V2 p, V2 dir, int cls, float maxd, V2 *pos, V2 *odir, float *dist);
int  lane_ramp_arm(const RCity *c, int32_t col, int32_t row, int links, int e);
/*  Sidewalks as a primitive (sidewalk.c): every sidewalk is registered by its
 *  two ends, and the check counts the ends that meet nothing. */
enum
{
    SIDEWALK_STRIP = 0, /* a road strip's sidewalk, one each side           */
    SIDEWALK_LINK  = 1, /* a junction's, from one mouth's end to the next's  */
    SIDEWALK_CAP   = 2  /* round a dead end's turning head                    */
};
void                sidewalk_enable(int on);
int                 sidewalk_on(void);
/*  What is true of every strip and junction of the family: the footway's
 *  share of the band, the junction box, a railway's tracks and a level
 *  crossing's approach.  The script's answer, kept for a build. */
const ScriptFamily *net_family_rules(Family f);
/*  Where a family's lanes run at that class, from the centreline, inner
 *  first: arc.rules.lanes's answer.  Answers how many were written. */
int net_lane_offsets(Family f, int cls, float *off, int max);
void net_lane_runs_reset(void);
int  net_lane_runs(void);
void net_lane_run_at(int i, const char **fam, int *cls);
void net_lane_run_is(int i, const float *off, int n);
void                sidewalk_reset(const RCity *c);
int                 sidewalk_add(int kind, V2 a, V2 b, V2 oa, V2 ob); /* oa, ob: the way out past each end, or zero */
void                sidewalk_stats_print(void);
typedef struct JBox JBox; /* the junction's box (below, with the working structs): the sidewalk round it takes the box whole */
/*  A junction's asphalt as the composing script is handed it: the
 *  polygon the fan is laid over -- the outline the arms cut out, inset
 *  by the footway's band where there is one -- and where its middle
 *  sits.  The outline itself is the pipeline's: arms, corners, trims and
 *  kerb returns are a solver's work.  What is drawn over it is not. */
/*  A footway as the composing script is handed it: which kind of band
 *  it is, the stations the network holds for it, and where it sits.  The
 *  network's own work -- which ports it names, what it joins, how deep a
 *  crossing the arm could spare -- is the pipeline's; the band drawn
 *  over those stations is not. */
typedef struct
{
    void *m;
    const void *c;
    uint8_t     mask_bit;
    const void *w;   /* the WalkPath */
    const void *st;  /* its WalkSt stations */
} WalkFan;

/*  The footways, from the network once it is complete.  The PASS is the
 *  script's (scripts/compose/world.lua): these gather one path at a time
 *  and it composes them.  `outline` says which rule to ask -- in outline
 *  the bands stand aside and the network is drawn in their place. */
int                 sidewalk_count(void);
int                 sidewalk_outline(void);
int                 sidewalk_gather(RMesh *m, const RCity *c, uint8_t mask_bit, int i, WalkFan *out, ShapeId *sh);

/*  One lane, connector or band edge as the outline view draws it: the
 *  pieces the fit produced, the paint they are drawn in, how far over
 *  the surface they float and which deck band their height comes from.
 *  Where the line runs is the fit's; the hairline over it is not. */
typedef struct
{
    RMesh       *m;
    const RCity *c;
    uint8_t      mask_bit;
    const Piece *pc;
    int          np;
    float        lift, paint;
    int          band;
    /*  A ramp's lane climbs to the deck as it goes, so its height is
     *  eased along the whole line rather than taken from the ground:
     *  `ramp` says so and `off` which end the gore is at. */
    int          ramp, off;
    float        step;
} LaneFan;
/*  A fitted path as the composing script finishes it: the vertices the
 *  join stage produced, the tangent length a biarc vertex was built with
 *  (-1 where the vertex was searched), and the radius and tangent limit
 *  each corner is to be given.  Which vertices are idle and what radius
 *  a corner gets are decisions; the corridor sweep that answers what a
 *  radius may be is a primitive and stays in C. */
typedef struct
{
    V2    *out;
    float *fixed, *rad, *tlim;
    int    n;
    float  res0, res1, rmax, rmin, band, share, trim_cap;
    const void *mark; /* the corridor, for the sweep */
} FitFan;
FitFan   *path_ending(void);

/*  A junction's ring as the footway reads it: which of its edges carry a
 *  band and which are a road's mouth, which way each faces into the
 *  junction, and the ring moved in by the footway's width.  The ring
 *  itself is arc.rules.outline's; how the pavement sits on it is
 *  arc.rules.junction_band's. */
typedef struct
{
    const JBox    *jb;
    const V2      *poly;
    const JuncArm *arms;
    int            np;
    float          lw; /* the footway's width round this junction */
    /*  What the script answers: a flag and an inward normal an edge, the
     *  arm each edge is the mouth of, and the ring moved in. */
    uint8_t *band;
    V2      *nrm;
    int8_t  *edge_arm;
    /*  The offset direction at each end of each edge: mitred where the
     *  band turns a corner of the ring, so a kerb round a return is one
     *  smooth band and not a row of quads each square to its own edge. */
    V2      *mitre0, *mitre1;
    V2      *inset;
    int      inset_max, inset_n;
} BandFan;

/*  A junction's outline as the composing script works it out: the arms
 *  that leave it -- where each path starts, the way it goes, which edge
 *  it belongs to -- and the numbers the junction is sized by.  The
 *  script sorts them, finds the corner between each pair, cuts each
 *  arm's mouth back and walks the ring.  What the pipeline keeps is the
 *  arm table those rays come from and the check on the finished ring. */
typedef struct
{
    int     f;
    int32_t col, row;
    float   cx, cy, w, far, gro, cap;
    int     curbs;
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

/*  A level crossing's panel as the composing script is handed it: the
 *  four corners where the road's edges meet the track bed's, which the
 *  two paths' own lines settle, and the surface under each.  What is
 *  laid over them -- the panel, and the stop line before it -- is not
 *  the solver's. */
typedef struct
{
    RMesh       *m;
    const RCity *c;
    uint8_t      mask_bit;
    float        q[4][2];
    float        ground[4];
    float        order, lift, slot;
} XingFan;

const XingFan *net_crossing_panel(void);
int            net_crossing_approaches(void);
int            net_crossing_approach(int i, ScriptApproachAsk *out);
float net_crossing_order(void);
int            net_crossing_place(const ScriptApproach *mk);

/*  The deck's own surface near a point, for a line that belongs to a
 *  band rather than to the ground (net/lane.c). */
float deck_z_near(const RCity *c, uint8_t mask_bit, int band, V2 p);

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
int                 sidewalk_junction(const JBox *jb, const V2 *poly, const JuncArm *arms, int np);
/*  The outline the junction's ASPHALT is laid on: the box's outline with
 *  every side that carries a footway drawn in to the footway's own inner
 *  edge, so the two meet along it instead of the asphalt being laid under
 *  the band and hiding it.  A mouth keeps the outline, since the road runs
 *  on through.  Up to 2*np points; 0 when the box carries no footway at
 *  all, and the asphalt then reaches the outline. */
int sidewalk_junction_inset(const JBox *jb, const V2 *poly, const JuncArm *arms, int np, V2 *out, int max);
/*  Which of a junction's arms may carry a crossing, from its outline
 *  alone: `want[e]` is the band a crosswalk asks for there, 0 for none.
 *  How much of it the arm can actually spare is walk.c's, which knows
 *  how long the road beyond the mouth is. */
int sidewalk_junction_wants(const RCity *c, Family f, int32_t col, int32_t row, const V2 *poly, const JuncArm *arms, int np, float lw, float *want);
/*  The band one arm gave up road for, 0 for none (walk.c s_xwalk). */
float net_cross_depth(Family f, int32_t col, int32_t row, int e);
float               junc_surface(Family f, const RCity *c, uint8_t mask_bit, int col, int row, float x, float y, float zj);
int                 furniture_gate_state(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy, float angle, float arm_len, float time);
int                 furniture_rail_aspect(RMesh *m, const RCity *c, uint8_t mask_bit, const RRailSig *sg2, float g, int red);

typedef struct
{
    V2    pos, dir;
    float s, z;   /* z: the section's height, the highest ground it spans */
    float xd;     /* the distance along to the nearest level crossing on the segment */
    float wr, wl; /* a deck's half width either side, as a fraction of hw: 1, or 0.70 where a ramp took the outer lane */
    float zr[2];  /* the ramp lane's height on the right and the left, where `lane` says there is one */
    int   lane;   /* bit 1: a ramp lane strip on the right; bit 2: on the left */
    int   split;  /* a station where two tiles meet: the ground is the higher side's */
} Sample;

/*  The street furniture pass (furniture.c) and the road-marking pass
 *  (marking.c): each switched as one. */
void          furniture_enable(int on);
int           furniture_on(void);
int           put_stop_sign(RMesh *m, const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float order, int e, float h);
void          marking_enable(int on);
int           marking_on(void);
int           marking_near_crossing(const RCity *c, V2 pos);
const HwRamp *lane_ramp_tile(int32_t col, int32_t row); /* the ramp at a tile, or NULL */
int           lane_ramp(RMesh *m, const RCity *c, uint8_t mask_bit, const Piece *pc, int np, float hw, int deck_lane, int road_lane, int road_port, int off);
int           lane_port_id(int col, int row, int e, int out, int k);
int           lane_table_count(void);                                                       /* the lanes as built: ramps, connectors, deck lanes */
int           lane_table_get(int i, int *cls, int *fam, const Piece **pc, int *np, float *w);
void          lane_check_ends(void);
void          hiway_lanes(const RCity *c);
XLaneFan     *lane_cross_ask(RMesh *m, const RCity *c, uint8_t mask_bit);
void          net_wires_reset(void);
int           net_wires(void);
LaneFan      *net_wire_at(int i);
void          net_wire_done(int i);
int           lane_transitions(RMesh *m, const RCity *c, uint8_t mask_bit);
#define LANE_CLS_ROAD 0
#define LANE_CLS_TURN 1 /* a junction's connector: the near lane inside a crossing piece's box */
#define LANE_CLS_DECK 2
#define LANE_CLS_RAMP 3 /* a lane dropped off a deck to a road */
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
    int      sfirst, ns; /* the loft's stations, in the build's sample arena; ns 0 for none yet */
    uint64_t phash;      /* the trimmed pieces they were sampled from */
    int      band;       /* a highway band: walked by hiway.c, no arms, no crossings; replayed by hiway.c */
} RSeg;
/*  A highway band in the table: stored by the grading pass from its
 *  walk and fit (its own tiles as its corridor, its start cell and way
 *  as its key), replayed by the building pass (hiway.c). */
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
    float              cls; /* a road's class from seg_class (road.c); the arms, the table and the loft read it */
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
 *  wins it; the geometry of a candidate -- the line a slope lies on, the
 *  chord a free span is given, whether either holds on the corridor and
 *  keeps its covered tiles -- is the pipeline's, asked for through
 *  `try`.  The script keeps the winner of each prefix, then walks the
 *  chain back and names the runs. */
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
 *  them.  The chain is the start's line, every run, and the goal's; what
 *  the script decides is whether either end needs a line of its own, and
 *  whether a slope that owns a junction's tile pulls the end onto its
 *  own angle.  The lines themselves are the pipeline's arithmetic. */
/*  One crossing of two lines, as the composing script judges it.  The
 *  crossing has to be ahead of the line behind and behind the line
 *  ahead, and an arc that reads as an arc has to fit there.  How far
 *  ahead and how far behind are measured here; how much of either is
 *  enough is the script's. */
struct JoinFan
{
    void *fit, *pair; /* the fit and the boundary, the pipeline's own */
    V2    at;         /* where the lines cross */
    int   free_line;  /* a free line at one end or the other */
    float ahead, behind;  /* the crossing past the line behind, and short of the one ahead */
    float reach, reach_on; /* how much line there is either side of it */
    float len_in, len_out; /* the edge either side of the crossing */
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
 *  script's; whether a placing yields a biarc the band holds is the
 *  pipeline's, asked through `try`. */
/*  Two of a path's lines that neither cross nor take a biarc, as the
 *  composing script walks between them: the line's end, the points of
 *  the gap, the next line's start.  Which of those are worth a vertex,
 *  and whether the gap is a sideways step to be drawn as one diagonal,
 *  are the script's. */
/*  One corner of a fitted path, as the composing script sweeps a fillet
 *  into it.  How wide an arc the corner could take, how narrow one may
 *  be before it stops reading as an arc, and how the search walks
 *  between the two are the script's; whether an arc of a given radius
 *  stays on the corridor and still covers the tiles the corner was drawn
 *  for is the pipeline's, asked through `holds`. */
/*  A fitted path's vertices as the composing script cuts them into the
 *  pieces a strip is lofted from: straights and arcs, end to end.  What
 *  radius a corner is finally given -- clamped by its budget, by the
 *  piece already laid on the way in, and by the edge it leaves along --
 *  is the script's; where the tangent points and the arc's centre fall
 *  is the pipeline's. */
/*  A highway band's cells as the composing script picks the points the
 *  fit is given.  A STAIRCASE -- curve blocks turning alternately with
 *  at most a few straight cells between, the game's way of laying a
 *  diagonal -- is one point, the centre of the blocks and short
 *  straights it is made of, so the run before it, the diagonal through
 *  its middle and the run after it are three legs the fit fillets at
 *  their two bends.  Every other cell is a point of its own. */
/*  A highway strip's elevation as the composing script lays it out.  A
 *  RAMP is one straight line from the ground at its road end to the
 *  ground at its deck end; a DECK is stiff, held to a grade and rounded
 *  over a window so it neither follows every bump nor dips into a hollow.
 *  Both then take the lift, tapered over the ramp cells at each end.  The
 *  stations and the ground under them are the pipeline's. */
/*  A ramp's join, as the composing script slides it.  The descent may
 *  start further along the deck and the join may sit further along the
 *  road; every pairing of the two is routed and the widest that holds
 *  wins.  Whether a pairing routes at all, and whether it leaves by the
 *  ramp tile's own road edge, are the pipeline's. */
/*  A deck's stations as the composing script narrows them for each ramp
 *  that leaves it.  The deck gives up its outer lane over the ramp's
 *  taper and takes it back afterwards, and where it does so -- and how
 *  far it stays narrow toward a partner ramp -- is the script's. */
/*  A strip's elevation as the composing script ramps it between the
 *  nodes at its ends.  A node -- a junction, a dead end, a level
 *  crossing -- stands at its own tile's levelled height, and every
 *  corridor that reaches it ramps to that one number, so two segments
 *  meeting at a junction agree without anything being solved between
 *  them.  Which stations are anchors, and how the ramp runs between
 *  them, are the script's. */
/*  An on-ramp's four sides, as the composing script reads them.  One is
 *  the deck it climbs to, one the road it comes down onto, and which is
 *  which decides everything the ramp is afterwards.  What each neighbour
 *  IS is the pipeline's; which side is the deck's and which the road's is
 *  the script's. */
/*  The shelf's copies of each corner, as the composing script reconciles
 *  them.  Every tile keeps its own copy of a corner -- which is what lets
 *  two corridors lie side by side as two shelves with a wall between them
 *  -- and the copies were written by whichever station was nearest, so
 *  one corridor's tiles can take the corner they share from different
 *  stations and disagree.  Which copies must agree, and on what, is the
 *  script's. */
/*  A road's lanes as the composing script carries them across a
 *  crossing.  A rail across a road, or a road under a deck, ends the
 *  segments on both sides of the crossing tile, and their lanes face
 *  each other open.  Which open end goes on into which is the script's;
 *  where each lane's end lies and how one is drawn to the other is the
 *  pipeline's. */
struct XLaneFan
{
    void       *m;
    const void *c;
    uint8_t     mask_bit;
    int         n;    /* the lanes as they stand; links are appended after */
    int         fail; /* a link the router could not draw */
};

struct ShelfFan
{
    int nodes; /* how many node tiles there are */
};

struct OrientFan
{
    const void *c;
    int32_t     col, row;
    int         kind;  /* 0 nothing here, 1 a ramp beside a deck, 2 a deck end-on */
    int         dside, rside, eside, off, roads;
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
    int    n, nramps;
    float  reach;  /* how far a ramp reaches for the station it drops from */
    float  narrow; /* the outer lane's inner edge, across the band */
};

struct SlideFan
{
    void  *ramp;             /* the Ramp being built */
    int    lane;             /* the road lane the join must land on */
    V2     B0, tB0, trav;    /* where the join aims, and the lane's own way */
    float  reach;            /* how far along the deck the descent may start, to the lane line */
    int    parallel;         /* the deck and the lane never meet: no sliding along the deck */
    float  merge;            /* how far along the road the join may slide */
    float  snap;             /* how near a recorded lane counts as on it */
    float  taper;            /* how far past the road edge the taper runs */
    /*  The placing under test, and the best kept. */
    Piece *tmp;
    int    n;
    float  r, beyond;
    V2     pos, dir;  /* where the placing under test lands on the lane */
    float  at;        /* and how far along the road it sits */
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
    int   ramp;       /* the strip is a structure: a ramp or a lane drop */
    int   lane_piece; /* a lane drop's turn-out, which eases rather than runs straight */
    int   lane_off;   /* and which end of it the gore is at */
    int   flat;       /* the strip takes no lift at all */
    float z0;         /* how far above the ground its deck end sits */
    float ramp0, ramp1; /* how far the lift is tapered in at each end */
    float grade, stiff; /* the deck's steepest rise, and the window it is rounded over */
    float lift;         /* how far a deck rides above the ground */
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
    int            padded;      /* the corridor is the road's own tiles, so the band carries the margin */
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
    float band, margin;    /* the band, and the padding a road's corridor carries */
    int   padded;          /* the corridor is the road's own tiles */
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
 *  outside it; net_loft_compose is that, for the callers still in C. */
Loft       *net_loft_strip(void);
void        net_loft_slab_done(double tp);
int         net_loft_draws(void);
const char *net_loft_taper(void);
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
struct JBox /* JBox, declared above with the sidewalk's API */
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
    int          records_only; /* the box reaches no chunk this build draws: its lanes' and its sidewalk's records, no drawing (the loft's rule) */
    int          comp;         /* the width compensation the build lofts with (walk.c net_compensate), for the box's own lofts */
};

/*  One station pair of a strip, as the slab lays it: what the family
 *  may restyle (the across range, the along offset, the class, the
 *  material and along of its markings) and what it may build beside. */
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

/*  A FAMILY: how one kind of line is drawn -- how wide it is, which
 *  material it wears, which of the loft's stages it supplies and what it
 *  builds at a junction.  So no generic stage has to branch on which
 *  family it is working for.
 *
 *  None of it is a C table.  A SCRIPT declares a family --
 *  scripts/families/road.lua and its neighbours -- and net/family.c
 *  builds this from the declaration, so another way to draw a highway is
 *  a file in scripts/families rather than a change here.  The knobs are
 *  pointers into the live tuning, by name.
 *
 *  Each STAGE is NAMED rather than pointed at.  A name net/family.c has
 *  registered as a primitive binds to that C function; any other name
 *  binds to the rule `arc.rules.<name>`, which is handed the thing the
 *  stage works on.  Ask net_family_has whether a family supplies a
 *  stage and call it through net_family_<stage>: which of the two
 *  answers is the registry's business and no call site's. */
typedef enum
{
    NH_CONTROL = 0, /* a junction's control */
    NH_BOX,         /* the junction box on the outline, or the family's own drawing */
    NH_RECORD,      /* what a strip records for the traffic and the passes */
    NH_CROSSING,    /* a tile whose second piece is this family's: it crosses the first */
    NH_FLIES,       /* the grading: 1 where the strip stands clear of the ground and notches nothing */
    NH_TAPER,       /* the stations' widths: a ramp's narrowing */
    NH_PROFILE,     /* the heights along the strip, in place of the ramp between nodes */
    NH_WORKS,       /* what stands under or beside the strip, before the slab: piers */
    NH_TRAFFIC,     /* where the traffic runs across the strip, as fractions of a tile */
    NH_FURNITURE,   /* the strip's furniture: a road's lamps, a rail's signals */
    NET_HOOKS
} NetHook;
extern const char *const NET_HOOK_NAME[NET_HOOKS]; /* the stage names a declaration uses (net/family.c) */

typedef struct NetFamily
{
    const char  *name;
    Family       f;         /* the tile family it answers for; the highway's is the road's */
    const float *width;     /* the strip's width across, the live knob */
    const float *rmin, *rmax; /* the fit's tightest and widest radius */
    float        ref_width; /* the width the junction outline's numbers were tuned at */
    float        mat;       /* the strip's and the box's material */
    LoftKind     loft;      /* the loft kind a segment of it is drawn as */
    int          fit_fam;   /* the tangent fit's family code */
    float        junc_lift; /* the box's order over the ground's: a rail's a hair over a road's */
    float        shelf_grade; /* the grading's ceiling on the profile's own grade, levels per tile */
    int          curbs;     /* the outline has curb returns and hands trims back; a rail's has none */
    int          ramps;     /* a ramp may attach beside a junction */
    int          ends_at_buildings; /* a building tile ends a segment with a turning head */
    int          caps;      /* a dead end gets a round cap */
    int          classed;   /* segments carry a class from their tiles: lanes, lamps */
    /*  The stages, as the declaration's names resolved: the primitive,
     *  or NULL where `rule` holds a rule's name instead. */
    int (*control)(const RCity *c, int32_t col, int32_t row, int links);
    int (*box)(JBox *jb);
    const char *ask[NET_HOOKS]; /* the rule a primitive of two halves asks for in between them */
    const char *ask_after[NET_HOOKS]; /* ... and the one its second half asks in turn */
    int (*box_done)(JBox *jb); /* the half of it that runs after the drive has composed the asphalt */
    int (*record)(Loft *x);
    int (*record_done)(Loft *x);
    int (*crossing)(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int32_t col, int32_t row, int second);
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
     *  be fitted: a railway sweeps its corners across the field, a road
     *  keeps to its tiles (0). */
    int free_reach;
    /*  A junction's reach along each arm from its centre, in tiles, for a
     *  family whose junction is a turnout: the arms' strips start there
     *  and the box draws across it (rail.c rail_box).  0 for a family
     *  whose junctions hand back curb trims. */
    float turnout;
    /*  The painter's slot its strip's carriageway draws at, NAMED: the
     *  numbers are the scripts' and a family says which of them it
     *  wants rather than pointing at a field. */
    const char *slot;
    /*  A freeway deck: its quads carry a gore where a ramp takes the
     *  outer lane, and an underside of soffit, fascias and end walls.
     *  Both are the composing script's (scripts/compose/deck.lua). */
    int deck;
} NetFamily;
enum
{
    NET_LANE_ENDS_OPEN,    /* the lanes stop */
    NET_LANE_ENDS_CAP,     /* a road: round the cap, lane for lane */
    NET_LANE_ENDS_REVERSE, /* a rail: the train reverses; the arriving track names the leaving one */
};

/*  A family as a script declares it, before the names are resolved.  The
 *  knobs, the loft kind, the tile family, the lane ending and every
 *  stage arrive as NAMES, which is what lets a declaration name a thing
 *  the C has never heard of. */
#define NET_FAM_MAX 8
typedef struct NetFamilyDecl
{
    const char *name;
    const char *tiles;   /* the tile family it answers for: "road", "rail", "power" */
    int         answers; /* it is the family that tile family means */
    int         walk;    /* the walk visits it, at this place in the order; -1 for a family the walk never reaches */
    const char *width, *rmin, *rmax; /* the live knobs, by name */
    float       ref_width, mat;
    const char *loft; /* "road", "rail", "deck", "ramp" */
    int         fit;
    float       junc_lift, shelf_grade;
    int         curbs, ramps, ends_at_buildings, caps, classed;
    const char *stage[NET_HOOKS];
    float       lane_paint;
    const char *lane_ends; /* "open", "cap", "reverse" */
    int         free_reach;
    float       turnout;
    const char *slot;
    int         deck;
} NetFamilyDecl;
/*  Declare one, replacing any of the same name.  Answers 0, or -1 with
 *  the reason logged: a name no knob, loft kind, tile family or stage
 *  answers to is a fault the script has to hear about. */
int  net_family_define(const NetFamilyDecl *d);
/*  The same reading, declaring nothing: what the LINT does.  `rule`
 *  takes 1 at each stage the declaration answered with a rule rather
 *  than a primitive, so the lint knows which rule names a family
 *  invented and can stop calling them unknown. */
int  net_family_check(const NetFamilyDecl *d, int *rule);
void net_family_reset(void); /* before a reading of the scripts: what stands is what this reading declares */
int  net_family_count(void);
const NetFamily *net_family_at(int i);
const NetFamily *net_family_named(const char *name);
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
void net_road_lamps_are(const ScriptLamp *lamp, int n);
void net_road_walks_drew(int drew);
void net_rail_cross_ask(const float **cross, int *n);
void net_rail_marks_are(const ScriptMark *mk, int n);
int  net_family_box(const NetFamily *fam, JBox *jb);
int  net_family_box_done(const NetFamily *fam, JBox *jb);
JuncFan *net_junction_fan(void); /* the outline the drive lays a junction's asphalt on (road.c) */
void           net_box_lofts_reset(void);
int            net_box_loft_add(const JBox *jb, const RLoft *d, const Piece *pc, int np, float total);
int            net_box_lofts(void);
int            net_box_loft(int i);
int  net_family_record(const NetFamily *fam, Loft *x);
int  net_family_crossing(const NetFamily *fam, RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int32_t col, int32_t row, int second);
int  net_family_flies(const NetFamily *fam, const RLoft *d, float over);
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
extern const NetFamily *net_road, *net_rail, *net_hiway, *net_power;
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
    NET_PROF_DECK_WORKS,
    NET_PROF_RECORD,
    NET_PROF_SLAB,
    NET_PROF_DECK_SLAB,
    NET_PROF_JUNC_LANES,
    NET_PROF_JUNC_BOX,
    NET_PROF_STATIONS,
    NET_PROF_CACHED,
    NET_PROF_SAMPLED,
    NET_PROF_GROUND,
    NET_PROF_HW_AIR, /* build_highways (hiway.c): the free-air scan, the bands, the tints, the ramps, the transitions, the lane check */
    NET_PROF_HW_BANDS,
    NET_PROF_HW_TINT,
    NET_PROF_HW_RAMPS,
    NET_PROF_HW_TRANS,
    NET_PROF_HW_CHECK,
    NET_PROF_RAMP_LOFT, /* of the ramps: their lofts (the rest is finding, posing and routing them) */
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
 *  measure fits every segment and settles the trims and the crossings'
 *  paths, and the draw lays the junctions and the strips.  The level
 *  crossings and the power lines fall in the gap, because a crossing is
 *  built from the two paths the measure fitted. */
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
void           junction_rings_reset(void);
void           junction_ring_keep(const OutlineFan *o);
int            junction_ask(const RCity *c, Family f, int32_t col, int32_t row, int links, OutlineFan *o, V2 *out, uint8_t *mouth, float *trim);

/*  What stage three measured and left for the drive to have answered:
 *  each junction's control, and each mouth's crosswalk depth. */
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
int   net_record(RRoadNet *net, const Sample *smp, int ns, float total, int cls, int rail, const RLoft *d);
void  piece_at(const Piece *p, float t, V2 *pos, V2 *dir);
void  pieces_at(const Piece *pc, int np, float t, V2 *pos, V2 *dir); /* along a chain, clamped */
int   seg_table_pieces_from(int32_t col, int32_t row, int e, Piece *out, int cap, int *np); /* a segment's fitted pieces from one end (table.c) */
int   lane_port(Family f, int col, int row, int e, int out, int k, V2 *pos, V2 *dir);      /* a junction port's pose (lane.c port_pose) */
float profile_at(const Sample *smp, int ns, float at);

/* ---- net/junction.c: the junction outline and box */
void arm_heading(const Piece *pc, int np, float total, int from_end, V2 *pos, V2 *dir);

/* ---- road.c: the road family */
float *mesh_tune(void);
int    seg_class(Seg *x);
void   seg_class_counts(const Seg *x, int cnt[3]);
int    net_seg_class_of(int32_t col, int32_t row, int e);
int    build_networks_classes(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp);
int    net_fits(void);
void   net_fits_reset(void);
int    net_fit_begin(const RCity *c, int i);
int    net_fit_done(int i);
const char *net_fit_choice(const int **free_, const int **held);
void   net_fit_choice_is(int keep_free);
int    net_seg_fit_of(Family f, int32_t col, int32_t row, int e, V2 *q, float *rad, float *tlim, int cap);
int    net_seg_class_at(int i, int cnt[3]);
void   net_seg_class_is(int i, int cls);

/* ---- rail.c: the rail family */
int seg_measure_crossings(Seg *x);
int on_crossing_panel(const RCity *c, int32_t tc, int32_t tr, float x, float y);

/* ---- hiway.c: the highway family */
int deck_edge(RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float ea[2], const float eb[2], float za, float zb, const float nrm[3], float girder, int parapet);

/* ---- mesh.c: the build's clock, read by the stages' timing lines */
extern int s_pass;
double     tms(void);
void       tnote(const char *what, double t0);
int        mesh_build(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int roads);
int        mesh_tile_corners(const RCity *c, int32_t col, int32_t row, int underground, float z[4]);
int        mesh_query(const RCity *c, int32_t col, int32_t row, char *buf, size_t n);

#endif
