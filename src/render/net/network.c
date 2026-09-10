/*  network.c: THE STORE the discovered network is kept in.
 *
 *  Nothing here discovers anything.  Which cells form a segment and
 *  where it runs is scripts/compose/network.lua's.  This file does two
 *  things for it: it offers the READING the script decides from.  The
 *  links a cell returns for a family, the links its own art claims.
 *  Whether a cell is a node.  And it KEEPS what the script hands back,
 *  one ordered list a family.  The list holds the segments it found, and
 *  the lone pieces it found no segment for.
 *
 *  Everything downstream steps through that list rather than over the
 *  map.  That is the class pass, the measuring walk and the drawing walk
 *  all read the same runs in the same order.  This is what keeps the
 *  three of them talking about the same network.
 *
 *  There is no walk in C behind it.  Take scripts/compose/network.lua
 *  away and the city has no lines of any family at all, and the build
 *  says so rather than drawing an empty map. */
#include <string.h>

#include "mesh/internal.h"
#include "pipeline.h"

/*  Room for every walked family's list at once.  A run is at most
 *  MAX_PTS cells and a city's segments are counted in thousands. */
#define DISC_MAX   16384
#define DISC_CELLS 262144

typedef struct
{
    int     kind;  /* NET_DISC_RUN, _LONE, _EDGE */
    int     first; /* a run: where its cells start in the arena */
    int     n;
    NetStop stop;
    int     exit;
    int32_t cell; /* an island: the tile it stands on */
} DiscEntry;

static struct
{
    DiscEntry e[DISC_MAX];
    int       n;
    /*  Where the junctions sit in that list.  They are kept in it, in
     *  the order the script found them, and indexed here as well.  Six
     *  passes walk the junctions alone, and searching the whole list for
     *  the k-th of them would make each of those a square sweep. */
    int junc[DISC_MAX];
    int n_junc;
} s_fam[NET_FAM_MAX];
static int32_t s_cells[DISC_CELLS];
static int     s_n_cells;

void net_disc_reset(void)
{
    memset(s_fam, 0, sizeof s_fam);
    s_n_cells = 0;
}

static DiscEntry *entry_add(int fk)
{
    if (fk < 0 || fk >= NET_FAM_MAX || s_fam[fk].n >= DISC_MAX)
        return NULL;
    return &s_fam[fk].e[s_fam[fk].n++];
}

/*  One run kept.  Answers 0 where there is no room for it, which the
 *  caller reports: a network half discovered draws a city half wrong. */
int net_disc_run_add(int fk, const int32_t *cells, int n, int stop, int exit)
{
    DiscEntry *e;
    if (n < 1 || n > MAX_PTS || s_n_cells + n > DISC_CELLS)
        return 0;
    if ((e = entry_add(fk)) == NULL)
        return 0;
    e->kind  = NET_DISC_RUN;
    e->first = s_n_cells;
    e->n     = n;
    e->stop  = (NetStop)stop;
    e->exit  = exit;
    e->cell  = cells[0];
    memcpy(&s_cells[s_n_cells], cells, (size_t)n * sizeof *cells);
    s_n_cells += n;
    return 1;
}

/*  And one JUNCTION: a cell where three or more ways meet, in the order
 *  the script found it.  Everything that shapes a junction, its control,
 *  its outline, its trims, its box, steps through this list.  So which
 *  cells are junctions is the same answer as which cells make a segment
 *  and comes from the same rule. */
int net_disc_junction_add(int fk, int32_t cell)
{
    DiscEntry *e = entry_add(fk);
    if (!e)
        return 0;
    e->kind                             = NET_DISC_JUNCTION;
    e->cell                             = cell;
    s_fam[fk].junc[s_fam[fk].n_junc++] = s_fam[fk].n - 1;
    return 1;
}

/*  And one lone piece: a tile no run reaches.  The two kinds are drawn
 *  differently.  A piece with no links at all is always its own short
 *  band.  One whose every link leaves the map is one only where no run
 *  already covered it.  So the script says which it found. */
int net_disc_island_add(int fk, int32_t cell, int edge)
{
    DiscEntry *e = entry_add(fk);
    if (!e)
        return 0;
    e->kind = edge ? NET_DISC_EDGE : NET_DISC_LONE;
    e->cell = cell;
    return 1;
}

int net_disc_count(int fk)
{
    return fk >= 0 && fk < NET_FAM_MAX ? s_fam[fk].n : 0;
}

int net_disc_kind(int fk, int i)
{
    if (fk < 0 || fk >= NET_FAM_MAX || i < 0 || i >= s_fam[fk].n)
        return -1;
    return s_fam[fk].e[i].kind;
}

int32_t net_disc_cell(int fk, int i)
{
    if (fk < 0 || fk >= NET_FAM_MAX || i < 0 || i >= s_fam[fk].n)
        return -1;
    return s_fam[fk].e[i].cell;
}

int net_disc_run_get(int fk, int i, NetRun *out)
{
    const DiscEntry *e;
    if (fk < 0 || fk >= NET_FAM_MAX || i < 0 || i >= s_fam[fk].n)
        return 0;
    e = &s_fam[fk].e[i];
    if (e->kind != NET_DISC_RUN)
        return 0;
    out->n    = e->n;
    out->stop = e->stop;
    out->exit = e->exit;
    memcpy(out->cell, &s_cells[e->first], (size_t)e->n * sizeof out->cell[0]);
    return 1;
}

/*  ---- what the script reads the network off ----------------------------
 *
 *  Two planes, a cell each.
 *
 *      The links a cell RETURNS for this family.
 *      A link both sides agree on.
 *      The links its own art claims.
 *
 *  Handed over whole because the script walks the map with them, and a
 *  call a cell would cost more than the walk. */
void net_disc_planes(const RCity *c, const RAtlasLevel *l, Family f,
                     uint8_t *links, uint8_t *art)
{
    int32_t i;
    for (i = 0; i < R_MAP * R_MAP; ++i)
    {
        int32_t col = i % R_MAP, row = i / R_MAP;
        links[i] = (uint8_t)eff_links(c, l, col, row, f);
        art[i]   = (uint8_t)tile_links(c, l, col, row, f);
    }
}

/*  ---- and what a NODE is -----------------------------------------------
 *
 *  Where a segment ends is where the next node begins.  So what counts
 *  as one is part of the same answer.  The script works it out off the
 *  two planes above, and hands the whole plane back.  `node_kind` reads
 *  this and nothing else: take the rule away and the map has no nodes,
 *  and therefore no segments. */
static uint8_t s_node[3][R_MAP * R_MAP];

void net_disc_nodes_set(Family f, const uint8_t *plane)
{
    if ((int)f >= 0 && (int)f < 3)
        memcpy(s_node[(int)f], plane, sizeof s_node[0]);
}

int node_kind(const RCity *c, const RAtlasLevel *l, Family f, int32_t col, int32_t row)
{
    (void)c;
    (void)l;
    if ((int)f < 0 || (int)f >= 3 || col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return 0;
    return s_node[(int)f][row * R_MAP + col];
}

/*  The junctions of one family, by the index the passes walk them at. */
int net_disc_junctions(int fk)
{
    return fk >= 0 && fk < NET_FAM_MAX ? s_fam[fk].n_junc : 0;
}

int32_t net_disc_junction(int fk, int k)
{
    if (fk < 0 || fk >= NET_FAM_MAX || k < 0 || k >= s_fam[fk].n_junc)
        return -1;
    return s_fam[fk].e[s_fam[fk].junc[k]].cell;
}

int net_run_edge(const NetRun *r)
{
    int32_t c0, r0, c1, r1;
    if (!r || r->n < 1)
        return -1;
    if (r->n < 2)
        return r->exit;
    c0 = r->cell[0] % R_MAP, r0 = r->cell[0] / R_MAP;
    c1 = r->cell[1] % R_MAP, r1 = r->cell[1] / R_MAP;
    return c1 > c0 ? 1 : c1 < c0 ? 3 : r1 > r0 ? 2 : 0;
}


/*  ---- the band's bands, as the script discovered them ---------------
 *
 *  The same question as a line segment's, asked of a different network:
 *  a band is a run of cells.  Which cells those are is the script's.
 *  Each band carries the cell it was started from and the way it was
 *  walked.  This together are its key in the segment table across
 *  builds. */
#define HW_BANDS 512

static struct
{
    HwRun   run;
    int32_t col, row;
    int     ew, sign;
} s_hwb_run[HW_BANDS];
static int s_n_hwb_run;

void net_hw_disc_reset(void)
{
    s_n_hwb_run = 0;
}

int net_hw_disc_add(const HwRun *r, int32_t col, int32_t row, int ew, int sign)
{
    if (!r || r->n < 1 || s_n_hwb_run >= HW_BANDS)
        return 0;
    s_hwb_run[s_n_hwb_run].run  = *r;
    s_hwb_run[s_n_hwb_run].col  = col;
    s_hwb_run[s_n_hwb_run].row  = row;
    s_hwb_run[s_n_hwb_run].ew   = ew;
    s_hwb_run[s_n_hwb_run].sign = sign;
    ++s_n_hwb_run;
    return 1;
}

int net_hw_disc_get(int i, const HwRun **r, int32_t *col, int32_t *row, int *ew, int *sign)
{
    if (i < 0 || i >= s_n_hwb_run)
        return 0;
    *r    = &s_hwb_run[i].run;
    *col  = s_hwb_run[i].col;
    *row  = s_hwb_run[i].row;
    *ew   = s_hwb_run[i].ew;
    *sign = s_hwb_run[i].sign;
    return 1;
}
