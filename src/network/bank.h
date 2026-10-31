/*  network/bank.h: the ground drawn beside a street (plans/terrain.md, "The
 *  ground beside a road").
 *
 *  A street's height is its own (network/shape.h), so the ground drawn
 *  beside it moves to meet it.  The land itself is never written.  Under a
 *  street the drawn ground meets the street.  Beside it the ground is
 *  graded on both sides: within BANK_REACH of the edge, it goes from the
 *  street's height to the land's own height.  A street junction lies on
 *  its own tile.  Heights are in levels and positions in tiles, as in shape.h. */
#ifndef ARC_NET_BANK_H
#define ARC_NET_BANK_H

#include <stddef.h>

#include "city.h"

/*  What the drawn ground is at a point. */
enum
{
    BANK_LAND = 0, /* the land's own height */
    BANK_ROAD,     /* under a street: the street's height */
    BANK_GRADED    /* a bank graded from a street's edge to the land */
};

/*  Work out the ground beside every street, from the shape built last
 *  and a city's land and buildings.  0 on success, -1 out of memory. */
int bank_build(const RCity *c);

/*  The ground a drawn surface of height z leaves at a point e tiles past
 *  its edge, over ground of height `ground`.  The ground lies `sink` under
 *  the surface, and on for `margin` past its edge.  From there it grades
 *  back to its own height over the reach. */
double bank_grade(double z, double e, double ground, double margin, double sink);
/*  The ground the streets leave at a point: each street within reach
 *  grades it, up to its ends, and the lowest grade wins.  So the ground
 *  is continuous where the streets' banks meet.  `ground` where none
 *  reaches. */
double bank_ground(double x, double y, double ground, double margin, double sink);

/*  A street's width, and how far past its edge the ground is graded, in
 *  tiles. */
double bank_width(void);
double bank_reach(void);

/*  The rule, held on what was worked out, said to the dump with `say`.
 *  The count of faults. */
int bank_check(int say, double margin, double sink);

/*  This module's state, saved and loaded whole by the store (store.h),
 *  and everything it owns freed. */
size_t bank_state_size(void);
void   bank_state_save(unsigned char *p);
void   bank_state_load(const unsigned char *p);
void   bank_release(void);

#endif /* ARC_NET_BANK_H */
