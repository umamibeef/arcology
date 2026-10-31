/*  net_cell.c: what one cell carries for the networks, as the original
 *  reads its bytes.  Addresses point into CODE 2.  The fitted pieces
 *  take their edges from the fitters' own tables.  Each fixed piece takes
 *  its edges from the placer that writes it. */
#include "net_cell.h"

#include "sim.h"

enum
{
    COL_AXIS  = NET_EDGE_COL_BEFORE | NET_EDGE_COL_AFTER,
    ROW_AXIS  = NET_EDGE_ROW_BEFORE | NET_EDGE_ROW_AFTER,
    ALL_EDGES = COL_AXIS | ROW_AXIS
};

/*  A fitter writes its first id plus NET_PIECE[mask] on flat ground, so
 *  a flat piece joins the edges of every mask that chooses it.  A lone
 *  tile and a dead end take a straight piece, which joins both edges of
 *  its axis.  A piece on a slope is NET_SLOPE_PIECE[slope] for slopes 1
 *  to 4.  It joins the two edges along the slope, and NET_JOIN_X is zero
 *  for a slope that the edges across columns do not join. */
static uint8_t net_piece_edges(int piece)
{
    int     m, ter;
    uint8_t edges = 0;
    for (ter = 1; ter <= 4; ter++)
        if (NET_SLOPE_PIECE[ter] == piece)
            return NET_JOIN_X[ter] ? COL_AXIS : ROW_AXIS;
    for (m = 0; m < 16; m++)
        if (NET_PIECE[m] == piece)
            edges = (uint8_t)(edges | m);
    return edges;
}

static int part(NetPart *out, int n, int net, int edges, int kind, int toward)
{
    out[n].net    = (uint8_t)net;
    out[n].edges  = (uint8_t)edges;
    out[n].kind   = (uint8_t)kind;
    out[n].toward = (uint8_t)toward;
    return n + 1;
}

/*  The axis bit the bridge placer ($1AB20) and the span placer ($1B61A)
 *  set when the way runs along the rows. */
static int span_axis(uint8_t xbit)
{
    return (xbit & 0x02) ? ROW_AXIS : COL_AXIS;
}

/*  The ramp placer $3B98 picks the id from the edge the road takes.  It
 *  sets XBIT 0x02 when that edge is across a column ($3D46, $3DEA).  Each
 *  entry is {road edge, band edge}, first with the bit clear and then
 *  with it set. */
static const uint8_t RAMP_EDGES[2][4][2] = {
    {{NET_EDGE_ROW_AFTER, NET_EDGE_COL_BEFORE}, /* 0x5D: $3DC2 */
     {NET_EDGE_ROW_BEFORE, NET_EDGE_COL_BEFORE}, /* 0x5E: $3E62 */
     {NET_EDGE_ROW_BEFORE, NET_EDGE_COL_AFTER}, /* 0x5F: $3E74 */
     {NET_EDGE_ROW_AFTER, NET_EDGE_COL_AFTER}}, /* 0x60: $3DD6 */
    {{NET_EDGE_COL_AFTER, NET_EDGE_ROW_BEFORE}, /* 0x5D: $3E30 */
     {NET_EDGE_COL_BEFORE, NET_EDGE_ROW_BEFORE}, /* 0x5E: $3D8E */
     {NET_EDGE_COL_BEFORE, NET_EDGE_ROW_AFTER}, /* 0x5F: $3D7A */
     {NET_EDGE_COL_AFTER, NET_EDGE_ROW_AFTER}}, /* 0x60: $3E1E */
};

/*  A tunnel mouth, 0x3F to 0x42, is written as 0x3E plus the slope it
 *  stands on ($20622).  The way goes into the hill toward the high edge
 *  and leaves the mouth on the edge across from it. */
static const uint8_t MOUTH_INTO[4] = {NET_EDGE_ROW_BEFORE, NET_EDGE_COL_BEFORE, NET_EDGE_ROW_AFTER, NET_EDGE_COL_AFTER};

/*  A crossing carries a piece of each of two networks, each straight and
 *  across the other.  A placer writes it over a straight piece of the
 *  other network.  The placers are power $1F196, road $1F4AC, rail
 *  $1F9DA, and a band over a line $1D23A.  Each entry is {first network, its axis, second
 *  network}.  The second runs across the first.  Power is given no axis
 *  by its placer, and it takes the one across the other network. */
static const uint8_t CROSSING[14][3] = {
    {NETW_ROAD, COL_AXIS, NETW_POWER}, /* 0x43 */
    {NETW_ROAD, ROW_AXIS, NETW_POWER}, /* 0x44 */
    {NETW_ROAD, COL_AXIS, NETW_RAIL }, /* 0x45 */
    {NETW_ROAD, ROW_AXIS, NETW_RAIL }, /* 0x46 */
    {NETW_RAIL, COL_AXIS, NETW_POWER}, /* 0x47 */
    {NETW_RAIL, ROW_AXIS, NETW_POWER}, /* 0x48 */
    {NETW_BAND, COL_AXIS, 0         }, /* 0x49: a slab alone */
    {NETW_BAND, ROW_AXIS, 0         }, /* 0x4A: a slab alone */
    {NETW_BAND, COL_AXIS, NETW_ROAD }, /* 0x4B */
    {NETW_BAND, ROW_AXIS, NETW_ROAD }, /* 0x4C */
    {NETW_BAND, COL_AXIS, NETW_RAIL }, /* 0x4D */
    {NETW_BAND, ROW_AXIS, NETW_RAIL }, /* 0x4E */
    {NETW_BAND, COL_AXIS, NETW_POWER}, /* 0x4F */
    {NETW_BAND, ROW_AXIS, NETW_POWER}, /* 0x50 */
};

/*  The band on a hill slope ($1C826, kinds 4 to 7): its axis and its high
 *  edge. */
static const uint8_t INCLINE[4][2] = {
    {ROW_AXIS, NET_EDGE_ROW_BEFORE}, /* 0x61: $1CBDA */
    {COL_AXIS, NET_EDGE_COL_BEFORE}, /* 0x62: $1CC30 */
    {ROW_AXIS, NET_EDGE_ROW_AFTER }, /* 0x63: $1CC86 */
    {COL_AXIS, NET_EDGE_COL_AFTER }, /* 0x64: $1CCDA */
};

/*  The band through a right angle, 0x65 to 0x68: the unit masks 3, 6, 12
 *  and 9 that choose it (A5-0x4510). */
static const uint8_t CURVE[4] = {3, 6, 12, 9};

/*  A rail end over a subway station, 0x6C to 0x6F ($3E9C): 0x6C plus the
 *  step to its rail in the order of A5-0x65E4 and A5-0x65DC. */
static const uint8_t TO_SUBWAY[4] = {NET_EDGE_ROW_AFTER, NET_EDGE_COL_AFTER, NET_EDGE_ROW_BEFORE, NET_EDGE_COL_BEFORE};

int net_cell_parts(uint8_t xbld, uint8_t xbit, NetPart out[2])
{
    int b = xbld;
    if (b >= 0x0E && b < 0x1D) /* $1F2FE */
        return part(out, 0, NETW_POWER, net_piece_edges(b - 0x0E), NETP_FITTED, 0);
    if (b >= 0x1D && b < 0x2C) /* $1F632 */
        return part(out, 0, NETW_ROAD, net_piece_edges(b - 0x1D), NETP_FITTED, 0);
    if (b >= 0x2C && b < 0x3B) /* $1FB60 */
        return part(out, 0, NETW_RAIL, net_piece_edges(b - 0x2C), NETP_FITTED, 0);
    if (b >= 0x3B && b < 0x3F) /* the rail fitter's slope feet, $1FC16 to $1FDE0 */
        return part(out, 0, NETW_RAIL, (b & 1) ? ROW_AXIS : COL_AXIS, NETP_SLOPE_FOOT, 0);
    if (b >= 0x3F && b < 0x43)
    {
        int into   = MOUTH_INTO[b - 0x3F];
        int leaves = into & COL_AXIS ? COL_AXIS & ~into : ROW_AXIS & ~into;
        return part(out, 0, NETW_ROAD, leaves, NETP_TUNNEL_MOUTH, into);
    }
    if (b >= 0x43 && b < 0x51)
    {
        const uint8_t *x    = CROSSING[b - 0x43];
        int            axis = x[1], across = axis == COL_AXIS ? ROW_AXIS : COL_AXIS;
        int            n = part(out, 0, x[0], axis, x[0] == NETW_BAND ? NETP_SLAB : NETP_CROSSING, 0);
        if (b == 0x49 || b == 0x4A)
            return n;
        return part(out, n, x[2], across, NETP_CROSSING, 0);
    }
    if (b >= 0x51 && b < 0x59) /* the road bridges, $1A820 types 2 to 4 */
        return part(out, 0, NETW_ROAD, span_axis(xbit), NETP_BRIDGE, 0);
    if (b == 0x5A || b == 0x5B) /* the rail bridge, type 1 */
        return part(out, 0, NETW_RAIL, span_axis(xbit), NETP_BRIDGE, 0);
    if (b == 0x5C) /* the power line over water, type 0 */
        return part(out, 0, NETW_POWER, span_axis(xbit), NETP_BRIDGE, 0);
    if (b >= 0x5D && b < 0x61)
    {
        const uint8_t *e = RAMP_EDGES[(xbit & 0x02) ? 1 : 0][b - 0x5D];
        int            n = part(out, 0, NETW_ROAD, e[0], NETP_RAMP, 0);
        return part(out, n, NETW_BAND, e[1], NETP_RAMP, 0);
    }
    if (b >= 0x61 && b < 0x65)
        return part(out, 0, NETW_BAND, INCLINE[b - 0x61][0], NETP_INCLINE, INCLINE[b - 0x61][1]);
    if (b >= 0x65 && b < 0x69)
        return part(out, 0, NETW_BAND, CURVE[b - 0x65], NETP_CURVE, 0);
    if (b == 0x69) /* its arms come from the units beside it, $1C3A8 */
        return part(out, 0, NETW_BAND, 0, NETP_INTERCHANGE, 0);
    if (b == 0x6A || b == 0x6B) /* $1B5F2, $1B604 */
        return part(out, 0, NETW_BAND, span_axis(xbit), NETP_SPAN, 0);
    if (b >= 0x6C && b < 0x70)
        return part(out, 0, NETW_RAIL, TO_SUBWAY[b - 0x6C], NETP_TO_SUBWAY, 0);
    return 0;
}

const char *net_cell_net_name(int net)
{
    static const char *const NAME[NETW_COUNT] = {"power", "road", "rail", "highway", "subway", "pipe"};
    return net >= 0 && net < NETW_COUNT ? NAME[net] : "?";
}
