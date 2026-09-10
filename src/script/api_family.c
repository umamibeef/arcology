/*  api_family.c: arc.family, where a family is declared.
 *
 *      arc.family.define{
 *          name = "line", tiles = "line", answers = true, walk = 0,
 *          width = "line_w", rmin = "line_rmin", rmax = "line_rmax",
 *          material = arc.mat.line, loft = "line",
 *          lips = true, caps = true, classed = true,
 *          stages = { box = "road_box", record = "road_record" },
 *      }
 *
 *  Everything a family is comes through here.  It gives its width and
 *  its radii by the name of the live knob.  It gives its material, the
 *  loft kind its segments are drawn as, what it builds at a junction and
 *  which stages it supplies.  A stage names one of the pipeline's
 *  primitives, or a rule of the script's own.  net/family.c decides
 *  which.  So a band drawn another way is another file here, not a
 *  change in C.
 *
 *  A name nothing answers to is a fault, reported and refused.  A family
 *  half declared would draw a city half wrong and say nothing, which is
 *  worse than one that never appears. */
#include <string.h>

#include "internal.h"
#include "pipeline.h"
#include "script.h"


/*  A field of the table at `t`, as a string copied into the caller's own
 *  room.  The Lua string dies with the table, and a family outlives it. */
static void field_str(lua_State *L, int t, const char *key, char *out, size_t cap, const char *def)
{
    const char *s;
    lua_getfield(L, t, key);
    s = lua_tostring(L, -1);
    snprintf(out, cap, "%s", s ? s : def ? def : "");
    lua_pop(L, 1);
}

static int field_int(lua_State *L, int t, const char *key, int def)
{
    int v;
    lua_getfield(L, t, key);
    v = lua_isnil(L, -1) ? def : (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return v;
}

static int field_bool(lua_State *L, int t, const char *key)
{
    int v;
    lua_getfield(L, t, key);
    v = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return v;
}

static float field_f(lua_State *L, int t, const char *key, float def)
{
    float v;
    lua_getfield(L, t, key);
    v = lua_isnil(L, -1) ? def : (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

/*  The scratch a declaration is read into.  One, reused: what a family
 *  keeps must outlive the Lua table it came from.  The program and the
 *  lint must read a declaration by the same expression or the lint
 *  passes what the program refuses. */
static struct
{
    NetFamilyDecl d;
    char          name[64], tiles[32], loft[32], ends[32], slot[64], graph[32], props[32], margin[32];
    char          width[64], rmin[64], rmax[64], stage[NET_HOOKS][64];
} s_read;

const NetFamilyDecl *api_family_read(lua_State *L, int t)
{
    NetFamilyDecl *d = &s_read.d;
    int            h;
    if (!lua_istable(L, t))
        return NULL;
    memset(&s_read, 0, sizeof s_read);
    field_str(L, t, "name", s_read.name, sizeof s_read.name, NULL);
    field_str(L, t, "tiles", s_read.tiles, sizeof s_read.tiles, NULL);
    field_str(L, t, "loft", s_read.loft, sizeof s_read.loft, "line");
    field_str(L, t, "lane_ends", s_read.ends, sizeof s_read.ends, "open");
    field_str(L, t, "slot", s_read.slot, sizeof s_read.slot, "slot_strip");
    field_str(L, t, "width", s_read.width, sizeof s_read.width, NULL);
    field_str(L, t, "rmin", s_read.rmin, sizeof s_read.rmin, NULL);
    field_str(L, t, "rmax", s_read.rmax, sizeof s_read.rmax, NULL);
    d->name              = s_read.name;
    d->tiles             = s_read.tiles;
    d->loft              = s_read.loft;
    d->lane_ends         = s_read.ends;
    d->slot              = s_read.slot;
    d->width             = s_read.width;
    d->rmin              = s_read.rmin;
    d->rmax              = s_read.rmax;
    d->answers           = field_bool(L, t, "answers");
    d->walk              = field_int(L, t, "walk", -1);
    d->ref_width         = field_f(L, t, "ref_width", 0.0f);
    d->mat               = field_f(L, t, "material", 0.0f);
    d->fit               = field_int(L, t, "fit", 0);
    d->junc_lift         = field_f(L, t, "junc_lift", 0.0f);
    d->shelf_grade       = field_f(L, t, "shelf_grade", 0.0f);
    d->lips             = field_bool(L, t, "lips");
    d->spurs             = field_bool(L, t, "spurs");
    d->ends_at_buildings = field_bool(L, t, "ends_at_buildings");
    d->caps              = field_bool(L, t, "caps");
    d->classed           = field_bool(L, t, "classed");
    /*  Where a strip files itself for the traffic.  A family that says
     *  so needs no record stage: the pipeline files it. */
    field_str(L, t, "graph", s_read.graph, sizeof s_read.graph, "");
    d->graph        = s_read.graph[0] ? s_read.graph : NULL;
    d->record_class = field_int(L, t, "record_class", -1);
    d->stations     = field_bool(L, t, "stations");
    d->meets    = field_bool(L, t, "meets");
    d->paved        = field_bool(L, t, "paved");
    d->crossed      = field_bool(L, t, "crossed");
    d->threads       = field_bool(L, t, "threads");
    field_str(L, t, "props", s_read.props, sizeof s_read.props, "");
    d->props        = s_read.props[0] ? s_read.props : NULL;
    field_str(L, t, "margin", s_read.margin, sizeof s_read.margin, "");
    d->margin      = s_read.margin[0] ? s_read.margin : NULL;
    d->lane_paint        = field_f(L, t, "lane_paint", 0.0f);
    d->free_reach        = field_int(L, t, "free_reach", 0);
    d->turnout           = field_f(L, t, "turnout", 0.0f);
    d->slab              = field_bool(L, t, "slab");
    /*  The stages, each under its own name: `stages = { box = "..." }`. */
    lua_getfield(L, t, "stages");
    for (h = 0; h < NET_HOOKS; ++h)
    {
        if (lua_istable(L, -1))
            field_str(L, lua_gettop(L), NET_HOOK_NAME[h], s_read.stage[h], sizeof s_read.stage[h], NULL);
        d->stage[h] = s_read.stage[h][0] ? s_read.stage[h] : NULL;
    }
    lua_pop(L, 1);
    return d;
}

static int l_family_define(lua_State *L)
{
    const NetFamilyDecl *d;
    luaL_checktype(L, 1, LUA_TTABLE);
    d = api_family_read(L, 1);
    lua_pushboolean(L, d && net_family_define(d) == 0);
    return 1;
}

/*  The NUMBERS a family is drawn by, pushed rather than asked for.  What
 *  is true of every strip and every junction of one family, and used all
 *  through the build.  Keyed by the family's name, since the band and
 *  the line share a tile family and not their numbers.
 *
 *  The reading is script_rule_family's, unchanged.  The same table read
 *  the same way.  So what moved is only who starts it. */
#define FAM_RULES_MAX 8
static struct
{
    char         name[32];
    ScriptFamily f;
} s_frules[FAM_RULES_MAX];
static int s_n_frules;

void script_family_rules_reset(void)
{
    s_n_frules = 0;
}

const ScriptFamily *script_family_rules(const char *name)
{
    static const ScriptFamily none;
    int                       i;
    for (i = 0; name && i < s_n_frules; ++i)
        if (strcmp(s_frules[i].name, name) == 0)
            return &s_frules[i].f;
    return &none;
}

/*  arc.family.rules(name, t) */
static int l_family_rules(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    int         i, at = -1;
    luaL_checktype(L, 2, LUA_TTABLE);
    for (i = 0; i < s_n_frules; ++i)
        if (strcmp(s_frules[i].name, name) == 0)
            at = i;
    if (at < 0)
    {
        if (s_n_frules >= FAM_RULES_MAX)
        {
            lua_pushboolean(L, 0);
            return 1;
        }
        at = s_n_frules++;
        snprintf(s_frules[at].name, sizeof s_frules[at].name, "%s", name);
    }
    memset(&s_frules[at].f, 0, sizeof s_frules[at].f);
    s_frules[at].f.inner = s_frules[at].f.edge = 1.0f;
    s_frules[at].f.parallel                    = 1.0f;
    lua_pushvalue(L, 2);
    script_family_read(L, &s_frules[at].f);
    lua_pop(L, 1);
    lua_pushboolean(L, 1);
    return 1;
}

/*  Every family a reading of the scripts declared, by name, so a script
 *  can ask what is there before it adds to it. */
static int l_family_list(lua_State *L)
{
    int i, n = net_family_count();
    lua_newtable(L);
    for (i = 0; i < n; ++i)
        lua_pushstring(L, net_family_at(i)->name), lua_rawseti(L, -2, i + 1);
    return 1;
}

void api_family_open(lua_State *L)
{
    lua_newtable(L);
    lua_pushcfunction(L, l_family_define), lua_setfield(L, -2, "define");
    lua_pushcfunction(L, l_family_list), lua_setfield(L, -2, "list");
    lua_pushcfunction(L, l_family_rules), lua_setfield(L, -2, "rules");
    lua_setfield(L, -2, "family");
}

void script_family_reset(void)
{
    net_family_reset();
    script_family_rules_reset();
}

