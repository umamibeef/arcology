/*  api_object.c: an OBJECT of the world, as a script sees one.
 *
 *  Everything the pipeline builds is a thing: a strip of line, a
 *  junction, a margin, a level meet, a prop.  A script is handed the
 *  thing itself, not a window on to "the one being drawn":
 *
 *      arc.rules.strip = function (s)
 *          for i = 1, s:count() - 1 do
 *              local x, y, z, dx, dy = s:at(i)
 *              s:quad(...)
 *          end
 *          return true
 *      end
 *
 *  `s` is a handle on the pipeline's own record.  It can be passed to a
 *  function, kept for the length of the call, and asked what it is.
 *  `s.kind` is "strip".  And every face it draws is attributed to it, so
 *  the inspector can say which thing put a triangle on a tile.
 *
 *  The methods an object has are its kind's.  They are all of one shape.
 *  The ones that MEASURE answer plain numbers and make no table.  A
 *  strip has thousands of stations, and a city thousands of strips.  The
 *  ones that DRAW answer whether the mesh took the face.
 *
 *      every kind      kind, tile, ground(x, y), order(x, y)
 *      strip           count, at, width, extras, class,
 *                      quad, tri, prism, fan, box, wire, model
 *
 *  A handle is dead the moment the rule that was given it returns.  The
 *  record it points at is the pipeline's, and the pipeline moves on.  A
 *  method on a dead handle answers nothing rather than reading freed
 *  memory.
 *
 *  THIS FILE IS THE HANDLE AND THE LIST OF KINDS.  A kind's own methods
 *  are in the file that knows the thing.
 *
 *      obj_world     the build itself
 *      obj_strip     one strip of line
 *      obj_path      a path, and everything it is cut into
 *      obj_node      a node, and everything drawn at one
 *      obj_band      a raised surface, and how it comes down
 *      obj_lane      a way of travel, and the world that moves on it
 *      obj_ground    the ground
 *      api_tile      one tile of it
 *
 *  Adding a kind is a row in KINDS[] and a methods table in one of
 *  those.  What they share is obj.h. */
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

#define OBJ_META "arc.object"

/*  The handle itself: what kind of thing, the pipeline's record, and the
 *  generation it was made in.  Every handle of an older generation is
 *  dead, which is how a script that stashed one cannot reach a record
 *  the pipeline has moved past. */
typedef struct
{
    const char *kind;
    void       *rec;
    unsigned    gen;
} ScriptObj;

static unsigned s_gen = 1;
static int      s_depth;  /* rules inside rules: only the outermost ends a generation */

static ScriptObj *obj_check(lua_State *L)
{
    ScriptObj *o = (ScriptObj *)luaL_checkudata(L, 1, OBJ_META);
    return o && o->gen == s_gen && o->rec ? o : NULL;
}

/*  The handle's record, if it is of the kind asked for.  A handle from a
 *  past call answers nothing: obj_check has already refused it. */
void *rec_of(lua_State *L, const char *kind)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, kind) == 0 ? o->rec : NULL;
}

/*  The fields as a fresh table, left on the stack.  The list is in
 *  obj.h, where every kind reads it. */
int api_fields(lua_State *L, const void *rec, const Field *f)
{
    if (!rec)
        return 0;
    lua_newtable(L);
    for (; f->name; ++f)
    {
        const char *at = (const char *)rec + f->off;
        switch (f->type)
        {
        case FLD_NUM: lua_pushnumber(L, (lua_Number)*(const float *)at); break;
        case FLD_INT: lua_pushinteger(L, *(const int *)at); break;
        case FLD_BOOL: lua_pushboolean(L, *(const int *)at != 0); break;
        }
        lua_setfield(L, -2, f->name);
    }
    return 1;
}
/*  The tile's own methods live in api_tile.c, which knows the ground.
 *  The handle is the same one every other kind uses. */
TileFan *api_tile_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "tile") == 0 ? (TileFan *)o->rec : NULL;
}

const luaL_Reg *api_tile_methods(int *n);

/*  ---- the handle ---------------------------------------------------- */

static int obj_index(lua_State *L)
{
    ScriptObj  *o   = (ScriptObj *)luaL_checkudata(L, 1, OBJ_META);
    const char *key = lua_tostring(L, 2);
    if (key && strcmp(key, "kind") == 0)
    {
        lua_pushstring(L, o->kind);
        return 1;
    }
    if (key && strcmp(key, "alive") == 0)
    {
        lua_pushboolean(L, o->gen == s_gen && o->rec != NULL);
        return 1;
    }
    /*  The methods are the KIND'S: a strip and a junction both answer
     *  `info`, and each answers its own. */
    lua_getmetatable(L, 1);
    lua_getfield(L, -1, "methods");
    lua_getfield(L, -1, o->kind);
    if (!lua_istable(L, -1))
        return 1; /* a kind with no methods: every name reads as nothing */
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    return 1;
}

static int obj_tostring(lua_State *L)
{
    ScriptObj *o = (ScriptObj *)luaL_checkudata(L, 1, OBJ_META);
    lua_pushfstring(L, "%s(%s)", o->kind, o->gen == s_gen && o->rec ? "live" : "past");
    return 1;
}

/*  A handle on a record, on the stack.  The same thing script_rule_object
 *  hands a rule, for the primitives that hand one BACK to a script that
 *  asked. */
void api_object_push(lua_State *L, const char *kind, void *rec)
{
    ScriptObj *o = (ScriptObj *)lua_newuserdata(L, sizeof *o);
    o->kind      = kind;
    o->rec       = rec;
    o->gen       = s_gen;
    luaL_getmetatable(L, OBJ_META);
    lua_setmetatable(L, -2);
}

/*  Hand a rule the thing itself.  A thing can be asked more than one
 *  question.  A strip is asked for its surface and for the margins
 *  beside it.  So the rule is named apart from the kind, and the kind is
 *  what says which methods the handle has. */
int script_rule_object(const char *rule, const char *kind, void *rec)
{
    lua_State *L = script_state();
    int        drew = 0;
    if (!L || !api_rule_begin(L, rule))
        return 0;
    api_object_push(L, kind, rec);
    ++s_depth;
    if (!api_rule_call(L, rule, 1))
    {
        --s_depth;
        return 0;
    }
    drew = !lua_isnil(L, -1) && lua_toboolean(L, -1);
    lua_pop(L, 1);
    /*  Every handle made before this point is now past: the record it
     *  points at is the pipeline's and the pipeline has moved on.  A
     *  rule a rule asked for has not moved it on, though.  The outer
     *  rule is still holding its own object and still has work to do
     *  with it.  So the stamp only advances when the outermost one is
     *  finished. */
    if (--s_depth == 0)
        ++s_gen;
    return drew;
}

/*  THE KINDS.  A kind is its name and its methods, declared once.  The
 *  handle's own table is built from this, and rec_of answers by the same
 *  name.  Adding a kind is a row here and a method table.  It is not a
 *  row here, a line in the registration and a helper of its own. */
static const struct
{
    const char       *name;
    const luaL_Reg   *methods;
} KINDS[] = {
    {"strip",    STRIP},
    {"junction", JUNCTION},
    {"margin",  MARGIN},
    {"lane",     LANE},
    {"panel",    LAP},
    {"outline",  OUTLINE},
    {"band",     BAND},
    {"fit",      FIT},
    {"runs",     RUNS},
    {"chain",    CHAIN},
    {"meet",     MEET},
    {"bridge",   BRIDGE},
    {"step",     STEP},
    {"sweep",    SWEEP},
    {"pieces",   PIECES},
    {"stair",    STAIR},
    {"profile",  PROFILE},
    {"slide",    SLIDE},
    {"drop",     DROP},
    {"ground",   GROUND},
    {"spurs",    ORIENT},
    {"terrain",  TERRAIN},
    {"shelf",    SHELF},
    {"cross",    CROSS},
    {"path",     PATH},
    {"world",    WORLD},
    {"network",  NETWORK},
    {"bands",    BANDS},
    {"moving",   MOVING},
    {"frame",    FRAME},
    {"links",    LINKS},
    {"snap",     SNAP},
    {"turns",    TURNS},
    {"threads",   THREADS},
    {"signs",    SIGNS},
    {"caps",     CAPS},
    {"target",   TARGET},
};

void api_object_open(lua_State *L)
{
    luaL_newmetatable(L, OBJ_META);
    lua_pushcfunction(L, obj_index), lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, obj_tostring), lua_setfield(L, -2, "__tostring");
    /*  One table of methods for each kind of thing, so two kinds may
     *  answer the same question in their own ways. */
    lua_newtable(L);
    {
        size_t k;
        for (k = 0; k < sizeof KINDS / sizeof KINDS[0]; ++k)
            lua_newtable(L), luaL_setfuncs(L, KINDS[k].methods, 0),
                lua_setfield(L, -2, KINDS[k].name);
    }
    {
        int n;
        lua_newtable(L), luaL_setfuncs(L, api_tile_methods(&n), 0), lua_setfield(L, -2, "tile");
    }
    lua_setfield(L, -2, "methods");
    lua_pop(L, 1);
}


