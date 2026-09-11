/*  spur.c: THE SPURS, as stores and fans.
 *
 *  Every spur tile the map carries, read once.  It says which side of it
 *  the slab lies on, and which the line.  It also says how it is
 *  oriented, and how far along the slab its descent runs.  It says where
 *  its foot aims, and how its join slides along the line's lane.
 *
 *  NOTHING HERE DECIDES ANY OF THAT.  Each is offered to a rule through
 *  a handle, and taken back.  The rules are arc.rules.orient, spur_side,
 *  spur_share, spur_span, spur_fork, spur_arm, spur_lane, spur_target
 *  and slide.  What is left is the arithmetic that turns an answer into
 *  a pose.  It also holds the chains queued for the drive to cut, and
 *  the lofts filed for the drive to lay. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "dump.h"
#include "mesh/internal.h"
#include "log.h"
#include "pipeline.h"
#include "net/internal.h"
#include "net/net.h"
#include "mesh/model.h"
#include "opt.h"

int gix_lane_line = -1;

/*  THE SPUR TILES: 0x5D to 0x60, one id per direction.
 *
 *  Each is one tile beside a slab and beside a surface line, on two
 *  adjacent sides.  The save's neighbors say so.  It is spec 7.3's "one
 *  1x1 tile adjacent to a slab edge, connected to a surface line".  In
 *  the shipped cities a pair of them flanks each line that crosses under
 *  a slab, one on either side.  That pair is a diamond.
 *
 *  The data does not grow, which is 7.3's hard constraint.  So the
 *  spur's form is derived on every build from what is free beside the
 *  slab, the way 7.3 sets out.
 *
 *      WHICH LANE   Traffic keeps right.  The lane nearest the spur
 *                   runs so that the spur is on its right.  North of an
 *                   east-west slab that is the westbound lane.  East of
 *                   a north-south one it is the northbound.
 *
 *      ON or OFF    An ON spur has the line upstream of it on that
 *                   lane.  Traffic comes off the line and merges
 *                   downstream.  An OFF spur has the line downstream.
 *                   Traffic leaves the slab upstream, descends, and
 *                   turns onto the line.  A diamond puts the OFF before
 *                   the cross street and the ON after it, which is what
 *                   the pairs are.
 *
 *      D            The free-air tiles along the slab's edge, in the
 *                   spur's own row.  They run downstream for an ON spur
 *                   and upstream for an OFF, capped at eight.  Free
 *                   means nothing is built on the tile, and nothing on
 *                   it stands higher than the spur's own ground.
 *
 *      THE FORM     D of 4 or more is PARALLEL.  The spur runs along
 *                   its row beside the slab.  It climbs over
 *                   min(D - 2, 6) tiles, then turns in over two more.
 *                   The turn-in is an S from the row's center onto the
 *                   slab's edge, tangent to it.  Half the spec's climb
 *                   at 4 or 5 is its "short parallel".  D under 4 is a
 *                   HELIX.  It is a spiral of one and a quarter turns
 *                   in the spur tile, turning toward the slab, so its
 *                   last quarter turn is the turn-in.  A short straight
 *                   then reaches the slab's edge.  The spec's hairpin
 *                   is not built yet, and the helix takes its place.
 *
 *  An OFF spur is an ON spur on the reversed lane, traversed backwards.
 *  It is built as one and then reversed, with the lift falling at its
 *  end instead of rising at its start.
 *
 *  A spur at a band's END feeds it head-on and goes straight across.  A
 *  spur is concrete from the line to the slab, and grades nothing. */

/*  How a spur tile sits.  It says which side its slab is on, and which
 *  its line.  It also gives the lane it serves by the right-hand rule,
 *  and the direction of travel on it.  Whether it is an OFF spur (the
 *  line downstream) or ON.  Answers 0 for a spur with no slab beside it,
 *  left to its sprite.  Sets `eside` for one at a band's end, head-on. */
/*  THE LINKS a tile's own piece claims, as the art shows them: which of
 *  its four edges carry the line's fill to the edge.  A rule reading the
 *  map for itself needs this, because it is not in the save.  It is what
 *  the pipeline made of the byte. */
int band_orient_links(const OrientFan *o, int32_t col, int32_t row)
{
    return tile_links((const RCity *)o->c, (const RAtlasLevel *)o->l, col, row, net_line->f);
}


/*  The sides the script settled on. */
/*  And the SPUR the rule made of the tile.  It is the reading above,
 *  turned into a spur lying along the slab.  It carries the side its
 *  taper falls on, and how far it reaches.  Answer none and the tile
 *  carries no spur. */
void band_orient_spur(OrientFan *o, const OrientFan *r)
{
    o->has = 1;
    o->ax = r->ax, o->ay = r->ay;
    o->tx = r->tx, o->ty = r->ty;
    o->off   = r->off;   /* the tile's own reading, which the join uses */
    o->r_off = r->r_off, o->len = r->len, o->opp = r->opp;
    o->fork = r->fork;
    o->arm  = r->arm;
    o->rdx = r->rdx, o->rdy = r->rdy;
    o->mdx = r->mdx, o->mdy = r->mdy;
}

void band_orient_answer(OrientFan *o, int kind, int dside, int rside, int eside, int off, int lines)
{
    o->kind  = kind;
    o->dside = dside;
    o->rside = rside;
    o->eside = eside;
    o->off   = off;
    o->lines = lines;
}

/*  Every on-spur tile read once, before any of them is built: which side
 *  of it the slab lies, which the line.  Which way round it runs are
 *  arc.rules.orient's, and they follow from the map alone. */

typedef struct
{
    int32_t   col, row;
    OrientFan o;
} OrientEntry;
static OrientEntry s_orient[ORIENTS_MAX];
static int s_n_orient;
static const RCity       *s_orient_c;
static const RAtlasLevel *s_orient_l;

/*  EVERY ON-SPUR TILE, as the script finds them.  Which cells carry a
 *  spur is arc.rules.spurs's, it walks the map itself, the way bands.lua
 *  walks it for the slabs.  This only holds what it hands back.  Nothing
 *  here reads the city. */
int net_spurs_begin(const RCity *c, const RAtlasLevel *l)
{
    s_n_orient = 0;
    s_orient_c = c;
    s_orient_l = l;
    return 1;
}

/*  One tile the script found, made current for the answers that follow.
 *  Answers 0 where there is no room left. */
int net_spur_at(int32_t col, int32_t row)
{
    OrientEntry *e;
    if (s_n_orient >= ORIENTS_MAX)
        return 0;
    e = &s_orient[s_n_orient++];
    memset(&e->o, 0, sizeof e->o);
    e->col     = col;
    e->row     = row;
    e->o.c     = s_orient_c;
    e->o.l     = s_orient_l;
    e->o.col   = col;
    e->o.row   = row;
    e->o.dside = e->o.rside = e->o.eside = -1;
    return 1;
}

/*  The tile the answers are about: the last one the script named. */
OrientFan *net_spur_current(void)
{
    return s_n_orient > 0 ? &s_orient[s_n_orient - 1].o : NULL;
}

static const OrientFan *orient_of(int32_t col, int32_t row)
{
    int i;
    for (i = 0; i < s_n_orient; ++i)
        if (s_orient[i].col == col && s_orient[i].row == row)
            return &s_orient[i].o;
    return NULL;
}



/*  Two spurs on one side of a slab whose tapers face each other would
 *  run their strips into one another.  The pairs are read here and the
 *  tiles between them divided by arc.rules.spur_share. */
static struct
{
    int   i, j;
    float gap;
} s_share[ORIENTS_MAX];
static int s_n_share;










/*  How a spur tile sits, as arc.rules.orient answered.  Which side its
 *  slab is on, which its line, the way it lies along the slab and the
 *  way the slab is.  Nothing is derived here: the rule worked all of it
 *  out from the tile's four neighbors and the map. */
int spur_orient(const RCity *c, int32_t col, int32_t row, int *dside, int *rside, int *eside, V2 *along, V2 *toward, int *off, int *lines)
{
    const OrientFan *op = orient_of(col, row);
    (void)c;
    *dside = *rside = *eside = -1;
    *off = *lines = 0;
    *along = *toward = (V2){0.0f, 0.0f};
    if (!op)
        return 0;
    *dside = op->dside, *rside = op->rside, *eside = op->eside;
    *lines = op->lines;
    if (op->kind != 1 || !op->has)
        return op->kind;
    *along  = (V2){op->ax, op->ay};
    *toward = (V2){op->tx, op->ty};
    *off    = op->off;
    return 1;
}

/*  THE SPURS, as the rule made them.  Every on-spur tile was offered to
 *  arc.rules.orient with its four neighbors.  What a spur lies along,
 *  which side its taper falls on and how far it reaches is that rule's
 *  answer, and this only files it. */
void band_lanes(const RCity *c)
{
    int i;
    (void)c;
    s_band_nspurs = 0;
    for (i = 0; i < s_n_orient && s_band_nspurs < MAX_SPURS; ++i)
    {
        const OrientFan *o = &s_orient[i].o;
        BandSpur          *rp;
        int32_t          d0c, d0r;
        if (!o->has)
            continue;
        d0c        = o->col + (int32_t)o->tx;
        d0r        = o->row + (int32_t)o->ty;
        rp         = &s_band_spurs[s_band_nspurs++];
        rp->rc     = o->col;
        rp->rr     = o->row;
        rp->c0     = (V2){(float)d0c + 0.5f + o->tx * 0.5f, (float)d0r + 0.5f + o->ty * 0.5f};
        rp->along  = (V2){o->ax, o->ay};
        rp->toward = (V2){o->tx, o->ty};
        rp->off    = o->r_off;
        rp->len    = o->len;
        rp->opp    = o->opp;
        rp->arm    = o->arm;
    }
    s_n_share = 0;
    {
        static int gix_pair_line = -1;
        int        i2, j;
        for (i2 = 0; i2 < s_band_nspurs; ++i2)
            for (j = i2 + 1; j < s_band_nspurs; ++j)
            {
                BandSpur *p = &s_band_spurs[i2], *q = &s_band_spurs[j];
                V2      tvp = p->off ? (V2){-p->along.x, -p->along.y} : p->along;
                V2      tvq = q->off ? (V2){-q->along.x, -q->along.y} : q->along;
                float   dx = (float)(q->rc - p->rc), dy = (float)(q->rr - p->rr), gap;
                if (p->toward.x != q->toward.x || p->toward.y != q->toward.y)
                    continue;
                if (fabsf(dx * p->toward.x + dy * p->toward.y) > geo_num(&gix_pair_line, "slab_pair_line"))
                    continue;
                if (tvp.x * dx + tvp.y * dy <= 0.0f || tvq.x * dx + tvq.y * dy >= 0.0f)
                    continue;
                gap  = fabsf(dx * p->along.x + dy * p->along.y) - 1.0f;
                if (s_n_share < ORIENTS_MAX)
                {
                    s_share[s_n_share].i   = i2;
                    s_share[s_n_share].j   = j;
                    s_share[s_n_share].gap = gap;
                    ++s_n_share;
                }
            }
    }
}

int net_spur_shares(void)
{
    return s_n_share;
}

int net_spur_share_at(int k, float *gap, int *cap)
{
    if (k < 0 || k >= s_n_share)
        return 0;
    *gap = s_share[k].gap;
    *cap = BAND_LANE_TAPER;
    return 1;
}

/*  The tiles the two share, as the rule divided them: each taper takes
 *  its half and no more. */
void net_spur_share_is(int k, int half)
{
    BandSpur *p, *q;
    if (k < 0 || k >= s_n_share || half < 0)
        return;
    p = &s_band_spurs[s_share[k].i];
    q = &s_band_spurs[s_share[k].j];
    if ((float)p->len > (float)half)
        p->len = half;
    if ((float)q->len > (float)half)
        q->len = half;
}

int s_spur_forms[6]; /* head-on, -, short taper, -, orphan, lane drop */

void spur_stats(void)
{
    if (s_spur_forms[0] + s_spur_forms[2] + s_spur_forms[4] + s_spur_forms[5] == 0)
        return;
    dumpf("on-spurs     %d lane drops (%d with a taper under two tiles), %d head-on, %d with no slab beside them\n", s_spur_forms[5], s_spur_forms[2], s_spur_forms[0], s_spur_forms[4]);
}


/*  The lane drop this tile is, and the slab station beside it: none, and the tile is not a spur of this kind. */
int spur_find(Spur *x)
{
    int32_t       col  = x->col;
    int32_t       row  = x->row;
    const BandSpur *rp   = x->rp;
    int           iref = x->iref;
    int           k;
    int           r;
    float         best = x->best;
    for (r = 0; r < s_band_nspurs; ++r)
        if (s_band_spurs[r].rc == col && s_band_spurs[r].rr == row)
            rp = &s_band_spurs[r];
    if (!rp)
    {
        if (s_pass != 1)
            ++s_spur_forms[4];
        band_spur_lost_is(x->col, x->row, 1);
        if (g_dev.lane_dump && s_pass != 1)
            dumpf("SPUR %d,%d lost: %s\n", (int)x->col, (int)x->row, "no spur record for the tile");
        return 1; /* not this tile */
    }
    float nearest = 1e9f; /* for the lane dump: how far the nearest station was when none was within reach */
    for (k = 0; k < s_band_nst; ++k)
    {
        float d = v2len((V2){s_band_st[k].pos.x - rp->c0.x, s_band_st[k].pos.y - rp->c0.y});
        if (d < nearest)
            nearest = d;
        if (d < best)
        {
            best = d;
            iref = k;
        }
    }
    if (iref < 0)
    {
        if (s_pass != 1)
            ++s_spur_forms[4];
        band_spur_lost_is(x->col, x->row, 2);
        if (g_dev.lane_dump && s_pass != 1)
            dumpf("SPUR %d,%d lost: no slab station within %.1f of c0 %.2f,%.2f (nearest %.2f of %d stations)\n", (int)x->col, (int)x->row, (double)s_tune.band_reach, (double)rp->c0.x, (double)rp->c0.y, (double)nearest, s_band_nst);
        return 1; /* no slab lofted beside it after all */
    }
    x->rp   = rp;
    x->iref = iref;
    x->k    = k;
    x->r    = r;
    x->best = best;
    return 0;
}

/*  What lies beyond the line tile: straight on (3), a through line across (2), a stub (1), neither (0).  And the way the line lane runs from the spur. */
int spur_classify(Spur *x)
{
    int32_t       col    = x->col;
    int32_t       row    = x->row;
    const BandSpur *rp     = x->rp;
    int           side   = x->side;
    int           fork;
    int           off    = x->off;
    V2            rd;
    V2            mdir   = x->mdir;
    V2            along  = x->along;
    V2            toward = x->toward;
    /*  The line the spur comes down to, as arc.rules.orient read it.  It
     *  gives the way to the line tile, and the way the lane leaves along
     *  it.  It also gives what the line tile is.  Nothing is read from
     *  the map here: the rule walked it. */
    {
        const OrientFan *op = orient_of(col, row);
        rd   = op ? (V2){op->rdx, op->rdy} : (V2){0.0f, 0.0f};
        mdir = op ? (V2){op->mdx, op->mdy} : mdir;
        fork = op ? op->fork : 0;
    }
    x->rp     = rp;
    x->side   = side;
    x->fork   = fork;
    x->off    = off;
    x->rd     = rd;
    x->mdir   = mdir;
    x->along  = along;
    x->toward = toward;
    return 0;
}
