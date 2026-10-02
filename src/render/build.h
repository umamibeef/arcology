/*  build.h: what running a build offers the rest of the renderer.
 *
 *  Four things, and no more: start one, take the next pass, ask how it
 *  went, and ask which mesh the last one wrote into. */
#ifndef ARC_BUILD_H
#define ARC_BUILD_H

#include "pipeline.h"
#include "mesh/internal.h"

/*  The whole world in world units.  Answers 0, or non-zero with the
 *  reason already reported.
 *
 *  `underground` builds the underground view's ground instead.
 *
 *      Every tile at the ground field.
 *      The seabed under water.
 *      No water.
 *      No walls.
 *      No plinths.
 *      For the white ground with the hairline grid.
 *
 *  `rotated` builds for the free rotation.  Every water surface becomes
 *  a mesh face, and the cut runs on all four edges, because any side may
 *  face the camera.
 *
 *  `lines` adds the line strips on the surface.
 *
 *      One constant-width band per line tile along the piece's connections.
 *      Bent on a quarter circle at a corner.
 *      In place of the line sprites. */
int          build_world(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines);
/*  The next pass, set up and handed over as a `world` for the script to
 *  compose.  NULL when the build has no more. */
void        *build_pass_next(void);
/*  How the passes went, for the door to answer with. */
int          build_rc(void);
/*  The mesh the last build wrote into, or NULL before the first one:
 *  what a query handed no mesh of its own asks (src/script). */
const RMesh *build_mesh(void);
/*  The pass in hand, as the script is given it.  build_pass_begin fills
 *  it, and the drive hands it over. */
extern WorldFan s_world;
/*  One pass begun: the stores emptied and the world handed over. */
int  build_pass_begin(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines);
/*  The field for the pass in hand, built when the script says the tops
 *  are settled. */
void build_pass_field(void);
/*  The grading pass skips what only the drawing needs: junction boxes,
 *  lanes, spurs, transitions.  --grade-all runs them anyway. */
int  grade_only(int allowed);
/*  The width compensation this build lofts with. */
int  build_compensate(void);
/*  The network passes, as primitives the script asks for by name: 0 the
 *  lane model, 1 the networks' measure, 3 their drawing, else the bands. */
int  build_world_nets(WorldFan *w, int what);

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
