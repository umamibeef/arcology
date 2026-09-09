/*  uiglue.c -- the two crossings between the app and its interface.
 *
 *  ui.h's contract is that the app fills what the windows SHOW once a
 *  frame, the windows edit switches in place and raise commands, and the
 *  app applies them and clears them.  Those two directions are ui_fill and
 *  ui_apply, and they are the only places either side touches the other;
 *  the tool in hand is applied here for the same reason. */
#include "adapt.h"
#include "internal.h"
#include "script.h"
#include "log.h"
#include "net/report.h"
#include "opt.h"
#include "project.h"
#include "sim.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dump.h"

/*  ==================================================================
 *  The interface, filled and applied
 *
 *  Everything the windows read is copied in once a frame, and everything
 *  they change is applied once a frame.  The interface never reaches
 *  into the app itself.
 *  ================================================================== */
/*  The four projected corners of a tile, put in order around their own
 *  middle.  They are computed in the grid's order -- NW, NE, SE, SW -- and
 *  in the map view that order happens to trace the tile's boundary; under
 *  the game's own camera the yaw turns which corner lands where on screen,
 *  the fixed order crosses itself, and the outline came out a thin dart
 *  instead of a tile.  Sorting by angle costs nothing and holds at any
 *  camera. */
static void poly_wind(float p[4][2])
{
    float cx = 0.25f * (p[0][0] + p[1][0] + p[2][0] + p[3][0]);
    float cy = 0.25f * (p[0][1] + p[1][1] + p[2][1] + p[3][1]);
    int   i, j;
    for (i = 1; i < 4; ++i)
        for (j = i; j > 0; --j)
        {
            float a = atan2f(p[j][1] - cy, p[j][0] - cx), b = atan2f(p[j - 1][1] - cy, p[j - 1][0] - cx);
            float t;
            if (a >= b)
                break;
            t = p[j][0], p[j][0] = p[j - 1][0], p[j - 1][0] = t;
            t = p[j][1], p[j][1] = p[j - 1][1], p[j - 1][1] = t;
        }
}

/*  What the UI shows this frame. */
/*  The pointer inside a triangle as the camera projects it, by the sign of
 *  the three edge tests; `z` comes back as the triangle's highest corner,
 *  which is what decides between two that overlap. */
static int inspect_hit(const App *a, uint32_t tri, float f, float dens, float *z, uint32_t *comp)
{
    float p3[3][3], sx[3], sy[3], d1, d2, d3, area;
    int   v;
    mesh_tri_get(&a->mesh, tri, p3, comp);
    for (v = 0; v < 3; ++v)
    {
        float cx, cy;
        grid_to_canvas(a, p3[v][0], p3[v][1], p3[v][2], &cx, &cy);
        sx[v] = (cx - (float)a->gv.scroll_x) * f / dens;
        sy[v] = (cy - (float)a->gv.scroll_y) * f / dens;
    }
    /*  A triangle with no area on screen covers no point.  It has to be
     *  turned away here, before the edge tests: a face seen edge on
     *  projects to a line, the pointer is then on one side of all three
     *  of its edges wherever it is, and the tests below answer YES to
     *  the whole window.  The oblique camera makes such faces of every
     *  wall and skirt that runs along the view, so one of them, standing
     *  higher than the thing under the pointer, wins the pick and the
     *  outline lands somewhere the pointer never was. */
    area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (area > -1e-3f && area < 1e-3f)
        return 0;
    d1 = (a->q_mx - sx[1]) * (sy[0] - sy[1]) - (sx[0] - sx[1]) * (a->q_my - sy[1]);
    d2 = (a->q_mx - sx[2]) * (sy[1] - sy[2]) - (sx[1] - sx[2]) * (a->q_my - sy[2]);
    d3 = (a->q_mx - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (a->q_my - sy[0]);
    if (((d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f)) && ((d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f)))
        return 0;
    *z = p3[0][2] > p3[1][2] ? p3[0][2] : p3[1][2];
    if (p3[2][2] > *z)
        *z = p3[2][2];
    return 1;
}

void inspect_copy(App *a)
{
    const RUiState *s = &a->us;
    char            buf[4096];
    size_t          n = 0;
    const char     *p;
    int             k;
    if (!a->inspect)
        return;
    if (!s->comp_who[0] && !s->comp_label[0])
    {
        ui_log(&a->us, "Nothing under the pointer to copy");
        return;
    }
#define ADD(...) (n < sizeof buf ? n += (size_t)snprintf(buf + n, sizeof buf - n, __VA_ARGS__) : 0)
    ADD("%s\n", s->comp_label[0] ? s->comp_label : "the mesh");
    ADD("  rendered by   %s at %s\n", s->comp_who[0] ? s->comp_who : "?", s->comp_where[0] ? s->comp_where : "?");
    ADD("  part of       %s%s%s\n", s->comp_gen[0] ? s->comp_gen : "nothing",
        s->comp_gen[0] && s->comp_gen_where[0] ? ", at " : "", s->comp_gen[0] ? s->comp_gen_where : "");
    ADD("  material      %g\n", (double)s->comp_mat);
    if (s->comp_gen_tris > s->comp_tris)
        ADD("  triangles     %u, %u with what is under it\n", s->comp_tris, s->comp_gen_tris);
    else
        ADD("  triangles     %u\n", s->comp_tris);
    ADD("  tile          %d,%d\n", (int)s->q_col, (int)s->q_row);
    if (s->comp_box_ok)
    {
        ADD("  extent        %.3f,%.3f to %.3f,%.3f\n", (double)s->comp_box[0], (double)s->comp_box[1], (double)s->comp_box[3], (double)s->comp_box[4]);
        ADD("  height        %.3f to %.3f\n", (double)s->comp_box[2], (double)s->comp_box[5]);
    }
    /*  The shape's own account of itself, a row a line: the key before
     *  the tab, the value after it. */
    for (p = s->comp_note; *p;)
    {
        const char *e   = strchr(p, '\n');
        size_t      len = e ? (size_t)(e - p) : strlen(p);
        const char *t   = (const char *)memchr(p, '\t', len);
        if (t)
            ADD("  %-13.*s %.*s\n", (int)(t - p), p, (int)(len - (size_t)(t - p) - 1u), t + 1);
        else
            ADD("  %.*s\n", (int)len, p);
        p = e ? e + 1 : p + len;
    }
    for (k = 0; k < s->comp_n_also; ++k)
        ADD("  drawn over    %s\n", s->comp_also[k]);
    ADD("  outline       %d edges\n", s->comp_n / 2);
#undef ADD
    SDL_SetClipboardText(buf);
    ui_log(&a->us, "Copied \"%s\" to the clipboard", s->comp_label[0] ? s->comp_label : "the mesh");
}

void ui_fill(App *a)
{
    /*  --inspect: the inspector on from the start.  Once, before the frame
     *  reads the switch -- applied further down it took a frame to arrive,
     *  and applied every frame it could never be switched off again. */
    static int inspect_flag_applied;
    RUiState   *s = &a->us;
    const City *c = a->city;
    uint32_t    inst, culled;
    if (!inspect_flag_applied)
    {
        inspect_flag_applied = 1;
        if (g_dev.inspect)
            a->inspect = 1;
    }
    int         k;
    if (a->view->name[0])
        snprintf(s->city_name, sizeof s->city_name, "%s", a->view->name);
    else
        snprintf(s->city_name, sizeof s->city_name, "%s", a->city_base);
    s->year       = c->year_founded + c->date / 300;
    s->month      = (c->date / 25) % 12;
    s->funds      = c->funds;
    s->population = c->population;
    s->stage      = c->misc[MISC_STAGE];
    for (k = 0; k < 3; ++k)
        s->demand[k] = c->rci_demand[k];
    s->power_pct      = c->power_pct;
    s->water_pct      = c->water_pct;
    s->unemployment   = c->unemployment;
    s->land_value_tot = c->land_value_tot;
    s->crime_tot      = c->crime_tot;
    s->traffic_tot    = c->traffic_tot;
    s->pollution_tot  = c->pollution_tot;
    s->fps            = a->fps;
    s->frame_ms       = a->frame_ms;
    gpu_stats(a->gpu, &inst, &culled);
    s->instances  = inst;
    s->culled     = culled;
    s->mesh_verts = a->mesh.n_land;
    s->driver     = gpu_driver(a->gpu);
    for (k = 0; k < RUI_N_GRAPH && k < N_GRAPH; ++k)
    {
        s->graph[k]      = c->graph[k];
        s->graph_name[k] = GRAPH_NAMES[k];
    }
    for (k = 0; k < RUI_N_DEPT && k < N_DEPT; ++k)
    {
        s->dept_name[k]    = DEPT_NAMES[k];
        s->dept_amount[k]  = c->dept[k].amount;
        s->dept_funding[k] = c->dept[k].funding;
        s->dept_accrued[k] = c->dept[k].accrued;
    }
    /*  The inspector points at the MESH. The triangles near the pointer are
     *  projected the way the camera projects them and the one under the
     *  pointer is found; through it comes the component that drew it, and
     *  the component's silhouette is the outline.  A tile is only how the
     *  search is narrowed.  The ground is not a thing to inspect -- the
     *  query tool says what a tile is -- so the ground, its banks, walls
     *  and water are passed over, and what is built on them is what the
     *  pointer finds. */
    s->outline      = mesh_tune()[9] > 0.5f;
    s->show_inspect = a->inspect;
    s->comp_ok = s->comp_n = s->comp_box_ok = 0;
    s->comp_label[0] = s->comp_who[0] = s->comp_where[0] = s->comp_gen[0] = s->comp_gen_where[0] = s->comp_note[0] = 0;
    s->comp_n_also                                       = 0;
    s->comp_mat                                          = 0.0f;
    s->comp_tris = s->comp_gen_tris = 0;
    if (a->inspect && a->q_col >= 0 && a->q_row >= 0 && a->sw.level)
    {
        float    f    = (float)a->gv.scale * (a->gv.zoom > 0.0f ? a->gv.zoom : 1.0f);
        float    dens = a->win_density > 0.0f ? a->win_density : 1.0f;
        uint32_t cand[4096];
        int      kt;
        int      best      = -1;
        float    best_z    = -1e9f;
        uint32_t best_comp = 0;
        /*  Every thing the pointer is over, not only the topmost: the one
         *  in front tells you what you clicked, and the rest tell you what
         *  it is drawn over.  Where two of them meet at one height the
         *  surfaces overlap, and no outline of either can show that. */
        enum
        {
            MAX_ALSO = 16
        };
        uint32_t akey[MAX_ALSO], acomp[MAX_ALSO];
        float    az[MAX_ALSO];
        int      n_also = 0;
        /*  Every tile whose geometry could be under the pointer, not a
         *  patch of tiles around the GROUND tile.  A deck stands over a
         *  metre of air, and in this projection height moves a thing up
         *  the screen: the tile a deck belongs to is three or four tiles
         *  from the ground the pointer lands on -- further than any fixed
         *  patch reaches, and the pick found the road underneath instead.
         *
         *  For a fixed window point the difference row - col is settled,
         *  and only the sum grows with altitude, so the candidates lie
         *  along a diagonal: walk the altitude from under the ground to
         *  above anything built, take the tile at each step and its
         *  neighbours, and test each tile's triangles as it is reached --
         *  no shared budget, so the densest place on the map, a deck with
         *  its ramps, cannot spend it before its own tile is looked at. */
        {
            static int32_t stamp[R_MAP * R_MAP];
            static int32_t gen;
            float          ysc, hsc, alt, step;
            cam_scales(a, &ysc, &hsc);
            step = hsc > 0.001f ? ysc / hsc : 1.0f; /* half a tile of travel */
            if (step < 0.05f)
                step = 0.05f;
            if (step > 4.0f)
                step = 4.0f;
            ++gen;
            for (alt = -4.0f; alt <= 48.0f; alt += step)
            {
                float   fc, fr;
                int32_t bc, br, dc, dr;
                canvas_to_grid_at(a, a->q_mx, a->q_my, dens, alt, &fc, &fr);
                bc = (int32_t)floorf(fc);
                br = (int32_t)floorf(fr);
                for (dr = -1; dr <= 1; ++dr)
                    for (dc = -1; dc <= 1; ++dc)
                    {
                        int32_t tc = bc + dc, tr = br + dr, idx;
                        int     ntri, ktri;
                        if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                            continue;
                        idx = tr * R_MAP + tc;
                        if (stamp[idx] == gen)
                            continue;
                        stamp[idx] = gen;
                        ntri       = mesh_tris_at(&a->mesh, tc, tr, cand, (int)(sizeof cand / sizeof *cand));
                        for (ktri = 0; ktri < ntri; ++ktri)
                        {
                            float    z;
                            uint32_t comp;
                            uint32_t key;
                            int      s2;
                            if (!inspect_hit(a, cand[ktri], f, dens, &z, &comp) || !shape_built(comp))
                                continue;
                            if (best < 0 || z > best_z)
                                best = (int)cand[ktri], best_z = z, best_comp = comp;
                            key = comp; /* by SURFACE, not by thing: a band drawn over its own junction's asphalt is the overlap worth seeing */
                            for (s2 = 0; s2 < n_also; ++s2)
                                if (akey[s2] == key)
                                    break;
                            if (s2 < n_also)
                            {
                                if (z > az[s2])
                                    az[s2] = z;
                            }
                            else if (n_also < MAX_ALSO)
                                akey[n_also] = key, acomp[n_also] = comp, az[n_also] = z, ++n_also;
                        }
                    }
            }
        }
        if (best >= 0)
        {
            const char  *who = NULL, *where = NULL;
            float        mat  = 0.0f, box[6];
            uint32_t     tris = 0;
            static float    segs[6 * 4096];
            static int      seg_n;
            static uint32_t seg_key = 0u, seg_land = 0u;
            static double   seg_ms;
            int             ns;
            {
                /*  What it is part of, all the way up: a footway belongs to
                 *  its junction, and the junction to nothing, so the chain
                 *  reads from the nearest owner outwards. */
                ShapeId par   = shape_parent(best_comp);
                size_t  used  = 0;
                int     depth = 0;
                while (par != SHAPE_NONE && depth++ < 8 && used + 1 < sizeof s->comp_gen)
                {
                    const char *nm = shape_name(par);
                    used += (size_t)snprintf(s->comp_gen + used, sizeof s->comp_gen - used, "%s%s", used ? ", in " : "", nm ? nm : "?");
                    if (depth == 1)
                    {
                        const char *pw  = shape_where(par);
                        const char *rel = pw ? strstr(pw, "src/") : NULL;
                        snprintf(s->comp_gen_where, sizeof s->comp_gen_where, "%s", rel ? rel : (pw ? pw : ""));
                    }
                    par = shape_parent(par);
                }
            }
            if (shape_get(best_comp, &mat, &tris, box) == 0)
            {
                const char *rel;
                who   = shape_who(best_comp);
                where = shape_where(best_comp);
                rel   = where ? strstr(where, "src/") : NULL;
                snprintf(s->comp_who, sizeof s->comp_who, "%s", who ? who : "?");
                snprintf(s->comp_where, sizeof s->comp_where, "%s", rel ? rel : (where ? where : ""));
                snprintf(s->comp_label, sizeof s->comp_label, "%s", shape_name(best_comp) ? shape_name(best_comp) : "mesh");
                s->comp_mat  = mat;
                s->comp_tris = tris;
                memcpy(s->comp_box, box, sizeof s->comp_box);
                s->comp_box_ok = 1;
                /*  The thing the component belongs to, when it was made
                 *  under a generating call: its triangles in all, and the
                 *  box round all of it. */
                if (shape_get_deep(best_comp, &tris, box) == 0)
                {
                    s->comp_gen_tris = tris;
                    memcpy(s->comp_box, box, sizeof s->comp_box);
                }
            }
            {
                /*  The generating call's own account of the thing: its
                 *  first line is the name the outline wears, the rest are
                 *  the window's rows. */
                const char *note = shape_note_of(best_comp);
                if (note)
                    snprintf(s->comp_note, sizeof s->comp_note, "%s", note);
            }
            {
                /*  What else is under the pointer, the highest first, each
                 *  with the material it is drawn in and the height it was
                 *  met at.  A surface at the same height as the one in
                 *  front is drawn over it. */
                int i2, j2;
                for (i2 = 1; i2 < n_also; ++i2)
                    for (j2 = i2; j2 > 0 && az[j2] > az[j2 - 1]; --j2)
                    {
                        uint32_t tk = akey[j2], tc = acomp[j2];
                        float    tz = az[j2];
                        akey[j2] = akey[j2 - 1], acomp[j2] = acomp[j2 - 1], az[j2] = az[j2 - 1];
                        akey[j2 - 1] = tk, acomp[j2 - 1] = tc, az[j2 - 1] = tz;
                    }
                for (i2 = 0; i2 < n_also && s->comp_n_also < (int)(sizeof s->comp_also / sizeof s->comp_also[0]); ++i2)
                {
                    const char *nt = shape_note_of(acomp[i2]), *wh = NULL;
                    char        name[80];
                    float       mat2 = 0.0f;
                    if (akey[i2] == best_comp)
                        continue;
                    shape_get(acomp[i2], &mat2, NULL, NULL);
                    wh = shape_name(acomp[i2]);
                    if (nt)
                    {
                        const char *nl  = strchr(nt, '\n');
                        size_t      len = nl ? (size_t)(nl - nt) : strlen(nt);
                        if (len >= sizeof name)
                            len = sizeof name - 1;
                        memcpy(name, nt, len);
                        name[len] = 0;
                    }
                    else
                        snprintf(name, sizeof name, "%s", wh ? wh : "?");
                    snprintf(s->comp_also[s->comp_n_also], sizeof s->comp_also[0], "%s, material %g, at %.3f", name, (double)mat2, (double)az[i2]);
                    ++s->comp_n_also;
                }
            }
            /*  The outline is the same for as long as the pointer stays on the
             *  THING -- a deck's slab, gores and columns share one -- and it
             *  is a walk of every triangle of the thing, so it is computed
             *  when the thing under the pointer changes and kept. */
            {
                uint32_t key = best_comp;
                if (key != seg_key || a->mesh.n_land != seg_land)
                {
                    double t0 = (double)SDL_GetTicksNS() * 1e-6;
                    seg_n     = mesh_comp_edges(&a->mesh, best_comp, segs, 4096);
                    seg_ms    = (double)SDL_GetTicksNS() * 1e-6 - t0;
                    seg_key   = key;
                    seg_land  = a->mesh.n_land;
                }
            }
            s->comp_at[0] = a->q_mx;
            s->comp_at[1] = a->q_my;
            ns            = seg_n;
            if (ns > 0)
            {
                int       out = 0;
                const int cap = (int)(sizeof s->comp_poly / sizeof s->comp_poly[0]);
                for (kt = 0; kt < ns && out + 1 < cap; ++kt)
                {
                    int e;
                    for (e = 0; e < 2; ++e)
                    {
                        const float *w = &segs[6 * kt + 3 * e];
                        float        cx, cy;
                        grid_to_canvas(a, w[0], w[1], w[2], &cx, &cy);
                        s->comp_poly[out][0] = (cx - (float)a->gv.scroll_x) * f / dens;
                        s->comp_poly[out][1] = (cy - (float)a->gv.scroll_y) * f / dens;
                        ++out;
                    }
                }
                s->comp_n  = out;
                s->comp_ok = out >= 2;
            }
            /*  One line when the component under the pointer changes, and
             *  never once a frame: it is a message, so it goes through the
             *  log like every other. */
            if (g_dev.inspect && best_comp != a->inspect_comp)
            {
                a->inspect_comp = best_comp;
                R_DBG("inspect", "%d,%d: %s generated by %s (%s), drawn by %s (%s), material %g, %u triangles, outline %d edges in %.1f ms",
                      (int)a->q_col, (int)a->q_row, s->comp_label, s->comp_gen[0] ? s->comp_gen : "-", s->comp_gen_where, s->comp_who, s->comp_where, (double)s->comp_mat, s->comp_tris, seg_n, seg_ms);
            }
        }
    }
    else
        a->inspect_c = a->inspect_r = -1;
    s->q_ok = a->q_col >= 0 && a->q_row >= 0;
    if (s->q_ok)
    {
        int y = a->q_row, x = a->q_col;
        s->q_col         = x;
        s->q_row         = y;
        s->q_xter        = c->xter[y][x];
        s->q_xbld        = c->xbld[y][x];
        s->q_xzon        = c->xzon[y][x];
        s->q_xbit        = c->xbit[y][x];
        s->q_xund        = c->xund[y][x];
        s->q_xtxt        = c->xtxt[y][x];
        s->q_altm        = c->altm[y][x];
        s->q_alt         = s->q_xter >= 0x10 ? (int32_t)((s->q_altm >> 5) & 0x1F)
                                             : (int32_t)(s->q_altm & 0x1F);
        s->q_water_level = c->water_level;
        s->q_building    = BUILDING[s->q_xbld].name;
        if (mesh_query(a->view, x, y, s->q_mesh, sizeof s->q_mesh) != 0)
            s->q_mesh[0] = 0;
        /*  The footprint the cell belongs to, as the original finds it
         *  ($763A): its edge n and its north-east tile, so it covers rows
         *  oy..oy+n-1 and columns ox-n+1..ox.  The highlight outlines the
         *  whole footprint, on the surface the mesh draws -- a building's
         *  pad is flat, a lone cell follows its own plane -- or, in the
         *  underground view, on the ground drawn there, the seabed under
         *  water. */
        {
            int n, x0, y0;
            s->q_orow = y; /* $763A moves the cell's own coordinates to the origin */
            s->q_ocol = x;
            n         = sim_footprint_origin(c, &s->q_orow, &s->q_ocol, c->xbld[y][x]);
            x0        = s->q_ocol - n + 1;
            y0        = s->q_orow;
            if (n < 1 || n > 4 || x0 < 0 || y0 < 0 || s->q_ocol >= R_MAP || y0 + n > R_MAP)
            {
                n         = 1;
                x0        = x;
                y0        = y;
                s->q_ocol = x;
                s->q_orow = y;
            }
            s->q_size    = n;
            s->q_poly_ok = 0;
            {
                const RAtlasLevel *lv = a->sw.level;
                int                ug = a->opts.underground;
                float              t[4], z[4];
                int                ok = lv != NULL;
                /* NW, NE, SE, SW of the footprint, each from its corner tile */
                ok   = ok && mesh_tile_corners(a->view, x0, y0, ug, t) == 0;
                z[0] = t[0];
                ok   = ok && mesh_tile_corners(a->view, x0 + n - 1, y0, ug, t) == 0;
                z[1] = t[1];
                ok   = ok && mesh_tile_corners(a->view, x0 + n - 1, y0 + n - 1, ug, t) == 0;
                z[2] = t[2];
                ok   = ok && mesh_tile_corners(a->view, x0, y0 + n - 1, ug, t) == 0;
                z[3] = t[3];
                if (ok)
                {
                    float f    = (float)a->gv.scale * (a->gv.zoom > 0.0f ? a->gv.zoom : 1.0f);
                    float dens = a->win_density > 0.0f ? a->win_density : 1.0f;
                    int   kq;
                    /*  The corners go to grid_to_canvas UNTURNED: it turns
                     *  them about the view's pivot itself.  Turning them here
                     *  first turns them twice, and in the map view (yaw 45)
                     *  the outline comes out a diamond on a square grid. */
                    for (kq = 0; kq < 4; ++kq)
                    {
                        static const int dc[4] = {0, 1, 1, 0}, dr[4] = {0, 0, 1, 1};
                        float            cx, cy;
                        grid_to_canvas(a, (float)(x0 + dc[kq] * n), (float)(y0 + dr[kq] * n), z[kq], &cx, &cy);
                        s->q_poly[kq][0] = (cx - (float)a->gv.scroll_x) * f / dens;
                        s->q_poly[kq][1] = (cy - (float)a->gv.scroll_y) * f / dens;
                    }
                    poly_wind(s->q_poly);
                    s->q_poly_ok = 1;
                }
            }
        }
    }
    /*  The area selection (Shift-drag in the map view, app.c) and, after
     *  the release, the confirmation of what was written: the rectangle's
     *  four corners projected as the query outline's are, and a caption. */
    s->sel_ok = 0;
    {
        /*  After the release the rectangle blinks twice over half a second
         *  and goes; what was written is the log's to say. */
        uint64_t now   = SDL_GetTicksNS();
        int      flash = now < a->sel_flash_until && (((a->sel_flash_until - now) / 120000000ull) & 1u) == 1u;
        /*  And before the drag: with Shift held, the tile under the cursor
         *  shows in the same tint, so the drag's start is seen.  The tile's
         *  outline belongs to this Shift-drag, which picks an AREA; the
         *  inspector outlines the component instead, and while it is on no
         *  tile is highlighted under the pointer. */
        int hover = a->plan && !a->inspect && !a->sel && !flash && (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 && a->q_col >= 0 && a->q_row >= 0;
        if (a->plan && (a->sel || flash || hover) && (hover || (a->sel_c0 >= 0 && a->sel_c1 >= 0)))
        {
            int32_t x0 = hover ? a->q_col : a->sel_c0 < a->sel_c1 ? a->sel_c0
                                                                  : a->sel_c1,
                    x1 = hover ? a->q_col : a->sel_c0 < a->sel_c1 ? a->sel_c1
                                                                  : a->sel_c0;
            int32_t y0 = hover ? a->q_row : a->sel_r0 < a->sel_r1 ? a->sel_r0
                                                                  : a->sel_r1,
                    y1 = hover ? a->q_row : a->sel_r0 < a->sel_r1 ? a->sel_r1
                                                                  : a->sel_r0;
            int     ug = a->opts.underground, ok = a->sw.level != NULL;
            float   t[4], z[4];
            ok   = ok && mesh_tile_corners(a->view, x0, y0, ug, t) == 0;
            z[0] = t[0];
            ok   = ok && mesh_tile_corners(a->view, x1, y0, ug, t) == 0;
            z[1] = t[1];
            ok   = ok && mesh_tile_corners(a->view, x1, y1, ug, t) == 0;
            z[2] = t[2];
            ok   = ok && mesh_tile_corners(a->view, x0, y1, ug, t) == 0;
            z[3] = t[3];
            if (ok)
            {
                float f    = (float)a->gv.scale * (a->gv.zoom > 0.0f ? a->gv.zoom : 1.0f);
                float dens = a->win_density > 0.0f ? a->win_density : 1.0f;
                int   kq;
                for (kq = 0; kq < 4; ++kq)
                {
                    static const int dc[4] = {0, 1, 1, 0}, dr[4] = {0, 0, 1, 1};
                    float            cx, cy;
                    grid_to_canvas(a, (float)(dc[kq] ? x1 + 1 : x0), (float)(dr[kq] ? y1 + 1 : y0), z[kq], &cx, &cy);
                    s->sel_poly[kq][0] = (cx - (float)a->gv.scroll_x) * f / dens;
                    s->sel_poly[kq][1] = (cy - (float)a->gv.scroll_y) * f / dens;
                }
                poly_wind(s->sel_poly);
                s->sel_ok = 1;
                if (a->sel)
                    snprintf(s->sel_text, sizeof s->sel_text, "Area %d,%d to %d,%d (%d x %d)", (int)x0, (int)y0, (int)x1, (int)y1, (int)(x1 - x0 + 1), (int)(y1 - y0 + 1));
                else if (hover)
                    snprintf(s->sel_text, sizeof s->sel_text, "%d,%d", (int)x0, (int)y0);
                else
                    s->sel_text[0] = 0;
            }
        }
    }
    /* the switches */
    s->speed    = a->speed;
    s->geometry = a->gv.geometry;
    /*  The road knobs, unless the window is mid-edit and owns them. */
    if (!s->tune_changed)
        memcpy(s->tune, mesh_tune(), sizeof s->tune);
    s->plan    = a->plan;
    s->cell_ok = 0;
    if (a->plan && s->show_cells && a->sw.level)
    {
        /*  The map's frame on screen: where tile 0,0's corner lands and
         *  what one column and one row step by.  Straight down, height
         *  moves nothing, so the ground's own zero will do. */
        float f    = (float)a->gv.scale * (a->gv.zoom > 0.0f ? a->gv.zoom : 1.0f);
        float dens = a->win_density > 0.0f ? a->win_density : 1.0f;
        float ox, oy, cxp, cyp, rxp, ryp;
        grid_to_canvas(a, 0.0f, 0.0f, 0.0f, &ox, &oy);
        grid_to_canvas(a, 1.0f, 0.0f, 0.0f, &cxp, &cyp);
        grid_to_canvas(a, 0.0f, 1.0f, 0.0f, &rxp, &ryp);
        s->cell_o[0] = (ox - (float)a->gv.scroll_x) * f / dens;
        s->cell_o[1] = (oy - (float)a->gv.scroll_y) * f / dens;
        s->cell_c[0] = (cxp - ox) * f / dens;
        s->cell_c[1] = (cyp - oy) * f / dens;
        s->cell_r[0] = (rxp - ox) * f / dens;
        s->cell_r[1] = (ryp - oy) * f / dens;
        s->cell_ok   = 1;
    }
    /*  The console's status line: which script, how many times it has
     *  been read, what it sets, and the last thing that went wrong. */
    {
        const char *p = script_path(), *e = script_error();
        snprintf(s->script_status, sizeof s->script_status, "%s  read %d, %d rules%s%s",
                 p ? p : "no script", script_generation(), script_rules(),
                 e ? "  --  " : "", e ? e : "");
    }
    s->grid        = a->gv.grid;
    s->markings    = a->gv.markings;
    s->furniture   = a->gv.furniture;
    s->sidewalks   = a->gv.sidewalks;
    s->plain_sweep = a->gv.plain_sweep;
    s->underground = a->opts.underground;
    s->view        = a->opts.view;
}

/*  What the UI changed or asked for. */
void ui_apply(App *a, SDL_Window *win, int pw, int ph)
{
    RUiState *s = &a->us;
    if (s->speed != a->speed)
        set_speed(a, s->speed);
    /*  A line typed at the console, run against the live state.  Its
     *  answer is caught from the dump sink rather than printed, so the
     *  window shows it and stdout stays the program's own. */
    if (s->script_run)
    {
        size_t n = strlen(s->script_out);
        char   got[4096];
        snprintf(s->script_out + n, sizeof s->script_out - n, "> %s\n", s->script_line);
        dump_capture(got, sizeof got);
        script_eval(s->script_line);
        dump_capture(NULL, 0);
        n = strlen(s->script_out);
        if (got[0])
            snprintf(s->script_out + n, sizeof s->script_out - n, "%s%s", got, got[strlen(got) - 1] == '\n' ? "" : "\n");
        else if (script_error())
        {
            n = strlen(s->script_out);
            snprintf(s->script_out + n, sizeof s->script_out - n, "%s\n", script_error());
        }
        /*  A console that fills up keeps its tail: the older half goes. */
        if (strlen(s->script_out) > sizeof s->script_out - 1024)
        {
            char *cut = strchr(s->script_out + sizeof s->script_out / 2, '\n');
            memmove(s->script_out, cut ? cut + 1 : s->script_out, cut ? strlen(cut + 1) + 1 : 1);
        }
        s->script_line[0] = 0;
        s->script_run     = 0;
        if (script_take_dirty())
            a->mesh_dirty = 1;
    }
    if (s->script_reload)
    {
        s->script_reload = 0;
        if (script_reload())
            a->mesh_dirty = 1;
    }
    if (s->tune_changed)
    {
        /*  The knobs are a tuning session's; outline is a view and is
         *  applied below with the other views. */
        float keep = mesh_tune()[9];
        memcpy(mesh_tune(), s->tune, sizeof s->tune);
        mesh_tune()[9] = keep;
        s->tune_changed = 0;
        a->mesh_dirty   = 1; /* the knobs are geometry: rebuild it */
    }
    if (s->geometry != a->gv.geometry)
    {
        a->gv.geometry = s->geometry;
        a->mesh_dirty  = 1; /* the roads are part of the mesh */
    }
    if (!!s->outline != (mesh_tune()[9] > 0.5f))
    {
        /*  Outline stands the roads aside and draws the fitted curves and
         *  the footway network on bare ground.  It is geometry, so the
         *  mesh is built again, and it is remembered like the grid. */
        mesh_tune()[9] = s->outline ? 1.0f : 0.0f;
        a->mesh_dirty  = 1;
        if (a->prefs_ok)
            prefs_set("curves", s->outline ? "on" : "off");
    }
    if (!!s->plan != !!a->plan)
        set_plan(a, s->plan, win);
    if (!!s->grid != !!a->gv.grid)
        if (a->prefs_ok)
            prefs_set("grid", s->grid ? "on" : "off"); /* the checkbox, remembered like the key */
    if (!!s->markings != !!a->gv.markings || !!s->furniture != !!a->gv.furniture || !!s->sidewalks != !!a->gv.sidewalks)
    {
        /*  The passes: markings are the shader's and a crosswalk band's,
         *  furniture and a junction's sidewalk band are built in, so any
         *  change rebuilds the mesh. */
        if (!!s->markings != !!a->gv.markings)
            if (a->prefs_ok)
                prefs_set("markings", s->markings ? "on" : "off");
        if (!!s->furniture != !!a->gv.furniture)
            if (a->prefs_ok)
                prefs_set("furniture", s->furniture ? "on" : "off");
        if (!!s->sidewalks != !!a->gv.sidewalks)
            if (a->prefs_ok)
                prefs_set("sidewalks", s->sidewalks ? "on" : "off");
        a->gv.markings  = s->markings;
        a->gv.furniture = s->furniture;
        a->gv.sidewalks = s->sidewalks;
        marking_enable(a->gv.markings);
        sidewalk_enable(a->gv.sidewalks); /* markings and sidewalks are draws: instant */
        if (furniture_on() != !!a->gv.furniture)
        {
            furniture_enable(a->gv.furniture); /* furniture is built in: a rebuild */
            a->mesh_dirty = 1;
        }
    }
    if (!!s->show_cells != !!a->cells_pref)
    {
        if (a->prefs_ok)
            prefs_set("cells", s->show_cells ? "on" : "off"); /* remembered too */
        a->cells_pref = s->show_cells;
    }
    a->gv.grid        = s->grid;
    a->gv.plain_sweep = s->plain_sweep;
    if (s->underground != a->opts.underground || s->view != a->opts.view)
    {
        if (s->underground != a->opts.underground)
            a->mesh_dirty = 1;
        a->opts.underground = s->underground;
        a->opts.view        = s->view;
        a->dirty            = 1;
    }
    if (s->want_zoom_step > 0)
        zoom_to(a, a->zoom_world * 2.0f, (float)pw * 0.5f, (float)ph * 0.5f);
    else if (s->want_zoom_step < 0)
        zoom_to(a, a->zoom_world * 0.5f, (float)pw * 0.5f, (float)ph * 0.5f);
    if (s->want_rotate)
        app_turn(a, s->want_rotate > 0 ? -90.0f : 90.0f, win);
    if (s->want_inspect)
    {
        a->inspect   = !a->inspect;
        a->inspect_c = a->inspect_r = -1;
    }
    if (s->want_quit)
        a->quit = 1;
    if (s->want_screenshot)
    {
        check_frame(a, win, "arcology-check.png");
        ui_log(s, "Wrote arcology-check.png");
    }
    if (s->want_load && s->load_path[0])
    {
        /*  adapt_city runs from a->city every frame, so swapping the
         *  city is all a load has to do. */
        City    *fresh = (City *)calloc(1, sizeof *fresh);
        uint64_t t0    = SDL_GetTicksNS();
        R_NOTE("city", "loading %s", s->load_path);
        if (fresh && city_load(s->load_path, fresh))
        {
            city_free(a->city);
            free(a->city);
            a->city = fresh;
            set_city_name(a, s->load_path);
            {
                /* opens paused; the saved speed is what unpausing resumes */
                int32_t saved = (int32_t)a->city->misc[MISC_SPEED];
                set_speed(a, 1);
                a->last_speed             = saved > 1 ? saved : 3;
                a->city->misc[MISC_SPEED] = (uint16_t)saved;
            }
            /*  the sweep and the terrain mesh are both cached, so a new
             *  city is not visible until they are asked to rebuild --
             *  without this the map keeps drawing the old one */
            a->dirty      = 1;
            a->mesh_dirty = 1;
            /*  and the UI's own copy of the speed, or the next frame
             *  compares the two and puts the old speed back */
            s->speed = a->speed;
            a->q_col = a->q_row = -1; /* the query box is about a gone tile */
            log_city_loaded(a->city, s->load_path, NS_MS(SDL_GetTicksNS() - t0));
            ui_log(s, "Loaded %s", a->city_base);
        }
        else
        {
            free(fresh);
            R_WARN("city", "could not load %s", s->load_path);
            ui_log(s, "Could not load %s", s->load_path);
        }
        s->want_load = 0;
    }
    if (s->want_music)
    {
        if (a->mus)
        {
            music_set_enabled(a->mus, !music_enabled(a->mus));
            s->music_on = music_enabled(a->mus);
            if (a->prefs_ok)
                prefs_set("music", s->music_on ? "on" : "off");
            ui_log(s, "Music %s", s->music_on ? "on" : "off");
        }
        s->want_music = 0;
    }
    if (s->want_theme)
    {
        char pick[64];
        snprintf(pick, sizeof pick, "%s", s->theme_name);
        if (apply_theme_choice(a, pick, "chosen"))
        {
            if (prefs_set("theme", s->theme_name))
                R_DBG("prefs", "theme=%s saved", s->theme_name);
            else
                R_WARN("prefs", "could not save the theme preference");
            ui_log(s, "Theme: %s", s->theme_name);
        }
        s->want_theme = 0;
    }
    if (s->want_save)
    {
        if (city_save(s->save_path, a->city))
            ui_log(s, "Saved %s", s->save_path);
        else
            ui_log(s, "Could not save %s", s->save_path);
    }
    if (s->want_disaster)
    {
        static const char *names[RUI_N_DISASTERS] = {
            "", "Fire", "Flood", "Riot", "Tornado", "Monster", "Earthquake", "Hurricane", "Chemical spill", "Air crash", "Microwave", "Meltdown", "Volcano", "Firestorm", "Pollution"};
        int r = 0;
        switch (s->want_disaster)
        {
            case RUI_DISASTER_FIRE:
                r = sim_disaster_fire(a->city);
                break;
            case RUI_DISASTER_FLOOD:
                r = sim_disaster_flood(a->city);
                break;
            case RUI_DISASTER_RIOT:
                r = sim_disaster_riot(a->city);
                break;
            case RUI_DISASTER_TORNADO:
                r = sim_disaster_tornado(a->city);
                break;
            case RUI_DISASTER_MONSTER:
                r = sim_disaster_monster(a->city);
                break;
            case RUI_DISASTER_EARTHQUAKE:
                r = sim_disaster_earthquake(a->city);
                break;
            case RUI_DISASTER_HURRICANE:
                r = sim_disaster_hurricane(a->city);
                break;
            case RUI_DISASTER_CHEMICAL:
                r = sim_disaster_chemical(a->city);
                break;
            case RUI_DISASTER_AIR_CRASH:
                r = sim_disaster_air_crash(a->city);
                break;
            case RUI_DISASTER_MICROWAVE:
                r = sim_disaster_microwave(a->city);
                break;
            case RUI_DISASTER_MELTDOWN:
                r = sim_disaster_meltdown(a->city);
                break;
            case RUI_DISASTER_VOLCANO:
                r = sim_disaster_volcano(a->city);
                break;
            case RUI_DISASTER_FIRESTORM:
                r = sim_disaster_firestorm(a->city);
                break;
            case RUI_DISASTER_POLLUTION:
                r = sim_disaster_pollution(a->city);
                break;
            default:
                break;
        }
        ui_log(s, "%s: %s", names[s->want_disaster], r ? "started" : "nothing happened");
        if (r)
        {
            static const int sounds[RUI_N_DISASTERS] = {
                0, R_SND_FIRE_LOOP, R_SND_FLOOD, R_SND_SHOT, R_SND_SIREN, R_SND_ROAR, R_SND_SIREN, R_SND_SIREN, R_SND_EXPLODE, R_SND_MAYDAY, R_SND_ZZAP, R_SND_EXPLODE, R_SND_EXPLODE, R_SND_FIRE_LOOP, 0};
            sound_play(a->snd, sounds[s->want_disaster]);
        }
        a->dirty      = 1;
        a->mesh_dirty = 1; /* a disaster may move the ground */
    }
    if (s->want_sound)
        sound_play(a->snd, s->want_sound);
    s->want_quit = s->want_screenshot = s->want_save = 0;
    s->want_load                                     = 0;
    s->want_disaster                                 = 0;
    s->want_zoom_step = s->want_rotate = s->want_sound = s->want_inspect = 0;
}

/*  What a tile sounds like when queried: the original lets a building be
 *  heard under the query tool.  By the building's name where the table
 *  has one and by the piece's range where it has not; 0 for silence. */
static int building_sound(const City *c, int32_t row, int32_t col)
{
    uint8_t     b    = c->xbld[row][col];
    uint8_t     zone = c->xzon[row][col] & 0x0F;
    const char *name = BUILDING[b].name;
    if (zone == 8)
        return R_SND_TAKEOFF;
    if (zone == 9)
        return R_SND_SHIP;
    if (name)
    {
        if (strstr(name, "Police"))
            return R_SND_POLICE;
        if (strstr(name, "Fire"))
            return R_SND_FIRETRUCK;
        if (strstr(name, "School") || strstr(name, "College"))
            return R_SND_SCHOOLBELL;
        if (strstr(name, "Prison"))
            return R_SND_PRISON;
        if (strstr(name, "Arco"))
            return R_SND_ARCO;
        if (strstr(name, "Wind"))
            return R_SND_WIND;
        if (strstr(name, "Nuclear") || strstr(name, "Fusion") || strstr(name, "Microwave"))
            return R_SND_ZZAP;
        if (strstr(name, "Stadium"))
            return R_SND_CHEERS;
        if (strstr(name, "Hospital"))
            return R_SND_SIREN;
    }
    if (b >= 0x1D && b <= 0x2B)
        return R_SND_HORNS; /* roads */
    if (b >= 0x2C && b <= 0x3A)
        return R_SND_TRAIN; /* rail  */
    if (b >= 0x0E && b <= 0x1C)
        return R_SND_ZZAP; /* power lines */
    if (b >= 0x51 && b <= 0x5F)
        return R_SND_HORNS; /* bridges and ramps */
    return 0;
}

/*  The selected tool on the tile under the pointer. */
void use_tool(App *a, SDL_Window *win)
{
    RUiState *s   = &a->us;
    int32_t   col = a->q_col, row = a->q_row;
    int       pw, ph;
    (void)win;
    if (s->tool < 0 || col < 0)
        return;
    switch (s->tool)
    {
        case RUI_TOOL_BULLDOZER:
            sim_demolish_tile(a->city, row, col, 0, 0);
            ui_log(s, "Bulldozed %d, %d", (int)col, (int)row);
            sound_play(a->snd, R_SND_BULLDOZE);
            a->dirty      = 1;
            a->mesh_dirty = 1;
            break;
        case RUI_TOOL_QUERY:
            s->show_query = 1;
            sound_play(a->snd, building_sound(a->city, row, col));
            break;
        case RUI_TOOL_CENTER:
            SDL_GetWindowSizeInPixels(win, &pw, &ph);
            {
                /*  Centre through the camera's own anchor rather than a
                 *  projection of its own.  The one that stood here was a
                 *  fourth transcription and it was wrong twice: it put
                 *  the origin half a tile out (tile_h / 2, not the
                 *  tile_h + 0.5 every other path uses), took no account
                 *  of pitch at all, and divided by gv.scale while
                 *  ignoring gv.zoom -- so the tile you clicked did not
                 *  land in the middle at any zoom but one, and never in
                 *  the map view. */
                int32_t idx   = row * R_MAP + col;
                a->anch_c     = (float)col + 0.5f;
                a->anch_r     = (float)row + 0.5f;
                a->anch_alt   = (float)rcity_alt_surface(a->view->altm[idx], a->view->xter[idx]);
                a->gv.pivot_c = a->anch_c;
                a->gv.pivot_r = a->anch_r;
                cam_hold(a, win);
            }
            break;
        default:
            ui_log(s, "%s: not yet ported", RUI_TOOL_NAME[s->tool]);
            sound_play(a->snd, R_SND_ERROR);
            break;
    }
}
