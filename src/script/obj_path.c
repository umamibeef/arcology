/*  obj_path.c: A PATH, and everything it is cut into.
 *
 *  The fit takes a chain of points and answers pieces.  Each of these
 *  is a kind of its own, and each offers a reading to the rule that
 *  decides it.
 *
 *      the runs of a path, and the chain of lines they make
 *      the meet of two lines
 *      the biarc between two parallel lines
 *      the walk between two that neither cross nor bridge
 *      the fillet swept into a corner */
#include "script.h"


#include <string.h>

#include "internal.h"
#include "obj.h"
#include "mesh/internal.h"
#include "pipeline.h"
#include "net/net.h"
#include "mesh/model.h"
#include "log.h"
#include "build.h"
#include "incr.h"
#include "mesh/shapes.h"
#include "mesh/emit.h"
#include "mesh/fit.h"
#include "mesh/loft.h"
#include "mesh/piece.h"
#include "mesh/surface.h"
#include "net/cell.h"
#include "walk/cursor.h"
#include "walk/walk.h"
#include "net/walkway.h"

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

/*  The way the script asked for, laid.  Answers the READING and nothing
 *  else.  Which rule settles an arc, a biarc or a walk is the script's.
 *  A name handed back from here would be the pipeline choosing. */
static int api_path_try(lua_State *L)
{
    static const char *const KIND[] = {NULL, "meet", "bridge", "step"};
    void                    *o;
    int                      way;
    if (!rec_of(L, "path"))
        return 0;
    if ((way = path_try(lua_tostring(L, 2), &o)) <= TRY_NONE)
        return 0;
    api_object_push(L, KIND[way], o);
    return 1;
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
 *  scripts', so the fit stops for each and takes up what it answered.
 *  Each answers its reading alone, and the script names the rule. */
static int api_path_runs(lua_State *L)
{
    RunFan *r;
    if (!rec_of(L, "path") || (r = path_runs()) == NULL)
        return 0;
    api_object_push(L, "runs", r);
    return 1;
}

static int api_path_chain(lua_State *L)
{
    ChainFan *c;
    if (!rec_of(L, "path") || (c = path_chain()) == NULL)
        return 0;
    api_object_push(L, "chain", c);
    return 1;
}

/*  And the path as it stands at the end.  The rule that drops the idle
 *  vertices reads it, and settles the radius at each of the rest. */
static int api_path_ending(lua_State *L)
{
    FitFan *f;
    if (!rec_of(L, "path") || (f = path_ending()) == NULL)
        return 0;
    api_object_push(L, "fit", f);
    return 1;
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

const luaL_Reg PATH[] = {
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
static int api_fit_swept(lua_State *L)
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

const luaL_Reg FIT[] = {
    {"info",       api_fit_info      },
    {"at",         api_fit_at        },
    {"drop",       api_fit_drop      },
    {"corner",     api_fit_corner    },
    {"sweep",      api_fit_sweep     },
    {"swept",      api_fit_swept     },
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

const luaL_Reg RUNS[] = {
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
static int chain_end(lua_State *L)
{
    return strcmp(luaL_checkstring(L, 2), "goal") == 0;
}

static const Field CHAIN_FIELDS[] = {
    {"nr",  FLD_INT,  offsetof(ChainFan, nr)     },
    {"ex0", FLD_BOOL, offsetof(ChainFan, ex0)    },
    {"ex1", FLD_BOOL, offsetof(ChainFan, ex1)    },
    {"sx",  FLD_NUM,  offsetof(ChainFan, start.x)},
    {"sy",  FLD_NUM,  offsetof(ChainFan, start.y)},
    {"gx",  FLD_NUM,  offsetof(ChainFan, goal.x) },
    {"gy",  FLD_NUM,  offsetof(ChainFan, goal.y) },
    {"at0", FLD_INT,  offsetof(ChainFan, at0)    },
    {"at1", FLD_INT,  offsetof(ChainFan, at1)    },
    {"s0x", FLD_NUM,  offsetof(ChainFan, st0.x)  },
    {"s0y", FLD_NUM,  offsetof(ChainFan, st0.y)  },
    {"s1x", FLD_NUM,  offsetof(ChainFan, st1.x)  },
    {"s1y", FLD_NUM,  offsetof(ChainFan, st1.y)  },
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
        path_chain_aim(c, chain_end(L));
    return 0;
}

/*  An end put where the script says, facing the way it says.  The
 *  point is where the chain starts or finishes.  The direction is the
 *  one its own line runs along there.  Which pose that is, such as an
 *  arm's end aimed at the node it meets, is the rule's.  This only takes
 *  it. */
static int api_chain_end_at(lua_State *L)
{
    ChainFan *c = chain_of(L);
    V2        p, d;
    if (!c)
        return 0;
    p.x = (float)luaL_checknumber(L, 3), p.y = (float)luaL_checknumber(L, 4);
    d.x = (float)luaL_checknumber(L, 5), d.y = (float)luaL_checknumber(L, 6);
    if (chain_end(L))
        c->goal = p, c->st1 = d;
    else
        c->start = p, c->st0 = d;
    return 0;
}

/*  Does that end already lie on the line of the run beside it? */
static int api_chain_on_line(lua_State *L)
{
    ChainFan *c = chain_of(L);
    if (!c || c->nr <= 0)
        return 0;
    lua_pushboolean(L, path_chain_on_line(c, chain_end(L)));
    return 1;
}

/*  The chain, a line at a time: an end's own line, or a run. */
static int api_chain_add_end(lua_State *L)
{
    ChainFan *c = chain_of(L);
    if (c)
        path_chain_end(c, chain_end(L));
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

const luaL_Reg CHAIN[] = {
    {"info",    api_chain_info   },
    {"run",     api_chain_run    },
    {"aim",     api_chain_aim    },
    {"end_at",  api_chain_end_at },
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

/*  A meet is swept the way a fit is: the same reading, named for the
 *  kind that asks. */
static int api_meet_swept(lua_State *L)
{
    return api_fit_swept(L);
}

const luaL_Reg MEET[] = {
    {"info",   api_meet_info  },
    {"holds",  api_meet_holds },
    {"covers", api_meet_covers},
    {"arc",    api_meet_arc   },
    {"swept",  api_meet_swept },
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

const luaL_Reg BRIDGE[] = {
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

static int st_side(lua_State *L)
{
    return strcmp(luaL_checkstring(L, 2), "ahead") == 0;
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
    lua_pushboolean(L, path_step_inline(w, st_side(L)));
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
        path_step_end(w, st_side(L));
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

const luaL_Reg STEP[] = {
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

const luaL_Reg SWEEP[] = {
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
    {"n",     FLD_INT,  offsetof(PieceFan, n)    },
    {"posed", FLD_BOOL, offsetof(PieceFan, posed)},
    {"ax",    FLD_NUM,  offsetof(PieceFan, A.x)  },
    {"ay",    FLD_NUM,  offsetof(PieceFan, A.y)  },
    {"adx",   FLD_NUM,  offsetof(PieceFan, tA.x) },
    {"ady",   FLD_NUM,  offsetof(PieceFan, tA.y) },
    {"bx",    FLD_NUM,  offsetof(PieceFan, B.x)  },
    {"by",    FLD_NUM,  offsetof(PieceFan, B.y)  },
    {"bdx",   FLD_NUM,  offsetof(PieceFan, tB.x) },
    {"bdy",   FLD_NUM,  offsetof(PieceFan, tB.y) },
    {NULL, FLD_NUM, 0}
};

/*  The points of a chain that was queued as two poses: what the script
 *  built between them.  Each point carries the radius its corner may
 *  sweep and the tangent it may spend.  Taken before the cut. */
static int api_pieces_points(lua_State *L)
{
    PieceFan *p = piece_of(L);
    V2        q[MAX_PTS];
    float     rad[MAX_PTS], tlim[MAX_PTS];
    int       n, k;
    if (!p || !p->posed || !lua_istable(L, 2))
        return 0;
    n = (int)lua_rawlen(L, 2);
    if (n > MAX_PTS)
        n = MAX_PTS;
    for (k = 0; k < n; ++k)
    {
        lua_rawgeti(L, 2, k + 1);
        q[k].x = api_field_num(L, "x", 0.0f);
        q[k].y = api_field_num(L, "y", 0.0f);
        lua_pop(L, 1);
        rad[k] = tlim[k] = 0.0f;
        if (lua_istable(L, 3))
        {
            lua_rawgeti(L, 3, k + 1);
            rad[k] = (float)luaL_optnumber(L, -1, 0.0);
            lua_pop(L, 1);
        }
        if (lua_istable(L, 4))
        {
            lua_rawgeti(L, 4, k + 1);
            tlim[k] = (float)luaL_optnumber(L, -1, 0.0);
            lua_pop(L, 1);
        }
    }
    lua_pushboolean(L, net_cut_points(p, q, n, rad, tlim) == 0);
    return 1;
}

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

const luaL_Reg PIECES[] = {
    {"info",     api_pieces_info    },
    {"points",   api_pieces_points  },
    {"corner",   api_pieces_corner  },
    {"straight", api_pieces_straight},
    {"arc",      api_pieces_arc     },
    {"tail",     api_pieces_tail    },
    {NULL,       NULL       }
};
