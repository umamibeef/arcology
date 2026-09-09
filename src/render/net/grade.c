/*  The corridor grading: what a strip asks of the ground under it, and
 *  the graded surface every later stage reads. */
#include <math.h>
#include <string.h>

#include "mesh/internal.h"
#include "net/internal.h"
#include "dump.h"
#include "opt.h"

float   s_zlow[GRID * GRID];
uint8_t s_corr[GRID * GRID]; /* the corner belongs to a corridor: its height is the corridor's */
float   s_zcap[GRID * GRID];

float          s_tilez[R_MAP * R_MAP * 4];
static float   s_tiled[R_MAP * R_MAP * 4]; /* per corner: how far the station that set it was */
static uint32_t s_tileo[R_MAP * R_MAP * 4]; /* ... and which corridor -- which edge of the network -- wrote it, 1 + the loft */
static uint8_t  s_node[R_MAP * R_MAP];      /* a node of the graph: edges that meet here share one level */
static uint32_t s_lofts;                    /* the lofts so far this pass: an edge's identity while it is graded */

void s_tile_reset(int32_t i)
{
    s_tiled[i] = 1e9f;
    s_tileo[i] = 0;
    if ((i & 3) == 0)
        s_node[i >> 2] = 0;
    if (i == 0)
        s_lofts = 0;
}

/*  A node of the graph: a tile where edges meet, and therefore where
 *  they are all at one level.  Marked as the junctions are built. */
void shelf_node(int32_t col, int32_t row)
{
    if (col >= 0 && row >= 0 && col < R_MAP && row < R_MAP)
        s_node[row * R_MAP + col] = 1;
}

/*  Where two corridor tiles disagree at a corner they share AND ought
 *  not to: the corner belongs to one edge on both sides, or one of the
 *  tiles is a node, where every edge that meets is at one level.  A
 *  corner where two different edges merely touch is not counted -- a
 *  wall between two corridors is the right answer, not a break.  Each
 *  shared corner is judged on its own: a pair of tiles may share one
 *  corner along an edge and another between two edges. */
static void shelf_corner(int32_t a, int32_t b, int at_node, int *n, float *worst, int32_t c, int32_t r, const char *side)
{
    float d;
    if (!s_tileo[a] || !s_tileo[b])
        return;
    if (!at_node && s_tileo[a] != s_tileo[b])
        return;
    d = fabsf(s_tilez[a] - s_tilez[b]);
    if (d <= 0.01f)
        return;
    ++*n;
    if (d > *worst)
        *worst = d;
    if (g_dev.junc_dump)
        dumpf("SHELFSTEP %d,%d %s %.2f  node %d  edges %u|%u\n", c, r, side, (double)d, at_node, s_tileo[a], s_tileo[b]);
}

void shelf_steps(int *n, float *worst)
{
    int32_t c, r;
    *n = 0, *worst = 0.0f;
    for (r = 0; r < R_MAP; ++r)
        for (c = 0; c < R_MAP; ++c)
        {
            int32_t s0 = (r * R_MAP + c) * 4;
            int     nd = s_node[r * R_MAP + c];
            if (s_tilez[s0] > 1e8f)
                continue;
            if (c + 1 < R_MAP && s_tilez[(r * R_MAP + c + 1) * 4] < 1e8f)
            {
                int32_t s1 = (r * R_MAP + c + 1) * 4;
                int     at = nd || s_node[r * R_MAP + c + 1];
                shelf_corner(s0 + NE, s1 + NW, at, n, worst, c, r, "E");
                shelf_corner(s0 + SE, s1 + SW, at, n, worst, c, r, "E");
            }
            if (r + 1 < R_MAP && s_tilez[((r + 1) * R_MAP + c) * 4] < 1e8f)
            {
                int32_t s1 = ((r + 1) * R_MAP + c) * 4;
                int     at = nd || s_node[(r + 1) * R_MAP + c];
                shelf_corner(s0 + SW, s1 + NW, at, n, worst, c, r, "S");
                shelf_corner(s0 + SE, s1 + NE, at, n, worst, c, r, "S");
            }
        }
}

/*  The four tiles that keep a copy of the corner at grid point (gx, gy),
 *  with the corridor that wrote each and how far away the station was. */
static int shelf_slots(int gx, int gy, int32_t *sl)
{
    int n = 0;
    if (gx > 0 && gy > 0)
        sl[n++] = ((gy - 1) * R_MAP + (gx - 1)) * 4 + SE;
    if (gx < R_MAP && gy > 0)
        sl[n++] = ((gy - 1) * R_MAP + gx) * 4 + SW;
    if (gx > 0 && gy < R_MAP)
        sl[n++] = (gy * R_MAP + (gx - 1)) * 4 + NE;
    if (gx < R_MAP && gy < R_MAP)
        sl[n++] = (gy * R_MAP + gx) * 4 + NW;
    return n;
}

int shelf_copies(const ShelfFan *s, int gx, int gy, int *owner, float *dist, float *z, int max)
{
    int32_t sl[4];
    int     n, k, out = 0;
    (void)s;
    n = shelf_slots(gx, gy, sl);
    for (k = 0; k < n && out < max; ++k)
        if (s_tileo[sl[k]])
        {
            owner[out] = (int)s_tileo[sl[k]];
            dist[out]  = s_tiled[sl[k]];
            z[out]     = s_tilez[sl[k]];
            ++out;
        }
    return out;
}

/*  Every copy of that corner written by that corridor takes this level. */
void shelf_set(ShelfFan *s, int gx, int gy, int owner, float z)
{
    int32_t sl[4];
    int     n, k;
    (void)s;
    n = shelf_slots(gx, gy, sl);
    for (k = 0; k < n; ++k)
        if ((int)s_tileo[sl[k]] == owner)
            s_tilez[sl[k]] = z;
}

/*  The node tiles, and the copies of their own four corners -- whichever
 *  corridor wrote them. */
int shelf_node_at(const ShelfFan *s, int i, int32_t *col, int32_t *row)
{
    int32_t c, r, n = 0;
    (void)s;
    for (r = 0; r < R_MAP; ++r)
        for (c = 0; c < R_MAP; ++c)
            if (s_node[r * R_MAP + c] && n++ == i)
            {
                *col = c;
                *row = r;
                return 1;
            }
    return 0;
}

static int shelf_node_slots(int32_t col, int32_t row, int32_t *out)
{
    int32_t sl[4];
    int     tq, k, n = 0;
    for (tq = 0; tq < 4; ++tq)
    {
        int gx = col + ((tq == NE || tq == SE) ? 1 : 0), gy = row + ((tq == SW || tq == SE) ? 1 : 0);
        int m  = shelf_slots(gx, gy, sl);
        for (k = 0; k < m; ++k)
            if (s_tileo[sl[k]])
                out[n++] = sl[k];
    }
    return n;
}

int shelf_node_heights(const ShelfFan *s, int32_t col, int32_t row, float *z, int max)
{
    int32_t sl[16];
    int     n, k;
    (void)s;
    n = shelf_node_slots(col, row, sl);
    if (n > max)
        n = max;
    for (k = 0; k < n; ++k)
        z[k] = s_tilez[sl[k]];
    return n;
}

void shelf_node_set(ShelfFan *s, int32_t col, int32_t row, float z)
{
    int32_t sl[16];
    int     n, k;
    (void)s;
    n = shelf_node_slots(col, row, sl);
    for (k = 0; k < n; ++k)
        s_tilez[sl[k]] = z;
}

/*  The corridors are the edges of a graph, and the rules are the graph's:
 *  arc.rules.shelf reconciles the copies. */
void shelf_reconcile(void)
{
    ShelfFan s;
    int32_t  c, r;
    memset(&s, 0, sizeof s);
    for (r = 0; r < R_MAP; ++r)
        for (c = 0; c < R_MAP; ++c)
            s.nodes += s_node[r * R_MAP + c] != 0;
    script_rule_object("shelf", "shelf", &s);
}

float s_zdist[GRID * GRID]; /* how far the nearest station that set it was */

/*  ------------------------------------------------------------------
 *  Lofting, stage by stage.  The ribbon is swept from the stations, but
 *  three tasks that ride along with it -- the furniture beside the line,
 *  the corridor surface under it, and the overlay drawn over it for
 *  looking at -- are their own jobs and stand on their own here.
 *  ------------------------------------------------------------------ */

/*  The corridor's own surface: the shelf the band lies on, notched out
 *  of the terrain rather than laid over it. */
/*  Heights only: this stage writes the shelf the corridor's tiles wear
 *  and emits no geometry of its own, which is why it needs neither the
 *  mesh nor the city. */
/*  The station's own tile's four corner heights, flat across the band and
 *  graded along it: each corner projects onto the centreline and takes the
 *  profile's height there, the nearest station owning it.  Only the tile
 *  the station stands in: the band reaches its neighbours through their own
 *  stations, and writing a shelf for a tile the band never crosses hands
 *  this segment's height to somebody else's road. */
static void surface_tile_shelf(const Sample *smp, int ns, int i, int32_t tc, int32_t tr, uint32_t edge)
{
    int tq;
    for (tq = 0; tq < 4; ++tq)
    {
        float   ccx = (float)tc + ((tq == NE || tq == SE) ? 1.0f : 0.0f);
        float   ccy = (float)tr + ((tq == SW || tq == SE) ? 1.0f : 0.0f);
        float   ax = ccx - smp[i].pos.x, ay = ccy - smp[i].pos.y;
        float   cd = ax * ax + ay * ay;
        int32_t sl = (tr * R_MAP + tc) * 4 + tq;
        if (cd >= s_tiled[sl])
            continue;
        s_tiled[sl] = cd;
        s_tileo[sl] = edge;
        s_tilez[sl] = profile_at(smp, ns, smp[i].s + ax * smp[i].dir.x + ay * smp[i].dir.y);
    }
}

/*  A grid corner's height off the station that owns it: the road's
 *  height AT THE CORNER'S OWN PROJECTION on the centreline, not the
 *  nearest station's, so the two corners across the band come out the
 *  same and the shelf is flat across by construction; along it the
 *  profile's own grade carries them.  Held to the grade the profile
 *  itself may take, and to the distance a corner can honestly be from
 *  its own station: a local estimate over two close stations can read
 *  far steeper than the road ever goes, and extrapolating on it throws
 *  the corner a level out. */
static float surface_corner_height(const Sample *smp, int ns, int i, float dx, float dy, float shelf_grade)
{
    float along = dx * smp[i].dir.x + dy * smp[i].dir.y;
    float grade = 0.0f;
    if (i > 0 && i + 1 < ns && smp[i + 1].s - smp[i - 1].s > 1e-3f)
        grade = (smp[i + 1].z - smp[i - 1].z) / (smp[i + 1].s - smp[i - 1].s);
    if (grade > shelf_grade)
        grade = shelf_grade;
    if (grade < -shelf_grade)
        grade = -shelf_grade;
    {
        const float cap = net_family_rules(F_ROAD)->shelf_along;
        if (along > cap)
            along = cap;
        if (along < -cap)
            along = -cap;
    }
    return smp[i].z + grade * along;
}

int loft_surface(const RCity *c, uint8_t mask_bit, Sample *smp, int ns, float hw, const RLoft *d)
{
    const uint32_t edge = ++s_lofts; /* this strip, for as long as it is graded: an edge of the network */
    int i;
    /*  The corridor's surface.  Every grid corner the band reaches takes
     *  the graded height over it -- the lowest, where two segments meet at
     *  a corner -- and is marked as the corridor's.  The next pass gives
     *  those corners that height outright instead of the terrain's average,
     *  so the corridor is a graded shelf notched into the hillside, the
     *  road lies ON it rather than over it, and the wall rule closes its
     *  sides. */
    if (s_pass == 1)
    {
        const float shelf_grade = d->fam->shelf_grade; /* the profile's own ceiling: the family's */
        for (i = 0; i < ns; ++i)
        {
            int32_t capc = (int32_t)floorf(smp[i].pos.x), capr = (int32_t)floorf(smp[i].pos.y);
            int     cq;
            /*  A viaduct does not grade the hillside: its columns take up
             *  the difference, and the ground under and beside it keeps
             *  its shape.  Only where the lift has tapered toward the
             *  ground -- the ramp cells at a band's ends -- is the deck
             *  earthworks, and shelved like a road.  Shelving the whole
             *  deck raised the ground a lift's worth for nearly two tiles
             *  either side of it, and where an arc's stations landed on a
             *  surface road that road ended up under the terrain (Lincoln,
             *  column 82 row 43). */
            if (d->fam->flies && d->fam->flies(d, smp[i].z - section_height(c, mask_bit, smp[i].pos, smp[i].dir, hw)))
                continue; /* a deck stands clear and notches nothing (hiway.c) */
            /*  The corridor's surface, sampled per corner of every tile
             *  the band passes through: each corner projects onto the
             *  centreline and takes the profile's height there.  It is
             *  one continuous surface -- neighbouring tiles ask the same
             *  question at their shared corner and get the same answer --
             *  and it is flat across, because the across coordinate never
             *  enters.  A tile keeps its own copy so that a road and a
             *  railway side by side stay two shelves with a wall between
             *  them. */
            surface_tile_shelf(smp, ns, i, capc, capr, edge);
            for (cq = 0; cq < 4; ++cq)
            {
                int32_t gc = capc + ((cq == NE || cq == SE) ? 1 : 0), gr = capr + ((cq == SW || cq == SE) ? 1 : 0);
                float   dx, dy;
                if (gc < 0 || gr < 0 || gc >= GRID || gr >= GRID)
                    continue;
                dx       = (float)gc - smp[i].pos.x;
                dy       = (float)gr - smp[i].pos.y;
                float d2 = dx * dx + dy * dy;
                if (d2 > (hw + net_family_rules(F_ROAD)->shelf_reach) * (hw + net_family_rules(F_ROAD)->shelf_reach))
                    continue;
                /*  The corners under the band itself carry its height; the
                 *  ring beyond them is the batter, half way back to the
                 *  hillside, so the shelf blends out instead of standing
                 *  on one wall of its full depth. */
                if (d2 > (hw + net_family_rules(F_ROAD)->shelf_batter) * (hw + net_family_rules(F_ROAD)->shelf_batter))
                {
                    if (!s_corr[gr * GRID + gc])
                        s_corr[gr * GRID + gc] = 2;
                    if (d2 < s_zdist[gr * GRID + gc])
                    {
                        s_zdist[gr * GRID + gc] = d2;
                        s_zcap[gr * GRID + gc]  = surface_corner_height(smp, ns, i, dx, dy, shelf_grade);
                    }
                    continue;
                }
                /*  The corner takes the road's height AT ITS OWN PROJECTION
                 *  on the centreline, not the height of the nearest
                 *  station.  The two corners across the band project to the
                 *  same point, so they come out at the same height and the
                 *  shelf is flat across the corridor by construction; along
                 *  it the profile's own grade carries them.  The nearest
                 *  station only says WHICH stretch of road owns the corner. */
                if (d2 < s_zdist[gr * GRID + gc] || s_corr[gr * GRID + gc] != 1)
                {
                    float zc                = surface_corner_height(smp, ns, i, dx, dy, shelf_grade);
                    s_zdist[gr * GRID + gc] = d2;
                    s_zcap[gr * GRID + gc]  = zc;
                    if (s_corr[gr * GRID + gc] != 1 || zc < s_zlow[gr * GRID + gc])
                        s_zlow[gr * GRID + gc] = zc;
                }
                s_corr[gr * GRID + gc] = 1;
            }
        }
    }
    return 0;
}

/*  The height of the drawn surface at (u, v) inside a tile, u along the
 *  column and v along the row, from the corners tile_top gives and the
 *  diagonal the top is cut on. */
static float surface_at(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float u, float v)
{
    float z[4];
    Kind  k    = tile_top(c, col, row, mask_bit, z);
    int   code = k == T_LAND ? slope_code(c->xter[row * R_MAP + col]) : 0;
    if (u < 0.0f)
        u = 0.0f;
    if (u > 1.0f)
        u = 1.0f;
    if (v < 0.0f)
        v = 0.0f;
    if (v > 1.0f)
        v = 1.0f;
    if (!cut_ne_sw(code))
        return u >= v ? z[NW] + (z[NE] - z[NW]) * u + (z[SE] - z[NE]) * v
                      : z[NW] + (z[SW] - z[NW]) * v + (z[SE] - z[SW]) * u;
    return u + v <= 1.0f ? z[NW] + (z[NE] - z[NW]) * u + (z[SW] - z[NW]) * v
                         : z[SE] + (z[NE] - z[SE]) * (1.0f - v) + (z[SW] - z[SE]) * (1.0f - u);
}

/*  The drawn surface of ONE tile at a world point, clamped to that
 *  tile.  A piece of a band belongs to the tile it was clipped into, and
 *  must take that tile's top even at a shared edge, where the
 *  neighbour's may be a wall's height away. */
float surface_at_tile(const RCity *c, uint8_t mask_bit, int32_t col, int32_t row, float x, float y)
{
    if (col < 0)
        col = 0;
    if (row < 0)
        row = 0;
    if (col >= R_MAP)
        col = R_MAP - 1;
    if (row >= R_MAP)
        row = R_MAP - 1;
    return surface_at(c, col, row, mask_bit, x - (float)col, y - (float)row);
}

/*  Is the tile's top one plane?  Then its two triangles are coplanar and
 *  a band on it needs no cut along the fold. */
int tile_top_planar(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit)
{
    float z[4];
    tile_top(c, col, row, mask_bit, z);
    return fabsf((z[NW] + z[SE]) - (z[NE] + z[SW])) < 1e-4f;
}

/*  The drawn surface at a world point, from whichever tile holds it, so
 *  a strip that reaches past its tile's edge follows the ground there. */
float surface_at_world(const RCity *c, uint8_t mask_bit, float x, float y)
{
    int32_t col = (int32_t)floorf(x), row = (int32_t)floorf(y);
    if (col < 0)
        col = 0;
    if (row < 0)
        row = 0;
    if (col >= R_MAP)
        col = R_MAP - 1;
    if (row >= R_MAP)
        row = R_MAP - 1;
    return surface_at(c, col, row, mask_bit, x - (float)col, y - (float)row);
}

int corridor_tile(int32_t col, int32_t row)
{
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return 0;
    return s_corr[row * GRID + col] && s_corr[row * GRID + col + 1] && s_corr[(row + 1) * GRID + col] &&
           s_corr[(row + 1) * GRID + col + 1];
}
