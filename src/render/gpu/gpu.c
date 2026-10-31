/*  gpu.c: the device: making it, and putting data on it.  What the
 *  renderer asks of it is in gpu.h.  Drawing with it is gpu/frame.c. */
#include "gpu/internal.h"
#include "log.h"
#include "mesh/ground_bytes.h"
#include "opt.h"
#include "project.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>

#include "dump.h"
#include "gpu/clouds.h"
#include "shaders.h"
#include "show.h"

/*  The sun for the lit material.
 *
 *      A direction toward the light.
 *      In world units (x along columns.
 *      Y along rows.
 *      Z up).
 *      Plus ambient and diffuse. */
static void gpu_set_light(RGpu *g, float x, float y, float z, float ambient, float diffuse);
static int  make_sky_field(RGpu *g);

/*  A square sampled texture of `n` a side, made again only when the side
 *  changes. */
static int fit_texture(RGpu *g, SDL_GPUTexture **tex, int32_t *have, SDL_GPUTextureFormat fmt, int32_t n);

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
    ci.num_storage_buffers = (Uint32)s->num_storage_buffers;
    return SDL_CreateGPUShader(g->dev, &ci);
}

enum
{
    L_NONE = 0,
    L_INSTANCE,
    L_MESH,
    L_MESH_SHOW /* the mesh as the edit show reads it: no attributes, the shader reads the buffers (show.h) */
};

static SDL_GPUGraphicsPipeline *make_pipe(RGpu *g, const char *vs_name, const char *fs_name, SDL_GPUTextureFormat target, int depth_test, int depth_write, int layout, int blend)
{
    SDL_GPUShader                    *vs = load_shader(g, vs_name);
    SDL_GPUShader                    *fs = load_shader(g, fs_name);
    SDL_GPUGraphicsPipelineCreateInfo pi;
    SDL_GPUVertexBufferDescription    vb;
    SDL_GPUVertexAttribute            va[5];
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
        va[4].location                                   = 4;
        va[4].buffer_slot                                = 0;
        va[4].format                                     = SDL_GPU_VERTEXELEMENTFORMAT_INT4;
        va[4].offset                                     = 64;
        pi.vertex_input_state.vertex_buffer_descriptions = &vb;
        pi.vertex_input_state.num_vertex_buffers         = 1;
        pi.vertex_input_state.vertex_attributes          = va;
        pi.vertex_input_state.num_vertex_attributes      = 5;
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
        va[1].format                                     = SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM;
        va[1].offset                                     = (Uint32)offsetof(RMeshVert, nrm);
        va[2].location                                   = 2;
        va[2].buffer_slot                                = 0;
        va[2].format                                     = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        va[2].offset                                     = (Uint32)offsetof(RMeshVert, paint);
        va[3].location                                   = 4;
        va[3].buffer_slot                                = 0;
        va[3].format                                     = SDL_GPU_VERTEXELEMENTFORMAT_HALF2;
        va[3].offset                                     = (Uint32)offsetof(RMeshVert, aux);
        pi.vertex_input_state.vertex_buffer_descriptions = &vb;
        pi.vertex_input_state.num_vertex_buffers         = 1;
        pi.vertex_input_state.vertex_attributes          = va;
        pi.vertex_input_state.num_vertex_attributes      = 4;
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
    if (target == SDL_GPU_TEXTUREFORMAT_INVALID)
    {
        /*  Depth alone, for the sun's shadow map.  The bias grows with
         *  the slope, so a face the sun grazes does not shade itself. */
        pi.target_info.num_color_targets               = 0;
        pi.rasterizer_state.enable_depth_bias          = true;
        pi.rasterizer_state.depth_bias_constant_factor = 1.0f;
        pi.rasterizer_state.depth_bias_slope_factor    = 1.5f;
    }
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

/*  A vertex buffer.  Nothing here makes any other kind. */
static SDL_GPUBuffer *make_buffer(RGpu *g, uint32_t bytes)
{
    SDL_GPUBufferCreateInfo bi;
    memset(&bi, 0, sizeof bi);
    bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
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
    /*  No wait for the device.  It runs what it is handed in order, so
     *  the frames after this read the new texels.  The transfer buffer is
     *  freed once the copy is done with it. */
    SDL_SubmitGPUCommandBuffer(cmd);
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
    if (g->air)
        SDL_ReleaseGPUTexture(g->dev, g->air);
    if (g->clouds)
        SDL_ReleaseGPUTexture(g->dev, g->clouds);
    if (g->ao)
        SDL_ReleaseGPUTexture(g->dev, g->ao);
    g->color = g->shadow = g->depth = g->offscreen = g->air = g->clouds = g->ao = NULL;
    g->tw = g->th = g->cw = g->ch = g->aw = g->ah = 0;
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
    g->color  = make_texture(g, GPU_COLOR_FMT, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h);
    g->shadow = make_texture(g, SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h);
    g->depth  = make_texture(g, g->depth_fmt, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h);
    g->air    = make_texture(g, GPU_COLOR_FMT, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h);
    if (!g->color || !g->shadow || !g->depth || !g->air)
    {
        release_targets(g);
        return -1;
    }
    g->tw = w;
    g->th = h;
    return 0;
}

/*  The clouds' target, sized on its own: the frame's target over the
 *  cloud pass's step.  It holds light and what is let through, so a
 *  half float keeps the faint wisps. */
int ensure_cloud_target(RGpu *g, int32_t w, int32_t h)
{
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    if (g->clouds && g->cw == w && g->ch == h)
        return 0;
    if (g->clouds)
        SDL_ReleaseGPUTexture(g->dev, g->clouds);
    g->clouds = make_texture(g, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h);
    if (!g->clouds)
        return -1;
    g->cw = w;
    g->ch = h;
    return 0;
}

/*  The occlusion's target, sized on its own: the frame's target over
 *  the occlusion pass's step.  The occlusion, and the depth it was found
 *  at, which the filter up compares. */
int ensure_ao_target(RGpu *g, int32_t w, int32_t h)
{
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    if (g->ao && g->aw == w && g->ah == h)
        return 0;
    if (g->ao)
        SDL_ReleaseGPUTexture(g->dev, g->ao);
    g->ao = make_texture(g, SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h);
    if (!g->ao)
        return -1;
    g->aw = w;
    g->ah = h;
    return 0;
}

/*  The sun's shadow map, n texels a side.  A new one holds nothing, so
 *  the frame draws it before it reads it. */
int ensure_sun_map(RGpu *g, int32_t n)
{
    if (g->sun_map && g->sun_n == n)
        return 0;
    if (g->sun_map)
        SDL_ReleaseGPUTexture(g->dev, g->sun_map);
    g->sun_map   = make_texture(g, g->depth_fmt, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, n, n);
    g->sun_n     = g->sun_map ? n : 0;
    g->sun_valid = 0;
    return g->sun_map ? 0 : -1;
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
    g->offscreen = make_texture(g, GPU_COLOR_FMT, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h);
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
    g->ibuf = make_buffer(g, cap * (uint32_t)sizeof(RInst));
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
    g->depth_fmt = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    if (!SDL_GPUTextureSupportsFormat(g->dev, g->depth_fmt, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
        g->depth_fmt = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    R_DBG("gpu", "depth %s", g->depth_fmt == SDL_GPU_TEXTUREFORMAT_D32_FLOAT ? "D32_FLOAT" : "D16_UNORM");

    /*  Pipelines.
     *
     *      The terrain writes depth.
     *      The sprites test it.
     *      The shadows test it into the mask.
     *      The water's art is terrain.
     *      The mesh is geometry.
     *      The resolve needs none. */
    g->pipe_terrain      = make_pipe(g, "sprite.vert", "sprite.frag", GPU_COLOR_FMT, 1, 1, L_INSTANCE, 0);
    g->pipe_sprite       = make_pipe(g, "sprite.vert", "sprite.frag", GPU_COLOR_FMT, 1, 0, L_INSTANCE, 0); /* the painter: the art tests, the ground wrote      */
    g->pipe_sprite_depth = make_pipe(g, "sprite.vert", "sprite.frag", GPU_COLOR_FMT, 1, 1, L_INSTANCE, 0); /* the camera's depth: the art writes it, it stands in the world */
    g->pipe_shadow       = make_pipe(g, "sprite.vert", "shadow.frag", SDL_GPU_TEXTUREFORMAT_R8_UNORM, 1, 0, L_INSTANCE, 0);
    g->pipe_mesh         = make_pipe(g, "terrain.vert", "terrain.frag", GPU_COLOR_FMT, 1, 1, L_MESH, 0);
    /*  The water column faces of the map edge's cut: blended over the
     *  seabed behind them, tested against depth but not writing it. */
    g->pipe_mesh_blend        = make_pipe(g, "terrain.vert", "terrain.frag", GPU_COLOR_FMT, 1, 0, L_MESH, 1);
    g->pipe_mesh_show         = make_pipe(g, "terrain_show.vert", "terrain_show.frag", GPU_COLOR_FMT, 1, 1, L_MESH_SHOW, 0);
    g->pipe_mesh_blend_show   = make_pipe(g, "terrain_show.vert", "terrain_show.frag", GPU_COLOR_FMT, 1, 0, L_MESH_SHOW, 1);
    g->pipe_mesh_settle       = make_pipe(g, "terrain_settle.vert", "terrain_settle.frag", GPU_COLOR_FMT, 1, 1, L_MESH, 0);
    g->pipe_mesh_blend_settle = make_pipe(g, "terrain_settle.vert", "terrain_settle.frag", GPU_COLOR_FMT, 1, 0, L_MESH, 1);
    g->pipe_sheet             = make_pipe(g, "terrain_sheet.vert", "terrain_settle.frag", GPU_COLOR_FMT, 1, 1, L_NONE, 0);
    g->pipe_mesh_tris         = make_pipe(g, "terrain_tris.vert", "terrain_tris.frag", GPU_COLOR_FMT, 1, 1, L_MESH_SHOW, 0);
    g->pipe_terrain_mark      = make_pipe(g, "sprite.vert", "sprite_mark.frag", GPU_COLOR_FMT, 1, 1, L_INSTANCE, 0);
    g->pipe_sprite_mark       = make_pipe(g, "sprite.vert", "sprite_mark.frag", GPU_COLOR_FMT, 1, 0, L_INSTANCE, 0);
    g->pipe_sprite_depth_mark = make_pipe(g, "sprite.vert", "sprite_mark.frag", GPU_COLOR_FMT, 1, 1, L_INSTANCE, 0);
    /*  The mesh with the marked tiles lit.  It reads the buffers as the
     *  edit show does, and each triangle's tile with them. */
    g->pipe_mesh_mark              = make_pipe(g, "terrain_mark.vert", "terrain_mark.frag", GPU_COLOR_FMT, 1, 1, L_MESH_SHOW, 0);
    g->pipe_mesh_blend_mark        = make_pipe(g, "terrain_mark.vert", "terrain_mark.frag", GPU_COLOR_FMT, 1, 0, L_MESH_SHOW, 1);
    g->pipe_mesh_settle_mark       = make_pipe(g, "terrain_settle_mark.vert", "terrain_settle_mark.frag", GPU_COLOR_FMT, 1, 1, L_MESH_SHOW, 0);
    g->pipe_mesh_blend_settle_mark = make_pipe(g, "terrain_settle_mark.vert", "terrain_settle_mark.frag", GPU_COLOR_FMT, 1, 0, L_MESH_SHOW, 1);
    g->pipe_resolve                = make_pipe(g, "resolve.vert", "resolve.frag", g->swap_fmt, 0, 0, L_NONE, 0);
    g->pipe_resolve_off            = make_pipe(g, "resolve.vert", "resolve.frag", GPU_COLOR_FMT, 0, 0, L_NONE, 0);
    g->pipe_air                    = make_pipe(g, "resolve.vert", "air.frag", GPU_COLOR_FMT, 0, 0, L_NONE, 0);
    g->pipe_air_clouds             = make_pipe(g, "resolve.vert", "air_clouds.frag", SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, 0, 0, L_NONE, 0);
    g->pipe_air_ao                 = make_pipe(g, "resolve.vert", "air_ao.frag", SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT, 0, 0, L_NONE, 0);
    g->pipe_sun                    = make_pipe(g, "sun_depth.vert", "sun_depth.frag", SDL_GPU_TEXTUREFORMAT_INVALID, 1, 1, L_MESH, 0);
    if (!g->pipe_terrain || !g->pipe_sprite || !g->pipe_sprite_depth || !g->pipe_shadow ||
        !g->pipe_mesh || !g->pipe_resolve ||
        !g->pipe_resolve_off || !g->pipe_mesh_show || !g->pipe_mesh_blend_show || !g->pipe_mesh_settle || !g->pipe_mesh_blend_settle || !g->pipe_sheet || !g->pipe_mesh_mark || !g->pipe_mesh_blend_mark || !g->pipe_mesh_settle_mark || !g->pipe_mesh_blend_settle_mark ||
        !g->pipe_terrain_mark || !g->pipe_sprite_mark || !g->pipe_sprite_depth_mark || !g->pipe_air || !g->pipe_air_clouds || !g->pipe_air_ao || !g->pipe_sun)
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
        si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        g->wrap           = SDL_CreateGPUSampler(g->dev, &si);
        /*  The sun's shadow map, compared and filtered by the device:
         *  a point nearer the sun than the map's surface is lit. */
        si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.enable_compare = true;
        si.compare_op     = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        g->sun_cmp        = SDL_CreateGPUSampler(g->dev, &si);
    }
    if (!g->nearest || !g->linear || !g->wrap || !g->sun_cmp || ensure_sun_map(g, 1024) != 0)
    {
        snprintf(err, err_len, "sampler creation failed: %s", SDL_GetError());
        gpu_destroy(g);
        return NULL;
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

    g->pal     = make_texture(g, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, 256, 1);
    g->ptb     = make_transfer(g, 0, 256u * 4u);
    g->shore   = make_texture(g, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, r_map, r_map);
    g->shore_n = r_map;
    g->show    = make_texture(g, SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, SHOW_W, SHOW_H);
    if (!g->pal || !g->ptb || !g->nearest || !g->linear || !g->shore || !g->show)
    {
        snprintf(err, err_len, "resource creation failed: %s", SDL_GetError());
        gpu_destroy(g);
        return NULL;
    }
    /*  Every piece still: a texture starts with whatever the device left
     *  in it. */
    {
        uint8_t *still = (uint8_t *)calloc(SHOW_PIECES, 1);
        if (!still || gpu_set_show(g, still) != 0)
        {
            free(still);
            snprintf(err, err_len, "resource creation failed: %s", SDL_GetError());
            gpu_destroy(g);
            return NULL;
        }
        free(still);
    }
    gpu_set_palette(g, a);
    /*  Until a build lays the map, what it lays over the ground is one
     *  empty texel. */
    {
        uint8_t none[4] = {0, 0, 0, 0};
        if (gpu_set_laid(g, none, 1) != 0)
        {
            snprintf(err, err_len, "what the build lays over the ground: %s", SDL_GetError());
            gpu_destroy(g);
            return NULL;
        }
    }
    if (make_sky_field(g) != 0)
    {
        snprintf(err, err_len, "the sky's field: %s", SDL_GetError());
        gpu_destroy(g);
        return NULL;
    }
    /*  The sun, fitted to the slope sprites.  Of the four plain slopes
     *  at 32 px the one facing south (code 1) is the brightest.  Its
     *  mean luminance is 135, then east 88, north 84 and west 71.  So
     *  the light comes from the south-east, over the viewer's shoulder. */
    gpu_set_light(g, 0.35f, 0.85f, 0.9f, 0.5f, 0.6f);
    g->exposure = 1.0f;
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
        if (g->smog)
            SDL_ReleaseGPUTexture(g->dev, g->smog);
        if (g->laid)
            SDL_ReleaseGPUTexture(g->dev, g->laid);
        if (g->cloud)
            SDL_ReleaseGPUTexture(g->dev, g->cloud);
        if (g->sky_field)
            SDL_ReleaseGPUTexture(g->dev, g->sky_field);
        if (g->wrap)
            SDL_ReleaseGPUSampler(g->dev, g->wrap);
        if (g->sun_cmp)
            SDL_ReleaseGPUSampler(g->dev, g->sun_cmp);
        if (g->sun_map)
            SDL_ReleaseGPUTexture(g->dev, g->sun_map);
        if (g->pipe_sun)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_sun);
        if (g->show)
            SDL_ReleaseGPUTexture(g->dev, g->show);
        if (g->settle)
            SDL_ReleaseGPUTexture(g->dev, g->settle);
        if (g->land)
            SDL_ReleaseGPUTexture(g->dev, g->land);
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
        if (g->mibuf)
            SDL_ReleaseGPUBuffer(g->dev, g->mibuf);
        if (g->mkbuf)
            SDL_ReleaseGPUBuffer(g->dev, g->mkbuf);
        if (g->mtbuf)
            SDL_ReleaseGPUBuffer(g->dev, g->mtbuf);
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
        if (g->pipe_mesh_show)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_mesh_show);
        if (g->pipe_mesh_mark)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_mesh_mark);
        if (g->pipe_mesh_blend_mark)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_mesh_blend_mark);
        if (g->pipe_mesh_settle_mark)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_mesh_settle_mark);
        if (g->pipe_mesh_blend_settle_mark)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_mesh_blend_settle_mark);
        if (g->pipe_terrain_mark)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_terrain_mark);
        if (g->pipe_sprite_mark)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_sprite_mark);
        if (g->pipe_sprite_depth_mark)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_sprite_depth_mark);
        if (g->marks)
            SDL_ReleaseGPUTexture(g->dev, g->marks);
        if (g->marks_sweep)
            SDL_ReleaseGPUTexture(g->dev, g->marks_sweep);
        if (g->pipe_mesh_blend_show)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_mesh_blend_show);
        if (g->pipe_mesh_settle)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_mesh_settle);
        if (g->pipe_mesh_blend_settle)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_mesh_blend_settle);
        if (g->pipe_sheet)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_sheet);
        if (g->pipe_mesh_tris)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_mesh_tris);
        if (g->pipe_resolve)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_resolve);
        if (g->pipe_resolve_off)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_resolve_off);
        if (g->pipe_air)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_air);
        if (g->pipe_air_clouds)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_air_clouds);
        if (g->pipe_air_ao)
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_air_ao);
        SDL_ReleaseWindowFromGPUDevice(g->dev, g->win);
        SDL_DestroyGPUDevice(g->dev);
    }
    free(g->settle_cpu);
    free(g->shaped);
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
    /*  The field is one texel a tile, so it is made again whenever the
     *  map takes another size. */
    if (g->shore_n != n)
    {
        if (g->shore)
            SDL_ReleaseGPUTexture(g->dev, g->shore);
        g->shore   = make_texture(g, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, n, n);
        g->shore_n = g->shore ? n : 0;
        if (!g->shore)
            return -1;
    }
    return upload_texture(g, g->shore, field, n, n, 4);
}

void gpu_set_leaves(RGpu *g, const float colors[GPU_LEAF_SPECIES][2][4], const float rag[GPU_LEAF_SPECIES])
{
    memcpy(g->leaf, colors, sizeof g->leaf);
    memcpy(g->leaf_rag, rag, sizeof g->leaf_rag);
}

int gpu_set_laid(RGpu *g, const uint8_t *field, int32_t n)
{
    if (fit_texture(g, &g->laid, &g->laid_n, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, n) != 0)
        return -1;
    return upload_texture(g, g->laid, field, n, n, 4);
}

int gpu_set_smog(RGpu *g, const uint8_t *field, int32_t n)
{
    if (fit_texture(g, &g->smog, &g->smog_n, SDL_GPU_TEXTUREFORMAT_R8_UNORM, n) != 0)
        return -1;
    return upload_texture(g, g->smog, field, n, n, 1);
}

/*  A 3D texture of `w` by `h` by `d` texels, filled from host memory. */
static SDL_GPUTexture *make_volume(RGpu *g, SDL_GPUTextureFormat fmt, int w, int h, int d, int bpp, void (*fill)(uint8_t *, const void *), const void *arg)
{
    const uint32_t             bytes = (uint32_t)(w * h * d * bpp);
    SDL_GPUTextureCreateInfo   ti;
    SDL_GPUTexture            *tex;
    SDL_GPUTransferBuffer     *tb;
    SDL_GPUCommandBuffer      *cmd;
    SDL_GPUCopyPass           *cp;
    SDL_GPUTextureTransferInfo src;
    SDL_GPUTextureRegion       dst;
    void                      *map;

    memset(&ti, 0, sizeof ti);
    ti.type                 = SDL_GPU_TEXTURETYPE_3D;
    ti.format               = fmt;
    ti.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width                = (Uint32)w;
    ti.height               = (Uint32)h;
    ti.layer_count_or_depth = (Uint32)d;
    ti.num_levels           = 1;
    tex                     = SDL_CreateGPUTexture(g->dev, &ti);
    tb                      = make_transfer(g, 0, bytes);
    map                     = tb ? SDL_MapGPUTransferBuffer(g->dev, tb, false) : NULL;
    if (!tex || !map)
    {
        if (tb)
            SDL_ReleaseGPUTransferBuffer(g->dev, tb);
        if (tex)
            SDL_ReleaseGPUTexture(g->dev, tex);
        return NULL;
    }
    fill((uint8_t *)map, arg);
    SDL_UnmapGPUTransferBuffer(g->dev, tb);
    memset(&src, 0, sizeof src);
    memset(&dst, 0, sizeof dst);
    src.transfer_buffer = tb;
    src.pixels_per_row  = (Uint32)w;
    src.rows_per_layer  = (Uint32)h;
    dst.texture         = tex;
    dst.w               = (Uint32)w;
    dst.h               = (Uint32)h;
    dst.d               = (Uint32)d;
    cmd                 = SDL_AcquireGPUCommandBuffer(g->dev);
    if (!cmd)
    {
        SDL_ReleaseGPUTransferBuffer(g->dev, tb);
        SDL_ReleaseGPUTexture(g->dev, tex);
        return NULL;
    }
    cp = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(g->dev, tb);
    return tex;
}

static void fill_noise(uint8_t *px, const void *arg)
{
    (void)arg;
    clouds_noise(px);
}

/*  The sky's field, from which the clouds take their place and size.
 *  The ground reads the same repeating noise for its large bumps, so it
 *  is made with the device. */
static int make_sky_field(RGpu *g)
{
    float *v = (float *)malloc(sizeof *v * SKY_FIELD_N * SKY_FIELD_N);
    int    rc;
    if (!v)
        return -1;
    clouds_field(v, g->sky_stats);
    /*  Each run's clouds start at their own place in the field, from the
     *  clock.  Under --seed two runs of one seed draw the same sky. */
    {
        uint32_t h = g_dev.seed ? (uint32_t)atoi(g_dev.seed) : (uint32_t)SDL_GetTicksNS() ^ (uint32_t)time(NULL);
        h ^= h >> 16, h *= 0x7feb352du, h ^= h >> 15, h *= 0x846ca68bu, h ^= h >> 16;
        g->sky_origin[0] = (float)(h & 0xFFFFu) / 65536.0f;
        g->sky_origin[1] = (float)(h >> 16) / 65536.0f;
    }
    g->sky_field = make_texture(g, SDL_GPU_TEXTUREFORMAT_R32_FLOAT, SDL_GPU_TEXTUREUSAGE_SAMPLER, SKY_FIELD_N, SKY_FIELD_N);
    rc           = g->sky_field ? upload_texture(g, g->sky_field, v, SKY_FIELD_N, SKY_FIELD_N, 4) : -1;
    free(v);
    return rc;
}

int gpu_air_ready(RGpu *g)
{
    uint8_t none = 0;

    if (g->cloud && g->sky_field)
        return 0;
    /*  Until the city gives its pollution, the smog field is one clean
     *  texel. */
    if (!g->smog && gpu_set_smog(g, &none, 1) != 0)
        return -1;
    g->cloud = make_volume(g, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, CLOUD_NOISE_N, CLOUD_NOISE_N, CLOUD_NOISE_N, 4, fill_noise, NULL);
    if (!g->cloud)
        return -1;
    return g->sky_field ? 0 : make_sky_field(g);
}

int gpu_set_marks(RGpu *g, const uint8_t *marks, const uint8_t *sweep, int32_t n)
{
    g->marks_on = marks != NULL && sweep != NULL;
    if (!g->marks_on)
        return 0;
    if (g->marks_n != n)
    {
        if (g->marks)
            SDL_ReleaseGPUTexture(g->dev, g->marks);
        if (g->marks_sweep)
            SDL_ReleaseGPUTexture(g->dev, g->marks_sweep);
        g->marks       = make_texture(g, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, n, n);
        g->marks_sweep = make_texture(g, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, n, n);
        g->marks_n     = n;
    }
    if (!g->marks || !g->marks_sweep || upload_texture(g, g->marks, marks, n, n, 4) != 0 ||
        upload_texture(g, g->marks_sweep, sweep, n, n, 4) != 0)
    {
        g->marks_on = 0;
        return -1;
    }
    return 0;
}

int gpu_set_show(RGpu *g, const uint8_t *state)
{
    return upload_texture(g, g->show, state, SHOW_W, SHOW_H, 1);
}

/*  A texture of one texel a tile or a chunk, made again whenever the
 *  edge it was made for changes. */
static int fit_texture(RGpu *g, SDL_GPUTexture **tex, int32_t *have, SDL_GPUTextureFormat fmt, int32_t n)
{
    if (*have == n && *tex)
        return 0;
    if (*tex)
        SDL_ReleaseGPUTexture(g->dev, *tex);
    *tex  = make_texture(g, fmt, SDL_GPU_TEXTUREUSAGE_SAMPLER, n, n);
    *have = *tex ? n : 0;
    return *tex ? 0 : -1;
}

int gpu_set_land(RGpu *g, const float *corners, int32_t n)
{
    if (fit_texture(g, &g->land, &g->land_n, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, n) != 0)
        return -1;
    return upload_texture(g, g->land, corners, n, n, 16);
}

int gpu_set_shaped(RGpu *g, const uint8_t *shaped, int32_t n)
{
    if (!shaped)
    {
        free(g->shaped);
        g->shaped   = NULL;
        g->shaped_n = 0;
        return 0;
    }
    if (g->shaped_n != n)
    {
        uint8_t *m = (uint8_t *)realloc(g->shaped, (size_t)n * (size_t)n);
        if (!m)
            return -1;
        g->shaped   = m;
        g->shaped_n = n;
    }
    memcpy(g->shaped, shaped, (size_t)n * (size_t)n);
    return 0;
}

int gpu_set_settle(RGpu *g, const float *state, int32_t per)
{
    if (g->settle_n != per)
    {
        free(g->settle_cpu);
        g->settle_cpu = NULL;
    }
    if (fit_texture(g, &g->settle, &g->settle_n, SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT, per) != 0)
        return -1;
    if (!g->settle_cpu)
        g->settle_cpu = (float *)malloc((size_t)per * (size_t)per * 2u * sizeof *g->settle_cpu);
    if (g->settle_cpu)
        memcpy(g->settle_cpu, state, (size_t)per * (size_t)per * 2u * sizeof *state);
    return upload_texture(g, g->settle, state, per, per, 8);
}

static void gpu_set_light(RGpu *g, float x, float y, float z, float ambient, float diffuse)
{
    g->sun[0]    = x;
    g->sun[1]    = y;
    g->sun[2]    = z;
    g->sun[3]    = ambient;
    g->params[0] = diffuse;
    g->params[1] = g->params[2] = g->params[3] = 0.0f;
}

/*  WHICH RUN OF SPRITES a picture belongs to, in the art set's own
 *  numbering, each by its first and its last.  The first run is the land the mesh replaces, whose last is also
 *  the map edge's dirt column.  The next is the water the water shader
 *  paints inside, with its column.  Then the wireframe lattice of an empty underground tile, a
 *  train, and a zoned lot's own ground.  The meshed view leaves a lot's
 *  ground out, since the lots are drawn as tints (compose.c).
 *  Everything else the terrain pass paints, the data-view tints and the
 *  power markers among it, stays a sprite whatever is switched on. */
enum
{
    AR_LAND0     = 256,
    AR_LAND1     = 269,
    AR_WATER0    = 270,
    AR_WATER1    = 290,
    AR_WATER_COL = 284,
    AR_LATTICE0  = 305,
    AR_LATTICE1  = 318,
    AR_TRAIN0    = 374,
    AR_TRAIN1    = 378,
    AR_LOT0      = 291,
    AR_LOT1      = 299
};

static uint8_t terrain_kind(int32_t tile)
{
    if (tile >= AR_LATTICE0 && tile <= AR_LATTICE1)
        return K_UG_LATTICE;
    if (tile == AR_WATER_COL)
        return K_WATER_COL;
    if (tile >= AR_WATER0 && tile <= AR_WATER1)
        return K_WATER_ART;
    if (tile >= AR_LAND0 && tile <= AR_LAND1)
        return K_LAND_ART;
    if (tile >= AR_LOT0 && tile <= AR_LOT1)
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
                (op->row == r_map - 1 || op->col == r_map - 1))
                g->kind[g->n_inst] = K_WATER_EDGE;
        }
        else
        {
            /*  The underground lattice is emitted as a plain sprite, not
             *  as terrain.  It is the empty tile's art all the same. */
            int32_t sh         = op->shape - l->id_base;
            g->kind[g->n_inst] = (sh >= AR_LATTICE0 && sh <= AR_LATTICE1)            ? K_UG_LATTICE
                                 : (sh >= 0 && sh < 256 && tile_meshed((uint8_t)sh)) ? K_LINE_ART
                                 : (op->stencil >= 0)                                ? K_CAR
                                 : (sh >= AR_TRAIN0 && sh <= AR_TRAIN1)              ? K_TRAIN
                                 : (sh >= 0 && sh < 256 && tile_placed((uint8_t)sh)) ? K_LANDMARK
                                                                                     : K_SPRITE;
        }
        /*  rank holds the op's painter's slot, the tile's altitude in
         *  levels, and whether the art stands.  A building, a tree, a
         *  car, a pole or a bridge stands, and has 1.  Art that lies
         *  flat has 0.  That is the land's slopes, the ground, the
         *  water, the lattice and a silhouette.  It is also a road,
         *  railway or power piece no taller than a tile's diamond.
         *  sprite.vert's depth reads all three.  They are integers, so a
         *  map of any size keeps every slot apart.  The altitude rides
         *  here because misc[1] is a stencilled car's line width. */
        {
            uint8_t kd   = g->kind[g->n_inst];
            int     flat = kd == K_TERRAIN || kd == K_LAND_ART || kd == K_WATER_ART || kd == K_WATER_COL || kd == K_WATER_EDGE || kd == K_UG_LATTICE || kd == K_SHADOW ||
                           (kd == K_LINE_ART && (int32_t)t->h <= (int32_t)l->tile_h + 1);
            in->misc[0]  = 0.0f;
            in->rank[0]  = (int32_t)op->order;
            in->rank[1]  = (int32_t)op->alt;
            in->rank[2]  = flat ? 0 : 1;
            in->rank[3]  = 0;
        }
        {
            /*  Debug: --gpu-dump row,col prints a tile's instances. */
            static int dr = -2, dc = -2;
            if (dr == -2)
            {
                const char *e = g_dev.gpu_dump_at;
                dr = dc = -1;
                if (e)
                    sscanf(e, "%d,%d", &dc, &dr);
            }
            if (op->row == dr && op->col == dc)
                dumpf("inst %u: tile r%d c%d shape %d kind %d dst (%d,%d %dx%d) "
                      "src (%d,%d flip %d stencil %d) alt %d standing %d order %u\n",
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
                      (int)in->rank[1],
                      (int)in->rank[2],
                      (unsigned)op->order);
        }
        g->n_inst++;
    }
    return 0;
}

/* ---- the mesh in slots --------------------------------------------------- */

#define GPU_SLOTS ((MESH_RANGES + 1) * MESH_CHUNKS)

/*  A slot's room for a range: an eighth over and a floor.  So a chunk
 *  can grow a little before the buffers are laid out again.  None under
 *  --gpu-tight, which lays them out at every upload to exercise that. */
static uint32_t slot_room(uint32_t n)
{
    return g_dev.gpu_tight ? n : n + n / 8u + 256u;
}

/*  Did this build change the chunk slot k belongs to? */
static int slot_changed(const RMesh *m, int k)
{
    return m->chunk_changed[k < MESH_RANGES * MESH_CHUNKS ? k / MESH_RANGES : k - MESH_RANGES * MESH_CHUNKS];
}

/*  The mesh's lists in slot order: the opaque lists, then the water's.
 *  Each with the number of its first triangle. */
static void mesh_ranges(const RMesh *m, const RMeshList **list, uint32_t *tbase)
{
    int k;
    for (k = 0; k < MESH_RANGES * MESH_CHUNKS; ++k)
    {
        list[k]  = k < m->n_rl ? &m->rl[k] : NULL;
        tbase[k] = m->range_start[k] / 3u;
    }
    for (k = 0; k < MESH_CHUNKS; ++k)
    {
        list[MESH_RANGES * MESH_CHUNKS + k]  = k < m->n_wl ? &m->wl[k] : NULL;
        tbase[MESH_RANGES * MESH_CHUNKS + k] = 0u;
    }
}

/*  A buffer of the mesh's, `bytes` long, made anew when it is too small.
 *  Its old contents go with it. */
static int mesh_buffer(RGpu *g, SDL_GPUBuffer **b, uint32_t *cap, uint32_t bytes, SDL_GPUBufferUsageFlags usage)
{
    SDL_GPUBufferCreateInfo bi;
    if (bytes <= *cap && *b)
        return 0;
    if (*b)
        SDL_ReleaseGPUBuffer(g->dev, *b);
    memset(&bi, 0, sizeof bi);
    bi.usage = usage;
    bi.size  = bytes ? bytes : 4u;
    *b       = SDL_CreateGPUBuffer(g->dev, &bi);
    *cap     = *b ? bytes : 0u;
    return *b ? 0 : -1;
}

/*  Every slot laid out afresh, in order, each with its room.  A slot
 *  holds its distinct vertices in the vertex buffer and its corners in
 *  the index buffer.  It holds a key a triangle in the key buffer, which
 *  runs beside the index buffer a third as long.  Every slot is uploaded
 *  after this.  The buffers hold a quarter more than the slots: a slot
 *  that outgrows its room moves into that free room at the end. */
static int slots_lay(RGpu *g, const uint32_t *verts, const uint32_t *corners)
{
    uint32_t vpos = 0, ipos = 0;
    int      k;
    for (k = 0; k < GPU_SLOTS; ++k)
    {
        g->slot[k].vfirst = vpos;
        g->slot[k].vcap   = slot_room(verts[k]);
        g->slot[k].ifirst = ipos;
        g->slot[k].icap   = 3u * (slot_room(corners[k]) / 3u + 1u);
        vpos += g->slot[k].vcap;
        ipos += g->slot[k].icap;
    }
    g->slotted = 0;
    g->vtop = vpos, g->itop = ipos;
    if (!g_dev.gpu_tight)
        vpos += vpos / 4u, ipos += 3u * (ipos / 12u);
    if (mesh_buffer(g, &g->mbuf, &g->mbuf_cap, vpos * (uint32_t)sizeof(RMeshVert), SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ) != 0 ||
        mesh_buffer(g, &g->mibuf, &g->mibuf_cap, ipos * 4u, SDL_GPU_BUFFERUSAGE_INDEX | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ) != 0 ||
        mesh_buffer(g, &g->mkbuf, &g->mkbuf_cap, ipos / 3u * 4u, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ) != 0 ||
        mesh_buffer(g, &g->mtbuf, &g->mtbuf_cap, ipos / 3u * 4u, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ) != 0)
        return -1;
    g->slotted = 1;
    if (g_dev.times)
        dumpf("time  gpu mesh buffers: vertices %.1f MB, corners %.1f MB, keys %.1f MB\n", (double)g->mbuf_cap / 1e6, (double)g->mibuf_cap / 1e6, (double)g->mkbuf_cap / 1e6);
    return 0;
}

/*  What a slot sends up, packed end to end in a stage.  That is its
 *  distinct vertices, its corners as places in the vertex buffer, and
 *  its keys. */
typedef struct
{
    uint8_t               *map;
    uint32_t               at, cap; /* bytes */
    SDL_GPUTransferBuffer *tb;
    SDL_GPUCopyPass       *cp;
    SDL_GPUCommandBuffer  *cmd;
} Stage;

/*  The most a stage holds, about 64 MB.  A slot larger than that has a
 *  stage of its own size. */
#define STAGE_BYTES (64u * 1024u * 1024u)

static int stage_open(RGpu *g, Stage *s, uint32_t bytes)
{
    s->cap = bytes > STAGE_BYTES ? bytes : STAGE_BYTES;
    s->at  = 0;
    s->tb  = make_transfer(g, 0, s->cap);
    if (!s->tb)
        return -1;
    s->map = (uint8_t *)SDL_MapGPUTransferBuffer(g->dev, s->tb, false);
    if (!s->map)
    {
        SDL_ReleaseGPUTransferBuffer(g->dev, s->tb);
        s->tb = NULL;
        return -1;
    }
    s->cmd = SDL_AcquireGPUCommandBuffer(g->dev);
    s->cp  = SDL_BeginGPUCopyPass(s->cmd);
    return 0;
}

/*  The stage's copies are recorded as it fills.  Closing it unmaps it and
 *  submits them.  There is no wait for the device: the frames after this
 *  draw what it copies. */
static void stage_close(RGpu *g, Stage *s)
{
    if (!s->tb)
        return;
    SDL_UnmapGPUTransferBuffer(g->dev, s->tb);
    SDL_EndGPUCopyPass(s->cp);
    SDL_SubmitGPUCommandBuffer(s->cmd);
    SDL_ReleaseGPUTransferBuffer(g->dev, s->tb);
    s->tb = NULL;
}

/*  Room in the stage for `bytes` more.  A full stage goes up and a new
 *  one opens. */
static uint8_t *stage_take(RGpu *g, Stage *s, uint32_t bytes)
{
    uint8_t *p;
    if (s->tb && s->at + bytes > s->cap)
        stage_close(g, s);
    if (!s->tb && stage_open(g, s, bytes) != 0)
        return NULL;
    p = s->map + s->at;
    return p;
}

static void stage_copy(Stage *s, SDL_GPUBuffer *to, uint32_t offset, uint32_t bytes)
{
    SDL_GPUTransferBufferLocation src;
    SDL_GPUBufferRegion           dst;
    if (!bytes)
        return;
    memset(&src, 0, sizeof src);
    memset(&dst, 0, sizeof dst);
    src.transfer_buffer = s->tb;
    src.offset          = s->at;
    dst.buffer          = to;
    dst.offset          = offset;
    dst.size            = bytes;
    SDL_UploadToGPUBuffer(s->cp, &src, &dst, false);
    s->at += bytes;
}

int gpu_set_mesh(RGpu *g, const RMesh *m, RGpuTriKey key)
{
    Uint64           t0 = SDL_GetTicksNS();
    uint32_t         tbase[GPU_SLOTS], verts[GPU_SLOTS], corners[GPU_SLOTS], n = 0, up_n = 0, up_v = 0, held = 0;
    const RMeshList *list[GPU_SLOTS];
    uint8_t          grown[GPU_SLOTS];
    Stage            st;
    int              k, lay = !g->slotted || g_dev.gpu_tight, rc = 0;
    g->mesh_n = 0;
    if (!m->ranged || m->n_land + m->n_water == 0)
    {
        g->slotted = 0; /* nothing, or a mesh without ranges: nothing to draw */
        return 0;
    }
    mesh_ranges(m, list, tbase);
    /*  The device draws packed lists alone.  A list the build could not
     *  pack is left out. */
    for (k = 0; k < GPU_SLOTS; ++k)
    {
        const RMeshList *l  = list[k];
        int              ok = l && l->idx;
        if (l && l->n && !l->idx)
            R_WARN("gpu", "range %d is not packed, so it is not drawn", k);
        verts[k]   = ok ? l->nv : 0u;
        corners[k] = 0u;
        if (ok)
        {
            int L;
            for (L = 0; L < MESH_LOD_LEVELS; ++L)
                corners[k] += L && l->lod[L] ? l->lod_n[L] : l->n;
        }
        grown[k] = slot_changed(m, k) && (verts[k] > g->slot[k].vcap || corners[k] > g->slot[k].icap);
    }
    /*  A slot that outgrew its room moves to the free room at the end of
     *  the buffers.  Only when there is not enough is every slot laid out
     *  afresh. */
    if (!lay)
    {
        uint32_t vt = g->vtop, it = g->itop;
        for (k = 0; k < GPU_SLOTS; ++k)
            if (grown[k])
                vt += slot_room(verts[k]), it += 3u * (slot_room(corners[k]) / 3u + 1u);
        if ((uint64_t)vt * sizeof(RMeshVert) > g->mbuf_cap || (uint64_t)it * 4u > g->mibuf_cap || (uint64_t)it / 3u * 4u > g->mkbuf_cap)
            lay = 1;
        else
            for (k = 0; k < GPU_SLOTS; ++k)
                if (grown[k])
                {
                    g->slot[k].vfirst = g->vtop, g->slot[k].vcap = slot_room(verts[k]);
                    g->slot[k].ifirst = g->itop, g->slot[k].icap = 3u * (slot_room(corners[k]) / 3u + 1u);
                    g->vtop += g->slot[k].vcap, g->itop += g->slot[k].icap;
                }
    }
    /*  A chunk no build has laid yet is given room for 1.5 times the
     *  mean of the laid chunks of its kind.  The kinds are terrain,
     *  network and water.  So the blocks built after it usually fit, and
     *  the buffers are not laid out again for each. */
    if (lay)
    {
        uint32_t want_v[GPU_SLOTS], want_c[GPU_SLOTS];
        uint64_t sum_v[MESH_RANGES + 1] = {0}, sum_c[MESH_RANGES + 1] = {0};
        uint32_t n_laid[MESH_RANGES + 1] = {0};
        for (k = 0; k < GPU_SLOTS; ++k)
        {
            int ch = k < MESH_RANGES * MESH_CHUNKS ? k / MESH_RANGES : k - MESH_RANGES * MESH_CHUNKS, kind = k < MESH_RANGES * MESH_CHUNKS ? k % MESH_RANGES : MESH_RANGES;
            want_v[k] = verts[k], want_c[k] = corners[k];
            if (m->chunk_laid[ch])
                sum_v[kind] += verts[k], sum_c[kind] += corners[k], ++n_laid[kind];
        }
        for (k = 0; k < GPU_SLOTS; ++k)
        {
            int ch = k < MESH_RANGES * MESH_CHUNKS ? k / MESH_RANGES : k - MESH_RANGES * MESH_CHUNKS, kind = k < MESH_RANGES * MESH_CHUNKS ? k % MESH_RANGES : MESH_RANGES;
            if (!m->chunk_laid[ch] && n_laid[kind])
            {
                uint32_t ev = (uint32_t)(sum_v[kind] * 3u / (2u * n_laid[kind])), ec = (uint32_t)(sum_c[kind] * 3u / (2u * n_laid[kind]));
                want_v[k] = want_v[k] > ev ? want_v[k] : ev;
                want_c[k] = want_c[k] > ec ? want_c[k] : ec;
            }
        }
        if (slots_lay(g, want_v, want_c) != 0)
            rc = -1;
    }
    memset(&st, 0, sizeof st);
    for (k = 0; k < GPU_SLOTS && rc == 0; ++k)
    {
        GpuSlot         *sl = &g->slot[k];
        const RMeshList *l  = list[k];
        uint32_t         vb, ib, kb, i, t, n0;
        uint8_t         *p;
        if (!lay && !slot_changed(m, k))
        {
            n += sl->lcount[0];
            held += sl->vcount;
            continue;
        }
        vb = verts[k] * (uint32_t)sizeof(RMeshVert);
        ib = corners[k] * 4u;
        kb = corners[k] / 3u * 4u;
        n0 = l && l->idx ? l->n : 0u;
        p  = stage_take(g, &st, vb + ib + 2u * kb);
        if (!p)
        {
            rc = -1;
            break;
        }
        /*  Each written once, never read back: the stage is
         *  write-combined. */
        if (vb)
            memcpy(p, l->v, vb);
        stage_copy(&st, g->mbuf, sl->vfirst * (uint32_t)sizeof(RMeshVert), vb);
        {
            /*  Every level's corners end to end, level 0 first. */
            uint32_t *ix = (uint32_t *)(st.map + st.at), at = 0;
            int       L;
            for (L = 0; L < MESH_LOD_LEVELS; ++L)
            {
                const uint32_t *src = L && l->lod[L] ? l->lod[L] : l->idx;
                uint32_t        cn  = L && l->lod[L] ? l->lod_n[L] : n0;
                sl->lfirst[L]       = sl->ifirst + at;
                sl->lcount[L]       = cn;
                for (i = 0; i < cn; ++i)
                    ix[at + i] = sl->vfirst + src[i];
                at += cn;
            }
        }
        stage_copy(&st, g->mibuf, sl->ifirst * 4u, ib);
        {
            /*  The edit show's key for each triangle of level 0
             *  (show.h).  The water has no pieces, and the coarser levels
             *  are never shown going. */
            uint32_t *kx = (uint32_t *)(st.map + st.at);
            for (t = 0; t < corners[k] / 3u; ++t)
                kx[t] = !key || k >= MESH_RANGES * MESH_CHUNKS || t >= n0 / 3u ? SHOW_KEY_NONE : key(tbase[k] + t, l->comp[t]);
        }
        stage_copy(&st, g->mkbuf, sl->ifirst / 3u * 4u, kb);
        {
            /*  The tile each triangle of level 0 belongs to, which a mark
             *  lights (tile_marks.glsl).  The coarser levels belong to none,
             *  and a marked frame draws level 0. */
            uint32_t *tx = (uint32_t *)(st.map + st.at);
            for (t = 0; t < corners[k] / 3u; ++t)
                tx[t] = t < n0 / 3u && l->tile && l->tile[t] >= 0 ? (uint32_t)l->tile[t] + 1u : 0u;
        }
        stage_copy(&st, g->mtbuf, sl->ifirst / 3u * 4u, kb);
        sl->vcount = verts[k];
        sl->icount = corners[k];
        n += n0;
        held += verts[k];
        up_n += corners[k];
        up_v += verts[k];
    }
    stage_close(g, &st);
    if (rc != 0)
    {
        g->slotted = 0;
        return -1;
    }
    g->mesh_n = n;
    ++g->mesh_gen;
    if (g_dev.times)
        dumpf("time  gpu_set_mesh %u tris, %u distinct vertices held; %.1f MB uploaded%s: %.1f ms\n",
              n / 3u,
              held,
              ((double)up_v * sizeof(RMeshVert) + (double)up_n * (4.0 + 4.0 / 3.0)) / 1e6,
              lay ? ", laid out afresh" : "",
              (double)(SDL_GetTicksNS() - t0) / 1e6);
    return 0;
}

int gpu_show_rekey(RGpu *g, const uint32_t *keys, const uint32_t at[SHOW_SLOTS], const uint32_t n[SHOW_SLOTS])
{
    Stage    st;
    uint32_t nk = 0, ns = 0, ng = 0, i;
    int      k;
    if (!keys || !g->slotted)
        return 0;
    memset(&st, 0, sizeof st);
    /*  A slot is keyed again only where it holds those triangles still:
     *  the same count as the last world put there.  Only the keys go up:
     *  the triangles stand. */
    for (k = 0; k < MESH_RANGES * MESH_CHUNKS && k < SHOW_SLOTS; ++k)
    {
        uint8_t *p;
        if (!n[k])
            continue;
        if (3u * n[k] != g->slot[k].lcount[0])
        {
            ++ns;
            continue;
        }
        p = stage_take(g, &st, 4u * n[k]);
        if (!p)
            return -1;
        memcpy(p, keys + at[k], 4u * (size_t)n[k]);
        for (i = 0; i < n[k]; ++i)
            ng += keys[at[k] + i] == SHOW_KEY_GONE;
        stage_copy(&st, g->mkbuf, g->slot[k].ifirst / 3u * 4u, 4u * n[k]);
        ++nk;
    }
    stage_close(g, &st);
    R_DBG("show", "rekey: %u slots, %u gone triangles, %u slots skipped", nk, ng, ns);
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

/*  The format an offscreen frame is drawn into.  It is the one every
 *  target of ours is made in, whatever device is in hand. */
int gpu_offscreen_format(void)
{
    return (int)GPU_COLOR_FMT;
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
