/*  obj_band.c: A RAISED SURFACE, and how it comes down to the ground.
 *
 *  The bands a walk found, the chain of fit points one makes, and its
 *  elevation.  Then the SPUR: where the surface comes down to grade.
 *  Its join slid along the line it aims at.  The lane it drops, and its
 *  four sides.  The lanes within reach of its end, and where it aims.
 *  And where a band's own lane ends go. */
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

/*  The lanes within reach of a spur's end, for the rule that picks one.
 *  The record is a token: the store behind it is the lane model's.  The
 *  world hands this handle out as well, which is why the push is here
 *  and not at either place that asks for it. */
static int s_spur_pick;

void obj_snap_push(lua_State *L)
{
    api_object_push(L, "snap", (void *)&s_spur_pick);
}

/*  ---- the band's bands ----------------------------------------------
 *
 *  The same question as a line segment's, asked of a different network.
 *  `plane` is the reading: which cell of a pair is its primary and which
 *  `band` is the answer.  One run of entries, each a cell of the band or
 *  a block it turns through. */

static BandDiscFan *bands_of(lua_State *L)
{
    return (BandDiscFan *)rec_of(L, "bands");
}

static int api_bands_info(lua_State *L)
{
    if (!bands_of(L))
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, R_MAP), lua_setfield(L, -2, "size");
    lua_pushinteger(L, MAX_PTS), lua_setfield(L, -2, "max_cells");
    return 1;
}

/*  o:band(entries, n, col, row, ew, sign): one band.  `entries` counts
 *  from nought and its length comes with it.  Each is {cell = , block =
 *  , ew = }.  The cell it was started from and the way it was walked
 *  make its key in the segment table across builds. */
static int api_bands_band(lua_State *L)
{
    static BandRun run;
    BandDiscFan   *d = bands_of(L);
    int          n = (int)luaL_checkinteger(L, 3), i;
    int32_t      cell;
    luaL_checktype(L, 2, LUA_TTABLE);
    if (!d || n < 1)
        return 0;
    if (n > MAX_PTS)
        n = MAX_PTS;
    run.n = n;
    for (i = 0; i < n; ++i)
    {
        run.cell[i] = 0, run.block[i] = 0, run.ew[i] = 0;
        lua_rawgeti(L, 2, i);
        if (lua_istable(L, -1))
        {
            run.cell[i] = (int32_t)api_field_num(L, "cell", 0.0f);
            lua_getfield(L, -1, "block");
            run.block[i] = (uint8_t)lua_toboolean(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, -1, "ew");
            run.ew[i] = (uint8_t)lua_toboolean(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    cell = (int32_t)luaL_checkinteger(L, 4);
    if (!net_band_disc_add(&run, cell % R_MAP, cell / R_MAP,
                         lua_toboolean(L, 5), (int)luaL_checkinteger(L, 6)))
        d->full = 1;
    return 0;
}

const luaL_Reg BANDS[] = {
    {"info", api_bands_info},
    {"band", api_bands_band},
    {NULL,    NULL           }
};

/*  ---- the lanes within reach of a spur's end ----------------------------
 *
 *  Which lane a spur fastens to.  At the top it is the nearest of its
 *  own band's slab lanes.  At the foot it is the lip-side lane of the
 *  line, or the turn it lands on inside a junction: is
 *  arc.rules.spur_lane's.  The candidates are measured for it: one per
 *  PIECE of every lane within reach.  This is because a lane's nearest
 *  station may run the wrong way where one further along runs the right
 *  way. */
static int api_snap_info(lua_State *L)
{
    const char *what;
    int         band;
    if (!rec_of(L, "snap"))
        return 0;
    net_spur_snap_what(&what, &band);
    lua_newtable(L);
    lua_pushinteger(L, lane_snap_count()), lua_setfield(L, -2, "n");
    lua_pushstring(L, what), lua_setfield(L, -2, "what");
    lua_pushinteger(L, band), lua_setfield(L, -2, "band");
    lua_pushnumber(L, net_line_rules()->spur_snap), lua_setfield(L, -2, "reach");
    lua_pushnumber(L, net_line_rules()->lane_pick_dot), lua_setfield(L, -2, "dot");
    return 1;
}

/*  One candidate, as plain values.  A spur end has thousands of them, so
 *  it makes no table.
 *
 *      The lane.
 *      Whether it is a slab's lane, a line's, or a turn's.
 *      The band it belongs to.
 *      How far off its centerline it lies.
 *      How far away that station is.
 *      How nearly it runs the way the spur does. */
static int api_snap_at(lua_State *L)
{
    int   lane, cls, band;
    float off, dist, dot, x, y, dx, dy;
    if (!rec_of(L, "snap") ||
        !lane_snap_at((int)luaL_checkinteger(L, 2), &lane, &cls, &band, &off, &dist, &dot, &x, &y, &dx, &dy))
        return 0;
    lua_pushinteger(L, lane);
    lua_pushboolean(L, cls == 2);
    lua_pushboolean(L, cls == 0);
    lua_pushboolean(L, cls == 1);
    lua_pushinteger(L, band);
    lua_pushnumber(L, off);
    lua_pushnumber(L, dist);
    lua_pushnumber(L, dot);
    return 8;
}

/*  And the pick, or nothing for an end that fastens to no lane. */
static int api_snap_is(lua_State *L)
{
    if (!rec_of(L, "snap"))
        return 0;
    lane_snap_is(lua_isnumber(L, 2) ? (int)lua_tointeger(L, 2) : -1);
    return 0;
}

const luaL_Reg SNAP[] = {
    {"info", api_snap_info},
    {"at",   api_snap_at  },
    {"is",   api_snap_is  },
    {NULL,   NULL         }
};

/*  ---- where a spur aims on the line ---------------------------------------
 *
 *  The fork its meet was classified as.  This way the spur runs, and
 *  where its foot lies.  The line's own rules give two numbers: the
 *  lane's offset from the centerline, and how far along the merge sits.
 *  The rule answers the point the join is drawn to, the tangent it is
 *  drawn with, and the way the line's lane travels there. */
static int api_target_info(lua_State *L)
{
    int   fork, off;
    float rdx, rdy, mdx, mdy, fx, fy, lane_off, merge_along;
    if (!rec_of(L, "target") ||
        !net_spur_target_at(&fork, &off, &rdx, &rdy, &mdx, &mdy, &fx, &fy, &lane_off, &merge_along))
        return 0;
    lua_createtable(L, 0, 10);
    lua_pushinteger(L, fork), lua_setfield(L, -2, "fork");
    lua_pushboolean(L, off), lua_setfield(L, -2, "off");
    lua_pushnumber(L, rdx), lua_setfield(L, -2, "rdx");
    lua_pushnumber(L, rdy), lua_setfield(L, -2, "rdy");
    lua_pushnumber(L, mdx), lua_setfield(L, -2, "mdx");
    lua_pushnumber(L, mdy), lua_setfield(L, -2, "mdy");
    lua_pushnumber(L, fx), lua_setfield(L, -2, "x");
    lua_pushnumber(L, fy), lua_setfield(L, -2, "y");
    lua_pushnumber(L, lane_off), lua_setfield(L, -2, "lane_off");
    lua_pushnumber(L, merge_along), lua_setfield(L, -2, "merge_along");
    return 1;
}

static int api_target_is(lua_State *L)
{
    if (!rec_of(L, "target"))
        return 0;
    net_spur_target_is((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                       (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5),
                       (float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7));
    return 0;
}

const luaL_Reg TARGET[] = {
    {"info", api_target_info},
    {"is",   api_target_is  },
    {NULL,   NULL           }
};

/*  ---- the band's lane ends -------------------------------------------
 *
 *  What arc.rules.links holds.  Which slab lane goes on to which.  It is
 *  the lane of another band round an interchange, the line's lane where
 *  the slab comes down.  The inner lane of its own band where it tapers
 *  out: is that rule's.  This offers the lanes and lays what it is told
 *  to lay.
 *
 *  The numbers the decisions are measured against come through `info`,
 *  as the floats the lane model holds them in.  Read again from arc.geo
 *  as doubles they answer a hair differently at a boundary.  A link that
 *  should be laid would not be. */
static int api_links_info(lua_State *L)
{
    const ScriptFamily *fr;
    if (!rec_of(L, "links"))
        return 0;
    fr = net_line_rules();
    lua_newtable(L);
    lua_pushinteger(L, net_links_count()), lua_setfield(L, -2, "n");
    lua_pushnumber(L, fr->lane_reach), lua_setfield(L, -2, "reach");
    lua_pushnumber(L, fr->band_abreast), lua_setfield(L, -2, "abreast");
    lua_pushnumber(L, fr->band_outer), lua_setfield(L, -2, "outer");
    lua_pushnumber(L, fr->band_taper_far), lua_setfield(L, -2, "taper_far");
    lua_pushnumber(L, fr->band_taper_near), lua_setfield(L, -2, "taper_near");
    lua_pushnumber(L, fr->band_taper_room), lua_setfield(L, -2, "taper_room");
    lua_pushnumber(L, fr->band_taper_gap), lua_setfield(L, -2, "taper_gap");
    lua_pushnumber(L, fr->band_line_dot), lua_setfield(L, -2, "line_dot");
    lua_pushnumber(L, fr->band_reach), lua_setfield(L, -2, "band_reach");
    lua_pushnumber(L, fr->band_dot), lua_setfield(L, -2, "band_dot");
    lua_pushnumber(L, fr->band_ahead), lua_setfield(L, -2, "band_ahead");
    lua_pushnumber(L, fr->band_aside), lua_setfield(L, -2, "band_aside");
    lua_pushnumber(L, fr->band_apart), lua_setfield(L, -2, "band_apart");
    return 1;
}

/*  One lane as it stands: what it is.  This band it belongs to, how far
 *  off that band's centerline it lies, and whether each end is open. */
static int api_links_lane(lua_State *L)
{
    int   slab, line, band, open0, open1;
    float off, w, len;
    if (!rec_of(L, "links") ||
        !net_links_lane((int)luaL_checkinteger(L, 2), &slab, &line, &band, &off, &w, &len, &open0, &open1))
        return 0;
    lua_createtable(L, 0, 8);
    lua_pushboolean(L, slab), lua_setfield(L, -2, "slab");
    lua_pushboolean(L, line), lua_setfield(L, -2, "line");
    lua_pushinteger(L, band), lua_setfield(L, -2, "band");
    lua_pushnumber(L, off), lua_setfield(L, -2, "off");
    lua_pushnumber(L, w), lua_setfield(L, -2, "w");
    lua_pushnumber(L, len), lua_setfield(L, -2, "len");
    lua_pushboolean(L, open0), lua_setfield(L, -2, "open0");
    lua_pushboolean(L, open1), lua_setfield(L, -2, "open1");
    return 1;
}

/*  Where one end of a lane is, and which way it runs.  `which` 1 is the
 *  end its travel leaves by, and 0 the end it arrives at. */
static int api_links_pose(lua_State *L)
{
    float x, y, dx, dy;
    if (!rec_of(L, "links") ||
        !net_links_pose((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3), &x, &y, &dx, &dy))
        return 0;
    lua_pushnumber(L, x), lua_pushnumber(L, y), lua_pushnumber(L, dx), lua_pushnumber(L, dy);
    return 4;
}

/*  And the station a given distance back from that end, where a lane
 *  tapering into its neighbor leaves. */
static int api_links_station(lua_State *L)
{
    float x, y, dx, dy;
    if (!rec_of(L, "links") ||
        !net_links_station((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                           (float)luaL_checknumber(L, 4), &x, &y, &dx, &dy))
        return 0;
    lua_pushnumber(L, x), lua_pushnumber(L, y), lua_pushnumber(L, dx), lua_pushnumber(L, dy);
    return 4;
}

/*  The link laid, from the pieces the script cut: which lane it leaves,
 *  which it arrives at, how wide, and the band it belongs to. */
static int api_links_link(lua_State *L)
{
    static Piece pc[MAX_PIECES];
    int          np, k;
    luaL_checktype(L, 2, LUA_TTABLE);
    if (!rec_of(L, "links"))
        return 0;
    np = (int)lua_rawlen(L, 2);
    if (np > MAX_PIECES)
        np = MAX_PIECES;
    for (k = 0; k < np; ++k)
    {
        lua_rawgeti(L, 2, k + 1);
        memset(&pc[k], 0, sizeof pc[k]);
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, "arc");
            pc[k].arc = lua_toboolean(L, -1);
            lua_pop(L, 1);
            pc[k].a.x = api_field_num(L, "ax", 0.0f);
            pc[k].a.y = api_field_num(L, "ay", 0.0f);
            pc[k].b.x = api_field_num(L, "bx", 0.0f);
            pc[k].b.y = api_field_num(L, "by", 0.0f);
            pc[k].c.x = api_field_num(L, "cx", 0.0f);
            pc[k].c.y = api_field_num(L, "cy", 0.0f);
            pc[k].r   = api_field_num(L, "r", 0.0f);
            pc[k].t0  = api_field_num(L, "t0", 0.0f);
            pc[k].t1  = api_field_num(L, "t1", 0.0f);
            pc[k].len = api_field_num(L, "len", 0.0f);
        }
        lua_pop(L, 1);
    }
    lua_pushboolean(L, net_links_add(pc, np, (float)luaL_checknumber(L, 3),
                                     (int)luaL_checkinteger(L, 4), (int)luaL_checkinteger(L, 5),
                                     (int)luaL_optinteger(L, 6, 0)) == 0);
    return 1;
}

/*  What the script counted while it joined them, for the report. */
static int api_links_note(lua_State *L)
{
    if (!rec_of(L, "links"))
        return 0;
    net_links_note(lua_tostring(L, 2), (int)luaL_optinteger(L, 3, 1));
    return 0;
}

const luaL_Reg LINKS[] = {
    {"info",    api_links_info   },
    {"lane",    api_links_lane   },
    {"pose",    api_links_pose   },
    {"station", api_links_station},
    {"link",    api_links_link   },
    {"note",    api_links_note   },
    {NULL,      NULL             }
};

/*  ---- a band's chain of fit points ------------------------------ */

static StairFan *stair_of(lua_State *L)
{
    return (StairFan *)rec_of(L, "stair");
}

/*  How many cells the band has, and how many straight ones a stair may
 *  step over between two of its blocks. */
static const Field STAIR_FIELDS[] = {
    {"n",   FLD_INT,  offsetof(StairFan, n)},
    {"gap", FLD_INT,  offsetof(StairFan, gap)},
    {NULL, FLD_NUM, 0}
};

static int api_stair_info(lua_State *L)
{
    return api_fields(L, stair_of(L), STAIR_FIELDS);
}

/*  Cell i: whether it is a curve block, which way the chain turns there,
 *  and whether an on-spur pins it. */
static int api_stair_block(lua_State *L)
{
    StairFan *s = stair_of(L);
    int       i = (int)luaL_checkinteger(L, 2);
    if (!s || i < 0 || i >= s->n)
        return 0;
    lua_pushboolean(L, s->block[i]);
    return 1;
}

static int api_stair_turn(lua_State *L)
{
    StairFan *s = stair_of(L);
    int       i = (int)luaL_checkinteger(L, 2);
    if (!s || i < 0 || i >= s->n)
        return 0;
    lua_pushinteger(L, band_stair_turn(s, i));
    return 1;
}

static int api_stair_pinned(lua_State *L)
{
    StairFan *s = stair_of(L);
    int       i = (int)luaL_checkinteger(L, 2);
    if (!s || i < 0 || i >= s->n)
        return 0;
    lua_pushboolean(L, band_stair_pinned(s, i));
    return 1;
}

/*  A cell as a point of the chain, and a run of cells as the one point
 *  at their center. */
static int api_stair_point(lua_State *L)
{
    StairFan *s = stair_of(L);
    int       i = (int)luaL_checkinteger(L, 2);
    if (s && i >= 0 && i < s->n)
        band_stair_point(s, i);
    return 0;
}

static int api_stair_centre(lua_State *L)
{
    StairFan *s = stair_of(L);
    int       i = (int)luaL_checkinteger(L, 2), j = (int)luaL_checkinteger(L, 3);
    if (s && i >= 0 && j >= i && j < s->n)
        band_stair_centre(s, i, j);
    return 0;
}

const luaL_Reg STAIR[] = {
    {"info",   api_stair_info  },
    {"block",  api_stair_block },
    {"turn",   api_stair_turn  },
    {"pinned", api_stair_pinned},
    {"point",  api_stair_point },
    {"centre", api_stair_centre},
    {NULL,     NULL     }
};

/*  ---- a band strip's elevation --------------------------------------- */

static ProfFan *prof_of(lua_State *L)
{
    return (ProfFan *)rec_of(L, "profile");
}

/*  What sort of strip it is and the numbers it is shaped by. */
static const Field PROFILE_FIELDS[] = {
    {"n",          FLD_INT,  offsetof(ProfFan, n)},
    {"total",      FLD_NUM,  offsetof(ProfFan, total)},
    {"spur",       FLD_BOOL, offsetof(ProfFan, spur)},
    {"lane_piece", FLD_BOOL, offsetof(ProfFan, lane_piece)},
    {"lane_off",   FLD_BOOL, offsetof(ProfFan, lane_off)},
    {"flat",       FLD_BOOL, offsetof(ProfFan, flat)},
    {"slab_above", FLD_NUM,  offsetof(ProfFan, z0)},
    {"taper0",     FLD_NUM,  offsetof(ProfFan, spur0)},
    {"taper1",     FLD_NUM,  offsetof(ProfFan, spur1)},
    {"grade",      FLD_NUM,  offsetof(ProfFan, grade)},
    {"stiff",      FLD_NUM,  offsetof(ProfFan, stiff)},
    {"lift",       FLD_NUM,  offsetof(ProfFan, lift)},
    {NULL, FLD_NUM, 0}
};

static int api_profile_info(lua_State *L)
{
    return api_fields(L, prof_of(L), PROFILE_FIELDS);
}

/*  Station i: how far along it is, and the ground under it. */
static int api_profile_at(lua_State *L)
{
    ProfFan *p = prof_of(L);
    int      i = (int)luaL_checkinteger(L, 2);
    float    s_at, z, ground;
    if (!p || i < 0 || i >= p->n)
        return 0;
    band_prof_at(p, i, &s_at, &z, &ground);
    lua_pushnumber(L, s_at);
    lua_pushnumber(L, ground);
    return 2;
}

/*  Where station i is and which way it runs.  A rule reads the map
 *  beyond the strip's end with it: what a slab's end meets is not in
 *  the strip. */
static int api_profile_pose(lua_State *L)
{
    ProfFan *p = prof_of(L);
    int      i = (int)luaL_checkinteger(L, 2);
    float    x, y, dx, dy;
    if (!p || i < 0 || i >= p->n)
        return 0;
    band_prof_pose(p, i, &x, &y, &dx, &dy);
    lua_pushnumber(L, x), lua_pushnumber(L, y), lua_pushnumber(L, dx), lua_pushnumber(L, dy);
    return 4;
}

/*  The height the station is given. */
static int api_profile_set(lua_State *L)
{
    ProfFan *p = prof_of(L);
    int      i = (int)luaL_checkinteger(L, 2);
    if (p && i >= 0 && i < p->n)
        band_prof_set(p, i, (float)luaL_checknumber(L, 3));
    return 0;
}

/*  The curve a lane drop's descent follows. */
static int api_profile_ease(lua_State *L)
{
    if (!prof_of(L))
        return 0;
    lua_pushnumber(L, ease_smooth((float)luaL_checknumber(L, 2)));
    return 1;
}

const luaL_Reg PROFILE[] = {
    {"info", api_profile_info},
    {"at",   api_profile_at  },
    {"pose", api_profile_pose},
    {"set",  api_profile_set },
    {"ease", api_profile_ease},
    {NULL,   NULL   }
};

/*  ---- a spur's join, slid ----------------------------------------------- */

static SlideFan *slide_of(lua_State *L)
{
    return (SlideFan *)rec_of(L, "slide");
}

/*  How far along the slab the descent may start before it reaches the
 *  lane line.  It is nothing where the two never meet.  It also gives
 *  how far along the line the join may slide. */
static int api_slide_info(lua_State *L)
{
    SlideFan *s = slide_of(L);
    if (!s)
        return 0;
    lua_newtable(L);
    if (!s->parallel)
        lua_pushnumber(L, s->reach), lua_setfield(L, -2, "reach");
    lua_pushnumber(L, s->merge), lua_setfield(L, -2, "merge");
    lua_pushnumber(L, s->taper), lua_setfield(L, -2, "taper");
    return 1;
}

/*  One placing, as the two POSES its chain runs between: on the slab,
 *  and on the lane picked.  The chain between them is the script's to
 *  build and cut.  Nothing and the reason where there is no placing:
 *  "off" the lane it aimed at. */
static int api_slide_poses(lua_State *L)
{
    SlideFan   *s = slide_of(L);
    V2          A, Q, tA, pos, tb;
    const char *why;
    if (!s)
        return 0;
    why = band_slide_poses(s, (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), &A, &Q, &tA, &pos, &tb);
    if (why)
    {
        lua_pushnil(L);
        lua_pushstring(L, why);
        return 2;
    }
    lua_pushnumber(L, Q.x), lua_pushnumber(L, Q.y), lua_pushnumber(L, tA.x), lua_pushnumber(L, tA.y);
    lua_pushnumber(L, pos.x), lua_pushnumber(L, pos.y), lua_pushnumber(L, tb.x), lua_pushnumber(L, tb.y);
    /*  And where the descent LEAVES the slab.  The placing starts `u`
     *  further along, so the lead between the two is the rule's to put
     *  at the head of its chain. */
    lua_pushnumber(L, A.x), lua_pushnumber(L, A.y);
    return 10;
}

/*  The lanes within reach of the join at this placing, for the rule that
 *  picks one.
 *
 *      The same handle.
 *      The same rule.
 *      The spur's foot is given.
 *
 *  Nothing where no lane is in reach, which ends the slide. */
static int api_slide_snap(lua_State *L)
{
    SlideFan *s = slide_of(L);
    if (!s || band_slide_snap(s, (float)luaL_checknumber(L, 2)) < 1)
        return 0;
    obj_snap_push(L);
    return 1;
}

/*  And the placing built from the pieces the script cut: the tightest
 *  arc's radius, or nothing for a cut that made none. */
static int api_slide_routed(lua_State *L)
{
    static Piece pc[MAX_PIECES];
    SlideFan    *s = slide_of(L);
    int          np, k;
    float        r;
    luaL_checktype(L, 2, LUA_TTABLE);
    if (!s)
        return 0;
    np = (int)lua_rawlen(L, 2);
    if (np > MAX_PIECES)
        np = MAX_PIECES;
    for (k = 0; k < np; ++k)
    {
        lua_rawgeti(L, 2, k + 1);
        memset(&pc[k], 0, sizeof pc[k]);
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, "arc");
            pc[k].arc = lua_toboolean(L, -1);
            lua_pop(L, 1);
            pc[k].a.x = api_field_num(L, "ax", 0.0f);
            pc[k].a.y = api_field_num(L, "ay", 0.0f);
            pc[k].b.x = api_field_num(L, "bx", 0.0f);
            pc[k].b.y = api_field_num(L, "by", 0.0f);
            pc[k].c.x = api_field_num(L, "cx", 0.0f);
            pc[k].c.y = api_field_num(L, "cy", 0.0f);
            pc[k].r   = api_field_num(L, "r", 0.0f);
            pc[k].t0  = api_field_num(L, "t0", 0.0f);
            pc[k].t1  = api_field_num(L, "t1", 0.0f);
            pc[k].len = api_field_num(L, "len", 0.0f);
        }
        lua_pop(L, 1);
    }
    r = band_slide_routed(s, pc, np);
    if (!(r > 0.0f))
        return 0;
    lua_pushnumber(L, r);
    return 1;
}

/*  Does the placing leave by the spur tile's own line edge? */
static int api_slide_exits(lua_State *L)
{
    SlideFan *s = slide_of(L);
    if (!s)
        return 0;
    lua_pushboolean(L, band_slide_exits(s));
    return 1;
}

/*  The placing kept, with the slack its taper is given past the edge. */
static int api_slide_keep(lua_State *L)
{
    SlideFan *s = slide_of(L);
    if (s)
        band_slide_keep(s, (float)luaL_checknumber(L, 2));
    return 0;
}

/*  Why no placing was found, for --lane-dump. */
static int api_slide_note(lua_State *L)
{
    SlideFan *s = slide_of(L);
    if (s)
        band_slide_note(s, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                         (int)luaL_checkinteger(L, 4), (int)luaL_checkinteger(L, 5));
    return 0;
}

const luaL_Reg SLIDE[] = {
    {"info",  api_slide_info },
    {"poses",  api_slide_poses },
    {"snap",   api_slide_snap  },
    {"routed", api_slide_routed},
    {"exits", api_slide_exits},
    {"keep",  api_slide_keep },
    {"note",  api_slide_note },
    {NULL,    NULL    }
};

/*  ---- the lane a spur drops from a slab --------------------------------- */

static DropFan *drop_of(lua_State *L)
{
    return (DropFan *)rec_of(L, "drop");
}

static const Field DROP_FIELDS[] = {
    {"n",      FLD_INT,  offsetof(DropFan, n)},
    {"spurs",  FLD_INT,  offsetof(DropFan, nspurs)},
    {"reach",  FLD_NUM,  offsetof(DropFan, reach)},
    {"narrow", FLD_NUM,  offsetof(DropFan, narrow)},
    {NULL, FLD_NUM, 0}
};

static int api_drop_info(lua_State *L)
{
    return api_fields(L, drop_of(L), DROP_FIELDS);
}

/*  Station i: how far along the slab it is, where it stands, which way
 *  it heads. */
static int api_drop_station(lua_State *L)
{
    DropFan *d = drop_of(L);
    float    at;
    V2       pos, dir;
    if (!d || !band_drop_station(d, (int)luaL_checkinteger(L, 2), &at, &pos, &dir))
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, at), lua_setfield(L, -2, "at");
    lua_pushnumber(L, pos.x), lua_setfield(L, -2, "x");
    lua_pushnumber(L, pos.y), lua_setfield(L, -2, "y");
    lua_pushnumber(L, dir.x), lua_setfield(L, -2, "dx");
    lua_pushnumber(L, dir.y), lua_setfield(L, -2, "dy");
    return 1;
}

/*  Spur r: the point on the centerline it drops from, its own tile's
 *  center, the way it runs, its taper's length in tiles.  Whether it
 *  leaves the slab or joins it. */
static int api_drop_spur(lua_State *L)
{
    DropFan *d = drop_of(L);
    V2       c0, tile, along;
    int      len, off;
    if (!d || !band_drop_spur(d, (int)luaL_checkinteger(L, 2), &c0, &tile, &along, &len, &off))
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, c0.x), lua_setfield(L, -2, "x");
    lua_pushnumber(L, c0.y), lua_setfield(L, -2, "y");
    lua_pushnumber(L, tile.x), lua_setfield(L, -2, "tx");
    lua_pushnumber(L, tile.y), lua_setfield(L, -2, "ty");
    lua_pushnumber(L, along.x), lua_setfield(L, -2, "ax");
    lua_pushnumber(L, along.y), lua_setfield(L, -2, "ay");
    lua_pushinteger(L, len), lua_setfield(L, -2, "len");
    lua_pushboolean(L, off), lua_setfield(L, -2, "leaves");
    return 1;
}

/*  Every station its full width.  The width one is left with on a side.
 *  And the gore, where the spur's own sliver sits beside the slab. */
static int api_drop_clear(lua_State *L)
{
    DropFan *d = drop_of(L);
    if (d)
        band_drop_clear(d);
    return 0;
}

static int api_drop_width(lua_State *L)
{
    DropFan *d = drop_of(L);
    if (d)
        band_drop_width(d, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                         (float)luaL_checknumber(L, 4));
    return 0;
}

static int api_drop_gore(lua_State *L)
{
    DropFan *d = drop_of(L);
    if (d)
        band_drop_gore(d, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3));
    return 0;
}

/*  Lane k on a side runs from s0 to s1 along the slab and no further:
 *  the lane model lays that much of it. */
static int api_drop_cut(lua_State *L)
{
    DropFan *d = drop_of(L);
    if (d)
        band_drop_cut(d, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                      (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5));
    return 0;
}

const luaL_Reg DROP[] = {
    {"info",    api_drop_info   },
    {"station", api_drop_station},
    {"spur",    api_drop_spur   },
    {"clear",   api_drop_clear  },
    {"width",   api_drop_width  },
    {"gore",    api_drop_gore   },
    {"cut",     api_drop_cut    },
    {NULL,      NULL      }
};

/*  ---- an on-spur's four sides ------------------------------------------- */

/*  The sides the script settled on: which is the slab's, which the
 *  line's, which a slab met end-on, and how many lines there were. */
static int api_spurs_answer(lua_State *L)
{
    OrientFan *o = rec_of(L, "spurs") ? net_spur_current() : NULL;
    if (!o)
        return 0;
    band_orient_answer(o, (int)luaL_checkinteger(L, 2),
                        (int)luaL_optinteger(L, 3, -1), (int)luaL_optinteger(L, 4, -1),
                        (int)luaL_optinteger(L, 5, -1), 0, (int)luaL_optinteger(L, 6, 0));
    return 0;
}

/*  Which pass is running, and.  Once the rule has named a tile.  Where
 *  that tile is.  The pass is the world's, so it answers before any tile
 *  is named. */
static int api_spurs_info(lua_State *L)
{
    OrientFan *o;
    if (!rec_of(L, "spurs"))
        return 0;
    o = net_spur_current();
    lua_newtable(L);
    lua_pushinteger(L, s_pass), lua_setfield(L, -2, "pass");
    if (o)
    {
        lua_pushinteger(L, o->col), lua_setfield(L, -2, "col");
        lua_pushinteger(L, o->row), lua_setfield(L, -2, "row");
    }
    return 1;
}

/*  And the SPUR the rule made of the tile.  It gives the way it lies
 *  along the slab, and the way the slab is.  It also gives which side
 *  its taper falls on, and how many tiles it reaches.  It gives the FOOT
 *  the descent stops at, and the way it heads there.  A tile the rule
 *  answers none for carries none. */
static int api_spurs_spur(lua_State *L)
{
    OrientFan *o = rec_of(L, "spurs") ? net_spur_current() : NULL, r;
    if (!o || !lua_istable(L, 2))
        return 0;
    memset(&r, 0, sizeof r);
    lua_pushvalue(L, 2);
    r.ax  = api_field_num(L, "ax", 0.0f);
    r.ay  = api_field_num(L, "ay", 0.0f);
    r.tx  = api_field_num(L, "tx", 0.0f);
    r.ty  = api_field_num(L, "ty", 0.0f);
    r.len = (int)api_field_num(L, "len", 0.0f);
    r.fork = (int)api_field_num(L, "fork", 0.0f);
    r.arm  = (int)api_field_num(L, "arm", 0.0f);
    r.rdx = api_field_num(L, "rdx", 0.0f);
    r.rdy = api_field_num(L, "rdy", 0.0f);
    r.mdx = api_field_num(L, "mdx", 0.0f);
    r.mdy = api_field_num(L, "mdy", 0.0f);
    r.fx  = api_field_num(L, "fx", 0.0f);
    r.fy  = api_field_num(L, "fy", 0.0f);
    r.fdx = api_field_num(L, "fdx", 0.0f);
    r.fdy = api_field_num(L, "fdy", 0.0f);
    lua_getfield(L, -1, "off"), r.r_off = lua_toboolean(L, -1), lua_pop(L, 1);
    lua_getfield(L, -1, "tile_off"), r.off = lua_toboolean(L, -1), lua_pop(L, 1);
    lua_getfield(L, -1, "opp"), r.opp = lua_toboolean(L, -1), lua_pop(L, 1);
    lua_pop(L, 1);
    band_orient_spur(o, &r);
    return 0;
}

/*  The links a tile's own piece claims: which of its four edges carry
 *  the line out to the edge, as a mask.  Not in the save.  The art says
 *  it.  So a rule reading the map for itself asks here. */
static int api_spurs_links(lua_State *L)
{
    OrientFan *o = rec_of(L, "spurs") ? net_spur_current() : NULL;
    if (!o)
        return 0;
    lua_pushinteger(L, band_orient_links(o, (int32_t)luaL_checkinteger(L, 2),
                                          (int32_t)luaL_checkinteger(L, 3)));
    return 1;
}

/*  One tile the rule found: named here, and the answers that follow are
 *  about it. */
static int api_spurs_at(lua_State *L)
{
    if (!rec_of(L, "spurs"))
        return 0;
    lua_pushboolean(L, net_spur_at((int32_t)luaL_checkinteger(L, 2), (int32_t)luaL_checkinteger(L, 3)));
    return 1;
}

const luaL_Reg ORIENT[] = {
    {"at",     api_spurs_at    },
    {"info",   api_spurs_info  },
    {"links",  api_spurs_links },
    {"answer", api_spurs_answer},
    {"spur",   api_spurs_spur  },
    {NULL,     NULL     }
};
