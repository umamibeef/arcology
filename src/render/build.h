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

#endif /* ARC_BUILD_H */
