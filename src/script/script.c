/*  script.c -- the Lua state, the script it runs, and the watch that
 *  reads it again when it changes.  See script.h. */
#include "script.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "dump.h"
#include "log.h"

#if SC2K_LUA

#include "internal.h"

lua_State *s_L;
int        s_dirty;

static char        s_dir[1024];
static char        s_path[1024];
static int         s_is_dir;
static struct stat s_stat;
static int         s_have_stat;
static char       s_err[1024];
static int        s_gen;
static int        s_reloading;

int script_on(void)
{
    return s_L != NULL;
}

const char *script_path(void)
{
    return s_path[0] ? s_path : NULL;
}

const char *script_error(void)
{
    return s_err[0] ? s_err : NULL;
}

int script_generation(void)
{
    return s_gen;
}

/*  What the script asked to be told: a message through the log, and a
 *  report line through the dump sink, so a script's output obeys the
 *  same two switches every other part of the program does. */
static int l_log(lua_State *L)
{
    int i, n = lua_gettop(L);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (i = 1; i <= n; ++i)
    {
        size_t      len;
        const char *s = luaL_tolstring(L, i, &len);
        if (i > 1)
            luaL_addchar(&b, ' ');
        luaL_addlstring(&b, s, len);
        lua_pop(L, 1);
    }
    luaL_pushresult(&b);
    R_NOTE("lua", "%s", lua_tostring(L, -1));
    return 0;
}

static int l_dump(lua_State *L)
{
    int i, n = lua_gettop(L);
    for (i = 1; i <= n; ++i)
    {
        const char *s = luaL_tolstring(L, i, NULL);
        dumpf(i > 1 ? " %s" : "%s", s);
        lua_pop(L, 1);
    }
    dumpf("\n");
    return 0;
}

/*  The script asking for the world to be drawn again, which a change to
 *  a knob or a constant needs and a change to a rule does too. */
static int l_rebuild(lua_State *L)
{
    (void)L;
    s_dirty = 1;
    return 0;
}

/*  The watch, from the script's own side: whether the file has changed
 *  since it was read, and reading it again.  The console has these, and
 *  so does anything that wants to prove the watch works without a live
 *  loop to wait in. */
static int l_stale(lua_State *L)
{
    lua_pushboolean(L, script_stale());
    return 1;
}

static int l_reload(lua_State *L)
{
    /*  A script that reloads itself would otherwise run itself again
     *  from inside itself, and again from inside that. */
    if (s_reloading)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, script_reload());
    return 1;
}

/*  Everything the script may reach, under one name.  Opened once, when
 *  the state comes up; reading the script again does not rebuild it, so
 *  a value the script left on `arc` between reads survives. */
static void arc_open(lua_State *L)
{
    lua_newtable(L);
    lua_pushcfunction(L, l_log);
    lua_setfield(L, -2, "log");
    lua_pushcfunction(L, l_dump);
    lua_setfield(L, -2, "dump");
    lua_pushcfunction(L, l_rebuild);
    lua_setfield(L, -2, "rebuild");
    lua_pushcfunction(L, l_stale);
    lua_setfield(L, -2, "stale");
    lua_pushcfunction(L, l_reload);
    lua_setfield(L, -2, "reload");
    api_tune_open(L);
    api_rules_open(L);
    api_world_open(L);
    api_put_open(L);
    api_object_open(L);
    api_model_open(L);
    lua_setglobal(L, "arc");
}

/*  The one place an error from Lua is turned into a line: kept for the
 *  console, said once through the log, and never thrown. */
static void fail(const char *what, const char *msg)
{
    snprintf(s_err, sizeof s_err, "%s: %s", what, msg ? msg : "?");
    R_ERR("lua", "%s", s_err);
}

/*  Every .lua under a directory and its folders, as paths relative to
 *  it, in name order -- so a file that depends on what another sets can
 *  be named to come after it, and a folder of models reads as one run
 *  between them. */
static int name_cmp(const void *a, const void *b)
{
    /*  The elements are the names themselves, not pointers to them. */
    return strcmp((const char *)a, (const char *)b);
}

static int dir_walk(const char *root, const char *rel, char names[][256], int max, int n)
{
    char           here[1300];
    DIR           *d;
    struct dirent *e;
    snprintf(here, sizeof here, "%s%s%s", root, rel[0] ? "/" : "", rel);
    d = opendir(here);
    if (!d)
        return n;
    while ((e = readdir(d)) != NULL && n < max)
    {
        char        sub[256];
        struct stat st;
        char        full[1600];
        size_t      l = strlen(e->d_name);
        if (e->d_name[0] == '.')
            continue; /* the folder itself, its parent, and anything hidden */
        snprintf(sub, sizeof sub, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
        snprintf(full, sizeof full, "%s/%s", root, sub);
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
        {
            n = dir_walk(root, sub, names, max, n);
            continue;
        }
        if (l > 4 && strcmp(e->d_name + l - 4, ".lua") == 0)
            snprintf(names[n++], 256, "%s", sub);
    }
    closedir(d);
    return n;
}

static int dir_files(const char *dir, char names[][256], int max)
{
    int n = dir_walk(dir, "", names, max, 0);
    qsort(names, (size_t)n, 256, name_cmp);
    return n;
}

/*  The newest thing in the watched place, so a change to any file in a
 *  directory is a change to the script. */
static void watched_stamp(struct stat *out)
{
    char        names[128][256];
    struct stat st;
    int         i, n;
    memset(out, 0, sizeof *out);
    if (!s_is_dir)
    {
        if (stat(s_path, out) != 0)
            memset(out, 0, sizeof *out);
        return;
    }
    n = dir_files(s_path, names, 128);
    for (i = 0; i < n; ++i)
    {
        char full[1300];
        snprintf(full, sizeof full, "%s/%s", s_path, names[i]);
        if (stat(full, &st) != 0)
            continue;
        if (st.st_mtime > out->st_mtime)
            out->st_mtime = st.st_mtime;
        out->st_size += st.st_size;
    }
}

static int run_watched(void);

static int run_file(const char *path)
{
    if (luaL_loadfile(s_L, path) != LUA_OK || lua_pcall(s_L, 0, 0, 0) != LUA_OK)
    {
        fail("script", lua_tostring(s_L, -1));
        lua_pop(s_L, 1);
        return -1;
    }
    s_err[0] = 0;
    ++s_gen;
    return 0;
}

int script_open(const char *dir, const char *path)
{
    if (s_L)
        script_close();
    s_L = luaL_newstate();
    if (!s_L)
    {
        fail("state", "no memory");
        return -1;
    }
    luaL_openlibs(s_L);
    arc_open(s_L);
    s_path[0]   = 0;
    s_dir[0]    = 0;
    s_have_stat = 0;
    s_gen       = 0;
    if (dir && *dir)
        snprintf(s_dir, sizeof s_dir, "%s", dir);
    if (!path || !*path)
    {
        if (!s_dir[0])
            return 0;
        snprintf(s_path, sizeof s_path, "%s", s_dir);
        s_dir[0] = 0; /* the shipped folder IS the watched place */
    }
    else
        snprintf(s_path, sizeof s_path, "%s", path);
    {
        struct stat st;
        s_is_dir = stat(s_path, &st) == 0 && S_ISDIR(st.st_mode);
        if (!s_is_dir && stat(s_path, &st) != 0)
        {
            fail("script", "no such file");
            return -1;
        }
    }
    watched_stamp(&s_stat);
    s_have_stat = 1;
    return run_watched();
}

/*  The shipped folder first, so its models stand, then the watched file
 *  or folder over it. */
static int run_dir(const char *dir)
{
    char names[128][256];
    int  i, n = dir_files(dir, names, 128), rc = 0;
    for (i = 0; i < n; ++i)
    {
        char full[1300];
        snprintf(full, sizeof full, "%s/%s", dir, names[i]);
        if (run_file(full) != 0)
            rc = -1;
    }
    if (n > 0)
        s_gen -= n - 1;
    return rc;
}

static int run_watched(void)
{
    char names[128][256];
    int  i, n, rc = 0;
    if (s_dir[0] && run_dir(s_dir) != 0)
        rc = -1;
    if (!s_is_dir)
        return run_file(s_path) != 0 ? -1 : rc;
    if (run_dir(s_path) != 0)
        rc = -1;
    (void)names, (void)i, (void)n;
    return rc;
}

void script_close(void)
{
    script_model_reset();
    if (s_L)
        lua_close(s_L);
    s_L         = NULL;
    s_path[0]   = 0;
    s_have_stat = 0;
    s_dirty     = 0;
}

/*  One value, written out.  A table is opened one level, in key order,
 *  because a console that answers "table: 0x..." has answered nothing;
 *  deeper than that is the script's own to print. */
static void show(lua_State *L, int idx)
{
    if (!lua_istable(L, idx))
    {
        dumpf("%s", luaL_tolstring(L, idx, NULL));
        lua_pop(L, 1);
        return;
    }
    dumpf("{");
    lua_pushnil(L);
    {
        int first = 1;
        while (lua_next(L, idx < 0 ? idx - 1 : idx))
        {
            dumpf(first ? " " : ", "), first = 0;
            dumpf("%s = ", luaL_tolstring(L, -2, NULL));
            lua_pop(L, 1);
            dumpf("%s", lua_istable(L, -1) ? "{...}" : luaL_tolstring(L, -1, NULL));
            if (!lua_istable(L, -1))
                lua_pop(L, 1);
            lua_pop(L, 1);
        }
        dumpf(first ? "}" : " }");
    }
}

int script_eval(const char *src)
{
    int base;
    if (!s_L || !src || !*src)
        return -1;
    base = lua_gettop(s_L);
    /*  An expression is the common case at a console, so it is tried as
     *  one first and as a statement only if that will not compile. */
    {
        char buf[4096];
        snprintf(buf, sizeof buf, "return %s", src);
        if (luaL_loadstring(s_L, buf) != LUA_OK)
        {
            lua_pop(s_L, 1);
            if (luaL_loadstring(s_L, src) != LUA_OK)
            {
                fail("eval", lua_tostring(s_L, -1));
                lua_settop(s_L, base);
                return -1;
            }
        }
    }
    if (lua_pcall(s_L, 0, LUA_MULTRET, 0) != LUA_OK)
    {
        fail("eval", lua_tostring(s_L, -1));
        lua_settop(s_L, base);
        return -1;
    }
    s_err[0] = 0;
    {
        int i, n = lua_gettop(s_L) - base;
        for (i = 1; i <= n; ++i)
        {
            if (i > 1)
                dumpf(" ");
            show(s_L, base + i);
        }
        if (n > 0)
            dumpf("\n");
    }
    lua_settop(s_L, base);
    return 0;
}

int script_take_dirty(void)
{
    int d   = s_dirty;
    s_dirty = 0;
    return d;
}

int script_stale(void)
{
    struct stat st;
    if (!s_L || !s_path[0])
        return 0;
    watched_stamp(&st);
    if (!s_have_stat)
        return 1;
    return st.st_mtime != s_stat.st_mtime || st.st_size != s_stat.st_size;
}

int script_reload(void)
{
    if (!s_L || !s_path[0])
        return 0;
    watched_stamp(&s_stat);
    s_have_stat = 1;

    /*  The rules and the models are cleared before the files run, so
     *  what stands is exactly what this reading sets and nothing a
     *  reading of an older one left. */
    script_model_reset();
    lua_getglobal(s_L, "arc");
    lua_newtable(s_L);
    lua_setfield(s_L, -2, "rules");
    lua_pop(s_L, 1);
    s_reloading = 1;
    run_watched();
    s_reloading = 0;
    s_dirty = 0;
    return 1;
}

/*  ---- the rules ---------------------------------------------------- */

lua_State *script_state(void)
{
    return s_L;
}

int api_rule_begin(lua_State *L, const char *name)
{
    lua_getglobal(L, "arc");
    lua_getfield(L, -1, "rules");
    lua_getfield(L, -1, name);
    if (!lua_isfunction(L, -1))
    {
        lua_pop(L, 3);
        return 0;
    }
    lua_remove(L, -2); /* rules */
    lua_remove(L, -2); /* arc   */
    return 1;
}

void api_rule_runaway(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    luaL_error(L, "runs away: no answer in %d steps", RULE_STEPS);
}

/*  A rule may ask for another rule -- the fit asks for the sweep at each
 *  of its corners -- and the runaway guard belongs to the outermost of
 *  them: an inner call that cleared the hook on the way out would leave
 *  the rest of the outer rule running unwatched. */
static int s_rule_depth;

int api_rule_call(lua_State *L, const char *name, int nargs, int nres)
{
    int ok;
    if (s_rule_depth++ == 0)
        lua_sethook(L, api_rule_runaway, LUA_MASKCOUNT, RULE_STEPS);
    ok = lua_pcall(L, nargs, nres, 0) == LUA_OK;
    if (--s_rule_depth == 0)
        lua_sethook(L, NULL, 0, 0);
    if (ok)
        return 1;
    fail(name, lua_tostring(L, -1));
    lua_pop(L, 1);
    return 0;
}

float api_field_num(lua_State *L, const char *key, float def)
{
    float v = def;
    lua_getfield(L, -1, key);
    if (lua_isnumber(L, -1))
        v = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

int script_rule_control(int col, int row, const int cls[4], const int traf[4], int busy, int *out)
{
    int e, ok = 0;
    if (!s_L || !api_rule_begin(s_L, "control"))
        return 0;
    lua_pushinteger(s_L, col);
    lua_pushinteger(s_L, row);
    lua_newtable(s_L); /* the arms: class and traffic, or nothing where there is no arm */
    for (e = 0; e < 4; ++e)
    {
        if (cls[e] < 0)
            continue;
        lua_newtable(s_L);
        lua_pushinteger(s_L, cls[e]);
        lua_setfield(s_L, -2, "class");
        lua_pushinteger(s_L, traf[e]);
        lua_setfield(s_L, -2, "traffic");
        lua_rawseti(s_L, -2, e + 1);
    }
    lua_pushboolean(s_L, busy);
    if (!api_rule_call(s_L, "control", 4, 1))
        return 0;
    if (lua_istable(s_L, -1))
    {
        int v = 0;
        for (e = 0; e < 4; ++e)
        {
            lua_rawgeti(s_L, -1, e + 1);
            v |= (lua_tointeger(s_L, -1) & 3) << (2 * e);
            lua_pop(s_L, 1);
        }
        *out = v;
        ok   = 1;
    }
    lua_pop(s_L, 1);
    return ok;
}

int script_rule_crossing_at(int col, int row, int arm, int ctrl, int pavement, float cs, float span, float *out)
{
    int ok = 0;
    if (!s_L || !api_rule_begin(s_L, "crossing_at"))
        return 0;
    lua_newtable(s_L);
    lua_pushinteger(s_L, col), lua_setfield(s_L, -2, "col");
    lua_pushinteger(s_L, row), lua_setfield(s_L, -2, "row");
    lua_pushinteger(s_L, arm), lua_setfield(s_L, -2, "arm");
    lua_pushinteger(s_L, ctrl), lua_setfield(s_L, -2, "control");
    lua_pushboolean(s_L, pavement), lua_setfield(s_L, -2, "pavement");
    lua_pushnumber(s_L, cs), lua_setfield(s_L, -2, "cos");
    lua_pushnumber(s_L, span), lua_setfield(s_L, -2, "span");
    if (!api_rule_call(s_L, "crossing_at", 1, 1))
        return 0;
    if (lua_isnumber(s_L, -1))
        *out = (float)lua_tonumber(s_L, -1), ok = *out > 0.0f;
    lua_pop(s_L, 1);
    return ok;
}

int script_rule_crossing(int col, int row, int arm, int ctrl, float want, float room, float straight, float *out)
{
    int ok = 0;
    if (!s_L || !api_rule_begin(s_L, "crossing"))
        return 0;
    lua_newtable(s_L);
    lua_pushinteger(s_L, col), lua_setfield(s_L, -2, "col");
    lua_pushinteger(s_L, row), lua_setfield(s_L, -2, "row");
    lua_pushinteger(s_L, arm), lua_setfield(s_L, -2, "arm");
    lua_pushinteger(s_L, ctrl), lua_setfield(s_L, -2, "control");
    lua_pushnumber(s_L, want), lua_setfield(s_L, -2, "want");
    lua_pushnumber(s_L, room), lua_setfield(s_L, -2, "room");
    lua_pushnumber(s_L, straight), lua_setfield(s_L, -2, "straight");
    if (!api_rule_call(s_L, "crossing", 1, 1))
        return 0;
    if (lua_isnumber(s_L, -1))
        *out = (float)lua_tonumber(s_L, -1), ok = 1;
    else if (lua_isboolean(s_L, -1))
        *out = lua_toboolean(s_L, -1) ? want : 0.0f, ok = 1;
    lua_pop(s_L, 1);
    return ok;
}

/*  A prop the script builds itself.  Its place goes in as one table and
 *  the answer is whether it drew: a rule that draws nothing, or none at
 *  all, leaves the C to build its own. */
int script_rule_prop(const char *name, const ScriptProp *at)
{
    int drew = 0;
    if (!s_L || !at || !api_rule_begin(s_L, name))
        return 0;
    lua_newtable(s_L);
    lua_pushnumber(s_L, at->x), lua_setfield(s_L, -2, "x");
    lua_pushnumber(s_L, at->y), lua_setfield(s_L, -2, "y");
    lua_pushnumber(s_L, at->z), lua_setfield(s_L, -2, "z");
    lua_pushnumber(s_L, at->fx), lua_setfield(s_L, -2, "fx");
    lua_pushnumber(s_L, at->fy), lua_setfield(s_L, -2, "fy");
    lua_pushnumber(s_L, at->size), lua_setfield(s_L, -2, "size");
    lua_pushinteger(s_L, at->col), lua_setfield(s_L, -2, "col");
    lua_pushinteger(s_L, at->row), lua_setfield(s_L, -2, "row");
    lua_pushinteger(s_L, at->arm), lua_setfield(s_L, -2, "arm");
    lua_pushinteger(s_L, at->links), lua_setfield(s_L, -2, "links");
    lua_pushnumber(s_L, at->phase), lua_setfield(s_L, -2, "phase");
    lua_pushnumber(s_L, at->angle), lua_setfield(s_L, -2, "angle");
    lua_pushnumber(s_L, at->len), lua_setfield(s_L, -2, "len");
    if (!api_rule_call(s_L, name, 1, 1))
        return 0;
    drew = !lua_isnil(s_L, -1) && lua_toboolean(s_L, -1);
    lua_pop(s_L, 1);
    return drew;
}

/*  The lamps along one strip.  The rule answers a list of places, and a
 *  strip it wants unlit answers an empty one. */
/*  The signals and posts along one railway strip.  The rule answers a
 *  list of places, each naming the model that stands there. */
/*  A level crossing's gate, once a frame.  The rule is given where the
 *  arm is and how near the nearest train is, and answers where the arm
 *  goes next. */
/*  A level crossing's own measurements, and what its approaches carry.
 *  Both are asked once for each crossing as the mesh is built. */
/*  What is true of every strip of one family.  Asked once for each as
 *  the mesh is built; net_family_rules keeps the answer. */
/*  How the moving world behaves.  Asked once; net_traffic_rules keeps
 *  the answer, so no car costs a call. */
/*  One strip's ribbon.  The rule walks the stations itself, so this is
 *  one call for a strip however many quads it comes to. */
int script_rule_strip(void)
{
    int drew = 0;
    if (!s_L || !api_rule_begin(s_L, "strip"))
        return 0;
    if (!api_rule_call(s_L, "strip", 0, 1))
        return 0;
    drew = !lua_isnil(s_L, -1) && lua_toboolean(s_L, -1);
    lua_pop(s_L, 1);
    return drew;
}

/*  What to try where two lines meet.  Asked once for each boundary of a
 *  fitted path -- a few thousand a build. */
int script_rule_join(int cross, int free_line, char how[][12], int max)
{
    int n = 0, i;
    if (!s_L || !api_rule_begin(s_L, "join"))
        return 0;
    lua_newtable(s_L);
    lua_pushboolean(s_L, cross), lua_setfield(s_L, -2, "cross");
    lua_pushboolean(s_L, free_line), lua_setfield(s_L, -2, "free");
    if (!api_rule_call(s_L, "join", 1, 1))
        return 0;
    if (lua_istable(s_L, -1))
    {
        int have = (int)lua_rawlen(s_L, -1);
        n = have < max ? have : max;
        for (i = 0; i < n; ++i)
        {
            const char *w;
            lua_rawgeti(s_L, -1, i + 1);
            w = lua_tostring(s_L, -1);
            snprintf(how[i], 12, "%s", w ? w : "");
            lua_pop(s_L, 1);
        }
    }
    lua_pop(s_L, 1);
    return n;
}

int script_rule_traffic(ScriptTraffic *out)
{
    int ok = 0;
    if (!s_L || !api_rule_begin(s_L, "traffic"))
        return 0;
    if (!api_rule_call(s_L, "traffic", 0, 1))
        return 0;
    if (lua_istable(s_L, -1))
    {
        out->blink        = api_field_num(s_L, "blink", 0.5f);
        out->train_speed  = api_field_num(s_L, "train_speed", 0.0f);
        out->train_spread = api_field_num(s_L, "train_spread", 0.0f);
        out->train_len    = api_field_num(s_L, "train_len", 0.0f);
        out->trail_step   = api_field_num(s_L, "trail_step", 1.0f);
        out->xing_find    = api_field_num(s_L, "xing_find", 0.0f);
        out->gate_up      = api_field_num(s_L, "gate_up", 0.0f);
        out->gate_watch   = api_field_num(s_L, "gate_watch", 0.0f);
        out->density      = api_field_num(s_L, "density", 0.0f);
        out->car_len      = api_field_num(s_L, "car_len", 0.0f);
        out->gap_stop     = api_field_num(s_L, "gap_stop", 0.0f);
        out->gap_free     = api_field_num(s_L, "gap_free", 1.0f);
        out->stop_junc    = api_field_num(s_L, "stop_junc", 0.0f);
        out->stop_hold    = api_field_num(s_L, "stop_hold", 0.0f);
        out->creep        = api_field_num(s_L, "creep", 0.0f);
        out->probe        = api_field_num(s_L, "probe", 0.0f);
        out->step_max     = api_field_num(s_L, "step_max", 1.0f);
        out->slot         = api_field_num(s_L, "slot", 0.0f);
        out->block_back  = api_field_num(s_L, "block_back", 0.0f);
        out->block_ahead = api_field_num(s_L, "block_ahead", 0.0f);
        ok                = 1;
    }
    lua_pop(s_L, 1);
    return ok;
}

/*  The highway tiles, by the byte the city stores.  The script answers a
 *  table keyed by that byte; anything it does not name is no highway. */
int script_rule_hiway_tiles(unsigned char *kind, unsigned char *ew, int n)
{
    int i;
    if (!s_L || !api_rule_begin(s_L, "hiway_tiles"))
        return 0;
    if (!api_rule_call(s_L, "hiway_tiles", 0, 1))
        return 0;
    if (!lua_istable(s_L, -1))
    {
        lua_pop(s_L, 1);
        return 0;
    }
    for (i = 0; i < n; ++i)
    {
        lua_pushinteger(s_L, i);
        lua_gettable(s_L, -2);
        if (lua_istable(s_L, -1))
        {
            const char *k, *a;
            lua_getfield(s_L, -1, "kind");
            k = lua_tostring(s_L, -1);
            kind[i] = (unsigned char)(!k                            ? 0
                                      : strcmp(k, "ramp") == 0      ? 2
                                      : strcmp(k, "onramp") == 0    ? 3
                                      : strcmp(k, "curve") == 0     ? 4
                                      : strcmp(k, "junction") == 0  ? 5
                                      : strcmp(k, "over") == 0      ? 6
                                                                    : 1);
            lua_pop(s_L, 1);
            lua_getfield(s_L, -1, "axis");
            a = lua_tostring(s_L, -1);
            ew[i] = (unsigned char)(a && strcmp(a, "ew") == 0);
            lua_pop(s_L, 1);
        }
        lua_pop(s_L, 1);
    }
    lua_pop(s_L, 1);
    return 1;
}

/*  What lies after a line, for the far end of a join's budget. */
int script_rule_after(int met, int free_line, float ahead, float reach)
{
    const char *k;
    int         crossing;
    if (!s_L || !api_rule_begin(s_L, "after"))
        return met;
    lua_newtable(s_L);
    lua_pushboolean(s_L, met), lua_setfield(s_L, -2, "met");
    lua_pushboolean(s_L, free_line), lua_setfield(s_L, -2, "free");
    lua_pushnumber(s_L, ahead), lua_setfield(s_L, -2, "ahead");
    lua_pushnumber(s_L, reach), lua_setfield(s_L, -2, "reach");
    if (!api_rule_call(s_L, "after", 1, 1))
        return met;
    k        = lua_tostring(s_L, -1);
    crossing = k && strcmp(k, "crossing") == 0;
    lua_pop(s_L, 1);
    return crossing;
}

/*  Which of a segment's two fits to keep. */
int script_rule_fit_choice(const char *fam, const int free_[3], const int held[3])
{
    static const char *const KEY[3] = {"corners", "tight", "nodes"};
    const char              *k;
    int                      i, keep;
    if (!s_L || !api_rule_begin(s_L, "fit_choice"))
        return 1;
    lua_newtable(s_L);
    lua_pushstring(s_L, fam), lua_setfield(s_L, -2, "family");
    lua_newtable(s_L);
    for (i = 0; i < 3; ++i)
        lua_pushinteger(s_L, free_[i]), lua_setfield(s_L, -2, KEY[i]);
    lua_setfield(s_L, -2, "free");
    lua_newtable(s_L);
    for (i = 0; i < 3; ++i)
        lua_pushinteger(s_L, held[i]), lua_setfield(s_L, -2, KEY[i]);
    lua_setfield(s_L, -2, "held");
    if (!api_rule_call(s_L, "fit_choice", 1, 1))
        return 1;
    k    = lua_tostring(s_L, -1);
    keep = !k || strcmp(k, "free") == 0;
    lua_pop(s_L, 1);
    return keep;
}

/*  How a ramp's foot meets the road it lands on. */
int script_rule_ramp_fork(int straight, int along, int against)
{
    int fork;
    if (!s_L || !api_rule_begin(s_L, "ramp_fork"))
        return 0;
    lua_newtable(s_L);
    lua_pushboolean(s_L, straight), lua_setfield(s_L, -2, "straight");
    lua_pushboolean(s_L, along), lua_setfield(s_L, -2, "along");
    lua_pushboolean(s_L, against), lua_setfield(s_L, -2, "against");
    if (!api_rule_call(s_L, "ramp_fork", 1, 1))
        return 0;
    fork = (int)lua_tointeger(s_L, -1);
    lua_pop(s_L, 1);
    return fork;
}

/*  How fast a car may go for the car ahead of it. */
float script_rule_car_follow(float gap, float v, float stop, float free)
{
    float out;
    if (!s_L || !api_rule_begin(s_L, "car_follow"))
        return v;
    lua_newtable(s_L);
    lua_pushnumber(s_L, gap), lua_setfield(s_L, -2, "gap");
    lua_pushnumber(s_L, v), lua_setfield(s_L, -2, "speed");
    lua_pushnumber(s_L, stop), lua_setfield(s_L, -2, "stop");
    lua_pushnumber(s_L, free), lua_setfield(s_L, -2, "free");
    if (!api_rule_call(s_L, "car_follow", 1, 1))
        return v;
    out = (float)lua_tonumber(s_L, -1);
    lua_pop(s_L, 1);
    return out;
}

/*  How fast a car may go for what holds it ahead. */
float script_rule_car_hold(float to_end, int hold, float line, float v, float dt)
{
    float out;
    if (!s_L || !api_rule_begin(s_L, "car_hold"))
        return v;
    lua_newtable(s_L);
    lua_pushnumber(s_L, to_end), lua_setfield(s_L, -2, "ahead");
    lua_pushboolean(s_L, hold), lua_setfield(s_L, -2, "held");
    lua_pushnumber(s_L, line), lua_setfield(s_L, -2, "line");
    lua_pushnumber(s_L, v), lua_setfield(s_L, -2, "speed");
    lua_pushnumber(s_L, dt), lua_setfield(s_L, -2, "step");
    if (!api_rule_call(s_L, "car_hold", 1, 1))
        return v;
    out = (float)lua_tonumber(s_L, -1);
    lua_pop(s_L, 1);
    return out;
}

/*  Where a highway band's walk begins. */
int script_rule_band_start(int back, int on)
{
    const char *k;
    int         way;
    if (!s_L || !api_rule_begin(s_L, "band_start"))
        return 0;
    lua_newtable(s_L);
    lua_pushboolean(s_L, back), lua_setfield(s_L, -2, "back");
    lua_pushboolean(s_L, on), lua_setfield(s_L, -2, "on");
    if (!api_rule_call(s_L, "band_start", 1, 1))
        return 0;
    k   = lua_tostring(s_L, -1);
    way = !k ? 0 : strcmp(k, "forward") == 0 ? 1
                   : strcmp(k, "backward") == 0 ? -1
                                                : 0;
    lua_pop(s_L, 1);
    return way;
}

/*  Where a ramp's descent runs along the deck. */
int script_rule_ramp_span(float at, int len, int leaves, int sgn,
                          float *top, float *foot, float *total, float *ds)
{
    if (!s_L || !api_rule_begin(s_L, "ramp_span"))
        return 0;
    lua_newtable(s_L);
    lua_pushnumber(s_L, at), lua_setfield(s_L, -2, "at");
    lua_pushinteger(s_L, len), lua_setfield(s_L, -2, "len");
    lua_pushboolean(s_L, leaves), lua_setfield(s_L, -2, "leaves");
    lua_pushinteger(s_L, sgn), lua_setfield(s_L, -2, "sgn");
    if (!api_rule_call(s_L, "ramp_span", 1, 1))
        return 0;
    if (!lua_istable(s_L, -1))
    {
        lua_pop(s_L, 1);
        return 0;
    }
    *top   = api_field_num(s_L, "top", 0.0f);
    *foot  = api_field_num(s_L, "foot", 0.0f);
    *total = api_field_num(s_L, "total", 0.0f);
    *ds    = api_field_num(s_L, "along", 0.0f);
    lua_pop(s_L, 1);
    return 1;
}

/*  One class for a whole segment. */
int script_rule_seg_class(const int counts[3])
{
    int cls, i;
    if (!s_L || !api_rule_begin(s_L, "seg_class"))
        return 0;
    lua_createtable(s_L, 3, 0);
    for (i = 0; i < 3; ++i)
        lua_pushinteger(s_L, counts[i]), lua_rawseti(s_L, -2, i + 1);
    if (!api_rule_call(s_L, "seg_class", 1, 1))
        return 0;
    cls = (int)lua_tointeger(s_L, -1);
    lua_pop(s_L, 1);
    return cls < 0 ? 0 : cls > 2 ? 2 : cls;
}

/*  Which way an on-ramp's taper lies, and how a facing pair share. */
int script_rule_ramp_side(int free_side, int room, int room_back)
{
    int keep;
    if (!s_L || !api_rule_begin(s_L, "ramp_side"))
        return 1;
    lua_newtable(s_L);
    lua_pushboolean(s_L, free_side), lua_setfield(s_L, -2, "free");
    lua_pushinteger(s_L, room), lua_setfield(s_L, -2, "room");
    lua_pushinteger(s_L, room_back), lua_setfield(s_L, -2, "room_back");
    if (!api_rule_call(s_L, "ramp_side", 1, 1))
        return 1;
    keep = lua_isnil(s_L, -1) || lua_toboolean(s_L, -1);
    lua_pop(s_L, 1);
    return keep;
}

int script_rule_ramp_share(float gap, int cap)
{
    int len;
    if (!s_L || !api_rule_begin(s_L, "ramp_share"))
        return -1;
    lua_newtable(s_L);
    lua_pushnumber(s_L, gap), lua_setfield(s_L, -2, "gap");
    lua_pushinteger(s_L, cap), lua_setfield(s_L, -2, "cap");
    if (!api_rule_call(s_L, "ramp_share", 1, 1))
        return -1;
    len = lua_isnumber(s_L, -1) ? (int)lua_tointeger(s_L, -1) : -1;
    lua_pop(s_L, 1);
    return len;
}

/*  A rule that answers a plain set of the city's building bytes. */
int script_rule_byte_set(const char *rule, unsigned char *set, int n)
{
    int i;
    if (!s_L || !api_rule_begin(s_L, rule))
        return 0;
    if (!api_rule_call(s_L, rule, 0, 1))
        return 0;
    if (!lua_istable(s_L, -1))
    {
        lua_pop(s_L, 1);
        return 0;
    }
    for (i = 0; i < n; ++i)
    {
        lua_pushinteger(s_L, i);
        lua_gettable(s_L, -2);
        set[i] = (unsigned char)lua_toboolean(s_L, -1);
        lua_pop(s_L, 1);
    }
    lua_pop(s_L, 1);
    return 1;
}

/*  The road tiles, by the byte the city stores. */
int script_rule_road_tiles(unsigned char *carries, int n)
{
    int i;
    if (!s_L || !api_rule_begin(s_L, "road_tiles"))
        return 0;
    if (!api_rule_call(s_L, "road_tiles", 0, 1))
        return 0;
    if (!lua_istable(s_L, -1))
    {
        lua_pop(s_L, 1);
        return 0;
    }
    for (i = 0; i < n; ++i)
    {
        const char *k;
        lua_pushinteger(s_L, i);
        lua_gettable(s_L, -2);
        k = lua_tostring(s_L, -1);
        carries[i] = (unsigned char)(!k ? 0 : strcmp(k, "road") == 0 ? 1
                                              : strcmp(k, "crossing") == 0 ? 2
                                              : strcmp(k, "under") == 0    ? 3
                                                                           : 0);
        lua_pop(s_L, 1);
    }
    lua_pop(s_L, 1);
    return 1;
}

int script_rule_family(const char *fam, float width, ScriptFamily *out)
{
    int ok = 0;
    if (!s_L || !api_rule_begin(s_L, "family"))
        return 0;
    lua_newtable(s_L);
    lua_pushstring(s_L, fam), lua_setfield(s_L, -2, "name");
    lua_pushnumber(s_L, width), lua_setfield(s_L, -2, "width");
    if (!api_rule_call(s_L, "family", 1, 1))
        return 0;
    if (lua_istable(s_L, -1))
    {
        int t = lua_gettop(s_L);
        ok    = 1;
        lua_getfield(s_L, t, "footway");
        if (lua_istable(s_L, -1))
        {
            out->walks         = 1;
            out->inner         = api_field_num(s_L, "inner", 1.0f);
            out->edge          = api_field_num(s_L, "edge", 1.0f);
            out->at_junction   = api_field_num(s_L, "at_junction", 0.0f);
            out->parallel      = api_field_num(s_L, "parallel", 1.0f);
            out->look          = api_field_num(s_L, "look", 0.0f);
            out->mouth         = api_field_num(s_L, "mouth", 0.0f);
            out->slot_strip    = api_field_num(s_L, "slot_strip", 0.0f);
            out->slot_junction = api_field_num(s_L, "slot_junction", 0.0f);
            out->slot_cross    = api_field_num(s_L, "slot_cross", 0.0f);
        }
        lua_pop(s_L, 1);
        lua_getfield(s_L, t, "junction");
        if (lua_istable(s_L, -1))
        {
            out->junc_inset = api_field_num(s_L, "inset", 0.0f);
            out->junc_far   = api_field_num(s_L, "far", 0.0f);
        }
        lua_pop(s_L, 1);
        lua_getfield(s_L, t, "track");
        if (lua_istable(s_L, -1))
        {
            out->gauge   = api_field_num(s_L, "gauge", 0.0f);
            out->through = api_field_num(s_L, "through", 0.0f);
            out->second  = api_field_num(s_L, "second", 0.0f);
        }
        lua_pop(s_L, 1);
        lua_getfield(s_L, t, "approach");
        if (lua_istable(s_L, -1))
        {
            out->app_near = api_field_num(s_L, "near", 0.0f);
            out->app_far  = api_field_num(s_L, "far", 0.0f);
        }
        lua_pop(s_L, 1);
        lua_getfield(s_L, t, "strip");
        if (lua_istable(s_L, -1))
        {
            out->step_run  = api_field_num(s_L, "step_run", 1.0f);
            out->step_arc  = api_field_num(s_L, "step_arc", 1.0f);
            out->lift      = api_field_num(s_L, "lift", 0.0f);
            out->lift_min  = api_field_num(s_L, "lift_min", 0.0f);
            out->cut       = api_field_num(s_L, "cut", 0.0f);
            out->dip       = api_field_num(s_L, "dip", 0.0f);
            out->mark_wide = api_field_num(s_L, "mark_wide", 0.0f);
            out->mark_lift = api_field_num(s_L, "mark_lift", 0.0f);
            out->mark_high = api_field_num(s_L, "mark_high", 0.0f);
            out->mark_slot = api_field_num(s_L, "mark_slot", 0.0f);
            out->line_wide = api_field_num(s_L, "line_wide", 0.0f);
        }
        lua_pop(s_L, 1);
        lua_getfield(s_L, t, "lane");
        if (lua_istable(s_L, -1))
        {
            int k;
            out->lane_rmin      = api_field_num(s_L, "rmin", 0.0f);
            out->lane_step_run  = api_field_num(s_L, "step_run", 1.0f);
            out->lane_step_arc  = api_field_num(s_L, "step_arc", 1.0f);
            out->lane_step_ramp = api_field_num(s_L, "step_ramp", 1.0f);
            out->lane_slot      = api_field_num(s_L, "slot", 0.0f);
            out->lane_wire      = api_field_num(s_L, "wire", 0.0f);
            out->lane_lift      = api_field_num(s_L, "lift", 0.0f);
            out->lane_join      = api_field_num(s_L, "join", 0.0f);
            out->lane_aim       = api_field_num(s_L, "aim", 0.0f);
            out->lane_edge      = api_field_num(s_L, "edge", 0.0f);
            out->lane_reach     = api_field_num(s_L, "reach", 0.0f);
            out->crossing_centre  = api_field_num(s_L, "crossing_centre", 0.0f);
            out->arm_own          = api_field_num(s_L, "arm_own", 0.0f);
            out->arm_base         = api_field_num(s_L, "arm_base", 0.0f);
            out->shelf_along      = api_field_num(s_L, "shelf_along", 0.0f);
            out->shelf_reach      = api_field_num(s_L, "shelf_reach", 0.0f);
            out->shelf_batter     = api_field_num(s_L, "shelf_batter", 0.0f);
            out->class_avenue     = api_field_num(s_L, "class_avenue", 0.0f);
            out->class_boulevard  = api_field_num(s_L, "class_boulevard", 0.0f);
            out->tile_inset       = api_field_num(s_L, "tile_inset", 0.0f);
            out->cap_kerb         = api_field_num(s_L, "cap_kerb", 0.0f);
            out->cross_share      = api_field_num(s_L, "cross_share", 0.0f);
            out->lane_pick_dot    = api_field_num(s_L, "pick_dot", 0.0f);
            out->lane_cross_reach = api_field_num(s_L, "cross_reach", 0.0f);
            out->lane_cross_ahead = api_field_num(s_L, "cross_ahead", 0.0f);
            out->lane_cross_aside = api_field_num(s_L, "cross_aside", 0.0f);
            out->lane_cross_dot   = api_field_num(s_L, "cross_dot", 0.0f);
            out->lane_cross_spot  = api_field_num(s_L, "cross_spot", 0.0f);
            out->lane_cross_off   = api_field_num(s_L, "cross_off", 0.0f);
            out->ramp_outer       = api_field_num(s_L, "ramp_outer", 0.0f);
            out->ramp_snap        = api_field_num(s_L, "ramp_snap", 0.0f);
            out->ramp_meet_cos    = api_field_num(s_L, "ramp_meet_cos", 0.0f);
            out->ramp_meet_sin    = api_field_num(s_L, "ramp_meet_sin", 0.0f);
            out->ramp_lane_off    = api_field_num(s_L, "ramp_lane_off", 0.0f);
            out->ramp_merge_along = api_field_num(s_L, "ramp_merge_along", 0.0f);
            out->ramp_taper       = api_field_num(s_L, "ramp_taper", 0.0f);
            out->band_reach       = api_field_num(s_L, "band_reach", 0.0f);
            out->band_off         = api_field_num(s_L, "band_off", 0.0f);
            out->band_dot         = api_field_num(s_L, "band_dot", 0.0f);
            out->band_ahead       = api_field_num(s_L, "band_ahead", 0.0f);
            out->band_aside       = api_field_num(s_L, "band_aside", 0.0f);
            out->band_apart       = api_field_num(s_L, "band_apart", 0.0f);
            out->band_abreast     = api_field_num(s_L, "band_abreast", 0.0f);
            out->band_outer       = api_field_num(s_L, "band_outer", 0.0f);
            out->band_taper_far   = api_field_num(s_L, "band_taper_far", 0.0f);
            out->band_taper_near  = api_field_num(s_L, "band_taper_near", 0.0f);
            out->band_taper_gap   = api_field_num(s_L, "band_taper_gap", 0.0f);
            out->band_taper_room  = api_field_num(s_L, "band_taper_room", 0.0f);
            out->band_road_dot    = api_field_num(s_L, "band_road_dot", 0.0f);
            lua_getfield(s_L, -1, "deck");
            for (k = 0; k < 3; ++k)
            {
                lua_rawgeti(s_L, -1, k + 1);
                out->deck_lane[k] = (float)lua_tonumber(s_L, -1);
                lua_pop(s_L, 1);
            }
            lua_pop(s_L, 1);
        }
        lua_pop(s_L, 1);
    }
    lua_pop(s_L, 1);
    return ok;
}

int script_rule_crossing_frame(int col, int row, float sn, float road, float rail, ScriptXing *out)
{
    int ok = 0;
    if (!s_L || !api_rule_begin(s_L, "crossing_frame"))
        return 0;
    lua_newtable(s_L);
    lua_pushinteger(s_L, col), lua_setfield(s_L, -2, "col");
    lua_pushinteger(s_L, row), lua_setfield(s_L, -2, "row");
    lua_pushnumber(s_L, sn), lua_setfield(s_L, -2, "sin");
    lua_pushnumber(s_L, road), lua_setfield(s_L, -2, "road");
    lua_pushnumber(s_L, rail), lua_setfield(s_L, -2, "rail");
    if (!api_rule_call(s_L, "crossing_frame", 1, 1))
        return 0;
    if (lua_istable(s_L, -1))
    {
        out->reach = api_field_num(s_L, "reach", 0.0f);
        out->mast  = api_field_num(s_L, "mast", 0.0f);
        out->bed   = api_field_num(s_L, "bed", 0.0f);
        out->lift  = api_field_num(s_L, "lift", 0.0f);
        out->slot  = api_field_num(s_L, "slot", 0.0f);
        ok         = 1;
    }
    lua_pop(s_L, 1);
    return ok;
}

int script_rule_crossing_marks(float reach, float mast, float limit, float road,
                               float cx, float cy, float fx, float fy, float gx, float gy,
                               ScriptApproach *out, int max)
{
    int n = 0, i;
    if (!s_L || !api_rule_begin(s_L, "crossing_marks"))
        return 0;
    lua_newtable(s_L);
    lua_pushnumber(s_L, reach), lua_setfield(s_L, -2, "reach");
    lua_pushnumber(s_L, mast), lua_setfield(s_L, -2, "mast");
    lua_pushnumber(s_L, limit), lua_setfield(s_L, -2, "limit");
    lua_pushnumber(s_L, road), lua_setfield(s_L, -2, "road");
    /*  The approach's own frame, so the rule may lay the stop line
     *  itself rather than describe it: the middle of the panel, the way
     *  the driver faces, and the driver's right. */
    lua_pushnumber(s_L, cx), lua_setfield(s_L, -2, "x");
    lua_pushnumber(s_L, cy), lua_setfield(s_L, -2, "y");
    lua_pushnumber(s_L, fx), lua_setfield(s_L, -2, "fx");
    lua_pushnumber(s_L, fy), lua_setfield(s_L, -2, "fy");
    lua_pushnumber(s_L, gx), lua_setfield(s_L, -2, "gx");
    lua_pushnumber(s_L, gy), lua_setfield(s_L, -2, "gy");
    if (!api_rule_call(s_L, "crossing_marks", 1, 1))
        return 0;
    if (lua_istable(s_L, -1))
    {
        int have = (int)lua_rawlen(s_L, -1);
        n = have < max ? have : max;
        for (i = 0; i < n; ++i)
        {
            const char *nm;
            lua_rawgeti(s_L, -1, i + 1);
            out[i].out    = api_field_num(s_L, "out", 0.0f);
            out[i].across = api_field_num(s_L, "across", 0.0f);
            lua_getfield(s_L, -1, "model");
            nm = lua_tostring(s_L, -1);
            snprintf(out[i].model, sizeof out[i].model, "%s", nm ? nm : "");
            lua_pop(s_L, 2);
        }
    }
    lua_pop(s_L, 1);
    return n;
}

float script_rule_gate(float angle, float near, float dt)
{
    float v = angle;
    if (!s_L || !api_rule_begin(s_L, "gate"))
        return angle;
    lua_newtable(s_L);
    lua_pushnumber(s_L, angle), lua_setfield(s_L, -2, "angle");
    lua_pushnumber(s_L, near), lua_setfield(s_L, -2, "near");
    lua_pushnumber(s_L, dt), lua_setfield(s_L, -2, "dt");
    if (!api_rule_call(s_L, "gate", 1, 1))
        return angle;
    if (lua_isnumber(s_L, -1))
        v = (float)lua_tonumber(s_L, -1);
    lua_pop(s_L, 1);
    return v;
}

int script_rule_rail_marks(float len, int ahead, int behind, const float *cross, int ncross,
                           ScriptMark *out, int max)
{
    int n = 0, i;
    if (!s_L || !api_rule_begin(s_L, "rail_marks"))
        return 0;
    lua_newtable(s_L);
    lua_pushnumber(s_L, len), lua_setfield(s_L, -2, "len");
    lua_pushboolean(s_L, ahead), lua_setfield(s_L, -2, "ahead");
    lua_pushboolean(s_L, behind), lua_setfield(s_L, -2, "behind");
    lua_newtable(s_L);
    for (i = 0; i < ncross; ++i)
        lua_pushnumber(s_L, cross[i]), lua_rawseti(s_L, -2, i + 1);
    lua_setfield(s_L, -2, "crossings");
    if (!api_rule_call(s_L, "rail_marks", 1, 1))
        return 0;
    if (lua_istable(s_L, -1))
    {
        int have = (int)lua_rawlen(s_L, -1);
        n = have < max ? have : max;
        for (i = 0; i < n; ++i)
        {
            const char *nm;
            lua_rawgeti(s_L, -1, i + 1);
            out[i].at = api_field_num(s_L, "at", 0.0f);
            out[i].side = api_field_num(s_L, "side", 1.0f);
            out[i].out = api_field_num(s_L, "out", 0.0f);
            out[i].signal = (int)api_field_num(s_L, "signal", -1.0f);
            lua_getfield(s_L, -1, "face");
            nm = lua_tostring(s_L, -1);
            out[i].to_map = nm && strcmp(nm, "map") == 0;
            lua_pop(s_L, 1);
            lua_getfield(s_L, -1, "clear");
            out[i].clear = lua_toboolean(s_L, -1);
            lua_pop(s_L, 1);
            lua_getfield(s_L, -1, "model");
            nm = lua_tostring(s_L, -1);
            snprintf(out[i].model, sizeof out[i].model, "%s", nm ? nm : "");
            lua_pop(s_L, 2);
        }
    }
    lua_pop(s_L, 1);
    return n;
}

int script_rule_lamps(float cls, float len, ScriptLamp *out, int max)
{
    int n = 0;
    if (!s_L || !api_rule_begin(s_L, "lamps"))
        return 0;
    lua_pushnumber(s_L, cls);
    lua_pushnumber(s_L, len);
    if (!api_rule_call(s_L, "lamps", 2, 1))
        return 0;
    if (lua_istable(s_L, -1))
    {
        int i, len2 = (int)lua_rawlen(s_L, -1);
        n = len2 < max ? len2 : max;
        for (i = 0; i < n; ++i)
        {
            lua_rawgeti(s_L, -1, i + 1);
            lua_getfield(s_L, -1, "at"), out[i].at = (float)lua_tonumber(s_L, -1), lua_pop(s_L, 1);
            lua_getfield(s_L, -1, "side"), out[i].side = (float)lua_tonumber(s_L, -1), lua_pop(s_L, 1);
            lua_getfield(s_L, -1, "in"), out[i].in = (float)lua_tonumber(s_L, -1), lua_pop(s_L, 1);
            lua_pop(s_L, 1);
        }
    }
    lua_pop(s_L, 1);
    return n;
}

int script_rule_lanes(const char *fam, int cls, float *off, int max)
{
    int n = -1;
    if (!s_L || !api_rule_begin(s_L, "lanes"))
        return -1;
    lua_pushstring(s_L, fam);
    lua_pushinteger(s_L, cls);
    if (!api_rule_call(s_L, "lanes", 2, 1))
        return -1;
    if (lua_istable(s_L, -1))
    {
        int i, len = (int)lua_rawlen(s_L, -1);
        n = len < max ? len : max;
        for (i = 0; i < n; ++i)
        {
            lua_rawgeti(s_L, -1, i + 1);
            off[i] = (float)lua_tonumber(s_L, -1);
            lua_pop(s_L, 1);
        }
    }
    lua_pop(s_L, 1);
    return n;
}

/*  One corner of a junction's outline.  What is known about it goes in
 *  as one table -- the numbers that are not yet known are simply absent
 *  from it -- and the answer is a table to round the corner with, false
 *  to leave it square, or nothing to drop it. */
int script_rule_corner(int col, int row, float phi, float grow, float width,
                       const float *room, const float *back, const float *fwd, ScriptCorner *out)
{
    int what = CORNER_SQUARE;
    if (!s_L || !api_rule_begin(s_L, "corner"))
        return CORNER_SQUARE;
    lua_newtable(s_L);
    lua_pushinteger(s_L, col), lua_setfield(s_L, -2, "col");
    lua_pushinteger(s_L, row), lua_setfield(s_L, -2, "row");
    lua_pushnumber(s_L, phi), lua_setfield(s_L, -2, "phi");
    lua_pushnumber(s_L, grow), lua_setfield(s_L, -2, "grow");
    lua_pushnumber(s_L, width), lua_setfield(s_L, -2, "width");
    if (room)
        lua_pushnumber(s_L, *room), lua_setfield(s_L, -2, "room");
    if (back)
        lua_pushnumber(s_L, *back), lua_setfield(s_L, -2, "back");
    if (fwd)
        lua_pushnumber(s_L, *fwd), lua_setfield(s_L, -2, "fwd");
    if (!api_rule_call(s_L, "corner", 1, 1))
        return CORNER_SQUARE;
    if (lua_istable(s_L, -1))
    {
        what = CORNER_ROUND;
        lua_getfield(s_L, -1, "tangent"), out->tangent = (float)lua_tonumber(s_L, -1), lua_pop(s_L, 1);
        lua_getfield(s_L, -1, "radius"), out->radius = (float)lua_tonumber(s_L, -1), lua_pop(s_L, 1);
        lua_getfield(s_L, -1, "steps"), out->steps = (float)lua_tonumber(s_L, -1), lua_pop(s_L, 1);
    }
    else
        what = lua_isnil(s_L, -1) ? CORNER_DROP : CORNER_SQUARE;
    lua_pop(s_L, 1);
    return what;
}

int script_rules(void)
{
    int n = 0;
    if (!s_L)
        return 0;
    lua_getglobal(s_L, "arc");
    lua_getfield(s_L, -1, "rules");
    lua_pushnil(s_L);
    while (lua_next(s_L, -2))
    {
        if (lua_isfunction(s_L, -1))
            ++n;
        lua_pop(s_L, 1);
    }
    lua_pop(s_L, 2);
    return n;
}

#else /* no Lua in this build: every answer is "the C decides" */

int         script_open(const char *path) { (void)path; return -1; }
void        script_close(void) {}
int         script_on(void) { return 0; }
const char *script_path(void) { return NULL; }
int         script_eval(const char *src) { (void)src; return -1; }
int         script_stale(void) { return 0; }
int         script_reload(void) { return 0; }
const char *script_error(void) { return NULL; }
int         script_generation(void) { return 0; }
int         script_take_dirty(void) { return 0; }
int         script_rules(void) { return 0; }
int         script_rule_control(int col, int row, const int cls[4], const int traf[4], int busy, int *out)
{
    (void)col, (void)row, (void)cls, (void)traf, (void)busy, (void)out;
    return 0;
}
int script_rule_crossing_at(int col, int row, int arm, int ctrl, int pavement, float cs, float span, float *out)
{
    int ok = 0;
    if (!s_L || !api_rule_begin(s_L, "crossing_at"))
        return 0;
    lua_newtable(s_L);
    lua_pushinteger(s_L, col), lua_setfield(s_L, -2, "col");
    lua_pushinteger(s_L, row), lua_setfield(s_L, -2, "row");
    lua_pushinteger(s_L, arm), lua_setfield(s_L, -2, "arm");
    lua_pushinteger(s_L, ctrl), lua_setfield(s_L, -2, "control");
    lua_pushboolean(s_L, pavement), lua_setfield(s_L, -2, "pavement");
    lua_pushnumber(s_L, cs), lua_setfield(s_L, -2, "cos");
    lua_pushnumber(s_L, span), lua_setfield(s_L, -2, "span");
    if (!api_rule_call(s_L, "crossing_at", 1, 1))
        return 0;
    if (lua_isnumber(s_L, -1))
        *out = (float)lua_tonumber(s_L, -1), ok = *out > 0.0f;
    lua_pop(s_L, 1);
    return ok;
}

int script_rule_crossing(int col, int row, int arm, int ctrl, float want, float room, float straight, float *out)
{
    (void)col, (void)row, (void)arm, (void)ctrl, (void)want, (void)room, (void)straight, (void)out;
    return 0;
}
/*  A prop the script builds itself.  Its place goes in as one table and
 *  the answer is whether it drew: a rule that draws nothing, or none at
 *  all, leaves the C to build its own. */
int script_rule_prop(const char *name, const ScriptProp *at)
{
    int drew = 0;
    if (!s_L || !at || !api_rule_begin(s_L, name))
        return 0;
    lua_newtable(s_L);
    lua_pushnumber(s_L, at->x), lua_setfield(s_L, -2, "x");
    lua_pushnumber(s_L, at->y), lua_setfield(s_L, -2, "y");
    lua_pushnumber(s_L, at->z), lua_setfield(s_L, -2, "z");
    lua_pushnumber(s_L, at->fx), lua_setfield(s_L, -2, "fx");
    lua_pushnumber(s_L, at->fy), lua_setfield(s_L, -2, "fy");
    lua_pushnumber(s_L, at->size), lua_setfield(s_L, -2, "size");
    lua_pushinteger(s_L, at->col), lua_setfield(s_L, -2, "col");
    lua_pushinteger(s_L, at->row), lua_setfield(s_L, -2, "row");
    lua_pushinteger(s_L, at->arm), lua_setfield(s_L, -2, "arm");
    lua_pushinteger(s_L, at->links), lua_setfield(s_L, -2, "links");
    lua_pushnumber(s_L, at->phase), lua_setfield(s_L, -2, "phase");
    lua_pushnumber(s_L, at->angle), lua_setfield(s_L, -2, "angle");
    lua_pushnumber(s_L, at->len), lua_setfield(s_L, -2, "len");
    if (!api_rule_call(s_L, name, 1, 1))
        return 0;
    drew = !lua_isnil(s_L, -1) && lua_toboolean(s_L, -1);
    lua_pop(s_L, 1);
    return drew;
}

int script_rule_corner(int col, int row, float phi, float grow, float width,
                       const float *room, const float *back, const float *fwd, ScriptCorner *out)
{
    (void)col, (void)row, (void)phi, (void)grow, (void)width, (void)room, (void)back, (void)fwd, (void)out;
    return CORNER_SQUARE;
}
int script_rule_strip(void)
{
    return 0;
}
int script_rule_join(int cross, int free_line, char how[][12], int max)
{
    (void)cross, (void)free_line, (void)how, (void)max;
    return 0;
}
int script_rule_traffic(ScriptTraffic *out)
{
    (void)out;
    return 0;
}
int script_rule_family(const char *fam, float width, ScriptFamily *out)
{
    (void)fam, (void)width, (void)out;
    return 0;
}
int script_rule_hiway_tiles(unsigned char *kind, unsigned char *ew, int n)
{
    (void)kind, (void)ew, (void)n;
    return 0;
}
int script_rule_road_tiles(unsigned char *carries, int n)
{
    (void)carries, (void)n;
    return 0;
}
int script_rule_byte_set(const char *rule, unsigned char *set, int n)
{
    (void)rule, (void)set, (void)n;
    return 0;
}
int script_rule_after(int met, int free_line, float ahead, float reach)
{
    (void)free_line, (void)ahead, (void)reach;
    return met;
}
int script_rule_fit_choice(const char *fam, const int free_[3], const int held[3])
{
    (void)fam, (void)free_, (void)held;
    return 1;
}
int script_rule_ramp_fork(int straight, int along, int against)
{
    (void)straight, (void)along, (void)against;
    return 0;
}
int script_rule_ramp_side(int free_side, int room, int room_back)
{
    (void)free_side, (void)room, (void)room_back;
    return 1;
}
int script_rule_seg_class(const int counts[3])
{
    (void)counts;
    return 0;
}
int script_rule_ramp_span(float at, int len, int leaves, int sgn,
                          float *top, float *foot, float *total, float *ds)
{
    (void)at, (void)len, (void)leaves, (void)sgn;
    (void)top, (void)foot, (void)total, (void)ds;
    return 0;
}
int script_rule_band_start(int back, int on)
{
    (void)back, (void)on;
    return 0;
}
float script_rule_car_follow(float gap, float v, float stop, float free)
{
    (void)gap, (void)stop, (void)free;
    return v;
}
float script_rule_car_hold(float to_end, int hold, float line, float v, float dt)
{
    (void)to_end, (void)hold, (void)line, (void)dt;
    return v;
}
int script_rule_ramp_share(float gap, int cap)
{
    (void)gap, (void)cap;
    return -1;
}
int script_rule_crossing_frame(int col, int row, float sn, float road, float rail, ScriptXing *out)
{
    (void)col, (void)row, (void)sn, (void)road, (void)rail, (void)out;
    return 0;
}
int script_rule_crossing_marks(float reach, float mast, float limit, float road,
                               float cx, float cy, float fx, float fy, float gx, float gy,
                               ScriptApproach *out, int max)
{
    (void)reach, (void)mast, (void)limit, (void)road, (void)cx, (void)cy;
    (void)fx, (void)fy, (void)gx, (void)gy, (void)out, (void)max;
    return 0;
}
float script_rule_gate(float angle, float near, float dt)
{
    (void)near, (void)dt;
    return angle;
}
int script_rule_rail_marks(float len, int ahead, int behind, const float *cross, int ncross,
                           ScriptMark *out, int max)
{
    (void)len, (void)ahead, (void)behind, (void)cross, (void)ncross, (void)out, (void)max;
    return 0;
}
int script_rule_lamps(float cls, float len, ScriptLamp *out, int max)
{
    (void)cls, (void)len, (void)out, (void)max;
    return 0;
}
int script_rule_lanes(const char *fam, int cls, float *off, int max)
{
    (void)fam, (void)cls, (void)off, (void)max;
    return -1;
}
void script_emit_open(void *mesh, const void *city, uint8_t mask_bit, float order)
{
    (void)mesh, (void)city, (void)mask_bit, (void)order;
}
void script_emit_close(void) {}
int  script_rule_prop(const char *name, const ScriptProp *at)
{
    (void)name, (void)at;
    return 0;
}

#endif
