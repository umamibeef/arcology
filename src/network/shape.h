/*  network/shape.h: layer 3 of the road network, the shape (plans/network.md).
 *
 *  Each road of the road network and of the band gets its reference line seen
 *  from above, OpenDRIVE's planView.  It also gets its height along that line,
 *  its elevationProfile.  Each junction gets its boundary.  The shape is
 *  worked out from the network and the land alone.  It adds, removes and
 *  relinks nothing in the network.
 *
 *  A position is in tiles: x along the columns and y along the rows.  A
 *  heading is the angle from the column axis toward the row axis, in radians.
 *  A height is in levels.  A road's `s` along its line starts at nought at its
 *  start and grows to its length. */
#ifndef ARC_NET_SHAPE_H
#define ARC_NET_SHAPE_H

#include <stddef.h>

#include "city.h"

/*  One piece of a reference line: OpenDRIVE's geometry.  A curvature of
 *  nought is a line, and any other value is an arc.  A PARAMPOLY3 is a
 *  cubic in the piece's own frame: along its heading from its start (u),
 *  and to its left (v).  It runs over p from 0 to 1 along it
 *  (OpenDRIVE's pRange normalized).  A SPIRAL's curvature changes evenly
 *  along it. */
typedef struct
{
    float s, x, y, hdg, length, curvature;
    int   poly;       /* 1 for a paramPoly3 */
    float u[4], v[4]; /* its cubics: a + b p + c p^2 + d p^3 */
    int   spiral;     /* 1 for a spiral: its curvature runs evenly from `curvature` to `cend` */
    float cend;
} SGeom;

/*  One piece of a height profile: OpenDRIVE's elevation.  The height at
 *  `s + ds` is `a + b ds + c ds^2 + d ds^3`, until the next piece.  A
 *  piece is a straight grade, or a vertical curve (a parabola, d nought)
 *  from the grade before it to the grade after. */
typedef struct
{
    float s, a, b, c, d;
} SElev;

/*  The shape of one road.  A road with no geometry has no shape.  That is a
 *  connecting road, a road of no units, or a road of a network the shape gives
 *  no line. */
typedef struct
{
    float length;       /* the length of its line, in tiles */
    int   geom0, ngeom; /* its pieces of line */
    int   elev0, nelev; /* its pieces of height */
    int   map0, nmap;   /* the `s` along its line at each of its unit boundaries */
} SRoad;

/*  Shape the network built last, over a city's land.  0 on success, -1 out of
 *  memory. */
int shape_build(const RCity *c);

/*  The shape of network road i, as network_road numbers it. */
const SRoad *shape_road(int i);

/*  The point and the heading of a road's line at `s` along it.  0 for a road
 *  with no shape. */
int shape_at(int road, float s, float *x, float *y, float *hdg);
/*  The height of a road at `s` along its line. */
float shape_height(int road, float s);
/*  The land's own height at a point, in levels: the slope of the tile it
 *  lies on, or the surface of the water there.  Built with the shape. */
double shape_land_at(double x, double y);
/*  The `s` along a road's line of an `s` the network counts in units. */
float shape_s(int road, float s_units);

/*  A road's width at `s` along its line, in tiles: the sum of its lanes'
 *  widths there (layer 2). */
float shape_width(int road, float s);

/*  How far along its units a point `s` along a road's line lies: the
 *  inverse of shape_s. */
float shape_units(int road, float s);

/*  Where lanes meet at a link, their borders meet in the world.  At each
 *  end of a lane, the lanes it links with in another section span what
 *  their partners in its own section span.  They meet at one height.  With `say`, the first lane
 *  ends apart are said to the dump, and with `say` 2 every one.  Answers
 *  how many are apart. */
int shape_lane_check(int say);

/*  A highway junction's height, where every arm meets it, in levels.  NAN
 *  for a junction of streets, which has no height of its own yet. */
float shape_junction_height(int junction);
/*  A junction's boundary, as a closed outline of points (x, y), the first not
 *  repeated at the end.  The count of points, or 0. */
int shape_boundary(int junction, const float **xy);
/*  Whether the edge from point k of a junction's boundary to the next is a
 *  mouth.  A mouth is where an arm crosses into the junction, its two ends
 *  the arm's edges.  Otherwise it is a corner between two arms. */
int shape_boundary_mouth(int junction, int k);
/*  Whether point k of a junction's boundary stands on a shaped road's arm:
 *  that road and the contact it reaches the junction by. */
int shape_boundary_arm(int junction, int k, int *road, int *contact);
/*  How far a road's junction reaches along it from its end at `contact`, in
 *  tiles.  Above nought the road is cut back that far.  Below nought the
 *  junction stops short of its end, and the road runs on straight to it. */
float shape_cut(int road, int contact);

/*  1 where the interchange of region end `end` cannot be routed
 *  (plans/interchanges.md, "The shape").  Its roads have no line, and
 *  the drawing stands a placeholder model there. */
int shape_placeholder(int end);

/*  The shape's properties, held on what it filed: said to the dump with `say`.
 *  The count of faults. */
int shape_check(int say);
/*  Profiles read from a file, through the same solves as a city's roads:
 *  each line `street` or `deck` and its samples `s,h`.  The answers go to
 *  the dump sink.  0 on success. */
int shape_profiles(const char *path);
/*  Every road's shape and every junction's boundary, to the dump sink. */
void shape_dump(void);

/*  This module's state, saved and loaded whole by the store (store.h),
 *  and everything it owns freed. */
size_t shape_state_size(void);
void   shape_state_save(unsigned char *p);
void   shape_state_load(const unsigned char *p);
void   shape_release(void);

#endif /* ARC_NET_SHAPE_H */
