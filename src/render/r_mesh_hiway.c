/*  r_mesh_hiway.c -- raised highways (the road spec, part 7).
 *
 *  Split out of r_mesh.c; see r_mesh_int.h.
 */
#include "r_mesh_int.h"

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
    return 0;
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

/*  Walk one band from an end and loft its deck.  The spine runs along
 *  the seam: half a tile across from the primary tile's centre. */
static int walk_hiway(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, int32_t col, int32_t row, int ew, uint8_t *seen)
{
    static V2    pts[MAX_PTS];
    static Piece pieces[MAX_PIECES];
    int32_t      cc = col, cr = row;
    int          n = 0, np, guard = 0;
    float        total = 0.0f;
    /*  Along the band, not across it: an east-west deck runs in +col.
     *  The spine is offset half a tile ACROSS, which is the other
     *  axis -- getting these two the same way round finds every band
     *  and then walks off it in one step. */
    const float ax = ew ? 1.0f : 0.0f, ay = ew ? 0.0f : 1.0f;
    const float sx = ew ? 0.5f : 1.0f, sy = ew ? 1.0f : 0.5f; /* spine  */
    while (guard++ < R_MAP)
    {
        int32_t pcol, prow;
        int     e2;
        if (cc < 0 || cr < 0 || cc >= R_MAP || cr >= R_MAP)
            break;
        if (!hiway_cell(c, cc, cr, &pcol, &prow, &e2) || e2 != ew)
            break;
        if (pcol != cc || prow != cr)
            break; /* not the primary: the band has stepped sideways */
        if (seen[cr * R_MAP + cc])
            break;
        seen[cr * R_MAP + cc] = 1;
        if (n + 1 >= MAX_PTS)
            break;
        pts[n++] = (V2){(float)cc + sx, (float)cr + sy};
        cc += (int32_t)ax;
        cr += (int32_t)ay;
    }
    if (n < 2)
        return 0;
    /*  Run the spine to the outer edge of the end cells, so the deck
     *  covers its whole first and last segment rather than stopping at
     *  their centres. */
    pts[0].x -= ax * 0.5f;
    pts[0].y -= ay * 0.5f;
    pts[n - 1].x += ax * 0.5f;
    pts[n - 1].y += ay * 0.5f;
    if (fillet(pts, n, 1.0f, pieces, &np) != 0 || np == 0)
        return 0;
    for (guard = 0; guard < np; ++guard)
        total += pieces[guard].len;
    /*  F_ROAD for now: the deck is lofted as a very wide road strip so
     *  it is visible and the band walk can be judged.  The parapets,
     *  the median barrier, the freeway markings of 7.1 and the piers of
     *  7.2 want a material of their own and come next. */
    {
        int rc;
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
