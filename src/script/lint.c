/*  lint.c: `arcology --lua-lint FILE...`: what is wrong with a script
 *  before it is ever run for real.
 *
 *  A generic Lua checker knows the language.  This one knows the
 *  VOCABULARY, which is where the time goes: `arc.geo.cros_deep` reads
 *  as nil, `arc.rules.crosing = f` is a rule that never fires.  Neither
 *  says anything at all: the city simply comes out unchanged and the
 *  next half hour goes on wondering why.  So every name is checked
 *  against what the program actually has.
 *
 *  Four passes, each catching what the one before it cannot:
 *
 *    1. it parses. 2. it runs, in an environment where `arc.tune` and
 *    `arc.geo` refuse a name that is not a setting, `arc.rules` refuses
 *    a name that is not a rule or a value that is not a function.  A
 *    write to a global that was never declared is reported: a missing
 *    `local`. 3. every rule it sets is CALLED once, with an argument of
 *    the shape the pipeline hands it, so a typo inside a rule body is
 *    found here rather than at the next build. 4. what each rule
 *    answered is the shape its caller reads.
 *
 *  Nothing it does touches the world: there is no city, no mesh and no
 *  window.  The settings it writes go to a table of its own. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "internal.h"
#include "mesh/internal.h"
#include "pipeline.h"

#include "net/net.h"
/*  One run's faults, said as they are found and counted for the exit. */
static int         s_bad;
static const char *s_file;
/*  The numbers the scripts set, with what they set them to.  A write of
 *  a name the program does not have MAKES one, so it is not a fault.  A
 *  read of a name neither the program nor the scripts have still is, and
 *  that is the typo worth catching.  The values are kept, because the
 *  rules are then exercised on the city's own numbers.  A rule that
 *  divides by one is only tried honestly when the number is the
 *  scripts'.  The same holds for one that steps along a strip by one,
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
    {"lap_at", "number"},
    {"stripe", "number or boolean"},
    {"corner",    "table, false or nil"},
    {"lanes",     "table"},
    {"lamps",     "table"},
    {"thread_marks","table"},
    {"gate",      "number"},
    {"family",    "table"},
    {"strip",     "boolean"},
    {"curves",    "boolean"},
    {"walks",     "boolean"},
    {"junction",  "boolean"},
    {"margin",   "boolean"},
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
    {"seg_class", "number"},
    {"spur_span", "table"},
    {"band_start", "string or nil"},
    {"car_follow", "number"},
    {"car_hold", "number"},
    {"car_turn", "number or nil"},
    {"train_turn", "number or nil"},
    {"signal",    "boolean"},
    {"junction_signs", "boolean"},
    {"control_prop", "table or nil"},
    {"signal_phase", "number"},
    {"signal_group", "number"},
    {"thread_signal", "string or nil"},
    {"car_density", "number"},
    {"line_class", "number"},
    {"shelf",     "boolean"},
    {"cross", "boolean"},
    {"spurs", "boolean"},
    {"terrain", "boolean"},
    {"spur_share", "number or nil"},
    {"lane",      "boolean"},
    {"panel", "boolean"},
    {"lap_frame", "table"},
    {"lap_marks", "table"},
    {"gate_arm",  "boolean"},
    {"power_tile","boolean"},
    {"path",      "nil"},
    {"power_meet","boolean"},
    {"invent",    "nil"},
    {"network",   "boolean"},
    {"bands",     "boolean"},
    {"moving",    "boolean"},
    {"frame",     "boolean"},
    {"links",     "boolean"},
    {"spur_lane", "boolean"},
    {"turns",     "boolean"},
    {"node_threads", "boolean"},
    {"cap",       "boolean"},
    {"spur_target", "boolean"},
};

/*  arc.tune and arc.geo.  A name that is not a setting is a fault on the
 *  way in and on the way out.  The live tables cannot be.  There a read
 *  of an unknown name is simply nil. */
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

/*  Every model built, at a size a prop might actually be given.  So a
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
 *  learns it from arc.family.define.  The stage says what shape the
 *  pipeline hands that rule.
 *
 *  A script may set the rule before the family that names it is
 *  declared.  So a name the lint has not met is KEPT rather than
 *  reported.  The two lists are settled against each other once every
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
 *  meet carries.  A layout outside 0..14 is a piece the art has no shape
 *  for.  A family that is none of the three is a byte the pipeline will
 *  read as a power line. */
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
            if (!fam || (strcmp(fam, "power") != 0 && strcmp(fam, "line") != 0 && strcmp(fam, "thread") != 0))
                bad("arc.pieces gave byte %d a family that is none of power, line or thread", (int)b);
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

/*  arc.bands(t): what part of a band a byte is, and which way it
 *  runs. */
static int l_bands_push(lua_State *L)
{
    if (!lua_istable(L, 1))
    {
        bad("arc.bands wants a table, not %s", luaL_typename(L, 1));
        return 0;
    }
    lua_pushnil(L);
    while (lua_next(L, 1))
    {
        lua_Integer b = lua_tointeger(L, -2);
        const char *k, *a;
        if (!lua_isnumber(L, -2) || b < 0 || b > 255)
            bad("arc.bands keyed an entry on %s, not a byte of the city", luaL_typename(L, -2));
        lua_getfield(L, -1, "kind");
        k = lua_tostring(L, -1);
        if (!k || (strcmp(k, "slab") != 0 && strcmp(k, "incline") != 0 && strcmp(k, "spur") != 0 &&
                   strcmp(k, "curve") != 0 && strcmp(k, "junction") != 0 && strcmp(k, "over") != 0))
            bad("arc.bands gave byte %d a kind that is none of slab, incline, spur, curve, junction or over", (int)b);
        lua_pop(L, 1);
        lua_getfield(L, -1, "axis");
        a = lua_tostring(L, -1);
        if (!a || (strcmp(a, "ew") != 0 && strcmp(a, "ns") != 0))
            bad("arc.bands gave byte %d an axis that is neither ew nor ns", (int)b);
        lua_pop(L, 2);
    }
    lua_pushboolean(L, 1);
    return 1;
}

/*  arc.numbers(name, t): named numbers, so every value must be one.  The
 *  closure's reaches are pushed this way.  A reach that is not a number
 *  is a reach of nought: which predicts too few chunks and leaves stale
 *  triangles standing. */
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
 *  byte.  So a key outside 0..255 is a byte nothing will ever look up
 *  and nearly always a typo. */
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
 *  lane ending the C has no name for is reported by net_family_check.
 *  What is learned here is which stages the declaration answered with
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
    d = api_family_read(L);
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

/*  A write to a global the script never declared: nearly always a
 *  `local` left off.  In a file that is read again and again it leaks. */
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

/*  A layer with no cells in it, and the edge to walk it by. */
static int l_empty_plane(lua_State *L)
{
    lua_newtable(L);
    lua_pushinteger(L, 0);
    return 2;
}

static void arc_open(lua_State *L)
{
    lua_newtable(L);
    settings_open(L, "tune", tune_name);
    settings_open(L, "geo", geo_name);
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
    /*  The map a script reads the simulation off.  There is no city
     *  here, so every layer is empty and every plane has no edge.  A
     *  walk over the map runs no iterations and a rule that reads a cell
     *  is still checked for everything but the answer. */
    lua_newtable(L); /* city */
    lua_pushinteger(L, R_MAP), lua_setfield(L, -2, "size");
    lua_pushcfunction(L, l_empty_table), lua_setfield(L, -2, "tile");
    lua_pushcfunction(L, l_zero), lua_setfield(L, -2, "line_class");
    lua_pushcfunction(L, l_zero), lua_setfield(L, -2, "at");
    lua_pushcfunction(L, l_zero), lua_setfield(L, -2, "near");
    lua_pushcfunction(L, l_empty_plane), lua_setfield(L, -2, "plane");
    lua_newtable(L), lua_setfield(L, -2, "layers");
    lua_setfield(L, -2, "city");
    /*  arc.fit, which answers pieces: an empty sequence here.  So a
     *  script that fits a path of its own is read and run and only its
     *  answer is missing. */
    lua_pushcfunction(L, l_empty_table), lua_setfield(L, -2, "fit");
    /*  The models, defined into this state and then built: a model file
     *  is read like any other.  Its build function is run so that a
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
     *  thrown away: what matters here is that the table is one.  That
     *  every key in it is a byte the city can hold. */
    lua_pushcfunction(L, l_bytes_push), lua_setfield(L, -2, "bytes");
    lua_pushcfunction(L, l_numbers_push), lua_setfield(L, -2, "numbers");
    lua_pushcfunction(L, l_pieces_push), lua_setfield(L, -2, "pieces");
    lua_pushcfunction(L, l_bands_push), lua_setfield(L, -2, "bands");
    /*  The families, declared and CHECKED, but never entered in the
     *  registry: the lint runs beside a program that may be drawing. */
    lua_newtable(L);
    lua_pushcfunction(L, l_family_define), lua_setfield(L, -2, "define");
    lua_pushcfunction(L, l_empty_table), lua_setfield(L, -2, "list");
    /*  arc.family.rules: the numbers a family is drawn by, pushed.  A
     *  table is all this can check: what is IN it is checked where the
     *  rule that builds it is called. */
    lua_pushcfunction(L, l_nop), lua_setfield(L, -2, "rules");
    lua_setfield(L, -2, "family");
    lua_newtable(L); /* mesh */
    lua_pushcfunction(L, l_empty_table), lua_setfield(L, -2, "meets");
    lua_pushcfunction(L, l_empty_table), lua_setfield(L, -2, "walkways");
    lua_pushcfunction(L, l_empty_table), lua_setfield(L, -2, "faults");
    lua_pushcfunction(L, l_nop), lua_setfield(L, -2, "probe");
    lua_setfield(L, -2, "mesh");
    lua_setglobal(L, "arc");
}

/*  What the pipeline hands a rule a FAMILY named for one of its stages,
 *  and how many answers it takes back.  The three that decide are asked
 *  with plain values and answer with numbers.  A stage handed the strip
 *  itself answers -1 here, since a lint has no strip to hand it. */
static int stage_args(lua_State *L, NetHook h, int *nargs)
{
    switch (h)
    {
    case NH_CONTROL:
        /*  A four-way junction in the middle of the map, as the pipeline
             hands one over: one table, not three numbers. */
        if (luaL_dostring(L, "return {col = 64, row = 64, links = 15, busy = false}") != LUA_OK)
        {
            bad("the lint's own control: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
        return 1;
    case NH_FLIES:
        lua_pushboolean(L, 1); /* a structure, the arm that always stands clear */
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
        /*  A stage's answer has the shape its stage takes back: the
         *  control gives one control a side, as a table of four.  The
         *  rest give plain numbers. */
        if (s_stage[i].stage == NH_CONTROL ? !lua_istable(L, -1) : !lua_isnumber(L, -1))
            bad("arc.rules.%s answers the %s stage: it must give %s, not %s", s_stage[i].name,
                NET_HOOK_NAME[s_stage[i].stage],
                s_stage[i].stage == NH_CONTROL ? "a control a side, as a table of four" : "numbers",
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
    else if (strcmp(rule, "lap_at") == 0)
    {
        lua_newtable(L);
        lua_pushinteger(L, 64), lua_setfield(L, -2, "col");
        lua_pushinteger(L, 64), lua_setfield(L, -2, "row");
        lua_pushinteger(L, 0), lua_setfield(L, -2, "arm");
        lua_pushinteger(L, 2), lua_setfield(L, -2, "control");
        lua_pushboolean(L, 1), lua_setfield(L, -2, "margin");
        lua_pushnumber(L, -1.0), lua_setfield(L, -2, "cos");
        lua_pushnumber(L, 3.0), lua_setfield(L, -2, "span");
        *nargs = 1;
    }
    else if (strcmp(rule, "stripe") == 0)
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
        lua_pushstring(L, "line");
        lua_pushinteger(L, 1);
        *nargs = 2;
    }
    else if (strcmp(rule, "lamps") == 0)
    {
        /*  A lit strip long enough to carry several, as the pipeline
             hands one over: the strip itself, which the rule stands its
             lamps beside. */
        if (luaL_dostring(L,
                          "return {kind = 'strip',"
                          " info = function () return {class = 1.0, len = 6.0, half = 0.5, n = 8} end,"
                          " at = function (_, i) return 10.0 + i, 20.0, 1.0, 1.0, 0.0, i * 1.0,"
                          "   0.5, 0.5, 1e9, 1.0 end,"
                          " near_lap = function () return false end,"
                          " on_map = function () return true end,"
                          " order = function () return 4.0 end,"
                          " prop = function () return true end}") != LUA_OK)
        {
            bad("the lint's own lamps: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "water_tiles") == 0 ||
             strcmp(rule, "slope_codes") == 0 || strcmp(rule, "built_tiles") == 0 ||
             strcmp(rule, "sloped_tiles") == 0 || strcmp(rule, "structure_tints") == 0 ||
             strcmp(rule, "building_tiles") == 0 || strcmp(rule, "elevated_tiles") == 0 ||
             strcmp(rule, "levelling_tiles") == 0 || strcmp(rule, "saddle_tiles") == 0 ||
             strcmp(rule, "open_tiles") == 0 ||
             strcmp(rule, "slab_air") == 0 ||
             strcmp(rule, "carrier_tiles") == 0 ||
             strcmp(rule, "lap_tiles") == 0)
        *nargs = 0;
    else if (strcmp(rule, "line_class") == 0)
    {
        lua_pushinteger(L, 200); /* a busy tile: the arm that is a boulevard */
        *nargs = 1;
    }
    else if (strcmp(rule, "car_density") == 0)
    {
        lua_pushinteger(L, 0xC0); /* a busy tile: the arm that carries two */
        *nargs = 1;
    }
    else if (strcmp(rule, "thread_signal") == 0)
    {
        /*  A car well down the block ahead and none behind. */
        if (luaL_dostring(L, "return {ahead = 4.0, back = 1e9}") != LUA_OK)
        {
            bad("the lint's own thread signal: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "junction_signs") == 0)
    {
        /*  A four-way junction with a signal on one arm and a stop
             sign on another, so both arms of the answer are walked. */
        if (luaL_dostring(L,
                          "return {kind = 'signs',"
                          " info = function () return {col = 40, row = 60, half = 0.5,"
                          "   arms = {{control = 2, edge = 0}, {control = 1, edge = 1},"
                          "           {control = 0, edge = 2}, {control = 0, edge = 3}}} end}") != LUA_OK)
        {
            bad("the lint's own junction signs: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "control_prop") == 0)
    {
        lua_pushinteger(L, 2); /* a signaled arm: the one that takes the cycle */
        *nargs = 1;
    }
    else if (strcmp(rule, "signal_phase") == 0 || strcmp(rule, "signal_group") == 0)
    {
        lua_pushinteger(L, 2); /* a stagger, or an edge: both are small numbers */
        *nargs = 1;
    }
    else if (strcmp(rule, "signal") == 0)
    {
        /*  A junction with lights and a car facing it along the
             map's columns, which is the group the cycle starts on. */
        if (luaL_dostring(L,
                          "return {col = 40, row = 60, hx = 1.0, hy = 0.0, t = 12.5, k = 2}") != LUA_OK)
        {
            bad("the lint's own signal: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "train_turn") == 0)
    {
        /*  Three arms at the node and the heading the train arrived
             on, so the straightest and the rest are both walked. */
        if (luaL_dostring(L,
                          "return {hx = 1.0, hy = 0.0, arms = {{dx = 1.0, dy = 0.0},"
                          "  {dx = 0.0, dy = 1.0}, {dx = 0.0, dy = -1.0}}}") != LUA_OK)
        {
            bad("the lint's own train turn: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "car_turn") == 0)
    {
        /*  Three arms at the node and a draw of the world's own, so the
             pick and the arm's heading are both walked. */
        if (luaL_dostring(L,
                          "return {draw = 7, arms = {{dx = 1.0, dy = 0.0},"
                          "  {dx = 0.0, dy = 1.0}, {dx = -1.0, dy = 0.0}}}") != LUA_OK)
        {
            bad("the lint's own car turn: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
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
    else if (strcmp(rule, "spur_span") == 0)
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
        /*  A segment of three tiles with the classes tallied over them.
         *  So a rule that reads the tally and one that goes and reads
         *  the cells are both exercised. */
        if (luaL_dostring(L,
                          "return {classes = {3, 5, 1}, n = 3,"
                          " cells = {[0] = {col = 64, row = 64},"
                          "          {col = 65, row = 64}, {col = 66, row = 64}}}") != LUA_OK)
        {
            bad("the lint's own classes: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "spur_share") == 0)
    {
        lua_newtable(L);
        lua_pushnumber(L, 3.0), lua_setfield(L, -2, "gap");
        lua_pushinteger(L, 3), lua_setfield(L, -2, "cap");
        *nargs = 1;
    }
    else if (strcmp(rule, "fit_choice") == 0)
    {
        if (luaL_dostring(L, "return {family = 'line',"
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
                          " corridor = function () return {n = 2, half = 0.25,"
                          "   cells = {[0] = {col = 64, row = 64}, {col = 65, row = 64}},"
                          "   start = {x = 64.5, y = 64.5}, goal = {x = 65.5, y = 64.5}} end,"
                          " answer = function () return 0 end,"
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
         *  line.  Both ends are a junction's mouth, so the guard on them
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
        /*  A meet a little ahead of both lines, on a free line so
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
        /*  A slab long enough for the closing to run, with a window and
         *  a taper at each end. */
        if (luaL_dostring(L,
                          "local n = 8\n"
                          "return {kind = 'profile',"
                          " info = function () return {n = n, total = 7.0, spur = false,"
                          "   lane_piece = false, lane_off = false, flat = false, slab_above = 1.0,"
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
                          /*  Two line lanes at the join, so the lip-side pick the
                           *  slide shares with the spur's foot is walked. */
                          " snap = function (_, at) if at > 0.5 then return nil end"
                          "   return {kind = 'snap',"
                          "     info = function () return {n = 2, what = 'line', band = -1,"
                          "       reach = 4.0, dot = 0.5} end,"
                          "     at = function (_, i) return {lane = i, line = true, slab = false,"
                          "       turn = false, band = -1, off = i * 0.5, dist = 1.0 + i,"
                          "       dot = 0.9} end,"
                          "     is = function () end} end,"
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
        /*  A short slab with two spurs on it, near enough to be each
         *  other's partner, so every arm of the taper is walked. */
        if (luaL_dostring(L,
                          "return {kind = 'drop',"
                          " info = function () return {n = 8, spurs = 2, reach = 2.0, narrow = 0.7} end,"
                          " station = function (_, i) return {at = i - 1.0, x = i - 1.0, y = 0.0,"
                          "   dx = 1.0, dy = 0.0} end,"
                          " spur = function (_, r) return {x = r + 1.0, y = 0.5, tx = r + 1.0, ty = 1.0,"
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
        /*  A strip pinned at both ends with a level meet along it,
         *  so every anchor arm is walked. */
        if (luaL_dostring(L,
                          "return {kind = 'ground',"
                          " info = function () return {n = 6, total = 5.0, pin0 = true, pin1 = true,"
                          "   dead0 = false, dead1 = false, reaches_node = true} end,"
                          " at = function (_, i) return i - 1.0, 4.0 end,"
                          " node = function () return 4.5 end,"
                          " lap = function (_, i) return i == 2 and 4.2 or nil end,"
                          " set = function () end}") != LUA_OK)
        {
            bad("the lint's own ground: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "terrain") == 0)
    {
        /*  A map's worth of tiles is what the rule walks, so the stub
         *  answers the readings and takes the three planes back. */
        if (luaL_dostring(L,
                          "return {kind = 'terrain',"
                          " info = function () return {size = 128, pass = 2, corner = 16} end,"
                          " graded = function () return {} end,"
                          " tops = function () return true end}") != LUA_OK)
        {
            bad("the lint's own terrain: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "spurs") == 0)
    {
        /*  The handle the walk names each tile it finds through, taking
         *  every one it is offered so that both arms of the reading run. */
        if (luaL_dostring(L,
                          "return {kind = 'spurs',"
                          " at = function () return true end,"
                          " info = function () return {pass = 2} end,"
                          " links = function () return 15 end,"
                          " spur = function () end,"
                          " answer = function () end}") != LUA_OK)
        {
            bad("the lint's own spurs: %s", lua_tostring(L, -1));
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
                          "   half = 0.25, far = 0.8, grow = 1.0, cap = 0.45, lips = true, n = 4} end,"
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
            bad("the lint's own meet panel: %s", lua_tostring(L, -1));
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
                          "   spur = false, off = false, step = 0.2} end,"
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
    else if (strcmp(rule, "margin") == 0 || strcmp(rule, "walk_curves") == 0)
    {
        /*  A band that stands for every band: three stations of a
         *  margin beside a line. */
        if (luaL_dostring(L,
                          "return {kind = 'margin',"
                          " info = function () return {band = 'side', n = 3, width = 0.05,"
                          "   asked = 0.3, order = 100, drape = true, col = 64, row = 64} end,"
                          " count = function () return 3 end,"
                          " at = function (_, k) return 64.0 + k * 0.1, 64.0, 64.0 + k * 0.1,"
                          "   64.05, 5.0 end,"
                          " quad = function () return true end,"
                          " ends = function () return 64, 64, 65, 64, 5.0, 5.0 end,"
                          " wire = function () return true end}") != LUA_OK)
        {
            bad("the lint's own margin: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "junction") == 0)
    {
        /*  A junction that stands for every junction: a square outline
         *  round a leveled tile. */
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
        /*  A strip that stands for every strip: four stations of a line
         *  with margins.  So the composition is actually run and a name
         *  it reaches for that nothing sets is a fault here. */
        if (luaL_dostring(L,
                          "return {kind = 'strip',"
                          " info = function () return {family = 'line', class = 0, half = 0.25,"
                          "   mat = 7, len = 1.5, n = 4, flies = false, lips = true,"
                          "   slab = false, cross0 = 0, cross1 = 0, slot = 'slot_strip',"
                          "   flat = false, lane_piece = false, structure = false,"
                          "   girder = 0.11, parapet = 0.045} end,"
                          " count = function () return 4 end,"
                          " at = function (_, i) return 64.0 + i * 0.5, 64.5, 5.0, 1, 0,"
                          "   (i - 1) * 0.5, 1, 1, 9, 5.0 end,"
                          " width = function () return 1 end,"
                          " order = function () return 100 end,"
                          " ground = function () return 5.0 end,"
                          " line_class = function () return 0 end,"
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
    else if (strcmp(rule, "world") == 0 || strcmp(rule, "invent") == 0)
    {
        /*  The build itself.  Its primitives draw nothing here.  There
         *  is no mesh.  So what this exercises is the DRIVE: the loops,
         *  the bounds it reads out of info, and every name it reaches
         *  for on the handle. */
        if (luaL_dostring(L,
                          "return {kind = 'world',"
                          " info = function () return {size = 8, pass = 2,"
                          "   lines = true, underground = false} end,"
                          " wanted = function () return true end,"
                          " shape = function () end,"
                          " loft = function () return 0 end,"
                          " net_discover = function () return 0 end,"
                          " net_cells = function () return nil end,"
                          " net_found = function () return true end,"
                          " band_cells = function () return nil end,"
                          " gate_rest = function () end,"
                          " tile = function () return nil end,"
                          " zone = function () return nil end,"
                          " power = function () return nil end,"
                          " margins = function () return 0, false end,"
                          " margin = function () return nil end,"
                          " junctions = function () return 0 end,"
                          " junction = function () return nil end,"
                          " junction_ring = function () end,"
                          " trims = function () return 0 end,"
                          " junction_bands = function () return 0 end,"
                          " junction_band = function () return nil end,"
                          " junction_band_done = function () return 0 end,"
                          " junction_trim = function () end,"
                          " box_band = function () return nil end,"
                          " box_band_done = function () return 0 end,"
                          " box_fill = function () return nil end,"
                          " mouth = function () return nil end,"
                          " mouth_is = function () end,"
                          " shelf = function () return nil end,"
                          " progress = function () end,"
                          " controls = function () return 0 end,"
                          " control = function () return nil end,"
                          " control_is = function () end,"
                          " xwalk = function () return nil end,"
                          " xwalk_deep = function () end,"
                          " lap = function () return nil end,"
                          " lap_frame = function () end,"
                          " lap_panel = function () return nil end,"
                          " lap_approaches = function () return true end,"
                          " lap_approach = function () return nil end,"
                          " lap_mark = function () return true end,"
                          " networks_draw = function () return true end,"
                          " net_families = function () return 0 end,"
                          " junction_boxes = function () end,"
                          " junction_box = function () return nil end,"
                          " junction_box_done = function () end,"
                          " junction_lanes = function () end,"
                          " junction_turns = function () return nil end,"
                          " node_threads = function () return nil end,"
                          " rail_threads_done = function () end,"
                          " box_paving = function () end,"
                          " box_paving_done = function () end,"
                          " junction_signs = function () return nil end,"
                          " junction_signs_done = function () end,"
                          " segment_caps = function () return nil end,"
                          " spur_target = function () return nil end,"
                          " box_lofts = function () return 0 end,"
                          " box_loft = function () end,"
                          " segments = function () end,"
                          " segment = function () return false end,"
                          " segment_done = function () end,"
                          " networks_drawn = function () end,"
                          " emitted = function () end,"
                          " lanes = function () return true end,"
                          " lane_runs = function () return 0 end,"
                          " lane_run = function () return 'line', 0 end,"
                          " lane_run_is = function () end,"
                          " traffic_runs = function () return 0 end,"
                          " traffic_run = function () return nil end,"
                          " traffic_run_is = function () end,"
                          " flies_runs = function () return 0 end,"
                          " flies_run = function () return nil end,"
                          " flies_run_is = function () end,"
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
                          " band_next = function () return false end,"
                          " car_density_is = function () end,"
                          " road_class_is = function () end,"
                          " control_prop_is = function () end,"
                          " signal_phase_is = function () end,"
                          " signal_group_is = function () end,"
                          " spurs = function () return nil end,"
                          " terrain = function () return nil end,"
                          " field = function () end,"
                          " spur_shares = function () return 0 end,"
                          " spur_share = function () return nil end,"
                          " spur_share_is = function () end,"
                          " spur_spans = function () return 0 end,"
                          " spur_span = function () return nil end,"
                          " spur_span_is = function () end,"
                          " band_chain = function () return nil end,"
                          " band_chained = function () end,"
                          " band_fits = function () return 0 end,"
                          " band_fit = function () return nil end,"
                          " band_fit_done = function () end,"
                          " band_fit_choice = function () return nil end,"
                          " band_fit_choice_is = function () end,"
                          " band_fitted = function () end,"
                          " band_cut = function () end,"
                          " cuts = function () return 0 end,"
                          " cut = function () return nil end,"
                          " cut_done = function () end,"
                          " band_done = function () end,"
                          " band_spurs = function () return true end,"
                          " spur_next = function () return false end,"
                          " spur_pick = function () return nil end,"
                          " spur_routed = function () return true end,"
                          " spur_joined = function () return true end,"
                          " spur_slid = function () return true end,"
                          " spur_slide = function () return nil end,"
                          " spur_done = function () end,"
                          " spur_lofts = function () return 0 end,"
                          " spur_loft = function () end,"
                          " wires = function () return 0 end,"
                          " wire = function () return nil end,"
                          " wire_done = function () end,"
                          " lane_cross_done = function () end,"
                          " lane_cross = function () return nil end,"
                          " band_links = function () return nil end,"
                          " band_links_done = function () end,"
                          " bands = function () return true end}") != LUA_OK)
        {
            bad("the lint's own world: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "spur_lane") == 0)
    {
        /*  Three lanes within reach: a slab lane of the spur's band, a
         *             line's lip-side lane and a turn: so every arm of the pick
         *             is walked. */
        if (luaL_dostring(L,
                          "local c = {[0] = {lane = 0, slab = true, line = false, turn = false,"
                          "    band = 1, off = 0.6, dist = 0.2, dot = 0.99},"
                          "  {lane = 1, slab = false, line = true, turn = false,"
                          "    band = 0, off = 0.3, dist = 0.4, dot = 0.98},"
                          "  {lane = 2, slab = false, line = false, turn = true,"
                          "    band = 0, off = 0.0, dist = 0.5, dot = 0.97}}\n"
                          "return {kind = 'snap',"
                          " info = function () return {n = 3, what = 'slab', band = 1,"
                          "   reach = 1.0, dot = 0.5} end,"
                          " at = function (_, i) return c[i] end,"
                          " is = function () end}") != LUA_OK)
        {
            bad("the lint's own spur lane: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "spur_target") == 0)
    {
        /*  The merge, which is the arm the other two are read against. */
        if (luaL_dostring(L,
                          "return {kind = 'target',"
                          " info = function () return {fork = 0, off = true,"
                          "   rdx = 1.0, rdy = 0.0, mdx = 0.0, mdy = 1.0,"
                          "   x = 10.5, y = 20.5, lane_off = 0.25,"
                          "   merge_along = 0.4} end,"
                          " is = function () end}") != LUA_OK)
        {
            bad("the lint's own spur target: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "cap") == 0)
    {
        /*  Two lanes at one dead end of a segment whose family caps
             round, so both arms of the answer are walked. */
        if (luaL_dostring(L,
                          "local p = {[0] = {['end'] = 1, lane = 0, from = 4, to = 5},"
                          "  {['end'] = 1, lane = 1, from = 6, to = 7}}\n"
                          "return {kind = 'caps',"
                          " info = function () return {n = 2, ends = 'cap',"
                          "   family = 'line'} end,"
                          " at = function (_, i) return p[i] end,"
                          " merge = function () return true end,"
                          " link = function () return true end}") != LUA_OK)
        {
            bad("the lint's own cap: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "node_threads") == 0)
    {
        /*  A T: two arms of one axis and one of the other, so the
             through line and the wye are both walked. */
        if (luaL_dostring(L,
                          "return {kind = 'threads',"
                          " info = function () return {col = 40, row = 60,"
                          "   links = 7, arms = 4} end,"
                          " thread = function () return true end}") != LUA_OK)
        {
            bad("the lint's own thread threads: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "turns") == 0)
    {
        /*  A four-armed junction: two arms of two lanes, one of a single
             lane, and one that is a spur with one port and no way in, so
             every arm of the pattern is walked. */
        if (luaL_dostring(L,
                          "local a = {[0] = {lanes = 2, into = true, out = true, spur = 0},"
                          "  [1] = {lanes = 2, into = true, out = true, spur = 0},"
                          "  [2] = {lanes = 1, into = true, out = true, spur = 0},"
                          "  [3] = {lanes = 1, into = false, out = true, spur = 2}}\n"
                          "return {kind = 'turns',"
                          " info = function () return {col = 10, row = 20, arms = 4,"
                          "   family = 'line'} end,"
                          " arm = function (_, e) return a[e] end,"
                          " want = function () return true end}") != LUA_OK)
        {
            bad("the lint's own turns: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "links") == 0)
    {
        /*  Two slab lanes of one band with their ends open and a line
             lane facing them, so the inner arm, the outer arm, the
             continuation and the taper are all walked. */
        if (luaL_dostring(L,
                          "local n = 3\n"
                          "local l = {[0] = {slab = true, line = false, band = 1, off = 0.2,"
                          "    w = 0.5, len = 8.0, open0 = true, open1 = true},"
                          "  {slab = true, line = false, band = 1, off = 0.6,"
                          "    w = 0.5, len = 8.0, open0 = true, open1 = true},"
                          "  {slab = false, line = true, band = 0, off = 0.0,"
                          "    w = 0.25, len = 4.0, open0 = true, open1 = true}}\n"
                          "return {kind = 'links',"
                          " info = function () return {n = n, reach = 1.0, abreast = 0.1,"
                          "   outer = 0.5, taper_far = 2.0, taper_near = 1.0, taper_room = 1.0,"
                          "   taper_gap = 0.5, road_dot = 0.7, band_reach = 6.0, band_dot = 0.7,"
                          "   band_ahead = 0.0, band_aside = 2.0, band_apart = 0.1} end,"
                          " lane = function (_, i) return l[i] end,"
                          " pose = function (_, i) return 64.0 + i, 64.0, 1.0, 0.0 end,"
                          " station = function () return 64.0, 64.0, 1.0, 0.0 end,"
                          " route = function () return {{x = 64, y = 64}, {x = 66, y = 64}},"
                          "   {0.0, 0.0}, {0.0, 0.0} end,"
                          " link = function () return true end,"
                          " note = function () end}") != LUA_OK)
        {
            bad("the lint's own links: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "frame") == 0)
    {
        /*  A turn that is a build with one pass in it, so the drive's
             loop over the passes is walked and every name it reaches for
             on the handle is answered. */
        if (luaL_dostring(L,
                          "local n = 1\n"
                          "return {kind = 'frame',"
                          " info = function () return {build = true, moving = true} end,"
                          " pass = function () n = n - 1; if n >= 0 then return nil end end,"
                          " moving = function () return nil end}") != LUA_OK)
        {
            bad("the lint's own frame: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "moving") == 0)
    {
        /*  A world of one gate, and one car with a car ahead of it.  It
         *  has a control at the end it runs towards, and a lap on its
         *  way.  So every arm of the beat is walked, and every name it
         *  reaches for on the handle is answered. */
        if (luaL_dostring(L,
                          "return {kind = 'moving',"
                          " info = function () return {beats = 1, draw = true, gates = 1} end,"
                          " run = function () end,"
                          " build = function () return true end,"
                          " gate = function () return {angle = 0.0, near = 1.0, dt = 0.016} end,"
                          " gate_is = function () end,"
                          " cars = function () return 1 end,"
                          " car = function () return {speed = 1.0, gap = 0.5, ahead = 0.3,"
                          "   held = true, stop = 0.42, free = 0.8, line = 0.45,"
                          "   creep = 0.02, step = 0.016, meets = {0.1},"
                          "   signal = {col = 40, row = 60, hx = 1.0, hy = 0.0, t = 12.5, k = 2}} end,"
                          " car_is = function () end,"
                          " car_turn_is = function () end,"
                          " arms = function () return nil end,"
                          " arm_is = function () end,"
                          " signals = function () return 1 end,"
                          " signal = function () return {ahead = 4.0, back = 1e9} end,"
                          " signal_is = function () end,"
                          " gates_drawn = function () return 1 end,"
                          " gate_prop = function () return {x = 64.5, y = 64.5, z = 4.0,"
                          "   fx = 1.0, fy = 0.0, size = 0.0, phase = 0.0, angle = 45.0,"
                          "   len = 0.3, col = 64, row = 64, arm = -1, links = 0} end,"
                          " gate_drawn = function () end}") != LUA_OK)
        {
            bad("the lint's own beat: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "bands") == 0)
    {
        /*  A map of nine cells holding one pair of slab tiles.  So the
         *  walk runs, both sweeps are taken and every name the rule
         *  reaches for on the handle is answered. */
        if (luaL_dostring(L,
                          "local n = 3\n"
                          "local prim, axis, block, side, spur = {}, {}, {}, {}, {}\n"
                          "for i = 0, n * n - 1 do\n"
                          "  prim[i], axis[i], block[i], side[i], spur[i] = -1, 0, -1, 0, 0\n"
                          "end\n"
                          "prim[4] = 4\n"
                          "return {kind = 'bands',"
                          " info = function () return {size = n, max_cells = 512} end,"
                          " plane = function () return prim, axis, block, side, spur end,"
                          " band = function () end}") != LUA_OK)
        {
            bad("the lint's own bands: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "network") == 0)
    {
        /*  A map of nine cells with a junction in the middle.  So the
         *  walk runs, the node arm and the loop arm are both taken.
         *  Every name the rule reaches for on the handle is answered. */
        if (luaL_dostring(L,
                          "local n = 3\n"
                          "local links, art = {}, {}\n"
                          "for i = 0, n * n - 1 do links[i], art[i] = 0, 0 end\n"
                          "links[1], art[1] = 4, 4\n"
                          "links[4], art[4] = 15, 15\n"
                          "links[7], art[7] = 1, 1\n"
                          "return {kind = 'network',"
                          " info = function () return {family = 'line', walked = 0, size = n,"
                          "   max_cells = 512, max_steps = 4096} end,"
                          " plane = function () return links, art end,"
                          " nodes = function () end,"
                          " segment = function () end,"
                          " island = function () end,"
                          " junction = function () end}") != LUA_OK)
        {
            bad("the lint's own network: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        *nargs = 1;
    }
    else if (strcmp(rule, "family") == 0)
    {
        lua_newtable(L);
        lua_pushstring(L, "line"), lua_setfield(L, -2, "name");
        lua_pushnumber(L, 0.50), lua_setfield(L, -2, "width");
        *nargs = 1;
    }
    else if (strcmp(rule, "lap_frame") == 0)
    {
        lua_newtable(L);
        lua_pushinteger(L, 64), lua_setfield(L, -2, "col");
        lua_pushinteger(L, 64), lua_setfield(L, -2, "row");
        lua_pushnumber(L, 1.0), lua_setfield(L, -2, "sin");
        lua_pushnumber(L, 0.50), lua_setfield(L, -2, "line");
        lua_pushnumber(L, 0.44), lua_setfield(L, -2, "thread");
        *nargs = 1;
    }
    else if (strcmp(rule, "lap_marks") == 0)
    {
        lua_newtable(L);
        lua_pushnumber(L, 0.30), lua_setfield(L, -2, "reach");
        lua_pushnumber(L, 0.31), lua_setfield(L, -2, "mast");
        lua_pushnumber(L, 2.00), lua_setfield(L, -2, "limit");
        lua_pushnumber(L, 0.50), lua_setfield(L, -2, "line");
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
    else if (strcmp(rule, "thread_marks") == 0)
    {
        /*  A double-thread line long enough to carry signals, with a
             meet on the way and a junction at one end. */
        if (luaL_dostring(L,
                          "return {kind = 'strip',"
                          " info = function () return {len = 24.0, n = 26, half = 0.5,"
                          "   ahead = true, behind = true} end,"
                          " at = function (_, i) return 10.0 + i, 20.0, 1.0, 1.0, 0.0,"
                          "   i * 1.0, 0.5, 0.5, 1e9, 1.0 end,"
                          " near_lap = function () return false end,"
                          " on_map = function () return true end,"
                          " meets = function () return {8.0} end,"
                          " order = function () return 4.0 end,"
                          " prop_flat = function () return true end,"
                          " thread_signal = function () return 'rail_aspect_red' end}") != LUA_OK)
        {
            bad("the lint's own thread marks: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_pushnil(L);
        }
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
    else if (strcmp(rule, "lap_at") == 0)
    {
        if (!lua_isnumber(L, -1))
            bad("arc.rules.lap_at must answer a depth in tiles or 0, not %s", luaL_typename(L, -1));
        else if (lua_tonumber(L, -1) < 0.0)
            bad("arc.rules.lap_at answered a depth below nothing");
    }
    else if (strcmp(rule, "stripe") == 0)
    {
        if (!lua_isnumber(L, -1) && !lua_isboolean(L, -1))
            bad("arc.rules.stripe must answer a depth in tiles or false, not %s", luaL_typename(L, -1));
        else if (lua_isnumber(L, -1) && lua_tonumber(L, -1) < 0.0)
            bad("arc.rules.stripe answered a depth below nothing");
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
        lua_getfield(L, -1, "margin");
        if (lua_istable(L, -1))
            for (i = 0; i < 6; ++i)
            {
                lua_getfield(L, -1, k[i]);
                if (!lua_isnumber(L, -1))
                    bad("arc.rules.family's margin has no %s", k[i]);
                lua_pop(L, 1);
            }
        lua_pop(L, 1);
    }
    else if (strcmp(rule, "lap_frame") == 0)
    {
        const char *k[3] = {"reach", "mast", "bed"};
        int         i;
        if (!lua_istable(L, -1))
            bad("arc.rules.lap_frame must answer the meet's measurements, not %s", luaL_typename(L, -1));
        else
            for (i = 0; i < 3; ++i)
            {
                lua_getfield(L, -1, k[i]);
                if (!lua_isnumber(L, -1) || lua_tonumber(L, -1) <= 0.0)
                    bad("arc.rules.lap_frame answered no %s", k[i]);
                lua_pop(L, 1);
            }
    }
    else if (strcmp(rule, "lap_marks") == 0)
    {
        if (!lua_istable(L, -1))
            bad("arc.rules.lap_marks must answer a table of places, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "thread_marks") == 0)
    {
        if (!lua_isboolean(L, -1))
            bad("arc.rules.thread_marks stands its own marks: it must answer whether it did, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "lamps") == 0)
    {
        if (!lua_isboolean(L, -1))
            bad("arc.rules.lamps stands its own lamps: it must answer whether it did, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "corner") == 0)
    {
        if (!lua_istable(L, -1) && !lua_isboolean(L, -1))
            bad("arc.rules.corner must answer a lip return, false or nothing, not %s", luaL_typename(L, -1));
        else if (lua_istable(L, -1))
        {
            const char *k[3] = {"tangent", "radius", "steps"};
            int         i;
            for (i = 0; i < 3; ++i)
            {
                lua_getfield(L, -1, k[i]);
                if (!lua_isnumber(L, -1))
                    bad("arc.rules.corner answered a lip return with no %s", k[i]);
                lua_pop(L, 1);
            }
        }
    }
    else if (strcmp(rule, "open_tiles") == 0 || strcmp(rule, "slab_air") == 0 ||
             strcmp(rule, "carrier_tiles") == 0 || strcmp(rule, "lap_tiles") == 0 ||
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
        if (!k || (strcmp(k, "meet") != 0 && strcmp(k, "end") != 0))
            bad("arc.rules.after must answer meet or end, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "fit_choice") == 0)
    {
        const char *k = lua_tostring(L, -1);
        if (!k || (strcmp(k, "free") != 0 && strcmp(k, "held") != 0))
            bad("arc.rules.fit_choice must answer free or held, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "line_class") == 0)
    {
        if (!lua_isnumber(L, -1))
            bad("arc.rules.line_class must answer the class of the line, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "car_density") == 0)
    {
        if (!lua_isnumber(L, -1))
            bad("arc.rules.car_density must answer how many cars the tile is worth, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "thread_signal") == 0)
    {
        if (!lua_isstring(L, -1) && !lua_isnil(L, -1))
            bad("arc.rules.thread_signal must answer the model its head shows, or nothing, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "control_prop") == 0)
    {
        if (!lua_istable(L, -1) && !lua_isnil(L, -1))
            bad("arc.rules.control_prop must answer what the arm carries, or nothing, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "signal_phase") == 0 || strcmp(rule, "signal_group") == 0)
    {
        if (!lua_isnumber(L, -1))
            bad("arc.rules.%s must answer a number, not %s", rule, luaL_typename(L, -1));
    }
    else if (strcmp(rule, "signal") == 0)
    {
        if (!lua_isboolean(L, -1))
            bad("arc.rules.signal must answer whether the signal reads red, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "train_turn") == 0)
    {
        if (!lua_isnumber(L, -1) && !lua_isnil(L, -1))
            bad("arc.rules.train_turn must answer which arm the train takes, or nothing, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "car_turn") == 0)
    {
        if (!lua_isnumber(L, -1) && !lua_isnil(L, -1))
            bad("arc.rules.car_turn must answer which arm the car takes, or nothing, not %s", luaL_typename(L, -1));
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
    else if (strcmp(rule, "spur_span") == 0)
    {
        if (!lua_istable(L, -1))
            bad("arc.rules.spur_span must answer where the descent runs, not %s", luaL_typename(L, -1));
        else
        {
            const char *k[4] = {"top", "foot", "total", "along"};
            int         i;
            for (i = 0; i < 4; ++i)
            {
                lua_getfield(L, -1, k[i]);
                if (!lua_isnumber(L, -1))
                    bad("arc.rules.spur_span answered a descent with no %s", k[i]);
                lua_pop(L, 1);
            }
        }
    }
    else if (strcmp(rule, "seg_class") == 0)
    {
        if (!lua_isnumber(L, -1))
            bad("arc.rules.seg_class must answer a class, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "spur_share") == 0)
    {
        if (!lua_isnumber(L, -1) && !lua_isnil(L, -1))
            bad("arc.rules.spur_share must answer a taper length or nothing, not %s", luaL_typename(L, -1));
    }
    else if (strcmp(rule, "line_tiles") == 0)
    {
        if (!lua_istable(L, -1))
            bad("arc.rules.line_tiles must answer a table keyed by the city's building byte, not %s", luaL_typename(L, -1));
        else
        {
            lua_pushnil(L);
            while (lua_next(L, -2))
            {
                lua_Integer b = lua_tointeger(L, -2);
                const char *k = lua_tostring(L, -1);
                if (!lua_isnumber(L, -2) || b < 0 || b > 255)
                    bad("arc.rules.line_tiles keyed an entry on %s, not a building byte", luaL_typename(L, -2));
                if (!k || (strcmp(k, "line") != 0 && strcmp(k, "meet") != 0 && strcmp(k, "under") != 0))
                    bad("arc.rules.line_tiles gave byte %d a line that is none of line, meet or under", (int)b);
                lua_pop(L, 1);
            }
        }
    }
    else if (!lua_isboolean(L, -1))
        bad("arc.rules.%s must answer whether it drew the prop, not %s", rule, luaL_typename(L, -1));
}

/*  Every file named, in ONE state and in the order given.  This is
 *  because that is how the program reads them.  A name geo.lua sets is a
 *  name models.lua may use, and linting each on its own would call the
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
    /*  A write to an undeclared global is caught.  A read of one is not,
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
         *  with the city's own numbers may still fail to end.  A lint
         *  that hangs says nothing at all. */
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

