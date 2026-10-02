/*  net/cell.h: what a cell carries.  Which family a tile's piece
 *  belongs to, how it links to its neighbors, and the art the piece
 *  tables are read from. */
#ifndef ARC_NET_CELL_H
#define ARC_NET_CELL_H

#include "pipeline.h"
#include "mesh/mesh.h"

/* ---- the types, knobs and state the pipeline shares --------------------- */
extern const uint8_t *s_check_xbld;               /* piece.c: the last built city's XBLD, for the piece scan */

/* the line art's anchors per edge, read by the piece tables and the meets */
extern const float SIDE_MU[4];

extern const float SIDE_MV[4];

extern const float SIDE_DU[4];

extern const float SIDE_DV[4];

int   piece_family(uint8_t b, Family *f);

/*  A carrier: a tile a line runs on into rather than stopping at, as
 *  arc.bytes("carrier_tiles") names it. */
int net_carrier(uint8_t b);

int net_line_lapped(uint8_t b);

int net_thread_lap(uint8_t b);  /* a lap the second family is part of, on either axis (net/cell.c) */

/*  What line a building byte carries, as arc.bytes("line_tiles") reads the
 *  city: on the tile itself, or under a slab standing on it. */
int net_line_on(uint8_t b);

int net_line_near(uint8_t b);

int   piece_second(uint8_t b, Family *f);

/*  Which edges a piece joins.  A slope piece answers from the terrain.
 *  Every other reads the line art, which a script read for it. */
int   piece_links(int piece, uint8_t xter);

int   tile_links(const RCity *c, int32_t col, int32_t row, Family want);

int   link_count(int links);

int   eff_links(const RCity *c, int32_t col, int32_t row, Family f);

#endif /* ARC_NET_CELL_H */
