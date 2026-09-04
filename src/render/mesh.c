/*  mesh.c -- building the mesh, and the query tool.
 *
 *  Split out of mesh.c; see mesh_int.h.
 */
#include "mesh_int.h"

static int build_networks(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, uint8_t mask_bit, int comp)
{
    static uint8_t visited[R_MAP * R_MAP * 4];
    int32_t        col, row;
    Family         fam;
    (void)a;
    /*  Stage two, for both families at once: every segment fits its path
     *  and records what the later stages need -- which way it leaves each
     *  junction, and where it passes each tile.  Nothing is drawn.  The
     *  crossings then have both paths to build from, which is what lets a
     *  road and a railway meet at whatever angle they actually meet at. */
    memset(s_arm, 0, sizeof s_arm);
    memset(s_trim, 0, sizeof s_trim);
    memset(s_cross, 0, sizeof s_cross);
    s_measure = 1;
    for (fam = F_ROAD; fam <= F_RAIL; ++fam)
    {
        memset(visited, 0, sizeof visited);
        for (row = 0; row < R_MAP; ++row)
            for (col = 0; col < R_MAP; ++col)
            {
                int links = eff_links(c, l, col, row, fam), e;
                if (!tile_links(c, l, col, row, fam) ||
                    node_kind(c, l, fam, col, row) == 0)
                    continue;
                for (e = 0; e < 4; ++e)
                    if ((links & (1 << e)) &&
                        walk_segment(m, c, l, mask_bit, comp, fam, col, row, e, visited) != 0)
                        return -1;
            }
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
    s_measure = 0;
    /*  Stage three for the roads: each junction takes its shape from its
     *  arms and hands each of them back the length to start at. */
    for (row = 0; row < R_MAP; ++row)
        for (col = 0; col < R_MAP; ++col)
        {
            V2    poly[16];
            float trm[4];
            int   links = eff_links(c, l, col, row, F_ROAD), k;
            if (!tile_links(c, l, col, row, F_ROAD) ||
                node_kind(c, l, F_ROAD, col, row) != 2)
                continue;
            if (junction_poly(c, F_ROAD, col, row, links, poly, NULL, 16, trm) < 3)
                continue;
            for (k = 0; k < 4; ++k)
                s_trim[FAMX(F_ROAD)][(row * R_MAP + col) * 4 + k] = trm[k];
        }
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
                 *  track mainline, so every road class gets gates.  It is
                 *  built from the two paths that actually cross here --
                 *  the road's and the railway's, as stage two fitted them
                 *  -- and not from the tile's axes, so a road meeting the
                 *  line at an angle gets a panel that lies along it (the
                 *  user: "the railway/road crossection doesn't take the
                 *  new intersection into account").
                 *
                 *  The panel covers the whole of the road it interrupts:
                 *  the road's full width across, and along the road as
                 *  far as the track bed reaches, which at an angle is
                 *  further than the track is wide.  The ballast and the
                 *  sleepers run under it right up to the road, since the
                 *  railway does not stop at a crossing (the user: "the
                 *  dark pavement needs to cover the segment of road being
                 *  crossed entirely and the gravel under the rails should
                 *  be present all the way up to the road").
                 *
                 *  On each approach the mast stands at the driver's
                 *  right, a road's half width from the centreline and the
                 *  panel's reach from the middle; the stop line crosses
                 *  the approach lane 4.5 m before it; a second-train sign
                 *  faces each footway.  The gates are down and the
                 *  flashers lit while a train stands within three tiles
                 *  along the line. */
                const RCross *xr = &s_cross[FAMX(F_ROAD)][idx];
                const RCross *xl = &s_cross[FAMX(F_RAIL)][idx];
                int   links2 = piece_links(l, second, c->xter[idx]);
                int   ns     = (links2 & (L_N | L_S)) == (L_N | L_S); /* the rail runs north-south */
                float cx = (float)col + 0.5f, cy = (float)row + 0.5f;
                /* the road's way through, and the rail's */
                float ox = xr->have ? xr->dx : (ns ? 1.0f : 0.0f);
                float oy = xr->have ? xr->dy : (ns ? 0.0f : 1.0f);
                float lx = xl->have ? xl->dx : (ns ? 0.0f : 1.0f);
                float ly = xl->have ? xl->dy : (ns ? 1.0f : 0.0f);
                float rx = -oy, ry = ox;   /* the driver's right */
                float sinang, reach, hb = ROAD_W * 0.5f;
                float rh, h = ROAD_W * 0.5f + 0.08f;
                int   ap, side;
                /*  Where the two centrelines meet: the middle of the
                 *  panel.  Failing that, the tile's own middle. */
                if (xr->have && xl->have)
                {
                    V2 met;
                    if (line_meet((V2){xr->x, xr->y}, (V2){ox, oy},
                                  (V2){xl->x, xl->y}, (V2){lx, ly}, &met))
                    {
                        float dx = met.x - cx, dy = met.y - cy;
                        if (dx * dx + dy * dy < 0.36f) /* still on this tile */
                        {
                            cx = met.x;
                            cy = met.y;
                        }
                    }
                }
                /*  How far along the road the track bed reaches: its half
                 *  width divided by the sine of the angle they cross at,
                 *  held to something sane where they cross obliquely. */
                sinang = fabsf(ox * ly - oy * lx);
                if (sinang < 0.45f)
                    sinang = 0.45f;
                reach = (RAIL_W * 0.5f + 0.06f) / sinang;
                if (reach > 0.62f)
                    reach = 0.62f;
                rh = reach;
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
                /*  The panel is where the two bands actually overlap:
                 *  the road's full width, cut by the track bed's two
                 *  sides.  A rectangle in the road's own frame covers too
                 *  much at an angle -- it buries the ballast either side
                 *  of the road, when the gravel should run up to the
                 *  asphalt and stop (the user: "the gravel under the
                 *  rails should be present all the way up to the road").
                 *  Four corners, each where a road edge meets a rail
                 *  edge. */
                {
                    float rw = RAIL_W * 0.5f + 0.06f;
                    V2    op = {xr->have ? xr->x : cx, xr->have ? xr->y : cy};
                    V2    rp = {xl->have ? xl->x : cx, xl->have ? xl->y : cy};
                    V2    od = {ox, oy}, rd = {lx, ly};
                    V2    rn = {-ly, lx};
                    V2    q[4];
                    int   ok = 1, k;
                    static const float es[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}};
                    for (k = 0; k < 4 && ok; ++k)
                    {
                        V2 ea = {op.x + rx * hb * es[k][0], op.y + ry * hb * es[k][0]};
                        V2 eb = {rp.x + rn.x * rw * es[k][1], rp.y + rn.y * rw * es[k][1]};
                        ok    = line_meet(ea, od, eb, rd, &q[k]);
                    }
                    if (ok)
                    {
                        float a0[2] = {q[0].x, q[0].y}, a1[2] = {q[1].x, q[1].y};
                        float b0[2] = {q[2].x, q[2].y}, b1[2] = {q[3].x, q[3].y};
                        /*  A hair over the asphalt it replaces, and
                         *  over the ballast it interrupts: on the ground
                         *  itself the panel is the one surface here that
                         *  the clip check can catch dipping under it. */
                        float za = surface_at_world(c, mask_bit, a0[0], a0[1]) + 0.045f;
                        float zb = surface_at_world(c, mask_bit, b0[0], b0[1]) + 0.045f;
                        if (strip_quad_z(m, c, mask_bit, order + 0.06f, a0, a1, b0, b1, za, zb, -1.0f, 1.0f, 0.0f, 1.0f, MAT_XPANEL) != 0)
                            return -1;
                    }
                }
                for (ap = 0; ap < 2; ++ap)
                {
                    /* the two road approaches, along the road's own line */
                    float fx = ap ? -ox : ox, fy = ap ? -oy : oy;
                    float gx = -fy, gy = fx; /* the driver's right on this approach */
                    float mx = cx - fx * rh + gx * h, my = cy - fy * rh + gy * h;
                    float sl = rh + 0.30f; /* 4.5 m before the panel (spec 3.15) */
                    float s0[2], s1[2], t0[2], t1[2];
                    if (put_gate(m, c, mask_bit, order, mx, my, fx, fy, ns) != 0)
                        return -1;
                    s0[0]        = cx - fx * (sl + 0.02f);
                    s0[1]        = cy - fy * (sl + 0.02f);
                    s1[0]        = cx - fx * (sl + 0.02f) + gx * (ROAD_W * 0.5f * 0.8f);
                    s1[1]        = cy - fy * (sl + 0.02f) + gy * (ROAD_W * 0.5f * 0.8f);
                    t0[0]        = cx - fx * (sl - 0.02f);
                    t0[1]        = cy - fy * (sl - 0.02f);
                    t1[0]        = cx - fx * (sl - 0.02f) + gx * (ROAD_W * 0.5f * 0.8f);
                    t1[1]        = cy - fy * (sl - 0.02f) + gy * (ROAD_W * 0.5f * 0.8f);
                    s_road_class = 0.0f;
                    if (strip_quad(m, c, mask_bit, order + 0.25f, s0, s1, t0, t1, 0.0f, 0.79f, 0.17f, 0.17f, MAT_ZEBRA) != 0)
                        return -1;
                    /* the second-train signs, one at each footway on this approach */
                    for (side = -1; side <= 1; side += 2)
                    {
                        float sx = cx - fx * (rh + 0.02f) + gx * (float)side * (ROAD_W * 0.5f - 0.04f);
                        float sy = cy - fy * (rh + 0.02f) + gy * (float)side * (ROAD_W * 0.5f - 0.04f);
                        if (put_second_train_sign(m, c, mask_bit, order, sx, sy, -fx, -fy) != 0)
                            return -1;
                    }
                }
            }
        }
    /*  And the drawing pass: the junctions first, so a leg knows whether
     *  it is signalled before it draws its crosswalk, then the segments,
     *  each cut back to the outline its junctions gave it. */
    for (fam = F_ROAD; fam <= F_RAIL; ++fam)
    {
        memset(visited, 0, sizeof visited);
        for (row = 0; row < R_MAP; ++row)
            for (col = 0; col < R_MAP; ++col)
            {
                int links = eff_links(c, l, col, row, fam);
                if (!tile_links(c, l, col, row, fam) ||
                    node_kind(c, l, fam, col, row) != 2)
                    continue;
                if (build_junction(m, c, mask_bit, fam, col, row, links, tile_order(c, col, row, mask_bit) + (fam == F_RAIL ? 0.05f : 0.0f)) != 0)
                    return -1;
            }
        for (row = 0; row < R_MAP; ++row)
            for (col = 0; col < R_MAP; ++col)
            {
                int links = eff_links(c, l, col, row, fam), e;
                int kind;
                if (!tile_links(c, l, col, row, fam))
                    continue;
                kind = node_kind(c, l, fam, col, row);
                if (kind == 0)
                {
                    /*  A piece none of whose links a neighbour returns:
                     *  an island, drawn as its own short band. */
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

/*  A tile the corridor pass graded: all four of its corners belong to a
 *  corridor. */
static int corridor_tile(int32_t col, int32_t row)
{
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return 0;
    return s_corr[row * GRID + col] && s_corr[row * GRID + col + 1] && s_corr[(row + 1) * GRID + col] &&
           s_corr[(row + 1) * GRID + col + 1];
}

int mesh_build(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int roads)
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
    const uint8_t      mask_bit = city_corner_mask(c->rotation);
    int                dump_r = -1, dump_c = -1;

    if (getenv("SC2K_MESH_DUMP"))
        sscanf(getenv("SC2K_MESH_DUMP"), "%d,%d", &dump_r, &dump_c);
    /*  Two passes when there are roads: the first lays the networks out
     *  and records the surface their corridors want, the second builds
     *  the world with those corridors notched into it. */
    if (roads && !underground && s_pass == 0 && !getenv("SC2K_NO_CAP"))
    {
        int32_t g;
        int     rc;
        for (g = 0; g < GRID * GRID; ++g)
        {
            s_zcap[g]  = 1e9f;
            s_zlow[g]  = 1e9f;
            s_zdist[g] = 1e9f;
            s_corr[g]  = 0;
        }
        {
            /*  The shelves are one entry per tile CORNER, four times the
             *  map and not the grid's own count.  Clearing them on the
             *  grid's loop left three quarters of the map holding a shelf
             *  of zero, which dropped every tile it touched to the sea
             *  and stood a wall round it. */
            int32_t t;
            for (t = 0; t < R_MAP * R_MAP * 4; ++t)
            {
                s_tilez[t] = 1e9f;
                s_tile_reset(t);
            }
        }
        s_pass = 1;
        rc     = mesh_build(m, c, a, l, underground, rotated, roads);
        if (rc == 0)
        {
            s_pass = 2;
            rc     = mesh_build(m, c, a, l, underground, rotated, roads);
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
        /*  A corridor tile must be graded on all four corners or its
         *  surface tilts through the road: where several segments meet,
         *  one corner could keep the hillside's height while its
         *  neighbours took the road's.  So every tile carrying a network
         *  piece with any graded corner has the rest filled in from the
         *  mean of those it has, twice over so it carries across a
         *  junction. */
        int32_t pass2, col2, row2;
        for (pass2 = 0; pass2 < 2; ++pass2)
            for (row2 = 0; row2 < R_MAP; ++row2)
                for (col2 = 0; col2 < R_MAP; ++col2)
                {
                    int32_t gi[4] = {row2 * GRID + col2, row2 * GRID + col2 + 1, (row2 + 1) * GRID + col2,
                                     (row2 + 1) * GRID + col2 + 1};
                    float   sum = 0.0f, low = 1e9f;
                    int     k2, have = 0;
                    uint8_t b2 = c->xbld[row2 * R_MAP + col2];
                    if (b2 < 0x0Eu || b2 >= 0x49u)
                        continue; /* not a network piece */
                    for (k2 = 0; k2 < 4; ++k2)
                        if (s_corr[gi[k2]] == 1 && s_zcap[gi[k2]] < 1e8f)
                        {
                            sum += s_zcap[gi[k2]];
                            if (s_zlow[gi[k2]] < low)
                                low = s_zlow[gi[k2]];
                            ++have;
                        }
                    if (!have || have == 4)
                        continue;
                    for (k2 = 0; k2 < 4; ++k2)
                        if (s_corr[gi[k2]] != 1)
                        {
                            /*  A corner filled in from its tile's others
                             *  inherits their ceiling too, or the
                             *  smoothing will lift it over the road. */
                            s_zcap[gi[k2]] = sum / (float)have;
                            if (low < s_zlow[gi[k2]])
                                s_zlow[gi[k2]] = low;
                            s_corr[gi[k2]] = 1;
                        }
                }
    }
    if (s_pass == 2)
    {
        /*  The corridors are NOTCHED into the terrain, and a notch only
         *  ever cuts (the user: "why are you raising the terrain?
         *  corridors do not affect the terrain at all, they NOTCH it").
         *  So a corner the band reaches is lowered to the corridor's
         *  height and never lifted above the ground it was cut from, and
         *  only corners the band itself reaches are touched at all -- the
         *  batter ring beyond them is terrain, and terrain is left as it
         *  is.  The wall rule closes the sides of the cut, and the
         *  watertight check covers the corridor like any other ground. */
        int32_t g;
        {

            /*  The corridor's surface, smoothed over the whole field
             *  rather than fitted segment by segment (the user: "you then
             *  create a surface that bisects these corridors, but does so
             *  in a smooth manner that doesn't deviate too much from the
             *  original altitude").  Each graded corner is averaged with
             *  its neighbours, again and again, and held within a level
             *  of the ground it started from: a step between two roads
             *  meeting at different heights then spreads over several
             *  tiles instead of tilting one tile through the band, which
             *  is what the clipping check kept finding. */
            static float zf[GRID * GRID];
            for (g = 0; g < GRID * GRID; ++g)
                zf[g] = (s_corr[g] && s_zcap[g] < 1e8f) ? s_zcap[g] : s_h[g];
            /*  No global solve: the corridor's height comes from its
             *  own ramp between node altitudes, which every segment
             *  computes for itself and writes into its corners.  Nothing
             *  here has to reconcile them, because two segments that meet
             *  ramp to the same node (the user: "I don't like global
             *  solves because that doesn't scale"). */
            /*  Nothing is written to the terrain.  The corridor carries
             *  its own surface, tile by tile, and the ground stays
             *  exactly where it was; the wall rule closes the step. */
            (void)zf;
        }
    }
    /*  SC2K_PLAN_DUMP=1 writes the world as numbers for tools/plan.py,
     *  which draws a city from above: the field's heights, what each
     *  tile carries, and (with the paths) the corridors and their
     *  centrelines.  A plan view is how this is inspected (the user:
     *  "can you provide a debug view that allows me to inspect a
     *  city?"). */
    if (s_pass != 1 && getenv("SC2K_PLAN_DUMP"))
    {
        int32_t gx, gy;
        printf("FIELD %d\n", GRID);
        for (gy = 0; gy < GRID; ++gy)
        {
            for (gx = 0; gx < GRID; ++gx)
                printf("%s%.3f", gx ? " " : "", (double)s_h[gy * GRID + gx]);
            printf("\n");
        }
        printf("XBLD %d\n", R_MAP);
        for (gy = 0; gy < R_MAP; ++gy)
        {
            for (gx = 0; gx < R_MAP; ++gx)
                printf("%s%02x", gx ? " " : "", (unsigned)c->xbld[gy * R_MAP + gx]);
            printf("\n");
        }
        printf("XTER %d\n", R_MAP);
        for (gy = 0; gy < R_MAP; ++gy)
        {
            for (gx = 0; gx < R_MAP; ++gx)
                printf("%s%02x", gx ? " " : "", (unsigned)c->xter[gy * R_MAP + gx]);
            printf("\n");
        }
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
                /*  A corridor's sides are engineered, as a building's
                 *  foundation is (the user: "this surface defines the
                 *  top of the column of the tiles that make up the
                 *  corridor, which have engineered walls as their side
                 *  texture"): a tile whose corners all belong to a
                 *  corridor is a graded shelf, and the step from it to
                 *  the hillside is a retaining wall, not a bank of
                 *  earth. */
                /*  A wall of coursed blocks where the step is
                 *  ENGINEERED, earth where it is the hill's own.  A
                 *  building's foundation is engineered; so is the side of
                 *  a network tile that stands ABOVE its neighbour, which
                 *  is a road or a line built up on fill (the user, on
                 *  Toronto's column 63, row 18: "the raised tile doesn't
                 *  show the engineered wall texture, only dirt").  A
                 *  network tile that sits BELOW its neighbour is a cut
                 *  into the slope, and the slope's face is earth, which
                 *  is what the art shows and what stopped Bay View's hill
                 *  road standing on block walls. */
                if (xbld >= 0x70u || c->xbld[ni] >= 0x70u || corridor_tile(col, row) || corridor_tile(nc, nr))
                    mat = eng_wall;
                else if ((z[ia] + z[ib]) > (qa[2] + qb[2]) + 1e-4f && xbld >= 0x0Eu && xbld < 0x49u)
                    mat = eng_wall; /* this tile is built up over its neighbour */
                else if ((qa[2] + qb[2]) > (z[ia] + z[ib]) + 1e-4f && c->xbld[ni] >= 0x0Eu && c->xbld[ni] < 0x49u)
                    mat = eng_wall; /* the neighbour is */
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
    /*  The zone tints, for the map view (the user: "in map view, you can
     *  just draw the zone colors as highlights").  One flat quad per
     *  zoned tile, a hair over its ground, carrying the zone in col.r;
     *  the vertex shader drops them unless the camera is looking down,
     *  so they cost nothing in the oblique view they would spoil.
     *
     *  A placed structure is tinted too (the user: "I want special
     *  buildings to be shown in the map view as well"): everything from
     *  the power plants up (0xC6) is something the player put there
     *  rather than something a zone grew, so it carries its own code --
     *  11 for a power plant, 12 for a park, 10 for every other one --
     *  instead of the zone under it. */
    for (row = 0; row < R_MAP; ++row)
        for (col = 0; col < R_MAP; ++col)
        {
            int32_t idx  = row * R_MAP + col;
            int     zone = c->xzon[idx] & 0x0Fu;
            float   zc[3], tri[3][3], z[4];
            float   ref[3];
            Kind    kind;
            if (c->xbld[idx] >= 0xC6u)
                zone = c->xbld[idx] <= 0xCFu  ? 11  /* the ten power plants */
                       : c->xbld[idx] == 0xD5u ? 12 /* a park              */
                                               : 10;
            if (!zone)
                continue;
            kind  = tile_top(c, col, row, mask_bit, z);
            (void)kind;
            zc[0] = (float)zone;
            zc[1] = 0.0f;
            zc[2] = MAT_ZONE;
            ref[0] = ref[1] = ref[2] = (float)zone;
            tri[0][0] = (float)col;
            tri[0][1] = (float)row;
            tri[0][2] = z[NW] + 0.02f;
            tri[1][0] = (float)col + 1.0f;
            tri[1][1] = (float)row;
            tri[1][2] = z[NE] + 0.02f;
            tri[2][0] = (float)col + 1.0f;
            tri[2][1] = (float)row + 1.0f;
            tri[2][2] = z[SE] + 0.02f;
            if (put_tri_r2(m, (const float (*)[3])tri, NULL, tile_order(c, col, row, mask_bit) + 0.4f, zc, ref, ref, 1) != 0)
                return -1;
            tri[1][0] = (float)col + 1.0f;
            tri[1][1] = (float)row + 1.0f;
            tri[1][2] = z[SE] + 0.02f;
            tri[2][0] = (float)col;
            tri[2][1] = (float)row + 1.0f;
            tri[2][2] = z[SW] + 0.02f;
            if (put_tri_r2(m, (const float (*)[3])tri, NULL, tile_order(c, col, row, mask_bit) + 0.4f, zc, ref, ref, 1) != 0)
                return -1;
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

int mesh_tile_corners(const RCity *c, int32_t col, int32_t row, int underground, float z[4])
{
    float o[4], t[4];
    int   k;
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return -1;
    if (underground)
        for (k = 0; k < 4; ++k)
            t[k] = s_b[corner_gi(col, row, k)];
    else
        tile_top(c, col, row, city_corner_mask(c->rotation), t);
    /* out in NW, NE, SE, SW order, whatever the corner enum's is */
    o[0] = t[NW];
    o[1] = t[NE];
    o[2] = t[SE];
    o[3] = t[SW];
    memcpy(z, o, sizeof o);
    return 0;
}

int mesh_query(const RCity *c, int32_t col, int32_t row, char *buf, size_t n)
{
    static const char *const names[] = {"ground", "water", "levelled pad", "sloped plane"};
    int32_t                  idx     = row * R_MAP + col;
    float                    z[4], b[4];
    Kind                     kind;
    int                      k;
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP || !buf || n == 0)
        return -1;
    kind = tile_top(c, col, row, city_corner_mask(c->rotation), z);
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
