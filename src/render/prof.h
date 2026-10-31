/*  prof.h: what a build's work is, unit by unit (--times), and the
 *  clock it is timed by.  Nothing here decides anything. */
#ifndef ARC_PROF_H
#define ARC_PROF_H

#include <stdint.h>

/*  THE WORK an edit's build does, unit by unit.  A unit is one piece of
 *  work that makes geometry: a sweep or a ground tile.  Each is counted
 *  with the box of the map it covers.  While an edit is built, some units
 *  have a box wholly further than the window from the edited tiles.
 *  These are counted apart: work an edit did not have to do. */
enum
{
    WORK_SWEEP,
    WORK_GROUND,
    WORK_N
};
void prof_work(int unit, float x0, float y0, float x1, float y1);
/*  The count begins again with a build: the edited tiles, first column,
 *  first row, last column, last row, and the window round them.  A NULL
 *  box is a whole build, with nothing counted apart. */
void prof_work_window(const int32_t *box, int window);
/*  The work of the whole build, under --times. */
void prof_work_print(void);

/*  The clock, in milliseconds. */
double prof_now(void);

/*  HOW FAR THE BUILD HAS GOT, for whatever is showing a bar.
 *
 *  The build holds the frame for as long as it takes, so nothing draws
 *  while it runs unless something draws from inside it.  That is what
 *  this is for: compose.c names each step it starts, and
 *  whoever registered a watcher may paint.  The GPU still holds the
 *  world the last build left, so a frame drawn here shows that world
 *  under a moving bar.
 *
 *  No watcher is the ordinary case and costs a null test a step. */
typedef void (*ProfWatcher)(void *ud, const char *step, int i, int n);
void prof_watch(ProfWatcher fn, void *ud);
/*  One step begun, as compose.c names it. */
void prof_step(const char *step, int i, int n);

#endif /* ARC_PROF_H */
