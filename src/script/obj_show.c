/*  obj_show.c: WHAT AN EDIT SHOWS going and coming (render/show.h).
 *
 *  The rule arc.rules.edit_show is handed the candidates of one phase
 *  and marks the pieces that move.  Going, the candidates are the pieces
 *  drawn on the edited tiles, before the build runs.  Leaving, they are
 *  the other pieces the build changed.  Coming, they are the pieces the
 *  build made, and the pieces that went and still stand.  How a piece
 *  moves is the script's numbers. */
#include "script.h"

#include "internal.h"
#include "obj.h"
#include "show.h"

/*  The phase, how many candidates it has, and the edited tiles: the
 *  first column and row, and one past the last of each. */
static int api_edit_show_info(lua_State *L)
{
    float area[4];
    if (!rec_of(L, "edit_show"))
        return 0;
    show_area(area);
    lua_newtable(L);
    lua_pushstring(L, show_phase() == SHOW_GOING ? "going" : show_phase() == SHOW_LEAVING ? "leaving" : "coming"), lua_setfield(L, -2, "phase");
    lua_pushinteger(L, show_count()), lua_setfield(L, -2, "n");
    lua_pushnumber(L, area[0]), lua_setfield(L, -2, "c0");
    lua_pushnumber(L, area[1]), lua_setfield(L, -2, "r0");
    lua_pushnumber(L, area[2]), lua_setfield(L, -2, "c1");
    lua_pushnumber(L, area[3]), lua_setfield(L, -2, "r1");
    return 1;
}

/*  One candidate, as plain values.
 *
 *      How far it lies from the edited tiles, in tiles.
 *      Whether its triangles are the last build's, every one.
 *      Whether some of them belong to a piece that went.
 *      Whether it is a piece that went and still stands. */
static int api_edit_show_at(lua_State *L)
{
    float dist;
    int   same, back, kept;
    if (!rec_of(L, "edit_show") ||
        !show_candidate((int)luaL_checkinteger(L, 2), &dist, &same, &back, &kept))
        return 0;
    lua_pushnumber(L, dist);
    lua_pushboolean(L, same);
    lua_pushboolean(L, back);
    lua_pushboolean(L, kept);
    return 4;
}

/*  The candidate moves in this phase: it goes, it leaves, or it comes. */
static int api_edit_show_mark(lua_State *L)
{
    if (!rec_of(L, "edit_show"))
        return 0;
    show_mark((int)luaL_checkinteger(L, 2));
    return 0;
}

const luaL_Reg EDIT_SHOW[] = {
    {"info", api_edit_show_info},
    {"at",   api_edit_show_at  },
    {"mark", api_edit_show_mark},
    {NULL,   NULL              }
};
