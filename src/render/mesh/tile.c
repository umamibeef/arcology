/*  mesh/tile.c: what a tile is, and the ground field.  See
 *  mesh/internal.h. */
#include <string.h>

#include "geo.h"
#include "log.h"
#include "mesh/internal.h"

static float field_edge(const RCity *c, int32_t col, int32_t row, int e); /* the dual ground at the middle of edge e */

/*  The tables and the field the other pieces read.  The state the
 *  segment pipeline carries across its stages. */
const int   EDGE_A[4]    = {NW, NE, SW, SW};
const int   EDGE_B[4]    = {NE, SE, SE, NW};
const int   NBR_A[4]     = {SW, NW, NW, SE};
const int   NBR_B[4]     = {SE, SW, NE, NE};
const int   EDGE_DR[4]   = {-1, 0, 1, 0};
const int   EDGE_DC[4]   = {0, 1, 0, -1};
const float EDGE_N[4][3] = {
    {0.0f,  -1.0f, 0.0f},
    {1.0f,  0.0f,  0.0f},
    {0.0f,  1.0f,  0.0f},
    {-1.0f, 0.0f,  0.0f}
};
R_GRID_BUF(, float, s_h, 1) /* the ground: one height per corner      */
R_GRID_BUF(, float, s_k, 1) /* curvature: positive in a hollow        */
R_GRID_BUF(, float, s_b, 1) /* the bed: the seabed at a corner that   */

/* ---- what a tile is ---------------------------------------------------- */

static float ground_of(const RCity *c, int32_t idx)
{
    return (float)rcity_alt_ground(c->map.altm[idx]);
}

static float table_of(const RCity *c, int32_t idx)
{
    return (float)rcity_alt_table(c->map.altm[idx]);
}

/*  The grid index of a tile's corner k. */
/*  The tiles whose piece is drawn from its own shape, one byte a tile, or
 *  NULL for none. */
static const uint8_t *s_shaped;

void mesh_tops_shaped(const uint8_t *shaped)
{
    s_shaped = shaped;
}

int32_t corner_gi(int32_t col, int32_t row, int k)
{
    return (row + ((k == SW || k == SE) ? 1 : 0)) * GRID + col +
           ((k == NE || k == SE) ? 1 : 0);
}

/*  A network piece on a saddle is drawn one step up
 *  (mesh/ground_bytes.c says which ground and which pieces). */
int saddle_lift(const RCity *c, int32_t idx)
{
    return saddle_terrain(c->map.xter[idx]) && saddle_tile(c->xbld[idx]);
}

/*  The corners a tile's slope code lifts, as a mask. */
static int own_mask(const RCity *c, int32_t idx)
{
    return corner_lifts(slope_code(c->map.xter[idx]));
}

/*  The tile's own plane: ALTM's level plus the slope code's lifts.  For
 *  a water body this is the bed. */
static void own_plane(const RCity *c, int32_t idx, float z[4])
{
    unsigned mask = (unsigned)own_mask(c, idx);
    float    base = ground_of(c, idx);
    int      k;
    for (k = 0; k < 4; ++k)
        z[k] = base + (((mask >> k) & 1u) ? 1.0f : 0.0f);
}

/*  ---- WHERE EVERY TILE'S TOP COMES FROM -------------------------------
 *
 *  Five places.  mesh_tops_settle walks the map once a build, before
 *  anything reads a tile, and says which of them each tile draws from.
 *
 *    TOP_FIELD       the terrain's own heightfield, four corners shared
 *                    with the tiles around it.  Open land.
 *    TOP_PAD         a flat pad at the tile's own level.  A structure
 *                    stands on level ground however the ground under it
 *                    falls.
 *    TOP_PAD_ANCHOR  the pad of the tile the footprint belongs to.  A
 *                    building covers up to sixteen tiles and every one of
 *                    them stands at the level of the anchor.  If not, the
 *                    footprint is a staircase.
 *    TOP_WATER       the table of the water, which a body of water draws.
 *    TOP_PLANE       the slope plane of the tile, from the level ALTM
 *                    gives and the lifts of the slope code.  A network
 *                    piece whose sprite fits a slope keeps the slope.
 *
 *  Each tile also gets the tile whose place in the painter's stack it
 *  takes, and how it votes for the seabed. */
R_MAP_BUF(static, uint8_t, s_top, 1)
R_MAP_BUF(static, int32_t, s_anchor, 1) /* the tile whose pad a footprint stands on */
R_MAP_BUF(static, int32_t, s_order, 1)  /* the tile whose place in the painter's stack it takes */
R_MAP_BUF(static, uint8_t, s_vote, 1)   /* water or land, for the seabed */
R_MAP_BUF(static, float, s_tc, 4)       /* each tile's own four corners: its side of any cliff */
R_GRID_BUF(, uint8_t, s_split, 1)       /* a grid point a cliff splits */
R_MAP_BUF(static, float, s_tl, 4)       /* how far a cliff's lean moved each tile corner (cliff_lean) */

static void mesh_tops_reset(void)
{
    int32_t i;
    memset(s_top, TOP_FIELD, r_map_bytes(s_top));
    memset(s_vote, VOTE_LAND, r_map_bytes(s_vote));
    for (i = 0; i < r_map * r_map; ++i)
        s_anchor[i] = s_order[i] = i;
}

static void mesh_top_is(int32_t at, int top, int32_t anchor, int32_t order, int vote)
{
    if (at < 0 || at >= r_map * r_map)
        return;
    s_vote[at]   = (uint8_t)(vote == VOTE_WATER ? VOTE_WATER : VOTE_LAND);
    s_top[at]    = (uint8_t)(top >= 0 && top <= TOP_PAD ? top : TOP_FIELD);
    s_anchor[at] = anchor >= 0 && anchor < r_map * r_map ? anchor : at;
    s_order[at]  = order >= 0 && order < r_map * r_map ? order : at;
}

/*  The ANCHOR of a footprint: the tile of it that carries the corner bit
 *  of the anchor.  It is up to three tiles north and east of this one.  The
 *  nearest such tile wins.  A tile that carries the bit itself is its own
 *  anchor. */
static int32_t anchor_of(const RCity *c, int32_t col, int32_t row, uint8_t corner)
{
    const int32_t at   = row * r_map + col;
    const uint8_t b    = c->xbld[at];
    int32_t       best = 99, ai = at, dr, dc;
    if (!tile_building(b) || (c->xzon[at] & corner) != 0)
        return at;
    for (dr = 0; dr >= -3; --dr)
        for (dc = 0; dc <= 3; ++dc)
        {
            const int32_t ar = row + dr, ac = col + dc;
            if (ar >= 0 && ac < r_map && -dr + dc < best)
            {
                const int32_t i = ar * r_map + ac;
                if (c->xbld[i] == b && (c->xzon[i] & corner) != 0)
                    best = -dr + dc, ai = i;
            }
        }
    return ai;
}

/*  Every tile's top, by a ladder.  THE ORDER OF THE LADDER IS THE RULE.  A
 *  building answers first, whatever else the tile is.  Then water, then a
 *  sloped piece, then any other structure, and open land last.
 *
 *  The place in the painter's stack: the mesh sweeps a tile in the order
 *  the original sweeps it, so the mesh composes with the sprites.  A
 *  footprint takes the place of its anchor, so the art stays in front of
 *  its own pad.  An elevated piece without the corner bit takes the place
 *  of the neighbor that owns its span.  So a raised slab and the tile it
 *  crosses do not each claim one.  The neighbors it looks at are east,
 *  north-east and north, and the first that is elevated and carries the
 *  corner bit wins.  Every other tile takes its own place.
 *
 *  A tile votes for the seabed as water where its terrain is water, and
 *  as land elsewhere. */
void mesh_tops_settle(const RCity *c)
{
    static const int32_t QDR[3] = {0, -1, -1}, QDC[3] = {1, 1, 0};
    const uint8_t        corner = city_corner_mask(0);
    int32_t              row, col;
    mesh_tops_reset();
    for (row = 0; row < r_map; ++row)
        for (col = 0; col < r_map; ++col)
        {
            const int32_t at = row * r_map + col;
            const uint8_t b = c->xbld[at], x = c->map.xter[at];
            int32_t       anchor = at, order = at;
            int           place, q;
            if (tile_building(b))
            {
                anchor = order = anchor_of(c, col, row, corner);
                place          = TOP_PAD_ANCHOR;
            }
            else if (is_water(x))
                place = TOP_WATER;
            else if (s_shaped && s_shaped[at])
                /*  A piece drawn from its own shape stands on the land. */
                place = TOP_FIELD;
            else if (tile_built(b))
                /*  A sloped piece keeps its slope, and anything else gets
                 *  a level pad.  An elevated piece stands clear of the
                 *  ground, so the tile under it is flat. */
                place = slope_code(x) != 0 && tile_sloped(b) && !tile_elevated(b) ? TOP_PLANE : TOP_PAD;
            else
                place = TOP_FIELD;
            if (tile_elevated(b) && (c->xzon[at] & corner) == 0)
                for (q = 0; q < 3; ++q)
                {
                    const int32_t ar = row + QDR[q], ac = col + QDC[q];
                    if (ar >= 0 && ac < r_map)
                    {
                        const int32_t ai = ar * r_map + ac;
                        if (tile_elevated(c->xbld[ai]) && (c->xzon[ai] & corner) != 0)
                        {
                            order = ai;
                            break;
                        }
                    }
                }
            mesh_top_is(at, place, anchor, order, is_water(x) ? VOTE_WATER : VOTE_LAND);
        }
}

/*  ---- THE DUAL GROUND --------------------------------------------------
 *
 *  Open land is drawn between the middles of the tiles (plans/terrain.md).
 *  A tile's middle is at its level.  The middle of an edge is at the mean
 *  of the two tiles across it.  A corner is at the mean of the four round
 *  it.  What a tile adds to a corner or to the middle of an edge is its
 *  CONTRIBUTION to that point.  Open land adds its level and water its
 *  table.  A pad or a sloped piece has a top of its own.  It adds the
 *  height that top is drawn at there, and it holds the point.  Where any
 *  such top touches a point, the point is at their mean, so the land meets
 *  it.  A wall is left only where two such tops disagree. */
enum
{
    C_LAND = 0,
    C_HELD
};

/*  A CLIFF lies between two land tiles side by side whose ground differs
 *  by more than one level (plans/terrain.md).  Across a cliff the ground
 *  does not join: each tile keeps its own side.  Water meets the land as a
 *  shore, held up to its table, and never forms a cliff.  The shipped
 *  cities hold 27 cliff edges in all. */
int tile_cliff(const RCity *c, int32_t a, int32_t b)
{
    int d = rcity_alt_ground(c->map.altm[a]) - rcity_alt_ground(c->map.altm[b]);
    if (s_vote[a] == VOTE_WATER || s_vote[b] == VOTE_WATER)
        return 0;
    return d > 1 || d < -1;
}

/*  Tile idx's contribution at `at`: 0 to 3 a corner, 4 to 7 the middle of
 *  edge at - 4.  `cls` says whether it is land's or holds the point. */
static float contrib(const RCity *c, int32_t idx, int at, int *cls)
{
    float z[4];
    *cls = C_LAND;
    if (s_top[idx] == TOP_FIELD)
        return ground_of(c, idx);
    if (s_top[idx] == TOP_WATER)
        return table_of(c, idx);
    *cls = C_HELD;
    tile_top(c, idx % r_map, idx / r_map, 0, z);
    return at < 4 ? z[at] : 0.5f * (z[EDGE_A[at - 4]] + z[EDGE_B[at - 4]]);
}

/*  The lean of a cliff.  The open land at a cliff's lip sags toward the
 *  step below it.  The land at its foot rises toward the step above it,
 *  as scree.  So the face stands shorter, between two slopes.  The geo
 *  store sets how far each goes, as a part of the step (cliff_lip and
 *  cliff_toe).  Each part is held under a half, so the two
 *  sides never cross. */
static int gix_cliff_lip = -1, gix_cliff_toe = -1;

static float cliff_part(int down)
{
    float f = down ? geo_num(&gix_cliff_lip, "cliff_lip") : geo_num(&gix_cliff_toe, "cliff_toe");
    return f < 0.0f ? 0.0f : f > 0.45f ? 0.45f
                                       : f;
}

/*  Height v of open land, moved toward the heights across a cliff.  It
 *  moves down toward the highest of those below it.  It moves up toward
 *  the lowest of those above it.  `o` holds the n heights across. */
static float cliff_lean(float v, const float *o, int n)
{
    float down = v, up = v;
    int   i, has_down = 0, has_up = 0;
    for (i = 0; i < n; ++i)
        if (o[i] < v && (!has_down || o[i] > down))
            down = o[i], has_down = 1;
        else if (o[i] > v && (!has_up || o[i] < up))
            up = o[i], has_up = 1;
    return v - cliff_part(1) * (v - down) + cliff_part(0) * (up - v);
}

/*  The middle of edge e of a tile.  It is the mean of the two tiles'
 *  contributions there, held up to the table of any water beside it.  Both
 *  tiles get the same answer, since a sum does not depend on its order.
 *  Across a cliff each tile keeps its own, which leans (cliff_lean). */
static float field_edge_lean(const RCity *c, int32_t col, int32_t row, int e, float *lean);

static float field_edge(const RCity *c, int32_t col, int32_t row, int e)
{
    float lean;
    return field_edge_lean(c, col, row, e, &lean);
}

/*  How far a cliff's lean moved point k of a tile (tile_point's numbers).
 *  It is down where the lip is cut away, and up where scree fills the
 *  foot.  It is nought where no cliff is near. */
float tile_lean(const RCity *c, int32_t col, int32_t row, int k)
{
    float lean = 0.0f;
    if (k < 4)
        return s_tl[(row * r_map + col) * 4 + k];
    if (k < 8)
        field_edge_lean(c, col, row, k - 4, &lean);
    return lean;
}

static float field_edge_lean(const RCity *c, int32_t col, int32_t row, int e, float *lean)
{
    int32_t idx = row * r_map + col, nc = col + EDGE_DC[e], nr = row + EDGE_DR[e];
    float   sum = 0.0f, ssum = 0.0f, v;
    int     n = 0, sn = 0, cls, i;
    for (i = 0; i < 2; ++i)
    {
        int32_t at = i == 0 ? idx : nr * r_map + nc;
        float   x;
        if (i == 1 && (nc < 0 || nr < 0 || nc >= r_map || nr >= r_map || tile_cliff(c, idx, at)))
            break;
        x = contrib(c, at, 4 + (i == 0 ? e : (e + 2) & 3), &cls);
        if (cls == C_LAND)
            sum += x, n++;
        else
            ssum += x, sn++;
    }
    v = sn ? ssum / (float)sn : n ? sum / (float)n
                                  : 0.0f;
    for (i = 0; i < 2; ++i)
    {
        int32_t at = i == 0 ? idx : nr * r_map + nc;
        if (i == 1 && (nc < 0 || nr < 0 || nc >= r_map || nr >= r_map || tile_cliff(c, idx, at)))
            break;
        if (s_vote[at] == VOTE_WATER && v < table_of(c, at))
            v = table_of(c, at);
    }
    /*  Open land at a cliff leans toward the other side (cliff_lean). */
    *lean = 0.0f;
    if (sn == 0 && nc >= 0 && nr >= 0 && nc < r_map && nr < r_map && tile_cliff(c, idx, nr * r_map + nc))
    {
        float o  = contrib(c, nr * r_map + nc, 4 + ((e + 2) & 3), &cls);
        float v0 = v;
        v        = cliff_lean(v, &o, 1);
        *lean    = v - v0;
    }
    return v;
}

/*  A tile drawn as the dual ground: open land.  Its middle's height, and
 *  the middle of each of its edges.  0 for any other tile. */
int tile_dual(const RCity *c, int32_t col, int32_t row, float *zc, float zm[4])
{
    int32_t idx = row * r_map + col;
    int     e;
    if (s_top[idx] != TOP_FIELD)
        return 0;
    *zc = ground_of(c, idx);
    for (e = 0; e < 4; ++e)
        zm[e] = field_edge(c, col, row, e);
    return 1;
}

/*  The level a flat pad sits at: on the water the water's surface (a
 *  marina), on a saddle one step up, else its ground. */
static float pad_level(const RCity *c, int32_t idx);

static float pad_level(const RCity *c, int32_t idx)
{
    if (is_water(c->map.xter[idx]))
        return table_of(c, idx);
    return ground_of(c, idx) + (saddle_lift(c, idx) ? 1.0f : 0.0f);
}

/*  Rule 2: what a tile draws, its four corner heights in the enum's
 *  order.  Both sides of every edge go through here. */
Kind tile_top(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float z[4])
{
    int32_t idx = row * r_map + col;
    int     k;
    (void)mask_bit;
    switch (s_top[idx])
    {
        case TOP_PAD_ANCHOR:
            {
                float lv = pad_level(c, s_anchor[idx]);
                for (k = 0; k < 4; ++k)
                    z[k] = lv;
                return T_PAD;
            }
        case TOP_WATER:
            {
                float t = table_of(c, idx);
                for (k = 0; k < 4; ++k)
                    z[k] = t;
                return T_WATER;
            }
        case TOP_PLANE:
            own_plane(c, idx, z);
            return T_PLANE;
        case TOP_PAD:
            {
                float lv = pad_level(c, idx);
                for (k = 0; k < 4; ++k)
                    z[k] = lv;
                return T_PAD;
            }
        default:
            for (k = 0; k < 4; ++k)
                z[k] = s_tc[idx * 4 + k];
            return T_LAND;
    }
}

/*  A tile's surface is water: a water tile, or a pad standing on the
 *  water (a marina stands on the water's surface). */
int water_top(const RCity *c, int32_t idx, Kind k)
{
    return k == T_WATER || (k == T_PAD && is_water(c->map.xter[idx]));
}

/*  The painter's index of the sweep, so the mesh composes with the
 *  sprites exactly as the software terrain pass does.  WHOSE place a
 *  tile takes is mesh_tops_settle's.  A building's footprint takes its
 *  anchor's and an elevated piece the neighbor's that owns its span
 *  ($173B8).  And this only turns the answer into an index. */
float tile_order(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit)
{
    int32_t at, ar, ac;
    (void)c, (void)mask_bit;
    /*  A tile off the map takes the place of the tile at the map's edge.
     *  A triangle there is filed in the chunk at the edge as well
     *  (mesh_chunk_of). */
    col = col < 0 ? 0 : col >= r_map ? r_map - 1
                                     : col;
    row = row < 0 ? 0 : row >= r_map ? r_map - 1
                                     : row;
    at = s_order[row * r_map + col], ar = at / r_map, ac = at % r_map;
    return (float)((ac + ar) * r_map + ar + 1);
}

/* ---- the field --------------------------------------------------------- */

R_GRID_BUF(static, float, s_fsum, 1)
R_GRID_BUF(static, float, s_fbsum, 1)
R_GRID_BUF(static, float, s_fbmin, 1)
R_GRID_BUF(static, float, s_fssum, 1)
R_GRID_BUF(static, uint8_t, s_fcnt, 1)
R_GRID_BUF(static, uint8_t, s_fbcnt, 1)
R_GRID_BUF(static, uint8_t, s_fscnt, 1)

/*  EACH TILE'S OWN CORNERS.  The four tiles round a corner fall into
 *  groups.  Two tiles side by side are in one group unless a cliff lies
 *  between them.  A tile's corner is its own group's: the mean of what
 *  the group adds there, held up to the table of any water in it.  Where
 *  the four are one group, which is every corner with no cliff at it,
 *  that is the shared corner itself. */
static void tile_corners(const RCity *c)
{
    /*  The four tiles round grid point (gx, gy), in the order the field
     *  sums them: north-west, north-east, south-west, south-east.  The
     *  corner of each that lies on the point, and the pairs that touch. */
    static const int DX[4] = {-1, 0, -1, 0}, DY[4] = {-1, -1, 0, 0}, KO[4] = {SE, SW, NE, NW};
    static const int PA[4] = {0, 2, 0, 1}, PB[4] = {1, 3, 2, 3};
    int32_t          col, row;
    int              k;
    memset(s_split, 0, (size_t)GRID * (size_t)GRID);
    for (row = 0; row < r_map; ++row)
        for (col = 0; col < r_map; ++col)
            for (k = 0; k < 4; ++k)
            {
                int32_t gi = corner_gi(col, row, k), gx = gi % GRID, gy = gi / GRID, at[4];
                int     in[4], grp[4], me = -1, i, j, all = 1, changed = 1;
                float   sum = 0.0f, ssum = 0.0f, v;
                int     n = 0, sn = 0;
                for (i = 0; i < 4; ++i)
                {
                    int32_t tc = gx + DX[i], tr = gy + DY[i];
                    in[i]  = tc >= 0 && tr >= 0 && tc < r_map && tr < r_map;
                    at[i]  = in[i] ? tr * r_map + tc : -1;
                    grp[i] = i;
                    if (at[i] == row * r_map + col)
                        me = i;
                }
                /*  Join the pairs with no cliff between them, until the
                 *  groups stand still. */
                while (changed)
                {
                    changed = 0;
                    for (j = 0; j < 4; ++j)
                    {
                        int a = PA[j], b = PB[j], lo;
                        if (!in[a] || !in[b] || grp[a] == grp[b] || tile_cliff(c, at[a], at[b]))
                            continue;
                        lo     = grp[a] < grp[b] ? grp[a] : grp[b];
                        grp[a] = grp[b] = lo, changed = 1;
                    }
                }
                for (i = 0; i < 4; ++i)
                    all &= !in[i] || grp[i] == grp[me];
                s_tl[(row * r_map + col) * 4 + k] = 0.0f;
                if (all)
                {
                    s_tc[(row * r_map + col) * 4 + k] = s_h[gi];
                    continue;
                }
                s_split[gi] = 1;
                {
                    /*  Each group's height: the held tops' mean first,
                     *  never below the water in the group. */
                    float gh[4];
                    int   gheld[4], ng = 0, g;
                    for (g = 0; g < 4; ++g)
                    {
                        sum = ssum = 0.0f, n = sn = 0;
                        for (i = 0; i < 4; ++i)
                        {
                            int   cls;
                            float x;
                            if (!in[i] || grp[i] != g)
                                continue;
                            x = contrib(c, at[i], KO[i], &cls);
                            if (cls == C_HELD)
                                ssum += x, sn++;
                            else
                                sum += x, n++;
                        }
                        gheld[g] = sn > 0;
                        gh[g]    = sn ? ssum / (float)sn : n ? sum / (float)n
                                                             : 0.0f;
                        for (i = 0; i < 4; ++i)
                            if (in[i] && grp[i] == g && s_vote[at[i]] == VOTE_WATER && gh[g] < table_of(c, at[i]))
                                gh[g] = table_of(c, at[i]);
                    }
                    /*  Open land leans toward the other groups (cliff_lean). */
                    v = gh[grp[me]];
                    if (!gheld[grp[me]])
                    {
                        float o[4];
                        for (g = 0; g < 4; ++g)
                            if (g != grp[me] && in[g] && grp[g] == g)
                                o[ng++] = gh[g];
                        v                                 = cliff_lean(v, o, ng);
                        s_tl[(row * r_map + col) * 4 + k] = v - gh[grp[me]];
                    }
                }
                s_tc[(row * r_map + col) * 4 + k] = v < 0.0f ? 0.0f : v;
            }
}

/*  Rule 1.  One shared height per corner: the mean of the contributions
 *  there, the held tops' first, never below the water the corner touches.
 *  Then each tile's own corners, its side of any cliff (tile_corners).
 *  The bed at a corner that touches water is the mean of the water tiles'
 *  beds there, never above the surface drawn over it.  Elsewhere it is
 *  the ground.
 *
 *  WHICH TILES ARE WATER for the seabed is the vote of mesh_tops_settle.
 *  Nothing here reads a cell to decide which is which. */
void build_field(const RCity *c)
{
    float *const sum = s_fsum, *const bsum = s_fbsum, *const bmin = s_fbmin, *const ssum = s_fssum;
    uint8_t *const cnt = s_fcnt, *const bcnt = s_fbcnt, *const scnt = s_fscnt;
    const uint8_t mask_bit = city_corner_mask(0);
    int32_t       col, row, g;
    int           k;
    memset(sum, 0, r_map_bytes(sum));
    memset(cnt, 0, r_map_bytes(cnt));
    memset(bsum, 0, r_map_bytes(bsum));
    memset(bcnt, 0, r_map_bytes(bcnt));
    memset(ssum, 0, r_map_bytes(ssum));
    memset(scnt, 0, r_map_bytes(scnt));
    for (g = 0; g < GRID * GRID; ++g)
    {
        s_h[g]  = -1.0f;
        bmin[g] = 1e9f;
    }
    for (row = 0; row < r_map; ++row)
        for (col = 0; col < r_map; ++col)
        {
            int32_t idx  = row * r_map + col;
            int     vote = s_vote[idx];
            float   z[4], top = 0.0f;
            own_plane(c, idx, z);
            if (vote == VOTE_WATER)
            {
                /*  The surface drawn over this bed: the table, or the
                 *  pad of a building standing on the water.  This can
                 *  lie below the table when its anchor is land a level
                 *  down.  The bed never rises above what is drawn over
                 *  it. */
                float zt[4];
                tile_top(c, col, row, mask_bit, zt);
                top = zt[0];
            }
            for (k = 0; k < 4; ++k)
            {
                int32_t gi = corner_gi(col, row, k);
                int     cls;
                float   x = contrib(c, idx, k, &cls);
                if (vote == VOTE_WATER)
                {
                    bsum[gi] += z[k];
                    bcnt[gi]++;
                    if (top < bmin[gi])
                        bmin[gi] = top;
                }
                if (cls == C_HELD)
                {
                    ssum[gi] += x;
                    scnt[gi]++;
                }
                else
                {
                    sum[gi] += x;
                    cnt[gi]++;
                }
            }
        }
    for (g = 0; g < GRID * GRID; ++g)
        if (scnt[g])
            s_h[g] = ssum[g] / (float)scnt[g]; /* a corner a top of its own holds */
        else if (cnt[g])
            s_h[g] = sum[g] / (float)cnt[g];
    /*  The shoreline is at the water level.  ALTM keeps one level per
     *  tile, and the slope code's lifts.  Along a shore the land's low
     *  corners lie at the bed's level, one below the water they touch:
     *  725 corners across the shipped cities.  The original never shows
     *  them, since the water tile's flat diamond at the table, with its
     *  sand rim, is painted over them.  The mesh showed a pit at every
     *  one.  So a land corner that touches water is never below that
     *  water, and a corner no land reaches is the water's table. */
    for (row = 0; row < r_map; ++row)
        for (col = 0; col < r_map; ++col)
        {
            int32_t idx = row * r_map + col;
            float   lv;
            if (s_vote[idx] != VOTE_WATER)
                continue;
            lv = table_of(c, idx);
            for (k = 0; k < 4; ++k)
                if (s_h[corner_gi(col, row, k)] < lv)
                    s_h[corner_gi(col, row, k)] = lv;
        }
    for (g = 0; g < GRID * GRID; ++g)
    {
        if (s_h[g] < 0.0f)
            s_h[g] = 0.0f;
        if (bcnt[g])
        {
            s_b[g] = bsum[g] / (float)bcnt[g];
            if (s_b[g] > bmin[g])
                s_b[g] = bmin[g];
        }
        else
            s_b[g] = s_h[g];
    }
    tile_corners(c);
    /*  Curvature is the Laplacian of the height field.  It is positive
     *  where the corner sits below the mean of its four neighbors, which
     *  is a hollow, and negative on a ridge.  The ground shader reads it
     *  as moisture. */
    for (row = 0; row < GRID; ++row)
        for (col = 0; col < GRID; ++col)
        {
            int32_t c0 = col > 0 ? col - 1 : col, c1 = col < GRID - 1 ? col + 1 : col;
            int32_t r0 = row > 0 ? row - 1 : row, r1 = row < GRID - 1 ? row + 1 : row;
            float   mean          = 0.25f * (s_h[row * GRID + c0] + s_h[row * GRID + c1] +
                                             s_h[r0 * GRID + col] + s_h[r1 * GRID + col]);
            s_k[row * GRID + col] = mean - s_h[row * GRID + col];
        }
}
