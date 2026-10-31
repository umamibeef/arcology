/*  draw/rail_shape.h: the railway's footprint, as the track drawing and
 *  the street drawing both read it (rail_shape.c).  Every number is a knob
 *  in geo_defaults. */
#ifndef ARC_RAIL_SHAPE_H
#define ARC_RAIL_SHAPE_H

#include <stdint.h>

#include "mesh/poly.h"
#include "network/network.h"

/*  The most cross-sections one sweep lays at once. */
#define SWEEP_MOST 4096
/*  The most points of a junction's gravel heights. */
#define GRAVEL_PTS 4096

/*  The railway's knobs, read once a draw. */
typedef struct
{
    float foot, bed, height, offset, gauge, rail_h, rail_w, gap, sleeper_half, sleeper_w, sleeper_h, step, sink;
    float stain, dark;                /* the half width of the darker gravel under each track, and how dark it is */
    float buf_half, buf_depth, buf_h; /* a buffer stop's half width, depth and height */
} RailKnobs;
void rail_knobs(RailKnobs *k);
/*  The half width of a railway's gravel at its foot, in tiles. */
float rail_foot(void);
/*  A point of a road's line at s, its heading and its height.  Past either
 *  end of the line, over a cap at a dead end, the line runs straight on
 *  along its heading there, level. */
void rail_way_at(int road, float s, float *x, float *y, float *h, float *z);
/*  A point of a road's way at s, moved t across it, with its height. */
void rail_line_at(int road, float s, float t, float lift, float out[3]);
/*  Where a railway's way runs from and to: its line, and its caps past its
 *  ends (laid_cap). */
float rail_way_from(int road);
float rail_way_to(int road);
/*  The stations of a sweep along a road from s0 to s1, no further apart
 *  than step.  Answers how many. */
int rail_stations(float s0, float s1, float step, float *st);
/*  The side of its road's line a rail lane lies on: its id's sign. */
float rail_lane_side(const NRoad *r, int which);
/*  Whether a connecting road of a railway's junction is drawn. */
int rail_connection_drawn(int i);

/*  A junction's gravel.  It holds the foot, the bed, the band between
 *  them, the darker gravel under each track and the rest of the bed's
 *  top.  It also holds the heights of the foot. */
typedef struct
{
    PolySet foot, bed, band, dark, off, light;
    float   gz[GRAVEL_PTS][3];
    int     ngz;
} RailGravel;
/*  Junction `j`'s gravel worked out.  `kind` is 0 where it has none, 1 for
 *  the foot, bed and band alone, and 2 with the dark and light tops too.
 *  The answer is the footprint's own, until the next call. */
RailGravel *rail_gravel(int j, const RailKnobs *k, int *kind);
/*  The height of the gravel's foot at a point of the junction. */
float rail_gravel_z(const RailGravel *g, float x, float y);
/*  The outer ring of a railway junction's gravel foot, x, y and z a point,
 *  at most `most` points.  Answers how many, 0 where it has none. */
int rail_gravel_outline(int junction, float *xyz, int most);
/*  The foot of a railway's gravel on one side, as the pile lays it.  side
 *  is 1 or -1 across its line.  It puts x and y a point, at most `most`
 *  points.  Answers how many. */
int rail_foot_line(int road, float side, float *xy, int most);

#endif /* ARC_RAIL_SHAPE_H */
