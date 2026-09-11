/*  net/types.h: the network's own vocabulary.
 *
 *  The types a store keeps, the fans a rule is offered, and the stores
 *  themselves.  Every header in this directory includes this one, so a
 *  type is declared once and in one place.
 *
 *  A type here is the NETWORK'S.  One the three halves share is in
 *  pipeline.h, and one only mesh/ names is with mesh/. */
#ifndef ARC_NET_TYPES_H
#define ARC_NET_TYPES_H

#include "pipeline.h"

/*  The structure's proportions, measured off the original's own
 *  rendering of Four Cities' viaduct rather than the specification.  The
 *  art draws a slim ribbon on thin columns.  Its edge is a dark shadow a
 *  quarter of a level deep.  No parapet stands over the way, and a
 *  column about a meter across carries it every two tiles.  Built to the
 *  specification's concrete sections it read as a viaduct of walls. */
#define BAND_LIFT      1.0f
#define SPUR_HW          (0.15f * s_tune.band_w) /* a spur lane: the slab's outer lane, 0.70 to 1.0 across, on its own */
#define BAND_LANE_IN    0.70f  /* the outer lane's inner edge, across the band */
#define BAND_LANE_TAPER 4      /* tiles: one gore, then the descent */
#define BAND_GIRDER  0.11f     /* the slab's edge: 0.9 m of slab and girder */
#define BAND_BENT    1.0f
#define LINE_W    (s_tune.line_w)
#define THREAD_W    (s_tune.thread_w)
#define LINE_RMIN (s_tune.line_rmin)
#define THREAD_RMIN (s_tune.thread_rmin)
#define TRAIN_PITCH 0.48f
#define TRAIN_BOGIE 0.14f
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
} BandSpur;
#define MAX_SPURS 2048
/*  A slab's stations, every band's, recorded by the loft for the spurs. */
typedef struct
{
    V2    pos, dir;
    float s, z;
    int   band;
} BandSt;
#define BAND_MAX_ST 120000
extern int  s_band_nst, s_band_st_at;
#define put_tri_ground(...) put_tri_ground_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
#define strip_quad_z(...) strip_quad_z_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
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
/*  A BAND's run, which is the same question asked of a different
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
} BandRun;
/*  What the script hands its bands back through: it reads the cells off
 *  the map itself. */
typedef struct
{
    int full;
} BandDiscFan;
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
typedef struct StairFan  StairFan;
typedef struct ProfFan   ProfFan;
typedef struct SlideFan  SlideFan;
typedef struct DropFan   DropFan;
typedef struct OrientFan OrientFan;
typedef struct ShelfFan  ShelfFan;
typedef struct XLaneFan  XLaneFan;
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
#define build_junction(...) build_junction_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
/*  THE ONE DOOR (net/drive.c): the only place the renderer calls up into
 *  a script.  A build hands its passes out one at a time and a move
 *  hands out the world that moves.  Which of the two a turn is, the
 *  script reads off the handle. */
enum
{
    DRIVE_BUILD = 1,
    DRIVE_MOVE  = 2
};
/*  Margins as a primitive (margin.c): every margin is registered by its
 *  two ends, and the check counts the ends that meet nothing. */
enum
{
    MARGIN_STRIP = 0, /* a line strip's margin, one each side           */
    MARGIN_LINK  = 1, /* a junction's, from one mouth's end to the next's  */
    MARGIN_CAP   = 2  /* round a dead end's turning head                    */
};
typedef struct JBox JBox; /* the junction's box (below, with the working structs): the margin round it takes the box whole */
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
#define LANE_CLS_SPUR 3 /* a lane dropped off a slab to a line */
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
    int      band;       /* a band: walked by band.c, no arms, no meets.  Replayed by band.c */
} RSeg;
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
/*  A FAMILY: how one kind of line is drawn.  How wide it is, which
 *  material it wears, which of the loft's stages it supplies and what it
 *  builds at a junction.  So no generic stage has to branch on which
 *  family it is working for.
 *
 *  None of it is a C table.  A SCRIPT declares a family, in a file in
 *  scripts/families, and net/family.c builds this from the declaration.  So another way to draw a band is a file
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
     *  Both are the composing script's. */
    int slab;
} NetFamily;
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
extern const NetFamily *net_line, *net_thread, *net_band, *net_power;
/*  Where a building pass's time goes, stage by stage (--times): the
 *  stages add to these, walk/walk.c prints them. */
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
    NET_PROF_BAND_AIR, /* build_bands (band.c): the free-air scan, the bands, the tints, the spurs, the transitions, the lane check */
    NET_PROF_BANDS,
    NET_PROF_BAND_TINT,
    NET_PROF_BAND_SPURS,
    NET_PROF_BAND_TRANS,
    NET_PROF_BAND_CHECK,
    NET_PROF_SPUR_LOFT, /* of the spurs: their lofts (the rest is finding, posing and routing them) */
    NET_PROF_N
};

#endif /* ARC_NET_TYPES_H */
