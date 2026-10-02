/*  obj_ground.c: THE GROUND, and what stands on it.
 *
 *  Where every tile's top comes from, a strip's elevation over the
 *  ground it crosses, and the shelf's copies of a corner. */
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

/*  The city the tops are being settled over.  The world says which
 *  before it hands the terrain handle to a rule, and the rule reads the
 *  map's rotation off it. */
static const RCity *s_terrain_city;

void obj_terrain_city_is(const RCity *c)
{
    s_terrain_city = c;
}

static int api_terrain_info(lua_State *L)
{
    if (!rec_of(L, "terrain"))
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, R_MAP), lua_setfield(L, -2, "size");
    lua_pushinteger(L, s_pass), lua_setfield(L, -2, "pass");
    lua_pushinteger(L, s_terrain_city ? city_corner_mask(s_terrain_city->rotation) : 0),
        lua_setfield(L, -2, "corner");
    return 1;
}

/*  What the CORRIDORS left on each tile.  This is the pipeline's own
 *  state and no reading of the city.  1 says the corridor wrote the
 *  tile's own shelf.  2 says it covers all four corners.  0 says
 *  neither.  Nothing before the building pass has any, so nothing is
 *  offered then. */
static int api_terrain_graded(lua_State *L)
{
    int32_t i;
    if (!rec_of(L, "terrain") || s_pass != 2)
        return 0;
    lua_createtable(L, R_MAP * R_MAP, 0);
    for (i = 0; i < R_MAP * R_MAP; ++i)
    {
        lua_pushinteger(L, mesh_top_graded(i));
        lua_rawseti(L, -2, i);
    }
    return 1;
}

/*  o:tops(place, anchor, order, vote, rest): five planes, each counted
 *  from nought.  A tile the place plane leaves out falls to the field.
 *  A tile the next two leave out stands on its own pad and takes its own
 *  place.  A tile the fourth leaves out votes for its corners with its
 *  own plane.  And a tile the last leaves out stands at rest where the
 *  pass composed it. */
static int api_terrain_tops(lua_State *L)
{
    int32_t i;
    if (!rec_of(L, "terrain") || !lua_istable(L, 2))
        return 0;
    for (i = 0; i < R_MAP * R_MAP; ++i)
    {
        int     top, vote = 0, rest;
        int32_t anchor = i, order = i;
        lua_rawgeti(L, 2, i);
        top = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        rest = top;
        if (lua_istable(L, 6))
        {
            lua_rawgeti(L, 6, i);
            if (lua_isnumber(L, -1))
                rest = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
        if (lua_istable(L, 5))
        {
            lua_rawgeti(L, 5, i);
            vote = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
        if (lua_istable(L, 3))
        {
            lua_rawgeti(L, 3, i);
            if (lua_isnumber(L, -1))
                anchor = (int32_t)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
        if (lua_istable(L, 4))
        {
            lua_rawgeti(L, 4, i);
            if (lua_isnumber(L, -1))
                order = (int32_t)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
        mesh_top_is(i, top, anchor, order, vote, rest);
    }
    lua_pushboolean(L, 1);
    return 1;
}

const luaL_Reg TERRAIN[] = {
    {"info",   api_terrain_info  },
    {"graded", api_terrain_graded},
    {"tops",   api_terrain_tops  },
    {NULL,     NULL              }
};

/*  ---- a strip's elevation over the ground ------------------------------- */

static GroundFan *ground_of(lua_State *L)
{
    return (GroundFan *)rec_of(L, "ground");
}

/*  How many stations, how long the strip is, and whether each end
 *  reaches a node it must be pinned to. */
static const Field GROUND_FIELDS[] = {
    {"n",            FLD_INT,  offsetof(GroundFan, n)},
    {"total",        FLD_NUM,  offsetof(GroundFan, total)},
    {"pin0",         FLD_BOOL, offsetof(GroundFan, pin0)},
    {"pin1",         FLD_BOOL, offsetof(GroundFan, pin1)},
    {"dead0",        FLD_BOOL, offsetof(GroundFan, dead0)},
    {"dead1",        FLD_BOOL, offsetof(GroundFan, dead1)},
    {"reaches_node", FLD_BOOL, offsetof(GroundFan, pin_node)},
    {NULL, FLD_NUM, 0}
};

static int api_ground_info(lua_State *L)
{
    return api_fields(L, ground_of(L), GROUND_FIELDS);
}

/*  Station i: how far along the strip it is, and the height the ground
 *  has given it so far. */
static int api_ground_at(lua_State *L)
{
    GroundFan *g = ground_of(L);
    float      at, z;
    if (!g || !loft_ground_at(g, (int)luaL_checkinteger(L, 2), &at, &z))
        return 0;
    lua_pushnumber(L, at);
    lua_pushnumber(L, z);
    return 2;
}

/*  The altitude an end's node stands at, and the altitude a level
 *  meet under station i pins the strip to. */
static int api_ground_node(lua_State *L)
{
    GroundFan *g = ground_of(L);
    float      z;
    int        which = strcmp(luaL_checkstring(L, 2), "goal") == 0;
    if (!g || !loft_ground_node(g, which, &z))
        return 0;
    lua_pushnumber(L, z);
    return 1;
}

static int api_ground_lap(lua_State *L)
{
    GroundFan *g = ground_of(L);
    float      z;
    if (!g || !loft_ground_lap(g, (int)luaL_checkinteger(L, 2), &z))
        return 0;
    lua_pushnumber(L, z);
    return 1;
}

/*  The height a station is given. */
static int api_ground_set(lua_State *L)
{
    GroundFan *g = ground_of(L);
    if (g)
        loft_ground_set(g, (int)luaL_checkinteger(L, 2), (float)luaL_checknumber(L, 3));
    return 0;
}

const luaL_Reg GROUND[] = {
    {"info",     api_ground_info    },
    {"at",       api_ground_at      },
    {"node",     api_ground_node    },
    {"lap", api_ground_lap},
    {"set",      api_ground_set     },
    {NULL,       NULL       }
};

/*  ---- the shelf's copies of a corner ------------------------------------ */

static ShelfFan *shelf_of(lua_State *L)
{
    return (ShelfFan *)rec_of(L, "shelf");
}

static int api_shelf_info(lua_State *L)
{
    ShelfFan *s = shelf_of(L);
    if (!s)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, R_MAP), lua_setfield(L, -2, "n");
    lua_pushinteger(L, s->nodes), lua_setfield(L, -2, "nodes");
    return 1;
}

/*  The copies of the corner at one grid point.  A flat run of the
 *  corridor that wrote each, how far its station was, and the height it
 *  put there.  Nothing where no corridor wrote it. */
static int api_shelf_copies(lua_State *L)
{
    ShelfFan *s = shelf_of(L);
    int       owner[4], i, n;
    float     dist[4], z[4];
    if (!s)
        return 0;
    n = shelf_copies(s, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3), owner, dist, z, 4);
    for (i = 0; i < n; ++i)
    {
        lua_pushinteger(L, owner[i]);
        lua_pushnumber(L, dist[i]);
        lua_pushnumber(L, z[i]);
    }
    return 3 * n;
}

/*  Every copy of that corner written by that corridor takes this level. */
static int api_shelf_set(lua_State *L)
{
    ShelfFan *s = shelf_of(L);
    if (s)
        shelf_set(s, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                  (int)luaL_checkinteger(L, 4), (float)luaL_checknumber(L, 5));
    return 0;
}

/*  The i-th node tile, the heights of every copy round it, and the level
 *  they all take. */
static int api_shelf_node(lua_State *L)
{
    ShelfFan *s = shelf_of(L);
    int32_t   col, row;
    if (!s || !shelf_node_at(s, (int)luaL_checkinteger(L, 2), &col, &row))
        return 0;
    lua_pushinteger(L, col);
    lua_pushinteger(L, row);
    return 2;
}

static int api_shelf_heights(lua_State *L)
{
    ShelfFan *s = shelf_of(L);
    float     z[16];
    int       i, n;
    if (!s)
        return 0;
    n = shelf_node_heights(s, (int32_t)luaL_checkinteger(L, 2), (int32_t)luaL_checkinteger(L, 3), z, 16);
    for (i = 0; i < n; ++i)
        lua_pushnumber(L, z[i]);
    return n;
}

static int api_shelf_node_set(lua_State *L)
{
    ShelfFan *s = shelf_of(L);
    if (s)
        shelf_node_set(s, (int32_t)luaL_checkinteger(L, 2), (int32_t)luaL_checkinteger(L, 3),
                       (float)luaL_checknumber(L, 4));
    return 0;
}

const luaL_Reg SHELF[] = {
    {"info",     api_shelf_info    },
    {"copies",   api_shelf_copies  },
    {"set",      api_shelf_set     },
    {"node",     api_shelf_node    },
    {"heights",  api_shelf_heights },
    {"node_set", api_shelf_node_set},
    {NULL,       NULL       }
};
