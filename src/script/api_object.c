/*  api_object.c -- an OBJECT of the world, as a script sees one.
 *
 *  Everything the pipeline builds is a thing: a strip of road, a
 *  junction, a footway, a level crossing, a prop.  A script is handed
 *  the thing itself, not a window on to "the one being drawn":
 *
 *      arc.rules.strip = function (s)
 *          for i = 1, s:count() - 1 do
 *              local x, y, z, dx, dy = s:at(i)
 *              s:quad(...)
 *          end
 *          return true
 *      end
 *
 *  `s` is a handle on the pipeline's own record.  It can be passed to a
 *  function, kept for the length of the call, and asked what it is --
 *  `s.kind` is "strip" -- and every face it draws is attributed to it,
 *  so the inspector can say which thing put a triangle on a tile.
 *
 *  The methods an object has are its kind's.  They are all of one shape:
 *  the ones that MEASURE answer plain numbers and make no table, since a
 *  strip has thousands of stations and a city thousands of strips; the
 *  ones that DRAW answer whether the mesh took the face.
 *
 *      every kind      kind, tile, ground(x, y), order(x, y)
 *      strip           count, at, width, extras, class,
 *                      quad, tri, prism, fan, box, wire, model
 *
 *  A handle is dead the moment the rule that was given it returns: the
 *  record it points at is the pipeline's, and the pipeline moves on.  A
 *  method on a dead handle answers nothing rather than reading freed
 *  memory.
 */
#include "script.h"


#include <string.h>

#include "internal.h"
#include "mesh/internal.h"
#include "net/internal.h"
#include "geo/model.h"

#define OBJ_META "arc.object"

/*  The handle itself: what kind of thing, the pipeline's record, and the
 *  generation it was made in.  Every handle of an older generation is
 *  dead, which is how a script that stashed one cannot reach a record
 *  the pipeline has moved past. */
typedef struct
{
    const char *kind;
    void       *rec;
    unsigned    gen;
} ScriptObj;

static unsigned s_gen = 1;
static int      s_depth;  /* rules inside rules: only the outermost ends a generation */

static ScriptObj *obj_check(lua_State *L)
{
    ScriptObj *o = (ScriptObj *)luaL_checkudata(L, 1, OBJ_META);
    return o && o->gen == s_gen && o->rec ? o : NULL;
}

/*  ---- a record's fields, as a script reads them --------------------- */

/*  The handle's record, if it is of the kind asked for.  A handle from a
 *  past call answers nothing: obj_check has already refused it. */
static void *rec_of(lua_State *L, const char *kind)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, kind) == 0 ? o->rec : NULL;
}

/*  A field of a record, by name and by where it sits.  `info` is nearly
 *  always nothing but a list of these, so the list IS the code: giving a
 *  script one more field to read is one row here, not a push and a
 *  setfield and two places to forget them. */
typedef enum
{
    FLD_NUM, /* a float, read as a number     */
    FLD_INT, /* an int, read as an integer    */
    FLD_BOOL /* an int, read as true or false */
} FieldType;

typedef struct
{
    const char *name;
    FieldType   type;
    size_t      off;
} Field;

/*  The fields as a fresh table, left on the stack. */
static int api_fields(lua_State *L, const void *rec, const Field *f)
{
    if (!rec)
        return 0;
    lua_newtable(L);
    for (; f->name; ++f)
    {
        const char *at = (const char *)rec + f->off;
        switch (f->type)
        {
        case FLD_NUM: lua_pushnumber(L, (lua_Number)*(const float *)at); break;
        case FLD_INT: lua_pushinteger(L, *(const int *)at); break;
        case FLD_BOOL: lua_pushboolean(L, *(const int *)at != 0); break;
        }
        lua_setfield(L, -2, f->name);
    }
    return 1;
}

static Loft *strip_of(lua_State *L)
{
    return (Loft *)rec_of(L, "strip");
}

/*  ---- what every object answers ------------------------------------ */

/*  The drawn surface under a point: what a band lying on the ground
 *  takes for its height. */
static int api_strip_ground(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    lua_pushnumber(L, surface_at_world(x->c, x->mask_bit, (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3)));
    return 1;
}

/*  Where a point sits in the painter's stack: its tile's own slot.  A
 *  ribbon takes the slot of the tile each of its quads lies on. */
static int api_strip_order(lua_State *L)
{
    Loft   *x = strip_of(L);
    int32_t tc, tr;
    if (!x)
        return 0;
    tc = (int32_t)floorf((float)luaL_checknumber(L, 2));
    tr = (int32_t)floorf((float)luaL_checknumber(L, 3));
    if (tc < 0)
        tc = 0;
    if (tr < 0)
        tr = 0;
    if (tc >= R_MAP)
        tc = R_MAP - 1;
    if (tr >= R_MAP)
        tr = R_MAP - 1;
    lua_pushnumber(L, tile_order(x->c, tc, tr, x->mask_bit));
    return 1;
}

/*  ---- a strip ------------------------------------------------------ */

/*  What the strip IS, as against what it is made of: the family it
 *  belongs to, the class it carries, its half width, the material it is
 *  laid in, how many stations it was cut into, whether it flies clear of
 *  the ground and carries its own height, whether it has footways beside
 *  it, and the crossing band each end's junction has laid over it. */
static int api_strip_info(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    lua_newtable(L);
    lua_pushstring(L, x->d->fam->name), lua_setfield(L, -2, "family");
    lua_pushnumber(L, x->d->cls), lua_setfield(L, -2, "class");
    lua_pushnumber(L, x->hw), lua_setfield(L, -2, "half");
    lua_pushnumber(L, x->mat), lua_setfield(L, -2, "mat");
    lua_pushnumber(L, x->total), lua_setfield(L, -2, "len");
    lua_pushinteger(L, x->ns), lua_setfield(L, -2, "n");
    lua_pushboolean(L, x->d->fam->flies != NULL), lua_setfield(L, -2, "flies");
    /*  A family with kerbs has footways beside it -- unless the pass
     *  that lays them is off, and then the carriageway takes the whole
     *  band and there is nothing to leave room for. */
    lua_pushboolean(L, x->d->fam->curbs && sidewalk_on()), lua_setfield(L, -2, "curbs");
    lua_pushboolean(L, x->d->fam->deck), lua_setfield(L, -2, "deck");
    lua_pushnumber(L, x->d->xw0), lua_setfield(L, -2, "cross0");
    lua_pushnumber(L, x->d->xw1), lua_setfield(L, -2, "cross1");
    lua_pushstring(L, x->d->fam->slot ? x->d->fam->slot : "slot_strip"), lua_setfield(L, -2, "slot");
    /*  A deck's own facts: whether it lies on the ground, whether it is
     *  one lane wide, whether it is a ramp's concrete, and where its
     *  girder and its parapet stand. */
    lua_pushboolean(L, x->d->flat), lua_setfield(L, -2, "flat");
    lua_pushboolean(L, x->d->lane_piece), lua_setfield(L, -2, "lane_piece");
    lua_pushboolean(L, x->d->struct_), lua_setfield(L, -2, "structure");
    lua_pushnumber(L, HIWAY_GIRDER), lua_setfield(L, -2, "girder");
    lua_pushnumber(L, HIWAY_PARAPET), lua_setfield(L, -2, "parapet");
    return 1;
}

/*  Which class of road stands on a tile, for a strip whose own class is
 *  -1 -- an island of road with no segment to read it from. */
static int api_strip_road_class(lua_State *L)
{
    Loft   *x = strip_of(L);
    int32_t tc, tr;
    if (!x)
        return 0;
    /*  Held to the map: a strip whose middle falls off the edge reads
     *  the tile it ran off, not whatever lies past the array. */
    tc = (int32_t)luaL_checkinteger(L, 2);
    tr = (int32_t)luaL_checkinteger(L, 3);
    if (tc < 0)
        tc = 0;
    if (tr < 0)
        tr = 0;
    if (tc >= R_MAP)
        tc = R_MAP - 1;
    if (tr >= R_MAP)
        tr = R_MAP - 1;
    lua_pushnumber(L, (float)road_class(x->c, tc, tr));
    return 1;
}

static int api_strip_count(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    lua_pushinteger(L, x->ns);
    return 1;
}

/*  One station, as plain numbers: where it is, the height its section
 *  was graded to, the way the centreline runs there, how far along the
 *  strip it stands, the band's two half widths as fractions, how far
 *  the nearest level crossing is, and the ground's own line. */
static int api_strip_at(lua_State *L)
{
    Loft *x = strip_of(L);
    int   i = (int)luaL_checkinteger(L, 2);
    if (!x || i < 0 || i >= x->ns)
        return 0;
    lua_pushnumber(L, x->smp[i].pos.x);
    lua_pushnumber(L, x->smp[i].pos.y);
    lua_pushnumber(L, x->smp[i].z);
    lua_pushnumber(L, x->smp[i].dir.x);
    lua_pushnumber(L, x->smp[i].dir.y);
    lua_pushnumber(L, x->smp[i].s);
    lua_pushnumber(L, x->smp[i].wl);
    lua_pushnumber(L, x->smp[i].wr);
    lua_pushnumber(L, x->smp[i].xd);
    lua_pushnumber(L, script_strip_zorig(x, i));
    return 10;
}

/*  How much narrower the band is where the centreline runs diagonally:
 *  the fit's own compensation, which keeps a diagonal road the width it
 *  looks rather than the width it measures. */
static int api_strip_width(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    lua_pushnumber(L, width_factor((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), x->comp));
    return 1;
}

/*  The class a quad is drawn under: the material reads it, and a value
 *  four higher says the quad lies in a cut, so the clipping check knows
 *  the ground standing over it is meant. */
static int api_strip_class(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    x->m->strip_class = (float)luaL_checknumber(L, 2);
    return 0;
}

/*  One cross-section of the ribbon to the next: the strip's own quad,
 *  cut on the tile folds.  `za`/`zb` are the two ends' heights, or -1
 *  each for a band that lies on the drawn surface at every corner it is
 *  cut into. */
static int api_strip_quad(lua_State *L)
{
    Loft *x = strip_of(L);
    float a0[2], a1[2], b0[2], b1[2];
    float za, zb, ac0, ac1, al0, al1, mat, order;
    if (!x)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    a0[0] = (float)luaL_checknumber(L, 2), a0[1] = (float)luaL_checknumber(L, 3);
    a1[0] = (float)luaL_checknumber(L, 4), a1[1] = (float)luaL_checknumber(L, 5);
    b0[0] = (float)luaL_checknumber(L, 6), b0[1] = (float)luaL_checknumber(L, 7);
    b1[0] = (float)luaL_checknumber(L, 8), b1[1] = (float)luaL_checknumber(L, 9);
    za = (float)luaL_checknumber(L, 10), zb = (float)luaL_checknumber(L, 11);
    ac0 = (float)luaL_checknumber(L, 12), ac1 = (float)luaL_checknumber(L, 13);
    al0 = (float)luaL_checknumber(L, 14), al1 = (float)luaL_checknumber(L, 15);
    mat = (float)luaL_checknumber(L, 16);
    order = (float)luaL_checknumber(L, 17);
    lua_pushboolean(L, strip_quad_z(x->m, x->c, x->mask_bit, order, a0, a1, b0, b1, za, zb, ac0, ac1, al0, al1, mat) == 0);
    return 1;
}

/*  The pair as the pipeline's own stages build it, for a check that the
 *  composition and they agree: the two edges, the across range, the
 *  along, the material and the tile's slot. */
static int api_strip_pair(lua_State *L)
{
    Loft    *x = strip_of(L);
    LoftPair p;
    int      i = (int)luaL_checkinteger(L, 2);
    if (!x || i < 1 || i >= x->ns || script_strip_pair(x, i, &p) != 0)
        return 0;
    lua_pushnumber(L, p.a0[0]), lua_pushnumber(L, p.a0[1]);
    lua_pushnumber(L, p.a1[0]), lua_pushnumber(L, p.a1[1]);
    lua_pushnumber(L, p.b0[0]), lua_pushnumber(L, p.b0[1]);
    lua_pushnumber(L, p.b1[0]), lua_pushnumber(L, p.b1[1]);
    lua_pushnumber(L, p.acr), lua_pushnumber(L, p.acl);
    lua_pushnumber(L, p.al_a), lua_pushnumber(L, p.al_b);
    lua_pushnumber(L, p.ma), lua_pushnumber(L, p.order);
    return 14;
}

/*  One station of the footway beside the strip: its outer edge, its
 *  inner one where it meets the carriageway, and the height. */
static int api_strip_walk_at(lua_State *L)
{
    if (!strip_of(L))
        return 0;
    lua_pushboolean(L, script_walk_at((int)luaL_checkinteger(L, 2),
                                      (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4),
                                      (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6),
                                      (float)luaL_checknumber(L, 7)));
    return 1;
}

/*  Where that footway's band begins and ends, which is what the network
 *  joins it to its neighbours by. */
static int api_strip_walk_ends(lua_State *L)
{
    if (!strip_of(L))
        return 0;
    script_walk_ends((int)luaL_checkinteger(L, 2),
                     (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4),
                     (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6));
    return 0;
}

/*  The pieces the fit produced, which the curve overlay draws in place
 *  of the roads: how many, how long each runs and whether it turns. */
static int api_strip_pieces(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    lua_pushinteger(L, x->np);
    return 1;
}

static int api_strip_piece(lua_State *L)
{
    Loft *x = strip_of(L);
    int   k = (int)luaL_checkinteger(L, 2);
    if (!x || k < 0 || k >= x->np)
        return 0;
    lua_pushnumber(L, x->pc[k].len);
    lua_pushboolean(L, x->pc[k].arc);
    return 2;
}

/*  Where a piece is `at` tiles along it. */
static int api_strip_piece_at(lua_State *L)
{
    Loft *x = strip_of(L);
    int   k = (int)luaL_checkinteger(L, 2);
    V2    p, d;
    if (!x || k < 0 || k >= x->np)
        return 0;
    piece_at(&x->pc[k], (float)luaL_checknumber(L, 3), &p, &d);
    lua_pushnumber(L, p.x), lua_pushnumber(L, p.y);
    lua_pushnumber(L, d.x), lua_pushnumber(L, d.y);
    return 4;
}

/*  A box standing on the ground under it: the mark at a piece boundary. */
static int api_strip_box(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, put_box(x->m, x->c, x->mask_bit, (float)luaL_checknumber(L, 2),
                               (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4),
                               (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6),
                               (float)luaL_checknumber(L, 7), (float)luaL_checknumber(L, 8),
                               (float)luaL_checknumber(L, 9), (float)luaL_checknumber(L, 10)) == 0);
    return 1;
}

/*  What a deck's station carries beside its place: which of its two
 *  outer lanes a ramp has taken, and the height of each where it has. */
static int api_strip_lane(lua_State *L)
{
    Loft *x = strip_of(L);
    int   i = (int)luaL_checkinteger(L, 2);
    if (!x || i < 0 || i >= x->ns)
        return 0;
    lua_pushinteger(L, x->smp[i].lane);
    lua_pushnumber(L, x->smp[i].zr[0]);
    lua_pushnumber(L, x->smp[i].zr[1]);
    return 3;
}

/*  One triangle of a structure, with a normal of its own: a deck's
 *  soffit hangs under the carriageway and faces down. */
static int api_strip_tri_n(lua_State *L)
{
    Loft *x = strip_of(L);
    float t[3][3], nrm[3], col3[3], ref[3] = {1.0f, 1.0f, 1.0f};
    int   i;
    if (!x)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    for (i = 0; i < 3; ++i)
    {
        t[i][0] = (float)luaL_checknumber(L, i * 3 + 2);
        t[i][1] = (float)luaL_checknumber(L, i * 3 + 3);
        t[i][2] = (float)luaL_checknumber(L, i * 3 + 4);
    }
    nrm[0] = (float)luaL_checknumber(L, 11), nrm[1] = (float)luaL_checknumber(L, 12);
    nrm[2] = (float)luaL_checknumber(L, 13);
    col3[0] = (float)luaL_checknumber(L, 14), col3[1] = (float)luaL_checknumber(L, 15);
    col3[2] = (float)luaL_checknumber(L, 16);
    lua_pushboolean(L, put_tri_road_n(x->m, x->c, x->mask_bit, (float)luaL_checknumber(L, 17),
                                      (const float (*)[3])t, nrm, col3, ref, ref) == 0);
    return 1;
}

/*  A wall between two points, from one pair of heights down to another:
 *  a deck's end, where it begins and ends in the air. */
static int api_strip_wall(lua_State *L)
{
    Loft *x = strip_of(L);
    float a[3], b[3], qa[3], qb[3], nrm[3], col3[3];
    if (!x)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    a[0] = (float)luaL_checknumber(L, 2), a[1] = (float)luaL_checknumber(L, 3);
    b[0] = (float)luaL_checknumber(L, 4), b[1] = (float)luaL_checknumber(L, 5);
    a[2] = (float)luaL_checknumber(L, 6), b[2] = (float)luaL_checknumber(L, 7);
    qa[0] = a[0], qa[1] = a[1], qa[2] = (float)luaL_checknumber(L, 8);
    qb[0] = b[0], qb[1] = b[1], qb[2] = (float)luaL_checknumber(L, 9);
    nrm[0] = (float)luaL_checknumber(L, 10), nrm[1] = (float)luaL_checknumber(L, 11);
    nrm[2] = (float)luaL_checknumber(L, 12);
    col3[0] = (float)luaL_checknumber(L, 14), col3[1] = (float)luaL_checknumber(L, 15);
    col3[2] = (float)luaL_checknumber(L, 16);
    lua_pushboolean(L, put_wall(x->m, a, b, qa, qb, nrm, (float)luaL_checknumber(L, 13), col3) == 0);
    return 1;
}

/*  A deck's edge: the girder's fascia down from the carriageway, in the
 *  deck's own shadow, and the parapet up from it. */
static int api_strip_edge(lua_State *L)
{
    Loft *x = strip_of(L);
    float ea[2], eb[2], nrm[3];
    if (!x)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    ea[0] = (float)luaL_checknumber(L, 2), ea[1] = (float)luaL_checknumber(L, 3);
    eb[0] = (float)luaL_checknumber(L, 4), eb[1] = (float)luaL_checknumber(L, 5);
    nrm[0] = (float)luaL_checknumber(L, 8), nrm[1] = (float)luaL_checknumber(L, 9), nrm[2] = 0.0f;
    lua_pushboolean(L, deck_edge(x->m, x->c, x->mask_bit, (float)luaL_checknumber(L, 12),
                                 ea, eb, (float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7),
                                 nrm, (float)luaL_checknumber(L, 10),
                                 lua_toboolean(L, 11)) == 0);
    return 1;
}

static const luaL_Reg STRIP[] = {
    {"lane",     api_strip_lane    },
    {"tri_n",    api_strip_tri_n   },
    {"wall",     api_strip_wall    },
    {"edge",     api_strip_edge    },
    {"pieces",   api_strip_pieces  },
    {"piece",    api_strip_piece   },
    {"piece_at", api_strip_piece_at},
    {"box",      api_strip_box     },
    {"walk_at",   api_strip_walk_at  },
    {"walk_ends", api_strip_walk_ends},
    {"pair",    api_strip_pair  },
    {"info",       api_strip_info      },
    {"road_class", api_strip_road_class},
    {"count",   api_strip_count },
    {"at",      api_strip_at    },
    {"width",   api_strip_width },
    {"class",   api_strip_class },
    {"quad",    api_strip_quad  },
    {"ground",  api_strip_ground},
    {"order",   api_strip_order },
    {NULL,      NULL    }
};

/*  ---- a junction ---------------------------------------------------- */

static JuncFan *junc_of(lua_State *L)
{
    return (JuncFan *)rec_of(L, "junction");
}

/*  What the junction IS: where its middle sits, the height its tile was
 *  levelled to, how many points its outline came to, and what its
 *  asphalt is laid in. */
static int api_junction_info(lua_State *L)
{
    JuncFan *j = junc_of(L);
    if (!j)
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, j->cx), lua_setfield(L, -2, "x");
    lua_pushnumber(L, j->cy), lua_setfield(L, -2, "y");
    lua_pushnumber(L, j->zj), lua_setfield(L, -2, "z");
    lua_pushnumber(L, j->mat), lua_setfield(L, -2, "mat");
    lua_pushnumber(L, j->order), lua_setfield(L, -2, "order");
    lua_pushinteger(L, j->np), lua_setfield(L, -2, "n");
    lua_pushinteger(L, j->jb->col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, j->jb->row), lua_setfield(L, -2, "row");
    lua_pushboolean(L, j->square), lua_setfield(L, -2, "square");
    return 1;
}

static int api_junction_count(lua_State *L)
{
    JuncFan *j = junc_of(L);
    if (!j)
        return 0;
    lua_pushinteger(L, j->np);
    return 1;
}

static int api_junction_at(lua_State *L)
{
    JuncFan *j = junc_of(L);
    int      i = (int)luaL_checkinteger(L, 2);
    if (!j || i < 0 || i >= j->np)
        return 0;
    lua_pushnumber(L, j->poly[i].x);
    lua_pushnumber(L, j->poly[i].y);
    return 2;
}

/*  The ground the junction's asphalt sits on at a point: its own tile is
 *  a levelled pad, so inside it every height is that flat one; where the
 *  outline reaches past the tile the asphalt follows the ground. */
static int api_junction_surface(lua_State *L)
{
    JuncFan *j = junc_of(L);
    if (!j)
        return 0;
    lua_pushnumber(L, junc_surface(j->jb->f, j->jb->c, j->jb->mask_bit, j->jb->col, j->jb->row,
                                   (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), j->zj));
    return 1;
}

/*  One triangle of the asphalt, laid ON the drawn surface: the fan's own
 *  heights are a guide, and each piece takes the surface where it lands,
 *  so a fan across a tile's fold does not cut under the terrain. */
static int api_junction_tri(lua_State *L)
{
    JuncFan *j = junc_of(L);
    float    t[3][3];
    float    rc[3], ref[3] = {0.5f, 0.5f, 0.5f}, ref2[3] = {-1.0f, -1.0f, -1.0f};
    int      i;
    if (!j)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    for (i = 0; i < 3; ++i)
    {
        t[i][0] = (float)luaL_checknumber(L, i * 3 + 2);
        t[i][1] = (float)luaL_checknumber(L, i * 3 + 3);
        t[i][2] = (float)luaL_checknumber(L, i * 3 + 4);
    }
    rc[0] = 0.0f, rc[1] = 0.0f, rc[2] = j->mat;
    lua_pushboolean(L, put_tri_ground(j->jb->m, j->jb->c, j->jb->mask_bit, j->order,
                                      (const float (*)[3])t, NULL, rc, ref, ref2) == 0);
    return 1;
}

/*  The square the box always was, for an outline that came to nothing. */
static int api_junction_quad(lua_State *L)
{
    JuncFan *j = junc_of(L);
    if (!j || !j->a0)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, strip_quad_z(j->jb->m, j->jb->c, j->jb->mask_bit, j->order,
                                    j->a0, j->a1, j->b0, j->b1, j->zj, j->zj,
                                    0.5f, 0.5f, -1.0f, -1.0f, j->mat) == 0);
    return 1;
}

/*  ---- the world -----------------------------------------------------
 *
 *  The build itself, handed to the rule that DRIVES it.  Everything here
 *  is a primitive: one tile's ground, one tile's tint, and the three
 *  network passes.  What the script decides is the ORDER -- which tiles,
 *  in what order, which passes run at all -- and there is no C loop
 *  behind it. */
static WorldFan *world_of(lua_State *L)
{
    return (WorldFan *)rec_of(L, "world");
}

/*  What the build IS: how wide the map is, which pass this is, and what
 *  it is allowed to draw. */
static int api_world_info(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, R_MAP), lua_setfield(L, -2, "size");
    lua_pushinteger(L, w->pass), lua_setfield(L, -2, "pass");
    lua_pushboolean(L, w->roads), lua_setfield(L, -2, "roads");
    lua_pushboolean(L, w->underground), lua_setfield(L, -2, "underground");
    return 1;
}

/*  Whether this build wants a tile at all.  An edit's build replaces
 *  only the chunks its closure named, and the rest stand: asking here
 *  saves the call, and the primitives check it again for themselves. */
static int api_world_wanted(lua_State *L)
{
    WorldFan *w = world_of(L);
    lua_pushboolean(L, w && (w->pass == 1 || mesh_want_tile((int32_t)luaL_checkinteger(L, 2),
                                                            (int32_t)luaL_checkinteger(L, 3))));
    return 1;
}

/*  One tile, GATHERED and handed over: its ground, or its zone for the
 *  map view's tint.  Answers a `tile` handle, or nothing where this
 *  build wants no such tile -- so a script writes
 *
 *      local t = w:tile(col, row)
 *      if t then ... end
 *
 *  and composes it itself.  The record is the world's own and is written
 *  again at the next ask, which is what keeps a map's worth of tiles
 *  from being a map's worth of allocations. */
static TileFan s_world_tile;

static int world_fan(lua_State *L, int ground)
{
    WorldFan *w   = world_of(L);
    int32_t   col = (int32_t)luaL_checkinteger(L, 2);
    int32_t   row = (int32_t)luaL_checkinteger(L, 3);
    if (!w || !(ground ? mesh_ground_fan(col, row, &s_world_tile)
                       : mesh_tint_fan(col, row, &s_world_tile)))
        return 0;
    api_object_push(L, "tile", &s_world_tile);
    return 1;
}

static int api_world_tile(lua_State *L)
{
    return world_fan(L, 1);
}

static int api_world_zone(lua_State *L)
{
    return world_fan(L, 0);
}

/*  The SHAPE a script composes into: what the inspector names the
 *  triangles by, and what the checks group them into.  One at a time --
 *  opening the next closes the one before, and `w:shape()` with nothing
 *  to name closes the last -- which is the discipline the C loop kept
 *  when it owned this, and it means no script can leave one open. */
static int api_world_shape(lua_State *L)
{
    WorldFan   *w    = world_of(L);
    const char *what = lua_tostring(L, 2);
    if (!w)
        return 0;
    shape_close(w->shape);
    w->shape = SHAPE_NONE;
    if (what)
        w->shape = shape_open("%s at %d,%d", what,
                              (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4));
    return 0;
}

/*  A POWER LINE'S TILE, gathered and handed over: the pylon's place and
 *  the edges its wires span to, with the mesh opened for the script to
 *  draw into.  Answers the prop and whether the tile is a crossing --
 *  where a line runs over a road or a railway there is no pylon, only
 *  the span -- or nothing where no line stands here.
 *
 *  `w:emitted()` closes the mesh again.  Opening the next one closes the
 *  last, so a script that forgets cannot leave it open. */
static int api_world_power(lua_State *L)
{
    WorldFan   *w   = world_of(L);
    int32_t     col = (int32_t)luaL_checkinteger(L, 2);
    int32_t     row = (int32_t)luaL_checkinteger(L, 3);
    int32_t     idx = row * R_MAP + col;
    uint8_t     b;
    Family      f, f2;
    int         piece, second, links, crossing;
    ScriptProp  at;
    if (!w)
        return 0;
    b      = w->c->xbld[idx];
    piece  = piece_family(b, &f);
    second = piece_second(b, &f2);
    /*  A line on its own ground, or the second family of a crossing:
     *  either way it is the POWER family that draws it. */
    if (piece >= 0 && f == F_POWER)
        crossing = 0, links = piece_links(w->l, piece, w->c->xter[idx]);
    else if (second >= 0 && f2 == F_POWER)
        crossing = 1, links = piece_links(w->l, second, w->c->xter[idx]);
    else
        return 0;
    if (!links)
        return 0;
    memset(&at, 0, sizeof at);
    at.col = (int)col, at.row = (int)row, at.arm = -1, at.links = links;
    at.x = (float)col + 0.5f, at.y = (float)row + 0.5f;
    at.z  = surface_at_world(w->c, w->mask_bit, at.x, at.y);
    at.fx = 1.0f, at.fy = 0.0f;
    /*  The shape is the primitive's, so the inspector names the LINE
     *  rather than the pole a primitive drew, and says which way it
     *  runs.  A crossing shares its tile with the road or the line
     *  under it, and says so. */
    shape_close(w->shape);
    w->shape = shape_open("power line at %d,%d%s", (int)col, (int)row,
                          crossing ? ", sharing the tile" : "");
    shape_note("links\t%s%s%s%s", links & L_N ? "north " : "", links & L_E ? "east " : "",
               links & L_S ? "south " : "", links & L_W ? "west " : "");
    script_emit_close();
    script_emit_open(w->m, w->c, w->mask_bit, tile_order(w->c, col, row, w->mask_bit));
    api_prop_push(L, &at);
    lua_pushboolean(L, crossing);
    return 2;
}

/*  THE FOOTWAYS, one path at a time.  `w:footways()` says how many the
 *  network holds and whether the outline is on; `w:footway(i)` gathers
 *  one and opens its shape, or answers nothing where there is nothing
 *  there to draw. */
static WalkFan s_world_walk;

static int api_world_footways(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, sidewalk_count());
    lua_pushboolean(L, sidewalk_outline());
    return 2;
}

static int api_world_footway(lua_State *L)
{
    WorldFan *w = world_of(L);
    ShapeId   sh;
    if (!w)
        return 0;
    /*  The one before is closed FIRST: a shape opened while another is
     *  still open nests inside it, and closing the outer one then takes
     *  the inner with it -- which leaves every triangle the script drew
     *  claimed by nothing at all. */
    shape_close(w->shape);
    w->shape = SHAPE_NONE;
    if (!sidewalk_gather(w->m, w->c, w->mask_bit, (int)luaL_checkinteger(L, 2),
                         &s_world_walk, &sh))
        return 0;
    w->shape = sh;
    api_object_push(L, "footway", &s_world_walk);
    return 1;
}

/*  THE JUNCTION RINGS.  A junction's outline is walked from the arms the
 *  measure filled, and both the trims and the box drawn later read it,
 *  so the script is asked for every one in a pass of its own between the
 *  two.  `w:junctions()` says how many there are, `w:junction(i)` sets
 *  one up and hands over its arms, and `w:junction_ring()` keeps what
 *  the script walked. */
static OutlineFan s_world_ring;
static V2         s_world_ring_out[JUNC_MAX];
static uint8_t    s_world_ring_mouth[JUNC_MAX];
static float      s_world_ring_trim[4];

static int api_world_junctions(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    junction_rings_reset();
    lua_pushinteger(L, build_junction_count(w->c, w->l));
    return 1;
}

static int api_world_junction(lua_State *L)
{
    WorldFan *w = world_of(L);
    Family    f;
    int32_t   col, row;
    int       links;
    if (!w || !build_junction_nth(w->c, w->l, (int)luaL_checkinteger(L, 2), &f, &col, &row, &links))
        return 0;
    if (!junction_ask(w->c, f, col, row, links, &s_world_ring, s_world_ring_out,
                      s_world_ring_mouth, s_world_ring_trim))
        return 0;
    api_object_push(L, "outline", &s_world_ring);
    return 1;
}

static int api_world_junction_ring(lua_State *L)
{
    if (!world_of(L))
        return 0;
    junction_ring_keep(&s_world_ring);
    return 0;
}

/*  THE JUNCTION CONTROLS.  `w:controls()` takes the reading -- every
 *  junction on the map, and what its own family measured there -- and
 *  answers how many there are.  `w:control(i)` hands one over as the
 *  name of the rule that settles it and a table of the reading, and
 *  `w:control_is` puts the answer, a stop or a signal an arm, on to the
 *  tile.  Nothing that turns on a control is measured until every one
 *  of them has been answered. */
static int api_world_controls(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    build_networks_controls(w->c, w->l);
    lua_pushinteger(L, net_control_asked());
    return 1;
}

static int api_world_control(lua_State *L)
{
    int         i = (int)luaL_checkinteger(L, 2), e, links, busy;
    int32_t     col, row;
    const int  *cls, *traf;
    const char *rule;
    if (!world_of(L))
        return 0;
    rule = net_control_ask_at(i, &col, &row, &links, &cls, &traf, &busy);
    if (!rule)
        return 0;
    lua_pushstring(L, rule);
    lua_newtable(L);
    lua_pushinteger(L, col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, row), lua_setfield(L, -2, "row");
    lua_pushinteger(L, links), lua_setfield(L, -2, "links");
    lua_pushboolean(L, busy), lua_setfield(L, -2, "busy");
    if (cls)
    {
        /*  The arms, class and traffic, with nothing where there is no
         *  arm: a rule reads them by edge. */
        lua_newtable(L);
        for (e = 0; e < 4; ++e)
        {
            if (cls[e] < 0)
                continue;
            lua_newtable(L);
            lua_pushinteger(L, cls[e]), lua_setfield(L, -2, "class");
            lua_pushinteger(L, traf[e]), lua_setfield(L, -2, "traffic");
            lua_rawseti(L, -2, e + 1);
        }
        lua_setfield(L, -2, "arms");
    }
    return 2;
}

static int api_world_control_is(lua_State *L)
{
    int i = (int)luaL_checkinteger(L, 2), e, v = 0;
    if (!world_of(L))
        return 0;
    if (lua_istable(L, 3))
        for (e = 0; e < 4; ++e)
        {
            lua_rawgeti(L, 3, e + 1);
            v |= ((int)lua_tointeger(L, -1) & 3) << (2 * e);
            lua_pop(L, 1);
        }
    net_control_is(i, v);
    return 0;
}

/*  THE CORRIDOR SHELVES the grading pass left, for the rule that
 *  reconciles the copies two corridors leave at a corner they share. */
static ShelfFan s_world_shelf;

static int api_world_shelf(lua_State *L)
{
    if (!world_of(L) || !shelf_ask(&s_world_shelf))
        return 0;
    api_object_push(L, "shelf", &s_world_shelf);
    return 1;
}

/*  Stage three: the trims, off the rings just walked.  It answers how
 *  many mouths asked for a crosswalk, which the drive then has answered
 *  one at a time. */
static int api_world_trims(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    build_networks_trims(w->c, w->l);
    lua_pushinteger(L, net_xwalk_asked());
    return 1;
}

/*  ONE MOUTH'S CROSSWALK: what the outline asked for, the road there is
 *  to give up, and how much of it runs straight from the mouth.  The
 *  depth the rule answers is held to the road, so one crossing can
 *  never eat the segment. */
static int api_world_xwalk(lua_State *L)
{
    int     i = (int)luaL_checkinteger(L, 2), e, ctrl;
    int32_t col, row;
    float   want, room, straight;
    if (!world_of(L) || !net_xwalk_ask_at(i, &col, &row, &e, &ctrl, &want, &room, &straight))
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, row), lua_setfield(L, -2, "row");
    lua_pushinteger(L, e), lua_setfield(L, -2, "arm");
    lua_pushinteger(L, ctrl), lua_setfield(L, -2, "control");
    lua_pushnumber(L, (double)want), lua_setfield(L, -2, "want");
    lua_pushnumber(L, (double)room), lua_setfield(L, -2, "room");
    lua_pushnumber(L, (double)straight), lua_setfield(L, -2, "straight");
    return 1;
}

static int api_world_xwalk_deep(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_xwalk_deep((int)luaL_checkinteger(L, 2), (float)luaL_optnumber(L, 3, 0.0));
    return 0;
}

/*  A LEVEL CROSSING.  `w:crossing(col,row)` gathers the one on that tile
 *  and opens its shape, answering what the script needs to measure it --
 *  the sine of the angle the road and the line cross at, and their two
 *  widths.  The script hands the measurements back through
 *  `w:crossing_frame`, and `w:crossing_draw` lays the panel, the record
 *  and the approaches from them. */
static int api_world_crossing(lua_State *L)
{
    WorldFan *w      = world_of(L);
    int32_t   col    = (int32_t)luaL_checkinteger(L, 2);
    int32_t   row    = (int32_t)luaL_checkinteger(L, 3);
    int32_t   idx    = row * R_MAP + col;
    Family    f2;
    int       second;
    float     sine, road, rail;
    int32_t   xc, xr;
    if (!w)
        return 0;
    second = piece_second(w->c->xbld[idx], &f2);
    if (second < 0 || !net_family_has(net_family(f2), NH_CROSSING))
        return 0;
    if (build_crossing(w->m, w->c, w->l, w->mask_bit, col, row, second) != 0)
        return 0;
    net_crossing_ask(&xc, &xr, &sine, &road, &rail);
    lua_newtable(L);
    lua_pushinteger(L, xc), lua_setfield(L, -2, "col");
    lua_pushinteger(L, xr), lua_setfield(L, -2, "row");
    lua_pushnumber(L, sine), lua_setfield(L, -2, "sin");
    lua_pushnumber(L, road), lua_setfield(L, -2, "road");
    lua_pushnumber(L, rail), lua_setfield(L, -2, "rail");
    return 1;
}

static int api_world_crossing_frame(lua_State *L)
{
    ScriptXing fr;
    if (!world_of(L) || !lua_istable(L, 2))
        return 0;
    memset(&fr, 0, sizeof fr);
    lua_pushvalue(L, 2);
    fr.reach = api_field_num(L, "reach", 0.0f);
    fr.mast  = api_field_num(L, "mast", 0.0f);
    fr.bed   = api_field_num(L, "bed", 0.0f);
    fr.lift  = api_field_num(L, "lift", 0.0f);
    fr.slot  = api_field_num(L, "slot", 0.0f);
    lua_pop(L, 1);
    net_crossing_frame(&fr);
    return 0;
}

/*  The record, and the panel's four corners: answered as a `panel`
 *  handle for the script to lay the panel over, or nothing where the two
 *  lines do not meet and there is none to lay. */
static int api_world_crossing_panel(lua_State *L)
{
    const XingFan *f;
    if (!world_of(L) || !(f = net_crossing_panel()))
        return 0;
    api_object_push(L, "panel", (void *)f);
    return 1;
}

/*  ONE APPROACH of the crossing: what the script needs to decide what
 *  stands on it, with the mesh opened for what it puts there.  Answers
 *  nothing past the second. */
static int api_world_crossing_approach(lua_State *L)
{
    WorldFan         *w = world_of(L);
    ScriptApproachAsk a;
    if (!w || !net_crossing_approach((int)luaL_checkinteger(L, 2), &a))
        return 0;
    /*  The mesh, opened for the marks the script decides on: each of
     *  them stands at the crossing's own place in the stack, and
     *  w:crossing_mark puts it where the script said. */
    script_emit_close();
    w->m->strip_class = 0.0f;
    script_emit_open(w->m, w->c, w->mask_bit, net_crossing_order());
    lua_newtable(L);
    lua_pushnumber(L, a.reach), lua_setfield(L, -2, "reach");
    lua_pushnumber(L, a.mast), lua_setfield(L, -2, "mast");
    lua_pushnumber(L, a.limit), lua_setfield(L, -2, "limit");
    lua_pushnumber(L, a.road), lua_setfield(L, -2, "road");
    lua_pushnumber(L, a.x), lua_setfield(L, -2, "x");
    lua_pushnumber(L, a.y), lua_setfield(L, -2, "y");
    lua_pushnumber(L, a.fx), lua_setfield(L, -2, "fx");
    lua_pushnumber(L, a.fy), lua_setfield(L, -2, "fy");
    lua_pushnumber(L, a.gx), lua_setfield(L, -2, "gx");
    lua_pushnumber(L, a.gy), lua_setfield(L, -2, "gy");
    return 1;
}

/*  And one mark where the script put it: `out` along the approach from
 *  the middle, `across` from its centreline, and the model that stands
 *  there. */
static int api_world_crossing_mark(lua_State *L)
{
    WorldFan       *w = world_of(L);
    ScriptApproach  mk;
    const char     *model;
    if (!w || !lua_istable(L, 2))
        return 0;
    memset(&mk, 0, sizeof mk);
    lua_pushvalue(L, 2);
    mk.out    = api_field_num(L, "out", 0.0f);
    mk.across = api_field_num(L, "across", 0.0f);
    lua_getfield(L, -1, "model");
    model = lua_tostring(L, -1);
    snprintf(mk.model, sizeof mk.model, "%s", model ? model : "");
    lua_pop(L, 2);
    if (net_crossing_place(&mk) != 0)
        w->rc = -1;
    lua_pushboolean(L, w->rc == 0);
    return 1;
}

/*  And the approaches: the masts, the stop lines and the signs. */
static int api_world_crossing_approaches(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = net_crossing_approaches();
    if (rc != 0)
        w->rc = rc;
    lua_pushboolean(L, rc == 0);
    return 1;
}

static int api_world_emitted(lua_State *L)
{
    if (world_of(L))
        script_emit_close();
    return 0;
}

/*  The three network passes.  Each answers whether it came out, and a
 *  failure is kept on the build so the pass it belongs to ends badly
 *  rather than quietly. */
static int world_nets(lua_State *L, int what)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = mesh_world_nets(w, what);
    if (rc != 0)
        w->rc = rc;
    lua_pushboolean(L, rc == 0);
    return 1;
}

/*  WHERE EVERY FAMILY'S LANES RUN.  `w:lane_runs()` answers how many
 *  family-and-class pairs the pipeline can present; `w:lane_run(i)` names
 *  one, and `w:lane_run_is` takes the offsets the rule answered for it.
 *  They are settled before the lane model is built, so nothing inside it
 *  has to ask. */
static int api_world_lane_runs(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_lane_runs_reset();
    lua_pushinteger(L, net_lane_runs());
    return 1;
}

static int api_world_lane_run(lua_State *L)
{
    const char *fam;
    int         cls, i = (int)luaL_checkinteger(L, 2);
    if (!world_of(L) || i < 0 || i >= net_lane_runs())
        return 0;
    net_lane_run_at(i, &fam, &cls);
    lua_pushstring(L, fam);
    lua_pushinteger(L, cls);
    return 2;
}

static int api_world_lane_run_is(lua_State *L)
{
    float off[4];
    int   i = (int)luaL_checkinteger(L, 2), k, n = 0;
    if (!world_of(L))
        return 0;
    if (lua_istable(L, 3))
    {
        n = (int)lua_rawlen(L, 3);
        if (n > (int)(sizeof off / sizeof off[0]))
            n = (int)(sizeof off / sizeof off[0]);
        for (k = 0; k < n; ++k)
        {
            lua_rawgeti(L, 3, k + 1);
            off[k] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
    }
    net_lane_run_is(i, off, n);
    return 0;
}

/*  WHERE THE TRAFFIC RUNS on a family whose stage is a rule rather than
 *  a primitive: the drive asks it once for every class before any strip
 *  is built.  `w:traffic_run(i)` answers nothing for a pair no rule
 *  settles. */
static int api_world_traffic_runs(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_traffic_runs_reset();
    lua_pushinteger(L, net_traffic_runs());
    return 1;
}

static int api_world_traffic_run(lua_State *L)
{
    const char *rule;
    int         cls;
    if (!world_of(L) || (rule = net_traffic_run_at((int)luaL_checkinteger(L, 2), &cls)) == NULL)
        return 0;
    lua_pushstring(L, rule);
    lua_pushinteger(L, cls);
    return 2;
}

static int api_world_traffic_run_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_traffic_run_is((int)luaL_checkinteger(L, 2), (float)luaL_optnumber(L, 3, 0.0), (float)luaL_optnumber(L, 4, 0.0));
    return 0;
}

static int api_world_lanes(lua_State *L)
{
    return world_nets(L, 0);
}

/*  ONE BOUNDARY of a fit, where two of its lines meet.  `p:pair(k)`
 *  answers what the two rules that settle it read: whether the far line
 *  crosses the one after it and how far ahead that lies, and whether
 *  these two cross at all.  `p:after_is` takes the first answer and
 *  `p:try` the second, one way at a time until one holds. */
static int api_path_pairs(lua_State *L)
{
    if (!rec_of(L, "path"))
        return 0;
    lua_pushinteger(L, path_pairs());
    return 1;
}

static int api_path_pair(lua_State *L)
{
    PathPair a;
    if (!rec_of(L, "path") || !path_pair((int)luaL_checkinteger(L, 2), &a))
        return 0;
    lua_newtable(L); /* what lies after the far line */
    lua_pushboolean(L, a.has_after), lua_setfield(L, -2, "has_after");
    lua_pushboolean(L, a.met), lua_setfield(L, -2, "met");
    lua_pushboolean(L, a.free), lua_setfield(L, -2, "free");
    lua_pushnumber(L, (double)a.ahead), lua_setfield(L, -2, "ahead");
    lua_pushnumber(L, (double)a.reach), lua_setfield(L, -2, "reach");
    lua_newtable(L); /* and the boundary itself */
    lua_pushboolean(L, a.cross), lua_setfield(L, -2, "cross");
    lua_pushboolean(L, a.free_join), lua_setfield(L, -2, "free");
    return 2;
}

static int api_path_after_is(lua_State *L)
{
    const char *v = lua_tostring(L, 2);
    if (!rec_of(L, "path"))
        return 0;
    path_after_is(v && strcmp(v, "crossing") == 0);
    return 0;
}

static int api_path_try(lua_State *L)
{
    const char *rule;
    void       *o;
    if (!rec_of(L, "path"))
        return 0;
    if ((rule = path_try(lua_tostring(L, 2), &o)) == NULL)
        return 0;
    lua_pushstring(L, rule);
    api_object_push(L, rule, o);
    return 2;
}

static int api_path_held(lua_State *L)
{
    if (!rec_of(L, "path"))
        return 0;
    lua_pushboolean(L, path_held());
    return 1;
}

/*  THE LINES the fit walks the boundaries of: the runs the corridor
 *  lets be straight, and the chain of lines they become.  Both are the
 *  scripts', so the fit stops for each and takes up what it answered. */
static int api_path_runs(lua_State *L)
{
    RunFan *r;
    if (!rec_of(L, "path") || (r = path_runs()) == NULL)
        return 0;
    lua_pushstring(L, "runs");
    api_object_push(L, "runs", r);
    return 2;
}

static int api_path_chain(lua_State *L)
{
    ChainFan *c;
    if (!rec_of(L, "path") || (c = path_chain()) == NULL)
        return 0;
    lua_pushstring(L, "chain");
    api_object_push(L, "chain", c);
    return 2;
}

/*  And the path as it stands at the end, for the rule that drops the
 *  idle vertices and settles the radius at each of the rest. */
static int api_path_ending(lua_State *L)
{
    FitFan *f;
    if (!rec_of(L, "path") || (f = path_ending()) == NULL)
        return 0;
    lua_pushstring(L, "fit");
    api_object_push(L, "fit", f);
    return 2;
}

static int api_path_lined(lua_State *L)
{
    if (!rec_of(L, "path"))
        return 0;
    lua_pushinteger(L, path_lined());
    return 1;
}

static const luaL_Reg PATH[] = {
    {"runs",     api_path_runs    },
    {"chain",    api_path_chain   },
    {"lined",    api_path_lined   },
    {"ending",   api_path_ending  },
    {"pairs",    api_path_pairs   },
    {"pair",     api_path_pair    },
    {"after_is", api_path_after_is},
    {"try",      api_path_try     },
    {"held",     api_path_held    },
    {NULL,       NULL             }
};

/*  THE FITS the walk read but did not run: every segment's path, and for
 *  a family whose runs may leave its own cells the two candidates and
 *  the choice between them. */
static int api_world_fits(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, net_fits());
    return 1;
}

static int api_world_fit(lua_State *L)
{
    WorldFan *w = world_of(L);
    void     *h;
    if (!w)
        return 0;
    net_fit_begin(w->c, (int)luaL_checkinteger(L, 2));
    if ((h = path_handle()) == NULL)
        return 0;
    api_object_push(L, "path", h);
    return 1;
}

static int api_world_fit_done(lua_State *L)
{
    const int  *free_, *held;
    const char *fam;
    int         k;
    static const char *const KEY[3] = {"corners", "tight", "nodes"};
    if (!world_of(L) || !net_fit_done((int)luaL_checkinteger(L, 2)))
        return 0;
    if ((fam = net_fit_choice(&free_, &held)) == NULL)
        return 0;
    lua_newtable(L);
    lua_pushstring(L, fam), lua_setfield(L, -2, "family");
    lua_newtable(L);
    for (k = 0; k < 3; ++k)
        lua_pushinteger(L, free_[k]), lua_setfield(L, -2, KEY[k]);
    lua_setfield(L, -2, "free");
    lua_newtable(L);
    for (k = 0; k < 3; ++k)
        lua_pushinteger(L, held[k]), lua_setfield(L, -2, KEY[k]);
    lua_setfield(L, -2, "held");
    return 1;
}

static int api_world_fit_choice_is(lua_State *L)
{
    const char *v = lua_tostring(L, 2);
    if (!world_of(L))
        return 0;
    net_fit_choice_is(!v || strcmp(v, "free") == 0);
    return 0;
}

/*  ONE CLASS FOR A WHOLE SEGMENT, before anything is fitted.
 *  `w:seg_classes()` steps every segment and answers how many there are;
 *  `w:seg_class(i)` hands over how many of one segment's tiles read as
 *  each class, and `w:seg_class_is` takes the class settled from them. */
static int api_world_seg_classes(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    lua_pushinteger(L, build_networks_classes(w->m, w->c, w->l, w->mask_bit, !w->rotated));
    return 1;
}

static int api_world_seg_class(lua_State *L)
{
    int cnt[3], k;
    if (!world_of(L) || !net_seg_class_at((int)luaL_checkinteger(L, 2), cnt))
        return 0;
    lua_newtable(L);
    for (k = 0; k < 3; ++k)
    {
        lua_pushinteger(L, cnt[k]);
        lua_rawseti(L, -2, k + 1);
    }
    return 1;
}

static int api_world_seg_class_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_seg_class_is((int)luaL_checkinteger(L, 2), (int)luaL_optinteger(L, 3, 0));
    return 0;
}

static int api_world_networks(lua_State *L)
{
    return world_nets(L, 1);
}

static int api_world_networks_draw(lua_State *L)
{
    return world_nets(L, 3);
}

/*  THE DRAWING PASS, one family at a time.  `w:net_families()` says how
 *  many networks are walked, `w:junction_boxes(fk)` draws one family's
 *  junctions -- all of them, before any of its segments, so a leg knows
 *  whether it is signalled before it draws its crosswalk -- and
 *  `w:segments(fk)` sets the cursor over what follows.  `w:segment()`
 *  draws the next of them and answers whether there was one. */
static int api_world_net_families(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, build_draw_families());
    return 1;
}

static int api_world_junction_boxes(lua_State *L)
{
    WorldFan *w  = world_of(L);
    int       fk = (int)luaL_checkinteger(L, 2);
    if (!w || fk < 0 || fk >= build_draw_families())
        return 0;
    build_draw_boxes_begin(w->m, w->c, w->l, w->mask_bit, fk);
    return 0;
}

/*  ONE JUNCTION'S BOX.  Answers nothing when the family has no more,
 *  false where the box lays no asphalt -- a rail junction, or a box
 *  reaching no chunk this build draws -- and the outline to lay it on
 *  otherwise.  `w:junction_box_done` takes up the footway round it, the
 *  signs on its sides and its corners. */
static int api_world_junction_box(lua_State *L)
{
    WorldFan *w = world_of(L);
    JuncFan  *j;
    int       rc;
    if (!w)
        return 0;
    rc = build_draw_box_next();
    if (rc < 0)
        w->rc = rc;
    if (rc <= 0)
        return 0;
    if ((j = net_junction_fan()) == NULL)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    api_object_push(L, "junction", j);
    return 1;
}

/*  And the strips the box gathered rather than drew: a rail junction's
 *  tracks, each lofted as a segment's strip is. */
static int api_world_box_lofts(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, net_box_lofts());
    return 1;
}

static int api_world_box_loft(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = net_box_loft((int)luaL_checkinteger(L, 2));
    if (rc != 0)
        w->rc = rc;
    return 0;
}

static int api_world_junction_box_done(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_junction_done();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

static int api_world_segments(lua_State *L)
{
    WorldFan *w  = world_of(L);
    int       fk = (int)luaL_checkinteger(L, 2);
    if (!w || fk < 0 || fk >= build_draw_families())
        return 0;
    build_draw_begin(w->m, w->c, w->l, w->mask_bit, !w->rotated, fk);
    return 0;
}

static int api_world_segment(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_draw_next();
    if (rc < 0)
        w->rc = rc;
    lua_pushboolean(L, rc > 0);
    return 1;
}

/*  And what follows the strip the last one lofted: the tiles it serves
 *  and the caps at its dead ends. */
static int api_world_segment_done(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_draw_done();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

static int api_world_networks_drawn(lua_State *L)
{
    if (!world_of(L))
        return 0;
    build_networks_drawn();
    return 0;
}

static int api_world_highways(lua_State *L)
{
    return world_nets(L, 2);
}

/*  THE LOFT'S STAGES, in the order it asks for them.  Each answers the
 *  name of the rule that settles it and the thing that rule is handed,
 *  or nothing where a primitive of the pipeline's own settled it.  A
 *  family declares which of the two answers each stage. */
static int loft_stage(lua_State *L, const char *rule, const char *kind, void *obj)
{
    if (!rule)
        return 0;
    lua_pushstring(L, rule);
    api_object_push(L, kind, obj);
    return 2;
}

static int api_world_loft_taper(lua_State *L)
{
    if (!world_of(L))
        return 0;
    return loft_stage(L, net_loft_taper(), "strip", net_loft_working());
}

static int api_world_loft_profile(lua_State *L)
{
    GroundFan  *g;
    const char *rule, *kind;
    void       *o;
    if (!world_of(L))
        return 0;
    rule = net_loft_profile(&g);
    /*  A family's own primitive may leave a reading of its own for the
     *  rule -- a deck's elevation rather than the strip. */
    if ((o = net_stage_taken(&kind)) != NULL)
        return loft_stage(L, rule, kind, o);
    return g ? loft_stage(L, rule, "ground", g) : loft_stage(L, rule, "strip", net_loft_working());
}

static int api_world_loft_dropped(lua_State *L)
{
    const char *rule, *kind;
    void       *o;
    if (!world_of(L))
        return 0;
    rule = net_loft_dropped();
    o    = net_stage_taken(&kind);
    return o ? loft_stage(L, rule, kind, o) : 0;
}

static int api_world_loft_works(lua_State *L)
{
    if (!world_of(L))
        return 0;
    return loft_stage(L, net_loft_works(), "strip", net_loft_working());
}

/*  THE RECORD and THE FURNITURE.  Both are a stage of two halves with
 *  the drive between them: what the strip leaves for the passes that
 *  read it, and what stands beside it.  A road's footway is composed by
 *  the rule; a road's lamps and a railway's signs are answered as a list
 *  and placed by the pipeline, since where each stands along the strip
 *  is the rule's and the walk that turns a distance into a place is
 *  not. */
static int api_world_loft_record(lua_State *L)
{
    if (!world_of(L))
        return 0;
    return loft_stage(L, net_loft_record(), "strip", net_loft_working());
}

static int api_world_loft_record_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_road_walks_drew(lua_toboolean(L, 2));
    return 0;
}

static int api_world_loft_furniture(lua_State *L)
{
    const char *rule;
    Loft       *o;
    if (!world_of(L))
        return 0;
    rule = net_loft_furniture();
    o    = net_loft_working();
    if (!rule || !o)
        return 0;
    lua_pushstring(L, rule);
    /*  A road's lamps are spaced by class and length; a railway's marks
     *  are placed against the crossings on the way and the ends the line
     *  runs on past.  A family that answers the stage with a rule of its
     *  own is handed the strip itself. */
    if (strcmp(rule, "lamps") == 0)
    {
        lua_pushnumber(L, (double)o->d->cls);
        lua_pushnumber(L, (double)o->total);
        return 3;
    }
    if (strcmp(rule, "rail_marks") == 0)
    {
        const float *cross;
        int          nc, i;
        net_rail_cross_ask(&cross, &nc);
        lua_newtable(L);
        lua_pushnumber(L, (double)o->total), lua_setfield(L, -2, "len");
        lua_pushboolean(L, o->pin1), lua_setfield(L, -2, "ahead");
        lua_pushboolean(L, o->pin0), lua_setfield(L, -2, "behind");
        lua_newtable(L);
        for (i = 0; i < nc; ++i)
            lua_pushnumber(L, (double)cross[i]), lua_rawseti(L, -2, i + 1);
        lua_setfield(L, -2, "crossings");
        return 2;
    }
    api_object_push(L, "strip", o);
    return 2;
}

static int api_world_loft_furniture_is(lua_State *L)
{
    ScriptLamp lamp[64];
    ScriptMark mark[192];
    const char *rule;
    if (!world_of(L))
        return 0;
    rule = net_loft_furniture_rule();
    if (!rule)
        return 0;
    if (strcmp(rule, "lamps") == 0)
        net_road_lamps_are(lamp, api_take_lamps(L, 2, lamp, (int)(sizeof lamp / sizeof lamp[0])));
    else if (strcmp(rule, "rail_marks") == 0)
        net_rail_marks_are(mark, api_take_marks(L, 2, mark, (int)(sizeof mark / sizeof mark[0])));
    return 0;
}

static int api_world_loft_recorded(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = net_loft_recorded();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

/*  THE STRIP THE LOFT FINISHED.  `w:curves()` answers it while the
 *  tuning window asks to see the fitted line over the world it made, and
 *  `w:strip()` answers it while the slab is drawn at all -- the grading
 *  pass lays no triangles.  `w:strip_done()` closes its shape.  Every
 *  loft in the build comes through this one place. */
static int api_world_curves(lua_State *L)
{
    Loft *o;
    if (!world_of(L) || (o = net_loft_curves()) == NULL)
        return 0;
    api_object_push(L, "strip", o);
    return 1;
}

static int api_world_strip(lua_State *L)
{
    Loft *o;
    if (!world_of(L) || (o = net_loft_strip()) == NULL)
        return 0;
    api_object_push(L, "strip", o);
    return 1;
}

static int api_world_strip_done(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = net_loft_close();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

/*  THE DECK BANDS, one at a time: the drive composes each deck between
 *  its loft and the lanes laid under it.  `w:hiway_band()` answers
 *  whether there was another. */
static int api_world_hiway_band(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_hiway_band_next();
    if (rc < 0)
        w->rc = rc;
    lua_pushboolean(L, rc > 0);
    return 1;
}

/*  THE DECK'S OWN FIT, run the same way a segment's is: two candidates
 *  and the choice between them, and then the band takes up the one that
 *  was kept.  A band the building pass replayed was never fitted, so it
 *  asks for none. */
/*  WHICH WAY A BAND IS WALKED from a cell that could start one.  There
 *  are four readings in all, so every one of them is settled before the
 *  walk begins. */
/*  HOW A RAMP'S FOOT MEETS ITS ROAD: eight readings, settled before any
 *  ramp is read. */
/*  EVERY ON-RAMP TILE READ ONCE: which side of it the deck lies, which
 *  the road, and which way round it runs, before any ramp is built. */
static int api_world_orients(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    lua_pushinteger(L, net_orients(w->c));
    return 1;
}

static int api_world_orient(lua_State *L)
{
    OrientFan *o;
    if (!world_of(L) || (o = net_orient_at((int)luaL_checkinteger(L, 2))) == NULL)
        return 0;
    api_object_push(L, "orient", o);
    return 1;
}

/*  AND WHICH WAY EACH RAMP'S TAPER LIES: how much deck it has each way,
 *  and whether the side away from its road is open at all. */
static int api_world_ramp_sides(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    lua_pushinteger(L, net_ramp_sides(w->c));
    return 1;
}

static int api_world_ramp_side(lua_State *L)
{
    int free_side, room, back;
    if (!world_of(L) || !net_ramp_side_at((int)luaL_checkinteger(L, 2), &free_side, &room, &back))
        return 0;
    lua_newtable(L);
    lua_pushboolean(L, free_side), lua_setfield(L, -2, "free");
    lua_pushinteger(L, room), lua_setfield(L, -2, "room");
    lua_pushinteger(L, back), lua_setfield(L, -2, "room_back");
    return 1;
}

static int api_world_ramp_side_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_ramp_side_is((int)luaL_checkinteger(L, 2), lua_toboolean(L, 3));
    return 0;
}

/*  AND THE PAIRS whose tapers face each other, with the tiles between
 *  them for the rule to divide. */
static int api_world_ramp_shares(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, net_ramp_shares());
    return 1;
}

static int api_world_ramp_share(lua_State *L)
{
    float gap;
    int   cap;
    if (!world_of(L) || !net_ramp_share_at((int)luaL_checkinteger(L, 2), &gap, &cap))
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, (double)gap), lua_setfield(L, -2, "gap");
    lua_pushinteger(L, cap), lua_setfield(L, -2, "cap");
    return 1;
}

static int api_world_ramp_share_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_ramp_share_is((int)luaL_checkinteger(L, 2),
                      lua_isnumber(L, 3) ? (int)lua_tointeger(L, 3) : -1);
    return 0;
}

/*  AND WHERE EACH RAMP'S DESCENT RUNS along its deck. */
static int api_world_ramp_spans(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, net_ramp_spans());
    return 1;
}

static int api_world_ramp_span(lua_State *L)
{
    float at;
    int   len, leaves, sgn;
    if (!world_of(L) || !net_ramp_span_at((int)luaL_checkinteger(L, 2), &at, &len, &leaves, &sgn))
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, (double)at), lua_setfield(L, -2, "at");
    lua_pushinteger(L, len), lua_setfield(L, -2, "len");
    lua_pushboolean(L, leaves), lua_setfield(L, -2, "leaves");
    lua_pushinteger(L, sgn), lua_setfield(L, -2, "sgn");
    return 1;
}

static int api_world_ramp_span_is(lua_State *L)
{
    int i = (int)luaL_checkinteger(L, 2);
    if (!world_of(L))
        return 0;
    if (!lua_istable(L, 3))
    {
        net_ramp_span_is(i, 0, 0.0f, 0.0f, 0.0f, 0.0f);
        return 0;
    }
    lua_pushvalue(L, 3);
    net_ramp_span_is(i, 1, api_field_num(L, "top", 0.0f), api_field_num(L, "foot", 0.0f),
                     api_field_num(L, "total", 0.0f), api_field_num(L, "along", 0.0f));
    lua_pop(L, 1);
    return 0;
}

static int api_world_ramp_fork_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_ramp_fork_is((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                     (int)luaL_checkinteger(L, 4), (int)luaL_optinteger(L, 5, 0));
    return 0;
}

static int api_world_band_start_is(lua_State *L)
{
    const char *v = lua_tostring(L, 4);
    if (!world_of(L))
        return 0;
    net_band_start_is((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                      !v ? 0 : strcmp(v, "forward") == 0 ? 1
                               : strcmp(v, "backward") == 0 ? -1
                                                            : 0);
    return 0;
}

/*  THE POINTS THE DECK'S FIT IS GIVEN, as the rule picks them from the
 *  band the walk read: the straight cells', a lone block's corner, and
 *  nothing of a staircase. */
static int api_world_hiway_chain(lua_State *L)
{
    StairFan *st;
    if (!world_of(L) || (st = net_hw_chain()) == NULL)
        return 0;
    lua_pushstring(L, "stair");
    api_object_push(L, "stair", st);
    return 2;
}

static int api_world_hiway_band_chained(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    build_hiway_band_chained();
    return 0;
}

static int api_world_hw_fits(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, net_hw_fits());
    return 1;
}

static int api_world_hw_fit(lua_State *L)
{
    void *h;
    if (!world_of(L))
        return 0;
    net_hw_fit_begin((int)luaL_checkinteger(L, 2));
    if ((h = path_handle()) == NULL)
        return 0;
    api_object_push(L, "path", h);
    return 1;
}

static int api_world_hw_fit_done(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_hw_fit_done((int)luaL_checkinteger(L, 2));
    return 0;
}

static int api_world_hw_fit_choice(lua_State *L)
{
    static const char *const KEY[3] = {"corners", "tight", "nodes"};
    const int               *free_, *held;
    const char              *fam;
    int                      k;
    if (!world_of(L) || (fam = net_hw_fit_choice(&free_, &held)) == NULL)
        return 0;
    lua_newtable(L);
    lua_pushstring(L, fam), lua_setfield(L, -2, "family");
    lua_newtable(L);
    for (k = 0; k < 3; ++k)
        lua_pushinteger(L, free_[k]), lua_setfield(L, -2, KEY[k]);
    lua_setfield(L, -2, "free");
    lua_newtable(L);
    for (k = 0; k < 3; ++k)
        lua_pushinteger(L, held[k]), lua_setfield(L, -2, KEY[k]);
    lua_setfield(L, -2, "held");
    return 1;
}

static int api_world_hw_fit_choice_is(lua_State *L)
{
    const char *v = lua_tostring(L, 2);
    if (!world_of(L))
        return 0;
    net_hw_fit_choice_is(!v || strcmp(v, "free") == 0);
    return 0;
}

static int api_world_hiway_band_fitted(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_hiway_band_fitted();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

static int api_world_hiway_band_done(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_hiway_band_done();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

/*  THE RAMPS' STRIPS, one at a time, in the order the pass read them. */
/*  THE RAMPS, one at a time: each is read up to the join the rule slides
 *  along the road's lane, and taken up again once it has answered. */
static int api_world_ramp_next(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    lua_pushboolean(L, build_ramp_next());
    return 1;
}

static int api_world_ramp_slide(lua_State *L)
{
    SlideFan *s;
    if (!world_of(L) || (s = net_ramp_slide()) == NULL)
        return 0;
    api_object_push(L, "slide", s);
    return 1;
}

static int api_world_ramp_done(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_ramp_done();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

static int api_world_ramp_lofts(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, build_ramp_lofts());
    return 1;
}

static int api_world_ramp_loft(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_ramp_loft((int)luaL_checkinteger(L, 2));
    if (rc != 0)
        w->rc = rc;
    return 0;
}

static int api_world_highway_ramps(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_highway_ramps(w->m, w->c, w->l, w->mask_bit, !w->rotated);
    if (rc != 0)
        w->rc = rc;
    lua_pushboolean(L, rc == 0);
    return 1;
}

/*  ACROSS A CROSSING: the lanes' open ends as the highway pass left
 *  them, for the rule that carries each of them on into the lane facing
 *  it.  `w:highway_links` then joins the bands' ends into the roads they
 *  become. */
/*  THE LANE OVERLAY'S WIRES, laid last: the hairline over every lane the
 *  passes gathered, each entered again under the shape it belongs to. */
static int api_world_wires(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, net_wires());
    return 1;
}

static int api_world_wire(lua_State *L)
{
    LaneFan *f;
    if (!world_of(L) || (f = net_wire_at((int)luaL_checkinteger(L, 2))) == NULL)
        return 0;
    api_object_push(L, "lane", f);
    return 1;
}

static int api_world_wire_done(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_wire_done((int)luaL_checkinteger(L, 2));
    return 0;
}

static int api_world_lane_cross(lua_State *L)
{
    WorldFan *w = world_of(L);
    XLaneFan *x;
    if (!w || (x = lane_cross_ask(w->m, w->c, w->mask_bit)) == NULL)
        return 0;
    api_object_push(L, "cross", x);
    return 1;
}

static int api_world_highway_links(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_highway_links(w->m, w->c, w->mask_bit);
    if (rc != 0)
        w->rc = rc;
    lua_pushboolean(L, rc == 0);
    return 1;
}

static const luaL_Reg WORLD[] = {
    {"info",     api_world_info    },
    {"wanted",   api_world_wanted  },
    {"tile",     api_world_tile    },
    {"zone",     api_world_zone    },
    {"shape",    api_world_shape   },
    {"power",    api_world_power   },
    {"junctions",    api_world_junctions   },
    {"junction",     api_world_junction    },
    {"junction_ring", api_world_junction_ring},
    {"trims",        api_world_trims       },
    {"shelf",    api_world_shelf   },
    {"controls", api_world_controls},
    {"control",  api_world_control },
    {"control_is", api_world_control_is},
    {"xwalk",    api_world_xwalk   },
    {"xwalk_deep", api_world_xwalk_deep},
    {"crossing",       api_world_crossing      },
    {"crossing_frame", api_world_crossing_frame},
    {"crossing_panel", api_world_crossing_panel},
    {"crossing_approach", api_world_crossing_approach},
    {"crossing_mark", api_world_crossing_mark},
    {"crossing_approaches", api_world_crossing_approaches},
    {"footways", api_world_footways},
    {"footway",  api_world_footway },
    {"emitted",  api_world_emitted },
    {"lanes",    api_world_lanes   },
    {"lane_runs", api_world_lane_runs},
    {"lane_run",  api_world_lane_run },
    {"lane_run_is", api_world_lane_run_is},
    {"traffic_runs", api_world_traffic_runs},
    {"traffic_run",  api_world_traffic_run },
    {"traffic_run_is", api_world_traffic_run_is},
    {"networks", api_world_networks},
    {"seg_classes", api_world_seg_classes},
    {"fits",     api_world_fits    },
    {"fit",      api_world_fit     },
    {"fit_done", api_world_fit_done},
    {"fit_choice_is", api_world_fit_choice_is},
    {"seg_class",  api_world_seg_class },
    {"seg_class_is", api_world_seg_class_is},
    {"networks_draw", api_world_networks_draw},
    {"net_families", api_world_net_families},
    {"junction_boxes", api_world_junction_boxes},
    {"junction_box", api_world_junction_box},
    {"junction_box_done", api_world_junction_box_done},
    {"box_lofts", api_world_box_lofts},
    {"box_loft",  api_world_box_loft },
    {"segments", api_world_segments},
    {"segment",  api_world_segment },
    {"segment_done", api_world_segment_done},
    {"networks_drawn", api_world_networks_drawn},
    {"highways", api_world_highways},
    {"loft_taper", api_world_loft_taper},
    {"loft_profile", api_world_loft_profile},
    {"loft_dropped", api_world_loft_dropped},
    {"loft_works", api_world_loft_works},
    {"loft_record", api_world_loft_record},
    {"loft_record_is", api_world_loft_record_is},
    {"loft_furniture", api_world_loft_furniture},
    {"loft_furniture_is", api_world_loft_furniture_is},
    {"loft_recorded", api_world_loft_recorded},
    {"curves",   api_world_curves  },
    {"strip",    api_world_strip   },
    {"strip_done", api_world_strip_done},
    {"hiway_band", api_world_hiway_band},
    {"band_start_is", api_world_band_start_is},
    {"ramp_fork_is", api_world_ramp_fork_is},
    {"orients",  api_world_orients },
    {"orient",   api_world_orient  },
    {"ramp_sides", api_world_ramp_sides},
    {"ramp_side",  api_world_ramp_side },
    {"ramp_side_is", api_world_ramp_side_is},
    {"ramp_shares", api_world_ramp_shares},
    {"ramp_share",  api_world_ramp_share },
    {"ramp_share_is", api_world_ramp_share_is},
    {"ramp_spans", api_world_ramp_spans},
    {"ramp_span",  api_world_ramp_span },
    {"ramp_span_is", api_world_ramp_span_is},
    {"hiway_chain", api_world_hiway_chain},
    {"hiway_band_chained", api_world_hiway_band_chained},
    {"hw_fits",  api_world_hw_fits },
    {"hw_fit",   api_world_hw_fit  },
    {"hw_fit_done", api_world_hw_fit_done},
    {"hw_fit_choice", api_world_hw_fit_choice},
    {"hw_fit_choice_is", api_world_hw_fit_choice_is},
    {"hiway_band_fitted", api_world_hiway_band_fitted},
    {"hiway_band_done", api_world_hiway_band_done},
    {"highway_ramps", api_world_highway_ramps},
    {"ramp_next", api_world_ramp_next},
    {"ramp_slide", api_world_ramp_slide},
    {"ramp_done", api_world_ramp_done},
    {"ramp_lofts", api_world_ramp_lofts},
    {"ramp_loft",  api_world_ramp_loft },
    {"wires",    api_world_wires   },
    {"wire",     api_world_wire    },
    {"wire_done", api_world_wire_done},
    {"lane_cross", api_world_lane_cross},
    {"highway_links", api_world_highway_links},
    {NULL,       NULL              }
};

static const luaL_Reg JUNCTION[] = {
    {"info",    api_junction_info   },
    {"count",   api_junction_count  },
    {"at",      api_junction_at     },
    {"surface", api_junction_surface},
    {"tri",     api_junction_tri    },
    {"quad",    api_junction_quad   },
    {NULL,      NULL     }
};

/*  ---- a footway ----------------------------------------------------- */

/*  The tile's own methods live in api_tile.c, which knows the ground;
 *  the handle is the same one every other kind uses. */
TileFan *api_tile_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "tile") == 0 ? (TileFan *)o->rec : NULL;
}

const luaL_Reg *api_tile_methods(int *n);

static WalkFan *walk_of(lua_State *L)
{
    return (WalkFan *)rec_of(L, "footway");
}

/*  What the band IS: which kind it is, how many stations the network
 *  holds for it, how wide it is, where it sits in the stack, whether it
 *  lies on the ground, and -- for a crossing -- the depth it asked for,
 *  which the material paints the stop line against. */
static int api_footway_info(lua_State *L)
{
    WalkFan        *f = walk_of(L);
    const WalkPath *w;
    if (!f)
        return 0;
    w = (const WalkPath *)f->w;
    lua_newtable(L);
    lua_pushstring(L, w->kind == WALK_SIDE ? "side" : w->kind == WALK_CORNER ? "corner"
                                                  : w->kind == WALK_CROSS  ? "crossing"
                                                                           : "cap"),
        lua_setfield(L, -2, "band");
    lua_pushinteger(L, w->nst), lua_setfield(L, -2, "n");
    lua_pushnumber(L, w->w), lua_setfield(L, -2, "width");
    lua_pushnumber(L, w->ask), lua_setfield(L, -2, "asked");
    lua_pushnumber(L, w->order), lua_setfield(L, -2, "order");
    lua_pushboolean(L, w->drape), lua_setfield(L, -2, "drape");
    lua_pushinteger(L, w->col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, w->row), lua_setfield(L, -2, "row");
    return 1;
}

static int api_footway_count(lua_State *L)
{
    WalkFan *f = walk_of(L);
    if (!f)
        return 0;
    lua_pushinteger(L, ((const WalkPath *)f->w)->nst);
    return 1;
}

/*  One cross-section: the band's outer edge, its inner one and the
 *  height the network recorded there. */
static int api_footway_at(lua_State *L)
{
    WalkFan      *f = walk_of(L);
    const WalkSt *st;
    int           i = (int)luaL_checkinteger(L, 2);
    if (!f || i < 0 || i >= ((const WalkPath *)f->w)->nst)
        return 0;
    st = &((const WalkSt *)f->st)[i];
    lua_pushnumber(L, st->outer.x);
    lua_pushnumber(L, st->outer.y);
    lua_pushnumber(L, st->inner.x);
    lua_pushnumber(L, st->inner.y);
    lua_pushnumber(L, st->z);
    return 5;
}

/*  One quad of the band, cut on the tile folds like every other. */
static int api_footway_quad(lua_State *L)
{
    WalkFan *f = walk_of(L);
    float    a0[2], a1[2], b0[2], b1[2];
    if (!f)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    a0[0] = (float)luaL_checknumber(L, 2), a0[1] = (float)luaL_checknumber(L, 3);
    a1[0] = (float)luaL_checknumber(L, 4), a1[1] = (float)luaL_checknumber(L, 5);
    b0[0] = (float)luaL_checknumber(L, 6), b0[1] = (float)luaL_checknumber(L, 7);
    b1[0] = (float)luaL_checknumber(L, 8), b1[1] = (float)luaL_checknumber(L, 9);
    lua_pushboolean(L, strip_quad_z((RMesh *)f->m, (const RCity *)f->c, f->mask_bit,
                                    (float)luaL_checknumber(L, 16),
                                    a0, a1, b0, b1,
                                    (float)luaL_checknumber(L, 10), (float)luaL_checknumber(L, 11),
                                    (float)luaL_checknumber(L, 12), (float)luaL_checknumber(L, 13),
                                    (float)luaL_checknumber(L, 14), (float)luaL_checknumber(L, 15),
                                    (float)luaL_checknumber(L, 17)) == 0);
    return 1;
}

/*  The two ends of the band, and the ground under each: what the
 *  network joins it to its neighbours by, and what a join is drawn
 *  between when the band itself has no line down it. */
static int api_footway_ends(lua_State *L)
{
    WalkFan        *f = walk_of(L);
    const WalkPath *w;
    if (!f)
        return 0;
    w = (const WalkPath *)f->w;
    lua_pushnumber(L, w->end[0].x), lua_pushnumber(L, w->end[0].y);
    lua_pushnumber(L, w->end[1].x), lua_pushnumber(L, w->end[1].y);
    lua_pushnumber(L, surface_at_world((const RCity *)f->c, f->mask_bit, w->end[0].x, w->end[0].y));
    lua_pushnumber(L, surface_at_world((const RCity *)f->c, f->mask_bit, w->end[1].x, w->end[1].y));
    return 6;
}

/*  A hairline of the network: a thin quad in the vehicle material, whose
 *  across carries the paint, a hair above the ground so it reads over
 *  whatever it crosses. */
static int api_footway_wire(lua_State *L)
{
    WalkFan *f = walk_of(L);
    float    q0[2], q1[2], r0[2], r1[2];
    float    ax, ay, bx, by, za, zb, paint, wide, over, slot;
    float    dx, dy, dl, px, py;
    int32_t  tc, tr;
    if (!f)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    ax = (float)luaL_checknumber(L, 2), ay = (float)luaL_checknumber(L, 3);
    bx = (float)luaL_checknumber(L, 4), by = (float)luaL_checknumber(L, 5);
    za = (float)luaL_checknumber(L, 6), zb = (float)luaL_checknumber(L, 7);
    paint = (float)luaL_checknumber(L, 8);
    wide = (float)luaL_checknumber(L, 9);
    over = (float)luaL_checknumber(L, 10);
    slot = (float)luaL_checknumber(L, 11);
    dx = bx - ax, dy = by - ay, dl = sqrtf(dx * dx + dy * dy);
    if (dl < 1e-5f)
    {
        lua_pushboolean(L, 1);
        return 1;
    }
    px = -dy / dl * wide, py = dx / dl * wide;
    q0[0] = ax - px, q0[1] = ay - py;
    q1[0] = ax + px, q1[1] = ay + py;
    r0[0] = bx - px, r0[1] = by - py;
    r1[0] = bx + px, r1[1] = by + py;
    tc = (int32_t)floorf(ax), tr = (int32_t)floorf(ay);
    lua_pushboolean(L, strip_quad_z((RMesh *)f->m, (const RCity *)f->c, f->mask_bit,
                                    tile_order((const RCity *)f->c, tc, tr, f->mask_bit) + slot,
                                    q0, q1, r0, r1, za + over, zb + over,
                                    paint, paint, 0.0f, 0.0f, MAT_VEHICLE) == 0);
    return 1;
}

static const luaL_Reg FOOTWAY[] = {
    {"ends",  api_footway_ends },
    {"wire",  api_footway_wire },
    {"info",  api_footway_info },
    {"count", api_footway_count},
    {"at",    api_footway_at   },
    {"quad",  api_footway_quad },
    {NULL,    NULL   }
};

/*  ---- a lane -------------------------------------------------------- */

static LaneFan *lane_of(lua_State *L)
{
    return (LaneFan *)rec_of(L, "lane");
}

static const Field LANE_FIELDS[] = {
    {"n",     FLD_INT,  offsetof(LaneFan, np)},
    {"lift",  FLD_NUM,  offsetof(LaneFan, lift)},
    {"paint", FLD_NUM,  offsetof(LaneFan, paint)},
    {"band",  FLD_INT,  offsetof(LaneFan, band)},
    {"ramp",  FLD_BOOL, offsetof(LaneFan, ramp)},
    {"off",   FLD_BOOL, offsetof(LaneFan, off)},
    {"step",  FLD_NUM,  offsetof(LaneFan, step)},
    {NULL, FLD_NUM, 0}
};

static int api_lane_info(lua_State *L)
{
    return api_fields(L, lane_of(L), LANE_FIELDS);
}

/*  One piece of the fitted line: how long it runs and whether it turns. */
static int api_lane_piece(lua_State *L)
{
    LaneFan *f = lane_of(L);
    int      k = (int)luaL_checkinteger(L, 2);
    if (!f || k < 0 || k >= f->np)
        return 0;
    lua_pushnumber(L, f->pc[k].len);
    lua_pushboolean(L, f->pc[k].arc);
    return 2;
}

/*  Where that piece is `at` tiles along it. */
static int api_lane_at(lua_State *L)
{
    LaneFan *f = lane_of(L);
    int      k = (int)luaL_checkinteger(L, 2);
    V2       p, d;
    if (!f || k < 0 || k >= f->np)
        return 0;
    piece_at(&f->pc[k], (float)luaL_checknumber(L, 3), &p, &d);
    lua_pushnumber(L, p.x), lua_pushnumber(L, p.y);
    lua_pushnumber(L, d.x), lua_pushnumber(L, d.y);
    return 4;
}

/*  The surface the hairline floats over: the deck's own where the line
 *  belongs to a band, the ground elsewhere. */
static int api_lane_height(lua_State *L)
{
    LaneFan *f = lane_of(L);
    V2       p;
    if (!f)
        return 0;
    p.x = (float)luaL_checknumber(L, 2), p.y = (float)luaL_checknumber(L, 3);
    if (f->ramp)
    {
        /*  A ramp's lane climbs to the deck: the ease is the same one
         *  the ramp's own strip is lifted by, so the two agree. */
        lua_pushnumber(L, surface_at_world(f->c, f->mask_bit, p.x, p.y) +
                              HIWAY_LIFT * hiway_lane_ease((float)luaL_checknumber(L, 4)));
        return 1;
    }
    lua_pushnumber(L, f->band > 0 ? deck_z_near(f->c, f->mask_bit, f->band, p)
                                  : surface_at_world(f->c, f->mask_bit, p.x, p.y));
    return 1;
}

static int api_lane_order(lua_State *L)
{
    LaneFan *f = lane_of(L);
    int32_t  tc, tr;
    if (!f)
        return 0;
    tc = (int32_t)floorf((float)luaL_checknumber(L, 2));
    tr = (int32_t)floorf((float)luaL_checknumber(L, 3));
    if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
        return 0;
    lua_pushnumber(L, tile_order(f->c, tc, tr, f->mask_bit));
    return 1;
}

/*  A hairline between two points in the air. */
static int api_lane_wire(lua_State *L)
{
    LaneFan *f = lane_of(L);
    if (!f)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, put_wire_paint(f->m, f->c, f->mask_bit, (float)luaL_checknumber(L, 8),
                                      (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                                      (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5),
                                      (float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7),
                                      0.0f, (float)luaL_checknumber(L, 9)) == 0);
    return 1;
}

static const luaL_Reg LANE[] = {
    {"info",   api_lane_info  },
    {"piece",  api_lane_piece },
    {"at",     api_lane_at    },
    {"height", api_lane_height},
    {"order",  api_lane_order },
    {"wire",   api_lane_wire  },
    {NULL,     NULL    }
};

/*  ---- a level crossing's panel -------------------------------------- */

static XingFan *xing_of(lua_State *L)
{
    return (XingFan *)rec_of(L, "panel");
}

static const Field PANEL_FIELDS[] = {
    {"order", FLD_NUM,  offsetof(XingFan, order)},
    {"lift",  FLD_NUM,  offsetof(XingFan, lift)},
    {"slot",  FLD_NUM,  offsetof(XingFan, slot)},
    {NULL, FLD_NUM, 0}
};

static int api_panel_info(lua_State *L)
{
    return api_fields(L, xing_of(L), PANEL_FIELDS);
}

/*  Corner k of the panel, 1 to 4, and the ground under it. */
static int api_panel_at(lua_State *L)
{
    XingFan *f = xing_of(L);
    int      k = (int)luaL_checkinteger(L, 2);
    if (!f || k < 0 || k > 3)
        return 0;
    lua_pushnumber(L, f->q[k][0]);
    lua_pushnumber(L, f->q[k][1]);
    lua_pushnumber(L, f->ground[k]);
    return 3;
}

/*  The panel itself: one quad over the road it interrupts, its heights
 *  taken from the higher of each end's two corners so that on a tile
 *  tilting across the road it stands on the ground and not under it. */
static int api_panel_quad(lua_State *L)
{
    XingFan *f = xing_of(L);
    float    a0[2], a1[2], b0[2], b1[2];
    if (!f)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    a0[0] = f->q[0][0], a0[1] = f->q[0][1];
    a1[0] = f->q[1][0], a1[1] = f->q[1][1];
    b0[0] = f->q[2][0], b0[1] = f->q[2][1];
    b1[0] = f->q[3][0], b1[1] = f->q[3][1];
    lua_pushboolean(L, strip_quad_z(f->m, f->c, f->mask_bit, (float)luaL_checknumber(L, 4),
                                    a0, a1, b0, b1,
                                    (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                                    -1.0f, 1.0f, 0.0f, 1.0f, MAT_XPANEL) == 0);
    return 1;
}

static const luaL_Reg XING[] = {
    {"info", api_panel_info},
    {"at",   api_panel_at  },
    {"quad", api_panel_quad},
    {NULL,   NULL  }
};

/*  ---- a junction's outline ------------------------------------------ */

static OutlineFan *outline_of(lua_State *L)
{
    return (OutlineFan *)rec_of(L, "outline");
}

/*  The numbers the junction is sized by: its middle, the half width of
 *  the band that runs through it, how far out a corner may stand, how
 *  much the family has been let out from the width it was tuned at, and
 *  the cap on how far an arm may be cut back for the junction's sake. */
static const Field OUTLINE_FIELDS[] = {
    {"col",   FLD_INT,  offsetof(OutlineFan, col)},
    {"row",   FLD_INT,  offsetof(OutlineFan, row)},
    {"x",     FLD_NUM,  offsetof(OutlineFan, cx)},
    {"y",     FLD_NUM,  offsetof(OutlineFan, cy)},
    {"half",  FLD_NUM,  offsetof(OutlineFan, w)},
    {"far",   FLD_NUM,  offsetof(OutlineFan, far)},
    {"grow",  FLD_NUM,  offsetof(OutlineFan, gro)},
    {"cap",   FLD_NUM,  offsetof(OutlineFan, cap)},
    {"curbs", FLD_BOOL, offsetof(OutlineFan, curbs)},
    {"n",     FLD_INT,  offsetof(OutlineFan, na)},
    {NULL, FLD_NUM, 0}
};

static int api_outline_info(lua_State *L)
{
    return api_fields(L, outline_of(L), OUTLINE_FIELDS);
}

/*  One arm: where its own path starts, the way it leaves, the angle that
 *  makes, and which of the tile's four edges it belongs to. */
static int api_outline_arm(lua_State *L)
{
    OutlineFan *o = outline_of(L);
    int         i = (int)luaL_checkinteger(L, 2);
    if (!o || i < 0 || i >= o->na)
        return 0;
    lua_pushnumber(L, o->arm[i].ox), lua_pushnumber(L, o->arm[i].oy);
    lua_pushnumber(L, o->arm[i].dx), lua_pushnumber(L, o->arm[i].dy);
    lua_pushnumber(L, o->arm[i].ang);
    lua_pushinteger(L, o->arm[i].e);
    return 6;
}

/*  One point of the ring, with the arm's own tag where it is a mouth.
 *  The same point twice is no edge, so a repeat is dropped. */
static int api_outline_point(lua_State *L)
{
    OutlineFan *o = outline_of(L);
    float       x, y;
    int         tag;
    if (!o)
        return 0;
    x   = (float)luaL_checknumber(L, 2);
    y   = (float)luaL_checknumber(L, 3);
    tag = (int)luaL_optinteger(L, 4, 0);
    if (o->n >= o->max)
        return 0;
    if (o->n > 0 && fabsf(o->out[o->n - 1].x - x) < 1e-5f && fabsf(o->out[o->n - 1].y - y) < 1e-5f)
        return 0;
    if (o->mouth)
        o->mouth[o->n] = (uint8_t)tag;
    o->out[o->n].x = x, o->out[o->n].y = y;
    ++o->n;
    return 0;
}

/*  How far along the arm on that edge its strip starts. */
static int api_outline_trim(lua_State *L)
{
    OutlineFan *o = outline_of(L);
    int         e = (int)luaL_checkinteger(L, 2);
    if (!o || e < 0 || e > 3)
        return 0;
    o->trim[e] = (float)luaL_checknumber(L, 3);
    return 0;
}

/*  A corner pulled in for standing further out than a junction reaches,
 *  which the outline report counts. */
static int api_outline_clamped(lua_State *L)
{
    if (outline_of(L))
        junction_outline_clamped((float)luaL_checknumber(L, 2));
    return 0;
}

/*  The arms as the script ordered them, given back so the box's mouths
 *  and the ring agree about which arm is which. */
static int api_outline_order(lua_State *L)
{
    OutlineFan *o = outline_of(L);
    int         i, n;
    if (!o || !lua_istable(L, 2))
        return 0;
    n = (int)luaL_checkinteger(L, 3);
    if (n > 4)
        n = 4;
    for (i = 0; i < n; ++i)
    {
        lua_rawgeti(L, 2, i);
        o->arm[i].ox  = api_field_num(L, "ox", 0.0f);
        o->arm[i].oy  = api_field_num(L, "oy", 0.0f);
        o->arm[i].dx  = api_field_num(L, "dx", 0.0f);
        o->arm[i].dy  = api_field_num(L, "dy", 0.0f);
        o->arm[i].ang = api_field_num(L, "ang", 0.0f);
        o->arm[i].e   = (int)api_field_num(L, "e", 0.0f);
        lua_pop(L, 1);
    }
    o->na = n;
    return 0;
}

/*  How many points the ring has come to, and how far the last of them
 *  is from a point the walk is thinking of adding. */
static int api_outline_count(lua_State *L)
{
    OutlineFan *o = outline_of(L);
    if (!o)
        return 0;
    lua_pushinteger(L, o->n);
    return 1;
}

static int api_outline_back(lua_State *L)
{
    OutlineFan *o = outline_of(L);
    float       x, y;
    if (!o || o->n < 1)
        return 0;
    x = (float)luaL_checknumber(L, 2) - o->out[o->n - 1].x;
    y = (float)luaL_checknumber(L, 3) - o->out[o->n - 1].y;
    lua_pushnumber(L, sqrtf(x * x + y * y));
    return 1;
}

/*  A closed ring: the last point must not repeat the first. */
static int api_outline_close(lua_State *L)
{
    OutlineFan *o = outline_of(L);
    if (!o)
        return 0;
    while (o->n > 1 && fabsf(o->out[o->n - 1].x - o->out[0].x) < 1e-5f &&
           fabsf(o->out[o->n - 1].y - o->out[0].y) < 1e-5f)
        --o->n;
    return 0;
}

static const luaL_Reg OUTLINE[] = {
    {"order", api_outline_order},
    {"count", api_outline_count},
    {"back",  api_outline_back },
    {"close", api_outline_close},
    {"info",    api_outline_info   },
    {"arm",     api_outline_arm    },
    {"point",   api_outline_point  },
    {"trim",    api_outline_trim   },
    {"clamped", api_outline_clamped},
    {NULL,      NULL     }
};

/*  ---- a junction's band --------------------------------------------- */

static BandFan *band_of(lua_State *L)
{
    return (BandFan *)rec_of(L, "band");
}

static int api_band_info(lua_State *L)
{
    BandFan *b = band_of(L);
    if (!b)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, b->np), lua_setfield(L, -2, "n");
    lua_pushnumber(L, b->lw), lua_setfield(L, -2, "width");
    lua_pushinteger(L, b->jb->col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, b->jb->row), lua_setfield(L, -2, "row");
    return 1;
}

/*  Point i of the ring. */
static int api_band_at(lua_State *L)
{
    BandFan *b = band_of(L);
    int      i = (int)luaL_checkinteger(L, 2);
    if (!b || i < 0 || i >= b->np)
        return 0;
    lua_pushnumber(L, b->poly[i].x), lua_pushnumber(L, b->poly[i].y);
    return 2;
}

/*  One arm's cut across the junction: whether the arm is there at all,
 *  and its two corners, which the ring's own mouth edge spans. */
static int api_band_arm(lua_State *L)
{
    BandFan *b = band_of(L);
    int      e = (int)luaL_checkinteger(L, 2);
    if (!b || e < 0 || e > 3 || !b->arms)
        return 0;
    lua_pushboolean(L, b->arms[e].have);
    lua_pushnumber(L, b->arms[e].a.x), lua_pushnumber(L, b->arms[e].a.y);
    lua_pushnumber(L, b->arms[e].b.x), lua_pushnumber(L, b->arms[e].b.y);
    return 5;
}

/*  What edge i carries: a band or not, the way it faces into the
 *  junction, and the arm it is the mouth of, or -1 for none. */
static int api_band_edge(lua_State *L)
{
    BandFan *b = band_of(L);
    int      i = (int)luaL_checkinteger(L, 2);
    if (!b || i < 0 || i >= b->np)
        return 0;
    b->band[i]     = (uint8_t)lua_toboolean(L, 3);
    b->nrm[i].x    = (float)luaL_checknumber(L, 4);
    b->nrm[i].y    = (float)luaL_checknumber(L, 5);
    b->edge_arm[i] = (int8_t)luaL_checkinteger(L, 6);
    if (b->mitre0)
    {
        b->mitre0[i].x = (float)luaL_optnumber(L, 7, 0.0);
        b->mitre0[i].y = (float)luaL_optnumber(L, 8, 0.0);
        b->mitre1[i].x = (float)luaL_optnumber(L, 9, 0.0);
        b->mitre1[i].y = (float)luaL_optnumber(L, 10, 0.0);
    }
    return 0;
}

/*  One point of the ring moved in by the footway's width. */
static int api_band_inset(lua_State *L)
{
    BandFan *b = band_of(L);
    float    x, y;
    if (!b || !b->inset)
        return 0;
    x = (float)luaL_checknumber(L, 2);
    y = (float)luaL_checknumber(L, 3);
    if (b->inset_n >= b->inset_max)
        return 0;
    if (b->inset_n > 0 && fabsf(b->inset[b->inset_n - 1].x - x) < 1e-5f &&
        fabsf(b->inset[b->inset_n - 1].y - y) < 1e-5f)
        return 0;
    b->inset[b->inset_n].x = x, b->inset[b->inset_n].y = y;
    ++b->inset_n;
    return 0;
}

/*  A closed ring: the last point must not repeat the first. */
static int api_band_close(lua_State *L)
{
    BandFan *b = band_of(L);
    if (!b || !b->inset)
        return 0;
    while (b->inset_n > 1 && fabsf(b->inset[b->inset_n - 1].x - b->inset[0].x) < 1e-5f &&
           fabsf(b->inset[b->inset_n - 1].y - b->inset[0].y) < 1e-5f)
        --b->inset_n;
    return 0;
}

static const luaL_Reg BAND[] = {
    {"info",  api_band_info },
    {"at",    api_band_at   },
    {"arm",   api_band_arm  },
    {"edge",  api_band_edge },
    {"inset", api_band_inset},
    {"close", api_band_close},
    {NULL,    NULL   }
};

/*  ---- a fitted path ------------------------------------------------- */

static FitFan *fit_of(lua_State *L)
{
    return (FitFan *)rec_of(L, "fit");
}

static const Field FIT_FIELDS[] = {
    {"n",        FLD_INT,  offsetof(FitFan, n)},
    {"reserve0", FLD_NUM,  offsetof(FitFan, res0)},
    {"reserve1", FLD_NUM,  offsetof(FitFan, res1)},
    {"rmax",     FLD_NUM,  offsetof(FitFan, rmax)},
    {"rmin",     FLD_NUM,  offsetof(FitFan, rmin)},
    {"band",     FLD_NUM,  offsetof(FitFan, band)},
    {"share",    FLD_NUM,  offsetof(FitFan, share)},
    {"trim_cap", FLD_NUM,  offsetof(FitFan, trim_cap)},
    {NULL, FLD_NUM, 0}
};

static int api_fit_info(lua_State *L)
{
    return api_fields(L, fit_of(L), FIT_FIELDS);
}

/*  Vertex k: where it is, and the tangent length it was built with -- a
 *  biarc's own, or -1 for a vertex the search placed. */
static int api_fit_at(lua_State *L)
{
    FitFan *q = fit_of(L);
    int     k = (int)luaL_checkinteger(L, 2);
    if (!q || k < 0 || k >= q->n)
        return 0;
    lua_pushnumber(L, q->out[k].x), lua_pushnumber(L, q->out[k].y);
    lua_pushnumber(L, q->fixed[k]);
    return 3;
}

/*  Drop vertex k: the ones behind it move up. */
static int api_fit_drop(lua_State *L)
{
    FitFan *q = fit_of(L);
    int     k = (int)luaL_checkinteger(L, 2), j;
    if (!q || k < 0 || k >= q->n)
        return 0;
    for (j = k; j + 1 < q->n; ++j)
    {
        q->out[j]   = q->out[j + 1];
        q->fixed[j] = q->fixed[j + 1];
    }
    --q->n;
    lua_pushinteger(L, q->n);
    return 1;
}

/*  The radius and tangent limit a corner is given. */
static int api_fit_corner(lua_State *L)
{
    FitFan *q = fit_of(L);
    int     k = (int)luaL_checkinteger(L, 2);
    if (!q || k < 0 || k >= q->n)
        return 0;
    q->rad[k]  = (float)luaL_checknumber(L, 3);
    q->tlim[k] = (float)luaL_checknumber(L, 4);
    return 0;
}

/*  The corridor sweep: the largest radius a fillet at this corner may
 *  have and still hold inside the band, and whether it came out under
 *  the minimum.  The sampling is the pipeline's; what to do with the
 *  answer is not. */
static int api_fit_sweep(lua_State *L)
{
    FitFan   *q = fit_of(L);
    SweepFan *s;
    V2        a, b, c;
    if (!q)
        return 0;
    a.x = (float)luaL_checknumber(L, 2), a.y = (float)luaL_checknumber(L, 3);
    b.x = (float)luaL_checknumber(L, 4), b.y = (float)luaL_checknumber(L, 5);
    c.x = (float)luaL_checknumber(L, 6), c.y = (float)luaL_checknumber(L, 7);
    s = path_sweep_ask(q->mark, a, b, c, (float)luaL_checknumber(L, 8), q->rmax, q->rmin, q->band);
    api_object_push(L, "sweep", s);
    return 1;
}

/*  And what the search came to. */
static int api_swept(lua_State *L)
{
    int tight = 0;
    float r   = path_sweep_take(&tight);
    (void)L;
    lua_pushnumber(L, (double)r);
    lua_pushboolean(L, tight);
    return 2;
}

/*  How much tangent a corner demands: the fit's own arithmetic, asked
 *  for here rather than copied. */
static int api_fit_demand(lua_State *L)
{
    if (!fit_of(L))
        return 0;
    lua_pushnumber(L, path_fit_demand((V2){(float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3)},
                                      (V2){(float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5)},
                                      (V2){(float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7)}));
    return 1;
}

/*  The tangent an arc of the smallest legal radius needs at a corner, or
 *  nothing where the corner has no turn to it. */
static int api_fit_need(lua_State *L)
{
    FitFan *q = fit_of(L);
    float   need;
    if (!q)
        return 0;
    need = path_fit_need((V2){(float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3)},
                         (V2){(float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5)},
                         (V2){(float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7)},
                         q->rmin);
    if (need < 0.0f)
        return 0;
    lua_pushnumber(L, need);
    return 1;
}

/*  What the fit came to, for its own report: a corner with no arc, one
 *  swept to a radius, or one held under the minimum. */
static int api_fit_tally(lua_State *L)
{
    const char *what = luaL_checkstring(L, 2);
    if (fit_of(L))
        path_fit_count(what);
    return 0;
}

static const luaL_Reg FIT[] = {
    {"info",       api_fit_info      },
    {"at",         api_fit_at        },
    {"drop",       api_fit_drop      },
    {"corner",     api_fit_corner    },
    {"sweep",      api_fit_sweep     },
    {"swept",      api_swept         },
    {"demand",     api_fit_demand    },
    {"need",       api_fit_need      },
    {"tally",      api_fit_tally     },
    {NULL,         NULL        }
};

/*  ---- the runs of a path ---------------------------------------------- */

static RunFan *runs_of(lua_State *L)
{
    return (RunFan *)rec_of(L, "runs");
}

/*  The chain: how many steps it has, whether a span may be any chord
 *  that holds, and whether either end is a junction's mouth. */
static const Field RUNS_FIELDS[] = {
    {"ns",   FLD_INT,  offsetof(RunFan, ns)},
    {"free", FLD_BOOL, offsetof(RunFan, free_lines)},
    {"ex0",  FLD_BOOL, offsetof(RunFan, ex0)},
    {"ex1",  FLD_BOOL, offsetof(RunFan, ex1)},
    {"band", FLD_NUM,  offsetof(RunFan, band)},
    {NULL, FLD_NUM, 0}
};

static int api_runs_info(lua_State *L)
{
    return api_fields(L, runs_of(L), RUNS_FIELDS);
}

/*  Step k, named by the first step it is the same as, and whether it
 *  goes anywhere at all.  Two steps are the same step exactly when they
 *  answer the same name. */
static int api_runs_step(lua_State *L)
{
    RunFan *x = runs_of(L);
    int     k = (int)luaL_checkinteger(L, 2);
    if (!x || k < 0 || k >= x->ns)
        return 0;
    lua_pushinteger(L, x->code[k]);
    lua_pushboolean(L, x->moves[k]);
    return 2;
}

/*  Do two steps meet at a right angle? */
static int api_runs_perp(lua_State *L)
{
    RunFan *x = runs_of(L);
    int     a = (int)luaL_checkinteger(L, 2), b = (int)luaL_checkinteger(L, 3);
    if (!x || a < 0 || a >= x->ns || b < 0 || b >= x->ns)
        return 0;
    lua_pushboolean(L, path_run_perp(x, a, b));
    return 1;
}

/*  The slope reaching from step i, as the script read it out of the
 *  pattern: its length, its period, and which steps are its majority and
 *  its minority. */
static int api_runs_slope(lua_State *L)
{
    RunFan *x = runs_of(L);
    int     i = (int)luaL_checkinteger(L, 2);
    if (!x || i < 0 || i >= x->ns)
        return 0;
    path_run_slope(x, i, (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4),
                   (int)luaL_checkinteger(L, 5), (int)luaL_checkinteger(L, 6));
    return 0;
}

/*  How far either side of the span's chord its covered points lie.
 *  Nothing where the span has no covered point, or no length. */
static int api_runs_spread(lua_State *L)
{
    RunFan *x = runs_of(L);
    int     i = (int)luaL_checkinteger(L, 2), j = (int)luaL_checkinteger(L, 3);
    float   lo, hi;
    if (!x || i < 0 || j > x->ns || i >= j || !path_run_spread(x, i, j, &lo, &hi))
        return 0;
    lua_pushnumber(L, lo);
    lua_pushnumber(L, hi);
    return 2;
}

/*  The chord the script settles on for the span, moved across by this
 *  much: the candidate `try` then judges. */
static int api_runs_chord(lua_State *L)
{
    RunFan *x = runs_of(L);
    int     i = (int)luaL_checkinteger(L, 2), j = (int)luaL_checkinteger(L, 3);
    if (x && i >= 0 && j <= x->ns && i < j)
        path_run_chord(x, i, j, (float)luaL_checknumber(L, 4));
    return 0;
}

/*  A span refused before it was ever sampled, for --path-dump. */
static int api_runs_note(lua_State *L)
{
    RunFan *x = runs_of(L);
    if (x)
        path_run_note(x, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3), luaL_checkstring(L, 4));
    return 0;
}

/*  Would the span i..j stand as this kind of run?  A slope is 1 and a
 *  free line 2; a straight is its own steps and needs no asking. */
static int api_runs_try(lua_State *L)
{
    RunFan *x = runs_of(L);
    int     i = (int)luaL_checkinteger(L, 2), j = (int)luaL_checkinteger(L, 3);
    if (!x || i < 0 || j > x->ns || i >= j)
        return 0;
    lua_pushboolean(L, path_run_try(x, i, j, (int)luaL_checkinteger(L, 4)));
    return 1;
}

/*  The span just tried wins the prefix ending at j. */
static int api_runs_keep(lua_State *L)
{
    RunFan *x = runs_of(L);
    int     j = (int)luaL_checkinteger(L, 2);
    if (x && j >= 0 && j <= x->ns)
        path_run_keep(x, j);
    return 0;
}

/*  A run of the answer, and the order they are put in once they are all
 *  named. */
static int api_runs_emit(lua_State *L)
{
    RunFan *x = runs_of(L);
    int     i = (int)luaL_checkinteger(L, 2), j = (int)luaL_checkinteger(L, 3);
    if (x && i >= 0 && j > i && j <= x->ns)
        path_run_emit(x, i, j, (int)luaL_checkinteger(L, 4));
    return 0;
}

static int api_runs_order(lua_State *L)
{
    RunFan *x = runs_of(L);
    if (x)
        path_run_order(x);
    return 0;
}

static const luaL_Reg RUNS[] = {
    {"info",  api_runs_info },
    {"step",  api_runs_step },
    {"perp",  api_runs_perp },
    {"slope", api_runs_slope},
    {"spread", api_runs_spread},
    {"chord",  api_runs_chord },
    {"note",   api_runs_note  },
    {"try",    api_runs_try   },
    {"keep",  api_runs_keep },
    {"emit",  api_runs_emit },
    {"order", api_runs_order},
    {NULL,    NULL   }
};

/*  ---- the chain of lines a path's runs make ---------------------------- */

static ChainFan *chain_of(lua_State *L)
{
    return (ChainFan *)rec_of(L, "chain");
}

/*  Which end a call names: the start of the chain, or its goal. */
static int chain_end(lua_State *L, int idx)
{
    return strcmp(luaL_checkstring(L, idx), "goal") == 0;
}

static const Field CHAIN_FIELDS[] = {
    {"nr",  FLD_INT,  offsetof(ChainFan, nr)},
    {"ex0", FLD_BOOL, offsetof(ChainFan, ex0)},
    {"ex1", FLD_BOOL, offsetof(ChainFan, ex1)},
    {NULL, FLD_NUM, 0}
};

static int api_chain_info(lua_State *L)
{
    return api_fields(L, chain_of(L), CHAIN_FIELDS);
}

/*  Run i: what sort of line it is, and whether it reaches the chain's
 *  first point or its last. */
static int api_chain_run(lua_State *L)
{
    ChainFan *c = chain_of(L);
    int       i = (int)luaL_checkinteger(L, 2);
    if (!c || i < 0 || i >= c->nr)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, c->runs[i].kind), lua_setfield(L, -2, "kind");
    lua_pushboolean(L, c->runs[i].ta == 0), lua_setfield(L, -2, "first");
    lua_pushboolean(L, c->runs[i].tb == c->nt - 1), lua_setfield(L, -2, "last");
    return 1;
}

/*  Pull an end onto the angle of the run beside it. */
static int api_chain_aim(lua_State *L)
{
    ChainFan *c = chain_of(L);
    if (c && c->nr > 0)
        path_chain_aim(c, chain_end(L, 2));
    return 0;
}

/*  Does that end already lie on the line of the run beside it? */
static int api_chain_on_line(lua_State *L)
{
    ChainFan *c = chain_of(L);
    if (!c || c->nr <= 0)
        return 0;
    lua_pushboolean(L, path_chain_on_line(c, chain_end(L, 2)));
    return 1;
}

/*  The chain, a line at a time: an end's own line, or a run. */
static int api_chain_add_end(lua_State *L)
{
    ChainFan *c = chain_of(L);
    if (c)
        path_chain_end(c, chain_end(L, 2));
    return 0;
}

static int api_chain_add(lua_State *L)
{
    ChainFan *c = chain_of(L);
    int       i = (int)luaL_checkinteger(L, 2);
    if (c && i >= 0 && i < c->nr)
        path_chain_run(c, i);
    return 0;
}

static const luaL_Reg CHAIN[] = {
    {"info",    api_chain_info   },
    {"run",     api_chain_run    },
    {"aim",     api_chain_aim    },
    {"on_line", api_chain_on_line},
    {"add_end", api_chain_add_end},
    {"add",     api_chain_add    },
    {NULL,      NULL     }
};

/*  ---- the crossing of two lines ---------------------------------------- */

static JoinFan *meet_of(lua_State *L)
{
    return (JoinFan *)rec_of(L, "meet");
}

/*  How the crossing sits: how far past the line behind it is, how far
 *  short of the line ahead, how much line there is either side, and
 *  whether a free line is involved or the chain's own end. */
static int api_meet_info(lua_State *L)
{
    JoinFan *j = meet_of(L);
    if (!j)
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, j->ahead), lua_setfield(L, -2, "ahead");
    lua_pushnumber(L, j->behind), lua_setfield(L, -2, "behind");
    lua_pushnumber(L, j->reach), lua_setfield(L, -2, "reach");
    lua_pushnumber(L, j->reach_on), lua_setfield(L, -2, "reach_on");
    lua_pushnumber(L, j->len_in), lua_setfield(L, -2, "len_in");
    lua_pushnumber(L, j->len_out), lua_setfield(L, -2, "len_out");
    lua_pushnumber(L, j->fixed_prev), lua_setfield(L, -2, "fixed_prev");
    lua_pushnumber(L, j->res0), lua_setfield(L, -2, "reserve0");
    lua_pushnumber(L, j->res1), lua_setfield(L, -2, "reserve1");
    if (j->need >= 0.0f)
        lua_pushnumber(L, j->need), lua_setfield(L, -2, "need");
    lua_pushnumber(L, j->share), lua_setfield(L, -2, "share");
    lua_pushnumber(L, j->trim_cap), lua_setfield(L, -2, "trim_cap");
    lua_pushboolean(L, j->free_line), lua_setfield(L, -2, "free");
    lua_pushboolean(L, j->first), lua_setfield(L, -2, "first");
    lua_pushboolean(L, j->last), lua_setfield(L, -2, "last");
    return 1;
}

/*  Does the leg's extension to the crossing hold on the corridor? */
static int api_meet_holds(lua_State *L)
{
    JoinFan *j = meet_of(L);
    if (!j)
        return 0;
    lua_pushboolean(L, path_join_holds(j, strcmp(luaL_checkstring(L, 2), "ahead") == 0));
    return 1;
}

/*  Would the corner leave a covered cell of the gap bare? */
static int api_meet_covers(lua_State *L)
{
    JoinFan *j = meet_of(L);
    if (!j)
        return 0;
    lua_pushboolean(L, path_join_covers(j));
    return 1;
}

/*  The radius the corridor allows at the crossing, given the tangent it
 *  may take, and whether the straights that reach an arc of that radius
 *  hold. */
static int api_meet_arc(lua_State *L)
{
    JoinFan *j = meet_of(L);
    if (!j)
        return 0;
    api_object_push(L, "sweep", path_join_arc(j, (float)luaL_checknumber(L, 2)));
    return 1;
}

static int api_meet_legs(lua_State *L)
{
    JoinFan *j = meet_of(L);
    if (!j)
        return 0;
    lua_pushboolean(L, path_join_legs(j, (float)luaL_checknumber(L, 2)));
    return 1;
}

/*  Put the vertex at the crossing. */
static int api_meet_place(lua_State *L)
{
    JoinFan *j = meet_of(L);
    if (j)
        path_join_place(j);
    return 0;
}

static const luaL_Reg MEET[] = {
    {"info",   api_meet_info  },
    {"holds",  api_meet_holds },
    {"covers", api_meet_covers},
    {"arc",    api_meet_arc   },
    {"swept",  api_swept      },
    {"legs",   api_meet_legs  },
    {"place",  api_meet_place },
    {NULL,     NULL    }
};

/*  ---- the biarc between two parallel lines ----------------------------- */

static BridgeFan *bridge_of(lua_State *L)
{
    return (BridgeFan *)rec_of(L, "bridge");
}

/*  The line either side of the pair, what the vertex behind was built
 *  with, how far the two lines' ends lie apart, and which of the two
 *  lines is the chain's own end. */
static const Field BRIDGE_FIELDS[] = {
    {"len_in",     FLD_NUM,  offsetof(BridgeFan, len_in)},
    {"len_out",    FLD_NUM,  offsetof(BridgeFan, len_out)},
    {"fixed_prev", FLD_NUM,  offsetof(BridgeFan, fixed_prev)},
    {"gap",        FLD_NUM,  offsetof(BridgeFan, gap)},
    {"reserve0",   FLD_NUM,  offsetof(BridgeFan, res0)},
    {"reserve1",   FLD_NUM,  offsetof(BridgeFan, res1)},
    {"head",       FLD_BOOL, offsetof(BridgeFan, head)},
    {"tail",       FLD_BOOL, offsetof(BridgeFan, tail)},
    {"first",      FLD_BOOL, offsetof(BridgeFan, first)},
    {"last",       FLD_BOOL, offsetof(BridgeFan, last)},
    {"share",      FLD_NUM,  offsetof(BridgeFan, share)},
    {"band",       FLD_NUM,  offsetof(BridgeFan, band)},
    {"margin",     FLD_NUM,  offsetof(BridgeFan, margin)},
    {"padded",     FLD_BOOL, offsetof(BridgeFan, padded)},
    {NULL, FLD_NUM, 0}
};

static int api_bridge_info(lua_State *L)
{
    return api_fields(L, bridge_of(L), BRIDGE_FIELDS);
}

/*  One placing of the S, its tangent points drawn back this far along
 *  each line: the biarc between them, and the edge either side of it.
 *  Nothing where there is no biarc at all. */
static int api_bridge_solve(lua_State *L)
{
    BridgeFan *b = bridge_of(L);
    if (!b || !path_bridge_solve(b, (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                                 (int)luaL_checkinteger(L, 4), (int)luaL_checkinteger(L, 5)))
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, b->d), lua_setfield(L, -2, "tangent");
    lua_pushnumber(L, b->solve_in), lua_setfield(L, -2, "len_in");
    lua_pushnumber(L, b->solve_out), lua_setfield(L, -2, "len_out");
    return 1;
}

/*  A placing the outer edges have no room for. */
static int api_bridge_refuse(lua_State *L)
{
    BridgeFan *b = bridge_of(L);
    if (b)
        path_bridge_refuse(b, strcmp(luaL_checkstring(L, 2), "out") == 0,
                           (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4));
    return 0;
}

/*  Does the band hold the placing, and at what radius? */
static int api_bridge_holds(lua_State *L)
{
    BridgeFan *b = bridge_of(L);
    if (!b)
        return 0;
    lua_pushnumber(L, path_bridge_holds(b));
    return 1;
}

/*  The placing as --sweep-probe reports it. */
static int api_bridge_result(lua_State *L)
{
    BridgeFan *b = bridge_of(L);
    if (b)
        path_bridge_result(b, (float)luaL_checknumber(L, 2));
    return 0;
}

static int api_bridge_keep(lua_State *L)
{
    BridgeFan *b = bridge_of(L);
    if (b)
        path_bridge_keep(b);
    return 0;
}

static int api_bridge_place(lua_State *L)
{
    BridgeFan *b = bridge_of(L);
    if (b)
        path_bridge_place(b);
    return 0;
}

/*  The pair as --sweep-probe reports it. */
static int api_bridge_note(lua_State *L)
{
    BridgeFan *b = bridge_of(L);
    if (b)
        path_bridge_note(b, (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3));
    return 0;
}

static const luaL_Reg BRIDGE[] = {
    {"info",  api_bridge_info },
    {"solve",  api_bridge_solve },
    {"refuse", api_bridge_refuse},
    {"holds",  api_bridge_holds },
    {"result", api_bridge_result},
    {"keep",  api_bridge_keep },
    {"place", api_bridge_place},
    {"note",  api_bridge_note },
    {NULL,    NULL    }
};

/*  ---- the walk between two lines that neither cross nor bridge ---------- */

static StepFan *step_of(lua_State *L)
{
    return (StepFan *)rec_of(L, "step");
}

static int st_side(lua_State *L, int idx)
{
    return strcmp(luaL_checkstring(L, idx), "ahead") == 0;
}

/*  How many of the chain's own points lie between the two lines, whether
 *  each line is a run rather than the chain's own end line, and how wide
 *  the band is. */
static const Field STEP_FIELDS[] = {
    {"gap",  FLD_INT,  offsetof(StepFan, gap)},
    {"head", FLD_BOOL, offsetof(StepFan, head)},
    {"tail", FLD_BOOL, offsetof(StepFan, tail)},
    {"half", FLD_NUM,  offsetof(StepFan, hw)},
    {NULL, FLD_NUM, 0}
};

static int api_step_info(lua_State *L)
{
    return api_fields(L, step_of(L), STEP_FIELDS);
}

/*  Does the gap point beside that end already lie in line with it? */
static int api_step_inline(lua_State *L)
{
    StepFan *w = step_of(L);
    if (!w || w->gap < 1)
        return 0;
    lua_pushboolean(L, path_step_inline(w, st_side(L, 2)));
    return 1;
}

/*  Is the gap a sideways step, and can it be drawn as one diagonal? */
static int api_step_jog(lua_State *L)
{
    StepFan *w = step_of(L);
    if (!w || w->gap < 1)
        return 0;
    lua_pushboolean(L, path_step_jog(w));
    return 1;
}

static int api_step_diagonal(lua_State *L)
{
    StepFan *w = step_of(L);
    if (!w || w->gap < 1)
        return 0;
    lua_pushboolean(L, path_step_diagonal(w, (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3)));
    return 1;
}

/*  A line's own end as a vertex, and a point of the gap. */
static int api_step_place(lua_State *L)
{
    StepFan *w = step_of(L);
    if (w)
        path_step_end(w, st_side(L, 2));
    return 0;
}

static int api_step_point(lua_State *L)
{
    StepFan *w = step_of(L);
    int      t = (int)luaL_checkinteger(L, 2);
    if (w && t >= 0 && t < w->gap)
        path_step_point(w, t);
    return 0;
}

static const luaL_Reg STEP[] = {
    {"info",     api_step_info    },
    {"inline",   api_step_inline  },
    {"jog",      api_step_jog     },
    {"diagonal", api_step_diagonal},
    {"place",    api_step_place   },
    {"point",    api_step_point   },
    {NULL,       NULL       }
};

/*  ---- the fillet swept into one corner --------------------------------- */

static SweepFan *sweep_of(lua_State *L)
{
    return (SweepFan *)rec_of(L, "sweep");
}

/*  The corner: whether there is anything to sweep at all, the tangent it
 *  has been given, the tangent of half its turn -- which is what turns a
 *  tangent length into a radius -- and the widths it is held between. */
static const Field SWEEP_FIELDS[] = {
    {"straight", FLD_BOOL, offsetof(SweepFan, straight)},
    {"tan_half", FLD_NUM,  offsetof(SweepFan, tan_half)},
    {"tangent",  FLD_NUM,  offsetof(SweepFan, tlim)},
    {"rmax",     FLD_NUM,  offsetof(SweepFan, rmax)},
    {"rmin",     FLD_NUM,  offsetof(SweepFan, rmin)},
    {"half",     FLD_NUM,  offsetof(SweepFan, hw)},
    {"margin",   FLD_NUM,  offsetof(SweepFan, margin)},
    {"padded",   FLD_BOOL, offsetof(SweepFan, padded)},
    {NULL, FLD_NUM, 0}
};

static int api_sweep_info(lua_State *L)
{
    return api_fields(L, sweep_of(L), SWEEP_FIELDS);
}

/*  Does an arc of this radius hold on the corridor and leave nothing
 *  bare? */
static int api_sweep_holds(lua_State *L)
{
    SweepFan *s = sweep_of(L);
    if (!s)
        return 0;
    lua_pushboolean(L, path_sweep_holds(s, (float)luaL_checknumber(L, 2)));
    return 1;
}

/*  The radius the corner is given, and whether it came out under the
 *  minimum. */
static int api_sweep_answer(lua_State *L)
{
    SweepFan *s = sweep_of(L);
    if (s)
        path_sweep_answer(s, (float)luaL_checknumber(L, 2), lua_toboolean(L, 3));
    return 0;
}

static const luaL_Reg SWEEP[] = {
    {"info",   api_sweep_info  },
    {"holds",  api_sweep_holds },
    {"answer", api_sweep_answer},
    {NULL,     NULL     }
};

/*  ---- a fitted path cut into pieces ------------------------------------ */

static PieceFan *piece_of(lua_State *L)
{
    return (PieceFan *)rec_of(L, "pieces");
}

static const Field PIECES_FIELDS[] = {
    {"n", FLD_INT,  offsetof(PieceFan, n)},
    {NULL, FLD_NUM, 0}
};

static int api_pieces_info(lua_State *L)
{
    return api_fields(L, piece_of(L), PIECES_FIELDS);
}

/*  Corner i: the radius and tangent it was given, the tangent of half
 *  its turn, how much of the incoming edge the piece already laid has
 *  left, and how long the edge it leaves along is.  Nothing at all for a
 *  vertex with no turn to it. */
static int api_pieces_corner(lua_State *L)
{
    PieceFan *p = piece_of(L);
    int       i = (int)luaL_checkinteger(L, 2);
    if (!p || i < 1 || i + 1 >= p->n || !path_piece_corner(p, i))
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, p->rad[i]), lua_setfield(L, -2, "radius");
    lua_pushnumber(L, p->tlim[i]), lua_setfield(L, -2, "tangent");
    lua_pushnumber(L, p->tan_half), lua_setfield(L, -2, "tan_half");
    lua_pushnumber(L, v2len((V2){p->q[i].x - p->cur.x, p->q[i].y - p->cur.y})), lua_setfield(L, -2, "room");
    lua_pushnumber(L, v2len((V2){p->q[i + 1].x - p->q[i].x, p->q[i + 1].y - p->q[i].y})), lua_setfield(L, -2, "leaving");
    return 1;
}

/*  The corner left as a corner, a fillet swept into it, and the run out
 *  to the far end. */
static int api_pieces_straight(lua_State *L)
{
    PieceFan *p = piece_of(L);
    int       i = (int)luaL_checkinteger(L, 2);
    if (p && i >= 0 && i < p->n)
        path_piece_straight(p, i);
    return 0;
}

static int api_pieces_arc(lua_State *L)
{
    PieceFan *p = piece_of(L);
    int       i = (int)luaL_checkinteger(L, 2);
    if (p && i >= 1 && i + 1 < p->n)
        path_piece_arc(p, i, (float)luaL_checknumber(L, 3));
    return 0;
}

static int api_pieces_tail(lua_State *L)
{
    PieceFan *p = piece_of(L);
    if (p)
        path_piece_tail(p);
    return 0;
}

static const luaL_Reg PIECES[] = {
    {"info",     api_pieces_info    },
    {"corner",   api_pieces_corner  },
    {"straight", api_pieces_straight},
    {"arc",      api_pieces_arc     },
    {"tail",     api_pieces_tail    },
    {NULL,       NULL       }
};

/*  ---- a highway band's chain of fit points ------------------------------ */

static StairFan *stair_of(lua_State *L)
{
    return (StairFan *)rec_of(L, "stair");
}

/*  How many cells the band has, and how many straight ones a stair may
 *  step over between two of its blocks. */
static const Field STAIR_FIELDS[] = {
    {"n",   FLD_INT,  offsetof(StairFan, n)},
    {"gap", FLD_INT,  offsetof(StairFan, gap)},
    {NULL, FLD_NUM, 0}
};

static int api_stair_info(lua_State *L)
{
    return api_fields(L, stair_of(L), STAIR_FIELDS);
}

/*  Cell i: whether it is a curve block, which way the chain turns there,
 *  and whether an on-ramp pins it. */
static int api_stair_block(lua_State *L)
{
    StairFan *s = stair_of(L);
    int       i = (int)luaL_checkinteger(L, 2);
    if (!s || i < 0 || i >= s->n)
        return 0;
    lua_pushboolean(L, s->block[i]);
    return 1;
}

static int api_stair_turn(lua_State *L)
{
    StairFan *s = stair_of(L);
    int       i = (int)luaL_checkinteger(L, 2);
    if (!s || i < 0 || i >= s->n)
        return 0;
    lua_pushinteger(L, hiway_stair_turn(s, i));
    return 1;
}

static int api_stair_pinned(lua_State *L)
{
    StairFan *s = stair_of(L);
    int       i = (int)luaL_checkinteger(L, 2);
    if (!s || i < 0 || i >= s->n)
        return 0;
    lua_pushboolean(L, hiway_stair_pinned(s, i));
    return 1;
}

/*  A cell as a point of the chain, and a run of cells as the one point
 *  at their centre. */
static int api_stair_point(lua_State *L)
{
    StairFan *s = stair_of(L);
    int       i = (int)luaL_checkinteger(L, 2);
    if (s && i >= 0 && i < s->n)
        hiway_stair_point(s, i);
    return 0;
}

static int api_stair_centre(lua_State *L)
{
    StairFan *s = stair_of(L);
    int       i = (int)luaL_checkinteger(L, 2), j = (int)luaL_checkinteger(L, 3);
    if (s && i >= 0 && j >= i && j < s->n)
        hiway_stair_centre(s, i, j);
    return 0;
}

static const luaL_Reg STAIR[] = {
    {"info",   api_stair_info  },
    {"block",  api_stair_block },
    {"turn",   api_stair_turn  },
    {"pinned", api_stair_pinned},
    {"point",  api_stair_point },
    {"centre", api_stair_centre},
    {NULL,     NULL     }
};

/*  ---- a highway strip's elevation --------------------------------------- */

static ProfFan *prof_of(lua_State *L)
{
    return (ProfFan *)rec_of(L, "profile");
}

/*  What sort of strip it is and the numbers it is shaped by. */
static const Field PROFILE_FIELDS[] = {
    {"n",          FLD_INT,  offsetof(ProfFan, n)},
    {"total",      FLD_NUM,  offsetof(ProfFan, total)},
    {"ramp",       FLD_BOOL, offsetof(ProfFan, ramp)},
    {"lane_piece", FLD_BOOL, offsetof(ProfFan, lane_piece)},
    {"lane_off",   FLD_BOOL, offsetof(ProfFan, lane_off)},
    {"flat",       FLD_BOOL, offsetof(ProfFan, flat)},
    {"deck_above", FLD_NUM,  offsetof(ProfFan, z0)},
    {"taper0",     FLD_NUM,  offsetof(ProfFan, ramp0)},
    {"taper1",     FLD_NUM,  offsetof(ProfFan, ramp1)},
    {"grade",      FLD_NUM,  offsetof(ProfFan, grade)},
    {"stiff",      FLD_NUM,  offsetof(ProfFan, stiff)},
    {"lift",       FLD_NUM,  offsetof(ProfFan, lift)},
    {NULL, FLD_NUM, 0}
};

static int api_profile_info(lua_State *L)
{
    return api_fields(L, prof_of(L), PROFILE_FIELDS);
}

/*  Station i: how far along it is, and the ground under it. */
static int api_profile_at(lua_State *L)
{
    ProfFan *p = prof_of(L);
    int      i = (int)luaL_checkinteger(L, 2);
    float    s_at, z, ground;
    if (!p || i < 0 || i >= p->n)
        return 0;
    hiway_prof_at(p, i, &s_at, &z, &ground);
    lua_pushnumber(L, s_at);
    lua_pushnumber(L, ground);
    return 2;
}

/*  The height the station is given. */
static int api_profile_set(lua_State *L)
{
    ProfFan *p = prof_of(L);
    int      i = (int)luaL_checkinteger(L, 2);
    if (p && i >= 0 && i < p->n)
        hiway_prof_set(p, i, (float)luaL_checknumber(L, 3));
    return 0;
}

/*  The curve a lane drop's descent follows. */
static int api_profile_ease(lua_State *L)
{
    if (!prof_of(L))
        return 0;
    lua_pushnumber(L, hiway_lane_ease((float)luaL_checknumber(L, 2)));
    return 1;
}

static const luaL_Reg PROFILE[] = {
    {"info", api_profile_info},
    {"at",   api_profile_at  },
    {"set",  api_profile_set },
    {"ease", api_profile_ease},
    {NULL,   NULL   }
};

/*  ---- a ramp's join, slid ----------------------------------------------- */

static SlideFan *slide_of(lua_State *L)
{
    return (SlideFan *)rec_of(L, "slide");
}

/*  How far along the deck the descent may start before it reaches the
 *  lane line -- nothing where the two never meet -- and how far along the
 *  road the join may slide. */
static int api_slide_info(lua_State *L)
{
    SlideFan *s = slide_of(L);
    if (!s)
        return 0;
    lua_newtable(L);
    if (!s->parallel)
        lua_pushnumber(L, s->reach), lua_setfield(L, -2, "reach");
    lua_pushnumber(L, s->merge), lua_setfield(L, -2, "merge");
    lua_pushnumber(L, s->taper), lua_setfield(L, -2, "taper");
    return 1;
}

/*  One placing.  The radius its route holds, or nothing and the reason:
 *  "off" the lane it aimed at, or "unroutable". */
static int api_slide_route(lua_State *L)
{
    SlideFan   *s = slide_of(L);
    const char *why;
    float       r = 0.0f;
    if (!s)
        return 0;
    why = hiway_slide_route(s, (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), &r);
    if (why)
    {
        lua_pushnil(L);
        lua_pushstring(L, why);
        return 2;
    }
    lua_pushnumber(L, r);
    return 1;
}

/*  Does the placing leave by the ramp tile's own road edge? */
static int api_slide_exits(lua_State *L)
{
    SlideFan *s = slide_of(L);
    if (!s)
        return 0;
    lua_pushboolean(L, hiway_slide_exits(s));
    return 1;
}

/*  The placing kept, with the slack its taper is given past the edge. */
static int api_slide_keep(lua_State *L)
{
    SlideFan *s = slide_of(L);
    if (s)
        hiway_slide_keep(s, (float)luaL_checknumber(L, 2));
    return 0;
}

/*  Why no placing was found, for --lane-dump. */
static int api_slide_note(lua_State *L)
{
    SlideFan *s = slide_of(L);
    if (s)
        hiway_slide_note(s, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                         (int)luaL_checkinteger(L, 4), (int)luaL_checkinteger(L, 5));
    return 0;
}

static const luaL_Reg SLIDE[] = {
    {"info",  api_slide_info },
    {"route", api_slide_route},
    {"exits", api_slide_exits},
    {"keep",  api_slide_keep },
    {"note",  api_slide_note },
    {NULL,    NULL    }
};

/*  ---- the lane a ramp drops from a deck --------------------------------- */

static DropFan *drop_of(lua_State *L)
{
    return (DropFan *)rec_of(L, "drop");
}

static const Field DROP_FIELDS[] = {
    {"n",      FLD_INT,  offsetof(DropFan, n)},
    {"ramps",  FLD_INT,  offsetof(DropFan, nramps)},
    {"reach",  FLD_NUM,  offsetof(DropFan, reach)},
    {"narrow", FLD_NUM,  offsetof(DropFan, narrow)},
    {NULL, FLD_NUM, 0}
};

static int api_drop_info(lua_State *L)
{
    return api_fields(L, drop_of(L), DROP_FIELDS);
}

/*  Station i: how far along the deck it is, where it stands, which way
 *  it heads. */
static int api_drop_station(lua_State *L)
{
    DropFan *d = drop_of(L);
    float    at;
    V2       pos, dir;
    if (!d || !hiway_drop_station(d, (int)luaL_checkinteger(L, 2), &at, &pos, &dir))
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, at), lua_setfield(L, -2, "at");
    lua_pushnumber(L, pos.x), lua_setfield(L, -2, "x");
    lua_pushnumber(L, pos.y), lua_setfield(L, -2, "y");
    lua_pushnumber(L, dir.x), lua_setfield(L, -2, "dx");
    lua_pushnumber(L, dir.y), lua_setfield(L, -2, "dy");
    return 1;
}

/*  Ramp r: the point on the centreline it drops from, its own tile's
 *  centre, the way it runs, its taper's length in tiles, and whether it
 *  leaves the deck or joins it. */
static int api_drop_ramp(lua_State *L)
{
    DropFan *d = drop_of(L);
    V2       c0, tile, along;
    int      len, off;
    if (!d || !hiway_drop_ramp(d, (int)luaL_checkinteger(L, 2), &c0, &tile, &along, &len, &off))
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, c0.x), lua_setfield(L, -2, "x");
    lua_pushnumber(L, c0.y), lua_setfield(L, -2, "y");
    lua_pushnumber(L, tile.x), lua_setfield(L, -2, "tx");
    lua_pushnumber(L, tile.y), lua_setfield(L, -2, "ty");
    lua_pushnumber(L, along.x), lua_setfield(L, -2, "ax");
    lua_pushnumber(L, along.y), lua_setfield(L, -2, "ay");
    lua_pushinteger(L, len), lua_setfield(L, -2, "len");
    lua_pushboolean(L, off), lua_setfield(L, -2, "leaves");
    return 1;
}

/*  Every station its full width; the width one is left with on a side;
 *  and the gore, where the ramp's own sliver sits beside the deck. */
static int api_drop_clear(lua_State *L)
{
    DropFan *d = drop_of(L);
    if (d)
        hiway_drop_clear(d);
    return 0;
}

static int api_drop_width(lua_State *L)
{
    DropFan *d = drop_of(L);
    if (d)
        hiway_drop_width(d, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                         (float)luaL_checknumber(L, 4));
    return 0;
}

static int api_drop_gore(lua_State *L)
{
    DropFan *d = drop_of(L);
    if (d)
        hiway_drop_gore(d, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3));
    return 0;
}

static const luaL_Reg DROP[] = {
    {"info",    api_drop_info   },
    {"station", api_drop_station},
    {"ramp",    api_drop_ramp   },
    {"clear",   api_drop_clear  },
    {"width",   api_drop_width  },
    {"gore",    api_drop_gore   },
    {NULL,      NULL      }
};

/*  ---- a strip's elevation over the ground ------------------------------- */

static GroundFan *ground_of(lua_State *L)
{
    return (GroundFan *)rec_of(L, "ground");
}

/*  How many stations, how long the strip is, and whether each end
 *  reaches a node it must be pinned to. */
static const Field GROUND_FIELDS[] = {
    {"n",            FLD_INT,  offsetof(GroundFan, n)},
    {"total",        FLD_NUM,  offsetof(GroundFan, total)},
    {"pin0",         FLD_BOOL, offsetof(GroundFan, pin0)},
    {"pin1",         FLD_BOOL, offsetof(GroundFan, pin1)},
    {"dead0",        FLD_BOOL, offsetof(GroundFan, dead0)},
    {"dead1",        FLD_BOOL, offsetof(GroundFan, dead1)},
    {"reaches_node", FLD_BOOL, offsetof(GroundFan, pin_node)},
    {NULL, FLD_NUM, 0}
};

static int api_ground_info(lua_State *L)
{
    return api_fields(L, ground_of(L), GROUND_FIELDS);
}

/*  Station i: how far along the strip it is, and the height the ground
 *  has given it so far. */
static int api_ground_at(lua_State *L)
{
    GroundFan *g = ground_of(L);
    float      at, z;
    if (!g || !loft_ground_at(g, (int)luaL_checkinteger(L, 2), &at, &z))
        return 0;
    lua_pushnumber(L, at);
    lua_pushnumber(L, z);
    return 2;
}

/*  The altitude an end's node stands at, and the altitude a level
 *  crossing under station i pins the strip to. */
static int api_ground_node(lua_State *L)
{
    GroundFan *g = ground_of(L);
    float      z;
    int        which = strcmp(luaL_checkstring(L, 2), "goal") == 0;
    if (!g || !loft_ground_node(g, which, &z))
        return 0;
    lua_pushnumber(L, z);
    return 1;
}

static int api_ground_crossing(lua_State *L)
{
    GroundFan *g = ground_of(L);
    float      z;
    if (!g || !loft_ground_crossing(g, (int)luaL_checkinteger(L, 2), &z))
        return 0;
    lua_pushnumber(L, z);
    return 1;
}

/*  The height a station is given. */
static int api_ground_set(lua_State *L)
{
    GroundFan *g = ground_of(L);
    if (g)
        loft_ground_set(g, (int)luaL_checkinteger(L, 2), (float)luaL_checknumber(L, 3));
    return 0;
}

static const luaL_Reg GROUND[] = {
    {"info",     api_ground_info    },
    {"at",       api_ground_at      },
    {"node",     api_ground_node    },
    {"crossing", api_ground_crossing},
    {"set",      api_ground_set     },
    {NULL,       NULL       }
};

/*  ---- an on-ramp's four sides ------------------------------------------- */

static OrientFan *orient_of(lua_State *L)
{
    return (OrientFan *)rec_of(L, "orient");
}

/*  Side k, going north, east, south, west: whether it is a deck tile,
 *  whether that deck runs along this side's own axis, and whether it
 *  carries a road. */
static int api_orient_side(lua_State *L)
{
    OrientFan *o = orient_of(L);
    int        k = (int)luaL_checkinteger(L, 2), deck, axis, road;
    if (!o || k < 0 || k > 3 || !hiway_orient_side(o, k, &deck, &axis, &road))
        return 0;
    lua_pushboolean(L, deck);
    lua_pushboolean(L, axis);
    lua_pushboolean(L, road);
    return 3;
}

/*  The sides the script settled on: which is the deck's, which the
 *  road's, which a deck met end-on, and how many roads there were. */
static int api_orient_answer(lua_State *L)
{
    OrientFan *o = orient_of(L);
    if (!o)
        return 0;
    hiway_orient_answer(o, (int)luaL_checkinteger(L, 2),
                        (int)luaL_optinteger(L, 3, -1), (int)luaL_optinteger(L, 4, -1),
                        (int)luaL_optinteger(L, 5, -1), 0, (int)luaL_optinteger(L, 6, 0));
    return 0;
}

static const luaL_Reg ORIENT[] = {
    {"side",   api_orient_side  },
    {"answer", api_orient_answer},
    {NULL,     NULL     }
};

/*  ---- the shelf's copies of a corner ------------------------------------ */

static ShelfFan *shelf_of(lua_State *L)
{
    return (ShelfFan *)rec_of(L, "shelf");
}

static int api_shelf_info(lua_State *L)
{
    ShelfFan *s = shelf_of(L);
    if (!s)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, R_MAP), lua_setfield(L, -2, "n");
    lua_pushinteger(L, s->nodes), lua_setfield(L, -2, "nodes");
    return 1;
}

/*  The copies of the corner at one grid point: a flat run of the
 *  corridor that wrote each, how far its station was, and the height it
 *  put there.  Nothing where no corridor wrote it. */
static int api_shelf_copies(lua_State *L)
{
    ShelfFan *s = shelf_of(L);
    int       owner[4], i, n;
    float     dist[4], z[4];
    if (!s)
        return 0;
    n = shelf_copies(s, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3), owner, dist, z, 4);
    for (i = 0; i < n; ++i)
    {
        lua_pushinteger(L, owner[i]);
        lua_pushnumber(L, dist[i]);
        lua_pushnumber(L, z[i]);
    }
    return 3 * n;
}

/*  Every copy of that corner written by that corridor takes this level. */
static int api_shelf_set(lua_State *L)
{
    ShelfFan *s = shelf_of(L);
    if (s)
        shelf_set(s, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                  (int)luaL_checkinteger(L, 4), (float)luaL_checknumber(L, 5));
    return 0;
}

/*  The i-th node tile, the heights of every copy round it, and the level
 *  they all take. */
static int api_shelf_node(lua_State *L)
{
    ShelfFan *s = shelf_of(L);
    int32_t   col, row;
    if (!s || !shelf_node_at(s, (int)luaL_checkinteger(L, 2), &col, &row))
        return 0;
    lua_pushinteger(L, col);
    lua_pushinteger(L, row);
    return 2;
}

static int api_shelf_heights(lua_State *L)
{
    ShelfFan *s = shelf_of(L);
    float     z[16];
    int       i, n;
    if (!s)
        return 0;
    n = shelf_node_heights(s, (int32_t)luaL_checkinteger(L, 2), (int32_t)luaL_checkinteger(L, 3), z, 16);
    for (i = 0; i < n; ++i)
        lua_pushnumber(L, z[i]);
    return n;
}

static int api_shelf_node_set(lua_State *L)
{
    ShelfFan *s = shelf_of(L);
    if (s)
        shelf_node_set(s, (int32_t)luaL_checkinteger(L, 2), (int32_t)luaL_checkinteger(L, 3),
                       (float)luaL_checknumber(L, 4));
    return 0;
}

static const luaL_Reg SHELF[] = {
    {"info",     api_shelf_info    },
    {"copies",   api_shelf_copies  },
    {"set",      api_shelf_set     },
    {"node",     api_shelf_node    },
    {"heights",  api_shelf_heights },
    {"node_set", api_shelf_node_set},
    {NULL,       NULL       }
};

/*  ---- a lane carried across a crossing ---------------------------------- */

static XLaneFan *cross_of(lua_State *L)
{
    return (XLaneFan *)rec_of(L, "cross");
}

static const Field CROSS_FIELDS[] = {
    {"n", FLD_INT,  offsetof(XLaneFan, n)},
    {NULL, FLD_NUM, 0}
};

static int api_cross_info(lua_State *L)
{
    return api_fields(L, cross_of(L), CROSS_FIELDS);
}

/*  Is lane i an open end that could carry on across a crossing? */
static int api_cross_end(lua_State *L)
{
    XLaneFan *x = cross_of(L);
    if (!x)
        return 0;
    lua_pushboolean(L, xlane_end(x, (int)luaL_checkinteger(L, 2)));
    return 1;
}

/*  How lane lb's start lies from lane la's end.  Nothing for a lane that
 *  is not a candidate at all. */
static int api_cross_measure(lua_State *L)
{
    XLaneFan *x = cross_of(L);
    float     off, dot, ahead, aside, dist;
    if (!x || !xlane_measure(x, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                             &off, &dot, &ahead, &aside, &dist))
        return 0;
    lua_pushnumber(L, off);
    lua_pushnumber(L, dot);
    lua_pushnumber(L, ahead);
    lua_pushnumber(L, aside);
    lua_pushnumber(L, dist);
    return 5;
}

/*  The two made one lane, or a link drawn between them. */
static int api_cross_merge(lua_State *L)
{
    XLaneFan *x = cross_of(L);
    if (x)
        xlane_merge(x, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3));
    return 0;
}

static int api_cross_link(lua_State *L)
{
    XLaneFan *x = cross_of(L);
    if (!x)
        return 0;
    lua_pushboolean(L, xlane_link(x, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3)));
    return 1;
}

static const luaL_Reg CROSS[] = {
    {"info",    api_cross_info   },
    {"open",    api_cross_end    },
    {"measure", api_cross_measure},
    {"merge",   api_cross_merge  },
    {"link",    api_cross_link   },
    {NULL,      NULL      }
};

/*  ---- the handle ---------------------------------------------------- */

static int obj_index(lua_State *L)
{
    ScriptObj  *o   = (ScriptObj *)luaL_checkudata(L, 1, OBJ_META);
    const char *key = lua_tostring(L, 2);
    if (key && strcmp(key, "kind") == 0)
    {
        lua_pushstring(L, o->kind);
        return 1;
    }
    if (key && strcmp(key, "alive") == 0)
    {
        lua_pushboolean(L, o->gen == s_gen && o->rec != NULL);
        return 1;
    }
    /*  The methods are the KIND'S: a strip and a junction both answer
     *  `info`, and each answers its own. */
    lua_getmetatable(L, 1);
    lua_getfield(L, -1, "methods");
    lua_getfield(L, -1, o->kind);
    if (!lua_istable(L, -1))
        return 1; /* a kind with no methods: every name reads as nothing */
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    return 1;
}

static int obj_tostring(lua_State *L)
{
    ScriptObj *o = (ScriptObj *)luaL_checkudata(L, 1, OBJ_META);
    lua_pushfstring(L, "%s(%s)", o->kind, o->gen == s_gen && o->rec ? "live" : "past");
    return 1;
}

/*  Hand a rule the thing itself.  A thing can be asked more than one
 *  question -- a strip is asked for its surface and for the footways
 *  beside it -- so the rule is named apart from the kind, and the kind
 *  is what says which methods the handle has. */
/*  A handle on a record, on the stack.  The same thing script_rule_object
 *  hands a rule, for the primitives that hand one BACK to a script that
 *  asked. */
void api_object_push(lua_State *L, const char *kind, void *rec)
{
    ScriptObj *o = (ScriptObj *)lua_newuserdata(L, sizeof *o);
    o->kind      = kind;
    o->rec       = rec;
    o->gen       = s_gen;
    luaL_getmetatable(L, OBJ_META);
    lua_setmetatable(L, -2);
}

int script_rule_object(const char *rule, const char *kind, void *rec)
{
    lua_State *L = script_state();
    int        drew = 0;
    if (!L || !api_rule_begin(L, rule))
        return 0;
    api_object_push(L, kind, rec);
    ++s_depth;
    if (!api_rule_call(L, rule, 1, 1))
    {
        --s_depth;
        return 0;
    }
    drew = !lua_isnil(L, -1) && lua_toboolean(L, -1);
    lua_pop(L, 1);
    /*  Every handle made before this point is now past: the record it
     *  points at is the pipeline's and the pipeline has moved on.  A rule
     *  a rule asked for has not moved it on, though -- the outer rule is
     *  still holding its own object and still has work to do with it --
     *  so the stamp only advances when the outermost one is finished. */
    if (--s_depth == 0)
        ++s_gen;
    return drew;
}

/*  THE KINDS.  A kind is its name and its methods, declared once: the
 *  handle's own table is built from this, and rec_of answers by the
 *  same name.  Adding a kind is a row here and a method table, not a
 *  row here and a line in the registration and a helper of its own. */
static const struct
{
    const char       *name;
    const luaL_Reg   *methods;
} KINDS[] = {
    {"strip",    STRIP},
    {"junction", JUNCTION},
    {"footway",  FOOTWAY},
    {"lane",     LANE},
    {"panel",    XING},
    {"outline",  OUTLINE},
    {"band",     BAND},
    {"fit",      FIT},
    {"runs",     RUNS},
    {"chain",    CHAIN},
    {"meet",     MEET},
    {"bridge",   BRIDGE},
    {"step",     STEP},
    {"sweep",    SWEEP},
    {"pieces",   PIECES},
    {"stair",    STAIR},
    {"profile",  PROFILE},
    {"slide",    SLIDE},
    {"drop",     DROP},
    {"ground",   GROUND},
    {"orient",   ORIENT},
    {"shelf",    SHELF},
    {"cross",    CROSS},
    {"path",     PATH},
    {"world",    WORLD},
};

void api_object_open(lua_State *L)
{
    luaL_newmetatable(L, OBJ_META);
    lua_pushcfunction(L, obj_index), lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, obj_tostring), lua_setfield(L, -2, "__tostring");
    /*  One table of methods for each kind of thing, so two kinds may
     *  answer the same question in their own ways. */
    lua_newtable(L);
    {
        size_t k;
        for (k = 0; k < sizeof KINDS / sizeof KINDS[0]; ++k)
            lua_newtable(L), luaL_setfuncs(L, KINDS[k].methods, 0),
                lua_setfield(L, -2, KINDS[k].name);
    }
    {
        int n;
        lua_newtable(L), luaL_setfuncs(L, api_tile_methods(&n), 0), lua_setfield(L, -2, "tile");
    }
    lua_setfield(L, -2, "methods");
    lua_pop(L, 1);
}

