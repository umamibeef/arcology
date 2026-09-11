/*  net/traffic.h: what traffic.c answers for.
 *
 *  the moving world: the movers, the signals and the gates.
 *
 *  Nothing declared here decides anything. */
#ifndef ARC_NET_TRAFFIC_H
#define ARC_NET_TRAFFIC_H

#include "net/types.h"

/*  THE BEAT the moving world runs on, as the script drives it.  It holds
 *  the gates to swing, the cars to move, and what each of them sees.  It
 *  also holds the move itself.  net_gate_rest_is takes the one angle the
 *  build's drive asks for: where a gate settles with nothing near it. */
void  net_gate_rest_is(float angle);
void *net_moving_fan(void);
int   net_beat_owed(void);
int   net_beat_draws(void);
/*  One beat, or as much of it as runs before a train reaches a junction.
 *  Answers 1 where it stopped for an arm to be chosen.  Is entered again
 *  once the rule has answered. */
int   net_beat_run(void);
int   net_beat_arms(int *n, float *hx, float *hy);
int   net_beat_arm_at(int k, float *dx, float *dy);
void  net_beat_arm_is(int k);
int   net_beat_build(void);
int   net_beat_gates(void);
int   net_beat_gate(int i, float *angle, float *near, float *dt);
void  net_beat_gate_is(int i, float angle);
int   net_beat_cars(void);
int   net_beat_car(int i);
void  net_signal_phase_is(int k, float phase);
void  net_signal_group_is(int e, float group);
int  net_beat_signal(int32_t *col, int32_t *row, float *hx, float *hy, float *time, int *stagger);
void net_car_density_is(int tv, int cars);
int  net_signals(void);
int  net_signal_at(int i, float *ahead, float *back);
void net_signal_is(int i, const char *model);
int  net_beat_turn(int *n, unsigned *draw);
int  net_beat_turn_at(int k, float *dx, float *dy);
void net_beat_turn_is(int k);
int   net_beat_reading(float *speed, int *have_gap, float *gap, int *have_ctrl, float *ahead, int *held, int *nx,
                       float *stop, float *free_, float *line, float *creep, float *step);
float net_beat_lap(int k);
void  net_beat_car_is(int i, float v);
/*  And the moving world DRAWN: the gate arms the drive lays, and the
 *  mesh they go into. */
int   net_movers_gates(void);
int   net_movers_gate(int i, float *x, float *y, float *fx, float *fy, float *angle, float *len, float *order);
void *net_movers_mesh(const void **city, uint8_t *mask_bit); /* net/traffic.c: the mesh the movers go into, with the city and mask it holds */

#endif /* ARC_NET_TRAFFIC_H */
