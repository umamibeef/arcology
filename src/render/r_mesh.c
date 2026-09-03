/*  r_mesh.c -- building the mesh, and the query tool.
 *
 *  Split out of r_mesh.c; see r_mesh_int.h.
 */
#include "r_mesh_int.h"

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
    /*  Two passes when there are roads: the first lays them out and
     *  collects the corners a cut sinks below the ground, the second
     *  builds everything on the lowered field. */
    if (roads && !underground && s_pass == 0 && !getenv("SC2K_NO_CAP"))
    {
        int32_t g;
        int     rc;
        for (g = 0; g < GRID * GRID; ++g)
            s_zcap[g] = 1e9f;
        s_pass = 1;
        rc     = r_mesh_build(m, c, a, l, underground, rotated, roads);
        if (rc == 0)
        {
            s_pass = 2;
            rc     = r_mesh_build(m, c, a, l, underground, rotated, roads);
        }
        s_pass = 0;
        return rc;
    }
    m->n_land   = 0;
    m->n_water  = 0;
    m->to_water = 0;
    m->n_walls  = 0;
    build_field(c);
    if (s_pass == 2)
    {
        /*  The ground bends into the cuts: a corner under a sunken road
         *  takes the road's height, never rising, and never falling more
         *  than a level, which no vertical curve asks for. */
        int32_t g;
        for (g = 0; g < GRID * GRID; ++g)
            if (s_zcap[g] < s_h[g] && s_h[g] - s_zcap[g] < 1.0f)
                s_h[g] = s_zcap[g];
    }
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
    {
        if (build_networks(m, c, a, l, mask_bit, !rotated) != 0)
            return -1;
        return build_highways(m, c, mask_bit, !rotated);
    }
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
