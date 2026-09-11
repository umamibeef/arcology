/*  api_fit.c: `arc.fit`: the corridor fit, as a service a script calls.
 *
 *      local pieces = arc.fit(points, radii, budgets)
 *
 *  `points` is the path: a Lua sequence of {x = , y = } the script
 *  invented, of its own, from anything it likes.  `radii` is how wide a
 *  corner may sweep, one to a point or one number for all of them.  A
 *  corner given nought is left as a corner.  `budgets` is how much of
 *  its two edges each corner may spend on tangent.  Where it is left out
 *  each corner gets its share of the shorter edge, which is what the
 *  pipeline's own fits hand over.
 *
 *  What comes back is a sequence of pieces.  Straights and arcs laid end
 *  to end.  Which is what the loft draws along:
 *
 *      {arc = false, ax, ay, bx, by, len} a straight {arc = true, ax,
 *      ay, bx, by, cx, cy, r, t0, t1, len} an arc
 *
 *  The cut itself is arc.rules.pieces.  So a script that calls this asks
 *  the same question the pipeline's own passes ask, and gets the same
 *  answer.  It may ask at any moment: nothing here reads a pass's state,
 *  and there need be no build running at all. */
#include <math.h>
#include <string.h>

#include "internal.h"
#include "script.h"

#include "pipeline.h"

/*  The cut itself: the chain handed to arc.rules.pieces as the drive
 *  hands it every other, and the pieces it laid taken back.  This is the
 *  one place outside the drive that asks a rule.  It asks because a
 *  SCRIPT asked: arc.fit is a service, so the question travels back out
 *  to the script that started it. */
static int cut_pieces(const V2 *q, int n, const float *rad, const float *tlim, Piece *out, int *count)
{
    PieceFan p;
    memset(&p, 0, sizeof p);
    p.q    = q;
    p.rad  = rad;
    p.tlim = tlim;
    p.n    = n;
    p.out  = out;
    p.cur  = q[0];
    script_rule_object("pieces", "pieces", &p);
    if (p.over)
        return -1;
    *count = p.np;
    return 0;
}

/*  One {x = , y = } off the sequence at `t`, at Lua's own index. */
static V2 point_at(lua_State *L, int t, int i)
{
    V2 p = {0.0f, 0.0f};
    lua_rawgeti(L, t, i);
    if (lua_istable(L, -1))
    {
        p.x = api_field_num(L, "x", 0.0f);
        p.y = api_field_num(L, "y", 0.0f);
    }
    lua_pop(L, 1);
    return p;
}

/*  A per-point number: the sequence's own entry, the one number the
 *  argument is, or `def` where there is neither. */
/*  A per-point number: the sequence's own entry, the one number the
 *  argument is, or nought where there is neither. */
static float num_at(lua_State *L, int t, int i)
{
    float v = 0.0f;
    if (lua_isnumber(L, t))
        return (float)lua_tonumber(L, t);
    if (!lua_istable(L, t))
        return 0.0f;
    lua_rawgeti(L, t, i);
    if (lua_isnumber(L, -1))
        v = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

/*  An arc carries its center and its two angles, and the pipeline reads
 *  it that way.  The ends it runs between are left where a straight
 *  keeps its own.  They are filled in here.  So a script has `ax,ay ->
 *  bx,by` on every piece, whatever kind it is.  It can lay them end to
 *  end without knowing which is which. */
static void piece_push(lua_State *L, const Piece *p, int at)
{
    V2 a = p->a, b = p->b;
    if (p->arc)
    {
        a = (V2){p->c.x + p->r * cosf(p->t0), p->c.y + p->r * sinf(p->t0)};
        b = (V2){p->c.x + p->r * cosf(p->t1), p->c.y + p->r * sinf(p->t1)};
    }
    lua_createtable(L, 0, 11);
    lua_pushboolean(L, p->arc), lua_setfield(L, -2, "arc");
    lua_pushnumber(L, a.x), lua_setfield(L, -2, "ax");
    lua_pushnumber(L, a.y), lua_setfield(L, -2, "ay");
    lua_pushnumber(L, b.x), lua_setfield(L, -2, "bx");
    lua_pushnumber(L, b.y), lua_setfield(L, -2, "by");
    lua_pushnumber(L, p->len), lua_setfield(L, -2, "len");
    if (p->arc)
    {
        lua_pushnumber(L, p->c.x), lua_setfield(L, -2, "cx");
        lua_pushnumber(L, p->c.y), lua_setfield(L, -2, "cy");
        lua_pushnumber(L, p->r), lua_setfield(L, -2, "r");
        lua_pushnumber(L, p->t0), lua_setfield(L, -2, "t0");
        lua_pushnumber(L, p->t1), lua_setfield(L, -2, "t1");
    }
    lua_rawseti(L, -2, at);
}

static int l_fit(lua_State *L)
{
    static V2    q[MAX_PTS];
    static float rad[MAX_PTS], tlim[MAX_PTS];
    static Piece out[MAX_PIECES];
    int          n, i, np = 0;
    luaL_checktype(L, 1, LUA_TTABLE);
    n = (int)lua_rawlen(L, 1);
    if (n > MAX_PTS)
        n = MAX_PTS;
    /*  Two points are a straight and one is nothing at all: a path the
     *  fit cannot cut answers an empty sequence rather than a fault.  So
     *  a script may hand over what its own walk produced without
     *  counting it first. */
    if (n < 2)
    {
        lua_createtable(L, 0, 0);
        return 1;
    }
    for (i = 0; i < n; ++i)
        q[i] = point_at(L, 1, i + 1);
    for (i = 0; i < n; ++i)
        rad[i] = num_at(L, 2, i + 1);
    if (lua_isnoneornil(L, 3))
        tlim_half(q, n, tlim);
    else
        for (i = 0; i < n; ++i)
            tlim[i] = num_at(L, 3, i + 1);
    if (cut_pieces(q, n, rad, tlim, out, &np) != 0)
        np = 0;
    lua_createtable(L, np, 0);
    for (i = 0; i < np; ++i)
        piece_push(L, &out[i], i + 1);
    return 1;
}

void api_fit_open(lua_State *L)
{
    lua_pushcfunction(L, l_fit);
    lua_setfield(L, -2, "fit");
}
