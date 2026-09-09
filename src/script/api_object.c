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

#if SC2K_LUA

#include <string.h>

#include "internal.h"
#include "mesh/internal.h"
#include "net/internal.h"
#include "net/model.h"

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

static Loft *strip_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "strip") == 0 ? (Loft *)o->rec : NULL;
}

/*  ---- what every object answers ------------------------------------ */

/*  The drawn surface under a point: what a band lying on the ground
 *  takes for its height. */
static int m_ground(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    lua_pushnumber(L, surface_at_world(x->c, x->mask_bit, (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3)));
    return 1;
}

/*  Where a point sits in the painter's stack: its tile's own slot.  A
 *  ribbon takes the slot of the tile each of its quads lies on. */
static int m_order(lua_State *L)
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
static int m_info(lua_State *L)
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
static int m_road_class(lua_State *L)
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

static int m_count(lua_State *L)
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
static int m_at(lua_State *L)
{
    Loft *x = strip_of(L);
    int   i = (int)luaL_checkinteger(L, 2) - 1;
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
static int m_width(lua_State *L)
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
static int m_class(lua_State *L)
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
static int m_quad(lua_State *L)
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
static int m_pair(lua_State *L)
{
    Loft    *x = strip_of(L);
    LoftPair p;
    int      i = (int)luaL_checkinteger(L, 2) - 1;
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
static int m_walk_at(lua_State *L)
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
static int m_walk_ends(lua_State *L)
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
static int m_pieces(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    lua_pushinteger(L, x->np);
    return 1;
}

static int m_piece(lua_State *L)
{
    Loft *x = strip_of(L);
    int   k = (int)luaL_checkinteger(L, 2) - 1;
    if (!x || k < 0 || k >= x->np)
        return 0;
    lua_pushnumber(L, x->pc[k].len);
    lua_pushboolean(L, x->pc[k].arc);
    return 2;
}

/*  Where a piece is `at` tiles along it. */
static int m_piece_at(lua_State *L)
{
    Loft *x = strip_of(L);
    int   k = (int)luaL_checkinteger(L, 2) - 1;
    V2    p, d;
    if (!x || k < 0 || k >= x->np)
        return 0;
    piece_at(&x->pc[k], (float)luaL_checknumber(L, 3), &p, &d);
    lua_pushnumber(L, p.x), lua_pushnumber(L, p.y);
    lua_pushnumber(L, d.x), lua_pushnumber(L, d.y);
    return 4;
}

/*  A box standing on the ground under it: the mark at a piece boundary. */
static int m_box(lua_State *L)
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
static int m_lane(lua_State *L)
{
    Loft *x = strip_of(L);
    int   i = (int)luaL_checkinteger(L, 2) - 1;
    if (!x || i < 0 || i >= x->ns)
        return 0;
    lua_pushinteger(L, x->smp[i].lane);
    lua_pushnumber(L, x->smp[i].zr[0]);
    lua_pushnumber(L, x->smp[i].zr[1]);
    return 3;
}

/*  One triangle of a structure, with a normal of its own: a deck's
 *  soffit hangs under the carriageway and faces down. */
static int m_tri_n(lua_State *L)
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
static int m_wall(lua_State *L)
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
static int m_edge(lua_State *L)
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
    {"lane",     m_lane    },
    {"tri_n",    m_tri_n   },
    {"wall",     m_wall    },
    {"edge",     m_edge    },
    {"pieces",   m_pieces  },
    {"piece",    m_piece   },
    {"piece_at", m_piece_at},
    {"box",      m_box     },
    {"walk_at",   m_walk_at  },
    {"walk_ends", m_walk_ends},
    {"pair",    m_pair  },
    {"info",       m_info      },
    {"road_class", m_road_class},
    {"count",   m_count },
    {"at",      m_at    },
    {"width",   m_width },
    {"class",   m_class },
    {"quad",    m_quad  },
    {"ground",  m_ground},
    {"order",   m_order },
    {NULL,      NULL    }
};

/*  ---- a junction ---------------------------------------------------- */

static JuncFan *junc_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "junction") == 0 ? (JuncFan *)o->rec : NULL;
}

/*  What the junction IS: where its middle sits, the height its tile was
 *  levelled to, how many points its outline came to, and what its
 *  asphalt is laid in. */
static int j_info(lua_State *L)
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

static int j_count(lua_State *L)
{
    JuncFan *j = junc_of(L);
    if (!j)
        return 0;
    lua_pushinteger(L, j->np);
    return 1;
}

static int j_at(lua_State *L)
{
    JuncFan *j = junc_of(L);
    int      i = (int)luaL_checkinteger(L, 2) - 1;
    if (!j || i < 0 || i >= j->np)
        return 0;
    lua_pushnumber(L, j->poly[i].x);
    lua_pushnumber(L, j->poly[i].y);
    return 2;
}

/*  The ground the junction's asphalt sits on at a point: its own tile is
 *  a levelled pad, so inside it every height is that flat one; where the
 *  outline reaches past the tile the asphalt follows the ground. */
static int j_surface(lua_State *L)
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
static int j_tri(lua_State *L)
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
static int j_quad(lua_State *L)
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

static const luaL_Reg JUNCTION[] = {
    {"info",    j_info   },
    {"count",   j_count  },
    {"at",      j_at     },
    {"surface", j_surface},
    {"tri",     j_tri    },
    {"quad",    j_quad   },
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
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "footway") == 0 ? (WalkFan *)o->rec : NULL;
}

/*  What the band IS: which kind it is, how many stations the network
 *  holds for it, how wide it is, where it sits in the stack, whether it
 *  lies on the ground, and -- for a crossing -- the depth it asked for,
 *  which the material paints the stop line against. */
static int w_info(lua_State *L)
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

static int w_count(lua_State *L)
{
    WalkFan *f = walk_of(L);
    if (!f)
        return 0;
    lua_pushinteger(L, ((const WalkPath *)f->w)->nst);
    return 1;
}

/*  One cross-section: the band's outer edge, its inner one and the
 *  height the network recorded there. */
static int w_at(lua_State *L)
{
    WalkFan      *f = walk_of(L);
    const WalkSt *st;
    int           i = (int)luaL_checkinteger(L, 2) - 1;
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
static int w_quad(lua_State *L)
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
static int w_ends(lua_State *L)
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
static int w_wire(lua_State *L)
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
    {"ends",  w_ends },
    {"wire",  w_wire },
    {"info",  w_info },
    {"count", w_count},
    {"at",    w_at   },
    {"quad",  w_quad },
    {NULL,    NULL   }
};

/*  ---- a lane -------------------------------------------------------- */

static LaneFan *lane_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "lane") == 0 ? (LaneFan *)o->rec : NULL;
}

static int n_info(lua_State *L)
{
    LaneFan *f = lane_of(L);
    if (!f)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, f->np), lua_setfield(L, -2, "n");
    lua_pushnumber(L, f->lift), lua_setfield(L, -2, "lift");
    lua_pushnumber(L, f->paint), lua_setfield(L, -2, "paint");
    lua_pushinteger(L, f->band), lua_setfield(L, -2, "band");
    lua_pushboolean(L, f->ramp), lua_setfield(L, -2, "ramp");
    lua_pushboolean(L, f->off), lua_setfield(L, -2, "off");
    lua_pushnumber(L, f->step), lua_setfield(L, -2, "step");
    return 1;
}

/*  One piece of the fitted line: how long it runs and whether it turns. */
static int n_piece(lua_State *L)
{
    LaneFan *f = lane_of(L);
    int      k = (int)luaL_checkinteger(L, 2) - 1;
    if (!f || k < 0 || k >= f->np)
        return 0;
    lua_pushnumber(L, f->pc[k].len);
    lua_pushboolean(L, f->pc[k].arc);
    return 2;
}

/*  Where that piece is `at` tiles along it. */
static int n_at(lua_State *L)
{
    LaneFan *f = lane_of(L);
    int      k = (int)luaL_checkinteger(L, 2) - 1;
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
static int n_height(lua_State *L)
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

static int n_order(lua_State *L)
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
static int n_wire(lua_State *L)
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
    {"info",   n_info  },
    {"piece",  n_piece },
    {"at",     n_at    },
    {"height", n_height},
    {"order",  n_order },
    {"wire",   n_wire  },
    {NULL,     NULL    }
};

/*  ---- a level crossing's panel -------------------------------------- */

static XingFan *xing_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "panel") == 0 ? (XingFan *)o->rec : NULL;
}

static int x_info(lua_State *L)
{
    XingFan *f = xing_of(L);
    if (!f)
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, f->order), lua_setfield(L, -2, "order");
    lua_pushnumber(L, f->lift), lua_setfield(L, -2, "lift");
    lua_pushnumber(L, f->slot), lua_setfield(L, -2, "slot");
    return 1;
}

/*  Corner k of the panel, 1 to 4, and the ground under it. */
static int x_at(lua_State *L)
{
    XingFan *f = xing_of(L);
    int      k = (int)luaL_checkinteger(L, 2) - 1;
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
static int x_quad(lua_State *L)
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
    {"info", x_info},
    {"at",   x_at  },
    {"quad", x_quad},
    {NULL,   NULL  }
};

/*  ---- a junction's outline ------------------------------------------ */

static OutlineFan *outline_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "outline") == 0 ? (OutlineFan *)o->rec : NULL;
}

/*  The numbers the junction is sized by: its middle, the half width of
 *  the band that runs through it, how far out a corner may stand, how
 *  much the family has been let out from the width it was tuned at, and
 *  the cap on how far an arm may be cut back for the junction's sake. */
static int o_info(lua_State *L)
{
    OutlineFan *o = outline_of(L);
    if (!o)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, o->col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, o->row), lua_setfield(L, -2, "row");
    lua_pushnumber(L, o->cx), lua_setfield(L, -2, "x");
    lua_pushnumber(L, o->cy), lua_setfield(L, -2, "y");
    lua_pushnumber(L, o->w), lua_setfield(L, -2, "half");
    lua_pushnumber(L, o->far), lua_setfield(L, -2, "far");
    lua_pushnumber(L, o->gro), lua_setfield(L, -2, "grow");
    lua_pushnumber(L, o->cap), lua_setfield(L, -2, "cap");
    lua_pushboolean(L, o->curbs), lua_setfield(L, -2, "curbs");
    lua_pushinteger(L, o->na), lua_setfield(L, -2, "n");
    return 1;
}

/*  One arm: where its own path starts, the way it leaves, the angle that
 *  makes, and which of the tile's four edges it belongs to. */
static int o_arm(lua_State *L)
{
    OutlineFan *o = outline_of(L);
    int         i = (int)luaL_checkinteger(L, 2) - 1;
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
static int o_point(lua_State *L)
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
static int o_trim(lua_State *L)
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
static int o_clamped(lua_State *L)
{
    if (outline_of(L))
        junction_outline_clamped((float)luaL_checknumber(L, 2));
    return 0;
}

/*  The arms as the script ordered them, given back so the box's mouths
 *  and the ring agree about which arm is which. */
static int o_order(lua_State *L)
{
    OutlineFan *o = outline_of(L);
    int         i, n;
    if (!o || !lua_istable(L, 2))
        return 0;
    n = (int)lua_rawlen(L, 2);
    if (n > 4)
        n = 4;
    for (i = 0; i < n; ++i)
    {
        lua_rawgeti(L, 2, i + 1);
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
static int o_count(lua_State *L)
{
    OutlineFan *o = outline_of(L);
    if (!o)
        return 0;
    lua_pushinteger(L, o->n);
    return 1;
}

static int o_back(lua_State *L)
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
static int o_close(lua_State *L)
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
    {"order", o_order},
    {"count", o_count},
    {"back",  o_back },
    {"close", o_close},
    {"info",    o_info   },
    {"arm",     o_arm    },
    {"point",   o_point  },
    {"trim",    o_trim   },
    {"clamped", o_clamped},
    {NULL,      NULL     }
};

/*  ---- a junction's band --------------------------------------------- */

static BandFan *band_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "band") == 0 ? (BandFan *)o->rec : NULL;
}

static int b_info(lua_State *L)
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
static int b_at(lua_State *L)
{
    BandFan *b = band_of(L);
    int      i = (int)luaL_checkinteger(L, 2) - 1;
    if (!b || i < 0 || i >= b->np)
        return 0;
    lua_pushnumber(L, b->poly[i].x), lua_pushnumber(L, b->poly[i].y);
    return 2;
}

/*  One arm's cut across the junction: whether the arm is there at all,
 *  and its two corners, which the ring's own mouth edge spans. */
static int b_arm(lua_State *L)
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
static int b_edge(lua_State *L)
{
    BandFan *b = band_of(L);
    int      i = (int)luaL_checkinteger(L, 2) - 1;
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
static int b_inset(lua_State *L)
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
static int b_close(lua_State *L)
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
    {"info",  b_info },
    {"at",    b_at   },
    {"arm",   b_arm  },
    {"edge",  b_edge },
    {"inset", b_inset},
    {"close", b_close},
    {NULL,    NULL   }
};

/*  ---- a fitted path ------------------------------------------------- */

static FitFan *fit_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "fit") == 0 ? (FitFan *)o->rec : NULL;
}

static int q_info(lua_State *L)
{
    FitFan *q = fit_of(L);
    if (!q)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, q->n), lua_setfield(L, -2, "n");
    lua_pushnumber(L, q->res0), lua_setfield(L, -2, "reserve0");
    lua_pushnumber(L, q->res1), lua_setfield(L, -2, "reserve1");
    lua_pushnumber(L, q->rmax), lua_setfield(L, -2, "rmax");
    lua_pushnumber(L, q->rmin), lua_setfield(L, -2, "rmin");
    lua_pushnumber(L, q->band), lua_setfield(L, -2, "band");
    lua_pushnumber(L, q->share), lua_setfield(L, -2, "share");
    lua_pushnumber(L, q->trim_cap), lua_setfield(L, -2, "trim_cap");
    return 1;
}

/*  Vertex k: where it is, and the tangent length it was built with -- a
 *  biarc's own, or -1 for a vertex the search placed. */
static int q_at(lua_State *L)
{
    FitFan *q = fit_of(L);
    int     k = (int)luaL_checkinteger(L, 2) - 1;
    if (!q || k < 0 || k >= q->n)
        return 0;
    lua_pushnumber(L, q->out[k].x), lua_pushnumber(L, q->out[k].y);
    lua_pushnumber(L, q->fixed[k]);
    return 3;
}

/*  Drop vertex k: the ones behind it move up. */
static int q_drop(lua_State *L)
{
    FitFan *q = fit_of(L);
    int     k = (int)luaL_checkinteger(L, 2) - 1, j;
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
static int q_corner(lua_State *L)
{
    FitFan *q = fit_of(L);
    int     k = (int)luaL_checkinteger(L, 2) - 1;
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
static int q_sweep(lua_State *L)
{
    FitFan *q = fit_of(L);
    V2      a, b, c;
    float   r;
    int     tight = 0;
    if (!q)
        return 0;
    a.x = (float)luaL_checknumber(L, 2), a.y = (float)luaL_checknumber(L, 3);
    b.x = (float)luaL_checknumber(L, 4), b.y = (float)luaL_checknumber(L, 5);
    c.x = (float)luaL_checknumber(L, 6), c.y = (float)luaL_checknumber(L, 7);
    r = path_fit_sweep(q->mark, a, b, c, (float)luaL_checknumber(L, 8), q->rmax, q->rmin, q->band, &tight);
    lua_pushnumber(L, r);
    lua_pushboolean(L, tight);
    return 2;
}

/*  How much tangent a corner demands: the fit's own arithmetic, asked
 *  for here rather than copied. */
static int q_demand(lua_State *L)
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
static int q_need(lua_State *L)
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
static int q_tally(lua_State *L)
{
    const char *what = luaL_checkstring(L, 2);
    if (fit_of(L))
        path_fit_count(what);
    return 0;
}

static const luaL_Reg FIT[] = {
    {"info",       q_info      },
    {"at",         q_at        },
    {"drop",       q_drop      },
    {"corner",     q_corner    },
    {"sweep",      q_sweep     },
    {"demand",     q_demand    },
    {"need",       q_need      },
    {"tally",      q_tally     },
    {NULL,         NULL        }
};

/*  ---- the runs of a path ---------------------------------------------- */

static RunFan *runs_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "runs") == 0 ? (RunFan *)o->rec : NULL;
}

/*  The chain: how many steps it has, whether a span may be any chord
 *  that holds, and whether either end is a junction's mouth. */
static int r_info(lua_State *L)
{
    RunFan *x = runs_of(L);
    if (!x)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, x->ns), lua_setfield(L, -2, "ns");
    lua_pushboolean(L, x->free_lines), lua_setfield(L, -2, "free");
    lua_pushboolean(L, x->ex0), lua_setfield(L, -2, "ex0");
    lua_pushboolean(L, x->ex1), lua_setfield(L, -2, "ex1");
    lua_pushnumber(L, x->band), lua_setfield(L, -2, "band");
    return 1;
}

/*  Step k, named by the first step it is the same as, and whether it
 *  goes anywhere at all.  Two steps are the same step exactly when they
 *  answer the same name. */
static int r_step(lua_State *L)
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
static int r_perp(lua_State *L)
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
static int r_slope(lua_State *L)
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
static int r_spread(lua_State *L)
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
static int r_chord(lua_State *L)
{
    RunFan *x = runs_of(L);
    int     i = (int)luaL_checkinteger(L, 2), j = (int)luaL_checkinteger(L, 3);
    if (x && i >= 0 && j <= x->ns && i < j)
        path_run_chord(x, i, j, (float)luaL_checknumber(L, 4));
    return 0;
}

/*  A span refused before it was ever sampled, for --path-dump. */
static int r_note(lua_State *L)
{
    RunFan *x = runs_of(L);
    if (x)
        path_run_note(x, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3), luaL_checkstring(L, 4));
    return 0;
}

/*  Would the span i..j stand as this kind of run?  A slope is 1 and a
 *  free line 2; a straight is its own steps and needs no asking. */
static int r_try(lua_State *L)
{
    RunFan *x = runs_of(L);
    int     i = (int)luaL_checkinteger(L, 2), j = (int)luaL_checkinteger(L, 3);
    if (!x || i < 0 || j > x->ns || i >= j)
        return 0;
    lua_pushboolean(L, path_run_try(x, i, j, (int)luaL_checkinteger(L, 4)));
    return 1;
}

/*  The span just tried wins the prefix ending at j. */
static int r_keep(lua_State *L)
{
    RunFan *x = runs_of(L);
    int     j = (int)luaL_checkinteger(L, 2);
    if (x && j >= 0 && j <= x->ns)
        path_run_keep(x, j);
    return 0;
}

/*  A run of the answer, and the order they are put in once they are all
 *  named. */
static int r_emit(lua_State *L)
{
    RunFan *x = runs_of(L);
    int     i = (int)luaL_checkinteger(L, 2), j = (int)luaL_checkinteger(L, 3);
    if (x && i >= 0 && j > i && j <= x->ns)
        path_run_emit(x, i, j, (int)luaL_checkinteger(L, 4));
    return 0;
}

static int r_order(lua_State *L)
{
    RunFan *x = runs_of(L);
    if (x)
        path_run_order(x);
    return 0;
}

static const luaL_Reg RUNS[] = {
    {"info",  r_info },
    {"step",  r_step },
    {"perp",  r_perp },
    {"slope", r_slope},
    {"spread", r_spread},
    {"chord",  r_chord },
    {"note",   r_note  },
    {"try",    r_try   },
    {"keep",  r_keep },
    {"emit",  r_emit },
    {"order", r_order},
    {NULL,    NULL   }
};

/*  ---- the chain of lines a path's runs make ---------------------------- */

static ChainFan *chain_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "chain") == 0 ? (ChainFan *)o->rec : NULL;
}

/*  Which end a call names: the start of the chain, or its goal. */
static int chain_end(lua_State *L, int idx)
{
    return strcmp(luaL_checkstring(L, idx), "goal") == 0;
}

static int c_info(lua_State *L)
{
    ChainFan *c = chain_of(L);
    if (!c)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, c->nr), lua_setfield(L, -2, "nr");
    lua_pushboolean(L, c->ex0), lua_setfield(L, -2, "ex0");
    lua_pushboolean(L, c->ex1), lua_setfield(L, -2, "ex1");
    return 1;
}

/*  Run i: what sort of line it is, and whether it reaches the chain's
 *  first point or its last. */
static int c_run(lua_State *L)
{
    ChainFan *c = chain_of(L);
    int       i = (int)luaL_checkinteger(L, 2) - 1;
    if (!c || i < 0 || i >= c->nr)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, c->runs[i].kind), lua_setfield(L, -2, "kind");
    lua_pushboolean(L, c->runs[i].ta == 0), lua_setfield(L, -2, "first");
    lua_pushboolean(L, c->runs[i].tb == c->nt - 1), lua_setfield(L, -2, "last");
    return 1;
}

/*  Pull an end onto the angle of the run beside it. */
static int c_aim(lua_State *L)
{
    ChainFan *c = chain_of(L);
    if (c && c->nr > 0)
        path_chain_aim(c, chain_end(L, 2));
    return 0;
}

/*  Does that end already lie on the line of the run beside it? */
static int c_on_line(lua_State *L)
{
    ChainFan *c = chain_of(L);
    if (!c || c->nr <= 0)
        return 0;
    lua_pushboolean(L, path_chain_on_line(c, chain_end(L, 2)));
    return 1;
}

/*  The chain, a line at a time: an end's own line, or a run. */
static int c_add_end(lua_State *L)
{
    ChainFan *c = chain_of(L);
    if (c)
        path_chain_end(c, chain_end(L, 2));
    return 0;
}

static int c_add(lua_State *L)
{
    ChainFan *c = chain_of(L);
    int       i = (int)luaL_checkinteger(L, 2) - 1;
    if (c && i >= 0 && i < c->nr)
        path_chain_run(c, i);
    return 0;
}

static const luaL_Reg CHAIN[] = {
    {"info",    c_info   },
    {"run",     c_run    },
    {"aim",     c_aim    },
    {"on_line", c_on_line},
    {"add_end", c_add_end},
    {"add",     c_add    },
    {NULL,      NULL     }
};

/*  ---- the crossing of two lines ---------------------------------------- */

static JoinFan *join_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "join") == 0 ? (JoinFan *)o->rec : NULL;
}

/*  How the crossing sits: how far past the line behind it is, how far
 *  short of the line ahead, how much line there is either side, and
 *  whether a free line is involved or the chain's own end. */
static int cr_info(lua_State *L)
{
    JoinFan *j = join_of(L);
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
static int cr_holds(lua_State *L)
{
    JoinFan *j = join_of(L);
    if (!j)
        return 0;
    lua_pushboolean(L, path_join_holds(j, strcmp(luaL_checkstring(L, 2), "ahead") == 0));
    return 1;
}

/*  Would the corner leave a covered cell of the gap bare? */
static int cr_covers(lua_State *L)
{
    JoinFan *j = join_of(L);
    if (!j)
        return 0;
    lua_pushboolean(L, path_join_covers(j));
    return 1;
}

/*  The radius the corridor allows at the crossing, given the tangent it
 *  may take, and whether the straights that reach an arc of that radius
 *  hold. */
static int cr_arc(lua_State *L)
{
    JoinFan *j = join_of(L);
    if (!j)
        return 0;
    lua_pushnumber(L, path_join_arc(j, (float)luaL_checknumber(L, 2)));
    return 1;
}

static int cr_legs(lua_State *L)
{
    JoinFan *j = join_of(L);
    if (!j)
        return 0;
    lua_pushboolean(L, path_join_legs(j, (float)luaL_checknumber(L, 2)));
    return 1;
}

/*  Put the vertex at the crossing. */
static int cr_place(lua_State *L)
{
    JoinFan *j = join_of(L);
    if (j)
        path_join_place(j);
    return 0;
}

static const luaL_Reg JOIN[] = {
    {"info",   cr_info  },
    {"holds",  cr_holds },
    {"covers", cr_covers},
    {"arc",    cr_arc   },
    {"legs",   cr_legs  },
    {"place",  cr_place },
    {NULL,     NULL    }
};

/*  ---- the biarc between two parallel lines ----------------------------- */

static BridgeFan *bridge_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "bridge") == 0 ? (BridgeFan *)o->rec : NULL;
}

/*  The line either side of the pair, what the vertex behind was built
 *  with, how far the two lines' ends lie apart, and which of the two
 *  lines is the chain's own end. */
static int br_info(lua_State *L)
{
    BridgeFan *b = bridge_of(L);
    if (!b)
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, b->len_in), lua_setfield(L, -2, "len_in");
    lua_pushnumber(L, b->len_out), lua_setfield(L, -2, "len_out");
    lua_pushnumber(L, b->fixed_prev), lua_setfield(L, -2, "fixed_prev");
    lua_pushnumber(L, b->gap), lua_setfield(L, -2, "gap");
    lua_pushnumber(L, b->res0), lua_setfield(L, -2, "reserve0");
    lua_pushnumber(L, b->res1), lua_setfield(L, -2, "reserve1");
    lua_pushboolean(L, b->head), lua_setfield(L, -2, "head");
    lua_pushboolean(L, b->tail), lua_setfield(L, -2, "tail");
    lua_pushboolean(L, b->first), lua_setfield(L, -2, "first");
    lua_pushboolean(L, b->last), lua_setfield(L, -2, "last");
    lua_pushnumber(L, b->share), lua_setfield(L, -2, "share");
    lua_pushnumber(L, b->band), lua_setfield(L, -2, "band");
    lua_pushnumber(L, b->margin), lua_setfield(L, -2, "margin");
    lua_pushboolean(L, b->padded), lua_setfield(L, -2, "padded");
    return 1;
}

/*  One placing of the S, its tangent points drawn back this far along
 *  each line: the biarc between them, and the edge either side of it.
 *  Nothing where there is no biarc at all. */
static int br_solve(lua_State *L)
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
static int br_refuse(lua_State *L)
{
    BridgeFan *b = bridge_of(L);
    if (b)
        path_bridge_refuse(b, strcmp(luaL_checkstring(L, 2), "out") == 0,
                           (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4));
    return 0;
}

/*  Does the band hold the placing, and at what radius? */
static int br_holds(lua_State *L)
{
    BridgeFan *b = bridge_of(L);
    if (!b)
        return 0;
    lua_pushnumber(L, path_bridge_holds(b));
    return 1;
}

/*  The placing as --sweep-probe reports it. */
static int br_result(lua_State *L)
{
    BridgeFan *b = bridge_of(L);
    if (b)
        path_bridge_result(b, (float)luaL_checknumber(L, 2));
    return 0;
}

static int br_keep(lua_State *L)
{
    BridgeFan *b = bridge_of(L);
    if (b)
        path_bridge_keep(b);
    return 0;
}

static int br_place(lua_State *L)
{
    BridgeFan *b = bridge_of(L);
    if (b)
        path_bridge_place(b);
    return 0;
}

/*  The pair as --sweep-probe reports it. */
static int br_note(lua_State *L)
{
    BridgeFan *b = bridge_of(L);
    if (b)
        path_bridge_note(b, (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3));
    return 0;
}

static const luaL_Reg BRIDGE[] = {
    {"info",  br_info },
    {"solve",  br_solve },
    {"refuse", br_refuse},
    {"holds",  br_holds },
    {"result", br_result},
    {"keep",  br_keep },
    {"place", br_place},
    {"note",  br_note },
    {NULL,    NULL    }
};

/*  ---- the walk between two lines that neither cross nor bridge ---------- */

static StepFan *step_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "step") == 0 ? (StepFan *)o->rec : NULL;
}

static int st_side(lua_State *L, int idx)
{
    return strcmp(luaL_checkstring(L, idx), "ahead") == 0;
}

/*  How many of the chain's own points lie between the two lines, whether
 *  each line is a run rather than the chain's own end line, and how wide
 *  the band is. */
static int st_info(lua_State *L)
{
    StepFan *w = step_of(L);
    if (!w)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, w->gap), lua_setfield(L, -2, "gap");
    lua_pushboolean(L, w->head), lua_setfield(L, -2, "head");
    lua_pushboolean(L, w->tail), lua_setfield(L, -2, "tail");
    lua_pushnumber(L, w->hw), lua_setfield(L, -2, "half");
    return 1;
}

/*  Does the gap point beside that end already lie in line with it? */
static int st_inline(lua_State *L)
{
    StepFan *w = step_of(L);
    if (!w || w->gap < 1)
        return 0;
    lua_pushboolean(L, path_step_inline(w, st_side(L, 2)));
    return 1;
}

/*  Is the gap a sideways step, and can it be drawn as one diagonal? */
static int st_jog(lua_State *L)
{
    StepFan *w = step_of(L);
    if (!w || w->gap < 1)
        return 0;
    lua_pushboolean(L, path_step_jog(w));
    return 1;
}

static int st_diagonal(lua_State *L)
{
    StepFan *w = step_of(L);
    if (!w || w->gap < 1)
        return 0;
    lua_pushboolean(L, path_step_diagonal(w, (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3)));
    return 1;
}

/*  A line's own end as a vertex, and a point of the gap. */
static int st_place(lua_State *L)
{
    StepFan *w = step_of(L);
    if (w)
        path_step_end(w, st_side(L, 2));
    return 0;
}

static int st_point(lua_State *L)
{
    StepFan *w = step_of(L);
    int      t = (int)luaL_checkinteger(L, 2);
    if (w && t >= 1 && t <= w->gap)
        path_step_point(w, t);
    return 0;
}

static const luaL_Reg STEP[] = {
    {"info",     st_info    },
    {"inline",   st_inline  },
    {"jog",      st_jog     },
    {"diagonal", st_diagonal},
    {"place",    st_place   },
    {"point",    st_point   },
    {NULL,       NULL       }
};

/*  ---- the fillet swept into one corner --------------------------------- */

static SweepFan *sweep_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "sweep") == 0 ? (SweepFan *)o->rec : NULL;
}

/*  The corner: whether there is anything to sweep at all, the tangent it
 *  has been given, the tangent of half its turn -- which is what turns a
 *  tangent length into a radius -- and the widths it is held between. */
static int sw_info(lua_State *L)
{
    SweepFan *s = sweep_of(L);
    if (!s)
        return 0;
    lua_newtable(L);
    lua_pushboolean(L, s->straight), lua_setfield(L, -2, "straight");
    lua_pushnumber(L, s->tan_half), lua_setfield(L, -2, "tan_half");
    lua_pushnumber(L, s->tlim), lua_setfield(L, -2, "tangent");
    lua_pushnumber(L, s->rmax), lua_setfield(L, -2, "rmax");
    lua_pushnumber(L, s->rmin), lua_setfield(L, -2, "rmin");
    lua_pushnumber(L, s->hw), lua_setfield(L, -2, "half");
    lua_pushnumber(L, s->margin), lua_setfield(L, -2, "margin");
    lua_pushboolean(L, s->padded), lua_setfield(L, -2, "padded");
    return 1;
}

/*  Does an arc of this radius hold on the corridor and leave nothing
 *  bare? */
static int sw_holds(lua_State *L)
{
    SweepFan *s = sweep_of(L);
    if (!s)
        return 0;
    lua_pushboolean(L, path_sweep_holds(s, (float)luaL_checknumber(L, 2)));
    return 1;
}

/*  The radius the corner is given, and whether it came out under the
 *  minimum. */
static int sw_answer(lua_State *L)
{
    SweepFan *s = sweep_of(L);
    if (s)
        path_sweep_answer(s, (float)luaL_checknumber(L, 2), lua_toboolean(L, 3));
    return 0;
}

static const luaL_Reg SWEEP[] = {
    {"info",   sw_info  },
    {"holds",  sw_holds },
    {"answer", sw_answer},
    {NULL,     NULL     }
};

/*  ---- a fitted path cut into pieces ------------------------------------ */

static PieceFan *piece_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "pieces") == 0 ? (PieceFan *)o->rec : NULL;
}

static int pc_info(lua_State *L)
{
    PieceFan *p = piece_of(L);
    if (!p)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, p->n), lua_setfield(L, -2, "n");
    return 1;
}

/*  Corner i: the radius and tangent it was given, the tangent of half
 *  its turn, how much of the incoming edge the piece already laid has
 *  left, and how long the edge it leaves along is.  Nothing at all for a
 *  vertex with no turn to it. */
static int pc_corner(lua_State *L)
{
    PieceFan *p = piece_of(L);
    int       i = (int)luaL_checkinteger(L, 2) - 1;
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
static int pc_straight(lua_State *L)
{
    PieceFan *p = piece_of(L);
    int       i = (int)luaL_checkinteger(L, 2) - 1;
    if (p && i >= 0 && i < p->n)
        path_piece_straight(p, i);
    return 0;
}

static int pc_arc(lua_State *L)
{
    PieceFan *p = piece_of(L);
    int       i = (int)luaL_checkinteger(L, 2) - 1;
    if (p && i >= 1 && i + 1 < p->n)
        path_piece_arc(p, i, (float)luaL_checknumber(L, 3));
    return 0;
}

static int pc_tail(lua_State *L)
{
    PieceFan *p = piece_of(L);
    if (p)
        path_piece_tail(p);
    return 0;
}

static const luaL_Reg PIECES[] = {
    {"info",     pc_info    },
    {"corner",   pc_corner  },
    {"straight", pc_straight},
    {"arc",      pc_arc     },
    {"tail",     pc_tail    },
    {NULL,       NULL       }
};

/*  ---- a highway band's chain of fit points ------------------------------ */

static StairFan *stair_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "stair") == 0 ? (StairFan *)o->rec : NULL;
}

/*  How many cells the band has, and how many straight ones a stair may
 *  step over between two of its blocks. */
static int sr_info(lua_State *L)
{
    StairFan *s = stair_of(L);
    if (!s)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, s->n), lua_setfield(L, -2, "n");
    lua_pushinteger(L, s->gap), lua_setfield(L, -2, "gap");
    return 1;
}

/*  Cell i: whether it is a curve block, which way the chain turns there,
 *  and whether an on-ramp pins it. */
static int sr_block(lua_State *L)
{
    StairFan *s = stair_of(L);
    int       i = (int)luaL_checkinteger(L, 2) - 1;
    if (!s || i < 0 || i >= s->n)
        return 0;
    lua_pushboolean(L, s->block[i]);
    return 1;
}

static int sr_turn(lua_State *L)
{
    StairFan *s = stair_of(L);
    int       i = (int)luaL_checkinteger(L, 2) - 1;
    if (!s || i < 0 || i >= s->n)
        return 0;
    lua_pushinteger(L, hiway_stair_turn(s, i));
    return 1;
}

static int sr_pinned(lua_State *L)
{
    StairFan *s = stair_of(L);
    int       i = (int)luaL_checkinteger(L, 2) - 1;
    if (!s || i < 0 || i >= s->n)
        return 0;
    lua_pushboolean(L, hiway_stair_pinned(s, i));
    return 1;
}

/*  A cell as a point of the chain, and a run of cells as the one point
 *  at their centre. */
static int sr_point(lua_State *L)
{
    StairFan *s = stair_of(L);
    int       i = (int)luaL_checkinteger(L, 2) - 1;
    if (s && i >= 0 && i < s->n)
        hiway_stair_point(s, i);
    return 0;
}

static int sr_centre(lua_State *L)
{
    StairFan *s = stair_of(L);
    int       i = (int)luaL_checkinteger(L, 2) - 1, j = (int)luaL_checkinteger(L, 3) - 1;
    if (s && i >= 0 && j >= i && j < s->n)
        hiway_stair_centre(s, i, j);
    return 0;
}

static const luaL_Reg STAIR[] = {
    {"info",   sr_info  },
    {"block",  sr_block },
    {"turn",   sr_turn  },
    {"pinned", sr_pinned},
    {"point",  sr_point },
    {"centre", sr_centre},
    {NULL,     NULL     }
};

/*  ---- a highway strip's elevation --------------------------------------- */

static ProfFan *prof_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "profile") == 0 ? (ProfFan *)o->rec : NULL;
}

/*  What sort of strip it is and the numbers it is shaped by. */
static int pr_info(lua_State *L)
{
    ProfFan *p = prof_of(L);
    if (!p)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, p->n), lua_setfield(L, -2, "n");
    lua_pushnumber(L, p->total), lua_setfield(L, -2, "total");
    lua_pushboolean(L, p->ramp), lua_setfield(L, -2, "ramp");
    lua_pushboolean(L, p->lane_piece), lua_setfield(L, -2, "lane_piece");
    lua_pushboolean(L, p->lane_off), lua_setfield(L, -2, "lane_off");
    lua_pushboolean(L, p->flat), lua_setfield(L, -2, "flat");
    lua_pushnumber(L, p->z0), lua_setfield(L, -2, "deck_above");
    lua_pushnumber(L, p->ramp0), lua_setfield(L, -2, "taper0");
    lua_pushnumber(L, p->ramp1), lua_setfield(L, -2, "taper1");
    lua_pushnumber(L, p->grade), lua_setfield(L, -2, "grade");
    lua_pushnumber(L, p->stiff), lua_setfield(L, -2, "stiff");
    lua_pushnumber(L, p->lift), lua_setfield(L, -2, "lift");
    return 1;
}

/*  Station i: how far along it is, and the ground under it. */
static int pr_at(lua_State *L)
{
    ProfFan *p = prof_of(L);
    int      i = (int)luaL_checkinteger(L, 2) - 1;
    float    s_at, z, ground;
    if (!p || i < 0 || i >= p->n)
        return 0;
    hiway_prof_at(p, i, &s_at, &z, &ground);
    lua_pushnumber(L, s_at);
    lua_pushnumber(L, ground);
    return 2;
}

/*  The height the station is given. */
static int pr_set(lua_State *L)
{
    ProfFan *p = prof_of(L);
    int      i = (int)luaL_checkinteger(L, 2) - 1;
    if (p && i >= 0 && i < p->n)
        hiway_prof_set(p, i, (float)luaL_checknumber(L, 3));
    return 0;
}

/*  The curve a lane drop's descent follows. */
static int pr_ease(lua_State *L)
{
    if (!prof_of(L))
        return 0;
    lua_pushnumber(L, hiway_lane_ease((float)luaL_checknumber(L, 2)));
    return 1;
}

static const luaL_Reg PROFILE[] = {
    {"info", pr_info},
    {"at",   pr_at  },
    {"set",  pr_set },
    {"ease", pr_ease},
    {NULL,   NULL   }
};

/*  ---- a ramp's join, slid ----------------------------------------------- */

static SlideFan *slide_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "slide") == 0 ? (SlideFan *)o->rec : NULL;
}

/*  How far along the deck the descent may start before it reaches the
 *  lane line -- nothing where the two never meet -- and how far along the
 *  road the join may slide. */
static int sd_info(lua_State *L)
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
static int sd_route(lua_State *L)
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
static int sd_exits(lua_State *L)
{
    SlideFan *s = slide_of(L);
    if (!s)
        return 0;
    lua_pushboolean(L, hiway_slide_exits(s));
    return 1;
}

/*  The placing kept, with the slack its taper is given past the edge. */
static int sd_keep(lua_State *L)
{
    SlideFan *s = slide_of(L);
    if (s)
        hiway_slide_keep(s, (float)luaL_checknumber(L, 2));
    return 0;
}

/*  Why no placing was found, for --lane-dump. */
static int sd_note(lua_State *L)
{
    SlideFan *s = slide_of(L);
    if (s)
        hiway_slide_note(s, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                         (int)luaL_checkinteger(L, 4), (int)luaL_checkinteger(L, 5));
    return 0;
}

static const luaL_Reg SLIDE[] = {
    {"info",  sd_info },
    {"route", sd_route},
    {"exits", sd_exits},
    {"keep",  sd_keep },
    {"note",  sd_note },
    {NULL,    NULL    }
};

/*  ---- the lane a ramp drops from a deck --------------------------------- */

static DropFan *drop_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "drop") == 0 ? (DropFan *)o->rec : NULL;
}

static int dp_info(lua_State *L)
{
    DropFan *d = drop_of(L);
    if (!d)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, d->n), lua_setfield(L, -2, "n");
    lua_pushinteger(L, d->nramps), lua_setfield(L, -2, "ramps");
    lua_pushnumber(L, d->reach), lua_setfield(L, -2, "reach");
    lua_pushnumber(L, d->narrow), lua_setfield(L, -2, "narrow");
    return 1;
}

/*  Station i: how far along the deck it is, where it stands, which way
 *  it heads. */
static int dp_station(lua_State *L)
{
    DropFan *d = drop_of(L);
    float    at;
    V2       pos, dir;
    if (!d || !hiway_drop_station(d, (int)luaL_checkinteger(L, 2) - 1, &at, &pos, &dir))
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
static int dp_ramp(lua_State *L)
{
    DropFan *d = drop_of(L);
    V2       c0, tile, along;
    int      len, off;
    if (!d || !hiway_drop_ramp(d, (int)luaL_checkinteger(L, 2) - 1, &c0, &tile, &along, &len, &off))
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
static int dp_clear(lua_State *L)
{
    DropFan *d = drop_of(L);
    if (d)
        hiway_drop_clear(d);
    return 0;
}

static int dp_width(lua_State *L)
{
    DropFan *d = drop_of(L);
    if (d)
        hiway_drop_width(d, (int)luaL_checkinteger(L, 2) - 1, (int)luaL_checkinteger(L, 3),
                         (float)luaL_checknumber(L, 4));
    return 0;
}

static int dp_gore(lua_State *L)
{
    DropFan *d = drop_of(L);
    if (d)
        hiway_drop_gore(d, (int)luaL_checkinteger(L, 2) - 1, (int)luaL_checkinteger(L, 3));
    return 0;
}

static const luaL_Reg DROP[] = {
    {"info",    dp_info   },
    {"station", dp_station},
    {"ramp",    dp_ramp   },
    {"clear",   dp_clear  },
    {"width",   dp_width  },
    {"gore",    dp_gore   },
    {NULL,      NULL      }
};

/*  ---- a strip's elevation over the ground ------------------------------- */

static GroundFan *ground_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "ground") == 0 ? (GroundFan *)o->rec : NULL;
}

/*  How many stations, how long the strip is, and whether each end
 *  reaches a node it must be pinned to. */
static int gd_info(lua_State *L)
{
    GroundFan *g = ground_of(L);
    if (!g)
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, g->n), lua_setfield(L, -2, "n");
    lua_pushnumber(L, g->total), lua_setfield(L, -2, "total");
    lua_pushboolean(L, g->pin0), lua_setfield(L, -2, "pin0");
    lua_pushboolean(L, g->pin1), lua_setfield(L, -2, "pin1");
    lua_pushboolean(L, g->dead0), lua_setfield(L, -2, "dead0");
    lua_pushboolean(L, g->dead1), lua_setfield(L, -2, "dead1");
    lua_pushboolean(L, g->pin_node), lua_setfield(L, -2, "reaches_node");
    return 1;
}

/*  Station i: how far along the strip it is, and the height the ground
 *  has given it so far. */
static int gd_at(lua_State *L)
{
    GroundFan *g = ground_of(L);
    float      at, z;
    if (!g || !loft_ground_at(g, (int)luaL_checkinteger(L, 2) - 1, &at, &z))
        return 0;
    lua_pushnumber(L, at);
    lua_pushnumber(L, z);
    return 2;
}

/*  The altitude an end's node stands at, and the altitude a level
 *  crossing under station i pins the strip to. */
static int gd_node(lua_State *L)
{
    GroundFan *g = ground_of(L);
    float      z;
    int        which = strcmp(luaL_checkstring(L, 2), "goal") == 0;
    if (!g || !loft_ground_node(g, which, &z))
        return 0;
    lua_pushnumber(L, z);
    return 1;
}

static int gd_crossing(lua_State *L)
{
    GroundFan *g = ground_of(L);
    float      z;
    if (!g || !loft_ground_crossing(g, (int)luaL_checkinteger(L, 2) - 1, &z))
        return 0;
    lua_pushnumber(L, z);
    return 1;
}

/*  The height a station is given. */
static int gd_set(lua_State *L)
{
    GroundFan *g = ground_of(L);
    if (g)
        loft_ground_set(g, (int)luaL_checkinteger(L, 2) - 1, (float)luaL_checknumber(L, 3));
    return 0;
}

static const luaL_Reg GROUND[] = {
    {"info",     gd_info    },
    {"at",       gd_at      },
    {"node",     gd_node    },
    {"crossing", gd_crossing},
    {"set",      gd_set     },
    {NULL,       NULL       }
};

/*  ---- an on-ramp's four sides ------------------------------------------- */

static OrientFan *orient_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "orient") == 0 ? (OrientFan *)o->rec : NULL;
}

/*  Side k, going north, east, south, west: whether it is a deck tile,
 *  whether that deck runs along this side's own axis, and whether it
 *  carries a road. */
static int or_side(lua_State *L)
{
    OrientFan *o = orient_of(L);
    int        k = (int)luaL_checkinteger(L, 2) - 1, deck, axis, road;
    if (!o || k < 0 || k > 3 || !hiway_orient_side(o, k, &deck, &axis, &road))
        return 0;
    lua_pushboolean(L, deck);
    lua_pushboolean(L, axis);
    lua_pushboolean(L, road);
    return 3;
}

/*  The sides the script settled on: which is the deck's, which the
 *  road's, which a deck met end-on, and how many roads there were. */
static int or_answer(lua_State *L)
{
    OrientFan *o = orient_of(L);
    if (!o)
        return 0;
    hiway_orient_answer(o, (int)luaL_checkinteger(L, 2),
                        (int)luaL_optinteger(L, 3, 0) - 1, (int)luaL_optinteger(L, 4, 0) - 1,
                        (int)luaL_optinteger(L, 5, 0) - 1, 0, (int)luaL_optinteger(L, 6, 0));
    return 0;
}

static const luaL_Reg ORIENT[] = {
    {"side",   or_side  },
    {"answer", or_answer},
    {NULL,     NULL     }
};

/*  ---- the shelf's copies of a corner ------------------------------------ */

static ShelfFan *shelf_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "shelf") == 0 ? (ShelfFan *)o->rec : NULL;
}

static int sh_info(lua_State *L)
{
    ShelfFan *s = shelf_of(L);
    if (!s)
        return 0;
    lua_pushinteger(L, R_MAP);
    lua_pushinteger(L, s->nodes);
    return 2;
}

/*  The copies of the corner at one grid point: a flat run of the
 *  corridor that wrote each, how far its station was, and the height it
 *  put there.  Nothing where no corridor wrote it. */
static int sh_copies(lua_State *L)
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
static int sh_set(lua_State *L)
{
    ShelfFan *s = shelf_of(L);
    if (s)
        shelf_set(s, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                  (int)luaL_checkinteger(L, 4), (float)luaL_checknumber(L, 5));
    return 0;
}

/*  The i-th node tile, the heights of every copy round it, and the level
 *  they all take. */
static int sh_node(lua_State *L)
{
    ShelfFan *s = shelf_of(L);
    int32_t   col, row;
    if (!s || !shelf_node_at(s, (int)luaL_checkinteger(L, 2) - 1, &col, &row))
        return 0;
    lua_pushinteger(L, col);
    lua_pushinteger(L, row);
    return 2;
}

static int sh_heights(lua_State *L)
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

static int sh_node_set(lua_State *L)
{
    ShelfFan *s = shelf_of(L);
    if (s)
        shelf_node_set(s, (int32_t)luaL_checkinteger(L, 2), (int32_t)luaL_checkinteger(L, 3),
                       (float)luaL_checknumber(L, 4));
    return 0;
}

static const luaL_Reg SHELF[] = {
    {"info",     sh_info    },
    {"copies",   sh_copies  },
    {"set",      sh_set     },
    {"node",     sh_node    },
    {"heights",  sh_heights },
    {"node_set", sh_node_set},
    {NULL,       NULL       }
};

/*  ---- a lane carried across a crossing ---------------------------------- */

static XLaneFan *xlane_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "xlane") == 0 ? (XLaneFan *)o->rec : NULL;
}

static int xl_info(lua_State *L)
{
    XLaneFan *x = xlane_of(L);
    if (!x)
        return 0;
    lua_pushinteger(L, x->n);
    return 1;
}

/*  Is lane i an open end that could carry on across a crossing? */
static int xl_end(lua_State *L)
{
    XLaneFan *x = xlane_of(L);
    if (!x)
        return 0;
    lua_pushboolean(L, xlane_end(x, (int)luaL_checkinteger(L, 2) - 1));
    return 1;
}

/*  How lane lb's start lies from lane la's end.  Nothing for a lane that
 *  is not a candidate at all. */
static int xl_measure(lua_State *L)
{
    XLaneFan *x = xlane_of(L);
    float     off, dot, ahead, aside, dist;
    if (!x || !xlane_measure(x, (int)luaL_checkinteger(L, 2) - 1, (int)luaL_checkinteger(L, 3) - 1,
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
static int xl_merge(lua_State *L)
{
    XLaneFan *x = xlane_of(L);
    if (x)
        xlane_merge(x, (int)luaL_checkinteger(L, 2) - 1, (int)luaL_checkinteger(L, 3) - 1);
    return 0;
}

static int xl_link(lua_State *L)
{
    XLaneFan *x = xlane_of(L);
    if (!x)
        return 0;
    lua_pushboolean(L, xlane_link(x, (int)luaL_checkinteger(L, 2) - 1, (int)luaL_checkinteger(L, 3) - 1));
    return 1;
}

static const luaL_Reg XLANE[] = {
    {"info",    xl_info   },
    {"open",    xl_end    },
    {"measure", xl_measure},
    {"merge",   xl_merge  },
    {"link",    xl_link   },
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
int script_rule_object(const char *rule, const char *kind, void *rec)
{
    lua_State *L = script_state();
    ScriptObj *o;
    int        drew = 0;
    if (!L || !api_rule_begin(L, rule))
        return 0;
    o       = (ScriptObj *)lua_newuserdata(L, sizeof *o);
    o->kind = kind;
    o->rec  = rec;
    o->gen  = s_gen;
    luaL_getmetatable(L, OBJ_META);
    lua_setmetatable(L, -2);
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

void api_object_open(lua_State *L)
{
    luaL_newmetatable(L, OBJ_META);
    lua_pushcfunction(L, obj_index), lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, obj_tostring), lua_setfield(L, -2, "__tostring");
    /*  One table of methods for each kind of thing, so two kinds may
     *  answer the same question in their own ways. */
    lua_newtable(L);
    lua_newtable(L), luaL_setfuncs(L, STRIP, 0), lua_setfield(L, -2, "strip");
    lua_newtable(L), luaL_setfuncs(L, JUNCTION, 0), lua_setfield(L, -2, "junction");
    lua_newtable(L), luaL_setfuncs(L, FOOTWAY, 0), lua_setfield(L, -2, "footway");
    lua_newtable(L), luaL_setfuncs(L, LANE, 0), lua_setfield(L, -2, "lane");
    lua_newtable(L), luaL_setfuncs(L, XING, 0), lua_setfield(L, -2, "panel");
    lua_newtable(L), luaL_setfuncs(L, OUTLINE, 0), lua_setfield(L, -2, "outline");
    lua_newtable(L), luaL_setfuncs(L, BAND, 0), lua_setfield(L, -2, "band");
    lua_newtable(L), luaL_setfuncs(L, FIT, 0), lua_setfield(L, -2, "fit");
    lua_newtable(L), luaL_setfuncs(L, RUNS, 0), lua_setfield(L, -2, "runs");
    lua_newtable(L), luaL_setfuncs(L, CHAIN, 0), lua_setfield(L, -2, "chain");
    lua_newtable(L), luaL_setfuncs(L, JOIN, 0), lua_setfield(L, -2, "join");
    lua_newtable(L), luaL_setfuncs(L, BRIDGE, 0), lua_setfield(L, -2, "bridge");
    lua_newtable(L), luaL_setfuncs(L, STEP, 0), lua_setfield(L, -2, "step");
    lua_newtable(L), luaL_setfuncs(L, SWEEP, 0), lua_setfield(L, -2, "sweep");
    lua_newtable(L), luaL_setfuncs(L, PIECES, 0), lua_setfield(L, -2, "pieces");
    lua_newtable(L), luaL_setfuncs(L, STAIR, 0), lua_setfield(L, -2, "stair");
    lua_newtable(L), luaL_setfuncs(L, PROFILE, 0), lua_setfield(L, -2, "profile");
    lua_newtable(L), luaL_setfuncs(L, SLIDE, 0), lua_setfield(L, -2, "slide");
    lua_newtable(L), luaL_setfuncs(L, DROP, 0), lua_setfield(L, -2, "drop");
    lua_newtable(L), luaL_setfuncs(L, GROUND, 0), lua_setfield(L, -2, "ground");
    lua_newtable(L), luaL_setfuncs(L, ORIENT, 0), lua_setfield(L, -2, "orient");
    lua_newtable(L), luaL_setfuncs(L, SHELF, 0), lua_setfield(L, -2, "shelf");
    lua_newtable(L), luaL_setfuncs(L, XLANE, 0), lua_setfield(L, -2, "xlane");
    {
        int n;
        lua_newtable(L), luaL_setfuncs(L, api_tile_methods(&n), 0), lua_setfield(L, -2, "tile");
    }
    lua_setfield(L, -2, "methods");
    lua_pop(L, 1);
}

#endif
