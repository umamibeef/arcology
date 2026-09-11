/*  model.h: the props, as shapes the scripts build.
 *
 *  A model is a Lua file of its own under scripts/models.  It carries
 *  its own parameters.  Nothing outside the file names them.  And a
 *  build function that turns them into the pieces the prop is made of,
 *  in the prop's own frame.  Asked for a prop of a given size, the model
 *  RETURNS its pieces: a signal's arm reaches over the junction it
 *  stands at.  So what it answers for one junction is not what it
 *  answers for another.
 *
 *  Nothing here ships a shape and nothing here holds a number.  Take the
 *  scripts away and there are no props at all.  What C keeps is the walk
 *  that turns the pieces into faces.  A cache of the last size each
 *  model was asked for, so a prop drawn every frame is built once. */
#ifndef R_NET_MODEL_H
#define R_NET_MODEL_H

typedef enum
{
    M_BOX = 0, /* a box in the prop's frame                                   */
    M_ARM,     /* a box spanning across from `ac2` to `ac`, `d` of it lapping */
    M_LENS,    /* three lenses down from z0, one every z1                     */
    M_FACE,    /* one flat face, w its half span and z0 the height of its middle */
    M_PRISM    /* a box along the facing, cut on the tile folds.  `d` is its length.  `ac` and `ac2` say how far its near and far ends' feet stand over the surface the prop is put on */
} ModelKind;

typedef struct ModelPart
{
    int   kind;
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
 *  along the way it faces and across it.  A signal is put at the tile's
 *  middle and stands itself out at the lip.  So where it goes is the
 *  model's business and not the caller's. */
typedef struct ModelHead
{
    float slot, lift, ax, ac;
} ModelHead;

/*  How many models the scripts define, and what each is called. */
int         net_model_count(void);
const char *net_model_name(int i);
int         net_model_find(const char *name);

/*  The pieces one model is made of at that size.  The script that
 *  defines it builds them.  They are kept until it is asked for another
 *  size or the scripts are read again.  Answers how many pieces there
 *  are. */
int net_model_shape(int model, float size, const ModelHead **head, const ModelPart **parts);

/*  Draw one model at (x, y), facing (fx, fy), on the ground under it.
 *  `phase` is the prop's own place in the signal's cycle and `group` the
 *  lamp code its lenses start at.  Answers 0, or -1 on a mesh error. */
int net_model_put(int model, RMesh *m, const RCity *c, uint8_t mask_bit, float order,
                  float x, float y, float fx, float fy);
/*  The same, standing on a surface of its own rather than on the ground:
 *  a lamp beside a strip stands on the strip's own height.  This the
 *  ground under the lip is not. */
int net_model_put_on(int model, RMesh *m, const RCity *c, uint8_t mask_bit, float order,
                     float x, float y, float fx, float fy, float size, float phase, float group,
                     float base, float base2, int on_base);

#endif
