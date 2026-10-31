/*  uiglue.c: the two joins between the app and its interface.
 *
 *  ui.h's contract is that the app fills what the windows SHOW once a
 *  frame.  The windows edit switches in place and raise commands, and
 *  the app applies them and clears them.  Those two directions are
 *  ui_fill and ui_apply, and they are the only places either side
 *  touches the other.  The tool in hand is applied here for the same
 *  reason. */
#include "adapt.h"
#include "area_report.h"
#include "atlas/atlas.h"
#include "draw/net_graph.h"
#include "geo.h"
#include "internal.h"
#include "log.h"
#include "network/network.h"
#include "network/topology.h"
#include "sim.h"
#include "trees.h"
#include "turn.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const GRAPH_NAMES[RUI_N_GRAPH] = {
    "City size", "Residents", "Commerce", "Industry", "Traffic", "Pollution", "Land value", "Crime", "Power shortfall", "Water shortfall", "Health", "Education", "Unemployment", "National GNP", "National population", "Federal rate"};
/*  The budget block's departments as sim.h names them.  The first three
 *  slots the reconstruction has not named. */
static const char *const DEPT_NAMES[RUI_N_DEPT] = {
    NULL, NULL, NULL, "Ordinances", "Bonds", "Police", "Fire", "Health", "Schools", "Colleges", "Roads", "Bands", "Subways", "Rail", "Transit", "Power"};

/*  ==================================================================
 *  The interface, filled and applied
 *
 *  Everything the windows read is copied in once a frame, and everything
 *  they change is applied once a frame.  The interface never reaches
 *  into the app itself.
 *  ================================================================== */
/*  The four projected corners of a tile, put in order around their own
 *  middle.  They are computed in the grid's order, NW, NE, SE, SW, and
 *  in the map view that order happens to trace the tile's boundary.
 *  Under the game's own camera the yaw turns which corner lands where on
 *  screen.  The fixed order then crosses itself, and the outline came
 *  out a thin dart instead of a tile.  Sorting by angle costs nothing
 *  and holds at any camera. */
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

/*  A tile's four corner heights, NW, NE, SE, SW.  They come from the
 *  copy that the last world left (world.c).  A build under way does not
 *  touch that copy.  With no copy for this view they come from the mesh,
 *  and only while no build is under way. */
static int ground_corners(const App *a, int32_t col, int32_t row, int ug, float z[4])
{
    if (col < 0 || row < 0 || col >= r_map || row >= r_map)
        return -1;
    if (a->ground_n == r_map && a->ground_under == ug)
    {
        memcpy(z, &a->ground[((size_t)row * (size_t)r_map + (size_t)col) * 4u], 4 * sizeof(float));
        return 0;
    }
    if (a->building)
        return -1;
    return mesh_tile_corners(a->view, col, row, ug, z);
}

/*  The tiles from column x0 to x1 and row y0 to y1, outlined on the
 *  surface the mesh draws, as four points on the screen.  A building's
 *  pad is flat, and a lone cell follows its own plane.  In the
 *  underground view the outline lies on the ground drawn there, the
 *  seabed under water.  Returns 0 when the corners cannot be found. */
static int area_poly(App *a, int x0, int y0, int x1, int y1, float poly[4][2])
{
    const RAtlasLevel *lv = a->sw.level;
    int                ug = a->opts.underground;
    float              t[4], z[4];
    float              dens;
    int                ok = lv != NULL, kq;
    /*  NW, NE, SE, SW of the footprint, each from its corner tile, as
     *  the world on the device has them. */
    ok   = ok && ground_corners(a, x0, y0, ug, t) == 0;
    z[0] = t[0];
    ok   = ok && ground_corners(a, x1, y0, ug, t) == 0;
    z[1] = t[1];
    ok   = ok && ground_corners(a, x1, y1, ug, t) == 0;
    z[2] = t[2];
    ok   = ok && ground_corners(a, x0, y1, ug, t) == 0;
    z[3] = t[3];
    if (!ok)
        return 0;
    dens = a->win_density > 0.0f ? a->win_density : 1.0f;
    /*  The corners go to grid_to_canvas UNTURNED: it turns them about the
     *  view's pivot itself.  Turning them here first turns them twice,
     *  and in the map view (yaw 45) the outline comes out a diamond on a
     *  square grid. */
    for (kq = 0; kq < 4; ++kq)
    {
        static const int dc[4] = {0, 1, 1, 0}, dr[4] = {0, 0, 1, 1};
        grid_to_window(a, (float)(dc[kq] ? x1 + 1 : x0), (float)(dr[kq] ? y1 + 1 : y0), z[kq], dens, &poly[kq][0], &poly[kq][1]);
    }
    poly_wind(poly);
    return 1;
}

/*  THE MARKS, the one highlight of tiles on the map (ui.h, RUI_MARK_*).
 *  Each area is its first column, first row, last column and last row,
 *  with its mark.  An area that comes later in the list goes over one
 *  that comes before it.  The device lights each marked tile in its
 *  mark's color, in the mesh and on the art (tile_marks.glsl).  The
 *  tiles go to the device only when they change. */
typedef struct
{
    int32_t rect[4];
    int     mark;
} MarkArea;

/*  How far a marked tile goes to its mark's color, out of 255
 *  (tile_marks.glsl).  Every mark takes the same wash, so the ground and
 *  what stands on it show through. */
#define MARK_WASH 97

/*  THE KINDS OF SPRITE a tile of the road network shows, from the parts
 *  its bytes carry (rcity_parts).  Each kind has the color the lane graph
 *  view marks it in, and the name the legend gives it. */
enum
{
    TK_STREET,
    TK_RAIL,
    TK_POWER,
    TK_HIGHWAY,
    TK_INTERCHANGE,
    TK_RAMP,
    TK_CROSSING,
    TK_BRIDGE,
    TK_TUNNEL,
    TK_SUBWAY,
    TK_KINDS
};
static const char *const TK_NAME[TK_KINDS] = {"street", "railway", "power line", "highway", "interchange", "ramp", "crossing", "bridge", "tunnel mouth", "subway link"};
static const uint32_t    TK_RGB[TK_KINDS]  = {0x8fa3b8, 0xb07a40, 0xffe14d, 0x3f8fff, 0xb050ff, 0xff50c8, 0xff3b30, 0x30d8c8, 0x404040, 0x50e060};

/*  The kind of sprite on a cell, or -1 where it carries no piece of the
 *  road network.  A part's own kind names it first, so a ramp of two
 *  parts is a ramp.  Then two networks on one cell are a crossing, and
 *  one is its network. */
static int tile_kind(uint8_t xbld, uint8_t xbit)
{
    RPart part[2];
    int   n = rcity_parts(xbld, xbit, part), k, net = -1;
    if (n <= 0)
        return -1;
    for (k = 0; k < n; k++)
    {
        switch (part[k].kind)
        {
            case RPART_CROSSING:
                return TK_CROSSING;
            case RPART_RAMP:
                return TK_RAMP;
            case RPART_INTERCHANGE:
                return TK_INTERCHANGE;
            case RPART_BRIDGE:
            case RPART_SPAN:
                return TK_BRIDGE;
            case RPART_MOUTH:
                return TK_TUNNEL;
            case RPART_TO_SUBWAY:
            case RPART_STATION:
                return TK_SUBWAY;
            default:
                break;
        }
        net = part[k].net;
    }
    if (n > 1)
        return TK_CROSSING;
    return net == RNET_BAND ? TK_HIGHWAY : net == RNET_RAIL ? TK_RAIL
                                       : net == RNET_POWER  ? TK_POWER
                                                            : TK_STREET;
}

/*  Under the lane graph, every tile that carries a piece of the road
 *  network is marked in the color of its kind of sprite.  The kind is
 *  read from the cells.  With `marks` NULL it only digests what it would paint. */
static uint32_t graph_marks(const RCity *c, uint8_t *marks)
{
    uint32_t h = 2166136261u;
    int32_t  i;
    if (!c || !c->xbld || !c->xbit)
        return 0;
    for (i = 0; i < r_map * r_map; i++)
    {
        const int      kind = tile_kind(c->xbld[i], c->xbit[i]);
        const uint32_t k    = (uint32_t)(kind + 1);
        h                   = (h ^ k) * 16777619u;
        if (marks && kind >= 0)
        {
            const uint32_t col = TK_RGB[kind];
            uint8_t       *t4  = marks + (size_t)i * 4u;
            t4[0] = (uint8_t)(col >> 16), t4[1] = (uint8_t)(col >> 8), t4[2] = (uint8_t)col, t4[3] = MARK_WASH;
        }
    }
    return h;
}

/*  Each road network tile's sprite, as the sweep draws it, for the lane
 *  graph view to draw small in the tile's corner.  It is the tile's first
 *  piece of art that is not ground or a car.  The op names its tile in
 *  the sweep's quarter, and the turn gives the tile of the map. */
static void graph_sprites(App *a, RUiState *s)
{
    static RUiSprite  *spr;
    static uint8_t    *seen; /* the tiles whose sprite is taken */
    static size_t      cap, seen_n;
    const RAtlasLevel *l     = a->sw.level;
    const size_t       cells = (size_t)r_map * (size_t)r_map;
    size_t             k;
    int                n = 0;
    s->n_spr             = 0;
    if (!l || !l->rgba || !a->view)
        return;
    if (a->ops.n > cap)
    {
        RUiSprite *ns = (RUiSprite *)realloc(spr, a->ops.n * sizeof *ns);
        if (!ns)
            return;
        spr = ns, cap = a->ops.n;
    }
    if (cells > seen_n)
    {
        uint8_t *ns = (uint8_t *)realloc(seen, cells);
        if (!ns)
            return;
        seen = ns, seen_n = cells;
    }
    memset(seen, 0, cells);
    for (k = 0; k < a->ops.n; k++)
    {
        const ROp   *op = &a->ops.v[k];
        const RTile *t;
        int32_t      sh = op->shape - l->id_base, mc, mr, idx;
        if (op->kind != R_OP_BLIT || op->terrain || op->stencil >= 0 || sh < 0 || sh >= 256)
            continue;
        adapt_turned_src(op->col, op->row, a->sweep_quarter, r_map, &mc, &mr);
        idx = mr * r_map + mc;
        if (mc < 0 || mr < 0 || mc >= r_map || mr >= r_map || seen[idx] || tile_kind(a->view->xbld[idx], a->view->xbit[idx]) < 0 || !(t = atlas_tile(l, op->shape)))
            continue;
        seen[idx]  = 1;
        spr[n].col = (int16_t)mc, spr[n].row = (int16_t)mr, spr[n].alt = op->alt;
        spr[n].x = t->x, spr[n].y = t->y, spr[n].w = t->w, spr[n].h = t->h;
        spr[n].flip = op->flip;
        ++n;
    }
    s->spr      = spr;
    s->n_spr    = n;
    s->spr_rgba = l->rgba;
    s->spr_w = l->w, s->spr_h = l->h;
}

static void marks_update(App *a, const MarkArea *area, int n, int lane_graph)
{
    uint32_t h = 2166136261u, cells = lane_graph ? graph_marks(a->view, NULL) : 0u;
    int      k, j;
#define MIX(v) (h = (h ^ (uint32_t)(v)) * 16777619u)
    MIX(n);
    MIX(cells);
    for (k = 0; k < n; ++k)
    {
        for (j = 0; j < 4; ++j)
            MIX(area[k].rect[j]);
        MIX(area[k].mark);
    }
    MIX(r_map);
    MIX(a->sweep_quarter);
#undef MIX
    if (!a->gpu || h == a->marks_digest)
        return;
    a->marks_digest = h;
    if (n == 0 && cells == 0)
    {
        gpu_set_marks(a->gpu, NULL, NULL, 0);
        return;
    }
    const size_t bytes = (size_t)r_map * (size_t)r_map * 4u;
    if (a->marks_n != r_map)
    {
        free(a->marks);
        free(a->marks_sweep);
        a->marks       = (uint8_t *)malloc(bytes);
        a->marks_sweep = (uint8_t *)malloc(bytes);
        a->marks_n     = a->marks && a->marks_sweep ? r_map : 0;
        if (!a->marks_n)
            return;
    }
    memset(a->marks, 0, bytes);
    if (cells)
        graph_marks(a->view, a->marks);
    for (k = 0; k < n; ++k)
    {
        const uint32_t c = ui_mark_color(area[k].mark);
        int32_t        x, y;
        for (y = area[k].rect[1]; y <= area[k].rect[3]; ++y)
            for (x = area[k].rect[0]; x <= area[k].rect[2]; ++x)
                if (x >= 0 && y >= 0 && x < r_map && y < r_map)
                {
                    uint8_t *t = a->marks + ((size_t)y * (size_t)r_map + (size_t)x) * 4u;
                    t[0]       = (uint8_t)(c >> 16);
                    t[1]       = (uint8_t)(c >> 8);
                    t[2]       = (uint8_t)c;
                    t[3]       = MARK_WASH;
                }
    }
    /*  The art is swept in the camera's quarter, so its copy is read
     *  off the map's through the same turn. */
    {
        int32_t x, y, sx, sy;
        for (y = 0; y < r_map; ++y)
            for (x = 0; x < r_map; ++x)
            {
                adapt_turned_src(x, y, a->sweep_quarter, r_map, &sx, &sy);
                memcpy(a->marks_sweep + ((size_t)y * (size_t)r_map + (size_t)x) * 4u, a->marks + ((size_t)sy * (size_t)r_map + (size_t)sx) * 4u, 4u);
            }
    }
    gpu_set_marks(a->gpu, a->marks, a->marks_sweep, r_map);
}

static int gesture_area(const App *a, int32_t col, int32_t row, ToolPreview *pv, int32_t rect[4]);

/*  THE SHADING NUMBERS the shading window shows: the number in the
 *  store, its label, the section it is shown in, and its range.  They
 *  move the next frame and rebuild nothing.  The hour is not among them:
 *  the View menu's Time of day sets it. */
static const struct
{
    const char *name, *label, *group;
    float       lo, hi;
} SHADE_KNOB[] = {
    {"sun_shadow",        "sun shadow strength",              "Light and shadow", 0.0f,   1.0f   },
    {"ao_strength",       "corner darkening",                 "Light and shadow", 0.0f,   3.0f   },
    {"ao_radius",         "corner darkening reach (tiles)",   "Light and shadow", 0.1f,   4.0f   },
    {"sky_shadow",        "cloud shadow strength",            "Light and shadow", 0.0f,   1.0f   },
    {"sky_cover_least",   "least cloud cover",                "Clouds",           0.0f,   1.0f   },
    {"sky_scale",         "cloud size (tiles)",               "Clouds",           6.0f,   96.0f  },
    {"sky_speed",         "wind speed (tiles a second)",      "Clouds",           0.0f,   4.0f   },
    {"sky_altitude",      "cloud height (levels)",            "Clouds",           8.0f,   160.0f },
    {"sky_thickness",     "cloud thickness (levels)",         "Clouds",           2.0f,   80.0f  },
    {"cloud_light",       "cloud brightness",                 "Clouds",           0.2f,   3.0f   },
    {"cloud_amb_floor",   "cloud underside brightness",       "Clouds",           0.0f,   1.0f   },
    {"sky_wisp",          "cloud density",                    "Cloud edges",      0.2f,   6.0f   },
    {"trees_sway",        "tree sway on the wind (tiles)",    "Trees",            0.0f,   0.1f   },
    {"trees_near_px",     "whole trees above (px a tile)",    "Trees",            8.0f,   200.0f },
    {"trees_far_px",      "no trees below (px a tile)",       "Trees",            2.0f,   60.0f  },
    {"trees_band",        "level crossover band",             "Trees",            0.0f,   0.5f   },
    {"cloud_ext",         "cloud opacity (per meter)",        "Cloud edges",      0.005f, 0.3f   },
    {"sky_reach",         "wisp spread",                      "Cloud edges",      0.0f,   1.5f   },
    {"cloud_shape_m",     "billow size (meters)",             "Cloud edges",      40.0f,  1200.0f},
    {"cloud_detail_m",    "edge detail size (meters)",        "Cloud edges",      10.0f,  400.0f },
    {"cloud_erode",       "edge wear",                        "Cloud edges",      0.0f,   1.0f   },
    {"cover",             "land cover strength",              "Ground",           0.0f,   1.0f   },
    {"cover_tiles",       "land cover patch size (tiles)",    "Ground",           8.0f,   160.0f },
    {"ground_bump",       "ground bumps",                     "Ground",           0.0f,   0.6f   },
    {"ground_bump_tiles", "ground bump size (tiles)",         "Ground",           1.0f,   20.0f  },
    {"water_clarity",     "water clarity",                    "Water",            0.2f,   4.0f   },
    {"water_wave",        "swell height",                     "Water",            0.0f,   0.4f   },
    {"water_wave_tiles",  "swell length (tiles)",             "Water",            1.0f,   30.0f  },
    {"cloud_res",         "cloud resolution (pixels a step)", "Cost",             1.0f,   4.0f   },
};
#define N_SHADE_KNOB ((int)(sizeof SHADE_KNOB / sizeof SHADE_KNOB[0]))

/*  The shading window's sliders, from the store, unless the window is
 *  mid-edit and owns them.  The first reading is each one's default. */
static void shading_fill(RUiState *s)
{
    static float def[N_SHADE_KNOB];
    static int   have;
    int          k;
    if (s->shading_changed)
        return;
    s->n_shade = N_SHADE_KNOB < RUI_MAX_KNOBS ? N_SHADE_KNOB : RUI_MAX_KNOBS;
    for (k = 0; k < s->n_shade; ++k)
    {
        float v = 0.0f;
        geo_find(SHADE_KNOB[k].name, &v);
        if (!have)
            def[k] = v;
        s->shade[k] = (RUiKnob){SHADE_KNOB[k].label, SHADE_KNOB[k].group, v, SHADE_KNOB[k].lo, SHADE_KNOB[k].hi, def[k]};
    }
    have = 1;
}

/*  The shading numbers taken back into the store, and said once in the
 *  log, so the values that look right can be kept in geo_defaults. */
static void shading_take(RUiState *s)
{
    char   line[512];
    size_t n = 0;
    int    k;
    for (k = 0; k < s->n_shade; ++k)
    {
        geo_put(SHADE_KNOB[k].name, s->shade[k].v);
        if (n < sizeof line)
            n += (size_t)snprintf(line + n, sizeof line - n, "%s%s %.3f", k ? ", " : "", SHADE_KNOB[k].name, (double)s->shade[k].v);
    }
    R_NOTE("shading", "%s", line);
    s->shading_changed = 0;
}

/*  What the UI shows this frame. */
void ui_fill(App *a)
{
    RUiState   *s = &a->us;
    const City *c = a->city;
    {
        int k;
        s->n_biomes = trees_biomes() < 16 ? trees_biomes() : 16;
        for (k = 0; k < s->n_biomes; ++k)
            s->biome_names[k] = trees_biome_name(k);
    }
    /*  The time of day starts at the store's own hour, until a saved one
     *  replaces it. */
    {
        static int hour_known;
        if (!hour_known)
            geo_find("sun_hour", &s->hour), hour_known = 1;
    }
    /*  The marked areas this frame, in the order they cover each other:
     *  what the pointer is over, what a drag has taken, the selection.
     *  What the pointer is over is marked only while the pointer is on
     *  the map and not on a window. */
    MarkArea  mark[sizeof a->taken / sizeof a->taken[0] + 3];
    int       n_mark = 0;
    const int on_map = !(a->ui && ui_wants_mouse(a->ui));
    uint32_t  inst, culled;
    int       k;
    if (a->view->name[0])
        snprintf(s->city_name, sizeof s->city_name, "%s", a->view->name);
    else
        snprintf(s->city_name, sizeof s->city_name, "%s", a->city_base);
    s->funds      = c->funds;
    s->population = c->population;
    s->stage      = c->misc[MISC_STAGE];
    for (k = 0; k < 3; ++k)
        s->demand[k] = c->rci_demand[k];
    s->power_pct    = c->power_pct;
    s->water_pct    = c->water_pct;
    s->unemployment = c->unemployment;
    for (k = 0; k < RUI_N_MAP_TOOLS; ++k)
    {
        int j;
        s->sub_ready[k] = 0;
        for (j = 0; j < RUI_N_SUB; ++j)
        {
            s->sub_cost[k][j] = TOOL_COST[k * N_SUBTOOL + j];
            if (sim_tool_ported(k, j))
                s->sub_ready[k] |= (uint16_t)(1u << j);
        }
    }
    s->land_value_tot = c->land_value_tot;
    s->crime_tot      = c->crime_tot;
    s->traffic_tot    = c->traffic_tot;
    s->pollution_tot  = c->pollution_tot;
    s->fps            = a->fps;
    s->frame_ms       = a->frame_ms;
    gpu_stats(a->gpu, &inst, &culled);
    s->instances = inst;
    s->culled    = culled;
    if (!a->building)
        s->mesh_verts = a->mesh.n_land;
    s->driver = gpu_driver(a->gpu);
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
    s->outline        = TUNE(show_curves) > 0.5f;
    s->tile_kinds     = s->outline ? TK_KINDS : 0;
    s->tile_kind_name = TK_NAME;
    s->tile_kind_rgb  = TK_RGB;
    if (s->outline)
        graph_sprites(a, s);
    else
        s->n_spr = 0;
    inspect_fill(a, s);
    s->q_ok         = a->q_col >= 0 && a->q_row >= 0;
    s->tool_poly_ok = 0;
    if (s->q_ok)
    {
        int32_t qdc, qdr;
        s->q_ok = app_city_find(a, a->q_col, a->q_row, &qdc, &qdr) >= 0;
    }
    if (s->q_ok)
    {
        int32_t qdc = 0, qdr = 0;
        int     qc = app_city_find(a, a->q_col, a->q_row, &qdc, &qdr);
        int     y = a->q_row - qdr, x = a->q_col - qdc;
        c             = a->world->city[qc].city;
        s->q_col      = a->q_col;
        s->q_row      = a->q_row;
        s->q_xter     = T_XTER(c, y, x);
        s->q_xbld     = T_XBLD(c, y, x);
        s->q_xzon     = T_XZON(c, y, x);
        s->q_xbit     = T_XBIT(c, y, x);
        s->q_altm     = T_ALTM(c, y, x);
        s->q_alt      = s->q_xter >= 0x10 ? (int32_t)((s->q_altm >> 5) & 0x1F)
                                          : (int32_t)(s->q_altm & 0x1F);
        s->q_building = BUILDING[s->q_xbld].name;
        if (a->building || mesh_query(a->view, a->q_col, a->q_row, s->q_mesh, sizeof s->q_mesh) != 0)
            s->q_mesh[0] = 0;
        /*  The footprint the cell belongs to, as the original finds it
         *  ($763A).  It gives its edge n and its north-east tile, so it
         *  covers rows oy..oy+n-1 and columns ox-n+1..ox.  The highlight
         *  outlines the whole footprint. */
        {
            int n, x0, y0;
            s->q_orow = y; /* $763A moves the cell's own coordinates to the origin */
            s->q_ocol = x;
            n         = sim_footprint_origin(c, &s->q_orow, &s->q_ocol, T_XBLD(c, y, x));
            x0        = s->q_ocol - n + 1;
            y0        = s->q_orow;
            if (n < 1 || n > 4 || x0 < 0 || y0 < 0 || s->q_ocol >= MAP_W || y0 + n > MAP_H)
            {
                n         = 1;
                x0        = x;
                y0        = y;
                s->q_ocol = x;
                s->q_orow = y;
            }
            /*  Back to the view's own tiles. */
            x0 += qdc;
            y0 += qdr;
            s->q_ocol += qdc;
            s->q_orow += qdr;
            s->q_size = n;
            area_poly(a, x0, y0, x0 + n - 1, y0 + n - 1, s->q_poly);
            /*  With the query window open, the query marks the footprint
             *  under the pointer, so the art that stands on it is lit
             *  whole.  With the inspector on, the inspector outlines the
             *  object under the pointer instead, and no tile is marked. */
            if (s->show_query && !a->insp.on && on_map)
            {
                mark[0] = (MarkArea){
                    {x0, y0, x0 + n - 1, y0 + n - 1},
                    RUI_MARK_HOVER
                };
                n_mark = 1;
            }
        }
        /*  The selected tool's area under the pointer: what it would work
         *  on, and whether it would refuse, as the simulation says. */
        if (s->tool >= 0 && s->tool < RUI_N_MAP_TOOLS && s->tool != RUI_TOOL_QUERY)
        {
            ToolPreview pv;
            int32_t     rect[4];
            const int   flags = s->underground ? GESTURE_UNDERGROUND : 0;
            /*  gesture_area takes the tile in the view's own frame, as
             *  every gesture call does. */
            int got = gesture_area(a, a->q_col, a->q_row, &pv, rect);
            if (!got && sim_tool_preview(c, s->tool, s->sub[s->tool], flags, y, x, &pv))
            {
                rect[0] = pv.x0 - pv.n + 1 + qdc;
                rect[1] = pv.y0 + qdr;
                rect[2] = pv.x0 + qdc;
                rect[3] = pv.y0 + pv.n - 1 + qdr;
                got     = 1;
            }
            if (got && on_map)
            {
                s->tool_poly_ok = area_poly(a, rect[0], rect[1], rect[2], rect[3], s->tool_poly);
                s->tool_refused = !pv.ok;
                s->tool_price   = pv.price;
                mark[0]         = (MarkArea){
                    {rect[0], rect[1], rect[2], rect[3]},
                    pv.ok ? RUI_MARK_HOVER : RUI_MARK_REFUSED
                };
                n_mark = 1;
            }
        }
    }
    /*  What the drag has taken.  The tiles stay marked until the world
     *  built after them is up. */
    const int pointer = n_mark;
    for (k = 0; k < a->n_taken; ++k)
    {
        mark[n_mark] = (MarkArea){
            {a->taken[k][0], a->taken[k][1], a->taken[k][2], a->taken[k][3]},
            RUI_MARK_TAKEN
        };
        ++n_mark;
    }
    /*  The area selection (Shift-drag in the map view, app.c) and, after
     *  the release, the confirmation of what was written.  Its tiles are
     *  marked, and its corners on screen place a caption. */
    s->sel_ok = 0;
    {
        /*  After the release the rectangle blinks twice over half a
         *  second and goes.  What was written is the log's to say. */
        uint64_t now   = SDL_GetTicksNS();
        int      flash = now < a->sel_flash_until && (((a->sel_flash_until - now) / 120000000ull) & 1u) == 1u;
        /*  And before the drag: with Shift held, the tile under the
         *  cursor is marked as the selection is, in place of what the
         *  pointer marks.  So the drag's start is seen.  The inspector
         *  outlines the object instead, and while it is on no tile is
         *  marked under the pointer. */
        int hover = a->plan && !a->insp.on && !a->sel && !flash && (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 && a->q_col >= 0 && a->q_row >= 0;
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
            if (hover && on_map && pointer)
            {
                memmove(mark, mark + 1, (size_t)(n_mark - 1) * sizeof mark[0]);
                --n_mark;
            }
            if (!hover || on_map)
            {
                mark[n_mark] = (MarkArea){
                    {x0, y0, x1, y1},
                    RUI_MARK_SELECT
                };
                ++n_mark;
            }
            if ((!hover || on_map) && area_poly(a, x0, y0, x1, y1, s->sel_poly))
            {
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
    /*  The tiles --mark names, as the pointer marks them. */
    if (a->have_mark_fixed && n_mark < (int)(sizeof mark / sizeof mark[0]))
    {
        mark[n_mark] = (MarkArea){
            {a->mark_fixed[0], a->mark_fixed[1], a->mark_fixed[2], a->mark_fixed[3]},
            RUI_MARK_HOVER
        };
        ++n_mark;
    }
    marks_update(a, mark, n_mark, TUNE(show_curves) > 0.5f);
    /* the switches */
    s->speed    = a->speed;
    s->geometry = a->gv.geometry;
    /*  The knobs, unless the window is mid-edit and owns them.  The list
     *  is the store's, which geo_defaults filled: the window is handed
     *  the label and the range along with the value. */
    if (!s->tune_changed)
    {
        int i;
        s->n_knob = tune_count() < RUI_MAX_KNOBS ? tune_count() : RUI_MAX_KNOBS;
        for (i = 0; i < s->n_knob; ++i)
        {
            RKnob got;
            tune_knob(i, &got);
            s->knob[i].label = got.label;
            s->knob[i].group = got.group;
            s->knob[i].v     = got.v;
            s->knob[i].lo    = got.lo;
            s->knob[i].hi    = got.hi;
            s->knob[i].def   = got.def;
        }
    }
    shading_fill(s);
    s->plan    = a->plan;
    s->cell_ok = 0;
    if (((a->plan && s->show_cells) || s->outline) && a->sw.level)
    {
        /*  The map's frame on screen: where tile 0,0's corner lands, and
         *  what one column, one row and one level of altitude step by.
         *  The cell numbers read it in the map view, and the lane graph
         *  view's sprites in any view. */
        float f    = cam_px(a);
        float dens = a->win_density > 0.0f ? a->win_density : 1.0f;
        float ox, oy, cxp, cyp, rxp, ryp, axp, ayp;
        grid_to_canvas(a, 0.0f, 0.0f, 0.0f, &ox, &oy);
        grid_to_canvas(a, 1.0f, 0.0f, 0.0f, &cxp, &cyp);
        grid_to_canvas(a, 0.0f, 1.0f, 0.0f, &rxp, &ryp);
        grid_to_window(a, 0.0f, 0.0f, 0.0f, dens, &s->cell_o[0], &s->cell_o[1]);
        s->cell_c[0] = (cxp - ox) * f / dens;
        s->cell_c[1] = (cyp - oy) * f / dens;
        s->cell_r[0] = (rxp - ox) * f / dens;
        s->cell_r[1] = (ryp - oy) * f / dens;
        grid_to_canvas(a, 0.0f, 0.0f, 1.0f, &axp, &ayp);
        s->cell_a[0] = (axp - ox) * f / dens;
        s->cell_a[1] = (ayp - oy) * f / dens;
        s->cell_ok   = 1;
    }
    s->grid        = a->gv.grid;
    s->plain_sweep = a->gv.plain_sweep;
    s->tris        = a->gv.tris;
    s->underground = a->opts.underground;
    s->view        = a->opts.view;
}

/*  What the UI changed or asked for. */
/*  The interface numbers the disasters as the simulation does. */
_Static_assert(RUI_DISASTER_FIRE == SIM_DISASTER_FIRE && RUI_DISASTER_FLOOD == SIM_DISASTER_FLOOD && RUI_DISASTER_RIOT == SIM_DISASTER_RIOT &&
                   RUI_DISASTER_TORNADO == SIM_DISASTER_TORNADO && RUI_DISASTER_MONSTER == SIM_DISASTER_MONSTER &&
                   RUI_DISASTER_EARTHQUAKE == SIM_DISASTER_EARTHQUAKE && RUI_DISASTER_HURRICANE == SIM_DISASTER_HURRICANE &&
                   RUI_DISASTER_CHEMICAL == SIM_DISASTER_CHEMICAL && RUI_DISASTER_AIR_CRASH == SIM_DISASTER_AIR_CRASH &&
                   RUI_DISASTER_MICROWAVE == SIM_DISASTER_MICROWAVE && RUI_DISASTER_MELTDOWN == SIM_DISASTER_MELTDOWN &&
                   RUI_DISASTER_VOLCANO == SIM_DISASTER_VOLCANO && RUI_DISASTER_FIRESTORM == SIM_DISASTER_FIRESTORM &&
                   RUI_DISASTER_POLLUTION == SIM_DISASTER_POLLUTION && RUI_N_DISASTERS == SIM_N_DISASTERS,
               "the disasters are numbered as the simulation numbers them");

void ui_apply(App *a, SDL_Window *win, int pw, int ph)
{
    RUiState *s = &a->us;
    /*  The trees' biome the View menu sets, or the city's own.  Every
     *  forest tile changes with it, so the world is built again. */
    {
        int want = s->biome >= 0 ? s->biome : a->biome_city;
        if (want != a->biome_shown)
        {
            trees_set_biome(want);
            if (a->biome_shown != -2)
                a->mesh_dirty = 1;
            a->biome_shown = want;
        }
    }
    /*  The time of day the View menu sets.  The renderer reads it from
     *  the store (gpu/frame.c, sun_clock). */
    {
        float now = 0.0f;
        geo_find("sun_hour", &now);
        if (now != s->hour)
            geo_put("sun_hour", s->hour);
    }
    if (s->speed != a->speed)
        set_speed(a, s->speed);
    if (s->tune_changed && !a->building)
    {
        /*  The knobs are a tuning session's.  The window writes back only
         *  what it drew a slider for, so a knob some other control owns
         *  keeps what that control left.  Outline is one of those and is
         *  applied below with the other views. */
        int k;
        for (k = 0; k < s->n_knob; ++k)
            if (s->knob[k].label)
                tune_set_at(k, s->knob[k].v);
        s->tune_changed = 0;
        a->mesh_dirty   = 1; /* the knobs are geometry: rebuild it */
    }
    if (s->shading_changed)
        shading_take(s);
    if (s->geometry != a->gv.geometry)
    {
        a->gv.geometry = s->geometry;
        a->mesh_dirty  = 1; /* the mesh stands in for the art */
    }
    if (!!s->outline != (TUNE(show_curves) > 0.5f) && !a->building)
    {
        /*  Outline lays the road network's lane graph over the bare
         *  ground.  It is geometry, so the mesh is built again, and it is
         *  remembered like the grid. */
        tune_set("show_curves", s->outline ? 1.0f : 0.0f);
        a->mesh_dirty = 1;
    }
    if (!!s->plan != !!a->plan)
        set_plan(a, s->plan, win);
    a->gv.grid        = s->grid;
    a->gv.plain_sweep = s->plain_sweep;
    a->gv.tris        = s->tris;
    /*  The switches remembered between runs.  The outline is not applied
     *  while a build runs, so none is saved until it has been. */
    if (!a->building)
        prefs_switches_save(a);
    if (s->underground != a->opts.underground || s->view != a->opts.view)
    {
        if (s->underground != a->opts.underground)
            a->mesh_dirty = 1;
        a->opts.underground = s->underground;
        a->opts.view        = s->view;
        a->dirty            = 1;
    }
    if (s->want.zoom_step > 0)
        zoom_by(a, 2.0f, (float)pw * 0.5f, (float)ph * 0.5f);
    else if (s->want.zoom_step < 0)
        zoom_by(a, 0.5f, (float)pw * 0.5f, (float)ph * 0.5f);
    if (s->want.rotate)
        app_turn(a, s->want.rotate > 0 ? -90.0f : 90.0f, win);
    inspect_apply(a, s);
    if (s->want.quit)
        a->quit = 1;
    if (s->want.screenshot)
        a->want_shot = 1;
    if (s->want.load && s->load_path[0])
    {
        /*  The view is read off the world at every sweep, so a load
         *  swaps the world.  The renderer's map then takes the new
         *  world's size. */
        uint64_t t0 = SDL_GetTicksNS();
        R_NOTE("city", "loading %s", s->load_path);
        remesh_wait(a);
        if (app_world_load(a, s->load_path) && r_map_set(app_world_side(a)) == 0)
        {
            app_streets_import(a);
            set_city_name(a, s->load_path);
            city_limits_from_args(a->city);
            open_paused(a);
            /*  the sweep and the terrain mesh are both cached.  So a new
             *  city is not visible until they are asked to rebuild:
             *  without this the map keeps drawing the old one */
            a->dirty      = 1;
            a->mesh_dirty = 1;
            resident_stale(a); /* and it is built a block at a time, the nearest first */
            /*  and the UI's own copy of the speed, or the next frame
             *  compares the two and puts the old speed back */
            s->speed = a->speed;
            a->q_col = a->q_row = -1; /* the query box is about a gone tile */
            log_city_loaded(a->city, s->load_path, NS_MS(SDL_GetTicksNS() - t0));
            ui_log(s, "Loaded %s", a->city_base);
        }
        else
        {
            R_WARN("city", "could not load %s", s->load_path);
            ui_log(s, "Could not load %s", s->load_path);
        }
    }
    if (s->want.music)
    {
        if (a->mus)
        {
            music_set_enabled(a->mus, !music_enabled(a->mus));
            s->music_on = music_enabled(a->mus);
            if (a->prefs_ok)
                prefs_set("music", s->music_on ? "on" : "off");
            ui_log(s, "Music %s", s->music_on ? "on" : "off");
        }
    }
    if (s->want.theme)
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
    }
    if (s->want.save)
    {
        if (world_save(a, s->save_path))
            ui_log(s, "Saved %s", s->save_path);
        else
            ui_log(s, "Could not save %s", s->save_path);
    }
    if (s->want.disaster)
    {
        /*  What each sounds like, by its number. */
        static const int sounds[RUI_N_DISASTERS] = {
            0, R_SND_FIRE_LOOP, R_SND_FLOOD, R_SND_SHOT, R_SND_SIREN, R_SND_ROAR, R_SND_SIREN, R_SND_SIREN, R_SND_EXPLODE, R_SND_MAYDAY, R_SND_ZZAP, R_SND_EXPLODE, R_SND_EXPLODE, R_SND_FIRE_LOOP, 0};
        int r = s->want.disaster > 0 && s->want.disaster < RUI_N_DISASTERS ? sim_disaster_start(a->city, s->want.disaster) : 0;
        ui_log(s, "%s: %s", RUI_DISASTER_NAME[s->want.disaster < RUI_N_DISASTERS ? s->want.disaster : 0], r ? "started" : "nothing happened");
        if (r)
            sound_play(a->snd, sounds[s->want.disaster]);
        a->dirty      = 1;
        a->mesh_dirty = 1; /* a disaster may move the ground */
    }
    if (s->want.sound)
        sound_play(a->snd, s->want.sound);
    memset(&s->want, 0, sizeof s->want);
}

/*  What a tile sounds like when queried: the original lets a building be
 *  heard under the query tool.  By the building's name where the table
 *  has one and by the piece's range where it has not.  0 for silence. */
static int building_sound(const City *c, int32_t row, int32_t col)
{
    uint8_t     b    = T_XBLD(c, row, col);
    uint8_t     zone = T_XZON(c, row, col) & 0x0F;
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

/*  The palette's map tools are the simulation's tools, in the same
 *  order, with room for the same number of sub-tools. */
typedef char tool_order_matches[(RUI_N_MAP_TOOLS == N_TOOL && RUI_N_SUB == N_SUBTOOL && RUI_TOOL_BULLDOZER == TOOL_BULLDOZER &&
                                 RUI_TOOL_LANDSCAPE == TOOL_LANDSCAPE && RUI_TOOL_DISPATCH == TOOL_DISPATCH &&
                                 RUI_TOOL_POWER == TOOL_POWER && RUI_TOOL_WATER == TOOL_WATER && RUI_TOOL_BONUS == TOOL_REWARD &&
                                 RUI_TOOL_ROADS == TOOL_ROAD && RUI_TOOL_RAIL == TOOL_RAIL && RUI_TOOL_PORTS == TOOL_PORT &&
                                 RUI_TOOL_RESIDENTIAL == TOOL_RESIDENTIAL && RUI_TOOL_COMMERCIAL == TOOL_COMMERCIAL &&
                                 RUI_TOOL_INDUSTRIAL == TOOL_INDUSTRIAL && RUI_TOOL_EDUCATION == TOOL_EDUCATION &&
                                 RUI_TOOL_HEALTH == TOOL_SAFETY && RUI_TOOL_RECREATION == TOOL_RECREATION &&
                                 RUI_TOOL_SIGN == TOOL_SIGN && RUI_TOOL_QUERY == TOOL_QUERY && RUI_TOOL_CENTER == TOOL_CENTRE)
                                    ? 1
                                    : -1];

/*  The gesture under way.  There is one mouse, so there is one gesture,
 *  and App.gesturing says whether it is on. */
static SimGesture s_gesture;
static void       gesture_take(App *a, int32_t col, int32_t row);

/*  While a tool that drags a rectangle is down: the rectangle to
 *  (col,row) as col0, row0, col1, row1, and its price. */
static int gesture_area(const App *a, int32_t col, int32_t row, ToolPreview *pv, int32_t rect[4])
{
    int16_t r[4];
    int32_t dc, dr;
    app_city_offset(a, &dc, &dr);
    if (!a->gesturing || !sim_tool_area(a->city, &s_gesture, (int)(row - dr), (int)(col - dc), pv, r))
        return 0;
    rect[0] = r[1] + dc, rect[1] = r[0] + dr, rect[2] = r[3] + dc, rect[3] = r[2] + dr;
    return 1;
}

int gesture_ready(const App *a)
{
    const RUiState *s = &a->us;
    return s->tool >= 0 && s->tool < RUI_N_MAP_TOOLS && sim_tool_ported(s->tool, s->sub[s->tool]);
}

/*  What the tool asked of the interface: its sounds, a picture in a
 *  window, a story for the newspaper.  The tile it worked on changed, so
 *  the city is swept again at once, which takes well under a
 *  millisecond.  The mesh is rebuilt once, when the gesture ends.  With
 *  the networks drawn a rebuild takes seconds, and one a tile would stop
 *  the drag dead. */
static void gesture_after(App *a)
{
    int k;
    for (k = 0; k < s_gesture.n_ev; ++k)
    {
        const ToolEvent *e = &s_gesture.ev[k];
        switch (e->kind)
        {
            case TOOL_EV_SOUND_LOOP:
                sound_loop(a->snd, (int)e->arg);
                break;
            case TOOL_EV_SOUND_STOP:
                sound_stop(a->snd, (int)e->arg);
                break;
            case TOOL_EV_SOUND:
                sound_play(a->snd, (int)e->arg);
                break;
            case TOOL_EV_COLLAPSE:
                {
                    int32_t dc, dr;
                    app_city_offset(a, &dc, &dr);
                    collapse_begin(a, TOOL_COLLAPSE_Y(e->arg) + dr, TOOL_COLLAPSE_X(e->arg) + dc, TOOL_COLLAPSE_N(e->arg));
                    break;
                }
            case TOOL_EV_PICTURE:
                a->us.show_picture = e->arg;
                break;
            case TOOL_EV_NEWS:
                ui_log(&a->us, "The newspaper has story %d", (int)e->arg);
                break;
            default:
                break;
        }
    }
    s_gesture.n_ev = 0;
    a->dirty       = 1;
    if (!a->gesturing)
        a->mesh_dirty = 1;
}

/*  The left button goes down over (col,row).  The Command key is the one
 *  the original tests at $810: with it down, a tile labeled $FF may go. */
void gesture_begin(App *a, int32_t col, int32_t row, int cmd_down)
{
    const RUiState *s     = &a->us;
    const int       flags = (cmd_down ? GESTURE_KEY : 0) | (s->underground ? GESTURE_UNDERGROUND : 0);
    int32_t         cc = col, cr = row;
    /*  A gesture works in the city whose window holds the tile it starts
     *  on, and stays in that city until it ends. */
    if (!app_city_at(a, &cc, &cr))
        return;
    gesture_take(a, col, row);
    a->gesturing = sim_tool_begin(a->city, &s_gesture, s->tool, s->sub[s->tool], flags, cr, cc);
    gesture_after(a);
}

/*  The area the tool takes at (col,row), as the simulation previews it,
 *  added to what the drag has taken. */
static void gesture_take(App *a, int32_t col, int32_t row)
{
    RUiState   *s = &a->us;
    ToolPreview pv;
    int32_t     r[4], dc, dr;
    int         k;
    if (gesture_area(a, col, row, &pv, r))
    {
        memcpy(a->taken[0], r, sizeof r);
        a->n_taken = 1;
        return;
    }
    app_city_offset(a, &dc, &dr);
    if (!sim_tool_preview(a->city, s->tool, s->sub[s->tool], s->underground ? GESTURE_UNDERGROUND : 0, row - dr, col - dc, &pv) ||
        !pv.ok)
        return;
    r[0] = pv.x0 - pv.n + 1 + dc, r[1] = pv.y0 + dr, r[2] = pv.x0 + dc, r[3] = pv.y0 + pv.n - 1 + dr;
    for (k = 0; k < a->n_taken; ++k)
        if (memcmp(a->taken[k], r, sizeof r) == 0)
            return;
    if (a->n_taken < (int)(sizeof a->taken / sizeof a->taken[0]))
        memcpy(a->taken[a->n_taken++], r, sizeof r);
}

/*  The pointer came onto another tile with the button down. */
void gesture_step(App *a, int32_t col, int32_t row)
{
    if (!a->gesturing)
        return;
    int32_t dc, dr;
    app_city_offset(a, &dc, &dr);
    gesture_take(a, col, row);
    sim_tool_step(a->city, &s_gesture, row - dr, col - dc);
    gesture_after(a);
}

/*  The button came up over (col,row).  Off the map, the original keeps
 *  the last tile the tool worked on. */
void gesture_end(App *a, int32_t col, int32_t row)
{
    if (!a->gesturing)
        return;
    int32_t dc, dr;
    app_city_offset(a, &dc, &dr);
    if (col < 0 || row < 0)
    {
        col = s_gesture.x + dc;
        row = s_gesture.y + dr;
    }
    sim_tool_end(a->city, &s_gesture, row - dr, col - dc);
    a->gesturing = 0;
    ++a->taken_gen;
    gesture_after(a);
}

/*  --gesture: the same gestures from the command line, for a run under
 *  test.  The spec is TOOL,SUB[,FLAGS]:C,R/C,R/...  A semicolon
 *  parts two gestures.  FLAGS is 1 for the Command key and 2 for the underground
 *  view.  Returns 0 when the spec does not read, or the tool is not
 *  reconstructed. */
int gesture_script(App *a, const char *spec)
{
    while (*spec)
    {
        int tool, sub, flags = 0, col = -1, row = -1, n = 0, used = 0;
        if (sscanf(spec, "%d,%d,%d:%n", &tool, &sub, &flags, &used) != 3 || !used)
        {
            flags = 0;
            used  = 0;
            if (sscanf(spec, "%d,%d:%n", &tool, &sub, &used) != 2 || !used)
                return 0;
        }
        if (tool < 0 || tool >= RUI_N_MAP_TOOLS || sub < 0 || sub >= RUI_N_SUB)
            return 0;
        spec += used;
        a->us.tool        = tool;
        a->us.sub[tool]   = sub;
        a->us.underground = (flags & 2) != 0;
        while (sscanf(spec, "%d,%d%n", &col, &row, &used) == 2)
        {
            if (n++ == 0)
            {
                gesture_begin(a, col, row, flags & 1);
                if (!a->gesturing)
                    return 0;
            }
            else
                gesture_step(a, col, row);
            spec += used;
            if (*spec != '/')
                break;
            ++spec;
        }
        if (!n)
            return 0;
        gesture_end(a, col, row);
        if (*spec == ';')
            ++spec;
        else if (*spec)
            return 0;
    }
    return 1;
}

/*  The selected tool on the tile under the pointer, for the tools that
 *  act on a click.  A tool the simulation can do is worked as a gesture
 *  instead, from the press of the button. */
void use_tool(App *a, SDL_Window *win)
{
    RUiState *s   = &a->us;
    int32_t   col = a->q_col, row = a->q_row;
    int       pw, ph;
    if (s->tool < 0 || col < 0)
        return;
    switch (s->tool)
    {
        case RUI_TOOL_QUERY:
            s->show_query = 1;
            {
                int32_t dc, dr;
                int     qc = app_city_find(a, col, row, &dc, &dr);
                if (qc >= 0)
                    sound_play(a->snd, building_sound(a->world->city[qc].city, row - dr, col - dc));
            }
            break;
        case RUI_TOOL_CENTER:
            SDL_GetWindowSizeInPixels(win, &pw, &ph);
            {
                /*  Center through the camera's own anchor rather than a
                 *  projection of its own.  A projection written here has
                 *  to agree with cam_anchor on the origin (tile_h + 0.5),
                 *  on the pitch and on gv.zoom as well as gv.scale.  One
                 *  that does not puts the tile clicked in the middle at
                 *  one zoom only, and never in the map view.  The anchor
                 *  already agrees. */
                int32_t idx   = row * r_map + col;
                a->anch_c     = (float)col + 0.5f;
                a->anch_r     = (float)row + 0.5f;
                a->anch_alt   = (float)rcity_alt_surface(a->view->map.altm[idx], a->view->map.xter[idx]);
                a->gv.pivot_c = a->anch_c;
                a->gv.pivot_r = a->anch_r;
                cam_hold(a, win);
            }
            break;
        default:
            if (s->tool < RUI_N_MAP_TOOLS && RUI_SUB_NAME[s->tool][1])
                ui_log(s, "%s: %s is not yet ported", RUI_TOOL_NAME[s->tool], RUI_SUB_NAME[s->tool][s->sub[s->tool]]);
            else
                ui_log(s, "%s: not yet ported", RUI_TOOL_NAME[s->tool]);
            sound_play(a->snd, R_SND_ERROR);
            break;
    }
}
