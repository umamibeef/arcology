/*  family.c: the families, as the scripts declare them.
 *
 *  A family says how one kind of line is drawn.  There is no C table of
 *  them.  Scripts/families/line.lua and its neighbors each call
 *  `arc.family.define`, and this file turns a declaration into the
 *  NetFamily the pipeline reads.  So another way to draw a band is a
 *  file in scripts/families.  A width, a material, a loft kind and a set
 *  of stages.  And no change to the renderer.
 *
 *  Two kinds of name meet here.  A KNOB, a loft kind, a tile family or a
 *  lane ending names something the C already has.  A name none of them
 *  answers to is a fault the script hears about.  A STAGE is looser: a
 *  name this file has registered as a primitive binds to that C
 *  function.  Any other name binds to the rule `arc.rules.<name>`, which
 *  is handed the thing the stage works on.  That is what lets a
 *  declaration supply a stage the C has never seen.
 *
 *  The modules keep their own primitives static and register them here.
 *  So the list of what a declaration may name is the list of what the
 *  pipeline can actually do. */
#include <string.h>

#include "pipeline.h"
#include "log.h"
#include "script.h"

const char *const NET_HOOK_NAME[NET_HOOKS] = {
    "control", "record", "flies", "taper", "profile", "works", "traffic", "furniture",
};

/*  The primitives, by stage and name.  Registration happens once, from
 *  the modules that hold them. */
#define NET_HOOK_MAX 24
static struct
{
    NetHook     h;
    const char *name;
    NetHookFn   fn;
    NetHookFn   after; /* the second half, where the drive composes between them */
    const char *ask;   /* and the rule the drive asks in between */
    const char *ask_after; /* ... and the one its second half asks in turn */
} s_hook[NET_HOOK_MAX];
static int s_n_hook;
static int s_registered;

void net_hook_add(NetHook h, const char *name, NetHookFn fn)
{
    net_hook_add_split(h, name, fn, NULL, NULL);
}

/*  A stage the drive composes inside runs in two halves.  The first
 *  gathers what the script is to be handed, the second takes up what it
 *  answered.  One name registers both. */
void net_hook_add_split(NetHook h, const char *name, NetHookFn fn, NetHookFn after, const char *ask)
{
    net_hook_add_split2(h, name, fn, after, ask, NULL);
}

/*  A stage whose second half asks a rule of its own in turn: a slab's
 *  profile shapes the slab.  Then reads what its spurs took from it. */
void net_hook_add_split2(NetHook h, const char *name, NetHookFn fn, NetHookFn after, const char *ask, const char *ask_after)
{
    if (s_n_hook >= NET_HOOK_MAX)
    {
        R_ERR("net", "no room for the primitive %s", name);
        return;
    }
    s_hook[s_n_hook].h      = h;
    s_hook[s_n_hook].name   = name;
    s_hook[s_n_hook].fn     = fn;
    s_hook[s_n_hook].after = after;
    s_hook[s_n_hook].ask       = ask;
    s_hook[s_n_hook].ask_after = ask_after;
    ++s_n_hook;
}

static NetHookFn hook_find(NetHook h, const char *name)
{
    int i;
    for (i = 0; i < s_n_hook; ++i)
        if (s_hook[i].h == h && strcmp(s_hook[i].name, name) == 0)
            return s_hook[i].fn;
    return NULL;
}

static NetHookFn hook_find_after(NetHook h, const char *name)
{
    int i;
    for (i = 0; i < s_n_hook; ++i)
        if (s_hook[i].h == h && strcmp(s_hook[i].name, name) == 0)
            return s_hook[i].after;
    return NULL;
}

/*  The rule a primitive of two halves asks for in between them, and the
 *  one its second half asks in turn. */
static const char *hook_find_ask(NetHook h, const char *name)
{
    int i;
    for (i = 0; i < s_n_hook; ++i)
        if (s_hook[i].h == h && strcmp(s_hook[i].name, name) == 0)
            return s_hook[i].ask;
    return NULL;
}

static const char *hook_find_ask_after(NetHook h, const char *name)
{
    int i;
    for (i = 0; i < s_n_hook; ++i)
        if (s_hook[i].h == h && strcmp(s_hook[i].name, name) == 0)
            return s_hook[i].ask_after;
    return NULL;
}

/*  ---- the families ------------------------------------------------- */

static NetFamily s_fam[NET_FAM_MAX];
static int       s_n_fam;
/*  A declaration's strings outlive the Lua table they came from, so the
 *  names a family keeps are copied here. */
static char s_pool[4096];
static int  s_pool_n;

/*  A tile family nobody declared for.  It is a family that is nothing.
 *  A pass reading one draws nothing, and does not read through NULL.
 *  Every name answers it before a script runs.  And what a build with
 *  the scripting layer off keeps. */
static const float     s_zero = 0.0f;
static const NetFamily s_none = {
    .name  = "none",
    .f     = 1, /* the middle slot: nothing is ever drawn through this one */
    .width = &s_zero,
    .rmin  = &s_zero,
    .rmax  = &s_zero,
    .loft  = LOFT_LINE,
    .slot  = "slot_strip",
};

const NetFamily *net_walked[NET_FAM_MAX];
int              net_n_walked;
const NetFamily *net_line = &s_none, *net_thread = &s_none, *net_band = &s_none, *net_power = &s_none;
/*  The family a tile family means, one each. */
static const NetFamily *s_answers[3];

static const char *keep(const char *s)
{
    int n;
    if (!s || !*s)
        return NULL;
    n = (int)strlen(s) + 1;
    if (s_pool_n + n > (int)sizeof s_pool)
    {
        R_ERR("net", "no room for the name %s", s);
        return NULL;
    }
    memcpy(s_pool + s_pool_n, s, (size_t)n);
    s_pool_n += n;
    return s_pool + s_pool_n - n;
}

void band_primitives(void);

/*  The primitives, once: the modules keep theirs static and lend them
 *  here, so what a declaration may name is what the pipeline can do.  A
 *  family that lends none needs no entry.  The power line is drawn tile
 *  by tile by the composing script and asks the pipeline for no stage at
 *  all. */
static void hooks_ready(void)
{
    if (s_registered)
        return;
    s_registered = 1;
    band_primitives();
}

void net_family_reset(void)
{
    memset(s_fam, 0, sizeof s_fam);
    memset(s_answers, 0, sizeof s_answers);
    memset(net_walked, 0, sizeof net_walked);
    s_n_fam = s_pool_n = net_n_walked = 0;
    net_line = net_thread = net_band = net_power = &s_none;
    hooks_ready();
}

int net_family_count(void)
{
    return s_n_fam;
}

const NetFamily *net_family_at(int i)
{
    return i >= 0 && i < s_n_fam ? &s_fam[i] : &s_none;
}

const NetFamily *net_family(Family f)
{
    return s_answers[f] ? s_answers[f] : &s_none;
}

/*  A name against a list of them, answering the place it sits at or -1. */
static int which(const char *name, const char *const *list, int n)
{
    int i;
    for (i = 0; name && i < n; ++i)
        if (strcmp(name, list[i]) == 0)
            return i;
    return -1;
}

static int decl_fault(const NetFamilyDecl *d, const char *what, const char *name)
{
    R_ERR("net", "family %s: no %s named %s", d->name ? d->name : "?", what, name ? name : "nothing");
    return -1;
}

/*  A declaration read into a family.  `rule` takes 1 at each stage the
 *  declaration answered with a rule rather than a primitive.  `store`
 *  says whether the names are kept: the lint reads a declaration to
 *  check its names and must leave the registry alone. */
static int family_read(const NetFamilyDecl *d, NetFamily *out, int *rule, int store)
{
    static const char *const TILES[3] = {"power", "line", "thread"}; /* Family's own order: the place a name sits at IS the code */
    static const char *const LOFTS[4] = {"line", "thread", "slab", "spur"};
    static const char *const ENDS[3]  = {"open", "cap", "reverse"};
    NetFamily                x;
    int                      tiles, loft, ends, h;
    hooks_ready();
    if (!d->name || !*d->name)
        return decl_fault(d, "name", NULL);
    if ((tiles = which(d->tiles, TILES, 3)) < 0)
        return decl_fault(d, "tile family", d->tiles);
    if ((loft = which(d->loft, LOFTS, 4)) < 0)
        return decl_fault(d, "loft kind", d->loft);
    if ((ends = which(d->lane_ends, ENDS, 3)) < 0)
        return decl_fault(d, "lane ending", d->lane_ends);
    memset(&x, 0, sizeof x);
    if (!(x.width = net_tune_at(d->width)))
        return decl_fault(d, "knob", d->width);
    if (!(x.rmin = net_tune_at(d->rmin)))
        return decl_fault(d, "knob", d->rmin);
    if (!(x.rmax = net_tune_at(d->rmax)))
        return decl_fault(d, "knob", d->rmax);
    x.f                 = (Family)tiles;
    x.loft              = (LoftKind)loft;
    x.lane_ends         = ends;
    x.ref_width         = d->ref_width;
    x.mat               = d->mat;
    x.fit_fam           = d->fit;
    x.junc_lift         = d->junc_lift;
    x.shelf_grade       = d->shelf_grade;
    x.lips             = d->lips;
    x.spurs             = d->spurs;
    x.ends_at_buildings = d->ends_at_buildings;
    x.caps              = d->caps;
    x.classed           = d->classed;
    /*  Where its strips file themselves for the traffic.  A name none of
     *  the graphs answers to is a fault, like every other name in a
     *  declaration. */
    x.graphed      = d->graph != NULL;
    x.stations     = d->stations;
    x.meets    = d->meets;
    x.paved        = d->paved;
    x.crossed      = d->crossed;
    x.margin      = d->margin;
    x.threads       = d->threads;
    x.props        = d->props ? keep(d->props) : NULL;
    x.graph        = 0;
    x.record_class = d->record_class;
    if (d->graph)
    {
        if (strcmp(d->graph, "line") == 0)
            x.graph = 0;
        else if (strcmp(d->graph, "thread") == 0)
            x.graph = 1;
        else
        {
            R_ERR("net", "family %s: no graph is called %s", d->name, d->graph);
            return -1;
        }
    }
    x.lane_paint        = d->lane_paint;
    x.free_reach        = d->free_reach;
    x.turnout           = d->turnout;
    x.slab              = d->slab;
    /*  A stage's name: the primitive if one answers to it, the rule of
     *  that name if none does.  A stage no rule can carry.  One whose
     *  work is a mesh the script has no handle on.  Is a fault rather
     *  than a silent nothing. */
    for (h = 0; h < NET_HOOKS; ++h)
    {
        NetHookFn fn;
        if (!d->stage[h] || !*d->stage[h])
            continue;
        if ((fn = hook_find((NetHook)h, d->stage[h])) != NULL)
        {
            switch ((NetHook)h)
            {
            case NH_CONTROL: x.control = (int (*)(const RCity *, int32_t, int32_t, int))fn; break;
            case NH_RECORD:
                x.record      = (int (*)(Loft *))fn;
                x.record_done = (int (*)(Loft *))hook_find_after(NH_RECORD, d->stage[h]);
                break;
            case NH_FLIES: x.flies = (int (*)(const RLoft *, float))fn; break;
            case NH_TAPER: x.taper = (void (*)(Loft *))fn; break;
            case NH_PROFILE:
                x.profile      = (int (*)(Loft *))fn;
                x.profile_done = (int (*)(Loft *))hook_find_after(NH_PROFILE, d->stage[h]);
                break;
            case NH_WORKS: x.works = (int (*)(Loft *))fn; break;
            case NH_TRAFFIC: x.traffic = (void (*)(const RLoft *, int, float *, float *))fn; break;
            case NH_FURNITURE:
                x.furniture      = (int (*)(Loft *))fn;
                x.furniture_done = (int (*)(Loft *))hook_find_after(NH_FURNITURE, d->stage[h]);
                break;
            case NET_HOOKS: break;
            }
            x.ask[h]       = hook_find_ask((NetHook)h, d->stage[h]);
            x.ask_after[h] = hook_find_ask_after((NetHook)h, d->stage[h]);
            continue;
        }
        if (rule)
            rule[h] = 1;
        x.rule[h] = store ? keep(d->stage[h]) : d->stage[h];
    }
    x.name = store ? keep(d->name) : d->name;
    x.slot = store ? keep(d->slot) : d->slot;
    if (!x.name || !x.slot)
        return decl_fault(d, "room for", d->name);
    *out = x;
    return 0;
}

int net_family_check(const NetFamilyDecl *d, int *rule)
{
    NetFamily x;
    return family_read(d, &x, rule, 0);
}

int net_family_define(const NetFamilyDecl *d)
{
    NetFamily x;
    int       i, at = -1;
    if (family_read(d, &x, NULL, 1) != 0)
        return -1;
    for (i = 0; i < s_n_fam; ++i)
        if (strcmp(s_fam[i].name, x.name) == 0)
            at = i;
    if (at < 0)
    {
        if (s_n_fam >= NET_FAM_MAX)
            return decl_fault(d, "room for another family", d->name);
        at = s_n_fam++;
    }
    s_fam[at] = x;
    if (d->answers)
        s_answers[x.f] = &s_fam[at];
    if (d->walk >= 0 && d->walk < NET_FAM_MAX)
    {
        net_walked[d->walk] = &s_fam[at];
        if (d->walk >= net_n_walked)
            net_n_walked = d->walk + 1;
    }
    /*  The four the pipeline still reaches for by name.  A slab's own
     *  stages are the band's wherever a band is lofted rather than a
     *  tile family walked. */
    if (strcmp(x.name, "line") == 0)
        net_line = &s_fam[at];
    else if (strcmp(x.name, "thread") == 0)
        net_thread = &s_fam[at];
    else if (strcmp(x.name, "band") == 0)
        net_band = &s_fam[at];
    else if (strcmp(x.name, "power") == 0)
        net_power = &s_fam[at];
    return 0;
}

/*  ---- the stages ---------------------------------------------------
 *
 *  One door each, so a call site never asks which of the two answered.
 *  A stage bound to a rule is handed the thing it works on.  A strip for
 *  the ones the loft runs, plain numbers for the two that decide rather
 *  than draw. */

int net_family_has(const NetFamily *fam, NetHook h)
{
    if (!fam)
        return 0;
    if (fam->rule[h])
        return 1;
    switch (h)
    {
    case NH_CONTROL: return fam->control != NULL;
    case NH_RECORD: return fam->record != NULL || fam->margin != NULL;
    case NH_FLIES: return fam->flies != NULL || fam->rule[NH_FLIES] != NULL;
    case NH_TAPER: return fam->taper != NULL;
    case NH_PROFILE: return fam->profile != NULL;
    case NH_WORKS: return fam->works != NULL;
    case NH_TRAFFIC: return fam->traffic != NULL;
    case NH_FURNITURE: return fam->furniture != NULL;
    case NET_HOOKS: break;
    }
    return 0;
}

/*  A junction's control, measured and left for the drive.  A family's
 *  own primitive reads the map and keeps its reading.  A family whose
 *  control is a rule and nothing else leaves the tile and its links, and
 *  the rule works from those. */
void net_family_control_ask(const NetFamily *fam, const RCity *c, int32_t col, int32_t row, int links)
{
    if (fam->control)
        fam->control(c, col, row, links);
    else if (fam->rule[NH_CONTROL])
        net_control_ask(fam->rule[NH_CONTROL], col, row, links, NULL, NULL, 0);
}

/*  A stage a RULE answers: its name, or nothing where a primitive of the
 *  pipeline's own answers it.  The drive asks the rule.  The primitives
 *  above run where there is one.  Every stage is settled one way or the
 *  other, so a name neither answers to is a fault the declaration
 *  refuses. */
const char *net_family_stage_rule(const NetFamily *fam, NetHook h)
{
    if (h < 0 || h >= NET_HOOKS)
        return NULL;
    switch (h)
    {
    case NH_CONTROL: return fam->control ? fam->ask[h] : fam->rule[h];
    case NH_RECORD: return fam->record ? fam->ask[h] : (fam->rule[h] ? fam->rule[h] : fam->margin);
    case NH_TAPER: return fam->taper ? fam->ask[h] : fam->rule[h];
    case NH_PROFILE: return fam->profile ? fam->ask[h] : fam->rule[h];
    case NH_WORKS: return fam->works ? fam->ask[h] : fam->rule[h];
    case NH_FURNITURE: return fam->furniture ? fam->ask[h] : fam->rule[h];
    default: break;
    }
    return NULL;
}

/*  Whether a primitive of the pipeline's own answers a stage, rather
 *  than a rule.  A primitive that asks for something in between its two
 *  halves leaves it.  Where it left nothing there is nothing to ask. */
int net_family_stage_primitive(const NetFamily *fam, NetHook h)
{
    switch (h)
    {
    case NH_CONTROL: return fam->control != NULL;
    case NH_RECORD: return fam->record != NULL || fam->margin != NULL;
    case NH_TAPER: return fam->taper != NULL;
    case NH_PROFILE: return fam->profile != NULL;
    case NH_WORKS: return fam->works != NULL;
    case NH_FURNITURE: return fam->furniture != NULL;
    default: break;
    }
    return 0;
}

/*  The half of a stage that runs after the drive has answered what the
 *  first half asked for.  A stage answered in one piece has none. */
/*  And the margin the rule laid, filed in the walk network so a
 *  junction's corners join it.  A family that named no margin rule has
 *  none to file. */
int net_family_record_done(const NetFamily *fam, Loft *x)
{
    if (fam->record_done)
        return fam->record_done(x);
    return fam->margin ? net_strip_margin(x) : 0;
}

int net_family_furniture_done(const NetFamily *fam, Loft *x)
{
    return fam->furniture_done ? fam->furniture_done(x) : 0;
}

int net_family_profile_done(const NetFamily *fam, Loft *x)
{
    return fam->profile_done ? fam->profile_done(x) : 0;
}

/*  And the rule that second half asks in turn, or nothing where it asks
 *  for none. */
const char *net_family_stage_rule_after(const NetFamily *fam, NetHook h)
{
    return h >= 0 && h < NET_HOOKS ? fam->ask_after[h] : NULL;
}

/*  The half of the box that runs BEFORE the drive cuts: a thread
 *  junction's threads are paths, and they are queued like every other. */
/*  And the half of it that runs after the drive has composed what the
 *  first half gathered.  A family whose box is one piece has none. */
/*  Whether a family files its strips without a stage of its own. */
int net_family_graphed(const NetFamily *fam)
{
    return fam->graphed;
}

int net_family_record(const NetFamily *fam, Loft *x)
{
    /*  What a strip records, in the order the readers want it.  A family
     *  that says its strips are CROSSED has every station measure the
     *  nearest level meet first.  This is because the approach markings
     *  on it run between one distance and another either side.  Then the
     *  strip is filed in the graph the family declared, under the class
     *  it named: its own where it named none.  Then the margin store is
     *  cleared for the rule the family named to lay one in.  A family
     *  with more to do than these names a stage as well. */
    if (fam->crossed)
        net_strip_laps(x);
    if (fam->record)
        return fam->record(x);
    if (fam->graphed &&
        net_record(fam->graph ? &x->m->threadnet : &x->m->net, x->smp, x->ns, x->total,
                   fam->record_class >= 0 ? fam->record_class
                                          : (x->d->cls >= 0.0f ? (int)(x->d->cls + 0.5f) : 0),
                   x->d) != 0)
        return -1;
    if (fam->margin)
        script_walk_reset();
    return 0;
}

/*  Whether a strip standing that far over the ground clears it.  It is
 *  asked at every STATION of every strip, in the middle of the grading.
 *  So no rule may answer it: a family's own primitive settles it or
 *  nothing does. */
int net_family_flies(const NetFamily *fam, const RLoft *d, float over)
{
    return fam->flies ? fam->flies(d, over) : 0;
}

void net_family_taper(const NetFamily *fam, Loft *x)
{
    if (fam->taper)
        fam->taper(x);
}

int net_family_profile(const NetFamily *fam, Loft *x)
{
    return fam->profile ? fam->profile(x) : 0;
}

const char *net_family_props(const NetFamily *fam)
{
    return fam->props;
}

/*  A family whose junction is a PAVED BOX, and one whose junction is a
 *  set of THREADS.  Both are declarations.  What the box lays is
 *  arc.rules.junction's and what the threads are is
 *  arc.rules.node_threads's, and world.lua calls for each in its own
 *  place. */
int net_family_paved(const NetFamily *fam)
{
    return fam && fam->paved;
}

int net_family_threads(const NetFamily *fam)
{
    return fam && fam->threads;
}

/*  A family that crosses another at grade: a tile whose second piece is
 *  this family's carries a level meet.  What one is made of is the
 *  script's from end to end, `w:meet` gathers it and the rules measure
 *  and furnish it.  So this is a declaration and not a stage. */
int net_family_laps(const NetFamily *fam)
{
    return fam && fam->meets;
}

int net_family_stations(const NetFamily *fam)
{
    return fam->stations;
}

int net_family_works(const NetFamily *fam, Loft *x)
{
    if (fam->works)
        return fam->works(x);
    /*  No stage of its own: a family that says its lofts file their
     *  stations has them filed.  One that does not has nothing to do
     *  here. */
    return fam->stations ? net_station_record(x) : 0;
}

/*  Where the traffic runs across a strip, as fractions of a tile.  A
 *  family whose stage is a primitive works it out for the strip in hand.
 *  The drive asks a family whose stage is a RULE once for every class
 *  the pipeline can present.  It asks before any strip is built: so
 *  nothing here reaches up in the middle of a loft.
 *
 *  A class runs from TRAFFIC_CLS_LO, which is the family that has none,
 *  up to the widest line there is. */
#define TRAFFIC_CLS_LO (-1)
#define TRAFFIC_CLS_HI 3 /* the slab's own class, which net_record files it under */
#define TRAFFIC_CLASSES (TRAFFIC_CLS_HI - TRAFFIC_CLS_LO + 1)

static struct
{
    float in, out;
    int   have;
} s_traffic_run[NET_FAM_MAX][TRAFFIC_CLASSES];

void net_traffic_runs_reset(void)
{
    memset(s_traffic_run, 0, sizeof s_traffic_run);
}

/*  How many family-and-class pairs the drive is to answer, and which
 *  rule and class each of them is. */
int net_traffic_runs(void)
{
    return s_n_fam * TRAFFIC_CLASSES;
}

const char *net_traffic_run_at(int i, int *cls)
{
    int fi = i / TRAFFIC_CLASSES;
    if (i < 0 || fi >= s_n_fam || s_fam[fi].traffic || !s_fam[fi].rule[NH_TRAFFIC])
        return NULL;
    *cls = TRAFFIC_CLS_LO + i % TRAFFIC_CLASSES;
    return s_fam[fi].rule[NH_TRAFFIC];
}

void net_traffic_run_is(int i, float in, float out)
{
    int fi = i / TRAFFIC_CLASSES;
    if (i < 0 || fi >= s_n_fam)
        return;
    s_traffic_run[fi][i % TRAFFIC_CLASSES].in   = in;
    s_traffic_run[fi][i % TRAFFIC_CLASSES].out  = out;
    s_traffic_run[fi][i % TRAFFIC_CLASSES].have = 1;
}

/*  WHERE A STRIP STANDS CLEAR of the ground, as a height above the
 *  surface beside it.  The family gives two answers, one for a plain
 *  strip and one for a structure.  Both are settled before anything is
 *  graded: so the grading compares at every station without asking
 *  again.  How high is high enough is the rule's, the comparison is
 *  arithmetic.  A strip that always stands clear answers a height
 *  nothing can be below. */
static struct
{
    float over;
    int   have;
} s_flies_run[NET_FAM_MAX][2];

void net_flies_runs_reset(void)
{
    memset(s_flies_run, 0, sizeof s_flies_run);
}

int net_flies_runs(void)
{
    return s_n_fam * 2;
}

const char *net_flies_run_at(int i, int *structure)
{
    int fi = i / 2;
    if (i < 0 || fi >= s_n_fam || s_fam[fi].flies || !s_fam[fi].rule[NH_FLIES])
        return NULL;
    *structure = i % 2;
    return s_fam[fi].rule[NH_FLIES];
}

void net_flies_run_is(int i, float over)
{
    int fi = i / 2;
    if (i < 0 || fi >= s_n_fam)
        return;
    s_flies_run[fi][i % 2].over = over;
    s_flies_run[fi][i % 2].have = 1;
}

/*  The height this strip stands clear past, or a height nothing reaches
 *  where the family answers nothing. */
float net_family_flies_over(const NetFamily *fam, const RLoft *d)
{
    int fi = (int)(fam - s_fam), k = d->struct_ ? 1 : 0;
    if (fi < 0 || fi >= s_n_fam || !s_flies_run[fi][k].have)
        return 1e30f;
    return s_flies_run[fi][k].over;
}

void net_family_traffic(const NetFamily *fam, const RLoft *d, int cls, float *lane_in, float *lane_out)
{
    int fi = (int)(fam - s_fam), k = cls - TRAFFIC_CLS_LO;
    if (fam->traffic)
    {
        fam->traffic(d, cls, lane_in, lane_out);
        return;
    }
    if (fi < 0 || fi >= s_n_fam || k < 0 || k >= TRAFFIC_CLASSES || !s_traffic_run[fi][k].have)
        return;
    *lane_in  = s_traffic_run[fi][k].in;
    *lane_out = s_traffic_run[fi][k].out;
}

int net_family_furniture(const NetFamily *fam, Loft *x)
{
    return fam->furniture ? fam->furniture(x) : 0;
}

