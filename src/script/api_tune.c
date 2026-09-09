/*  api_tune.c -- `arc.tune` and `arc.geo`: every tuned number the road
 *  works are drawn with, by name.
 *
 *  Both hold nothing themselves.  A read goes to the C, so the value is
 *  the one the build is running with however it was last moved -- by a
 *  script, by a slider, by a switch.  A write goes to the C too, which
 *  holds it to a range the rest of the pipeline can still draw with, and
 *  asks for the world to be drawn again.
 *
 *  A table that holds nothing cannot be walked with pairs, so
 *  `arc.settings()` answers a plain snapshot of both for printing. */
#include "script.h"


#include <string.h>

#include "internal.h"
#include "net/internal.h"

typedef int (*SetFn)(const char *name, float v);
typedef const char *(*NameFn)(int i, float *v);

/*  Which of the two a read or a write is for: the same pair of
 *  metamethods serves both, and the upvalues say which. */
static int l_get(lua_State *L)
{
    NameFn      name = (NameFn)lua_touserdata(L, lua_upvalueindex(1));
    const char *key  = lua_tostring(L, 2);
    int         i;
    for (i = 0; key; ++i)
    {
        float       v;
        const char *n = name(i, &v);
        if (!n)
            break;
        if (strcmp(n, key) == 0)
        {
            lua_pushnumber(L, (lua_Number)v);
            return 1;
        }
    }
    return 0;
}

static int l_set(lua_State *L)
{
    SetFn       set  = (SetFn)lua_touserdata(L, lua_upvalueindex(1));
    const char *name = lua_tostring(L, 2);
    if (!name || !set(name, (float)luaL_checknumber(L, 3)))
        return luaL_error(L, "no such setting: %s", name ? name : "?");
    s_dirty = 1;
    return 0;
}

static void table_open(lua_State *L, const char *field, SetFn set, NameFn name)
{
    lua_newtable(L); /* the proxy: empty, so every read and write is caught */
    lua_newtable(L); /* its metatable */
    lua_pushlightuserdata(L, (void *)name);
    lua_pushcclosure(L, l_get, 1);
    lua_setfield(L, -2, "__index");
    lua_pushlightuserdata(L, (void *)set);
    lua_pushcclosure(L, l_set, 1);
    lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, field);
}

/*  Both, as plain tables, for a script that wants to walk or print them. */
static void snapshot(lua_State *L, const char *field, NameFn name)
{
    int i;
    lua_newtable(L);
    for (i = 0;; ++i)
    {
        float       v;
        const char *n = name(i, &v);
        if (!n)
            break;
        lua_pushnumber(L, (lua_Number)v);
        lua_setfield(L, -2, n);
    }
    lua_setfield(L, -2, field);
}

static int l_settings(lua_State *L)
{
    lua_newtable(L);
    snapshot(L, "tune", net_tune_name);
    snapshot(L, "geo", net_geo_name);
    return 1;
}

void api_tune_open(lua_State *L)
{
    table_open(L, "tune", net_tune_set, net_tune_name);
    table_open(L, "geo", net_geo_set, net_geo_name);
    lua_pushcfunction(L, l_settings);
    lua_setfield(L, -2, "settings");
}

