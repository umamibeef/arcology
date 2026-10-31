/*  compose.h: the composition of one build of the world (compose.c). */
#ifndef ARC_COMPOSE_H
#define ARC_COMPOSE_H

#include "mesh/tile.h"

/*  THE WHOLE BUILD, as compose.c sees one.  The order the world is
 *  composed in is compose.c's: which tiles, in what order, and what is
 *  drawn over them.  The build hands it over (build.c). */
typedef struct
{
    RMesh             *m;
    const RCity       *c;
    const RAtlas      *a;
    const RAtlasLevel *l;
    uint8_t            mask_bit;
    int                lines, underground;
    int                rotated;
    int                shaped; /* the road network could be shaped, so its streets are drawn */
    int                rc;     /* what a primitive that failed left behind */
    ShapeId            shape;  /* the one compose.c has open, closed when it opens the next */
} WorldFan;

/*  Compose the world a build handed out: the tops and the field, the
 *  ground, the zone tints over it and the power lines over both.  Answers
 *  0. */
int compose_world(WorldFan *w);
/*  Once the world is composed and the mesh `m` finished, what an edit's
 *  build shows leaving and then coming. */
void compose_shown(RMesh *m);

#endif /* ARC_COMPOSE_H */
