/*  The line's geometry: the junction box drawn on the outline the arms
 *  cut out, the edge a strip records in the traffic's graph.  The walk
 *  that puts its lamps on the map.  Nothing here decides what is drawn.
 *  The control is arc.rules.node_control's, the fill
 *  arc.rules.junction's, the lamps arc.rules.lamps's.  It draws what it
 *  is asked for. */
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "mesh/internal.h"
#include "pipeline.h"
#include "net/net.h"
#include "mesh/model.h"
#include "script.h"


/*  The junction whose signs are still to be stood, or none, and the
 *  shape they belong in.  The box's shape is closed before the rule that
 *  stands them runs.  So it is ENTERED AGAIN for them: the same way the
 *  outline view's wires are laid into the shape each belongs to. */
static JBox  *s_signs_jb;
static ShapeId s_signs_sh;

/*  The fill: the outline the arms cut out, as a fan from the middle, the
 *  mouths' returns, and the margin round it.  It is laid in two halves
 *  with the drive between them.  This is because what goes on the
 *  outline is the SCRIPT'S (arc.rules.junction): the outline is a
 *  solver's work, arms, corners, trims and lip returns.  The fill drawn
 *  on it is not.  No rule is no junction surface at all. */
static struct
{
    JuncFan fan;
    V2      poly[JUNC_MAX];
    V2      apoly[2 * JUNC_MAX];
    uint8_t mouth[JUNC_MAX];
    JuncArm arms[4];
    float   trm[4];
    float   mat, cx, cy, h, lw, zj;
    int     np, live;
    JBox   *jb; /* the box in hand, between the outline and the fill */
} s_asph;

static int jb_fill_ask(JBox *jb)
{
    /*  No signs until this box asks for them.  A box that lays nothing
     *  this build never reaches jb_sides, and a stale one left here
     *  would stand its signs at the wrong junction. */
    s_signs_jb = NULL;
    const RCity *c     = jb->c;
    Family       f     = jb->f;
    int32_t      col   = jb->col;
    int32_t      row   = jb->row;
    int          links = jb->links;
    s_asph.live = 0;
    s_asph.mat = jb->mat, s_asph.cx = jb->cx, s_asph.cy = jb->cy;
    s_asph.h = jb->h, s_asph.lw = jb->lw, s_asph.zj = jb->zj;
    /*  The outline the arms cut out.  With every arm on a tile axis this
     *  is the square it has always been. */
    s_asph.np = junction_poly(c, f, col, row, links, s_asph.poly, s_asph.mouth, JUNC_MAX, s_asph.trm, s_asph.arms);
    /*  The box stops here for the drive to have the margin answered.
     *  The fill is laid inside it, so it cannot be laid until the margin
     *  is known.  A box that reaches no chunk this build draws keeps the
     *  margin's records and lays no fill at all.  But it still needs the
     *  ring read. */
    s_asph.jb = jb;
    return 0;
}

/*  The ring read as a margin, for the drive to have answered before the
 *  fill is laid inside it. */
void *net_junction_band(void)
{
    return s_asph.jb ? margin_band_ask(s_asph.jb, s_asph.poly, s_asph.arms, s_asph.np, s_asph.jb->lw)
                     : NULL;
}

int net_junction_mouths(void)
{
    return s_asph.jb ? margin_mouths_ask(s_asph.poly, s_asph.np, s_asph.jb->lw) : 0;
}

/*  And the fill, laid on the answer. */
int net_junction_band_done(void)
{
    JBox *jb = s_asph.jb;
    int   anp;
    if (!jb || jb->records_only)
        return 0;
    /*  The fill is laid INSIDE the margin, not under it.  The band takes
     *  the outline's outer `lw` and the fan stops on the band's own
     *  inner edge, so the two meet edge to edge.  A mouth carries no
     *  band, so there the fan reaches the outline and the arm meets it. */
    anp                = margin_junction_inset(jb, s_asph.poly, s_asph.arms, s_asph.np, s_asph.apoly, (int)(sizeof s_asph.apoly / sizeof s_asph.apoly[0]));
    s_asph.fan.jb      = jb;
    s_asph.fan.poly    = anp >= 3 ? s_asph.apoly : s_asph.poly;
    s_asph.fan.np      = anp >= 3 ? anp : s_asph.np;
    s_asph.fan.cx      = jb->cx;
    s_asph.fan.cy      = jb->cy;
    s_asph.fan.zj      = jb->zj;
    s_asph.fan.mat     = jb->mat;
    s_asph.fan.order   = jb->order;
    s_asph.fan.square  = s_asph.np < 3;
    s_asph.fan.a0 = jb->a0, s_asph.fan.a1 = jb->a1, s_asph.fan.b0 = jb->b0, s_asph.fan.b1 = jb->b1;
    s_asph.live        = 1;
    return 0;
}

/*  The outline the drive is to lay the fill on, or nothing where this
 *  box draws none. */
JuncFan *net_junction_fan(void)
{
    return s_asph.live ? &s_asph.fan : NULL;
}

/*  The junction's margin is the margin pass's (margin.c): a margin band
 *  round this outline, mouth to mouth.  This leaves the margin open
 *  where a line comes in and closed everywhere else.  Takes the
 *  outline's own corners as it goes. */
static int jb_fill_done(JBox *jb)
{
    int rc      = margin_junction(jb, s_asph.poly, s_asph.arms, s_asph.np);
    s_asph.live = 0;
    s_asph.jb   = NULL;
    jb->mat = s_asph.mat;
    jb->cx  = s_asph.cx;
    jb->cy  = s_asph.cy;
    jb->h   = s_asph.h;
    jb->lw  = s_asph.lw;
    jb->zj  = s_asph.zj;
    return rc;
}

/*  The four sides: a free side carries the margin and its lip, a linked one its signal or stop sign. */
/*  WHAT STANDS AT A JUNCTION'S ARMS.  A signal, a stop sign, or nothing.
 *  Is arc.rules.junction_signs's, which places them itself.  Only the
 *  signs are the sides'.  The margin band in jb_fill follows the box's
 *  outline, free sides and lip returns alike.  So a second margin drawn
 *  along the tile's own side would lie on it at the same height and
 *  slot.
 *
 *  This offers the arms the rule needs and nothing more.  No rule is a
 *  junction with no sign at any arm. */
int net_junction_signs_ask(void)
{
    return s_signs_jb != NULL;
}

int net_junction_signs_at(int e, int *ctrl, float *h, int32_t *col, int32_t *row)
{
    JBox *jb = s_signs_jb;
    if (!jb || e < 0 || e > 3 || !(jb->links & (1 << e)) || jb->f != net_line->f)
        return 0;
    *ctrl = (s_junc_ctrl[jb->row * R_MAP + jb->col] >> (2 * e)) & 3;
    *h    = jb->h;
    *col  = jb->col;
    *row  = jb->row;
    return 1;
}

/*  And the junction let go of, once its signs are stood. */
void net_junction_signs_taken(void)
{
    s_signs_jb = NULL;
}

float net_junction_signs_order(void)
{
    return s_signs_jb ? s_signs_jb->order : 0.0f;
}

/*  The shape the signs belong in, entered again for them. */
void net_junction_signs_enter(void)
{
    if (s_signs_jb)
        shape_use(s_signs_sh);
}

void net_junction_signs_leave(void)
{
    if (s_signs_jb)
        shape_close(s_signs_sh);
}

static int jb_sides(JBox *jb)
{
    s_signs_jb = jb;
    s_signs_sh = shape_current();
    return 0;
}

/* ---- the families as geometry ------------------------------------------ */

/*  The networks as geometry.  Three families share one layout of fifteen
 *  pieces.  They are two straights, four slopes, four corners, four tees
 *  and a lap.  One family sits at XBLD 0x0E, another at 0x1D and a third
 *  at 0x2C.  Which edges a piece joins comes from the art.  The fill at
 *  each edge's midpoint.  The other two families take the line piece at
 *  the same offset.  The four meets 0x44..0x47 carry two families on one
 *  tile.  A line or thread is a strip along the piece's connections: a
 *  straight piece from edge midpoint to edge midpoint.  A lone corner a
 *  quarter circle centered on the tile corner between its two edges,
 *  tangent to both.  A corner on a staircase the chord between its two
 *  midpoints, so the staircase draws as one straight diagonal.  A
 *  junction a box at the center with an arm to each joined edge.  Where
 *  two pieces meet at an angle the inner corners are mitred to one point
 *  and a fan fills the outside.  So the edge line runs unbroken round
 *  the turn.  The strips lie on the surface the tiles draw, sampled at
 *  every vertex's world position.  A power line is a pole at the tile's
 *  center with a wire to each joined edge, meeting the neighbor's wire
 *  at the midpoint.  Width.  The oblique camera stretches the diagonal
 *  that runs toward it and squashes the other.  So a line at one world
 *  width draws almost twice as wide on screen one way as the other.  In
 *  the snap view every strip's world width is scaled by the direction it
 *  runs so that it reads the same width on screen everywhere.  The
 *  turned inspection view uses the true width. */

/*  A line's class, from the traffic on it.  0 is a two-lane line.  1 is
 *  an avenue with a double center line and four lanes, and 2 a boulevard
 *  with a planted median.  Carried to the material in the normal's
 *  fourth component, where a ground vertex carries its curvature. */
/*  WHAT CLASS a tile's line is, from the traffic on it.  Where the steps
 *  fall is arc.rules.line_class's, and all two hundred and fifty-six
 *  answers are settled before anything is built.  So a tile costs a
 *  load.  Every reader comes through here, the junction's control, the
 *  class pass, the gate's arm and the script's own arc.city.line_class.
 *  So none of them can hold a different idea of what an avenue is. */
static uint8_t s_line_class[256];

void net_line_class_is(int tv, int cls)
{
    if (tv >= 0 && tv < 256)
        s_line_class[tv] = (uint8_t)(cls < 0 ? 0 : cls > 2 ? 2 : cls);
}

float line_class(const RCity *c, int32_t col, int32_t row)
{
    return (float)s_line_class[c->xtrf[(row >> 1) * R_HALF + (col >> 1)]];
}

/* ---- the line as a family ------------------------------------------------ */

/*  THE PAVED BOX, in the two moments world.lua calls for it: the outline
 *  gathered for arc.rules.junction to lay the fill on.  Then the fill
 *  taken back and the box handed to the signs.  A family that declares
 *  `paved` gets them.  One that does not answers nothing and its
 *  junction draws no fill at all.
 *
 *  A box has no corner pieces of its own.  It is not a square:
 *  junction_poly gives it an outline with its own lip returns.  The
 *  margin band follows that outline round, so a corner drawn on top
 *  would lie over the band in a shape of its own. */
int net_box_paving_ask(void)
{
    JBox *jb = net_junction_box_now();
    if (!net_junction_composing() || !net_family_paved(net_family(jb->f)))
        return 0;
    return jb_fill_ask(jb) == 0;
}

int net_box_paving_done(void)
{
    JBox *jb = net_junction_box_now();
    if (!net_junction_finishing() || !net_family_paved(net_family(jb->f)))
        return 0;
    if (jb_fill_done(jb) != 0)
        return 0;
    if (jb->records_only) /* the signs are drawing only */
        return 1;
    return jb_sides(jb) == 0;
}

/*  A line strip records its edge in the traffic's graph, under the class
 *  its tiles gave it.  An island has none, and records as a local line.
 *  Its two margins are the outer fifth each side, from its first station
 *  to its last.  So the margin check can see them meet the junctions'
 *  and the caps'. */
/*  Each station's distance along to the nearest level lap on the
 *  segment, for the line's approach to it (spec 3.15).  From the RXR
 *  stencil to the stop line the lines are solid.  The quads within a
 *  tile and a half of a lap tile's center carry the approach material
 *  with that distance along.  It is computed here and not by the
 *  furniture pass, which the driver can switch off. */

/*  The margin's two bands as the composing script lays them: the outer
 *  edge, the inner one, and the height, station by station.  The strip's
 *  own rule fills these, and the record below takes them up and names
 *  them into the network. */
#define WALK_MAX LOFT_MAX_ST
static float s_wk[2][WALK_MAX][5];
static int   s_wk_n[2];
static float s_wk_end[2][4];

void script_walk_reset(void)
{
    s_wk_n[0] = s_wk_n[1] = 0;
    memset(s_wk_end, 0, sizeof s_wk_end);
}

int script_walk_at(int side, float ox, float oy, float ix, float iy, float z)
{
    if (side < 0 || side > 1 || s_wk_n[side] >= WALK_MAX)
        return 0;
    s_wk[side][s_wk_n[side]][0] = ox;
    s_wk[side][s_wk_n[side]][1] = oy;
    s_wk[side][s_wk_n[side]][2] = ix;
    s_wk[side][s_wk_n[side]][3] = iy;
    s_wk[side][s_wk_n[side]][4] = z;
    ++s_wk_n[side];
    return 1;
}

void script_walk_ends(int side, float ax, float ay, float bx, float by)
{
    if (side < 0 || side > 1)
        return;
    s_wk_end[side][0] = ax, s_wk_end[side][1] = ay;
    s_wk_end[side][2] = bx, s_wk_end[side][3] = by;
}

int script_walk_count(int side)
{
    return side >= 0 && side <= 1 ? s_wk_n[side] : 0;
}

void script_walk_station(int side, int k, float *out)
{
    if (side >= 0 && side <= 1 && k >= 0 && k < s_wk_n[side])
        memcpy(out, s_wk[side][k], sizeof s_wk[0][0]);
}

void script_walk_end_pts(int side, float *out)
{
    if (side >= 0 && side <= 1)
        memcpy(out, s_wk_end[side], sizeof s_wk_end[0]);
}




/*  Street lighting (spec 1.6, 6.4).  Which strips are lit and how the
 *  lamps are spaced along them is arc.rules.lamps's, and their shape the
 *  street_lamp model's.  What is left here is the walk that turns a
 *  distance along the strip into a place on the map.  A lamp that would
 *  stand at a level meet is dropped: the meet's own protection is there. */





