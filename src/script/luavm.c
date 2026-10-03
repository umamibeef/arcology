/*  luavm.c: the parts of the Lua interface one VM has and the other does
 *  not.  See luavm.h. */
#include "luavm.h"

#include <stdint.h>

#ifdef ARC_LUAJIT

/*  luaL_tolstring as Lua 5.2 defines it: the value as a string, by its
 *  __tostring where it has one, pushed and returned. */
const char *luavm_tolstring(lua_State *L, int idx, size_t *len)
{
    if (idx < 0 && idx > LUA_REGISTRYINDEX)
        idx = lua_gettop(L) + idx + 1;
    if (luaL_callmeta(L, idx, "__tostring"))
    {
        if (!lua_isstring(L, -1))
            luaL_error(L, "'__tostring' must return a string");
    }
    else
        switch (lua_type(L, idx))
        {
            case LUA_TNUMBER:
            case LUA_TSTRING:
                lua_pushvalue(L, idx);
                break;
            case LUA_TBOOLEAN:
                lua_pushstring(L, lua_toboolean(L, idx) ? "true" : "false");
                break;
            case LUA_TNIL:
                lua_pushliteral(L, "nil");
                break;
            default:
                lua_pushfstring(L, "%s: %p", luaL_typename(L, idx), lua_topointer(L, idx));
                break;
        }
    return lua_tolstring(L, -1, len);
}

/*  The trace cache is made large enough for the scripts.  LuaJIT keeps
 *  1000 traces and 2 MB of machine code at most, and when either fills,
 *  it flushes every trace.  One build of the scripts compiles some
 *  thousands of traces.  With the default limits it flushes several
 *  times a build and compiles most traces again after each flush. */
void luavm_open(lua_State *L)
{
    luaL_openlibs(L);
    if (luaL_dostring(L, "jit.opt.start('maxtrace=60000', 'maxmcode=65536')") != 0)
        lua_pop(L, 1);
}

#else

/*  LuaJIT's `bit` library, for Lua 5.4.  An argument is taken to 32 bits
 *  and the answer is the signed 32-bit number, as LuaJIT gives it. */
static uint32_t arg32(lua_State *L, int i)
{
    lua_Number n = luaL_checknumber(L, i);
    return (uint32_t)(int64_t)n;
}

static int answer(lua_State *L, uint32_t v)
{
    lua_pushinteger(L, (lua_Integer)(int32_t)v);
    return 1;
}

static int bit_tobit(lua_State *L)
{
    return answer(L, arg32(L, 1));
}

static int bit_bnot(lua_State *L)
{
    return answer(L, ~arg32(L, 1));
}

static int fold(lua_State *L, int op)
{
    uint32_t v = arg32(L, 1);
    int      i, n = lua_gettop(L);
    for (i = 2; i <= n; ++i)
    {
        uint32_t w = arg32(L, i);
        v          = op == 0 ? v & w : op == 1 ? v | w
                                               : v ^ w;
    }
    return answer(L, v);
}

static int bit_band(lua_State *L)
{
    return fold(L, 0);
}

static int bit_bor(lua_State *L)
{
    return fold(L, 1);
}

static int bit_bxor(lua_State *L)
{
    return fold(L, 2);
}

static int bit_lshift(lua_State *L)
{
    return answer(L, arg32(L, 1) << (arg32(L, 2) & 31));
}

static int bit_rshift(lua_State *L)
{
    return answer(L, arg32(L, 1) >> (arg32(L, 2) & 31));
}

static int bit_arshift(lua_State *L)
{
    return answer(L, (uint32_t)((int32_t)arg32(L, 1) >> (arg32(L, 2) & 31)));
}

void luavm_open(lua_State *L)
{
    static const luaL_Reg BIT[] = {
        {"tobit",   bit_tobit  },
        {"bnot",    bit_bnot   },
        {"band",    bit_band   },
        {"bor",     bit_bor    },
        {"bxor",    bit_bxor   },
        {"lshift",  bit_lshift },
        {"rshift",  bit_rshift },
        {"arshift", bit_arshift},
        {NULL,      NULL       }
    };
    luaL_openlibs(L);
    luaL_newlib(L, BIT);
    lua_setglobal(L, "bit");
}

#endif
