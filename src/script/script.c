/*  script.c: the Lua state, the script it runs, and the watch that reads
 *  it again when it changes.  See script.h. */
#include "script.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "dump.h"
#include "log.h"

#include "internal.h"
#include "opt.h"

#ifdef ARC_LUAJIT
    #include <errno.h>
    #include <pthread.h>
#endif

lua_State *s_L;
int        s_dirty;

static char        s_dir[1024];
static char        s_path[1024];
static int         s_is_dir;
static struct stat s_stat;
static int         s_have_stat;
static char       s_err[1024];
/*  How many files the last reading read.  A reading is one step.  A file
 *  half read is no reading at all.  So this is a count for a report
 *  rather than a progress to watch while it happens. */
static int        s_files;
static int        s_gen;
/*  A number that changes whenever what the scripts describe changes: a
 *  reading of them, or one of them asking for the world again.  A build
 *  carries it in its key, so a mesh built under another reading is
 *  never taken as one that still stands. */
static int        s_stamp;
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

int script_stamp(void)
{
    return s_stamp;
}

/*  What the script asked to be told.  A message goes through the log,
 *  and a report line through the dump sink.  So a script's output obeys
 *  the same two switches every other part of the program does.
 *
 *  The log has LEVELS, and a script needs all three of them.  A rule
 *  that finds the world it was handed cannot be drawn.  A movement with
 *  no lane left, a path that will not fit.  Is reporting a FAULT, and a
 *  fault said at note level is a fault nobody sees: the levels are what
 *  a reader filters on.  So a script that can only say `note` cannot
 *  report anything as what it is. */
static int l_say(lua_State *L, int level)
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
    switch (level)
    {
    case 2: R_ERR("lua", "%s", lua_tostring(L, -1)); break;
    case 1: R_WARN("lua", "%s", lua_tostring(L, -1)); break;
    default: R_NOTE("lua", "%s", lua_tostring(L, -1)); break;
    }
    return 0;
}

static int l_log(lua_State *L)
{
    return l_say(L, 0);
}

static int l_warn(lua_State *L)
{
    return l_say(L, 1);
}

static int l_error(lua_State *L)
{
    return l_say(L, 2);
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

/*  The script asking for the world to be drawn again.  A change to a
 *  knob or a constant needs it.  A change to a rule does too. */
static int l_rebuild(lua_State *L)
{
    (void)L;
    s_dirty = 1;
    ++s_stamp;
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
 *  the state comes up.  Reading the script again does not rebuild it, so
 *  a value the script left on `arc` between reads survives. */
static void arc_open(lua_State *L)
{
    lua_newtable(L);
    lua_pushcfunction(L, l_log);
    lua_setfield(L, -2, "log");
    lua_pushcfunction(L, l_warn);
    lua_setfield(L, -2, "warn");
    lua_pushcfunction(L, l_error);
    lua_setfield(L, -2, "error");
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
    api_family_open(L);
    api_data_open(L);
    api_art_open(L);
    api_fit_open(L);
    lua_setglobal(L, "arc");
}

/*  The one place an error from Lua is turned into a line: kept for the
 *  console, said once through the log, and never thrown. */
static void fail(const char *what, const char *msg)
{
    snprintf(s_err, sizeof s_err, "%s: %s", what, msg ? msg : "?");
    R_ERR("lua", "%s", s_err);
}

/*  A FAULT: a rule that raised, or a file that would not load.  It is
 *  not the same thing as a rule the scripts never set: an unset rule
 *  answers "the C decides".  This is an answer, and a rule that raised
 *  answered nothing at all.  A build that carries on past one draws a
 *  city with pieces missing and reports success.  So the build asks
 *  after the fact and abandons itself.
 *
 *  The fault stands until the scripts are read again, which is what
 *  clears it: a broken rule cannot be got past by building twice.  Only
 *  the FIRST is logged.  A rule that raises in the strip pass raises
 *  once a strip, and a thousand identical lines say nothing the first
 *  one did not. */
static char s_fault[1024];
static int  s_n_fault;

static void fault(const char *what, const char *msg)
{
    snprintf(s_err, sizeof s_err, "%s: %s", what, msg ? msg : "?");
    if (s_n_fault++ == 0)
    {
        snprintf(s_fault, sizeof s_fault, "%s: %s", what, msg ? msg : "?");
        R_ERR("lua", "%s", s_fault);
    }
}

/*  A rule that raised is left alone until the scripts are read again.
 *  Asking it once a strip after it has already failed buys nothing.  The
 *  answer is the same, the report is the same.  Where the failure was a
 *  runaway the build spends the whole budget again at every call. */
#define RULE_DEAD_MAX 32
static char s_dead[RULE_DEAD_MAX][64];
static int  s_n_dead;

static int rule_dead(const char *name)
{
    int i;
    for (i = 0; name && i < s_n_dead; ++i)
        if (strcmp(s_dead[i], name) == 0)
            return 1;
    return 0;
}

static void rule_kill(const char *name)
{
    if (!name || rule_dead(name) || s_n_dead >= RULE_DEAD_MAX)
        return;
    snprintf(s_dead[s_n_dead++], sizeof s_dead[0], "%s", name);
}

static void fault_reset(void)
{
    s_fault[0] = 0;
    s_n_fault  = 0;
    s_n_dead   = 0;
}

const char *script_fault(void)
{
    return s_fault[0] ? s_fault : NULL;
}

int script_fault_count(void)
{
    return s_n_fault;
}

/*  Every .lua under a directory and its folders, as paths relative to
 *  it, in name order.  So a file that depends on what another sets can
 *  be named to come after it.  A folder of models reads as one run
 *  between them. */
static int name_cmp(const void *a, const void *b)
{
    /*  The elements are the names themselves, not pointers to them. */
    return strcmp((const char *)a, (const char *)b);
}

/*  The .lua files under a place, in one order, however many there are.
 *  A fixed cap here is silent at both ends: past it a script is never
 *  read.  An edit to it is never noticed. */
typedef struct
{
    char (*name)[256];
    int n, cap;
} ScriptList;

static int list_add(ScriptList *l, const char *s)
{
    if (l->n == l->cap)
    {
        int   cap = l->cap ? l->cap * 2 : 64;
        void *p   = realloc(l->name, (size_t)cap * 256);
        if (!p)
            return 0;
        l->name = p;
        l->cap  = cap;
    }
    snprintf(l->name[l->n++], 256, "%s", s);
    return 1;
}

static void dir_walk(const char *root, const char *rel, ScriptList *out)
{
    char           here[1300];
    DIR           *d;
    struct dirent *e;
    snprintf(here, sizeof here, "%s%s%s", root, rel[0] ? "/" : "", rel);
    d = opendir(here);
    if (!d)
        return;
    while ((e = readdir(d)) != NULL)
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
            dir_walk(root, sub, out);
            continue;
        }
        if (l > 4 && strcmp(e->d_name + l - 4, ".lua") == 0)
            list_add(out, sub);
    }
    closedir(d);
}

static int dir_files(const char *dir, ScriptList *out)
{
    out->n = 0;
    dir_walk(dir, "", out);
    qsort(out->name, (size_t)out->n, 256, name_cmp);
    return out->n;
}

/*  Every .lua under `dir` as full paths, in the order the program reads
 *  them.  The lint asks for this, so what it reads and the order it
 *  reads it in are the program's own and cannot drift from them.  The
 *  caller frees each path and then the array. */
int script_dir_paths(const char *dir, char ***out)
{
    ScriptList list = {NULL, 0, 0};
    int        i, n = dir_files(dir, &list);
    char     **paths = (char **)malloc((size_t)(n > 0 ? n : 1) * sizeof *paths);
    if (!paths)
    {
        free(list.name);
        return -1;
    }
    for (i = 0; i < n; ++i)
    {
        size_t len = strlen(dir) + 1 + strlen(list.name[i]) + 1;
        paths[i]   = (char *)malloc(len);
        if (!paths[i])
        {
            while (i-- > 0)
                free(paths[i]);
            free(paths);
            free(list.name);
            return -1;
        }
        snprintf(paths[i], len, "%s/%s", dir, list.name[i]);
    }
    free(list.name);
    *out = paths;
    return n;
}

/*  The newest thing in the watched place, so a change to any file in a
 *  directory is a change to the script. */
static ScriptList s_watched; /* reused, so watching costs a frame nothing */

static void watched_stamp(struct stat *out)
{
    struct stat st;
    int         i, n;
    memset(out, 0, sizeof *out);
    if (!s_is_dir)
    {
        if (stat(s_path, out) != 0)
            memset(out, 0, sizeof *out);
        return;
    }
    n = dir_files(s_path, &s_watched);
    for (i = 0; i < n; ++i)
    {
        char full[1300];
        snprintf(full, sizeof full, "%s/%s", s_path, s_watched.name[i]);
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
        fault("script", lua_tostring(s_L, -1));
        lua_pop(s_L, 1);
        return -1;
    }
    s_err[0] = 0;
    ++s_gen;
    ++s_stamp;
    ++s_files;
    return 0;
}

/*  THE MODULES A READING RAN, forgotten before the next reading.
 *  `require` keeps every module it has run in package.loaded and never
 *  runs one twice.  A reading empties arc.rules and then runs init.lua
 *  again.  Its requires would find every module kept, run none, and leave
 *  the rules empty.  So the libraries the state opened with are noted
 *  once, under BUILTIN in the registry.  A reading forgets every other
 *  module first. */
#define BUILTIN "arc.builtin_modules"

static void modules_note(lua_State *L)
{
    lua_newtable(L);
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "loaded");
    lua_pushnil(L);
    while (lua_next(L, -2))
    {
        lua_pop(L, 1);
        lua_pushvalue(L, -1);
        lua_pushboolean(L, 1);
        lua_settable(L, -6);
    }
    lua_pop(L, 2);
    lua_setfield(L, LUA_REGISTRYINDEX, BUILTIN);
}

static void modules_forget(lua_State *L)
{
    int k, n = 0;
    lua_getfield(L, LUA_REGISTRYINDEX, BUILTIN);
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "loaded");
    /*  The names to drop are gathered first: a table is not changed
     *  while it is walked. */
    lua_newtable(L);
    lua_pushnil(L);
    while (lua_next(L, -3))
    {
        lua_pop(L, 1);
        lua_pushvalue(L, -1);
        lua_rawget(L, -6);
        if (lua_isnil(L, -1))
        {
            lua_pushvalue(L, -2);
            lua_rawseti(L, -4, ++n);
        }
        lua_pop(L, 1);
    }
    for (k = 1; k <= n; ++k)
    {
        lua_rawgeti(L, -1, k);
        lua_pushnil(L);
        lua_rawset(L, -4);
    }
    lua_pop(L, 4);
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
    luavm_open(s_L);
    arc_open(s_L);
    modules_note(s_L);
    script_family_reset();
    script_data_reset();
    fault_reset();
    s_files = 0;
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

/*  Where `require` looks.  A folder of scripts is entered through its
 *  own init.lua, and everything else in it is required from there.
 *  This is what makes `require "compose.world"` find the file
 *  <dir>/compose/world.lua. */
static void require_path(const char *dir)
{
    char path[2300];
    snprintf(path, sizeof path, "%s/?.lua;%s/?/init.lua", dir, dir);
    lua_getglobal(s_L, "package");
    lua_pushstring(s_L, path);
    lua_setfield(s_L, -2, "path");
    lua_pop(s_L, 1);
}

/*  A FOLDER OF SCRIPTS is read through its init.lua and nothing else.
 *  That file requires the rest, in the order their dependencies need, so
 *  the layer says what it is in one place.  A stray .lua no line names
 *  is not read at all, where a walk of the folder would have run it.
 *
 *  A folder with no init.lua is somebody's own scripts rather than a
 *  layer.  Every .lua in it is read, in name order. */
static int run_dir(const char *dir)
{
    char        init[1300];
    struct stat st;
    ScriptList  list = {NULL, 0, 0};
    int         i, n, rc = 0;
    require_path(dir);
    snprintf(init, sizeof init, "%s/init.lua", dir);
    if (stat(init, &st) == 0 && S_ISREG(st.st_mode))
        return run_file(init);
    n = dir_files(dir, &list);
    for (i = 0; i < n; ++i)
    {
        char full[1300];
        snprintf(full, sizeof full, "%s/%s", dir, list.name[i]);
        if (run_file(full) != 0)
            rc = -1;
    }
    if (n > 0)
        s_gen -= n - 1;
    free(list.name);
    return rc;
}

static int run_watched(void)
{
    int rc = 0;
    if (s_dir[0] && run_dir(s_dir) != 0)
        rc = -1;
    if (!s_is_dir)
        rc = run_file(s_path) != 0 ? -1 : rc;
    else if (run_dir(s_path) != 0)
        rc = -1;
    /*  The knobs the renderer reads itself, bound to what the scripts
     *  just declared.  A name they did not declare is a fault: the
     *  renderer would draw with a nought where a width belongs.  The
     *  message names which one. */
    if (rc == 0 && !tune_bind())
    {
        fault("tune", "a knob the renderer reads is not declared");
        rc = -1;
    }
    return rc;
}

void script_close(void)
{
    free(s_watched.name);
    s_watched.name = NULL;
    s_watched.n = s_watched.cap = 0;
    script_model_reset();
    script_family_reset();
    if (s_L)
        lua_close(s_L);
    s_L         = NULL;
    s_path[0]   = 0;
    s_have_stat = 0;
    s_dirty     = 0;
}

/*  One value, written out.  A table is opened one level, in key order.
 *  This is because a console that answers "table: 0x..." has answered
 *  nothing.  Deeper than that is the script's own to print. */
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
    /*  An expression is the common case at a console.  So it is tried as
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

/*  The folder is looked at four times a second, not every frame.  A walk
 *  of it and a stat of each file costs more than a frame should spend,
 *  and a save is made by hand. */
#define STALE_EVERY_MS 250.0

int script_stale(void)
{
    static double   last;
    struct stat     st;
    struct timespec ts;
    double          now;
    if (!s_L || !s_path[0])
        return 0;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
    if (s_have_stat && now - last < STALE_EVERY_MS)
        return 0;
    last = now;
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

    /*  The rules and the models are cleared before the files run.  So
     *  what stands is exactly what this reading sets and nothing a
     *  reading of an older one left. */
    script_model_reset();
    script_material_reset();
    script_family_reset();
    script_data_reset();
    fault_reset();
    s_files = 0;
    lua_getglobal(s_L, "arc");
    lua_newtable(s_L);
    lua_setfield(s_L, -2, "rules");
    lua_pop(s_L, 1);
    modules_forget(s_L);
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
    if (rule_dead(name))
        return 0;
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

/*  When the rule being watched started.  Process time rather than the
 *  wall's: a rule is not to be failed for the machine being busy. */
static clock_t s_rule_clock;

/*  THE RUNAWAY GUARD.  A rule that never answers would hang the build,
 *  so one is watched and failed after RULE_SECONDS.
 *
 *  It watches CALLS, not instructions.  A count hook sounds cheaper,
 *  since its body would run once in a million steps.  But Lua takes its
 *  slow path on EVERY instruction while any count hook stands, and that
 *  is a fifth of a whole build.  A call hook costs one visit per Lua
 *  call and leaves the rest of the interpreter at full speed.
 *
 *  The one shape it cannot see is a loop that calls nothing at all.
 *  Every rule in the tree walks the map or reads arc.*, and both are
 *  calls.  The battery's own timeout is the backstop for the rest.
 *
 *  LuaJIT compiles nothing while any hook stands.  So on LuaJIT the hook
 *  is set only when the budget is spent.  A watchdog thread waits out the
 *  budget, and sets the hook then if the rule is still running.  A rule
 *  that answers in time runs with no hook, compiled.  LuaJIT's own
 *  profiler sets its hook from another thread in the same way. */
#ifdef ARC_LUAJIT
static pthread_mutex_t s_dog_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_dog_cv = PTHREAD_COND_INITIALIZER;
static lua_State      *s_dog_L;
static int             s_dog_up, s_dog_armed;
static unsigned        s_dog_gen;
static struct timespec s_dog_due;

static int rule_budget(void);

/*  The watchdog.  It wakes when a rule starts, and again when the
 *  rule's budget is spent.  The hook is set and cleared under the same
 *  lock, so it is never set for a rule that has already answered. */
static void *rule_dog(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&s_dog_mx);
    for (;;)
    {
        unsigned gen;
        while (!s_dog_armed)
            pthread_cond_wait(&s_dog_cv, &s_dog_mx);
        gen = s_dog_gen;
        while (s_dog_armed && s_dog_gen == gen)
            if (pthread_cond_timedwait(&s_dog_cv, &s_dog_mx, &s_dog_due) == ETIMEDOUT)
            {
                if (s_dog_armed && s_dog_gen == gen)
                    lua_sethook(s_dog_L, api_rule_runaway, LUA_MASKCALL, 0);
                break;
            }
        while (s_dog_armed && s_dog_gen == gen)
            pthread_cond_wait(&s_dog_cv, &s_dog_mx);
    }
    return NULL;
}
#endif

void api_rule_watch(lua_State *L, int on)
{
#ifdef ARC_LUAJIT
    pthread_mutex_lock(&s_dog_mx);
    if (!s_dog_up)
    {
        pthread_t t;
        s_dog_up = pthread_create(&t, NULL, rule_dog, NULL) == 0;
        if (s_dog_up)
            pthread_detach(t);
    }
    s_dog_gen++;
    if (on)
    {
        s_rule_clock = clock();
        s_dog_L      = L;
        s_dog_armed  = 1;
        clock_gettime(CLOCK_REALTIME, &s_dog_due);
        s_dog_due.tv_sec += rule_budget();
    }
    else
    {
        s_dog_armed = 0;
        lua_sethook(L, NULL, 0, 0);
    }
    pthread_cond_signal(&s_dog_cv);
    pthread_mutex_unlock(&s_dog_mx);
#else
    if (on)
    {
        s_rule_clock = clock();
        lua_sethook(L, api_rule_runaway, LUA_MASKCALL, 0);
    }
    else
        lua_sethook(L, NULL, 0, 0);
#endif
}

/*  The budget, in whole seconds.  --rule-seconds shortens it while
 *  somebody is chasing a rule that never finishes.  The default is
 *  generous on purpose, and a wait of that length every build is its own
 *  obstacle to finding the cause. */
static int rule_budget(void)
{
    int n = g_dev.rule_seconds ? atoi(g_dev.rule_seconds) : 0;
    return n > 0 ? n : RULE_SECONDS;
}

void api_rule_runaway(lua_State *L, lua_Debug *ar)
{
    /*  Reading the clock is the costly part, and a rule makes millions
     *  of calls.  So it is read once every RULE_CALLS of them, which
     *  leaves the guard exact to far less than a second. */
    static unsigned n;
    (void)ar;
    if (++n & (RULE_CALLS - 1))
        return;
    if ((double)(clock() - s_rule_clock) / (double)CLOCKS_PER_SEC < rule_budget())
        return;
    luaL_error(L, "runs away: no answer in %d seconds", rule_budget());
}

/*  A rule may ask for another rule.  The fit asks for the sweep at each
 *  of its corners.  And the runaway guard belongs to the outermost of
 *  them.  An inner call that cleared the hook on the way out would leave
 *  the rest of the outer rule running unwatched. */
static int s_rule_depth;

/*  A rule is asked for ONE answer.  A rule that wants to say more
 *  answers a table. */
int api_rule_call(lua_State *L, const char *name, int nargs)
{
    int ok;
    if (s_rule_depth++ == 0)
        api_rule_watch(L, 1);
    ok = lua_pcall(L, nargs, 1, 0) == LUA_OK;
    if (--s_rule_depth == 0)
        api_rule_watch(L, 0);
    if (ok)
        return 1;
    fault(name, lua_tostring(L, -1));
    rule_kill(name);
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

/*  A prop the script builds itself.  Its place goes in as one table and
 *  the answer is whether it drew.  A rule that draws nothing at all
 *  leaves the C to build its own.
 *
 *  A prop's own place, as a table.
 *
 *      Where it stands.
 *      Which way it faces.
 *      The ground under it.
 *      Its tile and the edges that tile joins.
 *
 *  One expression, so a rule that is ASKED and a script that asks for
 *  the prop itself read the same fields. */
void api_prop_push(lua_State *L, const ScriptProp *at)
{
    lua_newtable(L);
    lua_pushnumber(L, at->x), lua_setfield(L, -2, "x");
    lua_pushnumber(L, at->y), lua_setfield(L, -2, "y");
    lua_pushnumber(L, at->z), lua_setfield(L, -2, "z");
    lua_pushnumber(L, at->fx), lua_setfield(L, -2, "fx");
    lua_pushnumber(L, at->fy), lua_setfield(L, -2, "fy");
    lua_pushnumber(L, at->size), lua_setfield(L, -2, "size");
    lua_pushinteger(L, at->col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, at->row), lua_setfield(L, -2, "row");
    lua_pushinteger(L, at->arm), lua_setfield(L, -2, "arm");
    lua_pushinteger(L, at->links), lua_setfield(L, -2, "links");
    lua_pushnumber(L, at->phase), lua_setfield(L, -2, "phase");
    lua_pushnumber(L, at->angle), lua_setfield(L, -2, "angle");
    lua_pushnumber(L, at->len), lua_setfield(L, -2, "len");
}

/*  The family's numbers off the table on the stack top: the reading
 *  itself, so the same expression serves whoever starts it. */
void script_family_read(lua_State *L, ScriptFamily *out)
{
    int t = lua_gettop(L);
    if (!lua_istable(L, t))
    return;
    lua_getfield(L, t, "margin");
    if (lua_istable(L, -1))
    {
        out->walks         = 1;
        out->inner         = api_field_num(L, "inner", 1.0f);
        out->edge          = api_field_num(L, "edge", 1.0f);
        out->at_junction   = api_field_num(L, "at_junction", 0.0f);
        out->parallel      = api_field_num(L, "parallel", 1.0f);
        out->look          = api_field_num(L, "look", 0.0f);
        out->mouth         = api_field_num(L, "mouth", 0.0f);
        out->slot_strip    = api_field_num(L, "slot_strip", 0.0f);
        out->slot_junction = api_field_num(L, "slot_junction", 0.0f);
        out->slot_cross    = api_field_num(L, "slot_cross", 0.0f);
    }
    lua_pop(L, 1);
    lua_getfield(L, t, "junction");
    if (lua_istable(L, -1))
    {
        out->junc_inset = api_field_num(L, "inset", 0.0f);
        out->junc_far   = api_field_num(L, "far", 0.0f);
    }
    lua_pop(L, 1);
    lua_getfield(L, t, "thread");
    if (lua_istable(L, -1))
    {
        out->gauge   = api_field_num(L, "gauge", 0.0f);
        out->through = api_field_num(L, "through", 0.0f);
        out->second  = api_field_num(L, "second", 0.0f);
    }
    lua_pop(L, 1);
    lua_getfield(L, t, "approach");
    if (lua_istable(L, -1))
    {
        out->app_near = api_field_num(L, "near", 0.0f);
        out->app_far  = api_field_num(L, "far", 0.0f);
    }
    lua_pop(L, 1);
    lua_getfield(L, t, "strip");
    if (lua_istable(L, -1))
    {
        out->step_run  = api_field_num(L, "step_run", 1.0f);
        out->step_arc  = api_field_num(L, "step_arc", 1.0f);
        out->lift      = api_field_num(L, "lift", 0.0f);
        out->lift_min  = api_field_num(L, "lift_min", 0.0f);
        out->cut       = api_field_num(L, "cut", 0.0f);
        out->dip       = api_field_num(L, "dip", 0.0f);
        out->mark_wide = api_field_num(L, "mark_wide", 0.0f);
        out->mark_lift = api_field_num(L, "mark_lift", 0.0f);
        out->mark_high = api_field_num(L, "mark_high", 0.0f);
        out->mark_slot = api_field_num(L, "mark_slot", 0.0f);
        out->line_wide = api_field_num(L, "line_wide", 0.0f);
    }
    lua_pop(L, 1);
    lua_getfield(L, t, "lane");
    if (lua_istable(L, -1))
    {
        int k;
        out->lane_rmin      = api_field_num(L, "rmin", 0.0f);
        out->lane_step_run  = api_field_num(L, "step_run", 1.0f);
        out->lane_step_arc  = api_field_num(L, "step_arc", 1.0f);
        out->lane_step_spur = api_field_num(L, "step_spur", 1.0f);
        out->lane_slot      = api_field_num(L, "slot", 0.0f);
        out->lane_wire      = api_field_num(L, "wire", 0.0f);
        out->lane_lift      = api_field_num(L, "lift", 0.0f);
        out->lane_join      = api_field_num(L, "join", 0.0f);
        out->lane_aim       = api_field_num(L, "aim", 0.0f);
        out->lane_edge      = api_field_num(L, "edge", 0.0f);
        out->lane_reach     = api_field_num(L, "reach", 0.0f);
        out->lap_centre  = api_field_num(L, "lap_centre", 0.0f);
        out->arm_own          = api_field_num(L, "arm_own", 0.0f);
        out->arm_base         = api_field_num(L, "arm_base", 0.0f);
        out->shelf_along      = api_field_num(L, "shelf_along", 0.0f);
        out->shelf_reach      = api_field_num(L, "shelf_reach", 0.0f);
        out->shelf_batter     = api_field_num(L, "shelf_batter", 0.0f);
        out->class_avenue     = api_field_num(L, "class_avenue", 0.0f);
        out->class_boulevard  = api_field_num(L, "class_boulevard", 0.0f);
        out->tile_inset       = api_field_num(L, "tile_inset", 0.0f);
        out->cap_lip         = api_field_num(L, "cap_lip", 0.0f);
        out->cross_share      = api_field_num(L, "cross_share", 0.0f);
        out->lane_pick_dot    = api_field_num(L, "pick_dot", 0.0f);
        out->lane_cross_reach = api_field_num(L, "cross_reach", 0.0f);
        out->lane_cross_ahead = api_field_num(L, "cross_ahead", 0.0f);
        out->lane_cross_aside = api_field_num(L, "cross_aside", 0.0f);
        out->lane_cross_dot   = api_field_num(L, "cross_dot", 0.0f);
        out->lane_cross_spot  = api_field_num(L, "cross_spot", 0.0f);
        out->lane_cross_off   = api_field_num(L, "cross_off", 0.0f);
        out->spur_outer       = api_field_num(L, "spur_outer", 0.0f);
        out->spur_snap        = api_field_num(L, "spur_snap", 0.0f);
        out->spur_meet_cos    = api_field_num(L, "spur_meet_cos", 0.0f);
        out->spur_meet_sin    = api_field_num(L, "spur_meet_sin", 0.0f);
        out->spur_lane_off    = api_field_num(L, "spur_lane_off", 0.0f);
        out->spur_merge_along = api_field_num(L, "spur_merge_along", 0.0f);
        out->spur_taper       = api_field_num(L, "spur_taper", 0.0f);
        out->band_reach       = api_field_num(L, "band_reach", 0.0f);
        out->band_off         = api_field_num(L, "band_off", 0.0f);
        out->band_dot         = api_field_num(L, "band_dot", 0.0f);
        out->band_ahead       = api_field_num(L, "band_ahead", 0.0f);
        out->band_aside       = api_field_num(L, "band_aside", 0.0f);
        out->band_apart       = api_field_num(L, "band_apart", 0.0f);
        out->band_abreast     = api_field_num(L, "band_abreast", 0.0f);
        out->band_outer       = api_field_num(L, "band_outer", 0.0f);
        out->band_taper_far   = api_field_num(L, "band_taper_far", 0.0f);
        out->band_taper_near  = api_field_num(L, "band_taper_near", 0.0f);
        out->band_taper_gap   = api_field_num(L, "band_taper_gap", 0.0f);
        out->band_taper_room  = api_field_num(L, "band_taper_room", 0.0f);
        out->band_line_dot    = api_field_num(L, "band_line_dot", 0.0f);
        lua_getfield(L, -1, "slab");
        out->slab_lanes = (int)lua_rawlen(L, -1);
        if (out->slab_lanes > NET_SLAB_LANES_MAX)
            out->slab_lanes = NET_SLAB_LANES_MAX;
        for (k = 0; k < out->slab_lanes; ++k)
        {
            lua_rawgeti(L, -1, k + 1);
            out->slab_lane[k] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

/*  What is true of every strip of one family.  Asked once for each as
 *  the mesh is built.  Net_family_rules keeps the answer. */
int script_rule_family(const char *fam, float width, ScriptFamily *out)
{
    int ok = 0;
    if (!s_L || !api_rule_begin(s_L, "family"))
        return 0;
    lua_newtable(s_L);
    lua_pushstring(s_L, fam), lua_setfield(s_L, -2, "name");
    lua_pushnumber(s_L, width), lua_setfield(s_L, -2, "width");
    if (!api_rule_call(s_L, "family", 1))
        return 0;
    if (lua_istable(s_L, -1))
    {
        int t = lua_gettop(s_L);
        ok    = 1;
        lua_getfield(s_L, t, "margin");
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
        lua_getfield(s_L, t, "thread");
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
            out->lane_step_spur = api_field_num(s_L, "step_spur", 1.0f);
            out->lane_slot      = api_field_num(s_L, "slot", 0.0f);
            out->lane_wire      = api_field_num(s_L, "wire", 0.0f);
            out->lane_lift      = api_field_num(s_L, "lift", 0.0f);
            out->lane_join      = api_field_num(s_L, "join", 0.0f);
            out->lane_aim       = api_field_num(s_L, "aim", 0.0f);
            out->lane_edge      = api_field_num(s_L, "edge", 0.0f);
            out->lane_reach     = api_field_num(s_L, "reach", 0.0f);
            out->lap_centre  = api_field_num(s_L, "lap_centre", 0.0f);
            out->arm_own          = api_field_num(s_L, "arm_own", 0.0f);
            out->arm_base         = api_field_num(s_L, "arm_base", 0.0f);
            out->shelf_along      = api_field_num(s_L, "shelf_along", 0.0f);
            out->shelf_reach      = api_field_num(s_L, "shelf_reach", 0.0f);
            out->shelf_batter     = api_field_num(s_L, "shelf_batter", 0.0f);
            out->class_avenue     = api_field_num(s_L, "class_avenue", 0.0f);
            out->class_boulevard  = api_field_num(s_L, "class_boulevard", 0.0f);
            out->tile_inset       = api_field_num(s_L, "tile_inset", 0.0f);
            out->cap_lip         = api_field_num(s_L, "cap_lip", 0.0f);
            out->cross_share      = api_field_num(s_L, "cross_share", 0.0f);
            out->lane_pick_dot    = api_field_num(s_L, "pick_dot", 0.0f);
            out->lane_cross_reach = api_field_num(s_L, "cross_reach", 0.0f);
            out->lane_cross_ahead = api_field_num(s_L, "cross_ahead", 0.0f);
            out->lane_cross_aside = api_field_num(s_L, "cross_aside", 0.0f);
            out->lane_cross_dot   = api_field_num(s_L, "cross_dot", 0.0f);
            out->lane_cross_spot  = api_field_num(s_L, "cross_spot", 0.0f);
            out->lane_cross_off   = api_field_num(s_L, "cross_off", 0.0f);
            out->spur_outer       = api_field_num(s_L, "spur_outer", 0.0f);
            out->spur_snap        = api_field_num(s_L, "spur_snap", 0.0f);
            out->spur_meet_cos    = api_field_num(s_L, "spur_meet_cos", 0.0f);
            out->spur_meet_sin    = api_field_num(s_L, "spur_meet_sin", 0.0f);
            out->spur_lane_off    = api_field_num(s_L, "spur_lane_off", 0.0f);
            out->spur_merge_along = api_field_num(s_L, "spur_merge_along", 0.0f);
            out->spur_taper       = api_field_num(s_L, "spur_taper", 0.0f);
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
            out->band_line_dot    = api_field_num(s_L, "band_line_dot", 0.0f);
            lua_getfield(s_L, -1, "slab");
            out->slab_lanes = (int)lua_rawlen(s_L, -1);
            if (out->slab_lanes > NET_SLAB_LANES_MAX)
                out->slab_lanes = NET_SLAB_LANES_MAX;
            for (k = 0; k < out->slab_lanes; ++k)
            {
                lua_rawgeti(s_L, -1, k + 1);
                out->slab_lane[k] = (float)lua_tonumber(s_L, -1);
                lua_pop(s_L, 1);
            }
            lua_pop(s_L, 1);
        }
        lua_pop(s_L, 1);
    }
    lua_pop(s_L, 1);
    return ok;
}

int script_files(int *total)
{
    if (total)
        *total = s_files;
    return s_files;
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

