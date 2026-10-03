/*  obj_world.c: THE BUILD ITSELF, as the composing script drives it.
 *
 *  `w` is the world the drive hands arc.rules.world.  Every pass of a
 *  build is a method on it.  The tiles.  The networks a family's cells
 *  make.  The paths waiting to be cut, and the lofts.  The junctions and
 *  their margins.  The lanes, and the moving world.  The rule calls them
 *  in the order it chooses, and that order is the build.
 *
 *  It decides nothing itself.  Each method offers a reading or takes an
 *  answer back, and the shape of the world is the script's. */
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
#include "show.h"
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

/*  ---- the world -----------------------------------------------------
 *
 *  The build itself, handed to the rule that DRIVES it.  Everything here
 *  is a primitive: one tile's ground, one tile's tint, and the three
 *  network passes.  What the script decides is the ORDER.  Which tiles,
 *  in what order, which passes run at all.  And there is no C loop
 *  behind it. */
static WorldFan *world_of(lua_State *L)
{
    return (WorldFan *)rec_of(L, "world");
}

/*  What the build IS: how wide the map is, which pass this is, and what
 *  it is allowed to draw. */
static int api_world_info(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, R_MAP), lua_setfield(L, -2, "size");
    lua_pushinteger(L, w->pass), lua_setfield(L, -2, "pass");
    lua_pushboolean(L, w->lines), lua_setfield(L, -2, "lines");
    lua_pushboolean(L, w->underground), lua_setfield(L, -2, "underground");
    lua_pushboolean(L, incr_editing()), lua_setfield(L, -2, "edit");
    return 1;
}

/*  Whether this build wants a tile at all.  An edit's build replaces
 *  only the chunks its closure named.  The rest stand: asking here saves
 *  the call, and the primitives check it again for themselves. */
static int api_world_wanted(lua_State *L)
{
    WorldFan *w = world_of(L);
    lua_pushboolean(L, w && (w->pass == 1 || incr_want_tile((int32_t)luaL_checkinteger(L, 2),
                                                            (int32_t)luaL_checkinteger(L, 3))));
    return 1;
}

/*  One tile, GATHERED and handed over: its ground, or its zone for the
 *  map view's tint.  Answers a `tile` handle, or nothing where this
 *  build wants no such tile: so a script writes
 *
 *      local t = w:tile(col, row) if t then ... end
 *
 *  and composes it itself.  The record is the world's own and is written
 *  again at the next ask.  This is what keeps a map's worth of tiles
 *  from being a map's worth of allocations. */
static TileFan s_world_tile;

static int world_fan(lua_State *L, int ground)
{
    WorldFan *w   = world_of(L);
    int32_t   col = (int32_t)luaL_checkinteger(L, 2);
    int32_t   row = (int32_t)luaL_checkinteger(L, 3);
    if (!w || !(ground ? mesh_ground_fan(col, row, &s_world_tile)
                       : mesh_tint_fan(col, row, &s_world_tile)))
        return 0;
    api_object_push(L, "tile", &s_world_tile);
    return 1;
}

static int api_world_tile(lua_State *L)
{
    return world_fan(L, 1);
}

static int api_world_zone(lua_State *L)
{
    return world_fan(L, 0);
}

/*  ---- the network, discovered ------------------------------------------
 *
 *  `w:net_discover()` clears the store and answers how many families
 *  have a network to find.  `w:net_cells(fk)` hands one family's map
 *  over as a `network` handle.  What comes back through that handle is
 *  the whole of the network.  The class pass, the measuring walk and the
 *  drawing walk read it.  None of them reads the map. */
static NetDiscFan s_world_disc;

static int api_world_net_discover(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    net_disc_reset();
    lua_pushinteger(L, net_n_walked);
    return 1;
}

static int api_world_net_cells(lua_State *L)
{
    WorldFan *w  = world_of(L);
    int       fk = (int)luaL_checkinteger(L, 2);
    if (!w || fk < 0 || fk >= net_n_walked)
        return 0;
    s_world_disc.fk     = fk;
    s_world_disc.f      = net_walked[fk]->f;
    s_world_disc.family = net_walked[fk]->name;
    s_world_disc.full   = 0;
    net_disc_planes(w->c, net_walked[fk]->f, s_world_disc.links, s_world_disc.art);
    api_object_push(L, "network", &s_world_disc);
    return 1;
}

/*  And whether the store took everything the script found.  A network
 *  half kept draws a city half wrong, so it is asked about rather than
 *  discovered later as missing geometry. */
static int api_world_net_found(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    if (s_world_disc.full)
    {
        R_ERR("net", "no room for the network of %s: the store is full",
              s_world_disc.family ? s_world_disc.family : "?");
        w->rc = -1;
    }
    lua_pushboolean(L, !s_world_disc.full);
    return 1;
}

/*  The band's map, handed over for the script to walk its bands.
 *  Answers nothing where this build is replaying the bands a previous
 *  one fitted.  This is when there is nothing to discover. */
static BandDiscFan s_world_band_disc;

static int api_world_band_cells(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w || !net_band_replaying())
        return 0;
    net_band_disc_reset();
    s_world_band_disc.full = 0;
    api_object_push(L, "bands", &s_world_band_disc);
    return 1;
}

/*  ---- the margin round a junction ------------------------------------
 *
 *  The ring read as a margin, and off it which of the junction's mouths
 *  may carry a meet.  Both are rules, and the DRIVE asks them.  The pass
 *  that measures the trims and the pass that draws the box each ask for
 *  the junction in hand before reading it.  So nothing in the margin
 *  reaches up and a script that answers neither leaves the junction
 *  bare.
 *
 *  `mouths`, `mouth` and `mouth_is` serve both, because the mouths in
 *  hand are always the junction whose band was answered last. */
/*  Where a gate settles with nothing near it.  It is one angle.  The
 *  build asks for it once, and every gate the moving world starts reads
 *  it. */
static int api_world_gate_rest(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_gate_rest_is((float)luaL_optnumber(L, 2, 0.0));
    return 0;
}

static int api_world_junction_bands(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    lua_pushinteger(L, walk_trim_junctions(w->c));
    return 1;
}

static int api_world_junction_band(lua_State *L)
{
    void *b;
    if (!world_of(L) || (b = walk_trim_band((int)luaL_checkinteger(L, 2))) == NULL)
        return 0;
    api_object_push(L, "band", b);
    return 1;
}

/*  The answer taken, and the junction's mouths read off it. */
static int api_world_junction_band_done(lua_State *L)
{
    if (!world_of(L))
        return 0;
    margin_band_answered();
    lua_pushinteger(L, walk_trim_mouths());
    return 1;
}

/*  And what follows for the pass that measures: the trim each arm is
 *  handed, and how deep a band each mouth asks for. */
static int api_world_junction_trim(lua_State *L)
{
    if (!world_of(L))
        return 0;
    walk_trim_done((int)luaL_checkinteger(L, 2));
    return 0;
}

/*  The same three for the box the drive is drawing. */
static int api_world_box_band(lua_State *L)
{
    void *b;
    if (!world_of(L) || (b = net_junction_band()) == NULL)
        return 0;
    api_object_push(L, "band", b);
    return 1;
}

static int api_world_box_band_done(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    margin_band_answered();
    if (net_junction_band_done() != 0)
        w->rc = -1;
    lua_pushinteger(L, net_junction_mouths());
    return 1;
}

/*  One mouth of the junction in hand: what the outline makes of it, for
 *  the rule that says whether it carries a meet. */
static int api_world_mouth(lua_State *L)
{
    int32_t col, row;
    int     arm, ctrl, pave;
    float   cs, span;
    if (!world_of(L) || !margin_mouth_at((int)luaL_checkinteger(L, 2), &col, &row, &arm, &ctrl, &pave, &cs, &span))
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, row), lua_setfield(L, -2, "row");
    lua_pushinteger(L, arm), lua_setfield(L, -2, "arm");
    lua_pushinteger(L, ctrl), lua_setfield(L, -2, "control");
    lua_pushboolean(L, pave), lua_setfield(L, -2, "margin");
    lua_pushnumber(L, cs), lua_setfield(L, -2, "cos");
    lua_pushnumber(L, span), lua_setfield(L, -2, "span");
    return 1;
}

/*  And the answer: how deep a band it asks for.  A depth of nought is a
 *  mouth that carries no stripe, the same as no answer at all.  A band
 *  with no depth is not a shallow stripe.  It is none. */
static int api_world_mouth_is(lua_State *L)
{
    float deep = (float)luaL_optnumber(L, 3, 0.0);
    if (!world_of(L))
        return 0;
    margin_mouth_is((int)luaL_checkinteger(L, 2), lua_isnumber(L, 3) && deep > 0.0f, deep);
    return 0;
}

/*  ---- the paths waiting to be cut ---------------------------------------
 *
 *  Cutting a fitted path into pieces is arc.rules.pieces's, and only the
 *  drive may ask for it.  A pass that needs a path cut puts the chain in
 *  the queue and the drive comes round and cuts what is waiting.  The
 *  pass then reads the pieces back. */
static int api_world_cuts(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    if (net_cut_full())
    {
        R_ERR("net", "no room to cut every path this pass asked for");
        w->rc = -1;
    }
    lua_pushinteger(L, net_cuts());
    return 1;
}

static int api_world_cut(lua_State *L)
{
    void *p;
    if (!world_of(L) || (p = net_cut_at((int)luaL_checkinteger(L, 2))) == NULL)
        return 0;
    api_object_push(L, "pieces", p);
    return 1;
}

static int api_world_cut_done(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_cut_done((int)luaL_checkinteger(L, 2));
    return 0;
}

/*  ---- the loft, as a service the composing script calls
 *  ---------------
 *
 *      w:loft(pieces, profile [, how])  ->  faces
 *
 *  `pieces` is a path: what arc.fit answered, or anything with the same
 *  fields.  `profile` is the CROSS-SECTION, a sequence of {across = , up
 *  = , mat = } points read left to right across the centerline.  The
 *  face between one point and the next is drawn in that point's
 *  material.  `how` carries the rest: `step` and `step_arc`, how finely
 *  a straight and an arc are stationed.  `lift`, how far the section
 *  stands over the ground under the centerline, or `z` for a height
 *  outright.  `slot`, where in the tile's painter stack the faces go.
 *  `along`, the seat as heights at distances along the chain.  `width`,
 *  a list of {s, wr, wl}: the factor the section's across is scaled by
 *  on the right and on the left at that distance.  Between two entries
 *  the factor is a straight line from one to the next.  And `closed` to
 *  join the section's last point back to its first.
 *
 *  Nothing about a line reaches this: a script that wants a line lofts a
 *  line's section, and one that wants a canal lofts a canal's. */
#define LOFT_SEC_MAX 64
#define LOFT_KNOT_MAX 64

static int api_world_loft(lua_State *L)
{
    WorldFan *w = world_of(L);
    static Piece pc[MAX_PIECES];
    LoftRung     sec[LOFT_SEC_MAX];
    LoftSweep    how;
    float        knot_s[LOFT_KNOT_MAX], knot_z[LOFT_KNOT_MAX], knot_m[LOFT_KNOT_MAX];
    float        wide_s[LOFT_KNOT_MAX], wide_r[LOFT_KNOT_MAX], wide_l[LOFT_KNOT_MAX];
    int          np = 0, nsec = 0, i, faces;
    if (!w || !lua_istable(L, 2) || !lua_istable(L, 3))
        return 0;
    np = (int)lua_rawlen(L, 2);
    if (np > MAX_PIECES)
        np = MAX_PIECES;
    for (i = 0; i < np; ++i)
    {
        lua_rawgeti(L, 2, i + 1);
        memset(&pc[i], 0, sizeof pc[i]);
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, "arc");
            pc[i].arc = lua_toboolean(L, -1);
            lua_pop(L, 1);
            pc[i].a.x = api_field_num(L, "ax", 0.0f);
            pc[i].a.y = api_field_num(L, "ay", 0.0f);
            pc[i].b.x = api_field_num(L, "bx", 0.0f);
            pc[i].b.y = api_field_num(L, "by", 0.0f);
            pc[i].c.x = api_field_num(L, "cx", 0.0f);
            pc[i].c.y = api_field_num(L, "cy", 0.0f);
            pc[i].r   = api_field_num(L, "r", 0.0f);
            pc[i].t0  = api_field_num(L, "t0", 0.0f);
            pc[i].t1  = api_field_num(L, "t1", 0.0f);
            pc[i].len = api_field_num(L, "len", 0.0f);
        }
        lua_pop(L, 1);
    }
    nsec = (int)lua_rawlen(L, 3);
    if (nsec > LOFT_SEC_MAX)
        nsec = LOFT_SEC_MAX;
    for (i = 0; i < nsec; ++i)
    {
        lua_rawgeti(L, 3, i + 1);
        memset(&sec[i], 0, sizeof sec[i]);
        if (lua_istable(L, -1))
        {
            sec[i].across = api_field_num(L, "across", 0.0f);
            sec[i].up     = api_field_num(L, "up", 0.0f);
            sec[i].mat    = api_field_num(L, "mat", (float)MAT_LINE);
            lua_getfield(L, -1, "paint");
            sec[i].painted = lua_isnumber(L, -1);
            sec[i].paint   = sec[i].painted ? (float)lua_tonumber(L, -1) : sec[i].across;
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    memset(&how, 0, sizeof how);
    if (lua_istable(L, 4))
    {
        lua_pushvalue(L, 4);
        how.step_run = api_field_num(L, "step", 0.0f);
        how.step_arc = api_field_num(L, "step_arc", how.step_run);
        how.lift     = api_field_num(L, "lift", 0.0f);
        how.slot     = api_field_num(L, "slot", 0.0f);
        how.along0   = api_field_num(L, "along_at", 0.0f);
        lua_getfield(L, -1, "z");
        how.pinned = lua_isnumber(L, -1);
        how.z      = how.pinned ? (float)lua_tonumber(L, -1) : 0.0f;
        lua_pop(L, 1);
        /*  `along` is the seat as HEIGHTS at distances along the chain,
         *  a list of {s, z, dz} in order of s.  `dz` is the slope there,
         *  and nought where it is left out.  A way that climbs, crosses
         *  over another and comes down again is a sweep with four of
         *  them.  Read into arrays of this call's own. */
        lua_getfield(L, -1, "along");
        if (lua_istable(L, -1))
        {
            int nk = (int)lua_rawlen(L, -1), ki;
            if (nk > LOFT_KNOT_MAX)
                nk = LOFT_KNOT_MAX;
            for (ki = 0; ki < nk; ++ki)
            {
                lua_rawgeti(L, -1, ki + 1);
                knot_s[ki] = api_field_num(L, "s", 0.0f);
                knot_z[ki] = api_field_num(L, "z", 0.0f);
                knot_m[ki] = api_field_num(L, "dz", 0.0f);
                lua_pop(L, 1);
            }
            how.knot_s = knot_s;
            how.knot_z = knot_z;
            how.knot_m = knot_m;
            how.nknot  = nk;
        }
        lua_pop(L, 1);
        /*  `width` is the section's width each side, as a factor on its
         *  across, at distances along the chain.  It is a list of {s,
         *  wr, wl} in order of s.  A side left out keeps its width. */
        lua_getfield(L, -1, "width");
        if (lua_istable(L, -1))
        {
            int nw = (int)lua_rawlen(L, -1), wi;
            if (nw > LOFT_KNOT_MAX)
                nw = LOFT_KNOT_MAX;
            for (wi = 0; wi < nw; ++wi)
            {
                lua_rawgeti(L, -1, wi + 1);
                wide_s[wi] = api_field_num(L, "s", 0.0f);
                wide_r[wi] = api_field_num(L, "wr", 1.0f);
                wide_l[wi] = api_field_num(L, "wl", 1.0f);
                lua_pop(L, 1);
            }
            how.wide_s = wide_s;
            how.wide_r = wide_r;
            how.wide_l = wide_l;
            how.nwide  = nw;
        }
        lua_pop(L, 1);
        lua_getfield(L, -1, "closed");
        how.closed = lua_toboolean(L, -1);
        lua_pop(L, 2);
    }
    faces = loft_sweep(w->m, w->c, w->mask_bit, pc, np, sec, nsec, &how);
    if (faces < 0)
    {
        w->rc = -1;
        return 0;
    }
    lua_pushinteger(L, faces);
    return 1;
}

/*  HOW HIGH A SLAB STANDS near a point, read off the stations the lofts
 *  filed (net/station.c).  Answers the height and how far away the
 *  station that carries it is, and nothing at all where the store is
 *  empty.
 *
 *  A place that joins several slabs has no other way to find the height
 *  they arrive at.  The ground under it says how high the land is.  It
 *  does not say how far over the land the slab was carried, and the lane
 *  model keeps no height at all.
 *
 *  How far away the reading was taken is handed back with it.  A reading
 *  from the far side of the map is no answer, and only the caller knows
 *  what near enough is. */
static int api_world_slab_near(lua_State *L)
{
    V2    p;
    float z, away;
    if (!world_of(L))
        return 0;
    p.x = (float)luaL_checknumber(L, 2);
    p.y = (float)luaL_checknumber(L, 3);
    if (!net_station_near(p, &z, &away))
        return 0;
    lua_pushnumber(L, z), lua_pushnumber(L, away);
    return 2;
}

/*  The SHAPE a script composes into: what the inspector names the
 *  triangles by, and what the checks group them into.  One at a time.
 *  Opening the next closes the one before, and `w:shape()` with nothing
 *  to name closes the last.  Which is the discipline the C loop kept
 *  when it owned this, and it means no script can leave one open. */
static int api_world_shape(lua_State *L)
{
    WorldFan   *w    = world_of(L);
    const char *what = lua_tostring(L, 2);
    if (!w)
        return 0;
    shape_close(w->shape);
    w->shape = SHAPE_NONE;
    if (what)
        w->shape = shape_open("%s at %d,%d", what,
                              (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4));
    return 0;
}

/*  A POWER LINE'S TILE, gathered and handed over.  It holds the pylon's
 *  place and the edges its wires span to.  The mesh is open for the
 *  script to draw into.  Answers the prop and whether the tile is a
 *  meet.  Where a line runs over a line or a thread there is no pylon,
 *  only the span.  Or nothing where no line stands here.
 *
 *  `w:emitted()` closes the mesh again.  Opening the next one closes the
 *  last, so a script that forgets cannot leave it open. */
static int api_world_power(lua_State *L)
{
    WorldFan   *w   = world_of(L);
    int32_t     col = (int32_t)luaL_checkinteger(L, 2);
    int32_t     row = (int32_t)luaL_checkinteger(L, 3);
    int32_t     idx = row * R_MAP + col;
    uint8_t     b;
    Family      f, f2;
    int         piece, second, links, meet;
    ScriptProp  at;
    if (!w)
        return 0;
    b      = w->c->xbld[idx];
    piece  = piece_family(b, &f);
    second = piece_second(b, &f2);
    /*  A line on its own ground, or the second family of a meet:
     *  either way it is the POWER family that draws it. */
    if (piece >= 0 && f == net_power->f)
        meet = 0, links = piece_links(piece, w->c->xter[idx]);
    else if (second >= 0 && f2 == net_power->f)
        meet = 1, links = piece_links(second, w->c->xter[idx]);
    else
        return 0;
    if (!links)
        return 0;
    memset(&at, 0, sizeof at);
    at.col = (int)col, at.row = (int)row, at.arm = -1, at.links = links;
    at.x = (float)col + 0.5f, at.y = (float)row + 0.5f;
    at.z  = surface_at_world(w->c, w->mask_bit, at.x, at.y);
    at.fx = 1.0f, at.fy = 0.0f;
    /*  The shape is the primitive's, so the inspector names the LINE
     *  rather than the pole a primitive drew, and says which way it
     *  runs.  A meet shares its tile with the line or the line
     *  under it, and says so. */
    shape_close(w->shape);
    w->shape = shape_open("power line at %d,%d%s", (int)col, (int)row,
                          meet ? ", sharing the tile" : "");
    shape_note("links\t%s%s%s%s", links & L_N ? "north " : "", links & L_E ? "east " : "",
               links & L_S ? "south " : "", links & L_W ? "west " : "");
    script_emit_close();
    script_emit_open(w->m, w->c, w->mask_bit, tile_order(w->c, col, row, w->mask_bit));
    api_prop_push(L, &at);
    lua_pushboolean(L, meet);
    return 2;
}

/*  THE MARGINS, one path at a time.  `w:margins()` says how many the
 *  network holds and whether the outline is on.  `w:margin(i)` gathers
 *  one and opens its shape, or answers nothing where there is nothing
 *  there to draw. */
static WalkFan s_world_walk;

static int api_world_margins(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, margin_count());
    lua_pushboolean(L, margin_outline());
    return 2;
}

static int api_world_margin(lua_State *L)
{
    WorldFan *w = world_of(L);
    ShapeId   sh;
    if (!w)
        return 0;
    /*  The one before is closed FIRST.  A shape opened while another is
     *  still open nests inside it.  Closing the outer one then takes the
     *  inner with it.  Which leaves every triangle the script drew
     *  claimed by nothing at all. */
    shape_close(w->shape);
    w->shape = SHAPE_NONE;
    /*  An edit's build keeps the chunks it does not rebuild, and a margin
     *  draws nothing else.  So a path wholly in kept chunks is not
     *  gathered, and its shape is the one the last build opened. */
    if (incr_editing())
    {
        float box[4];
        if (margin_box((int)luaL_checkinteger(L, 2), box) &&
            !incr_want_rect((int32_t)floorf(box[0]) - 1, (int32_t)floorf(box[1]) - 1,
                            (int32_t)floorf(box[2]) + 1, (int32_t)floorf(box[3]) + 1))
            return 0;
    }
    if (!margin_gather(w->m, w->c, w->mask_bit, (int)luaL_checkinteger(L, 2),
                         &s_world_walk, &sh))
        return 0;
    w->shape = sh;
    api_object_push(L, "margin", &s_world_walk);
    return 1;
}

/*  THE JUNCTION RINGS.  A junction's outline is walked from the arms the
 *  measure filled, and both the trims and the box drawn later read it.
 *  So the script is asked for every one in a pass of its own between the
 *  two.  `w:junctions()` says how many there are, `w:junction(i)` sets
 *  one up and hands over its arms, and `w:junction_ring()` keeps what
 *  the script walked. */
static OutlineFan s_world_ring;
static V2         s_world_ring_out[JUNC_MAX];
static uint8_t    s_world_ring_mouth[JUNC_MAX];
static float      s_world_ring_trim[4];

static int api_world_junctions(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    junction_rings_reset();
    lua_pushinteger(L, build_junction_count(w->c));
    return 1;
}

static int api_world_junction(lua_State *L)
{
    WorldFan *w = world_of(L);
    Family    f;
    int32_t   col, row;
    int       links;
    if (!w || !build_junction_nth(w->c, (int)luaL_checkinteger(L, 2), &f, &col, &row, &links))
        return 0;
    if (!junction_ask(w->c, f, col, row, links, &s_world_ring, s_world_ring_out,
                      s_world_ring_mouth, s_world_ring_trim))
        return 0;
    api_object_push(L, "outline", &s_world_ring);
    return 1;
}

static int api_world_junction_ring(lua_State *L)
{
    if (!world_of(L))
        return 0;
    junction_ring_keep(&s_world_ring);
    return 0;
}

/*  THE JUNCTION CONTROLS.  `w:controls()` takes the reading, every
 *  junction on the map, and what its own family measured there, and
 *  answers how many there are.  `w:control(i)` hands one over as the
 *  name of the rule that settles it and a table of the reading.
 *  `w:control_is` puts the answer, a stop or a signal an arm, on to the
 *  tile.  Nothing that turns on a control is measured until every one of
 *  them has been answered. */
static int api_world_controls(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    build_networks_controls(w->c);
    lua_pushinteger(L, net_control_asked());
    return 1;
}

static int api_world_control(lua_State *L)
{
    int         i = (int)luaL_checkinteger(L, 2), e, links, busy;
    int32_t     col, row;
    const int  *cls, *traf;
    const char *rule;
    if (!world_of(L))
        return 0;
    rule = net_control_ask_at(i, &col, &row, &links, &cls, &traf, &busy);
    if (!rule)
        return 0;
    lua_pushstring(L, rule);
    lua_newtable(L);
    lua_pushinteger(L, col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, row), lua_setfield(L, -2, "row");
    lua_pushinteger(L, links), lua_setfield(L, -2, "links");
    lua_pushboolean(L, busy), lua_setfield(L, -2, "busy");
    if (cls)
    {
        /*  The arms, class and traffic, with nothing where there is no
         *  arm.  A rule reads them BY EDGE, and an edge counts from
         *  nought like every other index the API hands over. */
        lua_newtable(L);
        for (e = 0; e < 4; ++e)
        {
            if (cls[e] < 0)
                continue;
            lua_newtable(L);
            lua_pushinteger(L, cls[e]), lua_setfield(L, -2, "class");
            lua_pushinteger(L, traf[e]), lua_setfield(L, -2, "traffic");
            lua_rawseti(L, -2, e);
        }
        lua_setfield(L, -2, "arms");
    }
    return 2;
}

static int api_world_control_is(lua_State *L)
{
    int i = (int)luaL_checkinteger(L, 2), e, v = 0;
    if (!world_of(L))
        return 0;
    /*  The answer is read by edge too, from nought. */
    if (lua_istable(L, 3))
        for (e = 0; e < 4; ++e)
        {
            lua_rawgeti(L, 3, e);
            v |= ((int)lua_tointeger(L, -1) & 3) << (2 * e);
            lua_pop(L, 1);
        }
    net_control_is(i, v);
    return 0;
}

/*  THE CORRIDOR SHELVES the grading pass left, for the rule that
 *  reconciles the copies two corridors leave at a corner they share. */
static ShelfFan s_world_shelf;

/*  HOW FAR THE BUILD HAS GOT.  The drive names each step as it starts
 *  it, so whatever is showing a bar can move it.  Nothing here draws:
 *  build.c hands the name to whoever asked to be told. */
static int api_world_progress(lua_State *L)
{
    if (!world_of(L))
        return 0;
    build_step(luaL_checkstring(L, 2), (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4));
    return 0;
}

/*  The pieces an edit's build may show going, as its grading pass begins.
 *  It answers a handle of the kind edit_show for arc.rules.edit_show.  It
 *  answers nothing where the build is no edit being shown (render/show.h). */
static int api_world_edit_show(lua_State *L)
{
    static int token;
    if (!world_of(L) || show_offer(SHOW_GOING, build_mesh()) < 0)
        return 0;
    api_object_push(L, "edit_show", &token);
    return 1;
}

/*  A moment inside a long step.  While an edit shows its pieces going,
 *  whatever is showing the world draws it again from here, as often as
 *  it likes.  Otherwise nothing happens. */
static int api_world_tick(lua_State *L)
{
    if (world_of(L))
        build_tick();
    return 0;
}

static int api_world_shelf(lua_State *L)
{
    if (!world_of(L) || !shelf_ask(&s_world_shelf))
        return 0;
    api_object_push(L, "shelf", &s_world_shelf);
    return 1;
}

/*  Stage three: the trims, off the rings just walked.  It answers how
 *  many mouths asked for a stripe, which the drive then has answered
 *  one at a time. */
static int api_world_trims(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    build_networks_trims(w->c);
    lua_pushinteger(L, net_xwalk_asked());
    return 1;
}

/*  ONE MOUTH'S STRIPE: what the outline asked for, the line there is
 *  to give up, and how much of it runs straight from the mouth.  The
 *  depth the rule answers is held to the line, so one meet can
 *  never eat the segment. */
static int api_world_xwalk(lua_State *L)
{
    int     i = (int)luaL_checkinteger(L, 2), e, ctrl;
    int32_t col, row;
    float   want, room, straight;
    if (!world_of(L) || !net_xwalk_ask_at(i, &col, &row, &e, &ctrl, &want, &room, &straight))
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, row), lua_setfield(L, -2, "row");
    lua_pushinteger(L, e), lua_setfield(L, -2, "arm");
    lua_pushinteger(L, ctrl), lua_setfield(L, -2, "control");
    lua_pushnumber(L, (double)want), lua_setfield(L, -2, "want");
    lua_pushnumber(L, (double)room), lua_setfield(L, -2, "room");
    lua_pushnumber(L, (double)straight), lua_setfield(L, -2, "straight");
    return 1;
}

static int api_world_xwalk_deep(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_xwalk_deep((int)luaL_checkinteger(L, 2), (float)luaL_optnumber(L, 3, 0.0));
    return 0;
}

/*  A LEVEL LAP.  `w:lap(col,row)` gathers the one on that tile and opens
 *  its shape.  It answers what the script needs to measure it: the sine
 *  of the angle the two cross at, and their two widths.  The script
 *  hands the measurements back through `w:lap_frame`, and `w:lap_draw`
 *  lays the panel, the record and the approaches from them. */
static int api_world_lap(lua_State *L)
{
    WorldFan *w      = world_of(L);
    int32_t   col    = (int32_t)luaL_checkinteger(L, 2);
    int32_t   row    = (int32_t)luaL_checkinteger(L, 3);
    int32_t   idx    = row * R_MAP + col;
    Family    f2;
    int       second;
    float     sine, line, thread;
    int32_t   xc, xr;
    if (!w)
        return 0;
    second = piece_second(w->c->xbld[idx], &f2);
    if (second < 0 || !net_family_laps(net_family(f2)))
        return 0;
    if (build_lap(w->m, w->c, w->mask_bit, col, row, second) != 0)
        return 0;
    net_lap_ask(&xc, &xr, &sine, &line, &thread);
    lua_newtable(L);
    lua_pushinteger(L, xc), lua_setfield(L, -2, "col");
    lua_pushinteger(L, xr), lua_setfield(L, -2, "row");
    lua_pushnumber(L, sine), lua_setfield(L, -2, "sin");
    lua_pushnumber(L, line), lua_setfield(L, -2, "line");
    lua_pushnumber(L, thread), lua_setfield(L, -2, "thread");
    return 1;
}

/*  A level meet's own measurements, and what its approaches carry.  Both
 *  are asked once for each meet as the mesh is built. */
static int api_world_lap_frame(lua_State *L)
{
    ScriptLap fr;
    if (!world_of(L) || !lua_istable(L, 2))
        return 0;
    memset(&fr, 0, sizeof fr);
    lua_pushvalue(L, 2);
    fr.reach = api_field_num(L, "reach", 0.0f);
    fr.mast  = api_field_num(L, "mast", 0.0f);
    fr.bed   = api_field_num(L, "bed", 0.0f);
    fr.lift  = api_field_num(L, "lift", 0.0f);
    fr.slot  = api_field_num(L, "slot", 0.0f);
    lua_pop(L, 1);
    net_lap_frame(&fr);
    return 0;
}

/*  The record, and the panel's four corners.  They come back as a
 *  `panel` handle for the script to lay the panel over.  Nothing comes
 *  back where the two lines do not meet and there is none to lay. */
static int api_world_lap_panel(lua_State *L)
{
    const LapFan *f;
    if (!world_of(L) || !(f = net_lap_panel()))
        return 0;
    api_object_push(L, "panel", (void *)f);
    return 1;
}

/*  ONE APPROACH of the meet: what the script needs to decide what
 *  stands on it, with the mesh opened for what it puts there.  Answers
 *  nothing past the second. */
static int api_world_lap_approach(lua_State *L)
{
    WorldFan         *w = world_of(L);
    ScriptApproachAsk a;
    if (!w || !net_lap_approach((int)luaL_checkinteger(L, 2), &a))
        return 0;
    /*  The mesh, opened for the marks the script decides on: each of
     *  them stands at the meet's own place in the stack.  W:lap_mark
     *  puts it where the script said. */
    script_emit_close();
    w->m->strip_class = 0.0f;
    script_emit_open(w->m, w->c, w->mask_bit, net_lap_order());
    lua_newtable(L);
    lua_pushnumber(L, a.reach), lua_setfield(L, -2, "reach");
    lua_pushnumber(L, a.mast), lua_setfield(L, -2, "mast");
    lua_pushnumber(L, a.limit), lua_setfield(L, -2, "limit");
    lua_pushnumber(L, a.line), lua_setfield(L, -2, "line");
    lua_pushnumber(L, a.x), lua_setfield(L, -2, "x");
    lua_pushnumber(L, a.y), lua_setfield(L, -2, "y");
    lua_pushnumber(L, a.fx), lua_setfield(L, -2, "fx");
    lua_pushnumber(L, a.fy), lua_setfield(L, -2, "fy");
    lua_pushnumber(L, a.gx), lua_setfield(L, -2, "gx");
    lua_pushnumber(L, a.gy), lua_setfield(L, -2, "gy");
    lua_pushboolean(L, a.ns), lua_setfield(L, -2, "ns");
    return 1;
}

/*  And one mark where the script put it.  `out` runs along the approach
 *  from the middle and `across` from its centerline.  The mark names the
 *  model that stands there and the way it faces. */
static int api_world_lap_mark(lua_State *L)
{
    WorldFan       *w = world_of(L);
    ScriptApproach  mk;
    const char     *model;
    if (!w || !lua_istable(L, 2))
        return 0;
    memset(&mk, 0, sizeof mk);
    lua_pushvalue(L, 2);
    mk.out    = api_field_num(L, "out", 0.0f);
    mk.across = api_field_num(L, "across", 0.0f);
    mk.fx     = api_field_num(L, "fx", 0.0f);
    mk.fy     = api_field_num(L, "fy", 0.0f);
    lua_getfield(L, -1, "model");
    model = lua_tostring(L, -1);
    snprintf(mk.model, sizeof mk.model, "%s", model ? model : "");
    lua_pop(L, 2);
    if (net_lap_place(&mk) != 0)
        w->rc = -1;
    lua_pushboolean(L, w->rc == 0);
    return 1;
}

/*  And the approaches: the masts, the stop lines and the signs. */
static int api_world_lap_approaches(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = net_lap_approaches();
    if (rc != 0)
        w->rc = rc;
    lua_pushboolean(L, rc == 0);
    return 1;
}

static int api_world_emitted(lua_State *L)
{
    if (world_of(L))
        script_emit_close();
    return 0;
}

/*  The three network passes.  Each answers whether it came out, and a
 *  failure is kept on the build so the pass it belongs to ends badly
 *  rather than quietly. */
static int world_nets(lua_State *L, int what)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_world_nets(w, what);
    if (rc != 0)
        w->rc = rc;
    lua_pushboolean(L, rc == 0);
    return 1;
}

/*  WHERE EVERY FAMILY'S LANES RUN.  `w:lane_runs()` answers how many
 *  family-and-class pairs the pipeline can present.  `w:lane_run(i)`
 *  names one, and `w:lane_run_is` takes the offsets the rule answered
 *  for it.  They are settled before the lane model is built, so nothing
 *  inside it has to ask. */
static int api_world_lane_runs(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_lane_runs_reset();
    lua_pushinteger(L, net_lane_runs());
    return 1;
}

static int api_world_lane_run(lua_State *L)
{
    const char *fam;
    int         cls, i = (int)luaL_checkinteger(L, 2);
    if (!world_of(L) || i < 0 || i >= net_lane_runs())
        return 0;
    net_lane_run_at(i, &fam, &cls);
    lua_pushstring(L, fam);
    lua_pushinteger(L, cls);
    return 2;
}

static int api_world_lane_run_is(lua_State *L)
{
    float off[4];
    int   i = (int)luaL_checkinteger(L, 2), k, n = 0;
    if (!world_of(L))
        return 0;
    if (lua_istable(L, 3))
    {
        n = (int)lua_rawlen(L, 3);
        if (n > (int)(sizeof off / sizeof off[0]))
            n = (int)(sizeof off / sizeof off[0]);
        for (k = 0; k < n; ++k)
        {
            lua_rawgeti(L, 3, k + 1);
            off[k] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
    }
    net_lane_run_is(i, off, n);
    return 0;
}

/*  WHERE THE TRAFFIC RUNS on a family whose stage is a rule rather than
 *  a primitive.  The drive asks it once for every class before any strip
 *  is built.  `w:traffic_run(i)` answers nothing for a pair no rule
 *  settles. */
static int api_world_traffic_runs(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_traffic_runs_reset();
    lua_pushinteger(L, net_traffic_runs());
    return 1;
}

static int api_world_traffic_run(lua_State *L)
{
    const char *rule;
    int         cls;
    if (!world_of(L) || (rule = net_traffic_run_at((int)luaL_checkinteger(L, 2), &cls)) == NULL)
        return 0;
    lua_pushstring(L, rule);
    lua_pushinteger(L, cls);
    return 2;
}

static int api_world_traffic_run_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_traffic_run_is((int)luaL_checkinteger(L, 2), (float)luaL_optnumber(L, 3, 0.0), (float)luaL_optnumber(L, 4, 0.0));
    return 0;
}

/*  WHERE A STRIP STANDS CLEAR of the ground, on a family whose flies
 *  stage is a rule.  The drive asks it once for a plain strip and once
 *  for a structure, before anything is graded.  The rule answers a
 *  HEIGHT, so the grading compares at each station without asking again.
 *  A strip that always stands clear answers -math.huge. */
static int api_world_flies_runs(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_flies_runs_reset();
    lua_pushinteger(L, net_flies_runs());
    return 1;
}

static int api_world_flies_run(lua_State *L)
{
    const char *rule;
    int         structure;
    if (!world_of(L) || (rule = net_flies_run_at((int)luaL_checkinteger(L, 2), &structure)) == NULL)
        return 0;
    lua_pushstring(L, rule);
    lua_pushboolean(L, structure);
    return 2;
}

static int api_world_flies_run_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_flies_run_is((int)luaL_checkinteger(L, 2), (float)luaL_optnumber(L, 3, 1e30));
    return 0;
}

static int api_world_lanes(lua_State *L)
{
    return world_nets(L, 0);
}

/*  THE FITS the walk read but did not run: every segment's path.  For a
 *  family whose runs may leave its own cells the two candidates and the
 *  choice between them. */
static int api_world_fits(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, net_fits());
    return 1;
}

static int api_world_fit(lua_State *L)
{
    WorldFan *w = world_of(L);
    void     *h;
    if (!w)
        return 0;
    net_fit_begin(w->c, (int)luaL_checkinteger(L, 2));
    if ((h = path_handle()) == NULL)
        return 0;
    api_object_push(L, "path", h);
    return 1;
}

static int api_world_fit_done(lua_State *L)
{
    const int  *free_, *held;
    const char *fam;
    int         k;
    static const char *const KEY[3] = {"corners", "tight", "nodes"};
    if (!world_of(L) || !net_fit_done((int)luaL_checkinteger(L, 2)))
        return 0;
    if ((fam = net_fit_choice(&free_, &held)) == NULL)
        return 0;
    lua_newtable(L);
    lua_pushstring(L, fam), lua_setfield(L, -2, "family");
    lua_newtable(L);
    for (k = 0; k < 3; ++k)
        lua_pushinteger(L, free_[k]), lua_setfield(L, -2, KEY[k]);
    lua_setfield(L, -2, "free");
    lua_newtable(L);
    for (k = 0; k < 3; ++k)
        lua_pushinteger(L, held[k]), lua_setfield(L, -2, KEY[k]);
    lua_setfield(L, -2, "held");
    return 1;
}

static int api_world_fit_choice_is(lua_State *L)
{
    const char *v = lua_tostring(L, 2);
    if (!world_of(L))
        return 0;
    net_fit_choice_is(!v || strcmp(v, "free") == 0);
    return 0;
}

/*  ONE CLASS FOR A WHOLE SEGMENT, before anything is fitted.
 *  `w:seg_classes()` steps every segment and answers how many there are.
 *  `w:seg_class(i)` hands over how many of one segment's tiles read as
 *  each class.  `w:seg_class_is` takes the class settled from them. */
static int api_world_seg_classes(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    lua_pushinteger(L, build_networks_classes(w->m, w->c, w->mask_bit, !w->rotated));
    return 1;
}

static int api_world_seg_class(lua_State *L)
{
    const int32_t *cells;
    int            cnt[3], nt, k;
    if (!world_of(L) || !net_seg_class_at((int)luaL_checkinteger(L, 2), cnt, &cells, &nt))
        return 0;
    lua_createtable(L, 0, 3);
    /*  How many of the segment's tiles wear each class, as a sequence
     *  the rule reads with ipairs. */
    lua_createtable(L, 3, 0);
    for (k = 0; k < 3; ++k)
    {
        lua_pushinteger(L, cnt[k]);
        lua_rawseti(L, -2, k + 1);
    }
    lua_setfield(L, -2, "classes");
    /*  And the tiles themselves, counted from nought with their length.
     *  So a rule may read the density and the neighborhood at them
     *  through arc.city rather than being told a tally and no more. */
    lua_createtable(L, nt, 0);
    for (k = 0; k < nt; ++k)
    {
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, cells[k] % R_MAP), lua_setfield(L, -2, "col");
        lua_pushinteger(L, cells[k] / R_MAP), lua_setfield(L, -2, "row");
        lua_rawseti(L, -2, k);
    }
    lua_setfield(L, -2, "cells");
    lua_pushinteger(L, nt), lua_setfield(L, -2, "n");
    return 1;
}

static int api_world_seg_class_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_seg_class_is((int)luaL_checkinteger(L, 2), (int)luaL_optinteger(L, 3, 0));
    return 0;
}

static int api_world_networks(lua_State *L)
{
    return world_nets(L, 1);
}

static int api_world_networks_draw(lua_State *L)
{
    return world_nets(L, 3);
}

/*  THE DRAWING PASS, one family at a time.  `w:net_families()` says how
 *  many networks are walked, `w:junction_boxes(fk)` draws one family's
 *  junctions.  All of them, before any of its segments, so a leg knows
 *  whether it is signaled before it draws its stripe.  And
 *  `w:segments(fk)` sets the cursor over what follows.  `w:segment()`
 *  draws the next of them and answers whether there was one. */
static int api_world_net_families(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, build_draw_families());
    return 1;
}

static int api_world_junction_boxes(lua_State *L)
{
    WorldFan *w  = world_of(L);
    int       fk = (int)luaL_checkinteger(L, 2);
    if (!w || fk < 0 || fk >= build_draw_families())
        return 0;
    build_draw_boxes_begin(w->m, w->c, w->mask_bit, fk);
    return 0;
}

/*  ONE JUNCTION'S BOX.  Answers nothing when the family has no more,
 *  false where the box lays no fill.  A thread junction, or a box
 *  reaching no chunk this build draws.  And the outline to lay it on
 *  otherwise.  `w:junction_box_done` takes up the margin round it, the
 *  signs on its sides and its corners. */
static int api_world_junction_box(lua_State *L)
{
    WorldFan *w = world_of(L);
    JuncFan  *j;
    int       rc;
    if (!w)
        return 0;
    rc = build_draw_box_next();
    if (rc < 0)
        w->rc = rc;
    if (rc <= 0)
        return 0;
    (void)j;
    lua_pushboolean(L, 1);
    return 1;
}

/*  The connectors between the box's arms, taken from the pieces the
 *  drive cut, and the family's own drawing on the outline after them. */
static int api_world_junction_lanes(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_junction_lanes();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

/*  THE JUNCTION IN HAND AS ITS ARMS STAND, for the rule that lays the
 *  pattern its lanes make.  Nothing where there is no junction to
 *  pattern: the grading pass, or a box the family draws none for. */
static int s_turns_rec; /* the turns handle's record: the store is the lane model's */

static int api_world_junction_turns(lua_State *L)
{
    if (!world_of(L) || !lane_turns_ask())
        return 0;
    api_object_push(L, "turns", (void *)&s_turns_rec);
    return 1;
}

/*  THE THREAD JUNCTION IN HAND, for the rule that says which threads it
 *  carries.  Nothing where the box in hand is not a thread family's. */
static int s_threads_rec; /* the threads handle's record: the store is the thread box's */

static int api_world_node_threads(lua_State *L)
{
    if (!world_of(L) || !net_threads_ask())
        return 0;
    api_object_push(L, "threads", (void *)&s_threads_rec);
    return 1;
}

/*  And the threads the rule asked for, lofted from the pieces the drive
 *  cut for them.  Nothing where the box carries none. */
static int api_world_node_threads_done(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushboolean(L, net_threads_done());
    return 1;
}

/*  THE PAVED BOX, in its two moments: the outline gathered for
 *  arc.rules.junction to lay the fill on.  Then the fill taken back and
 *  the box handed to the signs.  Each answers nothing where the family's
 *  junction is not a paved box, or where the pass composes none. */
static int api_world_box_paving(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushboolean(L, net_box_paving_ask());
    return 1;
}

static int api_world_box_paving_done(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushboolean(L, net_box_paving_done());
    return 1;
}

/*  THE JUNCTION'S ARMS, for the rule that stands its signs.  The emit
 *  window is opened at the box's order and closed by the drive. */
static int s_signs_rec;

static int api_world_junction_signs(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w || !net_junction_signs_ask())
        return 0;
    net_junction_signs_enter();
    script_emit_open(w->m, w->c, w->mask_bit, net_junction_signs_order());
    api_object_push(L, "signs", (void *)&s_signs_rec);
    return 1;
}

static int api_world_junction_signs_done(lua_State *L)
{
    if (!world_of(L))
        return 0;
    script_emit_close();
    net_junction_signs_leave();
    net_junction_signs_taken();
    return 0;
}

/*  THE DEAD ENDS of the segment in hand, for the rule that decides what
 *  a lane does where its segment stops.  Nothing where the segment has
 *  no dead end. */
static int s_caps_rec; /* the caps handle's record: the store is the lane model's */

static int api_world_segment_caps(lua_State *L)
{
    if (!world_of(L) || !lane_caps_ask())
        return 0;
    api_object_push(L, "caps", (void *)&s_caps_rec);
    return 1;
}

/*  THE SPUR IN HAND, for the rule that says where it aims on the line.
 *  Nothing where there is no spur being built. */
static int s_target_rec; /* the target handle's record: the store is the spur builder's */

static int api_world_spur_target(lua_State *L)
{
    if (!world_of(L) || !build_spur_target())
        return 0;
    api_object_push(L, "target", (void *)&s_target_rec);
    return 1;
}

/*  The outline the fill is laid on, once the margin round it has been
 *  answered: the fan is cut back to the margin's inner edge.  So there
 *  is nothing to lay before that. */
static int api_world_box_fill(lua_State *L)
{
    JuncFan *j;
    if (!world_of(L) || (j = net_junction_fan()) == NULL)
        return 0;
    api_object_push(L, "junction", j);
    return 1;
}

/*  And the strips the box gathered rather than drew: a thread junction's
 *  threads, each lofted as a segment's strip is. */
static int api_world_box_lofts(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, net_box_lofts());
    return 1;
}

static int api_world_box_loft(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = net_box_loft((int)luaL_checkinteger(L, 2));
    if (rc != 0)
        w->rc = rc;
    return 0;
}

static int api_world_junction_box_done(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_junction_done();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

static int api_world_segments(lua_State *L)
{
    WorldFan *w  = world_of(L);
    int       fk = (int)luaL_checkinteger(L, 2);
    if (!w || fk < 0 || fk >= build_draw_families())
        return 0;
    build_draw_begin(w->m, w->c, w->mask_bit, !w->rotated, fk);
    return 0;
}

static int api_world_segment(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_draw_next();
    if (rc < 0)
        w->rc = rc;
    lua_pushboolean(L, rc > 0);
    return 1;
}

/*  And what follows the strip the last one lofted: the tiles it serves
 *  and the caps at its dead ends. */
static int api_world_segment_done(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_draw_done();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

static int api_world_networks_drawn(lua_State *L)
{
    if (!world_of(L))
        return 0;
    build_networks_drawn();
    return 0;
}

static int api_world_bands(lua_State *L)
{
    return world_nets(L, 2);
}

/*  THE LOFT'S STAGES, in the order it asks for them.  Each answers the
 *  name of the rule that settles it, and the thing that rule is handed.
 *  It answers nothing where a primitive of the pipeline's own settled
 *  it.  A family declares which of the two answers each stage. */
static int loft_stage(lua_State *L, const char *rule, const char *kind, void *obj)
{
    if (!rule)
        return 0;
    lua_pushstring(L, rule);
    api_object_push(L, kind, obj);
    return 2;
}

static int api_world_loft_taper(lua_State *L)
{
    if (!world_of(L))
        return 0;
    return loft_stage(L, net_loft_taper(), "strip", net_loft_working());
}

static int api_world_loft_profile(lua_State *L)
{
    GroundFan  *g;
    const char *rule, *kind;
    void       *o;
    if (!world_of(L))
        return 0;
    rule = net_loft_profile(&g);
    /*  A family's own primitive may leave a reading of its own for the
     *  rule: a slab's elevation rather than the strip. */
    if ((o = net_stage_taken(&kind)) != NULL)
        return loft_stage(L, rule, kind, o);
    return g ? loft_stage(L, rule, "ground", g) : loft_stage(L, rule, "strip", net_loft_working());
}

static int api_world_loft_dropped(lua_State *L)
{
    const char *rule, *kind;
    void       *o;
    if (!world_of(L))
        return 0;
    rule = net_loft_dropped();
    o    = net_stage_taken(&kind);
    return o ? loft_stage(L, rule, kind, o) : 0;
}

static int api_world_loft_works(lua_State *L)
{
    if (!world_of(L))
        return 0;
    return loft_stage(L, net_loft_works(), "strip", net_loft_working());
}

/*  THE RECORD and THE FURNITURE.  Both are a stage of two halves with
 *  the drive between them.  What the strip leaves for the passes that
 *  read it, and what stands beside it.  A line's margin is composed by
 *  the rule.  One family's lamps and another's signs come back as a
 *  list, and the pipeline places them.  Where each stands along the
 *  strip is the rule's, and the walk that turns a distance into a place
 *  is not. */
static int api_world_loft_record(lua_State *L)
{
    if (!world_of(L))
        return 0;
    return loft_stage(L, net_loft_record(), "strip", net_loft_working());
}

static int api_world_loft_record_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_strip_margin_drew(lua_toboolean(L, 2));
    return 0;
}

static int api_world_loft_furniture(lua_State *L)
{
    const char *rule;
    Loft       *o;
    if (!world_of(L))
        return 0;
    rule = net_loft_furniture();
    o    = net_loft_working();
    if (!rule || !o)
        return 0;
    lua_pushstring(L, rule);
    /*  EVERY rule is handed the strip itself, and stands what it wants
     *  beside it.  The meets on the way and the ends the line runs on
     *  past are the strip's to answer, not a stage's to gather. */
    api_object_push(L, "strip", o);
    return 2;
}

/*  What a rule gathered for a stage that walks it.  A family whose props
 *  rule stands them ITSELF gathers nothing here.  It has already stood
 *  what it wanted through arc.put, on the strip it was handed, and there
 *  is nothing to take back. */
static int api_world_loft_furniture_is(lua_State *L)
{
    (void)L;
    return 0;
}

static int api_world_loft_recorded(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = net_loft_recorded();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

/*  THE STRIP THE LOFT FINISHED.  `w:curves()` answers it while the
 *  tuning window asks to see the fitted line over the world it made.
 *  `w:strip()` answers it while the slab is drawn at all: the grading
 *  pass lays no triangles.  `w:strip_done()` closes its shape.  Every
 *  loft in the build comes through this one place. */
static int api_world_curves(lua_State *L)
{
    Loft *o;
    if (!world_of(L) || (o = net_loft_curves()) == NULL)
        return 0;
    api_object_push(L, "strip", o);
    return 1;
}

static int api_world_strip(lua_State *L)
{
    Loft *o;
    if (!world_of(L) || (o = net_loft_strip()) == NULL)
        return 0;
    api_object_push(L, "strip", o);
    return 1;
}

static int api_world_strip_done(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = net_loft_close();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

/*  THE SLAB BANDS, one at a time: the drive composes each slab between
 *  its loft and the lanes laid under it.  `w:band_next()` answers
 *  whether there was another. */
static int api_world_band_next(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_band_next();
    if (rc < 0)
        w->rc = rc;
    lua_pushboolean(L, rc > 0);
    return 1;
}

/*  ---- WHERE EVERY TILE'S TOP COMES FROM ---------------------------------
 *
 *  Six places, and arc.rules.terrain says which of them each tile draws
 *  from.  It walks the map itself and answers three planes at once.
 *  They are the place, the tile whose PAD a footprint stands on, and the
 *  tile whose place in the painter's stack this one takes.  Nothing in
 *  the pipeline classifies a cell for it.
 *
 *  The handle resets the store.  A rule that answers nothing then leaves
 *  every tile on the field.  That is a city with no water, no pads and
 *  no shelves in it.  This is what taking the rule away should look
 *  like. */
static int          s_terrain_rec;

static int api_world_terrain(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    mesh_tops_reset();
    obj_terrain_city_is(w->c);
    api_object_push(L, "terrain", (void *)&s_terrain_rec);
    return 1;
}

/*  THE FIELD, once the tops are settled: a water tile's bed is clamped
 *  under the surface drawn over it.  So the rule has to have answered
 *  first, and it is the drive that says when. */
static int api_world_field(lua_State *L)
{
    if (world_of(L))
        build_pass_field();
    return 0;
}

/*  EVERY ON-SPUR TILE, for the rule that walks the map and reads them.
 *  The rule names each tile it finds and answers for it.  Nothing here
 *  looks for one. */
static int s_spurs_rec;

static int api_world_spurs(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w || !net_spurs_begin(w->c))
        return 0;
    api_object_push(L, "spurs", (void *)&s_spurs_rec);
    return 1;
}

/*  AND THE PAIRS whose tapers face each other, with the tiles between
 *  them for the rule to divide. */
static int api_world_spur_shares(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, net_spur_shares());
    return 1;
}

/*  `gap` tiles lie between the two slab tiles, and `cap` is the longest
 *  taper either may have.  The answer is what each is cut to, or -1 to
 *  leave them alone. */
static int api_world_spur_share(lua_State *L)
{
    float gap;
    int   cap;
    if (!world_of(L) || !net_spur_share_at((int)luaL_checkinteger(L, 2), &gap, &cap))
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, (double)gap), lua_setfield(L, -2, "gap");
    lua_pushinteger(L, cap), lua_setfield(L, -2, "cap");
    return 1;
}

static int api_world_spur_share_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_spur_share_is((int)luaL_checkinteger(L, 2),
                      lua_isnumber(L, 3) ? (int)lua_tointeger(L, 3) : -1);
    return 0;
}

/*  AND WHERE EACH SPUR'S DESCENT RUNS along its slab. */
static int api_world_spur_spans(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, net_spur_spans());
    return 1;
}

/*  `at` is its station's distance along the band.  `len` is its taper in
 *  tiles.  `leaves` says whether it goes down from the slab or up onto
 *  it.  `sgn` says whether the band's own direction runs with the
 *  spur's.  Answers the top of the descent, its foot, how long it is,
 *  and which way along the band the taper lies. */
static int api_world_spur_span(lua_State *L)
{
    float at;
    int   len, leaves, sgn;
    if (!world_of(L) || !net_spur_span_at((int)luaL_checkinteger(L, 2), &at, &len, &leaves, &sgn))
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, (double)at), lua_setfield(L, -2, "at");
    lua_pushinteger(L, len), lua_setfield(L, -2, "len");
    lua_pushboolean(L, leaves), lua_setfield(L, -2, "leaves");
    lua_pushinteger(L, sgn), lua_setfield(L, -2, "sgn");
    return 1;
}

static int api_world_spur_span_is(lua_State *L)
{
    int i = (int)luaL_checkinteger(L, 2);
    if (!world_of(L))
        return 0;
    if (!lua_istable(L, 3))
    {
        net_spur_span_is(i, 0, 0.0f, 0.0f, 0.0f, 0.0f);
        return 0;
    }
    lua_pushvalue(L, 3);
    net_spur_span_is(i, 1, api_field_num(L, "top", 0.0f), api_field_num(L, "foot", 0.0f),
                     api_field_num(L, "total", 0.0f), api_field_num(L, "along", 0.0f));
    lua_pop(L, 1);
    return 0;
}

/*  A junction's cycle: which phase each of the eight staggers starts at,
 *  and which group of arms an edge belongs to. */
static int api_world_signal_phase_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_signal_phase_is((int)luaL_checkinteger(L, 2), (float)luaL_optnumber(L, 3, 0.0));
    return 0;
}

static int api_world_signal_group_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_signal_group_is((int)luaL_checkinteger(L, 2), (float)luaL_optnumber(L, 3, 0.0));
    return 0;
}

/*  What class a tile's line is, for every traffic byte there is. */
static int api_world_line_class_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_line_class_is((int)luaL_checkinteger(L, 2), (int)luaL_optinteger(L, 3, 0));
    return 0;
}

/*  And how many cars one tile's traffic is worth. */
static int api_world_car_density_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_car_density_is((int)luaL_checkinteger(L, 2), (int)luaL_optinteger(L, 3, 0));
    return 0;
}


/*  THE POINTS THE SLAB'S FIT IS GIVEN, as the rule picks them from the
 *  band the walk read.
 *
 *      The straight cells'.
 *      A lone block's corner.
 *      Nothing of a staircase. */
static int api_world_band_chain(lua_State *L)
{
    StairFan *st;
    if (!world_of(L) || (st = net_band_chain()) == NULL)
        return 0;
    api_object_push(L, "stair", st);
    return 1;
}

static int api_world_band_chained(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    build_band_chained();
    return 0;
}

/*  THE SLAB'S OWN FIT, run the same way a segment's is: two candidates
 *  and the choice between them.  Then the band takes up the one that was
 *  kept.  A band the building pass replayed was never fitted, so it asks
 *  for none. */
static int api_world_band_fits(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, net_band_fits());
    return 1;
}

static int api_world_band_fit(lua_State *L)
{
    void *h;
    if (!world_of(L))
        return 0;
    net_band_fit_begin((int)luaL_checkinteger(L, 2));
    if ((h = path_handle()) == NULL)
        return 0;
    api_object_push(L, "path", h);
    return 1;
}

static int api_world_band_fit_done(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_band_fit_done((int)luaL_checkinteger(L, 2));
    return 0;
}

static int api_world_band_fit_choice(lua_State *L)
{
    static const char *const KEY[3] = {"corners", "tight", "nodes"};
    const int               *free_, *held;
    const char              *fam;
    int                      k;
    if (!world_of(L) || (fam = net_band_fit_choice(&free_, &held)) == NULL)
        return 0;
    lua_newtable(L);
    lua_pushstring(L, fam), lua_setfield(L, -2, "family");
    lua_newtable(L);
    for (k = 0; k < 3; ++k)
        lua_pushinteger(L, free_[k]), lua_setfield(L, -2, KEY[k]);
    lua_setfield(L, -2, "free");
    lua_newtable(L);
    for (k = 0; k < 3; ++k)
        lua_pushinteger(L, held[k]), lua_setfield(L, -2, KEY[k]);
    lua_setfield(L, -2, "held");
    return 1;
}

static int api_world_band_fit_choice_is(lua_State *L)
{
    const char *v = lua_tostring(L, 2);
    if (!world_of(L))
        return 0;
    net_band_fit_choice_is(!v || strcmp(v, "free") == 0);
    return 0;
}

/*  And the band drawn, from the pieces the drive cut for it. */
static int api_world_band_cut(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    if (build_band_cut() != 0)
        w->rc = -1;
    return 0;
}

static int api_world_band_fitted(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_band_fitted();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

static int api_world_band_done(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_band_done();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

/*  THE SPURS, one at a time.  Each is read up to the join the rule
 *  slides along the line's lane.  It is taken up again once the rule has
 *  answered. */
static int api_world_spur_next(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    lua_pushboolean(L, build_spur_next());
    return 1;
}

/*  The lanes within reach of the spur's end in hand, for the rule that
 *  picks one.  Nothing where there is no spur in hand. */
static int api_world_spur_pick(lua_State *L)
{
    if (!world_of(L) || lane_snap_count() < 1)
        return 0;
    obj_snap_push(L);
    return 1;
}

/*  And the descent routed from the slab lane it picked, with the line
 *  end's lanes then measured in the same way. */
static int api_world_spur_routed(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushboolean(L, build_spur_routed());
    return 1;
}

/*  And the descent taken from the pieces the drive cut, with the line
 *  end's lanes then measured for the next pick. */
static int api_world_spur_joined(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushboolean(L, build_spur_joined());
    return 1;
}

/*  And the slide answered, with the legs the join falls back on queued
 *  for the drive to cut where no placing held. */
static int api_world_spur_slid(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushboolean(L, build_spur_slid());
    return 1;
}

static int api_world_spur_slide(lua_State *L)
{
    SlideFan *s;
    if (!world_of(L) || (s = net_spur_slide()) == NULL)
        return 0;
    api_object_push(L, "slide", s);
    return 1;
}

static int api_world_spur_done(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_spur_done();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

/*  How many of the spurs' strips the pass lofted. */
static int api_world_spur_lofts(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, build_spur_lofts());
    return 1;
}

static int api_world_spur_loft(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_spur_loft((int)luaL_checkinteger(L, 2));
    if (rc != 0)
        w->rc = rc;
    return 0;
}

static int api_world_band_spurs(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_band_spurs(w->m, w->c, w->mask_bit, !w->rotated);
    if (rc != 0)
        w->rc = rc;
    lua_pushboolean(L, rc == 0);
    return 1;
}

/*  THE LANE OVERLAY'S WIRES, laid last: the hairline over every lane the
 *  passes gathered, each entered again under the shape it belongs to. */
static int api_world_wires(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, net_wires());
    return 1;
}

static int api_world_wire(lua_State *L)
{
    LaneFan *f;
    if (!world_of(L) || (f = net_wire_at((int)luaL_checkinteger(L, 2))) == NULL)
        return 0;
    api_object_push(L, "lane", f);
    return 1;
}

static int api_world_wire_done(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_wire_done((int)luaL_checkinteger(L, 2));
    return 0;
}

/*  ACROSS A LAP.  These are the lanes' open ends as the band pass left
 *  them.  The rule carries each of them on into the lane facing it.
 *  `w:band_links` then joins the bands' ends into the lines they become. */
static int api_world_lane_cross(lua_State *L)
{
    WorldFan *w = world_of(L);
    XLaneFan *x;
    if (!w || (x = lane_cross_ask(w->m, w->c, w->mask_bit)) == NULL)
        return 0;
    api_object_push(L, "cross", x);
    return 1;
}

/*  And the links laid on the pieces the drive cut for them. */
static int api_world_lane_cross_done(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    if (lane_cross_take() != 0)
        w->rc = -1;
    return 0;
}

/*  The band's lane ends, handed over for the script to join: which
 *  slab lane goes on to which is arc.rules.links's.  Nothing where the
 *  pass has none to join. */
static int api_world_band_links(lua_State *L)
{
    WorldFan *w = world_of(L);
    void     *f;
    if (!w || (f = build_band_links(w->m, w->c, w->mask_bit)) == NULL)
        return 0;
    api_object_push(L, "links", f);
    return 1;
}

/*  And what follows the joining: every lane end goes somewhere and every
 *  start has something arriving. */
static int api_world_band_links_done(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    if (build_band_links_done() != 0)
        w->rc = -1;
    return 0;
}

const luaL_Reg WORLD[] = {
    {"info",     api_world_info    },
    {"wanted",   api_world_wanted  },
    {"tile",     api_world_tile    },
    {"zone",     api_world_zone    },
    {"shape",    api_world_shape   },
    {"slab_near", api_world_slab_near},
    {"loft",     api_world_loft    },
    {"net_discover", api_world_net_discover},
    {"net_cells",    api_world_net_cells},
    {"net_found",    api_world_net_found},
    {"band_cells",     api_world_band_cells},
    {"cuts",         api_world_cuts},
    {"cut",          api_world_cut},
    {"cut_done",     api_world_cut_done},
    {"gate_rest",      api_world_gate_rest},
    {"junction_bands", api_world_junction_bands},
    {"junction_band",  api_world_junction_band},
    {"junction_band_done", api_world_junction_band_done},
    {"junction_trim",  api_world_junction_trim},
    {"box_band",       api_world_box_band},
    {"junction_lanes", api_world_junction_lanes},
    {"junction_turns", api_world_junction_turns},
    {"node_threads", api_world_node_threads},
    {"node_threads_done", api_world_node_threads_done},
    {"box_paving", api_world_box_paving},
    {"box_paving_done", api_world_box_paving_done},
    {"junction_signs", api_world_junction_signs},
    {"junction_signs_done", api_world_junction_signs_done},
    {"segment_caps", api_world_segment_caps},
    {"spur_target", api_world_spur_target},
    {"box_fill",    api_world_box_fill},
    {"box_band_done",  api_world_box_band_done},
    {"mouth",          api_world_mouth},
    {"mouth_is",       api_world_mouth_is},
    {"power",    api_world_power   },
    {"junctions",    api_world_junctions   },
    {"junction",     api_world_junction    },
    {"junction_ring", api_world_junction_ring},
    {"trims",        api_world_trims       },
    {"shelf",    api_world_shelf   },
    {"progress", api_world_progress},
    {"tick", api_world_tick},
    {"edit_show", api_world_edit_show},
    {"controls", api_world_controls},
    {"control",  api_world_control },
    {"control_is", api_world_control_is},
    {"xwalk",    api_world_xwalk   },
    {"xwalk_deep", api_world_xwalk_deep},
    {"lap",        api_world_lap       },
    {"lap_frame", api_world_lap_frame},
    {"lap_panel", api_world_lap_panel},
    {"lap_approach", api_world_lap_approach},
    {"lap_mark", api_world_lap_mark},
    {"lap_approaches", api_world_lap_approaches},
    {"margins", api_world_margins},
    {"margin",  api_world_margin },
    {"emitted",  api_world_emitted },
    {"lanes",    api_world_lanes   },
    {"lane_runs", api_world_lane_runs},
    {"lane_run",  api_world_lane_run },
    {"lane_run_is", api_world_lane_run_is},
    {"traffic_runs", api_world_traffic_runs},
    {"traffic_run",  api_world_traffic_run },
    {"traffic_run_is", api_world_traffic_run_is},
    {"flies_runs", api_world_flies_runs},
    {"flies_run",  api_world_flies_run },
    {"flies_run_is", api_world_flies_run_is},
    {"networks", api_world_networks},
    {"seg_classes", api_world_seg_classes},
    {"fits",     api_world_fits    },
    {"fit",      api_world_fit     },
    {"fit_done", api_world_fit_done},
    {"fit_choice_is", api_world_fit_choice_is},
    {"seg_class",  api_world_seg_class },
    {"seg_class_is", api_world_seg_class_is},
    {"networks_draw", api_world_networks_draw},
    {"net_families", api_world_net_families},
    {"junction_boxes", api_world_junction_boxes},
    {"junction_box", api_world_junction_box},
    {"junction_box_done", api_world_junction_box_done},
    {"box_lofts", api_world_box_lofts},
    {"box_loft",  api_world_box_loft },
    {"segments", api_world_segments},
    {"segment",  api_world_segment },
    {"segment_done", api_world_segment_done},
    {"networks_drawn", api_world_networks_drawn},
    {"bands", api_world_bands},
    {"loft_taper", api_world_loft_taper},
    {"loft_profile", api_world_loft_profile},
    {"loft_dropped", api_world_loft_dropped},
    {"loft_works", api_world_loft_works},
    {"loft_record", api_world_loft_record},
    {"loft_record_is", api_world_loft_record_is},
    {"loft_furniture", api_world_loft_furniture},
    {"loft_furniture_is", api_world_loft_furniture_is},
    {"loft_recorded", api_world_loft_recorded},
    {"curves",   api_world_curves  },
    {"strip",    api_world_strip   },
    {"strip_done", api_world_strip_done},
    {"band_next", api_world_band_next},
    {"car_density_is", api_world_car_density_is},
    {"line_class_is", api_world_line_class_is},
    {"signal_phase_is", api_world_signal_phase_is},
    {"signal_group_is", api_world_signal_group_is},
    {"terrain",  api_world_terrain },
    {"field",    api_world_field   },
    {"spurs",    api_world_spurs   },
    {"spur_shares", api_world_spur_shares},
    {"spur_share",  api_world_spur_share },
    {"spur_share_is", api_world_spur_share_is},
    {"spur_spans", api_world_spur_spans},
    {"spur_span",  api_world_spur_span },
    {"spur_span_is", api_world_spur_span_is},
    {"band_chain", api_world_band_chain},
    {"band_chained", api_world_band_chained},
    {"band_fits",  api_world_band_fits },
    {"band_fit",   api_world_band_fit  },
    {"band_fit_done", api_world_band_fit_done},
    {"band_fit_choice", api_world_band_fit_choice},
    {"band_fit_choice_is", api_world_band_fit_choice_is},
    {"band_fitted", api_world_band_fitted},
    {"band_cut", api_world_band_cut},
    {"band_done", api_world_band_done},
    {"band_spurs", api_world_band_spurs},
    {"spur_next", api_world_spur_next},
    {"spur_pick",  api_world_spur_pick},
    {"spur_routed", api_world_spur_routed},
    {"spur_joined", api_world_spur_joined},
    {"spur_slide", api_world_spur_slide},
    {"spur_slid", api_world_spur_slid},
    {"spur_done", api_world_spur_done},
    {"spur_lofts", api_world_spur_lofts},
    {"spur_loft",  api_world_spur_loft },
    {"wires",    api_world_wires   },
    {"wire",     api_world_wire    },
    {"wire_done", api_world_wire_done},
    {"lane_cross", api_world_lane_cross},
    {"lane_cross_done", api_world_lane_cross_done},
    {"band_links", api_world_band_links},
    {"band_links_done", api_world_band_links_done},
    {NULL,       NULL              }
};
