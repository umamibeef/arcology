/*  network/network.h: layer 2, the road network, in ASAM OpenDRIVE's
 *  structure (plans/network.md).  It is built from the topology alone,
 *  before anything is fitted.
 *
 *  A ROAD has lane SECTIONS, and a section has LANES with signed ids.
 *  Lanes right of the reference line have negative ids, and lanes left
 *  of it positive ids.  Where the world drives on the right, the lanes
 *  right of the line travel with `s`, and on the left the lanes left of
 *  it.  A JUNCTION joins the roads that reach it.  A `default` junction
 *  joins them through CONNECTING ROADS, and a `direct` junction links
 *  their lanes straight across.  A LINK is one lane going on into
 *  another.  `s` is provisional: it counts the road's units from its
 *  first end.  A lane's width is the look's, so it is not here. */
#ifndef ARC_NET_NETWORK_H
#define ARC_NET_NETWORK_H

#include <stddef.h>

#include <stdint.h>

#include "city.h"

/*  OpenDRIVE's lane types, as the network uses them. */
enum
{
    NLANE_DRIVING = 0,
    NLANE_ENTRY,
    NLANE_EXIT,
    NLANE_SIDEWALK,
    NLANE_RAIL,
    NLANE_MEDIAN,   /* a boulevard's median, either side of its line */
    NLANE_BORDER,   /* a railway's gravel, outside its tracks */
    NLANE_SHOULDER, /* a highway's edge, outside its traffic lanes */
    NLANE_ONRAMP,   /* a ramp's lane leading to a highway */
    NLANE_OFFRAMP,  /* a ramp's lane leading away from a highway */
    NLANE_CONNRAMP, /* an interchange's ramp lane from one highway to another */
    NLANE_TYPES
};
/*  A lane type's name, as OpenDRIVE spells it. */
const char *network_lane_type_name(int type);
/*  Whether a lane of a type is a lane gained or dropped: an entry or an
 *  exit lane. */
static inline int nlane_is_change(int type)
{
    return type == NLANE_EXIT || type == NLANE_ENTRY;
}

enum
{
    NJUNCTION_DEFAULT = 0,
    NJUNCTION_DIRECT
};
/*  A junction type's name, as OpenDRIVE spells it. */
const char *network_junction_type_name(int type);

/*  What a road's start or end is linked to. */
enum
{
    NLINK_NONE = 0, /* the map's edge */
    NLINK_ROAD,     /* another road, across a seam */
    NLINK_JUNCTION
};
/*  How a drawn shape names a lane (shape_ref).  It gives the kind, and a
 *  key of the place of the lane's section in its road and the lane's id
 *  (NREF_LANE_KEY).  The shape it is drawn under names the road, by
 *  NLINK_ROAD and its id. */
enum
{
    NREF_LANE = 3
};
#define NREF_LANE_KEY(section, id) (((uint64_t)(uint32_t)(section) << 8) | (uint64_t)(uint8_t)(int8_t)(id))
#define NREF_LANE_SECTION(key)     ((int)((key) >> 8))
#define NREF_LANE_ID(key)          ((int8_t)(uint8_t)((key) & 0xFFu))

typedef struct
{
    int kind;    /* NLINK_* */
    int index;   /* the road or the junction */
    int contact; /* for a road: 0 its start, 1 its end */
} NLink;

typedef struct
{
    uint64_t id;       /* by place: its topology road's, or its junction's and its two arms' */
    int      net;      /* RNET_* */
    int      topo;     /* its topology road, or -1 for a connecting road */
    int      junction; /* the junction it is a connecting road of, else -1 */
    NLink    pred, succ;
    float    length; /* provisional, in units */
    int      sec0, nsec;
} NRoad;

typedef struct
{
    float   s;
    int     lane0, nlane;  /* its lanes, from the leftmost id down to the rightmost */
    uint8_t cls;           /* a street's class over it: 0 a street, 1 an avenue, 2 a boulevard */
    float   offset;        /* OpenDRIVE's laneOffset: where lane 0 lies across the reference line */
    float   offset1, ease; /* the offset it eases to, over `ease` units from its start */
} NSection;

typedef struct
{
    int     road, section;
    int8_t  id;
    uint8_t type;           /* NLANE_* */
    uint8_t change;         /* where traffic may change lanes: NCHANGE_* */
    int     width0, nwidth; /* its width records (NWidth), in order along its section */
} NLane;

/*  A lane's width over part of its section: OpenDRIVE's `width`.  It
 *  holds from `s`, its sOffset along the section, to the next record's.
 *  There the width is a + b ds + c ds^2 + d ds^3, with ds counted from
 *  `s`.  The last record holds to the section's end. */
typedef struct
{
    float  s;
    double a, b, c, d;
} NWidth;

/*  Where a lane's traffic may change into the lane beside it: OpenDRIVE's
 *  laneChange.  Along a road, a traffic lane may change into a traffic
 *  lane beside it that runs the same way.  Inside a junction no lane may. */
enum
{
    NCHANGE_INNER = 1, /* into the lane nearer the reference line */
    NCHANGE_OUTER = 2  /* into the lane farther from it */
};

typedef struct
{
    uint64_t id;   /* its topology end's */
    int      type; /* NJUNCTION_* */
    int      end;  /* its topology end */
    int      conn0, nconn;
} NJunction;

/*  A connection: traffic arriving by `incoming` goes on through
 *  `connecting`, a road of the junction, to `outgoing`. */
typedef struct
{
    int incoming, connecting, outgoing;
} NConnection;

/*  A junction's control on one arm (plans/network.md): NCONTROL_*.  The
 *  arm is the topology road that reaches the junction, by the contact it
 *  reaches it at. */
enum
{
    NCONTROL_NONE = 0,
    NCONTROL_STOP,   /* a street junction's stop */
    NCONTROL_SIGNAL, /* a street junction's signal */
    NCONTROL_RAILWAY /* a level crossing's road arm: it yields to the railway */
};
typedef struct
{
    int     junction, road, contact;
    uint8_t control;
} NControl;

/*  A ramp.  It has its cell and its road, the road whose end crosses the
 *  edge between the ramp's cell and the band junction beside it.  It also
 *  has the band road its lane joins or leaves.  Each road is a topology
 *  road, by the contact it reaches a junction at. */
typedef struct
{
    int32_t cell;               /* the ramp's own cell */
    int     road, road_contact; /* the ramp's road, by its contact at the band junction */
    int     band_junction;      /* the band junction the ramp's road reaches */
    int     band, band_contact; /* -1 where no band road joins it */
    int     heading;            /* the edge the band's traffic on the ramp's side heads by */
    int     on;                 /* 1 an on ramp, 0 an off ramp */
    int     endon;              /* 1 at the band's end, where it takes the whole carriageway */
    int     built;
    /*  A ramp beside the band has the arriving arm's outermost lane, and
     *  the auxiliary lane's lanes to its node and from it.  It has the
     *  ramp's lane inside the junction and the leaving arm's outermost lane.
     *  Then the auxiliary lane's first lane and its last on the ramp's side.
     *  -1 where it files none.  A side with no room for the auxiliary lane
     *  files only the two outermost lanes. */
    int lane_in, lane_ex, lane_en, lane_ramp, lane_out, aux_first, aux_last;
    int in_road, in_contact, out_road, out_contact;
    /*  A ramp that joins the band at a gore has the band's gore, else -1.
     *  The band's roads arriving at it and leaving it are in_road and
     *  out_road. */
    int gore;
    /*  A ramp beside an interchange's region has the region's end, else -1.
     *  `cell_band` is the band unit beside its cell. */
    int     region;
    int32_t cell_band;
} NRamp;

/*  A lane link: `from` goes on into `to`, both lanes.  Each end names
 *  the lane's contact point the link joins: 0 the start of its section
 *  in `s`, 1 the end.  A traffic lane is joined where it runs out and
 *  where it runs in.  A sidewalk is walked both ways, so its link names
 *  its end outright. */
typedef struct
{
    int     from, to;
    uint8_t from_end, to_end;
} NLaneLink;

/*  A NODE of the lane graph: one point where lane ends meet.  Every lane
 *  end is at one node, and two ends a link joins are at the same node.
 *  A node lies at a road's contact, at a lane section boundary inside a
 *  road, or inside a junction, between its connecting roads alone. */
typedef struct
{
    int   road;      /* the road the topology found that it lies on, else -1 */
    int   contact;   /* that road's contact, or -1 at a section boundary */
    float s;         /* at a section boundary, where along the road */
    int   junction;  /* inside a junction, the junction, else -1 */
    int   nin, nout; /* the traffic lanes that run into it and out of it */
} NNode;

/*  A road's OBJECT: OpenDRIVE's `object` with its `repeat`.  It stands
 *  on its road at `t` across it, from `s` along it for `length`, one every
 *  `distance` (plans/network.md, "the street lamps").  `s`, `length` and
 *  `distance` count the road's units.  `t` is in tiles, positive left of
 *  the line.  `facing` is the side its arm reaches over: -1 toward lower
 *  `t`, 1 toward higher, 0 both ways.  A bridge is OpenDRIVE's `bridge`
 *  of the road's objects: the road from `s` for `length` runs on a bridge.
 *  It has no repeat, so its `distance` is 0 (plans/network.md, "the
 *  bridges"). */
enum
{
    NOBJ_LAMP_POST = 0, /* a post-top lamp */
    NOBJ_LAMP_COBRA,    /* a cobra head on a davit arm */
    NOBJ_LAMP_TWIN,     /* a cobra head each way, on a median */
    NOBJ_BRIDGE,        /* a bridge: the road's units it spans */
    NOBJ_TYPES
};
typedef struct
{
    int     road;
    uint8_t type; /* NOBJ_* */
    int8_t  facing;
    float   s, length, distance, t;
    float   height; /* in meters */
} NObject;

/*  Build the road network from the topology built last, for a city's
 *  view: its traffic and the side its world drives on.  0 on success. */
int network_build(const RCity *c);

/*  Whether a lane is in the lane graph: traveled by traffic, or walked.
 *  A median and a border are neither. */
int network_lane_in_graph(int lane);

/*  A lane's width `ds` units along its section from its start: its
 *  width record there (NWidth), with `ds` held to its section. */
float network_lane_width(int lane, float ds);
/*  A lane's width records, `n` of them, in order along its section. */
const NWidth *network_lane_widths(int lane, int *n);
/*  Whether a lane has a width anywhere along its section. */
int network_lane_wide(int lane);

/*  Where a lane lies across its road `ds` units along its section.  It
 *  gives the border nearer the reference line and the border farther
 *  from it, as t, positive left of the line. */
void network_lane_t(int lane, float ds, float *t_in, float *t_out);

/*  Whether a lane's traffic runs with its road's `s`. */
int network_lane_with_s(int lane);

/*  A section's whole width `ds` units along it: the sum of its lanes'. */
float network_section_width(int sec, float ds);

/*  The whole width a road of a family and class takes, in tiles. */
float network_width(int net, int cls);

/*  The fixed width a lane of a type takes on a road of a family, in
 *  tiles.  It is 0 for the type that fills its road. */
float network_type_width(int net, int type);

/*  The section of a road `s` units along it. */
int network_section_at(int road, float s);

/*  A road's whole width `s` units along it: its section's there. */
float network_road_width(int road, float s);

int                network_roads(void);
const NRoad       *network_road(int i);
const NSection    *network_section(int i);
int                network_lanes(void);
const NLane       *network_lane(int i);
int                network_junctions(void);
const NJunction   *network_junction(int i);
const NConnection *network_connection(int i);
int                network_ramps(void);
const NRamp       *network_ramp(int i);
/*  1 where the world drives on the right, -1 on the left: a lane whose
 *  id times this is negative travels with `s`. */
int              network_side(void);
int              network_controls(void);
const NControl  *network_control(int i);
int              network_objects(void);
const NObject   *network_object(int i);
int              network_links(void);
const NLaneLink *network_link(int i);
int              network_nodes(void);
const NNode     *network_node(int i);
/*  The node at one end of a lane: 0 the start of its section in `s`, 1
 *  the end.  A traffic lane runs from its start to its end where it
 *  travels with `s`. */
int network_lane_node(int lane, int end);
/*  THE LINKS AT A LANE'S END, in the order they were filed.  `it` is -1
 *  for the first.  Answers the next, or -1 after the last.  It gives the
 *  lane and the end across the link, and whether the link runs into this
 *  end (1) or out of it (0). */
int network_end_link(int lane, int end, int it, int *other, int *other_end, int *into);
/*  The part a node of an interchange's region plays on its outermost lane. */
enum
{
    NNODE_RAMP = 0, /* where a city ramp joins or splits */
    NNODE_TURN,     /* where the turns split or join */
    NNODE_BETWEEN,  /* where two lanes join just before a split */
    NNODE_INNER,    /* where an arm with no way straight on takes or gives its inner lanes */
    NNODE_WEAVE,    /* where a cloverleaf's weave starts or ends */
    NNODE_PARTS
};
/*  Where a node of an interchange's region lies: on the outermost lane of
 *  the highway arm `road` at its contact `contact`.  That is the lane
 *  leaving the region where `leaving`, else the one arriving.  `part` is
 *  its NNODE_* part, and `rank` its place among that lane's nodes of the
 *  part, from the gore.  `order` is its place among all that lane's nodes,
 *  from the gore, from 1.  0 where junction `j` is no such node. */
int network_node_chain(int j, int *road, int *contact, int *leaving, int *part, int *rank, int *order);
/*  The end of the interchange's region a band unit's cell is in, else -1. */
int network_cell_region(int32_t cell);

/*  Every lane goes on and is fed, held on the records: the lanes that
 *  fail, said to the dump with `say`.  -1 out of memory.  Each lane's
 *  fault is kept, NFAULT_*, until the next check. */
enum
{
    NFAULT_NONE = 0,
    NFAULT_NO_ON,   /* a lane of travel that goes on into nothing */
    NFAULT_NO_FEED, /* a lane of travel nothing feeds */
    NFAULT_LONE,    /* a sidewalk with an end linked to no lane */
    NFAULT_PLACE    /* a lane whose road's contact is one node with a node inside a junction */
};
int network_check(int say);
int network_lane_fault(int i);
/*  Every road, junction and link, to the dump sink. */
void network_dump(void);

/*  This module's state, saved and loaded whole by the store (store.h),
 *  and everything it owns freed. */
size_t network_state_size(void);
void   network_state_save(unsigned char *p);
void   network_state_load(const unsigned char *p);
void   network_release(void);

#endif /* ARC_NET_NETWORK_H */
