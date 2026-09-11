/*  gpu/frame.c: drawing one frame with the device gpu.c made.  Culling
 *  and ordering the instances, the camera uniforms, the passes, and the
 *  two ways out: to the window, and to a readback buffer. */
#include "materials.h" /* GENERATED: the material numbers a script may add to */
#include "script.h"
#include "dump.h"
#include "gpu/internal.h"
#include "log.h"
#include "mesh/mesh.h"
#include "opt.h"
#include "project.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "shaders.h"

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
        float c   = (float)((k % (R_MAP / MESH_CHUNK)) * MESH_CHUNK + ((i & 1) ? MESH_CHUNK : 0));
        float r   = (float)((k / (R_MAP / MESH_CHUNK)) * MESH_CHUNK + ((i & 2) ? MESH_CHUNK : 0));
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

/*  The fragment uniform of every sprite pipeline.  Sprite.frag and
 *  shadow.frag declare only the first member.  Sprite_water.frag reads
 *  all three.  One struct is pushed for all of them. */
typedef struct
{
    int32_t p[4]; /* transparent index, 0, 0, 0     */
    float   f[4]; /* time, tw/2, th/2, 0            */
    float   sun[4];
} FragU;

typedef struct
{
    float screen[4];
    float sky[4];
} ResolveU;

typedef struct
{
    float sun[4];
    float params[4];
    float flags[4]; /* x: the pass (0 base, 1 margins, 2 markings).  Y: the margins on.  The rest spare */
    /*  The materials a script declared: a color and a roughness each, in
     *  the order arc.mat.define gave them.  So material MAT_SCRIPT_BASE
     *  + k is shaded from mats[k].  A built-in material has a branch of
     *  its own instead and reads none of this. */
    float mats[MAT_SCRIPT_MAX][4];
} LightU;

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

/*  --no-shadow 1 draws no silhouettes.  So a frame at the snap and a
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
    float th   = v->angle * ARC_DEG2RAD, ca = cosf(th), sa = sinf(th);
    float left = v->angle - 90.0f * (float)(v->sweep_quarter & 3);
    float pc, pr, c = (float)(R_MAP / 2);
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
    return (tx + ty + front + 1.05f) * cp + floorf(fmodf(in->misc[0], 256.0f) / 4.0f) * ARC_PITCH_COS0 * sp +
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
                 *  the settle.  --no-shadow drops them all. */
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
            g->vis[n++] = *in;
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
    u->view[0]           = (float)v->scroll_x;
    u->view[1]           = (float)v->scroll_y;
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
    u->alt[1]  = 800.0f; /* the depth range: the diagonal units of distance the buffer spans, from 300 before the map's near corner */
    u->alt[2]  = cosf(v->angle * ARC_DEG2RAD);
    u->alt[3]  = sinf(v->angle * ARC_DEG2RAD);
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
    u->mode[1] = (float)(2 * R_MAP * R_MAP + 2); /* the painter's slot divisor */
    u->mode[2] = u->mode[3] = 0.0f; /* the mesh turns about the pivot itself: no translation */
}

/*  Do any of a kind of slot hold vertices?  The networks are the odd
 *  opaque slots, the water the last sixteen. */
static int slots_held(const RGpu *g, int base, int stride)
{
    int k;
    for (k = 0; k < MESH_CHUNKS; ++k)
        if (g->slot[base + k * stride].count)
            return 1;
    return 0;
}

/*  The terrain mesh, in the depth pass. */
static void draw_mesh(RGpu *g, SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *rp, const RGpuView *v, const CamU *cam, const RAtlasLevel *l)
{
    SDL_GPUBufferBinding vb;
    if (v->geometry && g->mesh_n && g->pipe_mesh)
    {
        LightU lu;
        (void)l;
        memcpy(lu.sun, g->sun, sizeof lu.sun);
        memcpy(lu.params, g->params, sizeof lu.params);
        lu.params[1] = v->time;
        /*  The grid's half-width in canvas pixels: half a pixel, or in the
         *  underground view half a DEVICE pixel, a hairline at any scale. */
        lu.params[2] = v->underground ? 0.5f : (v->grid ? 0.5f * g->factor : 0.0f);
        lu.params[3] = v->underground ? 1.0f : (v->plan ? -1.0f : 0.0f); /* the map view is -1: the tints' switch */
        lu.flags[0]  = 0.0f;                                             /* the pass: 0 the base draw, 1 margins, 2 markings */
        lu.flags[1]  = v->margins ? 1.0f : 0.0f;
        lu.flags[2] = lu.flags[3] = 0.0f;
        /*  The script's own materials, as it declared them.  Anything it
         *  did not declare is left black and matt, which is what a
         *  material nobody defined should look like. */
        {
            const float *m = NULL;
            int          n = script_materials(&m);
            memset(lu.mats, 0, sizeof lu.mats);
            if (m && n > 0)
                memcpy(lu.mats, m, sizeof lu.mats[0] * (size_t)(n < MAT_SCRIPT_MAX ? n : MAT_SCRIPT_MAX));
        }
        SDL_GPUTextureSamplerBinding fb;
        fb.texture = g->shore;
        fb.sampler = g->linear;
        SDL_BindGPUGraphicsPipeline(rp, g->pipe_mesh);
        SDL_BindGPUFragmentSamplers(rp, 0, &fb, 1);
        SDL_PushGPUVertexUniformData(cmd, 0, cam, sizeof *cam);
        SDL_PushGPUFragmentUniformData(cmd, 0, &lu, sizeof lu);
        vb.buffer = g->mbuf;
        vb.offset = 0;
        SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
        /*  By chunk, when the mesh is bucketed: each chunk in view drawn
         *  as its terrain range then its network range.  A chunk outside
         *  the view, its eight corners, ground and highest ground, all
         *  past one edge in the shader's own projection, is skipped.
         *  This is what keeps a world 100x this one drawable.
         *  Unbucketed, the whole list. */
        {
            int k, drawn = 0;
            for (k = 0; k < MESH_CHUNKS; ++k)
            {
                if (!chunk_visible(cam, k))
                    continue;
                ++drawn;
                if (g->slot[2 * k].count)
                    SDL_DrawGPUPrimitives(rp, g->slot[2 * k].count, 1, g->slot[2 * k].first, 0);
                if (g->slot[2 * k + 1].count)
                    SDL_DrawGPUPrimitives(rp, g->slot[2 * k + 1].count, 1, g->slot[2 * k + 1].first, 0);
            }
            if (g_dev.times && !s_chunks_said)
            {
                s_chunks_said = 1;
                dumpf("time  frame  chunks drawn %d of %d\n", drawn, MESH_CHUNKS);
            }
        }
        /*  The passes.  The network range of the opaque list holds
         *  lines, slabs, boxes and furniture.  Each is drawn again under
         *  a pass number in flags.x, the same vertices each time.  Pass
         *  1 is the margin pass's (net/margin.c): a line strip's outer
         *  fifth, discarded by the base draw, painted now.  Pass 2 the
         *  marking pass's (marking.c): the lines alone over the fill, at
         *  equal depth.  A pass off is a draw skipped, no rebuild. */
        if (slots_held(g, 1, 2))
        {
            int pass;
            for (pass = 1; pass <= 2; ++pass)
            {
                if (pass == 1 ? !v->margins : !v->markings)
                    continue;
                lu.flags[0] = (float)pass;
                SDL_PushGPUFragmentUniformData(cmd, 0, &lu, sizeof lu);
                {
                    int k;
                    for (k = 0; k < MESH_CHUNKS; ++k)
                        if (g->slot[2 * k + 1].count && chunk_visible(cam, k))
                            SDL_DrawGPUPrimitives(rp, g->slot[2 * k + 1].count, 1, g->slot[2 * k + 1].first, 0);
                }
            }
            lu.flags[0] = 0.0f;
        }
        if (g->movers_n && g->mvbuf)
        {
            SDL_GPUBufferBinding mb;
            memset(&mb, 0, sizeof mb);
            mb.buffer = g->mvbuf;
            SDL_BindGPUVertexBuffers(rp, 0, &mb, 1);
            SDL_DrawGPUPrimitives(rp, g->movers_n, 1, 0, 0);
            SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
        }
        /*  The water, blended: every chunk's slot in order, whole: the
         *  order its faces were sorted into, as the one list was drawn. */
        if (slots_held(g, 2 * MESH_CHUNKS, 1))
        {
            int k;
            SDL_BindGPUGraphicsPipeline(rp, g->pipe_mesh_blend);
            SDL_BindGPUFragmentSamplers(rp, 0, &fb, 1);
            SDL_PushGPUVertexUniformData(cmd, 0, cam, sizeof *cam);
            SDL_PushGPUFragmentUniformData(cmd, 0, &lu, sizeof lu);
            for (k = 0; k < MESH_CHUNKS; ++k)
                if (g->slot[2 * MESH_CHUNKS + k].count)
                    SDL_DrawGPUPrimitives(rp, g->slot[2 * MESH_CHUNKS + k].count, 1, g->slot[2 * MESH_CHUNKS + k].first, 0);
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
    fu.p[1] = v->geometry ? 1 : 0; /* the water sprites drop their rim  */
    /*  and shade their water: underground there is no sky to reflect. */
    fu.p[2]   = (v->geometry && !v->underground) ? 1 : 0;
    fu.f[0]   = v->time;
    fu.f[1]   = arc_half_w((float)l->tile_w);
    fu.f[2]   = arc_half_h((float)l->tile_h);
    fu.sun[0] = g->sun[0];
    fu.sun[1] = g->sun[1];
    fu.sun[2] = g->sun[2];

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

    draw_mesh(g, cmd, rp, v, &cam, l);

    SDL_PushGPUVertexUniformData(cmd, 0, &scam, sizeof scam);
    SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
    SDL_BindGPUFragmentSamplers(rp, 0, ts, 2);
    off = 0;
    if (r.n_terrain)
    {
        SDL_BindGPUGraphicsPipeline(rp, g->pipe_terrain);
        vb.buffer = g->ibuf;
        vb.offset = off * (uint32_t)sizeof(RInst);
        SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
        SDL_DrawGPUPrimitives(rp, 12, r.n_terrain, 0, 0);
    }
    off += r.n_terrain;
    if (r.n_water)
    {
        ts[2].texture = g->shore;
        ts[2].sampler = g->linear;
        SDL_BindGPUGraphicsPipeline(rp, g->pipe_water);
        SDL_BindGPUFragmentSamplers(rp, 0, ts, 3);
        vb.buffer = g->ibuf;
        vb.offset = off * (uint32_t)sizeof(RInst);
        SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
        SDL_DrawGPUPrimitives(rp, 12, r.n_water, 0, 0);
        SDL_BindGPUFragmentSamplers(rp, 0, ts, 2);
    }
    off += r.n_water;
    if (r.n_sprite)
    {
        SDL_BindGPUGraphicsPipeline(rp, v->plain_sweep ? g->pipe_terrain : cam.mode[0] > 0.5f ? g->pipe_sprite : g->pipe_sprite_depth);
        vb.buffer = g->ibuf;
        vb.offset = off * (uint32_t)sizeof(RInst);
        SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
        SDL_DrawGPUPrimitives(rp, 12, r.n_sprite, 0, 0);
    }
    off += r.n_sprite;
    /*  per frame, so it stays behind its own switch rather than
     *  riding on --verbose */
    if (g_dev.gpu_debug)
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

    /*  Pass 3: resolve onto the final target. */
    {
        ResolveU ru;
        memset(&ct, 0, sizeof ct);
        ct.texture  = final;
        ct.load_op  = SDL_GPU_LOADOP_CLEAR;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        rp          = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
        SDL_BindGPUGraphicsPipeline(rp, resolve_pipe);
        ts[0].texture = g->color;
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

float view_factor(const RGpuView *v)
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
