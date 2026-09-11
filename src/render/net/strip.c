/*  strip.c: THE LOFT AS THE NETWORK DRIVES IT.
 *
 *  `w:loft(pieces, profile)` is the generic service and lives in
 *  mesh/loft.c: it knows a chain of pieces and a cross-section and
 *  nothing else.  This is the other half.  They are the ELEVEN MOMENTS
 *  the drive stops at while one strip is lofted.  Each offers a reading
 *  to the rule the family named, and takes its answer back.  They are
 *  the taper, the profile, the drop, the works, the record, the
 *  furniture, the slab, the overlay.
 *
 *  Nothing here decides any of them. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "dump.h"
#include "mesh/internal.h"
#include "opt.h"
#include "pipeline.h"
#include "net/net.h"
#include "script.h"

static const char *s_lx_furn_rule; /* the props rule the family named, for the take */


/*  The strip whose SLAB is still to be laid, and the shape its triangles
 *  belong to.  The loft leaves that shape open and the drive lays the
 *  slab inside it.  So the slab's triangles belong to the strip like
 *  every other stage's. */
static Loft    s_slab_x;
int     s_slab_ready;
uint32_t s_slab_sh;
static double  s_slab_tp;
static int     s_slab_records_only;
/*  The corridor's profile over the ground, gathered: arc.rules.ground
 *  spurs it between the nodes at its ends. */
static void loft_ground_fan(Loft *x, GroundFan *out)
{
    GroundFan g;
    memset(&g, 0, sizeof g);
    g.smp      = x->smp;
    g.city     = x->c;
    g.n        = x->ns;
    g.total    = x->total;
    g.pin0     = x->pin0;
    g.pin1     = x->pin1;
    g.dead0    = s_ld->nkind[0] == 1;
    g.dead1    = s_ld->nkind[1] == 1;
    g.pin_node = s_ld->fam->turnout <= 0.0f;
    g.lift     = net_family_rules(s_ld->fam->f)->lift;
    *out       = g;
}
static int loft_profile_pre(Loft *x)
{
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    Sample      *smp      = x->smp;
    float       *zraw     = x->zraw;
    int          ns       = x->ns;
    int          i;
    /*  The profile: at each station the ground under the cross-section's
     *  center, and the band lies on the corridor's formation a hair
     *  proud of it.  The corridor's tiles are graded to this same line
     *  in the second pass, so the two cannot cross.  The hair is the
     *  wearing surface over the formation, a few centimeters of it. */
    for (i = 0; i < ns; ++i)
        smp[i].z = zraw[i] + net_family_rules(s_ld->fam->f)->lift + s_ld->raise;
    /*  On the second pass the corridor has already been cut: the shelf
     *  under the band IS the surface this segment was fitted to.  So the
     *  band lays flat on it rather than measuring the ground again and
     *  riding the highest point across its own width.  The shelf is flat
     *  across, so a band on it is flat across too. */
    if (s_pass == 2)
        for (i = 0; i < ns; ++i)
        {
            int32_t tc = (int32_t)floorf(smp[i].pos.x), tr = (int32_t)floorf(smp[i].pos.y);
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            /*  The band lays on the corridor, read through the very
             *  function the terrain is drawn with.  That is the tile's
             *  own two triangles, cut on its own diagonal.  Evaluating
             *  it as a bilinear instead left the band under the drawn
             *  surface by the quad's twist.  This is what the clip check
             *  kept finding at a hundredth of a level.  Off its own
             *  corridor.  A level meet is a tile with two shelves, and
             *  the terrain draws one.  The band still never lies under
             *  the drawn ground (a line five centimeters under the
             *  line's shelf the day it was let off its tiles). */
            float zg = surface_at_world(c, mask_bit, smp[i].pos.x, smp[i].pos.y) + net_family_rules(s_ld->fam->f)->lift_min + s_ld->raise;
            if (s_corr[tr * GRID + tc] == 1 && s_corr[tr * GRID + tc + 1] == 1 &&
                s_corr[(tr + 1) * GRID + tc] == 1 && s_corr[(tr + 1) * GRID + tc + 1] == 1)
                smp[i].z = zg;
            else if (smp[i].z < zg)
                smp[i].z = zg;
        }

    /*  The corridor's profile: a spur between the altitudes of the NODES
     *  at its ends, and through any level meet on the way.  A node is a
     *  junction, a dead end or a lap.  It stands at its own tile's
     *  leveled height, and every corridor that reaches it spurs to that
     *  one number.  Two segments meeting at a junction therefore agree,
     *  with nothing solved between them.  Two families agree at a lap
     *  because the lap is a node they share.  An edit moves only the
     *  segments whose anchors moved.  The spur is eased at both ends.
     *  So a corridor leaves a node level and picks up its grade in
     *  between rather than kinking at the join.
     *
     *  A band takes no part in it: the band walk sets no nodes, so a
     *  slab must never be pinned to them.  A slab's heights are the
     *  ground under it and the lift over that, a spur's its own straight
     *  line (band.c). */
    return 0;
}
/*  And what the profile leaves behind it: the ground's own line, so a
 *  station below it reads as being in a cut. */
static int loft_profile_post(Loft *x)
{
    float *zraw = x->zraw;
    int    ns   = x->ns;
    int    i;
    for (i = 0; i < ns; ++i)
        s_zorig[i] = zraw[i] + net_family_rules(s_ld->fam->f)->lift; /* the ground's own line: a station below it is in a cut */
    /*  The slab stands clear (spec 7.2): 5 m under the soffit plus the
     *  girder is about 7.5 m to the line surface.  The vertical unit
     *  here is the altitude level, seven to eight meters.  So a little
     *  over one level, applied after the profile is settled so the slab
     *  follows the ground's shape while riding above it.  And it is a
     *  structure, not a carpet.  Its support line may rise or fall no
     *  faster than a sixth of a level a tile.  So it runs straight over
     *  what the ground does under it and the columns take up the
     *  difference.
     *
     *  The piers: one per segment boundary, every two tiles, which is
     *  the spec's 30 m span (7.2).  The type comes from what is under
     *  the slab there.  Over nothing, a lot or bare ground, it is a
     *  single hammerhead on the centerline carrying a cap the full width
     *  of the slab.  Over a surface line, a two-column bent with the
     *  columns outside the way, never in a lane.  The cap spans both
     *  tiles of the band.  So it is laid as two halves, each carrying
     *  the painter's order of the tile it is in.  One order for a piece
     *  that straddles the seam would put half the cap in front of the
     *  slab over the other tile. */
    return 0;
}
/*  The corridor surface under the strip, once its record and its
 *  furniture are laid. */
static int loft_record_surface(Loft *x)
{
    return loft_surface(x->c, x->mask_bit, x->smp, x->ns, x->hw, s_ld);
}
/*  --prof-dump: the finished profile of every segment. */
static int loft_prof_dump(Loft *x)
{
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    const Family f        = x->f;
    Sample      *smp      = x->smp;
    float        hw       = x->hw;
    int          ns       = x->ns;
    /*  --prof-dump 1 prints the finished profile of every segment: the
     *  distance along, the ground under the cross-section.  The height
     *  the band was given. tools/profile.py draws it, which is how the
     *  grade smoothing is looked at. */
    if (g_dev.prof_dump)
    {
        int d;
        dumpf("PROF f=%d band=%d n=%d\n", (int)f, s_ld->fam == net_band, ns);
        for (d = 0; d < ns; ++d)
        {
            float g = section_height(c, mask_bit, smp[d].pos, smp[d].dir, hw);
            dumpf("  %.4f %.4f %.4f %.3f %.3f  dir %.3f,%.3f  w %.3f/%.3f\n", (double)smp[d].s, (double)g, (double)smp[d].z, (double)smp[d].pos.x, (double)smp[d].pos.y, (double)smp[d].dir.x, (double)smp[d].dir.y, (double)smp[d].wl, (double)smp[d].wr);
        }
    }
    return 0;
}
/*  What the strip is, in the loft's own terms, for the inspector.  Its
 *  kind and its ends, the pieces it was fitted as, its stations and the
 *  heights they were given, its width.  What its ends were told.  Lines
 *  of key, TAB, value.  The first names the thing. */
static void loft_note(const Loft *x)
{
    static const char *const KIND[4] = {"open", "dead end", "junction", "?"};
    static const char *const CTRL[4] = {"", ", stop", ", signal", ""};
    const RLoft             *d       = x->d;
    const char              *seat;
    char                     buf[1024];
    size_t                   n   = 0;
    float                    zlo = 1e9f, zhi = -1e9f;
    int                      k;
    if (d->kind == LOFT_LINE || d->kind == LOFT_THREAD)
        note_add(buf, sizeof buf, &n, "ends\t%s %d,%d%s to %s %d,%d%s", KIND[d->nkind[0] & 3], (int)d->node[0][0], (int)d->node[0][1], CTRL[d->ctrl[0] & 3], KIND[d->nkind[1] & 3], (int)d->node[1][0], (int)d->node[1][1], CTRL[d->ctrl[1] & 3]);
    if (d->cls >= 0.0f)
        note_add(buf, sizeof buf, &n, "\nclass\t%g", (double)d->cls);
    note_add(buf, sizeof buf, &n, "\npieces\t%d over %.2f tiles:", x->np, (double)x->total);
    for (k = 0; k < x->np && k < 8; ++k)
    {
        const Piece *p = &x->pc[k];
        if (p->arc)
            note_add(buf, sizeof buf, &n, "%s arc r %.2f over %.0f deg", k ? "," : "", (double)p->r, (double)(fabsf(p->t1 - p->t0) * 57.29578f));
        else
            note_add(buf, sizeof buf, &n, "%s run %.2f", k ? "," : "", (double)p->len);
    }
    if (x->np > 8)
        note_add(buf, sizeof buf, &n, ", and %d more", x->np - 8);
    for (k = 0; k < x->ns; ++k)
    {
        if (x->smp[k].z < zlo)
            zlo = x->smp[k].z;
        if (x->smp[k].z > zhi)
            zhi = x->smp[k].z;
    }
    if (x->ns > 1)
        note_add(buf, sizeof buf, &n, "\nstations\t%d, %.3f tiles apart", x->ns, (double)(x->total / (float)(x->ns - 1)));
    seat = d->kind == LOFT_SLAB ? "carried on its columns" : d->kind == LOFT_SPUR ? "a structure between line and slab" : "on the graded ground";
    if (x->ns > 0)
        note_add(buf, sizeof buf, &n, "\nprofile\t%.2f to %.2f, %s", (double)zlo, (double)zhi, seat);
    if (d->spur0 > 0.0f || d->spur1 > 0.0f)
        note_add(buf, sizeof buf, &n, "\nlift\ttapers over %.2f tiles at the start, %.2f at the end", (double)d->spur0, (double)d->spur1);
    if (d->raise > 0.0f)
        note_add(buf, sizeof buf, &n, "\nraise\t%.3f over its usual seat", (double)d->raise);
    note_add(buf, sizeof buf, &n, "\nwidth\thalf %.2f tiles", (double)x->hw);
    if (d->taper > 0.0f)
        note_add(buf, sizeof buf, &n, ", narrowing to %.2f over %.2f at the %s", (double)d->hw_end, (double)d->taper, d->taper_start ? "start" : "end");
    note_add(buf, sizeof buf, &n, "\npinned\t%s", d->pin0 && d->pin1 ? "both ends" : d->pin0 ? "the start" : d->pin1 ? "the end" : "neither end");
    shape_note("%s", buf);
}
static GroundFan s_lx_ground;


/*  A primitive that leaves a reading of its own for the rule the drive
 *  asks next: a slab's profile stage measures the slab's elevation.
 *  That is what the rule is handed rather than the strip. */
static void       *s_stage_obj;
static const char *s_stage_kind;

void net_stage_hand(void *obj, const char *kind)
{
    s_stage_obj = obj, s_stage_kind = kind;
}

void *net_stage_taken(const char **kind)
{
    *kind = s_stage_kind;
    return s_stage_obj;
}

/*  The loft in hand, for a stage a rule answers: the same record the
 *  loft's own stages read. */
Loft *net_loft_working(void)
{
    return s_lx_live ? &s_lx : NULL;
}

/*  THE TAPER, where a spur narrows toward the line it meets (band.c).
 *  Answers the rule that settles it, or nothing where the pipeline's own
 *  primitive already has. */
const char *net_loft_taper(void)
{
    if (!s_lx_live || !net_family_has(s_ldv.fam, NH_TAPER))
        return NULL;
    net_family_taper(s_ldv.fam, &s_lx);
    return net_family_stage_rule(s_ldv.fam, NH_TAPER);
}

/*  THE PROFILE: where the strip's stations sit.  A family that declares
 *  one of its own is asked for it.  Every other corridor spurs between
 *  the altitudes of the nodes at its ends, which is arc.rules.ground's. */
const char *net_loft_profile(GroundFan **g)
{
    *g = NULL;
    net_stage_hand(NULL, NULL); /* whatever the last strip's primitive left is not this one's */
    if (!s_lx_live)
        return NULL;
    if (loft_profile_pre(&s_lx) != 0)
        return NULL;
    if (net_family_has(s_ldv.fam, NH_PROFILE))
    {
        const char *kind;
        net_family_profile(s_ldv.fam, &s_lx);
        /*  A primitive that settled the stage on its own, a slab too
         *  short to shape, leaves nothing for a rule to be asked. */
        if (net_family_stage_primitive(s_ldv.fam, NH_PROFILE) && net_stage_taken(&kind) == NULL)
            return NULL;
        return net_family_stage_rule(s_ldv.fam, NH_PROFILE);
    }
    loft_ground_fan(&s_lx, &s_lx_ground);
    *g = &s_lx_ground;
    return "ground";
}

/*  WHAT THE PROFILE STAGE LEFT until its own rule had answered.  A slab
 *  reads what its spurs took from it only once its heights are settled. */
const char *net_loft_dropped(void)
{
    net_stage_hand(NULL, NULL);
    if (!s_lx_live || !net_family_has(s_ldv.fam, NH_PROFILE))
        return NULL;
    if (net_family_profile_done(s_ldv.fam, &s_lx) != 0)
        return NULL;
    return net_family_stage_rule_after(s_ldv.fam, NH_PROFILE);
}

/*  THE WORKS: the piers and the like a slab stands on, before the slab
 *  (band.c).  The profile is settled by the time this runs, so what it
 *  leaves behind, the ground's own line, is taken up here. */
const char *net_loft_works(void)
{
    double tq;
    if (!s_lx_live)
        return NULL;
    if (loft_profile_post(&s_lx) != 0)
        return NULL;
    tq = prof_now(), net_prof_add(NET_PROF_PROFILE, tq - s_lx_tp), s_lx_tp = tq;
    if (!s_ldv.records_only)
        loft_note(&s_lx); /* what it is, for the inspector, before anything of it is drawn */
    if (!net_family_has(s_ldv.fam, NH_WORKS) && !net_family_stations(s_ldv.fam))
        return NULL;
    net_family_works(s_ldv.fam, &s_lx);
    return net_family_stage_rule(s_ldv.fam, NH_WORKS);
}

/*  THE RECORD: what the strip leaves for the traffic and the passes that
 *  read it, the furniture beside it and the corridor surface under it.
 *  The slab follows.  It is the only stage that draws the way.  So the
 *  loft ends with everything it worked out still standing and the strip
 *  composed from outside. */
/*  THE RECORD is what the strip leaves for the traffic and the passes
 *  that read it.  It holds a line's graph edge and its two margins, a
 *  thread's thread, a slab's edge under its own class (line.c, thread.c,
 *  band.c).  The grading pass records nothing: it lays the corridor and
 *  no more. */
/*  HOW FAR THE NEAREST LEVEL MEET IS, at every station of a strip that
 *  is crossed at grade.  A family that declares `crossed` gets it.
 *  Scripts/compose/strip.lua reads it: the approach markings on a line
 *  run between one distance and another either side of a meet.  Which
 *  bytes are a meet is arc.rules.lap_tiles's, and how near the tile's
 *  center a station must run to count is the family's own knob. */
void net_strip_laps(Loft *x)
{
    const RCity *c   = x->c;
    Sample      *smp = x->smp;
    int          ns  = x->ns;
    int          i;
    {
        static float xs[64];
        int          nx = 0;
        for (i = 0; i < ns && nx < 64; ++i)
        {
            int32_t col = (int32_t)floorf(smp[i].pos.x), row = (int32_t)floorf(smp[i].pos.y);
            uint8_t b;
            if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
                continue;
            b = c->xbld[row * R_MAP + col];
            if (net_line_lapped(b) && fabsf(smp[i].pos.x - (float)col - 0.5f) < net_family_rules(x->d->f)->lap_centre &&
                fabsf(smp[i].pos.y - (float)row - 0.5f) < net_family_rules(x->d->f)->lap_centre)
                xs[nx++] = smp[i].s;
        }
        for (i = 0; i < ns; ++i)
        {
            float best = 1e9f;
            int   k2;
            for (k2 = 0; k2 < nx; ++k2)
                if (fabsf(smp[i].s - xs[k2]) < best)
                    best = fabsf(smp[i].s - xs[k2]);
            smp[i].xd = best;
        }
    }
}

const char *net_loft_record(void)
{
    double tq;
    if (!s_lx_live)
        return NULL;
    tq = prof_now(), net_prof_add(NET_PROF_SLAB_WORKS, tq - s_lx_tp), s_lx_tp = tq;
    if (grade_only(g_dev.grade_loft))
        return NULL;
    if (s_lx.ns < 2 || (!net_family_has(s_ldv.fam, NH_RECORD) && !net_family_graphed(s_ldv.fam)))
        return NULL;
    net_family_record(s_ldv.fam, &s_lx);
    return net_family_stage_rule(s_ldv.fam, NH_RECORD);
}

/*  Signals, lighting, and everything else that stands BESIDE the line
 *  rather than being part of the ribbon. */
int loft_furniture(Loft *x)
{
    if (!furniture_on())
        return 0;
    return net_family_has(x->d->fam, NH_FURNITURE) ? net_family_furniture(x->d->fam, x) : 0;
}

/*  THE FURNITURE beside the strip: one family's lamps, another's signs
 *  and signals.  Where each of them stands along the strip is the
 *  rule's.  The walk that turns a distance into a place on the map is
 *  not. */
const char *net_loft_furniture(void)
{
    s_lx_furn_rule = NULL;
    if (!s_lx_live || grade_only(g_dev.grade_loft))
        return NULL;
    net_family_record_done(s_ldv.fam, &s_lx);
    /*  A family that names a props rule has it stand them itself.  One
     *  that lends a stage has the stage gather first. */
    if (!net_family_has(s_ldv.fam, NH_FURNITURE))
    {
        s_lx_furn_rule = net_family_props(s_ldv.fam);
        return s_lx_furn_rule;
    }
    net_family_furniture(s_ldv.fam, &s_lx);
    s_lx_furn_rule = net_family_stage_rule(s_ldv.fam, NH_FURNITURE);
    return s_lx_furn_rule;
}

int net_loft_recorded(void)
{
    double tq;
    if (!s_lx_live)
        return 0;
    s_lx_live = 0;
    if (grade_only(g_dev.grade_loft))
    {
        if (loft_record_surface(&s_lx) != 0) /* the grading pass: the corridor and nothing else */
            return -1;
        s_slab_tp = s_lx_tp;
        return 0;
    }
    if (net_family_furniture_done(s_ldv.fam, &s_lx) != 0)
        return -1;
    if (loft_record_surface(&s_lx) != 0)
        return -1;
    tq = prof_now(), net_prof_add(NET_PROF_RECORD, tq - s_lx_tp), s_lx_tp = tq;
    /*  The slab's own time runs from the end of the record, not from
     *  wherever the composer is reached. */
    s_slab_tp = s_lx_tp;
    if (s_ldv.records_only)
        return 0; /* nothing of it is drawn this build */
    if (loft_prof_dump(&s_lx) != 0)
        return -1;
    s_slab_ready        = 1;
    s_slab_x            = s_lx;
    s_slab_records_only = s_ldv.records_only;
    return 0;
}

static int net_loft_draws(void);

/*  The strip the loft just worked out, for whoever composes it: the same
 *  record the loft's own stages read, still standing.  Answers 0 where
 *  the loft drew nothing worth composing. */
Loft *net_loft_strip(void)
{
    return s_slab_ready && net_loft_draws() ? &s_slab_x : NULL;
}

/*  And the slab itself, once it is composed: the profile the pass keeps
 *  is the slab's.  So it is closed here rather than by the composer. */
static void net_loft_slab_done(double tp)
{
    double tq = prof_now();
    net_prof_add(NET_PROF_SLAB, tq - tp);
    if (s_ldv.fam == net_band)
        net_prof_add(NET_PROF_SLAB_SLAB, tq - tp);
    s_slab_ready = 0;
}

/*  Whether the slab is drawn at all: the grading pass lays no triangles. */
static int net_loft_draws(void)
{
    return !grade_only(g_dev.grade_loft);
}

/*  The strip the loft finished, for the drive to lay the fitted line
 *  over the world it made.  Arc.rules.curves, from the same stations and
 *  pieces the strip itself is laid from.  When the tuning window asks to
 *  see it.  Nothing is shown of a strip this build draws no part of, and
 *  the grading pass draws none of them. */
Loft *net_loft_curves(void)
{
    if (!s_slab_ready || s_slab_records_only || s_tune.show_curves <= 0.5f || s_pass == 1)
        return NULL;
    return &s_slab_x;
}

/*  And the strip's shape closed, once the drive has composed it: the
 *  profile the pass keeps is the slab's, so it is counted here. */
int net_loft_close(void)
{
    if (s_slab_ready)
        net_loft_slab_done(s_slab_tp);
    shape_close(s_slab_sh);
    s_slab_sh = SHAPE_NONE;
    return 0;
}

/*  The strip is one width in the world whatever direction it runs.  The
 *  oblique camera then draws a line toward it wider than one across it,
 *  as it draws everything else.  (A build that scaled the world width by
 *  direction to equalise the screen width was rejected: "you are not
 *  keeping line widths consistent".) */
float width_factor(float dx, float dy, int compensate)
{
    (void)dx;
    (void)dy;
    (void)compensate;
    return 1.0f;
}

/*  ==================================================================
 *  The loft as a service
 *
 *  loft_sweep takes pieces and a cross-section and lays the surface
 *  between them.  It has no idea what it is drawing: the section is a
 *  list of points across the centerline, each carrying the material of
 *  the face that leaves it, and a station is a place along the path
 *  where that section is stood up.  A line, a lip, a parapet and a
 *  culvert are all the same sweep over a different list of numbers,
 *  which is the whole point of it being a service.
 *  ================================================================== */

/*  Where the section stands at one station: the seat, and the two
 *  vectors the section's across and up run along. */
typedef struct
{
    V2    pos, side;
    float seat, s;
} SweepSt;

/*  Every station along the pieces, at the density the caller asked for.
 *  A step of nought puts a station at each piece's two ends and nowhere
 *  between.  This is exact for a straight and coarse for an arc: how
 *  finely a curve reads is the caller's to say and not this file's. */
static int sweep_stations(const RCity *c, uint8_t mask_bit, const Piece *pc, int np,
                          const LoftSweep *how, SweepSt *st, int cap)
{
    int   k, i, ns = 0;
    float s = 0.0f;
    for (k = 0; k < np; ++k)
    {
        const Piece *p    = &pc[k];
        float        step = p->arc ? how->step_arc : how->step_run;
        int          nd   = step > 1e-6f ? (int)ceilf(p->len / step) : 1;
        if (nd < 1)
            nd = 1;
        for (i = (ns ? 1 : 0); i <= nd && ns < cap; ++i)
        {
            float t = p->len * (float)i / (float)nd;
            V2    pos, dir;
            piece_at(p, t, &pos, &dir);
            st[ns].pos  = pos;
            st[ns].side = (V2){-dir.y, dir.x};
            st[ns].s    = s + t;
            st[ns].seat = how->pinned ? how->z
                                      : surface_at_world(c, mask_bit, pos.x, pos.y) + how->lift;
            ++ns;
        }
        s += p->len;
    }
    return ns;
}

/*  One point of the section, stood up at one station. */
static void sweep_point(const SweepSt *st, const LoftRung *r, float out[3])
{
    out[0] = st->pos.x + st->side.x * r->across;
    out[1] = st->pos.y + st->side.y * r->across;
    out[2] = st->seat + r->up;
}

int loft_sweep(RMesh *m, const RCity *c, uint8_t mask_bit, const Piece *pc, int np,
               const LoftRung *sec, int nsec, const LoftSweep *how)
{
    static SweepSt st[LOFT_MAX_ST];
    int            ns, i, k, faces = 0, flat = 1;
    int            rungs;
    if (!m || !c || !pc || np < 1 || !sec || nsec < 2 || !how)
        return 0;
    ns = sweep_stations(c, mask_bit, pc, np, how, st, LOFT_MAX_ST);
    if (ns < 2)
        return 0;
    /*  A section lying flat on the ground at every point is DRAPED.
     *  Each piece takes the drawn surface at its own corners.  This is
     *  how a line band lies on the terrain it was graded to.  One point
     *  off the ground and the whole section keeps its own heights
     *  instead.  The two would part company along a fold. */
    if (how->pinned || how->lift != 0.0f)
        flat = 0;
    for (k = 0; flat && k < nsec; ++k)
        if (sec[k].up != 0.0f)
            flat = 0;
    rungs = how->closed ? nsec : nsec - 1;
    for (i = 0; i + 1 < ns; ++i)
    {
        for (k = 0; k < rungs; ++k)
        {
            const LoftRung *r0 = &sec[k], *r1 = &sec[(k + 1) % nsec];
            float           p[4][3], tri[3][3], ref[3], ref2[3];
            float           col[3];
            sweep_point(&st[i], r0, p[0]);
            sweep_point(&st[i], r1, p[1]);
            sweep_point(&st[i + 1], r1, p[2]);
            sweep_point(&st[i + 1], r0, p[3]);
            col[0] = 0.0f, col[1] = 0.0f, col[2] = r0->mat;
            memcpy(tri[0], p[0], sizeof tri[0]);
            memcpy(tri[1], p[1], sizeof tri[1]);
            memcpy(tri[2], p[2], sizeof tri[2]);
            ref[0] = r0->across, ref[1] = r1->across, ref[2] = r1->across;
            ref2[0] = st[i].s, ref2[1] = st[i].s, ref2[2] = st[i + 1].s;
            if (flat)
            {
                if (put_tri_ground(m, c, mask_bit, how->slot, (const float (*)[3])tri, col, ref, ref2) != 0)
                    return -1;
            }
            else if (put_tri_line_n(m, c, mask_bit, how->slot, (const float (*)[3])tri, NULL, col, ref, ref2) != 0)
                return -1;
            memcpy(tri[0], p[0], sizeof tri[0]);
            memcpy(tri[1], p[2], sizeof tri[1]);
            memcpy(tri[2], p[3], sizeof tri[2]);
            ref[0] = r0->across, ref[1] = r1->across, ref[2] = r0->across;
            ref2[0] = st[i].s, ref2[1] = st[i + 1].s, ref2[2] = st[i + 1].s;
            if (flat)
            {
                if (put_tri_ground(m, c, mask_bit, how->slot, (const float (*)[3])tri, col, ref, ref2) != 0)
                    return -1;
            }
            else if (put_tri_line_n(m, c, mask_bit, how->slot, (const float (*)[3])tri, NULL, col, ref, ref2) != 0)
                return -1;
            faces += 2;
        }
    }
    return faces;
}
