/*  inspect.h: what the inspector holds between frames (inspect.c). */
#ifndef ARC_APP_INSPECT_H
#define ARC_APP_INSPECT_H

#include <stdint.h>

#include "net_pick.h"

typedef struct
{
    int      on;       /* the inspector is up: what is under the pointer is outlined and reported on */
    uint32_t logged;   /* the shape last said to the log: a shape is said once, not once a frame */
    NetPick  net;      /* the road network's record shown (app/net_pick.h) */
    int      net_ok;   /* `net` holds a record */
    int      thing_ok; /* the object shown is no record of the network but the shape `thing` */
    uint32_t thing;
    uint32_t shown;      /* the shape the object shown was found through */
    int      pinned;     /* a click holds the object shown where it is until the next click or Escape */
    int      level;      /* the level the player last stepped to, NPICK_*: a new pick keeps it where it can */
    int      level_set;  /* the player has stepped to a level: until then a pick shows its lane */
    float    mx, my;     /* the pointer the object shown was found for, so it is found again only on a move */
    float    click[2];   /* where the last click was: a second one there selects what lies under the first */
    int      depth;      /* how far down the stack under the pointer the object shown lies, from the top */
    char     text[8192]; /* what the records say of it, made again when it changes */
    int      remarks;    /* how many remarks this run has filed */
} Inspector;

#endif /* ARC_APP_INSPECT_H */
