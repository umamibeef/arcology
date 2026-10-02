/*  mesh/fit.h: the tangent fit, as a service and as the readings it
 *  offers a rule.
 *
 *  A path's cells become pieces: runs, arcs and biarcs inside the
 *  corridor the family allows.  Each stage of it is a FAN: a reading the
 *  fit hands to the composing script, and the answer the script hands
 *  back.  The fans are the fit's own structs, so they are declared with
 *  it. */
#ifndef ARC_MESH_FIT_H
#define ARC_MESH_FIT_H

#include "pipeline.h"
#include "mesh/mesh.h"

int  path_fit_begin(const RCity *c, const int32_t *tcol, const int32_t *trow, int nt, float hw, V2 start, V2 goal, float rmax, float rmin, float gro, float reserve, int32_t ex0, int32_t ex1, int free_reach, V2 *out, float *rad, float *tlim, int cap);

int  path_fit_end(void);

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

/*  The three ways a boundary may be joined.  A way lays a reading of
 *  its own kind, and path_try answers which one it laid. */
enum
{
    TRY_NONE = 0,
    TRY_ARC,
    TRY_BIARC,
    TRY_WALK
};
int path_try(const char *how, void **obj);

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

void fit_tally_get(int bucket, void *dst, size_t cap); /* the fit's tallies, around a fit that may be discarded */

void fit_tally_set(int bucket, const void *src, size_t cap);

int  path_fit_points_begin(const uint8_t *mark, const uint8_t *own, const V2 *pts, int n, float hw, V2 start, V2 goal, float rmax, float rmin, float gro, int32_t ex0, int32_t ex1, int free_lines, V2 *out, float *rad, float *tlim, int cap);

int  path_fit_points_end(void);

void path_fit_prims(void);

void fit_tally_into(int bucket); /* which tally bucket the next fit counts into */

/*  What the tangent fit did over a build, for --mesh-check. */
void fit_stats(void);

void fit_stats_reset(void);

int line_meet(V2 a, V2 da, V2 b, V2 db, V2 *out);

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

/*  A fitted path's vertices as the composing script cuts them into the
 *  pieces a strip is lofted from: straights and arcs, end to end.  What
 *  radius a corner is finally given.  Clamped by its budget, by the
 *  piece already laid on the way in, and by the edge it leaves along.
 *  Is the script's.  Where the tangent points and the arc's center fall
 *  is the pipeline's. */
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

/*  One corner of a fitted path, as the composing script sweeps a fillet
 *  into it.  How wide an arc the corner could take, how narrow one may
 *  be before it stops reading as an arc.  How the search walks between
 *  the two are the script's.  Whether an arc of a given radius stays on
 *  the corridor is the pipeline's.  So is whether it still covers the
 *  tiles the corner was drawn for.  This is the pipeline's, asked
 *  through `holds`. */
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

/*  Two of a path's lines that neither cross nor take a biarc, as the
 *  composing script walks between them.  The line's end, the points of
 *  the gap, the next line's start.  Which of those are worth a vertex,
 *  and whether the gap is a sideways step to be drawn as one diagonal,
 *  are the script's. */
struct StepFan
{
    void *fit, *pair;
    int   gap;        /* how many of the chain's own points lie between the lines */
    int   head, tail; /* the line either side is a run, not the chain's own end line */
    float hw;         /* the band's half width, which decides how wide a step may be stepped */
    int   placed;     /* the diagonal went in, so the walk is done */
};

/*  Two of a path's lines that do not cross, as the composing script
 *  bridges them.  An S is drawn between them, its tangent points pulled
 *  back along each line: the farther back, the wider its arcs.  How far
 *  back either may go, and in what order the placings are tried, are the
 *  script's.  Whether a placing yields a biarc the band holds is the
 *  pipeline's, asked through `try`. */
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

/*  A path's runs as the composing script makes the chain of lines from
 *  them.  The chain is the start's line, every run, and the goal's.
 *  What the script decides is whether either end needs a line of its
 *  own.  Whether a slope that owns a junction's tile pulls the end onto
 *  its own angle.  The lines themselves are the pipeline's arithmetic. */
struct ChainFan
{
    const V2 *pts;
    Run      *runs, *lines;
    int       nr, nl, nt, ns;
    int       ex0, ex1;       /* a junction's tile at that end of the chain */
    int32_t   at0, at1;       /* that tile's index, or -1: the pose store is keyed by it */
    V2        start, goal;    /* the ends, which `aim` may move */
    V2        st0, st1;       /* the first and last steps, which their lines run along */
};

/* ---- mesh/fit.c: the path fit */
int tf_biarc(V2 A, V2 t0, V2 B, V2 t1, V2 *c0, V2 *c1, float *dout);

#endif /* ARC_MESH_FIT_H */
