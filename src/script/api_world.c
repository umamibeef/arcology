/*  api_world.c -- `arc.city` and `arc.mesh`: what the script may look at.
 *
 *      arc.city.size                    the map, in tiles
 *      arc.city.tile(col, row)          what stands there
 *      arc.city.road_class(col, row)    0 a road, 1 an avenue, 2 a boulevard
 *      arc.mesh.crossings()             the crossing tally of the last build
 *      arc.mesh.walkways()              the footway network's counts
 *      arc.mesh.faults()                what the checks refuse to pass
 *      arc.mesh.probe(x, y)             every surface over one point, to the dump
 *
 *  All of it reads the build that has already happened, so a script may
 *  ask what its own rules produced. */
#include "script.h"

#if SC2K_LUA

#include "internal.h"
#include "mesh/mesh.h"
#include "net/internal.h"

static int l_tile(lua_State *L)
{
    const RCity *c   = walk_net_city();
    int32_t      col = (int32_t)luaL_checkinteger(L, 1);
    int32_t      row = (int32_t)luaL_checkinteger(L, 2);
    int32_t      i;
    if (!c || col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return 0;
    i = row * R_MAP + col;
    lua_newtable(L);
    lua_pushinteger(L, c->xbld[i]), lua_setfield(L, -2, "xbld");
    lua_pushinteger(L, c->xzon[i]), lua_setfield(L, -2, "xzon");
    lua_pushinteger(L, c->xter[i]), lua_setfield(L, -2, "xter");
    lua_pushinteger(L, c->xbit[i]), lua_setfield(L, -2, "xbit");
    lua_pushinteger(L, c->xtrf[(row >> 1) * R_HALF + (col >> 1)]), lua_setfield(L, -2, "traffic");
    return 1;
}

static int l_road_class(lua_State *L)
{
    const RCity *c   = walk_net_city();
    int32_t      col = (int32_t)luaL_checkinteger(L, 1);
    int32_t      row = (int32_t)luaL_checkinteger(L, 2);
    if (!c || col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return 0;
    lua_pushinteger(L, (lua_Integer)road_class(c, col, row));
    return 1;
}

static int l_crossings(lua_State *L)
{
    int t[6];
    walk_cross_counts(t);
    lua_newtable(L);
    lua_pushinteger(L, t[0]), lua_setfield(L, -2, "mouths");
    lua_pushinteger(L, t[1]), lua_setfield(L, -2, "uncontrolled");
    lua_pushinteger(L, t[2]), lua_setfield(L, -2, "no_pavement");
    lua_pushinteger(L, t[3]), lua_setfield(L, -2, "not_parallel");
    lua_pushinteger(L, t[4]), lua_setfield(L, -2, "no_road");
    lua_pushinteger(L, t[5]), lua_setfield(L, -2, "narrowed");
    return 1;
}

static int l_walkways(lua_State *L)
{
    int i, n = walk_net_count(), k[4] = {0, 0, 0, 0};
    for (i = 0; i < n; ++i)
        ++k[walk_net_get(i)->kind & 3];
    lua_newtable(L);
    lua_pushinteger(L, n), lua_setfield(L, -2, "paths");
    lua_pushinteger(L, k[WALK_SIDE]), lua_setfield(L, -2, "sides");
    lua_pushinteger(L, k[WALK_CORNER]), lua_setfield(L, -2, "corners");
    lua_pushinteger(L, k[WALK_CROSS]), lua_setfield(L, -2, "crossings");
    lua_pushinteger(L, k[WALK_CAP]), lua_setfield(L, -2, "caps");
    return 1;
}

static int l_faults(lua_State *L)
{
    lua_newtable(L);
    lua_pushinteger(L, junction_outline_faults()), lua_setfield(L, -2, "outlines");
    lua_pushinteger(L, walk_net_faults()), lua_setfield(L, -2, "crossings");
    return 1;
}

static int l_probe(lua_State *L)
{
    const RMesh *m = mesh_built();
    if (!m)
        return 0;
    mesh_probe(m, (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2));
    return 0;
}

void api_world_open(lua_State *L)
{
    lua_newtable(L);
    lua_pushinteger(L, R_MAP), lua_setfield(L, -2, "size");
    lua_pushcfunction(L, l_tile), lua_setfield(L, -2, "tile");
    lua_pushcfunction(L, l_road_class), lua_setfield(L, -2, "road_class");
    lua_setfield(L, -2, "city");

    lua_newtable(L);
    lua_pushcfunction(L, l_crossings), lua_setfield(L, -2, "crossings");
    lua_pushcfunction(L, l_walkways), lua_setfield(L, -2, "walkways");
    lua_pushcfunction(L, l_faults), lua_setfield(L, -2, "faults");
    lua_pushcfunction(L, l_probe), lua_setfield(L, -2, "probe");
    lua_setfield(L, -2, "mesh");
}

#endif
