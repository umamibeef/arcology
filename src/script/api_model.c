/*  api_model.c: `arc.model`: the props, each a file of its own.
 *
 *  A model is one file under scripts/models.  It names itself, carries
 *  its own parameters.  Answers with the pieces it is made of when it is
 *  asked for a prop of a given size:
 *
 *      arc.model.define("street_lamp", {
 *          p = {post = 0.016, tall = 1.35, ...},   its own numbers
 *          build = function (p, at)                 at.size: the thing
 *              return {                             it belongs to, across
 *                  slot = ..., lift = ...,          in the painter's stack
 *                  ax = ..., ac = ...,              its origin from where
 *                                                   it was put
 *                  parts = {
 *                      {kind = "prism", d = p.post, w = p.post,
 *                       z1 = p.tall, mat = arc.mat.prop},
 *                      ...
 *                  },
 *              }
 *          end,
 *      })
 *
 *  A piece is a table:
 *
 *      {kind = "box" | "arm" | "lens" | "face" | "prism",
 *       ax = <n>, ac = <n>, ac2 = <n>,     along, across, the far across
 *       w = <n>, d = <n>,                  across the facing, and along
 *       z0 = <n>, z1 = <n>,                over the ground
 *       f0 = <n>, f1 = <n>,                a prism's two feet, along the
 *                                          run between the heights it is
 *                                          put on: 0 near, 1 far
 *       mat = arc.mat.prop, code = 0}
 *
 *  Every number in it is a plain number.  The build function has already
 *  worked them out from the model's own parameters and the size it was
 *  asked for.  That is what makes a model parametric rather than merely
 *  stored: a signal's arm reaches over the junction it stands at.  So
 *  what it answers for one junction is not what it answers for another.
 *
 *      arc.model.names()          every model there is
 *      arc.model.build(name [, size])
 *                                 what it is made of at that size, as
 *                                 the program sees it
 *      arc.model.params(name)     its own numbers, to read or to change
 *
 *  There is no reset and nothing is shipped in C: what the files define
 *  is all there is. */
#include "script.h"


#include <string.h>

#include "internal.h"
#include "mesh/internal.h"
#include "pipeline.h"
#include "mesh/model.h"

static const char *const KIND[] = {"box", "arm", "lens", "face", "prism"};

#define KIND_N     ((int)(sizeof KIND / sizeof KIND[0]))
#define MODEL_MAX  64
#define MODEL_NAME 32

/*  The models, by name and in the order the files defined them.  Each
 *  has its own table held in the registry, so the build function and the
 *  parameters stay reachable. */
static char       s_name[MODEL_MAX][MODEL_NAME];
static int        s_ref[MODEL_MAX];
static int        s_nmodel;
static lua_State *s_ms;

int script_model_count(void)
{
    return s_nmodel;
}

const char *script_model_name(int i)
{
    return i >= 0 && i < s_nmodel ? s_name[i] : NULL;
}

int script_model_find(const char *name)
{
    int i;
    for (i = 0; name && i < s_nmodel; ++i)
        if (strcmp(s_name[i], name) == 0)
            return i;
    return -1;
}

void script_model_reset(void)
{
    int i;
    for (i = 0; i < s_nmodel; ++i)
        if (s_ms && s_ref[i] != LUA_NOREF)
            luaL_unref(s_ms, LUA_REGISTRYINDEX, s_ref[i]);
    s_nmodel = 0;
    s_ms     = NULL;
}

/*  A yes or no on a part.  Read apart from `field`, because
 *  lua_tonumber answers 0 for a boolean and a part that says `uv = true`
 *  would silently read as false. */
static int field_yes(lua_State *L, int t, const char *key)
{
    int v;
    lua_getfield(L, t, key);
    v = lua_toboolean(L, -1) && !lua_isnil(L, -1);
    lua_pop(L, 1);
    return v;
}

static float field(lua_State *L, int t, const char *key, float def)
{
    float v;
    lua_getfield(L, t, key);
    v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

/*  What the model answers for a prop of that size, read into the walk's
 *  own arrays.  A model that answers nothing draws nothing. */
int script_model_build(int model, float size, ModelHead *head, ModelPart *parts)
{
    lua_State *L = s_ms;
    int        n = 0, k, t;
    memset(head, 0, sizeof *head);
    if (!L || model < 0 || model >= s_nmodel || s_ref[model] == LUA_NOREF)
        return 0;
    lua_rawgeti(L, LUA_REGISTRYINDEX, s_ref[model]);
    lua_getfield(L, -1, "build");
    if (!lua_isfunction(L, -1))
    {
        lua_pop(L, 2);
        return 0;
    }
    lua_getfield(L, -2, "p"); /* the model's own numbers */
    lua_newtable(L);
    lua_pushnumber(L, size), lua_setfield(L, -2, "size");
    if (!api_rule_call(L, s_name[model], 2))
    {
        lua_pop(L, 1);
        return 0;
    }
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 2);
        return 0;
    }
    t          = lua_gettop(L);
    head->slot = field(L, t, "slot", 0.0f);
    head->lift = field(L, t, "lift", 0.0f);
    head->ax   = field(L, t, "ax", 0.0f);
    head->ac   = field(L, t, "ac", 0.0f);
    lua_getfield(L, t, "parts");
    if (lua_istable(L, -1))
    {
        int have = (int)lua_rawlen(L, -1);
        int list = lua_gettop(L);
        n        = have < MODEL_PARTS ? have : MODEL_PARTS;
        for (k = 0; k < n; ++k)
        {
            ModelPart  *p = &parts[k];
            const char *kn;
            int         q, pt;
            lua_rawgeti(L, list, k + 1);
            pt = lua_gettop(L);
            memset(p, 0, sizeof *p);
            if (!lua_istable(L, pt))
            {
                lua_pop(L, 1);
                continue;
            }
            lua_getfield(L, pt, "kind");
            kn     = lua_isstring(L, -1) ? lua_tostring(L, -1) : "box";
            p->kind = M_BOX;
            for (q = 0; q < KIND_N; ++q)
                if (strcmp(KIND[q], kn) == 0)
                    p->kind = q;
            lua_pop(L, 1);
            p->ax   = field(L, pt, "ax", 0.0f);
            p->ac   = field(L, pt, "ac", 0.0f);
            p->ac2  = field(L, pt, "ac2", 0.0f);
            p->w    = field(L, pt, "w", 0.0f);
            p->d    = field(L, pt, "d", 0.0f);
            p->z0   = field(L, pt, "z0", 0.0f);
            p->z1   = field(L, pt, "z1", 0.0f);
            p->f0   = field(L, pt, "f0", 0.0f);
            p->f1   = field(L, pt, "f1", 1.0f);
            p->mat  = field(L, pt, "mat", (float)MAT_PROP);
            p->code = field(L, pt, "code", 0.0f);
            p->uv   = field_yes(L, pt, "uv");
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 3); /* the parts list, the answer, the model */
    return n;
}

/*  arc.model.define(name, model): one file, one model. */
static int l_define(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    int         mi;
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_getfield(L, 2, "build");
    if (!lua_isfunction(L, -1))
        return luaL_error(L, "the model %s has no build function", name);
    lua_pop(L, 1);
    s_ms = L;
    mi   = script_model_find(name);
    if (mi < 0)
    {
        if (s_nmodel >= MODEL_MAX)
            return luaL_error(L, "no room for another model: %s", name);
        mi        = s_nmodel++;
        s_ref[mi] = LUA_NOREF;
        snprintf(s_name[mi], MODEL_NAME, "%s", name);
    }
    if (s_ref[mi] != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, s_ref[mi]);
    lua_pushvalue(L, 2);
    s_ref[mi] = luaL_ref(L, LUA_REGISTRYINDEX);
    s_dirty   = 1;
    return 0;
}

static int l_names(lua_State *L)
{
    int i;
    lua_newtable(L);
    for (i = 0; i < s_nmodel; ++i)
        lua_pushstring(L, s_name[i]), lua_rawseti(L, -2, i + 1);
    return 1;
}

/*  A model's own numbers, as the table it keeps them in: reading one
 *  says what the prop measures, writing one moves it. */
static int l_params(lua_State *L)
{
    int mi = script_model_find(luaL_checkstring(L, 1));
    if (mi < 0 || s_ref[mi] == LUA_NOREF)
        return 0;
    lua_rawgeti(L, LUA_REGISTRYINDEX, s_ref[mi]);
    lua_getfield(L, -1, "p");
    return 1;
}

/*  What a model is made of at that size, as the program sees it: the
 *  pieces with every number worked out.  For a console, a check or a
 *  report: the build itself goes straight to the walk. */
static int l_build(lua_State *L)
{
    ModelHead head;
    ModelPart part[MODEL_PARTS];
    int       mi = script_model_find(luaL_checkstring(L, 1)), k, n;
    float     size = (float)luaL_optnumber(L, 2, 0.0);
    if (mi < 0)
        return 0;
    n = script_model_build(mi, size, &head, part);
    lua_newtable(L);
    lua_pushnumber(L, head.slot), lua_setfield(L, -2, "slot");
    lua_pushnumber(L, head.lift), lua_setfield(L, -2, "lift");
    lua_pushnumber(L, head.ax), lua_setfield(L, -2, "ax");
    lua_pushnumber(L, head.ac), lua_setfield(L, -2, "ac");
    lua_newtable(L);
    for (k = 0; k < n; ++k)
    {
        lua_newtable(L);
        lua_pushstring(L, part[k].kind >= 0 && part[k].kind < KIND_N ? KIND[part[k].kind] : "box"), lua_setfield(L, -2, "kind");
        lua_pushnumber(L, part[k].ax), lua_setfield(L, -2, "ax");
        lua_pushnumber(L, part[k].ac), lua_setfield(L, -2, "ac");
        lua_pushnumber(L, part[k].ac2), lua_setfield(L, -2, "ac2");
        lua_pushnumber(L, part[k].w), lua_setfield(L, -2, "w");
        lua_pushnumber(L, part[k].d), lua_setfield(L, -2, "d");
        lua_pushnumber(L, part[k].z0), lua_setfield(L, -2, "z0");
        lua_pushnumber(L, part[k].z1), lua_setfield(L, -2, "z1");
        lua_pushnumber(L, part[k].f0), lua_setfield(L, -2, "f0");
        lua_pushnumber(L, part[k].f1), lua_setfield(L, -2, "f1");
        lua_pushnumber(L, part[k].mat), lua_setfield(L, -2, "mat");
        lua_pushnumber(L, part[k].code), lua_setfield(L, -2, "code");
        lua_rawseti(L, -2, k + 1);
    }
    lua_setfield(L, -2, "parts");
    return 1;
}

void api_model_open(lua_State *L)
{
    script_model_reset();
    lua_newtable(L);
    lua_pushcfunction(L, l_define), lua_setfield(L, -2, "define");
    lua_pushcfunction(L, l_names), lua_setfield(L, -2, "names");
    lua_pushcfunction(L, l_params), lua_setfield(L, -2, "params");
    lua_pushcfunction(L, l_build), lua_setfield(L, -2, "build");
    lua_setfield(L, -2, "model");
}

