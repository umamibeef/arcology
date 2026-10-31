/*  inspect.c: the inspector.  It finds what is under the pointer from the
 *  mesh, and the road network's record the shape there names
 *  (app/net_pick.h).  It finds the components of that record to outline.  It hands the UI its
 *  rows and its outlines, files a remark, and answers the inspector's
 *  clicks and keys. */
#include "internal.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "dump.h"
#include "log.h"
#include "network/network.h"
#include "network/store.h"
#include "opt.h"

static void inspect_toggle(App *a);

R_MAP_BUF(static, int32_t, s_pick_stamp, 1) /*  The pick's stamp on each tile, so a tile is tested once a pick. */

/*  The point of triangle `tri` under the pointer, and the shape that holds
 *  the triangle: 0 where the pointer is not on it.  The pointer's ray
 *  climbs toward the camera.  So of two surfaces under the pointer, the
 *  one in front has its point higher.
 *
 *  A triangle with no area on screen covers no point.  It has to be turned
 *  away before the edge tests.  A face seen edge on projects to a line.
 *  The pointer is then on one side of all three of its edges wherever it
 *  is.  The oblique camera makes such faces of every wall and skirt that
 *  runs along the view. */
static int inspect_hit(const App *a, uint32_t tri, float dens, float hit[3], uint32_t *comp)
{
    float p3[3][3], sx[3], sy[3], d1, d2, d3, area, w0, w1;
    int   v;
    mesh_tri_get(&a->mesh, tri, p3, comp);
    for (v = 0; v < 3; ++v)
        grid_to_window(a, p3[v][0], p3[v][1], p3[v][2], dens, &sx[v], &sy[v]);
    area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (area > -1e-3f && area < 1e-3f)
        return 0;
    d1 = (a->q_mx - sx[1]) * (sy[0] - sy[1]) - (sx[0] - sx[1]) * (a->q_my - sy[1]);
    d2 = (a->q_mx - sx[2]) * (sy[1] - sy[2]) - (sx[1] - sx[2]) * (a->q_my - sy[2]);
    d3 = (a->q_mx - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (a->q_my - sy[0]);
    if (((d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f)) && ((d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f)))
        return 0;
    /*  Where between its corners the pointer lies. */
    w0 = ((sy[1] - sy[2]) * (a->q_mx - sx[2]) + (sx[2] - sx[1]) * (a->q_my - sy[2])) / area;
    w1 = ((sy[2] - sy[0]) * (a->q_mx - sx[2]) + (sx[0] - sx[2]) * (a->q_my - sy[2])) / area;
    for (v = 0; v < 3; ++v)
        hit[v] = w0 * p3[0][v] + w1 * p3[1][v] + (1.0f - w0 - w1) * p3[2][v];
    return 1;
}

/*  Whether the mesh under the pointer is picked: with the inspector on.
 *  The query marks the tiles under the pointer instead (uiglue.c). */
static int picking(const App *a)
{
    return a->insp.on;
}

/*  The object under the pointer: the triangle there, and the record its
 *  shape names (net_pick_record).  Any other triangle stands for the thing
 *  that drew it.  It is found again only when the pointer moves or the
 *  world is built again.  A selected object stays.  A new record shows the
 *  level the player last stepped to, or the nearest level it has. */
static void hover(App *a, const float *hit, uint32_t comp)
{
    static uint32_t built_land;
    static int      built_roads = -1, built_on = -1;
    int             lv;
    NetPick         np;
    if (a->mesh.n_land != built_land || network_roads() != built_roads || a->insp.on != built_on)
    {
        net_pick_forget();
        built_land = a->mesh.n_land, built_roads = network_roads(), built_on = a->insp.on;
        a->insp.mx = -1e30f;
    }
    if (a->insp.pinned || ((a->insp.net_ok || a->insp.thing_ok) && a->q_mx == a->insp.mx && a->q_my == a->insp.my && a->insp.shown == comp))
        return;
    a->insp.mx = a->q_mx, a->insp.my = a->q_my, a->insp.shown = comp;
    a->insp.thing_ok = 0, a->insp.net_ok = 0;
    if (!hit)
        return;
    if (!net_pick_at(comp, hit, &np))
    {
        a->insp.thing_ok = 1, a->insp.thing = comp;
        return;
    }
    for (lv = a->insp.level_set ? a->insp.level : NPICK_LANE;
         lv < NPICK_LEVELS && !net_pick_has(&np, lv);
         ++lv)
        ;
    for (; lv >= 0 && !net_pick_has(&np, lv); --lv)
        ;
    np.level = lv < 0 ? np.level : lv;
    net_pick_text(&np, a->insp.text, sizeof a->insp.text);
    a->insp.net    = np;
    a->insp.net_ok = 1;
}

/*  Step the record shown to another level. */
static void step_level(App *a, int level)
{
    if (!a->insp.net_ok || level < 0 || level >= NPICK_LEVELS || !net_pick_has(&a->insp.net, level))
        return;
    a->insp.net.level = a->insp.level = level, a->insp.level_set = 1;
    net_pick_text(&a->insp.net, a->insp.text, sizeof a->insp.text);
}

/*  A remark on the record shown, put on the clipboard, and added to
 *  debug/remarks.md where `to_file`.  The entry holds the remark and the
 *  record's levels by the names the dump uses.  It also holds the camera
 *  and everything the records say of it. */
static void file_remark(App *a, RUiState *s, int to_file)
{
    static char entry[12288];
    char        when[32], name[120];
    size_t      at  = 0;
    time_t      now = time(NULL);
    struct tm   tmv;
    FILE       *fp;
    int         k;
    if (!a->insp.net_ok || !s->insp.remark[0])
        return;
    localtime_r(&now, &tmv);
    strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tmv);
    net_pick_name(&a->insp.net, a->insp.net.level, name, sizeof name);
    at += (size_t)snprintf(entry + at, sizeof entry - at, "## %s, %s: %s\n\n%s\n\n", when, a->city_base[0] ? a->city_base : "city", name, s->insp.remark);
    for (k = NPICK_LANE; k < NPICK_LEVELS && at < sizeof entry; ++k)
        if (net_pick_has(&a->insp.net, k))
        {
            net_pick_name(&a->insp.net, k, name, sizeof name);
            at += (size_t)snprintf(entry + at, sizeof entry - at, "- %s%s\n", name, k == a->insp.net.level ? "  (the one remarked on)" : "");
        }
    if (at < sizeof entry)
        at += (size_t)snprintf(entry + at, sizeof entry - at, "- the point %.3f,%.3f at height %.3f, tile %d,%d; the camera at zoom %.2f, yaw %.1f, pitch %.1f\n\n```\n%s```\n\n", (double)a->insp.net.hit[0], (double)a->insp.net.hit[1], (double)a->insp.net.hit[2], (int)a->q_col, (int)a->q_row, (double)a->zoom_world, (double)a->yaw_now, (double)a->gv.pitch, a->insp.text);
    SDL_SetClipboardText(entry);
    if (!to_file)
    {
        snprintf(s->insp.remark_said, sizeof s->insp.remark_said, "Copied to the clipboard");
        ui_log(s, "%s", s->insp.remark_said);
        return;
    }
    mkdir("debug", 0755);
    fp = fopen("debug/remarks.md", "a");
    if (fp)
    {
        fputs(entry, fp);
        fclose(fp);
    }
    ++a->insp.remarks;
    snprintf(s->insp.remark_said, sizeof s->insp.remark_said, "%s debug/remarks.md (remark %d this run), and copied to the clipboard", fp ? "Added to" : "Could not write", a->insp.remarks);
    ui_log(s, "%s", s->insp.remark_said);
    s->insp.remark[0] = 0;
}

/*  THE OBJECTS SHOWN, as groups of triangles, one group a component, all
 *  read from the mesh.  On a road the components are each lane's shape,
 *  what is drawn for the road beside its lanes, and the junction at each
 *  end.  In a junction they are the junction and each of its connecting
 *  roads the same way.  A road drawn with no lane of its own is one
 *  component.  The record the window describes is bright.  A thing the
 *  network does not hold is one group, its triangles and those of every
 *  shape under it.  The groups are made again only when the record or the
 *  world changes, and projected again every frame. */
#define OBJ_TRIS_MAX 196608
#define OBJ_GROUPS   512
static float    s_obj3[OBJ_TRIS_MAX][3][3];
static float    s_obj2[OBJ_TRIS_MAX][3][2];
static float    s_objt[OBJ_TRIS_MAX][3][3];
static uint16_t s_objg[OBJ_TRIS_MAX];
static int      s_obj_grp[OBJ_GROUPS][4], s_obj_ngrp, s_obj_n;

/*  The color of a lane, by its type and its road's network. */
static int lane_color(const NLane *l, int net)
{
    switch (l->type)
    {
        case NLANE_ENTRY:
        case NLANE_EXIT:
            return RUI_OBJ_CHANGE;
        case NLANE_ONRAMP:
        case NLANE_OFFRAMP:
        case NLANE_CONNRAMP:
            return RUI_OBJ_RAMP;
        case NLANE_SHOULDER:
            return RUI_OBJ_SHOULDER;
        case NLANE_MEDIAN:
            return RUI_OBJ_MEDIAN;
        case NLANE_SIDEWALK:
            return RUI_OBJ_SIDEWALK;
        case NLANE_RAIL:
            return RUI_OBJ_RAIL;
        case NLANE_BORDER:
            return RUI_OBJ_BORDER;
        default:
            return net == RNET_BAND ? RUI_OBJ_HIGHWAY : RUI_OBJ_STREET;
    }
}

/*  A road's own color, where it is drawn with no lane of its own. */
static int road_color(const NRoad *r)
{
    return r->net == RNET_RAIL ? RUI_OBJ_RAIL : r->net == RNET_BAND ? RUI_OBJ_HIGHWAY
                                                                    : RUI_OBJ_STREET;
}

static void obj_build(App *a)
{
    static uint16_t *grp;
    static int      *rec;
    static uint32_t  cap;
    static int       gcolor[OBJ_GROUPS], gbright[OBJ_GROUPS], glane[OBJ_GROUPS], groad[OBJ_GROUPS], gjunc[OBJ_GROUPS];
    static int       count[OBJ_GROUPS + 1], fill[OBJ_GROUPS + 1];
    const NetPick   *p  = &a->insp.net;
    uint32_t         ns = (uint32_t)shape_count(), i;
    int              ng = 0, n, g, t, show[256], laned[256], nshow = 0, junc[2], njunc = 0, q;
    s_obj_n = s_obj_ngrp = 0;
    if (ns > cap)
    {
        uint16_t *ng2 = (uint16_t *)realloc(grp, ns * sizeof *ng2);
        int      *nr2 = (int *)realloc(rec, 3u * ns * sizeof *nr2);
        if (ng2)
            grp = ng2;
        if (nr2)
            rec = nr2;
        if (!ng2 || !nr2)
            return;
        cap = ns;
    }
    memset(grp, 0, ns * sizeof *grp);
    if (a->insp.thing_ok)
    {
        for (i = 0; i < ns; ++i)
            grp[i] = i == a->insp.thing || shape_is_ancestor(a->insp.thing, i);
        gcolor[0] = RUI_OBJ_THING, gbright[0] = 1, ng = 1;
    }
    else
    {
        net_pick_load(p);
        /*  The roads shown and the junctions shown.  In a junction they are
         *  its connecting roads.  On a road they are the road and the
         *  junction at each end. */
        if (p->junction >= 0 && (p->level == NPICK_JUNCTION || p->road < 0 || network_road(p->road)->junction == p->junction))
            nshow = net_pick_junction_roads(p->junction, show, 256), junc[njunc++] = p->junction;
        else if (p->road >= 0)
        {
            const NRoad *r = network_road(p->road);
            show[nshow++]  = p->road;
            if (r->pred.kind == NLINK_JUNCTION)
                junc[njunc++] = r->pred.index;
            if (r->succ.kind == NLINK_JUNCTION && !(njunc && junc[0] == r->succ.index))
                junc[njunc++] = r->succ.index;
        }
        memset(laned, 0, sizeof laned);
        /*  Each shape's records, once, and which roads shown are drawn
         *  lane by lane. */
        for (i = 0; i < ns; ++i)
        {
            int *rc = rec + 3 * i;
            if (!net_pick_record(i, &rc[0], &rc[1], &rc[2]))
                rc[0] = rc[1] = rc[2] = -1;
            for (q = 0; rc[1] >= 0 && q < nshow; ++q)
                laned[q] |= show[q] == rc[0];
        }
        /*  Each shape to its group: a lane's, its road's, a junction's. */
        for (i = 0; i < ns; ++i)
        {
            const int *rc   = rec + 3 * i;
            int        road = -1, jn = -1, lane = rc[1];
            for (q = 0; rc[0] >= 0 && q < nshow && show[q] != rc[0]; ++q)
                ;
            if (rc[0] >= 0 && q < nshow)
                road = rc[0];
            else
            {
                for (q = 0; rc[2] >= 0 && q < njunc && junc[q] != rc[2]; ++q)
                    ;
                if (rc[0] >= 0 || rc[2] < 0 || q == njunc)
                    continue;
                jn = rc[2], lane = -1;
            }
            for (g = 0; g < ng && !(groad[g] == road && glane[g] == lane && gjunc[g] == jn); ++g)
                ;
            if (g == ng)
            {
                if (ng == OBJ_GROUPS)
                    continue;
                groad[g] = road, glane[g] = lane, gjunc[g] = jn, ++ng;
                if (jn >= 0)
                    gcolor[g] = RUI_OBJ_JUNCTION, gbright[g] = p->level == NPICK_JUNCTION && jn == p->junction;
                else
                {
                    const NRoad *r = network_road(road);
                    for (q = 0; q < nshow && show[q] != road; ++q)
                        ;
                    /*  A road drawn lane by lane draws its structure beside
                     *  its lanes.  One drawn whole is one component. */
                    gcolor[g]  = lane >= 0 ? lane_color(network_lane(lane), r->net) : laned[q] ? RUI_OBJ_STRUCTURE
                                                                                               : road_color(r);
                    gbright[g] = (p->level == NPICK_LANE && lane >= 0 && lane == p->lane) ||
                                 (p->level == NPICK_SECTION && lane >= 0 && network_lane(lane)->section == p->section) ||
                                 (p->level == NPICK_ROAD && road == p->road) || (p->level == NPICK_JUNCTION && r->junction == p->junction);
                }
            }
            grp[i] = (uint16_t)(g + 1);
        }
    }
    /*  Every triangle of every group, in one pass, then laid out group
     *  after group. */
    n = mesh_tris_by(&a->mesh, grp, ns, s_objt, s_objg, OBJ_TRIS_MAX);
    memset(count, 0, sizeof count);
    for (t = 0; t < n; ++t)
        ++count[s_objg[t]];
    for (g = 1, fill[1] = 0; g < ng; ++g)
        fill[g + 1] = fill[g] + count[g];
    for (t = 0; t < n; ++t)
        memcpy(s_obj3[fill[s_objg[t]]++], s_objt[t], sizeof s_obj3[0]);
    for (g = 0, t = 0; g < ng; ++g)
    {
        if (count[g + 1] > 0 && s_obj_ngrp < OBJ_GROUPS)
        {
            s_obj_grp[s_obj_ngrp][0] = t, s_obj_grp[s_obj_ngrp][1] = count[g + 1];
            s_obj_grp[s_obj_ngrp][2] = gcolor[g], s_obj_grp[s_obj_ngrp][3] = gbright[g];
            ++s_obj_ngrp;
        }
        t += count[g + 1];
    }
    s_obj_n = n;
}

/*  The object shown, handed to the UI: its record's levels and names, and
 *  its groups projected into window points as the camera projects the
 *  mesh. */
static void show_objects(App *a, RUiState *s, float dens)
{
    static NetPick  key_net;
    static int      key_ok = -1, key_thing = -1;
    static uint32_t key_comp, key_land;
    int             k, v, t;
    s->insp.rec_ok = picking(a) && a->insp.net_ok;
    s->insp.ngrp   = 0;
    if (!picking(a) || (!a->insp.net_ok && !a->insp.thing_ok))
        return;
    if (key_ok != a->insp.net_ok || key_thing != a->insp.thing_ok || key_land != a->mesh.n_land || (a->insp.thing_ok && key_comp != a->insp.thing) ||
        (a->insp.net_ok && memcmp(&key_net, &a->insp.net, sizeof key_net) != 0))
    {
        obj_build(a);
        key_ok = a->insp.net_ok, key_thing = a->insp.thing_ok, key_land = a->mesh.n_land, key_comp = a->insp.thing, key_net = a->insp.net;
    }
    for (t = 0; t < s_obj_n; ++t)
        for (v = 0; v < 3; ++v)
        {
            grid_to_window(a, s_obj3[t][v][0], s_obj3[t][v][1], s_obj3[t][v][2], dens, &s_obj2[t][v][0], &s_obj2[t][v][1]);
        }
    s->insp.tri  = &s_obj2[0][0][0];
    s->insp.ngrp = s_obj_ngrp;
    memcpy(s->insp.grp, s_obj_grp, sizeof s->insp.grp);
    s->insp.pinned = a->insp.pinned;
    if (!a->insp.pinned)
        s->insp.at[0] = a->q_mx, s->insp.at[1] = a->q_my;
    if (!a->insp.net_ok)
        return;
    s->insp.level = a->insp.net.level;
    s->insp.text  = a->insp.text;
    for (k = 0; k < NPICK_LEVELS; ++k)
    {
        s->insp.has[k] = net_pick_has(&a->insp.net, k);
        net_pick_name(&a->insp.net, k, s->insp.name[k], sizeof s->insp.name[k]);
    }
}

void inspect_copy(App *a)
{
    const RUiState *s = &a->us;
    char            buf[4096];
    size_t          n = 0;
    const char     *p;
    int             k;
    if (!a->insp.on)
        return;
    if (!s->insp.who[0] && !s->insp.label[0])
    {
        ui_log(&a->us, "Nothing under the pointer to copy");
        return;
    }
#define ADD(...) (n < sizeof buf ? n += (size_t)snprintf(buf + n, sizeof buf - n, __VA_ARGS__) : 0)
    ADD("%s\n", s->insp.label[0] ? s->insp.label : "the mesh");
    ADD("  rendered by   %s at %s\n", s->insp.who[0] ? s->insp.who : "?", s->insp.where[0] ? s->insp.where : "?");
    ADD("  part of       %s%s%s\n", s->insp.part_of[0] ? s->insp.part_of : "nothing", s->insp.part_of[0] && s->insp.part_of_where[0] ? ", at " : "", s->insp.part_of[0] ? s->insp.part_of_where : "");
    ADD("  material      %g\n", (double)s->insp.mat);
    if (s->insp.part_of_tris > s->insp.tris)
        ADD("  triangles     %u, %u with what is under it\n", s->insp.tris, s->insp.part_of_tris);
    else
        ADD("  triangles     %u\n", s->insp.tris);
    ADD("  tile          %d,%d\n", (int)s->q_col, (int)s->q_row);
    if (s->insp.box_ok)
    {
        ADD("  extent        %.3f,%.3f to %.3f,%.3f\n", (double)s->insp.box[0], (double)s->insp.box[1], (double)s->insp.box[3], (double)s->insp.box[4]);
        ADD("  height        %.3f to %.3f\n", (double)s->insp.box[2], (double)s->insp.box[5]);
    }
    /*  The shape's own account of itself, a row a line: the key before
     *  the tab, the value after it. */
    for (p = s->insp.note; *p;)
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
    for (k = 0; k < s->insp.n_also; ++k)
        ADD("  drawn over    %s\n", s->insp.also[k]);
#undef ADD
    SDL_SetClipboardText(buf);
    ui_log(&a->us, "Copied \"%s\" to the clipboard", s->insp.label[0] ? s->insp.label : "the mesh");
}

/*  The inspector's part of a frame: what is under the pointer, and what
 *  the window and the outline show of it. */
void inspect_fill(App *a, RUiState *s)
{
    /*  --inspect: the inspector on from the start.  Once, before the
     *  frame reads the switch.  Applied every frame it could never be
     *  switched off again. */
    static int flag_applied;
    if (!flag_applied)
    {
        flag_applied = 1;
        if (g_dev.inspect)
            a->insp.on = 1;
    }
    /*  The inspector points at the MESH.  The triangles near the pointer
     *  are projected the way the camera projects them.  The one under
     *  the pointer is found.  Through it comes the shape that drew it,
     *  and the record that shape names.  A tile is
     *  only how the search is narrowed.  The ground is not a thing to
     *  inspect.  The query tool says what a tile is.  So the ground, its
     *  banks, walls and water are passed over, and what is built on them
     *  is what the pointer finds. */
    s->insp.show     = a->insp.on;
    s->insp.box_ok   = 0;
    s->insp.label[0] = s->insp.who[0] = s->insp.where[0] = s->insp.part_of[0] = s->insp.part_of_where[0] = s->insp.note[0] = 0;
    s->insp.n_also                                                                                                         = 0;
    s->insp.mat                                                                                                            = 0.0f;
    s->insp.tris = s->insp.part_of_tris = 0;
    if (picking(a) && a->q_col >= 0 && a->q_row >= 0 && a->sw.level && !a->building)
    {
        float    dens = a->win_density > 0.0f ? a->win_density : 1.0f;
        uint32_t cand[4096];
        int      best      = -1;
        float    best_z    = -1e9f;
        uint32_t best_comp = 0;
        /*  Every thing the pointer is over, not only the topmost.  The
         *  one in front tells you what you clicked, and the rest tell
         *  you what it is drawn over.  Where two of them meet at one
         *  height the surfaces overlap, and no outline of either can
         *  show that. */
        enum
        {
            MAX_ALSO = 16
        };
        uint32_t acomp[MAX_ALSO];
        float    ahit[MAX_ALSO][3];
        int      n_also = 0;
        /*  Every tile whose geometry could be under the pointer, not a
         *  patch of tiles around the GROUND tile.  In this projection
         *  height moves a thing up the screen.  So the tile a raised
         *  thing belongs to can be three or four tiles from the ground
         *  the pointer lands on.  That is further than any fixed patch
         *  reaches.
         *
         *  For a fixed window point the difference row - col is settled.
         *  Only the sum grows with altitude, so the candidates lie along
         *  a diagonal.  Walk the altitude from under the ground to above
         *  anything built.  Take the tile at each step and its
         *  neighbors.  Test each tile's triangles as it is reached.
         *  There is no shared budget.  So the densest place on the map
         *  cannot spend it before another tile is looked at. */
        {
            int32_t       *stamp = s_pick_stamp;
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
                        if (tc < 0 || tr < 0 || tc >= r_map || tr >= r_map)
                            continue;
                        idx = tr * r_map + tc;
                        if (stamp[idx] == gen)
                            continue;
                        stamp[idx] = gen;
                        ntri       = mesh_tris_at(&a->mesh, tc, tr, cand, (int)(sizeof cand / sizeof *cand));
                        for (ktri = 0; ktri < ntri; ++ktri)
                        {
                            float    hit[3];
                            uint32_t comp;
                            int      s2;
                            if (!inspect_hit(a, cand[ktri], dens, hit, &comp) || !shape_built(comp))
                                continue;
                            if (best < 0 || hit[2] > best_z)
                                best = (int)cand[ktri], best_z = hit[2], best_comp = comp;
                            /*  By SURFACE, not by thing: a face drawn over
                             *  another of its own thing is the overlap
                             *  worth seeing. */
                            for (s2 = 0; s2 < n_also; ++s2)
                                if (acomp[s2] == comp)
                                    break;
                            if (s2 < n_also)
                            {
                                if (hit[2] > ahit[s2][2])
                                    memcpy(ahit[s2], hit, sizeof hit);
                            }
                            else if (n_also < MAX_ALSO)
                                acomp[n_also] = comp, memcpy(ahit[n_also], hit, sizeof hit), ++n_also;
                        }
                    }
            }
        }
        /*  What is under the pointer, the highest first.  A second click
         *  in one place shows the next record or thing down this stack,
         *  as clicking through stacked objects does. */
        {
            int i2, j2, nrec = 0, pick = -1;
            int seen[MAX_ALSO][3], first[MAX_ALSO];
            for (i2 = 1; i2 < n_also; ++i2)
                for (j2 = i2; j2 > 0 && ahit[j2][2] > ahit[j2 - 1][2]; --j2)
                {
                    uint32_t tc = acomp[j2];
                    float    th[3];
                    memcpy(th, ahit[j2], sizeof th), memcpy(ahit[j2], ahit[j2 - 1], sizeof th), memcpy(ahit[j2 - 1], th, sizeof th);
                    acomp[j2] = acomp[j2 - 1], acomp[j2 - 1] = tc;
                }
            /*  One entry a record, or a thing where no record is named. */
            for (i2 = 0; i2 < n_also; ++i2)
            {
                int r3[3], q;
                net_store_use_at((int32_t)floorf(ahit[i2][0]), (int32_t)floorf(ahit[i2][1]));
                if (!net_pick_record(acomp[i2], &r3[0], &r3[1], &r3[2]))
                    r3[0] = -2 - (int)acomp[i2], r3[1] = r3[2] = -1;
                for (q = 0; q < nrec && memcmp(seen[q], r3, sizeof r3) != 0; ++q)
                    ;
                if (q < nrec)
                    continue;
                memcpy(seen[nrec], r3, sizeof r3);
                first[nrec++] = i2;
            }
            if (nrec)
                pick = first[a->insp.depth % nrec];
            s->insp.depth  = nrec ? a->insp.depth % nrec : 0;
            s->insp.ndepth = nrec;
            hover(a, pick >= 0 ? ahit[pick] : NULL, pick >= 0 ? acomp[pick] : SHAPE_NONE);
        }
        if (best >= 0)
        {
            const char *who = NULL, *where = NULL;
            float       mat  = 0.0f, box[6];
            uint32_t    tris = 0;
            {
                /*  What it is part of, all the way up, so the chain reads
                 *  from the nearest owner outwards. */
                ShapeId par   = shape_parent(best_comp);
                size_t  used  = 0;
                int     depth = 0;
                while (par != SHAPE_NONE && depth++ < 8 && used + 1 < sizeof s->insp.part_of)
                {
                    const char *nm = shape_name(par);
                    used += (size_t)snprintf(s->insp.part_of + used, sizeof s->insp.part_of - used, "%s%s", used ? ", in " : "", nm ? nm : "?");
                    if (depth == 1)
                    {
                        const char *pw  = shape_where(par);
                        const char *rel = pw ? strstr(pw, "src/") : NULL;
                        snprintf(s->insp.part_of_where, sizeof s->insp.part_of_where, "%s", rel ? rel : (pw ? pw : ""));
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
                snprintf(s->insp.who, sizeof s->insp.who, "%s", who ? who : "?");
                snprintf(s->insp.where, sizeof s->insp.where, "%s", rel ? rel : (where ? where : ""));
                snprintf(s->insp.label, sizeof s->insp.label, "%s", shape_name(best_comp) ? shape_name(best_comp) : "mesh");
                s->insp.mat  = mat;
                s->insp.tris = tris;
                memcpy(s->insp.box, box, sizeof s->insp.box);
                s->insp.box_ok = 1;
                /*  The thing the component belongs to, when it was made
                 *  under a generating call: its triangles in all, and the
                 *  box round all of it. */
                if (shape_get_deep(best_comp, &tris, box) == 0)
                {
                    s->insp.part_of_tris = tris;
                    memcpy(s->insp.box, box, sizeof s->insp.box);
                }
            }
            {
                /*  The generating call's own account of the thing: its
                 *  first line is the name the outline wears, the rest are
                 *  the window's rows. */
                const char *note = shape_note_of(best_comp);
                if (note)
                    snprintf(s->insp.note, sizeof s->insp.note, "%s", note);
            }
            {
                /*  What else is under the pointer, the highest first, each
                 *  with the material it is drawn in and the height it was
                 *  met at.  A surface at the same height as the one in
                 *  front is drawn over it. */
                int i2;
                for (i2 = 0; i2 < n_also && s->insp.n_also < (int)(sizeof s->insp.also / sizeof s->insp.also[0]); ++i2)
                {
                    const char *nt = shape_note_of(acomp[i2]), *wh = NULL;
                    char        name[80];
                    float       mat2 = 0.0f;
                    if (acomp[i2] == best_comp)
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
                    snprintf(s->insp.also[s->insp.n_also], sizeof s->insp.also[0], "%s, material %g, at %.3f", name, (double)mat2, (double)ahit[i2][2]);
                    ++s->insp.n_also;
                }
            }
            s->insp.at[0] = a->q_mx;
            s->insp.at[1] = a->q_my;
            /*  One line when the component under the pointer changes,
             *  and never once a frame: it is a message.  So it goes
             *  through the log like every other. */
            if (g_dev.inspect && best_comp != a->insp.logged)
            {
                a->insp.logged = best_comp;
                R_DBG("inspect", "%d,%d: %s generated by %s (%s), drawn by %s (%s), material %g, %u triangles", (int)a->q_col, (int)a->q_row, s->insp.label, s->insp.part_of[0] ? s->insp.part_of : "-", s->insp.part_of_where, s->insp.who, s->insp.where, (double)s->insp.mat, s->insp.tris);
            }
        }
    }
    show_objects(a, s, a->win_density > 0.0f ? a->win_density : 1.0f);
}

/*  What the window asked of the inspector this frame. */
void inspect_apply(App *a, RUiState *s)
{
    if (s->want.inspect)
        inspect_toggle(a);
    if (s->want.level)
        step_level(a, s->want.level - 1);
    if (s->want.remark)
        file_remark(a, s, s->want.remark == 1);
}

static void inspect_toggle(App *a)
{
    a->insp.on = !a->insp.on;
}

int inspect_click(App *a, float x, float y, SDL_Window *win)
{
    /*  A click while inspecting selects what is under the pointer, so it
     *  can be looked at and remarked on.  A second click in the same place
     *  selects what lies under it, as clicking through stacked objects
     *  does.  A click on nothing, or Escape, clears the selection. */
    float dx = x - a->insp.click[0], dy = y - a->insp.click[1];
    if (!a->insp.on || a->building)
        return 0;
    a->insp.depth  = a->insp.pinned && dx * dx + dy * dy < 16.0f ? a->insp.depth + 1 : 0;
    a->insp.pinned = 0;
    pick_tile(a, x, y, win);
    a->win_density   = SDL_GetWindowPixelDensity(win);
    a->insp.net_ok   = 0;
    a->insp.thing_ok = 0;
    inspect_fill(a, &a->us);
    a->insp.pinned   = a->insp.net_ok || a->insp.thing_ok;
    a->insp.click[0] = x, a->insp.click[1] = y;
    return 1;
}

int inspect_key(App *a, SDL_Keycode k, int shift)
{
    /*  Escape clears the selection.  As a design tool's layers do, Enter
     *  selects the child, the road's lane section and then its lane.
     *  Shift-Enter selects the parent, up to the road and its junction. */
    if (!a->insp.on)
        return 0;
    if (k == SDLK_ESCAPE && a->insp.pinned)
    {
        a->insp.pinned = 0;
        return 1;
    }
    if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && a->insp.net_ok)
    {
        int lv = a->insp.net.level, step = shift ? 1 : -1;
        for (lv += step; lv >= 0 && lv < NPICK_LEVELS && !net_pick_has(&a->insp.net, lv); lv += step)
            ;
        step_level(a, lv);
        return 1;
    }
    return 0;
}

/*  --pick with --inspect: what the inspector says of the point, to the
 *  dump.  1 where the mesh under the point was inspected. */
int inspect_dump(const App *a)
{
    const RUiState *u = &a->us;
    const char     *p;
    int             lv, rc;
    if (u->insp.who[0])
    {
        dumpf("inspect   %s: drawn by %s at %s, part of %s, material %g, %u triangles (%u with what is under it)\n", u->insp.label, u->insp.who, u->insp.where, u->insp.part_of[0] ? u->insp.part_of : "nothing", (double)u->insp.mat, u->insp.tris, u->insp.part_of_tris > u->insp.tris ? u->insp.part_of_tris : u->insp.tris);
        for (p = u->insp.note; *p;)
        {
            const char *e   = strchr(p, '\n');
            int         len = (int)(e ? (size_t)(e - p) : strlen(p));
            dumpf("inspect     %.*s\n", len, p);
            p = e ? e + 1 : p + len;
        }
        if (u->insp.box_ok)
            dumpf("inspect     extent\t%.3f,%.3f to %.3f,%.3f, height %.3f to %.3f\n", (double)u->insp.box[0], (double)u->insp.box[1], (double)u->insp.box[3], (double)u->insp.box[4], (double)u->insp.box[2], (double)u->insp.box[5]);
        for (rc = 0; rc < u->insp.n_also; ++rc)
            dumpf("inspect     drawn over\t%s\n", u->insp.also[rc]);
    }
    if (u->insp.rec_ok)
    {
        for (lv = 0; lv < NPICK_LEVELS; ++lv)
            if (u->insp.has[lv])
                dumpf("network   %s%s\n", u->insp.name[lv], lv == u->insp.level ? "  (shown)" : "");
        dumpf("network   outlined as %d groups\n", u->insp.ngrp);
        for (lv = 0; lv < u->insp.ngrp; ++lv)
            dumpf("network     group %d: %d triangles, color %d\n", lv, u->insp.grp[lv][1], u->insp.grp[lv][2]);
        for (p = a->insp.text; *p;)
        {
            const char *e   = strchr(p, '\n');
            int         len = (int)(e ? (size_t)(e - p) : strlen(p));
            dumpf("network     %.*s\n", len, p);
            p = e ? e + 1 : p + len;
        }
    }
    return u->insp.who[0] != 0;
}
