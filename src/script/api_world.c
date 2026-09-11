/*  api_world.c: `arc.city` and `arc.mesh`: what the script may look at.
 *
 *      arc.city.size the map, in tiles arc.city.layers every layer's
 *      name and its edge arc.city.at(layer, col, row) one layer at one
 *      cell arc.city.near(layer, col, row) its four neighbors, N E S W
 *      arc.city.plane(layer) the whole layer, to walk in Lua
 *      arc.city.tile(col, row) every layer at one cell, as a table
 *      arc.city.line_class(col, row) 0 a line, 1 an avenue, 2 a
 *      boulevard arc.mesh.meets() the meet tally of the last build
 *      arc.mesh.walkways() the margin network's counts arc.mesh.faults()
 *      what the checks refuse to pass arc.mesh.probe(x, y) every surface
 *      over one point, to the dump
 *
 *  The city half is the SIMULATION, offered to be read.  A script asks
 *  at a full-resolution cell, whatever edge the layer is stored at.  So
 *  one walk over the map reads every layer the same way.  `plane` hands
 *  a layer over whole, for a walk that cannot afford a call a cell.
 *
 *  The mesh half reads the build that has already happened, so a script
 *  may ask what its own rules produced. */
#include <stddef.h>
#include <string.h>

#include "script.h"


#include "city.h"
#include "internal.h"
#include "mesh/mesh.h"
#include "pipeline.h"

#include "net/net.h"
#include "build.h"
/*  The city the last build was given.  A script reads the map through
 *  this and not through the network walk.  This has its own copy and is
 *  only up while a walk is running. */
static const RCity *s_city;

void script_city_is(const void *city)
{
    s_city = (const RCity *)city;
}

/*  ---- the layers ----------------------------------------------------
 *
 *  Each layer by the name a script reads it under, the edge it is stored
 *  at, and where it sits in the city.  Three edges: the full map, the
 *  half-resolution planes the simulation blurs over, and the quarter-
 *  resolution ones.  ALTM is the one word layer.  The rest are bytes.
 *
 *  Three more are DERIVED rather than stored, because the altitude word
 *  holds two heights and which of them a tile stands at is XTER's
 *  business.  A script that wants the raw word still has `altm`. */
enum
{
    L_BYTE,
    L_WORD,
    L_GROUND,
    L_TABLE,
    L_SURFACE
};

typedef struct
{
    const char *name;
    int         edge;
    int         kind;
    size_t      off;
} Layer;

static const Layer LAYERS[] = {
    {"altm", R_MAP, L_WORD, offsetof(RCity, altm)},
    {"xbld", R_MAP, L_BYTE, offsetof(RCity, xbld)},
    {"xzon", R_MAP, L_BYTE, offsetof(RCity, xzon)},
    {"xter", R_MAP, L_BYTE, offsetof(RCity, xter)},
    {"xund", R_MAP, L_BYTE, offsetof(RCity, xund)},
    {"xtxt", R_MAP, L_BYTE, offsetof(RCity, xtxt)},
    {"xbit", R_MAP, L_BYTE, offsetof(RCity, xbit)},
    {"xtrf", R_HALF, L_BYTE, offsetof(RCity, xtrf)},
    {"xplt", R_HALF, L_BYTE, offsetof(RCity, xplt)},
    {"xval", R_HALF, L_BYTE, offsetof(RCity, xval)},
    {"xcrm", R_HALF, L_BYTE, offsetof(RCity, xcrm)},
    {"xplc", R_QTR, L_BYTE, offsetof(RCity, xplc)},
    {"xfir", R_QTR, L_BYTE, offsetof(RCity, xfir)},
    {"xpop", R_QTR, L_BYTE, offsetof(RCity, xpop)},
    {"xrog", R_QTR, L_BYTE, offsetof(RCity, xrog)},
    {"ground", R_MAP, L_GROUND, 0},
    {"water_table", R_MAP, L_TABLE, 0},
    {"surface", R_MAP, L_SURFACE, 0},
};
#define N_LAYERS ((int)(sizeof LAYERS / sizeof LAYERS[0]))

static const Layer *layer_of(const char *name)
{
    int i;
    for (i = 0; name && i < N_LAYERS; ++i)
        if (strcmp(LAYERS[i].name, name) == 0)
            return &LAYERS[i];
    return NULL;
}

/*  One cell of one layer, at that layer's OWN index: so the caller has
 *  already scaled a full-resolution cell down to the layer's edge. */
static int layer_read(const Layer *ly, int i)
{
    const unsigned char *base = (const unsigned char *)s_city + ly->off;
    switch (ly->kind)
    {
    case L_WORD:
        return (int)((const uint16_t *)base)[i];
    case L_GROUND:
        return rcity_alt_ground(s_city->altm[i]);
    case L_TABLE:
        return rcity_alt_table(s_city->altm[i]);
    case L_SURFACE:
        return rcity_alt_surface(s_city->altm[i], s_city->xter[i]);
    default:
        return (int)base[i];
    }
}

/*  A full-resolution cell, at the layer's own edge.  Answers -1 where the
 *  cell is off the map, which is what tells a walk it has reached one. */
static int layer_index(const Layer *ly, lua_Integer col, lua_Integer row)
{
    int shift = R_MAP == ly->edge ? 0 : R_HALF == ly->edge ? 1 : 2;
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return -1;
    return (int)((row >> shift) * ly->edge + (col >> shift));
}

/*  arc.city.at(layer, col, row): one layer at one cell, or nothing off
 *  the map or under a name no layer answers to. */
static int l_at(lua_State *L)
{
    const Layer *ly = layer_of(luaL_checkstring(L, 1));
    int          i;
    if (!s_city || !ly)
        return 0;
    i = layer_index(ly, luaL_checkinteger(L, 2), luaL_checkinteger(L, 3));
    if (i < 0)
        return 0;
    lua_pushinteger(L, layer_read(ly, i));
    return 1;
}

/*  arc.city.near(layer, col, row) answers the four neighbors.  They come
 *  in the pipeline's own edge order: 0 north, 1 east, 2 south, 3 west,
 *  each nothing where that neighbor is off the map. */
static int l_near(lua_State *L)
{
    static const int DC[4] = {0, 1, 0, -1}, DR[4] = {-1, 0, 1, 0};
    const Layer     *ly  = layer_of(luaL_checkstring(L, 1));
    lua_Integer      col = luaL_checkinteger(L, 2), row = luaL_checkinteger(L, 3);
    int              e;
    if (!s_city || !ly)
        return 0;
    for (e = 0; e < 4; ++e)
    {
        int i = layer_index(ly, col + DC[e], row + DR[e]);
        if (i < 0)
            lua_pushnil(L);
        else
            lua_pushinteger(L, layer_read(ly, i));
    }
    return 4;
}

/*  arc.city.plane(layer): the whole layer as one array counted from
 *  nought, and its edge.  This is what a walk over the map reads: a call
 *  a cell would cost more than the walk itself. */
static int l_plane(lua_State *L)
{
    const Layer *ly = layer_of(luaL_checkstring(L, 1));
    int          i, n;
    if (!s_city || !ly)
        return 0;
    n = ly->edge * ly->edge;
    lua_createtable(L, n, 0);
    for (i = 0; i < n; ++i)
    {
        lua_pushinteger(L, layer_read(ly, i));
        lua_rawseti(L, -2, i);
    }
    lua_pushinteger(L, ly->edge);
    return 2;
}

static int l_tile(lua_State *L)
{
    lua_Integer col = luaL_checkinteger(L, 1);
    lua_Integer row = luaL_checkinteger(L, 2);
    int         i;
    if (!s_city || col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return 0;
    lua_createtable(L, 0, N_LAYERS);
    for (i = 0; i < N_LAYERS; ++i)
    {
        lua_pushinteger(L, layer_read(&LAYERS[i], layer_index(&LAYERS[i], col, row)));
        lua_setfield(L, -2, LAYERS[i].name);
    }
    return 1;
}

static int l_road_class(lua_State *L)
{
    int32_t col = (int32_t)luaL_checkinteger(L, 1);
    int32_t row = (int32_t)luaL_checkinteger(L, 2);
    if (!s_city || col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return 0;
    lua_pushinteger(L, (lua_Integer)line_class(s_city, col, row));
    return 1;
}

static int l_meets(lua_State *L)
{
    int t[6];
    walk_cross_counts(t);
    lua_newtable(L);
    lua_pushinteger(L, t[0]), lua_setfield(L, -2, "mouths");
    lua_pushinteger(L, t[1]), lua_setfield(L, -2, "uncontrolled");
    lua_pushinteger(L, t[2]), lua_setfield(L, -2, "no_margin");
    lua_pushinteger(L, t[3]), lua_setfield(L, -2, "not_parallel");
    lua_pushinteger(L, t[4]), lua_setfield(L, -2, "no_line");
    lua_pushinteger(L, t[5]), lua_setfield(L, -2, "narrowed");
    return 1;
}

static int l_walkways(lua_State *L)
{
    int i, n = walk_net_count(), k[4] = {0, 0, 0, 0};
    for (i = 0; i < n; ++i)
        ++k[walk_net_get(i)->kind & 3];
    lua_newtable(L);
    lua_pushinteger(L, n), lua_setfield(L, -2, "paths");
    lua_pushinteger(L, k[WALK_SIDE]), lua_setfield(L, -2, "sides");
    lua_pushinteger(L, k[WALK_CORNER]), lua_setfield(L, -2, "corners");
    lua_pushinteger(L, k[WALK_CROSS]), lua_setfield(L, -2, "meets");
    lua_pushinteger(L, k[WALK_CAP]), lua_setfield(L, -2, "caps");
    return 1;
}

static int l_faults(lua_State *L)
{
    lua_newtable(L);
    lua_pushinteger(L, junction_outline_faults()), lua_setfield(L, -2, "outlines");
    lua_pushinteger(L, walk_net_faults()), lua_setfield(L, -2, "meets");
    return 1;
}

static int l_probe(lua_State *L)
{
    const RMesh *m = build_mesh();
    if (!m)
        return 0;
    mesh_probe(m, (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2));
    return 0;
}

void api_world_open(lua_State *L)
{
    lua_newtable(L);
    lua_pushinteger(L, R_MAP), lua_setfield(L, -2, "size");
    lua_pushcfunction(L, l_at), lua_setfield(L, -2, "at");
    lua_pushcfunction(L, l_near), lua_setfield(L, -2, "near");
    lua_pushcfunction(L, l_plane), lua_setfield(L, -2, "plane");
    lua_pushcfunction(L, l_tile), lua_setfield(L, -2, "tile");
    lua_pushcfunction(L, l_road_class), lua_setfield(L, -2, "line_class");
    /*  The layers themselves, so a script can be told what there is to
     *  read rather than carrying a list of its own that goes stale.  A
     *  sequence the script reads with ipairs, so it is Lua's own and
     *  counts from one.  The edges inside it are the map's. */
    {
        int i;
        lua_createtable(L, N_LAYERS, 0);
        for (i = 0; i < N_LAYERS; ++i)
        {
            lua_createtable(L, 0, 2);
            lua_pushstring(L, LAYERS[i].name), lua_setfield(L, -2, "name");
            lua_pushinteger(L, LAYERS[i].edge), lua_setfield(L, -2, "edge");
            lua_rawseti(L, -2, i + 1);
        }
        lua_setfield(L, -2, "layers");
    }
    lua_setfield(L, -2, "city");

    lua_newtable(L);
    lua_pushcfunction(L, l_meets), lua_setfield(L, -2, "meets");
    lua_pushcfunction(L, l_walkways), lua_setfield(L, -2, "walkways");
    lua_pushcfunction(L, l_faults), lua_setfield(L, -2, "faults");
    lua_pushcfunction(L, l_probe), lua_setfield(L, -2, "probe");
    lua_setfield(L, -2, "mesh");
}

