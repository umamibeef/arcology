/*  obj_lane.c: A WAY OF TRAVEL, and the world that moves along it.
 *
 *  One lane inside a line or a slab, and a lane carried across a level
 *  meet.  The network a family's cells make, and the turn the world is
 *  handed.  And the moving world itself: the beat it runs on, and the
 *  drawing of it. */
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

/*  ---- a lane -------------------------------------------------------- */

static LaneFan *lane_of(lua_State *L)
{
    return (LaneFan *)rec_of(L, "lane");
}

static const Field LANE_FIELDS[] = {
    {"n",     FLD_INT,  offsetof(LaneFan, np)},
    {"lift",  FLD_NUM,  offsetof(LaneFan, lift)},
    {"paint", FLD_NUM,  offsetof(LaneFan, paint)},
    {"band",  FLD_INT,  offsetof(LaneFan, band)},
    {"spur",  FLD_BOOL, offsetof(LaneFan, spur)},
    {"off",   FLD_BOOL, offsetof(LaneFan, off)},
    {"step",  FLD_NUM,  offsetof(LaneFan, step)},
    {NULL, FLD_NUM, 0}
};

static int api_lane_info(lua_State *L)
{
    return api_fields(L, lane_of(L), LANE_FIELDS);
}

/*  One piece of the fitted line: how long it runs and whether it turns. */
static int api_lane_piece(lua_State *L)
{
    LaneFan *f = lane_of(L);
    int      k = (int)luaL_checkinteger(L, 2);
    if (!f || k < 0 || k >= f->np)
        return 0;
    lua_pushnumber(L, f->pc[k].len);
    lua_pushboolean(L, f->pc[k].arc);
    return 2;
}

/*  Where that piece is `at` tiles along it. */
static int api_lane_at(lua_State *L)
{
    LaneFan *f = lane_of(L);
    int      k = (int)luaL_checkinteger(L, 2);
    V2       p, d;
    if (!f || k < 0 || k >= f->np)
        return 0;
    piece_at(&f->pc[k], (float)luaL_checknumber(L, 3), &p, &d);
    lua_pushnumber(L, p.x), lua_pushnumber(L, p.y);
    lua_pushnumber(L, d.x), lua_pushnumber(L, d.y);
    return 4;
}

/*  The surface the hairline floats over: the slab's own where the line
 *  belongs to a band, the ground elsewhere. */
static int api_lane_height(lua_State *L)
{
    LaneFan *f = lane_of(L);
    V2       p;
    if (!f)
        return 0;
    p.x = (float)luaL_checknumber(L, 2), p.y = (float)luaL_checknumber(L, 3);
    if (f->spur)
    {
        /*  A spur's lane climbs to the slab: the ease is the same one
         *  the spur's own strip is lifted by, so the two agree. */
        lua_pushnumber(L, surface_at_world(f->c, f->mask_bit, p.x, p.y) +
                              BAND_LIFT * ease_smooth((float)luaL_checknumber(L, 4)));
        return 1;
    }
    lua_pushnumber(L, f->band > 0 ? slab_z_near(f->c, f->mask_bit, f->band, p)
                                  : surface_at_world(f->c, f->mask_bit, p.x, p.y));
    return 1;
}

static int api_lane_order(lua_State *L)
{
    LaneFan *f = lane_of(L);
    int32_t  tc, tr;
    if (!f)
        return 0;
    tc = (int32_t)floorf((float)luaL_checknumber(L, 2));
    tr = (int32_t)floorf((float)luaL_checknumber(L, 3));
    if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
        return 0;
    lua_pushnumber(L, tile_order(f->c, tc, tr, f->mask_bit));
    return 1;
}

/*  A hairline between two points in the air. */
static int api_lane_wire(lua_State *L)
{
    LaneFan *f = lane_of(L);
    if (!f)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, put_wire_paint(f->m, f->c, f->mask_bit, (float)luaL_checknumber(L, 8),
                                      (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                                      (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5),
                                      (float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7),
                                      0.0f, (float)luaL_checknumber(L, 9)) == 0);
    return 1;
}

const luaL_Reg LANE[] = {
    {"info",   api_lane_info  },
    {"piece",  api_lane_piece },
    {"at",     api_lane_at    },
    {"height", api_lane_height},
    {"order",  api_lane_order },
    {"wire",   api_lane_wire  },
    {NULL,     NULL    }
};

/*  ---- THE TURN THE WORLD IS HANDED ------------------------------------
 *
 *  The handle `arc.rules.frame` holds, and the only one C ever hands
 *  over of itself.  A turn is a BUILD or a MOVE: a build hands out its
 *  passes one at a time, each a `world` of its own.  A move hands out
 *  the world that moves.  What a turn does with either is the script's. */
static int api_frame_info(lua_State *L)
{
    if (!rec_of(L, "frame"))
        return 0;
    lua_newtable(L);
    lua_pushboolean(L, net_drive_what() == DRIVE_BUILD), lua_setfield(L, -2, "build");
    lua_pushboolean(L, net_drive_what() == DRIVE_MOVE), lua_setfield(L, -2, "moving");
    return 1;
}

/*  The next pass of the build, set up and ready to compose, or nothing
 *  when it has none left. */
static int api_frame_pass(lua_State *L)
{
    void *w;
    if (!rec_of(L, "frame") || net_drive_what() != DRIVE_BUILD)
        return 0;
    if ((w = build_pass_next()) == NULL)
        return 0;
    api_object_push(L, "world", w);
    return 1;
}

/*  Once the passes are done, the pieces an edit's build may show leaving,
 *  then those it may show coming, by the phase named.  It answers a handle
 *  of the kind edit_show.  It answers nothing where the build was no edit
 *  being shown (render/show.h). */
static int api_frame_edit_show(lua_State *L)
{
    static int  token;
    const char *phase = luaL_checkstring(L, 2);
    int         which = strcmp(phase, "leaving") == 0 ? SHOW_LEAVING : strcmp(phase, "coming") == 0 ? SHOW_COMING : -1;
    if (!rec_of(L, "frame") || net_drive_what() != DRIVE_BUILD || which < 0 || show_offer(which, build_mesh()) < 0)
        return 0;
    api_object_push(L, "edit_show", &token);
    return 1;
}

/*  And the world that moves. */
static int api_frame_moving(lua_State *L)
{
    void *b;
    if (!rec_of(L, "frame") || net_drive_what() != DRIVE_MOVE)
        return 0;
    if ((b = net_moving_fan()) == NULL)
        return 0;
    api_object_push(L, "moving", b);
    return 1;
}

const luaL_Reg FRAME[] = {
    {"info",   api_frame_info  },
    {"pass",   api_frame_pass  },
    {"moving", api_frame_moving},
    {"edit_show", api_frame_edit_show},
    {NULL,     NULL            }
};

/*  ---- the network a family's cells make -------------------------------
 *
 *  What the script discovers the network off, and what it hands back.
 *  `plane` is the reading.  It is three arrays a cell, counted from
 *  nought.  They are the links a cell RETURNS (a link both sides agree
 *  on), the links its own art claims.  What kind of node it is, 0 none,
 *  1 an end, 2 a junction.  `segment` and `island` are the answer. */

static NetDiscFan *disc_of(lua_State *L)
{
    return (NetDiscFan *)rec_of(L, "network");
}

static int api_network_info(lua_State *L)
{
    NetDiscFan *d = disc_of(L);
    if (!d)
        return 0;
    lua_newtable(L);
    lua_pushstring(L, d->family), lua_setfield(L, -2, "family");
    lua_pushinteger(L, d->fk), lua_setfield(L, -2, "walked");
    lua_pushinteger(L, R_MAP), lua_setfield(L, -2, "size");
    /*  The two the store cannot go past: how many cells one run may hold
     *  and how many steps a walk may take.  A script that walks the map
     *  reads its own limits rather than carrying a copy of them. */
    lua_pushinteger(L, MAX_PTS), lua_setfield(L, -2, "max_cells");
    lua_pushinteger(L, 4096), lua_setfield(L, -2, "max_steps");
    return 1;
}

static void plane_push(lua_State *L, const uint8_t *p)
{
    int32_t i;
    lua_createtable(L, R_MAP * R_MAP, 0);
    for (i = 0; i < R_MAP * R_MAP; ++i)
    {
        lua_pushinteger(L, p[i]);
        lua_rawseti(L, -2, i);
    }
}

static int api_network_plane(lua_State *L)
{
    NetDiscFan *d = disc_of(L);
    if (!d)
        return 0;
    plane_push(L, d->links);
    plane_push(L, d->art);
    return 2;
}

/*  o:nodes(plane): what counts as a node, a cell each, counted from
 *  nought: 0 none, 1 an end, 2 a junction.  Where a segment ends is
 *  where the next node begins, so this is the same answer as the runs
 *  and is given with them.  The whole pipeline reads it after. */
static int api_network_nodes(lua_State *L)
{
    static uint8_t plane[R_MAP * R_MAP];
    NetDiscFan    *d = disc_of(L);
    int32_t        i;
    luaL_checktype(L, 2, LUA_TTABLE);
    if (!d)
        return 0;
    for (i = 0; i < R_MAP * R_MAP; ++i)
    {
        lua_rawgeti(L, 2, i);
        plane[i] = (uint8_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    net_disc_nodes_set(d->f, plane);
    return 0;
}

/*  Why a run ended, by the name the script calls it.  A run that reached
 *  a node.  One that left the map.  One that outgrew a guard, one whose
 *  next cell did not return the link.  One that came round to where it
 *  started are five different shapes downstream. */
static int stop_code(const char *s)
{
    return !s                        ? -1
           : strcmp(s, "node") == 0  ? NET_STOP_NODE
           : strcmp(s, "edge") == 0  ? NET_STOP_EDGE
           : strcmp(s, "cut") == 0   ? NET_STOP_CUT
           : strcmp(s, "stuck") == 0 ? NET_STOP_STUCK
           : strcmp(s, "loop") == 0  ? NET_STOP_LOOP
                                     : -1;
}

/*  o:segment(cells, n, stop, exit): one run of the network.  `cells`
 *  counts from NOUGHT and its length comes with it, as every table the
 *  script hands back does.  Each entry is row * size + col. */
static int api_network_segment(lua_State *L)
{
    static int32_t cells[MAX_PTS];
    NetDiscFan    *d    = disc_of(L);
    int            n    = (int)luaL_checkinteger(L, 3);
    int            stop = stop_code(luaL_checkstring(L, 4));
    int            exit = (int)luaL_optinteger(L, 5, -1);
    int            i;
    luaL_checktype(L, 2, LUA_TTABLE);
    if (!d || n < 1 || stop < 0)
        return 0;
    if (n > MAX_PTS)
        n = MAX_PTS;
    for (i = 0; i < n; ++i)
    {
        lua_rawgeti(L, 2, i);
        cells[i] = (int32_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    if (!net_disc_run_add(d->fk, cells, n, stop, exit))
        d->full = 1;
    return 0;
}

/*  o:island(cell, edge): a piece no run reaches.  `edge` marks the kind
 *  whose links all leave the map, which is drawn only where no run
 *  already covered the tile. */
static int api_network_island(lua_State *L)
{
    NetDiscFan *d = disc_of(L);
    if (!d)
        return 0;
    if (!net_disc_island_add(d->fk, (int32_t)luaL_checkinteger(L, 2), lua_toboolean(L, 3)))
        d->full = 1;
    return 0;
}

/*  o:junction(cell): a cell where the ways meet.  Its control, its
 *  outline, the trims it hands its arms and the box it draws all step
 *  through this list.  So a script that decides what a junction is
 *  decides all four. */
static int api_network_junction(lua_State *L)
{
    NetDiscFan *d = disc_of(L);
    if (!d)
        return 0;
    if (!net_disc_junction_add(d->fk, (int32_t)luaL_checkinteger(L, 2)))
        d->full = 1;
    return 0;
}

const luaL_Reg NETWORK[] = {
    {"info",    api_network_info   },
    {"plane",   api_network_plane  },
    {"nodes",   api_network_nodes  },
    {"segment", api_network_segment},
    {"island",  api_network_island },
    {"junction", api_network_junction},
    {NULL,      NULL               }
};

/*  ---- the beat the moving world runs on ---------------------------------
 *
 *  The build has a drive and so does the beat: this is what the script
 *  that drives one holds.  `gates` and `cars` say how much there is to
 *  do, `gate` and `car` what each of them sees.  `gate_is` and `car_is`
 *  are the answer: the angle a gate has swung to and the speed a car
 *  leaves at.
 *
 *  A car is read and moved before the next is read.  A car looks at the
 *  one ahead of it, which has already moved this beat. */
static int api_moving_info(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    lua_newtable(L);
    /*  How many beats the clock owes, whether the frame wants what
     *  moves drawn, and how many gates there are to swing. */
    lua_pushinteger(L, net_beat_owed()), lua_setfield(L, -2, "beats");
    lua_pushboolean(L, net_beat_draws()), lua_setfield(L, -2, "draw");
    lua_pushinteger(L, net_beat_gates()), lua_setfield(L, -2, "gates");
    return 1;
}

/*  One beat begun: the trains run on their own threads, and what follows,
 *  the gates and the cars, is the script's. */
static int api_moving_run(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    lua_pushboolean(L, net_beat_run());
    return 1;
}

/*  THE ARMS AT THE THREAD NODE the beat stopped at, for the rule that
 *  chooses between them.  It gives the heading the train arrived on, and
 *  each arm's heading away from the node.  Nothing where the beat did
 *  not stop. */
static int api_moving_arms(lua_State *L)
{
    int   n, k;
    float hx, hy, dx, dy;
    if (!rec_of(L, "moving") || !net_beat_arms(&n, &hx, &hy))
        return 0;
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, hx), lua_setfield(L, -2, "hx");
    lua_pushnumber(L, hy), lua_setfield(L, -2, "hy");
    lua_createtable(L, n, 0);
    for (k = 0; k < n; ++k)
    {
        if (!net_beat_arm_at(k, &dx, &dy))
            continue;
        lua_createtable(L, 0, 2);
        lua_pushnumber(L, dx), lua_setfield(L, -2, "dx");
        lua_pushnumber(L, dy), lua_setfield(L, -2, "dy");
        lua_rawseti(L, -2, k + 1);
    }
    lua_setfield(L, -2, "arms");
    return 1;
}

/*  THE THREAD SIGNALS: how many, how near the nearest car is each way
 *  along the block one governs, and the aspect the rule gives it. */
static int api_moving_signals(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    lua_pushinteger(L, net_signals());
    return 1;
}

static int api_moving_signal(lua_State *L)
{
    float ahead, back;
    if (!rec_of(L, "moving") || !net_signal_at((int)luaL_checkinteger(L, 2), &ahead, &back))
        return 0;
    lua_createtable(L, 0, 2);
    lua_pushnumber(L, ahead), lua_setfield(L, -2, "ahead");
    lua_pushnumber(L, back), lua_setfield(L, -2, "back");
    return 1;
}

/*  The aspect one signal shows: the MODEL the rule named, or nothing for
 *  a signal that shows none. */
static int api_moving_signal_is(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    net_signal_is((int)luaL_checkinteger(L, 2), lua_tostring(L, 3));
    return 0;
}

/*  And the arm the rule chose. */
static int api_moving_arm_is(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    net_beat_arm_is(lua_isnumber(L, 2) ? (int)lua_tointeger(L, 2) : -1);
    return 0;
}

/*  And the geometry of everything that moves, where the frame asked for
 *  it: the cars, the trains and the signals' aspects. */
static int api_moving_build(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    lua_pushboolean(L, net_beat_build() == 0);
    return 1;
}

/*  A level meet's gate, once a beat.  The rule is given where the arm is
 *  and how near the nearest train is, and answers where the arm goes
 *  next. */
static int api_moving_gate(lua_State *L)
{
    float angle, near, dt;
    if (!rec_of(L, "moving") || !net_beat_gate((int)luaL_checkinteger(L, 2), &angle, &near, &dt))
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, angle), lua_setfield(L, -2, "angle");
    lua_pushnumber(L, near), lua_setfield(L, -2, "near");
    lua_pushnumber(L, dt), lua_setfield(L, -2, "dt");
    return 1;
}

static int api_moving_gate_is(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    if (lua_isnumber(L, 3))
        net_beat_gate_is((int)luaL_checkinteger(L, 2), (float)lua_tonumber(L, 3));
    return 0;
}

/*  The cars, put in the order they are read in.  Asked once, after the
 *  gates have swung: sorting them is what the order depends on. */
static int api_moving_cars(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    lua_pushinteger(L, net_beat_cars());
    return 1;
}

/*  Car i.  It holds its own speed, and the gap to the car ahead of it in
 *  its lane.  It also holds the control at the end it runs towards, and
 *  every lap on its way.  Nothing at all for a car with no decision to
 *  make, one meet a junction box.  Off the network, which has already
 *  been moved. */
static int api_moving_car(lua_State *L)
{
    float speed, gap, ahead, stop, free_, line, creep, step;
    int   have_gap, have_ctrl, held, nx, k;
    if (!rec_of(L, "moving") || !net_beat_car((int)luaL_checkinteger(L, 2)))
        return 0;
    if (!net_beat_reading(&speed, &have_gap, &gap, &have_ctrl, &ahead, &held, &nx,
                          &stop, &free_, &line, &creep, &step))
        return 0;
    lua_createtable(L, 0, 10);
    lua_pushnumber(L, speed), lua_setfield(L, -2, "speed");
    lua_pushnumber(L, stop), lua_setfield(L, -2, "stop");
    lua_pushnumber(L, free_), lua_setfield(L, -2, "free");
    lua_pushnumber(L, line), lua_setfield(L, -2, "line");
    lua_pushnumber(L, creep), lua_setfield(L, -2, "creep");
    lua_pushnumber(L, step), lua_setfield(L, -2, "step");
    if (have_gap)
        lua_pushnumber(L, gap), lua_setfield(L, -2, "gap");
    if (have_ctrl)
    {
        int32_t sc, sr;
        int     sk;
        float   shx, shy, st;
        lua_pushnumber(L, ahead), lua_setfield(L, -2, "ahead");
        lua_pushboolean(L, held), lua_setfield(L, -2, "held");
        /*  Where the control is a SIGNAL, its facts rather than an
         *  answer: whether it reads red is arc.rules.signal's. */
        if (net_beat_signal(&sc, &sr, &shx, &shy, &st, &sk))
        {
            lua_createtable(L, 0, 5);
            lua_pushinteger(L, sc), lua_setfield(L, -2, "col");
            lua_pushinteger(L, sr), lua_setfield(L, -2, "row");
            lua_pushnumber(L, shx), lua_setfield(L, -2, "hx");
            lua_pushnumber(L, shy), lua_setfield(L, -2, "hy");
            lua_pushnumber(L, st), lua_setfield(L, -2, "t");
            lua_pushinteger(L, sk), lua_setfield(L, -2, "k");
            lua_setfield(L, -2, "signal");
        }
    }
    lua_createtable(L, nx, 0);
    for (k = 0; k < nx; ++k)
    {
        lua_pushnumber(L, net_beat_lap(k));
        lua_rawseti(L, -2, k + 1);
    }
    lua_setfield(L, -2, "meets");
    /*  And the arms at the junction it is a beat away from.  There there
     *  is one to choose: the draw the world made for it, and each arm's
     *  heading away from the node. */
    {
        int      n;
        unsigned draw;
        if (net_beat_turn(&n, &draw))
        {
            float dx, dy;
            lua_pushinteger(L, (lua_Integer)draw), lua_setfield(L, -2, "draw");
            lua_createtable(L, n, 0);
            for (k = 0; k < n; ++k)
            {
                if (!net_beat_turn_at(k, &dx, &dy))
                    continue;
                lua_createtable(L, 0, 2);
                lua_pushnumber(L, dx), lua_setfield(L, -2, "dx");
                lua_pushnumber(L, dy), lua_setfield(L, -2, "dy");
                lua_rawseti(L, -2, k + 1);
            }
            lua_setfield(L, -2, "arms");
        }
    }
    return 1;
}

/*  The arm the rule chose for the car in hand. */
static int api_moving_car_turn_is(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    net_beat_turn_is(lua_isnumber(L, 2) ? (int)lua_tointeger(L, 2) : -1);
    return 0;
}

static int api_moving_car_is(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    net_beat_car_is((int)luaL_checkinteger(L, 2), (float)luaL_checknumber(L, 3));
    return 0;
}

/*  ---- and the moving world DRAWN ---------------------------------------
 *
 *  A gate's moving parts, its flashers and the striped arm swung about
 *  the mechanism's shaft, are a rule.  So the drive lays them: it asks
 *  for each arm's place, the script draws through arc.put, and the
 *  window is closed again.  Nothing in the moving world reaches up. */
static int api_moving_gates_drawn(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    lua_pushinteger(L, net_movers_gates());
    return 1;
}

static int api_moving_gate_prop(lua_State *L)
{
    ScriptProp  at;
    const void *c;
    void       *m;
    uint8_t     mask_bit;
    float       x, y, fx, fy, angle, len, order;
    if (!rec_of(L, "moving") || !net_movers_gate((int)luaL_checkinteger(L, 2), &x, &y, &fx, &fy, &angle, &len, &order))
        return 0;
    if ((m = net_movers_mesh(&c, &mask_bit)) == NULL)
        return 0;
    memset(&at, 0, sizeof at);
    at.col = (int)floorf(x), at.row = (int)floorf(y), at.arm = -1, at.links = 0;
    at.x = x, at.y = y;
    at.z = surface_at_world((const RCity *)c, mask_bit, x, y);
    at.fx = -fx, at.fy = -fy;
    at.size  = 0.0f;
    at.phase = (float)(((int)(x * 3.0f) + (int)(y * 5.0f)) & 7) / 8.0f;
    at.angle = angle, at.len = len;
    script_emit_open(m, c, mask_bit, order);
    api_prop_push(L, &at);
    return 1;
}

static int api_moving_gate_drawn(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    script_emit_close();
    return 0;
}

const luaL_Reg MOVING[] = {
    {"gates_drawn", api_moving_gates_drawn},
    {"gate_prop",   api_moving_gate_prop  },
    {"gate_drawn",  api_moving_gate_drawn },
    {"info",    api_moving_info   },
    {"run",     api_moving_run    },
    {"build",   api_moving_build  },
    {"gate",    api_moving_gate   },
    {"gate_is", api_moving_gate_is},
    {"cars",    api_moving_cars   },
    {"car",     api_moving_car    },
    {"car_turn_is", api_moving_car_turn_is},
    {"arms",    api_moving_arms   },
    {"arm_is",  api_moving_arm_is },
    {"signals", api_moving_signals},
    {"signal",  api_moving_signal },
    {"signal_is", api_moving_signal_is},
    {"car_is",  api_moving_car_is },
    {NULL,      NULL            }
};

/*  ---- a lane carried across a meet ---------------------------------- */

static XLaneFan *cross_of(lua_State *L)
{
    return (XLaneFan *)rec_of(L, "cross");
}

static const Field CROSS_FIELDS[] = {
    {"n", FLD_INT,  offsetof(XLaneFan, n)},
    {NULL, FLD_NUM, 0}
};

static int api_cross_info(lua_State *L)
{
    return api_fields(L, cross_of(L), CROSS_FIELDS);
}

/*  Is lane i an open end that could carry on across a meet? */
static int api_cross_end(lua_State *L)
{
    XLaneFan *x = cross_of(L);
    if (!x)
        return 0;
    lua_pushboolean(L, xlane_end(x, (int)luaL_checkinteger(L, 2)));
    return 1;
}

/*  How lane lb's start lies from lane la's end.  Nothing for a lane that
 *  is not a candidate at all. */
static int api_cross_measure(lua_State *L)
{
    XLaneFan *x = cross_of(L);
    float     off, dot, ahead, aside, dist;
    if (!x || !xlane_measure(x, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                             &off, &dot, &ahead, &aside, &dist))
        return 0;
    lua_pushnumber(L, off);
    lua_pushnumber(L, dot);
    lua_pushnumber(L, ahead);
    lua_pushnumber(L, aside);
    lua_pushnumber(L, dist);
    return 5;
}

/*  The two made one lane, or a link drawn between them. */
static int api_cross_merge(lua_State *L)
{
    XLaneFan *x = cross_of(L);
    if (x)
        xlane_merge(x, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3));
    return 0;
}

static int api_cross_link(lua_State *L)
{
    XLaneFan *x = cross_of(L);
    if (!x)
        return 0;
    lua_pushboolean(L, xlane_link(x, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3)));
    return 1;
}

const luaL_Reg CROSS[] = {
    {"info",    api_cross_info   },
    {"open",    api_cross_end    },
    {"measure", api_cross_measure},
    {"merge",   api_cross_merge  },
    {"link",    api_cross_link   },
    {NULL,      NULL      }
};
