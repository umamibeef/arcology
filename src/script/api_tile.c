/*  api_tile.c -- the ground, one tile at a time, as a script composes it.
 *
 *  What the terrain IS -- how high each corner stands, which corners a
 *  slope's art cuts, where a water surface sits, what lies under it --
 *  is settled before this: the heightfield, the slope codes and the pads
 *  a network cut are the pipeline's own.  What is DRAWN over them is
 *  arc.rules.tile's: the top face, the seabed beneath a water body, the
 *  wall down to each neighbour, and the sediment at the map's cut edges.
 *
 *  A city is sixteen thousand tiles, so a tile answers in plain numbers
 *  and makes no table but `info`.  Corners run 1 to 4 -- north-west,
 *  north-east, south-east, south-west -- and edges 1 to 4, north, east,
 *  south, west.
 */
#include "script.h"

#if SC2K_LUA

#include <string.h>

#include "internal.h"
#include "mesh/internal.h"
#include "net/internal.h"

TileFan *api_tile_of(lua_State *L);

/*  What the tile is: where it stands, what the map says about it, which
 *  slope its art cuts, where it sits in the painter's stack, and whether
 *  a body of water lies on it. */
static int t_info(lua_State *L)
{
    TileFan *t = api_tile_of(L);
    if (!t)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, t->col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, t->row), lua_setfield(L, -2, "row");
    lua_pushinteger(L, t->xter), lua_setfield(L, -2, "xter");
    lua_pushinteger(L, t->xbld), lua_setfield(L, -2, "xbld");
    lua_pushinteger(L, t->code), lua_setfield(L, -2, "code");
    lua_pushnumber(L, t->order), lua_setfield(L, -2, "order");
    lua_pushinteger(L, t->kind), lua_setfield(L, -2, "kind");
    lua_pushboolean(L, t->wet), lua_setfield(L, -2, "wet");
    lua_pushboolean(L, t->underground), lua_setfield(L, -2, "underground");
    lua_pushboolean(L, t->corridor), lua_setfield(L, -2, "corridor");
    lua_pushinteger(L, t->zone), lua_setfield(L, -2, "zone");
    lua_pushnumber(L, t->z[0]), lua_setfield(L, -2, "surface");
    return 1;
}

/*  Corner k: where it stands on the map, how high the drawn ground is
 *  there, how high the seabed under it, and how high the levelled pad. */
static int t_at(lua_State *L)
{
    TileFan *t = api_tile_of(L);
    int      k = (int)luaL_checkinteger(L, 2) - 1;
    if (!t || k < 0 || k > 3)
        return 0;
    lua_pushnumber(L, t->p[k][0]);
    lua_pushnumber(L, t->p[k][1]);
    lua_pushnumber(L, t->p[k][2]);
    lua_pushnumber(L, t->bed[k][2]);
    lua_pushnumber(L, t->pad[k]);
    return 5;
}

/*  A colour the ground is drawn in: the tile's own from the atlas, or
 *  one of the four the ground is walled and bedded in. */
static int t_colour(lua_State *L)
{
    TileFan    *t    = api_tile_of(L);
    const char *want = luaL_optstring(L, 2, "land");
    if (!t)
        return 0;
    if (strcmp(want, "earth") == 0)
        lua_pushnumber(L, 0.0), lua_pushnumber(L, 0.0), lua_pushnumber(L, (lua_Number)MAT_EARTH);
    else if (strcmp(want, "wall") == 0)
        lua_pushnumber(L, 0.0), lua_pushnumber(L, 0.0), lua_pushnumber(L, (lua_Number)MAT_ENG_WALL);
    else if (strcmp(want, "sediment") == 0)
        lua_pushnumber(L, 0.0), lua_pushnumber(L, 0.0), lua_pushnumber(L, (lua_Number)MAT_SEDIMENT);
    else if (strcmp(want, "seabed") == 0)
        lua_pushnumber(L, t->z[0]), lua_pushnumber(L, 0.0), lua_pushnumber(L, (lua_Number)MAT_SEABED);
    else if (strcmp(want, "glass") == 0)
        lua_pushnumber(L, t->z[0]), lua_pushnumber(L, 0.0), lua_pushnumber(L, (lua_Number)MAT_WATER);
    else
        lua_pushnumber(L, t->land[0]), lua_pushnumber(L, t->land[1]), lua_pushnumber(L, t->land[2]);
    return 3;
}

/*  What lies over one edge: whether there is a tile there at all, the
 *  neighbour's ground at this edge's two corners, whether it carries
 *  water under its top, what is built on it, whether a network's
 *  corridor runs over it, which two corners of this tile the edge joins,
 *  whether the edge is the map's own cut, and whether its terrain is
 *  water at all -- which is not the same question: a tile can be dry on
 *  top and still be a river's. */
static int t_edge(lua_State *L)
{
    TileFan *t = api_tile_of(L);
    int      e = (int)luaL_checkinteger(L, 2) - 1;
    if (!t || e < 0 || e > 3)
        return 0;
    lua_pushboolean(L, t->nbr[e].there);
    lua_pushnumber(L, t->nbr[e].za);
    lua_pushnumber(L, t->nbr[e].zb);
    lua_pushboolean(L, t->nbr[e].water);
    lua_pushinteger(L, t->nbr[e].xbld);
    lua_pushboolean(L, t->nbr[e].corridor);
    lua_pushinteger(L, t->nbr[e].ia + 1);
    lua_pushinteger(L, t->nbr[e].ib + 1);
    lua_pushboolean(L, t->nbr[e].rim);
    lua_pushboolean(L, t->nbr[e].sea);
    return 10;
}

/*  The outward normal of one edge, which a face turns to whichever side
 *  it is seen from. */
static int t_normal(lua_State *L)
{
    TileFan *t = api_tile_of(L);
    int      e = (int)luaL_checkinteger(L, 2) - 1;
    if (!t || e < 0 || e > 3)
        return 0;
    lua_pushnumber(L, t->nbr[e].nx);
    lua_pushnumber(L, t->nbr[e].ny);
    return 2;
}

/*  ---- what a tile draws --------------------------------------------- */

/*  The tile's top face, cut by the slope code its art carries: the drawn
 *  ground, or the seabed under a body of water. */
static int t_top(lua_State *L)
{
    TileFan *t    = api_tile_of(L);
    int      bed  = lua_toboolean(L, 2);
    int      code = (int)luaL_checkinteger(L, 3);
    float    order = (float)luaL_checknumber(L, 4);
    float    col3[3];
    int      flat;
    if (!t)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    col3[0] = (float)luaL_checknumber(L, 5);
    col3[1] = (float)luaL_checknumber(L, 6);
    col3[2] = (float)luaL_checknumber(L, 7);
    flat    = lua_toboolean(L, 8);
    lua_pushboolean(L, put_top(t->m, (const float (*)[3])(bed ? t->bed : t->p), code, order, col3, flat) == 0);
    return 1;
}

/*  A wall between two corners: the face from one pair of heights down to
 *  another, turned to (nx, ny). */
static int t_wall(lua_State *L)
{
    TileFan *t = api_tile_of(L);
    int      ia, ib, bed;
    float    a[3], b[3], qa[3], qb[3], nrm[3], col3[3], order;
    if (!t)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    ia  = (int)luaL_checkinteger(L, 2) - 1;
    ib  = (int)luaL_checkinteger(L, 3) - 1;
    bed = lua_toboolean(L, 4);
    if (ia < 0 || ia > 3 || ib < 0 || ib > 3)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    memcpy(a, bed ? t->bed[ia] : t->p[ia], sizeof a);
    memcpy(b, bed ? t->bed[ib] : t->p[ib], sizeof b);
    a[2]     = (float)luaL_checknumber(L, 5);
    b[2]     = (float)luaL_checknumber(L, 6);
    qa[0]    = a[0], qa[1] = a[1], qa[2] = (float)luaL_checknumber(L, 7);
    qb[0]    = b[0], qb[1] = b[1], qb[2] = (float)luaL_checknumber(L, 8);
    nrm[0]   = (float)luaL_checknumber(L, 9);
    nrm[1]   = (float)luaL_checknumber(L, 10);
    nrm[2]   = 0.0f;
    order    = (float)luaL_checknumber(L, 11);
    col3[0]  = (float)luaL_checknumber(L, 12);
    col3[1]  = (float)luaL_checknumber(L, 13);
    col3[2]  = (float)luaL_checknumber(L, 14);
    lua_pushboolean(L, put_wall(t->m, a, b, qa, qb, nrm, order, col3) == 0);
    return 1;
}

/*  The same wall, carrying the two heights the material reads along it:
 *  the layers of sediment at the map's cut edge run from what the tile
 *  draws down to the base. */
static int t_wall_r(lua_State *L)
{
    TileFan *t = api_tile_of(L);
    int      ia, ib;
    float    a[3], b[3], qa[3], qb[3], nrm[3], col3[3], order, ra, rb;
    if (!t)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    ia = (int)luaL_checkinteger(L, 2) - 1;
    ib = (int)luaL_checkinteger(L, 3) - 1;
    if (ia < 0 || ia > 3 || ib < 0 || ib > 3)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    a[0] = t->p[ia][0], a[1] = t->p[ia][1], a[2] = (float)luaL_checknumber(L, 4);
    b[0] = t->p[ib][0], b[1] = t->p[ib][1], b[2] = (float)luaL_checknumber(L, 5);
    qa[0] = a[0], qa[1] = a[1], qa[2] = (float)luaL_checknumber(L, 6);
    qb[0] = b[0], qb[1] = b[1], qb[2] = (float)luaL_checknumber(L, 7);
    nrm[0] = (float)luaL_checknumber(L, 8), nrm[1] = (float)luaL_checknumber(L, 9), nrm[2] = 0.0f;
    order  = (float)luaL_checknumber(L, 10);
    col3[0] = (float)luaL_checknumber(L, 11);
    col3[1] = (float)luaL_checknumber(L, 12);
    col3[2] = (float)luaL_checknumber(L, 13);
    ra = (float)luaL_checknumber(L, 14), rb = (float)luaL_checknumber(L, 15);
    lua_pushboolean(L, put_wall_r(t->m, a, b, qa, qb, nrm, order, col3, ra, rb) == 0);
    return 1;
}

/*  The glass at the map's cut edge: a water face carrying four heights,
 *  the surface and the bed at each end. */
static int t_glass(lua_State *L)
{
    TileFan *t = api_tile_of(L);
    int      ia, ib;
    float    a[3], b[3], qa[3], qb[3], nrm[3], col3[3], order;
    if (!t)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    ia = (int)luaL_checkinteger(L, 2) - 1;
    ib = (int)luaL_checkinteger(L, 3) - 1;
    if (ia < 0 || ia > 3 || ib < 0 || ib > 3)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    memcpy(a, t->p[ia], sizeof a);
    memcpy(b, t->p[ib], sizeof b);
    memcpy(qa, t->bed[ia], sizeof qa);
    memcpy(qb, t->bed[ib], sizeof qb);
    nrm[0] = (float)luaL_checknumber(L, 4), nrm[1] = (float)luaL_checknumber(L, 5), nrm[2] = 0.0f;
    order  = (float)luaL_checknumber(L, 6);
    col3[0] = (float)luaL_checknumber(L, 7);
    col3[1] = (float)luaL_checknumber(L, 8);
    col3[2] = (float)luaL_checknumber(L, 9);
    t->m->to_water = 1;
    lua_pushboolean(L, put_wall_r2(t->m, a, b, qa, qb, nrm, order, col3,
                                   a[2], b[2], qa[2], qb[2]) == 0);
    t->m->to_water = 0;
    return 1;
}

/*  One flat triangle over the tile, which the vertex shader drops
 *  unless the camera is looking down: the map view's zone tint. */
static int t_tri(lua_State *L)
{
    TileFan *t = api_tile_of(L);
    float    tri[3][3], col3[3], ref[3];
    int      i;
    if (!t)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    for (i = 0; i < 3; ++i)
    {
        tri[i][0] = (float)luaL_checknumber(L, i * 3 + 2);
        tri[i][1] = (float)luaL_checknumber(L, i * 3 + 3);
        tri[i][2] = (float)luaL_checknumber(L, i * 3 + 4);
    }
    col3[0] = (float)luaL_checknumber(L, 11);
    col3[1] = (float)luaL_checknumber(L, 12);
    col3[2] = (float)luaL_checknumber(L, 13);
    ref[0] = ref[1] = ref[2] = col3[0];
    lua_pushboolean(L, put_tri_r2(t->m, (const float (*)[3])tri, NULL,
                                  (float)luaL_checknumber(L, 14), col3, ref, ref, 1) == 0);
    return 1;
}

/*  How many walls the ground came to, which the mesh counts for its own
 *  report. */
static int t_walled(lua_State *L)
{
    TileFan *t = api_tile_of(L);
    if (t)
        t->m->n_walls++;
    return 0;
}

static const luaL_Reg TILE[] = {
    {"info",   t_info  },
    {"at",     t_at    },
    {"colour", t_colour},
    {"edge",   t_edge  },
    {"normal", t_normal},
    {"top",    t_top   },
    {"wall",   t_wall  },
    {"wall_r", t_wall_r},
    {"glass",  t_glass },
    {"tri",    t_tri   },
    {"walled", t_walled},
    {NULL,     NULL    }
};

const luaL_Reg *api_tile_methods(int *n)
{
    *n = (int)(sizeof TILE / sizeof TILE[0]) - 1;
    return TILE;
}

#endif
