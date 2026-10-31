/*  app/net_pick.h: the road network's record a drawn shape names.  Every
 *  shape the drawing lays names the record it draws (shape_ref).  A road
 *  and a junction are named by their ids.  A strip of a lane, under its
 *  road's shape, is named by the lane (NREF_LANE).  From a shape this
 *  finds that record and the records round it: the lane, its section, its
 *  road and its junction.  It says what the records hold of them.  Nothing
 *  here draws. */
#ifndef ARC_DEV_NET_PICK_H
#define ARC_DEV_NET_PICK_H

#include <stddef.h>
#include <stdint.h>

/*  What can be picked, from the finest up. */
enum
{
    NPICK_LANE = 0,
    NPICK_SECTION,
    NPICK_ROAD,
    NPICK_JUNCTION,
    NPICK_LEVELS
};

typedef struct
{
    int   level;                         /* NPICK_*, the record shown */
    int   road, section, lane, junction; /* network indices, -1 for none */
    float s, t;                          /* the point on the road's line, where it has one */
    float hit[3];                        /* the point the pointer is on */
} NetPick;

/*  The records shape `shape` names, up its parents: its road, its lane
 *  and its junction, each -1 for none.  0 where it names no record the
 *  network holds. */
int net_pick_record(uint32_t shape, int *road, int *lane, int *junction);
/*  The pick of shape `shape` at the point `hit` on it: its records, the
 *  junction of its road, and the point on the road's line.  0 where the
 *  shape names no record. */
int net_pick_at(uint32_t shape, const float hit[3], NetPick *p);
/*  The ids are looked up again: the network was built again. */
void net_pick_forget(void);
/*  Load the network the pick was made in: the network of the chunk its
 *  point lies in (network/store.h).  Every index a pick holds belongs to
 *  that network. */
void net_pick_load(const NetPick *p);
/*  Whether the pick has a record at `level`. */
int net_pick_has(const NetPick *p, int level);
/*  The connecting roads of a junction, by each road's own record of the
 *  junction it is in: at most `max` into `out`.  How many. */
int net_pick_junction_roads(int junction, int *out, int max);
/*  The name of the record at `level`, one line. */
void net_pick_name(const NetPick *p, int level, char *buf, size_t n);
/*  Everything the records say of the pick, from the lane up: lines of
 *  key, TAB, value. */
void net_pick_text(const NetPick *p, char *buf, size_t n);

#endif /* ARC_DEV_NET_PICK_H */
