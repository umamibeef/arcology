/*  api_put.c -- `arc.put`: the primitives a script builds a prop from.
 *
 *  The same ones the C builds its own from, so a script's signal and the
 *  one it replaces are made of the same faces and go through the same
 *  emitter, the shape layer and the checks included.
 *
 *      arc.put.box(x, y, w, d, z0, z1, mat, phase)
 *          A box on the ground at (x, y), `w` by `d` across and from z0
 *          to z1 above the surface under it.
 *      arc.put.wire(x0, y0, z0, x1, y1, z1, sag)
 *          A wire between two points, each so far over the ground under
 *          it, dipping by `sag` in the middle.
 *      arc.put.f32(x)
 *          `x` as the mesh holds it, which is a float.  A shared edge is
 *          worked out through this so both sides of it agree.
 *      arc.put.sqrt / sin / cos / tan / acos / atan2
 *          The same functions the pipeline uses, at the pipeline's own
 *          precision: a composition that has to land on the number the C
 *          beside it reached takes these rather than Lua's own.
 *      arc.put.tri(x0,y0,z0, x1,y1,z1, x2,y2,z2, mat, along, across, slot)
 *          One triangle, laid on the drawn surface under it.  `along`
 *          and `across` are what the material reads across the face.
 *      arc.put.quad(a0x,a0y, a1x,a1y, b0x,b0y, b1x,b1y,
 *                   za, zb, across0, across1, along_a, along_b, mat, slot)
 *          One cross-section of a ribbon to the next, cut on the tile
 *          folds.  `za`/`zb` are the two ends' heights, or -1 each for a
 *          band that lies on the drawn surface at every corner it is cut
 *          into.  This is the road surface itself.
 *      arc.put.fan(cx, cy, t0, t1, r0, r1, across, mat, n, lift, slot)
 *          A fan of `n` wedges about (cx, cy), from angle t0 to t1 and
 *          radius r0 to r1: a junction's turn.
 *      arc.put.prism(cx, cy, dx, dy, len, wid, zb, zf, z0, z1, mat, paint, slot)
 *          A box along (dx, dy), cut on the tile folds, its two ends'
 *          feet at zb and zf over the surface.
 *      arc.put.cyl(cx, cy, r, z0, z1, mat, slot)
 *          A round post.
 *      arc.put.bar(x0, y0, z0, x1, y1, z1, fx, fy, w, d, mat, code)
 *          A bar between two points in the air, `w` half its width
 *          across the face it turns to (fx, fy) and `d` half its
 *          depth: a gate's arm at whatever angle it has swung to.
 *      arc.put.model(name, x, y, fx, fy, size, phase, group [, ground])
 *          One of the stored models, so a script may build a prop out
 *          of them as easily as out of boxes.  Given a `ground` it
 *          stands on that height rather than on the surface under each
 *          of its own parts.
 *      arc.put.lamp(x, y, z, fx, fy, size, code, phase)
 *          One lamp face at (x, y), `z` over the ground, facing
 *          (fx, fy).  `code` picks the aspect the material draws.
 *      arc.mat.prop, arc.mat.lamp, arc.mat.road ...
 *          The materials, by name.
 *
 *  Outside the window in which a prop is being drawn there is no mesh to
 *  draw into, and every one of them answers false rather than reaching
 *  for a mesh that is not there. */
#include <math.h>

#include "script.h"

#if SC2K_LUA

#include "internal.h"
#include "mesh/internal.h"
#include "net/internal.h"
#include "net/model.h"

/*  The mesh a prop is being drawn into, and what it is drawn with.  Set
 *  for the length of one rule call and cleared after it. */
static RMesh       *s_m;
static const RCity *s_c;
static uint8_t      s_mask;
static float        s_order;
static int          s_faces;

/*  A script that asks for more faces than the thing it is composing
 *  could want has run away, and the mesh is finite: it is stopped rather
 *  than the build.  A strip's whole ribbon comes through here, so the
 *  cap is a strip's worth and not a prop's. */
#define PUT_MAX 65536

void script_emit_open(void *mesh, const void *city, uint8_t mask_bit, float order)
{
    s_m     = (RMesh *)mesh;
    s_c     = (const RCity *)city;
    s_mask  = mask_bit;
    s_order = order;
    s_faces = 0;
}

void script_emit_close(void)
{
    s_m = NULL;
    s_c = NULL;
}

static int l_box(lua_State *L)
{
    float x   = (float)luaL_checknumber(L, 1), y = (float)luaL_checknumber(L, 2);
    float w   = (float)luaL_checknumber(L, 3), d = (float)luaL_checknumber(L, 4);
    float z0  = (float)luaL_checknumber(L, 5), z1 = (float)luaL_checknumber(L, 6);
    float mat = (float)luaL_optnumber(L, 7, (lua_Number)MAT_PROP);
    float ph  = (float)luaL_optnumber(L, 8, 0.0);
    if (!s_m || ++s_faces > PUT_MAX)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, put_box(s_m, s_c, s_mask, s_order, x, y, w, d, z0, z1, mat, ph) == 0);
    return 1;
}

/*  A wire between two points in the air: a power line's span. */
static int l_wire(lua_State *L)
{
    float x0 = (float)luaL_checknumber(L, 1), y0 = (float)luaL_checknumber(L, 2);
    float z0 = (float)luaL_checknumber(L, 3);
    float x1 = (float)luaL_checknumber(L, 4), y1 = (float)luaL_checknumber(L, 5);
    float z1 = (float)luaL_checknumber(L, 6);
    float sag = (float)luaL_optnumber(L, 7, 0.05);
    if (!s_m || ++s_faces > PUT_MAX)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, put_wire(s_m, s_c, s_mask, s_order, x0, y0, z0, x1, y1, z1, sag) == 0);
    return 1;
}

/*  One model, by name, at a place of its own: a script builds a prop
 *  out of the ones already stored as easily as out of boxes. */
static int l_model(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    float       x = (float)luaL_checknumber(L, 2), y = (float)luaL_checknumber(L, 3);
    float       fx = (float)luaL_optnumber(L, 4, 1.0), fy = (float)luaL_optnumber(L, 5, 0.0);
    float       size = (float)luaL_optnumber(L, 6, 0.0);
    float       phase = (float)luaL_optnumber(L, 7, 0.0);
    float       group = (float)luaL_optnumber(L, 8, 0.0);
    float       g = (float)luaL_optnumber(L, 9, 0.0);
    int         on = !lua_isnoneornil(L, 9);
    if (!s_m)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, net_model_put_on(net_model_find(name), s_m, s_c, s_mask, s_order,
                                        x, y, fx, fy, size, phase, group, g, g, on) == 0);
    return 1;
}

/*  A bar in the air: the gate arm's shape, at whatever angle the rule
 *  has swung it to. */
static int l_bar(lua_State *L)
{
    float x0 = (float)luaL_checknumber(L, 1), y0 = (float)luaL_checknumber(L, 2);
    float z0 = (float)luaL_checknumber(L, 3);
    float x1 = (float)luaL_checknumber(L, 4), y1 = (float)luaL_checknumber(L, 5);
    float z1 = (float)luaL_checknumber(L, 6);
    float fx = (float)luaL_checknumber(L, 7), fy = (float)luaL_checknumber(L, 8);
    float w = (float)luaL_checknumber(L, 9), d = (float)luaL_checknumber(L, 10);
    float mat = (float)luaL_optnumber(L, 11, (lua_Number)MAT_PROP);
    float code = (float)luaL_optnumber(L, 12, 0.0);
    float ph = (float)luaL_optnumber(L, 13, 0.0);
    if (!s_m || ++s_faces > PUT_MAX)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, put_bar(s_m, s_c, s_mask, s_order, x0, y0, z0, x1, y1, z1, fx, fy, w, d, mat, code, ph) == 0);
    return 1;
}

/*  The strip's own quad: one cross-section of a ribbon to the next.
 *  The road surface, the footway's bands, a crossing's panel and a
 *  deck's deck are all this. */
static int l_quad(lua_State *L)
{
    float a0[2] = {(float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2)};
    float a1[2] = {(float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4)};
    float b0[2] = {(float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6)};
    float b1[2] = {(float)luaL_checknumber(L, 7), (float)luaL_checknumber(L, 8)};
    float za = (float)luaL_checknumber(L, 9), zb = (float)luaL_checknumber(L, 10);
    float ac0 = (float)luaL_checknumber(L, 11), ac1 = (float)luaL_checknumber(L, 12);
    float al0 = (float)luaL_checknumber(L, 13), al1 = (float)luaL_checknumber(L, 14);
    float mat = (float)luaL_optnumber(L, 15, (lua_Number)MAT_ROAD);
    float slot = (float)luaL_optnumber(L, 16, 0.0);
    if (!s_m || ++s_faces > PUT_MAX)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, strip_quad_z(s_m, s_c, s_mask, s_order + slot, a0, a1, b0, b1, za, zb, ac0, ac1, al0, al1, mat) == 0);
    return 1;
}

/*  One triangle on the drawn surface, for a shape no ribbon describes. */
static int l_tri(lua_State *L)
{
    float t[3][3];
    float mat = (float)luaL_optnumber(L, 10, (lua_Number)MAT_ROAD);
    float al = (float)luaL_optnumber(L, 11, 0.0), ac = (float)luaL_optnumber(L, 12, 0.0);
    float slot = (float)luaL_optnumber(L, 13, 0.0);
    float col[3], ref[3], ref2[3];
    int   i;
    for (i = 0; i < 3; ++i)
    {
        t[i][0] = (float)luaL_checknumber(L, i * 3 + 1);
        t[i][1] = (float)luaL_checknumber(L, i * 3 + 2);
        t[i][2] = (float)luaL_checknumber(L, i * 3 + 3);
    }
    if (!s_m || ++s_faces > PUT_MAX)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    col[0] = al, col[1] = ac, col[2] = mat;
    ref[0] = ref[1] = ref[2] = al;
    ref2[0] = ref2[1] = ref2[2] = ac;
    lua_pushboolean(L, put_tri_ground(s_m, s_c, s_mask, s_order + slot, (const float (*)[3])t, NULL, col, ref, ref2) == 0);
    return 1;
}

/*  A fan of wedges about a point: a junction's turn. */
static int l_fan(lua_State *L)
{
    float cx = (float)luaL_checknumber(L, 1), cy = (float)luaL_checknumber(L, 2);
    float t0 = (float)luaL_checknumber(L, 3), t1 = (float)luaL_checknumber(L, 4);
    float r0 = (float)luaL_checknumber(L, 5), r1 = (float)luaL_checknumber(L, 6);
    float ac = (float)luaL_optnumber(L, 7, 0.0);
    float mat = (float)luaL_optnumber(L, 8, (lua_Number)MAT_ROAD);
    int   n = (int)luaL_optinteger(L, 9, 8);
    float lift = (float)luaL_optnumber(L, 10, 0.0);
    float slot = (float)luaL_optnumber(L, 11, 0.0);
    if (!s_m)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    s_faces += n;
    lua_pushboolean(L, s_faces <= PUT_MAX && strip_fan_z(s_m, s_c, s_mask, s_order + slot, cx, cy, t0, t1, r0, r1, ac, mat, n, lift) == 0);
    return 1;
}

/*  A box along a heading, cut on the tile folds. */
static int l_prism(lua_State *L)
{
    float cx = (float)luaL_checknumber(L, 1), cy = (float)luaL_checknumber(L, 2);
    float dx = (float)luaL_checknumber(L, 3), dy = (float)luaL_checknumber(L, 4);
    float len = (float)luaL_checknumber(L, 5), wid = (float)luaL_checknumber(L, 6);
    float zb = (float)luaL_checknumber(L, 7), zf = (float)luaL_checknumber(L, 8);
    float z0 = (float)luaL_checknumber(L, 9), z1 = (float)luaL_checknumber(L, 10);
    float mat = (float)luaL_optnumber(L, 11, (lua_Number)MAT_PROP);
    float paint = (float)luaL_optnumber(L, 12, 0.0);
    float slot = (float)luaL_optnumber(L, 13, 0.0);
    if (!s_m || ++s_faces > PUT_MAX)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, put_prism_clip_m(s_m, s_c, s_mask, s_order + slot, cx, cy, dx, dy, len, wid, zb, zf, z0, z1, paint, mat) == 0);
    return 1;
}

/*  A round post. */
static int l_cyl(lua_State *L)
{
    float cx = (float)luaL_checknumber(L, 1), cy = (float)luaL_checknumber(L, 2);
    float r = (float)luaL_checknumber(L, 3);
    float z0 = (float)luaL_checknumber(L, 4), z1 = (float)luaL_checknumber(L, 5);
    float mat = (float)luaL_optnumber(L, 6, (lua_Number)MAT_PROP);
    float slot = (float)luaL_optnumber(L, 7, 0.0);
    if (!s_m || ++s_faces > PUT_MAX)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, put_cyl(s_m, s_c, s_mask, s_order + slot, cx, cy, r, z0, z1, mat) == 0);
    return 1;
}

static int l_lamp(lua_State *L)
{
    float x  = (float)luaL_checknumber(L, 1), y = (float)luaL_checknumber(L, 2);
    float z  = (float)luaL_checknumber(L, 3);
    float fx = (float)luaL_checknumber(L, 4), fy = (float)luaL_checknumber(L, 5);
    float sz = (float)luaL_checknumber(L, 6);
    float cd = (float)luaL_checknumber(L, 7);
    float ph = (float)luaL_optnumber(L, 8, 0.0);
    float g;
    if (!s_m || ++s_faces > PUT_MAX)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    g = surface_at_world(s_c, s_mask, x, y);
    lua_pushboolean(L, put_lamp_face(s_m, s_order, x, y, g, z, fx, fy, sz, ph, cd) == 0);
    return 1;
}

/*  A number as the MESH holds it.  Vertices are floats there, and a
 *  composition that works to more places than the mesh can keep lands a
 *  line a few millionths from where its neighbour thinks it is: the two
 *  then overlap by that hair instead of meeting along it.  A script that
 *  computes a shared edge narrows each step through this, and the two
 *  sides of the edge come out the same number. */
static int l_f32(lua_State *L)
{
    lua_pushnumber(L, (lua_Number)(float)luaL_checknumber(L, 1));
    return 1;
}

/*  The transcendentals as the MESH'S OWN arithmetic computes them.  A
 *  script works in doubles, and a sine narrowed from one is not always
 *  the float the pipeline would have reached: where a composition has to
 *  land on the same number as the C beside it, it takes these. */
static int l_sqrtf(lua_State *L)
{
    lua_pushnumber(L, (lua_Number)sqrtf((float)luaL_checknumber(L, 1)));
    return 1;
}

static int l_sinf(lua_State *L)
{
    lua_pushnumber(L, (lua_Number)sinf((float)luaL_checknumber(L, 1)));
    return 1;
}

static int l_cosf(lua_State *L)
{
    lua_pushnumber(L, (lua_Number)cosf((float)luaL_checknumber(L, 1)));
    return 1;
}

static int l_tanf(lua_State *L)
{
    lua_pushnumber(L, (lua_Number)tanf((float)luaL_checknumber(L, 1)));
    return 1;
}

static int l_acosf(lua_State *L)
{
    lua_pushnumber(L, (lua_Number)acosf((float)luaL_checknumber(L, 1)));
    return 1;
}

static int l_atan2f(lua_State *L)
{
    lua_pushnumber(L, (lua_Number)atan2f((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2)));
    return 1;
}

/*  The ground under a point, so a script may set its own heights.
 *  Outside a draw there is no city to ask and the answer is the flat
 *  nothing a script can still do arithmetic on. */
static int l_ground(lua_State *L)
{
    if (!s_c)
    {
        lua_pushnumber(L, 0.0);
        return 1;
    }
    lua_pushnumber(L, (lua_Number)surface_at_world(s_c, s_mask, (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2)));
    return 1;
}

void api_put_open(lua_State *L)
{
    lua_newtable(L);
    lua_pushcfunction(L, l_box), lua_setfield(L, -2, "box");
    lua_pushcfunction(L, l_lamp), lua_setfield(L, -2, "lamp");
    lua_pushcfunction(L, l_wire), lua_setfield(L, -2, "wire");
    lua_pushcfunction(L, l_bar), lua_setfield(L, -2, "bar");
    lua_pushcfunction(L, l_quad), lua_setfield(L, -2, "quad");
    lua_pushcfunction(L, l_tri), lua_setfield(L, -2, "tri");
    lua_pushcfunction(L, l_fan), lua_setfield(L, -2, "fan");
    lua_pushcfunction(L, l_prism), lua_setfield(L, -2, "prism");
    lua_pushcfunction(L, l_cyl), lua_setfield(L, -2, "cyl");
    lua_pushcfunction(L, l_model), lua_setfield(L, -2, "model");
    lua_pushcfunction(L, l_ground), lua_setfield(L, -2, "ground");
    lua_pushcfunction(L, l_f32), lua_setfield(L, -2, "f32");
    lua_pushcfunction(L, l_sqrtf), lua_setfield(L, -2, "sqrt");
    lua_pushcfunction(L, l_sinf), lua_setfield(L, -2, "sin");
    lua_pushcfunction(L, l_cosf), lua_setfield(L, -2, "cos");
    lua_pushcfunction(L, l_tanf), lua_setfield(L, -2, "tan");
    lua_pushcfunction(L, l_acosf), lua_setfield(L, -2, "acos");
    lua_pushcfunction(L, l_atan2f), lua_setfield(L, -2, "atan2");
    lua_setfield(L, -2, "put");

    lua_newtable(L);
    lua_pushnumber(L, (lua_Number)MAT_PROP), lua_setfield(L, -2, "prop");
    lua_pushnumber(L, (lua_Number)MAT_LAMP), lua_setfield(L, -2, "lamp");
    lua_pushnumber(L, (lua_Number)MAT_ROAD), lua_setfield(L, -2, "road");
    lua_pushnumber(L, (lua_Number)MAT_ZEBRA), lua_setfield(L, -2, "zebra");
    lua_pushnumber(L, (lua_Number)MAT_RAIL), lua_setfield(L, -2, "rail");
    lua_pushnumber(L, (lua_Number)MAT_WALK), lua_setfield(L, -2, "walk");
    lua_pushnumber(L, (lua_Number)MAT_VEHICLE), lua_setfield(L, -2, "vehicle");
    lua_pushnumber(L, (lua_Number)MAT_GROUND), lua_setfield(L, -2, "ground");
    lua_pushnumber(L, (lua_Number)MAT_HIWAY), lua_setfield(L, -2, "hiway");
    lua_pushnumber(L, (lua_Number)MAT_RAIL_X), lua_setfield(L, -2, "rail_x");
    lua_pushnumber(L, (lua_Number)MAT_XPANEL), lua_setfield(L, -2, "xpanel");
    lua_pushnumber(L, (lua_Number)MAT_XAPPROACH), lua_setfield(L, -2, "xapproach");
    lua_pushnumber(L, (lua_Number)MAT_SURFACE), lua_setfield(L, -2, "surface");
    lua_pushnumber(L, (lua_Number)MAT_SEABED), lua_setfield(L, -2, "seabed");
    lua_pushnumber(L, (lua_Number)MAT_EARTH), lua_setfield(L, -2, "earth");
    lua_pushnumber(L, (lua_Number)MAT_SEDIMENT), lua_setfield(L, -2, "sediment");
    lua_pushnumber(L, (lua_Number)MAT_ENG_WALL), lua_setfield(L, -2, "eng_wall");
    lua_pushnumber(L, (lua_Number)MAT_WATER), lua_setfield(L, -2, "water");
    lua_pushnumber(L, (lua_Number)MAT_PIER), lua_setfield(L, -2, "pier");
    lua_pushnumber(L, (lua_Number)MAT_ZONE), lua_setfield(L, -2, "zone");
    lua_setfield(L, -2, "mat");
}

#endif
