/*  model.h: the props, as shapes model.c builds.
 *
 *  A model is a build function in model.c.  It turns its own numbers
 *  into the pieces the prop is made of, in the prop's own frame.  Asked
 *  for a prop of a given size, the model RETURNS its pieces.  What it
 *  answers for one size is not what it answers for another.
 *
 *  Beside the models, model.c keeps the walk that turns the pieces into
 *  faces, and a cache of the last size each model was asked for.  So a
 *  prop drawn every frame is built once. */
#ifndef R_NET_MODEL_H
#define R_NET_MODEL_H

typedef enum
{
    M_BOX = 0, /* a box in the prop's frame                                   */
    M_ARM,     /* a box spanning across from `ac2` to `ac`, `d` of it lapping */
    M_LENS,    /* three lenses down from z0, one every z1                     */
    M_FACE,    /* one flat face, w its half span and z0 the height of its middle */
    M_UNDER,   /* a lamp's lens facing the ground: `d` along the facing, `w` across, at z0 */
    M_PRISM    /* a box along the facing, cut on the tile folds.  `d` is its length.  `ac` and `ac2` say how far its near and far ends' feet stand over the surface the prop is put on */
} ModelKind;

typedef struct ModelPart
{
    int kind;
    /*  `ac2` is the far end's across, for a piece that spans between two
     *  places rather than standing at one.  A signal's mast arm reaches
     *  from the pole to the head.  Both are numbers of their own. */
    float ax, ac, ac2, w, d, z0, z1;
    /*  Where each foot of a prism stands along the run between the two
     *  heights it is put on.  It is 0 at the near one, and 1 at the far.
     *  A car's body spans the whole run and its cabin the middle of it. */
    float f0, f1;
    float mat, code;
    int   uv; /* a face whose vertices carry their own corners, not a phase */
} ModelPart;

#define MODEL_PARTS 32

/*  What a model carries besides its pieces.  Where it sits in the
 *  painter's stack over the tile it stands on, how far over the surface
 *  its feet go.  Where its own origin is from the point it was put at:
 *  along the way it faces and across it.  So a prop put at a tile's
 *  middle can stand itself out at the tile's edge.  Where it goes is the
 *  model's business and not the caller's. */
typedef struct ModelHead
{
    float slot, lift, ax, ac;
} ModelHead;

int net_model_find(const char *name);

/*  Draw one model at (x, y), facing (fx, fy), `size` large.  `phase` is
 *  the prop's own place in a cycle and `group` the lamp code its lenses
 *  start at.  It stands on the ground under it, or at `base` (and `base2`
 *  at its far end) where `on_base` is set.  Answers 0, or -1 on a mesh
 *  error. */
int net_model_put_on(int model, RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy, float size, float phase, float group, float base, float base2, int on_base);

#endif
