/*  api_data.c: the data a script PUSHES down: arc.bytes, arc.numbers and
 *  arc.names.
 *
 *  What a byte of the city's save means.  Whether it is water, whether a
 *  viaduct may fly over it, which network piece it carries, what a
 *  report calls it.  Is the script's to say.  It says it once, when it
 *  is read:
 *
 *      arc.bytes("water_tiles", t)   : yes or no, per byte
 *      arc.bytes("slope_codes", t, "number"): a number, per byte
 *      arc.names("tile_kinds", t)    : a word, per byte
 *      arc.floats("structure_rise", t): a number no byte holds
 *
 *  and the pipeline reads the answer straight out of a table of 256.
 *  Nothing calls up: a build asking a rule what a byte means would be C
 *  driving the answer.  This is the contract the other way round.
 *
 *  A reading of the scripts clears the lot.  So what stands is exactly
 *  what this reading pushed: the same rule the families and the
 *  materials keep. */
#include <string.h>

#include "log.h"

#include "internal.h"
#include "net/family.h"
#include "script.h"

/*  WHAT EACH NAMED TABLE MEANS.  A byte table is a contract between one
 *  script and one reader, and neither half was written down anywhere.
 *  A name misspelt in the script pushes a table nothing reads.  A table
 *  pushed as a FLAG where the reader wants a number turns 1, 2 and 3
 *  alike into 1 without a word.  The contract is written here, once.
 *  The program and the lint both hold a push to it.
 *
 *  `number` is 1 where the table answers a number and 0 where it answers
 *  yes or no.  `high` is the largest value the CONCEPT allows, not the
 *  largest any city happens to hold. */
const ScriptByteKind script_byte_kinds[] = {
    {"building_tiles",  0, 1,  "a building with a footprint of its own"        },
    {"cap_ground",      0, 1,  "ground a turning head may be drawn on"         },
    {"carrier_tiles",   0, 1,  "a tile a line runs on into"                    },
    {"corner_lifts",    1, 15, "which of a tile's corners are raised, a bit each"},
    {"corridor_fill",   0, 1,  "a byte a corridor fills through"               },
    {"elevated_tiles",  0, 1,  "a raised piece ordered by its neighbor"        },
    {"end_against",     1, 2,  "what a segment's last tile ends against"       },
    {"fold_ne_sw",      0, 1,  "the diagonal a tile's top folds on"            },
    {"geometry_tiles",  1, 2,  "which bytes owe geometry, and which may sweep out"},
    {"lap_tiles",       0, 1,  "two families lapping one cell"                 },
    {"line_tiles",      1, 3,  "what line a byte carries"                      },
    {"margin_beyond",   1, 2,  "why a margin's end meets nothing"              },
    {"meshed_tiles",    0, 1,  "a tile whose art the mesh draws instead"       },
    {"open_tiles",      0, 1,  "ground a fitted line may sweep across"         },
    {"outline_tint",    1, 11, "the paint a network tile takes in the map view"},
    {"piece_links",     1, 15, "which edges a piece joins, a bit an edge"      },
    {"placed_tiles",    0, 1,  "a tile something was placed on"                },
    {"saddle_terrain",  0, 1,  "terrain the saddle lift applies to"            },
    {"saddle_tiles",    0, 1,  "a tile the saddle lift applies to"             },
    {"slab_air",        1, 2,  "what a raised slab may sweep over"             },
    {"slope_codes",     1, 13, "the slope a terrain byte carries"              },
    {"slope_pieces",    0, 1,  "a piece that answers from the terrain"         },
    {"slope_runs_ew",   0, 1,  "a slope that falls east and west"              },
    {"structure_seen",  0, 1,  "a structure the art can be measured on"        },
    {"structure_tints", 1, 12, "the map view's tint for a placed structure"    },
    {"thing_marks",     1, 40, "one past the record a text byte names"         },
    {"water_tiles",     0, 1,  "terrain that is water"                         },
    {"zone_of",         1, 15, "the zone a zoning byte carries"                },
};
const int script_byte_kinds_n = (int)(sizeof script_byte_kinds / sizeof script_byte_kinds[0]);

const ScriptByteKind *script_byte_kind(const char *name)
{
    int i;
    for (i = 0; name && i < script_byte_kinds_n; ++i)
        if (strcmp(script_byte_kinds[i].name, name) == 0)
            return &script_byte_kinds[i];
    return NULL;
}

#define BYTES_MAX 48

static struct
{
    char          name[32];
    unsigned char b[256];
} s_tab[BYTES_MAX];
static int s_n;

/*  A name nothing pushed.  Said ONCE, since the reader that asked is
 *  nearly always in a per-tile loop.  It matters: a table that was never
 *  pushed reads as nought at every byte, and nought is a plausible
 *  answer.  Nothing is water.  No tile is built.  No piece joins
 *  anything.  A city drawn from one of those looks wrong and says
 *  nothing, so the pipeline says it here instead. */
static void no_table(const char *kind, const char *why, const char *name)
{
    static const char *said[32];
    static int         n_said;
    int                i;
    for (i = 0; i < n_said; ++i)
        if (strcmp(said[i], name) == 0)
            return;
    if (n_said < (int)(sizeof said / sizeof said[0]))
        said[n_said++] = name;
    R_WARN("lua", "%s(\"%s\") %s it reads as empty", kind, name, why);
}

const unsigned char *script_bytes(const char *name)
{
    static const unsigned char none[256] = {0};
    int                        i;
    for (i = 0; name && i < s_n; ++i)
        if (strcmp(s_tab[i].name, name) == 0)
            return s_tab[i].b;
    if (name && !script_byte_kind(name))
        no_table("arc.bytes", "that no reader declares:", name);
    else if (name)
        no_table("arc.bytes", "that no script pushed:", name);
    return none;
}

/*  arc.bytes(name, t [, "number"]).  Answers whether it was kept: a
 *  script that pushes more tables than there is room for hears about it
 *  rather than losing one quietly. */
static int l_bytes(lua_State *L)
{
    const char           *name   = luaL_checkstring(L, 1);
    int                   number = lua_isstring(L, 3) && strcmp(lua_tostring(L, 3), "number") == 0;
    const ScriptByteKind *kind   = script_byte_kind(name);
    int                   i, at = -1;
    luaL_checktype(L, 2, LUA_TTABLE);
    /*  A name nothing reads, or a shape the reader does not expect: both
     *  push a table that is there and does nothing. */
    if (!kind)
        R_WARN("lua", "arc.bytes(%s): no reader asks for that table", name);
    else if (kind->number != number)
        R_WARN("lua", "arc.bytes(%s) is %s, and %s reads it as %s", name,
               number ? "pushed as a number" : "pushed as a flag", kind->what,
               kind->number ? "a number" : "a flag");
    for (i = 0; i < s_n; ++i)
        if (strcmp(s_tab[i].name, name) == 0)
            at = i;
    if (at < 0)
    {
        if (s_n >= BYTES_MAX)
        {
            R_WARN("lua", "arc.bytes(%s): no room for another table; %d is the most", name, BYTES_MAX);
            lua_pushboolean(L, 0);
            return 1;
        }
        at = s_n++;
        snprintf(s_tab[at].name, sizeof s_tab[at].name, "%s", name);
    }
    memset(s_tab[at].b, 0, sizeof s_tab[at].b);
    {
        int high = kind ? kind->high : 255, over = -1;
        for (i = 0; i < 256; ++i)
        {
            int v;
            lua_pushinteger(L, i);
            lua_gettable(L, 2);
            v = number ? (int)lua_tointeger(L, -1) : lua_toboolean(L, -1);
            if ((v < 0 || v > high) && over < 0)
                over = i;
            s_tab[at].b[i] = (unsigned char)v;
            lua_pop(L, 1);
        }
        if (over >= 0)
            R_WARN("lua", "arc.bytes(%s) answers %d for byte %02x, and %s runs to %d",
                   name, (int)s_tab[at].b[over], (unsigned)over, kind ? kind->what : "the table", high);
    }
    lua_pushboolean(L, 1);
    return 1;
}

/*  arc.numbers(name, t): a table of named numbers, kept the same way and
 *  read the same way.  A distance, a share, a count that the pipeline
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

/*  One pushed table by name, or -1 where no script pushed it. */
static int num_table(const char *name)
{
    int i;
    for (i = 0; name && i < s_n_num; ++i)
        if (strcmp(s_num[i].name, name) == 0)
            return i;
    return -1;
}

/*  One key of one table, or -1. */
static int num_key(int at, const char *key)
{
    int j;
    for (j = 0; j < s_num[at].n; ++j)
        if (strcmp(s_num[at].key[j], key) == 0)
            return j;
    return -1;
}

int script_numbers(const char *name, const char *const *keys, float *out, int n)
{
    int at = num_table(name), k, j;
    if (at < 0)
        return 0;
    for (k = 0; k < n; ++k)
        if ((j = num_key(at, keys[k])) >= 0)
            out[k] = s_num[at].v[j];
    return 1;
}

/*  The same numbers written into FIELDS.  A name and the float it fills
 *  are written together.  So a field added in the middle of a struct
 *  cannot hand its value to its neighbor.  A key the caller does not
 *  take is said once, since a name nothing answers to is a typo. */
int script_numbers_at(const char *name, const ScriptNumber *fill, int n)
{
    int at = num_table(name), k, j;
    if (at < 0)
        return 0;
    for (k = 0; k < n; ++k)
        if ((j = num_key(at, fill[k].name)) >= 0)
            *fill[k].at = s_num[at].v[j];
    for (j = 0; j < s_num[at].n; ++j)
    {
        for (k = 0; k < n; ++k)
            if (strcmp(s_num[at].key[j], fill[k].name) == 0)
                break;
        if (k >= n)
            R_WARN("lua", "arc.numbers(%s) gives a %s, which nothing reads", name, s_num[at].key[j]);
    }
    return 1;
}

static int l_numbers(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    int         i, at = -1, held = 1;
    luaL_checktype(L, 2, LUA_TTABLE);
    for (i = 0; i < s_n_num; ++i)
        if (strcmp(s_num[i].name, name) == 0)
            at = i;
    if (at < 0)
    {
        if (s_n_num >= NUMS_MAX)
        {
            R_WARN("lua", "arc.numbers(%s): no room for another table; %d is the most", name, NUMS_MAX);
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
        /*  The key is read at -2 WITHOUT being turned into a string
         *  there: lua_tostring on a number key in place would break the
         *  walk.  A copy is made and read instead. */
        const char *k;
        lua_pushvalue(L, -2);
        k = lua_tostring(L, -1);
        if (!k)
            held = 0;
        else if (s_num[at].n >= NUM_MAX)
            held = 0;
        else
        {
            snprintf(s_num[at].key[s_num[at].n], sizeof s_num[at].key[0], "%s", k);
            s_num[at].v[s_num[at].n] = (float)lua_tonumber(L, -2);
            ++s_num[at].n;
        }
        lua_pop(L, 2);
    }
    if (!held)
        R_WARN("lua", "arc.numbers(%s): more names than the %d there is room for; some are lost", name, NUM_MAX);
    lua_pushboolean(L, held);
    return 1;
}

/*  arc.floats(name, t): a NUMBER per byte of the city that will not fit
 *  in a byte.  A height in levels of altitude is one.  Read the same
 *  way the byte tables are, out of a table of 256. */
#define FLOATS_MAX 4
static struct
{
    char  name[32];
    float v[256];
} s_flt[FLOATS_MAX];
static int s_n_flt;

const float *script_floats(const char *name)
{
    static const float none[256] = {0.0f};
    int                i;
    for (i = 0; name && i < s_n_flt; ++i)
        if (strcmp(s_flt[i].name, name) == 0)
            return s_flt[i].v;
    if (name)
        no_table("arc.floats", "that no script pushed:", name);
    return none;
}

/*  Answers whether it was kept.  A script that pushes more tables than
 *  there is room for hears about it rather than lose one quietly. */
static int l_floats(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    int         i, at = -1;
    luaL_checktype(L, 2, LUA_TTABLE);
    for (i = 0; i < s_n_flt; ++i)
        if (strcmp(s_flt[i].name, name) == 0)
            at = i;
    if (at < 0)
    {
        if (s_n_flt >= FLOATS_MAX)
        {
            R_WARN("lua", "arc.floats(%s): no room for another table; %d is the most", name, FLOATS_MAX);
            lua_pushboolean(L, 0);
            return 1;
        }
        at = s_n_flt++;
        snprintf(s_flt[at].name, sizeof s_flt[at].name, "%s", name);
    }
    memset(s_flt[at].v, 0, sizeof s_flt[at].v);
    for (i = 0; i < 256; ++i)
    {
        lua_pushinteger(L, i);
        lua_gettable(L, 2);
        s_flt[at].v[i] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    lua_pushboolean(L, 1);
    return 1;
}

/*  arc.names(name, t): a WORD per byte of the city, pushed the way the
 *  byte tables are.  A report says what a tile holds in words, and the
 *  words are the script's.  A byte means what the script says it
 *  means.  The report must not keep a second opinion.
 *
 *  The words sit in one pool per table.  A byte holds ONE PAST the
 *  place its word starts, since nought has to mean no word. */
#define NAMES_MAX 8
#define NAME_POOL 8192
static struct
{
    char     name[32];
    char     pool[NAME_POOL];
    uint16_t at[256];
    int      used;
} s_word[NAMES_MAX];
static int s_n_word;

const char *script_name(const char *name, unsigned char b)
{
    int i;
    for (i = 0; name && i < s_n_word; ++i)
        if (strcmp(s_word[i].name, name) == 0)
            return s_word[i].at[b] ? s_word[i].pool + s_word[i].at[b] - 1 : NULL;
    return NULL;
}

/*  A word per building byte, for a report to print.  Answers whether
 *  every word was kept.  A table that runs out of room, or one table
 *  too many, is heard about rather than lose a word quietly. */
static int l_names(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    int         i, at = -1, held = 1;
    luaL_checktype(L, 2, LUA_TTABLE);
    for (i = 0; i < s_n_word; ++i)
        if (strcmp(s_word[i].name, name) == 0)
            at = i;
    if (at < 0)
    {
        if (s_n_word >= NAMES_MAX)
        {
            R_WARN("lua", "arc.names(%s): no room for another table; %d is the most", name, NAMES_MAX);
            lua_pushboolean(L, 0);
            return 1;
        }
        at = s_n_word++;
        snprintf(s_word[at].name, sizeof s_word[at].name, "%s", name);
    }
    memset(s_word[at].at, 0, sizeof s_word[at].at);
    s_word[at].used = 0;
    for (i = 0; i < 256; ++i)
    {
        const char *v;
        size_t      k;
        lua_pushinteger(L, i);
        lua_gettable(L, 2);
        v = lua_tostring(L, -1);
        k = v ? strlen(v) : 0;
        if (v && s_word[at].used + (int)k + 1 <= NAME_POOL)
        {
            memcpy(s_word[at].pool + s_word[at].used, v, k + 1);
            s_word[at].at[i] = (uint16_t)(s_word[at].used + 1);
            s_word[at].used += (int)k + 1;
        }
        else if (v)
        {
            if (held)
                R_WARN("lua", "arc.names(%s): the words do not fit in %d bytes; byte %d on is unnamed", name, NAME_POOL, i);
            held = 0;
        }
        lua_pop(L, 1);
    }
    lua_pushboolean(L, held);
    return 1;
}

/*  ---- the tables with a SHAPE ---------------------------------------
 *
 *  Two of what a byte means does not fit in one number.  Which network a
 *  byte carries and where in the shared layout it sits (and the same
 *  again for the second family a meet carries).  What part of a band a
 *  byte is and which way it runs.  Both are pushed whole and read as
 *  arrays, for the same reason the plain ones are.  Every tile of the
 *  map is looked up in them. */
static struct
{
    unsigned char fam[256], fam2[256];
    signed char   piece[256], piece2[256];
} s_piece;

static struct
{
    unsigned char kind[256], ew[256];
} s_band;

void script_pieces(const unsigned char **fam, const signed char **piece,
                   const unsigned char **fam2, const signed char **piece2)
{
    *fam = s_piece.fam, *piece = s_piece.piece;
    *fam2 = s_piece.fam2, *piece2 = s_piece.piece2;
}

void script_bandtiles(const unsigned char **kind, const unsigned char **ew)
{
    *kind = s_band.kind, *ew = s_band.ew;
}

/*  One {family = , piece = } off the table on the stack top.  Which
 *  slot a family name sits at is net_tile_family's, the same reading a
 *  declaration gets.  A second list here would file every byte of a
 *  whole family at the wrong slot and say nothing. */
static void piece_of(lua_State *L, unsigned char *fam, signed char *piece)
{
    int code;
    lua_getfield(L, -1, "family");
    code = net_tile_family(lua_tostring(L, -1));
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

/*  The band tiles, by the byte the city stores.  The script pushes a
 *  table keyed by that byte.  Anything it does not name is no band. */
static int l_bands(lua_State *L)
{
    int i, held = 1;
    luaL_checktype(L, 1, LUA_TTABLE);
    memset(&s_band, 0, sizeof s_band);
    for (i = 0; i < 256; ++i)
    {
        lua_pushinteger(L, i);
        lua_gettable(L, 1);
        if (lua_istable(L, -1))
        {
            /*  A kind is NAMED, and a name nothing answers to is a
             *  fault.  A misspelt kind read as a slab would draw the
             *  wrong thing on every tile that carried it. */
            static const char *const KIND[] = {"slab", "incline", "spur", "curve", "junction", "over"};
            const char              *k, *a;
            int                      n;
            lua_getfield(L, -1, "kind");
            k = lua_tostring(L, -1);
            for (n = 0; k && n < (int)(sizeof KIND / sizeof KIND[0]); ++n)
                if (strcmp(k, KIND[n]) == 0)
                    break;
            if (!k)
                s_band.kind[i] = 0;
            else if (n >= (int)(sizeof KIND / sizeof KIND[0]))
            {
                R_WARN("lua", "arc.bands: byte %02x is a \"%s\", which is no kind of band", (unsigned)i, k);
                s_band.kind[i] = 0;
                held           = 0;
            }
            else
                s_band.kind[i] = (unsigned char)(n + 1);
            lua_pop(L, 1);
            lua_getfield(L, -1, "axis");
            a             = lua_tostring(L, -1);
            s_band.ew[i] = (unsigned char)(a && strcmp(a, "ew") == 0);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    lua_pushboolean(L, held);
    return 1;
}

void api_data_open(lua_State *L)
{
    lua_pushcfunction(L, l_bytes);
    lua_setfield(L, -2, "bytes");
    lua_pushcfunction(L, l_numbers);
    lua_setfield(L, -2, "numbers");
    lua_pushcfunction(L, l_names);
    lua_setfield(L, -2, "names");
    lua_pushcfunction(L, l_floats);
    lua_setfield(L, -2, "floats");
    lua_pushcfunction(L, l_pieces);
    lua_setfield(L, -2, "pieces");
    lua_pushcfunction(L, l_bands);
    lua_setfield(L, -2, "bands");
}

void script_data_reset(void)
{
    memset(s_tab, 0, sizeof s_tab);
    memset(s_num, 0, sizeof s_num);
    memset(s_word, 0, sizeof s_word);
    memset(s_flt, 0, sizeof s_flt);
    memset(&s_piece, 0, sizeof s_piece);
    memset(s_piece.piece, -1, sizeof s_piece.piece);
    memset(s_piece.piece2, -1, sizeof s_piece.piece2);
    memset(&s_band, 0, sizeof s_band);
    s_n = s_n_num = s_n_word = s_n_flt = 0;
}
