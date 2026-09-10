/*  sim_int.h: what one file of the simulation asks of another.
 *
 *  sim.h is the simulation's face to the rest of the program: the phases
 *  the clock calls, and the City they act on.  This is narrower, and
 *  private to src/sim.  It names only what crosses between the files
 *  the simulation is made of.  Each of them keeps the rest to
 *  itself.  And it is short on purpose.  Every name here is a place two
 *  phases are coupled.  The list is meant to stay readable. */
#ifndef SIM_INT_H
#define SIM_INT_H

#include "sim.h"

/* THINK C's __sdiv32 ($524) truncates toward zero, which is also what
 * C99 '/' does, so plain division is faithful here.  The 68000 asr used
 * for power-of-two division is NOT the same thing.  It floors, so
 * anywhere the original used asr we shift, and anywhere it called the
 * helper we divide. */
#define ASR(v, n) ((int32_t)(v) >> (n)) /* arithmetic, floors */

/*  The XTHG slot table: forty records of twelve bytes.  sim_thing.c owns
 *  it.  The rest of the simulation reads and writes a thing's fields
 *  directly through thing(), the way the original did. */
#define THING_N  40
#define THING_SZ 12

uint8_t * thing(City *c, int slot);
void free_thing(City *c, int slot);

/*  The four compass steps, and the two turn tables beside them: shared
 *  because a station places a train with the same table the train then
 *  drives by.  A5-0x6226, A5-0x622E, A5-0x61A4, A5-0x61A0. */
extern const int STEP_DY[4], STEP_DX[4], TURN_A[4], TURN_B[4];
#define TRAIN_DY STEP_DY
#define TRAIN_DX STEP_DX

/*  The eight neighbors in the order the original walks them, A5-0x4F4E
 *  and A5-0x4F3C.  The terrain fix and the disasters that spread both
 *  step by them. */
extern const int BEAM_DY[8], BEAM_DX[8];

/*  sim_thing.c: taking a slot, and the spawners.  A disaster takes one
 *  the same way a station does. */
int alloc_thing(City *c);
int spawn_disaster_thing(City *c, int kind);
void spawn_helicopter(City *c, int y, int x);
void spawn_plane(City *c, int y, int x, int kind);
void seaport_ship(City *c, int y, int x);

/*  sim_place.c also spawns: a new station wants a train on it. */
int pick_direction(City *c, int y, int x, int turn, int kind);

/*  sim_queue.c: the shared 512-entry ring.  queueReset $21DD4, queuePush
 *  $21DF2, queuePop $21E3A, queuePopBack $21E66, queuePeekBack $21E96. */
void q_reset(void);
void q_push(int y, int x);
void q_pop(int *y, int *x);
int q_empty(void);
void q_pop_back(int *y, int *x);
void q_peek_back(int *y, int *x);

/*  sim_map.c: a label freed when whatever carried it is destroyed.  A
 *  disaster destroys as thoroughly as the bulldozer does. */
void release_label(City *c, int v);

/*  sim_disaster.c: the yearly roll that may pick one. */
void sim_disaster_roll(City *c);

/*  sim_map.c: the bulldozer's helpers, which placement needs too: it
 *  clears a tile before it builds on one. */
int near_powered(const City *c, int y, int x);
void set_under(City *c, int y, int x, uint8_t und);
void clear_tile(City *c, int y, int x);
void clear_footprint(City *c, int y, int x);
int stamp_footprint(City *c, int y, int x, int bld, int size);

/*  sim_place.c: what growth reaches for when a zone earns it. */
void grow_to_3x3(City *c, int y, int x, int zone);
void auto_marina(City *c, int y, int x);
int tile_fits(const City *c, int y, int x, int alt, int zone, int maxbld);

/*  The age pyramid's head count for one five-year bracket: A5+0x1EDE as
 *  the microsim reads it, three longs to a bracket. */
#define HEADS_AT(c, b) ((c)->misc[MISC_HIST_BASE + 3 * (b)])

/*  sim_micro.c: one XMIC record, and the words inside it.  The budget
 *  reads a plant's remaining life straight out of the table. */
uint8_t * micro_rec(const City *c, int i);
int micro_w(const uint8_t *r, int k);
void micro_set_w(uint8_t *r, int k, int v);
int micro_cap(const City *c, int want, int per);

#endif /* SIM_INT_H */
