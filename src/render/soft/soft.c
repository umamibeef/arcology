/*  soft.c: the sweep: WHAT the original draws, and in what order.  This
 *  half decides.  Soft/paint.c puts the pixels down and image.c writes
 *  them out.  See soft.h.  Every rule here is taken from the game's own
 *  drawing code in out/CODE_2.ann.asm and is cited by address where it
 *  is not obvious.  Nothing is inferred from how the result looks.  The
 *  renderer is checked against the original itself, with
 *  tools/render_diff.py (what it blits) and tools/pixel_diff.py (what it
 *  paints). */
#include "soft/soft.h"
#include "project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lodepng.h"
#include "tables.h"

void soft_defaults(RSoftOpts *o)
{
    memset(o, 0, sizeof *o);
    o->zoom           = 32;
    o->n              = R_MAP;
    o->sky[0]         = 16;
    o->sky[1]         = 20;
    o->sky[2]         = 22;
    o->draw_terrain   = 1;
    o->draw_things    = 1;
    o->draw_traffic   = 1;
    o->draw_buildings = 1;
    o->focus_row      = -1;
    o->focus_col      = -1;
}

/*  XTER -> terrain tile, and the height to draw it at.  Both come from the
 *  game's own tile drawing routine at $167CC rather than from inference:
 *  016820: cmpi.w #$10, d5      .  XTER < 0x10 is dry land, >= is water
 *  01683C: andi.w #$1f, d0      .  Dry: altitude is ALTM bits 0..4 016862:
 *  lsr.w  #$5, d0       .  Wet: shift down five, so the water 016864:
 *  andi.w #$1f, d0      .  Surface is ALTM bits 5..9 016872: lea.l
 *  -$493e(a5), a0 .  The XTER -> shape word table 016878: move.w (a0, d5.w),
 *  d5 ALTM therefore carries two heights.  Across the shipped cities the
 *  water field sits 0..7 levels above the ground on water tiles, which is
 *  what a water table is.  Drawing water at the ground height instead sinks
 *  every lake and river into its own bed. */
static int32_t terrain_tile(uint8_t xter)
{
    int32_t t = R_XTER_TILE[xter];
    /*  The table runs on past the terrain values into neighboring data,
     *  so anything that is not a tile id draws nothing.  The 103 shipped
     *  cities never exceed XTER 0x45. */
    return (t == 0 || t >= 500) ? 0 : t;
}

static int32_t tile_alt(uint16_t altm, uint8_t xter)
{
    return (int32_t)rcity_alt_surface(altm, xter);
}

/*  Is this the tile the building is drawn at?  The game's own test is a
 *  single AND ($1732C): andi.w #$f0, d0   .  The corner nibble and.w
 *  $122a(a5), d0  .  G_rotTable[rotation], set at $1545E beq  skip but
 *  it is only reached for some XBLD ranges.  Roads, rail, power lines
 *  and trees (XBLD < 0x61) are drawn through paths that never test it
 *  ($17528, $1775E).  This is why they carry no corner bits at all.
 *  Getting this wrong in either direction is visible.  Testing the mask
 *  on everything discards 68% of what is built.  Skipping the test on
 *  everything unzoned draws each large building once per interior tile.
 *  A 3x3 carries corner bits on only its four corners, so five of its
 *  nine tiles have a zero nibble, and a 4x4 has twelve. */
/*  Traffic, from $17612.  Cars come from the XTRF density layer.  They
 *  do not come from XTHG.  A line tile draws one when the density at
 *  (row/2, col/2) passes a threshold.  A heavier variant above a second
 *  one.  Returns the shape, or 0 for no car. */
static int32_t traffic_car(const RCity *c, int32_t idx, int32_t row, int32_t col, int32_t xbld, int *flip)
{
    int32_t t, lo, hi, k;

    if (xbld < 29 || xbld > 108)
        return 0;
    k = R_TRAFFIC_CAR[xbld - 29];
    if (!k)
        return 0;
    /*  $17632 for the surface band shapes, $174B4 for the elevated
     *  pieces: both use the lower pair of thresholds.  An ordinary road
     *  needs far more traffic on it before a car appears. */
    if ((xbld >= 73 && xbld <= 80) || (xbld >= 0x61 && xbld < 0x6C))
    {
        lo = 0x1C;
        hi = 0x38;
    }
    else
    {
        lo = 0x55;
        hi = 0xAA;
    }
    t = c->xtrf[(row >> 1) * R_HALF + (col >> 1)];
    if (t <= lo)
        return 0;
    *flip = (c->xbit[idx] & 0x02u) != 0;
    if (k == 0x0B) /* $17694: alternates with the row */
    {
        if (row & 1)
            k = 0x0C;
    }
    else if (k == 0x0C) /* $176A8: always mirrored, alternates with the col */
    {
        *flip = 1;
        if (col & 1)
            k = 0x0B;
    }
    /*  $176C2 and $174D8 both index the heavy table with the car slot
     *  directly, no range test.  The table runs from A5-0x47D6 up to
     *  where R_UGND_ZONE starts at A5-0x47BA: 28 entries, not 16.
     *  Capping it at 16 left the elevated bands showing light traffic
     *  where the game shows heavy. */
    if (t > hi && k < 28)
        k = R_TRAFFIC_HEAVY[k];
    return 399 + k;
}

static int draws_here(uint8_t xzon, uint8_t xbld, uint8_t mask)
{
    uint8_t corners = (uint8_t)(xzon & 0xF0u);
    if (corners & mask)
        return 1;                         /* the anchor for this rotation, and 0xF0 single tiles */
    return corners == 0u && xbld < 0x70u; /* unzoned 1x1: always */
}

/*  Where the --focus tile landed on the canvas.  Set by the sweep, read
 *  back through soft_focus_result. */
static int32_t g_focus_x = 0, g_focus_y = 0;
static int     g_focus_ok = 0;

#define R_ROAD_INDEX 0x91

/*  The tile the game stamps on a powered network that has no power:
 *  literal $56A at 32 px, $376 at 16, $182 at 8: the same tile 386 in
 *  each set's numbering. */
#define R_NO_POWER 386

/* ---- the op list ------------------------------------------------------
 *
 *  The sweep does not paint.  It emits one op per blit the original
 *  would make, in the original's order, and a consumer paints them.
 *
 *      This file's rasteriser.
 *      Or the GPU path.
 *      Which uploads the same list as instances.
 *
 *  One sweep, two backends.  The sweep is the part that is checked
 *  against the game. */

void ops_free(ROpList *ops)
{
    free(ops->v);
    ops->v   = NULL;
    ops->n   = 0;
    ops->cap = 0;
}

static ROp *ops_push(ROpList *ops)
{
    if (ops->n == ops->cap)
    {
        size_t ncap = ops->cap ? ops->cap * 2u : 8192u;
        ROp   *nv   = (ROp *)realloc(ops->v, ncap * sizeof *nv);
        if (!nv)
            return NULL;
        ops->v   = nv;
        ops->cap = ncap;
    }
    return &ops->v[ops->n++];
}

/*  What the sweep carries while it emits. */
typedef struct
{
    ROpList           *ops;
    const RAtlasLevel *l;
    uint32_t           order;
    int32_t            row, col;
    int32_t            alt; /* the tile's altitude in levels, as drawn */
    int                oom;
} Emitter;

/*  `rise` is how far above sy the art's top-left goes.  -1 means "the
 *  sprite's own", which is what every surface blit wants.  The underground
 *  overlays pass the GROUND sprite's instead, because $16298 works out
 *  -$4(a6) once from the ground shape and $16376, $163EA, $1640A, $164A2
 *  and $16514 all reuse it.  The op records the resolved top-left, which
 *  is the y the game itself passes to $18E96.
 *
 *  A shape with no art emits nothing.  The original paints nothing for
 *  it, and the blit list the oracle is diffed against does not list it. */
static ROp *emit(Emitter *e, int kind, int32_t shap, int32_t sx, int32_t sy, int flip, int32_t rise, int32_t stencil, int terrain)
{
    const RTile *t = atlas_tile(e->l, shap);
    ROp         *op;

    if (!t || e->oom)
        return NULL;
    op = ops_push(e->ops);
    if (!op)
    {
        e->oom = 1;
        return NULL;
    }
    memset(op, 0, sizeof *op);
    op->kind    = (uint8_t)kind;
    op->shape   = shap;
    op->x       = sx;
    op->y       = sy - (rise >= 0 ? rise : (int32_t)t->ay);
    op->flip    = (uint8_t)(flip != 0);
    op->stencil = stencil;
    op->terrain = (uint8_t)terrain;
    op->order   = e->order;
    op->row     = (int16_t)e->row;
    op->col     = (int16_t)e->col;
    op->alt     = (int16_t)e->alt;
    return op;
}

static void emit_blit(Emitter *e, int32_t shap, int32_t sx, int32_t sy, int flip, int32_t rise, int terrain)
{
    emit(e, R_OP_BLIT, shap, sx, sy, flip, rise, -1, terrain); /* no stencil: emit_car is the one that stencils */
}

/*  $1987E.  The traffic cars do not go through $18E96 at all: all five call
 *  sites ($17704, $17F8A, $18172, $188E8, $18A8C: the three zoom levels)
 *  call $19004, whose four inner blitters each read the destination pixel
 *  and write the car only where it is still index 0x91, the road surface.
 *  A car is therefore stencilled onto fill, not layered over it:
 *  wherever a power line, a building, a bridge rail or a tree already
 *  covers the road, the car pixel is dropped.  The op also names the road
 *  sprite it was stencilled onto, so a consumer that cannot read its
 *  destination can test the road's own texel instead. */
static void emit_car(Emitter *e, int32_t shap, int32_t sx, int32_t sy, int flip, int32_t stencil, int32_t road, int32_t road_x, int32_t road_y, int road_flip)
{
    ROp *op = emit(e, R_OP_BLIT, shap, sx, sy, flip, -1, stencil, 0);
    if (op)
    {
        const RTile *rt = atlas_tile(e->l, road);
        op->under_flip  = (uint8_t)(road_flip != 0);
        op->under_shape = rt ? road : 0;
        op->under_x     = road_x;
        op->under_y     = rt ? road_y - (int32_t)rt->ay : 0;
    }
}

/*  A thing's shadow, from $1911E / $19B76.  It is not a sprite.  $19B76
 *  walks the same silhouette but READS the destination.  It only
 *  rewrites it when it already holds one of the dirt spur's indices.
 *  The op carries the silhouette.  What it does to the destination is
 *  the consumer's business. */
static void emit_shadow(Emitter *e, int32_t shap, int32_t sx, int32_t sy, int flip)
{
    emit(e, R_OP_SHADOW, shap, sx, sy, flip, -1, -1, 0);
}

/* ---- painting the ops -------------------------------------------------- */

/*  The data-overlay views, transcribed from the jump table at $168C8.
 *  Views 1..8 map a layer value through an eight-step spur at shape
 *  0x1D4 + (value >> 5), leaving anything under 0x10 untinted.  Views
 *  9..11 already hold a shape id.  Returns 0 for "no tint". */
static int32_t overlay_tile(const RCity *c, int32_t row, int32_t col, int32_t view)
{
    int32_t v;
    uint8_t b;

    switch (view)
    {
        case R_VIEW_TRAFFIC:
            v = c->xtrf[(row >> 1) * R_HALF + (col >> 1)];
            break;
        case R_VIEW_DENSITY:
            v = c->xpop[(row >> 2) * R_QTR + (col >> 2)];
            break;
        case R_VIEW_GROWTH_VALUE:
            v = c->xrog[(row >> 2) * R_QTR + (col >> 2)];
            break;
        case R_VIEW_CRIME:
            v = c->xcrm[(row >> 1) * R_HALF + (col >> 1)];
            break;
        case R_VIEW_POLICE:
            v = c->xplc[(row >> 2) * R_QTR + (col >> 2)];
            break;
        case R_VIEW_POLLUTION:
            v = c->xplt[(row >> 1) * R_HALF + (col >> 1)];
            break;
        case R_VIEW_LANDVALUE:
            v = c->xval[(row >> 1) * R_HALF + (col >> 1)];
            break;
        case R_VIEW_FIRE:
            v = c->xfir[(row >> 2) * R_QTR + (col >> 2)];
            break;

        /*  $169E0 / $16A1C: conducting and supplied reads as 300,
         *  conducting only as 303.  Off the network, no tint. */
        case R_VIEW_POWER:
            b = c->xbit[row * R_MAP + col];
            return (b & 0x80u) ? ((b & 0x40u) ? 300 : 303) : 0;
        case R_VIEW_WATER:
            b = c->xbit[row * R_MAP + col];
            return (b & 0x20u) ? ((b & 0x10u) ? 300 : 303) : 0;

        /*  $16A52: growth arrows, thresholded rather than ramped. */
        case R_VIEW_GROWTH:
            v = c->xrog[(row >> 2) * R_QTR + (col >> 2)];
            if (v >= 0x83)
                return 476;
            if (v <= 0x7C)
                return 477;
            return 0;

        default:
            return 0;
    }
    return v < 0x10 ? 0 : 468 + (v >> 5);
}

/*  The underground view, from $161DC.  Every shape for one tile, in the
 *  order the game emits them.  Each branch is read off the disassembly,
 *  not inferred from how the result looks: $162A0  a tunnel first, from
 *  (ALTM >> 10) & 0x1F.  1 draws the mouth, 0x3E + XTER.  Anything
 *  higher draws the marker 0x160 one altitude step per level down
 *  ($162E6) $16334  a pipe (XUND 0x10..0x20) that both conducts water
 *  and is supplied gets the flowing variant, +0x74 $16396  any other
 *  non-zero XUND, subway, tunnel, draws 0x13E+v, then a water-status
 *  marker if the tile is on the network $1642A  nothing buried: the
 *  marker alone.  The wireframe lattice at $164A2 is reached ONLY when
 *  there is no pipe AND no water network, so it is the empty-tile art,
 *  not a backdrop laid under everything else: painting it under the
 *  subway was what put a lattice through the tunnels $164DC  then XBLD,
 *  so a subway shows the street above it.  At or above 0x70 a zone
 *  building collapses to one generic marker, 354 + R_UGND_ZONE[zone]
 *  ($16512), never its own art.  Below that the tile draws as itself,
 *  after two filters: $1667E  XBLD below 14 draws NOTHING: the small
 *  network pieces have no underground art $165A6  the elevated range
 *  0x61..0x6B draws at its anchor corner only, the XZON corners masked
 *  by $122A and it is then shifted: $166A4 drops a network standing over
 *  water to the water surface, and failing that $166E6 lifts it one step
 *  when the tile's own lattice shape is exactly 0x13E, which is what
 *  aligns a subway to a sloped tile.  Its mirror is XBIT & 2 ($16722):
 *  $18F10 only tests it, so any non-zero value means mirrored.  Returns
 *  the number of shapes written, each with a y shift and a mirror flag
 *  (1..4). */
static int underground_tiles(const RCity *c, int32_t idx, uint8_t mask, int32_t alt, int32_t out[6], int32_t dy[6], int flip[6], int gnd_anchor[6])
{
    int32_t  v   = c->xund[idx];
    uint8_t  b   = c->xbit[idx];
    int32_t  bld = c->xbld[idx];
    uint16_t am  = c->altm[idx];
    int32_t  ter = c->xter[idx] & 0x7F;
    int32_t  gnd = R_UGND_GROUND[ter];
    int32_t  tun = (am >> 10) & 0x1F;
    int32_t  d;
    int32_t  lift = 0;
    /*  $166E6, the slope lift, sits in the generic XBLD tail only.  The
     *  elevated path at $165EC branches over it to $1662E. */
    int onslope = 1;
    int n       = 0;

/*  `ga` says which descriptor anchors the shape.  $16298 works out
 *  -$4(a6) ONCE, from the ground shape.  The pipe/subway ($16376), the
 *  water markers ($163EA, $1640A), the wireframe ($164A2) and the zone
 *  marker ($16514) all push that same value.  Only the tunnel shapes
 *  ($162C8) and the XBLD network ($1670C) recompute it from their own.
 *  Anchoring a 34 px tunnel on its own art instead of on the 18 px
 *  diamond is what left the joins stepped at every corner. */
#define UG_EMIT(sh, ddy, ff, ga) \
    do                           \
    {                            \
        out[n]        = (sh);    \
        dy[n]         = (ddy);   \
        flip[n]       = (ff);    \
        gnd_anchor[n] = (ga);    \
        ++n;                     \
    } while (0)

    if (tun == 1)
        UG_EMIT(0x3E + ter, 0, 0, 0); /* $162B8: its own ($162C8) */
    else if (tun)
        UG_EMIT(0x160, (tun - 1) * alt, 0, 0); /* $162E6: its own */

    if (v >= 0x10 && v <= 0x20)
    {
        if ((b & 0x30u) == 0x30u)
            v += 0x74;
        UG_EMIT(0x13E + v, 0, 0, 1);
    }
    else
    {
        if (v)
            UG_EMIT(0x13E + v, 0, 0, 1);
        if (b & 0x20u)
            UG_EMIT((b & 0x10u) ? 0x1D3 : 0x15F, 0, 0, 1);
        else if (!v)
            UG_EMIT(gnd, 0, 0, 1); /* $164A2 */
    }

    if (!bld)
        return n;

    if (bld >= 0x70) /* $164EC */
    {
        UG_EMIT(354 + R_UGND_ZONE[c->xzon[idx] & 0x0Fu], 0, 0, 1);
        return n;
    }
    if (bld >= 0x61 && bld < 0x6C) /* $165A6 */
    {
        if (!((c->xzon[idx] & 0xF0u) & mask))
            return n;
        /*  $1663C adds d6 to the y of an elevated piece and of nothing
         *  else.  d6 is NOT 2: $161E4 loads 2 and $161EA shifts it by
         *  the zoom.  So it is 2 << zoom: 8 at this set, where `alt` is
         *  the 3 << zoom from $161EC.  Reading the moveq without the lsl
         *  put every band connector 6 px high in the underground view. */
        lift    = alt / 3 * 2;
        onslope = 0;
    }
    else if (bld < 0x0E) /* $1667E */
        return n;

    if (b & 0x04u) /* $166A4: drop to the water surface */
        d = (((int32_t)(am & 0x1Fu)) - ((int32_t)((am >> 5) & 0x1Fu))) * alt;
    else if (onslope && gnd == 0x13E) /* $166E6: lift onto the slope */
        d = -alt;
    else
        d = 0;
    UG_EMIT(bld, d + lift, (b & 0x02u) ? 1 : 0, 0);
#undef UG_EMIT
    return n;
}

/*  One moving thing, from $A032.
 *
 *    00A052  the type is at +0
 *    00A05E  skip entirely if g_zoom < R_THING_MINZOOM[type]
 *    00A0BA  dispatch on type through the jump table at $A0BE.  Types 12
 *            and 13 land on $A128, a bare branch to the exit
 *    00A13A  most arms take base = R_THING_SHAPE[type] and add a frame
 *            chosen by the heading at +1, mirrored for the headings that
 *            reuse a sprite */
/*  Road vehicles, thing types 10 and 11 ($A7E0).  The sprite follows the
 *  ROAD under the vehicle, not its heading.  Returns the slot, or -1 for
 *  "do not draw".  *raise is 1 when the sprite sits one altitude level
 *  up. */
static int32_t veh_slot(const RCity *c, int32_t idx, const uint8_t *rec, int *raise)
{
    int32_t b = c->xbld[idx], d4, slot;

    *raise = 0;
    if (b == 0x5Au || b == 0x5Bu)
    {
        /*  A bridge slab.  The vehicle rides at the water table plus one
         *  level, and $A84C reads ALTM bits 5..9.  XBIT bit 1, the
         *  orientation flag, picks which way it faces. */
        *raise = 1;
        return (c->xbit[idx] & 0x02u) ? 1 : 0;
    }
    d4 = b - 0x2C;
    if (d4 < 0 || d4 > 0x22)
        return -1; /* $A894: not a road the vehicle can be drawn on */
    if (d4 > 0x12)
        d4 -= 6;
    if (d4 > 0x16)
        d4 -= 4;
    if (d4 < 0 || d4 >= 28)
        return -1;
    slot = R_VEH_SLOT[d4];
    if (slot == 50)
        slot = R_VEH_SLOT8[rec[8] & 7u]; /* an intersection: use thing[+8] */
    if (slot >= 20)
        return -1;
    if (c->xter[idx] == 0x0Du)
        *raise = 1; /* $A90E */
    return slot;
}

/*  $01756A: a raising bridge stands open while a ship is near.  The tile
 *  renderer draws the lift span, XBLD 0x58.  Then asks: is there a ship
 *  at all ($12E4, the count the ship step keeps).  If so is one within
 *  eight tiles by $A6E4's Manhattan distance?  If it is, the drawn id
 *  becomes 0x59, the same span, raised.  That is why the sim's
 *  SHIP_PASSABLE lists BOTH: a ship sails under either.  Without it the
 *  span never rises.  Since things draw last in a tile ($FABA, so a
 *  train covers its own thread) the ship then sails visibly over a
 *  closed bridge, which is what gave this away.  The original scans from
 *  slot 1 and takes the FIRST ship it finds, not the nearest, and stops
 *  at slot 0x28.  Both are kept.  The rule is written twice over there,
 *  once per zoom's tile renderer: $01756A and $017FD8, identical down to
 *  the threshold.  The sweep does not branch on zoom at this point, so
 *  once here covers all three.  The raise was measured at 32, 16 and 8
 *  px. */
static int bridge_is_raised(const RCity *c, int32_t col, int32_t row)
{
    int slot;
    for (slot = 1; slot < 0x28 && slot < R_MAX_THINGS; ++slot)
    {
        const uint8_t *rec = c->xthg + (size_t)slot * 12u;
        int32_t        dy, dx;
        if (rec[0] != 3) /* $17586: type 3, a ship */
            continue;
        dy = (int32_t)rec[3] - row; /* $175B2 */
        dx = (int32_t)rec[4] - col; /* $175AA */
        if (dy < 0)
            dy = -dy;
        if (dx < 0)
            dx = -dx;
        return dy + dx < 8; /* $175C4: nearer than eight */
    }
    return 0;
}

static void draw_thing(Emitter *e, const RCity *c, int32_t idx, const uint8_t *rec, int32_t zoom_level, int32_t sx, int32_t sy)
{
    const RAtlasLevel *l    = e->l;
    int32_t            type = rec[0];
    int32_t            head, shape, flip = 0, dx = 0, dy = 0;

    if (type == 0 || type > 16)
        return;
    if (type == 12 || type == 13)
        return; /* $A128 is a bare branch to the exit */
    if (zoom_level < (int32_t)R_THING_MINZOOM[type])
        return;
    shape = R_THING_SHAPE[type];
    if (!shape)
        return;

    if (type == 10 || type == 11)
    {
        int     raise;
        int32_t slot = veh_slot(c, idx, rec, &raise);
        if (slot < 0)
            return;
        shape += R_VEH_SHAPE[slot];
        flip = R_VEH_FLIP[slot];
        dx   = R_VEH_DX[slot];
        dy   = R_VEH_DY[slot];
        if (raise)
            sy -= l->alt_step;
    }
    else if (type == 0 || type == 4 || type == 9)
    {
        /*  $A1CA.  These three share an arm that reads a FOUR-entry
         *  heading pair ($A214, $A224), not the eight-entry tables every
         *  other arm uses.  And $A1E2 gives type 9 a variant of its own
         *  when the record's byte at +2 is set. */
        if (type == 9 && rec[2])
        {
            shape = 0x17B;
            flip  = 0; /* $12F8 & 1: a global the reconstruction has no
                        * equivalent for, so the unmirrored frame */
        }
        else
        {
            head = rec[1] & 3u;
            shape += R_DIR_FRAME4[head];
            flip = R_DIR_FLIP4[head];
        }
    }
    else
    {
        head = rec[1] & 7u;
        shape += R_DIR_FRAME[head];
        flip = R_DIR_FLIP[head];
    }

    /*  $A96C centers the sprite in the tile using the shape descriptor's
     *  width field.  The atlas tile's own width is that number. */
    {
        const RTile *t = atlas_tile(l, l->id_base + shape);
        if (t)
            dx += arc_half_wi(l->tile_w) - (int32_t)t->w / 2;
    }

    /*  A thing does not sit on its tile.  $A232 reads three more fields
     *  out of the 12-byte XTHG record and none of them were being used:
     *  +5  the thing's OWN altitude, scaled by 2<<zoom: half the terrain
     *  step ($A29E, $A2B2).  An aircraft carries a big value here, which
     *  is the whole reason it appears in the sky.  Without it the plane
     *  was drawn at street level on its own tile and every later
     *  building painted straight over it. +6  sub-tile y, +7 sub-tile x
     *  ($A2E4, $A2EE), projected with the same isometric divide as the
     *  map: (y-x) across, (y+x) down ($A310, $A33C), divided by the
     *  per-zoom constants in R_THING_DIVX / R_THING_DIVY.  This is what
     *  makes a car or a boat move smoothly between tiles instead of
     *  snapping.  The terrain part of the altitude ($A29A / $A2CE,
     *  3<<zoom) is already in sy, and for a water tile the game reads
     *  the water surface there, which is what tile_alt does too. */
    if (type != 10 && type != 11)
    {
        /*  Only the arms that fall through to $A232 get this.  The road
         *  and rail vehicles are dispatched at $A116 to $A7E0.  This
         *  does its own positioning off the network tile under the
         *  vehicle and returns straight to the exit.  Applying the
         *  generic tail to them put a train 12 px left and 35 px below
         *  its thread. */
        int32_t sub_y = rec[6], sub_x = rec[7];
        int32_t divx = R_THING_DIVX[zoom_level & 3];
        int32_t divy = R_THING_DIVY[zoom_level & 3];

        if (divx)
            dx += (sub_y - sub_x) / divx;
        if (divy)
            dy += (sub_y + sub_x) / divy;
        dy -= (int32_t)rec[5] * (2 << zoom_level);
    }
    /*  $A370: only three thing types cast one: 1, 2 and 16, the things
     *  that fly.  It is an allow-list, not an exclusion: a boat or a
     *  train reaches $A3C0 and is drawn with no shadow at all.  On top
     *  of that $A396 skips it when the tile underneath is a zone
     *  building (XBLD >= 0x70).  The shadow is the same sprite at the
     *  same x, dropped by (altitude - 2) steps of 2<<zoom so it lands on
     *  the ground the thing is flying over. */
    if ((type == 1 || type == 2 || type == 16) && c->xbld[idx] < 0x70)
        emit_shadow(e, l->id_base + shape, sx + dx, sy + dy + ((int32_t)rec[5] - 2) * (2 << zoom_level), flip);

    emit_blit(e, l->id_base + shape, sx + dx, sy + dy, flip, -1, 0);
}

int soft_sweep(const RAtlas *a, const RCity *c, const RSoftOpts *o, ROpList *ops, RSweep *info)
{
    const RAtlasLevel *l = NULL;
    int32_t            i, tw, th, alt, W, H, ox, oy, s, n = o->n;
    uint8_t            mask;
    static int16_t     thing_at[R_MAP * R_MAP];
    int                elevated, ground_here;
    int32_t            zoom_level;
    Emitter            E;

    memset(info, 0, sizeof *info);
    for (i = 0; i < a->n_levels; ++i)
        if (a->level[i].zoom == o->zoom)
            l = &a->level[i];
    if (!l)
        return -1;

    tw  = l->tile_w;
    th  = l->tile_h;
    alt = l->alt_step;

    /*  The canvas.  Quoted at zoom 32 and scaled from there, and placed
     *  so it sits on the original renderer's own coordinates: see the
     *  note on `oy` below. */
    W  = n * tw + tw * 4;
    H  = n * th + 420 * th / 16;
    ox = n * arc_half_wi(tw);
    /*  `ay` is the sprite's full height: exactly the $1226 descriptor's
     *  +4 that the game subtracts at $16298: so this origin puts the
     *  canvas on the original's own coordinates and tools/pixel_diff.py
     *  can compare the two directly. */
    oy = 200 * th / 16;

    zoom_level = (l->zoom == 8) ? 0 : (l->zoom == 16 ? 1 : 2);
    mask       = city_corner_mask(c->rotation);
    g_focus_ok = 0;

    E.ops   = ops;
    E.l     = l;
    E.order = 0;
    E.row   = 0;
    E.alt   = 0;
    E.col   = 0;
    E.oom   = 0;
    ops->n  = 0;

    /*  Which thing is on a tile is not worked out by scanning XTHG and
     *  trusting the row and column stored in each record.  That was a
     *  guess, and it put things on tiles the game leaves empty.  $FABA
     *  reads it straight out of the XTXT layer:
     *
     *      00FAD0  a1 = XTXT[row]
     *      00FADA  d5 = XTXT[row][col]
     *      00FB12  cmpa.w #$c9, a0 .  Blt -> draw nothing
     *      00FB18  d0 = a0 - $C9          .  The thing's index
     *      00FB26  jsr $A032(row, col, d0)
     *
     *  Values below 0xC9 are signs and labels ($FB32) and 0xF1..0xFA
     *  draw nothing.  0xFB and above go to $399D8, which this does not
     *  model. */
    {
        int32_t k;
        for (k = 0; k < R_MAP * R_MAP; ++k)
        {
            int32_t v   = c->xtxt[k];
            thing_at[k] = (v >= 0xC9 && v <= 0xF0 && v - 0xC9 < R_MAX_THINGS)
                              ? (int16_t)(v - 0xC9)
                              : (int16_t)-1;
        }
    }

    /*  Back to front along anti-diagonals.  The order is arithmetic, not
     *  a sort: for a fixed s = dx + dy every tile is at the same depth.
     *  The 2.5D split into a terrain pass and a sprite pass is the
     *  consumer's.  Every op says which pass it belongs to.  The list is
     *  in the original's order for both. */
    for (s = 0; s < 2 * n; ++s)
    {
        int32_t dy;
        for (dy = 0; dy < n; ++dy)
        {
            int32_t dx = s - dy;
            int32_t idx, sx, sy, b;
            if (dx < 0 || dx >= n)
                continue;
            idx = (o->y0 + dy) * R_MAP + (o->x0 + dx);
            if (idx < 0 || idx >= R_MAP * R_MAP)
                continue;
            /*  The painter's index: 1-based so that 0 in the depth plane
             *  means "no terrain here". */
            E.order = (uint32_t)(s * n + dy) + 1u;
            E.row   = o->y0 + dy;
            E.col   = o->x0 + dx;

            /*  screen_x = (row - col) * halfwidth, NOT (col - row).
             *  $167EE loads $8(a6): the row, since it indexes the
             *  row-pointer table at $16812: and subtracts the column.
             *  Getting this backwards mirrors the entire map: the city
             *  still looks like a city.  But every diagonal road runs
             *  the wrong way and the Hudson ends up on the wrong side. */
            sx = ox + (dy - dx) * arc_half_wi(tw);
            /*  $01623E: the underground view masks ALTM with 0x1F and
             *  stops.  It always uses the GROUND altitude, never the
             *  water table.  Using the surface rule there shifts every
             *  tile that has water above it, which is what threw the
             *  subway joins out of line. */
            E.alt = o->underground ? rcity_alt_ground(c->altm[idx])
                                   : tile_alt(c->altm[idx], c->xter[idx]);
            sy    = oy + (dx + dy) * arc_half_hi(th) - E.alt * alt;

            if (o->focus_row >= 0 && o->y0 + dy == o->focus_row &&
                o->x0 + dx == o->focus_col)
            {
                g_focus_x  = sx;
                g_focus_y  = sy;
                g_focus_ok = 1;
            }

            /*  Ground columns: the skirt down the two far edges of the
             *  map.  $17008 gates the WHOLE block, both loops, on
             *  `col == 127 || row == 127`:
             *
             *      017008  cmpi.w #$7f, d7        .  Col
             *      01700C  beq    $17018
             *      01700E  cmpi.w #$7f, $c(a6)    .  Row
             *      017014  bne    $170DE          .  Neither: skip both
             *
             *  $17040 then stacks shape 269 once per altitude level (the
             *  dirt cliff) and $170C6 stacks 284 from the sea bed up to
             *  the water surface.  Running that second loop on every
             *  water tile, not just the edge ones, drew a column of open
             *  water under every lake in the city.
             *
             *  $161DC references neither shape, so the underground view
             *  draws no skirt at all. */
            if (o->draw_terrain && !o->underground &&
                (o->y0 + dy == R_MAP - 1 || o->x0 + dx == R_MAP - 1))
            {
                int32_t ga = rcity_alt_ground(c->altm[idx]);
                int32_t wa = rcity_alt_table(c->altm[idx]);
                int32_t k, y = sy + tile_alt(c->altm[idx], c->xter[idx]) * alt;
                /*  One d3, walked UPWARD through both loops.  $17062 and
                 *  $170D4 are the same `subi.w #$c, d3`, and $170B2
                 *  picks d3 up exactly where the dirt loop left it: so
                 *  the water faces stack on top of the dirt, they do not
                 *  hang below the tile.  Starting them at sy and walking
                 *  down is why only the bottom face ever looked right. */
                for (k = 0; k < ga; ++k, y -= alt)
                    emit_blit(&E, l->id_base + 269, sx, y, 0, -1, 1);
                if (c->xbit[idx] & 0x04u)
                    for (k = 0; k < wa - ga; ++k, y -= alt)
                        emit_blit(&E, l->id_base + 284, sx, y, 0, -1, 1);
            }

            /*  Ground.  Only a ZONE building (XBLD >= 0x70) suppresses
             *  it.  The network path at $17564 and the 0x61..0x6B group
             *  at $17366 both blit the terrain before their own tile.
             *  Get this wrong in one direction and multi-tile buildings
             *  get a square hole punched in them.  Wrong in the other
             *  and every road and band sits on a black tile. */
            b = c->xbld[idx];
            /*  XBLD 0x61..0x6B are the 2x2 elevated pieces: band curves,
             *  spurs and interchanges.  Their three non-anchor tiles
             *  fail the corner mask at $17334 and draw NOTHING, not even
             *  ground.  The anchor paints the whole footprint itself at
             *  $17366/$173B8/$1740A/$1744A.  Letting each tile draw its
             *  own ground instead means the later ones paint over the
             *  slab, which is what breaks the joins. */
            elevated = (b >= 0x61 && b < 0x6C);

            /*  A data view runs $160CA, which never reads XBLD at all.
             *  So it lays ground under EVERYTHING.  That includes the
             *  elevated 2x2 pieces.  Their anchor-paints-the-footprint
             *  trick applies to the normal view alone. $161DC emits the
             *  underground tile's shapes itself, the wireframe included.
             *  So there is no ground pass under that one. */
            ground_here = o->underground
                              ? 0
                          : (o->view != R_VIEW_NORMAL)
                              ? 1
                              : (zoom_level == 0
                                     /*  The 8 px renderer is stingier
                                      *  than the other two.  $186BA
                                      *  gates the ground pass on XBLD <
                                      *  0x1D outright.  The network
                                      *  branch it falls to, $188F4, lays
                                      *  ground only when XTER is 13: the
                                      *  case that also lifts the network
                                      *  by one altitude step.
                                      *  Everything else at that size
                                      *  carries its own ground in the
                                      *  sprite. */
                                     ? (b < 0x1D ||
                                        (b < 0x61 && c->xter[idx] == 13))
                                     : (b < 0x70 && !elevated));
            if (o->draw_terrain && ground_here)
            {
                int32_t t;
                int32_t zone = c->xzon[idx] & 0x0F;
                if (o->view != R_VIEW_NORMAL)
                {
                    t = terrain_tile(c->xter[idx]);
                    if (t == 256)
                    {
                        int32_t tint =
                            overlay_tile(c, o->y0 + dy, o->x0 + dx, o->view);
                        if (tint)
                            t = tint;
                    }
                }
                else if (b < 0x1D && !c->xter[idx] && zone)
                    /*  The zoned lot's own ground, 290 + zone.  Two paths
                     *  reach it with identical tests: $17172 for a bare
                     *  tile and $171EC for one carrying a small network
                     *  piece (XBLD 1..0x1C).  Requiring XBLD == 0 put
                     *  plain terrain under every road meet a zone. */
                    t = 290 + zone;
                else
                    t = terrain_tile(c->xter[idx]);
                if (t)
                    emit_blit(&E, l->id_base + t, sx, sy, 0, -1, 1);
            }

            /*  A data view draws terrain and tint only.  $1547A sends a
             *  non-zero $2C34 to $160CA.  That reads no XBLD at all.
             *  The buildings are not drawn over the map, they are
             *  replaced by it. */
            if (o->underground)
            {
                int32_t      u[6], udy[6];
                int          uf[6], ug[6];
                const RTile *gt = atlas_tile(
                    l, l->id_base + R_UGND_GROUND[c->xter[idx] & 0x7Fu]);
                int32_t gh = gt ? (int32_t)gt->h : l->tile_h;
                int     k, nu = underground_tiles(c, idx, mask, alt, u, udy, uf, ug);
                for (k = 0; k < nu; ++k)
                    emit_blit(&E, l->id_base + u[k], sx, sy + udy[k], uf[k], ug[k] ? gh : -1, 0);
            }

            if (!o->underground && o->view == R_VIEW_NORMAL &&
                o->draw_buildings && b &&
                draws_here(c->xzon[idx], (uint8_t)b, mask))
            {
                /*  A multi-tile building is anchored at the tile
                 *  carrying the rotation's corner bit.  This for an FxF
                 *  footprint is the leftmost tile on screen: NOT the
                 *  lowest.  Its art has to reach the bottom vertex of
                 *  the whole footprint diamond, which is (F-1)*tile_h/2
                 *  below the anchor's own diamond.  Without this every
                 *  large building floats, and the tiles it covers.
                 *  Which draw no terrain of their own.  Show through as
                 *  holes. */
                const RTile *bt = atlas_tile(l, l->id_base + b);
                int32_t      drop, road_y;
                int          flip;

                /*  The anchor of an elevated 2x2 lays its own
                 *  footprint's ground first, all four tiles at the
                 *  ANCHOR's altitude plus a fixed screen offset ($173B8,
                 *  $1740A, $1744A).  It is ground, so the terrain pass
                 *  owns it. $187FC: at 8 px the elevated branch blits
                 *  the piece and nothing else: no footprint ground under
                 *  it. */
                if (elevated && zoom_level != 0)
                {
                    static const int8_t fp[4][4] = {
                        {0, 0,  0, 0 },
                        {0, -1, 1, -1},
                        {1, -1, 2, 0 },
                        {1, 0,  1, 1 }
                    };
                    int k;
                    for (k = 0; k < 4; ++k)
                    {
                        int32_t r  = o->y0 + dy + fp[k][0];
                        int32_t cc = o->x0 + dx + fp[k][1];
                        int32_t gt;
                        if (r < 0 || r >= R_MAP || cc < 0 || cc >= R_MAP)
                            continue;
                        gt = terrain_tile(c->xter[r * R_MAP + cc]);
                        if (gt)
                            emit_blit(&E, l->id_base + gt, sx + fp[k][2] * (l->tile_w / 2), sy + fp[k][3] * (l->tile_h / 2), 0, -1, 1);
                    }
                }
                drop = (bt && !o->no_drop)
                           ? ((int32_t)bt->foot - 1) * l->tile_h / 2
                           : 0;
                /*  XBIT bit 1 is the orientation flag and it is handed
                 *  straight to the blitter as its mirror argument:
                 *  $175FE for network tiles, $178BC for zone buildings.
                 *  Without it a power line or an spur running south-east
                 *  is drawn running south-west.  On the zone path $178C8
                 *  inverts it when the rotation is odd. */
                flip = (c->xbit[idx] & 0x02u) != 0;
                if (b >= 0x70 && (c->rotation & 1))
                    flip = !flip;
                /*  An on-spur's art is which of 0x5D..0x60, flipped or
                 *  not, by the side its slab is on and the side its road
                 *  is on.  The rotation's remap leaves these four ids
                 *  alone.  XBIT's flip bit moves as a plain byte, so a
                 *  turned city drew every spur as it stood before the
                 *  turn.  The pairs are what all the shipped cities
                 *  show, at every rotation. */
                if (b >= 0x5D && b <= 0x60)
                {
                    static const int32_t DC4[4] = {0, 1, 0, -1}, DR4[4] = {-1, 0, 1, 0};
                    int                  slab = -1, road = -1, k;
                    for (k = 0; k < 4; ++k)
                    {
                        int32_t nc = o->x0 + dx + DC4[k], nr = o->y0 + dy + DR4[k];
                        uint8_t nb;
                        if (nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
                            continue;
                        nb = c->xbld[nr * R_MAP + nc];
                        if (slab < 0 && nb >= 0x49 && nb <= 0x50)
                            slab = k;
                    }
                    for (k = 0; k < 4 && slab >= 0; ++k)
                    {
                        int32_t nc = o->x0 + dx + DC4[k], nr = o->y0 + dy + DR4[k];
                        uint8_t nb;
                        if (k == slab || k == ((slab + 2) & 3) || nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
                            continue;
                        nb = c->xbld[nr * R_MAP + nc];
                        if ((nb >= 0x1D && nb <= 0x2B) || (nb >= 0x43 && nb <= 0x48))
                        {
                            road = k;
                            break;
                        }
                    }
                    if (slab >= 0 && road >= 0)
                    {
                        /* slab N: road E 5d/1, W 5e/1.  E: N 5f/0, S 60/0.  S: E 60/1, W 5f/1.  W: N 5e/0, S 5d/0 */
                        static const uint8_t ID[4][4] = {
                            {0,    0x5D, 0,    0x5E},
                            {0x5F, 0,    0x60, 0   },
                            {0,    0x60, 0,    0x5F},
                            {0x5E, 0,    0x5D, 0   }
                        };
                        static const uint8_t FL[4][4] = {
                            {0, 1, 0, 1},
                            {0, 0, 0, 0},
                            {0, 1, 0, 1},
                            {0, 0, 0, 0}
                        };
                        if (ID[slab][road])
                        {
                            b    = ID[slab][road];
                            flip = FL[slab][road] != 0;
                        }
                    }
                }
                /*  $17528: on XTER 13 the network sits one altitude step
                 *  above its tile.  `moveq #$f4, d0` sign-extends to -12,
                 *  which is the 32 px alt step: reading it as +244 is
                 *  what left this as a supposed shape offset for so long.
                 *      017528  cmpi.w #$d, d4
                 *      01752E  moveq  #$f4, d0     .  -12
                 *      017530  add.w  d3, d0
                 *      017532  move.w d0, $a(a6)   .  The network's y
                 *
                 *  The lift belongs to the NETWORK branch only ($17528,
                 *  and $188F4 at 8 px).  The elevated branch ($173B8,
                 *  $187FC) has no such test: an elevated piece sits at
                 *  its tile's own y whatever the terrain does.  Applying
                 *  it to both put every elevated piece standing on XTER
                 *  13 one altitude step too high: invisible until a city
                 *  put the two together. */
                road_y = sy + drop -
                         ((!elevated && c->xter[idx] == 13) ? l->alt_step
                                                            : 0);

                /*  $01756A: the lift span draws raised with a ship about. */
                if (b == 0x58u && bridge_is_raised(c, o->x0 + dx, o->y0 + dy))
                    b = 0x59;

                emit_blit(&E, l->id_base + b, sx, road_y, flip, -1, 0);

                if (o->draw_things && o->draw_traffic && b < 0x70)
                {
                    int     cflip = 0;
                    int32_t car   = traffic_car(c, idx, o->y0 + dy, o->x0 + dx, b, &cflip);
                    /*  $17506: the car's y is the road's plus the road's
                     *  height less the car's: their BOTTOMS line up.
                     *  The op takes each sprite's own height off, so
                     *  that is simply the road's y.  Which blitter
                     *  depends on the piece AND on the zoom.  At 32 px
                     *  the two car blocks are different calls: $17514
                     *  (elevated, XBLD 0x61..0x6B) uses the plain $18E96
                     *  while $17704 (roads and surface bands) uses the
                     *  stencilled $19004: an elevated slab is not fill,
                     *  so stencilling there would delete every car on
                     *  the band.  That split is 32 px ONLY.  At 16 and 8
                     *  px both car sites go to $19004: $188E8 sits
                     *  directly after the 8 px elevated test at $187EC
                     *  and still stencils, and neither 16 px site
                     *  ($17F8A, $18172) uses $18E96 at all.  Carrying
                     *  the 32 px rule down left cars painted over the
                     *  slab edges. */
                    if (car)
                        emit_car(&E, l->id_base + car, sx, road_y, cflip, (zoom_level == 2 && b >= 0x61 && b < 0x6C) ? -1 : R_ROAD_INDEX, l->id_base + b, sx, road_y, flip);
                }

                /*  The no-power marker.  Three branches of the tile
                 *  renderer end the same way ($172B6, $17720, $1790C,
                 *  all branching to the common exit at $17948).  If the
                 *  tile CONDUCTS power (XBIT 0x80) but is not SUPPLIED
                 *  (0x40 clear), stamp tile 386 over it.  It goes after
                 *  the traffic, so it sits on top of a car.
                 *
                 *      0172B6  andi.w #$c0, d0
                 *      0172BA  cmpi.w #$80, d0
                 *      0172C8  moveq  #$f0, d0     .  -16 = -(tile_w/2)
                 *      0172CA  add.w  d3, d0       .  The TILE's y, not
                 *      0172CE  ...                 ; the network's
                 *      0172DC  d1 = width of the sprite just drawn
                 *      0172E0  lsr.w #1, d1        .  ...halved, added to x
                 *
                 *  The y goes straight to $18E96.  So it is a top-left
                 *  and the sprite's own height is NOT subtracted: hence
                 *  the rise of 0 rather than the default. */
                if ((c->xbit[idx] & 0xC0u) == 0x80u)
                {
                    int32_t off = arc_half_wi(l->tile_w);
                    emit_blit(&E, l->id_base + R_NO_POWER, sx - off + (bt ? (int32_t)bt->w / 2 : 0), sy - off, 0, 0, 0);
                }
            }

            /*  Things last.  $FABA, which looks the tile's thing up via
             *  $399D8 and calls $A032, is invoked at the END of the
             *  per-tile renderer, after the terrain and the network.  So
             *  a train draws OVER the thread it runs on.  Drawing it
             *  earlier put the network back on top of the vehicle. */
            if (o->draw_things && o->view == R_VIEW_NORMAL &&
                !o->underground && thing_at[idx] >= 0)
            {
                const uint8_t *rec = c->xthg + (size_t)thing_at[idx] * 12u;
                /*  Road vehicles, types 10 and 11, stay off under the 3D
                 *  networks, which carry their own. */
                if (o->draw_traffic || (rec[0] != 10 && rec[0] != 11))
                    draw_thing(&E, c, idx, rec, zoom_level, sx, sy);
            }
        }
    }
    if (E.oom)
        return -1;

    info->w          = W;
    info->h          = H;
    info->ox         = ox;
    info->oy         = oy;
    info->zoom_level = zoom_level;
    info->level      = l;
    info->focus_x    = g_focus_x;
    info->focus_y    = g_focus_y;
    info->focus_ok   = g_focus_ok;
    return 0;
}

/*  The same image as a palette PNG.  Every pixel the renderer emits
 *  comes from the 256-entry game palette.  Letting lodepng fold it back
 *  to an indexed image is lossless and roughly a third the size.  Which
 *  is what makes a sheet of full-map previews fit inside one page. */
int soft_focus_result(int32_t *x, int32_t *y)
{
    if (!g_focus_ok)
        return 0;
    *x = g_focus_x;
    *y = g_focus_y;
    return 1;
}
