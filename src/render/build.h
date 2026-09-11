/*  build.h: what running a build offers the rest of the renderer.
 *
 *  Four things, and no more: start one, take the next pass, ask how it
 *  went, and ask which mesh the last one wrote into. */
#ifndef ARC_BUILD_H
#define ARC_BUILD_H

#include "pipeline.h"

/*  The whole world in world units.  Answers 0, or non-zero with the
 *  reason already reported. */
int          build_world(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines);
/*  The next pass, set up and handed over as a `world` for the script to
 *  compose.  NULL when the build has no more. */
void        *build_pass_next(void);
/*  How the passes went, for the door to answer with. */
int          build_rc(void);
/*  The mesh the last build wrote into: what a query handed no mesh of
 *  its own asks. */
const RMesh *build_mesh(void);
/*  The pass in hand, as the script is given it.  mesh/mesh.c fills it. */
extern WorldFan s_world;

/*  What every pass of the build has to say about what it laid, asked
 *  once a run rather than from inside a check. */
void build_reports(void);

/*  HOW FAR THE BUILD HAS GOT, for whatever is showing a bar.
 *
 *  The build holds the frame for as long as it takes, so nothing draws
 *  while it runs unless something draws from inside it.  That is what
 *  this is for: the composing script names each step it starts, and
 *  whoever registered a watcher may paint.  The GPU still holds the
 *  world the last build left, so a frame drawn here shows that world
 *  under a moving bar.
 *
 *  No watcher is the ordinary case and costs a null test a step. */
typedef void (*BuildWatcher)(void *ud, const char *step, int i, int n);
void build_watch(BuildWatcher fn, void *ud);
/*  One step begun, as the script names it. */
void build_step(const char *step, int i, int n);

#endif /* ARC_BUILD_H */
