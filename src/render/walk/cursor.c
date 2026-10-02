/*  cursor.c: THE WALK AS THE DRIVE STEPS IT.
 *
 *  Every cursor scripts/compose/world.lua turns is here.  The junctions,
 *  in the order the walk visits them.  The fits waiting to be settled.
 *  The classes a segment may be given.  The boxes and the segments the
 *  drawing pass hands over, one at a time.
 *
 *  Walking a segment is walk.c's own business.  This is the ORDER the
 *  build asks for them in.  Nothing here decides what any of them
 *  becomes: a cursor opens a thing, the script is handed it, and the
 *  answer is taken back. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "dump.h"
#include "incr.h"
#include "log.h"
#include "mesh/internal.h"
#include "mesh/model.h"
#include "net/net.h"
#include "opt.h"
#include "pipeline.h"
#include "script.h"
#include "walk/internal.h"
#include "mesh/fit.h"
#include "net/cell.h"
#include "walk/cursor.h"
#include "walk/walk.h"

/*  Every junction on the map, in the order the walk visits them, so the
 *  script can be asked for each one's ring. */
int build_junction_count(const RCity *c)
{
    int fk, n = 0;
    (void)c;
    for (fk = 0; fk < net_n_walked; ++fk)
        n += net_disc_junctions(fk);
    return n;
}

int build_junction_nth(const RCity *c, int i, Family *f, int32_t *ocol, int32_t *orow, int *olinks)
{
    int fk, k, n = 0;
    for (fk = 0; fk < net_n_walked; ++fk)
    {
        Family fam = net_walked[fk]->f;
        k          = net_disc_junctions(fk);
        if (i - n < k)
        {
            int32_t cell = net_disc_junction(fk, i - n);
            *f           = fam;
            *ocol        = cell % R_MAP;
            *orow        = cell / R_MAP;
            *olinks      = eff_links(c, *ocol, *orow, fam);
            return 1;
        }
        n += k;
    }
    return 0;
}
/*  ------------------------------------------------------------------
 *  The classes, read before anything is fitted
 *
 *  One class for a whole segment settles how wide it is, where its lanes
 *  run and what is painted on it.  So it has to be known before the fit.
 *  The walk here does nothing but step the tiles of every segment and
 *  count how many of them read as each class.  The drive settles each
 *  one and hands it back, and the walks that follow look the answer up.
 *
 *  It reaches the same segments in the same order as the pass that
 *  follows it, because seg_walk reads the map and its own visited marks
 *  and nothing either walk writes.
 *  ------------------------------------------------------------------ */

/*  ------------------------------------------------------------------
 *  The fits, run by the drive
 *
 *  Every segment's path is a pure function of the tiles it covers and
 *  the nodes at its ends.  So the walk asks for none of them.  The pass
 *  below reads what each fit needs into the lines' arena (net/fits.c).
 *  The drive runs each in turn, and the walk looks the answer up.
 *
 *  A family whose runs may leave its own cells, one sweeping across
 *  the field, is fitted TWICE, once held to its tiles and once free.
 *  Which of the two to keep is arc.rules.fit_choice's.
 *  ------------------------------------------------------------------ */

static void seg_class_ask(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, Family f, const NetRun *run, uint8_t *visited)
{
    Seg              x;
    const NetFamily *fam = net_family(f);
    int32_t          col = run->cell[0] % R_MAP, row = run->cell[0] / R_MAP;
    int              e   = net_run_edge(run);
    if (visited[(row * R_MAP + col) * 4 + e])
        return;
    visited[(row * R_MAP + col) * 4 + e] = 1;
    memset(&x, 0, sizeof x);
    x.m = m, x.c = c, x.mask_bit = mask_bit, x.comp = comp, x.f = f, x.col = col, x.row = row, x.e = e, x.visited = visited;
    x.pts = s_wk_pts, x.q = s_wk_q, x.rad = s_wk_rad, x.tlim = s_wk_tlim;
    x.tcol = s_wk_tcol, x.trow = s_wk_trow, x.pieces = s_wk_pieces, x.marks = s_wk_marks;
    x.hw    = *fam->width * 0.5f;
    x.kind0 = node_kind(c, f, col, row);
    x.cc = col, x.cr = row, x.back = (e + 2) & 3, x.ee = e;
    if (seg_from_cells(&x, run) != 0 || x.n < 2)
        return;
    if (fam->classed)
    {
        int cnt[3];
        seg_class_counts(&x, cnt);
        if (seg_class_file(col, row, e, cnt, x.tcol, x.trow, x.nt) != 0)
            return;
    }
    /*  The corridor is the segment's own tiles, its gates the crossable
     *  part of each shared edge.  The path is the taut string through
     *  them.  It cuts every corner of a staircase into one diagonal by
     *  itself, with each corner then swept as wide as the corridor
     *  allows.  The corridor is tested against the line's own half
     *  width, not a fraction of it: what has to fit inside the corridor
     *  is the line. */
    if (fam->free_reach > 0)
    {
        fits_add_tiles(fits_lines(), fam, f, col, row, e, &x, 1, fam->free_reach);
        fits_add_tiles(fits_lines(), fam, f, col, row, e, &x, 2, fam->free_reach);
    }
    else
        fits_add_tiles(fits_lines(), fam, f, col, row, e, &x, 0, 0);
}

int build_networks_classes(RMesh *m, const RCity *c, uint8_t mask_bit, int comp)
{
    static uint8_t visited[R_MAP * R_MAP * 4];
    int            fk;
    seg_class_reset();
    net_cut_reset(); /* the fits queue their cuts as they are kept */
    fits_reset(fits_lines());
    for (fk = 0; fk < net_n_walked; ++fk)
    {
        /*  The same runs the measuring walk reads, in the same order:
             both step through the network the script discovered, so the
             class a segment is given and the segment it is given to
             cannot be about different runs. */
        Family fam = net_walked[fk]->f;
        int    i, n = net_disc_count(fk);
        memset(visited, 0, sizeof visited);
        for (i = 0; i < n; ++i)
        {
            NetRun run;
            if (net_disc_run_get(fk, i, &run))
                seg_class_ask(m, c, mask_bit, comp, fam, &run, visited);
        }
    }
    return seg_class_asks();
}



/*  ------------------------------------------------------------------
 *  The drawing pass, walked by the drive
 *
 *  The pass is a cursor, not a loop: the drive asks a family for its
 *  junctions, then for one thing at a time until there is nothing left.
 *  The order is the SCRIPT'S: the network it discovered, entry by
 *  entry: and the junctions come first so a leg knows whether it is
 *  signaled before it draws its stripe.
 *  ------------------------------------------------------------------ */
static struct
{
    RMesh             *m;
    const RCity       *c;
    uint8_t            mask_bit;
    int                comp;
    Family             fam;
    int                fk; /* which walked family: its list in the store */
    int                at; /* how far down that list the cursor has come */
} s_draw;
static uint8_t s_draw_visited[R_MAP * R_MAP * 4];
static double  s_draw_t;

int build_networks_draw(RMesh *m, const RCity *c, uint8_t mask_bit, int comp)
{
    double tt = tms();
    (void)m, (void)c, (void)mask_bit, (void)comp;
    tnote("stage three: junction shapes", tt);
    s_draw_t = tms();
    return 0;
}

int build_draw_families(void)
{
    return net_n_walked;
}

/*  One family's junctions, all of them, before any of its segments.  It
 *  is a cursor like the one below, because the drive lays the fill on
 *  each outline between the two halves of its box. */
static struct
{
    RMesh             *m;
    const RCity       *c;
    uint8_t            mask_bit;
    Family             fam;
    int                fk, at;
} s_box;

void build_draw_boxes_begin(RMesh *m, const RCity *c, uint8_t mask_bit, int fk)
{
    memset(&s_box, 0, sizeof s_box);
    s_box.m = m, s_box.c = c, s_box.mask_bit = mask_bit;
    s_box.fam = net_walked[fk]->f;
    s_box.fk  = fk;
}

int build_draw_box_next(void)
{
    while (s_box.at < net_disc_junctions(s_box.fk))
    {
        const int32_t cell = net_disc_junction(s_box.fk, s_box.at++);
        const int32_t col = cell % R_MAP, row = cell / R_MAP;
        const int     links = eff_links(s_box.c, col, row, s_box.fam);
        if (build_junction(s_box.m, s_box.c, s_box.mask_bit, s_box.fam, col, row, links,
                           tile_order(s_box.c, col, row, s_box.mask_bit) + net_family(s_box.fam)->junc_lift) != 0)
            return -1;
        return 1;
    }
    return 0;
}

/*  And the cursor over what it draws after them, set to the first.  What
 *  it steps through is the network the script discovered: the runs and
 *  the lone pieces, in the order they were handed over. */
void build_draw_begin(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, int fk)
{
    memset(&s_draw, 0, sizeof s_draw);
    s_draw.m = m, s_draw.c = c;
    s_draw.mask_bit = mask_bit, s_draw.comp = comp;
    s_draw.fam = net_walked[fk]->f;
    s_draw.fk  = fk;
    memset(s_draw_visited, 0, sizeof s_draw_visited);
}

/*  The next thing this family draws.  Answers 1 for one drawn, 0 when
 *  the family has none left, -1 with the reason already reported. */
int build_draw_next(void)
{
    RMesh             *m        = s_draw.m;
    const RCity       *c        = s_draw.c;
    const uint8_t      mask_bit = s_draw.mask_bit;
    const int          comp     = s_draw.comp;
    const Family       fam      = s_draw.fam;
    while (s_draw.at < net_disc_count(s_draw.fk))
    {
        const int     i    = s_draw.at++;
        const int     kind = net_disc_kind(s_draw.fk, i);
        const int32_t cell = net_disc_cell(s_draw.fk, i);
        const int32_t col = cell % R_MAP, row = cell / R_MAP;
        net_cut_reset(); /* this thing's own paths, and nothing left from the last */
        if (kind == NET_DISC_RUN)
        {
            NetRun run;
            if (!net_disc_run_get(s_draw.fk, i, &run))
                continue;
            if (walk_segment(m, c, mask_bit, comp, fam, &run, s_draw_visited) != 0)
                return -1;
            return 1;
        }
        /*  A junction is not drawn here: the box cursor above has
         *  already drawn every one of them.  So a leg knows whether it
         *  is signaled before it draws its stripe. */
        if (kind == NET_DISC_JUNCTION)
            continue;
        /*  A lone piece: its own short band.  The kind whose links all
         *  leave the map is drawn only where no run covered the tile
         *  after all.  A run reaches it, steps straight off the map and
         *  comes back with one cell.  Which is why the script hands
         *  those over last. */
        if (kind == NET_DISC_EDGE && net_tile_served(row * R_MAP + col))
            continue;
        if (build_island(m, c, mask_bit, comp, fam, col, row) != 0)
            return -1;
        return 1;
    }
    return 0;
}

/*  The margins are the composing script's own pass, run after this one
 *  (scripts/compose/world.lua).  It asks for each path the network holds
 *  and lays the band over it.  They come last because every junction has
 *  registered its corners by then.  So the bands are laid in one place
 *  instead of inside whichever box happened to make them. */
int build_networks_drawn(void)
{
    tnote("junctions and segments drawn", s_draw_t);
    return 0;
}

/*  A tile the corridor pass graded: all four of its corners belong to a
 *  corridor. */

