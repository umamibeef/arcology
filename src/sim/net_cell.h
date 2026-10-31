/*  net_cell.h: what one cell carries for the networks, as the original
 *  reads its bytes.  This is layer 0 of the road network
 *  (plans/network.md).  A cell carries up to two PARTS, one for each
 *  network on it.  A part says which network it belongs to and which
 *  edges of the cell it joins.  Every answer comes from the original's
 *  fitters and placers, and none from the art.
 *
 *  The edge bits are the fitters' own.  They index NET_PIECE.  An edge
 *  is LINKED where the parts of one network on both its cells join it.
 *  This file says what one cell claims, and the caller pairs the claims. */
#ifndef NET_CELL_H
#define NET_CELL_H

#include <stdint.h>

/*  The networks a part belongs to. */
enum
{
    NETW_POWER = 0,
    NETW_ROAD,
    NETW_RAIL,
    NETW_BAND, /* the raised two-cell way the highway tool lays */
    NETW_SUBWAY,
    NETW_PIPE,
    NETW_COUNT
};
/*  A network's name, "?" for no network. */
const char *net_cell_net_name(int net);

/*  The edges of a cell, as the bits of the join mask the fitters build. */
enum
{
    NET_EDGE_COL_BEFORE = 1, /* toward (row, col - 1) */
    NET_EDGE_ROW_AFTER  = 2, /* toward (row + 1, col) */
    NET_EDGE_COL_AFTER  = 4, /* toward (row, col + 1) */
    NET_EDGE_ROW_BEFORE = 8  /* toward (row - 1, col) */
};

/*  What wrote a part, which says how its edges are read. */
enum
{
    NETP_FITTED = 0,   /* a fitter chose it from its neighbors or its slope */
    NETP_CROSSING,     /* two networks across each other on one cell */
    NETP_SLOPE_FOOT,   /* the rail cell at the foot of a slope piece */
    NETP_TUNNEL_MOUTH, /* `toward` is the edge into the hill */
    NETP_BRIDGE,       /* a span over water */
    NETP_RAMP,         /* the one piece that joins a road and a band */
    NETP_TO_SUBWAY,    /* a rail end over a subway station */
    NETP_STATION,      /* a subway joins it from every side */
    NETP_SLAB,         /* a band on flat ground */
    NETP_INCLINE,      /* a band on a hill slope: `toward` is the high edge */
    NETP_CURVE,        /* a band through a right angle */
    NETP_INTERCHANGE,  /* a band unit whose arms are not in its byte */
    NETP_SPAN          /* a band over water */
};

typedef struct
{
    uint8_t net;    /* NETW_* */
    uint8_t edges;  /* NET_EDGE_* bits */
    uint8_t kind;   /* NETP_* */
    uint8_t toward; /* one NET_EDGE_* bit where the kind names one, else 0 */
} NetPart;

/*  The parts on a cell of the surface, from its XBLD and XBIT bytes.  The
 *  answer is how many, 0 to 2.
 *
 *  A band is laid in units of two cells by two, at an even row and col.
 *  A band part's edges are the edges its unit's arms leave by.  A
 *  ramp's band edge is the edge to the band cell beside it.
 *  For a span (0x6A, 0x6B), `xbit` must be the XBIT of the unit's cell
 *  at (row, col + 1).  The original reads that cell ($1D520).  The
 *  placer at $1B62C sets the bit on the wrong cell of a row unit, so the
 *  other cells cannot be trusted. */
int net_cell_parts(uint8_t xbld, uint8_t xbit, NetPart out[2]);

#endif /* NET_CELL_H */
