/*  api_art.c: arc.art, the ART as a script may READ it.
 *
 *  What a piece of line art joins, and how high a structure stands, are
 *  read off the PICTURES rather than listed anywhere.  Those readings
 *  are interpretations of the material, so they are the script's.
 *
 *  This offers the pictures and nothing more.  How many levels the art
 *  has.  One level's measurements.  Where a sprite sits in it, one
 *  pixel of a sprite, and how much of one row of it is painted.  What
 *  any of that MEANS is decided in a script, which pushes the answer
 *  down as a table the pipeline reads (arc.bytes, arc.floats).
 *
 *  A level is counted from nought and the LAST is the finest.  A sprite
 *  is named by its number within the level, so the level's own first
 *  number stays here.
 *
 *  A run with no art answers nothing at all.  A script that reads it
 *  then pushes empty tables, and the city draws no network.  That is
 *  what a run which cannot find its art should do. */
#include <string.h>

#include "internal.h"
#include "script.h"

#include "atlas/atlas.h"

static const RAtlas *s_art;

void script_art(const RAtlas *a)
{
    s_art = a;
}

/*  One level, or NULL for a number outside the art. */
static const RAtlasLevel *level_at(lua_State *L, int arg)
{
    int i = (int)luaL_checkinteger(L, arg);
    if (!s_art || i < 0 || i >= s_art->n_levels)
        return NULL;
    return &s_art->level[i];
}

/*  One sprite of one level, or NULL where the art set has none. */
static const RTile *tile_at(lua_State *L, const RAtlasLevel *l, int arg)
{
    int id = (int)luaL_checkinteger(L, arg);
    if (!l || id < 0)
        return NULL;
    return atlas_tile(l, l->id_base + id);
}

/*  arc.art.levels(): how many levels the art has, 0 for a run with
 *  none.  The last is the finest. */
static int l_levels(lua_State *L)
{
    lua_pushinteger(L, s_art ? s_art->n_levels : 0);
    return 1;
}

/*  arc.art.level(i): what one level measures.  `tile_w` and `tile_h`
 *  are the ground diamond, `alt_step` the pixels one level of altitude
 *  takes, and `transparent` the palette index that paints nothing. */
static int l_level(lua_State *L)
{
    const RAtlasLevel *l = level_at(L, 1);
    if (!l)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, l->zoom), lua_setfield(L, -2, "zoom");
    lua_pushinteger(L, l->tile_w), lua_setfield(L, -2, "tile_w");
    lua_pushinteger(L, l->tile_h), lua_setfield(L, -2, "tile_h");
    lua_pushinteger(L, l->alt_step), lua_setfield(L, -2, "alt_step");
    lua_pushinteger(L, l->transparent), lua_setfield(L, -2, "transparent");
    return 1;
}

/*  arc.art.tile(level, id): one sprite's own measurements.  `ay` is how
 *  far the art rises above the left corner of its ground diamond, and
 *  `foot` how many tiles across the diamond is. */
static int l_tile(lua_State *L)
{
    const RAtlasLevel *l = level_at(L, 1);
    const RTile       *t = tile_at(L, l, 2);
    if (!t)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, t->w), lua_setfield(L, -2, "w");
    lua_pushinteger(L, t->h), lua_setfield(L, -2, "h");
    lua_pushinteger(L, t->foot), lua_setfield(L, -2, "foot");
    lua_pushinteger(L, t->ax), lua_setfield(L, -2, "ax");
    lua_pushinteger(L, t->ay), lua_setfield(L, -2, "ay");
    return 1;
}

/*  arc.art.pixel(level, id, x, y): the palette index one pixel of a
 *  sprite carries, counting from its own top left corner.  Nothing for
 *  a point outside the sprite. */
static int l_pixel(lua_State *L)
{
    const RAtlasLevel *l = level_at(L, 1);
    const RTile       *t = tile_at(L, l, 2);
    int                x = (int)luaL_checkinteger(L, 3);
    int                y = (int)luaL_checkinteger(L, 4);
    if (!t || x < 0 || y < 0 || x >= (int)t->w || y >= (int)t->h)
        return 0;
    lua_pushinteger(L, l->indices[((size_t)t->y + (size_t)y) * (size_t)l->w + (size_t)t->x + (size_t)x]);
    return 1;
}

/*  arc.art.row(level, id, y): how many pixels of one row of a sprite
 *  are painted, which is every pixel that is not the transparent
 *  index.  This is the reading a rule measures a structure's bulk
 *  with, and one call a row rather than one a pixel. */
static int l_row(lua_State *L)
{
    const RAtlasLevel *l = level_at(L, 1);
    const RTile       *t = tile_at(L, l, 2);
    int                y = (int)luaL_checkinteger(L, 3);
    int                x, n = 0;
    if (!t || y < 0 || y >= (int)t->h)
        return 0;
    for (x = 0; x < (int)t->w; ++x)
        if ((int)l->indices[((size_t)t->y + (size_t)y) * (size_t)l->w + (size_t)t->x + (size_t)x] != l->transparent)
            ++n;
    lua_pushinteger(L, n);
    return 1;
}

void api_art_open(lua_State *L)
{
    lua_newtable(L);
    lua_pushcfunction(L, l_levels), lua_setfield(L, -2, "levels");
    lua_pushcfunction(L, l_level), lua_setfield(L, -2, "level");
    lua_pushcfunction(L, l_tile), lua_setfield(L, -2, "tile");
    lua_pushcfunction(L, l_pixel), lua_setfield(L, -2, "pixel");
    lua_pushcfunction(L, l_row), lua_setfield(L, -2, "row");
    lua_setfield(L, -2, "art");
}
