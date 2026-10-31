/*  build.h: what running a build offers the rest of the renderer.
 *
 *  Start one, finish its mesh, ask how it went, and ask what it has to
 *  say.  The road network is derived once a build, before the world is
 *  handed over. */
#ifndef ARC_BUILD_H
#define ARC_BUILD_H

#include "compose.h"

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
 *  `lines` adds the power lines compose.c lays, in place of the line
 *  sprites. */
int build_world(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines);
/*  How the build went, for the door to answer with. */
int build_rc(void);
/*  The world in hand, as compose.c is given it. */
extern WorldFan s_world;

/*  What the build has to say about what it laid, asked once a run rather
 *  than from inside a check. */
void build_reports(void);

#endif /* ARC_BUILD_H */
