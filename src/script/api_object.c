/*  api_object.c: an OBJECT of the world, as a script sees one.
 *
 *  Everything the pipeline builds is a thing: a strip of line, a
 *  junction, a margin, a level meet, a prop.  A script is handed the
 *  thing itself, not a window on to "the one being drawn":
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
 *  function, kept for the length of the call, and asked what it is.
 *  `s.kind` is "strip".  And every face it draws is attributed to it, so
 *  the inspector can say which thing put a triangle on a tile.
 *
 *  The methods an object has are its kind's.  They are all of one shape.
 *  The ones that MEASURE answer plain numbers and make no table.  A
 *  strip has thousands of stations, and a city thousands of strips.  The
 *  ones that DRAW answer whether the mesh took the face.
 *
 *      every kind      kind, tile, ground(x, y), order(x, y)
 *      strip           count, at, width, extras, class,
 *                      quad, tri, prism, fan, box, wire, model
 *
 *  A handle is dead the moment the rule that was given it returns.  The
 *  record it points at is the pipeline's, and the pipeline moves on.  A
 *  method on a dead handle answers nothing rather than reading freed
 *  memory. */
#include "script.h"


#include <string.h>

#include "internal.h"
#include "mesh/internal.h"
#include "pipeline.h"
#include "mesh/model.h"
#include "log.h"

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
 *  always nothing but a list of these.  So the list IS the code.  One
 *  more field for a script to read is one row here.  It is not a push
 *  and a setfield and two places to forget them. */
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

/*  What the strip IS, as against what it is made of.  The family it
 *  belongs to, and the class it carries.  Its half width, and the
 *  material it is laid in.  How many stations it was cut into, whether
 *  it flies clear of the ground and carries its own height, whether it
 *  has margins beside it.  The meet band each end's junction has laid
 *  over it. */
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
    lua_pushboolean(L, net_family_has(x->d->fam, NH_FLIES)), lua_setfield(L, -2, "flies");
    /*  A family with lips has margins beside it: unless the pass that
     *  lays them is off.  Then the way takes the whole band and there is
     *  nothing to leave room for. */
    lua_pushboolean(L, x->d->fam->lips && margin_on()), lua_setfield(L, -2, "lips");
    lua_pushboolean(L, x->d->fam->slab), lua_setfield(L, -2, "slab");
    lua_pushnumber(L, x->d->xw0), lua_setfield(L, -2, "cross0");
    lua_pushnumber(L, x->d->xw1), lua_setfield(L, -2, "cross1");
    lua_pushstring(L, x->d->fam->slot ? x->d->fam->slot : "slot_strip"), lua_setfield(L, -2, "slot");
    /*  A slab's own facts.
     *
     *      Whether it lies on the ground.
     *      Whether it is one lane wide.
     *      Whether it is a spur's concrete.
     *      Where its girder and its parapet stand. */
    lua_pushboolean(L, x->d->flat), lua_setfield(L, -2, "flat");
    lua_pushboolean(L, x->d->lane_piece), lua_setfield(L, -2, "lane_piece");
    lua_pushboolean(L, x->d->struct_), lua_setfield(L, -2, "structure");
    lua_pushnumber(L, BAND_GIRDER), lua_setfield(L, -2, "girder");
    lua_pushnumber(L, BAND_PARAPET), lua_setfield(L, -2, "parapet");
    /*  And how it narrows: the length the taper runs over, the half
     *  width it narrows to, and which end it starts from. */
    lua_pushboolean(L, x->pin1), lua_setfield(L, -2, "ahead");
    lua_pushboolean(L, x->pin0), lua_setfield(L, -2, "behind");
    lua_pushnumber(L, x->d->taper), lua_setfield(L, -2, "taper");
    lua_pushnumber(L, x->d->hw_end), lua_setfield(L, -2, "taper_to");
    lua_pushboolean(L, x->d->taper_start), lua_setfield(L, -2, "taper_start");
    return 1;
}

/*  Which class of line stands on a tile, for a strip whose own class is
 *  -1: an island of line with no segment to read it from. */
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
    lua_pushnumber(L, (float)line_class(x->c, tc, tr));
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

/*  One station, as plain numbers.
 *
 *      Where it is.  The height its section was graded to.  The way the
 *      centerline runs there.  How far along the strip it stands.  The
 *      band's two half widths as fractions.  How far the nearest level
 *      meet is.  The ground's own line. */
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

/*  How much narrower the band is where the centerline runs diagonally:
 *  the fit's own compensation.  This keeps a diagonal line the width it
 *  looks rather than the width it measures. */
/*  A station's half widths set: how a strip NARROWS along its length is
 *  the family's taper stage.  A rule answering it reads each station's
 *  widths through `at` and writes them back here.  Nothing is decided in
 *  the writing: it takes what it is given. */
static int api_strip_narrow(lua_State *L)
{
    Loft *x = strip_of(L);
    int   i = (int)luaL_checkinteger(L, 2);
    if (!x || i < 0 || i >= x->ns)
        return 0;
    x->smp[i].wl = (float)luaL_checknumber(L, 3);
    x->smp[i].wr = (float)luaL_checknumber(L, 4);
    return 0;
}

static int api_strip_width(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    lua_pushnumber(L, width_factor((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), x->comp));
    return 1;
}

/*  The class a quad is drawn under: the material reads it.  A value four
 *  higher says the quad lies in a cut, so the clipping check knows the
 *  ground standing over it is meant. */
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
 *  composition and they agree.
 *
 *      The two edges.
 *      The across range.
 *      The along.
 *      The material and the tile's slot. */
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

/*  One station of the margin beside the strip: its outer edge, its
 *  inner one where it meets the way, and the height. */
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

/*  Where that margin's band begins and ends, which is what the network
 *  joins it to its neighbors by. */
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
 *  of the lines: how many, how long each runs and whether it turns. */
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

/*  What a slab's station carries beside its place.  It says which of its
 *  two outer lanes a spur has taken, and the height of each where it
 *  has. */
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

/*  One triangle of a structure, with a normal of its own: a slab's
 *  soffit hangs under the way and faces down. */
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
    lua_pushboolean(L, put_tri_line_n(x->m, x->c, x->mask_bit, (float)luaL_checknumber(L, 17),
                                      (const float (*)[3])t, nrm, col3, ref, ref) == 0);
    return 1;
}

/*  A wall between two points, from one pair of heights down to another:
 *  a slab's end, where it begins and ends in the air. */
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

/*  A slab's edge: the girder's fascia down from the way, in the
 *  slab's own shadow, and the parapet up from it. */
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
    lua_pushboolean(L, put_fascia(x->m, x->c, x->mask_bit, (float)luaL_checknumber(L, 12),
                                 ea, eb, (float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7),
                                 nrm, (float)luaL_checknumber(L, 10),
                                 lua_toboolean(L, 11)) == 0);
    return 1;
}

/*  Whether a station stands on or beside a level meet, which has its
 *  own protection: a lamp is not stood there. */
static int api_strip_near_lap(lua_State *L)
{
    Loft *x = strip_of(L);
    V2    p;
    if (!x)
        return 0;
    p.x = (float)luaL_checknumber(L, 2);
    p.y = (float)luaL_checknumber(L, 3);
    lua_pushboolean(L, marking_near_lap(x->c, p));
    return 1;
}

/*  One model stood beside the strip, standing on the strip's own height
 *  rather than the ground under the lip. */
static int api_strip_prop(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    lua_pushboolean(L, net_model_put_on(net_model_find(luaL_checkstring(L, 2)), x->m, x->c, x->mask_bit,
                                        (float)luaL_checknumber(L, 3),
                                        (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5),
                                        (float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7),
                                        0.0f, 0.0f, 0.0f,
                                        (float)luaL_checknumber(L, 8), (float)luaL_checknumber(L, 8), 1) == 0);
    return 1;
}

/*  Where along this strip a line crosses it, in tiles.  A railway's
 *  whistle posts are placed against these.  A lamp is kept away from
 *  them.  The reading is the map's, the placing is the rule's. */
static int api_strip_laps(lua_State *L)
{
    Loft *x = strip_of(L);
    int   i, n = 0;
    if (!x)
        return 0;
    lua_newtable(L);
    for (i = 1; i < x->ns && n < 64; ++i)
    {
        int32_t col = (int32_t)floorf(x->smp[i].pos.x), row = (int32_t)floorf(x->smp[i].pos.y);
        /*  The tile the station BEFORE this one stood on: a run of
         *  stations across one tile counts once.  The count is against
         *  the previous station rather than the last tile answered.  So
         *  a path that leaves a tile and comes back to it counts it
         *  again. */
        int32_t pc = (int32_t)floorf(x->smp[i - 1].pos.x), pr = (int32_t)floorf(x->smp[i - 1].pos.y);
        if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP || (col == pc && row == pr))
            continue;
        if (!net_line_lapped(x->c->xbld[row * R_MAP + col]))
            continue;
        lua_pushnumber(L, (double)x->smp[i].s), lua_rawseti(L, -2, ++n);
    }
    return 1;
}

/*  Whether a point is on the map at all.  `order` clamps to the edge, so
 *  a prop stood without asking this would appear at the border rather
 *  than not at all. */
static int api_strip_on_map(lua_State *L)
{
    int32_t tc = (int32_t)floorf((float)luaL_checknumber(L, 2));
    int32_t tr = (int32_t)floorf((float)luaL_checknumber(L, 3));
    if (!strip_of(L))
        return 0;
    lua_pushboolean(L, tc >= 0 && tr >= 0 && tc < R_MAP && tr < R_MAP);
    return 1;
}

/*  One model stood flat on the ground beside the strip. */
static int api_strip_prop_flat(lua_State *L)
{
    Loft *x = strip_of(L);
    if (!x)
        return 0;
    lua_pushboolean(L, net_model_put(net_model_find(luaL_checkstring(L, 2)), x->m, x->c, x->mask_bit,
                                     (float)luaL_checknumber(L, 3),
                                     (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5),
                                     (float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7),
                                     0.0f, 0.0f, 0.0f) == 0);
    return 1;
}

/*  And a thread signal, which is the traffic's as well as the mesh's:
 *  the model the script names stands where it said.  The signal is then
 *  registered so the block it watches can light it each frame.  The
 *  furniture switch takes the model and leaves the record: a signal that
 *  is not drawn still governs its block. */
static int api_strip_rail_signal(lua_State *L)
{
    Loft       *x     = strip_of(L);
    const char *model = luaL_checkstring(L, 2);
    float       order = (float)luaL_checknumber(L, 3);
    float       px = (float)luaL_checknumber(L, 4), py = (float)luaL_checknumber(L, 5);
    float       fx = (float)luaL_checknumber(L, 6), fy = (float)luaL_checknumber(L, 7);
    if (!x)
        return 0;
    if (furniture_on() &&
        net_model_put(net_model_find(model), x->m, x->c, x->mask_bit, order, px, py, fx, fy, 0.0f, 0.0f, 0.0f) != 0)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, mesh_signal_add(x->m, px, py, fx, fy,
                                            (float)luaL_checknumber(L, 9),
                                            (int)luaL_checkinteger(L, 10),
                                            (int)luaL_checkinteger(L, 8)) == 0);
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
    {"line_class", api_strip_road_class},
    {"count",   api_strip_count },
    {"at",      api_strip_at    },
    {"width",   api_strip_width },
    {"narrow",  api_strip_narrow},
    {"class",   api_strip_class },
    {"quad",    api_strip_quad  },
    {"ground",  api_strip_ground},
    {"order",   api_strip_order },
    {"near_lap", api_strip_near_lap},
    {"prop",    api_strip_prop  },
    {"prop_flat", api_strip_prop_flat},
    {"on_map",  api_strip_on_map},
    {"thread_signal", api_strip_rail_signal},
    {"meets", api_strip_laps},
    {NULL,      NULL    }
};

/*  ---- a junction ---------------------------------------------------- */

static JuncFan *junc_of(lua_State *L)
{
    return (JuncFan *)rec_of(L, "junction");
}

/*  What the junction IS: where its middle sits, the height its tile was
 *  leveled to, how many points its outline came to.  What its fill is
 *  laid in. */
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

/*  The ground the junction's fill sits on at a point: its own tile is a
 *  leveled pad.  So inside it every height is that flat one.  Where the
 *  outline reaches past the tile the fill follows the ground. */
static int api_junction_surface(lua_State *L)
{
    JuncFan *j = junc_of(L);
    if (!j)
        return 0;
    lua_pushnumber(L, junc_surface(j->jb->f, j->jb->c, j->jb->mask_bit, j->jb->col, j->jb->row,
                                   (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), j->zj));
    return 1;
}

/*  One triangle of the fill, laid ON the drawn surface: the fan's own
 *  heights are a guide.  Each piece takes the surface where it lands, so
 *  a fan across a tile's fold does not cut under the terrain. */
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
 *  network passes.  What the script decides is the ORDER.  Which tiles,
 *  in what order, which passes run at all.  And there is no C loop
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
    lua_pushboolean(L, w->lines), lua_setfield(L, -2, "lines");
    lua_pushboolean(L, w->underground), lua_setfield(L, -2, "underground");
    return 1;
}

/*  Whether this build wants a tile at all.  An edit's build replaces
 *  only the chunks its closure named.  The rest stand: asking here saves
 *  the call, and the primitives check it again for themselves. */
static int api_world_wanted(lua_State *L)
{
    WorldFan *w = world_of(L);
    lua_pushboolean(L, w && (w->pass == 1 || mesh_want_tile((int32_t)luaL_checkinteger(L, 2),
                                                            (int32_t)luaL_checkinteger(L, 3))));
    return 1;
}

/*  One tile, GATHERED and handed over: its ground, or its zone for the
 *  map view's tint.  Answers a `tile` handle, or nothing where this
 *  build wants no such tile: so a script writes
 *
 *      local t = w:tile(col, row) if t then ... end
 *
 *  and composes it itself.  The record is the world's own and is written
 *  again at the next ask.  This is what keeps a map's worth of tiles
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

/*  ---- the network, discovered ------------------------------------------
 *
 *  `w:net_discover()` clears the store and answers how many families
 *  have a network to find.  `w:net_cells(fk)` hands one family's map
 *  over as a `network` handle.  What comes back through that handle is
 *  the whole of the network.  The class pass, the measuring walk and the
 *  drawing walk read it.  None of them reads the map. */
static NetDiscFan s_world_disc;

static int api_world_net_discover(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    net_disc_reset();
    lua_pushinteger(L, net_n_walked);
    return 1;
}

static int api_world_net_cells(lua_State *L)
{
    WorldFan *w  = world_of(L);
    int       fk = (int)luaL_checkinteger(L, 2);
    if (!w || fk < 0 || fk >= net_n_walked)
        return 0;
    s_world_disc.fk     = fk;
    s_world_disc.f      = net_walked[fk]->f;
    s_world_disc.family = net_walked[fk]->name;
    s_world_disc.full   = 0;
    net_disc_planes(w->c, w->l, net_walked[fk]->f, s_world_disc.links, s_world_disc.art);
    api_object_push(L, "network", &s_world_disc);
    return 1;
}

/*  And whether the store took everything the script found.  A network
 *  half kept draws a city half wrong, so it is asked about rather than
 *  discovered later as missing geometry. */
static int api_world_net_found(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    if (s_world_disc.full)
    {
        R_ERR("net", "no room for the network of %s: the store is full",
              s_world_disc.family ? s_world_disc.family : "?");
        w->rc = -1;
    }
    lua_pushboolean(L, !s_world_disc.full);
    return 1;
}

/*  The band's map, handed over for the script to walk its bands.
 *  Answers nothing where this build is replaying the bands a previous
 *  one fitted.  This is when there is nothing to discover. */
static HwDiscFan s_world_hw;

static int api_world_hw_cells(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w || !net_hw_replaying())
        return 0;
    net_hw_disc_reset();
    s_world_hw.full = 0;
    api_object_push(L, "bands", &s_world_hw);
    return 1;
}

/*  ---- the margin round a junction ------------------------------------
 *
 *  The ring read as a margin, and off it which of the junction's mouths
 *  may carry a meet.  Both are rules, and the DRIVE asks them.  The pass
 *  that measures the trims and the pass that draws the box each ask for
 *  the junction in hand before reading it.  So nothing in the margin
 *  reaches up and a script that answers neither leaves the junction
 *  bare.
 *
 *  `mouths`, `mouth` and `mouth_is` serve both, because the mouths in
 *  hand are always the junction whose band was answered last. */
/*  Where a gate settles with nothing near it.  It is one angle.  The
 *  build asks for it once, and every gate the moving world starts reads
 *  it. */
static int api_world_gate_rest(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_gate_rest_is((float)luaL_optnumber(L, 2, 0.0));
    return 0;
}

static int api_world_junction_bands(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    lua_pushinteger(L, net_trim_junctions(w->c, w->l));
    return 1;
}

static int api_world_junction_band(lua_State *L)
{
    void *b;
    if (!world_of(L) || (b = net_trim_band((int)luaL_checkinteger(L, 2))) == NULL)
        return 0;
    api_object_push(L, "band", b);
    return 1;
}

/*  The answer taken, and the junction's mouths read off it. */
static int api_world_junction_band_done(lua_State *L)
{
    if (!world_of(L))
        return 0;
    margin_band_answered();
    lua_pushinteger(L, net_trim_mouths());
    return 1;
}

/*  And what follows for the pass that measures: the trim each arm is
 *  handed, and how deep a band each mouth asks for. */
static int api_world_junction_trim(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_trim_done((int)luaL_checkinteger(L, 2));
    return 0;
}

/*  The same three for the box the drive is drawing. */
static int api_world_box_band(lua_State *L)
{
    void *b;
    if (!world_of(L) || (b = net_junction_band()) == NULL)
        return 0;
    api_object_push(L, "band", b);
    return 1;
}

static int api_world_box_band_done(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    margin_band_answered();
    if (net_junction_band_done() != 0)
        w->rc = -1;
    lua_pushinteger(L, net_junction_mouths());
    return 1;
}

/*  One mouth of the junction in hand: what the outline makes of it, for
 *  the rule that says whether it carries a meet. */
static int api_world_mouth(lua_State *L)
{
    int32_t col, row;
    int     arm, ctrl, pave;
    float   cs, span;
    if (!world_of(L) || !margin_mouth_at((int)luaL_checkinteger(L, 2), &col, &row, &arm, &ctrl, &pave, &cs, &span))
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, row), lua_setfield(L, -2, "row");
    lua_pushinteger(L, arm), lua_setfield(L, -2, "arm");
    lua_pushinteger(L, ctrl), lua_setfield(L, -2, "control");
    lua_pushboolean(L, pave), lua_setfield(L, -2, "margin");
    lua_pushnumber(L, cs), lua_setfield(L, -2, "cos");
    lua_pushnumber(L, span), lua_setfield(L, -2, "span");
    return 1;
}

/*  And the answer: how deep a band it asks for.  A depth of nought is a
 *  mouth that carries no stripe, the same as no answer at all.  A band
 *  with no depth is not a shallow stripe.  It is none. */
static int api_world_mouth_is(lua_State *L)
{
    float deep = (float)luaL_optnumber(L, 3, 0.0);
    if (!world_of(L))
        return 0;
    margin_mouth_is((int)luaL_checkinteger(L, 2), lua_isnumber(L, 3) && deep > 0.0f, deep);
    return 0;
}

/*  ---- the paths waiting to be cut ---------------------------------------
 *
 *  Cutting a fitted path into pieces is arc.rules.pieces's, and only the
 *  drive may ask for it.  A pass that needs a path cut puts the chain in
 *  the queue and the drive comes round and cuts what is waiting.  The
 *  pass then reads the pieces back. */
static int api_world_cuts(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    if (net_cut_full())
    {
        R_ERR("net", "no room to cut every path this pass asked for");
        w->rc = -1;
    }
    lua_pushinteger(L, net_cuts());
    return 1;
}

static int api_world_cut(lua_State *L)
{
    void *p;
    if (!world_of(L) || (p = net_cut_at((int)luaL_checkinteger(L, 2))) == NULL)
        return 0;
    api_object_push(L, "pieces", p);
    return 1;
}

static int api_world_cut_done(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_cut_done((int)luaL_checkinteger(L, 2));
    return 0;
}

/*  ---- the loft, as a service the composing script calls
 *  ---------------
 *
 *      w:loft(pieces, profile [, how])  ->  faces
 *
 *  `pieces` is a path: what arc.fit answered, or anything with the same
 *  fields.  `profile` is the CROSS-SECTION, a sequence of {across = , up
 *  = , mat = } points read left to right across the centerline.  The
 *  face between one point and the next is drawn in that point's
 *  material.  `how` carries the rest: `step` and `step_arc`, how finely
 *  a straight and an arc are stationed.  `lift`, how far the section
 *  stands over the ground under the centerline, or `z` for a height
 *  outright.  `slot`, where in the tile's painter stack the faces go.
 *  And `closed` to join the section's last point back to its first.
 *
 *  Nothing about a line reaches this: a script that wants a line lofts a
 *  line's section, and one that wants a canal lofts a canal's. */
#define LOFT_SEC_MAX 64

static int api_world_loft(lua_State *L)
{
    WorldFan *w = world_of(L);
    static Piece pc[MAX_PIECES];
    LoftRung     sec[LOFT_SEC_MAX];
    LoftSweep    how;
    int          np = 0, nsec = 0, i, faces;
    if (!w || !lua_istable(L, 2) || !lua_istable(L, 3))
        return 0;
    np = (int)lua_rawlen(L, 2);
    if (np > MAX_PIECES)
        np = MAX_PIECES;
    for (i = 0; i < np; ++i)
    {
        lua_rawgeti(L, 2, i + 1);
        memset(&pc[i], 0, sizeof pc[i]);
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, "arc");
            pc[i].arc = lua_toboolean(L, -1);
            lua_pop(L, 1);
            pc[i].a.x = api_field_num(L, "ax", 0.0f);
            pc[i].a.y = api_field_num(L, "ay", 0.0f);
            pc[i].b.x = api_field_num(L, "bx", 0.0f);
            pc[i].b.y = api_field_num(L, "by", 0.0f);
            pc[i].c.x = api_field_num(L, "cx", 0.0f);
            pc[i].c.y = api_field_num(L, "cy", 0.0f);
            pc[i].r   = api_field_num(L, "r", 0.0f);
            pc[i].t0  = api_field_num(L, "t0", 0.0f);
            pc[i].t1  = api_field_num(L, "t1", 0.0f);
            pc[i].len = api_field_num(L, "len", 0.0f);
        }
        lua_pop(L, 1);
    }
    nsec = (int)lua_rawlen(L, 3);
    if (nsec > LOFT_SEC_MAX)
        nsec = LOFT_SEC_MAX;
    for (i = 0; i < nsec; ++i)
    {
        lua_rawgeti(L, 3, i + 1);
        memset(&sec[i], 0, sizeof sec[i]);
        if (lua_istable(L, -1))
        {
            sec[i].across = api_field_num(L, "across", 0.0f);
            sec[i].up     = api_field_num(L, "up", 0.0f);
            sec[i].mat    = api_field_num(L, "mat", (float)MAT_LINE);
        }
        lua_pop(L, 1);
    }
    memset(&how, 0, sizeof how);
    if (lua_istable(L, 4))
    {
        lua_pushvalue(L, 4);
        how.step_run = api_field_num(L, "step", 0.0f);
        how.step_arc = api_field_num(L, "step_arc", how.step_run);
        how.lift     = api_field_num(L, "lift", 0.0f);
        how.slot     = api_field_num(L, "slot", 0.0f);
        lua_getfield(L, -1, "z");
        how.pinned = lua_isnumber(L, -1);
        how.z      = how.pinned ? (float)lua_tonumber(L, -1) : 0.0f;
        lua_pop(L, 1);
        lua_getfield(L, -1, "closed");
        how.closed = lua_toboolean(L, -1);
        lua_pop(L, 2);
    }
    faces = loft_sweep(w->m, w->c, w->mask_bit, pc, np, sec, nsec, &how);
    if (faces < 0)
    {
        w->rc = -1;
        return 0;
    }
    lua_pushinteger(L, faces);
    return 1;
}

/*  The SHAPE a script composes into: what the inspector names the
 *  triangles by, and what the checks group them into.  One at a time.
 *  Opening the next closes the one before, and `w:shape()` with nothing
 *  to name closes the last.  Which is the discipline the C loop kept
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

/*  A POWER LINE'S TILE, gathered and handed over.  It holds the pylon's
 *  place and the edges its wires span to.  The mesh is open for the
 *  script to draw into.  Answers the prop and whether the tile is a
 *  meet.  Where a line runs over a line or a railway there is no pylon,
 *  only the span.  Or nothing where no line stands here.
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
    int         piece, second, links, meet;
    ScriptProp  at;
    if (!w)
        return 0;
    b      = w->c->xbld[idx];
    piece  = piece_family(b, &f);
    second = piece_second(b, &f2);
    /*  A line on its own ground, or the second family of a meet:
     *  either way it is the POWER family that draws it. */
    if (piece >= 0 && f == net_power->f)
        meet = 0, links = piece_links(w->l, piece, w->c->xter[idx]);
    else if (second >= 0 && f2 == net_power->f)
        meet = 1, links = piece_links(w->l, second, w->c->xter[idx]);
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
     *  runs.  A meet shares its tile with the line or the line
     *  under it, and says so. */
    shape_close(w->shape);
    w->shape = shape_open("power line at %d,%d%s", (int)col, (int)row,
                          meet ? ", sharing the tile" : "");
    shape_note("links\t%s%s%s%s", links & L_N ? "north " : "", links & L_E ? "east " : "",
               links & L_S ? "south " : "", links & L_W ? "west " : "");
    script_emit_close();
    script_emit_open(w->m, w->c, w->mask_bit, tile_order(w->c, col, row, w->mask_bit));
    api_prop_push(L, &at);
    lua_pushboolean(L, meet);
    return 2;
}

/*  THE MARGINS, one path at a time.  `w:margins()` says how many the
 *  network holds and whether the outline is on.  `w:margin(i)` gathers
 *  one and opens its shape, or answers nothing where there is nothing
 *  there to draw. */
static WalkFan s_world_walk;

static int api_world_margins(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, margin_count());
    lua_pushboolean(L, margin_outline());
    return 2;
}

static int api_world_margin(lua_State *L)
{
    WorldFan *w = world_of(L);
    ShapeId   sh;
    if (!w)
        return 0;
    /*  The one before is closed FIRST.  A shape opened while another is
     *  still open nests inside it.  Closing the outer one then takes the
     *  inner with it.  Which leaves every triangle the script drew
     *  claimed by nothing at all. */
    shape_close(w->shape);
    w->shape = SHAPE_NONE;
    if (!margin_gather(w->m, w->c, w->mask_bit, (int)luaL_checkinteger(L, 2),
                         &s_world_walk, &sh))
        return 0;
    w->shape = sh;
    api_object_push(L, "margin", &s_world_walk);
    return 1;
}

/*  THE JUNCTION RINGS.  A junction's outline is walked from the arms the
 *  measure filled, and both the trims and the box drawn later read it.
 *  So the script is asked for every one in a pass of its own between the
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

/*  THE JUNCTION CONTROLS.  `w:controls()` takes the reading, every
 *  junction on the map, and what its own family measured there, and
 *  answers how many there are.  `w:control(i)` hands one over as the
 *  name of the rule that settles it and a table of the reading.
 *  `w:control_is` puts the answer, a stop or a signal an arm, on to the
 *  tile.  Nothing that turns on a control is measured until every one of
 *  them has been answered. */
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

/*  ONE MOUTH'S CROSSWALK: what the outline asked for, the line there is
 *  to give up, and how much of it runs straight from the mouth.  The
 *  depth the rule answers is held to the line, so one meet can
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

/*  A LEVEL LAP.  `w:lap(col,row)` gathers the one on that tile and opens
 *  its shape.  It answers what the script needs to measure it: the sine
 *  of the angle the two cross at, and their two widths.  The script
 *  hands the measurements back through `w:lap_frame`, and `w:lap_draw`
 *  lays the panel, the record and the approaches from them. */
static int api_world_lap(lua_State *L)
{
    WorldFan *w      = world_of(L);
    int32_t   col    = (int32_t)luaL_checkinteger(L, 2);
    int32_t   row    = (int32_t)luaL_checkinteger(L, 3);
    int32_t   idx    = row * R_MAP + col;
    Family    f2;
    int       second;
    float     sine, line, thread;
    int32_t   xc, xr;
    if (!w)
        return 0;
    second = piece_second(w->c->xbld[idx], &f2);
    if (second < 0 || !net_family_laps(net_family(f2)))
        return 0;
    if (build_lap(w->m, w->c, w->l, w->mask_bit, col, row, second) != 0)
        return 0;
    net_lap_ask(&xc, &xr, &sine, &line, &thread);
    lua_newtable(L);
    lua_pushinteger(L, xc), lua_setfield(L, -2, "col");
    lua_pushinteger(L, xr), lua_setfield(L, -2, "row");
    lua_pushnumber(L, sine), lua_setfield(L, -2, "sin");
    lua_pushnumber(L, line), lua_setfield(L, -2, "line");
    lua_pushnumber(L, thread), lua_setfield(L, -2, "thread");
    return 1;
}

static int api_world_lap_frame(lua_State *L)
{
    ScriptLap fr;
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
    net_lap_frame(&fr);
    return 0;
}

/*  The record, and the panel's four corners.  They come back as a
 *  `panel` handle for the script to lay the panel over.  Nothing comes
 *  back where the two lines do not meet and there is none to lay. */
static int api_world_lap_panel(lua_State *L)
{
    const LapFan *f;
    if (!world_of(L) || !(f = net_lap_panel()))
        return 0;
    api_object_push(L, "panel", (void *)f);
    return 1;
}

/*  ONE APPROACH of the meet: what the script needs to decide what
 *  stands on it, with the mesh opened for what it puts there.  Answers
 *  nothing past the second. */
static int api_world_lap_approach(lua_State *L)
{
    WorldFan         *w = world_of(L);
    ScriptApproachAsk a;
    if (!w || !net_lap_approach((int)luaL_checkinteger(L, 2), &a))
        return 0;
    /*  The mesh, opened for the marks the script decides on: each of
     *  them stands at the meet's own place in the stack.  W:lap_mark
     *  puts it where the script said. */
    script_emit_close();
    w->m->strip_class = 0.0f;
    script_emit_open(w->m, w->c, w->mask_bit, net_lap_order());
    lua_newtable(L);
    lua_pushnumber(L, a.reach), lua_setfield(L, -2, "reach");
    lua_pushnumber(L, a.mast), lua_setfield(L, -2, "mast");
    lua_pushnumber(L, a.limit), lua_setfield(L, -2, "limit");
    lua_pushnumber(L, a.line), lua_setfield(L, -2, "line");
    lua_pushnumber(L, a.x), lua_setfield(L, -2, "x");
    lua_pushnumber(L, a.y), lua_setfield(L, -2, "y");
    lua_pushnumber(L, a.fx), lua_setfield(L, -2, "fx");
    lua_pushnumber(L, a.fy), lua_setfield(L, -2, "fy");
    lua_pushnumber(L, a.gx), lua_setfield(L, -2, "gx");
    lua_pushnumber(L, a.gy), lua_setfield(L, -2, "gy");
    lua_pushboolean(L, a.ns), lua_setfield(L, -2, "ns");
    return 1;
}

/*  And one mark where the script put it.  `out` runs along the approach
 *  from the middle and `across` from its centerline.  The mark names the
 *  model that stands there and the way it faces. */
static int api_world_lap_mark(lua_State *L)
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
    mk.fx     = api_field_num(L, "fx", 0.0f);
    mk.fy     = api_field_num(L, "fy", 0.0f);
    lua_getfield(L, -1, "model");
    model = lua_tostring(L, -1);
    snprintf(mk.model, sizeof mk.model, "%s", model ? model : "");
    lua_pop(L, 2);
    if (net_lap_place(&mk) != 0)
        w->rc = -1;
    lua_pushboolean(L, w->rc == 0);
    return 1;
}

/*  And the approaches: the masts, the stop lines and the signs. */
static int api_world_lap_approaches(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = net_lap_approaches();
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
 *  family-and-class pairs the pipeline can present.  `w:lane_run(i)`
 *  names one, and `w:lane_run_is` takes the offsets the rule answered
 *  for it.  They are settled before the lane model is built, so nothing
 *  inside it has to ask. */
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
 *  a primitive.  The drive asks it once for every class before any strip
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

/*  WHERE A STRIP STANDS CLEAR of the ground, on a family whose flies
 *  stage is a rule.  The drive asks it once for a plain strip and once
 *  for a structure, before anything is graded.  The rule answers a
 *  HEIGHT, so the grading compares at each station without asking again.
 *  A strip that always stands clear answers -math.huge. */
static int api_world_flies_runs(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_flies_runs_reset();
    lua_pushinteger(L, net_flies_runs());
    return 1;
}

static int api_world_flies_run(lua_State *L)
{
    const char *rule;
    int         structure;
    if (!world_of(L) || (rule = net_flies_run_at((int)luaL_checkinteger(L, 2), &structure)) == NULL)
        return 0;
    lua_pushstring(L, rule);
    lua_pushboolean(L, structure);
    return 2;
}

static int api_world_flies_run_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_flies_run_is((int)luaL_checkinteger(L, 2), (float)luaL_optnumber(L, 3, 1e30));
    return 0;
}

static int api_world_lanes(lua_State *L)
{
    return world_nets(L, 0);
}

/*  ONE BOUNDARY of a fit, where two of its lines meet.  `p:pair(k)`
 *  answers what the two rules that settle it read.  It says whether the
 *  far line crosses the one after it, and how far ahead that lies.  It
 *  also says whether these two cross at all.  `p:after_is` takes the
 *  first answer and `p:try` the second, one way at a time until one
 *  holds. */
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
    path_after_is(v && strcmp(v, "meet") == 0);
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

/*  And the path as it stands at the end.  The rule that drops the idle
 *  vertices reads it, and settles the radius at each of the rest. */
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

/*  p:corridor(): the cells the fit was given, the two ends it has to run
 *  between, and the band's own half width.  This is the grid corridor
 *  itself.  So a script may sweep its own line through it and hand that
 *  back with p:answer: a different algorithm, and not a compile. */
static int api_path_corridor(lua_State *L)
{
    const int32_t *tcol, *trow;
    int            nt, k;
    V2             start, goal;
    float          hw;
    if (!rec_of(L, "path") || !path_corridor(&tcol, &trow, &nt, &start, &goal, &hw))
        return 0;
    lua_createtable(L, 0, 5);
    lua_createtable(L, nt, 0);
    for (k = 0; k < nt; ++k)
    {
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, tcol[k]), lua_setfield(L, -2, "col");
        lua_pushinteger(L, trow[k]), lua_setfield(L, -2, "row");
        lua_rawseti(L, -2, k);
    }
    lua_setfield(L, -2, "cells");
    lua_pushinteger(L, nt), lua_setfield(L, -2, "n");
    lua_pushnumber(L, hw), lua_setfield(L, -2, "half");
    lua_createtable(L, 0, 2);
    lua_pushnumber(L, start.x), lua_setfield(L, -2, "x");
    lua_pushnumber(L, start.y), lua_setfield(L, -2, "y");
    lua_setfield(L, -2, "start");
    lua_createtable(L, 0, 2);
    lua_pushnumber(L, goal.x), lua_setfield(L, -2, "x");
    lua_pushnumber(L, goal.y), lua_setfield(L, -2, "y");
    lua_setfield(L, -2, "goal");
    return 1;
}

/*  p:answer(points, radii, budgets, n): the path settled OUTRIGHT, in
 *  place of the pipeline's own stages.  `points` counts from nought and
 *  its length comes with it.  `radii` and `budgets` may be left out, and
 *  then every corner is a corner.  A script that sweeps a line some
 *  other way.  A spline, a smoothing, anything it can compute, says so
 *  here and the whole build reads its answer. */
static int api_path_answer(lua_State *L)
{
    static V2    q[MAX_PTS];
    static float rad[MAX_PTS], tlim[MAX_PTS];
    int          n = (int)luaL_checkinteger(L, 5), i;
    luaL_checktype(L, 2, LUA_TTABLE);
    if (!rec_of(L, "path") || n < 2)
        return 0;
    if (n > MAX_PTS)
        n = MAX_PTS;
    for (i = 0; i < n; ++i)
    {
        lua_rawgeti(L, 2, i);
        q[i] = (V2){0.0f, 0.0f};
        if (lua_istable(L, -1))
        {
            q[i].x = api_field_num(L, "x", 0.0f);
            q[i].y = api_field_num(L, "y", 0.0f);
        }
        lua_pop(L, 1);
        rad[i] = tlim[i] = 0.0f;
        if (lua_istable(L, 3))
        {
            lua_rawgeti(L, 3, i);
            rad[i] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        if (lua_istable(L, 4))
        {
            lua_rawgeti(L, 4, i);
            tlim[i] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
    }
    lua_pushinteger(L, path_answer(q, rad, tlim, n));
    return 1;
}

static const luaL_Reg PATH[] = {
    {"corridor", api_path_corridor},
    {"answer",   api_path_answer  },
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

/*  THE FITS the walk read but did not run: every segment's path.  For a
 *  family whose runs may leave its own cells the two candidates and the
 *  choice between them. */
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
 *  `w:seg_classes()` steps every segment and answers how many there are.
 *  `w:seg_class(i)` hands over how many of one segment's tiles read as
 *  each class.  `w:seg_class_is` takes the class settled from them. */
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
    const int32_t *cells;
    int            cnt[3], nt, k;
    if (!world_of(L) || !net_seg_class_at((int)luaL_checkinteger(L, 2), cnt, &cells, &nt))
        return 0;
    lua_createtable(L, 0, 3);
    /*  How many of the segment's tiles wear each class, as a sequence
     *  the rule reads with ipairs. */
    lua_createtable(L, 3, 0);
    for (k = 0; k < 3; ++k)
    {
        lua_pushinteger(L, cnt[k]);
        lua_rawseti(L, -2, k + 1);
    }
    lua_setfield(L, -2, "classes");
    /*  And the tiles themselves, counted from nought with their length.
     *  So a rule may read the density and the neighborhood at them
     *  through arc.city rather than being told a tally and no more. */
    lua_createtable(L, nt, 0);
    for (k = 0; k < nt; ++k)
    {
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, cells[k] % R_MAP), lua_setfield(L, -2, "col");
        lua_pushinteger(L, cells[k] / R_MAP), lua_setfield(L, -2, "row");
        lua_rawseti(L, -2, k);
    }
    lua_setfield(L, -2, "cells");
    lua_pushinteger(L, nt), lua_setfield(L, -2, "n");
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
 *  junctions.  All of them, before any of its segments, so a leg knows
 *  whether it is signaled before it draws its crosswalk.  And
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
 *  false where the box lays no fill.  A thread junction, or a box
 *  reaching no chunk this build draws.  And the outline to lay it on
 *  otherwise.  `w:junction_box_done` takes up the margin round it, the
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
    (void)j;
    lua_pushboolean(L, 1);
    return 1;
}

/*  The connectors between the box's arms, taken from the pieces the
 *  drive cut, and the family's own drawing on the outline after them. */
static int api_world_junction_lanes(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_junction_lanes();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

/*  THE JUNCTION IN HAND AS ITS ARMS STAND, for the rule that lays the
 *  pattern its lanes make.  Nothing where there is no junction to
 *  pattern: the grading pass, or a box the family draws none for. */
static int s_turns_rec; /* the turns handle's record: the store is the lane model's */

static int api_world_junction_turns(lua_State *L)
{
    if (!world_of(L) || !lane_turns_ask())
        return 0;
    api_object_push(L, "turns", (void *)&s_turns_rec);
    return 1;
}

/*  THE THREAD JUNCTION IN HAND, for the rule that says which threads it
 *  carries.  Nothing where the box in hand is not a railway's. */
static int s_threads_rec; /* the threads handle's record: the store is the thread box's */

static int api_world_rail_threads(lua_State *L)
{
    if (!world_of(L) || !net_threads_ask())
        return 0;
    api_object_push(L, "threads", (void *)&s_threads_rec);
    return 1;
}

/*  And the threads the rule asked for, lofted from the pieces the drive
 *  cut for them.  Nothing where the box carries none. */
static int api_world_rail_threads_done(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushboolean(L, net_threads_done());
    return 1;
}

/*  THE PAVED BOX, in its two moments: the outline gathered for
 *  arc.rules.junction to lay the fill on.  Then the fill taken back and
 *  the box handed to the signs.  Each answers nothing where the family's
 *  junction is not a paved box, or where the pass composes none. */
static int api_world_box_paving(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushboolean(L, net_box_paving_ask());
    return 1;
}

static int api_world_box_paving_done(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushboolean(L, net_box_paving_done());
    return 1;
}

/*  THE JUNCTION'S ARMS, for the rule that stands its signs.  The emit
 *  window is opened at the box's order and closed by the drive. */
static int s_signs_rec;

static int api_world_junction_signs(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w || !net_junction_signs_ask())
        return 0;
    net_junction_signs_enter();
    script_emit_open(w->m, w->c, w->mask_bit, net_junction_signs_order());
    api_object_push(L, "signs", (void *)&s_signs_rec);
    return 1;
}

static int api_world_junction_signs_done(lua_State *L)
{
    if (!world_of(L))
        return 0;
    script_emit_close();
    net_junction_signs_leave();
    net_junction_signs_taken();
    return 0;
}

/*  THE DEAD ENDS of the segment in hand, for the rule that decides what
 *  a lane does where its segment stops.  Nothing where the segment has
 *  no dead end. */
static int s_caps_rec; /* the caps handle's record: the store is the lane model's */

static int api_world_segment_caps(lua_State *L)
{
    if (!world_of(L) || !lane_caps_ask())
        return 0;
    api_object_push(L, "caps", (void *)&s_caps_rec);
    return 1;
}

/*  THE SPUR IN HAND, for the rule that says where it aims on the line.
 *  Nothing where there is no spur being built. */
static int s_target_rec; /* the target handle's record: the store is the spur builder's */

static int api_world_spur_target(lua_State *L)
{
    if (!world_of(L) || !build_spur_target())
        return 0;
    api_object_push(L, "target", (void *)&s_target_rec);
    return 1;
}

/*  The outline the fill is laid on, once the margin round it has been
 *  answered: the fan is cut back to the margin's inner edge.  So there
 *  is nothing to lay before that. */
static int api_world_box_fill(lua_State *L)
{
    JuncFan *j;
    if (!world_of(L) || (j = net_junction_fan()) == NULL)
        return 0;
    api_object_push(L, "junction", j);
    return 1;
}

/*  And the strips the box gathered rather than drew: a thread junction's
 *  threads, each lofted as a segment's strip is. */
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

static int api_world_bands(lua_State *L)
{
    return world_nets(L, 2);
}

/*  THE LOFT'S STAGES, in the order it asks for them.  Each answers the
 *  name of the rule that settles it, and the thing that rule is handed.
 *  It answers nothing where a primitive of the pipeline's own settled
 *  it.  A family declares which of the two answers each stage. */
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
     *  rule: a slab's elevation rather than the strip. */
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
 *  the drive between them.  What the strip leaves for the passes that
 *  read it, and what stands beside it.  A line's margin is composed by
 *  the rule.  One family's lamps and another's signs come back as a
 *  list, and the pipeline places them.  Where each stands along the
 *  strip is the rule's, and the walk that turns a distance into a place
 *  is not. */
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
    net_strip_margin_drew(lua_toboolean(L, 2));
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
    /*  EVERY rule is handed the strip itself, and stands what it wants
     *  beside it.  The meets on the way and the ends the line runs on
     *  past are the strip's to answer, not a stage's to gather. */
    api_object_push(L, "strip", o);
    return 2;
}

/*  What a rule gathered for a stage that walks it.  A family whose props
 *  rule stands them ITSELF gathers nothing here: it has already drawn
 *  them through arc.put, and there is nothing to take back. */
/*  Nothing is taken back from a props rule: it has already stood what it
 *  wanted through the strip it was handed. */
static int api_world_loft_furniture_is(lua_State *L)
{
    (void)L;
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
 *  tuning window asks to see the fitted line over the world it made.
 *  `w:strip()` answers it while the slab is drawn at all: the grading
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

/*  THE SLAB BANDS, one at a time: the drive composes each slab between
 *  its loft and the lanes laid under it.  `w:band_band()` answers
 *  whether there was another. */
static int api_world_band_band(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_band_next();
    if (rc < 0)
        w->rc = rc;
    lua_pushboolean(L, rc > 0);
    return 1;
}

/*  THE SLAB'S OWN FIT, run the same way a segment's is: two candidates
 *  and the choice between them.  Then the band takes up the one that was
 *  kept.  A band the building pass replayed was never fitted, so it asks
 *  for none. */
/*  WHICH WAY A BAND IS WALKED from a cell that could start one.  There
 *  are four readings in all, so every one of them is settled before the
 *  walk begins. */
/*  HOW A SPUR'S FOOT MEETS ITS ROAD: eight readings, settled before any
 *  spur is read. */
/*  ---- WHERE EVERY TILE'S TOP COMES FROM ---------------------------------
 *
 *  Six places, and arc.rules.terrain says which of them each tile draws
 *  from.  It walks the map itself and answers three planes at once.
 *  They are the place, the tile whose PAD a footprint stands on, and the
 *  tile whose place in the painter's stack this one takes.  Nothing in
 *  the pipeline classifies a cell for it.
 *
 *  The handle resets the store.  A rule that answers nothing then leaves
 *  every tile on the field.  That is a city with no water, no pads and
 *  no shelves in it.  This is what taking the rule away should look
 *  like. */
static int          s_terrain_rec;
static const RCity *s_terrain_city;

static int api_world_terrain(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    mesh_tops_reset();
    s_terrain_city = w->c;
    api_object_push(L, "terrain", (void *)&s_terrain_rec);
    return 1;
}

/*  THE FIELD, once the tops are settled: a water tile's bed is clamped
 *  under the surface drawn over it.  So the rule has to have answered
 *  first, and it is the drive that says when. */
static int api_world_field(lua_State *L)
{
    if (world_of(L))
        mesh_field();
    return 0;
}

static int api_terrain_info(lua_State *L)
{
    if (!rec_of(L, "terrain"))
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, R_MAP), lua_setfield(L, -2, "size");
    lua_pushinteger(L, s_pass), lua_setfield(L, -2, "pass");
    lua_pushinteger(L, s_terrain_city ? city_corner_mask(s_terrain_city->rotation) : 0),
        lua_setfield(L, -2, "corner");
    return 1;
}

/*  What the CORRIDORS left on each tile.  This is the pipeline's own
 *  state and no reading of the city.  1 says the corridor wrote the
 *  tile's own shelf.  2 says it covers all four corners.  0 says
 *  neither.  Nothing before the building pass has any, so nothing is
 *  offered then. */
static int api_terrain_graded(lua_State *L)
{
    int32_t i;
    if (!rec_of(L, "terrain") || s_pass != 2)
        return 0;
    lua_createtable(L, R_MAP * R_MAP, 0);
    for (i = 0; i < R_MAP * R_MAP; ++i)
    {
        lua_pushinteger(L, mesh_top_graded(i));
        lua_rawseti(L, -2, i);
    }
    return 1;
}

/*  o:tops(place, anchor, order, vote, rest): five planes, each counted
 *  from nought.  A tile the place plane leaves out falls to the field.
 *  A tile the next two leave out stands on its own pad and takes its own
 *  place.  A tile the fourth leaves out votes for its corners with its
 *  own plane.  And a tile the last leaves out stands at rest where the
 *  pass composed it. */
static int api_terrain_tops(lua_State *L)
{
    int32_t i;
    if (!rec_of(L, "terrain") || !lua_istable(L, 2))
        return 0;
    for (i = 0; i < R_MAP * R_MAP; ++i)
    {
        int     top, vote = 0, rest;
        int32_t anchor = i, order = i;
        lua_rawgeti(L, 2, i);
        top = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        rest = top;
        if (lua_istable(L, 6))
        {
            lua_rawgeti(L, 6, i);
            if (lua_isnumber(L, -1))
                rest = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
        if (lua_istable(L, 5))
        {
            lua_rawgeti(L, 5, i);
            vote = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
        if (lua_istable(L, 3))
        {
            lua_rawgeti(L, 3, i);
            if (lua_isnumber(L, -1))
                anchor = (int32_t)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
        if (lua_istable(L, 4))
        {
            lua_rawgeti(L, 4, i);
            if (lua_isnumber(L, -1))
                order = (int32_t)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
        mesh_top_is(i, top, anchor, order, vote, rest);
    }
    lua_pushboolean(L, 1);
    return 1;
}

static const luaL_Reg TERRAIN[] = {
    {"info",   api_terrain_info  },
    {"graded", api_terrain_graded},
    {"tops",   api_terrain_tops  },
    {NULL,     NULL              }
};

/*  EVERY ON-SPUR TILE, for the rule that walks the map and reads them.
 *  The rule names each tile it finds and answers for it.  Nothing here
 *  looks for one. */
static int s_spurs_rec;

static int api_world_spurs(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w || !net_spurs_begin(w->c, w->l))
        return 0;
    api_object_push(L, "spurs", (void *)&s_spurs_rec);
    return 1;
}

/*  AND THE PAIRS whose tapers face each other, with the tiles between
 *  them for the rule to divide. */
static int api_world_spur_shares(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, net_spur_shares());
    return 1;
}

static int api_world_spur_share(lua_State *L)
{
    float gap;
    int   cap;
    if (!world_of(L) || !net_spur_share_at((int)luaL_checkinteger(L, 2), &gap, &cap))
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, (double)gap), lua_setfield(L, -2, "gap");
    lua_pushinteger(L, cap), lua_setfield(L, -2, "cap");
    return 1;
}

static int api_world_spur_share_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_spur_share_is((int)luaL_checkinteger(L, 2),
                      lua_isnumber(L, 3) ? (int)lua_tointeger(L, 3) : -1);
    return 0;
}

/*  AND WHERE EACH SPUR'S DESCENT RUNS along its slab. */
static int api_world_spur_spans(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, net_spur_spans());
    return 1;
}

static int api_world_spur_span(lua_State *L)
{
    float at;
    int   len, leaves, sgn;
    if (!world_of(L) || !net_spur_span_at((int)luaL_checkinteger(L, 2), &at, &len, &leaves, &sgn))
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, (double)at), lua_setfield(L, -2, "at");
    lua_pushinteger(L, len), lua_setfield(L, -2, "len");
    lua_pushboolean(L, leaves), lua_setfield(L, -2, "leaves");
    lua_pushinteger(L, sgn), lua_setfield(L, -2, "sgn");
    return 1;
}

static int api_world_spur_span_is(lua_State *L)
{
    int i = (int)luaL_checkinteger(L, 2);
    if (!world_of(L))
        return 0;
    if (!lua_istable(L, 3))
    {
        net_spur_span_is(i, 0, 0.0f, 0.0f, 0.0f, 0.0f);
        return 0;
    }
    lua_pushvalue(L, 3);
    net_spur_span_is(i, 1, api_field_num(L, "top", 0.0f), api_field_num(L, "foot", 0.0f),
                     api_field_num(L, "total", 0.0f), api_field_num(L, "along", 0.0f));
    lua_pop(L, 1);
    return 0;
}

/*  A junction's cycle: which phase each of the eight staggers starts at,
 *  and which group of arms an edge belongs to. */
static int api_world_signal_phase_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_signal_phase_is((int)luaL_checkinteger(L, 2), (float)luaL_optnumber(L, 3, 0.0));
    return 0;
}

static int api_world_signal_group_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_signal_group_is((int)luaL_checkinteger(L, 2), (float)luaL_optnumber(L, 3, 0.0));
    return 0;
}

/*  What class a tile's line is, for every traffic byte there is. */
static int api_world_road_class_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_line_class_is((int)luaL_checkinteger(L, 2), (int)luaL_optinteger(L, 3, 0));
    return 0;
}

/*  And how many cars one tile's traffic is worth. */
static int api_world_car_density_is(lua_State *L)
{
    if (!world_of(L))
        return 0;
    net_car_density_is((int)luaL_checkinteger(L, 2), (int)luaL_optinteger(L, 3, 0));
    return 0;
}


/*  THE POINTS THE SLAB'S FIT IS GIVEN, as the rule picks them from the
 *  band the walk read.
 *
 *      The straight cells'.
 *      A lone block's corner.
 *      Nothing of a staircase. */
static int api_world_band_chain(lua_State *L)
{
    StairFan *st;
    if (!world_of(L) || (st = net_hw_chain()) == NULL)
        return 0;
    lua_pushstring(L, "stair");
    api_object_push(L, "stair", st);
    return 2;
}

static int api_world_band_band_chained(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    build_band_chained();
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

/*  And the band drawn, from the pieces the drive cut for it. */
static int api_world_band_band_cut(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    if (build_band_cut() != 0)
        w->rc = -1;
    return 0;
}

static int api_world_band_band_fitted(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_band_fitted();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

static int api_world_band_band_done(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_band_done();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

/*  THE SPURS' STRIPS, one at a time, in the order the pass read them. */
/*  THE SPURS, one at a time.  Each is read up to the join the rule
 *  slides along the line's lane.  It is taken up again once the rule has
 *  answered. */
static int api_world_spur_next(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    lua_pushboolean(L, build_spur_next());
    return 1;
}

/*  The lanes within reach of the spur's end in hand, for the rule that
 *  picks one.  Nothing where there is no spur in hand. */
static int s_spur_pick; /* the snap handle's record: the store is the lane model's */

static int api_world_spur_pick(lua_State *L)
{
    if (!world_of(L) || lane_snap_count() < 1)
        return 0;
    api_object_push(L, "snap", (void *)&s_spur_pick);
    return 1;
}

/*  And the descent routed from the slab lane it picked, with the line
 *  end's lanes then measured in the same way. */
static int api_world_spur_routed(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushboolean(L, build_spur_routed());
    return 1;
}

/*  And the descent taken from the pieces the drive cut, with the line
 *  end's lanes then measured for the next pick. */
static int api_world_spur_joined(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushboolean(L, build_spur_joined());
    return 1;
}

/*  And the slide answered, with the legs the join falls back on queued
 *  for the drive to cut where no placing held. */
static int api_world_spur_slid(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushboolean(L, build_spur_slid());
    return 1;
}

static int api_world_spur_slide(lua_State *L)
{
    SlideFan *s;
    if (!world_of(L) || (s = net_spur_slide()) == NULL)
        return 0;
    api_object_push(L, "slide", s);
    return 1;
}

static int api_world_spur_done(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_spur_done();
    if (rc != 0)
        w->rc = rc;
    return 0;
}

static int api_world_spur_lofts(lua_State *L)
{
    if (!world_of(L))
        return 0;
    lua_pushinteger(L, build_spur_lofts());
    return 1;
}

static int api_world_spur_loft(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_spur_loft((int)luaL_checkinteger(L, 2));
    if (rc != 0)
        w->rc = rc;
    return 0;
}

static int api_world_band_spurs(lua_State *L)
{
    WorldFan *w = world_of(L);
    int       rc;
    if (!w)
        return 0;
    rc = build_band_spurs(w->m, w->c, w->l, w->mask_bit, !w->rotated);
    if (rc != 0)
        w->rc = rc;
    lua_pushboolean(L, rc == 0);
    return 1;
}

/*  ACROSS A LAP.  These are the lanes' open ends as the band pass left
 *  them.  The rule carries each of them on into the lane facing it.
 *  `w:band_links` then joins the bands' ends into the lines they become. */
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

/*  And the links laid on the pieces the drive cut for them. */
static int api_world_lane_cross_done(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    if (lane_cross_take() != 0)
        w->rc = -1;
    return 0;
}

/*  The band's lane ends, handed over for the script to join: which
 *  slab lane goes on to which is arc.rules.links's.  Nothing where the
 *  pass has none to join. */
static int api_world_band_links(lua_State *L)
{
    WorldFan *w = world_of(L);
    void     *f;
    if (!w || (f = build_band_links(w->m, w->c, w->mask_bit)) == NULL)
        return 0;
    api_object_push(L, "links", f);
    return 1;
}

/*  And what follows the joining: every lane end goes somewhere and every
 *  start has something arriving. */
static int api_world_band_links_done(lua_State *L)
{
    WorldFan *w = world_of(L);
    if (!w)
        return 0;
    if (build_band_links_done() != 0)
        w->rc = -1;
    return 0;
}

static const luaL_Reg WORLD[] = {
    {"info",     api_world_info    },
    {"wanted",   api_world_wanted  },
    {"tile",     api_world_tile    },
    {"zone",     api_world_zone    },
    {"shape",    api_world_shape   },
    {"loft",     api_world_loft    },
    {"net_discover", api_world_net_discover},
    {"net_cells",    api_world_net_cells},
    {"net_found",    api_world_net_found},
    {"hw_cells",     api_world_hw_cells},
    {"cuts",         api_world_cuts},
    {"cut",          api_world_cut},
    {"cut_done",     api_world_cut_done},
    {"gate_rest",      api_world_gate_rest},
    {"junction_bands", api_world_junction_bands},
    {"junction_band",  api_world_junction_band},
    {"junction_band_done", api_world_junction_band_done},
    {"junction_trim",  api_world_junction_trim},
    {"box_band",       api_world_box_band},
    {"junction_lanes", api_world_junction_lanes},
    {"junction_turns", api_world_junction_turns},
    {"node_threads", api_world_rail_threads},
    {"rail_threads_done", api_world_rail_threads_done},
    {"box_paving", api_world_box_paving},
    {"box_paving_done", api_world_box_paving_done},
    {"junction_signs", api_world_junction_signs},
    {"junction_signs_done", api_world_junction_signs_done},
    {"segment_caps", api_world_segment_caps},
    {"spur_target", api_world_spur_target},
    {"box_fill",    api_world_box_fill},
    {"box_band_done",  api_world_box_band_done},
    {"mouth",          api_world_mouth},
    {"mouth_is",       api_world_mouth_is},
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
    {"lap",        api_world_lap       },
    {"lap_frame", api_world_lap_frame},
    {"lap_panel", api_world_lap_panel},
    {"lap_approach", api_world_lap_approach},
    {"lap_mark", api_world_lap_mark},
    {"lap_approaches", api_world_lap_approaches},
    {"margins", api_world_margins},
    {"margin",  api_world_margin },
    {"emitted",  api_world_emitted },
    {"lanes",    api_world_lanes   },
    {"lane_runs", api_world_lane_runs},
    {"lane_run",  api_world_lane_run },
    {"lane_run_is", api_world_lane_run_is},
    {"traffic_runs", api_world_traffic_runs},
    {"traffic_run",  api_world_traffic_run },
    {"traffic_run_is", api_world_traffic_run_is},
    {"flies_runs", api_world_flies_runs},
    {"flies_run",  api_world_flies_run },
    {"flies_run_is", api_world_flies_run_is},
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
    {"bands", api_world_bands},
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
    {"band_band", api_world_band_band},
    {"car_density_is", api_world_car_density_is},
    {"road_class_is", api_world_road_class_is},
    {"signal_phase_is", api_world_signal_phase_is},
    {"signal_group_is", api_world_signal_group_is},
    {"terrain",  api_world_terrain },
    {"field",    api_world_field   },
    {"spurs",    api_world_spurs   },
    {"spur_shares", api_world_spur_shares},
    {"spur_share",  api_world_spur_share },
    {"spur_share_is", api_world_spur_share_is},
    {"spur_spans", api_world_spur_spans},
    {"spur_span",  api_world_spur_span },
    {"spur_span_is", api_world_spur_span_is},
    {"band_chain", api_world_band_chain},
    {"band_band_chained", api_world_band_band_chained},
    {"hw_fits",  api_world_hw_fits },
    {"hw_fit",   api_world_hw_fit  },
    {"hw_fit_done", api_world_hw_fit_done},
    {"hw_fit_choice", api_world_hw_fit_choice},
    {"hw_fit_choice_is", api_world_hw_fit_choice_is},
    {"band_band_fitted", api_world_band_band_fitted},
    {"band_band_cut", api_world_band_band_cut},
    {"band_band_done", api_world_band_band_done},
    {"band_spurs", api_world_band_spurs},
    {"spur_next", api_world_spur_next},
    {"spur_pick",  api_world_spur_pick},
    {"spur_routed", api_world_spur_routed},
    {"spur_joined", api_world_spur_joined},
    {"spur_slide", api_world_spur_slide},
    {"spur_slid", api_world_spur_slid},
    {"spur_done", api_world_spur_done},
    {"spur_lofts", api_world_spur_lofts},
    {"spur_loft",  api_world_spur_loft },
    {"wires",    api_world_wires   },
    {"wire",     api_world_wire    },
    {"wire_done", api_world_wire_done},
    {"lane_cross", api_world_lane_cross},
    {"lane_cross_done", api_world_lane_cross_done},
    {"band_links", api_world_band_links},
    {"band_links_done", api_world_band_links_done},
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

/*  ---- a margin ----------------------------------------------------- */

/*  The tile's own methods live in api_tile.c, which knows the ground.
 *  The handle is the same one every other kind uses. */
TileFan *api_tile_of(lua_State *L)
{
    ScriptObj *o = obj_check(L);
    return o && strcmp(o->kind, "tile") == 0 ? (TileFan *)o->rec : NULL;
}

const luaL_Reg *api_tile_methods(int *n);

static WalkFan *walk_of(lua_State *L)
{
    return (WalkFan *)rec_of(L, "margin");
}

/*  What the band IS.
 *
 *      Which kind it is.
 *      How many stations the network holds for it.
 *      How wide it is.
 *      Where it sits in the stack.
 *      Whether it lies on the ground.
 *      And.
 *      For a meet.
 *      The depth it asked for.
 *
 *  This the material paints the stop line against. */
static int api_margin_info(lua_State *L)
{
    WalkFan        *f = walk_of(L);
    const WalkPath *w;
    if (!f)
        return 0;
    w = (const WalkPath *)f->w;
    lua_newtable(L);
    lua_pushstring(L, w->kind == WALK_SIDE ? "side" : w->kind == WALK_CORNER ? "corner"
                                                  : w->kind == WALK_CROSS  ? "meet"
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

static int api_margin_count(lua_State *L)
{
    WalkFan *f = walk_of(L);
    if (!f)
        return 0;
    lua_pushinteger(L, ((const WalkPath *)f->w)->nst);
    return 1;
}

/*  One cross-section: the band's outer edge, its inner one and the
 *  height the network recorded there. */
static int api_margin_at(lua_State *L)
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
static int api_margin_quad(lua_State *L)
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

/*  The two ends of the band, and the ground under each: what the network
 *  joins it to its neighbors by.  What a join is drawn between when the
 *  band itself has no line down it. */
static int api_margin_ends(lua_State *L)
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

/*  A hairline of the network.  It is a thin quad in the vehicle
 *  material, and its across carries the paint.  It sits a hair above the
 *  ground, so it reads over whatever it crosses. */
static int api_margin_wire(lua_State *L)
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

static const luaL_Reg MARGIN[] = {
    {"ends",  api_margin_ends },
    {"wire",  api_margin_wire },
    {"info",  api_margin_info },
    {"count", api_margin_count},
    {"at",    api_margin_at   },
    {"quad",  api_margin_quad },
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
    {"spur",  FLD_BOOL, offsetof(LaneFan, spur)},
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

/*  The surface the hairline floats over: the slab's own where the line
 *  belongs to a band, the ground elsewhere. */
static int api_lane_height(lua_State *L)
{
    LaneFan *f = lane_of(L);
    V2       p;
    if (!f)
        return 0;
    p.x = (float)luaL_checknumber(L, 2), p.y = (float)luaL_checknumber(L, 3);
    if (f->spur)
    {
        /*  A spur's lane climbs to the slab: the ease is the same one
         *  the spur's own strip is lifted by, so the two agree. */
        lua_pushnumber(L, surface_at_world(f->c, f->mask_bit, p.x, p.y) +
                              BAND_LIFT * ease_smooth((float)luaL_checknumber(L, 4)));
        return 1;
    }
    lua_pushnumber(L, f->band > 0 ? slab_z_near(f->c, f->mask_bit, f->band, p)
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

/*  ---- a level meet's panel -------------------------------------- */

static LapFan *lap_of(lua_State *L)
{
    return (LapFan *)rec_of(L, "panel");
}

static const Field PANEL_FIELDS[] = {
    {"order", FLD_NUM,  offsetof(LapFan, order)},
    {"lift",  FLD_NUM,  offsetof(LapFan, lift)},
    {"slot",  FLD_NUM,  offsetof(LapFan, slot)},
    {NULL, FLD_NUM, 0}
};

static int api_panel_info(lua_State *L)
{
    return api_fields(L, lap_of(L), PANEL_FIELDS);
}

/*  Corner k of the panel, 1 to 4, and the ground under it. */
static int api_panel_at(lua_State *L)
{
    LapFan *f = lap_of(L);
    int      k = (int)luaL_checkinteger(L, 2);
    if (!f || k < 0 || k > 3)
        return 0;
    lua_pushnumber(L, f->q[k][0]);
    lua_pushnumber(L, f->q[k][1]);
    lua_pushnumber(L, f->ground[k]);
    return 3;
}

/*  The panel itself.  It is one quad over the line it interrupts.  Its
 *  heights come from the higher of each end's two corners.  So that on a
 *  tile tilting across the line it stands on the ground and not under
 *  it. */
static int api_panel_quad(lua_State *L)
{
    LapFan *f = lap_of(L);
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

static const luaL_Reg LAP[] = {
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

/*  The numbers the junction is sized by.  They are its middle, and the
 *  half width of the band that runs through it.  They also say how far
 *  out a corner may stand.  How much the family has been let out from
 *  the width it was tuned at.  The cap on how far an arm may be cut back
 *  for the junction's sake. */
static const Field OUTLINE_FIELDS[] = {
    {"col",   FLD_INT,  offsetof(OutlineFan, col)},
    {"row",   FLD_INT,  offsetof(OutlineFan, row)},
    {"x",     FLD_NUM,  offsetof(OutlineFan, cx)},
    {"y",     FLD_NUM,  offsetof(OutlineFan, cy)},
    {"half",  FLD_NUM,  offsetof(OutlineFan, w)},
    {"far",   FLD_NUM,  offsetof(OutlineFan, far)},
    {"grow",  FLD_NUM,  offsetof(OutlineFan, gro)},
    {"cap",   FLD_NUM,  offsetof(OutlineFan, cap)},
    {"lips", FLD_BOOL, offsetof(OutlineFan, lips)},
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
 *  junction.  The arm it is the mouth of, or -1 for none. */
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

/*  One point of the ring moved in by the margin's width. */
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

/*  Vertex k: where it is, and the tangent length it was built with: a
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

/*  The corridor sweep.  It is the largest radius a fillet at this corner
 *  may have and still hold inside the band.  It also says whether that
 *  radius came out under the minimum.  The sampling is the pipeline's.
 *  What to do with the answer is not. */
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
 *  pattern: its length, its period.  Which steps are its majority and
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
 *  free line 2.  A straight is its own steps and needs no asking. */
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

/*  ---- the meet of two lines ---------------------------------------- */

static JoinFan *meet_of(lua_State *L)
{
    return (JoinFan *)rec_of(L, "meet");
}

/*  How the meet sits.
 *
 *      How far past the line behind it is.
 *      How far short of the line ahead.
 *      How much line there is either side.
 *
 *  Whether a free line is involved or the chain's own end. */
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

/*  Does the leg's extension to the meet hold on the corridor? */
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

/*  The radius the corridor allows at the meet, given the tangent it may
 *  take.  It also says whether the straights that reach an arc of that
 *  radius hold. */
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

/*  Put the vertex at the meet. */
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

/*  The line either side of the pair, and what the vertex behind was
 *  built with.  It also gives how far the two lines' ends lie apart, and
 *  which of the two lines is the chain's own end. */
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
 *  each line.  It holds the biarc between them, and the edge either side
 *  of it.  Nothing where there is no biarc at all. */
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

/*  How many of the chain's own points lie between the two lines.  It
 *  also says whether each line is a run rather than the chain's own end
 *  line, and how wide the band is. */
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
 *  has been given, the tangent of half its turn.  Which is what turns a
 *  tangent length into a radius.  And the widths it is held between. */
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

/*  ---- THE TURN THE WORLD IS HANDED ------------------------------------
 *
 *  The handle `arc.rules.frame` holds, and the only one C ever hands
 *  over of itself.  A turn is a BUILD or a MOVE: a build hands out its
 *  passes one at a time, each a `world` of its own.  A move hands out
 *  the world that moves.  What a turn does with either is the script's. */
static int api_frame_info(lua_State *L)
{
    if (!rec_of(L, "frame"))
        return 0;
    lua_newtable(L);
    lua_pushboolean(L, net_drive_what() == DRIVE_BUILD), lua_setfield(L, -2, "build");
    lua_pushboolean(L, net_drive_what() == DRIVE_MOVE), lua_setfield(L, -2, "moving");
    return 1;
}

/*  The next pass of the build, set up and ready to compose, or nothing
 *  when it has none left. */
static int api_frame_pass(lua_State *L)
{
    void *w;
    if (!rec_of(L, "frame") || net_drive_what() != DRIVE_BUILD)
        return 0;
    if ((w = mesh_build_pass_next()) == NULL)
        return 0;
    api_object_push(L, "world", w);
    return 1;
}

/*  And the world that moves. */
static int api_frame_moving(lua_State *L)
{
    void *b;
    if (!rec_of(L, "frame") || net_drive_what() != DRIVE_MOVE)
        return 0;
    if ((b = net_moving_fan()) == NULL)
        return 0;
    api_object_push(L, "moving", b);
    return 1;
}

static const luaL_Reg FRAME[] = {
    {"info",   api_frame_info  },
    {"pass",   api_frame_pass  },
    {"moving", api_frame_moving},
    {NULL,     NULL            }
};

/*  ---- the network a family's cells make -------------------------------
 *
 *  What the script discovers the network off, and what it hands back.
 *  `plane` is the reading.  It is three arrays a cell, counted from
 *  nought.  They are the links a cell RETURNS (a link both sides agree
 *  on), the links its own art claims.  What kind of node it is, 0 none,
 *  1 an end, 2 a junction.  `segment` and `island` are the answer. */

static NetDiscFan *disc_of(lua_State *L)
{
    return (NetDiscFan *)rec_of(L, "network");
}

static int api_network_info(lua_State *L)
{
    NetDiscFan *d = disc_of(L);
    if (!d)
        return 0;
    lua_newtable(L);
    lua_pushstring(L, d->family), lua_setfield(L, -2, "family");
    lua_pushinteger(L, d->fk), lua_setfield(L, -2, "walked");
    lua_pushinteger(L, R_MAP), lua_setfield(L, -2, "size");
    /*  The two the store cannot go past: how many cells one run may hold
     *  and how many steps a walk may take.  A script that walks the map
     *  reads its own limits rather than carrying a copy of them. */
    lua_pushinteger(L, MAX_PTS), lua_setfield(L, -2, "max_cells");
    lua_pushinteger(L, 4096), lua_setfield(L, -2, "max_steps");
    return 1;
}

static void plane_push(lua_State *L, const uint8_t *p)
{
    int32_t i;
    lua_createtable(L, R_MAP * R_MAP, 0);
    for (i = 0; i < R_MAP * R_MAP; ++i)
    {
        lua_pushinteger(L, p[i]);
        lua_rawseti(L, -2, i);
    }
}

static int api_network_plane(lua_State *L)
{
    NetDiscFan *d = disc_of(L);
    if (!d)
        return 0;
    plane_push(L, d->links);
    plane_push(L, d->art);
    return 2;
}

/*  o:nodes(plane): what counts as a node, a cell each, counted from
 *  nought: 0 none, 1 an end, 2 a junction.  Where a segment ends is
 *  where the next node begins, so this is the same answer as the runs
 *  and is given with them.  The whole pipeline reads it after. */
static int api_network_nodes(lua_State *L)
{
    static uint8_t plane[R_MAP * R_MAP];
    NetDiscFan    *d = disc_of(L);
    int32_t        i;
    luaL_checktype(L, 2, LUA_TTABLE);
    if (!d)
        return 0;
    for (i = 0; i < R_MAP * R_MAP; ++i)
    {
        lua_rawgeti(L, 2, i);
        plane[i] = (uint8_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    net_disc_nodes_set(d->f, plane);
    return 0;
}

/*  Why a run ended, by the name the script calls it.  A run that reached
 *  a node.  One that left the map.  One that outgrew a guard, one whose
 *  next cell did not return the link.  One that came round to where it
 *  started are five different shapes downstream. */
static int stop_code(const char *s)
{
    return !s                        ? -1
           : strcmp(s, "node") == 0  ? NET_STOP_NODE
           : strcmp(s, "edge") == 0  ? NET_STOP_EDGE
           : strcmp(s, "cut") == 0   ? NET_STOP_CUT
           : strcmp(s, "stuck") == 0 ? NET_STOP_STUCK
           : strcmp(s, "loop") == 0  ? NET_STOP_LOOP
                                     : -1;
}

/*  o:segment(cells, n, stop, exit): one run of the network.  `cells`
 *  counts from NOUGHT and its length comes with it, as every table the
 *  script hands back does.  Each entry is row * size + col. */
static int api_network_segment(lua_State *L)
{
    static int32_t cells[MAX_PTS];
    NetDiscFan    *d    = disc_of(L);
    int            n    = (int)luaL_checkinteger(L, 3);
    int            stop = stop_code(luaL_checkstring(L, 4));
    int            exit = (int)luaL_optinteger(L, 5, -1);
    int            i;
    luaL_checktype(L, 2, LUA_TTABLE);
    if (!d || n < 1 || stop < 0)
        return 0;
    if (n > MAX_PTS)
        n = MAX_PTS;
    for (i = 0; i < n; ++i)
    {
        lua_rawgeti(L, 2, i);
        cells[i] = (int32_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    if (!net_disc_run_add(d->fk, cells, n, stop, exit))
        d->full = 1;
    return 0;
}

/*  o:island(cell, edge): a piece no run reaches.  `edge` marks the kind
 *  whose links all leave the map, which is drawn only where no run
 *  already covered the tile. */
static int api_network_island(lua_State *L)
{
    NetDiscFan *d = disc_of(L);
    if (!d)
        return 0;
    if (!net_disc_island_add(d->fk, (int32_t)luaL_checkinteger(L, 2), lua_toboolean(L, 3)))
        d->full = 1;
    return 0;
}

/*  o:junction(cell): a cell where the ways meet.  Its control, its
 *  outline, the trims it hands its arms and the box it draws all step
 *  through this list.  So a script that decides what a junction is
 *  decides all four. */
static int api_network_junction(lua_State *L)
{
    NetDiscFan *d = disc_of(L);
    if (!d)
        return 0;
    if (!net_disc_junction_add(d->fk, (int32_t)luaL_checkinteger(L, 2)))
        d->full = 1;
    return 0;
}

static const luaL_Reg NETWORK[] = {
    {"info",    api_network_info   },
    {"plane",   api_network_plane  },
    {"nodes",   api_network_nodes  },
    {"segment", api_network_segment},
    {"island",  api_network_island },
    {"junction", api_network_junction},
    {NULL,      NULL               }
};

/*  ---- the band's bands ----------------------------------------------
 *
 *  The same question as a line segment's, asked of a different network.
 *  `plane` is the reading: which cell of a pair is its primary and which
 *  `band` is the answer.  One run of entries, each a cell of the band or
 *  a block it turns through. */

static HwDiscFan *bands_of(lua_State *L)
{
    return (HwDiscFan *)rec_of(L, "bands");
}

static int api_bands_info(lua_State *L)
{
    if (!bands_of(L))
        return 0;
    lua_newtable(L);
    lua_pushinteger(L, R_MAP), lua_setfield(L, -2, "size");
    lua_pushinteger(L, MAX_PTS), lua_setfield(L, -2, "max_cells");
    return 1;
}

/*  o:band(entries, n, col, row, ew, sign): one band.  `entries` counts
 *  from nought and its length comes with it.  Each is {cell = , block =
 *  , ew = }.  The cell it was started from and the way it was walked
 *  make its key in the segment table across builds. */
static int api_bands_band(lua_State *L)
{
    static HwRun run;
    HwDiscFan   *d = bands_of(L);
    int          n = (int)luaL_checkinteger(L, 3), i;
    int32_t      cell;
    luaL_checktype(L, 2, LUA_TTABLE);
    if (!d || n < 1)
        return 0;
    if (n > MAX_PTS)
        n = MAX_PTS;
    run.n = n;
    for (i = 0; i < n; ++i)
    {
        run.cell[i] = 0, run.block[i] = 0, run.ew[i] = 0;
        lua_rawgeti(L, 2, i);
        if (lua_istable(L, -1))
        {
            run.cell[i] = (int32_t)api_field_num(L, "cell", 0.0f);
            lua_getfield(L, -1, "block");
            run.block[i] = (uint8_t)lua_toboolean(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, -1, "ew");
            run.ew[i] = (uint8_t)lua_toboolean(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    cell = (int32_t)luaL_checkinteger(L, 4);
    if (!net_hw_disc_add(&run, cell % R_MAP, cell / R_MAP,
                         lua_toboolean(L, 5), (int)luaL_checkinteger(L, 6)))
        d->full = 1;
    return 0;
}

static const luaL_Reg BANDS[] = {
    {"info", api_bands_info},
    {"band", api_bands_band},
    {NULL,    NULL           }
};

/*  ---- the beat the moving world runs on ---------------------------------
 *
 *  The build has a drive and so does the beat: this is what the script
 *  that drives one holds.  `gates` and `cars` say how much there is to
 *  do, `gate` and `car` what each of them sees.  `gate_is` and `car_is`
 *  are the answer: the angle a gate has swung to and the speed a car
 *  leaves at.
 *
 *  A car is read and moved before the next is read.  A car looks at the
 *  one ahead of it, which has already moved this beat. */
static int api_moving_info(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    lua_newtable(L);
    /*  How many beats the clock owes, whether the frame wants what
     *  moves drawn, and how many gates there are to swing. */
    lua_pushinteger(L, net_beat_owed()), lua_setfield(L, -2, "beats");
    lua_pushboolean(L, net_beat_draws()), lua_setfield(L, -2, "draw");
    lua_pushinteger(L, net_beat_gates()), lua_setfield(L, -2, "gates");
    return 1;
}

/*  One beat begun: the trains run on their own rails, and what follows,
 *  the gates and the cars, is the script's. */
static int api_moving_run(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    lua_pushboolean(L, net_beat_run());
    return 1;
}

/*  THE ARMS AT THE THREAD NODE the beat stopped at, for the rule that
 *  chooses between them.  It gives the heading the train arrived on, and
 *  each arm's heading away from the node.  Nothing where the beat did
 *  not stop. */
static int api_moving_arms(lua_State *L)
{
    int   n, k;
    float hx, hy, dx, dy;
    if (!rec_of(L, "moving") || !net_beat_arms(&n, &hx, &hy))
        return 0;
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, hx), lua_setfield(L, -2, "hx");
    lua_pushnumber(L, hy), lua_setfield(L, -2, "hy");
    lua_createtable(L, n, 0);
    for (k = 0; k < n; ++k)
    {
        if (!net_beat_arm_at(k, &dx, &dy))
            continue;
        lua_createtable(L, 0, 2);
        lua_pushnumber(L, dx), lua_setfield(L, -2, "dx");
        lua_pushnumber(L, dy), lua_setfield(L, -2, "dy");
        lua_rawseti(L, -2, k + 1);
    }
    lua_setfield(L, -2, "arms");
    return 1;
}

/*  THE THREAD SIGNALS: how many, how near the nearest car is each way
 *  along the block one governs, and the aspect the rule gives it. */
static int api_moving_signals(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    lua_pushinteger(L, net_signals());
    return 1;
}

static int api_moving_signal(lua_State *L)
{
    float ahead, back;
    if (!rec_of(L, "moving") || !net_signal_at((int)luaL_checkinteger(L, 2), &ahead, &back))
        return 0;
    lua_createtable(L, 0, 2);
    lua_pushnumber(L, ahead), lua_setfield(L, -2, "ahead");
    lua_pushnumber(L, back), lua_setfield(L, -2, "back");
    return 1;
}

/*  The aspect one signal shows: the MODEL the rule named, or nothing for
 *  a signal that shows none. */
static int api_moving_signal_is(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    net_signal_is((int)luaL_checkinteger(L, 2), lua_tostring(L, 3));
    return 0;
}

/*  And the arm the rule chose. */
static int api_moving_arm_is(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    net_beat_arm_is(lua_isnumber(L, 2) ? (int)lua_tointeger(L, 2) : -1);
    return 0;
}

/*  And the geometry of everything that moves, where the frame asked for
 *  it: the cars, the trains and the signals' aspects. */
static int api_moving_build(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    lua_pushboolean(L, net_beat_build() == 0);
    return 1;
}

static int api_moving_gate(lua_State *L)
{
    float angle, near, dt;
    if (!rec_of(L, "moving") || !net_beat_gate((int)luaL_checkinteger(L, 2), &angle, &near, &dt))
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, angle), lua_setfield(L, -2, "angle");
    lua_pushnumber(L, near), lua_setfield(L, -2, "near");
    lua_pushnumber(L, dt), lua_setfield(L, -2, "dt");
    return 1;
}

static int api_moving_gate_is(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    if (lua_isnumber(L, 3))
        net_beat_gate_is((int)luaL_checkinteger(L, 2), (float)lua_tonumber(L, 3));
    return 0;
}

/*  The cars, put in the order they are read in.  Asked once, after the
 *  gates have swung: sorting them is what the order depends on. */
static int api_moving_cars(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    lua_pushinteger(L, net_beat_cars());
    return 1;
}

/*  Car i.  It holds its own speed, and the gap to the car ahead of it in
 *  its lane.  It also holds the control at the end it runs towards, and
 *  every lap on its way.  Nothing at all for a car with no decision to
 *  make, one meet a junction box.  Off the network, which has already
 *  been moved. */
static int api_moving_car(lua_State *L)
{
    float speed, gap, ahead, stop, free_, line, creep, step;
    int   have_gap, have_ctrl, held, nx, k;
    if (!rec_of(L, "moving") || !net_beat_car((int)luaL_checkinteger(L, 2)))
        return 0;
    if (!net_beat_reading(&speed, &have_gap, &gap, &have_ctrl, &ahead, &held, &nx,
                          &stop, &free_, &line, &creep, &step))
        return 0;
    lua_createtable(L, 0, 10);
    lua_pushnumber(L, speed), lua_setfield(L, -2, "speed");
    lua_pushnumber(L, stop), lua_setfield(L, -2, "stop");
    lua_pushnumber(L, free_), lua_setfield(L, -2, "free");
    lua_pushnumber(L, line), lua_setfield(L, -2, "line");
    lua_pushnumber(L, creep), lua_setfield(L, -2, "creep");
    lua_pushnumber(L, step), lua_setfield(L, -2, "step");
    if (have_gap)
        lua_pushnumber(L, gap), lua_setfield(L, -2, "gap");
    if (have_ctrl)
    {
        int32_t sc, sr;
        int     sk;
        float   shx, shy, st;
        lua_pushnumber(L, ahead), lua_setfield(L, -2, "ahead");
        lua_pushboolean(L, held), lua_setfield(L, -2, "held");
        /*  Where the control is a SIGNAL, its facts rather than an
         *  answer: whether it reads red is arc.rules.signal's. */
        if (net_beat_signal(&sc, &sr, &shx, &shy, &st, &sk))
        {
            lua_createtable(L, 0, 5);
            lua_pushinteger(L, sc), lua_setfield(L, -2, "col");
            lua_pushinteger(L, sr), lua_setfield(L, -2, "row");
            lua_pushnumber(L, shx), lua_setfield(L, -2, "hx");
            lua_pushnumber(L, shy), lua_setfield(L, -2, "hy");
            lua_pushnumber(L, st), lua_setfield(L, -2, "t");
            lua_pushinteger(L, sk), lua_setfield(L, -2, "k");
            lua_setfield(L, -2, "signal");
        }
    }
    lua_createtable(L, nx, 0);
    for (k = 0; k < nx; ++k)
    {
        lua_pushnumber(L, net_beat_lap(k));
        lua_rawseti(L, -2, k + 1);
    }
    lua_setfield(L, -2, "meets");
    /*  And the arms at the junction it is a beat away from.  There there
     *  is one to choose: the draw the world made for it, and each arm's
     *  heading away from the node. */
    {
        int      n;
        unsigned draw;
        if (net_beat_turn(&n, &draw))
        {
            float dx, dy;
            lua_pushinteger(L, (lua_Integer)draw), lua_setfield(L, -2, "draw");
            lua_createtable(L, n, 0);
            for (k = 0; k < n; ++k)
            {
                if (!net_beat_turn_at(k, &dx, &dy))
                    continue;
                lua_createtable(L, 0, 2);
                lua_pushnumber(L, dx), lua_setfield(L, -2, "dx");
                lua_pushnumber(L, dy), lua_setfield(L, -2, "dy");
                lua_rawseti(L, -2, k + 1);
            }
            lua_setfield(L, -2, "arms");
        }
    }
    return 1;
}

/*  The arm the rule chose for the car in hand. */
static int api_moving_car_turn_is(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    net_beat_turn_is(lua_isnumber(L, 2) ? (int)lua_tointeger(L, 2) : -1);
    return 0;
}

static int api_moving_car_is(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    net_beat_car_is((int)luaL_checkinteger(L, 2), (float)luaL_checknumber(L, 3));
    return 0;
}

/*  ---- and the moving world DRAWN ---------------------------------------
 *
 *  A gate's moving parts, its flashers and the striped arm swung about
 *  the mechanism's shaft, are a rule.  So the drive lays them: it asks
 *  for each arm's place, the script draws through arc.put, and the
 *  window is closed again.  Nothing in the moving world reaches up. */
static int api_moving_gates_drawn(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    lua_pushinteger(L, net_movers_gates());
    return 1;
}

static int api_moving_gate_prop(lua_State *L)
{
    ScriptProp  at;
    const void *c;
    void       *m;
    uint8_t     mask_bit;
    float       x, y, fx, fy, angle, len, order;
    if (!rec_of(L, "moving") || !net_movers_gate((int)luaL_checkinteger(L, 2), &x, &y, &fx, &fy, &angle, &len, &order))
        return 0;
    if ((m = net_movers_mesh(&c, &mask_bit)) == NULL)
        return 0;
    memset(&at, 0, sizeof at);
    at.col = (int)floorf(x), at.row = (int)floorf(y), at.arm = -1, at.links = 0;
    at.x = x, at.y = y;
    at.z = surface_at_world((const RCity *)c, mask_bit, x, y);
    at.fx = -fx, at.fy = -fy;
    at.size  = 0.0f;
    at.phase = (float)(((int)(x * 3.0f) + (int)(y * 5.0f)) & 7) / 8.0f;
    at.angle = angle, at.len = len;
    script_emit_open(m, c, mask_bit, order);
    api_prop_push(L, &at);
    return 1;
}

static int api_moving_gate_drawn(lua_State *L)
{
    if (!rec_of(L, "moving"))
        return 0;
    script_emit_close();
    return 0;
}

static const luaL_Reg MOVING[] = {
    {"gates_drawn", api_moving_gates_drawn},
    {"gate_prop",   api_moving_gate_prop  },
    {"gate_drawn",  api_moving_gate_drawn },
    {"info",    api_moving_info   },
    {"run",     api_moving_run    },
    {"build",   api_moving_build  },
    {"gate",    api_moving_gate   },
    {"gate_is", api_moving_gate_is},
    {"cars",    api_moving_cars   },
    {"car",     api_moving_car    },
    {"car_turn_is", api_moving_car_turn_is},
    {"arms",    api_moving_arms   },
    {"arm_is",  api_moving_arm_is },
    {"signals", api_moving_signals},
    {"signal",  api_moving_signal },
    {"signal_is", api_moving_signal_is},
    {"car_is",  api_moving_car_is },
    {NULL,      NULL            }
};

/*  ---- the lanes within reach of a spur's end ----------------------------
 *
 *  Which lane a spur fastens to.  At the top it is the nearest of its
 *  own band's slab lanes.  At the foot it is the lip-side lane of the
 *  line, or the turn it lands on inside a junction: is
 *  arc.rules.spur_lane's.  The candidates are measured for it: one per
 *  PIECE of every lane within reach.  This is because a lane's nearest
 *  station may run the wrong way where one further along runs the right
 *  way. */
static int api_snap_info(lua_State *L)
{
    const char *what;
    int         band;
    if (!rec_of(L, "snap"))
        return 0;
    net_spur_snap_what(&what, &band);
    lua_newtable(L);
    lua_pushinteger(L, lane_snap_count()), lua_setfield(L, -2, "n");
    lua_pushstring(L, what), lua_setfield(L, -2, "what");
    lua_pushinteger(L, band), lua_setfield(L, -2, "band");
    lua_pushnumber(L, net_line_rules()->spur_snap), lua_setfield(L, -2, "reach");
    lua_pushnumber(L, net_line_rules()->lane_pick_dot), lua_setfield(L, -2, "dot");
    return 1;
}

/*  One candidate.  It gives the lane, what it is, and the band it
 *  belongs to.  It also gives how far off its centerline it lies.  It
 *  gives how far away that station is, and how nearly it runs the way
 *  the spur does. */
static int api_snap_at(lua_State *L)
{
    int   lane, cls, band;
    float off, dist, dot, x, y, dx, dy;
    if (!rec_of(L, "snap") ||
        !lane_snap_at((int)luaL_checkinteger(L, 2), &lane, &cls, &band, &off, &dist, &dot, &x, &y, &dx, &dy))
        return 0;
    lua_createtable(L, 0, 9);
    lua_pushinteger(L, lane), lua_setfield(L, -2, "lane");
    lua_pushboolean(L, cls == 2), lua_setfield(L, -2, "slab");
    lua_pushboolean(L, cls == 0), lua_setfield(L, -2, "line");
    lua_pushboolean(L, cls == 1), lua_setfield(L, -2, "turn");
    lua_pushinteger(L, band), lua_setfield(L, -2, "band");
    lua_pushnumber(L, off), lua_setfield(L, -2, "off");
    lua_pushnumber(L, dist), lua_setfield(L, -2, "dist");
    lua_pushnumber(L, dot), lua_setfield(L, -2, "dot");
    return 1;
}

/*  And the pick, or nothing for an end that fastens to no lane. */
static int api_snap_is(lua_State *L)
{
    if (!rec_of(L, "snap"))
        return 0;
    lane_snap_is(lua_isnumber(L, 2) ? (int)lua_tointeger(L, 2) : -1);
    return 0;
}

static const luaL_Reg SNAP[] = {
    {"info", api_snap_info},
    {"at",   api_snap_at  },
    {"is",   api_snap_is  },
    {NULL,   NULL         }
};

/*  ---- the arms of a junction ---------------------------------------------
 *
 *  THE PATTERN AN INTERSECTION DRAWS is arc.rules.turns's: which arm's
 *  inbound lane joins which arm's outbound lane.  The arms are offered as
 *  the ports stage measured them, and each pair the rule wants is routed
 *  and queued for the drive to cut.  There is no matcher in C behind it:
 *  a rule that wants no pair draws a junction whose arms meet nothing. */
static int api_turns_info(lua_State *L)
{
    const char *fam;
    int         col, row, arms;
    if (!rec_of(L, "turns"))
        return 0;
    lane_turns_info(&col, &row, &arms, &fam);
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, row), lua_setfield(L, -2, "row");
    lua_pushinteger(L, arms), lua_setfield(L, -2, "arms");
    lua_pushstring(L, fam), lua_setfield(L, -2, "family");
    return 1;
}

/*  One arm: the lanes it carries, whether a lane may enter the junction
 *  along it and whether one may leave.  Whether it is a spur.  1 is an
 *  arm every other arm's outermost lane may use.  2 is one only the lane
 *  arriving straight at it uses.  Nothing for an arm that is not there. */
static int api_turns_arm(lua_State *L)
{
    int lanes, into, out, spur;
    if (!rec_of(L, "turns") ||
        !lane_turns_arm((int)luaL_checkinteger(L, 2), &lanes, &into, &out, &spur))
        return 0;
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, lanes), lua_setfield(L, -2, "lanes");
    lua_pushboolean(L, into), lua_setfield(L, -2, "into");
    lua_pushboolean(L, out), lua_setfield(L, -2, "out");
    lua_pushinteger(L, spur), lua_setfield(L, -2, "spur");
    return 1;
}

/*  A connector wanted, from arm e's inbound lane k to arm e2's outbound
 *  lane k2.  Answers whether the junction has that pair to give. */
static int api_turns_want(lua_State *L)
{
    if (!rec_of(L, "turns"))
        return 0;
    lua_pushboolean(L, lane_turns_want((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                                       (int)luaL_checkinteger(L, 4), (int)luaL_checkinteger(L, 5)));
    return 1;
}

/*  ---- the threads a thread junction has -------------------------------------
 *
 *  Which arm runs into which is arc.rules.node_threads's.  So is how the
 *  threads stack: a second through line lies over the first as a
 *  diamond, a wye's threads lie a gauge apart. */
static int api_threads_info(lua_State *L)
{
    int col, row, links;
    if (!rec_of(L, "threads"))
        return 0;
    net_threads_info(&col, &row, &links);
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, col), lua_setfield(L, -2, "col");
    lua_pushinteger(L, row), lua_setfield(L, -2, "row");
    lua_pushinteger(L, links), lua_setfield(L, -2, "links");
    lua_pushinteger(L, 4), lua_setfield(L, -2, "arms");
    return 1;
}

/*  One thread wanted, from one arm to another, raised by so much. */
static int api_threads_thread(lua_State *L)
{
    if (!rec_of(L, "threads"))
        return 0;
    lua_pushboolean(L, net_thread_want((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                                           (float)luaL_optnumber(L, 4, 0.0)));
    return 1;
}

/*  ---- a junction's signs --------------------------------------------------
 *
 *  Which arm carries what is arc.rules.junction_signs's, and it places
 *  them itself through arc.put.  The emit window is opened at the box's
 *  own order, so a sign lands in the junction's shape. */
static int api_signs_info(lua_State *L)
{
    int     e, ctrl;
    float   h;
    int32_t col, row;
    if (!rec_of(L, "signs"))
        return 0;
    lua_createtable(L, 0, 3);
    lua_createtable(L, 4, 0);
    for (e = 0; e < 4; ++e)
    {
        if (!net_junction_signs_at(e, &ctrl, &h, &col, &row))
            continue;
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, ctrl), lua_setfield(L, -2, "control");
        lua_pushinteger(L, e), lua_setfield(L, -2, "edge");
        lua_rawseti(L, -2, e + 1);
    }
    lua_setfield(L, -2, "arms");
    if (net_junction_signs_at(0, &ctrl, &h, &col, &row) ||
        net_junction_signs_at(1, &ctrl, &h, &col, &row) ||
        net_junction_signs_at(2, &ctrl, &h, &col, &row) ||
        net_junction_signs_at(3, &ctrl, &h, &col, &row))
    {
        lua_pushinteger(L, col), lua_setfield(L, -2, "col");
        lua_pushinteger(L, row), lua_setfield(L, -2, "row");
        lua_pushnumber(L, h), lua_setfield(L, -2, "half");
    }
    return 1;
}

static const luaL_Reg SIGNS[] = {
    {"info", api_signs_info},
    {NULL,   NULL          }
};

static const luaL_Reg THREADS[] = {
    {"info",  api_threads_info },
    {"thread", api_threads_thread},
    {NULL,    NULL            }
};

static const luaL_Reg TURNS[] = {
    {"info", api_turns_info},
    {"arm",  api_turns_arm },
    {"want", api_turns_want},
    {NULL,   NULL          }
};

/*  ---- a segment's dead ends ----------------------------------------------
 *
 *  What a lane does where its segment simply stops is arc.rules.cap's.
 *  The family declares which KIND of ending it has.  The rule decides
 *  the shape: drawn round the cap, named on the spot, or neither. */
static int api_caps_info(lua_State *L)
{
    const char *ends, *fam;
    int         n;
    if (!rec_of(L, "caps"))
        return 0;
    lane_caps_info(&n, &ends, &fam);
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, n), lua_setfield(L, -2, "n");
    lua_pushstring(L, ends), lua_setfield(L, -2, "ends");
    lua_pushstring(L, fam), lua_setfield(L, -2, "family");
    return 1;
}

/*  One pair: the end of the segment it lies at, the lane of it, and the
 *  two lanes: the one arriving and the one leaving. */
static int api_caps_at(lua_State *L)
{
    int end, lane, from, to;
    if (!rec_of(L, "caps") || !lane_caps_at((int)luaL_checkinteger(L, 2), &end, &lane, &from, &to))
        return 0;
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, end), lua_setfield(L, -2, "end");
    lua_pushinteger(L, lane), lua_setfield(L, -2, "lane");
    lua_pushinteger(L, from), lua_setfield(L, -2, "from");
    lua_pushinteger(L, to), lua_setfield(L, -2, "to");
    return 1;
}

static int api_caps_merge(lua_State *L)
{
    if (!rec_of(L, "caps"))
        return 0;
    lua_pushboolean(L, lane_caps_merge((int)luaL_checkinteger(L, 2)));
    return 1;
}

static int api_caps_link(lua_State *L)
{
    if (!rec_of(L, "caps"))
        return 0;
    lua_pushboolean(L, lane_caps_link((int)luaL_checkinteger(L, 2)));
    return 1;
}

static const luaL_Reg CAPS[] = {
    {"info",  api_caps_info },
    {"at",    api_caps_at   },
    {"merge", api_caps_merge},
    {"link",  api_caps_link },
    {NULL,    NULL          }
};

/*  ---- where a spur aims on the line ---------------------------------------
 *
 *  The fork its meet was classified as.  This way the spur runs, and
 *  where its foot lies.  The line's own rules give two numbers: the
 *  lane's offset from the centerline, and how far along the merge sits.
 *  The rule answers the point the join is drawn to, the tangent it is
 *  drawn with, and the way the line's lane travels there. */
static int api_target_info(lua_State *L)
{
    int   fork, off;
    float rdx, rdy, mdx, mdy, fx, fy, lane_off, merge_along;
    if (!rec_of(L, "target") ||
        !net_spur_target_at(&fork, &off, &rdx, &rdy, &mdx, &mdy, &fx, &fy, &lane_off, &merge_along))
        return 0;
    lua_createtable(L, 0, 10);
    lua_pushinteger(L, fork), lua_setfield(L, -2, "fork");
    lua_pushboolean(L, off), lua_setfield(L, -2, "off");
    lua_pushnumber(L, rdx), lua_setfield(L, -2, "rdx");
    lua_pushnumber(L, rdy), lua_setfield(L, -2, "rdy");
    lua_pushnumber(L, mdx), lua_setfield(L, -2, "mdx");
    lua_pushnumber(L, mdy), lua_setfield(L, -2, "mdy");
    lua_pushnumber(L, fx), lua_setfield(L, -2, "x");
    lua_pushnumber(L, fy), lua_setfield(L, -2, "y");
    lua_pushnumber(L, lane_off), lua_setfield(L, -2, "lane_off");
    lua_pushnumber(L, merge_along), lua_setfield(L, -2, "merge_along");
    return 1;
}

static int api_target_is(lua_State *L)
{
    if (!rec_of(L, "target"))
        return 0;
    net_spur_target_is((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                       (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5),
                       (float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7));
    return 0;
}

static const luaL_Reg TARGET[] = {
    {"info", api_target_info},
    {"is",   api_target_is  },
    {NULL,   NULL           }
};

/*  ---- the band's lane ends -------------------------------------------
 *
 *  What arc.rules.links holds.  Which slab lane goes on to which.  It is
 *  the lane of another band round an interchange, the line's lane where
 *  the slab comes down.  The inner lane of its own band where it tapers
 *  out: is that rule's.  This offers the lanes and lays what it is told
 *  to lay.
 *
 *  The numbers the decisions are measured against come through `info`,
 *  as the floats the lane model holds them in.  Read again from arc.geo
 *  as doubles they answer a hair differently at a boundary.  A link that
 *  should be laid would not be. */
static int api_links_info(lua_State *L)
{
    const ScriptFamily *fr;
    if (!rec_of(L, "links"))
        return 0;
    fr = net_line_rules();
    lua_newtable(L);
    lua_pushinteger(L, net_links_count()), lua_setfield(L, -2, "n");
    lua_pushnumber(L, fr->lane_reach), lua_setfield(L, -2, "reach");
    lua_pushnumber(L, fr->band_abreast), lua_setfield(L, -2, "abreast");
    lua_pushnumber(L, fr->band_outer), lua_setfield(L, -2, "outer");
    lua_pushnumber(L, fr->band_taper_far), lua_setfield(L, -2, "taper_far");
    lua_pushnumber(L, fr->band_taper_near), lua_setfield(L, -2, "taper_near");
    lua_pushnumber(L, fr->band_taper_room), lua_setfield(L, -2, "taper_room");
    lua_pushnumber(L, fr->band_taper_gap), lua_setfield(L, -2, "taper_gap");
    lua_pushnumber(L, fr->band_road_dot), lua_setfield(L, -2, "road_dot");
    lua_pushnumber(L, fr->band_reach), lua_setfield(L, -2, "band_reach");
    lua_pushnumber(L, fr->band_dot), lua_setfield(L, -2, "band_dot");
    lua_pushnumber(L, fr->band_ahead), lua_setfield(L, -2, "band_ahead");
    lua_pushnumber(L, fr->band_aside), lua_setfield(L, -2, "band_aside");
    lua_pushnumber(L, fr->band_apart), lua_setfield(L, -2, "band_apart");
    return 1;
}

/*  One lane as it stands: what it is.  This band it belongs to, how far
 *  off that band's centerline it lies, and whether each end is open. */
static int api_links_lane(lua_State *L)
{
    int   slab, line, band, open0, open1;
    float off, w, len;
    if (!rec_of(L, "links") ||
        !net_links_lane((int)luaL_checkinteger(L, 2), &slab, &line, &band, &off, &w, &len, &open0, &open1))
        return 0;
    lua_createtable(L, 0, 8);
    lua_pushboolean(L, slab), lua_setfield(L, -2, "slab");
    lua_pushboolean(L, line), lua_setfield(L, -2, "line");
    lua_pushinteger(L, band), lua_setfield(L, -2, "band");
    lua_pushnumber(L, off), lua_setfield(L, -2, "off");
    lua_pushnumber(L, w), lua_setfield(L, -2, "w");
    lua_pushnumber(L, len), lua_setfield(L, -2, "len");
    lua_pushboolean(L, open0), lua_setfield(L, -2, "open0");
    lua_pushboolean(L, open1), lua_setfield(L, -2, "open1");
    return 1;
}

/*  Where one end of a lane is, and which way it runs.  `which` 1 is the
 *  end its travel leaves by, and 0 the end it arrives at. */
static int api_links_pose(lua_State *L)
{
    float x, y, dx, dy;
    if (!rec_of(L, "links") ||
        !net_links_pose((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3), &x, &y, &dx, &dy))
        return 0;
    lua_pushnumber(L, x), lua_pushnumber(L, y), lua_pushnumber(L, dx), lua_pushnumber(L, dy);
    return 4;
}

/*  And the station a given distance back from that end, where a lane
 *  tapering into its neighbor leaves. */
static int api_links_station(lua_State *L)
{
    float x, y, dx, dy;
    if (!rec_of(L, "links") ||
        !net_links_station((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                           (float)luaL_checknumber(L, 4), &x, &y, &dx, &dy))
        return 0;
    lua_pushnumber(L, x), lua_pushnumber(L, y), lua_pushnumber(L, dx), lua_pushnumber(L, dy);
    return 4;
}

/*  The chain between two poses, for arc.fit to cut: the points, the
 *  radius each corner may sweep and the tangent each may spend.  Nothing
 *  at all where no lane joins them, which is B behind A. */
static int api_links_route(lua_State *L)
{
    V2    q[MAX_PTS];
    float rad[MAX_PTS], tlim[MAX_PTS];
    int   n, k;
    if (!rec_of(L, "links"))
        return 0;
    n = net_links_route((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                        (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5),
                        (float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7),
                        (float)luaL_checknumber(L, 8), (float)luaL_checknumber(L, 9),
                        q, rad, tlim);
    if (n < 2)
        return 0;
    lua_createtable(L, n, 0);
    for (k = 0; k < n; ++k)
    {
        lua_createtable(L, 0, 2);
        lua_pushnumber(L, q[k].x), lua_setfield(L, -2, "x");
        lua_pushnumber(L, q[k].y), lua_setfield(L, -2, "y");
        lua_rawseti(L, -2, k + 1);
    }
    lua_createtable(L, n, 0);
    for (k = 0; k < n; ++k)
        lua_pushnumber(L, rad[k]), lua_rawseti(L, -2, k + 1);
    lua_createtable(L, n, 0);
    for (k = 0; k < n; ++k)
        lua_pushnumber(L, tlim[k]), lua_rawseti(L, -2, k + 1);
    return 3;
}

/*  The link laid, from the pieces the script cut: which lane it leaves,
 *  which it arrives at, how wide, and the band it belongs to. */
static int api_links_link(lua_State *L)
{
    static Piece pc[MAX_PIECES];
    int          np, k;
    luaL_checktype(L, 2, LUA_TTABLE);
    if (!rec_of(L, "links"))
        return 0;
    np = (int)lua_rawlen(L, 2);
    if (np > MAX_PIECES)
        np = MAX_PIECES;
    for (k = 0; k < np; ++k)
    {
        lua_rawgeti(L, 2, k + 1);
        memset(&pc[k], 0, sizeof pc[k]);
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, "arc");
            pc[k].arc = lua_toboolean(L, -1);
            lua_pop(L, 1);
            pc[k].a.x = api_field_num(L, "ax", 0.0f);
            pc[k].a.y = api_field_num(L, "ay", 0.0f);
            pc[k].b.x = api_field_num(L, "bx", 0.0f);
            pc[k].b.y = api_field_num(L, "by", 0.0f);
            pc[k].c.x = api_field_num(L, "cx", 0.0f);
            pc[k].c.y = api_field_num(L, "cy", 0.0f);
            pc[k].r   = api_field_num(L, "r", 0.0f);
            pc[k].t0  = api_field_num(L, "t0", 0.0f);
            pc[k].t1  = api_field_num(L, "t1", 0.0f);
            pc[k].len = api_field_num(L, "len", 0.0f);
        }
        lua_pop(L, 1);
    }
    lua_pushboolean(L, net_links_add(pc, np, (float)luaL_checknumber(L, 3),
                                     (int)luaL_checkinteger(L, 4), (int)luaL_checkinteger(L, 5),
                                     (int)luaL_optinteger(L, 6, 0)) == 0);
    return 1;
}

/*  What the script counted while it joined them, for the report. */
static int api_links_note(lua_State *L)
{
    if (!rec_of(L, "links"))
        return 0;
    net_links_note(lua_tostring(L, 2), (int)luaL_optinteger(L, 3, 1));
    return 0;
}

static const luaL_Reg LINKS[] = {
    {"info",    api_links_info   },
    {"lane",    api_links_lane   },
    {"pose",    api_links_pose   },
    {"station", api_links_station},
    {"route",   api_links_route  },
    {"link",    api_links_link   },
    {"note",    api_links_note   },
    {NULL,      NULL             }
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

/*  Corner i.  It gives the radius and tangent it was given, and the
 *  tangent of half its turn.  It also gives how much of the incoming
 *  edge the piece already laid has left, and how long the edge it leaves
 *  along is.  Nothing at all for a vertex with no turn to it. */
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

/*  ---- a band band's chain of fit points ------------------------------ */

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
 *  and whether an on-spur pins it. */
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
    lua_pushinteger(L, band_stair_turn(s, i));
    return 1;
}

static int api_stair_pinned(lua_State *L)
{
    StairFan *s = stair_of(L);
    int       i = (int)luaL_checkinteger(L, 2);
    if (!s || i < 0 || i >= s->n)
        return 0;
    lua_pushboolean(L, band_stair_pinned(s, i));
    return 1;
}

/*  A cell as a point of the chain, and a run of cells as the one point
 *  at their center. */
static int api_stair_point(lua_State *L)
{
    StairFan *s = stair_of(L);
    int       i = (int)luaL_checkinteger(L, 2);
    if (s && i >= 0 && i < s->n)
        band_stair_point(s, i);
    return 0;
}

static int api_stair_centre(lua_State *L)
{
    StairFan *s = stair_of(L);
    int       i = (int)luaL_checkinteger(L, 2), j = (int)luaL_checkinteger(L, 3);
    if (s && i >= 0 && j >= i && j < s->n)
        band_stair_centre(s, i, j);
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

/*  ---- a band strip's elevation --------------------------------------- */

static ProfFan *prof_of(lua_State *L)
{
    return (ProfFan *)rec_of(L, "profile");
}

/*  What sort of strip it is and the numbers it is shaped by. */
static const Field PROFILE_FIELDS[] = {
    {"n",          FLD_INT,  offsetof(ProfFan, n)},
    {"total",      FLD_NUM,  offsetof(ProfFan, total)},
    {"spur",       FLD_BOOL, offsetof(ProfFan, spur)},
    {"lane_piece", FLD_BOOL, offsetof(ProfFan, lane_piece)},
    {"lane_off",   FLD_BOOL, offsetof(ProfFan, lane_off)},
    {"flat",       FLD_BOOL, offsetof(ProfFan, flat)},
    {"slab_above", FLD_NUM,  offsetof(ProfFan, z0)},
    {"taper0",     FLD_NUM,  offsetof(ProfFan, spur0)},
    {"taper1",     FLD_NUM,  offsetof(ProfFan, spur1)},
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
    band_prof_at(p, i, &s_at, &z, &ground);
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
        band_prof_set(p, i, (float)luaL_checknumber(L, 3));
    return 0;
}

/*  The curve a lane drop's descent follows. */
static int api_profile_ease(lua_State *L)
{
    if (!prof_of(L))
        return 0;
    lua_pushnumber(L, ease_smooth((float)luaL_checknumber(L, 2)));
    return 1;
}

static const luaL_Reg PROFILE[] = {
    {"info", api_profile_info},
    {"at",   api_profile_at  },
    {"set",  api_profile_set },
    {"ease", api_profile_ease},
    {NULL,   NULL   }
};

/*  ---- a spur's join, slid ----------------------------------------------- */

static SlideFan *slide_of(lua_State *L)
{
    return (SlideFan *)rec_of(L, "slide");
}

/*  How far along the slab the descent may start before it reaches the
 *  lane line.  It is nothing where the two never meet.  It also gives
 *  how far along the line the join may slide. */
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

/*  One placing, as far as the CHAIN it is cut from: the points, the
 *  radius each corner may sweep and the tangent each may spend.  Nothing
 *  and the reason where there is no placing at all: "off" the lane it
 *  aimed at, or "unroutable". */
static int api_slide_route(lua_State *L)
{
    SlideFan   *s = slide_of(L);
    V2          q[MAX_PTS];
    float       rad[MAX_PTS], tlim[MAX_PTS];
    const char *why;
    int         n = 0, k;
    if (!s)
        return 0;
    why = band_slide_chain(s, (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                            q, rad, tlim, &n);
    if (why)
    {
        lua_pushnil(L);
        lua_pushstring(L, why);
        return 2;
    }
    lua_createtable(L, n, 0);
    for (k = 0; k < n; ++k)
    {
        lua_createtable(L, 0, 2);
        lua_pushnumber(L, q[k].x), lua_setfield(L, -2, "x");
        lua_pushnumber(L, q[k].y), lua_setfield(L, -2, "y");
        lua_rawseti(L, -2, k + 1);
    }
    lua_createtable(L, n, 0);
    for (k = 0; k < n; ++k)
        lua_pushnumber(L, rad[k]), lua_rawseti(L, -2, k + 1);
    lua_createtable(L, n, 0);
    for (k = 0; k < n; ++k)
        lua_pushnumber(L, tlim[k]), lua_rawseti(L, -2, k + 1);
    return 3;
}

/*  The lanes within reach of the join at this placing, for the rule that
 *  picks one.
 *
 *      The same handle.
 *      The same rule.
 *      The spur's foot is given.
 *
 *  Nothing where no lane is in reach, which ends the slide. */
static int api_slide_snap(lua_State *L)
{
    SlideFan *s = slide_of(L);
    if (!s || band_slide_snap(s, (float)luaL_checknumber(L, 2)) < 1)
        return 0;
    api_object_push(L, "snap", (void *)&s_spur_pick);
    return 1;
}

/*  And the placing built from the pieces the script cut: the tightest
 *  arc's radius, or nothing for a cut that made none. */
static int api_slide_routed(lua_State *L)
{
    static Piece pc[MAX_PIECES];
    SlideFan    *s = slide_of(L);
    int          np, k;
    float        r;
    luaL_checktype(L, 2, LUA_TTABLE);
    if (!s)
        return 0;
    np = (int)lua_rawlen(L, 2);
    if (np > MAX_PIECES)
        np = MAX_PIECES;
    for (k = 0; k < np; ++k)
    {
        lua_rawgeti(L, 2, k + 1);
        memset(&pc[k], 0, sizeof pc[k]);
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, "arc");
            pc[k].arc = lua_toboolean(L, -1);
            lua_pop(L, 1);
            pc[k].a.x = api_field_num(L, "ax", 0.0f);
            pc[k].a.y = api_field_num(L, "ay", 0.0f);
            pc[k].b.x = api_field_num(L, "bx", 0.0f);
            pc[k].b.y = api_field_num(L, "by", 0.0f);
            pc[k].c.x = api_field_num(L, "cx", 0.0f);
            pc[k].c.y = api_field_num(L, "cy", 0.0f);
            pc[k].r   = api_field_num(L, "r", 0.0f);
            pc[k].t0  = api_field_num(L, "t0", 0.0f);
            pc[k].t1  = api_field_num(L, "t1", 0.0f);
            pc[k].len = api_field_num(L, "len", 0.0f);
        }
        lua_pop(L, 1);
    }
    r = band_slide_routed(s, pc, np);
    if (!(r > 0.0f))
        return 0;
    lua_pushnumber(L, r);
    return 1;
}

/*  Does the placing leave by the spur tile's own line edge? */
static int api_slide_exits(lua_State *L)
{
    SlideFan *s = slide_of(L);
    if (!s)
        return 0;
    lua_pushboolean(L, band_slide_exits(s));
    return 1;
}

/*  The placing kept, with the slack its taper is given past the edge. */
static int api_slide_keep(lua_State *L)
{
    SlideFan *s = slide_of(L);
    if (s)
        band_slide_keep(s, (float)luaL_checknumber(L, 2));
    return 0;
}

/*  Why no placing was found, for --lane-dump. */
static int api_slide_note(lua_State *L)
{
    SlideFan *s = slide_of(L);
    if (s)
        band_slide_note(s, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                         (int)luaL_checkinteger(L, 4), (int)luaL_checkinteger(L, 5));
    return 0;
}

static const luaL_Reg SLIDE[] = {
    {"info",  api_slide_info },
    {"route",  api_slide_route },
    {"snap",   api_slide_snap  },
    {"routed", api_slide_routed},
    {"exits", api_slide_exits},
    {"keep",  api_slide_keep },
    {"note",  api_slide_note },
    {NULL,    NULL    }
};

/*  ---- the lane a spur drops from a slab --------------------------------- */

static DropFan *drop_of(lua_State *L)
{
    return (DropFan *)rec_of(L, "drop");
}

static const Field DROP_FIELDS[] = {
    {"n",      FLD_INT,  offsetof(DropFan, n)},
    {"spurs",  FLD_INT,  offsetof(DropFan, nspurs)},
    {"reach",  FLD_NUM,  offsetof(DropFan, reach)},
    {"narrow", FLD_NUM,  offsetof(DropFan, narrow)},
    {NULL, FLD_NUM, 0}
};

static int api_drop_info(lua_State *L)
{
    return api_fields(L, drop_of(L), DROP_FIELDS);
}

/*  Station i: how far along the slab it is, where it stands, which way
 *  it heads. */
static int api_drop_station(lua_State *L)
{
    DropFan *d = drop_of(L);
    float    at;
    V2       pos, dir;
    if (!d || !band_drop_station(d, (int)luaL_checkinteger(L, 2), &at, &pos, &dir))
        return 0;
    lua_newtable(L);
    lua_pushnumber(L, at), lua_setfield(L, -2, "at");
    lua_pushnumber(L, pos.x), lua_setfield(L, -2, "x");
    lua_pushnumber(L, pos.y), lua_setfield(L, -2, "y");
    lua_pushnumber(L, dir.x), lua_setfield(L, -2, "dx");
    lua_pushnumber(L, dir.y), lua_setfield(L, -2, "dy");
    return 1;
}

/*  Spur r: the point on the centerline it drops from, its own tile's
 *  center, the way it runs, its taper's length in tiles.  Whether it
 *  leaves the slab or joins it. */
static int api_drop_spur(lua_State *L)
{
    DropFan *d = drop_of(L);
    V2       c0, tile, along;
    int      len, off;
    if (!d || !band_drop_spur(d, (int)luaL_checkinteger(L, 2), &c0, &tile, &along, &len, &off))
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

/*  Every station its full width.  The width one is left with on a side.
 *  And the gore, where the spur's own sliver sits beside the slab. */
static int api_drop_clear(lua_State *L)
{
    DropFan *d = drop_of(L);
    if (d)
        band_drop_clear(d);
    return 0;
}

static int api_drop_width(lua_State *L)
{
    DropFan *d = drop_of(L);
    if (d)
        band_drop_width(d, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3),
                         (float)luaL_checknumber(L, 4));
    return 0;
}

static int api_drop_gore(lua_State *L)
{
    DropFan *d = drop_of(L);
    if (d)
        band_drop_gore(d, (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3));
    return 0;
}

static const luaL_Reg DROP[] = {
    {"info",    api_drop_info   },
    {"station", api_drop_station},
    {"spur",    api_drop_spur   },
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
 *  meet under station i pins the strip to. */
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

static int api_ground_lap(lua_State *L)
{
    GroundFan *g = ground_of(L);
    float      z;
    if (!g || !loft_ground_lap(g, (int)luaL_checkinteger(L, 2), &z))
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
    {"lap", api_ground_lap},
    {"set",      api_ground_set     },
    {NULL,       NULL       }
};

/*  ---- an on-spur's four sides ------------------------------------------- */

/*  The sides the script settled on: which is the slab's, which the
 *  line's, which a slab met end-on, and how many lines there were. */
static int api_orient_answer(lua_State *L)
{
    OrientFan *o = rec_of(L, "spurs") ? net_spur_current() : NULL;
    if (!o)
        return 0;
    band_orient_answer(o, (int)luaL_checkinteger(L, 2),
                        (int)luaL_optinteger(L, 3, -1), (int)luaL_optinteger(L, 4, -1),
                        (int)luaL_optinteger(L, 5, -1), 0, (int)luaL_optinteger(L, 6, 0));
    return 0;
}

/*  Which pass is running, and.  Once the rule has named a tile.  Where
 *  that tile is.  The pass is the world's, so it answers before any tile
 *  is named. */
static int api_orient_info(lua_State *L)
{
    OrientFan *o;
    if (!rec_of(L, "spurs"))
        return 0;
    o = net_spur_current();
    lua_newtable(L);
    lua_pushinteger(L, s_pass), lua_setfield(L, -2, "pass");
    if (o)
    {
        lua_pushinteger(L, o->col), lua_setfield(L, -2, "col");
        lua_pushinteger(L, o->row), lua_setfield(L, -2, "row");
    }
    return 1;
}

/*  And the SPUR the rule made of the tile.  It gives the way it lies
 *  along the slab, and the way the slab is.  It also gives which side
 *  its taper falls on, and how many tiles it reaches.  A tile the rule
 *  answers none for carries none. */
static int api_orient_spur(lua_State *L)
{
    OrientFan *o = rec_of(L, "spurs") ? net_spur_current() : NULL, r;
    if (!o || !lua_istable(L, 2))
        return 0;
    memset(&r, 0, sizeof r);
    lua_pushvalue(L, 2);
    r.ax  = api_field_num(L, "ax", 0.0f);
    r.ay  = api_field_num(L, "ay", 0.0f);
    r.tx  = api_field_num(L, "tx", 0.0f);
    r.ty  = api_field_num(L, "ty", 0.0f);
    r.len = (int)api_field_num(L, "len", 0.0f);
    r.fork = (int)api_field_num(L, "fork", 0.0f);
    r.arm  = (int)api_field_num(L, "arm", 0.0f);
    r.rdx = api_field_num(L, "rdx", 0.0f);
    r.rdy = api_field_num(L, "rdy", 0.0f);
    r.mdx = api_field_num(L, "mdx", 0.0f);
    r.mdy = api_field_num(L, "mdy", 0.0f);
    lua_getfield(L, -1, "off"), r.r_off = lua_toboolean(L, -1), lua_pop(L, 1);
    lua_getfield(L, -1, "tile_off"), r.off = lua_toboolean(L, -1), lua_pop(L, 1);
    lua_getfield(L, -1, "opp"), r.opp = lua_toboolean(L, -1), lua_pop(L, 1);
    lua_pop(L, 1);
    band_orient_spur(o, &r);
    return 0;
}

/*  The links a tile's own piece claims: which of its four edges carry
 *  the line out to the edge, as a mask.  Not in the save.  The art says
 *  it.  So a rule reading the map for itself asks here. */
static int api_orient_links(lua_State *L)
{
    OrientFan *o = rec_of(L, "spurs") ? net_spur_current() : NULL;
    if (!o)
        return 0;
    lua_pushinteger(L, band_orient_links(o, (int32_t)luaL_checkinteger(L, 2),
                                          (int32_t)luaL_checkinteger(L, 3)));
    return 1;
}

/*  One tile the rule found: named here, and the answers that follow are
 *  about it. */
static int api_orient_at(lua_State *L)
{
    if (!rec_of(L, "spurs"))
        return 0;
    lua_pushboolean(L, net_spur_at((int32_t)luaL_checkinteger(L, 2), (int32_t)luaL_checkinteger(L, 3)));
    return 1;
}

static const luaL_Reg ORIENT[] = {
    {"at",     api_orient_at    },
    {"info",   api_orient_info  },
    {"links",  api_orient_links },
    {"answer", api_orient_answer},
    {"spur",   api_orient_spur  },
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

/*  The copies of the corner at one grid point.  A flat run of the
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

/*  ---- a lane carried across a meet ---------------------------------- */

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

/*  Is lane i an open end that could carry on across a meet? */
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
 *  question.  A strip is asked for its surface and for the margins
 *  beside it.  So the rule is named apart from the kind, and the kind is
 *  what says which methods the handle has. */
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
     *  points at is the pipeline's and the pipeline has moved on.  A
     *  rule a rule asked for has not moved it on, though.  The outer
     *  rule is still holding its own object and still has work to do
     *  with it.  So the stamp only advances when the outermost one is
     *  finished. */
    if (--s_depth == 0)
        ++s_gen;
    return drew;
}

/*  THE KINDS.  A kind is its name and its methods, declared once.  The
 *  handle's own table is built from this, and rec_of answers by the same
 *  name.  Adding a kind is a row here and a method table.  It is not a
 *  row here, a line in the registration and a helper of its own. */
static const struct
{
    const char       *name;
    const luaL_Reg   *methods;
} KINDS[] = {
    {"strip",    STRIP},
    {"junction", JUNCTION},
    {"margin",  MARGIN},
    {"lane",     LANE},
    {"panel",    LAP},
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
    {"spurs",    ORIENT},
    {"terrain",  TERRAIN},
    {"shelf",    SHELF},
    {"cross",    CROSS},
    {"path",     PATH},
    {"world",    WORLD},
    {"network",  NETWORK},
    {"bands",    BANDS},
    {"moving",   MOVING},
    {"frame",    FRAME},
    {"links",    LINKS},
    {"snap",     SNAP},
    {"turns",    TURNS},
    {"threads",   THREADS},
    {"signs",    SIGNS},
    {"caps",     CAPS},
    {"target",   TARGET},
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

