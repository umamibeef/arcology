/*  internal.h -- what the scripting layer's own sources share.  The Lua
 *  state and the helpers that put one table on it; nothing outside
 *  src/script includes this, and nothing inside src/script exposes a Lua
 *  type through script.h. */
#ifndef ARC_SCRIPT_INTERNAL_H
#define ARC_SCRIPT_INTERNAL_H


#include "lauxlib.h"
#include "lua.h"
#include "script.h"
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
void api_object_push(lua_State *L, const char *kind, void *rec); /* a handle on a record, for a primitive that hands one back */
lua_State *script_state(void);
void api_model_open(lua_State *L); /* arc.model           */
void api_family_open(lua_State *L); /* arc.family         */
void api_data_open(lua_State *L);   /* arc.bytes, arc.numbers */
/*  A family's numbers off the table on the stack top: one reading, so
 *  whoever starts it -- a rule that answers, or a script that pushes --
 *  gets the same fields the same way. */
void script_family_read(lua_State *L, ScriptFamily *out);
void api_prop_push(lua_State *L, const ScriptProp *at); /* a prop's place, as a table */
/*  A family declaration read off the Lua table on the stack at `t`, into
 *  room that outlives it: the strings a family keeps would otherwise die
 *  with the table.  ONE scratch, reused, so the running program and the
 *  lint read a declaration by the same expression.  Answers NULL where
 *  the table is not one. */
struct NetFamilyDecl;
const struct NetFamilyDecl *api_family_read(lua_State *L, int t);

/*  Call `arc.rules[name]` with `nargs` values already on the stack,
 *  asking for `nres`.  Answers 1 when it ran, 0 when there is no such
 *  rule; on 0 the arguments are popped.  A rule that errors is reported
 *  once and then left alone until the script is read again. */
int api_rule_begin(lua_State *L, const char *name);
int api_rule_call(lua_State *L, const char *name, int nargs, int nres);
/*  A rule that never returns would freeze the frame it was asked in, and
 *  a rule is edited while the city is on screen, so one is stopped and
 *  reported like any other fault: a loop that cannot end costs a message
 *  rather than the program.  The lint puts the same stop on the rules it
 *  tries.
 *
 *  The stop is a TIME, not a count of instructions.  What it has to tell
 *  apart is a loop that will never end from a composition that is merely
 *  long, and how many instructions the second of those takes is a
 *  property of the CITY -- a band with sixty ramps over thousands of
 *  stations runs tens of millions of them, and a count tuned on one map
 *  cuts a bigger one off in the middle and leaves its geometry missing.
 *  Time says what is actually meant, and says it the same on every map.
 *
 *  And the budget is GENEROUS, because the outermost rule is now the
 *  whole build: arc.rules.world composes a city and takes seconds doing
 *  it.  This is here to catch a program that will never finish, not to
 *  time one that will -- a budget close to a real build's cost abandons
 *  good builds on a busy machine, and a guard that cries wolf is worse
 *  than none.
 *
 *  RULE_STEPS is only how often the guard looks; RULE_SECONDS is the
 *  budget, and it is the whole of an outermost rule, nested calls
 *  included. */
#define RULE_STEPS   1000000
#define RULE_SECONDS 30 /* whole seconds: Lua's own formatter has no %g */
/*  And a rule that ran away, or raised, is NOT ASKED AGAIN until the
 *  scripts are read: the budget is one rule call's, and a rule asked
 *  once a strip would otherwise spend it a thousand times over and hang
 *  the build it was meant to protect. */
void api_rule_runaway(lua_State *L, lua_Debug *ar);
/*  The guard, on or off.  On resets the clock, so it belongs at the
 *  outermost call and nowhere inside it. */
void api_rule_watch(lua_State *L, int on);

/*  A field of the table on the stack top, as a number, or `def`. */
float api_field_num(lua_State *L, const char *key, float def);
int   api_take_lamps(lua_State *L, int idx, ScriptLamp *out, int max);
int   api_take_marks(lua_State *L, int idx, ScriptMark *out, int max);

#endif
