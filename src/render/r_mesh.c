/*  r_mesh.c -- see r_mesh.h.
 *
 *  The rules, all of them:
 *
 *  1. The ground is one field.  Every grid corner has one height, the
 *     mean of what the land tiles meeting there say (ALTM's level plus
 *     the slope code's lifts).  Water does not vote, but a corner that
 *     touches water is never below it: the shoreline is at the water.
 *  2. Every tile draws one top face, at the level the original draws its
 *     sprite: plain land draws the field; water -- a body, a stream, a
 *     waterfall, XTER 0x10 and up -- is flat at its table, as the
 *     original draws it; a building or a flat network piece is a flat
 *     pad at its level (the ground, one step up on a saddle as $17528
 *     has it, the table on water, the anchor's level across a building's
 *     footprint); a sloped network piece draws its own plane.
 *  3. Where two neighbours draw different heights along their common
 *     edge, a wall fills the gap: coursed blocks when either tile carries
 *     a structure, water where both are water, earth otherwise.
 *  4. A water body has a floor, the seabed, and the land continues below
 *     the surface down to it; the map's cut edges show the layers of
 *     sediment under everything and the water as the side of an aquarium.
 *
 *  Nothing else.  Because both sides of every edge are measured by the
 *  same function, the result is watertight: r_mesh_check proves it by
 *  finding no edge that belongs to only one triangle.
 */
#include "r_mesh.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  XTER's low nibble is one of fourteen slope codes, and each code is a
 *  corner mask -- bit 0 NW, 1 SW, 2 SE, 3 NE -- read off the sprites by
 *  tools/terrain_shapes.py into assets/terrain-shapes.json.  A set bit
 *  lifts that corner one level, and a lift is never more than one.
 *
 *  On screen NW is the diamond's top vertex, NE its left, SW its right
 *  and SE its bottom: NW is grid point (col, row), NE is (col + 1, row),
 *  SE is (col + 1, row + 1) and SW is (col, row + 1). */
static const uint8_t CODE_MASK[14] = {0, 9, 3, 6, 12, 11, 7, 14, 13, 1, 2, 4, 8, 5};
enum
{
    NW = 0,
    SW = 1,
    SE = 2,
    NE = 3
};

/*  The four edges, and the corners that bound each, A to B. */
enum
{
    E_N = 0,
    E_E = 1,
    E_S = 2,
    E_W = 3
};
static const int EDGE_A[4] = {NW, NE, SW, SW};
static const int EDGE_B[4] = {NE, SE, SE, NW};
/*  The neighbour's corners that coincide with ours across each edge. */
static const int   NBR_A[4]     = {SW, NW, NW, SE};
static const int   NBR_B[4]     = {SE, SW, NE, NE};
static const int   EDGE_DR[4]   = {-1, 0, 1, 0};
static const int   EDGE_DC[4]   = {0, 1, 0, -1};
static const float EDGE_N[4][3] = {
    {0.0f,  -1.0f, 0.0f},
    {1.0f,  0.0f,  0.0f},
    {0.0f,  1.0f,  0.0f},
    {-1.0f, 0.0f,  0.0f}
};

/*  World units per altitude level, for the normals only: the projection
 *  draws a level as 0.75 of a tile height whatever this says. */
#define LEVEL_H 0.5f

/*  The alpha channel carries a palette index for the resolve pass's
 *  shadow rule: a dirt-ramp index, so a flying thing darkens the ground
 *  as $19B76 does. */
#define LAND_INDEX (105.0f / 255.0f)

#define GRID (R_MAP + 1)
static float s_h[GRID * GRID]; /* the ground: one height per corner      */
static float s_k[GRID * GRID]; /* curvature: positive in a hollow        */
static float s_b[GRID * GRID]; /* the bed: the seabed at a corner that   */
                               /* touches water, the ground elsewhere    */
static float          s_road_class;
static float          s_seg_class = -1.0f;        /* the class of the segment being lofted, or -1 for the tile's */
static int32_t        s_seg_node[2][2];           /* the node tiles of the segment being lofted */
static int            s_seg_ctrl[2];              /* the control of the arm at each end of the segment being lofted */
static uint8_t        s_junc_ctrl[R_MAP * R_MAP]; /* per junction tile, two bits per arm: 0 none, 1 stop, 2 signal */
static int            s_seg_kind[2];
static const uint8_t *s_check_xbld; /* the last built city's XBLD, for the piece scan */ /* the class of the road strip being emitted */

#define MAT_GROUND   0.0f
#define MAT_ENG_WALL 1.0f   /* a retaining wall of coursed blocks          */
#define MAT_SEDIMENT 2.0f   /* the map edge's cut, layers of sediment      */
#define MAT_WATER    3.0f   /* the water column in that cut, an aquarium   */
#define MAT_SEABED   4.0f   /* the floor under the water, seen through it  */
#define MAT_EARTH    5.0f   /* a natural bank                              */
#define MAT_SURFACE  6.0f   /* the water's surface; vertical, a cascade    */
#define MAT_ROAD     7.0f   /* a road strip on the surface: col.r across,  */
                            /* -1..1, col.g along, in tiles                 */
#define MAT_PROP      8.0f  /* street furniture: a traffic light's pole     */
#define MAT_LAMP      9.0f  /* its lamp: col.r the junction's phase         */
#define MAT_ZEBRA     10.0f /* a crosswalk across a junction's arm           */
#define MAT_RAIL      11.0f /* a railway: two rails on ties, col.r across    */
#define MAT_WALK      13.0f /* the sidewalk: a road tile paved to its edges  */
#define MAT_SKIRT     12.0f /* a raised road's embankment: blocks, a decal's  */
#define MAT_RAIL_X    14.0f /* a rail across a road: the rails alone, flush in the crossing surface */
#define MAT_VEHICLE   15.0f /* a train car or a road car: col.r the paint, col.g the shade  */
#define MAT_XPANEL    16.0f /* a level crossing's surface: rubber panels across both tracks     */
#define MAT_XAPPROACH 17.0f /* the road approaching a crossing: solid lines and the RXR stencil */
                            /* depth, and no part of the surface              */

/* ---- what a tile is ---------------------------------------------------- */

/*  Water: XTER 0x10 and up.  Submerged and shore slopes, 0x10..0x2F, are
 *  a body at ALTM's table over a bed at ALTM's level; streams, canals
 *  and the waterfall, 0x30 on, have their table at their level.  The
 *  original draws every one flat at its table (tile_alt in r_soft.c):
 *  the low nibble says where the art puts its rim, not a height.  A
 *  stream with a slope nibble among flat neighbours, Bay View's column
 *  110, row 19, drew as a bump while it was read as a height. */
static int is_water(uint8_t xter)
{
    return xter >= 0x10u;
}

/*  A structure: anything but bare ground and trees. */
static int is_structure(uint8_t xbld)
{
    return xbld >= 0x0Eu;
}

/*  The network pieces drawn by a sprite that fits a slope: across every
 *  shipped city these stand on a sloped terrain code every time and the
 *  others on flat ground.  Power lines 0x10..0x13, roads 0x1F..0x22, rail
 *  0x2E..0x31, highway 0x3F..0x42, and the elevated pieces 0x61..0x64. */
static int sloped_piece(uint8_t b)
{
    return (b >= 0x10u && b <= 0x13u) || (b >= 0x1Fu && b <= 0x22u) ||
           (b >= 0x2Eu && b <= 0x31u) || (b >= 0x3Fu && b <= 0x42u) ||
           (b >= 0x61u && b <= 0x64u);
}

static int32_t slope_code(uint8_t xter)
{
    int32_t code = (xter < 0x40u) ? (xter & 0x0F) : 0;
    return code > 13 ? 0 : code;
}

static float ground_of(const RCity *c, int32_t idx)
{
    return (float)(c->altm[idx] & 0x1Fu);
}

static float table_of(const RCity *c, int32_t idx)
{
    return (float)((c->altm[idx] >> 5) & 0x1Fu);
}

/*  The grid index of a tile's corner k. */
static int32_t corner_gi(int32_t col, int32_t row, int k)
{
    return (row + ((k == SW || k == SE) ? 1 : 0)) * GRID + col +
           ((k == NE || k == SE) ? 1 : 0);
}

/*  A network piece on a saddle, terrain code 13, is drawn one step up
 *  by the original ($17528: `cmpi.w #$d`, then -12), and only there,
 *  and never an elevated piece. */
static int saddle_lift(const RCity *c, int32_t idx)
{
    uint8_t xter = c->xter[idx], xbld = c->xbld[idx];
    return xter < 0x10u && (xter & 0x0F) == 13 && is_structure(xbld) &&
           xbld < 0x61u;
}

/*  The tile's own plane: ALTM's level plus the slope code's lifts.  For
 *  a water body this is the bed. */
static void own_plane(const RCity *c, int32_t idx, float z[4])
{
    uint8_t mask = CODE_MASK[slope_code(c->xter[idx])];
    float   base = ground_of(c, idx);
    int     k;
    for (k = 0; k < 4; ++k)
        z[k] = base + (((mask >> k) & 1u) ? 1.0f : 0.0f);
}

/*  The anchor of a building's footprint: the tile carrying the
 *  rotation's corner bit, up to three tiles north and east. */
static int32_t anchor_of(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit)
{
    int32_t idx = row * R_MAP + col, best = 99, ai = idx;
    uint8_t b = c->xbld[idx];
    int     dr, dc;
    if (b < 0x70u || (c->xzon[idx] & mask_bit))
        return idx;
    for (dr = 0; dr >= -3; --dr)
        for (dc = 0; dc <= 3; ++dc)
        {
            int32_t ar = row + dr, ac = col + dc, i;
            if (ar < 0 || ac >= R_MAP || -dr + dc >= best)
                continue;
            i = ar * R_MAP + ac;
            if (c->xbld[i] == b && (c->xzon[i] & mask_bit))
            {
                best = -dr + dc;
                ai   = i;
            }
        }
    return ai;
}

/*  The level a flat pad sits at: on the water the water's surface (a
 *  marina), on a saddle one step up, else its ground. */
static float pad_level(const RCity *c, int32_t idx)
{
    if (is_water(c->xter[idx]))
        return table_of(c, idx);
    return ground_of(c, idx) + (saddle_lift(c, idx) ? 1.0f : 0.0f);
}

typedef enum
{
    T_LAND,  /* the field, cut on the sprite's diagonal              */
    T_WATER, /* flat at the table, over a seabed                     */
    T_PAD,   /* a flat pad at pad_level: a building, a flat piece    */
    T_PLANE  /* its own plane: a sloped network piece                */
} Kind;

/*  Rule 2: what a tile draws, its four corner heights in the enum's
 *  order.  Both sides of every edge go through here. */
static Kind tile_top(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float z[4])
{
    int32_t idx  = row * R_MAP + col;
    uint8_t xter = c->xter[idx], xbld = c->xbld[idx];
    int     k;
    if (xbld >= 0x70u)
    {
        float lv = pad_level(c, anchor_of(c, col, row, mask_bit));
        for (k = 0; k < 4; ++k)
            z[k] = lv;
        return T_PAD;
    }
    if (is_water(xter))
    {
        float t = table_of(c, idx);
        for (k = 0; k < 4; ++k)
            z[k] = t;
        return T_WATER;
    }
    if (is_structure(xbld))
    {
        if (slope_code(xter) != 0 && sloped_piece(xbld))
        {
            own_plane(c, idx, z);
            return T_PLANE;
        }
        {
            float lv = pad_level(c, idx);
            for (k = 0; k < 4; ++k)
                z[k] = lv;
        }
        return T_PAD;
    }
    for (k = 0; k < 4; ++k)
        z[k] = s_h[corner_gi(col, row, k)];
    return T_LAND;
}

/*  A tile's surface is water: a water tile, or a pad standing on the
 *  water (a marina stands on the water's surface). */
static int water_top(const RCity *c, int32_t idx, Kind k)
{
    return k == T_WATER || (k == T_PAD && is_water(c->xter[idx]));
}

/*  The painter's index of the sweep, so the mesh composes with the
 *  sprites exactly as the software terrain pass does.  A building's
 *  footprint takes its anchor's, as the elevated 2x2 pieces do
 *  ($173B8), so the art stays in front of its own pad. */
static float tile_order(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit)
{
    int32_t idx = row * R_MAP + col;
    uint8_t b   = c->xbld[idx];
    if (b >= 0x70u)
    {
        int32_t ai = anchor_of(c, col, row, mask_bit);
        int32_t ar = ai / R_MAP, ac = ai % R_MAP;
        return (float)((ac + ar) * R_MAP + ar + 1);
    }
    if (b >= 0x61u && b < 0x6Cu && !(c->xzon[idx] & mask_bit))
    {
        static const int dr[3] = {0, -1, -1};
        static const int dc[3] = {1, 1, 0};
        int              q;
        for (q = 0; q < 3; ++q)
        {
            int32_t ar = row + dr[q], ac = col + dc[q], ai;
            if (ar < 0 || ac >= R_MAP)
                continue;
            ai = ar * R_MAP + ac;
            if (c->xbld[ai] >= 0x61u && c->xbld[ai] < 0x6Cu &&
                (c->xzon[ai] & mask_bit))
                return (float)((ac + ar) * R_MAP + ar + 1);
        }
    }
    return (float)((col + row) * R_MAP + row + 1);
}

/* ---- the field --------------------------------------------------------- */

/*  Rule 1.  One height per corner from the land's votes, never below the
 *  water the corner touches; a corner with no land is the water's table
 *  there.  The bed at a corner that touches
 *  water is the mean of the water tiles' beds there, never above the
 *  surface drawn over it; elsewhere it is the ground. */
static void build_field(const RCity *c)
{
    static float   sum[GRID * GRID], bsum[GRID * GRID], bmin[GRID * GRID], ssum[GRID * GRID];
    static uint8_t cnt[GRID * GRID], bcnt[GRID * GRID], scnt[GRID * GRID];
    const uint8_t  mask_bit = r_city_corner_mask(c->rotation);
    int32_t        col, row, g;
    int            k;
    memset(sum, 0, sizeof sum);
    memset(cnt, 0, sizeof cnt);
    memset(bsum, 0, sizeof bsum);
    memset(bcnt, 0, sizeof bcnt);
    memset(ssum, 0, sizeof ssum);
    memset(scnt, 0, sizeof scnt);
    for (g = 0; g < GRID * GRID; ++g)
    {
        s_h[g]  = -1.0f;
        bmin[g] = 1e9f;
    }
    for (row = 0; row < R_MAP; ++row)
        for (col = 0; col < R_MAP; ++col)
        {
            int32_t idx  = row * R_MAP + col;
            uint8_t xter = c->xter[idx];
            float   z[4], top = 0.0f;
            int     structure = !is_water(xter) && is_structure(c->xbld[idx]);
            own_plane(c, idx, z);
            if (is_water(xter))
            {
                /*  The surface drawn over this bed: the table, or the pad
                 *  of a building standing on the water, which can lie
                 *  below the table when its anchor is land a level down
                 *  (The Bahamas, column 46, row 28); the bed never rises
                 *  above what is drawn over it. */
                float zt[4];
                tile_top(c, col, row, mask_bit, zt);
                top = zt[0];
            }
            for (k = 0; k < 4; ++k)
            {
                int32_t gi = corner_gi(col, row, k);
                if (is_water(xter))
                {
                    bsum[gi] += z[k];
                    bcnt[gi]++;
                    if (top < bmin[gi])
                        bmin[gi] = top;
                }
                else if (structure)
                {
                    /*  A structure draws its own pad or plane, not the
                     *  field, so it does not vote: three road pads and
                     *  one land tile a level up had put the corner a
                     *  quarter up, tilting the land and leaving a
                     *  quarter-level tooth of wall along the road (the
                     *  user's Barcelona screenshot, 2 September 2026).
                     *  The land keeps its shape; the wall rule closes the
                     *  whole difference. */
                    ssum[gi] += z[k];
                    scnt[gi]++;
                }
                else
                {
                    sum[gi] += z[k];
                    cnt[gi]++;
                }
            }
        }
    for (g = 0; g < GRID * GRID; ++g)
        if (cnt[g])
            s_h[g] = sum[g] / (float)cnt[g];
        else if (scnt[g])
            s_h[g] = ssum[g] / (float)scnt[g]; /* a corner among structures only */
    /*  The shoreline is at the water level.  ALTM keeps one level per
     *  tile and the slope code's lifts, and along a shore the land's low
     *  corners lie at the bed's level, one below the water they touch:
     *  725 corners across the shipped cities.  The original never shows
     *  them, since the water tile's flat diamond at the table, with its
     *  sand rim, is painted over them; the mesh showed a pit at every one
     *  (the user, on Bay View's column 67, row 124: "tiles dip
     *  underground... in the real game there are tiles used that hide
     *  this").  So a land corner that touches water is never below that
     *  water, and a corner no land reaches is the water's table. */
    for (row = 0; row < R_MAP; ++row)
        for (col = 0; col < R_MAP; ++col)
        {
            int32_t idx = row * R_MAP + col;
            float   lv;
            if (!is_water(c->xter[idx]))
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
    /*  Curvature: the Laplacian of the height field, positive where the
     *  corner sits below the mean of its four neighbours, a hollow, and
     *  negative on a ridge.  The ground shader reads it as moisture. */
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

/* ---- emitting ---------------------------------------------------------- */

static int grow(RMeshVert **v, uint32_t *n, uint32_t *cap, uint32_t need)
{
    uint32_t   c;
    RMeshVert *nv;
    if (*n + need <= *cap)
        return 0;
    c = *cap ? *cap : 4096u;
    while (c < *n + need)
        c *= 2u;
    nv = (RMeshVert *)realloc(*v, (size_t)c * sizeof *nv);
    if (!nv)
        return -1;
    *v   = nv;
    *cap = c;
    return 0;
}

/*  The normal of a face given as grid points, computed in the metric
 *  space where a level is LEVEL_H units, and pointed upward. */
static void face_normal(const float a[3], const float b[3], const float c[3], float n[3])
{
    float u[3], v[3], len;
    u[0] = b[0] - a[0];
    u[1] = b[1] - a[1];
    u[2] = (b[2] - a[2]) * LEVEL_H;
    v[0] = c[0] - a[0];
    v[1] = c[1] - a[1];
    v[2] = (c[2] - a[2]) * LEVEL_H;
    n[0] = u[1] * v[2] - u[2] * v[1];
    n[1] = u[2] * v[0] - u[0] * v[2];
    n[2] = u[0] * v[1] - u[1] * v[0];
    if (n[2] < 0.0f)
    {
        n[0] = -n[0];
        n[1] = -n[1];
        n[2] = -n[2];
    }
    len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len > 0.0f)
    {
        n[0] /= len;
        n[1] /= len;
        n[2] /= len;
    }
    else
        n[2] = 1.0f;
}

static int same_point(const float a[3], const float b[3])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

/*  One flat-shaded triangle.  A ground vertex also carries what the
 *  ground material reads of the topology at its corner: the curvature in
 *  the normal's fourth component and the height field's gradient, in
 *  world units, in the colour's first two -- or zero gradient when `flat`
 *  is set, for a levelled pad.  A wall vertex carries the material's
 *  reference heights instead, `ref` and `ref2` per vertex: the ground the
 *  sediment's layers follow, the table and the bed the water column
 *  deepens between.  A degenerate triangle is dropped. */
static int put_tri_r2(RMesh *m, const float p[3][3], const float *nrm, float order, const float col[3], const float *ref, const float *ref2, int flat)
{
    float nn[3];
    int   k;
    if (same_point(p[0], p[1]) || same_point(p[1], p[2]) || same_point(p[0], p[2]))
        return 0;
    if (m->to_water)
    {
        if (grow(&m->water, &m->n_water, &m->cap_water, 3) != 0)
            return -1;
    }
    else if (grow(&m->land, &m->n_land, &m->cap_land, 3) != 0)
        return -1;
    if (nrm)
        memcpy(nn, nrm, sizeof nn);
    else
        face_normal(p[0], p[1], p[2], nn);
    for (k = 0; k < 3; ++k)
    {
        RMeshVert *o  = m->to_water ? &m->water[m->n_water++]
                                    : &m->land[m->n_land++];
        int32_t    gc = (int32_t)(p[k][0] + 0.5f), gr = (int32_t)(p[k][1] + 0.5f);
        float      curv = 0.0f, gx = 0.0f, gy = 0.0f;
        if (!flat && gc >= 0 && gc < GRID && gr >= 0 && gr < GRID)
        {
            int32_t c0 = gc > 0 ? gc - 1 : gc, c1 = gc < GRID - 1 ? gc + 1 : gc;
            int32_t r0 = gr > 0 ? gr - 1 : gr, r1 = gr < GRID - 1 ? gr + 1 : gr;
            curv = s_k[gr * GRID + gc];
            gx   = (s_h[gr * GRID + c1] - s_h[gr * GRID + c0]) * LEVEL_H / (float)(c1 - c0);
            gy   = (s_h[r1 * GRID + gc] - s_h[r0 * GRID + gc]) * LEVEL_H / (float)(r1 - r0);
        }
        o->pos[0] = p[k][0];
        o->pos[1] = p[k][1];
        o->pos[2] = p[k][2];
        o->pos[3] = order;
        o->nrm[0] = nn[0];
        o->nrm[1] = nn[1];
        o->nrm[2] = nn[2];
        o->nrm[3] = col[2] > 6.5f ? s_road_class : curv; /* a strip: its class */
        if (col[2] >= 0.5f)
        {
            o->col[0] = ref ? ref[k] : col[0];
            o->col[1] = ref2 ? ref2[k] : col[1];
        }
        else
        {
            o->col[0] = gx;
            o->col[1] = gy;
        }
        o->col[2] = col[2];
        o->col[3] = LAND_INDEX;
    }
    return 0;
}

static int put_tri(RMesh *m, const float p[3][3], float order, const float col[3], int flat)
{
    return put_tri_r2(m, p, NULL, order, col, NULL, NULL, flat);
}

/*  A quad standing on an edge: the top points t0, t1 and the points b0,
 *  b1 under them.  `r` and `s` are the material's reference heights at
 *  the two ends.  The quad may twist -- t0 above b0 and t1 below b1 --
 *  and is still two triangles on the same four points, so every edge it
 *  makes is shared. */
static int put_wall_r2(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3], float r0, float r1, float s0, float s1)
{
    float p[3][3], ref[3], ref2[3];
    memcpy(p[0], t0, sizeof p[0]);
    memcpy(p[1], t1, sizeof p[1]);
    memcpy(p[2], b1, sizeof p[2]);
    ref[0]  = r0;
    ref[1]  = r1;
    ref[2]  = r1;
    ref2[0] = s0;
    ref2[1] = s1;
    ref2[2] = s1;
    if (put_tri_r2(m, p, nrm, order, col, ref, ref2, 0) != 0)
        return -1;
    memcpy(p[0], t0, sizeof p[0]);
    memcpy(p[1], b1, sizeof p[1]);
    memcpy(p[2], b0, sizeof p[2]);
    ref[0]  = r0;
    ref[1]  = r1;
    ref[2]  = r0;
    ref2[0] = s0;
    ref2[1] = s1;
    ref2[2] = s0;
    return put_tri_r2(m, p, nrm, order, col, ref, ref2, 0);
}

static int put_wall_r(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3], float r0, float r1)
{
    return put_wall_r2(m, t0, t1, b0, b1, nrm, order, col, r0, r1, col[1], col[1]);
}

static int put_wall(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3])
{
    return put_wall_r(m, t0, t1, b0, b1, nrm, order, col, t0[2], t1[2]);
}

/*  The diagonal a tile's top is cut on: a tile with one odd corner keeps
 *  a flat triangle on the other three (tools/terrain_shapes.py), so the
 *  cut avoids the odd corner; the saddle is cut NE-SW; a plane is planar
 *  either way. */
static int cut_ne_sw(int32_t code)
{
    uint8_t mask   = CODE_MASK[code];
    int     raised = 0, odd = -1, k;
    for (k = 0; k < 4; ++k)
        if (mask & (1u << k))
            ++raised;
    if (raised == 1 || raised == 3)
        for (k = 0; k < 4; ++k)
            if (((mask >> k) & 1u) == (raised == 1 ? 1u : 0u))
                odd = k;
    return (code == 13) || odd == NW || odd == SE;
}

/*  The two triangles of a tile's top, cut on the diagonal the sprite is
 *  cut on. */
static int put_top(RMesh *m, const float p[4][3], int32_t code, float order, const float col[3], int flat)
{
    int   ne_sw = cut_ne_sw(code);
    float tri[3][3];
    if (ne_sw)
    {
        memcpy(tri[0], p[NE], sizeof tri[0]);
        memcpy(tri[1], p[SE], sizeof tri[1]);
        memcpy(tri[2], p[SW], sizeof tri[2]);
        if (put_tri(m, tri, order, col, flat) != 0)
            return -1;
        memcpy(tri[0], p[NW], sizeof tri[0]);
        memcpy(tri[1], p[NE], sizeof tri[1]);
        memcpy(tri[2], p[SW], sizeof tri[2]);
        return put_tri(m, tri, order, col, flat);
    }
    memcpy(tri[0], p[NW], sizeof tri[0]);
    memcpy(tri[1], p[NE], sizeof tri[1]);
    memcpy(tri[2], p[SE], sizeof tri[2]);
    if (put_tri(m, tri, order, col, flat) != 0)
        return -1;
    memcpy(tri[0], p[NW], sizeof tri[0]);
    memcpy(tri[1], p[SE], sizeof tri[1]);
    memcpy(tri[2], p[SW], sizeof tri[2]);
    return put_tri(m, tri, order, col, flat);
}

/*  The mean colour of a sprite's opaque pixels through the phase-0
 *  palette, or `fallback` when the level has no such tile. */
static void tile_colour(const RAtlas *a, const RAtlasLevel *l, int32_t tile, float out[3], const float fallback[3])
{
    const RTile *t = r_atlas_tile(l, l->id_base + tile);
    long         r = 0, g = 0, b = 0, n = 0;
    int32_t      x, y;
    if (t)
        for (y = 0; y < (int32_t)t->h; ++y)
            for (x = 0; x < (int32_t)t->w; ++x)
            {
                uint8_t v = l->indices[((size_t)t->y + (size_t)y) *
                                           (size_t)l->w +
                                       (size_t)t->x + (size_t)x];
                if ((int32_t)v == l->transparent)
                    continue;
                r += a->palette0[v][0];
                g += a->palette0[v][1];
                b += a->palette0[v][2];
                ++n;
            }
    if (!n)
    {
        out[0] = fallback[0];
        out[1] = fallback[1];
        out[2] = fallback[2];
        return;
    }
    out[0] = (float)r / (float)n / 255.0f;
    out[1] = (float)g / (float)n / 255.0f;
    out[2] = (float)b / (float)n / 255.0f;
}

static void grid_point(int32_t col, int32_t row, int k, float z, float out[3])
{
    out[0] = (float)(col + ((k == NE || k == SE) ? 1 : 0));
    out[1] = (float)(row + ((k == SW || k == SE) ? 1 : 0));
    out[2] = z;
}

/* ---- roads, rails and power lines -------------------------------------- */

/*  The networks as geometry (the user, 2 September 2026: "a road renderer,
 *  which we can enable/disable like the other enhancements... real curved
 *  segments instead of the sharp diagonals... the road width to remain
 *  constant"; "handle many complex interactions"; "the power lines over
 *  roads look bad since it's a sprite that does the work").
 *
 *  Three families share one layout of fifteen pieces: two straights,
 *  four slopes, four corners, four tees and a crossing -- power lines at
 *  XBLD 0x0E, roads at 0x1D, rails at 0x2C.  Which edges a piece joins is
 *  read once from the road art, the asphalt at each edge's midpoint, and
 *  the other two families take the road piece at the same offset.  The
 *  four crossings 0x44..0x47 carry two families on one tile.
 *
 *  A road or rail is a strip along the piece's connections: a straight
 *  piece from edge midpoint to edge midpoint; a lone corner a quarter
 *  circle centred on the tile corner between its two edges, tangent to
 *  both; a corner on a staircase the chord between its two midpoints, so
 *  the staircase draws as one straight diagonal; a junction a box at the
 *  centre with an arm to each joined edge.  Where two pieces meet at an
 *  angle the inner corners are mitred to one point and a fan fills the
 *  outside, so the edge line runs unbroken round the turn.  The strips
 *  lie on the surface the tiles draw, sampled at every vertex's world
 *  position.  A power line is a pole at the tile's centre with a wire to
 *  each joined edge, meeting the neighbour's wire at the midpoint.
 *
 *  Width.  The oblique camera stretches the diagonal that runs toward it
 *  and squashes the other, so a road at one world width draws almost
 *  twice as wide on screen one way as the other; in the snap view every
 *  strip's world width is scaled by the direction it runs so that it
 *  reads the same width on screen everywhere (the user: "adjust the
 *  width of the road so that it remains consistent"), and the turned
 *  inspection view uses the true width. */
#define ROAD_W 0.71f    /* the strip: asphalt, a curb and a sidewalk each side; one  \
                         * width in the world everywhere (the user: "max 0.71 of the \
                         * tile size by my calculations") */
#define RAIL_W 0.62f    /* a double track's right-of-way, ballast shoulders in: two tracks 0.38 of a \
                         * tile apart, the spec's ratio of gauge to track spacing, sized against the \
                         * road as the road is sized to the tile (the user: "there needs to be two   \
                         * tracks", "especially since we had to resize the roads")                  */
#define ROAD_GRADE 1.0f /* the profile's steepest rise, levels per tile of road */

typedef enum
{
    F_POWER = 0,
    F_ROAD  = 1,
    F_RAIL  = 2
} Family;

/*  A road's class, from the traffic on it (the user: "avenues,
 *  boulevards, etc where appropriate"): 0 a two-lane road, 1 an avenue
 *  with a double centre line and four lanes, 2 a boulevard with a
 *  planted median.  Carried to the material in the normal's fourth
 *  component, where a ground vertex carries its curvature. */
static float road_class(const RCity *c, int32_t col, int32_t row)
{
    uint8_t t = c->xtrf[(row >> 1) * R_HALF + (col >> 1)];
    return t >= 160u ? 2.0f : t >= 64u ? 1.0f
                                       : 0.0f;
}

/*  The family of a piece and its index in the shared layout, 0..14, or
 *  -1 for anything else.  A crossing answers for its road (0x44, 0x45,
 *  0x46) or its rail (0x47) with the straight piece along the right
 *  axis; road_second() gives the other family on it. */
static int piece_family(uint8_t b, Family *f)
{
    if (b >= 0x0Eu && b <= 0x1Cu)
    {
        *f = F_POWER;
        return b - 0x0E;
    }
    if (b >= 0x1Du && b <= 0x2Bu)
    {
        *f = F_ROAD;
        return b - 0x1D;
    }
    if (b >= 0x2Cu && b <= 0x3Au)
    {
        *f = F_RAIL;
        return b - 0x2C;
    }
    /*  The six crossings, read off every shipped city's neighbours (the
     *  corpus, 2 September 2026): 0x43 a road east-west under a power
     *  line, 0x44 a road north-south under one; 0x45 a road east-west
     *  over a rail, 0x46 a road north-south over one; 0x47 a rail
     *  east-west under a power line, 0x48 a rail north-south under one.
     *  The highways begin at 0x49.  (An earlier table had 0x44 to 0x46
     *  turned and lacked 0x43 and 0x48, so a power line stopped at every
     *  road it crossed: the user's Barcelona, column 88, row 23.) */
    if (b == 0x43u || b == 0x45u)
    {
        *f = F_ROAD;
        return 0;
    }
    if (b == 0x44u || b == 0x46u)
    {
        *f = F_ROAD;
        return 1;
    }
    if (b == 0x47u)
    {
        *f = F_RAIL;
        return 0;
    }
    if (b == 0x48u)
    {
        *f = F_RAIL;
        return 1;
    }
    return -1;
}

/*  The second family a crossing carries, on the other axis: its piece
 *  index, or -1. */
static int piece_second(uint8_t b, Family *f)
{
    if (b == 0x43u || b == 0x47u)
    {
        *f = F_POWER;
        return 1;
    }
    if (b == 0x44u || b == 0x48u)
    {
        *f = F_POWER;
        return 0;
    }
    if (b == 0x45u)
    {
        *f = F_RAIL;
        return 1;
    }
    if (b == 0x46u)
    {
        *f = F_RAIL;
        return 0;
    }
    return -1;
}

/*  The strip is one width in the world whatever direction it runs; the
 *  oblique camera then draws a road toward it wider than one across
 *  it, as it draws everything else.  (A build that scaled the world
 *  width by direction to equalise the screen width was rejected: "you
 *  are not keeping road widths consistent".) */
static float width_factor(float dx, float dy, int compensate)
{
    (void)dx;
    (void)dy;
    (void)compensate;
    return 1.0f;
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

/*  The drawn surface at a world point, from whichever tile holds it, so
 *  a strip that reaches past its tile's edge follows the ground there. */
static float surface_at_world(const RCity *c, uint8_t mask_bit, float x, float y)
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

enum
{
    L_N = 1,
    L_E = 2,
    L_S = 4,
    L_W = 8
};

static const float ROAD_MU[4] = {0.5f, 1.0f, 0.5f, 0.0f}; /* edge midpoints, N E S W */
static const float ROAD_MV[4] = {0.0f, 0.5f, 1.0f, 0.5f};
static const float ROAD_DU[4] = {0.0f, 1.0f, 0.0f, -1.0f}; /* out through the edge */
static const float ROAD_DV[4] = {-1.0f, 0.0f, 1.0f, 0.0f};

/*  Which edges a piece joins, from the road art: the asphalt (palette
 *  0x91) or a dash (0x8B) in the three-by-three around each edge's
 *  midpoint, a tenth of the way in.  The slope pieces' art is tall; they
 *  are straight along their slope, which the terrain code says. */
static int piece_links(const RAtlasLevel *l, int piece, uint8_t xter)
{
    static int8_t cache[15];
    static int    inited = 0;
    const RTile  *t;
    int32_t       tw = l->tile_w, th = l->tile_h, y0, e, links = 0;
    if (!inited)
    {
        memset(cache, -1, sizeof cache);
        inited = 1;
    }
    if (piece < 0 || piece > 14)
        return 0;
    if (piece >= 2 && piece <= 5)
    {
        uint8_t mask = CODE_MASK[slope_code(xter)];
        return (mask == 3 || mask == 12) ? (L_E | L_W) : (L_N | L_S);
    }
    if (cache[piece] >= 0)
        return cache[piece];
    t = r_atlas_tile(l, l->id_base + 0x1D + piece);
    if (!t)
        return 0;
    y0 = (int32_t)t->h - (th + 1); /* the diamond fills the bottom rows */
    for (e = 0; e < 4; ++e)
    {
        static const float mx[4] = {0.25f, 0.25f, 0.75f, 0.75f};
        static const float my[4] = {0.25f, 0.75f, 0.75f, 0.25f};
        int32_t            sx    = (int32_t)((0.5f + (mx[e] - 0.5f) * 0.9f) * (float)tw);
        int32_t            sy    = y0 + (int32_t)((0.5f + (my[e] - 0.5f) * 0.9f) * (float)th);
        int32_t            dx, dy, n = 0;
        for (dy = -1; dy <= 1; ++dy)
            for (dx = -1; dx <= 1; ++dx)
            {
                int32_t x = sx + dx, y = sy + dy;
                uint8_t v;
                if (x < 0 || y < 0 || x >= (int32_t)t->w || y >= (int32_t)t->h)
                    continue;
                v = l->indices[((size_t)t->y + (size_t)y) * (size_t)l->w +
                               (size_t)t->x + (size_t)x];
                if (v == 0x91u || v == 0x8Bu)
                    ++n;
            }
        if (n >= 2)
            links |= 1 << e;
    }
    cache[piece] = (int8_t)links;
    return links;
}

/*  What a tile carries of a family: its links, or 0. */
static int tile_links(const RCity *c, const RAtlasLevel *l, int32_t col, int32_t row, Family want)
{
    int32_t idx;
    Family  f;
    int     piece;
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return 0;
    idx   = row * R_MAP + col;
    piece = piece_family(c->xbld[idx], &f);
    if (piece >= 0 && f == want)
        return piece_links(l, piece, c->xter[idx]);
    piece = piece_second(c->xbld[idx], &f);
    if (piece >= 0 && f == want)
        return piece_links(l, piece, c->xter[idx]);
    return 0;
}

static int link_count(int links)
{
    return (links & 1) + ((links >> 1) & 1) + ((links >> 2) & 1) + ((links >> 3) & 1);
}

/*  The links a tile can actually follow: those its neighbour returns,
 *  and those that leave the map.  A piece whose art points at grass, a
 *  stub against the flat side of a T, a road bulldozed short: the
 *  original draws every such sprite as it is, so the segment must end
 *  there, at a butt end, rather than the tile going undrawn because
 *  the chain it lies on has no node to be walked from. */
static int eff_links(const RCity *c, const RAtlasLevel *l, int32_t col, int32_t row, Family f)
{
    int links = tile_links(c, l, col, row, f), out = 0, e;
    for (e = 0; e < 4; ++e)
    {
        int32_t nc, nr;
        if (!(links & (1 << e)))
            continue;
        nc = col + (int32_t)ROAD_DU[e];
        nr = row + (int32_t)ROAD_DV[e];
        if (nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
            out |= 1 << e; /* off the map: the road runs to the edge */
        else if (tile_links(c, l, nc, nr, f) & (1 << ((e + 2) & 3)))
            out |= 1 << e;
    }
    return out;
}

/*  One quad of a strip between two cross-sections, each given by its
 *  two end points in world tile coordinates, with the across and along
 *  values of the material; `za`/`zb` are the sections' heights, or
 *  negative to take the ground under each corner. */
/*  A road face clipped to the tile grid, each piece with the painter's
 *  order of the tile under its own centroid (the caller's fraction of
 *  an order, a strip's small offset, carried over).  A quad that
 *  straddles two tiles had carried the order of the tile under its
 *  midpoint, and the later tile's ground was drawn over the part of the
 *  quad inside it: a wedge of grass cut into every arc and every
 *  diagonal at each tile boundary (Barcelona's rail at column 106, row
 *  10; Oakland's bends).  Sutherland-Hodgman against every integer x
 *  and y line the triangle's box spans, the across and along values
 *  interpolated with the points. */
typedef struct
{
    float x, y, z, ac, al;
} ClipVert;

static int clip_axis(const ClipVert *in, int n, int axis, float k, int below, ClipVert *out)
{
    int m = 0, i;
    for (i = 0; i < n; ++i)
    {
        const ClipVert *a = &in[i], *b = &in[(i + 1) % n];
        float           va = axis ? a->y : a->x, vb = axis ? b->y : b->x;
        int             ia = below ? va <= k + 1e-6f : va >= k - 1e-6f;
        int             ib = below ? vb <= k + 1e-6f : vb >= k - 1e-6f;
        if (ia)
            out[m++] = *a;
        if (ia != ib && fabsf(vb - va) > 1e-9f)
        {
            float    t = (k - va) / (vb - va);
            ClipVert v;
            v.x      = a->x + (b->x - a->x) * t;
            v.y      = a->y + (b->y - a->y) * t;
            v.z      = a->z + (b->z - a->z) * t;
            v.ac     = a->ac + (b->ac - a->ac) * t;
            v.al     = a->al + (b->al - a->al) * t;
            out[m++] = v;
        }
        if (m >= 14)
            break;
    }
    return m;
}

static int put_tri_road_n(RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float *nrm, const float col[3], const float ref[3], const float ref2[3])
{
    static ClipVert poly[48][16];
    static int      cnt[48];
    ClipVert        tmp[16];
    const float     xcol[3] = {col[0], col[1], MAT_RAIL_X};
    const float    *pcol    = col;
    float           minx = tri[0][0], maxx = tri[0][0], miny = tri[0][1], maxy = tri[0][1], frac;
    int             np = 1, k, axis, q;
    for (k = 1; k < 3; ++k)
    {
        if (tri[k][0] < minx)
            minx = tri[k][0];
        if (tri[k][0] > maxx)
            maxx = tri[k][0];
        if (tri[k][1] < miny)
            miny = tri[k][1];
        if (tri[k][1] > maxy)
            maxy = tri[k][1];
    }
    frac = order - floorf(order);
    /*  Within one tile: as it is, in that tile's slot (a face of a
     *  train car lying wholly in the tile ahead of the car's centre had
     *  kept the centre's slot, and lay under that tile's ground), a rail
     *  on a crossing tile as its rails alone. */
    if (floorf(minx) == floorf(maxx - 1e-6f) && floorf(miny) == floorf(maxy - 1e-6f))
    {
        int32_t tc = (int32_t)floorf(minx), tr = (int32_t)floorf(miny);
        if (tc >= 0 && tr >= 0 && tc < R_MAP && tr < R_MAP)
        {
            uint8_t b = c->xbld[tr * R_MAP + tc];
            order     = tile_order(c, tc, tr, mask_bit) + frac;
            if (col[2] > 10.5f && col[2] < 11.5f && (b == 0x45u || b == 0x46u))
                pcol = xcol;
        }
        return put_tri_r2(m, tri, nrm, order, pcol, ref, ref2, 0);
    }
    for (k = 0; k < 3; ++k)
    {
        poly[0][k].x  = tri[k][0];
        poly[0][k].y  = tri[k][1];
        poly[0][k].z  = tri[k][2];
        poly[0][k].ac = ref[k];
        poly[0][k].al = ref2[k];
    }
    cnt[0] = 3;
    for (axis = 0; axis < 2; ++axis)
    {
        float lo = axis ? miny : minx, hi = axis ? maxy : maxx;
        float line;
        for (line = floorf(lo) + 1.0f; line < hi - 1e-6f; line += 1.0f)
        {
            int n0 = np;
            for (q = 0; q < n0; ++q)
            {
                int nb = clip_axis(poly[q], cnt[q], axis, line, 1, tmp);
                int na;
                if (np >= 48)
                    break;
                na = clip_axis(poly[q], cnt[q], axis, line, 0, poly[np]);
                memcpy(poly[q], tmp, (size_t)nb * sizeof tmp[0]);
                cnt[q] = nb;
                if (na >= 3)
                    cnt[np++] = na;
            }
        }
    }
    for (q = 0; q < np; ++q)
    {
        float   cx = 0.0f, cy = 0.0f, porder;
        int32_t tc, tr;
        if (cnt[q] < 3)
            continue;
        for (k = 0; k < cnt[q]; ++k)
        {
            cx += poly[q][k].x;
            cy += poly[q][k].y;
        }
        cx /= (float)cnt[q];
        cy /= (float)cnt[q];
        tc = (int32_t)floorf(cx);
        tr = (int32_t)floorf(cy);
        if (tc < 0)
            tc = 0;
        if (tr < 0)
            tr = 0;
        if (tc >= R_MAP)
            tc = R_MAP - 1;
        if (tr >= R_MAP)
            tr = R_MAP - 1;
        porder = tile_order(c, tc, tr, mask_bit) + frac;
        /* a rail on a road-over-rail crossing tile: only its rails, in the crossing surface (spec 3.15) */
        pcol = col;
        if (col[2] > 10.5f && col[2] < 11.5f)
        {
            uint8_t b = c->xbld[tr * R_MAP + tc];
            if (b == 0x45u || b == 0x46u)
                pcol = xcol;
        }
        for (k = 1; k + 1 < cnt[q]; ++k)
        {
            float           t3[3][3], r[3], r2[3];
            const ClipVert *v[3] = {&poly[q][0], &poly[q][k], &poly[q][k + 1]};
            int             j;
            for (j = 0; j < 3; ++j)
            {
                t3[j][0] = v[j]->x;
                t3[j][1] = v[j]->y;
                t3[j][2] = v[j]->z;
                r[j]     = v[j]->ac;
                r2[j]    = v[j]->al;
            }
            if (put_tri_r2(m, (const float (*)[3])t3, nrm, porder, pcol, r, r2, 0) != 0)
                return -1;
        }
    }
    return 0;
}

static int put_tri_road(RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float col[3], const float ref[3], const float ref2[3])
{
    return put_tri_road_n(m, c, mask_bit, order, tri, NULL, col, ref, ref2);
}

static int strip_quad_z(RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float a0[2], const float a1[2], const float b0[2], const float b1[2], float za, float zb, float across0, float across1, float along_a, float along_b, float mat)
{
    float        road_col[3] = {0.0f, 0.0f, mat};
    float        p[4][3], tri[3][3], ref[3], ref2[3];
    const float *pt[4] = {a0, a1, b1, b0};
    int          k;
    for (k = 0; k < 4; ++k)
    {
        float z = k < 2 ? za : zb;
        p[k][0] = pt[k][0];
        p[k][1] = pt[k][1];
        p[k][2] = z >= 0.0f ? z : surface_at_world(c, mask_bit, p[k][0], p[k][1]);
    }
    memcpy(tri[0], p[0], sizeof tri[0]);
    memcpy(tri[1], p[1], sizeof tri[1]);
    memcpy(tri[2], p[2], sizeof tri[2]);
    ref[0]  = across0;
    ref[1]  = across1;
    ref[2]  = across1;
    ref2[0] = along_a;
    ref2[1] = along_a;
    ref2[2] = along_b;
    if (put_tri_road(m, c, mask_bit, order, (const float (*)[3])tri, road_col, ref, ref2) != 0)
        return -1;
    memcpy(tri[0], p[0], sizeof tri[0]);
    memcpy(tri[1], p[2], sizeof tri[1]);
    memcpy(tri[2], p[3], sizeof tri[2]);
    ref[0]  = across0;
    ref[1]  = across1;
    ref[2]  = across0;
    ref2[0] = along_a;
    ref2[1] = along_b;
    ref2[2] = along_b;
    return put_tri_road(m, c, mask_bit, order, (const float (*)[3])tri, road_col, ref, ref2);
}

static int strip_quad(RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float a0[2], const float a1[2], const float b0[2], const float b1[2], float across0, float across1, float along_a, float along_b, float mat)
{
    return strip_quad_z(m, c, mask_bit, order, a0, a1, b0, b1, -1.0f, -1.0f, across0, across1, along_a, along_b, mat);
}

/*  A fan of triangles about a point, from angle t0 at radius r0 to t1
 *  at r1, one material; the rim carries across 1, the apex `across_c`. */
static int strip_fan(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float t0, float t1, float r0, float r1, float across_c, float mat, int n)
{
    float road_col[3] = {0.0f, 0.0f, mat};
    int   i;
    for (i = 0; i < n; ++i)
    {
        float fa = (float)i / (float)n, fb = (float)(i + 1) / (float)n;
        float ta = t0 + (t1 - t0) * fa, tb = t0 + (t1 - t0) * fb;
        float ra = r0 + (r1 - r0) * fa, rb = r0 + (r1 - r0) * fb;
        float tri[3][3], ref[3] = {across_c, 1.0f, 1.0f}, ref2[3] = {-1.0f, -1.0f, -1.0f};
        tri[0][0] = cx;
        tri[0][1] = cy;
        tri[1][0] = cx + ra * cosf(ta);
        tri[1][1] = cy + ra * sinf(ta);
        tri[2][0] = cx + rb * cosf(tb);
        tri[2][1] = cy + rb * sinf(tb);
        tri[0][2] = surface_at_world(c, mask_bit, tri[0][0], tri[0][1]);
        tri[1][2] = surface_at_world(c, mask_bit, tri[1][0], tri[1][1]);
        tri[2][2] = surface_at_world(c, mask_bit, tri[2][0], tri[2][1]);
        if (put_tri_road(m, c, mask_bit, order, (const float (*)[3])tri, road_col, ref, ref2) != 0)
            return -1;
    }
    return 0;
}

/*  A box standing on the surface at world (x, y), `w` by `d` in tiles,
 *  from `z0` to `z1` levels above the ground there: four sides and a
 *  top, one material, `phase` in col.r. */
static int put_box(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float w, float d, float z0, float z1, float mat, float phase)
{
    float g  = surface_at_world(c, mask_bit, cx, cy);
    float x0 = cx - w * 0.5f, x1 = cx + w * 0.5f, y0 = cy - d * 0.5f, y1 = cy + d * 0.5f;
    float lo = g + z0, hi = g + z1;
    float col3[3] = {phase, 0.0f, mat};
    float t[4][3] = {
        {x0, y0, hi},
        {x1, y0, hi},
        {x1, y1, hi},
        {x0, y1, hi}
    };
    float b[4][3] = {
        {x0, y0, lo},
        {x1, y0, lo},
        {x1, y1, lo},
        {x0, y1, lo}
    };
    static const float nrm[4][3] = {
        {0.0f,  -1.0f, 0.0f},
        {1.0f,  0.0f,  0.0f},
        {0.0f,  1.0f,  0.0f},
        {-1.0f, 0.0f,  0.0f}
    };
    float tri[3][3], ref[3] = {phase, phase, phase};
    int   k;
    for (k = 0; k < 4; ++k)
        if (put_wall_r(m, t[k], t[(k + 1) & 3], b[k], b[(k + 1) & 3], nrm[k], order, col3, phase, phase) != 0)
            return -1;
    memcpy(tri[0], t[0], sizeof tri[0]);
    memcpy(tri[1], t[1], sizeof tri[1]);
    memcpy(tri[2], t[2], sizeof tri[2]);
    if (put_tri_r2(m, (const float (*)[3])tri, NULL, order, col3, ref, ref, 0) != 0)
        return -1;
    memcpy(tri[1], t[2], sizeof tri[1]);
    memcpy(tri[2], t[3], sizeof tri[2]);
    return put_tri_r2(m, (const float (*)[3])tri, NULL, order, col3, ref, ref, 0);
}

/*  A wire: a thin quad from world (x0, y0) at height z0 to (x1, y1) at
 *  z1 above the ground at each end, sagging in the middle. */
static int put_wire(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x0, float y0, float z0, float x1, float y1, float z1, float sag)
{
    static const float col3[3] = {0.0f, 0.0f, MAT_PROP};
    const int          n       = 4;
    float              w       = 0.02f;
    float              dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    float              nx = -dy / len * w, ny = dx / len * w;
    float              ga = surface_at_world(c, mask_bit, x0, y0);
    float              gb = surface_at_world(c, mask_bit, x1, y1);
    int                i;
    for (i = 0; i < n; ++i)
    {
        float ta = (float)i / (float)n, tb = (float)(i + 1) / (float)n;
        float za = ga + (gb - ga) * ta + z0 + (z1 - z0) * ta - sag * 4.0f * ta * (1.0f - ta);
        float zb = ga + (gb - ga) * tb + z0 + (z1 - z0) * tb - sag * 4.0f * tb * (1.0f - tb);
        float tri[3][3], ref[3] = {0.0f, 0.0f, 0.0f};
        float ax = x0 + dx * ta, ay = y0 + dy * ta, bx = x0 + dx * tb, by = y0 + dy * tb;
        tri[0][0] = ax - nx;
        tri[0][1] = ay - ny;
        tri[0][2] = za;
        tri[1][0] = ax + nx;
        tri[1][1] = ay + ny;
        tri[1][2] = za;
        tri[2][0] = bx + nx;
        tri[2][1] = by + ny;
        tri[2][2] = zb;
        if (put_tri_r2(m, (const float (*)[3])tri, NULL, order, col3, ref, ref, 0) != 0)
            return -1;
        tri[1][0] = bx + nx;
        tri[1][1] = by + ny;
        tri[1][2] = zb;
        tri[2][0] = bx - nx;
        tri[2][1] = by - ny;
        tri[2][2] = zb;
        if (put_tri_r2(m, (const float (*)[3])tri, NULL, order, col3, ref, ref, 0) != 0)
            return -1;
    }
    return 0;
}

/*  A traffic signal for the approach from edge `e` of a road junction
 *  at tile (col, row) (the user: "real ones... proper ones with poles and
 *  lights hanging down"): a pole at the driver's right-hand corner of
 *  the junction, a mast arm from its top out over the road, and a
 *  three-lamp head hanging from the arm's end, its lamps facing the
 *  approaching traffic.  The lamps carry the junction's phase in col.r
 *  and, in col.g, their group (north-south or east-west arms) and which
 *  lamp they are, for the cycle the shader runs. */
static int put_signal(RMesh *m, const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float order, int e, float h)
{
    /*  Spec 6.2 at the road's scale: a mast-arm pole 7 m tall, the arm at
     *  6 m over the road to the lane's centre, a three-section head 340
     *  by 1070 mm hanging from it with its bottom 5 m up, 300 mm lenses. */
    static const int right[4] = {3, 0, 1, 2};
    float            phase    = (float)((col * 7 + row * 13) % 8) / 8.0f;
    float            group    = (e == 0 || e == 2) ? 0.0f : 3.0f;
    int              r        = right[e];
    float            cx = (float)col + 0.5f, cy = (float)row + 0.5f;
    float            pu = cx + (ROAD_DU[e] + ROAD_DU[r]) * (h + 0.06f), pv = cy + (ROAD_DV[e] + ROAD_DV[r]) * (h + 0.06f);
    float            hu = cx + ROAD_DU[e] * (h + 0.06f) + ROAD_DU[r] * 0.14f, hv = cy + ROAD_DV[e] * (h + 0.06f) + ROAD_DV[r] * 0.14f;
    float            g = surface_at_world(c, mask_bit, hu, hv);
    int              k;
    if (put_box(m, c, mask_bit, order, pu, pv, 0.025f, 0.025f, 0.0f, 0.88f, MAT_PROP, phase) != 0)
        return -1;
    {
        float au = 0.5f * (pu + hu), av = 0.5f * (pv + hv);
        float len     = fabsf(hu - pu) + fabsf(hv - pv) + 0.02f;
        int   along_u = fabsf(hu - pu) > fabsf(hv - pv);
        if (put_box(m, c, mask_bit, order, au, av, along_u ? len : 0.016f, along_u ? 0.016f : len, 0.735f, 0.75f, MAT_PROP, phase) != 0)
            return -1;
    }
    if (put_box(m, c, mask_bit, order, hu, hv, ROAD_DU[e] != 0.0f ? 0.02f : 0.023f, ROAD_DU[e] != 0.0f ? 0.023f : 0.02f, 0.60f, 0.735f, MAT_PROP, phase) != 0)
        return -1;
    for (k = 0; k < 3; ++k)
    {
        float lz = 0.712f - 0.045f * (float)k, sz = 0.011f;
        float fu = hu + ROAD_DU[e] * 0.012f, fv = hv + ROAD_DV[e] * 0.012f;
        float lamp[3] = {phase, group + (float)k, MAT_LAMP};
        float q[4][3], tri[3][3], ref[3] = {phase, phase, phase}, ref2[3];
        float wu = ROAD_DU[e] != 0.0f ? 0.0f : sz, wv = ROAD_DU[e] != 0.0f ? sz : 0.0f;
        float nrm[3] = {ROAD_DU[e], ROAD_DV[e], 0.0f};
        int   t;
        q[0][0] = fu - wu;
        q[0][1] = fv - wv;
        q[0][2] = g + lz - sz;
        q[1][0] = fu + wu;
        q[1][1] = fv + wv;
        q[1][2] = g + lz - sz;
        q[2][0] = fu + wu;
        q[2][1] = fv + wv;
        q[2][2] = g + lz + sz;
        q[3][0] = fu - wu;
        q[3][1] = fv - wv;
        q[3][2] = g + lz + sz;
        for (t = 0; t < 3; ++t)
            ref2[t] = lamp[1];
        memcpy(tri[0], q[0], sizeof tri[0]);
        memcpy(tri[1], q[1], sizeof tri[1]);
        memcpy(tri[2], q[2], sizeof tri[2]);
        if (put_tri_r2(m, (const float (*)[3])tri, nrm, order, lamp, ref, ref2, 0) != 0)
            return -1;
        memcpy(tri[1], q[2], sizeof tri[1]);
        memcpy(tri[2], q[3], sizeof tri[2]);
        if (put_tri_r2(m, (const float (*)[3])tri, nrm, order, lamp, ref, ref2, 0) != 0)
            return -1;
    }
    return 0;
}

/*  A crossbuck at a rail crossing: a post with two crossed arms. */
static int put_crossbuck(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, int along_u)
{
    /* spec 6.1: a 100 mm post 4.3 m tall, the blades 1.22 by 0.23 m crossed at 2.7 m */
    if (put_box(m, c, mask_bit, order, x, y, 0.012f, 0.012f, 0.0f, 0.54f, MAT_PROP, 0.0f) != 0)
        return -1;
    if (put_box(m, c, mask_bit, order, x, y, along_u ? 0.02f : 0.081f, along_u ? 0.081f : 0.02f, 0.325f, 0.355f, MAT_LAMP, 0.0f) != 0)
        return -1;
    return put_box(m, c, mask_bit, order, x, y, along_u ? 0.02f : 0.081f, along_u ? 0.081f : 0.02f, 0.30f, 0.33f, MAT_LAMP, 0.0f);
}

/*  One lamp face, a small square of MAT_LAMP with lamp code `code`
 *  facing (fx, fy) at (x, y), `z` levels over the ground `g`. */
static int put_lamp_face(RMesh *m, float order, float x, float y, float g, float z, float fx, float fy, float sz, float phase, float code)
{
    float lamp[3] = {phase, code, MAT_LAMP};
    float q[4][3], tri[3][3], ref[3] = {phase, phase, phase}, ref2[3] = {code, code, code};
    float wu = -fy * sz, wv = fx * sz, nrm[3] = {fx, fy, 0.0f};
    float ox = x + fx * 0.02f, oy = y + fy * 0.02f;
    q[0][0] = ox - wu;
    q[0][1] = oy - wv;
    q[0][2] = g + z - sz;
    q[1][0] = ox + wu;
    q[1][1] = oy + wv;
    q[1][2] = g + z - sz;
    q[2][0] = ox + wu;
    q[2][1] = oy + wv;
    q[2][2] = g + z + sz;
    q[3][0] = ox - wu;
    q[3][1] = oy - wv;
    q[3][2] = g + z + sz;
    memcpy(tri[0], q[0], sizeof tri[0]);
    memcpy(tri[1], q[1], sizeof tri[1]);
    memcpy(tri[2], q[2], sizeof tri[2]);
    if (code >= 12.5f && code < 13.5f)
    {
        /* a sign's face: u in col.r, v in the fraction of col.g, for its shape */
        ref[0]  = 0.0f;
        ref[1]  = 1.0f;
        ref[2]  = 1.0f;
        ref2[0] = 13.0f;
        ref2[1] = 13.0f;
        ref2[2] = 13.5f;
    }
    if (put_tri_r2(m, (const float (*)[3])tri, nrm, order, lamp, ref, ref2, 0) != 0)
        return -1;
    memcpy(tri[1], q[2], sizeof tri[1]);
    memcpy(tri[2], q[3], sizeof tri[2]);
    if (code >= 12.5f && code < 13.5f)
    {
        ref[0]  = 0.0f;
        ref[1]  = 1.0f;
        ref[2]  = 0.0f;
        ref2[0] = 13.0f;
        ref2[1] = 13.5f;
        ref2[2] = 13.5f;
    }
    return put_tri_r2(m, (const float (*)[3])tri, nrm, order, lamp, ref, ref2, 0);
}

/*  A wayside colour-light signal (spec 5.6): a mast, a head with a
 *  hood, and one steady aspect facing the approaching train, green on
 *  a block signal, red on an absolute one at a junction. */
static int put_rail_signal(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy, int absolute, float s_along, int dir)
{
    RRailSig *sig;
    if (put_box(m, c, mask_bit, order, x, y, 0.012f, 0.012f, 0.0f, 0.62f, MAT_PROP, 0.0f) != 0)
        return -1;
    if (put_box(m, c, mask_bit, order, x, y, 0.03f, 0.03f, 0.50f, 0.62f, MAT_PROP, 0.0f) != 0)
        return -1;
    /* the aspect is the traffic's, lit by the block's occupancy each frame */
    if (m->n_rsigs + 1u > m->cap_rsigs)
    {
        uint32_t  nc = m->cap_rsigs ? m->cap_rsigs * 2u : 128u;
        RRailSig *ns = (RRailSig *)realloc(m->rsigs, nc * sizeof *ns);
        if (!ns)
            return -1;
        m->rsigs     = ns;
        m->cap_rsigs = nc;
    }
    sig           = &m->rsigs[m->n_rsigs++];
    sig->x        = x;
    sig->y        = y;
    sig->fx       = fx;
    sig->fy       = fy;
    sig->s        = s_along;
    sig->seg      = (int32_t)m->railnet.n_segs - 1;
    sig->dir      = dir;
    sig->absolute = absolute;
    return 0;
}

/*  A level crossing's protection on one approach (spec 3.15): the
 *  mast at the driver's right with the crossbuck, the "2 TRACKS"
 *  plaque, a pair of flashers below it, and the gate on its own
 *  counterweighted post beside the mast, its striped arm down across
 *  the approach lane with the flashers lit while a train stands within
 *  three tiles of the crossing, raised otherwise; and a second-train
 *  sign facing each sidewalk.  (fx, fy) is the direction of travel on
 *  this approach; the face turns to meet it. */
static int put_gate(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy, int along_u)
{
    /*  The standing parts of a crossing's protection on one approach
     *  (spec 3.15, 6.1): the mast at the driver's right with the
     *  crossbuck, the "2 TRACKS" plaque, the flasher bar and the base
     *  junction box, and the gate mechanism's case beside it on the
     *  road side.  The lamps and the arm are the traffic's, rebuilt each
     *  frame with the trains' positions.  (fx, fy) is the direction of
     *  travel on this approach. */
    float bx = -fx, by = -fy;
    float wu = -by, wv = bx; /* across the face: to the driver's left */
    (void)bx;
    if (put_crossbuck(m, c, mask_bit, order, x, y, along_u) != 0)
        return -1;
    if (put_box(m, c, mask_bit, order, x, y, 0.03f, 0.03f, 0.0f, 0.075f, MAT_PROP, 0.0f) != 0)
        return -1;
    if (put_box(m, c, mask_bit, order, x, y, along_u ? 0.015f : 0.046f, along_u ? 0.046f : 0.015f, 0.25f, 0.31f, MAT_LAMP, 0.0f) != 0)
        return -1;
    if (put_box(m, c, mask_bit, order, x, y, along_u ? 0.015f : 0.10f, along_u ? 0.10f : 0.015f, 0.285f, 0.30f, MAT_PROP, 0.0f) != 0)
        return -1;
    return put_box(m, c, mask_bit, order, x + wu * 0.045f, y + wv * 0.045f, 0.03f, 0.03f, 0.10f, 0.16f, MAT_PROP, 0.0f);
}

/*  A second-train sign: a post with a yellow diamond facing the
 *  sidewalk it stands at the end of, one at each of the crossing's
 *  four sidewalk corners (spec 3.15, tracks >= 2). */
static int put_second_train_sign(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy)
{
    float g = surface_at_world(c, mask_bit, x, y);
    if (put_box(m, c, mask_bit, order, x, y, 0.008f, 0.008f, 0.0f, 0.33f, MAT_PROP, 0.0f) != 0)
        return -1;
    return put_lamp_face(m, order, x, y, g, 0.30f, fx, fy, 0.02f, 0.0f, 10.0f);
}

/* ---- the segment pipeline (the road spec, part 3.10) ------------------ */

/*  A network is walked as segments between its nodes, the junction and
 *  end tiles; every other tile has two links and lies on one segment.
 *  A segment's centreline is the polyline through its tiles' centres,
 *  from the side of the junction box it leaves to the side it reaches.
 *  The polyline is straightened -- a staircase of corners becomes one
 *  straight line, at 45 degrees or 2:1 -- and every remaining bend is
 *  filleted with an arc, the quarter circle of a lone corner or the
 *  gentler sweep where a diagonal meets the grid.  Then one strip is
 *  lofted along the whole path by arc length, its width a function of
 *  the direction at each sample, its dashes and crosswalks placed by
 *  the distance from the junction.  There is no join inside a segment
 *  to get wrong, and a segment meets its junction box square on. */

typedef struct
{
    float x, y;
} V2;

typedef struct
{
    int   arc; /* 0 a straight from a to b; 1 an arc about c        */
    V2    a, b, c;
    float r, t0, t1; /* the arc's radius and its angles, t0 to t1, signed */
    float len;
} Piece;

#define MAX_PIECES 1024
#define MAX_PTS    512

static float v2len(V2 v)
{
    return sqrtf(v.x * v.x + v.y * v.y);
}

/*  Fillet the polyline into straights and arcs.  At each bend the arc's
 *  radius is the class's, clamped so the tangent points stay within the
 *  adjacent edges (spec 3.10, step 3). */
static int fillet(const V2 *q, int n, float rmax, Piece *out, int *count)
{
    int i, np = 0;
    V2  cur = q[0];
    for (i = 1; i < n - 1; ++i)
    {
        V2    u_in  = {q[i].x - q[i - 1].x, q[i].y - q[i - 1].y};
        V2    u_out = {q[i + 1].x - q[i].x, q[i + 1].y - q[i].y};
        float lin = v2len(u_in), lout = v2len(u_out);
        float cross, dot, theta, d, R, cos_h, sin_h;
        V2    t1, t2, cen, n1;
        if (lin < 1e-6f || lout < 1e-6f)
            continue;
        u_in.x /= lin;
        u_in.y /= lin;
        u_out.x /= lout;
        u_out.y /= lout;
        cross = u_in.x * u_out.y - u_in.y * u_out.x;
        dot   = u_in.x * u_out.x + u_in.y * u_out.y;
        if (dot > 0.9999f)
            continue;                             /* straight on: no vertex */
        theta = acosf(dot < -1.0f ? -1.0f : dot); /* the turn, 0..pi */
        /* the tangent points at distance d from the vertex; R clamped to the edges */
        R = rmax;
        {
            float lim   = 0.5f * (lin < lout ? lin : lout);
            float tan_h = tanf(0.5f * theta);
            if (R * tan_h > lim)
                R = lim / tan_h;
        }
        if (R < 0.04f)
            R = 0.04f;
        d  = R * tanf(0.5f * theta);
        t1 = (V2){q[i].x - u_in.x * d, q[i].y - u_in.y * d};
        t2 = (V2){q[i].x + u_out.x * d, q[i].y + u_out.y * d};
        /* the arc's centre: off t1 along the inward normal of u_in, toward the turn */
        n1    = cross > 0.0f ? (V2){-u_in.y, u_in.x} : (V2){u_in.y, -u_in.x};
        cen   = (V2){t1.x + n1.x * R, t1.y + n1.y * R};
        cos_h = cosf(0.5f * theta);
        sin_h = sinf(0.5f * theta);
        (void)cos_h;
        (void)sin_h;
        if (np + 2 > MAX_PIECES)
            return -1;
        if (v2len((V2){t1.x - cur.x, t1.y - cur.y}) > 1e-4f)
        {
            out[np].arc = 0;
            out[np].a   = cur;
            out[np].b   = t1;
            out[np].len = v2len((V2){t1.x - cur.x, t1.y - cur.y});
            ++np;
        }
        out[np].arc = 1;
        out[np].c   = cen;
        out[np].r   = R;
        out[np].t0  = atan2f(t1.y - cen.y, t1.x - cen.x);
        out[np].t1  = out[np].t0 + (cross > 0.0f ? theta : -theta);
        out[np].len = R * theta;
        ++np;
        cur = t2;
    }
    if (np + 1 > MAX_PIECES)
        return -1;
    if (v2len((V2){q[n - 1].x - cur.x, q[n - 1].y - cur.y}) > 1e-4f)
    {
        out[np].arc = 0;
        out[np].a   = cur;
        out[np].b   = q[n - 1];
        out[np].len = v2len((V2){q[n - 1].x - cur.x, q[n - 1].y - cur.y});
        ++np;
    }
    *count = np;
    return 0;
}

/*  The point and unit direction at distance `t` along a piece. */
static void piece_at(const Piece *p, float t, V2 *pos, V2 *dir)
{
    if (!p->arc)
    {
        float f = p->len > 1e-6f ? t / p->len : 0.0f;
        pos->x  = p->a.x + (p->b.x - p->a.x) * f;
        pos->y  = p->a.y + (p->b.y - p->a.y) * f;
        dir->x  = (p->b.x - p->a.x) / (p->len > 1e-6f ? p->len : 1.0f);
        dir->y  = (p->b.y - p->a.y) / (p->len > 1e-6f ? p->len : 1.0f);
    }
    else
    {
        float f  = p->len > 1e-6f ? t / p->len : 0.0f;
        float th = p->t0 + (p->t1 - p->t0) * f;
        float s  = p->t1 > p->t0 ? 1.0f : -1.0f;
        pos->x   = p->c.x + p->r * cosf(th);
        pos->y   = p->c.y + p->r * sinf(th);
        dir->x   = -sinf(th) * s;
        dir->y   = cosf(th) * s;
    }
}

typedef struct
{
    V2    pos, dir;
    float s, z; /* z: the section's height, the highest ground it spans */
    float xd;   /* the distance along to the nearest level crossing on the segment */
} Sample;

/*  The height a cross-section sits at: the highest of the ground under
 *  its centre and its two edges, so the terrain never cuts through the
 *  road (the user: "raise roadways if they're going to cut into the
 *  terrain"); where that lifts it, the skirts below make the fill. */
static float section_height(const RCity *c, uint8_t mask_bit, V2 pos, V2 dir, float h)
{
    float zc = surface_at_world(c, mask_bit, pos.x, pos.y);
    float zl = surface_at_world(c, mask_bit, pos.x + dir.y * h, pos.y - dir.x * h);
    float zr = surface_at_world(c, mask_bit, pos.x - dir.y * h, pos.y + dir.x * h);
    float z  = zc > zl ? zc : zl;
    return z > zr ? z : zr;
}

/*  Loft one strip along the pieces: cross-sections by arc length, their
 *  width the class's scaled by the direction (the snap view's
 *  compensation), one quad per pair, each with the painter's order of
 *  the tile under it.  `zeb0`/`zeb1`: the length of the crosswalk band
 *  at either end, 0 for none. */
/*  Record a road segment's stations in the mesh's network, for the
 *  traffic.  The lane centres by class, tiles from the centreline: a
 *  road's one lane each way at half the carriageway, an avenue's outer
 *  and inner lanes, a boulevard's outer and inner between median and
 *  curb, where the material draws its lines. */
static int net_record(RRoadNet *net, const Sample *smp, int ns, float total, int cls, int rail)
{
    RNetSeg *sg;
    int      i;
    if (net->n_pts + (uint32_t)ns > net->cap_pts)
    {
        uint32_t nc = net->cap_pts ? net->cap_pts * 2u : 4096u;
        RNetPt  *np;
        while (nc < net->n_pts + (uint32_t)ns)
            nc *= 2u;
        np = (RNetPt *)realloc(net->pts, nc * sizeof *np);
        if (!np)
            return -1;
        net->pts     = np;
        net->cap_pts = nc;
    }
    if (net->n_segs + 1u > net->cap_segs)
    {
        uint32_t nc = net->cap_segs ? net->cap_segs * 2u : 256u;
        RNetSeg *np = (RNetSeg *)realloc(net->segs, nc * sizeof *np);
        if (!np)
            return -1;
        net->segs     = np;
        net->cap_segs = nc;
    }
    sg        = &net->segs[net->n_segs++];
    sg->first = net->n_pts;
    sg->count = (uint32_t)ns;
    sg->total = total;
    sg->cls   = cls;
    memcpy(sg->node, s_seg_node, sizeof sg->node);
    sg->kind[0]  = s_seg_kind[0];
    sg->kind[1]  = s_seg_kind[1];
    sg->ctrl[0]  = s_seg_ctrl[0];
    sg->ctrl[1]  = s_seg_ctrl[1];
    sg->lane_out = cls == 0 ? 0.142f : cls == 1 ? 0.217f
                                                : 0.225f;
    sg->lane_in  = cls == 0 ? 0.142f : cls == 1 ? 0.082f
                                                : 0.115f;
    if (rail)
        sg->lane_out = sg->lane_in = 0.133f; /* a rail: the track to the right of travel */
    for (i = 0; i < ns; ++i)
    {
        RNetPt *q = &net->pts[net->n_pts++];
        q->x      = smp[i].pos.x;
        q->y      = smp[i].pos.y;
        q->z      = smp[i].z;
        q->dx     = smp[i].dir.x;
        q->dy     = smp[i].dir.y;
        q->s      = smp[i].s;
    }
    return 0;
}

/*  Whether a station's tile is a level crossing or within a tile of one. */
static int near_crossing(const RCity *c, V2 pos)
{
    int32_t col = (int32_t)floorf(pos.x), row = (int32_t)floorf(pos.y), dc, dr;
    for (dr = -1; dr <= 1; ++dr)
        for (dc = -1; dc <= 1; ++dc)
        {
            int32_t cc = col + dc, rr = row + dr;
            uint8_t b;
            if (cc < 0 || rr < 0 || cc >= R_MAP || rr >= R_MAP)
                continue;
            b = c->xbld[rr * R_MAP + cc];
            if (b >= 0x45u && b <= 0x48u)
                return 1;
        }
    return 0;
}

static int loft(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, Family f, const Piece *pc, int np, float total, float zeb0, float zeb1, int pin0, int pin1)
{
    static Sample smp[8192];
    static float  zraw[8192];
    float         hw  = f == F_ROAD ? ROAD_W * 0.5f : RAIL_W * 0.5f;
    float         mat = f == F_ROAD ? MAT_ROAD : MAT_RAIL;
    int           k, ns = 0, i;
    float         s = 0.0f;
    /* the samples */
    for (k = 0; k < np; ++k)
    {
        const Piece *p  = &pc[k];
        int          nd = p->arc ? (int)ceilf(p->len / 0.08f) : (int)ceilf(p->len / 0.125f);
        if (nd < 1)
            nd = 1;
        for (i = (ns ? 1 : 0); i <= nd; ++i)
        {
            float t = p->len * (float)i / (float)nd;
            V2    pos, dir;
            if (ns >= 8190)
                break;
            piece_at(p, t, &pos, &dir);
            /*  A station on every tile edge the centreline crosses, found
             *  by bisection, so a crease in the ground -- a crest at the
             *  edge between a rising and a falling tile -- is a station
             *  and never a chord's underside. */
            if (ns > 0 && i > 0)
            {
                /*  The centreline and both edges of the band each cross
                 *  the tile edges at their own point; a station at every
                 *  crossing, in order along the piece, so a wall the band
                 *  crosses obliquely is met at a station on the side it
                 *  first reaches. */
                float t0 = p->len * (float)(i - 1) / (float)nd, t1 = t;
                float tc[9];
                int   ntc = 0, side, pass, q;
                for (side = -1; side <= 1; ++side)
                    for (pass = 0; pass < 2; ++pass)
                    {
                        int   ax  = pass == 0;
                        float off = (float)side * hw;
                        V2    p0, d0, q0, q1;
                        piece_at(p, t0, &p0, &d0);
                        q0 = (V2){p0.x + d0.y * off, p0.y - d0.x * off};
                        q1 = (V2){pos.x + dir.y * off, pos.y - dir.x * off};
                        {
                            float a0 = ax ? q0.x : q0.y, a1 = ax ? q1.x : q1.y;
                            if (floorf(a0) != floorf(a1) && ntc < 9)
                            {
                                float lo = t0, hi = t1, edge = floorf(a1 > a0 ? a1 : a0);
                                V2    pb, db, qb;
                                int   it;
                                for (it = 0; it < 12; ++it)
                                {
                                    float mid = 0.5f * (lo + hi), v;
                                    piece_at(p, mid, &pb, &db);
                                    qb = (V2){pb.x + db.y * off, pb.y - db.x * off};
                                    v  = ax ? qb.x : qb.y;
                                    if ((v < edge) == (a0 < edge))
                                        lo = mid;
                                    else
                                        hi = mid;
                                }
                                if (hi - t0 > 1e-3f && t1 - hi > 1e-3f)
                                    tc[ntc++] = hi;
                            }
                        }
                    }
                /* in order along the piece, each once */
                for (q = 1; q < ntc; ++q)
                {
                    float v = tc[q];
                    int   r = q;
                    while (r > 0 && tc[r - 1] > v)
                    {
                        tc[r] = tc[r - 1];
                        --r;
                    }
                    tc[r] = v;
                }
                for (q = 0; q < ntc && ns < 8190; ++q)
                {
                    V2    pb, db, pm, pp;
                    float zm, zp;
                    if (q > 0 && tc[q] - tc[q - 1] < 2e-3f)
                        continue;
                    piece_at(p, tc[q], &pb, &db);
                    /* both tiles meet here, maybe at a small wall: the higher */
                    pm          = (V2){pb.x - db.x * 0.004f, pb.y - db.y * 0.004f};
                    pp          = (V2){pb.x + db.x * 0.004f, pb.y + db.y * 0.004f};
                    zm          = section_height(c, mask_bit, pm, db, hw);
                    zp          = section_height(c, mask_bit, pp, db, hw);
                    smp[ns].pos = pb;
                    smp[ns].dir = db;
                    smp[ns].s   = s + tc[q];
                    zraw[ns]    = zm > zp ? zm : zp;
                    ++ns;
                }
            }
            smp[ns].pos = pos;
            smp[ns].dir = dir;
            smp[ns].s   = s + t;
            zraw[ns]    = section_height(c, mask_bit, pos, dir, hw);
            ++ns;
        }
        s += p->len;
    }
    /*  The profile: at each station the highest ground the cross-section
     *  spans, centre and both edges, and a hair of clearance, so the
     *  terrain never cuts through the road, on a straightened diagonal
     *  across the corner tiles beside the road tiles above all; a road
     *  along a slope lies on it, a road across one rides on its uphill
     *  edge with a skirt below the other (the user: "raise roadways if
     *  they're going to cut into the terrain"). */
    for (i = 0; i < ns; ++i)
        smp[i].z = zraw[i] + 0.03f;
    /*  The grade: no steeper than the original's slope pieces, one level
     *  per tile of road, so a band that climbs across a slope or steps
     *  onto a higher tile does so on a ramp with a skirt rather than a
     *  near-vertical face the light turns black.  Stations are only ever
     *  raised, so the band stays above the ground; an end that meets a
     *  junction box is pinned to it, a free end may rise with the band
     *  (pinned, a dead end on a slope fell a level in one station). */
    for (i = 1; i < ns - (pin1 ? 1 : 0); ++i)
    {
        float lim = smp[i - 1].z - ROAD_GRADE * (smp[i].s - smp[i - 1].s);
        if (smp[i].z < lim)
            smp[i].z = lim;
    }
    for (i = ns - 2; i >= (pin0 ? 1 : 0); --i)
    {
        float lim = smp[i + 1].z - ROAD_GRADE * (smp[i + 1].s - smp[i].s);
        if (smp[i].z < lim)
            smp[i].z = lim;
    }
    if (f == F_ROAD && ns >= 2 && net_record(&m->net, smp, ns, total, (int)(s_seg_class + 0.5f), 0) != 0)
        return -1;
    if (f == F_RAIL && ns >= 2 && net_record(&m->railnet, smp, ns, total, 0, 1) != 0)
        return -1;
    /*  A rail's signalling (spec 5.6), right-hand running: the track to
     *  the right of the walk carries traffic forward and its signals
     *  stand on its outer side facing back; the other track's face
     *  forward on the other side.  A block signal every ten tiles,
     *  green; an absolute one, red, a tile before a junction; a whistle
     *  post two tiles before a level crossing; none on or beside a
     *  crossing. */
    if (f == F_RAIL && ns > 2)
    {
        float sig;
        int   side;
        for (side = 0; side < 2; ++side)
        {
            float sgn = side ? -1.0f : 1.0f; /* the right track (forward) then the left (back) */
            for (sig = 5.0f; sig < total - 0.5f; sig += 10.0f)
            {
                int j = 1;
                while (j < ns - 1 && smp[j].s < sig)
                    ++j;
                if (near_crossing(c, smp[j].pos))
                    continue;
                {
                    float   rx = -smp[j].dir.y * sgn * 0.33f, ry = smp[j].dir.x * sgn * 0.33f;
                    int32_t tc = (int32_t)floorf(smp[j].pos.x + rx), tr = (int32_t)floorf(smp[j].pos.y + ry);
                    if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                        continue;
                    if (put_rail_signal(m, c, mask_bit, tile_order(c, tc, tr, mask_bit) + 0.3f, smp[j].pos.x + rx, smp[j].pos.y + ry, -smp[j].dir.x * sgn, -smp[j].dir.y * sgn, 0, smp[j].s, (int)sgn) != 0)
                        return -1;
                }
            }
            /* the absolute signal before the junction this track runs toward */
            if ((side == 0 && pin1) || (side == 1 && pin0))
            {
                float at = side == 0 ? total - 1.0f : 1.0f;
                int   j  = 1;
                if (total > 2.5f)
                {
                    while (j < ns - 1 && smp[j].s < at)
                        ++j;
                    if (!near_crossing(c, smp[j].pos))
                    {
                        float   rx = -smp[j].dir.y * sgn * 0.33f, ry = smp[j].dir.x * sgn * 0.33f;
                        int32_t tc = (int32_t)floorf(smp[j].pos.x + rx), tr = (int32_t)floorf(smp[j].pos.y + ry);
                        if (tc >= 0 && tr >= 0 && tc < R_MAP && tr < R_MAP &&
                            put_rail_signal(m, c, mask_bit, tile_order(c, tc, tr, mask_bit) + 0.3f, smp[j].pos.x + rx, smp[j].pos.y + ry, -smp[j].dir.x * sgn, -smp[j].dir.y * sgn, 1, smp[j].s, (int)sgn) != 0)
                            return -1;
                    }
                }
            }
        }
        /* whistle posts: two tiles before each crossing tile, each direction, on the right */
        for (i = 1; i < ns; ++i)
        {
            int32_t col = (int32_t)floorf(smp[i].pos.x), row = (int32_t)floorf(smp[i].pos.y), pc2 = (int32_t)floorf(smp[i - 1].pos.x), pr2 = (int32_t)floorf(smp[i - 1].pos.y);
            uint8_t b;
            if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP || (col == pc2 && row == pr2))
                continue;
            b = c->xbld[row * R_MAP + col];
            if (!(b == 0x45u || b == 0x46u))
                continue;
            for (side = 0; side < 2; ++side)
            {
                float   sgn = side ? -1.0f : 1.0f;
                float   at  = smp[i].s - sgn * 2.0f;
                int     j   = 1;
                float   rx, ry;
                int32_t tc, tr;
                if (at < 0.3f || at > total - 0.3f)
                    continue;
                while (j < ns - 1 && smp[j].s < at)
                    ++j;
                rx = -smp[j].dir.y * sgn * 0.30f;
                ry = smp[j].dir.x * sgn * 0.30f;
                tc = (int32_t)floorf(smp[j].pos.x + rx);
                tr = (int32_t)floorf(smp[j].pos.y + ry);
                if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                    continue;
                if (put_box(m, c, mask_bit, tile_order(c, tc, tr, mask_bit) + 0.3f, smp[j].pos.x + rx, smp[j].pos.y + ry, 0.012f, 0.012f, 0.0f, 0.19f, MAT_LAMP, 0.0f) != 0)
                    return -1;
            }
        }
    }
    /*  The road's approach to a level crossing (spec 3.15): from the RXR
     *  stencil to the stop line the lines are solid; the quads within a
     *  tile and a half of a crossing tile's centre carry the approach
     *  material with the distance to the crossing along. */
    {
        static float xs[64];
        int          nx = 0;
        if (f == F_ROAD)
            for (i = 0; i < ns && nx < 64; ++i)
            {
                int32_t col = (int32_t)floorf(smp[i].pos.x), row = (int32_t)floorf(smp[i].pos.y);
                uint8_t b;
                if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
                    continue;
                b = c->xbld[row * R_MAP + col];
                if ((b == 0x45u || b == 0x46u) && fabsf(smp[i].pos.x - (float)col - 0.5f) < 0.07f && fabsf(smp[i].pos.y - (float)row - 0.5f) < 0.07f)
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
    /* the quads and their skirts */
    for (i = 1; i < ns; ++i)
    {
        const Sample *pv = &smp[i - 1], *cu = &smp[i];
        float         ha    = hw * width_factor(pv->dir.x, pv->dir.y, comp);
        float         hb    = hw * width_factor(cu->dir.x, cu->dir.y, comp);
        float         a0[2] = {pv->pos.x + pv->dir.y * ha, pv->pos.y - pv->dir.x * ha};
        float         a1[2] = {pv->pos.x - pv->dir.y * ha, pv->pos.y + pv->dir.x * ha};
        float         b0[2] = {cu->pos.x + cu->dir.y * hb, cu->pos.y - cu->dir.x * hb};
        float         b1[2] = {cu->pos.x - cu->dir.y * hb, cu->pos.y + cu->dir.x * hb};
        float         mx = 0.5f * (pv->pos.x + cu->pos.x), my = 0.5f * (pv->pos.y + cu->pos.y);
        int32_t       tc = (int32_t)floorf(mx), tr = (int32_t)floorf(my);
        float         order, ma = mat, al_a = pv->s, al_b = cu->s;
        int           side;
        if (tc < 0)
            tc = 0;
        if (tr < 0)
            tr = 0;
        if (tc >= R_MAP)
            tc = R_MAP - 1;
        if (tr >= R_MAP)
            tr = R_MAP - 1;
        order        = tile_order(c, tc, tr, mask_bit);
        s_road_class = f != F_ROAD ? 0.0f : s_seg_class >= 0.0f ? s_seg_class
                                                                : road_class(c, tc, tr);
        if (f == F_ROAD && pv->xd > 0.45f && 0.5f * (pv->xd + cu->xd) < 1.55f)
        {
            ma   = MAT_XAPPROACH; /* along: the distance to the crossing */
            al_a = pv->xd;
            al_b = cu->xd;
        }
        else if (f == F_ROAD && zeb0 > 0.0f && cu->s <= zeb0 + 1e-4f)
            ma = MAT_ZEBRA; /* the band's along runs from the box outward */
        else if (f == F_ROAD && zeb1 > 0.0f && pv->s >= total - zeb1 - 1e-4f)
        {
            ma   = MAT_ZEBRA;
            al_a = total - pv->s;
            al_b = total - cu->s;
        }
        if (strip_quad_z(m, c, mask_bit, order, a0, a1, b0, b1, pv->z, cu->z, -1.0f, 1.0f, al_a, al_b, ma) != 0)
            return -1;
        /* the skirts: where the road stands above the ground at an edge, blocks down to it */
        for (side = 0; side < 2; ++side)
        {
            static const float blocks[3] = {0.0f, 0.0f, MAT_SKIRT};
            const float       *ea = side ? a1 : a0, *eb = side ? b1 : b0;
            float              ga    = surface_at_world(c, mask_bit, ea[0], ea[1]);
            float              gb    = surface_at_world(c, mask_bit, eb[0], eb[1]);
            float              t0[3] = {ea[0], ea[1], pv->z}, t1[3] = {eb[0], eb[1], cu->z};
            float              q0[3] = {ea[0], ea[1], ga}, q1[3] = {eb[0], eb[1], gb};
            float              nrm[3];
            if (pv->z <= ga + 0.035f && cu->z <= gb + 0.035f)
                continue;
            if (q0[2] > t0[2])
                q0[2] = t0[2];
            if (q1[2] > t1[2])
                q1[2] = t1[2];
            nrm[0] = side ? -cu->dir.y : cu->dir.y;
            nrm[1] = side ? cu->dir.x : -cu->dir.x;
            nrm[2] = 0.0f;
            if (put_wall(m, t0, t1, q0, q1, nrm, order, blocks) != 0)
                return -1;
        }
    }
    return 0;
}

static float v2cross(V2 a, V2 b)
{
    return a.x * b.y - a.y * b.x;
}

/*  The point where the line through a in direction da meets the line
 *  through b in direction db; 0 when they are parallel. */
static int line_meet(V2 a, V2 da, V2 b, V2 db, V2 *out)
{
    float den = v2cross(da, db);
    float t;
    if (fabsf(den) < 1e-5f)
        return 0;
    t      = v2cross((V2){b.x - a.x, b.y - a.y}, db) / den;
    out->x = a.x + da.x * t;
    out->y = a.y + da.y * t;
    return 1;
}

/*  Straighten a polyline (spec 3.10, step 4): collinear points go; a
 *  staircase -- a run of two or more bends turning left, right, left
 *  with grid-aligned legs of at most three tiles between them -- becomes
 *  one line in the run's net direction through its corners' centroid,
 *  joined to the legs before and after where it crosses them.  The rule
 *  is explicit rather than a chord tolerance: a staircase's corners
 *  alternate between two parallel lines a tile-diagonal apart, so any
 *  chord leaves half of them standing off it.  Writes to `out`. */
static int straighten(const V2 *p, int n, V2 *out)
{
    static V2 w[MAX_PTS];
    int       m = 0, i, changed, passes = 0;
    for (i = 0; i < n; ++i)
    {
        if (i > 0 && i < n - 1)
        {
            V2 a = {p[i].x - w[m - 1].x, p[i].y - w[m - 1].y};
            V2 b = {p[i + 1].x - p[i].x, p[i + 1].y - p[i].y};
            if (fabsf(v2cross(a, b)) < 1e-4f && a.x * b.x + a.y * b.y > 0.0f)
                continue;
        }
        w[m++] = p[i];
    }
    do
    {
        changed = 0;
        for (i = 1; i < m - 1 && !changed; ++i)
        {
            int   j = i, k, grid = 1;
            float sign_prev = 0.0f;
            for (k = i; k < m - 1; ++k)
            {
                V2    a    = {w[k].x - w[k - 1].x, w[k].y - w[k - 1].y};
                V2    b    = {w[k + 1].x - w[k].x, w[k + 1].y - w[k].y};
                float cr   = v2cross(a, b);
                float sign = cr > 1e-4f ? 1.0f : cr < -1e-4f ? -1.0f
                                                             : 0.0f;
                if (sign == 0.0f)
                    break;
                if (k > i && (sign == sign_prev || v2len(a) > 3.05f))
                    break;
                /*  The legs inside a staircase repeat every two: a leg
                 *  unlike the one two before it ends the run there, so a
                 *  stair that meets a straight through a crossing and
                 *  goes on as a stair is two stairs and a straight, not
                 *  one irregular run left to the fillet (Barcelona's rail
                 *  at column 105, rows 4 to 11). */
                if (k >= i + 3)
                {
                    V2 a2 = {w[k - 2].x - w[k - 3].x, w[k - 2].y - w[k - 3].y};
                    if (fabsf(v2len(a) - v2len(a2)) > 0.1f)
                        break;
                }
                if (k > i && fabsf(a.x) > 1e-3f && fabsf(a.y) > 1e-3f)
                    grid = 0; /* a diagonal leg inside the run: already straightened */
                sign_prev = sign;
                j         = k;
            }
            /*  Two full periods at least (spec 3.10, step 4): a single
             *  N, E pair is a corner and N, E, N a jog, both filleted. */
            if (j - i + 1 >= 3 && grid)
            {
                /*  The staircase's corners lie on two parallel lines, the
                 *  left-turning corners on one and the right-turning on
                 *  the other; the centreline is their midline, exactly.
                 *  The direction is that between corners two apart, which
                 *  share a line, not the chord from first to last, which
                 *  leans across by half a step when the run starts and
                 *  ends on different lines.  A run whose corners do not
                 *  sit on two lines -- legs of one, three, one, two -- is
                 *  no staircase and keeps its corners for the fillet. */
                V2    cen, dir = {w[i + 2].x - w[i].x, w[i + 2].y - w[i].y};
                float L = v2len(dir);
                V2    qin, qout, din = {w[i].x - w[i - 1].x, w[i].y - w[i - 1].y};
                V2    dout = {w[j + 1].x - w[j].x, w[j + 1].y - w[j].y};
                int   t, na = 0, nb = 0, regular = 1;
                float sa = 0.0f, sb = 0.0f, mid, ma, mb;
                if (L < 1e-4f)
                    continue;
                dir.x /= L;
                dir.y /= L;
                for (k = i; k <= j; ++k)
                {
                    V2    a   = {w[k].x - w[k - 1].x, w[k].y - w[k - 1].y};
                    V2    b   = {w[k + 1].x - w[k].x, w[k + 1].y - w[k].y};
                    float off = v2cross(dir, (V2){w[k].x - w[i].x, w[k].y - w[i].y});
                    if (v2cross(a, b) > 0.0f)
                    {
                        sa += off;
                        ++na;
                    }
                    else
                    {
                        sb += off;
                        ++nb;
                    }
                }
                if (!na || !nb)
                    continue;
                {
                    /*  The legs between the corners alternate: a riser of
                     *  one tile and a run of one to three, every riser and
                     *  every run alike (1:1, 2:1, 3:1).  Three-tile legs
                     *  both ways are a zig-zag of corners, not a staircase,
                     *  and its midline would leave the road tiles. */
                    float l0 = -1.0f, l1 = -1.0f;
                    int   ok = 1;
                    for (k = i; k < j; ++k)
                    {
                        float  len = v2len((V2){w[k + 1].x - w[k].x, w[k + 1].y - w[k].y});
                        float *ref = ((k - i) & 1) ? &l1 : &l0;
                        /* whole tiles: a leg left by an earlier join is no step */
                        if (fabsf(len - rintf(len)) > 0.1f || len < 0.9f)
                            ok = 0;
                        if (*ref < 0.0f)
                            *ref = len;
                        else if (fabsf(*ref - len) > 0.1f)
                            ok = 0;
                    }
                    if (!ok || (l0 > 1.05f && l1 > 1.05f))
                        continue;
                }
                ma = sa / (float)na;
                mb = sb / (float)nb;
                for (k = i; k <= j; ++k)
                {
                    V2    a   = {w[k].x - w[k - 1].x, w[k].y - w[k - 1].y};
                    V2    b   = {w[k + 1].x - w[k].x, w[k + 1].y - w[k].y};
                    float off = v2cross(dir, (V2){w[k].x - w[i].x, w[k].y - w[i].y});
                    if (fabsf(off - (v2cross(a, b) > 0.0f ? ma : mb)) > 0.2f)
                        regular = 0;
                }
                if (!regular)
                    continue;
                mid   = 0.5f * (ma + mb);
                cen.x = w[i].x - dir.y * mid;
                cen.y = w[i].y + dir.x * mid;
                if (!line_meet(w[i - 1], din, cen, dir, &qin) || !line_meet(w[j], dout, cen, dir, &qout))
                    continue;
                /* a leg nearly parallel to the midline meets it far away: no join there */
                if (v2len((V2){qin.x - w[i].x, qin.y - w[i].y}) > 3.0f || v2len((V2){qout.x - w[j].x, qout.y - w[j].y}) > 3.0f)
                    continue;
                /*  The join must cut the leg it joins between that leg's
                 *  ends: past an end the polyline would double back on
                 *  itself.  Toronto's rail at column 92, row 113: a stair
                 *  of one three-tile step and a one-tile jog, taken as a
                 *  3:1 staircase, had its midline meet the one-tile leg
                 *  after it half a tile past its corner, a hairpin drawn
                 *  in the middle of a diagonal.  Refused, the corners
                 *  stand, and the 1:1 staircase they begin is found from
                 *  the next corner instead. */
                {
                    float tin  = ((qin.x - w[i - 1].x) * din.x + (qin.y - w[i - 1].y) * din.y) / (din.x * din.x + din.y * din.y);
                    float tout = ((qout.x - w[j].x) * dout.x + (qout.y - w[j].y) * dout.y) / (dout.x * dout.x + dout.y * dout.y);
                    if (tin < -1e-3f || tin > 1.001f || tout < -1e-3f || tout > 1.001f)
                        continue;
                }
                w[i]     = qin;
                w[i + 1] = qout;
                for (t = j + 1; t < m; ++t)
                    w[i + 2 + (t - j - 1)] = w[t];
                m       = i + 2 + (m - j - 1);
                changed = 1;
            }
        }
    } while (changed && ++passes < 64);
    /* a join that lands on the corner it replaces leaves the point twice; once is enough */
    {
        int o = 0;
        for (i = 0; i < m; ++i)
            if (o == 0 || fabsf(w[i].x - out[o - 1].x) > 1e-3f || fabsf(w[i].y - out[o - 1].y) > 1e-3f)
                out[o++] = w[i];
        m = o;
    }
    return m;
}

/*  A lone piece no neighbour joins: a band across its own tile along the
 *  axis its art links, both ends capped, as the original's lone sprite. */
static int build_island(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, int32_t col, int32_t row)
{
    Piece pc;
    int   links = tile_links(c, l, col, row, f), ns;
    float cx = (float)col + 0.5f, cy = (float)row + 0.5f;
    if (!links)
        return 0;
    ns               = (links & (L_N | L_S)) ? 1 : 0;
    pc.arc           = 0;
    pc.a             = (V2){ns ? cx : cx - 0.49f, ns ? cy - 0.49f : cy}; /* a hair inside the tile, so the end stations read its surface */
    pc.b             = (V2){ns ? cx : cx + 0.49f, ns ? cy + 0.49f : cy};
    s_seg_node[0][0] = s_seg_node[1][0] = col;
    s_seg_node[0][1] = s_seg_node[1][1] = row;
    s_seg_kind[0] = s_seg_kind[1] = 0;
    s_seg_ctrl[0] = s_seg_ctrl[1] = 0;
    pc.c                          = pc.a;
    pc.len                        = 0.98f;
    pc.r                          = 0.0f;
    pc.t0 = pc.t1 = 0.0f;
    return loft(m, c, mask_bit, comp, f, &pc, 1, 0.98f, 0.0f, 0.0f, 0, 0);
}

/*  A node of a family's network: a junction (three or four links), an
 *  end (one), or nothing. */
static int node_kind(const RCity *c, const RAtlasLevel *l, Family f, int32_t col, int32_t row)
{
    /*  A junction piece is a node whatever its neighbours return: its
     *  box is drawn and its dangling arms end at the box (Barcelona's
     *  crossing at column 101, row 0, two arms into buildings, was
     *  walked through as a bend to the map's edge).  Otherwise one
     *  returned link makes an end. */
    int n = link_count(eff_links(c, l, col, row, f));
    if (link_count(tile_links(c, l, col, row, f)) >= 3)
        return 2;
    return n == 1 ? 1 : 0;
}

/*  Room for a cul-de-sac bulb: the end tile's neighbours that are not
 *  road are open land, nothing built on them (spec 3.10, step 11). */
static int cul_de_sac_room(const RCity *c, int32_t col, int32_t row)
{
    int e;
    for (e = 0; e < 4; ++e)
    {
        int32_t nc = col + (int32_t)ROAD_DU[e], nr = row + (int32_t)ROAD_DV[e];
        uint8_t b;
        if (nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
            continue;
        b = c->xbld[nr * R_MAP + nc];
        if (b > 0x0Du && !(b >= 0x1Du && b <= 0x2Bu) && !(b >= 0x43u && b <= 0x46u))
            return 0;
    }
    return 1;
}

/*  Where a segment ends on an end tile, and how.  The tile's art links
 *  that no neighbour returns point at the dead side; what stands there
 *  decides: against a building, a bridge, a tunnel end or a highway
 *  the road runs square to the tile's edge, as the original draws the
 *  straight piece whole and the carrier's sprite goes on from there;
 *  against open land the segment ends at the tile's centre and a road
 *  gets its turning head.  Returns 1 for a square end. */
static int end_point(const RCity *c, const RAtlasLevel *l, Family f, int32_t col, int32_t row, V2 *pt)
{
    int dead = tile_links(c, l, col, row, f) & ~eff_links(c, l, col, row, f), e;
    *pt      = (V2){(float)col + 0.5f, (float)row + 0.5f};
    for (e = 0; e < 4; ++e)
    {
        int32_t nc, nr;
        uint8_t b;
        if (!(dead & (1 << e)))
            continue;
        nc = col + (int32_t)ROAD_DU[e];
        nr = row + (int32_t)ROAD_DV[e];
        if (nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
            continue;
        b = c->xbld[nr * R_MAP + nc];
        if (b >= 0x3Bu) /* a tunnel end, a highway, a bridge, a building: a carrier */
        {
            pt->x += ROAD_DU[e] * 0.49f;
            pt->y += ROAD_DV[e] * 0.49f;
            return 1;
        }
    }
    return 0;
}

/*  Walk one segment of a family from a node tile out through link `e`,
 *  collect its centreline, straighten, fillet and loft it.  `visited`
 *  marks (tile, link) so each segment is walked once, from either end. */
static int walk_segment(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, int32_t col, int32_t row, int e, uint8_t *visited)
{
    static V2    pts[MAX_PTS];
    static V2    q[MAX_PTS];
    static Piece pieces[MAX_PIECES];
    float        hw = f == F_ROAD ? ROAD_W * 0.5f : RAIL_W * 0.5f;
    int          n = 0, k, nk, np, kind0 = node_kind(c, l, f, col, row), kind1 = 0;
    int          square0 = 0, square1 = 0; /* an end that runs square to a carrier */
    int32_t      cc = col, cr = row, back = (e + 2) & 3, guard = 0;
    int          ee    = e;
    float        total = 0.0f, rmax;
    if (visited[(row * R_MAP + col) * 4 + e])
        return 0;
    visited[(row * R_MAP + col) * 4 + e] = 1;
    /* the start: the side of the junction box, or the end tile's centre */
    if (kind0 == 2)
        pts[n++] = (V2){(float)col + 0.5f + ROAD_DU[e] * hw, (float)row + 0.5f + ROAD_DV[e] * hw};
    else
        square0 = end_point(c, l, f, col, row, &pts[n++]);
    for (;;)
    {
        int links, other;
        cc += (int32_t)ROAD_DU[ee];
        cr += (int32_t)ROAD_DV[ee];
        back = (ee + 2) & 3;
        if (cc < 0 || cr < 0 || cc >= R_MAP || cr >= R_MAP)
        {
            /* off the map: the segment runs to the edge, the last tile's side */
            pts[n++] = (V2){(float)(cc - (int32_t)ROAD_DU[ee]) + 0.5f + ROAD_DU[ee] * 0.5f,
                            (float)(cr - (int32_t)ROAD_DV[ee]) + 0.5f + ROAD_DV[ee] * 0.5f};
            kind1    = 0;
            break;
        }
        links = eff_links(c, l, cc, cr, f);
        if (!(links & (1 << back)))
            break; /* cannot happen on effective links; kept as a guard */
        visited[(cr * R_MAP + cc) * 4 + back] = 1;
        if (n + 2 >= MAX_PTS || ++guard > 4096)
            break;
        kind1 = node_kind(c, l, f, cc, cr);
        if (kind1 != 0 || link_count(links) != 2)
        {
            /* a node: the far end */
            if (kind1 == 0)
                kind1 = 1;
            if (kind1 == 2)
                pts[n++] = (V2){(float)cc + 0.5f + ROAD_DU[back] * hw, (float)cr + 0.5f + ROAD_DV[back] * hw};
            else
                square1 = end_point(c, l, f, cc, cr, &pts[n++]);
            break;
        }
        pts[n++]                            = (V2){(float)cc + 0.5f, (float)cr + 0.5f};
        other                               = links & ~(1 << back);
        ee                                  = other == L_N ? 0 : other == L_E ? 1
                                                             : other == L_S   ? 2
                                                                              : 3;
        visited[(cr * R_MAP + cc) * 4 + ee] = 1;
        if (cc == col && cr == row)
            break; /* a loop back to the start */
    }
    if (n < 2)
        return 0;
    /*  Straighten (spec 3.10, step 4).  First the collinear points go,
     *  so a straight run is one edge.  Then a staircase -- a run of two
     *  or more bends turning left, right, left with legs of at most
     *  three tiles between them -- becomes one straight line: its
     *  direction the run's net direction, through the centroid of its
     *  corners, joined to the legs before and after at the points where
     *  it crosses them.  A 45-degree staircase gives a 45-degree road, a
     *  2:1 staircase a 2:1 road; a lone corner or a U-turn is left for
     *  the fillet. */
    /*  One class for the whole segment, the median of its tiles', so an
     *  avenue's centre line does not start and stop mid-block with the
     *  traffic count of each tile (Bay View's shore road). */
    if (f == F_ROAD)
    {
        int cnt[3] = {0, 0, 0}, half, acc = 0, cls;
        for (k = 0; k < n; ++k)
        {
            int32_t tc = (int32_t)floorf(pts[k].x), tr = (int32_t)floorf(pts[k].y);
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            ++cnt[(int)road_class(c, tc, tr)];
        }
        half = (cnt[0] + cnt[1] + cnt[2] + 1) / 2;
        for (cls = 0; cls < 2; ++cls)
        {
            acc += cnt[cls];
            if (acc >= half)
                break;
        }
        s_seg_class = (float)cls;
    }
    else
        s_seg_class = -1.0f;
    nk = straighten(pts, n, q);
    {
        /*  SC2K_ROAD_DUMP=1 prints every segment of six tiles or more;
         *  SC2K_ROAD_DUMP=col,row every segment through that tile. */
        const char *dump = getenv("SC2K_ROAD_DUMP");
        int         dc = -1, dr = -1, show = 0;
        if (dump && sscanf(dump, "%d,%d", &dc, &dr) == 2)
        {
            for (k = 0; k < n; ++k)
                if ((int32_t)floorf(pts[k].x) == dc && (int32_t)floorf(pts[k].y) == dr)
                    show = 1;
        }
        else if (dump && n >= 6)
            show = 1;
        if (show)
        {
            printf("segment f%d from c%d r%d e%d: %d points, %d kept:", (int)f, (int)col, (int)row, e, n, nk);
            for (k = 0; k < nk; ++k)
                printf(" (%.2f,%.2f)", (double)q[k].x, (double)q[k].y);
            printf("\n  raw:");
            for (k = 0; k < n; ++k)
                printf(" (%.2f,%.2f)", (double)pts[k].x, (double)pts[k].y);
            printf("\n");
        }
    }
    /* fillet: a lone corner's quarter circle, a diagonal merge's sweep of up to a tile (spec 3.9) */
    rmax = 1.0f;
    if (fillet(q, nk, rmax, pieces, &np) != 0 || np == 0)
        return 0;
    for (k = 0; k < np; ++k)
        total += pieces[k].len;
    s_seg_node[0][0] = col;
    s_seg_node[0][1] = row;
    s_seg_node[1][0] = cc;
    s_seg_node[1][1] = cr;
    s_seg_kind[0]    = kind0;
    s_seg_kind[1]    = kind1;
    s_seg_ctrl[0]    = (f == F_ROAD && kind0 == 2) ? (s_junc_ctrl[row * R_MAP + col] >> (2 * e)) & 3 : 0;
    s_seg_ctrl[1]    = (f == F_ROAD && kind1 == 2) ? (s_junc_ctrl[cr * R_MAP + cc] >> (2 * back)) & 3 : 0;
    /* the crosswalk and stop bar only on a controlled leg (spec 3.4) */
    if (loft(m, c, mask_bit, comp, f, pieces, np, total, s_seg_ctrl[0] ? 0.2f : 0.0f, s_seg_ctrl[1] ? 0.2f : 0.0f, kind0 == 2, kind1 == 2) != 0)
        return -1;
    /* an end on open land: a road's round cap, the turning head of a dead end; a rail ends square */
    if (f == F_ROAD)
    {
        int which;
        for (which = 0; which < 2; ++which)
        {
            int   at_end = which == 1;
            int   end    = at_end ? np - 1 : 0;
            V2    pos, dir;
            float h, ang;
            if (at_end ? (kind1 != 1 || square1) : (kind0 != 1 || square0))
                continue;
            /*  Spec 3.10, step 11: the turning head is a local road's, and
             *  only where the tile's other neighbours are open land; an
             *  avenue or boulevard ends square, a barricade to come. */
            if (s_seg_class > 0.5f || !cul_de_sac_room(c, at_end ? (int32_t)floorf(pts[n - 1].x) : col, at_end ? (int32_t)floorf(pts[n - 1].y) : row))
                continue;
            piece_at(&pieces[end], at_end ? pieces[end].len : 0.0f, &pos, &dir);
            if (!at_end)
            {
                dir.x = -dir.x;
                dir.y = -dir.y;
            }
            h   = hw * width_factor(dir.x, dir.y, comp);
            ang = atan2f(dir.y, dir.x);
            {
                int32_t tc = (int32_t)floorf(pos.x), tr = (int32_t)floorf(pos.y);
                if (tc < 0)
                    tc = 0;
                if (tr < 0)
                    tr = 0;
                if (tc >= R_MAP)
                    tc = R_MAP - 1;
                if (tr >= R_MAP)
                    tr = R_MAP - 1;
                if (strip_fan(m, c, mask_bit, tile_order(c, tc, tr, mask_bit), pos.x, pos.y, ang - 1.5707963f, ang + 1.5707963f, h, h, 0.0f, MAT_ROAD, 8) != 0)
                    return -1;
            }
        }
    }
    return 0;
}

/*  The control of a road junction's arms (spec 3.4), from the classes
 *  of the roads meeting there, read on the first tile of each arm: all
 *  local, four legs, a two-way stop on the quieter axis; three legs
 *  with a local stem, a stop on the stem; a local against an avenue,
 *  stop on the local legs; avenue against avenue, an all-way stop, or a
 *  signal where the junction is busy; anything against a boulevard, a
 *  signal.  Two bits per arm. */
static int junction_control(const RCity *c, int32_t col, int32_t row, int links)
{
    int cls[4], ctrl[4] = {0, 0, 0, 0}, e, n = 0, maxc = 0, minc = 9, busy;
    int traf[4];
    for (e = 0; e < 4; ++e)
    {
        int32_t nc = col + (int32_t)ROAD_DU[e], nr = row + (int32_t)ROAD_DV[e];
        cls[e]  = -1;
        traf[e] = 0;
        if (!(links & (1 << e)) || nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
            continue;
        cls[e]  = (int)road_class(c, nc, nr);
        traf[e] = c->xtrf[(nr >> 1) * R_HALF + (nc >> 1)];
        ++n;
        if (cls[e] > maxc)
            maxc = cls[e];
        if (cls[e] < minc)
            minc = cls[e];
    }
    busy = c->xtrf[(row >> 1) * R_HALF + (col >> 1)] > 0xAA;
    if (maxc >= 2)
    {
        for (e = 0; e < 4; ++e)
            if (cls[e] >= 0)
                ctrl[e] = 2;
    }
    else if (maxc == 1 && minc == 1)
    {
        for (e = 0; e < 4; ++e)
            if (cls[e] >= 0)
                ctrl[e] = busy ? 2 : 1;
    }
    else if (maxc == 1)
    {
        for (e = 0; e < 4; ++e)
            if (cls[e] == 0)
                ctrl[e] = 1;
    }
    else if (n == 3)
    {
        /* the stem: the arm whose opposite is missing */
        for (e = 0; e < 4; ++e)
            if (cls[e] >= 0 && cls[(e + 2) & 3] < 0)
                ctrl[e] = 1;
    }
    else
    {
        /* four local legs: a two-way stop on the axis with the lighter traffic */
        int ns = traf[0] > traf[2] ? traf[0] : traf[2];
        int ew = traf[1] > traf[3] ? traf[1] : traf[3];
        for (e = 0; e < 4; ++e)
            if (cls[e] >= 0 && ((e == 0 || e == 2) ? ns <= ew : ew < ns))
                ctrl[e] = 1;
    }
    return ctrl[0] | (ctrl[1] << 2) | (ctrl[2] << 4) | (ctrl[3] << 6);
}

/*  A STOP sign at the driver's right of an approach, 1.5 m behind the
 *  crosswalk (spec 3.4, 6.3): a post with the bottom of the sign at
 *  2.1 m and a 750 mm red octagon facing the driver. */
static int put_stop_sign(RMesh *m, const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float order, int e, float h)
{
    static const int right[4] = {3, 0, 1, 2};
    int              r        = right[e];
    float            cx = (float)col + 0.5f, cy = (float)row + 0.5f;
    float            x = cx + ROAD_DU[e] * (h + 0.32f) + ROAD_DU[r] * (h + 0.03f);
    float            y = cy + ROAD_DV[e] * (h + 0.32f) + ROAD_DV[r] * (h + 0.03f);
    float            g = surface_at_world(c, mask_bit, x, y);
    if (put_box(m, c, mask_bit, order, x, y, 0.008f, 0.008f, 0.0f, 0.31f, MAT_PROP, 0.0f) != 0)
        return -1;
    return put_lamp_face(m, order, x, y, g, 0.29f, -ROAD_DU[e], -ROAD_DV[e], 0.025f, 0.0f, 13.0f);
}

/*  A junction of a family: the box, its sidewalk corners rounded as
 *  curb returns where two arms meet and square where a side is free,
 *  the outline along a free side, and for a road a signal on every arm. */
static int build_junction(RMesh *m, const RCity *c, uint8_t mask_bit, Family f, int32_t col, int32_t row, int links, float order)
{
    float hw  = f == F_ROAD ? ROAD_W * 0.5f : RAIL_W * 0.5f;
    float mat = f == F_ROAD ? MAT_ROAD : MAT_RAIL;
    float cx = (float)col + 0.5f, cy = (float)row + 0.5f, h = hw;
    float sw    = h * 0.20f; /* the curb return's radius: the sidewalk's width      */
    float lw    = h * 0.20f; /* the sidewalk along a free side, across 0.8..1       */
    float a0[2] = {cx - h, cy - h}, a1[2] = {cx + h, cy - h}, b0[2] = {cx - h, cy + h}, b1[2] = {cx + h, cy + h};
    int   e;
    s_road_class = 0.0f; /* the box is plain asphalt; a median ends at the junction */
    if (f == F_ROAD)
        s_junc_ctrl[row * R_MAP + col] = (uint8_t)junction_control(c, col, row, links);
    if (f == F_RAIL)
    {
        /*  A rail junction is no box: each arm's rails run through to
         *  the centre, so a T reads as a turnout and a crossing as two
         *  tracks crossing (a plain box was a brown square with the
         *  rails stopping at its sides). */
        for (e = 0; e < 4; ++e)
        {
            float su = cx + ROAD_DU[e] * h, sv = cy + ROAD_DV[e] * h;
            float px = -ROAD_DV[e], py = ROAD_DU[e];
            float o0[2] = {su - px * h, sv - py * h}, o1[2] = {su + px * h, sv + py * h};
            float q0[2] = {cx - px * h, cy - py * h}, q1[2] = {cx + px * h, cy + py * h};
            if (!(links & (1 << e)))
                continue;
            if (strip_quad(m, c, mask_bit, order + 0.01f * (float)e, o0, o1, q0, q1, -1.0f, 1.0f, 0.0f, h, mat) != 0)
                return -1;
        }
        return 0;
    }
    if (strip_quad(m, c, mask_bit, order, a0, a1, b0, b1, 0.5f, 0.5f, -1.0f, -1.0f, mat) != 0)
        return -1;
    for (e = 0; e < 4; ++e)
    {
        /* the side: a free side carries the sidewalk and its curb along its length */
        float su = cx + ROAD_DU[e] * h, sv = cy + ROAD_DV[e] * h;
        float px = -ROAD_DV[e], py = ROAD_DU[e];
        if (!(links & (1 << e)))
        {
            float o0[2] = {su - px * h - ROAD_DU[e] * lw, sv - py * h - ROAD_DV[e] * lw};
            float o1[2] = {su + px * h - ROAD_DU[e] * lw, sv + py * h - ROAD_DV[e] * lw};
            float q0[2] = {su - px * h, sv - py * h}, q1[2] = {su + px * h, sv + py * h};
            if (strip_quad(m, c, mask_bit, order + 0.02f, o0, o1, q0, q1, 0.80f, 1.0f, -1.0f, -1.0f, mat) != 0)
                return -1;
        }
        else if (f == F_ROAD)
        {
            int ctrl = (s_junc_ctrl[row * R_MAP + col] >> (2 * e)) & 3;
            if (ctrl == 2 && put_signal(m, c, col, row, mask_bit, order, e, h) != 0)
                return -1;
            if (ctrl == 1 && put_stop_sign(m, c, col, row, mask_bit, order, e, h) != 0)
                return -1;
        }
    }
    if (f == F_ROAD)
        for (e = 0; e < 4; ++e)
        {
            /*  The corner between side e and side e+1.  Two arms meet
             *  there: a curb return, the sidewalk's corner rounded on a
             *  fillet of radius sw.  Otherwise the sidewalk's square. */
            int   ea = e, eb = (e + 1) & 3;
            float ku = cx + (ROAD_DU[ea] + ROAD_DU[eb]) * h, kv = cy + (ROAD_DV[ea] + ROAD_DV[eb]) * h;
            float iu = ku - (ROAD_DU[ea] + ROAD_DU[eb]) * sw, iv = kv - (ROAD_DV[ea] + ROAD_DV[eb]) * sw;
            if ((links & (1 << ea)) && (links & (1 << eb)))
            {
                /* the return: a fan from the corner point over the quarter circle about (iu, iv)...
                 * drawn as the region between the corner and the arc, as a fan from the corner */
                float ang0 = atan2f(ROAD_DV[ea] * h, ROAD_DU[ea] * h);
                float ang1 = atan2f(ROAD_DV[eb] * h, ROAD_DU[eb] * h);
                float t0   = atan2f(-(ROAD_DV[ea] + ROAD_DV[eb]), -(ROAD_DU[ea] + ROAD_DU[eb]));
                int   i;
                (void)ang0;
                (void)ang1;
                /* the arc about (iu, iv), radius sw, spanning the 90 degrees that face the corner */
                for (i = 0; i < 6; ++i)
                {
                    float fa = (float)i / 6.0f, fb = (float)(i + 1) / 6.0f;
                    float ta = t0 - 0.7853982f + 1.5707963f * fa, tb = t0 - 0.7853982f + 1.5707963f * fb;
                    float tri[3][3], ref[3] = {0.95f, 0.95f, 0.95f}, ref2[3] = {-1.0f, -1.0f, -1.0f};
                    float rc[3] = {0.0f, 0.0f, MAT_WALK};
                    /* the region between the arc and the corner: a fan from the corner point */
                    tri[0][0] = ku;
                    tri[0][1] = kv;
                    tri[1][0] = iu + sw * cosf(ta);
                    tri[1][1] = iv + sw * sinf(ta);
                    tri[2][0] = iu + sw * cosf(tb);
                    tri[2][1] = iv + sw * sinf(tb);
                    tri[0][2] = surface_at_world(c, mask_bit, tri[0][0], tri[0][1]);
                    tri[1][2] = surface_at_world(c, mask_bit, tri[1][0], tri[1][1]);
                    tri[2][2] = surface_at_world(c, mask_bit, tri[2][0], tri[2][1]);
                    /* over the box, which a sidewalk face otherwise lies under */
                    if (put_tri_r2(m, (const float (*)[3])tri, NULL, order + 0.15f, rc, ref, ref2, 0) != 0)
                        return -1;
                }
                (void)ku;
                (void)kv;
            }
        }
    return 0;
}

/*  A power line on one tile: the pole at the centre with its crossbar,
 *  and a wire from its top to each joined edge's midpoint, where the
 *  neighbour's wire meets it.  On a crossing over a road or rail there
 *  is no pole: the wire spans from edge to edge. */
static int build_power_tile(RMesh *m, const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, int links, float order, int crossing)
{
    int   e;
    float top = 1.45f, cx = (float)col + 0.5f, cy = (float)row + 0.5f;
    if (!crossing)
    {
        int ns = (links & (L_N | L_S)) && !(links & (L_E | L_W));
        if (put_box(m, c, mask_bit, order, cx, cy, 0.04f, 0.04f, 0.0f, top, MAT_PROP, 0.0f) != 0)
            return -1;
        if (put_box(m, c, mask_bit, order, cx, cy, ns ? 0.24f : 0.04f, ns ? 0.04f : 0.24f, top - 0.12f, top - 0.08f, MAT_PROP, 0.0f) != 0)
            return -1;
        for (e = 0; e < 4; ++e)
            if (links & (1 << e))
                if (put_wire(m, c, mask_bit, order, cx, cy, top - 0.1f, (float)col + ROAD_MU[e], (float)row + ROAD_MV[e], top - 0.1f, 0.05f) != 0)
                    return -1;
        return 0;
    }
    if ((links & (L_N | L_S)) == (L_N | L_S))
        return put_wire(m, c, mask_bit, order, cx, (float)row, top - 0.1f, cx, (float)row + 1.0f, top - 0.1f, 0.1f);
    return put_wire(m, c, mask_bit, order, (float)col, cy, top - 0.1f, (float)col + 1.0f, cy, top - 0.1f, 0.1f);
}

/*  A prism clipped to the tile grid, each piece in the depth slot of
 *  the tile under it (the user: "train cars get masked off once in a
 *  while, like they flicker").  A car had carried the order of the tile
 *  under its centre, and the part of it over the next tile toward the
 *  viewer lay under that tile's ground until the centre crossed, when
 *  the whole car stood up at once.  `order` is the centre tile's slot
 *  with the fraction the pieces keep above each tile's ground. */
static int put_prism_clip(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float dx, float dy, float len, float wid, float zb, float zf, float z0, float z1, float paint)
{
    static const float up[3] = {0.0f, 0.0f, 1.0f};
    float              ax = dx * len * 0.5f, ay = dy * len * 0.5f, bx = -dy * wid * 0.5f, by = dx * wid * 0.5f;
    float              p[4][3], top[4][3], col[3] = {paint, 0.0f, MAT_VEHICLE}, nrm[3], t3[3][3];
    float              ref[3] = {paint, paint, paint}, ref2[3] = {0.0f, 0.0f, 0.0f};
    int                k;
    p[0][0] = cx - ax - bx;
    p[0][1] = cy - ay - by;
    p[1][0] = cx + ax - bx;
    p[1][1] = cy + ay - by;
    p[2][0] = cx + ax + bx;
    p[2][1] = cy + ay + by;
    p[3][0] = cx - ax + bx;
    p[3][1] = cy - ay + by;
    for (k = 0; k < 4; ++k)
    {
        float zg  = (k == 1 || k == 2) ? zf : zb;
        p[k][2]   = zg + z0;
        top[k][0] = p[k][0];
        top[k][1] = p[k][1];
        top[k][2] = zg + z1;
    }
    for (k = 0; k < 4; ++k)
    {
        const float *a = p[k], *b = p[(k + 1) & 3], *ta = top[k], *tb = top[(k + 1) & 3];
        float        ex = b[0] - a[0], ey = b[1] - a[1], el = sqrtf(ex * ex + ey * ey);
        if (el < 1e-6f)
            continue;
        nrm[0] = ey / el;
        nrm[1] = -ex / el;
        nrm[2] = 0.0f;
        memcpy(t3[0], ta, sizeof t3[0]);
        memcpy(t3[1], tb, sizeof t3[1]);
        memcpy(t3[2], b, sizeof t3[2]);
        if (put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])t3, nrm, col, ref, ref2) != 0)
            return -1;
        memcpy(t3[0], ta, sizeof t3[0]);
        memcpy(t3[1], b, sizeof t3[1]);
        memcpy(t3[2], a, sizeof t3[2]);
        if (put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])t3, nrm, col, ref, ref2) != 0)
            return -1;
    }
    memcpy(t3[0], top[0], sizeof t3[0]);
    memcpy(t3[1], top[1], sizeof t3[1]);
    memcpy(t3[2], top[2], sizeof t3[2]);
    if (put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])t3, up, col, ref, ref2) != 0)
        return -1;
    memcpy(t3[0], top[0], sizeof t3[0]);
    memcpy(t3[1], top[2], sizeof t3[1]);
    memcpy(t3[2], top[3], sizeof t3[2]);
    return put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])t3, up, col, ref, ref2);
}

/*  Every network: the power lines per tile; the roads and rails as
 *  junctions and segments. */
static int build_networks(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, uint8_t mask_bit, int comp)
{
    static uint8_t visited[R_MAP * R_MAP * 4];
    int32_t        col, row;
    Family         fam;
    (void)a;
    for (row = 0; row < R_MAP; ++row)
        for (col = 0; col < R_MAP; ++col)
        {
            int32_t idx   = row * R_MAP + col;
            uint8_t b     = c->xbld[idx];
            float   order = tile_order(c, col, row, mask_bit);
            Family  f, f2;
            int     piece = piece_family(b, &f), second = piece_second(b, &f2);
            if (piece >= 0 && f == F_POWER)
            {
                int links = piece_links(l, piece, c->xter[idx]);
                if (links && build_power_tile(m, c, col, row, mask_bit, links, order, 0) != 0)
                    return -1;
            }
            if (second >= 0 && f2 == F_POWER)
            {
                if (build_power_tile(m, c, col, row, mask_bit, piece_links(l, second, c->xter[idx]), order, 1) != 0)
                    return -1;
            }
            if (second >= 0 && f2 == F_RAIL)
            {
                /*  A level crossing (spec 3.15): every rail is a double-
                 *  track mainline, so every road class gets gates.  The
                 *  crossing surface is one panel across both tracks, the
                 *  road's width and its sidewalks, with the rails alone
                 *  showing through it.  On each approach the mast stands
                 *  at the driver's right, a road's half width plus a hair
                 *  from the centreline and a rail's half width plus a hair
                 *  from the track; the stop line crosses the approach lane
                 *  4.5 m, two fifths of a tile, before the nearest rail;
                 *  a second-train sign faces each sidewalk.  The gates are
                 *  down and the flashers lit while a train stands within
                 *  three tiles along the line. */
                int   links2 = piece_links(l, second, c->xter[idx]);
                int   ns     = (links2 & (L_N | L_S)) == (L_N | L_S); /* the rail runs north-south */
                float h = ROAD_W * 0.5f + 0.08f, rh = RAIL_W * 0.5f + 0.10f, cx = (float)col + 0.5f, cy = (float)row + 0.5f;
                int   ap, side;
                if (m->n_xings + 1u > m->cap_xings)
                {
                    uint32_t nc = m->cap_xings ? m->cap_xings * 2u : 64u;
                    RXing   *nx = (RXing *)realloc(m->xings, nc * sizeof *nx);
                    if (!nx)
                        return -1;
                    m->xings     = nx;
                    m->cap_xings = nc;
                }
                m->xings[m->n_xings].col = col;
                m->xings[m->n_xings].row = row;
                m->xings[m->n_xings].ns  = ns;
                ++m->n_xings;
                /* the surface: the panel across the tracks, the band's width, over the asphalt */
                {
                    float pw = RAIL_W * 0.5f + 0.06f, hb = ROAD_W * 0.5f;
                    float a0[2] = {ns ? cx - pw : cx - hb, ns ? cy - hb : cy - pw}, a1[2] = {ns ? cx + pw : cx + hb, ns ? cy - hb : cy - pw};
                    float b0[2] = {ns ? cx - pw : cx - hb, ns ? cy + hb : cy + pw}, b1[2] = {ns ? cx + pw : cx + hb, ns ? cy + hb : cy + pw};
                    if (strip_quad(m, c, mask_bit, order, a0, a1, b0, b1, -1.0f, 1.0f, 0.0f, 1.0f, MAT_XPANEL) != 0)
                        return -1;
                }
                for (ap = 0; ap < 2; ++ap)
                {
                    /* the two road approaches: travel direction f along the road's axis */
                    float fx = ns ? (ap ? -1.0f : 1.0f) : 0.0f, fy = ns ? 0.0f : (ap ? -1.0f : 1.0f);
                    float rx = -fy, ry = fx; /* the driver's right */
                    float mx = cx - fx * rh + rx * h, my = cy - fy * rh + ry * h;
                    float sl = 0.133f + 0.048f + 0.30f; /* the stop line: 4.5 m before the nearest rail of the nearer track (spec 3.15, multi-track) */
                    float s0[2], s1[2], t0[2], t1[2];
                    if (put_gate(m, c, mask_bit, order, mx, my, fx, fy, ns) != 0)
                        return -1;
                    s0[0]        = cx - fx * (sl + 0.02f);
                    s0[1]        = cy - fy * (sl + 0.02f);
                    s1[0]        = cx - fx * (sl + 0.02f) + rx * (ROAD_W * 0.5f * 0.8f);
                    s1[1]        = cy - fy * (sl + 0.02f) + ry * (ROAD_W * 0.5f * 0.8f);
                    t0[0]        = cx - fx * (sl - 0.02f);
                    t0[1]        = cy - fy * (sl - 0.02f);
                    t1[0]        = cx - fx * (sl - 0.02f) + rx * (ROAD_W * 0.5f * 0.8f);
                    t1[1]        = cy - fy * (sl - 0.02f) + ry * (ROAD_W * 0.5f * 0.8f);
                    s_road_class = 0.0f;
                    if (strip_quad(m, c, mask_bit, order + 0.25f, s0, s1, t0, t1, 0.0f, 0.79f, 0.17f, 0.17f, MAT_ZEBRA) != 0)
                        return -1;
                    /* the second-train signs, one at the end of each sidewalk on this approach */
                    for (side = -1; side <= 1; side += 2)
                    {
                        float sx = cx - fx * (rh + 0.02f) + rx * (float)side * (ROAD_W * 0.5f - 0.04f);
                        float sy = cy - fy * (rh + 0.02f) + ry * (float)side * (ROAD_W * 0.5f - 0.04f);
                        if (put_second_train_sign(m, c, mask_bit, order, sx, sy, -fx, -fy) != 0)
                            return -1;
                    }
                }
            }
        }
    for (fam = F_ROAD; fam <= F_RAIL; ++fam)
    {
        memset(visited, 0, sizeof visited);
        /* the junctions and the segments that leave every node */
        for (row = 0; row < R_MAP; ++row)
            for (col = 0; col < R_MAP; ++col)
            {
                int links = eff_links(c, l, col, row, fam), e;
                int kind;
                if (!tile_links(c, l, col, row, fam))
                    continue;
                kind = node_kind(c, l, fam, col, row);
                if (kind == 2 && build_junction(m, c, mask_bit, fam, col, row, links, tile_order(c, col, row, mask_bit) + (fam == F_RAIL ? 0.05f : 0.0f)) != 0)
                    return -1;
                if (kind == 0)
                {
                    /*  A piece none of whose links a neighbour returns: an
                     *  island, drawn as its own short band, tile centre to
                     *  its two sides, so it is not left out. */
                    if (links == 0 && build_island(m, c, l, mask_bit, comp, fam, col, row) != 0)
                        return -1;
                    continue;
                }
                for (e = 0; e < 4; ++e)
                    if ((links & (1 << e)) &&
                        walk_segment(m, c, l, mask_bit, comp, fam, col, row, e, visited) != 0)
                        return -1;
            }
        /* the loops with no node at all: start anywhere still unvisited */
        for (row = 0; row < R_MAP; ++row)
            for (col = 0; col < R_MAP; ++col)
            {
                int links = eff_links(c, l, col, row, fam), e;
                if (link_count(links) != 2)
                    continue;
                for (e = 0; e < 4; ++e)
                    if ((links & (1 << e)) && !visited[(row * R_MAP + col) * 4 + e] &&
                        walk_segment(m, c, l, mask_bit, comp, fam, col, row, e, visited) != 0)
                        return -1;
            }
    }
    return 0;
}

int r_mesh_build(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int roads)
{
    static const float land_fb[3] = {0.45f, 0.62f, 0.30f};
    s_check_xbld                  = roads ? c->xbld : NULL;
    m->net.n_pts                  = 0;
    m->net.n_segs                 = 0;
    m->railnet.n_pts              = 0;
    m->railnet.n_segs             = 0;
    m->n_xings                    = 0;
    m->n_rsigs                    = 0;
    memset(s_junc_ctrl, 0, sizeof s_junc_ctrl);
    static const float sediment[3] = {0.0f, 0.0f, MAT_SEDIMENT};
    static const float eng_wall[3] = {0.0f, 0.0f, MAT_ENG_WALL};
    static const float earth[3]    = {0.0f, 0.0f, MAT_EARTH};
    float              land_col[3];
    int32_t            col, row;
    const uint8_t      mask_bit = r_city_corner_mask(c->rotation);
    int                dump_r = -1, dump_c = -1;

    if (getenv("SC2K_MESH_DUMP"))
        sscanf(getenv("SC2K_MESH_DUMP"), "%d,%d", &dump_r, &dump_c);
    m->n_land   = 0;
    m->n_water  = 0;
    m->to_water = 0;
    m->n_walls  = 0;
    build_field(c);
    tile_colour(a, l, 256, land_col, land_fb);
    land_col[2] = MAT_GROUND;

    for (row = 0; row < R_MAP; ++row)
        for (col = 0; col < R_MAP; ++col)
        {
            int32_t idx  = row * R_MAP + col;
            uint8_t xter = c->xter[idx], xbld = c->xbld[idx];
            int32_t code  = slope_code(xter);
            float   order = tile_order(c, col, row, mask_bit);
            float   z[4], p[4][3], bed[4][3];
            Kind    kind = tile_top(c, col, row, mask_bit, z);
            int     wet  = water_top(c, idx, kind); /* a body of water under the top */
            int     k, e;

            for (k = 0; k < 4; ++k)
            {
                grid_point(col, row, k, z[k], p[k]);
                grid_point(col, row, k, s_b[corner_gi(col, row, k)], bed[k]);
            }
            if (row == dump_r && col == dump_c)
                printf("tile r%d c%d: xter %02x xbld %02x kind %d order %g  "
                       "NW %g NE %g SE %g SW %g  bed %g %g %g %g\n",
                       (int)row,
                       (int)col,
                       xter,
                       xbld,
                       (int)kind,
                       order,
                       z[NW],
                       z[NE],
                       z[SE],
                       z[SW],
                       bed[NW][2],
                       bed[NE][2],
                       bed[SE][2],
                       bed[SW][2]);

            if (underground)
            {
                /*  The underground view: the ground itself, the seabed
                 *  under water, cut by its own slope code, nothing else. */
                if (put_top(m, (const float (*)[3])bed, code, order, land_col, 0) != 0)
                    return -1;
                continue;
            }

            /*  Rule 2: the top. */
            {
                float surface[3];
                if (wet)
                {
                    surface[0] = z[NW];
                    /* calm, no foam and no caustics: under a marina, on a stream */
                    surface[1] = (kind == T_PAD || xter >= 0x30u) ? 1.0f : 0.0f;
                    surface[2] = MAT_SURFACE;
                }
                if (put_top(m, (const float (*)[3])p, kind == T_LAND ? code : 0, order, wet ? surface : land_col, kind == T_PAD) != 0)
                    return -1;
            }

            /*  Rule 4: under a water body, the seabed, and the land going
             *  on below the surface down to it on every side that is not
             *  water.  Both a quarter step behind the tile, so the surface
             *  wins inside the diamond; they show through the glass. */
            if (wet)
            {
                float scol[3] = {z[NW], 0.0f, MAT_SEABED};
                if (put_top(m, (const float (*)[3])bed, 0, order - 0.25f, scol, 0) != 0)
                    return -1;
                for (e = 0; e < 4; ++e)
                {
                    int32_t nr = row + EDGE_DR[e], nc = col + EDGE_DC[e];
                    float   nrm[3];
                    int     ia = EDGE_A[e], ib = EDGE_B[e];
                    if (nr < 0 || nc < 0 || nr >= R_MAP || nc >= R_MAP)
                        continue; /* the map's edge: the glass */
                    if (is_water(c->xter[nr * R_MAP + nc]))
                        continue; /* water beside water shares the bed */
                    if (bed[ia][2] >= p[ia][2] - 1e-4f && bed[ib][2] >= p[ib][2] - 1e-4f)
                        continue;
                    nrm[0] = -EDGE_N[e][0]; /* seen from inside the water */
                    nrm[1] = -EDGE_N[e][1];
                    nrm[2] = 0.0f;
                    if (put_wall(m, p[ia], p[ib], bed[ia], bed[ib], nrm, order - 0.25f, earth) != 0)
                        return -1;
                }
            }

            /*  Rule 3: the walls, on the east and south edges, so each
             *  edge between two tiles is done once. */
            for (e = E_E; e <= E_S; ++e)
            {
                int32_t      nr = row + EDGE_DR[e], nc = col + EDGE_DC[e], ni;
                float        zn[4], qa[3], qb[3], nrm[3], col3[3];
                Kind         kn;
                int          ia = EDGE_A[e], ib = EDGE_B[e];
                const float *mat;
                if (nr >= R_MAP || nc >= R_MAP)
                    continue;
                ni = nr * R_MAP + nc;
                kn = tile_top(c, nc, nr, mask_bit, zn);
                if (fabsf(z[ia] - zn[NBR_A[e]]) < 1e-4f &&
                    fabsf(z[ib] - zn[NBR_B[e]]) < 1e-4f)
                    continue;
                memcpy(qa, p[ia], sizeof qa);
                memcpy(qb, p[ib], sizeof qb);
                qa[2] = zn[NBR_A[e]];
                qb[2] = zn[NBR_B[e]];
                /*  A building's pad has a foundation of coursed blocks; a
                 *  step under a network piece, a road curve lifted on its
                 *  saddle or a slope piece against a corner the field
                 *  averages, is a bank of earth, as the original draws
                 *  the slope under such a sprite (Bay View, column 26,
                 *  row 12, had block walls along a hill road). */
                if (xbld >= 0x70u || c->xbld[ni] >= 0x70u)
                    mat = eng_wall;
                else if (wet && water_top(c, ni, kn))
                {
                    /* a drop between two waters: a face of water, a cascade */
                    col3[0] = fmaxf(fmaxf(z[ia], z[ib]), fmaxf(qa[2], qb[2]));
                    col3[1] = 1.0f;
                    col3[2] = MAT_SURFACE;
                    mat     = col3;
                }
                else
                    mat = earth;
                /* the face is seen from the lower side */
                {
                    float up = (z[ia] + z[ib]) - (qa[2] + qb[2]);
                    nrm[0]   = up >= 0.0f ? EDGE_N[e][0] : -EDGE_N[e][0];
                    nrm[1]   = up >= 0.0f ? EDGE_N[e][1] : -EDGE_N[e][1];
                    nrm[2]   = 0.0f;
                }
                if (put_wall(m, p[ia], p[ib], qa, qb, nrm, order + 0.5f, mat) != 0)
                    return -1;
                m->n_walls++;
            }

            /*  The map's cut edges: the two far ones, all four when the
             *  view turns.  Through land, the layers of sediment from what
             *  the tile draws down to the base, the foundation of a pad or
             *  a ramp as blocks above the ground; through water, the glass
             *  from the surface to the seabed and the sediment under it. */
            for (e = 0; e < 4; ++e)
            {
                int   on = e == E_E ? col == R_MAP - 1 : e == E_S ? row == R_MAP - 1
                                                     : e == E_W   ? (rotated && col == 0)
                                                                  : (rotated && row == 0);
                int   ia = EDGE_A[e], ib = EDGE_B[e];
                float base_a[3], base_b[3], ga[3], gb[3];
                if (!on)
                    continue;
                grid_point(col, row, ia, 0.0f, base_a);
                grid_point(col, row, ib, 0.0f, base_b);
                if (wet)
                {
                    float wcol[3] = {z[NW], 0.0f, MAT_WATER};
                    m->to_water   = 1;
                    if (put_wall_r2(m, p[ia], p[ib], bed[ia], bed[ib], EDGE_N[e], order, wcol, p[ia][2], p[ib][2], bed[ia][2], bed[ib][2]) != 0)
                        return -1;
                    m->to_water = 0;
                    if (put_wall_r(m, bed[ia], bed[ib], base_a, base_b, EDGE_N[e], order, sediment, bed[ia][2], bed[ib][2]) != 0)
                        return -1;
                    continue;
                }
                memcpy(ga, p[ia], sizeof ga);
                memcpy(gb, p[ib], sizeof gb);
                if (kind == T_PAD || kind == T_PLANE)
                {
                    ga[2] = s_h[corner_gi(col, row, ia)];
                    gb[2] = s_h[corner_gi(col, row, ib)];
                    if (put_wall(m, p[ia], p[ib], ga, gb, EDGE_N[e], order, eng_wall) != 0)
                        return -1;
                }
                if (put_wall_r(m, ga, gb, base_a, base_b, EDGE_N[e], order, sediment, bed[ia][2], bed[ib][2]) != 0)
                    return -1;
            }
        }
    if (roads && !underground)
        return build_networks(m, c, a, l, mask_bit, !rotated);
    return 0;
}

/* ---- the query tool ---------------------------------------------------- */

int r_mesh_tile_corners(const RCity *c, int32_t col, int32_t row, int underground, float z[4])
{
    float o[4], t[4];
    int   k;
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return -1;
    if (underground)
        for (k = 0; k < 4; ++k)
            t[k] = s_b[corner_gi(col, row, k)];
    else
        tile_top(c, col, row, r_city_corner_mask(c->rotation), t);
    /* out in NW, NE, SE, SW order, whatever the corner enum's is */
    o[0] = t[NW];
    o[1] = t[NE];
    o[2] = t[SE];
    o[3] = t[SW];
    memcpy(z, o, sizeof o);
    return 0;
}

int r_mesh_query(const RCity *c, int32_t col, int32_t row, char *buf, size_t n)
{
    static const char *const names[] = {"ground", "water", "levelled pad", "sloped plane"};
    int32_t                  idx     = row * R_MAP + col;
    float                    z[4], b[4];
    Kind                     kind;
    int                      k;
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP || !buf || n == 0)
        return -1;
    kind = tile_top(c, col, row, r_city_corner_mask(c->rotation), z);
    for (k = 0; k < 4; ++k)
        b[k] = s_b[corner_gi(col, row, k)];
    if (water_top(c, idx, kind))
        snprintf(buf, n, "Mesh: %s, surface %g, seabed NW %g NE %g SE %g SW %g", kind == T_PAD ? "pad on the water" : names[kind], z[NW], b[NW], b[NE], b[SE], b[SW]);
    else if (kind == T_PAD)
        snprintf(buf, n, "Mesh: %s at %g%s", names[kind], z[NW], saddle_lift(c, idx) ? " (saddle: lifted a level, $17528)" : "");
    else
        snprintf(buf, n, "Mesh: %s, NW %g NE %g SE %g SW %g", names[kind], z[NW], z[NE], z[SE], z[SW]);
    return 0;
}

/* ---- the check --------------------------------------------------------- */

/*  Every edge of every triangle must belong to another triangle too: a
 *  surface with no free edge has no crack.  Edges are keyed on their
 *  quantised end points (heights are means of at most four levels, so
 *  forty-eighths are exact).  A vertical edge is a span on its corner's
 *  vertical, and the spans there may be split differently by the walls
 *  meeting at the corner, so those are checked as coverage: every part
 *  of the vertical that any edge covers must be covered at least twice.
 *  The base of the map's cut, z = 0, is the floor and is closed by
 *  definition. */
typedef struct
{
    int32_t a[3], b[3];
    int32_t count;
    float   mat;
} Edge;

static uint32_t edge_hash(const int32_t *a, const int32_t *b)
{
    uint32_t h = 2166136261u;
    int      k;
    for (k = 0; k < 3; ++k)
    {
        h = (h ^ (uint32_t)a[k]) * 16777619u;
        h = (h ^ (uint32_t)b[k]) * 16777619u;
    }
    return h;
}

static void quantise(const float p[4], int32_t q[3])
{
    q[0] = (int32_t)lrintf(p[0] * 16.0f);
    q[1] = (int32_t)lrintf(p[1] * 16.0f);
    q[2] = (int32_t)lrintf(p[2] * 48.0f);
}

static int key_less(const int32_t *a, const int32_t *b)
{
    int k;
    for (k = 0; k < 3; ++k)
        if (a[k] != b[k])
            return a[k] < b[k];
    return 0;
}

typedef struct
{
    int32_t x, y, z0, z1;
    float   mat;
} Span;

static int span_cmp(const void *pa, const void *pb)
{
    const Span *a = (const Span *)pa, *b = (const Span *)pb;
    if (a->x != b->x)
        return a->x < b->x ? -1 : 1;
    if (a->y != b->y)
        return a->y < b->y ? -1 : 1;
    if (a->z0 != b->z0)
        return a->z0 < b->z0 ? -1 : 1;
    return 0;
}

static const char *mat_name(float m)
{
    switch ((int)(m + 0.5f))
    {
        case 0:
            return "ground";
        case 1:
            return "blocks";
        case 2:
            return "sediment";
        case 3:
            return "glass";
        case 4:
            return "seabed";
        case 5:
            return "earth";
        case 6:
            return "water";
        default:
            return "?";
    }
}

/*  The clipping check: no terrain face may rise above a road or sidewalk
 *  face over its footprint.  Every road triangle is sampled on a
 *  barycentric grid; at each sample the top faces of the tile under it
 *  are evaluated, and a terrain height above the road's by more than a
 *  hair is a penetration.  Returns the number of penetrating samples;
 *  with `verbose` prints the tiles, the deepest first. */
typedef struct
{
    uint32_t n;
    float    worst;
} ClipTile;

int r_mesh_check_roads(const RMesh *m, int verbose)
{
    uint32_t  n_tri = m->n_land / 3u, i, *count, *start, *list, hits = 0;
    ClipTile *tiles;
    uint32_t *order;
    const int N = 8;
    count       = (uint32_t *)calloc((size_t)R_MAP * R_MAP + 1u, sizeof *count);
    start       = (uint32_t *)calloc((size_t)R_MAP * R_MAP + 1u, sizeof *start);
    list        = (uint32_t *)malloc((size_t)n_tri * sizeof *list);
    tiles       = (ClipTile *)calloc((size_t)R_MAP * R_MAP, sizeof *tiles);
    order       = (uint32_t *)malloc((size_t)R_MAP * R_MAP * sizeof *order);
    if (!count || !start || !list || !tiles || !order)
    {
        free(count);
        free(start);
        free(list);
        free(tiles);
        free(order);
        return -1;
    }
    /*  The terrain's top faces by tile: the ones with an area in plan. */
    for (i = 0; i < n_tri; ++i)
    {
        const RMeshVert *v  = &m->land[i * 3u];
        float            ax = v[1].pos[0] - v[0].pos[0], ay = v[1].pos[1] - v[0].pos[1];
        float            bx = v[2].pos[0] - v[0].pos[0], by = v[2].pos[1] - v[0].pos[1];
        float            cx, cy;
        int32_t          col, row;
        if (v[0].col[2] > 6.5f || fabsf(ax * by - ay * bx) < 1e-6f)
            continue;
        cx  = (v[0].pos[0] + v[1].pos[0] + v[2].pos[0]) / 3.0f;
        cy  = (v[0].pos[1] + v[1].pos[1] + v[2].pos[1]) / 3.0f;
        col = (int32_t)floorf(cx);
        row = (int32_t)floorf(cy);
        if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
            continue;
        ++count[row * R_MAP + col];
    }
    for (i = 0; i < (uint32_t)R_MAP * R_MAP; ++i)
        start[i + 1] = start[i] + count[i];
    memset(count, 0, ((size_t)R_MAP * R_MAP + 1u) * sizeof *count);
    for (i = 0; i < n_tri; ++i)
    {
        const RMeshVert *v  = &m->land[i * 3u];
        float            ax = v[1].pos[0] - v[0].pos[0], ay = v[1].pos[1] - v[0].pos[1];
        float            bx = v[2].pos[0] - v[0].pos[0], by = v[2].pos[1] - v[0].pos[1];
        float            cx, cy;
        int32_t          col, row, t;
        if (v[0].col[2] > 6.5f || fabsf(ax * by - ay * bx) < 1e-6f)
            continue;
        cx  = (v[0].pos[0] + v[1].pos[0] + v[2].pos[0]) / 3.0f;
        cy  = (v[0].pos[1] + v[1].pos[1] + v[2].pos[1]) / 3.0f;
        col = (int32_t)floorf(cx);
        row = (int32_t)floorf(cy);
        if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
            continue;
        t                           = row * R_MAP + col;
        list[start[t] + count[t]++] = i;
    }
    /*  SC2K_TILE_DUMP=col,row prints every face whose centroid lies on
     *  that tile, any material, for inspection. */
    {
        const char *dump = getenv("SC2K_TILE_DUMP");
        int         dc, dr;
        if (dump && sscanf(dump, "%d,%d", &dc, &dr) == 2)
            for (i = 0; i < n_tri; ++i)
            {
                const RMeshVert *v  = &m->land[i * 3u];
                float            cx = (v[0].pos[0] + v[1].pos[0] + v[2].pos[0]) / 3.0f;
                float            cy = (v[0].pos[1] + v[1].pos[1] + v[2].pos[1]) / 3.0f;
                if ((int32_t)floorf(cx) == dc && (int32_t)floorf(cy) == dr)
                    printf("tri mat %g order %g (%.3f,%.3f,%.3f) (%.3f,%.3f,%.3f) (%.3f,%.3f,%.3f)\n", (double)v[0].col[2], (double)v[0].pos[3], (double)v[0].pos[0], (double)v[0].pos[1], (double)v[0].pos[2], (double)v[1].pos[0], (double)v[1].pos[1], (double)v[1].pos[2], (double)v[2].pos[0], (double)v[2].pos[1], (double)v[2].pos[2]);
            }
    }
    /*  Every road face, sampled. */
    for (i = 0; i < n_tri; ++i)
    {
        const RMeshVert *v   = &m->land[i * 3u];
        float            mat = v[0].col[2];
        int              a, b;
        if (!(mat > 6.5f && mat < 7.5f) && !(mat > 9.5f && mat < 11.5f) && !(mat > 12.5f && mat < 14.5f) && !(mat > 15.5f))
            continue; /* the bands, the sidewalk corners, the crossing surface and approach; not props, not vehicles */
        /* strictly inside the face: a sample on an edge would read the
         * tile across it, which may stand higher behind a wall */
        for (a = 1; a < N; ++a)
            for (b = 1; a + b < N; ++b)
            {
                float    u = (float)a / (float)N, w = (float)b / (float)N, s = 1.0f - u - w;
                float    x   = u * v[0].pos[0] + w * v[1].pos[0] + s * v[2].pos[0];
                float    y   = u * v[0].pos[1] + w * v[1].pos[1] + s * v[2].pos[1];
                float    z   = u * v[0].pos[2] + w * v[1].pos[2] + s * v[2].pos[2];
                int32_t  col = (int32_t)floorf(x), row = (int32_t)floorf(y), t;
                uint32_t j;
                float    worst = 0.0f;
                if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
                    continue;
                t = row * R_MAP + col;
                for (j = start[t]; j < start[t + 1]; ++j)
                {
                    const RMeshVert *q  = &m->land[list[j] * 3u];
                    float            d  = (q[1].pos[0] - q[0].pos[0]) * (q[2].pos[1] - q[0].pos[1]) -
                                          (q[2].pos[0] - q[0].pos[0]) * (q[1].pos[1] - q[0].pos[1]);
                    float            l1 = ((q[1].pos[0] - x) * (q[2].pos[1] - y) - (q[2].pos[0] - x) * (q[1].pos[1] - y)) / d;
                    float            l2 = ((q[2].pos[0] - x) * (q[0].pos[1] - y) - (q[0].pos[0] - x) * (q[2].pos[1] - y)) / d;
                    float            l3 = 1.0f - l1 - l2, zt;
                    if (l1 < -1e-4f || l2 < -1e-4f || l3 < -1e-4f)
                        continue;
                    zt = l1 * q[0].pos[2] + l2 * q[1].pos[2] + l3 * q[2].pos[2];
                    if (zt - z > worst)
                        worst = zt - z;
                }
                if (worst > 0.02f) /* a hair: a fifth of a pixel at the base zoom */
                {
                    ++hits;
                    ++tiles[t].n;
                    if (worst > tiles[t].worst)
                        tiles[t].worst = worst;
                    if (getenv("SC2K_CLIP_DUMP") && tiles[t].n <= 2)
                    {
                        printf("  clip at %.2f,%.2f road z %.2f mat %g tri (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f) order %g; ground +%.2f\n",
                               (double)x,
                               (double)y,
                               (double)z,
                               (double)mat,
                               (double)v[0].pos[0],
                               (double)v[0].pos[1],
                               (double)v[0].pos[2],
                               (double)v[1].pos[0],
                               (double)v[1].pos[1],
                               (double)v[1].pos[2],
                               (double)v[2].pos[0],
                               (double)v[2].pos[1],
                               (double)v[2].pos[2],
                               (double)v[0].pos[3],
                               (double)worst);
                        for (j = start[t]; j < start[t + 1]; ++j)
                        {
                            const RMeshVert *q = &m->land[list[j] * 3u];
                            printf("     ground mat %g (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f)\n", (double)q[0].col[2], (double)q[0].pos[0], (double)q[0].pos[1], (double)q[0].pos[2], (double)q[1].pos[0], (double)q[1].pos[1], (double)q[1].pos[2], (double)q[2].pos[0], (double)q[2].pos[1], (double)q[2].pos[2]);
                        }
                    }
                }
            }
    }
    /*  Every tile carrying a network piece must have network geometry
     *  over it: a face of a road material whose centroid lies on the
     *  tile.  A piece the walk could not draw would otherwise vanish,
     *  its sprite skipped and nothing in its place. */
    {
        uint32_t missing = 0;
        memset(count, 0, ((size_t)R_MAP * R_MAP + 1u) * sizeof *count);
        for (i = 0; i < n_tri; ++i)
        {
            const RMeshVert *v = &m->land[i * 3u];
            int              k;
            if (v[0].col[2] < 6.5f || (v[0].col[2] > 14.5f && v[0].col[2] < 15.5f))
                continue; /* a vehicle is no network geometry */
            /* the face touches every tile a vertex, an edge's midpoint or the centroid lies on */
            for (k = 0; k < 7; ++k)
            {
                float   x   = k < 3 ? v[k].pos[0] : k < 6 ? 0.5f * (v[k - 3].pos[0] + v[(k - 2) % 3].pos[0])
                                                          : (v[0].pos[0] + v[1].pos[0] + v[2].pos[0]) / 3.0f;
                float   y   = k < 3 ? v[k].pos[1] : k < 6 ? 0.5f * (v[k - 3].pos[1] + v[(k - 2) % 3].pos[1])
                                                          : (v[0].pos[1] + v[1].pos[1] + v[2].pos[1]) / 3.0f;
                int32_t col = (int32_t)floorf(x), row = (int32_t)floorf(y);
                if (col >= 0 && row >= 0 && col < R_MAP && row < R_MAP)
                    ++count[row * R_MAP + col];
            }
        }
        for (i = 0; i < (uint32_t)R_MAP * R_MAP; ++i)
        {
            uint8_t b = s_check_xbld ? s_check_xbld[i] : 0;
            if (!((b >= 0x0Eu && b <= 0x3Au) || (b >= 0x43u && b <= 0x48u)))
                continue;
            if (count[i] == 0)
            {
                ++missing;
                if (verbose && missing <= 40)
                    printf("  no network geometry on column %3d row %3d, XBLD %02x\n", (int)(i % R_MAP), (int)(i / R_MAP), (unsigned)b);
            }
        }
        if (verbose)
            printf("road pieces without geometry: %u\n", missing);
        hits += missing;
    }
    if (verbose)
    {
        uint32_t shown = 0, n_bad = 0;
        for (i = 0; i < (uint32_t)R_MAP * R_MAP; ++i)
            if (tiles[i].n)
                order[n_bad++] = i;
        /* the deepest first: sort the indices by the tile's worst */
        {
            uint32_t p, q;
            for (p = 1; p < n_bad; ++p)
                for (q = p; q > 0 && tiles[order[q - 1]].worst < tiles[order[q]].worst; --q)
                {
                    uint32_t tmp = order[q];
                    order[q]     = order[q - 1];
                    order[q - 1] = tmp;
                }
        }
        printf("road clip  %u samples under the terrain on %u tiles\n", hits, n_bad);
        for (i = 0; i < n_bad && shown < 40; ++i, ++shown)
            printf("  column %3d row %3d  %4u samples, terrain above by %.2f\n",
                   (int)(order[i] % R_MAP),
                   (int)(order[i] / R_MAP),
                   tiles[order[i]].n,
                   (double)tiles[order[i]].worst);
    }
    free(count);
    free(start);
    free(list);
    free(tiles);
    free(order);
    return (int)hits;
}

int r_mesh_check(const RMesh *m, int verbose)
{
    uint32_t n_tri = (m->n_land + m->n_water) / 3u, cap = 1u, i, free_edges = 0, free_spans = 0;
    Edge    *tab;
    Span    *spans;
    uint32_t n_spans = 0;
    uint32_t shown   = 0;
    uint32_t by_mat[8];
    memset(by_mat, 0, sizeof by_mat);
    while (cap < n_tri * 6u)
        cap *= 2u;
    tab   = (Edge *)calloc(cap, sizeof *tab);
    spans = (Span *)malloc((size_t)n_tri * 3u * sizeof *spans);
    if (!tab || !spans)
    {
        free(tab);
        free(spans);
        return -1;
    }
    for (i = 0; i < n_tri; ++i)
    {
        const RMeshVert *v = i * 3u < m->n_land ? &m->land[i * 3u]
                                                : &m->water[i * 3u - m->n_land];
        int32_t          q[3][3];
        int              k;
        if (v[0].col[2] > 6.5f)
            continue; /* a road strip lies on the surface; it is not the surface */
        for (k = 0; k < 3; ++k)
            quantise(v[k].pos, q[k]);
        for (k = 0; k < 3; ++k)
        {
            const int32_t *a = q[k], *b = q[(k + 1) % 3];
            uint32_t       h;
            if (a[0] == b[0] && a[1] == b[1])
            {
                Span *s;
                if (a[2] == b[2])
                    continue; /* a point, not an edge */
                s      = &spans[n_spans++];
                s->x   = a[0];
                s->y   = a[1];
                s->z0  = a[2] < b[2] ? a[2] : b[2];
                s->z1  = a[2] < b[2] ? b[2] : a[2];
                s->mat = v[k].col[2];
                continue;
            }
            if (key_less(b, a))
            {
                const int32_t *t = a;
                a                = b;
                b                = t;
            }
            h = edge_hash(a, b) & (cap - 1u);
            for (;;)
            {
                Edge *e = &tab[h];
                if (e->count == 0)
                {
                    memcpy(e->a, a, sizeof e->a);
                    memcpy(e->b, b, sizeof e->b);
                    e->count = 1;
                    e->mat   = v[k].col[2];
                    break;
                }
                if (memcmp(e->a, a, sizeof e->a) == 0 && memcmp(e->b, b, sizeof e->b) == 0)
                {
                    e->count++;
                    break;
                }
                h = (h + 1u) & (cap - 1u);
            }
        }
    }
    for (i = 0; i < cap; ++i)
    {
        const Edge *e = &tab[i];
        if (e->count != 1)
            continue;
        if (e->a[2] == 0 && e->b[2] == 0)
            continue; /* the floor */
        free_edges++;
        by_mat[(int)(e->mat + 0.5f) & 7]++;
        if (verbose && shown++ < 40)
            printf("free edge  (%g,%g,%g)-(%g,%g,%g)  tile c%d r%d  %s\n",
                   e->a[0] / 16.0,
                   e->a[1] / 16.0,
                   e->a[2] / 48.0,
                   e->b[0] / 16.0,
                   e->b[1] / 16.0,
                   e->b[2] / 48.0,
                   (int)((e->a[0] < e->b[0] ? e->a[0] : e->b[0]) / 16),
                   (int)((e->a[1] < e->b[1] ? e->a[1] : e->b[1]) / 16),
                   mat_name(e->mat));
    }
    /*  The verticals: per corner, the coverage of every elementary span. */
    if (n_spans)
    {
        uint32_t s0 = 0;
        qsort(spans, n_spans, sizeof *spans, span_cmp);
        while (s0 < n_spans)
        {
            uint32_t s1 = s0, j;
            int32_t  zs[512];
            uint32_t nz = 0, zi;
            while (s1 < n_spans && spans[s1].x == spans[s0].x && spans[s1].y == spans[s0].y)
                ++s1;
            for (j = s0; j < s1 && nz < 510; ++j)
            {
                zs[nz++] = spans[j].z0;
                zs[nz++] = spans[j].z1;
            }
            /* sort the breakpoints, small n */
            for (zi = 1; zi < nz; ++zi)
            {
                int32_t  t = zs[zi];
                uint32_t p = zi;
                while (p > 0 && zs[p - 1] > t)
                {
                    zs[p] = zs[p - 1];
                    --p;
                }
                zs[p] = t;
            }
            for (zi = 0; zi + 1 < nz; ++zi)
            {
                int32_t lo = zs[zi], hi = zs[zi + 1], cover = 0;
                float   mat = 0.0f;
                if (lo == hi)
                    continue;
                for (j = s0; j < s1; ++j)
                    if (spans[j].z0 <= lo && spans[j].z1 >= hi)
                    {
                        cover++;
                        mat = spans[j].mat;
                    }
                if (cover == 1)
                {
                    free_spans++;
                    by_mat[(int)(mat + 0.5f) & 7]++;
                    if (verbose && shown++ < 40)
                        printf("free span  corner (%g,%g) z %g..%g  %s\n",
                               spans[s0].x / 16.0,
                               spans[s0].y / 16.0,
                               lo / 48.0,
                               hi / 48.0,
                               mat_name(mat));
                }
            }
            s0 = s1;
        }
    }
    if (verbose)
    {
        int k;
        printf("mesh check: %u triangles, %u free edges, %u free vertical spans\n",
               (unsigned)n_tri,
               (unsigned)free_edges,
               (unsigned)free_spans);
        for (k = 0; k < 8; ++k)
            if (by_mat[k])
                printf("  %-9s %u\n", mat_name((float)k), (unsigned)by_mat[k]);
    }
    free(tab);
    free(spans);
    return (int)(free_edges + free_spans);
}

void r_mesh_free(RMesh *m)
{
    free(m->land);
    free(m->water);
    free(m->net.pts);
    free(m->net.segs);
    free(m->railnet.pts);
    free(m->railnet.segs);
    free(m->xings);
    free(m->rsigs);
    memset(m, 0, sizeof *m);
}

/* ---- the traffic ------------------------------------------------------ */

static uint32_t car_rand(uint32_t *st)
{
    *st = *st * 1664525u + 1013904223u;
    return *st >> 8;
}

static float car_frand(uint32_t *st)
{
    return (float)(car_rand(st) & 0xFFFFu) / 65535.0f;
}

/*  The band at distance s along a segment: position, height, direction. */
static void net_at(const RRoadNet *net, const RNetSeg *sg, float s, float *x, float *y, float *z, float *dx, float *dy)
{
    const RNetPt *p = net->pts + sg->first;
    uint32_t      n = sg->count, lo = 0, hi = n - 1;
    float         t, l;
    if (n == 1 || s <= p[0].s)
    {
        *x  = p[0].x;
        *y  = p[0].y;
        *z  = p[0].z;
        *dx = p[0].dx;
        *dy = p[0].dy;
        return;
    }
    if (s >= p[n - 1].s)
    {
        *x  = p[n - 1].x;
        *y  = p[n - 1].y;
        *z  = p[n - 1].z;
        *dx = p[n - 1].dx;
        *dy = p[n - 1].dy;
        return;
    }
    while (hi - lo > 1)
    {
        uint32_t mid = (lo + hi) / 2;
        if (p[mid].s <= s)
            lo = mid;
        else
            hi = mid;
    }
    t   = p[hi].s > p[lo].s ? (s - p[lo].s) / (p[hi].s - p[lo].s) : 0.0f;
    *x  = p[lo].x + (p[hi].x - p[lo].x) * t;
    *y  = p[lo].y + (p[hi].y - p[lo].y) * t;
    *z  = p[lo].z + (p[hi].z - p[lo].z) * t;
    *dx = p[lo].dx + (p[hi].dx - p[lo].dx) * t;
    *dy = p[lo].dy + (p[hi].dy - p[lo].dy) * t;
    l   = sqrtf(*dx * *dx + *dy * *dy);
    if (l > 1e-6f)
    {
        *dx /= l;
        *dy /= l;
    }
}

/*  A car's place on the road: the band at s offset into its lane, to the
 *  right of its direction of travel (y down), and its heading. */
static void car_place(const RRoadNet *net, const RCar *car, float *x, float *y, float *z, float *hx, float *hy)
{
    const RNetSeg *sg = &net->segs[car->seg];
    float          bx, by, bz, dx, dy, lane = car->lane ? sg->lane_in : sg->lane_out;
    net_at(net, sg, car->s, &bx, &by, &bz, &dx, &dy);
    if (car->dir < 0)
    {
        dx = -dx;
        dy = -dy;
    }
    *hx = dx;
    *hy = dy;
    *x  = bx - dy * lane;
    *y  = by + dx * lane;
    *z  = bz;
}

/*  The signal an arm faces: the junction's phase and the arm's axis, as
 *  the lamp material draws it: north-south arms run green for the first
 *  two fifths of the twelve-second cycle from the phase, east-west arms
 *  half a cycle later; amber and red both hold a car at the line. */
static int signal_red(int32_t col, int32_t row, float hx, float hy, float time)
{
    float phase = (float)((col * 7 + row * 13) % 8) / 8.0f;
    float t     = time / 12.0f + phase;
    int   ew    = fabsf(hx) > fabsf(hy);
    t           = t - floorf(t);
    if (ew)
    {
        t += 0.5f;
        t -= floorf(t);
    }
    return t >= 0.40f;
}

/*  The crossing's approach a (0 or 1): the mast at the driver's right
 *  and the direction of travel, as the mesh placed the mast. */
static void xing_approach(const RXing *x, int a, float *mx, float *my, float *fx, float *fy)
{
    float h = ROAD_W * 0.5f + 0.08f, rh = RAIL_W * 0.5f + 0.10f, cx = (float)x->col + 0.5f, cy = (float)x->row + 0.5f;
    *fx = x->ns ? (a ? -1.0f : 1.0f) : 0.0f;
    *fy = x->ns ? 0.0f : (a ? -1.0f : 1.0f);
    *mx = cx - *fx * rh + (-*fy) * h;
    *my = cy - *fy * rh + (*fx) * h;
}

/*  Append a point to a train's path ring. */
/*  A train car: 0.42 of a tile long (the user: "make the cars smaller",
 *  then "still too big"; a rigid car short enough for the art's
 *  one-tile bends), 0.10 wide, coupled 0.48 apart along the path, its
 *  bogies 0.14 each side of its centre. */
#define TRAIN_PITCH 0.48f
#define TRAIN_LEN   0.42f
#define TRAIN_WID   0.10f
#define TRAIN_BOGIE 0.14f

static int trail_push(RTrain *tr, float x, float y, float z, float hx, float hy, float d, int32_t seg, float s, int dir)
{
    RTrailPt *q;
    if (!tr->trail)
    {
        tr->trail_cap = 2048u;
        tr->trail     = (RTrailPt *)malloc(tr->trail_cap * sizeof *tr->trail);
        if (!tr->trail)
            return -1;
        tr->trail_n = tr->trail_head = 0;
    }
    tr->trail_head = tr->trail_n < tr->trail_cap ? tr->trail_n : (tr->trail_head + 1u) % tr->trail_cap;
    if (tr->trail_n < tr->trail_cap)
        ++tr->trail_n;
    q      = &tr->trail[tr->trail_n < tr->trail_cap ? tr->trail_n - 1u : (tr->trail_head + tr->trail_cap - 1u) % tr->trail_cap];
    q->x   = x;
    q->y   = y;
    q->z   = z;
    q->hx  = hx;
    q->hy  = hy;
    q->d   = d;
    q->s   = s;
    q->seg = seg;
    q->dir = dir;
    return 0;
}

/*  The ring's k-th newest point, 0 the newest. */
static const RTrailPt *trail_at(const RTrain *tr, uint32_t k)
{
    uint32_t idx;
    if (k >= tr->trail_n)
        k = tr->trail_n - 1u;
    if (tr->trail_n < tr->trail_cap)
        idx = tr->trail_n - 1u - k;
    else
        idx = (tr->trail_head + tr->trail_cap - 1u - k) % tr->trail_cap;
    return &tr->trail[idx];
}

/*  Where the train's car k stands: `back` tiles behind the engine along
 *  its path, interpolated between the two ring points around it. */
static void train_car_at(const RTrain *tr, float back, float *x, float *y, float *z, float *hx, float *hy)
{
    const RTrailPt *a    = trail_at(tr, 0), *b;
    float           want = tr->d - back, t;
    uint32_t        k;
    if (tr->trail_n == 0)
    {
        *x = *y = *z = *hx = *hy = 0.0f;
        return;
    }
    for (k = 1; k < tr->trail_n; ++k)
    {
        b = trail_at(tr, k);
        if (b->d <= want)
        {
            a   = b;
            b   = trail_at(tr, k - 1);
            t   = b->d > a->d ? (want - a->d) / (b->d - a->d) : 0.0f;
            *x  = a->x + (b->x - a->x) * t;
            *y  = a->y + (b->y - a->y) * t;
            *z  = a->z + (b->z - a->z) * t;
            *hx = a->hx + (b->hx - a->hx) * t;
            *hy = a->hy + (b->hy - a->hy) * t;
            return;
        }
        a = b;
    }
    *x  = a->x;
    *y  = a->y;
    *z  = a->z;
    *hx = a->hx;
    *hy = a->hy;
}

/*  The engine's place on the track: the band at s offset onto the track
 *  to the right of its direction of travel. */
static void train_place(const RRoadNet *net, const RTrain *tr, float *x, float *y, float *z, float *hx, float *hy)
{
    const RNetSeg *sg = &net->segs[tr->seg];
    float          bx, by, bz, dx, dy;
    net_at(net, sg, tr->s, &bx, &by, &bz, &dx, &dy);
    if (tr->dir < 0)
    {
        dx = -dx;
        dy = -dy;
    }
    *hx = dx;
    *hy = dy;
    *x  = bx - dy * sg->lane_out;
    *y  = by + dx * sg->lane_out;
    *z  = bz;
}

/*  Onward from a rail node: the arm that continues straightest. */
static int train_turn(const RRoadNet *net, int32_t nc, int32_t nr, int from_seg, float hx, float hy, int *out_seg, int *out_dir)
{
    float    best = -2.0f;
    uint32_t i;
    int      found = 0;
    for (i = 0; i < net->n_segs; ++i)
    {
        const RNetSeg *sg = &net->segs[i];
        int            e;
        for (e = 0; e < 2; ++e)
        {
            const RNetPt *q;
            float         dx, dy, dot;
            if (sg->node[e][0] != nc || sg->node[e][1] != nr)
                continue;
            if ((int)i == from_seg && net->n_segs > 1u)
                continue;
            q   = &net->pts[sg->first + (e == 0 ? 0u : sg->count - 1u)];
            dx  = e == 0 ? q->dx : -q->dx;
            dy  = e == 0 ? q->dy : -q->dy;
            dot = dx * hx + dy * hy;
            if (dot > best)
            {
                best     = dot;
                *out_seg = (int)i;
                *out_dir = e == 0 ? 1 : -1;
                found    = 1;
            }
        }
    }
    return found;
}

/*  The nearest station of the rail net to a point: its segment and s. */
static int railnet_nearest(const RRoadNet *net, float x, float y, int32_t *seg, float *s, float *dx, float *dy)
{
    float    best = 1e9f;
    uint32_t i;
    int      found = 0;
    for (i = 0; i < net->n_segs; ++i)
    {
        const RNetSeg *sg = &net->segs[i];
        uint32_t       j;
        for (j = 0; j < sg->count; ++j)
        {
            const RNetPt *q = &net->pts[sg->first + j];
            float         d = (q->x - x) * (q->x - x) + (q->y - y) * (q->y - y);
            if (d < best)
            {
                best  = d;
                *seg  = (int32_t)i;
                *s    = q->s;
                *dx   = q->dx;
                *dy   = q->dy;
                found = 1;
            }
        }
    }
    return found && best < 1.0f;
}

/*  Fill a train's path backward along its segment, so its cars start
 *  coupled behind the engine. */
static int train_prefill(RTrain *tr, const RRoadNet *net)
{
    float len   = (float)tr->n_cars * TRAIN_PITCH + 0.5f, back;
    tr->trail_n = tr->trail_head = 0;
    for (back = len; back >= 0.0f; back -= 0.05f)
    {
        RTrain probe = *tr;
        float  x, y, z, hx, hy;
        probe.s = tr->s - (float)tr->dir * back;
        if (probe.s < 0.0f)
            probe.s = 0.0f;
        if (probe.s > net->segs[tr->seg].total)
            probe.s = net->segs[tr->seg].total;
        train_place(net, &probe, &x, &y, &z, &hx, &hy);
        if (trail_push(tr, x, y, z, hx, hy, tr->d - back, tr->seg, probe.s, tr->dir) != 0)
            return -1;
    }
    return 0;
}

/*  The trains from the save: a run of consecutive records of types 10
 *  and 11 on adjacent tiles is one train, the type-10 engine its head
 *  (the user: "we need rendered train cars", "the old renderer
 *  technically collides trains"; spec 5.6, right-hand running). */
static int trains_init(RTraffic *t, const RMesh *m, const RCity *c)
{
    static int32_t  tile_of[R_MAX_THINGS];
    const RRoadNet *net = &m->railnet;
    int32_t         k, i, n = c->n_things;
    uint32_t        rng = 777u;
    if (n > R_MAX_THINGS)
        n = R_MAX_THINGS;
    for (i = 0; i < n; ++i)
        tile_of[i] = -1;
    for (k = 0; k < R_MAP * R_MAP; ++k)
    {
        int32_t v = c->xtxt[k];
        if (v >= 0xC9 && v <= 0xF0 && v - 0xC9 < n)
            tile_of[v - 0xC9] = k;
    }
    for (i = 0; i < n; ++i)
    {
        const uint8_t *rec = c->xthg + (size_t)i * 12u;
        int32_t        j, head = i, cars = 0;
        RTrain        *tr;
        float          hx, hy, sx, sy;
        int32_t        seg;
        float          s, dx, dy;
        if ((rec[0] != 10 && rec[0] != 11) || tile_of[i] < 0)
            continue;
        /* the run: adjacent tiles, consecutive indices */
        for (j = i; j < n; ++j)
        {
            const uint8_t *r2 = c->xthg + (size_t)j * 12u;
            if ((r2[0] != 10 && r2[0] != 11) || tile_of[j] < 0)
                break;
            if (j > i)
            {
                int32_t a = tile_of[j - 1], b = tile_of[j];
                if (abs((int)(a % R_MAP) - (int)(b % R_MAP)) + abs((int)(a / R_MAP) - (int)(b / R_MAP)) != 1)
                    break;
            }
            if (r2[0] == 10)
                head = j;
            ++cars;
        }
        if (cars > 32)
            cars = 32;
        {
            const uint8_t     *hr    = c->xthg + (size_t)head * 12u;
            static const float HX[8] = {0.0f, 0.707f, 1.0f, 0.707f, 0.0f, -0.707f, -1.0f, -0.707f};
            static const float HY[8] = {-1.0f, -0.707f, 0.0f, 0.707f, 1.0f, 0.707f, 0.0f, -0.707f};
            hx                       = HX[hr[1] & 7];
            hy                       = HY[hr[1] & 7];
            sx                       = (float)(tile_of[head] % R_MAP) + 0.5f;
            sy                       = (float)(tile_of[head] / R_MAP) + 0.5f;
        }
        if (railnet_nearest(net, sx, sy, &seg, &s, &dx, &dy))
        {
            RTrain *nt = (RTrain *)realloc(t->trains, (t->n_trains + 1u) * sizeof *nt);
            int     q;
            if (!nt)
                return -1;
            t->trains = nt;
            tr        = &t->trains[t->n_trains++];
            memset(tr, 0, sizeof *tr);
            tr->seg    = seg;
            tr->s      = s;
            tr->dir    = (hx * dx + hy * dy) >= 0.0f ? 1 : -1;
            tr->speed  = 2.4f + 0.6f * car_frand(&rng); /* tiles a second (the user: "they're slow af") */
            tr->n_cars = cars;
            tr->d      = 0.0f;
            tr->rng    = rng;
            for (q = 0; q < cars; ++q)
                tr->paint[q] = q == 0 ? 0.0f : (float)(1 + (int)(car_rand(&rng) % 3u));
            if (train_prefill(tr, net) != 0)
                return -1;
        }
        i += cars - 1;
    }
    t->gate = (float *)calloc(m->n_xings ? m->n_xings : 1u, sizeof *t->gate);
    /*  Each crossing's place on the road network, the segment through its
     *  tile and the distance along it, so the cars can stop at its line
     *  (the user: "they don't cause traffic to stop"). */
    t->xseg = (int32_t *)malloc((m->n_xings ? m->n_xings : 1u) * sizeof *t->xseg);
    t->xs   = (float *)malloc((m->n_xings ? m->n_xings : 1u) * sizeof *t->xs);
    if (t->xseg && t->xs)
    {
        uint32_t xi, xk, xj;
        for (xi = 0; xi < m->n_xings; ++xi)
        {
            const RXing *x    = &m->xings[xi];
            float        best = 0.25f; /* within half a tile of the crossing's centre */
            t->xseg[xi]       = -1;
            t->xs[xi]         = 0.0f;
            for (xk = 0; xk < m->net.n_segs; ++xk)
            {
                const RNetSeg *sg = &m->net.segs[xk];
                for (xj = 0; xj < sg->count; ++xj)
                {
                    const RNetPt *q  = &m->net.pts[sg->first + xj];
                    float         dx = q->x - ((float)x->col + 0.5f), dy = q->y - ((float)x->row + 0.5f), d = dx * dx + dy * dy;
                    if (d < best)
                    {
                        best        = d;
                        t->xseg[xi] = (int32_t)xk;
                        t->xs[xi]   = q->s;
                    }
                }
            }
        }
    }
    if (!t->gate)
        return -1;
    for (k = 0; k < (int32_t)m->n_xings; ++k)
        t->gate[k] = 88.0f;
    return 0;
}

static void trains_step(RTraffic *t, const RMesh *m, float dt)
{
    const RRoadNet *net = &m->railnet;
    uint32_t        i;
    for (i = 0; i < t->n_trains; ++i)
    {
        RTrain        *tr = &t->trains[i];
        const RNetSeg *sg;
        float          step = tr->speed * dt, x, y, z, hx, hy, to_end;
        int            end;
        if (tr->seg < 0 || (uint32_t)tr->seg >= net->n_segs)
            continue;
        sg     = &net->segs[tr->seg];
        end    = tr->dir > 0 ? 1 : 0;
        to_end = tr->dir > 0 ? sg->total - tr->s : tr->s;
        if (step >= to_end)
        {
            int nseg, ndir;
            train_place(net, tr, &x, &y, &z, &hx, &hy);
            if (sg->kind[end] == 2 && train_turn(net, sg->node[end][0], sg->node[end][1], tr->seg, hx, hy, &nseg, &ndir))
            {
                /* across the junction tile onto the next arm: the path point at the box's far side */
                RTrain probe = *tr;
                float  x1, y1, z1, h1x, h1y;
                probe.seg = nseg;
                probe.dir = ndir;
                probe.s   = ndir > 0 ? 0.0f : net->segs[nseg].total;
                train_place(net, &probe, &x1, &y1, &z1, &h1x, &h1y);
                tr->d += sqrtf((x1 - x) * (x1 - x) + (y1 - y) * (y1 - y)) + to_end;
                tr->seg = nseg;
                tr->dir = ndir;
                tr->s   = probe.s;
                trail_push(tr, x1, y1, z1, h1x, h1y, tr->d, tr->seg, tr->s, tr->dir);
                continue;
            }
            /* a dead end or a carrier: the train reverses, its tail the new head */
            {
                float px[32], py[32], pz[32], phx[32], phy[32];
                int   q, nc = tr->n_cars;
                for (q = 0; q < nc; ++q)
                    train_car_at(tr, (float)q * TRAIN_PITCH, &px[q], &py[q], &pz[q], &phx[q], &phy[q]);
                tr->trail_n = tr->trail_head = 0;
                tr->d                        = 0.0f;
                for (q = 0; q < nc; ++q)
                {
                    int r = nc - 1 - q; /* the old tail first, the old head last */
                    trail_push(tr, px[r], py[r], pz[r], -phx[r], -phy[r], (float)q * TRAIN_PITCH, tr->seg, tr->s, -tr->dir);
                }
                tr->d = (float)(nc - 1) * TRAIN_PITCH;
                /* the new head: the old tail's place on the track, found afresh */
                {
                    int32_t seg2;
                    float   s2, dx2, dy2;
                    if (railnet_nearest(net, px[nc - 1], py[nc - 1], &seg2, &s2, &dx2, &dy2))
                    {
                        tr->seg = seg2;
                        tr->s   = s2;
                        tr->dir = (-phx[nc - 1] * dx2 - phy[nc - 1] * dy2) >= 0.0f ? 1 : -1;
                    }
                    else
                        tr->dir = -tr->dir;
                }
                continue;
            }
        }
        tr->s += (float)tr->dir * step;
        tr->d += step;
        train_place(net, tr, &x, &y, &z, &hx, &hy);
        if (tr->trail_n == 0 || tr->d - trail_at(tr, 0)->d >= 0.03f)
            trail_push(tr, x, y, z, hx, hy, tr->d, tr->seg, tr->s, tr->dir);
    }
}

/*  Whether any train car stands within `reach` tiles of a crossing
 *  along the rail's axis, on either track (spec 3.15: gates stay down
 *  while any track's approach is occupied). */
static int xing_occupied(const RTraffic *t, const RXing *x, float reach)
{
    float    cx = (float)x->col + 0.5f, cy = (float)x->row + 0.5f;
    uint32_t i;
    for (i = 0; i < t->n_trains; ++i)
    {
        const RTrain *tr = &t->trains[i];
        int           q;
        for (q = 0; q < tr->n_cars; ++q)
        {
            float px, py, pz, hx, hy;
            train_car_at(tr, (float)q * TRAIN_PITCH, &px, &py, &pz, &hx, &hy);
            if (x->ns ? (fabsf(px - cx) < 0.6f && fabsf(py - cy) < reach) : (fabsf(py - cy) < 0.6f && fabsf(px - cx) < reach))
                return 1;
        }
    }
    return 0;
}

static void gates_step(RTraffic *t, const RMesh *m, float dt)
{
    uint32_t i;
    if (!t->gate)
        return;
    for (i = 0; i < m->n_xings; ++i)
    {
        int   down = xing_occupied(t, &m->xings[i], 8.0f); /* eight tiles: three seconds of warning at the trains' speed */
        float a    = t->gate[i];
        if (down)
            a -= 44.0f * dt; /* down in two seconds */
        else
            a += 30.0f * dt; /* up in three */
        t->gate[i] = a < 0.0f ? 0.0f : a > 88.0f ? 88.0f
                                                 : a;
    }
}

/*  The gates' moving parts on one approach: the flashers, lit while the
 *  arm is off its rest, and the striped arm at its angle about the
 *  mechanism's shaft, 1.0 m up, swung across the approach lane. */
static int put_gate_state(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy, float angle, float arm_len, float time)
{
    float g  = surface_at_world(c, mask_bit, x, y);
    float bx = -fx, by = -fy;
    float wu = -by, wv = bx;
    float ph  = (float)(((int)(x * 3.0f) + (int)(y * 5.0f)) & 7) / 8.0f;
    int   lit = angle < 87.5f;
    float px = x + wu * 0.045f, py = y + wv * 0.045f;
    float ca = cosf(angle * 3.14159265f / 180.0f), sa = sinf(angle * 3.14159265f / 180.0f);
    float tx = px + wu * arm_len * ca, ty = py + wv * arm_len * ca, tz = g + 0.13f + arm_len * sa * 0.53f;
    float t0[3] = {px, py, g + 0.13f + 0.006f}, t1[3] = {tx, ty, tz + 0.006f};
    float b0[3] = {px, py, g + 0.13f - 0.006f}, b1[3] = {tx, ty, tz - 0.006f};
    float nrm[3] = {bx, by, 0.0f};
    float col[3] = {11.0f, 0.0f, MAT_LAMP};
    (void)time;
    if (put_lamp_face(m, order, x + bx * 0.012f + wu * 0.045f, y + by * 0.012f + wv * 0.045f, g, 0.293f, bx, by, 0.011f, ph, lit ? 8.0f : 12.0f) != 0 ||
        put_lamp_face(m, order, x + bx * 0.012f - wu * 0.045f, y + by * 0.012f - wv * 0.045f, g, 0.293f, bx, by, 0.011f, ph, lit ? 9.0f : 12.0f) != 0)
        return -1;
    /* the arm: a ribbon seen from the approach and one seen from above */
    if (put_wall_r2(m, t0, t1, b0, b1, nrm, order + 0.05f, col, 0.0f, 0.0f, 0.0f, 0.0f) != 0)
        return -1;
    {
        float l0[3] = {px - bx * 0.006f, py - by * 0.006f, g + 0.13f}, l1[3] = {tx - bx * 0.006f, ty - by * 0.006f, tz};
        float r0[3] = {px + bx * 0.006f, py + by * 0.006f, g + 0.13f}, r1[3] = {tx + bx * 0.006f, ty + by * 0.006f, tz};
        float up[3] = {0.0f, 0.0f, 1.0f};
        if (put_wall_r2(m, l0, l1, r0, r1, up, order + 0.05f, col, 0.0f, 0.0f, 0.0f, 0.0f) != 0)
            return -1;
    }
    return 0;
}

int r_traffic_init(RTraffic *t, const RMesh *m, const RCity *c)
{
    const RRoadNet *net = &m->net;
    uint32_t        i, total_cap = 2500u;
    uint32_t        rng = 12345u;
    r_traffic_free(t);
    for (i = 0; i < net->n_segs; ++i)
    {
        const RNetSeg *sg = &net->segs[i];
        int            n1 = 0, n2 = 0, k, cars;
        int32_t        lc = -1, lr = -1;
        uint32_t       j;
        /* the original's density: a tile whose traffic passes 0x55 carries a car, past 0xAA two */
        for (j = 0; j < sg->count; ++j)
        {
            const RNetPt *q  = &net->pts[sg->first + j];
            int32_t       tc = (int32_t)floorf(q->x), tr = (int32_t)floorf(q->y);
            int32_t       tv;
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP || (tc == lc && tr == lr))
                continue;
            lc = tc;
            lr = tr;
            tv = c->xtrf[(tr >> 1) * R_HALF + (tc >> 1)];
            if (tv > 0x55)
                ++n1;
            if (tv > 0xAA)
                ++n2;
        }
        cars = n1 + n2;
        if (cars > (int)(sg->total * 1.5f) + 1)
            cars = (int)(sg->total * 1.5f) + 1;
        for (k = 0; k < cars && t->n < total_cap; ++k)
        {
            RCar *car;
            if (t->n + 1u > t->cap)
            {
                uint32_t nc = t->cap ? t->cap * 2u : 512u;
                RCar    *nl = (RCar *)realloc(t->cars, nc * sizeof *nl);
                if (!nl)
                    return -1;
                t->cars = nl;
                t->cap  = nc;
            }
            car = &t->cars[t->n++];
            memset(car, 0, sizeof *car);
            car->seg   = (int32_t)i;
            car->s     = car_frand(&rng) * sg->total;
            car->dir   = (k & 1) ? -1 : 1;
            car->lane  = (sg->cls > 0 && (k % 3) == 2) ? 1 : 0;
            car->speed = 0.8f + 0.5f * car_frand(&rng); /* tiles a second */
            car->paint = 4.0f + (float)(car_rand(&rng) % 5u);
            car->rng   = rng ^ ((uint32_t)k * 2654435761u);
        }
    }
    return trains_init(t, m, c);
}

void r_traffic_free(RTraffic *t)
{
    uint32_t i;
    free(t->cars);
    for (i = 0; i < t->n_trains; ++i)
        free(t->trains[i].trail);
    free(t->trains);
    free(t->gate);
    free(t->xseg);
    free(t->xs);
    r_mesh_free(&t->scratch);
    t->cars   = NULL;
    t->trains = NULL;
    t->gate   = NULL;
    t->xseg   = NULL;
    t->xs     = NULL;
    t->n = t->cap = t->n_trains = 0;
}

static int car_order_cmp(const void *a, const void *b)
{
    const RCar *x = (const RCar *)a, *y = (const RCar *)b;
    if (x->seg != y->seg)
        return x->seg < y->seg ? -1 : 1;
    if (x->dir != y->dir)
        return x->dir < y->dir ? -1 : 1;
    if (x->lane != y->lane)
        return x->lane < y->lane ? -1 : 1;
    return x->s < y->s ? -1 : x->s > y->s ? 1
                                          : 0;
}

/*  Onward from a junction: a random arm at the node other than the one
 *  arrived by, unless it is the only one. */
static int car_turn(const RRoadNet *net, RCar *car, int32_t nc, int32_t nr, int from_seg, int *out_seg, int *out_dir)
{
    int      cand[16], cdir[16], n = 0, pick;
    uint32_t i;
    for (i = 0; i < net->n_segs && n < 16; ++i)
    {
        const RNetSeg *sg = &net->segs[i];
        if ((int)i == from_seg && net->n_segs > 1u)
            continue;
        if (sg->node[0][0] == nc && sg->node[0][1] == nr && sg->kind[0] == 2)
        {
            cand[n] = (int)i;
            cdir[n] = 1;
            ++n;
        }
        else if (sg->node[1][0] == nc && sg->node[1][1] == nr && sg->kind[1] == 2)
        {
            cand[n] = (int)i;
            cdir[n] = -1;
            ++n;
        }
    }
    if (!n)
        return 0;
    pick     = (int)(car_rand(&car->rng) % (uint32_t)n);
    *out_seg = cand[pick];
    *out_dir = cdir[pick];
    return 1;
}

static int s_xing_held;

void r_traffic_step(RTraffic *t, const RMesh *m, float dt, float time)
{
    const RRoadNet *net = &m->net;
    uint32_t        i;
    if (dt > 0.1f)
        dt = 0.1f;
    trains_step(t, m, dt);
    gates_step(t, m, dt);
    if (getenv("SC2K_XING_DEBUG"))
    {
        /* SC2K_XING_DEBUG=1: per step, the crossings with their gates off rest and the cars held at a line */
        uint32_t q, down = 0;
        for (q = 0; q < m->n_xings && t->gate; ++q)
            if (t->gate[q] < 87.5f)
            {
                if (down < 4)
                    fprintf(stderr, "xing down: c%d r%d angle %.0f\n", (int)m->xings[q].col, (int)m->xings[q].row, (double)t->gate[q]);
                ++down;
            }
        fprintf(stderr, "xing: t %.2f gates down %u cars held %d\n", (double)time, down, s_xing_held);
    }
    s_xing_held = 0;
    if (!t->n || !net->n_segs)
        return;
    qsort(t->cars, t->n, sizeof *t->cars, car_order_cmp);
    for (i = 0; i < t->n; ++i)
    {
        RCar          *car = &t->cars[i];
        const RNetSeg *sg;
        float          to_end, v = car->speed, x, y, z, hx, hy;
        int            end;
        if (car->in_box)
        {
            car->bt += v * dt;
            if (car->bt >= car->blen)
            {
                car->in_box = 0;
                car->hold   = 0.0f;
                car->seg    = car->next_seg;
                car->dir    = car->next_dir;
                sg          = &net->segs[car->seg];
                car->s      = car->dir > 0 ? 0.0f : sg->total;
            }
            continue;
        }
        if (car->seg < 0 || (uint32_t)car->seg >= net->n_segs)
            continue;
        sg     = &net->segs[car->seg];
        end    = car->dir > 0 ? 1 : 0;
        to_end = car->dir > 0 ? sg->total - car->s : car->s;
        /* the car ahead in the same lane: the next in the sorted order, forward, or the previous, back */
        {
            uint32_t j = car->dir > 0 ? i + 1u : i - 1u;
            if ((car->dir > 0 ? i + 1u < t->n : i > 0))
            {
                const RCar *o = &t->cars[j];
                if (o->seg == car->seg && o->dir == car->dir && o->lane == car->lane && !o->in_box)
                {
                    float gap = fabsf(o->s - car->s);
                    if (gap < 0.42f)
                        v = 0.0f;
                    else if (gap < 0.8f)
                        v = v * (gap - 0.42f) / 0.38f;
                }
            }
        }
        /* the control at the junction ahead (spec 3.4): a signal holds the car at the
         * line while it is not green; a stop sign holds it a second, then it goes */
        if (sg->kind[end] == 2 && sg->ctrl[end] && to_end <= 0.47f)
        {
            int hold = 0;
            if (sg->ctrl[end] == 2)
            {
                car_place(net, car, &x, &y, &z, &hx, &hy);
                hold = signal_red(sg->node[end][0], sg->node[end][1], hx, hy, time);
            }
            else if (car->hold < 1.0f)
            {
                hold = 1;
                if (to_end <= 0.45f)
                    car->hold += dt;
            }
            if (hold)
            {
                if (to_end <= 0.45f)
                    v = 0.0f;
                else if (v * dt > to_end - 0.45f)
                    v = (to_end - 0.45f) / dt;
            }
        }
        /* a level crossing ahead on this segment (spec 3.15): while its flashers are on
         * the car stops with its front at the stop line, 4.5 m before the nearest rail */
        if (t->xseg && t->xs && t->gate)
        {
            uint32_t q;
            for (q = 0; q < m->n_xings; ++q)
            {
                float ahead;
                if (t->xseg[q] != car->seg || t->gate[q] >= 87.5f)
                    continue;
                ahead = (t->xs[q] - (float)car->dir * 0.556f - car->s) * (float)car->dir;
                if (ahead < -0.02f || ahead > 0.8f)
                    continue;
                if (ahead <= 0.02f)
                {
                    v = 0.0f;
                    ++s_xing_held; /* SC2K_XING_DEBUG counts these per step */
                }
                else if (v * dt > ahead - 0.02f)
                    v = (ahead - 0.02f) / dt;
            }
        }
        car->s += (float)car->dir * v * dt;
        to_end = car->dir > 0 ? sg->total - car->s : car->s;
        if (to_end <= 0.0f)
        {
            if (sg->kind[end] == 2)
            {
                int nseg, ndir;
                if (car_turn(net, car, sg->node[end][0], sg->node[end][1], car->seg, &nseg, &ndir))
                {
                    const RNetSeg *ng    = &net->segs[nseg];
                    RCar           probe = *car;
                    float          x1, y1, z1, h1x, h1y;
                    car->s = car->dir > 0 ? sg->total : 0.0f;
                    car_place(net, car, &car->bx0, &car->by0, &car->bz0, &hx, &hy);
                    probe.seg = nseg;
                    probe.dir = ndir;
                    probe.s   = ndir > 0 ? 0.0f : ng->total;
                    car_place(net, &probe, &x1, &y1, &z1, &h1x, &h1y);
                    car->bx1      = x1;
                    car->by1      = y1;
                    car->bz1      = z1;
                    car->blen     = sqrtf((x1 - car->bx0) * (x1 - car->bx0) + (y1 - car->by0) * (y1 - car->by0));
                    car->bt       = 0.0f;
                    car->in_box   = 1;
                    car->next_seg = nseg;
                    car->next_dir = ndir;
                    if (car->blen < 1e-4f)
                        car->blen = 1e-4f;
                    continue;
                }
            }
            /* a dead end, the map's edge, a bridge or a tunnel: turn back */
            car->hold = 0.0f;
            car->dir  = -car->dir;
            car->s    = car->dir > 0 ? 0.0f : sg->total;
        }
    }
}

/*  The cars as geometry (spec 6, a car 4.5 by 1.8 m, 1.5 m tall, at the
 *  road's scale of about seventeen metres to the tile): a body and a
 *  cabin set back on it, painted by the car. */
int r_traffic_build(RTraffic *t, const RMesh *m, const RCity *c)
{
    const RRoadNet *net      = &m->net;
    const uint8_t   mask_bit = r_city_corner_mask(c->rotation);
    uint32_t        i;
    t->scratch.n_land  = 0;
    t->scratch.n_water = 0;
    for (i = 0; i < t->n; ++i)
    {
        const RCar *car = &t->cars[i];
        float       x, y, z, hx, hy, order;
        int32_t     tc, tr;
        if (car->in_box)
        {
            float f = car->blen > 0.0f ? car->bt / car->blen : 1.0f;
            x       = car->bx0 + (car->bx1 - car->bx0) * f;
            y       = car->by0 + (car->by1 - car->by0) * f;
            z       = car->bz0 + (car->bz1 - car->bz0) * f;
            hx      = car->bx1 - car->bx0;
            hy      = car->by1 - car->by0;
            {
                float l = sqrtf(hx * hx + hy * hy);
                if (l > 1e-5f)
                {
                    hx /= l;
                    hy /= l;
                }
                else
                {
                    hx = 1.0f;
                    hy = 0.0f;
                }
            }
        }
        else
            car_place(net, car, &x, &y, &z, &hx, &hy);
        tc = (int32_t)floorf(x);
        tr = (int32_t)floorf(y);
        if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
            continue;
        order = tile_order(c, tc, tr, mask_bit) + 0.3f;
        /* the grade: the band's height a car's half length behind and ahead */
        {
            float zb = z, zf = z;
            if (!car->in_box)
            {
                RCar  probe = *car;
                float px, py, pz, phx, phy;
                probe.s = car->s - (float)car->dir * 0.075f;
                car_place(net, &probe, &px, &py, &pz, &phx, &phy);
                zb      = pz;
                probe.s = car->s + (float)car->dir * 0.075f;
                car_place(net, &probe, &px, &py, &pz, &phx, &phy);
                zf = pz;
            }
            /* the user: "the cars are still too huge": a body 0.15 by 0.065 of a tile, a low cabin set back */
            if (put_prism_clip(&t->scratch, c, mask_bit, order, x, y, hx, hy, 0.15f, 0.065f, zb + 0.005f, zf + 0.005f, 0.0f, 0.045f, car->paint) != 0)
                return -1;
            if (put_prism_clip(&t->scratch, c, mask_bit, order, x - hx * 0.012f, y - hy * 0.012f, hx, hy, 0.07f, 0.055f, zb + (zf - zb) * 0.27f + 0.005f, zb + (zf - zb) * 0.73f + 0.005f, 0.045f, 0.085f, car->paint) != 0)
                return -1;
        }
    }
    /* the trains: each car on the engine's path, coupled a tenth of a tile apart */
    for (i = 0; i < t->n_trains; ++i)
    {
        const RTrain *tr = &t->trains[i];
        int           q;
        for (q = 0; q < tr->n_cars; ++q)
        {
            /*  A rigid car (the user: "since when do trains curve??") on
             *  the chord between its two bogies.  The art's one-tile
             *  bends and half circles are under any radius the spec
             *  allows a class (5.4: 1.5 tiles for a yard); a car of 0.85
             *  cut the inside of such a bend and its neighbours, so the
             *  cars are short (the user: "make the cars smaller").  Set
             *  along the tangent at its centre instead a car had swung
             *  its ends out and into its neighbours. */
            float   back = (float)q * TRAIN_PITCH, x, y, hx, hy, order, l, slope, zc;
            float   bx2, by2, bz2, bhx, bhy, fx2, fy2, fz2, fhx, fhy;
            int32_t tc, tr2;
            train_car_at(tr, back + TRAIN_BOGIE, &bx2, &by2, &bz2, &bhx, &bhy);
            train_car_at(tr, back - TRAIN_BOGIE, &fx2, &fy2, &fz2, &fhx, &fhy);
            hx = fx2 - bx2;
            hy = fy2 - by2;
            l  = sqrtf(hx * hx + hy * hy);
            if (l < 1e-4f)
                continue;
            hx /= l;
            hy /= l;
            x     = 0.5f * (bx2 + fx2);
            y     = 0.5f * (by2 + fy2);
            zc    = 0.5f * (bz2 + fz2);
            slope = (fz2 - bz2) / l;
            tc    = (int32_t)floorf(x);
            tr2   = (int32_t)floorf(y);
            if (tc < 0 || tr2 < 0 || tc >= R_MAP || tr2 >= R_MAP)
                continue;
            order = tile_order(c, tc, tr2, mask_bit) + 0.3f;
            if (put_prism_clip(&t->scratch, c, mask_bit, order, x, y, hx, hy, TRAIN_LEN, TRAIN_WID, zc - slope * 0.5f * TRAIN_LEN + 0.02f, zc + slope * 0.5f * TRAIN_LEN + 0.02f, 0.0f, q == 0 ? 0.26f : 0.24f, tr->paint[q]) != 0)
                return -1;
        }
    }
    /* the rail signals' aspects (spec 5.6): red while any car of a train stands in the
     * block ahead of the signal in the direction it governs, ten tiles, green otherwise */
    for (i = 0; i < m->n_rsigs; ++i)
    {
        const RRailSig *sg2 = &m->rsigs[i];
        float           g   = surface_at_world(c, mask_bit, sg2->x, sg2->y);
        int32_t         tc = (int32_t)floorf(sg2->x), tr2 = (int32_t)floorf(sg2->y);
        int             red = 0;
        uint32_t        k;
        if (tc < 0 || tr2 < 0 || tc >= R_MAP || tr2 >= R_MAP)
            continue;
        for (k = 0; k < t->n_trains && !red; ++k)
        {
            const RTrain *tr = &t->trains[k];
            int           q;
            for (q = 0; q < tr->n_cars && !red; ++q)
            {
                const RTrailPt *pt;
                float           back = (float)q * TRAIN_PITCH, want = tr->d - back;
                uint32_t        j;
                /* the car's segment and distance: the ring point just behind it */
                for (j = 0; j < tr->trail_n; ++j)
                {
                    pt = trail_at(tr, j);
                    if (pt->d <= want)
                        break;
                }
                if (j >= tr->trail_n)
                    continue;
                if (pt->seg == sg2->seg)
                {
                    float ahead = (float)sg2->dir * (pt->s - sg2->s);
                    if (ahead > -0.6f && ahead < 10.0f)
                        red = 1;
                }
            }
        }
        if (put_lamp_face(&t->scratch, tile_order(c, tc, tr2, mask_bit) + 0.3f, sg2->x + sg2->fx * 0.015f, sg2->y + sg2->fy * 0.015f, g, 0.56f, sg2->fx, sg2->fy, 0.014f, 0.0f, red ? 7.0f : 6.0f) != 0)
            return -1;
    }
    /* the crossings' gates and flashers */
    for (i = 0; i < m->n_xings && t->gate; ++i)
    {
        const RXing *x     = &m->xings[i];
        float        order = tile_order(c, x->col, x->row, mask_bit) + 0.3f;
        int          a;
        for (a = 0; a < 2; ++a)
        {
            float mx, my, fx, fy;
            xing_approach(x, a, &mx, &my, &fx, &fy);
            if (put_gate_state(&t->scratch, c, mask_bit, order, mx, my, fx, fy, t->gate[i], road_class(c, x->col, x->row) > 0.5f ? 0.36f : 0.29f, 0.0f) != 0)
                return -1;
        }
    }
    return 0;
}
