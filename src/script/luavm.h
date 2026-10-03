/*  luavm.h: the Lua the scripts run on, behind one set of names.  The
 *  scripts run on LuaJIT, or on Lua 5.4 when ARC_LUAJIT is off.  The
 *  bindings are written to the 5.4 interface, and this header gives the
 *  few calls that LuaJIT does not have.  The scripts keep to what both
 *  read, so they use no integer division and no bit operators.  They use
 *  the `bit` library, which LuaJIT carries and luavm.c gives to Lua 5.4. */
#ifndef ARC_LUAVM_H
#define ARC_LUAVM_H

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#ifdef ARC_LUAJIT
    #include "luajit.h"
    #define lua_rawlen(L, i)       lua_objlen((L), (i))
    #define lua_pushglobaltable(L) lua_pushvalue((L), LUA_GLOBALSINDEX)
    #define luaL_tolstring         luavm_tolstring
const char *luavm_tolstring(lua_State *L, int idx, size_t *len);
#endif

/*  The standard libraries, and `bit` where the VM does not carry it. */
void luavm_open(lua_State *L);

#endif /* ARC_LUAVM_H */
