/*  mesh_hiway.c -- raised highways (the road spec, part 7).
 *
 *  Split out of mesh.c; see mesh_int.h.
 */
#include "mesh_int.h"
#include "opt.h"

/* ---- raised highways (the road spec, part 7) --------------------------- */

/*  A highway is not one tile wide.  Its deck is a TWO-TILE BAND -- the
 *  spec's 2x2 segment -- so its centreline runs along the seam between
 *  two rows or two columns, never through a tile centre, and the whole
 *  segment pipeline above (which walks tile to tile) cannot express it.
 *  This walks the band instead and hands the ordinary loft a spine that
 *  is offset half a tile across.
 *
 *  Which ids are which was read off the shipped cities, not the sprite
 *  sheet -- see the Part 7 section of docs/future.rst.  Every one of
 *  these is ALWAYS part of a 2x2 square of highway (6905 of 6905 for
 *  0x49), and the id alone says which way the band runs, because an
 *  interior tile of a two-wide band has two neighbours along it and one
 *  across.  The six crossings are DECK tiles too: 0x4D is a deck in an
 *  east-west band with a railway underneath, and the rail below it is
 *  drawn by the rail family, not this one. */
/*  Is there a surface road under the deck here?  7.2 puts a two-column
 *  bent over one, its columns outside the curbs, and a hammerhead over
 *  anything else.  The two tiles the deck's own band covers are asked
 *  across it, since that is where a column would land. */
int road_under_deck(const RCity *c, float x, float y, float px, float py)
{
    int k;
    for (k = -1; k <= 1; k += 2)
    {
        int32_t tc = (int32_t)floorf(x + px * 0.5f * (float)k);
        int32_t tr = (int32_t)floorf(y + py * 0.5f * (float)k);
        uint8_t b;
        if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
            continue;
        b = c->xbld[tr * R_MAP + tc];
        if ((b >= 0x1Du && b <= 0x2Bu) || (b >= 0x43u && b <= 0x48u))
            return 1;
    }
    return 0;
}

/*  Straightening and corner cutting belong to the highway path: it is
 *  the only caller, and keeping them in the road algorithm made that
 *  file look like it still had two fitters in it. */
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
            /*  A run that would end after one period because its first
             *  inner leg is unlike the third yields to the run from the
             *  next corner when that one is longer: Toronto's rail at
             *  column 108, row 25, a two-tile leg into a 1:1 staircase,
             *  had been taken as a 2:1 stair of one step joined to the
             *  1:1 stair by a half-tile jog, an abrupt turn where the
             *  staircase should have begun a tile later. */
            if (i + 4 < m)
            {
                V2 l1 = {w[i + 1].x - w[i].x, w[i + 1].y - w[i].y};
                V2 l3 = {w[i + 3].x - w[i + 2].x, w[i + 3].y - w[i + 2].y};
                V2 l2 = {w[i + 2].x - w[i + 1].x, w[i + 2].y - w[i + 1].y};
                V2 l4 = {w[i + 4].x - w[i + 3].x, w[i + 4].y - w[i + 3].y};
                if (fabsf(v2len(l1) - v2len(l3)) > 0.1f && fabsf(v2len(l2) - v2len(l4)) <= 0.1f && v2len(l1) > 1.05f)
                    continue;
            }
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
                    /*  A highway's band is two tiles wide and the art
                     *  draws its diagonal as a staircase of two-tile
                     *  steps, whose midline stays inside the band; a
                     *  road's zig-zag of long legs both ways would leave
                     *  its tiles, so only a highway takes them. */
                    if (!ok || (l0 > 1.05f && l1 > 1.05f && !(s_hiway && l0 < 2.05f && l1 < 2.05f)))
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

/*  A rail's corners cut (the user, on Toronto's rail at column 108,
 *  row 25: "why can't it have a softer angle to reach its destination?"):
 *  a leg of two tiles or less between two longer legs whose lines meet
 *  on one of those legs, at a turn of sixty degrees or less, goes, the
 *  two corners it made replaced by that one point, which the fillet
 *  rounds.  A line down a column turning west a tile and a half, then
 *  north-west along a staircase, becomes a line down the column that
 *  bends once, forty-five degrees, onto the staircase's diagonal a tile
 *  and a half further on; the rail tile at the old corner is bypassed,
 *  which the check for undrawn pieces allows a rail beside its line. */
static int cut_corners(V2 *q, int n)
{
    int i, changed = 1;
    while (changed && n >= 4)
    {
        changed = 0;
        for (i = 1; i + 2 < n; ++i)
        {
            V2    a = q[i - 1], b = q[i], c = q[i + 1], d = q[i + 2], x;
            V2    ab = {b.x - a.x, b.y - a.y}, bc = {c.x - b.x, c.y - b.y}, cd = {d.x - c.x, d.y - c.y};
            float lab = v2len(ab), lbc = v2len(bc), lcd = v2len(cd), t, u, cosang;
            if (lbc > 2.05f || lab < 1e-4f || lcd < 1e-4f || lbc < 1e-4f)
                continue;
            cosang = (ab.x * cd.x + ab.y * cd.y) / (lab * lcd);
            if (cosang < 0.5f)
                continue; /* a turn over sixty degrees keeps its corners */
            if (!line_meet(a, ab, c, cd, &x))
                continue;
            t = ((x.x - a.x) * ab.x + (x.y - a.y) * ab.y) / (lab * lab); /* along a-b */
            u = ((x.x - c.x) * cd.x + (x.y - c.y) * cd.y) / (lcd * lcd); /* along c-d */
            if ((t > 0.2f && t < 1.0f - 1e-3f && lab - t * lab <= 2.05f) || (u > 1e-3f && u < 0.8f && u * lcd <= 2.05f))
            {
                int k;
                q[i] = x;
                for (k = i + 1; k + 1 < n; ++k)
                    q[k] = q[k + 1];
                --n;
                changed = 1;
                break;
            }
        }
    }
    return n;
}

static int hiway_deck(uint8_t b, int *east_west)
{
    if (b == 0x49u || b == 0x4Bu || b == 0x4Du || b == 0x4Fu)
    {
        *east_west = 1;
        return 1;
    }
    if (b == 0x4Au || b == 0x4Cu || b == 0x4Eu || b == 0x50u)
    {
        *east_west = 0;
        return 1;
    }
    /*  The ramps, 0x61 to 0x64, belong to the band too (the user: "the
     *  highways look like shit"): without them the deck stopped at the
     *  last elevated tile, a carriageway ending in mid-air over the
     *  field.  Their axis is the deck's they carry, read off the
     *  the shipped cities: 0x62 and 0x64 join an east-west band, 0x61 and 0x63 a
     *  north-south one (200 ramp tiles in the shipped cities, two
     *  exceptions). */
    if (b >= 0x61u && b <= 0x64u)
    {
        *east_west = (b == 0x62u || b == 0x64u);
        return 2;
    }
    return 0;
}

static int hiway_is_ramp(uint8_t b)
{
    return b >= 0x61u && b <= 0x64u;
}

/*  The band cell a deck tile belongs to, as its PRIMARY tile: the lower
 *  of the two across the band.  Answers 0 if the tile has no partner --
 *  a lone deck tile is malformed data and is left to the sprites. */
static int hiway_cell(const RCity *c, int32_t col, int32_t row, int32_t *pc, int32_t *pr, int *ew)
{
    uint8_t b = c->xbld[row * R_MAP + col];
    int32_t oc, orr;
    int     e2;
    if (!hiway_deck(b, ew))
        return 0;
    /*  across the band: north-south for an east-west deck */
    oc  = *ew ? col : col - 1;
    orr = *ew ? row - 1 : row;
    if (oc >= 0 && orr >= 0 && hiway_deck(c->xbld[orr * R_MAP + oc], &e2) && e2 == *ew)
    {
        *pc = oc;
        *pr = orr;
        return 1;
    }
    oc  = *ew ? col : col + 1;
    orr = *ew ? row + 1 : row;
    if (oc < R_MAP && orr < R_MAP && hiway_deck(c->xbld[orr * R_MAP + oc], &e2) && e2 == *ew)
    {
        *pc = col;
        *pr = row;
        return 1;
    }
    return 0;
}

/*  A curve block: the four tiles of one id, 0x65 to 0x68, that carry a
 *  highway through a right angle.  Answers its lowest tile and, of the
 *  four sides, which two carry the runs it joins.  The ids are not
 *  trusted for the orientation -- it is read off the runs that touch
 *  the block, as the crossing table was read off the shipped cities. */
static int hiway_block(const RCity *c, int32_t col, int32_t row, int32_t *bc, int32_t *br, int side[4])
{
    uint8_t b = c->xbld[row * R_MAP + col];
    int32_t k;
    int     e2;
    if (b < 0x65u || b > 0x68u)
        return 0;
    *bc = col;
    *br = row;
    for (k = 0; k < 2; ++k)
    {
        if (*bc > 0 && c->xbld[*br * R_MAP + *bc - 1] == b)
            --*bc;
        if (*br > 0 && c->xbld[(*br - 1) * R_MAP + *bc] == b)
            --*br;
    }
    /*  Its sides, north, east, south, west: a run of the axis that side
     *  would carry, on either of the two tiles along it. */
    side[0] = side[1] = side[2] = side[3] = 0;
    for (k = 0; k < 2; ++k)
    {
        int32_t x = *bc + k, y = *br + k;
        if (*br > 0 && hiway_deck(c->xbld[(*br - 1) * R_MAP + x], &e2) == 1 && !e2)
            side[0] = 1;
        if (*br + 2 < R_MAP && hiway_deck(c->xbld[(*br + 2) * R_MAP + x], &e2) == 1 && !e2)
            side[2] = 1;
        if (*bc + 2 < R_MAP && hiway_deck(c->xbld[y * R_MAP + *bc + 2], &e2) == 1 && e2)
            side[1] = 1;
        if (*bc > 0 && hiway_deck(c->xbld[y * R_MAP + *bc - 1], &e2) == 1 && e2)
            side[3] = 1;
    }
    return 1;
}

/*  Walk one band from an end and loft its deck.  The spine runs along
 *  the seam: half a tile across from the primary tile's centre. */
static int walk_hiway(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, int32_t col, int32_t row, int ew, uint8_t *seen)
{
    static V2      pts[MAX_PTS];
    static uint8_t ramp[MAX_PTS];
    static Piece   pieces[MAX_PIECES];
    int32_t        cc = col, cr = row;
    int            n = 0, np, guard = 0, cew = ew;
    int32_t        dx = ew ? 1 : 0, dy = ew ? 0 : 1;
    float          total = 0.0f;
    /*  The spine runs along the seam between the band's two tiles, half
     *  a tile across from the primary tile's centre: for an east-west
     *  band the seam is its southern edge, for a north-south band its
     *  eastern one.  The walk follows the band wherever it goes (the
     *  user: "work on segments that are diagonal, same basic treatment
     *  as roads", "we want the renderer to generate elegant transitions
     *  based on the path the highways are taking"): straight on while it
     *  can, through a curve block at a right angle, or a step sideways,
     *  which makes a staircase the straightener turns into one diagonal
     *  and the fillet then rounds. */
    while (guard++ < 4 * R_MAP)
    {
        int32_t pcol, prow, nc, nr, bc, br;
        int     e2, side[4], k;
        if (cc < 0 || cr < 0 || cc >= R_MAP || cr >= R_MAP)
            break;
        if (!hiway_cell(c, cc, cr, &pcol, &prow, &e2) || e2 != cew)
            break;
        if (pcol != cc || prow != cr)
            break; /* not the primary of its pair */
        if (seen[cr * R_MAP + cc])
            break;
        seen[cr * R_MAP + cc] = 1;
        if (n + 2 >= MAX_PTS)
            break;
        ramp[n]  = (uint8_t)hiway_is_ramp(c->xbld[cr * R_MAP + cc]);
        pts[n++] = (V2){(float)cc + (cew ? 0.5f : 1.0f), (float)cr + (cew ? 1.0f : 0.5f)};
        nc       = cc + dx;
        nr       = cr + dy;
        /*  Straight on. */
        if (nc >= 0 && nr >= 0 && nc < R_MAP && nr < R_MAP && hiway_cell(c, nc, nr, &pcol, &prow, &e2) && e2 == cew &&
            pcol == nc && prow == nr && !seen[nr * R_MAP + nc])
        {
            cc = nc;
            cr = nr;
            continue;
        }
        /*  Through a curve block, and on through every block that
         *  follows it: its centre lies on both seams, so the spine turns
         *  there and the fillet carries the arc back into the tile
         *  before and on into the one after (the user: "you start the
         *  curve earlier in the previous piece and into the last
         *  piece").  The art draws a diagonal highway as a chain of
         *  these blocks touching at their corners, so the walk follows
         *  the chain by whatever step joins one to the next, two tiles
         *  along, across, or both, and the straightener makes one line
         *  of the centres. */
        {
            int32_t tc = cew ? (dx > 0 ? cc + 1 : cc - 1) : cc;
            int32_t tr = cew ? cr : (dy > 0 ? cr + 1 : cr - 1);
            int32_t sx = dx, sy = dy; /* the step that brought us here */
            int     chained = 0;
            while (tc >= 0 && tr >= 0 && tc < R_MAP && tr < R_MAP && hiway_block(c, tc, tr, &bc, &br, side) && n + 2 < MAX_PTS &&
                   !seen[br * R_MAP + bc])
            {
                static const int32_t off[8][2] = {
                    {2, 0},
                    {-2, 0},
                    {0, 2},
                    {0, -2},
                    {2, 2},
                    {2, -2},
                    {-2, 2},
                    {-2, -2}
                };
                int32_t nbc = 0, nbr = 0;
                int     have = 0;
                seen[br * R_MAP + bc]           = 1;
                seen[br * R_MAP + bc + 1]       = 1;
                seen[(br + 1) * R_MAP + bc]     = 1;
                seen[(br + 1) * R_MAP + bc + 1] = 1;
                ramp[n]  = 0;
                pts[n++] = (V2){(float)bc + 1.0f, (float)br + 1.0f};
                chained  = 1;
                /*  The next block of the chain: a step that carries on
                 *  the way we were going, never back. */
                for (k = 0; k < 8 && !have; ++k)
                {
                    int32_t qc = bc + off[k][0], qr = br + off[k][1], mc, mr;
                    int     qs[4];
                    /*  Never back the way we came; a step across is
                     *  allowed, and comes first, so a chain of blocks is
                     *  followed block by block rather than jumping the
                     *  diagonal and leaving an irregular path the
                     *  straightener cannot read. */
                    if (off[k][0] * sx + off[k][1] * sy < 0)
                        continue;
                    if (qc < 0 || qr < 0 || qc >= R_MAP || qr >= R_MAP)
                        continue;
                    if (!hiway_block(c, qc, qr, &mc, &mr, qs) || seen[mr * R_MAP + mc])
                        continue;
                    nbc  = mc;
                    nbr  = mr;
                    sx   = off[k][0];
                    sy   = off[k][1];
                    have = 1;
                }
                if (have)
                {
                    tc = nbc;
                    tr = nbr;
                    continue;
                }
                /*  The chain ends here: the run this block hands the
                 *  highway to, which is the side that carries one and is
                 *  not the way we came in. */
                {
                    static const int32_t odx[4] = {0, 1, 0, -1};
                    static const int32_t ody[4] = {-1, 0, 1, 0};
                    int                  out    = -1;
                    for (k = 0; k < 4; ++k)
                        if (side[k] && odx[k] * sx + ody[k] * sy > 0)
                            out = k;
                    if (out < 0)
                        for (k = 0; k < 4; ++k)
                            if (side[k] && !(odx[k] * sx + ody[k] * sy < 0))
                                out = k;
                    if (out < 0)
                        break;
                    cew = (out == 1 || out == 3);
                    dx  = odx[out];
                    dy  = ody[out];
                    cc  = bc + (out == 1 ? 2 : out == 3 ? -1 : 0);
                    cr  = br + (out == 2 ? 2 : out == 0 ? -1 : 0);
                    break;
                }
            }
            if (chained)
                continue;
        }
        /*  A step sideways: the staircase of a diagonal run. */
        {
            int   found = 0;
            int32_t sc, sr;
            for (k = -1; k <= 1 && !found; k += 2)
            {
                sc = nc + (cew ? 0 : k);
                sr = nr + (cew ? k : 0);
                if (sc < 0 || sr < 0 || sc >= R_MAP || sr >= R_MAP)
                    continue;
                if (hiway_cell(c, sc, sr, &pcol, &prow, &e2) && e2 == cew && pcol == sc && prow == sr && !seen[sr * R_MAP + sc])
                {
                    cc    = sc;
                    cr    = sr;
                    found = 1;
                }
            }
            if (found)
                continue;
        }
        break;
    }
    if (n < 2)
        return 0;
    /*  Run the spine to the outer edge of the end cells, so the deck
     *  covers its whole first and last segment rather than stopping at
     *  their centres.  The direction at each end is the polyline's own,
     *  since the band may have turned along the way. */
    {
        float d0x = pts[0].x - pts[1].x, d0y = pts[0].y - pts[1].y;
        float d1x = pts[n - 1].x - pts[n - 2].x, d1y = pts[n - 1].y - pts[n - 2].y;
        float l0 = sqrtf(d0x * d0x + d0y * d0y), l1 = sqrtf(d1x * d1x + d1y * d1y);
        if (l0 > 1e-4f)
        {
            pts[0].x += d0x / l0 * 0.5f;
            pts[0].y += d0y / l0 * 0.5f;
        }
        if (l1 > 1e-4f)
        {
            pts[n - 1].x += d1x / l1 * 0.5f;
            pts[n - 1].y += d1y / l1 * 0.5f;
        }
    }
    /*  Straightened as a road is: a staircase of cells becomes one
     *  diagonal, then every bend is filleted, with the wide radius a
     *  highway wants so the curve begins well before the corner. */
    /*  The ramp lengths first, measured along the raw polyline in
     *  tiles: the taper below compares them against arc length, and
     *  counting points instead was wrong the moment the straightener
     *  collapsed a run -- the flags no longer lined up with the points
     *  and the count was no length at all. */
    s_hiway_ramp0 = s_hiway_ramp1 = 0.0f;
    {
        int k2;
        for (k2 = 0; k2 + 1 < n && ramp[k2]; ++k2)
        {
            float rx = pts[k2 + 1].x - pts[k2].x, ry = pts[k2 + 1].y - pts[k2].y;
            s_hiway_ramp0 += sqrtf(rx * rx + ry * ry);
        }
        for (k2 = n - 1; k2 > 0 && ramp[k2]; --k2)
        {
            float rx = pts[k2].x - pts[k2 - 1].x, ry = pts[k2].y - pts[k2 - 1].y;
            s_hiway_ramp1 += sqrtf(rx * rx + ry * ry);
        }
    }
    {
        static V2 q[MAX_PTS];
        int       nk;
        /*  The straightener takes a band's wider steps only for a
         *  highway, which the flag says.  It is cleared the moment it is
         *  done with: left set, an early return below carried it into
         *  the next pass, where every road and rail in the city was
         *  lofted as a freeway deck. */
        s_hiway = 1;
        nk      = straighten(pts, n, q);
        nk      = cut_corners(q, nk);
        s_hiway = 0;
        if (opt_set("hiway-dump"))
        {
            int q2;
            printf("hiway band from c%d r%d: %d points ->", (int)col, (int)row, n);
            for (q2 = 0; q2 < n && q2 < 14; ++q2)
                printf(" (%.2f,%.2f)", (double)pts[q2].x, (double)pts[q2].y);
            printf("\n   straightened %d ->", nk);
            for (q2 = 0; q2 < nk && q2 < 14; ++q2)
                printf(" (%.2f,%.2f)", (double)q[q2].x, (double)q[q2].y);
            printf("\n");
        }
        if (nk >= 2)
        {
            memcpy(pts, q, (size_t)nk * sizeof pts[0]);
            n = nk;
        }
    }
    if (fillet(pts, n, 2.5f, pieces, &np) != 0 || np == 0)
        return 0;
    for (guard = 0; guard < np; ++guard)
        total += pieces[guard].len;
    /*  F_ROAD for now: the deck is lofted as a very wide road strip so
     *  it is visible and the band walk can be judged.  The parapets,
     *  the median barrier, the freeway markings of 7.1 and the piers of
     *  7.2 want a material of their own and come next. */
    {
        /*  The lift tapers over the ramp cells at each end, from the
         *  deck's height where the elevated tiles begin down to the
         *  ground at the band's outer end, so a ramp is a ramp. */
        int rc;
        if (s_hiway_ramp0 >= total)
            s_hiway_ramp1 = 0.0f; /* all ramp: one slope, not two */
        s_hiway = 1;
        rc      = loft(m, c, mask_bit, comp, F_ROAD, pieces, np, total, 0.0f, 0.0f, 1, 1);
        s_hiway = 0;
        return rc;
    }
}

/*  Every band in the city, each walked once from an end. */
int build_highways(RMesh *m, const RCity *c, uint8_t mask_bit, int comp)
{
    static uint8_t seen[R_MAP * R_MAP];
    int32_t        col, row;
    memset(seen, 0, sizeof seen);
    for (row = 0; row < R_MAP; ++row)
        for (col = 0; col < R_MAP; ++col)
        {
            int32_t pcol, prow;
            int     ew;
            int32_t bc, br;
            if (!hiway_cell(c, col, row, &pcol, &prow, &ew))
                continue;
            if (pcol != col || prow != row || seen[row * R_MAP + col])
                continue;
            /*  only from an end: the cell before this one along the band
             *  is not a band cell */
            bc = ew ? col - 1 : col;
            br = ew ? row : row - 1;
            if (bc >= 0 && br >= 0)
            {
                int32_t qc, qr;
                int     e2;
                if (hiway_cell(c, bc, br, &qc, &qr, &e2) && e2 == ew && qc == bc && qr == br)
                    continue;
            }
            if (walk_hiway(m, c, mask_bit, comp, col, row, ew, seen) != 0)
                return -1;
        }
    return 0;
}
