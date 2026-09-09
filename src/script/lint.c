/*  lint.c -- `arcology --lua-lint FILE...`: what is wrong with a script
 *  before it is ever run for real.
 *
 *  A generic Lua checker knows the language.  This one knows the
 *  VOCABULARY, which is where the time goes: `arc.geo.cros_deep` reads
 *  as nil, `arc.rules.crosing = f` is a rule that never fires, and
 *  neither says anything at all -- the city simply comes out unchanged
 *  and the next half hour goes on wondering why.  So every name is
 *  checked against what the program actually has.
 *
 *  Four passes, each catching what the one before it cannot:
 *
 *    1. it parses.
 *    2. it runs, in an environment where `arc.tune` and `arc.geo` refuse
 *       a name that is not a setting, `arc.rules` refuses a name that is
 *       not a rule or a value that is not a function, and a write to a
 *       global that was never declared is reported -- a missing `local`.
 *    3. every rule it sets is CALLED once, with an argument of the shape
 *       the pipeline hands it, so a typo inside a rule body is found
 *       here rather than at the next build.
 *    4. what each rule answered is the shape its caller reads.
 *
 *  Nothing it does touches the world: there is no city, no mesh and no
 *  window, and the settings it writes go to a table of its own. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "internal.h"
#include "mesh/internal.h"
#include "net/internal.h"

/*  One run's faults, said as they are found and counted for the exit. */
static int         s_bad;
static const char *s_file;
/*  The numbers the scripts set, with what they set them to.  A write of
 *  a name the program does not have MAKES one, so it is not a fault; a
 *  read of a name neither the program nor the scripts have still is, and
 *  that is the typo worth catching.  The values are kept because the
 *  rules are then exercised on the city's own numbers: a rule that
 *  divides by one, or steps along a strip by one, is only tried honestly
 *  when the number is what the scripts gave it. */
static char  s_own[512][32];
static float s_ownv[512];
static int   s_nown;

static int own_at(const char *name)
{
    int i;
    for (i = 0; name && i < s_nown; ++i)
        if (strcmp(s_own[i], name) == 0)
            return i;
    return -1;
}

static void bad(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "%s: ", s_file);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    ++s_bad;
}

/*  The rules the C asks for, and what each answers.  A rule not on this
 *  list is one nothing will ever call. */
static const struct
{
    const char *name;
    const char *shape; /* what its answer must be */
} RULES[] = {
    {"control",   "table"},
    {"crossing_at", "number"},
    {"crossing",  "number or boolean"},
    {"corner",    "table, false or nil"},
    {"lanes",     "table"},
    {"lamps",     "table"},
    {"rail_marks","table"},
    {"gate",      "number"},
    {"family",    "table"},
    {"strip",     "boolean"},
    {"curves",    "boolean"},
    {"walks",     "boolean"},
    {"junction",  "boolean"},
    {"footway",   "boolean"},
    {"walk_curves", "boolean"},
    {"tile",      "boolean"},
    {"zone_tint", "boolean"},
    {"outline",   "boolean"},
    {"band", "boolean"},
    {"fit","boolean"},
    {"runs",      "boolean"},
    {"chain",     "boolean"},
    {"meet",   "boolean"},
    {"bridge",    "boolean"},
    {"step",      "boolean"},
    {"world",      "boolean"},
    {"sweep",     "boolean"},
    {"pieces",    "boolean"},
    {"stair",     "boolean"},
    {"profile", "boolean"},
    {"slide", "boolean"},
    {"drop", "boolean"},
    {"ground", "boolean"},
    {"join",      "table"},
    {"after",     "string"},
    {"fit_choice", "string"},
    {"ramp_fork", "number"},
    {"seg_class", "number"},
    {"ramp_span", "table"},
    {"band_start", "string or nil"},
    {"car_follow", "number"},
    {"car_hold", "number"},
    {"shelf",     "boolean"},
    {"cross", "boolean"},
    {"orient", "boolean"},
    {"ramp_side", "boolean"},
    {"ramp_share", "number or nil"},
    {"lane",      "boolean"},
    {"panel", "boolean"},
    {"crossing_frame", "table"},
    {"crossing_marks", "table"},
    {"gate_arm",  "boolean"},
    {"power_tile","boolean"},
    {"path",      "nil"},
    {"power_crossing","boolean"},
};

/*  arc.tune and arc.geo: a name that is not a setting is a fault on the
 *  way in and on the way out, which the live tables cannot be -- there a
 *  read of an unknown name is simply nil. */
static int l_get(lua_State *L)
{
    const char *(*name)(int, float *) = (const char *(*)(int, float *))lua_touserdata(L, lua_upvalueindex(1));
    const char *key = lua_tostring(L, 2);
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
    i = own_at(key);
    if (i < 0)
        bad("no such setting to read: %s.%s", (const char *)lua_touserdata(L, lua_upvalueindex(2)), key ? key : "?");
    lua_pushnumber(L, i >= 0 ? (lua_Number)s_ownv[i] : 0.0);
    return 1;
}

static int l_set(lua_State *L)
{
    const char *(*name)(int, float *) = (const char *(*)(int, float *))lua_touserdata(L, lua_upvalueindex(1));
    const char *key = lua_tostring(L, 2);
    int         i, found = 0;
    for (i = 0; key && !found; ++i)
    {
        float       v;
        const char *n = name(i, &v);
        if (!n)
            break;
        found = strcmp(n, key) == 0;
    }
    if (!lua_isnumber(L, 3))
        bad("%s.%s wants a number", (const char *)lua_touserdata(L, lua_upvalueindex(2)), key ? key : "?");
    else if (key)
    {
        int at = own_at(key);
        if (at < 0 && s_nown < (int)(sizeof s_own / sizeof s_own[0]))
        {
            at = s_nown++;
            snprintf(s_own[at], sizeof s_own[0], "%s", key); /* a number of the script's own */
        }
        if (at >= 0)
            s_ownv[at] = (float)lua_tonumber(L, 3);
    }
    (void)found;
    return 0;
}

/*  A model, kept so its build function can be run once the whole set of
 *  scripts has been read.  A file that defines none, or one whose build
 *  is not a function, is a file that draws nothing. */
static char s_model[64][32];
static int  s_model_ref[64];
static int  s_nmodel;

static int l_model_define(lua_State *L)
{
    const char *name = lua_tostring(L, 1);
    if (!name)
    {
        bad("arc.model.define wants a name");
        return 0;
    }
    if (!lua_istable(L, 2))
    {
        bad("the model %s is not a table", name);
        return 0;
    }
    lua_getfield(L, 2, "build");
    if (!lua_isfunction(L, -1))
        bad("the model %s has no build function", name);
    lua_pop(L, 1);
    if (s_nmodel < (int)(sizeof s_model / sizeof s_model[0]))
    {
        snprintf(s_model[s_nmodel], sizeof s_model[0], "%s", name);
        lua_pushvalue(L, 2);
        s_model_ref[s_nmodel++] = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    return 0;
}

/*  Every model built, at a size a prop might actually be given, so a
 *  build that reaches for a number nothing sets is a fault here rather
 *  than a piece missing from the city. */
static void models_check(lua_State *L)
{
    int i;
    for (i = 0; i < s_nmodel; ++i)
    {
        int rc, n;
        lua_rawgeti(L, LUA_REGISTRYINDEX, s_model_ref[i]);
        lua_getfield(L, -1, "build");
        lua_getfield(L, -2, "p");
        lua_newtable(L);
        lua_pushnumber(L, 0.25), lua_setfield(L, -2, "size");
        api_rule_watch(L, 1);
        rc = lua_pcall(L, 2, 1, 0);
        api_rule_watch(L, 0);
        if (rc != LUA_OK)
        {
            bad("the model %s: %s", s_model[i], lua_tostring(L, -1));
            lua_pop(L, 2);
            continue;
        }
        if (!lua_istable(L, -1))
        {
            bad("the model %s built no shape at all", s_model[i]);
            lua_pop(L, 2);
            continue;
        }
        lua_getfield(L, -1, "parts");
        n = lua_istable(L, -1) ? (int)lua_rawlen(L, -1) : -1;
        if (n < 0)
            bad("the model %s built no parts", s_model[i]);
        else if (n == 0)
            bad("the model %s built an empty shape", s_model[i]);
        else
        {
            int k;
            for (k = 1; k <= n; ++k)
            {
                const char *key[6] = {"ax", "ac", "ac2", "w", "d", "z1"};
                int         q;
                lua_rawgeti(L, -1, k);
                for (q = 0; q < 6; ++q)
                {
                    lua_getfield(L, -1, key[q]);
                    if (!lua_isnil(L, -1) && !lua_isnumber(L, -1))
                        bad("the model %s, piece %d: %s is not a number", s_model[i], k, key[q]);
                    lua_pop(L, 1);
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 3);
    }
}

static void settings_open(lua_State *L, const char *field, const char *(*name)(int, float *))
{
    lua_newtable(L);
    lua_newtable(L);
    lua_pushlightuserdata(L, (void *)name);
    lua_pushlightuserdata(L, (void *)field);
    lua_pushcclosure(L, l_get, 2);
    lua_setfield(L, -2, "__index");
    lua_pushlightuserdata(L, (void *)name);
    lua_pushlightuserdata(L, (void *)field);
    lua_pushcclosure(L, l_set, 2);
    lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, field);
}

/*  A FAMILY'S STAGE, where a declaration answered it with a rule rather
 *  than with one of the pipeline's primitives.  The name is the family's
 *  own invention, so the fixed list above cannot hold it: the lint
 *  learns it from arc.family.define, and the stage says what shape the
 *  pipeline hands that rule.
 *
 *  A script may set the rule before the family that names it is
 *  declared, so a name the lint has not met is KEPT rather than
 *  reported, and the two lists are settled against each other once every
 *  file has been read. */
#define LINT_STAGE_MAX 32
static struct
{
    char    name[64];
    NetHook stage;
} s_stage[LINT_STAGE_MAX];
static int s_n_stage;
static struct
{
    char name[64];
    char file[256];
} s_unknown[LINT_STAGE_MAX];
static int s_n_unknown;

static int stage_of(const char *name)
{
    int i;
    for (i = 0; name && i < s_n_stage; ++i)
        if (strcmp(s_stage[i].name, name) == 0)
            return (int)s_stage[i].stage;
    return -1;
}

/*  arc.rules: a name that is not a rule, or a value that is not a
 *  function, is a rule that will never be called. */
static int l_rule_set(lua_State *L)
{
    const char *key = lua_tostring(L, 2);
    size_t      i;
    int         known = 0;
    for (i = 0; key && i < sizeof RULES / sizeof RULES[0]; ++i)
        known |= strcmp(RULES[i].name, key) == 0;
    if (!lua_isfunction(L, 3))
    {
        bad("arc.rules.%s wants a function", key ? key : "?");
        return 0;
    }
    if (!known && key && s_n_unknown < LINT_STAGE_MAX)
    {
        snprintf(s_unknown[s_n_unknown].name, sizeof s_unknown[0].name, "%s", key);
        snprintf(s_unknown[s_n_unknown].file, sizeof s_unknown[0].file, "%s", s_file ? s_file : "?");
        ++s_n_unknown;
    }
    lua_rawset(L, 1);
    return 0;
}

/*  Every rule name no family claimed, once all the files are read. */
static void unknown_rules(void)
{
    const char *was = s_file;
    int         i;
    for (i = 0; i < s_n_unknown; ++i)
    {
        if (stage_of(s_unknown[i].name) >= 0)
            continue;
        s_file = s_unknown[i].file;
        bad("no such rule: arc.rules.%s -- nothing will call it", s_unknown[i].name);
    }
    s_file = was;
}

/*  arc.pieces(t): which network a byte carries and where in the shared
 *  fifteen-piece layout it sits, and the same for the second family a
 *  crossing carries.  A layout outside 0..14 is a piece the art has no
 *  shape for; a family that is none of the three is a byte the pipeline
 *  will read as a power line. */
static int l_pieces_push(lua_State *L)
{
    if (!lua_istable(L, 1))
    {
        bad("arc.pieces wants a table, not %s", luaL_typename(L, 1));
        return 0;
    }
    lua_pushnil(L);
    while (lua_next(L, 1))
    {
        lua_Integer b = lua_tointeger(L, -2);
        int         k;
        if (!lua_isnumber(L, -2) || b < 0 || b > 255)
            bad("arc.pieces keyed an entry on %s, not a byte of the city", luaL_typename(L, -2));
        for (k = 0; k < 2; ++k)
        {
            const char *fam;
            if (k == 1)
            {
                lua_getfield(L, -1, "second");
                if (!lua_istable(L, -1))
                {
                    lua_pop(L, 1);
                    break;
                }
            }
            lua_getfield(L, -1, "family");
            fam = lua_tostring(L, -1);
            if (!fam || (strcmp(fam, "power") != 0 && strcmp(fam, "road") != 0 && strcmp(fam, "rail") != 0))
                bad("arc.pieces gave byte %d a family that is none of power, road or rail", (int)b);
            lua_pop(L, 1);
            lua_getfield(L, -1, "piece");
            if (!lua_isnumber(L, -1) || lua_tointeger(L, -1) < 0 || lua_tointeger(L, -1) > 14)
                bad("arc.pieces gave byte %d a layout outside 0..14", (int)b);
            lua_pop(L, 1);
            if (k == 1)
                lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    lua_pushboolean(L, 1);
    return 1;
}

/*  arc.highways(t): what part of a highway a byte is, and which way it
 *  runs. */
static int l_highways_push(lua_State *L)
{
    if (!lua_istable(L, 1))
    {
        bad("arc.highways wants a table, not %s", luaL_typename(L, 1));
        return 0;
    }
    lua_pushnil(L);
    while (lua_next(L, 1))
    {
        lua_Integer b = lua_tointeger(L, -2);
        const char *k, *a;
        if (!lua_isnumber(L, -2) || b < 0 || b > 255)
            bad("arc.highways keyed an entry on %s, not a byte of the city", luaL_typename(L, -2));
        lua_getfield(L, -1, "kind");
        k = lua_tostring(L, -1);
        if (!k || (strcmp(k, "deck") != 0 && strcmp(k, "ramp") != 0 && strcmp(k, "onramp") != 0 &&
                   strcmp(k, "curve") != 0 && strcmp(k, "junction") != 0 && strcmp(k, "over") != 0))
            bad("arc.highways gave byte %d a kind that is none of deck, ramp, onramp, curve, junction or over", (int)b);
        lua_pop(L, 1);
        lua_getfield(L, -1, "axis");
        a = lua_tostring(L, -1);
        if (!a || (strcmp(a, "ew") != 0 && strcmp(a, "ns") != 0))
            bad("arc.highways gave byte %d an axis that is neither ew nor ns", (int)b);
        lua_pop(L, 2);
    }
    lua_pushboolean(L, 1);
    return 1;
}

/*  arc.numbers(name, t): named numbers, so every value must be one.
 *  The closure's reaches are pushed this way, and a reach that is not a
 *  number is a reach of nought -- which predicts too few chunks and
 *  leaves stale triangles standing. */
static int l_numbers_push(lua_State *L)
{
    const char *name = lua_tostring(L, 1);
    if (!lua_istable(L, 2))
    {
        bad("arc.numbers(%s) wants a table, not %s", name ? name : "?", luaL_typename(L, 2));
        return 0;
    }
    lua_pushnil(L);
    while (lua_next(L, 2))
    {
        if (!lua_isnumber(L, -1))
            bad("arc.numbers(%s) gives %s a %s, not a number", name ? name : "?",
                lua_tostring(L, -2), luaL_typename(L, -1));
        lua_pop(L, 1);
    }
    lua_pushboolean(L, 1);
    return 1;
}

/*  arc.bytes(name, t [, "number"]).  A table keyed by the city's own
 *  byte, so a key outside 0..255 is a byte nothing will ever look up and
 *  nearly always a typo. */
static int l_bytes_push(lua_State *L)
{
    const char *name = lua_tostring(L, 1);
    if (!name)
        bad("arc.bytes wants a name");
    if (!lua_istable(L, 2))
    {
        bad("arc.bytes(%s) wants a table, not %s", name ? name : "?", luaL_typename(L, 2));
        return 0;
    }
    lua_pushnil(L);
    while (lua_next(L, 2))
    {
        lua_Integer b = lua_tointeger(L, -2);
        if (!lua_isnumber(L, -2) || b < 0 || b > 255)
            bad("arc.bytes(%s) keyed an entry on %s, not a byte of the city",
                name ? name : "?", luaL_typename(L, -2));
        lua_pop(L, 1);
    }
    lua_pushboolean(L, 1);
    return 1;
}

/*  arc.family.define, reading the declaration exactly as the program
 *  does and declaring nothing.  A knob, a loft kind, a tile family or a
 *  lane ending the C has no name for is reported by net_family_check;
 *  what is learned here is which stages the declaration answered with
 *  rules of its own. */
static int l_family_define(lua_State *L)
{
    const NetFamilyDecl *d;
    int                  rule[NET_HOOKS], h;
    memset(rule, 0, sizeof rule);
    if (!lua_istable(L, 1))
    {
        bad("arc.family.define wants a table");
        return 0;
    }
    d = api_family_read(L, 1);
    if (!d || net_family_check(d, rule) != 0)
    {
        bad("arc.family.define: the declaration names something the pipeline has not");
        return 0;
    }
    for (h = 0; h < NET_HOOKS; ++h)
    {
        if (!rule[h] || stage_of(d->stage[h]) >= 0)
            continue;
        if (s_n_stage >= LINT_STAGE_MAX)
            continue;
        snprintf(s_stage[s_n_stage].name, sizeof s_stage[0].name, "%s", d->stage[h]);
        s_stage[s_n_stage].stage = (NetHook)h;
        ++s_n_stage;
    }
    return 0;
}

/*  A write to a global the script never declared: nearly always a `local`
 *  left off, and in a file that is read again and again it leaks. */
static int l_global_set(lua_State *L)
{
    const char *key = lua_tostring(L, 2);
    bad("`%s` is written as a global; say `local %s`", key ? key : "?", key ? key : "?");
    lua_rawset(L, 1);
    return 0;
}

/*  Everything else the script may reach, doing nothing: a lint runs the
 *  file, so what the file calls must be there and must be harmless. */
static int l_nop(lua_State *L)
{
    (void)L;
    return 0;
}

static int l_settings(lua_State *L)
{
    lua_newtable(L);
    return 1;
}

static int l_empty_table(lua_State *L)
{
    lua_newtable(L);
    return 1;
}

static int l_false(lua_State *L)
{
    lua_pushboolean(L, 0);
    return 1;
}

static int l_zero(lua_State *L)
{
    lua_pushinteger(L, 0);
    return 1;
}

static void arc_open(lua_State *L)
{
    lua_newtable(L);
    settings_open(L, "tune", net_tune_name);
    settings_open(L, "geo", net_geo_name);
    lua_newtable(L); /* rules */
    lua_newtable(L);
    lua_pushcfunction(L, l_rule_set);
    lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "rules");
    lua_pushcfunction(L, l_nop), lua_setfield(L, -2, "log");
    lua_pushcfunction(L, l_nop), lua_setfield(L, -2, "dump");
    lua_pushcfunction(L, l_nop), lua_setfield(L, -2, "rebuild");
    lua_pushcfunction(L, l_false), lua_setfield(L, -2, "stale");
    lua_pushcfunction(L, l_false), lua_setfield(L, -2, "reload");
    lua_pushcfunction(L, l_settings), lua_setfield(L, -2, "settings");
    lua_newtable(L); /* city */
    lua_pushinteger(L, R_MAP), lua_setfield(L, -2, "size");
    lua_pushcfunction(L, l_empty_table), lua_setfield(L, -2, "tile");
    lua_pushcfunction(L, l_zero), lua_setfield(L, -2, "road_class");
    lua_setfield(L, -2, "city");
    /*  The models, defined into this state and then built: a model file
     *  is read like any other, and its build function is run so that a
     *  number it names but nothing sets is caught here. */
    lua_newtable(L);
    lua_pushcfunction(L, l_model_define), lua_setfield(L, -2, "define");
    lua_pushcfunction(L, l_empty_table), lua_setfield(L, -2, "names");
    lua_pushcfunction(L, l_empty_table), lua_setfield(L, -2, "params");
    lua_pushcfunction(L, l_empty_table), lua_setfield(L, -2, "build");
    lua_setfield(L, -2, "model");
    /*  The prop primitives and the materials, from api_put.c itself:
     *  one list, so a primitive cannot exist for the program and not
     *  for the lint.  With no mesh open each of them draws nothing. */
    api_put_open(L);
    /*  arc.bytes: the byte tables a script pushes down.  Checked and
     *  thrown away -- what matters here is that the table is one, and
     *  that every key in it is a byte the city can hold. */
    lua_pushcfunction(L, l_bytes_push), lua_setfield(L, -2, "bytes");
    lua_pushcfunction(L, l_numbers_push), lua_setfield(L, -2, "numbers");
    lua_pushcfunction(L, l_pieces_push), lua_setfield(L, -2, "pieces");
    lua_pushcfunction(L, l_highways_push), lua_setfield(L, -2, "highways");
    /*  The families, declared and CHECKED, but never entered in the
     *  registry: the lint runs beside a program that may be drawing. */
    lua_newtable(L);
    lua_pushcfunction(L, l_family_define), lua_setfield(L, -2, "define");
    lua_pushcfunction(L, l_empty_table), lua_setfield(L, -2, "list");
    /*  arc.family.rules: the numbers a family is drawn by, pushed.  A
     *  table is all this can check -- what is IN it is checked where the
     *  rule that builds it is called. */
    lua_pushcfunction(L, l_nop), lua_setfield(L, -2, "rules");
    lua_setfield(L, -2, "family");
    lua_newtable(L); /* mesh */
    lua_pushcfunction(L, l_empty_table), lua_setfield(L, -2, "crossings");
    lua_pushcfunction(L, l_empty_table), lua_setfield(L, -2, "walkways");
    lua_pushcfunction(L, l_empty_table), lua_setfield(L, -2, "faults");
    lua_pushcfunction(L, l_nop), lua_setfield(L, -2, "probe");
    lua_setfield(L, -2, "mesh");
    lua_setglobal(L, "arc");
}

/*  What the pipeline hands a rule a FAMILY named for one of its stages,
 *  and how many answers it takes back.  The three that decide are asked
 *  with plain numbers; a stage handed the strip itself answers -1 here,
 *  since a lint has no strip to hand it. */
static int stage_args(lua_State *L, NetHook h, int *nargs)
{
    switch (h)
    {
    case NH_CONTROL:
        lua_pushinteger(L, 64), lua_pushinteger(L, 64), lua_pushinteger(L, 15);
        *nargs = 3;
        return 1;
    case NH_FLIES:
        lua_pushnumber(L, 1.5);
        *nargs = 1;
        return 1;
    case NH_TRAFFIC:
        lua_pushinteger(L, 1);
        *nargs = 1;
        return 2;
    default: *nargs = 0; return -1;
    }
}

/*  Every stage rule a family named, asked once with the shape its stage
 *  is handed and read for the shape its stage takes back. */
static void stages_check(lua_State *L)
{
    int i;
    for (i = 0; i < s_n_stage; ++i)
    {
        int nargs = 0, nres = stage_args(L, s_stage[i].stage, &nargs), rc;
        if (nres < 0)
            continue;
        lua_pop(L, nargs);
        lua_getglobal(L, "arc");
        lua_getfield(L, -1, "rules");
        lua_getfield(L, -1, s_stage[i].name);
        if (!lua_isfunction(L, -1))
        {
            bad("arc.family: the %s stage names %s, which is neither a primitive nor a rule",
                NET_HOOK_NAME[s_stage[i].stage], s_stage[i].name);
            lua_pop(L, 3);
            continue;
        }
        stage_args(L, s_stage[i].stage, &nargs);
        api_rule_watch(L, 1);
        rc = lua_pcall(L, nargs, nres, 0);
        api_rule_watch(L, 0);
        if (rc != LUA_OK)
        {
            bad("arc.rules.%s: %s", s_stage[i].name, lua_tostring(L, -1));
            lua_pop(L, 3);
            continue;
        }
        if (s_stage[i].stage == NH_FLIES ? !lua_isboolean(L, -1) : !lua_isnumber(L, -1))
            bad("arc.rules.%s answers the %s stage: it must give %s, not %s", s_stage[i].name,
                NET_HOOK_NAME[s_stage[i].stage], s_stage[i].stage == NH_FLIES ? "a boolean" : "numbers",
                luaL_typename(L, -1));
        lua_pop(L, nres + 2);
    }
}

/*  The argument the pipeline hands each rule, so its body is exercised
 *  rather than merely defined. */
static void rule_args(lua_State *L, const char *rule, int *nargs)
{
    if (strcmp(rule, "control") == 0)
    {
        int e;
        lua_newtable(L);
        lua_pushinteger(L, 64), lua_setfield(L, -2, "col");
        lua_pushinteger(L, 64), lua_setfield(L, -2, "row");
        lua_pushinteger(L, 15), lua_setfield(L, -2, "links");
        lua_pushboolean(L, 1), lua_setfield(L, -2, "busy");
        lua_newtable(L);
        for (e = 1; e <= 4; ++e)
        {
            lua_newtable(L);
            lua_pushinteger(L, e - 1), lua_setfield(L, -2, "class");
            lua_pushinteger(L, 128), lua_setfield(L, -2, "traffic");
            lua_rawseti(L, -2, e);
        }
        lua_setfield(L, -2, "arms");
        *nargs = 1;
    }
    else if (strcmp(rule, "crossing_at") == 0)
    {
        lua_newtable(L);
        lua_pushinteger(L, 64), lua_setfield(L, -2, "col");
        lua_pushinteger(L, 64), lua_setfield(L, -2, "row");
        lua_pushinteger(L, 0), lua_setfield(L, -2, "arm");
        lua_pushinteger(L, 2), lua_setfield(L, -2, "control");
        lua_pushboolean(L, 1), lua_setfield(L, -2, "pavement");
        lua_pushnumber(L, -1.0), lua_setfield(L, -2, "cos");
        lua_pushnumber(L, 3.0), lua_setfield(L, -2, "span");
        *nargs = 1;
    }
    else if (strcmp(rule, "crossing") == 0)
    {
        lua_newtable(L);
        lua_pushinteger(L, 64), lua_setfield(L, -2, "col");
        lua_pushinteger(L, 64), lua_setfield(L, -2, "row");
        lua_pushinteger(L, 0), lua_setfield(L, -2, "arm");
        lua_pushinteger(L, 2), lua_setfield(L, -2, "control");
        lua_pushnumber(L, 0.20), lua_setfield(L, -2, "want");
        lua_pushnumber(L, 1.50), lua_setfield(L, -2, "room");
        lua_pushnumber(L, 0.80), lua_setfield(L, -2, "straight");
        *nargs = 1;
    }
    else if (strcmp(rule, "lanes") == 0)
    {
        lua_pushstring(L, "road");
        lua_pushinteger(L, 1);
        *nargs = 2;
    }
    else if (strcmp(rule, "lamps") == 0)
    {
        lua_pushnumber(L, 1.0);
        lua_pushnumber(L, 6.0);
        *nargs = 2;
    }
    else if (strcmp(rule, "water_tiles") == 0 ||
             strcmp(rule, "slope_codes") == 0 || strcmp(rule, "built_tiles") == 0 ||
             strcmp(rule, "sloped_tiles") == 0 || strcmp(rule, "structure_tints") == 0 ||
             strcmp(rule, "building_tiles") == 0 || strcmp(rule, "elevated_tiles") == 0 ||
             strcmp(rule, "levelling_tiles") == 0 || strcmp(rule, "saddle_tiles") == 0 ||
             strcmp(rule, "open_tiles") == 0 ||
             strcmp(rule, "standing_tiles") == 0 ||
             strcmp(rule, "carrier_tiles") == 0 ||
             strcmp(rule, "rail_crossing_tiles") == 0)
        *nargs = 0;
    else if (strcmp(rule, "car_hold") == 0)
    {
        lua_newtable(L);
        lua_pushnumber(L, 0.4), lua_setfield(L, -2, "ahead");
        lua_pushboolean(L, 1), lua_setfield(L, -2, "held");
        lua_pushnumber(L, 0.1), lua_setfield(L, -2, "line");
        lua_pushnumber(L, 2.0), lua_setfield(L, -2, "speed");
        lua_pushnumber(L, 0.016), lua_setfield(L, -2, "step");
        *nargs = 1;
    }
    else if (strcmp(rule, "car_follow") == 0)
    {
        lua_newtable(L);
        lua_pushnumber(L, 0.3), lua_setfield(L, -2, "gap");
        lua_pushnumber(L, 2.0), lua_setfield(L, -2, "speed");
        lua_pushnumber(L, 0.1), lua_setfield(L, -2, "stop");
        lua_pushnumber(L, 0.6), lua_setfield(L, -2, "free");
        *nargs = 1;
    }
    else if (strcmp(rule, "band_start") == 0)
    {
        lua_newtable(L);
        lua_pushboolean(L, 0), lua_setfield(L, -2, "back");
        lua_pushboolean(L, 1), lua_setfield(L, -2, "on");
        *nargs = 1;
    }
    else if (strcmp(rule, "ramp_span") == 0)
    {
        lua_newtable(L);
        lua_pushnumber(L, 4.0), lua_setfield(L, -2, "at");
        lua_pushinteger(L, 2), lua_setfield(L, -2, "len");
        lua_pushboolean(L, 1), lua_setfield(L, -2, "leaves");
        lua_pushinteger(L, 1), lua_setfield(L, -2, "sgn");
        *nargs = 1;
    }
    else if (strcmp(rule, "seg_class") == 0)
    {
        if (luaL_dostring(L, "return {3, 5, 1}") != LUA_OK)
        {
            bad("the lint's own classes: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "ramp_side") == 0)
    {
        lua_newtable(L);
        lua_pushboolean(L, 1), lua_setfield(L, -2, "free");
        lua_pushinteger(L, 2), lua_setfield(L, -2, "room");
        lua_pushinteger(L, 3), lua_setfield(L, -2, "room_back");
        *nargs = 1;
    }
    else if (strcmp(rule, "ramp_share") == 0)
    {
        lua_newtable(L);
        lua_pushnumber(L, 3.0), lua_setfield(L, -2, "gap");
        lua_pushinteger(L, 3), lua_setfield(L, -2, "cap");
        *nargs = 1;
    }
    else if (strcmp(rule, "ramp_fork") == 0)
    {
        lua_newtable(L);
        lua_pushboolean(L, 0), lua_setfield(L, -2, "straight");
        lua_pushboolean(L, 1), lua_setfield(L, -2, "along");
        lua_pushboolean(L, 1), lua_setfield(L, -2, "against");
        *nargs = 1;
    }
    else if (strcmp(rule, "fit_choice") == 0)
    {
        if (luaL_dostring(L, "return {family = 'road',"
                             " free = {corners = 1, tight = 2, nodes = 9},"
                             " held = {corners = 1, tight = 2, nodes = 9}}") != LUA_OK)
        {
            bad("the lint's own fit choice: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "after") == 0)
    {
        lua_newtable(L);
        lua_pushboolean(L, 1), lua_setfield(L, -2, "met");
        lua_pushboolean(L, 1), lua_setfield(L, -2, "free");
        lua_pushnumber(L, 1.0), lua_setfield(L, -2, "ahead");
        lua_pushnumber(L, 2.0), lua_setfield(L, -2, "reach");
        *nargs = 1;
    }
    else if (strcmp(rule, "path") == 0)
    {
        /*  A fit of two boundaries, each of which crosses and has a line
         *  after it, so both the after and the join arms are run. */
        if (luaL_dostring(L,
                          "return {kind = 'path',"
                          " runs = function () return nil end,"
                          " chain = function () return nil end,"
                          " lined = function () return 2 end,"
                          " pairs = function () return 2 end,"
                          " pair = function () return {has_after = true, met = true, free = false,"
                          "   ahead = 1.0, reach = 2.0}, {cross = true, free = false} end,"
                          " after_is = function () end,"
                          " try = function () return nil end,"
                          " held = function () return true end,"
                          " ending = function () return nil end}") != LUA_OK)
            lua_pop(L, 1), lua_pushnil(L);
        *nargs = 1;
    }
    else if (strcmp(rule, "join") == 0)
    {
        lua_newtable(L);
        lua_pushboolean(L, 1), lua_setfield(L, -2, "cross");
        lua_pushboolean(L, 0), lua_setfield(L, -2, "free");
        *nargs = 1;
    }
    else if (strcmp(rule, "fit") == 0)
    {
        /*  A path of four vertices with a bend in it, one of them a
         *  biarc's, so both arms of the radius stage are run. */
        if (luaL_dostring(L,
                          "local n = 4\n"
                          "return {kind = 'fit',"
                          " info = function () return {n = n, reserve0 = 0.4, reserve1 = 0.4,"
                          "   rmax = 6.0, rmin = 0.9, band = 0.3, share = 0.45, trim_cap = 0.3} end,"
                          " at = function (_, k)"
                          "   local p = {{64,64,-1},{66,64,-1},{68,66,0.2},{70,66,-1}}\n"
                          "   if k < 0 or k >= n then return end"
                          "   return p[k + 1][1], p[k + 1][2], p[k + 1][3] end,"
                          " drop = function () n = n - 1; return n end,"
                          " corner = function () end,"
                          " sweep = function () return nil end,"
                          " swept = function () return 1.5, false end,"
                          " demand = function () return 0.5 end,"
                          " need = function () return 0.5 end,"
                          " tally = function () end}") != LUA_OK)
        {
            bad("the lint's own fit: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "runs") == 0)
    {
        /*  A chain long enough to hold a straight, a slope and a free
         *  line, with both ends a junction's mouth so the guard on them
         *  is walked too. */
        if (luaL_dostring(L,
                          "local ns = 8\n"
                          "return {kind = 'runs',"
                          " info = function () return {ns = ns, free = true, ex0 = true, ex1 = true, band = 0.3} end,"
                          " spread = function () return -0.1, 0.1 end,"
                          " chord = function () end,"
                          " note = function () end,"
                          " step = function (_, k) return k % 3, true end,"
                          " perp = function () return true end,"
                          " slope = function () end,"
                          " try = function (_, i, j) return j - i < 5 end,"
                          " keep = function () end,"
                          " emit = function () end,"
                          " order = function () end}") != LUA_OK)
        {
            bad("the lint's own runs: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "chain") == 0)
    {
        /*  Two runs, the first reaching the chain's start and the last
         *  its goal, so both ends are asked about. */
        if (luaL_dostring(L,
                          "return {kind = 'chain',"
                          " info = function () return {nr = 2, ex0 = true, ex1 = true} end,"
                          " run = function (_, i) return {kind = 1, first = i == 0, last = i == 1} end,"
                          " aim = function () end,"
                          " on_line = function () return false end,"
                          " add_end = function () end,"
                          " add = function () end}") != LUA_OK)
        {
            bad("the lint's own chain: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "meet") == 0)
    {
        /*  A crossing a little ahead of both lines, on a free line so
         *  that both slack rules are walked. */
        if (luaL_dostring(L,
                          "return {kind = 'meet',"
                          " info = function () return {ahead = 0.4, behind = 0.4, reach = 3.0,"
                          "   reach_on = 3.0, free = true, first = true, last = true,"
                          "   len_in = 2.0, len_out = 2.0, fixed_prev = -1.0, reserve0 = 0.4,"
                          "   reserve1 = 0.4, need = 0.5, share = 0.45, trim_cap = 0.3} end,"
                          " holds = function () return true end,"
                          " covers = function () return true end,"
                          " arc = function () return nil end,"
                          " swept = function () return 1.2 end,"
                          " legs = function () return true end,"
                          " place = function () end}") != LUA_OK)
        {
            bad("the lint's own join: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "bridge") == 0)
    {
        /*  A pair with room either side and a placing that never holds,
         *  so both the symmetric sweep and every slide are walked. */
        if (luaL_dostring(L,
                          "return {kind = 'bridge',"
                          " info = function () return {len_in = 2.0, len_out = 2.0, fixed_prev = -1.0,"
                          "   gap = 1.0, reserve0 = 0.4, reserve1 = 0.4,"
                          "   head = false, tail = false, first = false, last = false,"
                          "   share = 0.45, band = 0.3, margin = 0.02, padded = true} end,"
                          " solve = function () return nil end,"
                          " refuse = function () end,"
                          " holds = function () return -1.0 end,"
                          " result = function () end,"
                          " note = function () end,"
                          " keep = function () end,"
                          " place = function () end}") != LUA_OK)
        {
            bad("the lint's own bridge: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "step") == 0)
    {
        /*  One gap point between two runs, square to both, so the jog
         *  arm and the plain walk are both reached. */
        if (luaL_dostring(L,
                          "return {kind = 'step',"
                          " info = function () return {gap = 1, head = true, tail = true, half = 0.25} end,"
                          " inline = function (_, s) return s == 'behind' end,"
                          " jog = function () return true end,"
                          " diagonal = function () return false end,"
                          " place = function () end,"
                          " point = function () end}") != LUA_OK)
        {
            bad("the lint's own walk: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "sweep") == 0)
    {
        /*  A right-angle corner with a tangent to spare and nothing that
         *  ever holds, so the whole search down to the floor is walked. */
        if (luaL_dostring(L,
                          "return {kind = 'sweep',"
                          " info = function () return {straight = false, tan_half = 1.0, tangent = 1.2,"
                          "   rmax = 6.0, rmin = 0.9, half = 0.25, margin = 0.02, padded = true} end,"
                          " holds = function () return false end,"
                          " answer = function () end}") != LUA_OK)
        {
            bad("the lint's own sweep: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "pieces") == 0)
    {
        /*  Four vertices: one corner with room and one with none, so
         *  both arms of the cut are walked. */
        if (luaL_dostring(L,
                          "return {kind = 'pieces',"
                          " info = function () return {n = 4} end,"
                          " corner = function (_, i) return {radius = i == 1 and 1.2 or 0.0,"
                          "   tangent = 0.8, tan_half = 1.0, room = 0.9, leaving = 1.0} end,"
                          " straight = function () end,"
                          " arc = function () end,"
                          " tail = function () end}") != LUA_OK)
        {
            bad("the lint's own pieces: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "stair") == 0)
    {
        /*  Six cells, every other one a block turning the other way, so
         *  a staircase is found and the chain takes both shapes. */
        if (luaL_dostring(L,
                          "local n = 6\n"
                          "return {kind = 'stair',"
                          " info = function () return {n = n, gap = 2} end,"
                          " block = function (_, i) return i % 2 == 0 end,"
                          " turn = function (_, i) return i % 4 == 0 and 1 or -1 end,"
                          " pinned = function () return false end,"
                          " point = function () end,"
                          " centre = function () end}") != LUA_OK)
        {
            bad("the lint's own stair: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "profile") == 0)
    {
        /*  A deck long enough for the closing to run, with a window and
         *  a taper at each end. */
        if (luaL_dostring(L,
                          "local n = 8\n"
                          "return {kind = 'profile',"
                          " info = function () return {n = n, total = 7.0, ramp = false,"
                          "   lane_piece = false, lane_off = false, flat = false, deck_above = 1.0,"
                          "   taper0 = 1.0, taper1 = 1.0, grade = 0.3, stiff = 2.0, lift = 1.5} end,"
                          " at = function (_, i) return i - 1.0, 4.0 end,"
                          " set = function () end,"
                          " ease = function (_, t) return t end}") != LUA_OK)
        {
            bad("the lint's own profile: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "slide") == 0)
    {
        /*  A slide with room along both, whose placings never route, so
         *  every arm of the walk is reached. */
        if (luaL_dostring(L,
                          "return {kind = 'slide',"
                          " info = function () return {reach = 2.0, merge = 1.0, taper = 0.6} end,"
                          " route = function (_, u, at) if at > 0.5 then return nil, 'off' end"
                          "   return nil, 'unroutable' end,"
                          " exits = function () return false end,"
                          " keep = function () end,"
                          " note = function () end}") != LUA_OK)
        {
            bad("the lint's own slide: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "drop") == 0)
    {
        /*  A short deck with two ramps on it, near enough to be each
         *  other's partner, so every arm of the taper is walked. */
        if (luaL_dostring(L,
                          "return {kind = 'drop',"
                          " info = function () return {n = 8, ramps = 2, reach = 2.0, narrow = 0.7} end,"
                          " station = function (_, i) return {at = i - 1.0, x = i - 1.0, y = 0.0,"
                          "   dx = 1.0, dy = 0.0} end,"
                          " ramp = function (_, r) return {x = r + 1.0, y = 0.5, tx = r + 1.0, ty = 1.0,"
                          "   ax = 1.0, ay = 0.0, len = 2, leaves = r == 1} end,"
                          " clear = function () end,"
                          " width = function () end,"
                          " gore = function () end}") != LUA_OK)
        {
            bad("the lint's own drop: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "ground") == 0)
    {
        /*  A strip pinned at both ends with a level crossing along it,
         *  so every anchor arm is walked. */
        if (luaL_dostring(L,
                          "return {kind = 'ground',"
                          " info = function () return {n = 6, total = 5.0, pin0 = true, pin1 = true,"
                          "   dead0 = false, dead1 = false, reaches_node = true} end,"
                          " at = function (_, i) return i - 1.0, 4.0 end,"
                          " node = function () return 4.5 end,"
                          " crossing = function (_, i) return i == 2 and 4.2 or nil end,"
                          " set = function () end}") != LUA_OK)
        {
            bad("the lint's own ground: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "orient") == 0)
    {
        /*  A ramp with a deck on its north side and a road to its east,
         *  which is the shape every arm of the reading is written for. */
        if (luaL_dostring(L,
                          "return {kind = 'orient',"
                          " side = function (_, k) return k == 0, k == 0, k == 1 end,"
                          " answer = function () end}") != LUA_OK)
        {
            bad("the lint's own orient: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "shelf") == 0)
    {
        /*  A small grid with one node on it, whose corner has two copies
         *  from two corridors, so both rules are walked. */
        if (luaL_dostring(L,
                          "return {kind = 'shelf',"
                          " info = function () return {n = 2, nodes = 1} end,"
                          " copies = function () return 1, 0.3, 4.0, 2, 0.1, 4.5 end,"
                          " set = function () end,"
                          " node = function () return 1, 1 end,"
                          " heights = function () return 4.0, 4.5 end,"
                          " node_set = function () end}") != LUA_OK)
        {
            bad("the lint's own shelf: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "cross") == 0)
    {
        /*  Two lanes end to end a little apart, so both the merge and
         *  the link arms are reachable. */
        if (luaL_dostring(L,
                          "return {kind = 'cross',"
                          " info = function () return {n = 2} end,"
                          " open = function (_, i) return i == 0 end,"
                          " measure = function (_, a, b) if a == b then return nil end"
                          "   return 0.0, 1.0, 0.3, 0.0, 0.3 end,"
                          " merge = function () end,"
                          " link = function () return true end}") != LUA_OK)
        {
            bad("the lint's own xlane: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "band") == 0)
    {
        /*  A square ring with one arm's mouth on its first edge. */
        if (luaL_dostring(L,
                          "return {kind = 'band',"
                          " info = function () return {n = 4, width = 0.05, col = 64, row = 64} end,"
                          " at = function (_, i) local q = {{64,64},{65,64},{65,65},{64,65}}"
                          "   return q[i + 1][1], q[i + 1][2] end,"
                          " arm = function (_, e) return e == 0, 64, 64, 65, 64 end,"
                          " edge = function () end,"
                          " inset = function () end,"
                          " close = function () end}") != LUA_OK)
        {
            bad("the lint's own band: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "outline") == 0)
    {
        /*  A junction that stands for every junction: four arms on the
         *  tile's own axes, which is the square every outline starts
         *  from. */
        if (luaL_dostring(L,
                          "local pts = 0\n"
                          "return {kind = 'outline',"
                          " info = function () return {col = 64, row = 64, x = 64.5, y = 64.5,"
                          "   half = 0.25, far = 0.8, grow = 1.0, cap = 0.45, curbs = true, n = 4} end,"
                          " arm = function (_, i)"
                          "   local d = {{0,-1},{1,0},{0,1},{-1,0}}\n"
                          "   return 64.5 + d[i + 1][1] * 0.25, 64.5 + d[i + 1][2] * 0.25,"
                          "          d[i + 1][1], d[i + 1][2], math.atan(d[i + 1][2], d[i + 1][1]), i end,"
                          " order = function () end,"
                          " trim = function () end,"
                          " clamped = function () end,"
                          " count = function () return pts end,"
                          " back = function () return 1.0 end,"
                          " point = function () pts = pts + 1 end,"
                          " close = function () end}") != LUA_OK)
        {
            bad("the lint's own outline: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "panel") == 0)
    {
        if (luaL_dostring(L,
                          "return {kind = 'panel',"
                          " info = function () return {order = 100, lift = 0.01, slot = 0.06} end,"
                          " at = function (_, k) return 64 + k * 0.1, 64.5, 5.0 end,"
                          " quad = function () return true end}") != LUA_OK)
        {
            bad("the lint's own crossing panel: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "lane") == 0)
    {
        /*  A line of two pieces, a straight and an arc. */
        if (luaL_dostring(L,
                          "return {kind = 'lane',"
                          " info = function () return {n = 2, lift = 0.02, paint = 5, band = 0,"
                          "   ramp = false, off = false, step = 0.2} end,"
                          " piece = function (_, k) return 1.5, k == 1 end,"
                          " at = function (_, _, t) return 64.0 + t, 64.5, 1, 0 end,"
                          " height = function () return 5.0 end,"
                          " order = function () return 100 end,"
                          " wire = function () return true end}") != LUA_OK)
        {
            bad("the lint's own lane: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "tile") == 0 || strcmp(rule, "zone_tint") == 0)
    {
        /*  A tile that stands for every tile: a field with a wall down
         *  to the east, at the map's northern rim. */
        if (luaL_dostring(L,
                          "return {kind = 'tile',"
                          " info = function () return {col = 64, row = 0, xter = 0, xbld = 0,"
                          "   code = 0, order = 100, kind = 0, wet = false, underground = false,"
                          "   corridor = false, surface = 5.0, zone = 1} end,"
                          " at = function (_, k) return 64 + (k % 2), k // 3, 5.0, 4.0, 5.0 end,"
                          " colour = function () return 0.0, 0.0, 1.0 end,"
                          " edge = function (_, e) return e ~= 0, 4.5, 4.5, false, 0, false,"
                          "   1, 2, e == 1, false end,"
                          " normal = function () return 1.0, 0.0 end,"
                          " top = function () return true end,"
                          " wall = function () return true end,"
                          " wall_r = function () return true end,"
                          " glass = function () return true end,"
                          " tri = function () return true end,"
                          " walled = function () end}") != LUA_OK)
        {
            bad("the lint's own tile: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "footway") == 0 || strcmp(rule, "walk_curves") == 0)
    {
        /*  A band that stands for every band: three stations of a
         *  pavement beside a road. */
        if (luaL_dostring(L,
                          "return {kind = 'footway',"
                          " info = function () return {band = 'side', n = 3, width = 0.05,"
                          "   asked = 0.3, order = 100, drape = true, col = 64, row = 64} end,"
                          " count = function () return 3 end,"
                          " at = function (_, k) return 64.0 + k * 0.1, 64.0, 64.0 + k * 0.1,"
                          "   64.05, 5.0 end,"
                          " quad = function () return true end,"
                          " ends = function () return 64, 64, 65, 64, 5.0, 5.0 end,"
                          " wire = function () return true end}") != LUA_OK)
        {
            bad("the lint's own footway: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "junction") == 0)
    {
        /*  A junction that stands for every junction: a square outline
         *  round a levelled tile. */
        if (luaL_dostring(L,
                          "return {kind = 'junction',"
                          " info = function () return {x = 64.5, y = 64.5, z = 5.0, mat = 7,"
                          "   order = 100, n = 4, col = 64, row = 64} end,"
                          " count = function () return 4 end,"
                          " at = function (_, i) return 64.0 + (i % 2), 64.0 + (i > 2 and 1 or 0) end,"
                          " surface = function () return 5.0 end,"
                          " tri = function () return true end,"
                          " quad = function () return true end}") != LUA_OK)
        {
            bad("the lint's own junction: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "strip") == 0 || strcmp(rule, "walks") == 0 || strcmp(rule, "curves") == 0)
    {
        /*  A strip that stands for every strip: four stations of a road
         *  with footways, so the composition is actually run and a name
         *  it reaches for that nothing sets is a fault here. */
        if (luaL_dostring(L,
                          "return {kind = 'strip',"
                          " info = function () return {family = 'road', class = 0, half = 0.25,"
                          "   mat = 7, len = 1.5, n = 4, flies = false, curbs = true,"
                          "   deck = false, cross0 = 0, cross1 = 0, slot = 'slot_strip',"
                          "   flat = false, lane_piece = false, structure = false,"
                          "   girder = 0.11, parapet = 0.045} end,"
                          " count = function () return 4 end,"
                          " at = function (_, i) return 64.0 + i * 0.5, 64.5, 5.0, 1, 0,"
                          "   (i - 1) * 0.5, 1, 1, 9, 5.0 end,"
                          " width = function () return 1 end,"
                          " order = function () return 100 end,"
                          " ground = function () return 5.0 end,"
                          " road_class = function () return 0 end,"
                          " class = function () end,"
                          " extras = function () return true end,"
                          " quad = function () return true end,"
                          " walk_at = function () return true end,"
                          " walk_ends = function () end,"
                          " lane = function () return 0, 5.0, 5.0 end,"
                          " tri_n = function () return true end,"
                          " wall = function () return true end,"
                          " edge = function () return true end,"
                          " pieces = function () return 2 end,"
                          " piece = function (_, k) return 0.75, k == 1 end,"
                          " piece_at = function (_, _, t) return 64.0 + t, 64.5, 1, 0 end,"
                          " box = function () return true end}") != LUA_OK)
        {
            bad("the lint's own strip: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "world") == 0)
    {
        /*  The build itself.  Its primitives draw nothing here -- there
         *  is no mesh -- so what this exercises is the DRIVE: the loops,
         *  the bounds it reads out of info, and every name it reaches
         *  for on the handle. */
        if (luaL_dostring(L,
                          "return {kind = 'world',"
                          " info = function () return {size = 8, pass = 2,"
                          "   roads = true, underground = false} end,"
                          " wanted = function () return true end,"
                          " shape = function () end,"
                          " tile = function () return nil end,"
                          " zone = function () return nil end,"
                          " power = function () return nil end,"
                          " footways = function () return 0, false end,"
                          " footway = function () return nil end,"
                          " junctions = function () return 0 end,"
                          " junction = function () return nil end,"
                          " junction_ring = function () end,"
                          " trims = function () return 0 end,"
                          " shelf = function () return nil end,"
                          " controls = function () return 0 end,"
                          " control = function () return nil end,"
                          " control_is = function () end,"
                          " xwalk = function () return nil end,"
                          " xwalk_deep = function () end,"
                          " crossing = function () return nil end,"
                          " crossing_frame = function () end,"
                          " crossing_panel = function () return nil end,"
                          " crossing_approaches = function () return true end,"
                          " crossing_approach = function () return nil end,"
                          " crossing_mark = function () return true end,"
                          " networks_draw = function () return true end,"
                          " net_families = function () return 0 end,"
                          " junction_boxes = function () end,"
                          " junction_box = function () return nil end,"
                          " junction_box_done = function () end,"
                          " box_lofts = function () return 0 end,"
                          " box_loft = function () end,"
                          " segments = function () end,"
                          " segment = function () return false end,"
                          " segment_done = function () end,"
                          " networks_drawn = function () end,"
                          " emitted = function () end,"
                          " lanes = function () return true end,"
                          " lane_runs = function () return 0 end,"
                          " lane_run = function () return 'road', 0 end,"
                          " lane_run_is = function () end,"
                          " traffic_runs = function () return 0 end,"
                          " traffic_run = function () return nil end,"
                          " traffic_run_is = function () end,"
                          " networks = function () return true end,"
                          " seg_classes = function () return 0 end,"
                          " fits = function () return 0 end,"
                          " fit = function () return nil end,"
                          " fit_done = function () return nil end,"
                          " fit_choice_is = function () end,"
                          " seg_class = function () return nil end,"
                          " seg_class_is = function () end,"
                          " loft_taper = function () return nil end,"
                          " loft_profile = function () return nil end,"
                          " loft_dropped = function () return nil end,"
                          " loft_works = function () return nil end,"
                          " loft_record = function () return nil end,"
                          " loft_record_is = function () end,"
                          " loft_furniture = function () return nil end,"
                          " loft_furniture_is = function () end,"
                          " loft_recorded = function () end,"
                          " curves = function () return nil end,"
                          " strip = function () return nil end,"
                          " strip_done = function () end,"
                          " hiway_band = function () return false end,"
                          " band_start_is = function () end,"
                          " ramp_fork_is = function () end,"
                          " orients = function () return 0 end,"
                          " orient = function () return nil end,"
                          " ramp_sides = function () return 0 end,"
                          " ramp_side = function () return nil end,"
                          " ramp_side_is = function () end,"
                          " ramp_shares = function () return 0 end,"
                          " ramp_share = function () return nil end,"
                          " ramp_share_is = function () end,"
                          " ramp_spans = function () return 0 end,"
                          " ramp_span = function () return nil end,"
                          " ramp_span_is = function () end,"
                          " hiway_chain = function () return nil end,"
                          " hiway_band_chained = function () end,"
                          " hw_fits = function () return 0 end,"
                          " hw_fit = function () return nil end,"
                          " hw_fit_done = function () end,"
                          " hw_fit_choice = function () return nil end,"
                          " hw_fit_choice_is = function () end,"
                          " hiway_band_fitted = function () end,"
                          " hiway_band_done = function () end,"
                          " highway_ramps = function () return true end,"
                          " ramp_next = function () return false end,"
                          " ramp_slide = function () return nil end,"
                          " ramp_done = function () end,"
                          " ramp_lofts = function () return 0 end,"
                          " ramp_loft = function () end,"
                          " wires = function () return 0 end,"
                          " wire = function () return nil end,"
                          " wire_done = function () end,"
                          " lane_cross = function () return nil end,"
                          " highway_links = function () return true end,"
                          " highways = function () return true end}") != LUA_OK)
        {
            bad("the lint's own world: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "family") == 0)
    {
        lua_newtable(L);
        lua_pushstring(L, "road"), lua_setfield(L, -2, "name");
        lua_pushnumber(L, 0.50), lua_setfield(L, -2, "width");
        *nargs = 1;
    }
    else if (strcmp(rule, "crossing_frame") == 0)
    {
        lua_newtable(L);
        lua_pushinteger(L, 64), lua_setfield(L, -2, "col");
        lua_pushinteger(L, 64), lua_setfield(L, -2, "row");
        lua_pushnumber(L, 1.0), lua_setfield(L, -2, "sin");
        lua_pushnumber(L, 0.50), lua_setfield(L, -2, "road");
        lua_pushnumber(L, 0.44), lua_setfield(L, -2, "rail");
        *nargs = 1;
    }
    else if (strcmp(rule, "crossing_marks") == 0)
    {
        lua_newtable(L);
        lua_pushnumber(L, 0.30), lua_setfield(L, -2, "reach");
        lua_pushnumber(L, 0.31), lua_setfield(L, -2, "mast");
        lua_pushnumber(L, 2.00), lua_setfield(L, -2, "limit");
        lua_pushnumber(L, 0.50), lua_setfield(L, -2, "road");
        lua_pushnumber(L, 64.5), lua_setfield(L, -2, "x");
        lua_pushnumber(L, 64.5), lua_setfield(L, -2, "y");
        lua_pushnumber(L, 1.0), lua_setfield(L, -2, "fx");
        lua_pushnumber(L, 0.0), lua_setfield(L, -2, "fy");
        lua_pushnumber(L, 0.0), lua_setfield(L, -2, "gx");
        lua_pushnumber(L, -1.0), lua_setfield(L, -2, "gy");
        *nargs = 1;
    }
    else if (strcmp(rule, "gate") == 0)
    {
        lua_newtable(L);
        lua_pushnumber(L, 45.0), lua_setfield(L, -2, "angle");
        lua_pushnumber(L, 3.0), lua_setfield(L, -2, "near");
        lua_pushnumber(L, 0.016), lua_setfield(L, -2, "dt");
        *nargs = 1;
    }
    else if (strcmp(rule, "rail_marks") == 0)
    {
        lua_newtable(L);
        lua_pushnumber(L, 24.0), lua_setfield(L, -2, "len");
        lua_pushboolean(L, 1), lua_setfield(L, -2, "ahead");
        lua_pushboolean(L, 1), lua_setfield(L, -2, "behind");
        lua_newtable(L);
        lua_pushnumber(L, 8.0), lua_rawseti(L, -2, 1);
        lua_setfield(L, -2, "crossings");
        *nargs = 1;
    }
    else if (strcmp(rule, "corner") == 0)
    {
        lua_newtable(L);
        lua_pushinteger(L, 64), lua_setfield(L, -2, "col");
        lua_pushinteger(L, 64), lua_setfield(L, -2, "row");
        lua_pushnumber(L, 1.5707963), lua_setfield(L, -2, "phi");
        lua_pushnumber(L, 1.0), lua_setfield(L, -2, "grow");
        lua_pushnumber(L, 0.12), lua_setfield(L, -2, "width");
        lua_pushnumber(L, 0.40), lua_setfield(L, -2, "room");
        lua_pushnumber(L, 0.50), lua_setfield(L, -2, "back");
        lua_pushnumber(L, 0.50), lua_setfield(L, -2, "fwd");
        *nargs = 1;
    }
    else
    {
        /*  A prop: where it stands and which way it faces.  Nothing it
         *  draws reaches a mesh, since the lint opens none. */
        lua_newtable(L);
        lua_pushnumber(L, 64.5), lua_setfield(L, -2, "x");
        lua_pushnumber(L, 64.5), lua_setfield(L, -2, "y");
        lua_pushnumber(L, 5.0), lua_setfield(L, -2, "z");
        lua_pushnumber(L, 1.0), lua_setfield(L, -2, "fx");
        lua_pushnumber(L, 0.0), lua_setfield(L, -2, "fy");
        lua_pushnumber(L, 0.25), lua_setfield(L, -2, "size");
        lua_pushinteger(L, 64), lua_setfield(L, -2, "col");
        lua_pushinteger(L, 64), lua_setfield(L, -2, "row");
        lua_pushinteger(L, 0), lua_setfield(L, -2, "arm");
        lua_pushinteger(L, 15), lua_setfield(L, -2, "links");
        lua_pushnumber(L, 0.0), lua_setfield(L, -2, "phase");
        lua_pushnumber(L, 45.0), lua_setfield(L, -2, "angle");
        lua_pushnumber(L, 0.30), lua_setfield(L, -2, "len");
        *nargs = 1;
    }
}

/*  What a rule answered, against what its caller reads. */
static void rule_answer(lua_State *L, const char *rule)
{
    if (lua_isnil(L, -1))
        return; /* nil is "the C decides", which every rule may answer */
    if (strcmp(rule, "control") == 0)
    {
        int e;
        if (!lua_istable(L, -1))
        {
            bad("arc.rules.control must answer a table of four, not %s", luaL_typename(L, -1));
            return;
        }
        for (e = 1; e <= 4; ++e)
        {
            lua_rawgeti(L, -1, e);
            if (!lua_isnumber(L, -1))
                bad("arc.rules.control answered no code for arm %d", e - 1);
            else if (lua_tointeger(L, -1) < 0 || lua_tointeger(L, -1) > 2)
                bad("arc.rules.control answered %d for arm %d: 0 none, 1 stop, 2 signal",
                    (int)lua_tointeger(L, -1), e - 1);
            lua_pop(L, 1);
        }
    }
    else if (strcmp(rule, "crossing_at") == 0)
    {
        if (!lua_isnumber(L, -1))
            bad("arc.rules.crossing_at must answer a depth in tiles or 0, not %s", luaL_typename(L, -1));
        else if (lua_tonumber(L, -1) < 0.0)
            bad("arc.rules.crossing_at answered a depth below nothing");
    }
    else if (strcmp(rule, "crossing") == 0)
    {
        if (!lua_isnumber(L, -1) && !lua_isboolean(L, -1))
            bad("arc.rules.crossing must answer a depth in tiles or false, not %s", luaL_typename(L, -1));
        else if (lua_isnumber(L, -1) && lua_tonumber(L, -1) < 0.0)
            bad("arc.rules.crossing answered a depth below nothing");
    }
    else if (strcmp(rule, "lanes") == 0)
    {
        if (!lua_istable(L, -1))
            bad("arc.rules.lanes must answer a table of offsets, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "join") == 0)
    {
        if (!lua_istable(L, -1))
            bad("arc.rules.join must answer the ways to try, not %s", luaL_typename(L, -1));
        else
        {
            int i, n = (int)lua_rawlen(L, -1);
            for (i = 1; i <= n; ++i)
            {
                const char *w;
                lua_rawgeti(L, -1, i);
                w = lua_tostring(L, -1);
                if (!w || (strcmp(w, "arc") && strcmp(w, "biarc") && strcmp(w, "walk")))
                    bad("arc.rules.join named %s, which is not a way to join",
                        w ? w : luaL_typename(L, -1));
                lua_pop(L, 1);
            }
        }
    }
    else if (strcmp(rule, "gate") == 0)
    {
        if (!lua_isnumber(L, -1))
            bad("arc.rules.gate must answer the arm's angle, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "traffic") == 0)
    {
        const char *k[5] = {"train_speed", "gap_stop", "gap_free", "step_max", "car_len"};
        int         i;
        if (!lua_istable(L, -1))
            bad("arc.rules.traffic must answer how the moving world behaves, not %s", luaL_typename(L, -1));
        else
            for (i = 0; i < 5; ++i)
            {
                lua_getfield(L, -1, k[i]);
                if (!lua_isnumber(L, -1))
                    bad("arc.rules.traffic answered no %s", k[i]);
                lua_pop(L, 1);
            }
    }
    else if (strcmp(rule, "family") == 0)
    {
        const char *k[6] = {"inner", "edge", "at_junction", "parallel", "look", "mouth"};
        int         i;
        if (!lua_istable(L, -1))
        {
            bad("arc.rules.family must answer what is true of the family, not %s", luaL_typename(L, -1));
            return;
        }
        lua_getfield(L, -1, "junction");
        if (!lua_istable(L, -1))
            bad("arc.rules.family answered no junction box");
        lua_pop(L, 1);
        lua_getfield(L, -1, "footway");
        if (lua_istable(L, -1))
            for (i = 0; i < 6; ++i)
            {
                lua_getfield(L, -1, k[i]);
                if (!lua_isnumber(L, -1))
                    bad("arc.rules.family's footway has no %s", k[i]);
                lua_pop(L, 1);
            }
        lua_pop(L, 1);
    }
    else if (strcmp(rule, "crossing_frame") == 0)
    {
        const char *k[3] = {"reach", "mast", "bed"};
        int         i;
        if (!lua_istable(L, -1))
            bad("arc.rules.crossing_frame must answer the crossing's measurements, not %s", luaL_typename(L, -1));
        else
            for (i = 0; i < 3; ++i)
            {
                lua_getfield(L, -1, k[i]);
                if (!lua_isnumber(L, -1) || lua_tonumber(L, -1) <= 0.0)
                    bad("arc.rules.crossing_frame answered no %s", k[i]);
                lua_pop(L, 1);
            }
    }
    else if (strcmp(rule, "crossing_marks") == 0)
    {
        if (!lua_istable(L, -1))
            bad("arc.rules.crossing_marks must answer a table of places, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "rail_marks") == 0)
    {
        if (!lua_istable(L, -1))
            bad("arc.rules.rail_marks must answer a table of places, not %s", luaL_typename(L, -1));
        else
        {
            int i, n = (int)lua_rawlen(L, -1);
            for (i = 1; i <= n; ++i)
            {
                lua_rawgeti(L, -1, i);
                lua_getfield(L, -1, "model");
                if (!lua_isstring(L, -1))
                    bad("arc.rules.rail_marks answered a place with no model to stand there");
                lua_pop(L, 2);
            }
        }
    }
    else if (strcmp(rule, "lamps") == 0)
    {
        if (!lua_istable(L, -1))
            bad("arc.rules.lamps must answer a table of places, not %s", luaL_typename(L, -1));
        else
        {
            int i, n = (int)lua_rawlen(L, -1);
            for (i = 1; i <= n; ++i)
            {
                lua_rawgeti(L, -1, i);
                lua_getfield(L, -1, "at");
                if (!lua_isnumber(L, -1))
                    bad("arc.rules.lamps answered a lamp with no place along the strip");
                lua_pop(L, 2);
            }
        }
    }
    else if (strcmp(rule, "corner") == 0)
    {
        if (!lua_istable(L, -1) && !lua_isboolean(L, -1))
            bad("arc.rules.corner must answer a kerb return, false or nothing, not %s", luaL_typename(L, -1));
        else if (lua_istable(L, -1))
        {
            const char *k[3] = {"tangent", "radius", "steps"};
            int         i;
            for (i = 0; i < 3; ++i)
            {
                lua_getfield(L, -1, k[i]);
                if (!lua_isnumber(L, -1))
                    bad("arc.rules.corner answered a kerb return with no %s", k[i]);
                lua_pop(L, 1);
            }
        }
    }
    else if (strcmp(rule, "open_tiles") == 0 || strcmp(rule, "standing_tiles") == 0 ||
             strcmp(rule, "carrier_tiles") == 0 || strcmp(rule, "rail_crossing_tiles") == 0 ||
             strcmp(rule, "water_tiles") == 0 || strcmp(rule, "built_tiles") == 0 ||
             strcmp(rule, "sloped_tiles") == 0 || strcmp(rule, "slope_codes") == 0 ||
             strcmp(rule, "structure_tints") == 0 || strcmp(rule, "building_tiles") == 0 ||
             strcmp(rule, "elevated_tiles") == 0 || strcmp(rule, "levelling_tiles") == 0 ||
             strcmp(rule, "saddle_tiles") == 0)
    {
        if (!lua_istable(L, -1))
            bad("arc.rules.%s must answer a table keyed by the city's building byte, not %s", rule, luaL_typename(L, -1));
        else
        {
            lua_pushnil(L);
            while (lua_next(L, -2))
            {
                lua_Integer b = lua_tointeger(L, -2);
                if (!lua_isnumber(L, -2) || b < 0 || b > 255)
                    bad("arc.rules.%s keyed an entry on %s, not a building byte", rule, luaL_typename(L, -2));
                lua_pop(L, 1);
            }
        }
    }
    else if (strcmp(rule, "after") == 0)
    {
        const char *k = lua_tostring(L, -1);
        if (!k || (strcmp(k, "crossing") != 0 && strcmp(k, "end") != 0))
            bad("arc.rules.after must answer crossing or end, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "fit_choice") == 0)
    {
        const char *k = lua_tostring(L, -1);
        if (!k || (strcmp(k, "free") != 0 && strcmp(k, "held") != 0))
            bad("arc.rules.fit_choice must answer free or held, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "ramp_fork") == 0)
    {
        if (!lua_isnumber(L, -1))
            bad("arc.rules.ramp_fork must answer how the foot meets the road, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "car_follow") == 0 || strcmp(rule, "car_hold") == 0)
    {
        if (!lua_isnumber(L, -1))
            bad("arc.rules.%s must answer a speed, not %s", rule, luaL_typename(L, -1));
    }
    else if (strcmp(rule, "band_start") == 0)
    {
        const char *k = lua_tostring(L, -1);
        if (!lua_isnil(L, -1) && (!k || (strcmp(k, "forward") != 0 && strcmp(k, "backward") != 0)))
            bad("arc.rules.band_start must answer forward, backward or nothing, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "ramp_span") == 0)
    {
        if (!lua_istable(L, -1))
            bad("arc.rules.ramp_span must answer where the descent runs, not %s", luaL_typename(L, -1));
        else
        {
            const char *k[4] = {"top", "foot", "total", "along"};
            int         i;
            for (i = 0; i < 4; ++i)
            {
                lua_getfield(L, -1, k[i]);
                if (!lua_isnumber(L, -1))
                    bad("arc.rules.ramp_span answered a descent with no %s", k[i]);
                lua_pop(L, 1);
            }
        }
    }
    else if (strcmp(rule, "seg_class") == 0)
    {
        if (!lua_isnumber(L, -1))
            bad("arc.rules.seg_class must answer a class, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "ramp_side") == 0)
    {
        if (!lua_isboolean(L, -1))
            bad("arc.rules.ramp_side must answer whether the taper keeps its own way, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "ramp_share") == 0)
    {
        if (!lua_isnumber(L, -1) && !lua_isnil(L, -1))
            bad("arc.rules.ramp_share must answer a taper length or nothing, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "road_tiles") == 0)
    {
        if (!lua_istable(L, -1))
            bad("arc.rules.road_tiles must answer a table keyed by the city's building byte, not %s", luaL_typename(L, -1));
        else
        {
            lua_pushnil(L);
            while (lua_next(L, -2))
            {
                lua_Integer b = lua_tointeger(L, -2);
                const char *k = lua_tostring(L, -1);
                if (!lua_isnumber(L, -2) || b < 0 || b > 255)
                    bad("arc.rules.road_tiles keyed an entry on %s, not a building byte", luaL_typename(L, -2));
                if (!k || (strcmp(k, "road") != 0 && strcmp(k, "crossing") != 0 && strcmp(k, "under") != 0))
                    bad("arc.rules.road_tiles gave byte %d a road that is none of road, crossing or under", (int)b);
                lua_pop(L, 1);
            }
        }
    }
    else if (!lua_isboolean(L, -1))
        bad("arc.rules.%s must answer whether it drew the prop, not %s", rule, luaL_typename(L, -1));
}

/*  Every file named, in ONE state and in the order given, because that
 *  is how the program reads them: a name geo.lua sets is a name
 *  models.lua may use, and linting each on its own would call the
 *  second a typo.  The rules are called once the lot has been read. */
static int lint_all(char **paths, int n)
{
    lua_State *L = luaL_newstate();
    size_t     i;
    int        k, before = s_bad;
    if (!L)
        return 1;
    luaL_openlibs(L);
    arc_open(L);
    /*  A write to an undeclared global is caught; a read of one is not,
     *  since that is how the standard library is reached. */
    lua_pushglobaltable(L);
    lua_newtable(L);
    lua_pushcfunction(L, l_global_set);
    lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);
    lua_pop(L, 1);
    for (k = 0; k < n; ++k)
    {
        s_file = paths[k];
        if (luaL_loadfile(L, paths[k]) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK)
        {
            bad("%s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    s_file = n > 0 ? paths[n - 1] : "?";
    unknown_rules();
    models_check(L);
    stages_check(L);
    for (i = 0; i < sizeof RULES / sizeof RULES[0]; ++i)
    {
        int nargs = 0, rc;
        lua_getglobal(L, "arc");
        lua_getfield(L, -1, "rules");
        lua_getfield(L, -1, RULES[i].name);
        if (!lua_isfunction(L, -1))
        {
            lua_pop(L, 3);
            continue;
        }
        rule_args(L, RULES[i].name, &nargs);
        /*  The same stop the running program puts on a rule: one asked
         *  with the city's own numbers may still fail to end, and a
         *  lint that hangs says nothing at all. */
        api_rule_watch(L, 1);
        rc = lua_pcall(L, nargs, 1, 0);
        api_rule_watch(L, 0);
        if (rc != LUA_OK)
        {
            bad("arc.rules.%s: %s", RULES[i].name, lua_tostring(L, -1));
            lua_pop(L, 3);
            continue;
        }
        rule_answer(L, RULES[i].name);
        lua_pop(L, 3);
    }
    lua_close(L);
    return s_bad != before;
}

int lua_lint_main(int argc, char **argv)
{
    char **paths = NULL;
    int    i, n = 0;
    paths = (char **)malloc((size_t)(argc > 0 ? argc : 1) * sizeof *paths);
    if (!paths)
        return 2;
    for (i = 1; i < argc; ++i)
        if (argv[i][0] != '-')
            paths[n++] = argv[i];
    if (!n)
    {
        free(paths);
        fprintf(stderr, "arcology --lua-lint: name the scripts to read\n");
        return 2;
    }
    s_nown = s_n_stage = s_n_unknown = 0;
    lint_all(paths, n);
    free(paths);
    printf("lua lint  %d script%s read together, %d fault%s\n",
           n, n == 1 ? "" : "s", s_bad, s_bad == 1 ? "" : "s");
    return s_bad != 0;
}

