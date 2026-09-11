/*  api_put.c: `arc.put`: the primitives a script builds a prop from.
 *
 *  They are the same ones the C builds its own from.  So a script's
 *  signal and the one it replaces are made of the same faces.  They go
 *  through the same emitter, the shape layer and the checks included.
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
 *          into.  This is the line surface itself.
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
 *      arc.mat.prop, arc.mat.lamp, arc.mat.line ...
 *          The materials, by name.
 *
 *  Outside the window in which a prop is being drawn there is no mesh to
 *  draw into.  Every one of them answers false rather than reaching for
 *  a mesh that is not there. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "script.h"


#include "internal.h"
#include "mesh/internal.h"
#include "pipeline.h"
#include "net/net.h"
#include "mesh/model.h"

/*  The world a prop is being drawn into, and what it is drawn with.  Set
 *  for the length of one rule call and cleared after it. */
static RMesh       *s_m;
static const RCity *s_c;
static uint8_t      s_mask;
static float        s_order;
static int          s_faces;

/*  A script that asks for more faces than the thing it is composing
 *  could want has run away.  The mesh is finite: it is stopped rather
 *  than the build.  A strip's whole ribbon comes through here, so the
 *  cap is a strip's worth and not a prop's. */
#define PUT_MAX 65536

/*  ---- THE DRAW QUEUE -----------------------------------------------------
 *
 *  A SCRIPT NEVER WRITES INTO A LIVE MESH.  `arc.put` RECORDS what it
 *  was asked for.  Which primitive, its numbers, and the order it is
 *  drawn at.  And the pipeline lays the record when the window closes.
 *
 *  This is the shape the rest of the pipeline already has.  A path is
 *  cut by queueing the chain and reading the pieces back once the drive
 *  has been round (the cut queue, mesh/fit.c).  The outline view's
 *  hairlines are gathered with the shape each belongs to and laid at the
 *  end (net_wires).  Every stage a family names is settled before
 *  anything is built.  In each of them a script says WHAT it wants and
 *  the pipeline decides WHEN it happens.
 *
 *  What that buys is not tidiness.  A stage that only describes needs no
 *  mesh in its hand.  So no stage has to be refused a rule for want of
 *  one: which is the whole reason a family still lends a primitive. */
enum
{
    PUT_BOX = 1,
    PUT_WIRE,
    PUT_MODEL,
    PUT_BAR,
    PUT_QUAD,
    PUT_TRI,
    PUT_FAN,
    PUT_PRISM,
    PUT_CYL,
    PUT_LAMP
};

#define PUT_ARGS 16

typedef struct
{
    uint8_t kind;
    int     n;     /* a fan's wedges.  A model's index and whether it is lit */
    int     on;
    float   order; /* the window's own, plus the call's slot */
    float   a[PUT_ARGS];
} PutRec;

static PutRec *s_put;
static int     s_n_put, s_put_cap;

/*  One record kept.  Answers whether there was room: a queue that cannot
 *  grow is a prop the script asked for and the world will not have.
 *  This is what the caller is told. */
static int put_add(int kind, float order, const float *a, int na, int n, int on)
{
    PutRec *r;
    if (s_n_put >= s_put_cap)
    {
        int   cap = s_put_cap ? s_put_cap * 2 : 4096;
        void *q   = realloc(s_put, (size_t)cap * sizeof *s_put);
        if (!q)
            return 0;
        s_put = q, s_put_cap = cap;
    }
    r        = &s_put[s_n_put++];
    r->kind  = (uint8_t)kind;
    r->order = order;
    r->n     = n;
    r->on    = on;
    memset(r->a, 0, sizeof r->a);
    if (na > PUT_ARGS)
        na = PUT_ARGS;
    memcpy(r->a, a, (size_t)na * sizeof *a);
    return 1;
}

/*  And the queue laid into the mesh, in the order it was asked for. */
static void put_flush(void)
{
    int i;
    for (i = 0; i < s_n_put && s_m; ++i)
    {
        const PutRec *r = &s_put[i];
        const float  *a = r->a;
        switch (r->kind)
        {
        case PUT_BOX: put_box(s_m, s_c, s_mask, r->order, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]); break;
        case PUT_WIRE: put_wire(s_m, s_c, s_mask, r->order, a[0], a[1], a[2], a[3], a[4], a[5], a[6]); break;
        case PUT_MODEL:
            net_model_put_on(r->n, s_m, s_c, s_mask, r->order, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[7], r->on);
            break;
        case PUT_BAR:
            put_bar(s_m, s_c, s_mask, r->order, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12]);
            break;
        case PUT_QUAD:
        {
            float a0[2] = {a[0], a[1]}, a1[2] = {a[2], a[3]}, b0[2] = {a[4], a[5]}, b1[2] = {a[6], a[7]};
            strip_quad_z(s_m, s_c, s_mask, r->order, a0, a1, b0, b1, a[8], a[9], a[10], a[11], a[12], a[13], a[14]);
            break;
        }
        case PUT_TRI:
        {
            float t[3][3] = {{a[0], a[1], a[2]}, {a[3], a[4], a[5]}, {a[6], a[7], a[8]}};
            float col[3]  = {a[9], a[10], a[11]};
            float ref[3]  = {a[9], a[9], a[9]}, ref2[3] = {a[10], a[10], a[10]};
            put_tri_ground(s_m, s_c, s_mask, r->order, (const float (*)[3])t, col, ref, ref2);
            break;
        }
        case PUT_FAN: strip_fan_z(s_m, s_c, s_mask, r->order, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], r->n, a[8]); break;
        case PUT_PRISM:
            put_prism_clip_m(s_m, s_c, s_mask, r->order, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]);
            break;
        case PUT_CYL: put_cyl(s_m, s_c, s_mask, r->order, a[0], a[1], a[2], a[3], a[4], a[5]); break;
        case PUT_LAMP: put_lamp_face(s_m, r->order, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9] > 0.5f); break;
        default: break;
        }
    }
    s_n_put = 0;
}

void script_emit_open(void *mesh, const void *city, uint8_t mask_bit, float order)
{
    s_m     = (RMesh *)mesh;
    s_c     = (const RCity *)city;
    s_mask  = mask_bit;
    s_order = order;
    s_faces = 0;
    /*  The queue is NOT cleared here.  It is emptied by the flush at
     *  close, and clearing it on open would throw away whatever an
     *  outer window had already recorded. */
}

void script_emit_close(void)
{
    put_flush();
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
    {
        float q[8] = {x, y, w, d, z0, z1, mat, ph};
        lua_pushboolean(L, put_add(PUT_BOX, s_order, q, 8, 0, 0));
    }
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
    {
        float q[7] = {x0, y0, z0, x1, y1, z1, sag};
        lua_pushboolean(L, put_add(PUT_WIRE, s_order, q, 7, 0, 0));
    }
    return 1;
}

/*  One model, by name, at a place of its own.  A script builds a prop
 *  out of the ones already stored, as easily as out of boxes. */
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
    {
        float q[8] = {x, y, fx, fy, size, phase, group, g};
        lua_pushboolean(L, put_add(PUT_MODEL, s_order, q, 8, net_model_find(name), on));
    }
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
    {
        float q[13] = {x0, y0, z0, x1, y1, z1, fx, fy, w, d, mat, code, ph};
        lua_pushboolean(L, put_add(PUT_BAR, s_order, q, 13, 0, 0));
    }
    return 1;
}

/*  The strip's own quad: one cross-section of a ribbon to the next.
 *  The line surface, the margin's bands, a meet's panel and a
 *  slab's slab are all this. */
static int l_quad(lua_State *L)
{
    float a0[2] = {(float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2)};
    float a1[2] = {(float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4)};
    float b0[2] = {(float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6)};
    float b1[2] = {(float)luaL_checknumber(L, 7), (float)luaL_checknumber(L, 8)};
    float za = (float)luaL_checknumber(L, 9), zb = (float)luaL_checknumber(L, 10);
    float ac0 = (float)luaL_checknumber(L, 11), ac1 = (float)luaL_checknumber(L, 12);
    float al0 = (float)luaL_checknumber(L, 13), al1 = (float)luaL_checknumber(L, 14);
    float mat = (float)luaL_optnumber(L, 15, (lua_Number)MAT_LINE);
    float slot = (float)luaL_optnumber(L, 16, 0.0);
    if (!s_m || ++s_faces > PUT_MAX)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    {
        float q[15] = {a0[0], a0[1], a1[0], a1[1], b0[0], b0[1], b1[0], b1[1],
                       za, zb, ac0, ac1, al0, al1, mat};
        lua_pushboolean(L, put_add(PUT_QUAD, s_order + slot, q, 15, 0, 0));
    }
    return 1;
}

/*  One triangle on the drawn surface, for a shape no ribbon describes. */
static int l_tri(lua_State *L)
{
    float t[3][3];
    float mat = (float)luaL_optnumber(L, 10, (lua_Number)MAT_LINE);
    float al = (float)luaL_optnumber(L, 11, 0.0), ac = (float)luaL_optnumber(L, 12, 0.0);
    float slot = (float)luaL_optnumber(L, 13, 0.0);
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
    {
        float q[12] = {t[0][0], t[0][1], t[0][2], t[1][0], t[1][1], t[1][2],
                       t[2][0], t[2][1], t[2][2], al, ac, mat};
        lua_pushboolean(L, put_add(PUT_TRI, s_order + slot, q, 12, 0, 0));
    }
    return 1;
}

/*  A fan of wedges about a point: a junction's turn. */
static int l_fan(lua_State *L)
{
    float cx = (float)luaL_checknumber(L, 1), cy = (float)luaL_checknumber(L, 2);
    float t0 = (float)luaL_checknumber(L, 3), t1 = (float)luaL_checknumber(L, 4);
    float r0 = (float)luaL_checknumber(L, 5), r1 = (float)luaL_checknumber(L, 6);
    float ac = (float)luaL_optnumber(L, 7, 0.0);
    float mat = (float)luaL_optnumber(L, 8, (lua_Number)MAT_LINE);
    int   n = (int)luaL_optinteger(L, 9, 8);
    float lift = (float)luaL_optnumber(L, 10, 0.0);
    float slot = (float)luaL_optnumber(L, 11, 0.0);
    if (!s_m)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    s_faces += n;
    {
        float q[9] = {cx, cy, t0, t1, r0, r1, ac, mat, lift};
        lua_pushboolean(L, s_faces <= PUT_MAX && put_add(PUT_FAN, s_order + slot, q, 9, n, 0));
    }
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
    {
        float q[12] = {cx, cy, dx, dy, len, wid, zb, zf, z0, z1, paint, mat};
        lua_pushboolean(L, put_add(PUT_PRISM, s_order + slot, q, 12, 0, 0));
    }
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
    {
        float q[6] = {cx, cy, r, z0, z1, mat};
        lua_pushboolean(L, put_add(PUT_CYL, s_order + slot, q, 6, 0, 0));
    }
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
    {
        float q[9] = {x, y, g, z, fx, fy, sz, ph, cd};
        lua_pushboolean(L, put_add(PUT_LAMP, s_order, q, 9, 0, 0));
    }
    return 1;
}

/*  A number as the MESH holds it.  Vertices are floats there.  A
 *  composition that works to more places than the mesh can keep lands a
 *  line wrongly.  It falls a few millionths from where its neighbor
 *  thinks it is: the two then overlap by that hair instead of meeting
 *  along it.  A script that computes a shared edge narrows each step
 *  through this, and the two sides of the edge come out the same number. */
static int l_f32(lua_State *L)
{
    lua_pushnumber(L, (lua_Number)(float)luaL_checknumber(L, 1));
    return 1;
}

/*  The transcendentals as the MESH'S OWN arithmetic computes them.  A
 *  script works in doubles, and a sine narrowed from one is not always
 *  the float the pipeline would have reached.  Where a composition has
 *  to land on the same number as the C beside it, it takes these. */
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

/*  A SCRIPT'S OWN MATERIALS.
 *
 *  A built-in material has a branch of its own in the shaders: water
 *  ripples, sediment is layered, a zebra is striped.  One a script
 *  declares cannot have that.  The shaders are built with the program.
 *  So it is shaded from PARAMETERS instead, which the frame hands the
 *  shader every pass.  A color and a roughness is enough for a surface
 *  that is simply a surface.  That is most of what a new representation
 *  wants before it wants anything else. */
static struct
{
    char  name[32];
    float rgba[4]; /* the color, and roughness in the fourth */
} s_script_mat[MAT_SCRIPT_MAX];
static int s_n_script_mat;

void script_material_reset(void)
{
    s_n_script_mat = 0;
}

int script_materials(const float **out)
{
    *out = s_n_script_mat ? s_script_mat[0].rgba : NULL;
    return s_n_script_mat;
}

/*  arc.mat.define{name = "...", color = {r, g, b}, rough = 0..1}.
 *  Answers the number the material is known by.  So a script can hold it
 *  and hand it to arc.put.  Declaring the same name twice answers the
 *  same number and rewrites its parameters, which is what a reload of
 *  the scripts does. */
static int l_mat_define(lua_State *L)
{
    const char *name;
    int         i, at = -1;
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "name");
    name = lua_tostring(L, -1);
    if (!name || !name[0])
        return luaL_error(L, "arc.mat.define wants a name");
    for (i = 0; i < s_n_script_mat; ++i)
        if (strcmp(s_script_mat[i].name, name) == 0)
            at = i;
    if (at < 0)
    {
        if (s_n_script_mat >= MAT_SCRIPT_MAX)
            return luaL_error(L, "arc.mat.define: no room past %d materials", MAT_SCRIPT_MAX);
        at = s_n_script_mat++;
        snprintf(s_script_mat[at].name, sizeof s_script_mat[at].name, "%s", name);
    }
    lua_pop(L, 1);
    s_script_mat[at].rgba[0] = s_script_mat[at].rgba[1] = s_script_mat[at].rgba[2] = 0.5f;
    s_script_mat[at].rgba[3] = 0.5f;
    lua_getfield(L, 1, "colour");
    if (lua_istable(L, -1))
        for (i = 0; i < 3; ++i)
        {
            lua_rawgeti(L, -1, i + 1);
            s_script_mat[at].rgba[i] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
    lua_pop(L, 1);
    s_script_mat[at].rgba[3] = api_field_num(L, "rough", 0.5f);
    lua_pushnumber(L, (lua_Number)(MAT_SCRIPT_BASE + (float)at));
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

    /*  arc.mat: every material by name, from the generated table.  So
     *  the list a script sees and the list the mesh writes are one list.
     *  arc.mat.define adds one of the script's own. */
    lua_newtable(L);
    {
        int i;
        for (i = 0; i < r_materials_n; ++i)
            lua_pushnumber(L, (lua_Number)r_materials[i].value),
                lua_setfield(L, -2, r_materials[i].name);
    }
    lua_pushcfunction(L, l_mat_define), lua_setfield(L, -2, "define");
    lua_setfield(L, -2, "mat");
}

