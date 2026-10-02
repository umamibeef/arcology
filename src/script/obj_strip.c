/*  obj_strip.c: ONE STRIP OF LINE, as a script draws it.
 *
 *  A strip is the geometry one family lays along a run of cells: one
 *  cell wide, following the ground it crosses.  It has thousands of
 *  stations, and a city has thousands of strips, so the methods that
 *  MEASURE answer plain numbers and make no table. */
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

static Loft *strip_of(lua_State *L)
{
    return (Loft *)rec_of(L, "strip");
}

/*  ---- what every object answers ------------------------------------ */

/*  The drawn surface under a point: what a band lying on the ground
 *  takes for its height. */
static int api_strip_ground(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    lua_pushnumber(L, surface_at_world(x->c, x->mask_bit, (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3)));
    return 1;
}

/*  Where a point sits in the painter's stack: its tile's own slot.  A
 *  ribbon takes the slot of the tile each of its quads lies on. */
static int api_strip_order(lua_State *L)
{
    Loft   *x = strip_of(L);
    int32_t tc, tr;
    if (!x)
        return 0;
    tc = (int32_t)floorf((float)luaL_checknumber(L, 2));
    tr = (int32_t)floorf((float)luaL_checknumber(L, 3));
    if (tc < 0)
        tc = 0;
    if (tr < 0)
        tr = 0;
    if (tc >= R_MAP)
        tc = R_MAP - 1;
    if (tr >= R_MAP)
        tr = R_MAP - 1;
    lua_pushnumber(L, tile_order(x->c, tc, tr, x->mask_bit));
    return 1;
}

/*  ---- a strip ------------------------------------------------------ */

/*  What the strip IS, as against what it is made of.  The family it
 *  belongs to, and the class it carries.  Its half width, and the
 *  material it is laid in.  How many stations it was cut into, whether
 *  it flies clear of the ground and carries its own height, whether it
 *  has margins beside it.  The meet band each end's junction has laid
 *  over it. */
static int api_strip_info(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    lua_newtable(L);
    lua_pushstring(L, x->d->fam->name), lua_setfield(L, -2, "family");
    lua_pushnumber(L, x->d->cls), lua_setfield(L, -2, "class");
    lua_pushnumber(L, x->hw), lua_setfield(L, -2, "half");
    lua_pushnumber(L, x->mat), lua_setfield(L, -2, "mat");
    lua_pushnumber(L, x->total), lua_setfield(L, -2, "len");
    lua_pushinteger(L, x->ns), lua_setfield(L, -2, "n");
    lua_pushboolean(L, net_family_has(x->d->fam, NH_FLIES)), lua_setfield(L, -2, "flies");
    /*  A family with lips has margins beside it: unless the pass that
     *  lays them is off.  Then the way takes the whole band and there is
     *  nothing to leave room for. */
    lua_pushboolean(L, x->d->fam->lips && margin_on()), lua_setfield(L, -2, "lips");
    lua_pushboolean(L, x->d->fam->slab), lua_setfield(L, -2, "slab");
    lua_pushnumber(L, x->d->xw0), lua_setfield(L, -2, "cross0");
    lua_pushnumber(L, x->d->xw1), lua_setfield(L, -2, "cross1");
    lua_pushstring(L, x->d->fam->slot ? x->d->fam->slot : "slot_strip"), lua_setfield(L, -2, "slot");
    /*  A slab's own facts.
     *
     *      Whether it lies on the ground.
     *      Whether it is one lane wide.
     *      Whether it is a spur's concrete.
     *      Where its girder and its parapet stand. */
    lua_pushboolean(L, x->d->flat), lua_setfield(L, -2, "flat");
    lua_pushboolean(L, x->d->lane_piece), lua_setfield(L, -2, "lane_piece");
    lua_pushboolean(L, x->d->struct_), lua_setfield(L, -2, "structure");
    lua_pushnumber(L, BAND_GIRDER), lua_setfield(L, -2, "girder");
    lua_pushnumber(L, BAND_PARAPET), lua_setfield(L, -2, "parapet");
    /*  And how it narrows: the length the taper runs over, the half
     *  width it narrows to, and which end it starts from. */
    lua_pushboolean(L, x->pin1), lua_setfield(L, -2, "ahead");
    lua_pushboolean(L, x->pin0), lua_setfield(L, -2, "behind");
    lua_pushnumber(L, x->d->taper), lua_setfield(L, -2, "taper");
    lua_pushnumber(L, x->d->hw_end), lua_setfield(L, -2, "taper_to");
    lua_pushboolean(L, x->d->taper_start), lua_setfield(L, -2, "taper_start");
    return 1;
}

/*  Which class of line stands on a tile, for a strip whose own class is
 *  -1: an island of line with no segment to read it from. */
static int api_strip_line_class(lua_State *L)
{
    Loft   *x = strip_of(L);
    int32_t tc, tr;
    if (!x)
        return 0;
    /*  Held to the map: a strip whose middle falls off the edge reads
     *  the tile it ran off, not whatever lies past the array. */
    tc = (int32_t)luaL_checkinteger(L, 2);
    tr = (int32_t)luaL_checkinteger(L, 3);
    if (tc < 0)
        tc = 0;
    if (tr < 0)
        tr = 0;
    if (tc >= R_MAP)
        tc = R_MAP - 1;
    if (tr >= R_MAP)
        tr = R_MAP - 1;
    lua_pushnumber(L, (float)line_class(x->c, tc, tr));
    return 1;
}

static int api_strip_count(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    lua_pushinteger(L, x->ns);
    return 1;
}

/*  One station, as plain numbers.
 *
 *      Where it is.  The height its section was graded to.  The way the
 *      centerline runs there.  How far along the strip it stands.  The
 *      band's two half widths as fractions.  How far the nearest level
 *      meet is.  The ground's own line. */
static int api_strip_at(lua_State *L)
{
    Loft *x = strip_of(L);
    int   i = (int)luaL_checkinteger(L, 2);
    if (!x || i < 0 || i >= x->ns)
        return 0;
    lua_pushnumber(L, x->smp[i].pos.x);
    lua_pushnumber(L, x->smp[i].pos.y);
    lua_pushnumber(L, x->smp[i].z);
    lua_pushnumber(L, x->smp[i].dir.x);
    lua_pushnumber(L, x->smp[i].dir.y);
    lua_pushnumber(L, x->smp[i].s);
    lua_pushnumber(L, x->smp[i].wl);
    lua_pushnumber(L, x->smp[i].wr);
    lua_pushnumber(L, x->smp[i].xd);
    lua_pushnumber(L, script_strip_zorig(x, i));
    return 10;
}

/*  A station's half widths set: how a strip NARROWS along its length is
 *  the family's taper stage.  A rule answering it reads each station's
 *  widths through `at` and writes them back here.  Nothing is decided in
 *  the writing: it takes what it is given. */
static int api_strip_narrow(lua_State *L)
{
    Loft *x = strip_of(L);
    int   i = (int)luaL_checkinteger(L, 2);
    if (!x || i < 0 || i >= x->ns)
        return 0;
    x->smp[i].wl = (float)luaL_checknumber(L, 3);
    x->smp[i].wr = (float)luaL_checknumber(L, 4);
    return 0;
}

/*  How much narrower the band is where the centerline runs diagonally:
 *  the fit's own compensation.  This keeps a diagonal line the width it
 *  looks rather than the width it measures. */
static int api_strip_width(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    lua_pushnumber(L, width_factor((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), x->comp));
    return 1;
}

/*  The class a quad is drawn under: the material reads it.  A value four
 *  higher says the quad lies in a cut, so the clipping check knows the
 *  ground standing over it is meant. */
static int api_strip_class(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    x->m->strip_class = (float)luaL_checknumber(L, 2);
    return 0;
}

/*  One cross-section of the ribbon to the next: the strip's own quad,
 *  cut on the tile folds.  `za`/`zb` are the two ends' heights, or -1
 *  each for a band that lies on the drawn surface at every corner it is
 *  cut into. */
static int api_strip_quad(lua_State *L)
{
    Loft *x = strip_of(L);
    float a0[2], a1[2], b0[2], b1[2];
    float za, zb, ac0, ac1, ac0b, ac1b, al0, al1, mat, order;
    if (!x)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    a0[0] = (float)luaL_checknumber(L, 2), a0[1] = (float)luaL_checknumber(L, 3);
    a1[0] = (float)luaL_checknumber(L, 4), a1[1] = (float)luaL_checknumber(L, 5);
    b0[0] = (float)luaL_checknumber(L, 6), b0[1] = (float)luaL_checknumber(L, 7);
    b1[0] = (float)luaL_checknumber(L, 8), b1[1] = (float)luaL_checknumber(L, 9);
    za = (float)luaL_checknumber(L, 10), zb = (float)luaL_checknumber(L, 11);
    ac0 = (float)luaL_checknumber(L, 12), ac1 = (float)luaL_checknumber(L, 13);
    al0 = (float)luaL_checknumber(L, 14), al1 = (float)luaL_checknumber(L, 15);
    mat = (float)luaL_checknumber(L, 16);
    order = (float)luaL_checknumber(L, 17);
    /* the across at the far station, where the quad's two widths differ.  The near station's otherwise. */
    ac0b = (float)luaL_optnumber(L, 18, ac0);
    ac1b = (float)luaL_optnumber(L, 19, ac1);
    lua_pushboolean(L, strip_quad_z(x->m, x->c, x->mask_bit, order, a0, a1, b0, b1, za, zb, ac0, ac1, ac0b, ac1b, al0, al1, mat) == 0);
    return 1;
}

/*  The pair as the pipeline's own stages build it, for a check that the
 *  composition and they agree.
 *
 *      The two edges.
 *      The across range.
 *      The along.
 *      The material and the tile's slot. */
static int api_strip_pair(lua_State *L)
{
    Loft    *x = strip_of(L);
    LoftPair p;
    int      i = (int)luaL_checkinteger(L, 2);
    if (!x || i < 1 || i >= x->ns || script_strip_pair(x, i, &p) != 0)
        return 0;
    lua_pushnumber(L, p.a0[0]), lua_pushnumber(L, p.a0[1]);
    lua_pushnumber(L, p.a1[0]), lua_pushnumber(L, p.a1[1]);
    lua_pushnumber(L, p.b0[0]), lua_pushnumber(L, p.b0[1]);
    lua_pushnumber(L, p.b1[0]), lua_pushnumber(L, p.b1[1]);
    lua_pushnumber(L, p.acr), lua_pushnumber(L, p.acl);
    lua_pushnumber(L, p.al_a), lua_pushnumber(L, p.al_b);
    lua_pushnumber(L, p.ma), lua_pushnumber(L, p.order);
    return 14;
}

/*  One station of the margin beside the strip: its outer edge, its
 *  inner one where it meets the way, and the height. */
static int api_strip_walk_at(lua_State *L)
{
    if (!strip_of(L))
        return 0;
    lua_pushboolean(L, script_walk_at((int)luaL_checkinteger(L, 2),
                                      (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4),
                                      (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6),
                                      (float)luaL_checknumber(L, 7)));
    return 1;
}

/*  Where that margin's band begins and ends, which is what the network
 *  joins it to its neighbors by. */
static int api_strip_walk_ends(lua_State *L)
{
    if (!strip_of(L))
        return 0;
    script_walk_ends((int)luaL_checkinteger(L, 2),
                     (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4),
                     (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6));
    return 0;
}

/*  The pieces the fit produced, which the curve overlay draws in place
 *  of the lines: how many, how long each runs and whether it turns. */
static int api_strip_pieces(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    lua_pushinteger(L, x->np);
    return 1;
}

static int api_strip_piece(lua_State *L)
{
    Loft *x = strip_of(L);
    int   k = (int)luaL_checkinteger(L, 2);
    if (!x || k < 0 || k >= x->np)
        return 0;
    lua_pushnumber(L, x->pc[k].len);
    lua_pushboolean(L, x->pc[k].arc);
    return 2;
}

/*  Where a piece is `at` tiles along it. */
static int api_strip_piece_at(lua_State *L)
{
    Loft *x = strip_of(L);
    int   k = (int)luaL_checkinteger(L, 2);
    V2    p, d;
    if (!x || k < 0 || k >= x->np)
        return 0;
    piece_at(&x->pc[k], (float)luaL_checknumber(L, 3), &p, &d);
    lua_pushnumber(L, p.x), lua_pushnumber(L, p.y);
    lua_pushnumber(L, d.x), lua_pushnumber(L, d.y);
    return 4;
}

/*  A box standing on the ground under it: the mark at a piece boundary. */
static int api_strip_box(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, put_box(x->m, x->c, x->mask_bit, (float)luaL_checknumber(L, 2),
                               (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4),
                               (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6),
                               (float)luaL_checknumber(L, 7), (float)luaL_checknumber(L, 8),
                               (float)luaL_checknumber(L, 9), (float)luaL_checknumber(L, 10)) == 0);
    return 1;
}

/*  What a slab's station carries beside its place.  It says which of its
 *  two outer lanes a spur has taken, and the height of each where it
 *  has. */
static int api_strip_lane(lua_State *L)
{
    Loft *x = strip_of(L);
    int   i = (int)luaL_checkinteger(L, 2);
    if (!x || i < 0 || i >= x->ns)
        return 0;
    lua_pushinteger(L, x->smp[i].lane);
    lua_pushnumber(L, x->smp[i].zr[0]);
    lua_pushnumber(L, x->smp[i].zr[1]);
    return 3;
}

/*  One triangle of a structure, with a normal of its own: a slab's
 *  soffit hangs under the way and faces down. */
static int api_strip_tri_n(lua_State *L)
{
    Loft *x = strip_of(L);
    float t[3][3], nrm[3], col3[3], ref[3] = {1.0f, 1.0f, 1.0f};
    int   i;
    if (!x)
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
    nrm[0] = (float)luaL_checknumber(L, 11), nrm[1] = (float)luaL_checknumber(L, 12);
    nrm[2] = (float)luaL_checknumber(L, 13);
    col3[0] = (float)luaL_checknumber(L, 14), col3[1] = (float)luaL_checknumber(L, 15);
    col3[2] = (float)luaL_checknumber(L, 16);
    lua_pushboolean(L, put_tri_line_n(x->m, x->c, x->mask_bit, (float)luaL_checknumber(L, 17),
                                      (const float (*)[3])t, nrm, col3, ref, ref) == 0);
    return 1;
}

/*  A wall between two points, from one pair of heights down to another:
 *  a slab's end, where it begins and ends in the air. */
static int api_strip_wall(lua_State *L)
{
    Loft *x = strip_of(L);
    float a[3], b[3], qa[3], qb[3], nrm[3], col3[3];
    if (!x)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    a[0] = (float)luaL_checknumber(L, 2), a[1] = (float)luaL_checknumber(L, 3);
    b[0] = (float)luaL_checknumber(L, 4), b[1] = (float)luaL_checknumber(L, 5);
    a[2] = (float)luaL_checknumber(L, 6), b[2] = (float)luaL_checknumber(L, 7);
    qa[0] = a[0], qa[1] = a[1], qa[2] = (float)luaL_checknumber(L, 8);
    qb[0] = b[0], qb[1] = b[1], qb[2] = (float)luaL_checknumber(L, 9);
    nrm[0] = (float)luaL_checknumber(L, 10), nrm[1] = (float)luaL_checknumber(L, 11);
    nrm[2] = (float)luaL_checknumber(L, 12);
    col3[0] = (float)luaL_checknumber(L, 14), col3[1] = (float)luaL_checknumber(L, 15);
    col3[2] = (float)luaL_checknumber(L, 16);
    lua_pushboolean(L, put_wall(x->m, a, b, qa, qb, nrm, (float)luaL_checknumber(L, 13), col3) == 0);
    return 1;
}

/*  A slab's edge: the girder's fascia down from the way, in the
 *  slab's own shadow, and the parapet up from it. */
static int api_strip_edge(lua_State *L)
{
    Loft *x = strip_of(L);
    float ea[2], eb[2], nrm[3];
    if (!x)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    ea[0] = (float)luaL_checknumber(L, 2), ea[1] = (float)luaL_checknumber(L, 3);
    eb[0] = (float)luaL_checknumber(L, 4), eb[1] = (float)luaL_checknumber(L, 5);
    nrm[0] = (float)luaL_checknumber(L, 8), nrm[1] = (float)luaL_checknumber(L, 9), nrm[2] = 0.0f;
    lua_pushboolean(L, put_fascia(x->m, x->c, x->mask_bit, (float)luaL_checknumber(L, 12),
                                 ea, eb, (float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7),
                                 nrm, (float)luaL_checknumber(L, 10),
                                 lua_toboolean(L, 11)) == 0);
    return 1;
}

/*  Whether a station stands on or beside a level meet, which has its
 *  own protection: a lamp is not stood there. */
static int api_strip_near_lap(lua_State *L)
{
    Loft *x = strip_of(L);
    V2    p;
    if (!x)
        return 0;
    p.x = (float)luaL_checknumber(L, 2);
    p.y = (float)luaL_checknumber(L, 3);
    lua_pushboolean(L, marking_near_lap(x->c, p));
    return 1;
}

/*  One model stood beside the strip, standing on the strip's own height
 *  rather than the ground under the lip. */
static int api_strip_prop(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    lua_pushboolean(L, net_model_put_on(net_model_find(luaL_checkstring(L, 2)), x->m, x->c, x->mask_bit,
                                        (float)luaL_checknumber(L, 3),
                                        (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5),
                                        (float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7),
                                        0.0f, 0.0f, 0.0f,
                                        (float)luaL_checknumber(L, 8), (float)luaL_checknumber(L, 8), 1) == 0);
    return 1;
}

/*  Where along this strip a line crosses it, in tiles.  A thread family's
 *  whistle posts are placed against these.  A lamp is kept away from
 *  them.  The reading is the map's, the placing is the rule's. */
static int api_strip_laps(lua_State *L)
{
    Loft *x = strip_of(L);
    int   i, n = 0;
    if (!x)
        return 0;
    lua_newtable(L);
    for (i = 1; i < x->ns && n < 64; ++i)
    {
        int32_t col = (int32_t)floorf(x->smp[i].pos.x), row = (int32_t)floorf(x->smp[i].pos.y);
        /*  The tile the station BEFORE this one stood on: a run of
         *  stations across one tile counts once.  The count is against
         *  the previous station rather than the last tile answered.  So
         *  a path that leaves a tile and comes back to it counts it
         *  again. */
        int32_t pc = (int32_t)floorf(x->smp[i - 1].pos.x), pr = (int32_t)floorf(x->smp[i - 1].pos.y);
        if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP || (col == pc && row == pr))
            continue;
        if (!net_line_lapped(x->c->xbld[row * R_MAP + col]))
            continue;
        lua_pushnumber(L, (double)x->smp[i].s), lua_rawseti(L, -2, ++n);
    }
    return 1;
}

/*  Whether a point is on the map at all.  `order` clamps to the edge, so
 *  a prop stood without asking this would appear at the border rather
 *  than not at all. */
static int api_strip_on_map(lua_State *L)
{
    int32_t tc = (int32_t)floorf((float)luaL_checknumber(L, 2));
    int32_t tr = (int32_t)floorf((float)luaL_checknumber(L, 3));
    if (!strip_of(L))
        return 0;
    lua_pushboolean(L, tc >= 0 && tr >= 0 && tc < R_MAP && tr < R_MAP);
    return 1;
}

/*  One model stood flat on the ground beside the strip. */
static int api_strip_prop_flat(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    lua_pushboolean(L, net_model_put(net_model_find(luaL_checkstring(L, 2)), x->m, x->c, x->mask_bit,
                                     (float)luaL_checknumber(L, 3),
                                     (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5),
                                     (float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7)) == 0);
    return 1;
}

/*  And a thread signal, which is the traffic's as well as the mesh's:
 *  the model the script names stands where it said.  The signal is then
 *  registered so the block it watches can light it each frame.  The
 *  furniture switch takes the model and leaves the record: a signal that
 *  is not drawn still governs its block. */
static int api_strip_thread_signal(lua_State *L)
{
    Loft       *x     = strip_of(L);
    const char *model = luaL_checkstring(L, 2);
    float       order = (float)luaL_checknumber(L, 3);
    float       px = (float)luaL_checknumber(L, 4), py = (float)luaL_checknumber(L, 5);
    float       fx = (float)luaL_checknumber(L, 6), fy = (float)luaL_checknumber(L, 7);
    if (!x)
        return 0;
    if (furniture_on() &&
        net_model_put(net_model_find(model), x->m, x->c, x->mask_bit, order, px, py, fx, fy) != 0)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, mesh_signal_add(x->m, px, py, fx, fy,
                                            (float)luaL_checknumber(L, 9),
                                            (int)luaL_checkinteger(L, 10),
                                            (int)luaL_checkinteger(L, 8)) == 0);
    return 1;
}

const luaL_Reg STRIP[] = {
    {"lane",     api_strip_lane    },
    {"tri_n",    api_strip_tri_n   },
    {"wall",     api_strip_wall    },
    {"edge",     api_strip_edge    },
    {"pieces",   api_strip_pieces  },
    {"piece",    api_strip_piece   },
    {"piece_at", api_strip_piece_at},
    {"box",      api_strip_box     },
    {"walk_at",   api_strip_walk_at  },
    {"walk_ends", api_strip_walk_ends},
    {"pair",    api_strip_pair  },
    {"info",       api_strip_info      },
    {"line_class", api_strip_line_class},
    {"count",   api_strip_count },
    {"at",      api_strip_at    },
    {"width",   api_strip_width },
    {"narrow",  api_strip_narrow},
    {"class",   api_strip_class },
    {"quad",    api_strip_quad  },
    {"ground",  api_strip_ground},
    {"order",   api_strip_order },
    {"near_lap", api_strip_near_lap},
    {"prop",    api_strip_prop  },
    {"prop_flat", api_strip_prop_flat},
    {"on_map",  api_strip_on_map},
    {"thread_signal", api_strip_thread_signal},
    {"meets", api_strip_laps},
    {NULL,      NULL    }
};
