/*  internal.h -- what the scripting layer's own sources share.  The Lua
 *  state and the helpers that put one table on it; nothing outside
 *  src/script includes this, and nothing inside src/script exposes a Lua
 *  type through script.h. */
#ifndef ARC_SCRIPT_INTERNAL_H
#define ARC_SCRIPT_INTERNAL_H

#if SC2K_LUA

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

/*  The one state, or NULL when none is up. */
extern lua_State *s_L;
/*  Set where a script writes a knob or a constant: the mesh needs
 *  building again for the change to be on the screen. */
extern int s_dirty;

/*  Each surface puts its own table on `arc`, which is on the stack top
 *  when these are called. */
void api_tune_open(lua_State *L);  /* arc.tune, arc.geo */
void api_rules_open(lua_State *L); /* arc.rules */
void api_world_open(lua_State *L); /* arc.city, arc.mesh */
void api_put_open(lua_State *L);   /* arc.put, arc.mat    */
void api_object_open(lua_State *L); /* the object handle  */
lua_State *script_state(void);
void api_model_open(lua_State *L); /* arc.model           */

/*  Call `arc.rules[name]` with `nargs` values already on the stack,
 *  asking for `nres`.  Answers 1 when it ran, 0 when there is no such
 *  rule; on 0 the arguments are popped.  A rule that errors is reported
 *  once and then left alone until the script is read again. */
int api_rule_begin(lua_State *L, const char *name);
int api_rule_call(lua_State *L, const char *name, int nargs, int nres);
/*  A rule that never returns would freeze the frame it was asked in, and
 *  a rule is edited while the city is on screen: one is stopped after
 *  this many instructions and reported like any other fault, so a loop
 *  that cannot end costs a message rather than the program.  The lint
 *  puts the same stop on the rules it tries.
 *
 *  The cap is a COMPOSITION'S, not a decision's: a rule that lays a long
 *  strip's whole surface runs a hundred instructions a station over
 *  thousands of stations, and the longest strip in the largest shipped
 *  city is well inside this.  It is still a blink -- a loop that cannot
 *  end is caught in well under a tenth of a second. */
#define RULE_STEPS 8000000
void api_rule_runaway(lua_State *L, lua_Debug *ar);

/*  A field of the table on the stack top, as a number, or `def`. */
float api_field_num(lua_State *L, const char *key, float def);

#endif
#endif
