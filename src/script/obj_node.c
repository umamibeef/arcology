/*  obj_node.c: A NODE, and everything drawn at one.
 *
 *  Where three or four lines meet, the box is drawn over that cell.
 *  Its outline, the margins beside its arms, and the band round it.
 *  The panel where two families lap.  The arms' turns, the threads,
 *  the signs, and a segment's dead ends. */
#include "script.h"


#include <string.h>

#include "internal.h"
#include "obj.h"
#include "mesh/internal.h"
#include "pipeline.h"
#include "net/net.h"
#include "mesh/model.h"
#include "log.h"
#include "build.h"
#include "incr.h"
#include "mesh/shapes.h"
#include "mesh/emit.h"
#include "mesh/fit.h"
#include "mesh/loft.h"
#include "mesh/piece.h"
#include "mesh/surface.h"
#include "net/cell.h"
#include "walk/cursor.h"
#include "walk/walk.h"
#include "net/walkway.h"

/*  ---- a junction ---------------------------------------------------- */

static JuncFan *junc_of(lua_State *L)
{
    return (JuncFan *)rec_of(L, "junction");
}

/*  What the junction IS: where its middle sits, the height its tile was
 *  leveled to, how many points its outline came to.  What its fill is
 *  laid in. */
static int api_junction_info(lua_State *L)
{
    JuncFan *j = junc_of(L);
    if (!j)
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, j->cx), lua_setfield(L, -2, "x");
    lua_pushnumber(L, j->cy), lua_setfield(L, -2, "y");
    lua_pushnumber(L, j->zj), lua_setfield(L, -2, "z");
    lua_pushnumber(L, j->mat), lua_setfield(L, -2, "mat");
    lua_pushnumber(L, j->order), lua_setfield(L, -2, "order");
    lua_pushinteger(L, j->np), lua_setfield(L, -2, "n");
    lua_pushinteger(L, j->jb->col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, j->jb->row), lua_setfield(L, -2, "row");
    lua_pushboolean(L, j->square), lua_setfield(L, -2, "square");
    return 1;
}

static int api_junction_count(lua_State *L)
{
    JuncFan *j = junc_of(L);
    if (!j)
        return 0;
    lua_pushinteger(L, j->np);
    return 1;
}

static int api_junction_at(lua_State *L)
{
    JuncFan *j = junc_of(L);
    int      i = (int)luaL_checkinteger(L, 2);
    if (!j || i < 0 || i >= j->np)
        return 0;
    lua_pushnumber(L, j->poly[i].x);
    lua_pushnumber(L, j->poly[i].y);
    return 2;
}

/*  The ground the junction's fill sits on at a point: its own tile is a
 *  leveled pad.  So inside it every height is that flat one.  Where the
 *  outline reaches past the tile the fill follows the ground. */
static int api_junction_surface(lua_State *L)
{
    JuncFan *j = junc_of(L);
    if (!j)
        return 0;
    lua_pushnumber(L, junc_surface(j->jb->f, j->jb->c, j->jb->mask_bit, j->jb->col, j->jb->row,
                                   (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), j->zj));
    return 1;
}

/*  One triangle of the fill, laid ON the drawn surface: the fan's own
 *  heights are a guide.  Each piece takes the surface where it lands, so
 *  a fan across a tile's fold does not cut under the terrain. */
static int api_junction_tri(lua_State *L)
{
    JuncFan *j = junc_of(L);
    float    t[3][3];
    float    rc[3], ref[3] = {0.5f, 0.5f, 0.5f}, ref2[3] = {-1.0f, -1.0f, -1.0f};
    int      i;
    if (!j)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    for (i = 0; i < 3; ++i)
    {
        t[i][0] = (float)luaL_checknumber(L, i * 3 + 2);
        t[i][1] = (float)luaL_checknumber(L, i * 3 + 3);
        t[i][2] = (float)luaL_checknumber(L, i * 3 + 4);
    }
    rc[0] = 0.0f, rc[1] = 0.0f, rc[2] = j->mat;
    lua_pushboolean(L, put_tri_ground(j->jb->m, j->jb->c, j->jb->mask_bit, j->order,
                                      (const float (*)[3])t, rc, ref, ref2) == 0);
    return 1;
}

/*  The square the box always was, for an outline that came to nothing. */
static int api_junction_quad(lua_State *L)
{
    JuncFan *j = junc_of(L);
    if (!j || !j->a0)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, strip_quad_z(j->jb->m, j->jb->c, j->jb->mask_bit, j->order,
                                    j->a0, j->a1, j->b0, j->b1, j->zj, j->zj,
                                    0.5f, 0.5f, 0.5f, 0.5f, -1.0f, -1.0f, j->mat) == 0);
    return 1;
}

const luaL_Reg JUNCTION[] = {
    {"info",    api_junction_info   },
    {"count",   api_junction_count  },
    {"at",      api_junction_at     },
    {"surface", api_junction_surface},
    {"tri",     api_junction_tri    },
    {"quad",    api_junction_quad   },
    {NULL,      NULL     }
};

static WalkFan *walk_of(lua_State *L)
{
    return (WalkFan *)rec_of(L, "margin");
}

/*  What the band IS.
 *
 *      Which kind it is.
 *      How many stations the network holds for it.
 *      How wide it is.
 *      Where it sits in the stack.
 *      Whether it lies on the ground.
 *      And.
 *      For a meet.
 *      The depth it asked for.
 *
 *  This the material paints the stop line against. */
static int api_margin_info(lua_State *L)
{
    WalkFan        *f = walk_of(L);
    const WalkPath *w;
    if (!f)
        return 0;
    w = (const WalkPath *)f->w;
    lua_newtable(L);
    lua_pushstring(L, w->kind == WALK_SIDE ? "side" : w->kind == WALK_CORNER ? "corner"
                                                  : w->kind == WALK_CROSS  ? "meet"
                                                                           : "cap"),
        lua_setfield(L, -2, "band");
    lua_pushinteger(L, w->nst), lua_setfield(L, -2, "n");
    lua_pushnumber(L, w->w), lua_setfield(L, -2, "width");
    lua_pushnumber(L, w->ask), lua_setfield(L, -2, "asked");
    lua_pushnumber(L, w->order), lua_setfield(L, -2, "order");
    lua_pushboolean(L, w->drape), lua_setfield(L, -2, "drape");
    lua_pushinteger(L, w->col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, w->row), lua_setfield(L, -2, "row");
    return 1;
}

static int api_margin_count(lua_State *L)
{
    WalkFan *f = walk_of(L);
    if (!f)
        return 0;
    lua_pushinteger(L, ((const WalkPath *)f->w)->nst);
    return 1;
}

/*  One cross-section: the band's outer edge, its inner one and the
 *  height the network recorded there. */
static int api_margin_at(lua_State *L)
{
    WalkFan      *f = walk_of(L);
    const WalkSt *st;
    int           i = (int)luaL_checkinteger(L, 2);
    if (!f || i < 0 || i >= ((const WalkPath *)f->w)->nst)
        return 0;
    st = &((const WalkSt *)f->st)[i];
    lua_pushnumber(L, st->outer.x);
    lua_pushnumber(L, st->outer.y);
    lua_pushnumber(L, st->inner.x);
    lua_pushnumber(L, st->inner.y);
    lua_pushnumber(L, st->z);
    return 5;
}

/*  One quad of the band, cut on the tile folds like every other. */
static int api_margin_quad(lua_State *L)
{
    WalkFan *f = walk_of(L);
    float    a0[2], a1[2], b0[2], b1[2];
    if (!f)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    a0[0] = (float)luaL_checknumber(L, 2), a0[1] = (float)luaL_checknumber(L, 3);
    a1[0] = (float)luaL_checknumber(L, 4), a1[1] = (float)luaL_checknumber(L, 5);
    b0[0] = (float)luaL_checknumber(L, 6), b0[1] = (float)luaL_checknumber(L, 7);
    b1[0] = (float)luaL_checknumber(L, 8), b1[1] = (float)luaL_checknumber(L, 9);
    lua_pushboolean(L, strip_quad_z((RMesh *)f->m, (const RCity *)f->c, f->mask_bit,
                                    (float)luaL_checknumber(L, 16),
                                    a0, a1, b0, b1,
                                    (float)luaL_checknumber(L, 10), (float)luaL_checknumber(L, 11),
                                    (float)luaL_checknumber(L, 12), (float)luaL_checknumber(L, 13),
                                    (float)luaL_checknumber(L, 12), (float)luaL_checknumber(L, 13),
                                    (float)luaL_checknumber(L, 14), (float)luaL_checknumber(L, 15),
                                    (float)luaL_checknumber(L, 17)) == 0);
    return 1;
}

/*  The two ends of the band, and the ground under each: what the network
 *  joins it to its neighbors by.  What a join is drawn between when the
 *  band itself has no line down it. */
static int api_margin_ends(lua_State *L)
{
    WalkFan        *f = walk_of(L);
    const WalkPath *w;
    if (!f)
        return 0;
    w = (const WalkPath *)f->w;
    lua_pushnumber(L, w->end[0].x), lua_pushnumber(L, w->end[0].y);
    lua_pushnumber(L, w->end[1].x), lua_pushnumber(L, w->end[1].y);
    lua_pushnumber(L, surface_at_world((const RCity *)f->c, f->mask_bit, w->end[0].x, w->end[0].y));
    lua_pushnumber(L, surface_at_world((const RCity *)f->c, f->mask_bit, w->end[1].x, w->end[1].y));
    return 6;
}

/*  A hairline of the network.  It is a thin quad in the vehicle
 *  material, and its across carries the paint.  It sits a hair above the
 *  ground, so it reads over whatever it crosses. */
static int api_margin_wire(lua_State *L)
{
    WalkFan *f = walk_of(L);
    float    q0[2], q1[2], r0[2], r1[2];
    float    ax, ay, bx, by, za, zb, paint, wide, over, slot;
    float    dx, dy, dl, px, py;
    int32_t  tc, tr;
    if (!f)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    ax = (float)luaL_checknumber(L, 2), ay = (float)luaL_checknumber(L, 3);
    bx = (float)luaL_checknumber(L, 4), by = (float)luaL_checknumber(L, 5);
    za = (float)luaL_checknumber(L, 6), zb = (float)luaL_checknumber(L, 7);
    paint = (float)luaL_checknumber(L, 8);
    wide = (float)luaL_checknumber(L, 9);
    over = (float)luaL_checknumber(L, 10);
    slot = (float)luaL_checknumber(L, 11);
    dx = bx - ax, dy = by - ay, dl = sqrtf(dx * dx + dy * dy);
    if (dl < 1e-5f)
    {
        lua_pushboolean(L, 1);
        return 1;
    }
    px = -dy / dl * wide, py = dx / dl * wide;
    q0[0] = ax - px, q0[1] = ay - py;
    q1[0] = ax + px, q1[1] = ay + py;
    r0[0] = bx - px, r0[1] = by - py;
    r1[0] = bx + px, r1[1] = by + py;
    tc = (int32_t)floorf(ax), tr = (int32_t)floorf(ay);
    lua_pushboolean(L, strip_quad_z((RMesh *)f->m, (const RCity *)f->c, f->mask_bit,
                                    tile_order((const RCity *)f->c, tc, tr, f->mask_bit) + slot,
                                    q0, q1, r0, r1, za + over, zb + over,
                                    paint, paint, paint, paint, 0.0f, 0.0f, MAT_VEHICLE) == 0);
    return 1;
}

const luaL_Reg MARGIN[] = {
    {"ends",  api_margin_ends },
    {"wire",  api_margin_wire },
    {"info",  api_margin_info },
    {"count", api_margin_count},
    {"at",    api_margin_at   },
    {"quad",  api_margin_quad },
    {NULL,    NULL   }
};

/*  ---- a level meet's panel -------------------------------------- */

static LapFan *lap_of(lua_State *L)
{
    return (LapFan *)rec_of(L, "panel");
}

static const Field PANEL_FIELDS[] = {
    {"order", FLD_NUM,  offsetof(LapFan, order)},
    {"lift",  FLD_NUM,  offsetof(LapFan, lift)},
    {"slot",  FLD_NUM,  offsetof(LapFan, slot)},
    {NULL, FLD_NUM, 0}
};

static int api_panel_info(lua_State *L)
{
    return api_fields(L, lap_of(L), PANEL_FIELDS);
}

/*  Corner k of the panel, 1 to 4, and the ground under it. */
static int api_panel_at(lua_State *L)
{
    LapFan *f = lap_of(L);
    int      k = (int)luaL_checkinteger(L, 2);
    if (!f || k < 0 || k > 3)
        return 0;
    lua_pushnumber(L, f->q[k][0]);
    lua_pushnumber(L, f->q[k][1]);
    lua_pushnumber(L, f->ground[k]);
    return 3;
}

/*  The panel itself.  It is one quad over the line it interrupts.  Its
 *  heights come from the higher of each end's two corners.  So that on a
 *  tile tilting across the line it stands on the ground and not under
 *  it. */
static int api_panel_quad(lua_State *L)
{
    LapFan *f = lap_of(L);
    float    a0[2], a1[2], b0[2], b1[2];
    if (!f)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    a0[0] = f->q[0][0], a0[1] = f->q[0][1];
    a1[0] = f->q[1][0], a1[1] = f->q[1][1];
    b0[0] = f->q[2][0], b0[1] = f->q[2][1];
    b1[0] = f->q[3][0], b1[1] = f->q[3][1];
    lua_pushboolean(L, strip_quad_z(f->m, f->c, f->mask_bit, (float)luaL_checknumber(L, 4),
                                    a0, a1, b0, b1,
                                    (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                                    -1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f, MAT_XPANEL) == 0);
    return 1;
}

const luaL_Reg LAP[] = {
    {"info", api_panel_info},
    {"at",   api_panel_at  },
    {"quad", api_panel_quad},
    {NULL,   NULL  }
};

/*  ---- a junction's outline ------------------------------------------ */

static OutlineFan *outline_of(lua_State *L)
{
    return (OutlineFan *)rec_of(L, "outline");
}

/*  The numbers the junction is sized by.  They are its middle, and the
 *  half width of the band that runs through it.  They also say how far
 *  out a corner may stand.  How much the family has been let out from
 *  the width it was tuned at.  The cap on how far an arm may be cut back
 *  for the junction's sake. */
static const Field OUTLINE_FIELDS[] = {
    {"col",   FLD_INT,  offsetof(OutlineFan, col)},
    {"row",   FLD_INT,  offsetof(OutlineFan, row)},
    {"x",     FLD_NUM,  offsetof(OutlineFan, cx)},
    {"y",     FLD_NUM,  offsetof(OutlineFan, cy)},
    {"half",  FLD_NUM,  offsetof(OutlineFan, w)},
    {"far",   FLD_NUM,  offsetof(OutlineFan, far)},
    {"grow",  FLD_NUM,  offsetof(OutlineFan, gro)},
    {"cap",   FLD_NUM,  offsetof(OutlineFan, cap)},
    {"lips", FLD_BOOL, offsetof(OutlineFan, lips)},
    {"n",     FLD_INT,  offsetof(OutlineFan, na)},
    {NULL, FLD_NUM, 0}
};

static int api_outline_info(lua_State *L)
{
    return api_fields(L, outline_of(L), OUTLINE_FIELDS);
}

/*  One arm: where its own path starts, the way it leaves, the angle that
 *  makes, and which of the tile's four edges it belongs to. */
static int api_outline_arm(lua_State *L)
{
    OutlineFan *o = outline_of(L);
    int         i = (int)luaL_checkinteger(L, 2);
    if (!o || i < 0 || i >= o->na)
        return 0;
    lua_pushnumber(L, o->arm[i].ox), lua_pushnumber(L, o->arm[i].oy);
    lua_pushnumber(L, o->arm[i].dx), lua_pushnumber(L, o->arm[i].dy);
    lua_pushnumber(L, o->arm[i].ang);
    lua_pushinteger(L, o->arm[i].e);
    return 6;
}

/*  One point of the ring, with the arm's own tag where it is a mouth.
 *  The same point twice is no edge, so a repeat is dropped. */
static int api_outline_point(lua_State *L)
{
    OutlineFan *o = outline_of(L);
    float       x, y;
    int         tag;
    if (!o)
        return 0;
    x   = (float)luaL_checknumber(L, 2);
    y   = (float)luaL_checknumber(L, 3);
    tag = (int)luaL_optinteger(L, 4, 0);
    if (o->n >= o->max)
        return 0;
    if (o->n > 0 && fabsf(o->out[o->n - 1].x - x) < 1e-5f && fabsf(o->out[o->n - 1].y - y) < 1e-5f)
        return 0;
    if (o->mouth)
        o->mouth[o->n] = (uint8_t)tag;
    o->out[o->n].x = x, o->out[o->n].y = y;
    ++o->n;
    return 0;
}

/*  How far along the arm on that edge its strip starts. */
static int api_outline_trim(lua_State *L)
{
    OutlineFan *o = outline_of(L);
    int         e = (int)luaL_checkinteger(L, 2);
    if (!o || e < 0 || e > 3)
        return 0;
    o->trim[e] = (float)luaL_checknumber(L, 3);
    return 0;
}

/*  A corner pulled in for standing further out than a junction reaches,
 *  which the outline report counts. */
static int api_outline_clamped(lua_State *L)
{
    if (outline_of(L))
        junction_outline_clamped((float)luaL_checknumber(L, 2));
    return 0;
}

/*  The arms as the script ordered them, given back so the box's mouths
 *  and the ring agree about which arm is which. */
static int api_outline_order(lua_State *L)
{
    OutlineFan *o = outline_of(L);
    int         i, n;
    if (!o || !lua_istable(L, 2))
        return 0;
    n = (int)luaL_checkinteger(L, 3);
    if (n > 4)
        n = 4;
    for (i = 0; i < n; ++i)
    {
        lua_rawgeti(L, 2, i);
        o->arm[i].ox  = api_field_num(L, "ox", 0.0f);
        o->arm[i].oy  = api_field_num(L, "oy", 0.0f);
        o->arm[i].dx  = api_field_num(L, "dx", 0.0f);
        o->arm[i].dy  = api_field_num(L, "dy", 0.0f);
        o->arm[i].ang = api_field_num(L, "ang", 0.0f);
        o->arm[i].e   = (int)api_field_num(L, "e", 0.0f);
        lua_pop(L, 1);
    }
    o->na = n;
    return 0;
}

/*  How many points the ring has come to, and how far the last of them
 *  is from a point the walk is thinking of adding. */
static int api_outline_count(lua_State *L)
{
    OutlineFan *o = outline_of(L);
    if (!o)
        return 0;
    lua_pushinteger(L, o->n);
    return 1;
}

static int api_outline_back(lua_State *L)
{
    OutlineFan *o = outline_of(L);
    float       x, y;
    if (!o || o->n < 1)
        return 0;
    x = (float)luaL_checknumber(L, 2) - o->out[o->n - 1].x;
    y = (float)luaL_checknumber(L, 3) - o->out[o->n - 1].y;
    lua_pushnumber(L, sqrtf(x * x + y * y));
    return 1;
}

/*  A closed ring: the last point must not repeat the first. */
static int api_outline_close(lua_State *L)
{
    OutlineFan *o = outline_of(L);
    if (!o)
        return 0;
    while (o->n > 1 && fabsf(o->out[o->n - 1].x - o->out[0].x) < 1e-5f &&
           fabsf(o->out[o->n - 1].y - o->out[0].y) < 1e-5f)
        --o->n;
    return 0;
}

const luaL_Reg OUTLINE[] = {
    {"order", api_outline_order},
    {"count", api_outline_count},
    {"back",  api_outline_back },
    {"close", api_outline_close},
    {"info",    api_outline_info   },
    {"arm",     api_outline_arm    },
    {"point",   api_outline_point  },
    {"trim",    api_outline_trim   },
    {"clamped", api_outline_clamped},
    {NULL,      NULL     }
};

/*  ---- a junction's band --------------------------------------------- */

static BandFan *band_of(lua_State *L)
{
    return (BandFan *)rec_of(L, "band");
}

static int api_band_info(lua_State *L)
{
    BandFan *b = band_of(L);
    if (!b)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, b->np), lua_setfield(L, -2, "n");
    lua_pushnumber(L, b->lw), lua_setfield(L, -2, "width");
    lua_pushinteger(L, b->jb->col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, b->jb->row), lua_setfield(L, -2, "row");
    return 1;
}

/*  Point i of the ring. */
static int api_band_at(lua_State *L)
{
    BandFan *b = band_of(L);
    int      i = (int)luaL_checkinteger(L, 2);
    if (!b || i < 0 || i >= b->np)
        return 0;
    lua_pushnumber(L, b->poly[i].x), lua_pushnumber(L, b->poly[i].y);
    return 2;
}

/*  One arm's cut across the junction: whether the arm is there at all,
 *  and its two corners, which the ring's own mouth edge spans. */
static int api_band_arm(lua_State *L)
{
    BandFan *b = band_of(L);
    int      e = (int)luaL_checkinteger(L, 2);
    if (!b || e < 0 || e > 3 || !b->arms)
        return 0;
    lua_pushboolean(L, b->arms[e].have);
    lua_pushnumber(L, b->arms[e].a.x), lua_pushnumber(L, b->arms[e].a.y);
    lua_pushnumber(L, b->arms[e].b.x), lua_pushnumber(L, b->arms[e].b.y);
    return 5;
}

/*  What edge i carries: a band or not, the way it faces into the
 *  junction.  The arm it is the mouth of, or -1 for none. */
static int api_band_edge(lua_State *L)
{
    BandFan *b = band_of(L);
    int      i = (int)luaL_checkinteger(L, 2);
    if (!b || i < 0 || i >= b->np)
        return 0;
    b->band[i]     = (uint8_t)lua_toboolean(L, 3);
    b->nrm[i].x    = (float)luaL_checknumber(L, 4);
    b->nrm[i].y    = (float)luaL_checknumber(L, 5);
    b->edge_arm[i] = (int8_t)luaL_checkinteger(L, 6);
    if (b->mitre0)
    {
        b->mitre0[i].x = (float)luaL_optnumber(L, 7, 0.0);
        b->mitre0[i].y = (float)luaL_optnumber(L, 8, 0.0);
        b->mitre1[i].x = (float)luaL_optnumber(L, 9, 0.0);
        b->mitre1[i].y = (float)luaL_optnumber(L, 10, 0.0);
    }
    return 0;
}

/*  One point of the ring moved in by the margin's width. */
static int api_band_inset(lua_State *L)
{
    BandFan *b = band_of(L);
    float    x, y;
    if (!b || !b->inset)
        return 0;
    x = (float)luaL_checknumber(L, 2);
    y = (float)luaL_checknumber(L, 3);
    if (b->inset_n >= b->inset_max)
        return 0;
    if (b->inset_n > 0 && fabsf(b->inset[b->inset_n - 1].x - x) < 1e-5f &&
        fabsf(b->inset[b->inset_n - 1].y - y) < 1e-5f)
        return 0;
    b->inset[b->inset_n].x = x, b->inset[b->inset_n].y = y;
    ++b->inset_n;
    return 0;
}

/*  A closed ring: the last point must not repeat the first. */
static int api_band_close(lua_State *L)
{
    BandFan *b = band_of(L);
    if (!b || !b->inset)
        return 0;
    while (b->inset_n > 1 && fabsf(b->inset[b->inset_n - 1].x - b->inset[0].x) < 1e-5f &&
           fabsf(b->inset[b->inset_n - 1].y - b->inset[0].y) < 1e-5f)
        --b->inset_n;
    return 0;
}

const luaL_Reg BAND[] = {
    {"info",  api_band_info },
    {"at",    api_band_at   },
    {"arm",   api_band_arm  },
    {"edge",  api_band_edge },
    {"inset", api_band_inset},
    {"close", api_band_close},
    {NULL,    NULL   }
};

/*  ---- the arms of a junction ---------------------------------------------
 *
 *  THE PATTERN AN INTERSECTION DRAWS is arc.rules.turns's: which arm's
 *  inbound lane joins which arm's outbound lane.  The arms are offered as
 *  the ports stage measured them, and each pair the rule wants is routed
 *  and queued for the drive to cut.  There is no matcher in C behind it:
 *  a rule that wants no pair draws a junction whose arms meet nothing. */
static int api_turns_info(lua_State *L)
{
    const char *fam;
    int         col, row, arms;
    if (!rec_of(L, "turns"))
        return 0;
    lane_turns_info(&col, &row, &arms, &fam);
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, row), lua_setfield(L, -2, "row");
    lua_pushinteger(L, arms), lua_setfield(L, -2, "arms");
    lua_pushstring(L, fam), lua_setfield(L, -2, "family");
    return 1;
}

/*  One arm: the lanes it carries, whether a lane may enter the junction
 *  along it and whether one may leave.  Whether it is a spur.  1 is an
 *  arm every other arm's outermost lane may use.  2 is one only the lane
 *  arriving straight at it uses.  Nothing for an arm that is not there. */
static int api_turns_arm(lua_State *L)
{
    int lanes, into, out, spur;
    if (!rec_of(L, "turns") ||
        !lane_turns_arm((int)luaL_checkinteger(L, 2), &lanes, &into, &out, &spur))
        return 0;
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, lanes), lua_setfield(L, -2, "lanes");
    lua_pushboolean(L, into), lua_setfield(L, -2, "into");
    lua_pushboolean(L, out), lua_setfield(L, -2, "out");
    lua_pushinteger(L, spur), lua_setfield(L, -2, "spur");
    return 1;
}

/*  A connector wanted, from arm e's inbound lane k to arm e2's outbound
 *  lane k2.  Answers whether the junction has that pair to give. */
static int api_turns_want(lua_State *L)
{
    if (!rec_of(L, "turns"))
        return 0;
    lua_pushboolean(L, lane_turns_want((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                                       (int)luaL_checkinteger(L, 4), (int)luaL_checkinteger(L, 5)));
    return 1;
}

/*  ---- the threads a thread junction has -------------------------------------
 *
 *  Which arm runs into which is arc.rules.node_threads's.  So is how the
 *  threads stack: a second through line lies over the first as a
 *  diamond, a wye's threads lie a gauge apart. */
static int api_threads_info(lua_State *L)
{
    int col, row, links;
    if (!rec_of(L, "threads"))
        return 0;
    net_threads_info(&col, &row, &links);
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, row), lua_setfield(L, -2, "row");
    lua_pushinteger(L, links), lua_setfield(L, -2, "links");
    lua_pushinteger(L, 4), lua_setfield(L, -2, "arms");
    return 1;
}

/*  One thread wanted, from one arm to another, raised by so much. */
static int api_threads_thread(lua_State *L)
{
    if (!rec_of(L, "threads"))
        return 0;
    lua_pushboolean(L, net_thread_want((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                                           (float)luaL_optnumber(L, 4, 0.0)));
    return 1;
}

/*  ---- a junction's signs --------------------------------------------------
 *
 *  Which arm carries what is arc.rules.junction_signs's, and it places
 *  them itself through arc.put.  The emit window is opened at the box's
 *  own order, so a sign lands in the junction's shape. */
static int api_signs_info(lua_State *L)
{
    int     e, ctrl;
    float   h;
    int32_t col, row;
    if (!rec_of(L, "signs"))
        return 0;
    lua_createtable(L, 0, 3);
    lua_createtable(L, 4, 0);
    for (e = 0; e < 4; ++e)
    {
        if (!net_junction_signs_at(e, &ctrl, &h, &col, &row))
            continue;
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, ctrl), lua_setfield(L, -2, "control");
        lua_pushinteger(L, e), lua_setfield(L, -2, "edge");
        lua_rawseti(L, -2, e);
    }
    lua_setfield(L, -2, "arms");
    if (net_junction_signs_at(0, &ctrl, &h, &col, &row) ||
        net_junction_signs_at(1, &ctrl, &h, &col, &row) ||
        net_junction_signs_at(2, &ctrl, &h, &col, &row) ||
        net_junction_signs_at(3, &ctrl, &h, &col, &row))
    {
        lua_pushinteger(L, col), lua_setfield(L, -2, "col");
        lua_pushinteger(L, row), lua_setfield(L, -2, "row");
        lua_pushnumber(L, h), lua_setfield(L, -2, "half");
    }
    return 1;
}

const luaL_Reg SIGNS[] = {
    {"info", api_signs_info},
    {NULL,   NULL          }
};

const luaL_Reg THREADS[] = {
    {"info",  api_threads_info },
    {"thread", api_threads_thread},
    {NULL,    NULL            }
};

const luaL_Reg TURNS[] = {
    {"info", api_turns_info},
    {"arm",  api_turns_arm },
    {"want", api_turns_want},
    {NULL,   NULL          }
};

/*  ---- a segment's dead ends ----------------------------------------------
 *
 *  What a lane does where its segment simply stops is arc.rules.cap's.
 *  The family declares which KIND of ending it has.  The rule decides
 *  the shape: drawn round the cap, named on the spot, or neither. */
static int api_caps_info(lua_State *L)
{
    const char *ends, *fam;
    int         n;
    if (!rec_of(L, "caps"))
        return 0;
    lane_caps_info(&n, &ends, &fam);
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, n), lua_setfield(L, -2, "n");
    lua_pushstring(L, ends), lua_setfield(L, -2, "ends");
    lua_pushstring(L, fam), lua_setfield(L, -2, "family");
    return 1;
}

/*  One pair: the end of the segment it lies at, the lane of it, and the
 *  two lanes: the one arriving and the one leaving. */
static int api_caps_at(lua_State *L)
{
    int end, lane, from, to;
    if (!rec_of(L, "caps") || !lane_caps_at((int)luaL_checkinteger(L, 2), &end, &lane, &from, &to))
        return 0;
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, end), lua_setfield(L, -2, "end");
    lua_pushinteger(L, lane), lua_setfield(L, -2, "lane");
    lua_pushinteger(L, from), lua_setfield(L, -2, "from");
    lua_pushinteger(L, to), lua_setfield(L, -2, "to");
    return 1;
}

static int api_caps_merge(lua_State *L)
{
    if (!rec_of(L, "caps"))
        return 0;
    lua_pushboolean(L, lane_caps_merge((int)luaL_checkinteger(L, 2)));
    return 1;
}

static int api_caps_link(lua_State *L)
{
    if (!rec_of(L, "caps"))
        return 0;
    lua_pushboolean(L, lane_caps_link((int)luaL_checkinteger(L, 2)));
    return 1;
}

const luaL_Reg CAPS[] = {
    {"info",  api_caps_info },
    {"at",    api_caps_at   },
    {"merge", api_caps_merge},
    {"link",  api_caps_link },
    {NULL,    NULL          }
};
