/*  gpu/frame.c: drawing one frame with the device gpu.c made.  Culling
 *  and ordering the instances, the camera uniforms, the passes, and the
 *  two ways out: to the window, and to a readback buffer. */
#include "dump.h"
#include "geo.h"
#include "gpu/air.h"
#include "gpu/clouds.h"
#include "gpu/internal.h"
#include "log.h"
#include "material.h"
#include "materials.h" /* GENERATED: the fixed material numbers, and the room after them */
#include "mesh/mesh.h"
#include "opt.h"
#include "project.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "shaders.h"

/*  Canvas pixels to screen pixels: the integer pixel scale times the
 *  continuous zoom.  The frame defines it.  The device uses it too. */
static float view_factor(const RGpuView *v);

/* ---- drawing ---------------------------------------------------------- */

/*  The vertex uniform of every pipeline that places things on the
 *  canvas, sprites and mesh alike.
 *
 *      The scroll and the scale.
 *      The tile projection.
 *      The altitude step and the free rotation.
 *
 *  The sprite shader reads the rotation too, to move each sprite with
 *  its tile. */
typedef struct
{
    float view[4]; /* scroll x, scroll y, 2*scale/target_w, 2*scale/target_h */
    float proj[4]; /* ox + tw/2, oy - th - 0.5, tw/2, th/2                   */
    float alt[4];  /* alt_step, depth divisor, cos, sin of the rotation      */
    float rot[4];  /* free camera (1 or 0), pivot column, pivot row, pitch, radians */
    float mode[4]; /* the sweep's painter (1) or the camera's depth (0), the painter's slot divisor, the sprites' translation D in grid units (x, y) */
    float wind[4]; /* the way the wind blows, the seconds, and how far a tree's crown sways in tiles (0 still) */
} CamU;

static int s_chunks_said; /* --times: the chunks drawn, said once */

/*  Is chunk k in view?  Its four corner tiles at the ground's floor and
 *  its highest level, through the very projection terrain.vert applies.
 *  The turn about the pivot, the pitch's foreshortening, the height's
 *  lift, to normalized device coordinates.  Out only when every corner
 *  lies past one edge, with a twentieth of margin. */
static int chunk_visible(const CamU *u, int k)
{
    const float sp = sinf(u->rot[3]), cp = cosf(u->rot[3]);
    float       minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    int         i;
    for (i = 0; i < 8; ++i)
    {
        float c   = (float)((k % (r_map / MESH_CHUNK)) * MESH_CHUNK + ((i & 1) ? MESH_CHUNK : 0));
        float r   = (float)((k / (r_map / MESH_CHUNK)) * MESH_CHUNK + ((i & 2) ? MESH_CHUNK : 0));
        float alt = (i & 4) ? 32.0f : 0.0f;
        float cx = c - u->rot[1], cy = r - u->rot[2];
        float rx   = cx * u->alt[2] - cy * u->alt[3] + u->rot[1];
        float ry   = cx * u->alt[3] + cy * u->alt[2] + u->rot[2];
        float canx = u->proj[0] + (ry - rx) * u->proj[2];
        float cany = u->proj[1] + (rx + ry) * u->proj[3] * (sp / ARC_PITCH_SIN0) - alt * u->alt[0] * (cp / ARC_PITCH_COS0);
        float nx = (canx - u->view[0]) * u->view[2] - 1.0f, ny = (cany - u->view[1]) * u->view[3] - 1.0f;
        minx = nx < minx ? nx : minx;
        maxx = nx > maxx ? nx : maxx;
        miny = ny < miny ? ny : miny;
        maxy = ny > maxy ? ny : maxy;
    }
    return maxx >= -1.05f && minx <= 1.05f && maxy >= -1.05f && miny <= 1.05f;
}

/*  The fragment uniform of every sprite pipeline, sprite.frag's and
 *  shadow.frag's. */
typedef struct
{
    int32_t p[4]; /* transparent index, 0, 0, 0 */
} FragU;

typedef struct
{
    float screen[4];
    float sky[4];
} ResolveU;

/*  The fragment uniform of air.frag: the camera, to run its projection
 *  backwards, and the air.  The shader names each member. */
typedef struct
{
    float     proj[4], view[4], turn[4], grid[4], ray[4], sun[4], steps[4];
    AirMedium medium;
    float     multi[4], sky[4], shade[4], cloud[4], cloudk[4], cloudp[4], cloudq[4], smog[4], smogabs[4];
    float     skyf[4], skyw[4], skym[4], skyc[4], lowres[4], ao[4], daysun[4], dayamb[4], tint[4];
} AirU;

typedef struct
{
    float sun[4];
    float params[4];
    /*  The materials named at run time: a color and a roughness each, in
     *  the order material_define gave them.  So material MAT_RUNTIME_BASE
     *  + k is shaded from mats[k].  A built-in material has a branch of
     *  its own instead and reads none of this. */
    float mats[MAT_RUNTIME_MAX][4];
    /*  The tones of a cut's layers (STRATA), and how many in the first
     *  of the last four. */
    float strata[MAT_STRATA_MAX][4];
    float strata_n[4];
    float sunm[3][4];                   /* the sun's frame (sun_frame) */
    float sunsh[4];                     /* 1 while the sun's map holds, the share of the sun a shadow takes, the map's texel in tiles, MESH_LEVEL_H */
    float ground[4];                    /* the ground's large bumps: their steepest slope, their width in tiles, SKY_FIELD_CELLS, and the water's clarity */
    float eye[4];                       /* toward the camera (cam_ray), and 0 */
    float wave[4];                      /* the water's swells: their steepest slope, their length in tiles, the wind in tiles a second */
    float cover[4];                     /* the land's cover: the tiles a cell of its noise spans, the noise's mean and spread, its strength */
    float amb[4];                       /* the ambient light's color at this hour (sun_clock) */
    float suncol[4];                    /* the sun's light's color at this hour */
    float lamp[4];                      /* the street lamps' light at full: its color, scaled by how dark the hour is */
    float fine[MAT_RUNTIME_MAX / 4][4]; /* each run-time material's thinnest feature, in tiles (material_fine) */
    float leaf[GPU_LEAF_SPECIES][2][4]; /* each species' two leaf colors (gpu_set_leaves) */
    float rag[GPU_LEAF_SPECIES / 4][4]; /* each species' raggedness */
    float trees[4];                     /* 1 while a draw of trees, the dither from which it keeps them and to which, the far cover's share */
} LightU;

/*  THE LAYERS OF A CUT through the land, at a cliff and at the edge of
 *  the map: the tones each band gets one of.  A few close earth tones
 *  read as one rock.  They are tan, a darker tan and a pale tan. */
enum
{
    N_STRATA = 3
};
static const float STRATA[N_STRATA][4] = {
    {0.62f, 0.55f, 0.44f, 0.0f},
    {0.52f, 0.45f, 0.36f, 0.0f},
    {0.68f, 0.62f, 0.51f, 0.0f},
};

typedef struct
{
    uint32_t n_terrain, n_water, n_sprite, n_shadow;
} Ranges;

/*  Cull to the target and order the kinds into four runs. */
/*  The camera's pitch, degrees above the ground.  A view left at zero is
 *  a view that never asked: it gets the game's own 30. */
static float view_pitch(const RGpuView *v)
{
    return v->pitch > 0.0f ? v->pitch : ARC_PITCH_DEG;
}

/*  --disable shadows draws no silhouettes.  So a frame at the snap and a
 *  frame a hair off it differ only in how they order what is left. */
static int g_no_shadow = -1;

/*  Is the camera off the game's own view: turned, or raised? */
static int cam_free(const RGpuView *v)
{
    /*  A quarter turn on the game's pitch is not free.  The sweep was
     *  run on the view turned that way and its sprites are drawn as at
     *  the snap, unturned.  Only the mesh is projected at the quarter. */
    float turn = fmodf(fabsf(v->angle), 90.0f);
    int   off  = turn > 0.01f && turn < 89.99f;
    return off || !arc_is_game_pitch(view_pitch(v));
}

/*  A sprite's distance from the camera along the way it looks: the base
 *  of the standing or flat quad sprite.vert builds, the same number.
 *  The art writes its depth now, so the depth test orders it.  The sort
 *  is what settles the ties, art at equal distance, which the turned
 *  camera has plenty of.  Farthest first. */
typedef struct
{
    float    k;
    uint32_t i;
    RInst    in;
} SortInst;

/*  The sprites' own turn.  The sweep is made for a quarter.  Its art is
 *  drawn with the turn LEFT from that quarter to the camera's angle,
 *  about the map's center.  What a quarter's sweep is turned about.  And
 *  carried by the translation that puts a turn about the center where
 *  the mesh's turn about the pivot is.  At a settled quarter the turn
 *  left is nothing and the translation nothing: the art sits where the
 *  sweep put it.  On the move the second half of the swing draws the
 *  destination's sweep, so the settle draws nothing new. */
typedef struct
{
    float ca, sa; /* cos, sin of the turn left                */
    float cx, cy; /* the map's center, the sweep's own pivot   */
    float dx, dy; /* the translation, grid units              */
    int   moved;  /* the art is not where the sweep put it     */
} SpriteTurn;

static void sprite_turn(const RGpuView *v, SpriteTurn *t)
{
    float th = v->angle * ARC_DEG2RAD, ca = cosf(th), sa = sinf(th);
    float left = v->angle - 90.0f * (float)(v->sweep_quarter & 3);
    float pc, pr, c = (float)(r_map / 2);
    left = fmodf(left, 360.0f);
    if (left > 180.0f)
        left -= 360.0f;
    if (left <= -180.0f)
        left += 360.0f;
    t->ca = cosf(left * ARC_DEG2RAD);
    t->sa = sinf(left * ARC_DEG2RAD);
    t->cx = t->cy = c;
    gpu_view_pivot(v, &pc, &pr);
    /*  D = R(theta)(C - P) + P - C: a turn about the pivot P is a turn
     *  about the center C and this translation. */
    t->dx    = (c - pc) * ca - (c - pr) * sa + pc - c;
    t->dy    = (c - pc) * sa + (c - pr) * ca + pr - c;
    t->moved = fabsf(left) > 0.01f || !arc_is_game_pitch(view_pitch(v));
}

static float sprite_key(const RInst *in, const RGpuView *v, const RAtlasLevel *l)
{
    SpriteTurn t;
    float      pt = view_pitch(v) * ARC_DEG2RAD;
    float      sp = sinf(pt), cp = cosf(pt);
    float      nf = l && l->tile_w ? floorf((float)in->dst[2] / (float)l->tile_w + 0.5f) : 1.0f;
    float      half, fx, fy, dx, dy, tx, ty, front, ca, sa;
    sprite_turn(v, &t);
    ca = t.ca, sa = t.sa;
    if (nf < 1.0f)
        nf = 1.0f;
    half = (nf - 1.0f) * 0.5f;
    /*  The middle of the footprint: the art is anchored at the block's
     *  leftmost tile, the greatest column and the least row. */
    fx    = (float)in->under[2] + 0.5f - half;
    fy    = (float)in->under[3] + 0.5f + half;
    dx    = fx - t.cx;
    dy    = fy - t.cy;
    tx    = dx * ca - dy * sa + t.cx + t.dx;
    ty    = dx * sa + dy * ca + t.cy + t.dy;
    front = half * (fabsf(ca + sa) + fabsf(ca - sa));
    return (tx + ty + front + 1.05f) * cp + (float)in->rank[1] * ARC_PITCH_COS0 * sp +
           (ty - tx) * ARC_SPRITE_SKEW;
}

static int sort_cmp(const void *a, const void *b)
{
    const SortInst *x = (const SortInst *)a, *y = (const SortInst *)b;
    if (x->k < y->k)
        return -1;
    if (x->k > y->k)
        return 1;
    /*  A tie keeps the sweep's own order, so nothing shimmers between
     *  frames. */
    return x->i < y->i ? -1 : (x->i > y->i ? 1 : 0);
}

/*  Sort `n` instances at `v0` back to front for the turned camera. */
static int sort_sprites(RGpu *g, RInst *v0, uint32_t n, const RGpuView *v, const RAtlasLevel *l)
{
    static SortInst *buf = NULL;
    static uint32_t  cap = 0;
    uint32_t         k;
    static int       off = -1;
    if (off < 0)
        off = g_dev.no_sort; /* draw in the sweep's order, to show what it costs */
    if (n < 2 || off)
        return 0;
    if (n > cap)
    {
        SortInst *nb = (SortInst *)realloc(buf, (size_t)n * sizeof *nb);
        if (!nb)
            return -1;
        buf = nb;
        cap = n;
    }
    for (k = 0; k < n; ++k)
    {
        buf[k].k  = sprite_key(&v0[k], v, l);
        buf[k].i  = k;
        buf[k].in = v0[k];
    }
    qsort(buf, n, sizeof *buf, sort_cmp);
    for (k = 0; k < n; ++k)
        v0[k] = buf[k].in;
    (void)g;
    return 0;
}

/*  The trains of waves on the settling land.  Each has the way it runs,
 *  its length as a share of settle_wave_length, and its share of the
 *  height.  The ground's shader reads them from the uniform. */
static const float TRAIN[5][4] = {
    {0.94f,  0.34f,  1.00f, 0.30f},
    {-0.50f, 0.87f,  0.73f, 0.24f},
    {0.17f,  -0.98f, 1.31f, 0.20f},
    {-0.91f, -0.41f, 0.57f, 0.14f},
    {0.64f,  0.77f,  0.86f, 0.12f},
};

typedef struct
{
    float wave[4], clock[4], sweep[4], start[4], train[5][4];
} SettleU;

static void settle_uniform(const RGpuView *v, SettleU *u)
{
    memcpy(u->wave, v->settle, sizeof u->wave);
    memcpy(u->clock, v->settle + 4, sizeof u->clock);
    u->sweep[0] = v->settle[15];
    u->sweep[1] = v->settle[16];
    u->sweep[2] = v->settle[17];
    u->sweep[3] = v->settle[19];
    u->start[0] = v->settle[20];
    u->start[1] = v->settle[21];
    u->start[2] = v->settle[22];
    u->start[3] = 0.0f;
    memcpy(u->train, TRAIN, sizeof u->train);
}

/*  How far up a sprite standing on a tile is drawn, in levels, while the
 *  land settles, to `lift`.  False while it is not drawn.  Each piece
 *  falls on its own, once its chunk is built and the sweep reaches its
 *  tile, a little later by its own seed.  So the art comes down piece by
 *  piece, out from the view's center, as the ground under it settles.
 *  It comes straight down from settle[8] levels and slows as it lands. */
static int sprite_lift(const RGpu *g, const RGpuView *v, const RInst *in, float *lift)
{
    int32_t      side = (int32_t)v->settle[5], per = (int32_t)v->settle[6], n = (int32_t)v->settle[7];
    float        sc = (float)in->under[2] + 0.5f, sr = (float)in->under[3] + 0.5f, x, y, t0, t, dc, dr;
    float        now = v->settle[4], sweep = v->settle[17], fall = v->settle[18];
    uint32_t     h;
    int32_t      c, r;
    const float *st;
    *lift = 0.0f;
    if (v->settle[6] < 0.5f || !g->settle_cpu || g->settle_n != per)
        return 1;
    /*  The sprite's cell in the map's own frame. */
    x = v->settle[9] + 0.5f + (sc - 0.5f) * v->settle[11] + (sr - 0.5f) * v->settle[13];
    y = v->settle[10] + 0.5f + (sc - 0.5f) * v->settle[12] + (sr - 0.5f) * v->settle[14];
    c = (int32_t)floorf(x), r = (int32_t)floorf(y);
    c  = c < 0 ? 0 : c >= n ? n - 1
                            : c;
    r  = r < 0 ? 0 : r >= n ? n - 1
                            : r;
    st = &g->settle_cpu[2 * ((r / side) * per + c / side)];
    if (st[1] < 0.5f)
        return 0;
    h = (uint32_t)c * 747796405u + (uint32_t)r * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    dc = x - v->settle[15], dr = y - v->settle[16];
    t0 = v->settle[20] + (sweep > 0.0f ? sqrtf(dc * dc + dr * dr) / sweep : 0.0f) + 0.15f * (float)(h & 0xFFFFu) / 65535.0f;
    t0 = t0 > st[0] ? t0 : st[0];
    if (now < t0)
        return 0;
    t     = fall > 0.0f ? (now - t0) / fall : 1.0f;
    t     = t > 1.0f ? 1.0f : t;
    *lift = v->settle[8] * (1.0f - t) * (1.0f - t);
    return 1;
}

static int build_visible(RGpu *g, const RGpuView *v, Ranges *r)
{
    uint32_t k, n = 0, culled = 0;
    int32_t  x0 = v->scroll_x, y0 = v->scroll_y;
    int32_t  x1 = x0 + (int32_t)((float)g->tw / g->factor) + 1;
    int32_t  y1 = y0 + (int32_t)((float)g->th / g->factor) + 1;
    int      pass;

    if (g->n_inst > g->vis_cap)
    {
        RInst *nv = (RInst *)realloc(g->vis, (size_t)g->n_inst * sizeof *nv);
        if (!nv)
            return -1;
        g->vis     = nv;
        g->vis_cap = g->n_inst;
    }
    SpriteTurn st;
    sprite_turn(v, &st);
    memset(r, 0, sizeof *r);
    for (pass = 0; pass < 4; ++pass)
    {
        uint32_t start = n;
        if (g_no_shadow < 0)
            g_no_shadow = g_dev.no_shadow;
        for (k = 0; k < g->n_inst; ++k)
        {
            const RInst *in   = &g->inst[k];
            uint8_t      kind = g->kind[k];
            int          want;
            /*  Turned, the sprites still draw, each moved with its tile
             *  and unturned, as the original shows the same art at every
             *  one of its rotations.  Only the silhouettes, cast on the
             *  unturned canvas, stay off. */
            if (pass == 0)
                want = (kind == K_TERRAIN) ||
                       (kind == K_LAND_ART && !v->geometry) ||
                       (kind == K_UG_LATTICE && !(v->geometry && v->underground));
            else if (pass == 1)
                want = (kind == K_WATER_ART || kind == K_WATER_COL ||
                        kind == K_WATER_EDGE) &&
                       !v->geometry; /* meshed, the water is a body of faces */
            else if (pass == 2)
                /*  Looking down, the landmarks alone: the rest of the art
                 *  would cover the map it is meant to describe. */
                want = view_pitch(v) > 30.5f
                           ? (kind == K_LANDMARK)
                           : ((kind == K_SPRITE) || (kind == K_LANDMARK) ||
                              ((kind == K_LINE_ART || kind == K_CAR || kind == K_TRAIN) && !v->geometry));
            else
                /*  A silhouette is cast on the unturned canvas of its
                 *  sweep.  It moves with its tile as the art does.  So
                 *  it neither vanishes for the swing nor pops back at
                 *  the settle.  --disable shadows drops them all. */
                want = (kind == K_SHADOW) && !g_no_shadow;
            if (!want)
                continue;
            if (!st.moved &&
                (in->dst[0] >= x1 || in->dst[1] >= y1 ||
                 in->dst[0] + in->dst[2] <= x0 || in->dst[1] + in->dst[3] <= y0))
            {
                ++culled;
                continue;
            }
            g->vis[n] = *in;
            /*  While the land settles, the standing art falls onto it,
             *  and a silhouette waits for its art to land. */
            /*  A piece drawn from its own shape: its art is not drawn. */
            if (pass == 2 && kind == K_SPRITE && v->geometry && g->shaped && in->under[2] >= 0 &&
                in->under[3] >= 0 && in->under[2] < g->shaped_n && in->under[3] < g->shaped_n &&
                g->shaped[in->under[3] * g->shaped_n + in->under[2]])
                continue;
            if (pass >= 2)
            {
                float lift;
                if (!sprite_lift(g, v, in, &lift))
                    continue;
                if (pass == 3 && fabsf(lift) > 0.01f)
                    continue;
                g->vis[n].dst[1] -= (int32_t)floorf(lift * (float)g->sw.level->alt_step + 0.5f);
            }
            ++n;
        }
        if (pass == 0)
            r->n_terrain = n - start;
        else if (pass == 1)
            r->n_water = n - start;
        else if (pass == 2)
        {
            r->n_sprite = n - start;
            if (st.moved && sort_sprites(g, &g->vis[start], r->n_sprite, v, g->sw.level) != 0)
                return -1;
        }
        else
            r->n_shadow = n - start;
    }
    g->last_drawn  = n;
    g->last_culled = culled;
    return 0;
}

static void cam_for(const RGpu *g, const RGpuView *v, CamU *u)
{
    const RAtlasLevel *l = g->sw.level;
    u->view[0]           = (float)v->scroll_x + v->scroll_fx;
    u->view[1]           = (float)v->scroll_y + v->scroll_fy;
    /*  The canvas is drawn at the target's own resolution.  A canvas
     *  pixel is view_factor() target pixels, so sprites magnify by
     *  nearest and the mesh and the water shade every target pixel. */
    u->view[2] = 2.0f * g->factor / (float)g->tw;
    u->view[3] = 2.0f * g->factor / (float)g->th;
    /*  A ground sprite's rise is its full height.  So its diamond
     *  occupies the th + 1 rows ABOVE the tile's origin row.  The top
     *  vertex at sy - th - 1, the bottom row at sy - 1.  The mesh
     *  rhombus is th tall and is centered on that hexagon.  This is one
     *  pixel inside the sprite on the top and bottom rows, the residual
     *  tools/terrain_shapes.py records. */
    u->proj[0] = l ? arc_origin_x((float)g->sw.ox, (float)l->tile_w) : 0.0f;
    u->proj[1] = l ? arc_origin_y((float)g->sw.oy, (float)l->tile_h) : 0.0f;
    u->proj[2] = l ? arc_half_w((float)l->tile_w) : arc_half_w(ARC_TILE_W_MAX);
    u->proj[3] = l ? arc_half_h((float)l->tile_h) : arc_half_h(ARC_TILE_H_MAX);
    u->alt[0]  = l ? (float)l->alt_step : ARC_ALT_STEP_MAX;
    /*  The depth range: the diagonal units of distance the buffer spans.
     *  It is 800 for a map of the original's 128 tiles a side, and in
     *  proportion for a wider one.  The shaders start it three eighths of
     *  the range before the map's near corner.  So a view turned about any
     *  point of the map, or raised, stays inside it. */
    u->alt[1] = 800.0f * (r_map > 128 ? (float)r_map / 128.0f : 1.0f);
    u->alt[2] = cosf(v->angle * ARC_DEG2RAD);
    u->alt[3] = sinf(v->angle * ARC_DEG2RAD);
    /*  The free camera, turned off the snap, or raised off the game's
     *  own pitch, stops the painter's slot ordering the tiles.  A
     *  quarter turn on the game's pitch keeps it: the shaders turn the
     *  grid about the pivot and map each slot through the quarter. */
    {
        float turn = fmodf(fabsf(v->angle), 90.0f);
        int   off  = turn > 0.01f && turn < 89.99f;
        u->rot[0]  = (off || !arc_is_game_pitch(view_pitch(v))) ? 1.0f : 0.0f;
    }
    gpu_view_pivot(v, &u->rot[1], &u->rot[2]);
    u->rot[3] = view_pitch(v) * ARC_DEG2RAD;
    /*  The compositing: the original's art alone, at the original's own
     *  camera, composes as the sweep drew it, by the painter's slot.
     *  With the mesh, or off that camera, everything composes by the
     *  camera's depth and the art writes it. */
    u->mode[0] = (v->geometry || cam_free(v)) ? 0.0f : 1.0f;
    u->mode[1] = (float)(2 * r_map * r_map + 2); /* the painter's slot divisor */
    u->mode[2] = u->mode[3] = 0.0f;              /* the mesh turns about the pivot itself: no translation */
    /*  The trees sway on the wind the clouds move on, while the clouds
     *  are on (terrain_body.vert, tree_sway). */
    {
        static int  c_wx = -1, c_wy = -1, c_sway = -1;
        const float wx = geo_num(&c_wx, "sky_wind_x"), wy = geo_num(&c_wy, "sky_wind_y"), wl = sqrtf(wx * wx + wy * wy);
        u->wind[0] = wl > 0.0f ? wx / wl : 1.0f;
        u->wind[1] = wl > 0.0f ? wy / wl : 0.0f;
        u->wind[2] = v->time;
        u->wind[3] = v->wind ? geo_num(&c_sway, "trees_sway") : 0.0f;
    }
}

/*  The luminance of a color. */
static float luma(const float c[3])
{
    return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
}

/*  The way the camera looks, a unit vector in the grid's own axes.
 *  Across the ground it runs where a step leaves the canvas still: the
 *  column less the row stays, and the sum falls.  It goes down at the
 *  camera's pitch. */
static void cam_ray(const CamU *u, float r[3])
{
    const float sp = sinf(u->rot[3]), cp = cosf(u->rot[3]);
    const float rx = -0.70710678f, ry = -0.70710678f;
    const float wx = rx * u->alt[2] + ry * u->alt[3], wy = -rx * u->alt[3] + ry * u->alt[2];
    r[0] = wx * cp, r[1] = wy * cp, r[2] = -sp;
}

/*  The frame's pixels a side that one pixel of the occlusion covers
 *  (`ao_res`), from 1 to 4. */
static int ao_div(void)
{
    static int c_res = -1;
    const int  div   = (int)(geo_num(&c_res, "ao_res") + 0.5f);
    return div < 1 ? 1 : div > 4 ? 4
                                 : div;
}

/*  Does the occlusion run?  Over the mesh, with a strength, and not under
 *  the ground or on the map view. */
static int ao_wanted(const RGpuView *v)
{
    static int c_str = -1;
    return v->geometry && !v->underground && !v->plan && geo_num(&c_str, "ao_strength") > 0.0f;
}

/*  The frame's pixels a side that one pixel of the clouds' march covers
 *  (`cloud_res`), from 1 to 8. */
static int cloud_div(void)
{
    static int c_res = -1;
    const int  div   = (int)(geo_num(&c_res, "cloud_res") + 0.5f);
    return div < 1 ? 1 : div > 8 ? 8
                                 : div;
}

/*  The air's uniform, from the camera the frame draws with.
 *
 *  The ray is the way the camera looks.  Across the ground it runs
 *  where a step leaves the canvas still: the column less the row stays,
 *  and the sum falls.  It goes down at the camera's pitch.  The art draws
 *  a level shorter than the pitch would, so the pitch, not the art, says
 *  how steeply the camera looks through the air.
 *
 *  The camera stands back from the pivot by a share of the width the
 *  view shows.  So the air in front of the city grows as the view
 *  widens, as it does for a real camera that moves back. */
static void air_for(const RGpu *g, const RGpuView *v, const CamU *u, AirU *a)
{
    static int   c_tile = -1, c_level = -1, c_k = -1, c_haze = -1, c_albedo = -1, c_steps = -1, c_sun = -1;
    static float s_said[5] = {-1.0f, 0.0f, 0.0f, 0.0f, 0.0f}, s_multi[3], s_sky[3];
    static int   c_ce = -1, c_cs = -1, c_cd = -1;
    static int   c_g0 = -1, c_g1 = -1, c_gb = -1, c_oa = -1, c_on = -1, c_af = -1, c_zf = -1, c_zg = -1;
    static int   c_sx = -1, c_sh = -1, c_ss = -1, c_sg = -1, c_sr = -1, c_sgr = -1, c_sb = -1;
    static int   c_aor = -1, c_aos = -1, c_aob = -1;
    static int   c_shadow = -1, c_wisp = -1, c_reach = -1, c_er = -1, c_light = -1, c_alt = -1, c_thick = -1, c_scale = -1, c_speed = -1, c_wx = -1, c_wy = -1, c_hole = -1, c_ff = -1, c_ft = -1;
    const float  tm = geo_num(&c_tile, "air_tile_m"), lm = geo_num(&c_level, "air_level_m");
    float        r[3], sn, width;
    /*  The zoom: canvas pixels to target pixels over the display's own
     *  scale, at the art's size against the largest. */
    const float zoom = g->factor / (float)(v->scale < 1 ? 1 : v->scale) * (g->sw.level ? (float)g->sw.level->tile_w : 32.0f) / 32.0f;
    cam_ray(u, r);
    memset(a, 0, sizeof *a);
    memcpy(a->proj, u->proj, sizeof a->proj);
    a->view[0] = u->view[0], a->view[1] = u->view[1], a->view[2] = g->factor, a->view[3] = u->alt[0];
    a->turn[0] = u->alt[1], a->turn[1] = u->alt[2], a->turn[2] = u->alt[3], a->turn[3] = u->rot[3];
    a->grid[0] = u->rot[1], a->grid[1] = u->rot[2], a->grid[2] = tm, a->grid[3] = lm;
    /*  The width a view of 1280 points shows at this zoom, in meters: forty
     *  tiles at zoom one, each a tile's diagonal across.  A wider window
     *  shows more and moves nothing. */
    width     = 40.0f * 1.41421356f * tm / zoom;
    a->ray[0] = r[0], a->ray[1] = r[1], a->ray[2] = r[2];
    a->ray[3] = geo_num(&c_k, "air_cam_k") * width;
    sn        = sqrtf(g->sun[0] * g->sun[0] + g->sun[1] * g->sun[1] + g->sun[2] * g->sun[2]);
    a->sun[0] = g->sun[0] / sn, a->sun[1] = g->sun[1] / sn, a->sun[2] = g->sun[2] / sn;
    /*  A white surface lit to one by the sun takes pi of its light. */
    a->sun[3]   = 3.14159265f * geo_num(&c_sun, "air_sun") * g->exposure;
    a->steps[0] = geo_num(&c_steps, "air_steps");
    air_medium(geo_num(&c_haze, "air_haze"), &a->medium);
    /*  The light scattered many times depends on the sun, the haze and
     *  the ground alone.  It is integrated again only when one of them
     *  moves. */
    {
        float key[5];
        key[0] = geo_num(&c_haze, "air_haze"), key[1] = geo_num(&c_albedo, "air_albedo");
        key[2] = a->sun[0], key[3] = a->sun[1], key[4] = a->sun[2];
        if (memcmp(key, s_said, sizeof key) != 0)
        {
            air_multiple(&a->medium, a->sun, key[1], s_multi, s_sky);
            memcpy(s_said, key, sizeof key);
            R_DBG("air", "light scattered many times %.4g %.4g %.4g, the sky %.4g %.4g %.4g, the camera %.0f m back", (double)s_multi[0], (double)s_multi[1], (double)s_multi[2], (double)s_sky[0], (double)s_sky[1], (double)s_sky[2], (double)a->ray[3]);
        }
        memcpy(a->multi, s_multi, sizeof s_multi);
        memcpy(a->sky, s_sky, sizeof s_sky);
        a->sky[3] = key[1];
    }
    /*  The frame's own lighting, so a cloud's shadow takes away the sun's
     *  share of it and leaves the ambient (gpu_set_light). */
    a->shade[0] = g->sun[3] * luma(g->amb_rgb), a->shade[1] = g->params[0] * luma(g->sun_rgb) * (a->sun[2] > 0.0f ? a->sun[2] : 0.0f);
    /*  The clouds face the sun, so the exposure that lifts a low sun on
     *  the ground would burn them out.  Their sun keeps its hue and is
     *  held to its brightest channel at 1.1. */
    {
        float top = fmaxf(g->sun_rgb[0], fmaxf(g->sun_rgb[1], g->sun_rgb[2])), k = top > 1.1f ? 1.1f / top : 1.0f;
        a->daysun[0] = g->sun_rgb[0] * k, a->daysun[1] = g->sun_rgb[1] * k, a->daysun[2] = g->sun_rgb[2] * k;
    }
    memcpy(a->dayamb, g->amb_rgb, sizeof g->amb_rgb);
    a->dayamb[3] = g->exposure;
    memcpy(a->tint, g->tint, sizeof g->tint);
    a->tint[3] = g->tinted ? 1.0f : 0.0f;
    /*  The clouds' layer, from the sky's altitude and thickness in levels,
     *  and the share of the sky under cloud the view gives.  The art draws
     *  a level alt_step pixels tall, taller than a level's meters at the
     *  camera's pitch.  So a level of the layer is as many meters as make
     *  it as tall on the screen as the art's.  A cloud at a height then
     *  stands where a building of that height reaches.  The pitch is in
     *  both, so it cancels.  The clouds keep their own depth in meters. */
    {
        const float px_m = 2.0f * u->proj[2] / (tm * 1.41421356f);
        const float k    = px_m > 0.0f ? u->alt[0] / (ARC_PITCH_COS0 * lm * px_m) : 1.0f;
        a->cloud[0]      = geo_num(&c_alt, "sky_altitude") * lm * k;
        a->cloud[1]      = a->cloud[0] + geo_num(&c_thick, "sky_thickness") * lm;
    }
    a->cloud[2] = v->sky_cover, a->cloud[3] = geo_num(&c_ce, "cloud_ext");
    /*  The sky's field (gpu/clouds.h): the tiles its lowest octave spans,
     *  its mean and spread, and the tiles a texture of it spans.  The wind
     *  carries its first layer, and the second at six tenths of the speed
     *  on a wind turned a little. */
    {
        float scale = geo_num(&c_scale, "sky_scale"), sp2 = geo_num(&c_speed, "sky_speed") * v->time;
        float dx = geo_num(&c_wx, "sky_wind_x"), dy = geo_num(&c_wy, "sky_wind_y"), dl = sqrtf(dx * dx + dy * dy);
        dx = dl > 0.0f ? dx / dl : 0.0f, dy = dl > 0.0f ? dy / dl : 0.0f;
        a->skyf[0] = scale, a->skyf[1] = g->sky_stats[0], a->skyf[2] = g->sky_stats[1], a->skyf[3] = scale * (float)SKY_FIELD_CELLS;
        a->skyw[0] = dx * sp2 + g->sky_origin[0] * a->skyf[3], a->skyw[1] = dy * sp2 + g->sky_origin[1] * a->skyf[3];
        a->skyw[2] = 0.6f * (0.98f * a->skyw[0] - 0.2f * a->skyw[1]), a->skyw[3] = 0.6f * (0.2f * a->skyw[0] + 0.98f * a->skyw[1]);
        /*  The clouds part over this many meters round the pointer. */
        a->skym[0] = geo_num(&c_hole, "sky_cursor_hole") * tm, a->skym[1] = (float)r_map;
        a->skym[2] = geo_num(&c_ff, "sky_fade_from"), a->skym[3] = geo_num(&c_ft, "sky_fade_to");
        a->skyc[0] = v->sky_cursor[0], a->skyc[1] = v->sky_cursor[1];
        a->skyc[2] = geo_num(&c_reach, "sky_reach"), a->skyc[3] = geo_num(&c_wisp, "sky_wisp");
    }
    a->steps[2] = v->air ? 1.0f : 0.0f, a->steps[3] = geo_num(&c_light, "cloud_light");
    a->lowres[0] = (float)cloud_div(), a->lowres[2] = (float)ao_div();
    /*  The occlusion: its reach in tiles, its strength, the share of a
     *  tap's way it ignores, and the frame's pixels a tile's edge spans. */
    a->ao[0] = geo_num(&c_aor, "ao_radius"), a->ao[1] = geo_num(&c_aos, "ao_strength");
    a->ao[2] = geo_num(&c_aob, "ao_bias"), a->ao[3] = sqrtf(u->proj[2] * u->proj[2] + u->proj[3] * u->proj[3]) * g->factor;
    a->cloudk[0] = geo_num(&c_cs, "cloud_shape_m"), a->cloudk[1] = geo_num(&c_cd, "cloud_detail_m");
    a->cloudk[3] = geo_num(&c_er, "cloud_erode");
    a->cloudp[0] = geo_num(&c_g0, "cloud_g0"), a->cloudp[1] = geo_num(&c_g1, "cloud_g1");
    a->cloudp[2] = geo_num(&c_gb, "cloud_back"), a->cloudp[3] = geo_num(&c_oa, "cloud_octave_a");
    a->cloudq[0] = geo_num(&c_on, "cloud_octaves"), a->cloudq[1] = geo_num(&c_af, "cloud_amb_floor");
    a->cloudq[3] = geo_num(&c_shadow, "sky_shadow");
    /*  How much of the clouds the zoom shows: all of them out to one zoom,
     *  none from another in. */
    {
        float full = geo_num(&c_zf, "cloud_zoom_full"), gone = geo_num(&c_zg, "cloud_zoom_gone");
        float t      = (zoom - full) / (gone - full);
        t            = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f
                                                  : t;
        a->cloudq[2] = 1.0f - t * t * (3.0f - 2.0f * t);
    }
    a->smog[0] = geo_num(&c_sx, "smog_ext"), a->smog[1] = geo_num(&c_sh, "smog_h");
    a->smog[2] = (float)r_map, a->smog[3] = geo_num(&c_ss, "smog_scatter");
    a->smogabs[0] = geo_num(&c_sr, "smog_abs_r"), a->smogabs[1] = geo_num(&c_sgr, "smog_abs_g");
    a->smogabs[2] = geo_num(&c_sb, "smog_abs_b"), a->smogabs[3] = geo_num(&c_sg, "smog_g");
}

/*  Do any of a kind of slot hold vertices?  MESH_CHUNKS slots from
 *  `base`, `stride` apart.  The water's are the last MESH_CHUNKS. */
static int slots_held(const RGpu *g, int base, int stride)
{
    int k;
    for (k = 0; k < MESH_CHUNKS; ++k)
        if (g->slot[base + k * stride].lcount[0])
            return 1;
    return 0;
}

/*  One slot of the mesh drawn.  Plain, it is an indexed draw.  While
 *  the show moves pieces, the shader reads the corners itself (pulled),
 *  so the draw runs over the corners in order. */
static void draw_slot(SDL_GPURenderPass *rp, const RGpu *g, int k, int pulled)
{
    const GpuSlot *s = &g->slot[k];
    int            L = pulled ? 0 : g->lod_level;
    if (!s->lcount[L])
        return;
    if (pulled)
        SDL_DrawGPUPrimitives(rp, s->lcount[0], 1, s->lfirst[0], 0);
    else
        SDL_DrawGPUIndexedPrimitives(rp, s->lcount[L], 1, s->lfirst[L], 0, 0);
}

/*  The level of detail the camera draws: the coarsest whose error a
 *  screen pixel at this zoom still hides.  A tile's step along a column
 *  or a row is that many pixels on the screen. */
static int lod_for(const RGpu *g, const CamU *u)
{
    float px   = sqrtf(u->proj[2] * u->proj[2] + u->proj[3] * u->proj[3]) * g->factor;
    float hide = px > 1e-6f ? 1.0f / px : 1e9f;
    int   L    = 0;
    if (g_dev.no_lod)
        return 0;
    while (L + 1 < MESH_LOD_LEVELS && MESH_LOD_ERR0 * (float)(1u << L) <= hide)
        ++L;
    return L;
}

/*  THE TREES' LEVELS OF DETAIL, from the pixels a tile spans on the
 *  screen.  The view is a parallel projection, so a tile spans the same
 *  pixels in every chunk.  So the level a chunk's size picks is one for
 *  the frame.  Above trees_near_px each tree draws whole.  Below it each
 *  tree draws as a simple crown, and below trees_far_px no tree draws and
 *  the ground takes the canopy.  Each change is a band trees_band either
 *  side, where both levels draw and keep the pixels the other discards
 *  (dither4, terrain_body.frag).  So there is no pop, and a camera at
 *  rest in a band shows a still mix. */
typedef struct
{
    float to_crowns; /* the share of the pixels the crowns take from the whole trees */
    float to_ground; /* the share the ground's canopy takes from the crowns */
} TreeLevels;

static TreeLevels tree_levels(const RGpu *g, const CamU *u)
{
    static int  c_near = -1, c_far = -1, c_band = -1;
    const float px   = sqrtf(u->proj[2] * u->proj[2] + u->proj[3] * u->proj[3]) * g->factor;
    const float band = fmaxf(geo_num(&c_band, "trees_band"), 1e-3f);
    const float s0 = geo_num(&c_near, "trees_near_px"), s1 = geo_num(&c_far, "trees_far_px");
    TreeLevels  t;
    t.to_crowns = fminf(fmaxf((s0 * (1.0f + band) - px) / (2.0f * band * s0), 0.0f), 1.0f);
    t.to_ground = fminf(fmaxf((s1 * (1.0f + band) - px) / (2.0f * band * s1), 0.0f), 1.0f);
    return t;
}

/*  The chunk range of trees the sun's map draws at these levels: the
 *  level that holds most of the pixels, or none past the crowns. */
static int sun_trees(TreeLevels t)
{
    return t.to_crowns < 0.5f ? MESH_RANGE_TREES : t.to_ground < 0.5f ? MESH_RANGE_CROWNS
                                                                      : -1;
}

/*  The mesh's buffers bound for the draws after: the vertices and the
 *  corners as a plain draw reads them, or all three for the show's. */
static void bind_mesh(SDL_GPURenderPass *rp, const RGpu *g, int pulled, int marked)
{
    if (pulled)
    {
        SDL_GPUBuffer *sb[4];
        sb[0] = g->mbuf, sb[1] = g->mibuf, sb[2] = g->mkbuf, sb[3] = g->mtbuf;
        SDL_BindGPUVertexStorageBuffers(rp, 0, sb, marked ? 4u : 3u);
    }
    else
    {
        SDL_GPUBufferBinding vb, ib;
        memset(&vb, 0, sizeof vb);
        memset(&ib, 0, sizeof ib);
        vb.buffer = g->mbuf;
        ib.buffer = g->mibuf;
        SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
        SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    }
}

/*  THE SUN'S CLOCK.  The sun runs a fixed arc over the day: it rises
 *  near six and sets near eighteen, as on an equinox.  The sun the light is
 *  fitted to (FIT_SUN, as gpu_create sets it) lies on the arc.  The arc's
 *  pole leans back from the zenith along the row, so that the fitted
 *  sun lies on it at the fitted hour (FIT_HOUR).  `sun_hour` holds
 *  that hour by default, and there the sun is the fitted one exactly.  The hour
 *  turns the sun about the pole by fifteen degrees an hour.  Its
 *  height over the horizon gives its light:
 *
 *    the sun's color   what reaches the ground through the air
 *                      (air_sun), over what reaches it at the fitted
 *                      hour.  The air takes blue before red, so the sun
 *                      warms as it sinks, and it goes out at the horizon.
 *    the ambient       the sky's light on a level surface
 *                      (air_multiple), over its light at the fitted hour.
 *                      At night, when the sky is dark, a dim blue still
 *                      shows the city.
 *    the art's tint    the art carries the fitted hour's light.  At any
 *                      other hour it takes that hour's light over the
 *                      fitted hour's, on a level face.
 *
 *  The air is the one the frame is seen through, so the light and the
 *  haze agree at every hour.  The exposure lifts a dim hour's light as an
 *  eye does, and the air's own light with it. */
static const float FIT_SUN[3] = {0.35f, 0.85f, 0.9f};
#define FIT_HOUR 13.052f /* `sun_hour`'s default: where the arc meets the fitted sun */

static float smooth01(float x)
{
    x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f
                                   : x;
    return x * x * (3.0f - 2.0f * x);
}

static void sun_clock(RGpu *g)
{
    static const float NIGHT[3] = {0.20f, 0.25f, 0.42f};
    static int         c_hour = -1, c_haze = -1, c_albedo = -1;
    static float       s_key[3] = {-1.0f, 0.0f, 0.0f};
    const float        key[3]   = {geo_num(&c_hour, "sun_hour"), geo_num(&c_haze, "air_haze"), geo_num(&c_albedo, "air_albedo")};
    const float        hour     = key[0];
    AirMedium          med;
    float              f[3], n, lat, h, up, ks, day, w, t[3], t0[3], sky[3], sky0[3], psi[3];
    int                k;
    if (memcmp(key, s_key, sizeof key) == 0)
        return;
    memcpy(s_key, key, sizeof key);
    n    = sqrtf(FIT_SUN[0] * FIT_SUN[0] + FIT_SUN[1] * FIT_SUN[1] + FIT_SUN[2] * FIT_SUN[2]);
    f[0] = FIT_SUN[0] / n, f[1] = FIT_SUN[1] / n, f[2] = FIT_SUN[2] / n;
    lat = atan2f(f[1], f[2]);
    h   = asinf(f[0]) + (hour - FIT_HOUR) * (3.14159265f / 12.0f);
    if (hour == FIT_HOUR)
        g->sun[0] = f[0], g->sun[1] = f[1], g->sun[2] = f[2];
    else
        g->sun[0] = sinf(h), g->sun[1] = cosf(h) * sinf(lat), g->sun[2] = cosf(h) * cosf(lat);
    up = g->sun[2];
    /*  The air's own answer for the hour's sun and for the fitted one.
     *  The sun's disc sets over a little of its height, so its light
     *  goes out smoothly at the horizon. */
    air_medium(key[1], &med);
    air_sun(&med, g->sun, t);
    air_sun(&med, f, t0);
    air_multiple(&med, g->sun, key[2], psi, sky);
    air_multiple(&med, f, key[2], psi, sky0);
    ks  = smooth01((up + 0.01f) / 0.04f);
    day = smooth01((up + 0.10f) / 0.20f);
    for (k = 0; k < 3; ++k)
    {
        const float s = t0[k] > 0.0f ? t[k] / t0[k] : 0.0f, a = sky0[k] > 0.0f ? sky[k] / sky0[k] : 0.0f;
        g->sun_rgb[k] = hour == FIT_HOUR ? 1.0f : ks * (s < 1.3f ? s : 1.3f);
        g->amb_rgb[k] = hour == FIT_HOUR ? 1.0f : a + NIGHT[k] * (1.0f - day);
    }
    /*  The exposure: as an eye or a camera does, the frame takes in more
     *  of a dim hour's light.  A power under one keeps dusk dimmer than
     *  noon, and a bound keeps the night dark. */
    w = g->sun[3] + g->params[0] * f[2];
    {
        float lum   = g->sun[3] * luma(g->amb_rgb) + g->params[0] * luma(g->sun_rgb) * (up > 0.0f ? up : 0.0f);
        float e     = lum > 1e-6f ? powf(w / lum, 0.75f) : 2.5f;
        g->exposure = hour == FIT_HOUR ? 1.0f : e < 1.0f ? 1.0f
                                            : e > 2.5f   ? 2.5f
                                                         : e;
        for (k = 0; k < 3; ++k)
            g->sun_rgb[k] *= g->exposure, g->amb_rgb[k] *= g->exposure;
    }
    g->tinted = 0;
    for (k = 0; k < 3; ++k)
    {
        g->tint[k] = (g->sun[3] * g->amb_rgb[k] + g->params[0] * g->sun_rgb[k] * (up > 0.0f ? up : 0.0f)) / w;
        g->tinted |= fabsf(g->tint[k] - 1.0f) > 2e-3f;
    }
    g->tint[3] = 0.0f;
}

/*  THE SUN'S SHADOW MAP.  The sun looks down its own way, so its frame
 *  has three axes: two across that way and one along it.  A grid point
 *  goes into the frame with a level as MESH_LEVEL_H tiles, the metric
 *  the normals and the sun's way are in.  The map covers a square of
 *  the sun's frame round every chunk in view, with a margin.  It is
 *  drawn again only when the mesh, the sun or the level of detail
 *  changes.  It is also drawn again when the view leaves the square, or
 *  when the view closes in so far that the square's texels are coarse.  So a still or a
 *  panning view draws nothing for its shadows. */
typedef struct
{
    float u[3], v[3], l[3];
} SunAxes;

static void sun_axes(const RGpu *g, SunAxes *a)
{
    float n = sqrtf(g->sun[0] * g->sun[0] + g->sun[1] * g->sun[1] + g->sun[2] * g->sun[2]);
    float h;
    a->l[0] = g->sun[0] / n, a->l[1] = g->sun[1] / n, a->l[2] = g->sun[2] / n;
    h = sqrtf(a->l[0] * a->l[0] + a->l[1] * a->l[1]);
    if (h < 1e-4f)
        a->u[0] = 1.0f, a->u[1] = 0.0f, a->u[2] = 0.0f;
    else
        a->u[0] = a->l[1] / h, a->u[1] = -a->l[0] / h, a->u[2] = 0.0f;
    a->v[0] = a->l[1] * a->u[2] - a->l[2] * a->u[1];
    a->v[1] = a->l[2] * a->u[0] - a->l[0] * a->u[2];
    a->v[2] = a->l[0] * a->u[1] - a->l[1] * a->u[0];
}

/*  Chunk k's bounds in the sun's frame: low u, low v, high u, high v,
 *  and its nearest and furthest from the sun.  They come from its eight
 *  corners at the ground's floor and highest level, as chunk_visible
 *  takes them. */
static void sun_chunk(const SunAxes *a, int k, float b[6])
{
    int i;
    b[0] = b[1] = b[4] = 1e30f;
    b[2] = b[3] = b[5] = -1e30f;
    for (i = 0; i < 8; ++i)
    {
        float c  = (float)((k % (r_map / MESH_CHUNK)) * MESH_CHUNK + ((i & 1) ? MESH_CHUNK : 0));
        float r  = (float)((k / (r_map / MESH_CHUNK)) * MESH_CHUNK + ((i & 2) ? MESH_CHUNK : 0));
        float z  = ((i & 4) ? 32.0f : 0.0f) * MESH_LEVEL_H;
        float pu = c * a->u[0] + r * a->u[1] + z * a->u[2];
        float pv = c * a->v[0] + r * a->v[1] + z * a->v[2];
        float pl = c * a->l[0] + r * a->l[1] + z * a->l[2];
        b[0]     = pu < b[0] ? pu : b[0];
        b[1]     = pv < b[1] ? pv : b[1];
        b[2]     = pu > b[2] ? pu : b[2];
        b[3]     = pv > b[3] ? pv : b[3];
        b[4]     = pl < b[4] ? pl : b[4];
        b[5]     = pl > b[5] ? pl : b[5];
    }
}

/*  Is the sun's shadow drawn this frame?  Not under the ground, on the
 *  map view, with the sun down or without the mesh.  While the land
 *  settles each point takes its shadow as it settles (terrain_body.frag). */
static int sun_wanted(const RGpu *g, const RGpuView *v, const CamU *cam)
{
    static int c_on = -1;
    return v->geometry && g->mesh_n && g->pipe_sun && g->sun[2] > 0.02f && g->sun_map && !v->underground && !v->plan &&
           cam->mode[0] < 0.5f && geo_num(&c_on, "sun_shadow") > 0.0f;
}

/*  Make the sun's map hold for this view, drawing it again only when it
 *  must.  Run before the frame's pass, in a pass of its own. */
static void sun_frame(RGpu *g, SDL_GPUCommandBuffer *cmd, const RGpuView *v, const CamU *cam)
{
    static int c_res = -1, c_margin = -1;
    SunAxes    a;
    float      need[4] = {1e30f, 1e30f, -1e30f, -1e30f}, key[6];
    int        k, any = 0, lod, trees;
    if (!sun_wanted(g, v, cam))
        return;
    {
        int n = (int)geo_num(&c_res, "sun_shadow_res");
        n     = n < 256 ? 256 : n > 8192 ? 8192
                                         : n;
        if (ensure_sun_map(g, n) != 0)
            return;
    }
    sun_axes(g, &a);
    for (k = 0; k < MESH_CHUNKS; ++k)
    {
        float b[6];
        if (!chunk_visible(cam, k))
            continue;
        sun_chunk(&a, k, b);
        need[0] = b[0] < need[0] ? b[0] : need[0];
        need[1] = b[1] < need[1] ? b[1] : need[1];
        need[2] = b[2] > need[2] ? b[2] : need[2];
        need[3] = b[3] > need[3] ? b[3] : need[3];
        any     = 1;
    }
    if (!any)
        return;
    lod    = lod_for(g, cam);
    trees  = sun_trees(tree_levels(g, cam));
    key[0] = a.l[0], key[1] = a.l[1], key[2] = a.l[2], key[3] = (float)lod, key[4] = (float)g->mesh_gen, key[5] = (float)trees;
    {
        const float side = fmaxf(need[2] - need[0], need[3] - need[1]);
        const float held = g->sun_box[2] - g->sun_box[0];
        if (g->sun_valid && memcmp(key, g->sun_key, sizeof key) == 0 && need[0] >= g->sun_box[0] &&
            need[1] >= g->sun_box[1] && need[2] <= g->sun_box[2] && need[3] <= g->sun_box[3] && side * 2.5f > held)
            return;
        /*  The new square: the view's own, widened by the margin, its
         *  middle on a texel so a pan does not crawl the shadows' edges. */
        {
            const float margin = fmaxf(geo_num(&c_margin, "sun_shadow_margin"), 1.0f);
            const float S      = side * margin;
            const float texel  = S / (float)g->sun_n;
            float       cu     = floorf(0.5f * (need[0] + need[2]) / texel) * texel;
            float       cv     = floorf(0.5f * (need[1] + need[3]) / texel) * texel;
            float       lo = 1e30f, hi = -1e30f;
            g->sun_box[0] = cu - 0.5f * S, g->sun_box[1] = cv - 0.5f * S;
            g->sun_box[2] = cu + 0.5f * S, g->sun_box[3] = cv + 0.5f * S;
            g->sun_box[4] = texel;
            /*  The depth runs over every chunk the square reaches, which
             *  holds everything that can shade what is in it. */
            for (k = 0; k < MESH_CHUNKS; ++k)
            {
                float b[6];
                sun_chunk(&a, k, b);
                if (b[2] < g->sun_box[0] || b[0] > g->sun_box[2] || b[3] < g->sun_box[1] || b[1] > g->sun_box[3])
                    continue;
                lo = b[4] < lo ? b[4] : lo;
                hi = b[5] > hi ? b[5] : hi;
            }
            {
                const float s2 = 2.0f / S, dr = 1.0f / fmaxf(hi - lo, 1e-3f);
                g->sun_m[0][0] = a.u[0] * s2, g->sun_m[0][1] = a.u[1] * s2, g->sun_m[0][2] = a.u[2] * MESH_LEVEL_H * s2;
                g->sun_m[0][3] = -cu * s2;
                g->sun_m[1][0] = a.v[0] * s2, g->sun_m[1][1] = a.v[1] * s2, g->sun_m[1][2] = a.v[2] * MESH_LEVEL_H * s2;
                g->sun_m[1][3] = -cv * s2;
                g->sun_m[2][0] = -a.l[0] * dr, g->sun_m[2][1] = -a.l[1] * dr, g->sun_m[2][2] = -a.l[2] * MESH_LEVEL_H * dr;
                g->sun_m[2][3] = hi * dr;
            }
        }
    }
    {
        SDL_GPUDepthStencilTargetInfo dt;
        SDL_GPURenderPass            *rp;
        int                           drawn = 0;
        memset(&dt, 0, sizeof dt);
        dt.texture          = g->sun_map;
        dt.clear_depth      = 1.0f;
        dt.load_op          = SDL_GPU_LOADOP_CLEAR;
        dt.store_op         = SDL_GPU_STOREOP_STORE;
        dt.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
        dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        rp                  = SDL_BeginGPURenderPass(cmd, NULL, 0, &dt);
        if (!rp)
            return;
        SDL_BindGPUGraphicsPipeline(rp, g->pipe_sun);
        SDL_PushGPUVertexUniformData(cmd, 0, g->sun_m, sizeof g->sun_m);
        bind_mesh(rp, g, 0, 0);
        g->lod_level = lod;
        for (k = 0; k < MESH_CHUNKS; ++k)
        {
            float b[6];
            sun_chunk(&a, k, b);
            if (b[2] < g->sun_box[0] || b[0] > g->sun_box[2] || b[3] < g->sun_box[1] || b[1] > g->sun_box[3])
                continue;
            draw_slot(rp, g, MESH_RANGES * k + MESH_RANGE_GROUND, 0);
            draw_slot(rp, g, MESH_RANGES * k + MESH_RANGE_STANDS, 0);
            if (trees >= 0)
                draw_slot(rp, g, MESH_RANGES * k + trees, 0);
            ++drawn;
        }
        SDL_EndGPURenderPass(rp);
        memcpy(g->sun_key, key, sizeof key);
        g->sun_valid = 1;
        if (g_dev.times)
            dumpf("time  frame  the sun's map drawn again: %d chunks, a texel %.3f tiles\n", drawn, (double)g->sun_box[4]);
    }
}

/*  The terrain mesh, in the depth pass. */
/*  The mesh, in two draws.  The opaque list goes first, before the art.
 *  The blended list is drawn after the ground's art (`blended`), so what
 *  is under it shows through and the art does not cover it. */
static void draw_mesh(RGpu *g, SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *rp, const RGpuView *v, const CamU *cam, const RAtlasLevel *l, int blended)
{
    /*  While the land settles, the sheet stands over every chunk not yet
     *  built, and the mesh draws with the shaders that settle it.  An
     *  edit's show and the tool's marks wait until it has settled. */
    const int settling = v->settle[6] > 0.5f && g->settle && g->land && g->pipe_sheet;
    if (v->geometry && (g->mesh_n || settling) && g->pipe_mesh)
    {
        LightU           lu;
        const TreeLevels levels = tree_levels(g, cam);
        (void)l;
        memcpy(lu.sun, g->sun, sizeof lu.sun);
        memcpy(lu.params, g->params, sizeof lu.params);
        lu.params[1] = v->time;
        /*  The grid's half-width in canvas pixels: half a pixel, or in the
         *  underground view half a DEVICE pixel, a hairline at any scale. */
        lu.params[2] = v->underground ? 0.5f : (v->grid ? 0.5f * g->factor : 0.0f);
        lu.params[3] = v->underground ? 1.0f : (v->plan ? -1.0f : 0.0f); /* the map view is -1: the tints' switch */
        /*  The materials named at run time.  Anything not named is left
         *  black and matt, which is what a material nobody defined should
         *  look like. */
        {
            const float *m = NULL;
            int          n = material_list(&m);
            memset(lu.mats, 0, sizeof lu.mats);
            if (m && n > 0)
                memcpy(lu.mats, m, sizeof lu.mats[0] * (size_t)(n < MAT_RUNTIME_MAX ? n : MAT_RUNTIME_MAX));
            memcpy(lu.fine, material_fine_list(), sizeof lu.fine);
            memcpy(lu.leaf, g->leaf, sizeof lu.leaf);
            memcpy(lu.rag, g->leaf_rag, sizeof lu.rag);
            memset(lu.trees, 0, sizeof lu.trees);
            lu.trees[3] = levels.to_ground;
            memset(lu.strata, 0, sizeof lu.strata);
            memset(lu.strata_n, 0, sizeof lu.strata_n);
            memcpy(lu.strata, STRATA, sizeof STRATA);
            lu.strata_n[0] = (float)N_STRATA;
        }
        {
            static int c_sh = -1;
            memcpy(lu.sunm, g->sun_m, sizeof lu.sunm);
            lu.sunsh[0] = g->sun_valid && sun_wanted(g, v, cam) ? 1.0f : 0.0f;
            lu.sunsh[1] = geo_num(&c_sh, "sun_shadow");
            lu.sunsh[2] = g->sun_box[4];
            lu.sunsh[3] = MESH_LEVEL_H;
        }
        {
            static int c_bump = -1, c_wide = -1, c_clear = -1;
            lu.ground[0] = geo_num(&c_bump, "ground_bump");
            lu.ground[1] = geo_num(&c_wide, "ground_bump_tiles");
            lu.ground[2] = (float)SKY_FIELD_CELLS;
            lu.ground[3] = geo_num(&c_clear, "water_clarity");
        }
        {
            int k;
            for (k = 0; k < 3; ++k)
                lu.amb[k] = g->sun[3] * g->amb_rgb[k], lu.suncol[k] = g->params[0] * g->sun_rgb[k];
            lu.amb[3] = lu.suncol[3] = 0.0f;
            {
                /*  The lamps come on as the hour's sun fades. */
                float top = fmaxf(g->sun_rgb[0], fmaxf(g->sun_rgb[1], g->sun_rgb[2])), on = (0.35f - top) / 0.30f;
                on         = on < 0.0f ? 0.0f : on > 1.0f ? 1.0f
                                                          : on;
                on         = on * on * (3.0f - 2.0f * on);
                lu.lamp[0] = 1.0f * on, lu.lamp[1] = 0.78f * on, lu.lamp[2] = 0.50f * on, lu.lamp[3] = 0.0f;
            }
        }
        {
            static int c_cells = -1, c_cover = -1;
            lu.cover[0] = geo_num(&c_cells, "cover_tiles");
            lu.cover[1] = g->sky_stats[0];
            lu.cover[2] = g->sky_stats[1];
            lu.cover[3] = geo_num(&c_cover, "cover");
        }
        /*  The water reads the camera's real way, and its swells move
         *  on the wind the clouds move on. */
        {
            static int c_wave = -1, c_long = -1, c_speed = -1, c_wx = -1, c_wy = -1;
            float      r[3], wx = geo_num(&c_wx, "sky_wind_x"), wy = geo_num(&c_wy, "sky_wind_y");
            float      wl = sqrtf(wx * wx + wy * wy), sp = geo_num(&c_speed, "sky_speed");
            cam_ray(cam, r);
            lu.eye[0] = -r[0], lu.eye[1] = -r[1], lu.eye[2] = -r[2], lu.eye[3] = 0.0f;
            lu.wave[0] = geo_num(&c_wave, "water_wave");
            lu.wave[1] = geo_num(&c_long, "water_wave_tiles");
            lu.wave[2] = wl > 0.0f ? wx / wl * sp : 0.0f;
            lu.wave[3] = wl > 0.0f ? wy / wl * sp : 0.0f;
        }
        /*  Every draw's samplers end with the sun's map, the noise the
         *  ground's bumps are read from and what the build lays over the
         *  ground. */
        SDL_GPUTextureSamplerBinding fb[4];
        fb[0].texture = g->shore;
        fb[0].sampler = g->linear;
        fb[1].texture = g->sun_map;
        fb[1].sampler = g->sun_cmp;
        fb[2].texture = g->sky_field;
        fb[2].sampler = g->wrap;
        fb[3].texture = g->laid;
        fb[3].sampler = g->linear;
        /*  While pieces an edit reached move, the shaders that move them
         *  draw (show.h).  They read every piece's state by its id, and
         *  the numbers that say how the pieces move.  The triangle view
         *  reads the mesh from its buffers the same way, with nothing
         *  moving.  A still frame draws with the plain ones. */
        const int                    tri_view = v->tris && g->pipe_mesh_tris && !settling;
        const int                    moving   = (v->show[15] > 0.5f && !settling) || tri_view;
        SDL_GPUTextureSamplerBinding sb;
        sb.texture = g->show;
        sb.sampler = g->nearest;
        /*  While anything is marked, every draw of the mesh lights what
         *  belongs to the marked tiles (terrain_mark.frag), settling or
         *  not, as the art does.  It reads each triangle's tile from the
         *  buffers, as the edit show reads its key, so it draws pulled. */
        const int                    marked = g->marks_on && g->marks && g->mtbuf && !tri_view;
        const int                    pulled = moving || marked;
        SDL_GPUTextureSamplerBinding fm[5];
        fm[0]         = fb[0];
        fm[1].texture = g->marks;
        fm[1].sampler = g->nearest;
        fm[2]         = fb[1];
        fm[3]         = fb[2];
        fm[4]         = fb[3];
        if (!marked)
            fm[1] = fb[1], fm[2] = fb[2], fm[3] = fb[3];
        /*  What the settle reads: each chunk's state, filtered so it
         *  varies smoothly from chunk to chunk, and each tile's corners. */
        SettleU su;
        settle_uniform(v, &su);
        SDL_GPUTextureSamplerBinding ss[2];
        ss[0].texture = g->settle;
        ss[0].sampler = g->linear;
        ss[1].texture = g->land;
        ss[1].sampler = g->nearest;
        if (!blended && settling)
        {
            const Uint32 side = (Uint32)v->settle[5], per = (Uint32)v->settle[6];
            SDL_BindGPUGraphicsPipeline(rp, g->pipe_sheet);
            SDL_BindGPUVertexSamplers(rp, 0, ss, 2);
            SDL_PushGPUVertexUniformData(cmd, 0, cam, sizeof *cam);
            SDL_PushGPUVertexUniformData(cmd, 1, &su, sizeof su);
            SDL_BindGPUFragmentSamplers(rp, 0, fb, 4);
            SDL_PushGPUFragmentUniformData(cmd, 0, &lu, sizeof lu);
            SDL_DrawGPUPrimitives(rp, 6u * side * side, per * per, 0, 0);
        }
        if (!g->mesh_n)
            return;
        if (!blended)
        {
            if (settling)
                SDL_BindGPUGraphicsPipeline(rp, marked ? g->pipe_mesh_settle_mark : g->pipe_mesh_settle);
            else if (tri_view)
                SDL_BindGPUGraphicsPipeline(rp, g->pipe_mesh_tris);
            else if (marked)
                SDL_BindGPUGraphicsPipeline(rp, g->pipe_mesh_mark);
            else
                SDL_BindGPUGraphicsPipeline(rp, moving ? g->pipe_mesh_show : g->pipe_mesh);
            SDL_BindGPUFragmentSamplers(rp, 0, fm, marked ? 5 : 4);
            SDL_PushGPUVertexUniformData(cmd, 0, cam, sizeof *cam);
            if (pulled && !settling)
            {
                SDL_BindGPUVertexSamplers(rp, 0, &sb, 1);
                SDL_PushGPUVertexUniformData(cmd, 1, v->show, sizeof v->show);
            }
            if (settling)
            {
                SDL_BindGPUVertexSamplers(rp, 0, ss, 2);
                SDL_PushGPUVertexUniformData(cmd, 1, &su, sizeof su);
            }
            SDL_PushGPUFragmentUniformData(cmd, 0, &lu, sizeof lu);
            bind_mesh(rp, g, pulled, marked);
            /*  A simplified chunk's long edges cannot follow the waves, so
             *  the ground is drawn whole while it settles.  Only the whole
             *  ground's triangles know their tiles, so a marked frame draws
             *  it whole too. */
            g->lod_level = settling || marked ? 0 : lod_for(g, cam);
            /*  By chunk, when the mesh is bucketed: each chunk in view drawn
             *  as its terrain range then its network range.  A chunk outside
             *  the view, its eight corners, ground and highest ground, all
             *  past one edge in the shader's own projection, is skipped.
             *  This is what keeps a world 100x this one drawable.
             *  Unbucketed, the whole list. */
            {
                static uint8_t seen[MESH_CHUNKS_CAP];
                int            k, r, drawn = 0;
                uint32_t       tris = 0, whole = 0;
                for (k = 0; k < MESH_CHUNKS; ++k)
                {
                    seen[k] = (uint8_t)chunk_visible(cam, k);
                    if (!seen[k])
                        continue;
                    ++drawn;
                    for (r = MESH_RANGE_GROUND; r <= MESH_RANGE_STANDS; ++r)
                    {
                        draw_slot(rp, g, MESH_RANGES * k + r, pulled);
                        tris += g->slot[MESH_RANGES * k + r].lcount[g->lod_level] / 3u;
                        whole += g->slot[MESH_RANGES * k + r].lcount[0] / 3u;
                    }
                }
                /*  The trees at their levels, each level a pass over the
                 *  chunks in view, with the share of the dither it keeps. */
                for (r = MESH_RANGE_TREES; r <= MESH_RANGE_CROWNS; ++r)
                {
                    const float lo = r == MESH_RANGE_TREES ? levels.to_crowns : levels.to_ground;
                    const float hi = r == MESH_RANGE_TREES ? 2.0f : levels.to_crowns;
                    if (lo >= hi)
                        continue;
                    lu.trees[0] = 1.0f, lu.trees[1] = lo, lu.trees[2] = hi;
                    SDL_PushGPUFragmentUniformData(cmd, 0, &lu, sizeof lu);
                    for (k = 0; k < MESH_CHUNKS; ++k)
                        if (seen[k])
                        {
                            draw_slot(rp, g, MESH_RANGES * k + r, pulled);
                            tris += g->slot[MESH_RANGES * k + r].lcount[0] / 3u;
                        }
                }
                for (k = 0; k < MESH_CHUNKS; ++k)
                    for (r = MESH_RANGE_TREES; r <= MESH_RANGE_CROWNS; ++r)
                        whole += g->slot[MESH_RANGES * k + r].lcount[0] / 3u;
                lu.trees[0] = 0.0f;
                SDL_PushGPUFragmentUniformData(cmd, 0, &lu, sizeof lu);
                if (g_dev.times && !s_chunks_said)
                {
                    s_chunks_said = 1;
                    dumpf("time  frame  chunks drawn %d of %d, at level of detail %d: %u triangles of %u\n", drawn, MESH_CHUNKS, g->lod_level, tris, whole);
                }
            }
        }
        /*  The water, blended: every chunk's slot in order, whole: the
         *  order its faces were sorted into, as the one list was drawn. */
        if (blended && slots_held(g, MESH_RANGES * MESH_CHUNKS, 1))
        {
            int k;
            SDL_BindGPUGraphicsPipeline(rp, settling ? (marked ? g->pipe_mesh_blend_settle_mark : g->pipe_mesh_blend_settle) : marked ? g->pipe_mesh_blend_mark
                                                                                                                           : moving   ? g->pipe_mesh_blend_show
                                                                                                                                      : g->pipe_mesh_blend);
            bind_mesh(rp, g, pulled, marked);
            SDL_BindGPUFragmentSamplers(rp, 0, marked ? fm : fb, marked ? 5 : 4);
            SDL_PushGPUVertexUniformData(cmd, 0, cam, sizeof *cam);
            if (pulled && !settling)
            {
                SDL_BindGPUVertexSamplers(rp, 0, &sb, 1);
                SDL_PushGPUVertexUniformData(cmd, 1, v->show, sizeof v->show);
            }
            if (settling)
            {
                SDL_BindGPUVertexSamplers(rp, 0, ss, 2);
                SDL_PushGPUVertexUniformData(cmd, 1, &su, sizeof su);
            }
            SDL_PushGPUFragmentUniformData(cmd, 0, &lu, sizeof lu);
            for (k = 0; k < MESH_CHUNKS; ++k)
                draw_slot(rp, g, MESH_RANGES * MESH_CHUNKS + k, pulled);
        }
    }
}

/*  Draw passes 1 and 2 (the frame target and the shadow mask) and then
 *  resolve to `final`.  This is the swapchain texture or the offscreen
 *  one.  `sw`/`sh` are the final target's size in pixels. */
static int draw_frame(RGpu *g, SDL_GPUCommandBuffer *cmd, const RGpuView *v, SDL_GPUTexture *final, SDL_GPUGraphicsPipeline *resolve_pipe, int32_t sw, int32_t sh)
{
    Ranges                        r;
    SDL_GPUCopyPass              *cp;
    SDL_GPURenderPass            *rp;
    SDL_GPUColorTargetInfo        ct;
    SDL_GPUDepthStencilTargetInfo dt;
    SDL_GPUBufferBinding          vb;
    SDL_GPUTextureSamplerBinding  ts[3];
    CamU                          cam, scam;
    FragU                         fu;
    uint32_t                      bytes, off;
    const RAtlasLevel            *l = g->sw.level;

    if (build_visible(g, v, &r) != 0)
        return -1;
    /*  Raised off the game's own pitch the mesh draws alone.  The
     *  sprites are drawn for one camera and cannot be looked at from
     *  above.  There they would only cover the world they are meant to
     *  show.  The structures the player placed are the exception.  They
     *  are kept: a map wants its landmarks on it, and the original's own
     *  art is what says which is which. */
    if (v->mesh_only || view_pitch(v) > 30.5f)
    {
        r.n_terrain = r.n_water = r.n_shadow = 0;
        if (v->mesh_only)
            r.n_sprite = 0;
    }
    if (ensure_instances(g, g->last_drawn ? g->last_drawn : 1) != 0)
        return -1;

    /*  Uploads: the visible instances and, when it changed, the palette. */
    cp    = SDL_BeginGPUCopyPass(cmd);
    bytes = g->last_drawn * (uint32_t)sizeof(RInst);
    if (bytes)
    {
        SDL_GPUTransferBufferLocation src;
        SDL_GPUBufferRegion           dst;
        void                         *map = SDL_MapGPUTransferBuffer(g->dev, g->itb, true);
        if (!map)
        {
            SDL_EndGPUCopyPass(cp);
            return -1;
        }
        memcpy(map, g->vis, bytes);
        SDL_UnmapGPUTransferBuffer(g->dev, g->itb);
        memset(&src, 0, sizeof src);
        memset(&dst, 0, sizeof dst);
        src.transfer_buffer = g->itb;
        dst.buffer          = g->ibuf;
        dst.size            = bytes;
        SDL_UploadToGPUBuffer(cp, &src, &dst, true);
    }
    if (g->pal_dirty)
    {
        SDL_GPUTextureTransferInfo src;
        SDL_GPUTextureRegion       dst;
        void                      *map = SDL_MapGPUTransferBuffer(g->dev, g->ptb, true);
        if (map)
        {
            memcpy(map, g->palette, sizeof g->palette);
            SDL_UnmapGPUTransferBuffer(g->dev, g->ptb);
            memset(&src, 0, sizeof src);
            memset(&dst, 0, sizeof dst);
            src.transfer_buffer = g->ptb;
            src.pixels_per_row  = 256;
            src.rows_per_layer  = 1;
            dst.texture         = g->pal;
            dst.w               = 256;
            dst.h               = 1;
            dst.d               = 1;
            SDL_UploadToGPUTexture(cp, &src, &dst, true);
            g->pal_dirty = 0;
        }
    }
    SDL_EndGPUCopyPass(cp);

    cam_for(g, v, &cam);
    /*  The sprites' own camera.
     *
     *      The turn left from their sweep's quarter.  About the map's
     *      center.  The translation onto the mesh's turn about the pivot
     *      (sprite_turn).
     *
     *  Both the sine AND the cosine are tested.  A cosine of minus one
     *  is a half turn, and with the art carrying real depth a sine-only
     *  test drops half the sprites. */
    scam = cam;
    {
        SpriteTurn st;
        sprite_turn(v, &st);
        scam.alt[2]  = st.ca;
        scam.alt[3]  = st.sa;
        scam.rot[0]  = st.moved ? 1.0f : 0.0f;
        scam.rot[1]  = st.cx;
        scam.rot[2]  = st.cy;
        scam.mode[2] = st.dx;
        scam.mode[3] = st.dy;
    }
    memset(&fu, 0, sizeof fu);
    fu.p[0] = g->transparent;

    /*  The sun where the hour puts it, and its shadow map, when it must
     *  be drawn again. */
    sun_clock(g);
    sun_frame(g, cmd, v, &cam);

    /*  Pass 1: the frame.  Cleared to the sky, with the background's
     *  palette index in alpha as the software rasteriser snaps it. */
    memset(&ct, 0, sizeof ct);
    memset(&dt, 0, sizeof dt);
    ct.texture          = g->color;
    ct.clear_color.r    = (float)g->palette[g->bg_index][0] / 255.0f;
    ct.clear_color.g    = (float)g->palette[g->bg_index][1] / 255.0f;
    ct.clear_color.b    = (float)g->palette[g->bg_index][2] / 255.0f;
    ct.clear_color.a    = (float)g->bg_index / 255.0f;
    ct.load_op          = SDL_GPU_LOADOP_CLEAR;
    ct.store_op         = SDL_GPU_STOREOP_STORE;
    dt.texture          = g->depth;
    dt.clear_depth      = 1.0f;
    dt.load_op          = SDL_GPU_LOADOP_CLEAR;
    dt.store_op         = SDL_GPU_STOREOP_STORE;
    dt.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
    dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    rp                  = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);

    memset(ts, 0, sizeof ts);
    ts[0].texture = g->atlas[g->level];
    ts[0].sampler = g->nearest;
    ts[1].texture = g->pal;
    ts[1].sampler = g->nearest;

    draw_mesh(g, cmd, rp, v, &cam, l, 0);

    SDL_PushGPUVertexUniformData(cmd, 0, &scam, sizeof scam);
    SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
    /*  While the player's tool marks a tile, the art on it is lit
     *  (sprite_mark.frag), from the marks in the sweep's quarter. */
    const int art_marked = g->marks_on && g->marks_sweep;
    ts[2].texture        = g->marks_sweep;
    ts[2].sampler        = g->nearest;
    SDL_BindGPUFragmentSamplers(rp, 0, ts, art_marked ? 3 : 2);
    off = 0;
    if (r.n_terrain)
    {
        SDL_BindGPUGraphicsPipeline(rp, art_marked ? g->pipe_terrain_mark : g->pipe_terrain);
        vb.buffer = g->ibuf;
        vb.offset = off * (uint32_t)sizeof(RInst);
        SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
        SDL_DrawGPUPrimitives(rp, 12, r.n_terrain, 0, 0);
    }
    off += r.n_terrain;
    draw_mesh(g, cmd, rp, v, &cam, l, 1);
    SDL_PushGPUVertexUniformData(cmd, 0, &scam, sizeof scam);
    SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
    SDL_BindGPUFragmentSamplers(rp, 0, ts, art_marked ? 3 : 2);
    /*  The water's art, as the original paints it.  It draws only
     *  without the mesh, whose water is a body of faces. */
    if (r.n_water)
    {
        SDL_BindGPUGraphicsPipeline(rp, g->pipe_terrain);
        SDL_BindGPUFragmentSamplers(rp, 0, ts, 2);
        vb.buffer = g->ibuf;
        vb.offset = off * (uint32_t)sizeof(RInst);
        SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
        SDL_DrawGPUPrimitives(rp, 12, r.n_water, 0, 0);
        SDL_BindGPUFragmentSamplers(rp, 0, ts, art_marked ? 3 : 2);
    }
    off += r.n_water;
    if (r.n_sprite)
    {
        if (art_marked)
            SDL_BindGPUGraphicsPipeline(rp, v->plain_sweep ? g->pipe_terrain_mark : cam.mode[0] > 0.5f ? g->pipe_sprite_mark
                                                                                                       : g->pipe_sprite_depth_mark);
        else
            SDL_BindGPUGraphicsPipeline(rp, v->plain_sweep ? g->pipe_terrain : cam.mode[0] > 0.5f ? g->pipe_sprite
                                                                                                  : g->pipe_sprite_depth);
        vb.buffer = g->ibuf;
        vb.offset = off * (uint32_t)sizeof(RInst);
        SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
        SDL_DrawGPUPrimitives(rp, 12, r.n_sprite, 0, 0);
    }
    off += r.n_sprite;
    /*  The counts drawn, said every frame, so behind a switch of their own
     *  (--gpu-counts) as well as the debug level. */
    if (g_dev.gpu_counts)
        R_DBG("gpu", "terrain %u water %u sprite %u shadow %u, mesh %u", (unsigned)r.n_terrain, (unsigned)r.n_water, (unsigned)r.n_sprite, (unsigned)r.n_shadow, (unsigned)(v->geometry ? g->mesh_n : 0));
    SDL_EndGPURenderPass(rp);

    /*  Pass 2: the shadow mask, tested against the same depth. */
    memset(&ct, 0, sizeof ct);
    ct.texture  = g->shadow;
    ct.load_op  = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    dt.load_op  = SDL_GPU_LOADOP_LOAD;
    dt.store_op = SDL_GPU_STOREOP_DONT_CARE;
    rp          = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
    if (r.n_shadow)
    {
        SDL_BindGPUGraphicsPipeline(rp, g->pipe_shadow);
        vb.buffer = g->ibuf;
        vb.offset = off * (uint32_t)sizeof(RInst);
        SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
        SDL_PushGPUVertexUniformData(cmd, 0, &scam, sizeof scam); /* the silhouettes move with their tiles as the art does */
        SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
        SDL_BindGPUFragmentSamplers(rp, 0, ts, 1);
        SDL_DrawGPUPrimitives(rp, 12, r.n_shadow, 0, 0);
    }
    SDL_EndGPURenderPass(rp);

    /*  Pass 3, with the air, the clouds or the occlusion: the frame through
     *  the air, into its own target.  The depth is a distance only when
     *  the camera's depth composes the frame.  The painter's slot is not,
     *  so the original's own composition is left as it is. */
    const int clouded  = v->air || (v->geometry && v->sky_cover > 0.0f);
    const int occluded = ao_wanted(v);
    const int tinted   = v->geometry && g->tinted;
    const int cdiv = cloud_div(), adiv = ao_div();
    int       aired = (clouded || occluded || tinted) && cam.mode[0] < 0.5f && gpu_air_ready(g) == 0 &&
                      ensure_cloud_target(g, (g->tw + cdiv - 1) / cdiv, (g->th + cdiv - 1) / cdiv) == 0 &&
                      ensure_ao_target(g, (g->tw + adiv - 1) / adiv, (g->th + adiv - 1) / adiv) == 0;
    if (aired)
    {
        AirU                         au;
        SDL_GPUTextureSamplerBinding as[7];
        air_for(g, v, &cam, &au);
        au.lowres[1] = clouded ? 1.0f : 0.0f;
        au.lowres[3] = occluded ? 1.0f : 0.0f;
        memset(as, 0, sizeof as);
        as[0].texture = g->color, as[0].sampler = g->nearest;
        as[1].texture = g->depth, as[1].sampler = g->nearest;
        as[2].texture = g->cloud, as[2].sampler = g->wrap;
        as[3].texture = g->smog, as[3].sampler = g->linear;
        as[4].texture = g->sky_field, as[4].sampler = g->wrap;
        as[5].texture = g->clouds, as[5].sampler = g->linear;
        as[6].texture = g->ao, as[6].sampler = g->nearest;
        /*  The clouds alone, marched at one pixel for `cloud_res` of the
         *  frame's a side.  The occlusion, at one pixel for `ao_res`.
         *  Then the frame through the air, which filters both up. */
        if (clouded)
        {
            memset(&ct, 0, sizeof ct);
            ct.texture  = g->clouds;
            ct.load_op  = SDL_GPU_LOADOP_DONT_CARE;
            ct.store_op = SDL_GPU_STOREOP_STORE;
            rp          = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
            SDL_BindGPUGraphicsPipeline(rp, g->pipe_air_clouds);
            SDL_BindGPUFragmentSamplers(rp, 0, as, 5);
            SDL_PushGPUFragmentUniformData(cmd, 0, &au, sizeof au);
            SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
            SDL_EndGPURenderPass(rp);
        }
        if (occluded)
        {
            memset(&ct, 0, sizeof ct);
            ct.texture  = g->ao;
            ct.load_op  = SDL_GPU_LOADOP_DONT_CARE;
            ct.store_op = SDL_GPU_STOREOP_STORE;
            rp          = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
            SDL_BindGPUGraphicsPipeline(rp, g->pipe_air_ao);
            SDL_BindGPUFragmentSamplers(rp, 0, as, 5);
            SDL_PushGPUFragmentUniformData(cmd, 0, &au, sizeof au);
            SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
            SDL_EndGPURenderPass(rp);
        }
        memset(&ct, 0, sizeof ct);
        ct.texture  = g->air;
        ct.load_op  = SDL_GPU_LOADOP_DONT_CARE;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        rp          = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
        SDL_BindGPUGraphicsPipeline(rp, g->pipe_air);
        SDL_BindGPUFragmentSamplers(rp, 0, as, 7);
        SDL_PushGPUFragmentUniformData(cmd, 0, &au, sizeof au);
        SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
        SDL_EndGPURenderPass(rp);
    }

    /*  Pass 4: resolve onto the final target. */
    {
        ResolveU ru;
        memset(&ct, 0, sizeof ct);
        ct.texture  = final;
        ct.load_op  = SDL_GPU_LOADOP_CLEAR;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        rp          = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
        SDL_BindGPUGraphicsPipeline(rp, resolve_pipe);
        ts[0].texture = aired ? g->air : g->color;
        ts[0].sampler = g->linear; /* the fractional zoom filters.  Integer scales fetch texels */
        ts[1].texture = g->shadow;
        ts[2].texture = g->pal;
        ts[2].sampler = g->nearest;
        SDL_BindGPUFragmentSamplers(rp, 0, ts, 3);
        ru.screen[0] = (float)sw;
        ru.screen[1] = (float)sh;
        ru.screen[2] = 1.0f; /* the frame is drawn at its own size */
        ru.screen[3] = 0.0f;
        ru.sky[0]    = (float)g->palette[g->bg_index][0] / 255.0f;
        ru.sky[1]    = (float)g->palette[g->bg_index][1] / 255.0f;
        ru.sky[2]    = (float)g->palette[g->bg_index][2] / 255.0f;
        ru.sky[3]    = 1.0f;
        SDL_PushGPUFragmentUniformData(cmd, 0, &ru, sizeof ru);
        SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
        SDL_EndGPURenderPass(rp);
    }
    return 0;
}

/*  The palette entry the software rasteriser snaps the sky to: the frame
 *  is cleared to that entry's color, as the software paints it.  The
 *  shadow rule reads the same index off the background. */
static void snap_background(RGpu *g, const uint8_t sky[3])
{
    int  pi, best = 0;
    long bestd = -1;
    for (pi = 0; pi < 256; ++pi)
    {
        long dr = (long)g->palette[pi][0] - sky[0];
        long dg = (long)g->palette[pi][1] - sky[1];
        long db = (long)g->palette[pi][2] - sky[2];
        long d  = dr * dr + dg * dg + db * db;
        if (bestd < 0 || d < bestd)
        {
            bestd = d;
            best  = pi;
        }
        if (!d)
            break;
    }
    g->bg_index = (uint8_t)best;
}

static float view_factor(const RGpuView *v)
{
    float f = (float)(v->scale < 1 ? 1 : v->scale);
    if (v->zoom > 0.0f)
        f *= v->zoom;
    return f;
}

int gpu_frame(RGpu *g, const RGpuView *v, const uint8_t sky[3], RGpuOverlay overlay, void *ud)
{
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUTexture       *swap = NULL;
    Uint32                sw = 0, sh = 0;
    float                 scale = view_factor(v);

    if (g->level < 0)
        return -1;
    cmd = SDL_AcquireGPUCommandBuffer(g->dev);
    if (!cmd)
        return -1;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, g->win, &swap, &sw, &sh))
    {
        SDL_SubmitGPUCommandBuffer(cmd);
        return -1;
    }
    if (!swap || !sw || !sh)
    {
        SDL_SubmitGPUCommandBuffer(cmd);
        return 0;
    }
    if (ensure_targets(g, (int32_t)sw, (int32_t)sh) != 0)
    {
        SDL_SubmitGPUCommandBuffer(cmd);
        return -1;
    }
    g->factor = scale;
    /*  The palette snapped to the sky: use the phase-0 colors, which is
     *  what the software does with the palette it was given. */
    snap_background(g, sky);
    if (draw_frame(g, cmd, v, swap, g->pipe_resolve, (int32_t)sw, (int32_t)sh) != 0)
    {
        SDL_SubmitGPUCommandBuffer(cmd);
        return -1;
    }
    if (overlay)
        overlay(ud, cmd, swap, sw, sh);
    SDL_SubmitGPUCommandBuffer(cmd);
    return 0;
}

int gpu_readback(RGpu *g, const RGpuView *v, const uint8_t sky[3], int32_t w, int32_t h, int32_t scale, RImage *out, RGpuOverlay overlay, void *ud)
{
    SDL_GPUCommandBuffer      *cmd;
    SDL_GPUCopyPass           *cp;
    SDL_GPUFence              *fence;
    SDL_GPUTextureRegion       src;
    SDL_GPUTextureTransferInfo dst;
    uint32_t                   bytes = (uint32_t)w * (uint32_t)h * 4u;
    const uint8_t             *map;
    size_t                     k, npx = (size_t)w * (size_t)h;

    memset(out, 0, sizeof *out);
    if (scale < 1)
        scale = 1;
    if (g->level < 0 || w < 1 || h < 1)
        return -1;
    if (ensure_targets(g, w, h) != 0)
        return -1;
    g->factor = (float)scale * (v->zoom > 0.0f ? v->zoom : 1.0f);
    if (ensure_offscreen(g, w, h) != 0)
        return -1;
    if (bytes > g->dtb_cap)
    {
        if (g->dtb)
            SDL_ReleaseGPUTransferBuffer(g->dev, g->dtb);
        g->dtb     = make_transfer(g, 1, bytes);
        g->dtb_cap = g->dtb ? bytes : 0;
        if (!g->dtb)
            return -1;
    }
    snap_background(g, sky);
    cmd = SDL_AcquireGPUCommandBuffer(g->dev);
    if (!cmd)
        return -1;
    if (draw_frame(g, cmd, v, g->offscreen, g->pipe_resolve_off, w, h) != 0)
    {
        SDL_SubmitGPUCommandBuffer(cmd);
        return -1;
    }
    if (overlay)
        overlay(ud, cmd, g->offscreen, (uint32_t)w, (uint32_t)h);
    memset(&src, 0, sizeof src);
    memset(&dst, 0, sizeof dst);
    src.texture         = g->offscreen;
    src.w               = (Uint32)w;
    src.h               = (Uint32)h;
    src.d               = 1;
    dst.transfer_buffer = g->dtb;
    dst.pixels_per_row  = (Uint32)w;
    dst.rows_per_layer  = (Uint32)h;
    cp                  = SDL_BeginGPUCopyPass(cmd);
    SDL_DownloadFromGPUTexture(cp, &src, &dst);
    SDL_EndGPUCopyPass(cp);
    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (!fence)
        return -1;
    SDL_WaitForGPUFences(g->dev, true, &fence, 1);
    SDL_ReleaseGPUFence(g->dev, fence);

    map = (const uint8_t *)SDL_MapGPUTransferBuffer(g->dev, g->dtb, false);
    if (!map)
        return -1;
    out->w    = w;
    out->h    = h;
    out->rgb  = (uint8_t *)malloc(npx * 3u);
    out->idx  = (uint8_t *)calloc(npx, 1);
    out->prov = (uint16_t *)calloc(npx, sizeof(uint16_t));
    if (out->rgb && out->idx && out->prov)
        for (k = 0; k < npx; ++k)
        {
            out->rgb[k * 3u]      = map[k * 4u];
            out->rgb[k * 3u + 1u] = map[k * 4u + 1u];
            out->rgb[k * 3u + 2u] = map[k * 4u + 2u];
        }
    SDL_UnmapGPUTransferBuffer(g->dev, g->dtb);
    if (!out->rgb || !out->idx || !out->prov)
    {
        image_free(out);
        return -1;
    }
    return 0;
}
