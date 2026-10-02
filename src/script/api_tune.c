/*  api_tune.c: `arc.tune` and `arc.geo`: every tuned number the line
 *  works are drawn with, by name.
 *
 *  A knob is DECLARED (`arc.tune.declare`, scripts/tune.lua): it carries
 *  a range, a value it starts at and the name a slider shows.  A number
 *  is simply set.  That is the only difference between the two.
 *
 *  Both hold nothing themselves.  A read goes to the C.  So the value is
 *  the one the build is running with, however it was last moved.  It may
 *  be moved by a script, by a slider, by a switch.  A write goes to the
 *  C too.  This holds it to a range the rest of the pipeline can still
 *  draw with.  Asks for the world to be drawn again.
 *
 *  A table that holds nothing cannot be walked with pairs, so
 *  `arc.settings()` answers a plain snapshot of both for printing. */
#include "script.h"


#include <string.h>

#include "internal.h"
#include "pipeline.h"

#include "net/net.h"
typedef int (*SetFn)(const char *name, float v);
typedef int (*FindFn)(const char *name, float *v);
typedef const char *(*NameFn)(int i, float *v);

/*  Which of the two a read or a write is for: the same pair of
 *  metamethods serves both, and the upvalues say which.  A read asks
 *  the store for the one name.  A rule that runs per tile reads several
 *  numbers, and a walk of the store at each is the cost of the rule. */
static int l_get(lua_State *L)
{
    FindFn      find = (FindFn)lua_touserdata(L, lua_upvalueindex(1));
    const char *key  = lua_tostring(L, 2);
    float       v;
    /*  A name no script has set is a FAULT, not a nought.  A nil here
     *  would arrive in the arithmetic as a silent zero.  The scripts are
     *  read through one init.lua, so the order is the script layer's
     *  own.  A name it does not hold can only be a mistake. */
    if (!key || !find(key, &v))
        return luaL_error(L, "no such setting to read: %s.%s",
                          lua_tostring(L, lua_upvalueindex(2)),
                          key ? key : "?");
    lua_pushnumber(L, (lua_Number)v);
    return 1;
}

/*  A string field of the table at 1, copied: the store keeps its own. */
static void copy_field(lua_State *L, const char *key, char *out, size_t cap)
{
    const char *s;
    lua_getfield(L, 1, key);
    s = lua_tostring(L, -1);
    snprintf(out, cap, "%s", s ? s : "");
    lua_pop(L, 1);
}

/*  A number field of the table at 1.  A knob says all four of them: a
 *  range guessed for it would clamp a value the script meant. */
static float need_field(lua_State *L, const char *key)
{
    float v;
    lua_getfield(L, 1, key);
    if (!lua_isnumber(L, -1))
        luaL_error(L, "arc.tune.declare wants a number for %s", key);
    v = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

/*  arc.tune.declare{name=, label=, group=, default=, lo=, hi=}: ONE
 *  KNOB.  The store keeps its float at an address that never moves.  So
 *  a family may hold a pointer at it.
 *
 *  A declaration that names a knob the store already holds moves its
 *  range and its label.  It keeps the value.  That is what lets the
 *  scripts be read again while somebody is tuning. */
int api_tune_declare(lua_State *L)
{
    RKnob k;
    char  name[32], label[48], group[32];
    memset(&k, 0, sizeof k);
    luaL_checktype(L, 1, LUA_TTABLE);
    copy_field(L, "name", name, sizeof name);
    copy_field(L, "label", label, sizeof label);
    copy_field(L, "group", group, sizeof group);
    if (!name[0])
        return luaL_error(L, "arc.tune.declare wants a name");
    k.name  = name;
    k.label = label[0] ? label : NULL;
    k.group = group[0] ? group : NULL;
    k.lo    = need_field(L, "lo");
    k.hi    = need_field(L, "hi");
    k.def   = need_field(L, "default");
    if (k.hi < k.lo)
        return luaL_error(L, "the knob %s: its hi is under its lo", name);
    if (!tune_declare(&k))
        return luaL_error(L, "the knob %s cannot be kept", name);
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

static void table_open(lua_State *L, const char *field, SetFn set, FindFn find, lua_CFunction declare)
{
    lua_newtable(L); /* the proxy: empty, so every read and write is caught */
    if (declare)
    {
        /*  A knob is DECLARED through the same table it is set through.
         *  The metatable is not on yet, so this is a real key on the
         *  proxy and the metamethods below never see it. */
        lua_pushcfunction(L, declare);
        lua_setfield(L, -2, "declare");
    }
    lua_newtable(L); /* its metatable */
    lua_pushlightuserdata(L, (void *)find);
    lua_pushstring(L, field);
    lua_pushcclosure(L, l_get, 2);
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
    snapshot(L, "tune", tune_name);
    snapshot(L, "geo", geo_name);
    return 1;
}

void api_tune_open(lua_State *L)
{
    table_open(L, "tune", tune_set, tune_find, api_tune_declare);
    table_open(L, "geo", geo_set, geo_find, NULL);
    lua_pushcfunction(L, l_settings);
    lua_setfield(L, -2, "settings");
}

