/*  mesh_net.c -- roads, rails and power lines: the pieces, and the furniture.
 *
 *  Split out of mesh.c; see mesh_int.h.
 */
#include "mesh_int.h"

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

/*  A road's class, from the traffic on it (the user: "avenues,
 *  boulevards, etc where appropriate"): 0 a two-lane road, 1 an avenue
 *  with a double centre line and four lanes, 2 a boulevard with a
 *  planted median.  Carried to the material in the normal's fourth
 *  component, where a ground vertex carries its curvature. */
float road_class(const RCity *c, int32_t col, int32_t row)
{
    uint8_t t = c->xtrf[(row >> 1) * R_HALF + (col >> 1)];
    return t >= 160u ? 2.0f : t >= 64u ? 1.0f
                                       : 0.0f;
}

/*  The family of a piece and its index in the shared layout, 0..14, or
 *  -1 for anything else.  A crossing answers for its road (0x44, 0x45,
 *  0x46) or its rail (0x47) with the straight piece along the right
 *  axis; road_second() gives the other family on it. */
int piece_family(uint8_t b, Family *f)
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
     *  the shipped cities, 2 September 2026): 0x43 a road east-west under a power
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
    /*  What runs under a viaduct is drawn too (the user: "a part of the
     *  highway is exposed with no road"): the deck's tile carried no
     *  surface at all, so a road or a line vanished under every elevated
     *  crossing.  Read off the shipped cities the way the crossings were: 0x4B
     *  spans a north-south road (545 of 579), 0x4C an east-west one
     *  (652 of 674), 0x4D a north-south rail (52 of 54), 0x4E an
     *  east-west one (37 of 41). */
    if (b == 0x4Bu)
    {
        *f = F_ROAD;
        return 1;
    }
    if (b == 0x4Cu)
    {
        *f = F_ROAD;
        return 0;
    }
    if (b == 0x4Du)
    {
        *f = F_RAIL;
        return 1;
    }
    if (b == 0x4Eu)
    {
        *f = F_RAIL;
        return 0;
    }
    /*  Four more rail straights, and they are NOT in the 0x2C..0x3A run:
     *  0x3B and 0x3D go north-south, 0x3C and 0x3E east-west.  Read off
     *  the shipped cities the same way the crossings were -- of 37 tiles of
     *  0x3B, 29 join north and south and five join one of them; 0x3C is
     *  40 of 54 east-west; and so on.  (Two ids to an axis because the
     *  art has two elevations of trestle.)
     *
     *  Missing them did not merely leave a gap.  A tile no family claims
     *  is not treated as bare ground -- it falls through to the BUILDING
     *  path and is given a levelled pad, so a rail tile in the middle of
     *  a line became a raised slab with the track drawn on top of it and
     *  the ground either side untouched: a piece of track hanging in the
     *  air (the user, on Toronto column 110 row 101).  344 tiles across
     *  the shipped cities, which is why it turned up everywhere. */
    /*  Rail under a HIGHWAY, the same shape as 0x47/0x48 under a power
     *  line: 0x4D carries the rail north-south, 0x4E east-west.  They
     *  come in pairs, one per tile of the highway's two-tile width, so
     *  each sees rail on one side and its partner on the other -- of 62
     *  tiles of 0x4D, 61 touch rail and every one of them touches
     *  another 0x4D.
     *
     *  Left out, the rail simply stopped where a highway crossed it and
     *  a bare embankment pad sat in the gap: 131 tiles of it across the
     *  the shipped cities. */
    if (b == 0x4Du)
    {
        *f = F_RAIL;
        return 1; /* north-south */
    }
    if (b == 0x4Eu)
    {
        *f = F_RAIL;
        return 0; /* east-west */
    }
    if (b == 0x3Bu || b == 0x3Du)
    {
        *f = F_RAIL;
        return 1; /* north-south, as 0x48 is */
    }
    if (b == 0x3Cu || b == 0x3Eu)
    {
        *f = F_RAIL;
        return 0; /* east-west, as 0x47 is */
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
int piece_second(uint8_t b, Family *f)
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
float width_factor(float dx, float dy, int compensate)
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

const float ROAD_MU[4] = {0.5f, 1.0f, 0.5f, 0.0f}; /* edge midpoints, N E S W */
const float ROAD_MV[4] = {0.0f, 0.5f, 1.0f, 0.5f};
const float ROAD_DU[4] = {0.0f, 1.0f, 0.0f, -1.0f}; /* out through the edge */
const float ROAD_DV[4] = {-1.0f, 0.0f, 1.0f, 0.0f};

/*  Which edges a piece joins, from the road art: the asphalt (palette
 *  0x91) or a dash (0x8B) in the three-by-three around each edge's
 *  midpoint, a tenth of the way in.  The slope pieces' art is tall; they
 *  are straight along their slope, which the terrain code says. */
int piece_links(const RAtlasLevel *l, int piece, uint8_t xter)
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
    t = atlas_tile(l, l->id_base + 0x1D + piece);
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
int tile_links(const RCity *c, const RAtlasLevel *l, int32_t col, int32_t row, Family want)
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

int link_count(int links)
{
    return (links & 1) + ((links >> 1) & 1) + ((links >> 2) & 1) + ((links >> 3) & 1);
}

/*  The links a tile can actually follow: those its neighbour returns,
 *  and those that leave the map.  A piece whose art points at grass, a
 *  stub against the flat side of a T, a road bulldozed short: the
 *  original draws every such sprite as it is, so the segment must end
 *  there, at a butt end, rather than the tile going undrawn because
 *  the chain it lies on has no node to be walked from. */
int eff_links(const RCity *c, const RAtlasLevel *l, int32_t col, int32_t row, Family f)
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

/*  On a level crossing, the rail is drawn as its rails alone -- the
 *  crossing's own surface stands in for the ballast -- but only where
 *  that surface actually is, which is the width of the road it crosses.
 *  Beyond the asphalt the railway is a railway again: gravel and
 *  sleepers, right up to the road (the user: "the gravel does *not* run
 *  up to the road").  The road's own line through the tile says where
 *  that is. */
static int on_crossing_panel(const RCity *c, int32_t tc, int32_t tr, float x, float y)
{
    const RCross *xr;
    uint8_t       b = c->xbld[tr * R_MAP + tc];
    float         dx, dy, across;
    if (b != 0x45u && b != 0x46u)
        return 0;
    xr = &s_cross[FAMX(F_ROAD)][tr * R_MAP + tc];
    if (!xr->have)
        return 1; /* no line to measure against: the tile, as it was */
    dx     = x - xr->x;
    dy     = y - xr->y;
    across = fabsf(dx * -xr->dy + dy * xr->dx);
    return across <= ROAD_W * 0.5f;
}

int put_tri_road_n(RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float *nrm, const float col[3], const float ref[3], const float ref2[3])
{
    /*  With the curves shown, the road itself gets out of the way: only
     *  the fitted centreline and the marks at its piece boundaries are
     *  drawn, over bare ground (the user: "curves are not visible - maybe
     *  hide the roads when you have that enabled").  Vehicles carry the
     *  overlay, so they are what survives the filter. */
    if (s_tune.show_curves > 0.5f && col[2] < 14.5f && col[2] > 6.5f)
        return 0;
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
            if (col[2] > 10.5f && col[2] < 11.5f && (b == 0x45u || b == 0x46u) &&
                on_crossing_panel(c, tc, tr, (tri[0][0] + tri[1][0] + tri[2][0]) / 3.0f, (tri[0][1] + tri[1][1] + tri[2][1]) / 3.0f))
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
        if (col[2] > 10.5f && col[2] < 11.5f && on_crossing_panel(c, tc, tr, cx, cy))
            pcol = xcol;
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

int strip_quad_z(RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float a0[2], const float a1[2], const float b0[2], const float b1[2], float za, float zb, float across0, float across1, float along_a, float along_b, float mat)
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

int strip_quad(RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float a0[2], const float a1[2], const float b0[2], const float b1[2], float across0, float across1, float along_a, float along_b, float mat)
{
    return strip_quad_z(m, c, mask_bit, order, a0, a1, b0, b1, -1.0f, -1.0f, across0, across1, along_a, along_b, mat);
}

/*  A fan of triangles about a point, from angle t0 at radius r0 to t1
 *  at r1, one material; the rim carries across 1, the apex `across_c`. */
/*  `lift` is added to the ground under each vertex, so a cap on a slope
 *  lies on the slope (set flat at its centre's height, the uphill half
 *  of a dead end's cap on a slope piece lay under the ground). */
int strip_fan_z(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float t0, float t1, float r0, float r1, float across_c, float mat, int n, float lift)
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
        tri[0][2] = surface_at_world(c, mask_bit, tri[0][0], tri[0][1]) + lift;
        tri[1][2] = surface_at_world(c, mask_bit, tri[1][0], tri[1][1]) + lift;
        tri[2][2] = surface_at_world(c, mask_bit, tri[2][0], tri[2][1]) + lift;
        if (put_tri_road(m, c, mask_bit, order, (const float (*)[3])tri, road_col, ref, ref2) != 0)
            return -1;
    }
    return 0;
}

/*  A box standing on the surface at world (x, y), `w` by `d` in tiles,
 *  from `z0` to `z1` levels above the ground there: four sides and a
 *  top, one material, `phase` in col.r. */
/*  A round column (the user: "I want the pillars to be round"): a
 *  twelve-sided prism of radius r about (cx, cy), its top flat at z1
 *  over the ground under its centre and its foot draped on the terrain,
 *  each base vertex at the ground under itself, so the column meets a
 *  slope along its whole rim instead of hanging over the downhill side
 *  or burying a foot that then paints over the grass in front of it.
 *  Every face goes through the tile clipper: a column crossing a tile
 *  line with one painter's order was banded by the ground either side. */
int put_cyl(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float r, float z0, float z1, float mat)
{
    enum
    {
        N = 12
    };
    float g       = surface_at_world(c, mask_bit, cx, cy);
    float hi      = g + z1;
    float col3[3] = {0.0f, 0.0f, mat}, ref[3] = {0.0f, 0.0f, 0.0f};
    float px[N], py[N], pz[N];
    int   k;
    (void)z0;
    for (k = 0; k < N; ++k)
    {
        float a = -6.2831853f * ((float)k + 0.5f) / (float)N;
        px[k]   = cx + r * cosf(a);
        py[k]   = cy + r * sinf(a);
        pz[k]   = surface_at_world(c, mask_bit, px[k], py[k]) - 0.01f;
        if (pz[k] > hi - 0.02f)
            pz[k] = hi - 0.02f;
    }
    for (k = 0; k < N; ++k)
    {
        int   j  = (k + 1) % N;
        float mx = 0.5f * (px[k] + px[j]) - cx, my = 0.5f * (py[k] + py[j]) - cy;
        float ml  = sqrtf(mx * mx + my * my), nrm[3], tri[3][3];
        nrm[0]    = ml > 1e-6f ? mx / ml : 1.0f;
        nrm[1]    = ml > 1e-6f ? my / ml : 0.0f;
        nrm[2]    = 0.0f;
        tri[0][0] = px[k];
        tri[0][1] = py[k];
        tri[0][2] = hi;
        tri[1][0] = px[j];
        tri[1][1] = py[j];
        tri[1][2] = hi;
        tri[2][0] = px[j];
        tri[2][1] = py[j];
        tri[2][2] = pz[j];
        if (put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])tri, nrm, col3, ref, ref) != 0)
            return -1;
        tri[1][0] = px[j];
        tri[1][1] = py[j];
        tri[1][2] = pz[j];
        tri[2][0] = px[k];
        tri[2][1] = py[k];
        tri[2][2] = pz[k];
        if (put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])tri, nrm, col3, ref, ref) != 0)
            return -1;
    }
    for (k = 1; k + 1 < N; ++k)
    {
        float up[3] = {0.0f, 0.0f, 1.0f}, tri[3][3];
        tri[0][0]   = px[0];
        tri[0][1]   = py[0];
        tri[0][2]   = hi;
        tri[1][0]   = px[k];
        tri[1][1]   = py[k];
        tri[1][2]   = hi;
        tri[2][0]   = px[k + 1];
        tri[2][1]   = py[k + 1];
        tri[2][2]   = hi;
        if (put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])tri, up, col3, ref, ref) != 0)
            return -1;
    }
    return 0;
}

int put_box(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float w, float d, float z0, float z1, float mat, float phase)
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
    if (put_tri_r2(m, (const float (*)[3])tri, NULL, order, col3, ref, ref, 0) != 0)
        return -1;
    /*  The underside: a box that does not stand flat on the ground, a
     *  pier's column on a slope, showed its hollow inside (the user:
     *  "they're open faced"). */
    memcpy(tri[0], b[0], sizeof tri[0]);
    memcpy(tri[1], b[2], sizeof tri[1]);
    memcpy(tri[2], b[1], sizeof tri[2]);
    if (put_tri_r2(m, (const float (*)[3])tri, NULL, order, col3, ref, ref, 0) != 0)
        return -1;
    memcpy(tri[1], b[3], sizeof tri[1]);
    memcpy(tri[2], b[2], sizeof tri[2]);
    return put_tri_r2(m, (const float (*)[3])tri, NULL, order, col3, ref, ref, 0);
}

/*  A wire: a thin quad from world (x0, y0) at height z0 to (x1, y1) at
 *  z1 above the ground at each end, sagging in the middle. */
int put_wire(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x0, float y0, float z0, float x1, float y1, float z1, float sag)
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
int put_signal(RMesh *m, const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float order, int e, float h)
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
int put_lamp_face(RMesh *m, float order, float x, float y, float g, float z, float fx, float fy, float sz, float phase, float code)
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
int put_rail_signal(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy, int absolute, float s_along, int dir)
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
int put_gate(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy, int along_u)
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
int put_second_train_sign(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy)
{
    float g = surface_at_world(c, mask_bit, x, y);
    if (put_box(m, c, mask_bit, order, x, y, 0.008f, 0.008f, 0.0f, 0.33f, MAT_PROP, 0.0f) != 0)
        return -1;
    return put_lamp_face(m, order, x, y, g, 0.30f, fx, fy, 0.02f, 0.0f, 10.0f);
}
