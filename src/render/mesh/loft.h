/*  mesh/loft.h: the loft, the sweep of a cross-section along some
 *  pieces, and the strip in flight that the drive's stages read. */
#ifndef ARC_MESH_LOFT_H
#define ARC_MESH_LOFT_H

#include "pipeline.h"
#include "mesh/mesh.h"
#include "mesh/shape.h"

/*  Stations a loft may hold.  8190 is one whole ring band's worth.  A
 *  ring that runs out of stations on its last stretch leaves its spurs
 *  with no slab beside them.  The count moves with the slab's width
 *  through the meets sampled at the tile edges. */
#define LOFT_MAX_ST      32768

typedef struct GroundFan GroundFan;

/*  A strip's elevation over the ground: its stations, the altitude an
 *  end or a level meet pins it to, and the height each is given. */
int   loft_ground_at(const GroundFan *g, int i, float *at, float *z);

int   loft_ground_node(const GroundFan *g, int which, float *z);

int   loft_ground_lap(const GroundFan *g, int i, float *z);

void  loft_ground_set(GroundFan *g, int i, float z);

int loft_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, int comp, const RLoft *desc, const Piece *pc, int np, float total);

#define loft(...) loft_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)

/*  A strip's elevation as the composing script spurs it between the
 *  nodes at its ends.  A node is a junction, a dead end or a level lap.
 *  It stands at its own tile's leveled height.  Every corridor that
 *  reaches it spurs to that one number, so two segments meeting at a
 *  junction agree without anything being solved between them.  Which
 *  stations are anchors, and how the spur runs between them, are the
 *  script's. */
struct GroundFan
{
    void  *smp;
    const void *city;
    int    n;
    float  total;
    int    pin0, pin1;   /* the end reaches its node's own tile */
    int    dead0, dead1; /* and is a dead end, which is pinned whatever the family */
    int    pin_node;     /* the family's junction is not a turnout, so an arm reaches its node */
    float  lift;
};

typedef struct
{
    RMesh       *m;
    const RCity *c;
    uint8_t      mask_bit;
    int          comp;
    Family       f;
    int          pin0, pin1;
    Sample      *smp;
    float       *zraw;
    float        hw, mat, total;
    const Piece *pc;
    int          np;
    int          ns;
    const RLoft *d; /* what the loft was asked to draw, for the stages other modules supply */
} Loft;

/*  THE STRIP IN FLIGHT and the one whose slab is still to be laid.  The
 *  loft (mesh/loft.c) fills them, the drive (net/strip.c) stops at each
 *  of its stages over them. */
extern RLoft   s_ldv;

extern Loft    s_lx;

extern int     s_lx_live;

extern const RLoft *s_ld;              /* the strip being lofted, for both halves */

extern float   s_zorig[];              /* the ground's own line under each station */

extern double  s_lx_tp;                /* when the stage in hand started, for the profile */

void note_add(char *buf, size_t cap, size_t *n, const char *fmt, ...);

/*  One station pair of a strip, as the slab lays it.  What the family
 *  may restyle.  That is the across range, the along offset, the class,
 *  and the material and along of its markings.  And what it may build
 *  beside. */
typedef struct
{
    int           i; /* the pair's index: the quad from station i - 1 to i */
    const Sample *pv, *cu;
    float         ha, hb;                     /* the half widths at either station, compensated */
    float         a0[2], a1[2], b0[2], b1[2]; /* the quad: a right, a1/b1 left */
    int32_t       tc, tr;                     /* the tile under the pair's midpoint */
    float         order;
    float         acr, acl, aoff; /* the across range and the along offset the quad is painted with */
    float         cls;            /* the strip's class here */
    float         ma, al_a, al_b; /* the material and the along at either station */
} LoftPair;

/* ---- mesh/loft.c: the loft */
float section_height(const RCity *c, uint8_t mask_bit, V2 pos, V2 dir, float h);

int   net_record(RNet *net, const Sample *smp, int ns, float total, int cls, const RLoft *d);

float profile_at(const Sample *smp, int ns, float at);

#endif /* ARC_MESH_LOFT_H */
