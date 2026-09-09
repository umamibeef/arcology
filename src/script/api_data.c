/*  api_data.c -- the data a script PUSHES down: arc.bytes and
 *  arc.numbers.
 *
 *  What a byte of the city's save means -- whether it is water, whether
 *  a viaduct may fly over it, which network piece it carries -- is the
 *  script's to say.  It says it once, when it is read:
 *
 *      arc.bytes("water_tiles", t)          -- yes or no, per byte
 *      arc.bytes("slope_codes", t, "number") -- a number, per byte
 *
 *  and the pipeline reads the answer straight out of a table of 256.
 *  Nothing calls up: a build asking a rule what a byte means would be C
 *  driving the answer, which is the contract the other way round.
 *
 *  A reading of the scripts clears the lot, so what stands is exactly
 *  what this reading pushed -- the same rule the families and the
 *  materials keep.
 */
#include <string.h>

#include "internal.h"
#include "script.h"

#define BYTES_MAX 24

static struct
{
    char          name[32];
    unsigned char b[256];
} s_tab[BYTES_MAX];
static int s_n;

const unsigned char *script_bytes(const char *name)
{
    static const unsigned char none[256] = {0};
    int                        i;
    for (i = 0; name && i < s_n; ++i)
        if (strcmp(s_tab[i].name, name) == 0)
            return s_tab[i].b;
    return none;
}

/*  arc.bytes(name, t [, "number"]).  Answers whether it was kept: a
 *  script that pushes more tables than there is room for hears about it
 *  rather than losing one quietly. */
static int l_bytes(lua_State *L)
{
    const char *name   = luaL_checkstring(L, 1);
    int         number = lua_isstring(L, 3) && strcmp(lua_tostring(L, 3), "number") == 0;
    int         i, at = -1;
    luaL_checktype(L, 2, LUA_TTABLE);
    for (i = 0; i < s_n; ++i)
        if (strcmp(s_tab[i].name, name) == 0)
            at = i;
    if (at < 0)
    {
        if (s_n >= BYTES_MAX)
        {
            lua_pushboolean(L, 0);
            return 1;
        }
        at = s_n++;
        snprintf(s_tab[at].name, sizeof s_tab[at].name, "%s", name);
    }
    memset(s_tab[at].b, 0, sizeof s_tab[at].b);
    for (i = 0; i < 256; ++i)
    {
        lua_pushinteger(L, i);
        lua_gettable(L, 2);
        s_tab[at].b[i] = (unsigned char)(number ? lua_tointeger(L, -1) : lua_toboolean(L, -1));
        lua_pop(L, 1);
    }
    lua_pushboolean(L, 1);
    return 1;
}

/*  arc.numbers(name, t): a table of named numbers, kept the same way and
 *  read the same way -- a distance, a share, a count that the pipeline
 *  wants once a build and must not have to ask a rule for. */
#define NUMS_MAX 8
#define NUM_MAX  32
static struct
{
    char  name[32];
    char  key[NUM_MAX][32];
    float v[NUM_MAX];
    int   n;
} s_num[NUMS_MAX];
static int s_n_num;

int script_numbers(const char *name, const char *const *keys, float *out, int n)
{
    int i, k, j;
    for (i = 0; name && i < s_n_num; ++i)
    {
        if (strcmp(s_num[i].name, name) != 0)
            continue;
        for (k = 0; k < n; ++k)
            for (j = 0; j < s_num[i].n; ++j)
                if (strcmp(s_num[i].key[j], keys[k]) == 0)
                    out[k] = s_num[i].v[j];
        return 1;
    }
    return 0;
}

static int l_numbers(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    int         i, at = -1;
    luaL_checktype(L, 2, LUA_TTABLE);
    for (i = 0; i < s_n_num; ++i)
        if (strcmp(s_num[i].name, name) == 0)
            at = i;
    if (at < 0)
    {
        if (s_n_num >= NUMS_MAX)
        {
            lua_pushboolean(L, 0);
            return 1;
        }
        at = s_n_num++;
        snprintf(s_num[at].name, sizeof s_num[at].name, "%s", name);
    }
    s_num[at].n = 0;
    lua_pushnil(L);
    while (lua_next(L, 2))
    {
        const char *k = lua_tostring(L, -2);
        if (k && s_num[at].n < NUM_MAX)
        {
            snprintf(s_num[at].key[s_num[at].n], sizeof s_num[at].key[0], "%s", k);
            s_num[at].v[s_num[at].n] = (float)lua_tonumber(L, -1);
            ++s_num[at].n;
        }
        lua_pop(L, 1);
    }
    lua_pushboolean(L, 1);
    return 1;
}

/*  ---- the tables with a SHAPE ---------------------------------------
 *
 *  Two of what a byte means does not fit in one number: which network a
 *  byte carries and where in the shared layout it sits (and the same
 *  again for the second family a crossing carries), and what part of a
 *  highway a byte is and which way it runs.  Both are pushed whole and
 *  read as arrays, for the same reason the plain ones are: every tile of
 *  the map is looked up in them. */
static struct
{
    unsigned char fam[256], fam2[256];
    signed char   piece[256], piece2[256];
} s_piece;

static struct
{
    unsigned char kind[256], ew[256];
} s_hiway;

void script_pieces(const unsigned char **fam, const signed char **piece,
                   const unsigned char **fam2, const signed char **piece2)
{
    *fam = s_piece.fam, *piece = s_piece.piece;
    *fam2 = s_piece.fam2, *piece2 = s_piece.piece2;
}

void script_highways(const unsigned char **kind, const unsigned char **ew)
{
    *kind = s_hiway.kind, *ew = s_hiway.ew;
}

/*  A family by the name a script calls it, in the Family enum's order. */
static int family_code(const char *name)
{
    return !name                     ? -1
           : strcmp(name, "power") == 0 ? 0
           : strcmp(name, "road") == 0  ? 1
           : strcmp(name, "rail") == 0  ? 2
                                        : -1;
}

/*  One {family = , piece = } off the table on the stack top. */
static void piece_of(lua_State *L, unsigned char *fam, signed char *piece)
{
    int code;
    lua_getfield(L, -1, "family");
    code = family_code(lua_tostring(L, -1));
    lua_pop(L, 1);
    lua_getfield(L, -1, "piece");
    if (code >= 0 && lua_isnumber(L, -1))
    {
        *fam   = (unsigned char)code;
        *piece = (signed char)lua_tointeger(L, -1);
    }
    lua_pop(L, 1);
}

static int l_pieces(lua_State *L)
{
    int i;
    luaL_checktype(L, 1, LUA_TTABLE);
    memset(&s_piece, 0, sizeof s_piece);
    memset(s_piece.piece, -1, sizeof s_piece.piece);
    memset(s_piece.piece2, -1, sizeof s_piece.piece2);
    for (i = 0; i < 256; ++i)
    {
        lua_pushinteger(L, i);
        lua_gettable(L, 1);
        if (lua_istable(L, -1))
        {
            piece_of(L, &s_piece.fam[i], &s_piece.piece[i]);
            lua_getfield(L, -1, "second");
            if (lua_istable(L, -1))
                piece_of(L, &s_piece.fam2[i], &s_piece.piece2[i]);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int l_highways(lua_State *L)
{
    int i;
    luaL_checktype(L, 1, LUA_TTABLE);
    memset(&s_hiway, 0, sizeof s_hiway);
    for (i = 0; i < 256; ++i)
    {
        lua_pushinteger(L, i);
        lua_gettable(L, 1);
        if (lua_istable(L, -1))
        {
            const char *k, *a;
            lua_getfield(L, -1, "kind");
            k             = lua_tostring(L, -1);
            s_hiway.kind[i] = (unsigned char)(!k                          ? 0
                                              : strcmp(k, "ramp") == 0     ? 2
                                              : strcmp(k, "onramp") == 0   ? 3
                                              : strcmp(k, "curve") == 0    ? 4
                                              : strcmp(k, "junction") == 0 ? 5
                                              : strcmp(k, "over") == 0     ? 6
                                                                           : 1);
            lua_pop(L, 1);
            lua_getfield(L, -1, "axis");
            a             = lua_tostring(L, -1);
            s_hiway.ew[i] = (unsigned char)(a && strcmp(a, "ew") == 0);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    lua_pushboolean(L, 1);
    return 1;
}

void api_data_open(lua_State *L)
{
    lua_pushcfunction(L, l_bytes);
    lua_setfield(L, -2, "bytes");
    lua_pushcfunction(L, l_numbers);
    lua_setfield(L, -2, "numbers");
    lua_pushcfunction(L, l_pieces);
    lua_setfield(L, -2, "pieces");
    lua_pushcfunction(L, l_highways);
    lua_setfield(L, -2, "highways");
}

void script_data_reset(void)
{
    memset(s_tab, 0, sizeof s_tab);
    memset(s_num, 0, sizeof s_num);
    memset(&s_piece, 0, sizeof s_piece);
    memset(s_piece.piece, -1, sizeof s_piece.piece);
    memset(s_piece.piece2, -1, sizeof s_piece.piece2);
    memset(&s_hiway, 0, sizeof s_hiway);
    s_n = s_n_num = 0;
}
