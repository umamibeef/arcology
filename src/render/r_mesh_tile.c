/*  r_mesh_tile.c -- what a tile is, and the ground field.
 *
 *  Split out of r_mesh.c; see r_mesh_int.h.
 */
#include "r_mesh_int.h"

/*  The tables and the field the other pieces read; the state the
 *  segment pipeline carries across its stages. */
const uint8_t CODE_MASK[14] = {0, 9, 3, 6, 12, 11, 7, 14, 13, 1, 2, 4, 8, 5};
const int EDGE_A[4] = {NW, NE, SW, SW};
const int EDGE_B[4] = {NE, SE, SE, NW};
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
float s_h[GRID * GRID]; /* the ground: one height per corner      */
float s_k[GRID * GRID]; /* curvature: positive in a hollow        */
float s_b[GRID * GRID]; /* the bed: the seabed at a corner that   */
float          s_road_class;
float          s_seg_class = -1.0f;        /* the class of the segment being lofted, or -1 for the tile's */
int32_t        s_seg_node[2][2];           /* the node tiles of the segment being lofted */
int            s_seg_ctrl[2];              /* the control of the arm at each end of the segment being lofted */
uint8_t        s_junc_ctrl[R_MAP * R_MAP]; /* per junction tile, two bits per arm: 0 none, 1 stop, 2 signal */
int            s_seg_kind[2];
const uint8_t *s_check_xbld; /* the last built city's XBLD, for the piece scan */ /* the class of the road strip being emitted */

/* ---- what a tile is ---------------------------------------------------- */

/*  Water: XTER 0x10 and up.  Submerged and shore slopes, 0x10..0x2F, are
 *  a body at ALTM's table over a bed at ALTM's level; streams, canals
 *  and the waterfall, 0x30 on, have their table at their level.  The
 *  original draws every one flat at its table (tile_alt in r_soft.c):
 *  the low nibble says where the art puts its rim, not a height.  A
 *  stream with a slope nibble among flat neighbours, Bay View's column
 *  110, row 19, drew as a bump while it was read as a height. */
int is_water(uint8_t xter)
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

int32_t slope_code(uint8_t xter)
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
int32_t corner_gi(int32_t col, int32_t row, int k)
{
    return (row + ((k == SW || k == SE) ? 1 : 0)) * GRID + col +
           ((k == NE || k == SE) ? 1 : 0);
}

/*  A network piece on a saddle, terrain code 13, is drawn one step up
 *  by the original ($17528: `cmpi.w #$d`, then -12), and only there,
 *  and never an elevated piece. */
int saddle_lift(const RCity *c, int32_t idx)
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


/*  Rule 2: what a tile draws, its four corner heights in the enum's
 *  order.  Both sides of every edge go through here. */
Kind tile_top(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float z[4])
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
int water_top(const RCity *c, int32_t idx, Kind k)
{
    return k == T_WATER || (k == T_PAD && is_water(c->xter[idx]));
}

/*  The painter's index of the sweep, so the mesh composes with the
 *  sprites exactly as the software terrain pass does.  A building's
 *  footprint takes its anchor's, as the elevated 2x2 pieces do
 *  ($173B8), so the art stays in front of its own pad. */
float tile_order(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit)
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
void build_field(const RCity *c)
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
