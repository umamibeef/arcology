/*  draw/net_graph.h: the lane graph laid out as a diagram on the map.
 *
 *  The layout reads the road network's records alone (network/network.h)
 *  and hands each line and arrow it draws to a sink.  The SVG writer and
 *  the outline view's overlay are two sinks of the one layout, so the two
 *  pictures cannot disagree. */
#ifndef ARC_NET_GRAPH_H
#define ARC_NET_GRAPH_H

#include <stdint.h>

/*  What a mark is drawn in: a lane's type (NLANE_*), and then these. */
enum
{
    NG_FAULT = 5, /* a lane the check fails */
    NG_LINK,      /* a lane link between two ends that do not meet */
    NG_JUNCTION,  /* a junction's cells */
    NG_CELL,      /* a road's units */
    NG_COLORS
};
/*  Each mark's color, as 0xRRGGBB, and its name in a legend. */
extern const uint32_t    NG_RGB[NG_COLORS];
extern const char *const NG_NAME[NG_COLORS];

typedef struct
{
    float x, y;
} NgP;

typedef struct
{
    void *user;
    /*  A line of `n` points in map units, `half` wide each side.  `what`
     *  names it, for the inspector.  0, or -1 to stop. */
    int (*line)(void *user, const char *what, const NgP *p, int n, float half, int color);
    /*  An arrow with its middle at `at`, pointing along the unit `dir`. */
    int (*arrow)(void *user, NgP at, NgP dir, float size, int color);
    /*  A node of the lane graph where it is placed: `inside` is 1 for a
     *  node inside a junction.  NULL where the sink draws no node. */
    int (*node)(void *user, NgP at, int inside);
    /*  1 where the sink draws each junction's cells and each road's
     *  units.  The outline view leaves them out, and the tile marks show
     *  those cells under the lane graph (uiglue.c). */
    int cells;
} NgSink;

/*  Lay out the network last built into the sink.  Answers how many
 *  lanes inside a junction could not be placed, or -1 where the sink
 *  stopped or memory ran out. */
int net_graph_draw(const NgSink *sink);

#endif /* ARC_NET_GRAPH_H */
