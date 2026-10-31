/*  network/topology.h: layer 1 of the road network (plans/network.md).
 *
 *  From the cells alone, through layer 0 (rcity_parts), and before any
 *  geometry.  A UNIT is what a network is laid in.  It is one cell for
 *  the road and the rail networks.  For a band it is two cells by two, at
 *  an even row and col.  Two units are LINKED across an edge where both
 *  claim it.  A unit of two links is plain.  Every other unit is a
 *  JUNCTION unit.  It is a dead end, a node of three links or more, or an
 *  interchange.  It is also a level meet.  A ramp's cell links across its
 *  edge to the band, so it is the first unit of the ramp's road.  That road
 *  ends at a gore there, or at the band unit's end.  Junction units of one network and one kind that link
 *  merge into one END of the topology, and a dead end never merges.  A
 *  ROAD is a run of plain units between two ends.  It can also be a road
 *  of no units between two ends side by side.
 *
 *  Every end and every road is named by where it is.  The topology is
 *  built again from the whole map at the start of each build. */
#ifndef ARC_NET_TOPOLOGY_H
#define ARC_NET_TOPOLOGY_H

#include <stddef.h>

#include <stdint.h>

#include "city.h"

/*  What an end is. */
enum
{
    END_NODE = 0, /* junction units with a node, an interchange or a band unit a ramp joins */
    END_DEAD,     /* junction units that are dead ends alone */
    END_MEET,     /* level meets alone: a road and a railway across each other on a cell */
    END_SEAM,     /* the border of a chunk between two units with two links each */
    END_MAP_EDGE, /* a link that leaves the map */
    END_GORE,     /* where a ramp leaves or joins a band: a cut of the band's road at a border of the
                     band unit the ramp's cell stands beside, of the ramp's road at the edge to it, or of
                     a band arm at the border of an interchange */
    END_KINDS
};
/*  An end kind's name. */
const char *topology_end_name(int kind);

typedef struct
{
    uint64_t id;    /* by place: its kind and its first cell */
    int      net;   /* RNET_*, or -1 for an end that holds units of two networks */
    int      kind;  /* END_* */
    int      cell0; /* its cells in topology_cell: each the first cell of a unit */
    int      ncell;
    int8_t   edge; /* for a seam and a map edge, the edge it lies across, seen from its first cell, else 0 */
} TopoEnd;

typedef struct
{
    uint64_t id;    /* by place: its first unit and the edge it leaves its first end by */
    int      net;   /* RNET_* */
    int      unit0; /* its units in order, in topology_cell, each the first cell of a unit */
    int      nunit;
    int      end[2]; /* its two ends, at its first unit and at its last */
    /*  The unit of each end the road touches.  It is -1 for a seam and a
     *  map edge, which have no unit of their own. */
    int32_t at[2];
    /*  The edge of the first unit toward end 0, and of the last toward
     *  end 1.  A road of no units has the edge each end's unit leaves it
     *  by. */
    int8_t edge[2];
} TopoRoad;

/*  Build the topology of a city's cells.  0 on success. */
int topology_build(const RCity *c);

int             topology_ends(void);
const TopoEnd  *topology_end(int i);
int             topology_roads(void);
const TopoRoad *topology_road(int i);
/*  A cell index (row * side + col) from an end's or a road's list. */
int32_t topology_cell(int i);

/*  Cells a side of the map the topology was built for. */
int32_t topology_side(void);
/*  Whether a network's unit at a cell links across an edge (NET_EDGE_*).
 *  It does where the pieces on both of the edge's cells join it, or
 *  where it leads off the map.  A band's unit is named by any of its
 *  cells. */
int topology_linked(int net, int32_t cell, int edge);
/*  A ramp's two edges: the edge its piece joins the road network by, and
 *  the edge it joins the band by.  0 for a cell that is no ramp. */
int topology_ramp_edges(int32_t cell, int *road_edge, int *band_edge);
/*  The way a ramp's lane runs, from its cell (plans/network.md, "A
 *  ramp's side"): `heading` is the edge the band's traffic on its side
 *  heads toward.  `on` is 1 for an on ramp.  0 for a cell that is no
 *  ramp, or whose road edge is not along the band. */
int topology_ramp_way(int32_t cell, int *heading, int *on);
/*  The end of a ramp's gore on the band, a taper along it from the ramp's
 *  unit, else -1.  Its first cell is the unit nearer the ramp. */
int topology_ramp_gore(int32_t cell);

/*  Every end and every road, to the dump sink. */
void topology_dump(void);

/*  This module's state, saved and loaded whole by the store (store.h),
 *  and everything it owns freed. */
size_t topology_state_size(void);
void   topology_state_save(unsigned char *p);
void   topology_state_load(const unsigned char *p);
void   topology_release(void);

#endif /* ARC_NET_TOPOLOGY_H */
