/*  pipeline.h: the vocabulary the renderer's three halves share.
 *
 *  This header is no one directory's own.  It holds the small set of
 *  things all three name.  A point, a piece, a sample, a family slot,
 *  and what a loft is asked to draw.  Nothing is declared here that one
 *  directory answers.  Those declarations are in that directory's own
 *  headers, one a file.  A reader who wants to know what the fit or the
 *  walk offers opens mesh/fit.h or walk/walk.h.
 *
 *  Each directory holds one kind of thing.
 *
 *      mesh/   the shape primitives
 *      net/    the stores, and the plumbing that turns a Lua
 *              declaration into a family
 *      walk/   a segment's stages
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
 *  The families that answer the stages are declared in Lua. */
#ifndef ARC_PIPELINE_H
#define ARC_PIPELINE_H

#include <math.h>
#include <stdint.h>

#include "geo.h"

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

/*  Outline points: four mouths' two corners each, and four returns of
 *  one segment more than the smoothness asks for.  A bound, not a knob:
 *  geo_set holds junc_arc to what this leaves room for. */
#define JUNC_MAX 96

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

#endif /* ARC_PIPELINE_H */
