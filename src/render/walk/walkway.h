/*  walkway.h: the margin network.
 *
 *  The margins as a NETWORK, the way the lanes are a network of lanes.
 *  A path with a PORT at each end, and two paths naming the same port
 *  are one continuous walk.  A segment carries a path along each of its
 *  sides.  A junction turns each of its corners with one.  A meet joins
 *  the two sides of an arm.  A cap closes a terminus.  A port belongs to
 *  an arm of a node and a side of that arm, so neither path has to know
 *  the other exists.  They meet because they name the same port.
 *
 *  This file decides where a margin runs and what it joins.  Drawing it
 *  is net/margin.c's, and nothing here reaches the mesh. */
#ifndef R_WALKWAY_H
#define R_WALKWAY_H

#include <stdint.h>

#include "city.h"

/*  V2 is pipeline.h's.  This header is included after it. */
struct RCityTag;

/*  The numbers a margin is built from.  How far across a band it
 *  reaches, how deep a meet runs and how nearly parallel its two margins
 *  must be.  Are the line works' own, which the scripts set
 *  (scripts/geo.lua).  A meet's own depth is arc.rules.lap_at's answer,
 *  held to the line the arm can spare by arc.rules.meet. */
typedef enum
{
    WALK_SIDE = 0, /* along a segment, one side of the way */
    WALK_CORNER,   /* round a junction, the end of one arm to the start of the next */
    WALK_CROSS,    /* across an arm, one side to the other */
    WALK_CAP       /* round a terminus, one side to the other */
} WalkKind;

/*  One cross-section of a margin: the two edges of the band and the
 *  height it lies at.  A path is a run of these, so whoever draws it
 *  needs to know nothing of outlines, mitres or lip returns. */
typedef struct
{
    V2    outer, inner;
    float z;
} WalkSt;

/*  A margin, as the network holds it: what kind it is, the node and arm
 *  it belongs to.  There its two ends are, the port each end names, the
 *  way out past an end that names none, and its cross-sections. */
typedef struct
{
    uint8_t kind;
    int32_t col, row; /* the node it belongs to */
    int     e;        /* the arm it leaves by, 0..3, or -1 */
    int     port[2];  /* the port each end names, -1 for an end that meets nothing by name */
    V2      end[2];
    V2      out[2]; /* the way on past each end, for what stands beyond an open one */
    float   w;
    float   ask;   /* the depth a meet asked for, against which its own is drawn */
    float   order; /* the painter's slot its node draws in */
    ShapeId  owner; /* the shape it belongs to, its junction, though it is drawn in a later pass */
    uint8_t drape; /* it lies ON the ground, as a strip does, rather than flat at its own height */
    int     st0, nst;
} WalkPath;

/*  The port at one side of one arm of one node.  Both the segment running
 *  out along that arm and the junction turning its corner there name this,
 *  and that is what makes them one walk.  Side 0 is the right hand looking
 *  OUT from the node along the arm, side 1 the left. */
int walk_port(int32_t col, int32_t row, int e, int side);
/*  A port in words, "75,70 west, right", for a report or a note. */
void walk_port_name(int port, char *out, size_t n);
/*  An arm in words, ", north arm" and so on, or "" for no arm.  A tail a
 *  name can carry whether or not the thing belongs to an arm. */
const char *walk_arm_name(int e);

void walk_net_reset(const RCity *c);
/*  A path and its cross-sections, which the network copies into its own
 *  arena.  `st` may be NULL for a path that is a join and not a band. */
int             walk_net_add(const WalkPath *p, const WalkSt *st, int nst);
int             walk_net_count(void);
const WalkPath *walk_net_get(int i);
const WalkSt   *walk_net_st(const WalkPath *p);
/*  Every port and how many margins name it: two is a join, one is an end
 *  that meets nothing.  The reason is read off the map. */
void walk_net_check(void);
/*  A margin the network cannot account for: a meet whose two ends reach
 *  no margin.  Zero when every path meets what it names. */
int walk_net_faults(void);

/*  A junction MARKS a stripe on an arm when it has a margin either side
 *  of that arm's mouth to walk between.  It lays the band itself
 *  (net/margin.c).  Its fill stops at the band, so the two meet along a
 *  line.  This is what the inset asks.  Reset with the network. */
void walk_cross_offer(int32_t col, int32_t row, int e);
int  walk_cross_at(int32_t col, int32_t row, int e);
/*  How the mouths came out: the count, the reason each one that carries
 *  no meet does not.  How many of the ones that do run shallower than a
 *  full band because the junction is small. */
void walk_cross_tally(int mouths, int uncontrolled, int no_margin, int not_parallel, int no_line, int narrowed);
/*  Those six counts as they stand, for a script or a report. */
void walk_cross_counts(int out[6]);
/*  The city the network was built on, or NULL before the first build. */
const RCity *walk_net_city(void);

#endif /* R_WALKWAY_H */
