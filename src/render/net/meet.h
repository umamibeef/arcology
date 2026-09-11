/*  net/meet.h: what meet.c answers for.
 *
 *  the thread family: its junction, and the level meets with lines.
 *
 *  Nothing declared here decides anything. */
#ifndef ARC_NET_MEET_H
#define ARC_NET_MEET_H

#include "net/types.h"

/*  A LEVEL MEET, in three: the ask gathers it and opens its shape, the
 *  script measures it.  The draw lays the panel and the approaches.
 *  Every measurement follows from the angle the line and the line cross
 *  at.  This is why the script sits between the two. */
int  build_lap(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int32_t col, int32_t row, int second);
void net_lap_ask(int32_t *col, int32_t *row, float *sine, float *line, float *thread);
void net_lap_frame(const ScriptLap *fr);
const LapFan *net_lap_panel(void);
int            net_lap_approaches(void);
int            net_lap_approach(int i, ScriptApproachAsk *out);
float net_lap_order(void);
int            net_lap_place(const ScriptApproach *mk);
int           marking_near_lap(const RCity *c, V2 pos);
int  mesh_signal_add(RMesh *m, float x, float y, float fx, float fy, float s_along, int dir, int absolute);
/*  The threads a thread junction has, for arc.rules.node_threads: which arm
 *  runs into which, and how far each is raised over the last. */
int  net_threads_ask(void);
void net_threads_info(int *col, int *row, int *links);
int  net_thread_want(int from, int to, float raise);
int  net_threads_done(void);
/* ---- thread.c: the thread family */
int seg_measure_laps(Seg *x);
int on_lap_panel(const RCity *c, int32_t tc, int32_t tr, float x, float y);

#endif /* ARC_NET_MEET_H */
