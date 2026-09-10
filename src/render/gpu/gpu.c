/*  gpu.c: the device: making it, and putting data on it.  What the
 *  renderer asks of it is in gpu.h.  Drawing with it is gpu/frame.c. */
#include "gpu/internal.h"
#include "log.h"
#include "opt.h"
#include "script.h"
#include "project.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "dump.h"
#include "shaders.h"

/* ---- shaders and pipelines ------------------------------------------- */

static SDL_GPUShader *load_shader(RGpu *g, const char *name)
{
    const RShaderSrc       *s = shader_find(name);
    SDL_GPUShaderCreateInfo ci;

    if (!s)
        return NULL;
    memset(&ci, 0, sizeof ci);
    if (g->fmt & SDL_GPU_SHADERFORMAT_MSL)
    {
        ci.code       = s->msl;
        ci.code_size  = s->msl_len;
        ci.format     = SDL_GPU_SHADERFORMAT_MSL;
        ci.entrypoint = "main0";
    }
    else
    {
        ci.code       = s->spv;
        ci.code_size  = s->spv_len;
        ci.format     = SDL_GPU_SHADERFORMAT_SPIRV;
        ci.entrypoint = "main";
    }
    ci.stage               = s->stage ? SDL_GPU_SHADERSTAGE_FRAGMENT
                                      : SDL_GPU_SHADERSTAGE_VERTEX;
    ci.num_samplers        = (Uint32)s->num_samplers;
    ci.num_uniform_buffers = (Uint32)s->num_uniform_buffers;
    return SDL_CreateGPUShader(g->dev, &ci);
}

enum
{
    L_NONE = 0,
    L_INSTANCE,
    L_MESH
};

static SDL_GPUGraphicsPipeline *make_pipe(RGpu *g, const char *vs_name, const char *fs_name, SDL_GPUTextureFormat target, int depth_test, int depth_write, int layout, int blend)
{
    SDL_GPUShader                    *vs = load_shader(g, vs_name);
    SDL_GPUShader                    *fs = load_shader(g, fs_name);
    SDL_GPUGraphicsPipelineCreateInfo pi;
    SDL_GPUVertexBufferDescription    vb;
    SDL_GPUVertexAttribute            va[4];
    SDL_GPUColorTargetDescription     ct;
    SDL_GPUGraphicsPipeline          *p;

    if (!vs || !fs)
    {
        if (vs)
            SDL_ReleaseGPUShader(g->dev, vs);
        if (fs)
            SDL_ReleaseGPUShader(g->dev, fs);
        return NULL;
    }
    memset(&pi, 0, sizeof pi);
    memset(&vb, 0, sizeof vb);
    memset(va, 0, sizeof va);
    memset(&ct, 0, sizeof ct);

    pi.vertex_shader   = vs;
    pi.fragment_shader = fs;
    pi.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    if (layout == L_INSTANCE)
    {
        vb.slot                                          = 0;
        vb.pitch                                         = sizeof(RInst);
        vb.input_rate                                    = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
        va[0].location                                   = 0;
        va[0].buffer_slot                                = 0;
        va[0].format                                     = SDL_GPU_VERTEXELEMENTFORMAT_INT4;
        va[0].offset                                     = 0;
        va[1].location                                   = 1;
        va[1].buffer_slot                                = 0;
        va[1].format                                     = SDL_GPU_VERTEXELEMENTFORMAT_INT4;
        va[1].offset                                     = 16;
        va[2].location                                   = 2;
        va[2].buffer_slot                                = 0;
        va[2].format                                     = SDL_GPU_VERTEXELEMENTFORMAT_INT4;
        va[2].offset                                     = 32;
        va[3].location                                   = 3;
        va[3].buffer_slot                                = 0;
        va[3].format                                     = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        va[3].offset                                     = 48;
        pi.vertex_input_state.vertex_buffer_descriptions = &vb;
        pi.vertex_input_state.num_vertex_buffers         = 1;
        pi.vertex_input_state.vertex_attributes          = va;
        pi.vertex_input_state.num_vertex_attributes      = 4;
    }
    else if (layout == L_MESH)
    {
        vb.slot                                          = 0;
        vb.pitch                                         = sizeof(RMeshVert);
        vb.input_rate                                    = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        va[0].location                                   = 0;
        va[0].buffer_slot                                = 0;
        va[0].format                                     = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        va[0].offset                                     = 0;
        va[1].location                                   = 1;
        va[1].buffer_slot                                = 0;
        va[1].format                                     = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        va[1].offset                                     = 16;
        va[2].location                                   = 2;
        va[2].buffer_slot                                = 0;
        va[2].format                                     = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        va[2].offset                                     = 32;
        pi.vertex_input_state.vertex_buffer_descriptions = &vb;
        pi.vertex_input_state.num_vertex_buffers         = 1;
        pi.vertex_input_state.vertex_attributes          = va;
        pi.vertex_input_state.num_vertex_attributes      = 3;
    }

    pi.rasterizer_state.fill_mode         = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.cull_mode         = SDL_GPU_CULLMODE_NONE;
    pi.rasterizer_state.front_face        = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pi.rasterizer_state.enable_depth_clip = true;
    pi.multisample_state.sample_count     = SDL_GPU_SAMPLECOUNT_1;

    pi.depth_stencil_state.enable_depth_test  = depth_test ? true : false;
    pi.depth_stencil_state.enable_depth_write = depth_write ? true : false;
    pi.depth_stencil_state.compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;

    ct.format                   = target;
    ct.blend_state.enable_blend = blend ? true : false;
    if (blend)
    {
        /*  Over the frame by the source's alpha.  The target's alpha is
         *  the palette index the resolve pass reads, so it is kept. */
        ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ct.blend_state.color_blend_op        = SDL_GPU_BLENDOP_ADD;
        ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ct.blend_state.alpha_blend_op        = SDL_GPU_BLENDOP_ADD;
    }
    pi.target_info.color_target_descriptions = &ct;
    pi.target_info.num_color_targets         = 1;
    if (depth_test)
    {
        pi.target_info.has_depth_stencil_target = true;
        pi.target_info.depth_stencil_format     = g->depth_fmt;
    }

    p = SDL_CreateGPUGraphicsPipeline(g->dev, &pi);
    SDL_ReleaseGPUShader(g->dev, vs);
    SDL_ReleaseGPUShader(g->dev, fs);
    return p;
}

/* ---- resources ------------------------------------------------------- */

static SDL_GPUTexture *make_texture(RGpu *g, SDL_GPUTextureFormat fmt, Uint32 usage, int32_t w, int32_t h)
{
    SDL_GPUTextureCreateInfo ti;
    memset(&ti, 0, sizeof ti);
    ti.type                 = SDL_GPU_TEXTURETYPE_2D;
    ti.format               = fmt;
    ti.usage                = usage;
    ti.width                = (Uint32)w;
    ti.height               = (Uint32)h;
    ti.layer_count_or_depth = 1;
    ti.num_levels           = 1;
    ti.sample_count         = SDL_GPU_SAMPLECOUNT_1;
    return SDL_CreateGPUTexture(g->dev, &ti);
}

static SDL_GPUBuffer *make_buffer(RGpu *g, Uint32 usage, uint32_t bytes)
{
    SDL_GPUBufferCreateInfo bi;
    memset(&bi, 0, sizeof bi);
    bi.usage = usage;
    bi.size  = bytes;
    return SDL_CreateGPUBuffer(g->dev, &bi);
}

SDL_GPUTransferBuffer *make_transfer(RGpu *g, int download, uint32_t bytes)
{
    SDL_GPUTransferBufferCreateInfo ti;
    memset(&ti, 0, sizeof ti);
    ti.usage = download ? SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD
                        : SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size  = bytes;
    return SDL_CreateGPUTransferBuffer(g->dev, &ti);
}

/*  Upload a whole 2D texture from host memory, synchronously. */
static int upload_texture(RGpu *g, SDL_GPUTexture *tex, const void *px, int32_t w, int32_t h, int32_t bpp)
{
    uint32_t                   bytes = (uint32_t)w * (uint32_t)h * (uint32_t)bpp;
    SDL_GPUTransferBuffer     *tb    = make_transfer(g, 0, bytes);
    SDL_GPUCommandBuffer      *cmd;
    SDL_GPUCopyPass           *cp;
    SDL_GPUTextureTransferInfo src;
    SDL_GPUTextureRegion       dst;
    void                      *map;

    if (!tb)
        return -1;
    map = SDL_MapGPUTransferBuffer(g->dev, tb, false);
    if (!map)
    {
        SDL_ReleaseGPUTransferBuffer(g->dev, tb);
        return -1;
    }
    memcpy(map, px, bytes);
    SDL_UnmapGPUTransferBuffer(g->dev, tb);

    memset(&src, 0, sizeof src);
    memset(&dst, 0, sizeof dst);
    src.transfer_buffer = tb;
    src.pixels_per_row  = (Uint32)w;
    src.rows_per_layer  = (Uint32)h;
    dst.texture         = tex;
    dst.w               = (Uint32)w;
    dst.h               = (Uint32)h;
    dst.d               = 1;

    cmd = SDL_AcquireGPUCommandBuffer(g->dev);
    if (!cmd)
    {
        SDL_ReleaseGPUTransferBuffer(g->dev, tb);
        return -1;
    }
    cp = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_WaitForGPUIdle(g->dev);
    SDL_ReleaseGPUTransferBuffer(g->dev, tb);
    return 0;
}

static void release_targets(RGpu *g)
{
    if (g->color)
        SDL_ReleaseGPUTexture(g->dev, g->color);
    if (g->shadow)
        SDL_ReleaseGPUTexture(g->dev, g->shadow);
    if (g->depth)
        SDL_ReleaseGPUTexture(g->dev, g->depth);
    if (g->offscreen)
        SDL_ReleaseGPUTexture(g->dev, g->offscreen);
    g->color = g->shadow = g->depth = g->offscreen = NULL;
    g->tw = g->th = 0;
}

int ensure_targets(RGpu *g, int32_t w, int32_t h)
{
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    if (g->color && g->tw == w && g->th == h)
        return 0;
    release_targets(g);
    g->color  = make_texture(g, g->color_fmt, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h);
    g->shadow = make_texture(g, SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h);
    g->depth  = make_texture(g, g->depth_fmt, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, w, h);
    if (!g->color || !g->shadow || !g->depth)
    {
        release_targets(g);
        return -1;
    }
    g->tw = w;
    g->th = h;
    return 0;
}

/*  The readback's target, sized on its own: it is the resolved frame,
 *  the canvas times the pixel scale, not the canvas. */
int ensure_offscreen(RGpu *g, int32_t w, int32_t h)
{
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    if (g->offscreen && g->ow == w && g->oh == h)
        return 0;
    if (g->offscreen)
        SDL_ReleaseGPUTexture(g->dev, g->offscreen);
    g->offscreen = make_texture(g, g->color_fmt, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h);
    if (!g->offscreen)
        return -1;
    g->ow = w;
    g->oh = h;
    return 0;
}

int ensure_instances(RGpu *g, uint32_t n)
{
    uint32_t cap;
    if (n <= g->ibuf_cap)
        return 0;
    cap = g->ibuf_cap ? g->ibuf_cap : 16384u;
    while (cap < n)
        cap *= 2u;
    if (g->ibuf)
        SDL_ReleaseGPUBuffer(g->dev, g->ibuf);
    if (g->itb)
        SDL_ReleaseGPUTransferBuffer(g->dev, g->itb);
    g->ibuf = make_buffer(g, SDL_GPU_BUFFERUSAGE_VERTEX, cap * (uint32_t)sizeof(RInst));
    g->itb  = make_transfer(g, 0, cap * (uint32_t)sizeof(RInst));
    if (!g->ibuf || !g->itb)
        return -1;
    g->ibuf_cap = cap;
    return 0;
}

/* ---- creation --------------------------------------------------------- */

RGpu *gpu_create(SDL_Window *win, const RAtlas *a, char *err, size_t err_len)
{
    RGpu   *g = (RGpu *)calloc(1, sizeof *g);
    int32_t i;

    if (!g)
        return NULL;
    g->win = win;
    g->dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV |
                                     SDL_GPU_SHADERFORMAT_MSL,
                                 false,
                                 NULL);
    if (!g->dev)
    {
        snprintf(err, err_len, "no GPU device: %s", SDL_GetError());
        free(g);
        return NULL;
    }
    if (!SDL_ClaimWindowForGPUDevice(g->dev, win))
    {
        snprintf(err, err_len, "cannot claim the window: %s", SDL_GetError());
        SDL_DestroyGPUDevice(g->dev);
        free(g);
        return NULL;
    }
    g->fmt       = SDL_GetGPUShaderFormats(g->dev);
    g->swap_fmt  = SDL_GetGPUSwapchainTextureFormat(g->dev, win);
    g->color_fmt = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    g->depth_fmt = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    if (!SDL_GPUTextureSupportsFormat(g->dev, g->depth_fmt, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
        g->depth_fmt = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    R_DBG("gpu", "depth %s", g->depth_fmt == SDL_GPU_TEXTUREFORMAT_D32_FLOAT ? "D32_FLOAT" : "D16_UNORM");

    /*  Pipelines.
     *
     *      The terrain writes depth.
     *      The sprites test it.
     *      The shadows test it into the mask.
     *      The water is terrain with its own fragment shader.
     *      The mesh is geometry.
     *      The resolve needs none. */
    g->pipe_terrain = make_pipe(g, "sprite.vert", "sprite.frag", g->color_fmt, 1, 1, L_INSTANCE, 0);
    g->pipe_sprite  = make_pipe(g, "sprite.vert", "sprite.frag", g->color_fmt, 1, 0, L_INSTANCE, 0); /* the painter: the art tests, the ground wrote      */
    g->pipe_sprite_depth = make_pipe(g, "sprite.vert", "sprite.frag", g->color_fmt, 1, 1, L_INSTANCE, 0); /* the camera's depth: the art writes it, it stands in the world */
    g->pipe_shadow  = make_pipe(g, "sprite.vert", "shadow.frag", SDL_GPU_TEXTUREFORMAT_R8_UNORM, 1, 0, L_INSTANCE, 0);
    g->pipe_water   = make_pipe(g, "sprite.vert", "sprite_water.frag", g->color_fmt, 1, 1, L_INSTANCE, 0);
    g->pipe_mesh    = make_pipe(g, "terrain.vert", "terrain.frag", g->color_fmt, 1, 1, L_MESH, 0);
    /*  The water column faces of the map edge's cut: blended over the
     *  seabed behind them, tested against depth but not writing it. */
    g->pipe_mesh_blend  = make_pipe(g, "terrain.vert", "terrain.frag", g->color_fmt, 1, 0, L_MESH, 1);
    g->pipe_resolve     = make_pipe(g, "resolve.vert", "resolve.frag", g->swap_fmt, 0, 0, L_NONE, 0);
    g->pipe_resolve_off = make_pipe(g, "resolve.vert", "resolve.frag", g->color_fmt, 0, 0, L_NONE, 0);
    if (!g->pipe_terrain || !g->pipe_sprite || !g->pipe_sprite_depth || !g->pipe_shadow ||
        !g->pipe_water || !g->pipe_mesh || !g->pipe_resolve ||
        !g->pipe_resolve_off)
    {
        snprintf(err, err_len, "pipeline creation failed: %s", SDL_GetError());
        gpu_destroy(g);
        return NULL;
    }

    {
        SDL_GPUSamplerCreateInfo si;
        memset(&si, 0, sizeof si);
        si.min_filter     = SDL_GPU_FILTER_NEAREST;
        si.mag_filter     = SDL_GPU_FILTER_NEAREST;
        si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        g->nearest        = SDL_CreateGPUSampler(g->dev, &si);
        si.min_filter     = SDL_GPU_FILTER_LINEAR;
        si.mag_filter     = SDL_GPU_FILTER_LINEAR;
        g->linear         = SDL_CreateGPUSampler(g->dev, &si);
    }

    /*  The atlases: palette indices as they are, one R8 texture per art
     *  set.  Nothing is resolved to color on the CPU. */
    for (i = 0; i < a->n_levels; ++i)
    {
        const RAtlasLevel *l = &a->level[i];
        g->atlas[i]          = make_texture(g, SDL_GPU_TEXTUREFORMAT_R8_UINT, SDL_GPU_TEXTUREUSAGE_SAMPLER, l->w, l->h);
        if (!g->atlas[i] ||
            upload_texture(g, g->atlas[i], l->indices, l->w, l->h, 1) != 0)
        {
            snprintf(err, err_len, "atlas upload failed: %s", SDL_GetError());
            gpu_destroy(g);
            return NULL;
        }
        g->atlas_transparent[i] = l->transparent;
        g->atlas_zoom[i]        = l->zoom;
    }
    g->n_atlas = a->n_levels;

    g->pal   = make_texture(g, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, 256, 1);
    g->ptb   = make_transfer(g, 0, 256u * 4u);
    g->shore = make_texture(g, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, R_MAP, R_MAP);
    if (!g->pal || !g->ptb || !g->nearest || !g->linear || !g->shore)
    {
        snprintf(err, err_len, "resource creation failed: %s", SDL_GetError());
        gpu_destroy(g);
        return NULL;
    }
    gpu_set_palette(g, a);
    /*  The sun, fitted to the slope sprites.  Of the four plain slopes
     *  at 32 px the one facing south (code 1) is the brightest.  Its
     *  mean luminance is 135, then east 88, north 84 and west 71.  So
     *  the light comes from the south-east, over the viewer's shoulder. */
    gpu_set_light(g, 0.35f, 0.85f, 0.9f, 0.5f, 0.6f);
    return g;
}

void gpu_destroy(RGpu *g)
{
    int32_t i;
    if (!g)
        return;
    if (g->dev)
    {
        SDL_WaitForGPUIdle(g->dev);
        release_targets(g);
        for (i = 0; i < g->n_atlas; ++i)
            if (g->atlas[i])
                SDL_ReleaseGPUTexture(g->dev, g->atlas[i]);
        if (g->pal)
            SDL_ReleaseGPUTexture(g->dev, g->pal);
        if (g->shore)
            SDL_ReleaseGPUTexture(g->dev, g->shore);
        if (g->nearest)
            SDL_ReleaseGPUSampler(g->dev, g->nearest);
        if (g->linear)
            SDL_ReleaseGPUSampler(g->dev, g->linear);
        if (g->ibuf)
            SDL_ReleaseGPUBuffer(g->dev, g->ibuf);
        if (g->itb)
            SDL_ReleaseGPUTransferBuffer(g->dev, g->itb);
        if (g->mbuf)
            SDL_ReleaseGPUBuffer(g->dev, g->mbuf);
        if (g->mvbuf)
            SDL_ReleaseGPUBuffer(g->dev, g->mvbuf);
        if (g->ptb)
            SDL_ReleaseGPUTransferBuffer(g->dev, g->ptb);
        if (g->dtb)
            SDL_ReleaseGPUTransferBuffer(g->dev, g->dtb);
        if (g->pipe_terrain)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_terrain);
        if (g->pipe_sprite)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_sprite);
        if (g->pipe_sprite_depth)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_sprite_depth);
        if (g->pipe_shadow)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_shadow);
        if (g->pipe_mesh)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_mesh);
        if (g->pipe_mesh_blend)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_mesh_blend);
        if (g->pipe_water)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_water);
        if (g->pipe_resolve)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_resolve);
        if (g->pipe_resolve_off)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_resolve_off);
        SDL_ReleaseWindowFromGPUDevice(g->dev, g->win);
        SDL_DestroyGPUDevice(g->dev);
    }
    free(g->inst);
    free(g->kind);
    free(g->vis);
    free(g);
}

/*  A palette entry is water when it is strongly blue.  In the water and
 *  shore sprites those are 79, 130, 184..188 and 192..195.  The beach is
 *  104 and 106.  The animated runs cycle blues among blues, so the mark
 *  survives the animation.  Only the water sprites' pipeline reads it. */
static int is_water_colour(const uint8_t *rgb)
{
    int r = rgb[0], gr = rgb[1], b = rgb[2];
    return b >= 100 && b > r + 80 && b > gr + 60;
}

void gpu_set_palette(RGpu *g, const RAtlas *a)
{
    int i;
    for (i = 0; i < 256; ++i)
    {
        g->palette[i][0] = a->palette[i][0];
        g->palette[i][1] = a->palette[i][1];
        g->palette[i][2] = a->palette[i][2];
        g->palette[i][3] = is_water_colour(a->palette[i]) ? 0 : 255;
    }
    g->pal_dirty = 1;
}

int gpu_set_shore(RGpu *g, const uint8_t *field, int32_t n)
{
    if (n != R_MAP)
        return -1;
    return upload_texture(g, g->shore, field, n, n, 4);
}

void gpu_set_light(RGpu *g, float x, float y, float z, float ambient, float diffuse)
{
    g->sun[0]    = x;
    g->sun[1]    = y;
    g->sun[2]    = z;
    g->sun[3]    = ambient;
    g->params[0] = diffuse;
    g->params[1] = g->params[2] = g->params[3] = 0.0f;
}

/*  Terrain art: the land shapes 256..269 (269 is also the map-edge dirt
 *  column), which the mesh replaces.  And the water art 270..290.  Open
 *  water, the shore shapes, the water column 284 and the channel pieces
 *  285..290 that XTER 0x40..0x45 draw.  Which the water shader paints
 *  inside.  Everything else the terrain pass paints: lot tints,
 *  data-view tints, the power markers: stays a sprite whatever is
 *  switched on. */
static uint8_t terrain_kind(int32_t tile)
{
    if (tile >= 305 && tile <= 318)
        return K_UG_LATTICE; /* $164A2: the wireframe lattice of an empty tile,
                              *  0x131 + the terrain code (0x13E is the last) */
    if (tile == 284)
        return K_WATER_COL;
    if (tile >= 270 && tile <= 290)
        return K_WATER_ART;
    if (tile >= 256 && tile <= 269)
        return K_LAND_ART;
    return K_TERRAIN;
}

int gpu_set_ops(RGpu *g, const ROpList *ops, const RSweep *sw)
{
    const RAtlasLevel *l = sw->level;
    size_t             k;
    int32_t            i;

    g->sw    = *sw;
    g->level = -1;
    for (i = 0; i < g->n_atlas; ++i)
        if (g->atlas_zoom[i] == l->zoom)
            g->level = i;
    if (g->level < 0)
        return -1;
    g->transparent = g->atlas_transparent[g->level];

    if (ops->n > g->inst_cap)
    {
        size_t   cap = ops->n + 1024u;
        RInst   *ni  = (RInst *)realloc(g->inst, cap * sizeof *ni);
        uint8_t *nk  = (uint8_t *)realloc(g->kind, cap);
        if (!ni || !nk)
        {
            free(ni ? ni : g->inst);
            free(nk ? nk : g->kind);
            g->inst     = NULL;
            g->kind     = NULL;
            g->inst_cap = 0;
            return -1;
        }
        g->inst     = ni;
        g->kind     = nk;
        g->inst_cap = (uint32_t)cap;
    }
    g->n_inst = 0;
    /*  Which tiles the MESH draws the art of.  Read once for the sweep,
     *  since a table lookup by name for every sprite of every frame is
     *  the same answer over and over. */
    const uint8_t *meshed = script_bytes("meshed_tiles");
    for (k = 0; k < ops->n; ++k)
    {
        const ROp   *op = &ops->v[k];
        const RTile *t  = atlas_tile(l, op->shape);
        RInst       *in;
        if (!t)
            continue;
        in         = &g->inst[g->n_inst];
        in->dst[0] = op->x;
        in->dst[1] = op->y;
        in->dst[2] = (int32_t)t->w;
        in->dst[3] = (int32_t)t->h;
        in->src[0] = (int32_t)t->x;
        in->src[1] = (int32_t)t->y;
        in->src[2] = op->flip;
        in->src[3] = op->stencil;
        /*  misc[1] is the line's width for a stencilled car and, for
         *  every other op, the tile's altitude.  What sprite.vert needs
         *  to put the sprite back where its tile went when the camera is
         *  off the original's own. */
        in->misc[1] = (float)op->alt;
        in->misc[2] = in->misc[3] = 0.0f;
        /*  The tile's diamond origin and grid position, for the water
         *  shader: the art's top-left plus its rise is the diamond's row. */
        in->under[0] = op->x;
        in->under[1] = op->y + (int32_t)t->ay;
        in->under[2] = op->col;
        in->under[3] = op->row;
        if (op->stencil >= 0 && op->under_shape)
        {
            const RTile *ut = atlas_tile(l, op->under_shape);
            if (ut)
            {
                in->under[0] = (int32_t)ut->x;
                in->under[1] = (int32_t)ut->y;
                in->under[2] = op->under_x;
                in->under[3] = op->under_y;
                in->misc[1]  = (float)ut->w;
                in->misc[2]  = (float)op->under_flip;
                in->misc[3]  = (float)ut->h;
            }
            else
                in->src[3] = -1;
        }
        if (op->kind == R_OP_SHADOW)
            g->kind[g->n_inst] = K_SHADOW;
        else if (op->terrain)
        {
            g->kind[g->n_inst] = terrain_kind(op->shape - l->id_base);
            if (g->kind[g->n_inst] == K_WATER_ART &&
                (op->row == R_MAP - 1 || op->col == R_MAP - 1))
                g->kind[g->n_inst] = K_WATER_EDGE;
        }
        else
        {
            /*  The underground lattice is emitted as a plain sprite, not
             *  as terrain.  It is the empty tile's art all the same. */
            int32_t sh         = op->shape - l->id_base;
            g->kind[g->n_inst] = (sh >= 305 && sh <= 318)                       ? K_UG_LATTICE
                                 : (sh >= 0 && sh < 256 && meshed[sh])          ? K_LINE_ART
                                 : (op->stencil >= 0)                           ? K_CAR
                                 : (sh >= 374 && sh <= 378)                     ? K_TRAIN
                                 : (sh >= 0xC6 && sh <= 0xFF)                   ? K_LANDMARK
                                                                                : K_SPRITE;
        }
        /*  misc[0] is the op's painter's slot times 256.  It adds the
         *  tile's altitude in levels times four.  It adds one for art
         *  that stands: a building, a tree, a car, a pole or a bridge.
         *  It adds nothing for art that lies flat.  That is the land's
         *  slopes, the ground, the water, the lattice and a silhouette.
         *  It also covers a line, thread or power piece no taller than a
         *  tile's diamond. sprite.vert's depth reads both.  The altitude
         *  rides here because misc[1] is a stencilled car's line width. */
        {
            uint8_t kd   = g->kind[g->n_inst];
            int     flat = kd == K_TERRAIN || kd == K_LAND_ART || kd == K_WATER_ART || kd == K_WATER_COL || kd == K_WATER_EDGE || kd == K_UG_LATTICE || kd == K_SHADOW ||
                       (kd == K_LINE_ART && (int32_t)t->h <= (int32_t)l->tile_h + 1);
            in->misc[0]  = 256.0f * (float)op->order + 4.0f * (float)op->alt + (flat ? 0.0f : 1.0f); /* exact in a float below 2^24 */
        }
        {
            /*  Debug: --gpu-dump row,col prints a tile's instances. */
            static int dr = -2, dc = -2;
            if (dr == -2)
            {
                const char *e = g_dev.gpu_dump_at;
                dr = dc = -1;
                if (e)
                    sscanf(e, "%d,%d", &dr, &dc);
            }
            if (op->row == dr && op->col == dc)
                dumpf("inst %u: tile r%d c%d shape %d kind %d dst (%d,%d %dx%d) "
                      "src (%d,%d flip %d stencil %d) depth %.6f order %u\n",
                      (unsigned)g->n_inst,
                      (int)op->row,
                      (int)op->col,
                      (int)(op->shape - l->id_base),
                      (int)g->kind[g->n_inst],
                      (int)in->dst[0],
                      (int)in->dst[1],
                      (int)in->dst[2],
                      (int)in->dst[3],
                      (int)in->src[0],
                      (int)in->src[1],
                      (int)in->src[2],
                      (int)in->src[3],
                      (double)in->misc[0],
                      (unsigned)op->order);
        }
        g->n_inst++;
    }
    return 0;
}

/* ---- the mesh in slots --------------------------------------------------- */

#define GPU_SLOTS (3 * MESH_CHUNKS)

/*  A slot's room for a range: an eighth over and a floor.  So a chunk
 *  can grow a little before the buffer is laid out again.  None under
 *  --gpu-tight, which lays it out at every upload to exercise that. */
static uint32_t slot_room(uint32_t n)
{
    return g_dev.gpu_tight ? n : n + n / 8u + 2048u;
}

/*  Did this build change the chunk slot k belongs to? */
static int slot_changed(const RMesh *m, int k)
{
    return m->chunk_changed[k < 2 * MESH_CHUNKS ? k / 2 : k - 2 * MESH_CHUNKS];
}

/*  The mesh's ranges in slot order: the opaque list's, then the water's. */
static void mesh_ranges(const RMesh *m, uint32_t *first, uint32_t *count, const RMeshVert **list)
{
    int k;
    for (k = 0; k < 2 * MESH_CHUNKS; ++k)
    {
        first[k] = m->range_start[k];
        count[k] = m->range_count[k];
        list[k]  = m->land;
    }
    for (k = 0; k < MESH_CHUNKS; ++k)
    {
        first[2 * MESH_CHUNKS + k] = m->wrange_start[k];
        count[2 * MESH_CHUNKS + k] = m->wrange_count[k];
        list[2 * MESH_CHUNKS + k]  = m->water;
    }
}

/*  Every slot laid out afresh, in order, each with its room.  A buffer
 *  too small for the total is made anew.  Either way every slot is
 *  uploaded after this: the old contents are in the wrong places, or
 *  gone. */
static int slots_lay(RGpu *g, const uint32_t *count)
{
    uint32_t pos = 0;
    int      k;
    for (k = 0; k < GPU_SLOTS; ++k)
    {
        g->slot[k].first = pos;
        g->slot[k].cap   = slot_room(count[k]);
        pos += g->slot[k].cap;
    }
    if (pos > g->mbuf_cap)
    {
        if (g->mbuf)
            SDL_ReleaseGPUBuffer(g->dev, g->mbuf);
        g->mbuf = make_buffer(g, SDL_GPU_BUFFERUSAGE_VERTEX, pos * (uint32_t)sizeof(RMeshVert));
        if (!g->mbuf)
        {
            g->mbuf_cap = 0;
            g->slotted  = 0;
            return -1;
        }
        g->mbuf_cap = pos;
    }
    g->slotted = 1;
    return 0;
}

/*  The ranges `up` names into their slots.
 *
 *      One transfer buffer holding them end to end.
 *      One copy pass.
 *      No cycling.
 *      So the other slots' contents stay. */
static int slots_upload(RGpu *g, const uint32_t *first, const uint32_t *count, const RMeshVert *const *list, const uint8_t *up)
{
    uint32_t               bytes = 0, at = 0;
    int                    k;
    SDL_GPUTransferBuffer *tb;
    SDL_GPUCommandBuffer  *cmd;
    SDL_GPUCopyPass       *cp;
    uint8_t               *map;
    for (k = 0; k < GPU_SLOTS; ++k)
        if (up[k])
            bytes += count[k] * (uint32_t)sizeof(RMeshVert);
    if (!bytes)
        return 0;
    tb = make_transfer(g, 0, bytes);
    if (!tb)
        return -1;
    map = (uint8_t *)SDL_MapGPUTransferBuffer(g->dev, tb, false);
    if (!map)
    {
        SDL_ReleaseGPUTransferBuffer(g->dev, tb);
        return -1;
    }
    for (k = 0; k < GPU_SLOTS; ++k)
        if (up[k] && count[k])
        {
            memcpy(map + at, list[k] + first[k], count[k] * sizeof(RMeshVert));
            at += count[k] * (uint32_t)sizeof(RMeshVert);
        }
    SDL_UnmapGPUTransferBuffer(g->dev, tb);
    cmd = SDL_AcquireGPUCommandBuffer(g->dev);
    cp  = SDL_BeginGPUCopyPass(cmd);
    at  = 0;
    for (k = 0; k < GPU_SLOTS; ++k)
        if (up[k] && count[k])
        {
            SDL_GPUTransferBufferLocation src;
            SDL_GPUBufferRegion           dst;
            memset(&src, 0, sizeof src);
            memset(&dst, 0, sizeof dst);
            src.transfer_buffer = tb;
            src.offset          = at;
            dst.buffer          = g->mbuf;
            dst.offset          = g->slot[k].first * (uint32_t)sizeof(RMeshVert);
            dst.size            = count[k] * (uint32_t)sizeof(RMeshVert);
            SDL_UploadToGPUBuffer(cp, &src, &dst, false);
            at += dst.size;
        }
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_WaitForGPUIdle(g->dev);
    SDL_ReleaseGPUTransferBuffer(g->dev, tb);
    return 0;
}

int gpu_set_mesh(RGpu *g, const RMesh *m)
{
    Uint64           t0 = SDL_GetTicksNS();
    uint32_t         first[GPU_SLOTS], count[GPU_SLOTS], n = 0, up_n = 0;
    const RMeshVert *list[GPU_SLOTS];
    uint8_t          up[GPU_SLOTS];
    int              k, lay = !g->slotted || g_dev.gpu_tight;
    g->mesh_n = 0;
    if (!m->ranged || m->n_land + m->n_water == 0)
    {
        g->slotted = 0; /* nothing, or a mesh without ranges: nothing to draw */
        return 0;
    }
    mesh_ranges(m, first, count, list);
    /*  The ranges to upload: the changed chunks'.  Every one when a slot
     *  must move, since the layout, or the buffer, is new. */
    for (k = 0; k < GPU_SLOTS; ++k)
        if (slot_changed(m, k) && count[k] > g->slot[k].cap)
            lay = 1;
    if (lay && slots_lay(g, count) != 0)
        return -1;
    for (k = 0; k < GPU_SLOTS; ++k)
    {
        up[k]            = (uint8_t)(lay || slot_changed(m, k));
        g->slot[k].count = count[k];
        n += count[k];
        up_n += up[k] ? count[k] : 0u;
    }
    if (slots_upload(g, first, count, list, up) != 0)
        return -1;
    g->mesh_n = n;
    if (g_dev.times)
        dumpf("time  gpu_set_mesh %u tris, %.1f MB uploaded of %.1f held%s: %.1f ms\n",
              n / 3u,
              (double)up_n * sizeof(RMeshVert) / 1e6,
              (double)n * sizeof(RMeshVert) / 1e6,
              lay ? ", laid out afresh" : "",
              (double)(SDL_GetTicksNS() - t0) / 1e6);
    return 0;
}

int gpu_set_movers(RGpu *g, const RMeshVert *v, uint32_t n)
{
    uint32_t                      bytes = n * (uint32_t)sizeof(RMeshVert);
    SDL_GPUTransferBuffer        *tb;
    SDL_GPUCommandBuffer         *cmd;
    SDL_GPUCopyPass              *cp;
    SDL_GPUTransferBufferLocation src;
    SDL_GPUBufferRegion           dst;
    void                         *map;
    g->movers_n = 0;
    if (!n)
        return 0;
    if (n > g->mvbuf_cap)
    {
        uint32_t cap = g->mvbuf_cap ? g->mvbuf_cap : 4096u;
        while (cap < n)
            cap *= 2u;
        if (g->mvbuf)
            SDL_ReleaseGPUBuffer(g->dev, g->mvbuf);
        g->mvbuf = make_buffer(g, SDL_GPU_BUFFERUSAGE_VERTEX, cap * (uint32_t)sizeof(RMeshVert));
        if (!g->mvbuf)
        {
            g->mvbuf_cap = 0;
            return -1;
        }
        g->mvbuf_cap = cap;
    }
    tb = make_transfer(g, 0, bytes);
    if (!tb)
        return -1;
    map = SDL_MapGPUTransferBuffer(g->dev, tb, true);
    if (!map)
    {
        SDL_ReleaseGPUTransferBuffer(g->dev, tb);
        return -1;
    }
    memcpy(map, v, bytes);
    SDL_UnmapGPUTransferBuffer(g->dev, tb);
    memset(&src, 0, sizeof src);
    memset(&dst, 0, sizeof dst);
    src.transfer_buffer = tb;
    dst.buffer          = g->mvbuf;
    dst.size            = bytes;
    cmd                 = SDL_AcquireGPUCommandBuffer(g->dev);
    cp                  = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUBuffer(cp, &src, &dst, true);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_WaitForGPUIdle(g->dev);
    SDL_ReleaseGPUTransferBuffer(g->dev, tb);
    g->movers_n = n;
    return 0;
}

struct SDL_GPUDevice *gpu_device(const RGpu *g)
{
    return g->dev;
}

int gpu_swapchain_format(const RGpu *g)
{
    return (int)g->swap_fmt;
}

int gpu_offscreen_format(const RGpu *g)
{
    return (int)g->color_fmt;
}

const char *gpu_driver(const RGpu *g)
{
    const char *d = SDL_GetGPUDeviceDriver(g->dev);
    return d ? d : "?";
}

void gpu_stats(const RGpu *g, uint32_t *instances, uint32_t *culled)
{
    *instances = g->last_drawn;
    *culled    = g->last_culled;
}
