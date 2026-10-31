/*  draw/laid.h: what the drawing of the road network laid.  The streets
 *  and the highways' decks each keep their own record of the roads they
 *  drew, and lend it here.  So each drawing reads what the others laid
 *  through this file, and none calls another (laid.c). */
#ifndef ARC_LAID_H
#define ARC_LAID_H

#include <stdint.h>

/*  The streets' record: one byte a road, 1 where the road is drawn, and
 *  each drawn road's cap past each end of its line.  NULL for none. */
void laid_streets(const uint8_t *road, const float *cap, int n);
/*  The decks' record: one byte a road, 1 where the road is a deck. */
void laid_decks(const uint8_t *road, int n);

/*  Whether network road `road` is drawn: a street, or a railway whose every
 *  tile carries a track. */
int laid_road(int road);
/*  Whether a connecting road reaches a drawn road through network road
 *  `road`.  It does where that road is drawn, or is a road of no tiles
 *  between two ends side by side. */
int laid_reach(int road);
/*  How far a drawn road's cap runs past the end of its line at a dead
 *  end.  Contact 0 is its start, and contact 1 its end.  0 where it has
 *  none. */
float laid_cap(int road, int contact);
/*  Whether road `road` is drawn as a deck. */
int laid_deck(int road);
/*  Whether road `road`'s line passes within draw_margin (geo.c) tiles of
 *  the window the build lays.  A drawing draws only such roads, since the
 *  mesh keeps nothing of the rest.  Every road where the build lays the
 *  whole map. */
int laid_near(int road);
/*  Whether a point lies within draw_margin tiles of the window, and
 *  `reach` tiles more.  Always where the build lays the whole map. */
int laid_near_xy(float x, float y, float reach);
/*  The record the open shape draws, and the network it draws
 *  (shape_net): a road's own, or `net` for a junction.  So a triangle
 *  of the road network belongs to the nearest cell of its network. */
void laid_ref_road(int road);
void laid_ref_junction(int junction, int net);
/*  The materials every drawn road shares: the asphalt of a lane traffic
 *  drives, and the concrete of a sidewalk. */
void laid_materials(float *asphalt, float *sidewalk);

#endif /* ARC_LAID_H */
