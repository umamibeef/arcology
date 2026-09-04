/*  gpu.c -- see gpu.h. */
#include "gpu.h"
#include "log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "shaders.h"

/*  Canvas pixels to screen pixels: the integer pixel scale times the
 *  continuous zoom.  Defined with the frame. */
static float view_factor(const RGpuView *v);

/*  One instance = one op.  The layout is the vertex input state below and
 *  the attributes in sprite.vert, in that order.  `under` is the road a
 *  car is stencilled onto for a stencil op, and the tile's diamond origin
 *  and grid position for every other op, which the water shader reads. */
typedef struct
{
    int32_t dst[4];   /* canvas x, y, w, h                     */
    int32_t src[4];   /* atlas x, y, flip, stencil             */
    int32_t under[4]; /* road atlas x, y, canvas x, y          */
    float   misc[4];  /* depth, road w, road flip, road h      */
} RInst;

enum
{
    K_TERRAIN    = 0,  /* ground the sprites stand on: depth write        */
    K_SPRITE     = 1,  /* everything else: depth test                     */
    K_SHADOW     = 2,  /* $19B76 silhouettes, into the mask               */
    K_LAND_ART   = 3,  /* land art the terrain mesh replaces              */
    K_WATER_ART  = 4,  /* water and shore art the water shader paints in  */
    K_WATER_COL  = 5,  /* the water column stacked at the map edge, 284    */
    K_UG_LATTICE = 7,  /* the underground view's empty-tile lattice, 318..331 */
    K_WATER_EDGE = 8,  /* water art on a cut edge: the mesh draws the surface  */
    K_CAR        = 10, /* a car, $19004's stencilled traffic sprite; off with the road mesh */
    K_TRAIN      = 11, /* a train car, thing types 10 and 11, shapes 374..378; off with the road mesh */
    K_LANDMARK   = 12, /* a structure the player placed, XBLD 0xC6 and up:
                        *  the power plants, the civic buildings, the ports
                        *  and the landmarks.  It is the one kind of sprite
                        *  the map view keeps (the user: "I want special
                        *  buildings to be shown in the map view as well",
                        *  then "You can simply overlay the original
                        *  sprite!"), drawn over the tint on its own tile. */
    K_ROAD_ART   = 9   /* a power, road, rail or crossing piece: XBLD
                        *  0x0E..0x48, one contiguous run.
                        *
                        *  This kind is not a label, it is the
                        *  SUPPRESSION LIST.  With the mesh on, a
                        *  K_ROAD_ART sprite is dropped because the
                        *  geometry stands in for it.  An id outside the
                        *  range keeps its sprite, which is then drawn
                        *  over the mesh at the sprite's own height -- a
                        *  piece of track hanging in the air above a
                        *  surface that is already there.
                        *
                        *  So the range must cover EXACTLY what the mesh
                        *  draws as a strip.  It used to stop at 0x3A
                        *  and skip 0x3B..0x42, and rail floated (the
                        *  user, Toronto column 110 row 101).  Widening
                        *  it, I wrote 0x0E..0x42 and dropped the
                        *  crossings, and 0x44 at 112,91 and 0x45 at
                        *  111,93 floated within the hour.
                        *
                        *  It now stops at 0x50, not 0x48: the eight
                        *  highway DECK ids are meshed as of the Part 7
                        *  work, so their sprites go the same way the
                        *  road's do.  A second run, 0x61..0x68, joins
                        *  them: the four RAMP ids and the four CURVE
                        *  blocks, which the band walk lofts as part of
                        *  the deck (the user: "the sprites are still
                        *  showing on top of it").  What lies between,
                        *  0x51..0x60, is still sprites -- the long
                        *  inclines, the bridges and the corner fills
                        *  beside a curve, none of them meshed yet --
                        *  and must stay outside these ranges until
                        *  something draws them.  The rule is the same as it
                        *  always was: the range is exactly what the
                        *  mesh draws, no more and no less, and
                        *  --mesh-only is how you check. */
};

struct RGpu
{
    SDL_GPUDevice       *dev;
    SDL_Window          *win;
    SDL_GPUShaderFormat  fmt;
    SDL_GPUTextureFormat swap_fmt, depth_fmt, color_fmt;

    SDL_GPUGraphicsPipeline *pipe_terrain, *pipe_sprite, *pipe_shadow;
    SDL_GPUGraphicsPipeline *pipe_mesh, *pipe_mesh_blend, *pipe_water, *pipe_resolve,
        *pipe_resolve_off;

    SDL_GPUTexture *atlas[R_MAX_LEVELS];
    int32_t         atlas_transparent[R_MAX_LEVELS];
    int32_t         atlas_zoom[R_MAX_LEVELS];
    int32_t         n_atlas;
    SDL_GPUTexture *pal;
    SDL_GPUTexture *shore;
    SDL_GPUSampler *nearest, *linear;

    SDL_GPUTexture *color, *shadow, *depth, *offscreen;
    int32_t         tw, th;
    int32_t         ow, oh; /* the offscreen's size */
    float           factor; /* canvas pixels to target pixels, this frame */

    SDL_GPUBuffer         *ibuf;
    SDL_GPUTransferBuffer *itb;
    uint32_t               ibuf_cap; /* instances */
    SDL_GPUBuffer         *mbuf;
    uint32_t               mbuf_cap, mesh_n, mesh_n_opaque;
    SDL_GPUBuffer         *mvbuf; /* the movers */
    uint32_t               mvbuf_cap, movers_n;
    SDL_GPUTransferBuffer *ptb; /* palette */
    SDL_GPUTransferBuffer *dtb; /* download */
    uint32_t               dtb_cap;

    RInst   *inst;
    uint8_t *kind;
    uint32_t n_inst, inst_cap;
    RInst   *vis;
    uint32_t vis_cap;
    uint32_t last_drawn, last_culled;

    int32_t level;       /* index into atlas[] */
    int32_t transparent; /* that level's reserved index */
    RSweep  sw;
    uint8_t palette[256][4];
    int     pal_dirty;
    uint8_t bg_index;

    float sun[4];    /* x, y, z, ambient */
    float params[4]; /* diffuse */
};

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
        /*  Over the frame by the source's alpha; the target's alpha is
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

static SDL_GPUTransferBuffer *make_transfer(RGpu *g, int download, uint32_t bytes)
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

static int ensure_targets(RGpu *g, int32_t w, int32_t h)
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
static int ensure_offscreen(RGpu *g, int32_t w, int32_t h)
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

static int ensure_instances(RGpu *g, uint32_t n)
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

    /*  Pipelines: the terrain writes depth, the sprites test it, the
     *  shadows test it into the mask, the water is terrain with its own
     *  fragment shader, the mesh is geometry, the resolve needs none. */
    g->pipe_terrain = make_pipe(g, "sprite.vert", "sprite.frag", g->color_fmt, 1, 1, L_INSTANCE, 0);
    g->pipe_sprite  = make_pipe(g, "sprite.vert", "sprite.frag", g->color_fmt, 1, 0, L_INSTANCE, 0);
    g->pipe_shadow  = make_pipe(g, "sprite.vert", "shadow.frag", SDL_GPU_TEXTUREFORMAT_R8_UNORM, 1, 0, L_INSTANCE, 0);
    g->pipe_water   = make_pipe(g, "sprite.vert", "sprite_water.frag", g->color_fmt, 1, 1, L_INSTANCE, 0);
    g->pipe_mesh    = make_pipe(g, "terrain.vert", "terrain.frag", g->color_fmt, 1, 1, L_MESH, 0);
    /*  The water column faces of the map edge's cut: blended over the
     *  seabed behind them, tested against depth but not writing it. */
    g->pipe_mesh_blend  = make_pipe(g, "terrain.vert", "terrain.frag", g->color_fmt, 1, 0, L_MESH, 1);
    g->pipe_resolve     = make_pipe(g, "resolve.vert", "resolve.frag", g->swap_fmt, 0, 0, L_NONE, 0);
    g->pipe_resolve_off = make_pipe(g, "resolve.vert", "resolve.frag", g->color_fmt, 0, 0, L_NONE, 0);
    if (!g->pipe_terrain || !g->pipe_sprite || !g->pipe_shadow ||
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
     *  set.  Nothing is resolved to colour on the CPU. */
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
    /*  The sun, fitted to the slope sprites: of the four plain slopes at
     *  32 px the one facing south (code 1) is the brightest at a mean
     *  luminance of 135, then east 88, north 84 and west 71, so the light
     *  comes from the south-east, over the viewer's shoulder. */
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
 *  shore sprites those are 79, 130, 184..188 and 192..195; the beach is
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
 *  column), which the mesh replaces; and the water art 270..290 -- open
 *  water, the shore shapes, the water column 284 and the channel pieces
 *  285..290 that XTER 0x40..0x45 draw -- which the water shader paints
 *  inside.  Everything else the terrain pass paints -- lot tints,
 *  data-view tints, the power markers -- stays a sprite whatever is
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
    float              depth_div = (float)(2 * R_MAP * R_MAP + 2);

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
        /*  The depth slot of a tile is one unit of order; a sprite sits at
         *  the top of its tile's slot and the mesh's fragments below it by
         *  height, so within a tile a sprite is nearer than the ground it
         *  stands on and the water surface meets the ground where the
         *  heights cross. */
        in->misc[0] = 1.0f - ((float)op->order + 0.999f) / depth_div;
        /*  misc[1] is the road's width for a stencilled car and, for
         *  every other op, the tile's altitude: what sprite.vert needs to
         *  put the sprite back where its tile went when the camera is
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
             *  as terrain; it is the empty tile's art all the same. */
            int32_t sh         = op->shape - l->id_base;
            g->kind[g->n_inst] = (sh >= 305 && sh <= 318)     ? K_UG_LATTICE
                                 : ((sh >= 0x0E && sh <= 0x50) || (sh >= 0x61 && sh <= 0x68)) ? K_ROAD_ART
                                 : (op->stencil >= 0)         ? K_CAR
                                 : (sh >= 374 && sh <= 378)   ? K_TRAIN
                                 : (sh >= 0xC6 && sh <= 0xFF) ? K_LANDMARK
                                                              : K_SPRITE;
        }
        {
            /*  Debug: SC2K_GPU_DUMP=row,col prints a tile's instances. */
            static int dr = -2, dc = -2;
            if (dr == -2)
            {
                const char *e = getenv("SC2K_GPU_DUMP");
                dr = dc = -1;
                if (e)
                    sscanf(e, "%d,%d", &dr, &dc);
            }
            if (op->row == dr && op->col == dc)
                printf("inst %u: tile r%d c%d shape %d kind %d dst (%d,%d %dx%d) "
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

int gpu_set_mesh(RGpu *g, const RMeshVert *v, uint32_t n, const RMeshVert *w, uint32_t nw)
{
    uint32_t n_opaque = n;
    uint32_t bytes;
    n += nw;
    bytes = n * (uint32_t)sizeof(RMeshVert);
    SDL_GPUTransferBuffer        *tb;
    SDL_GPUCommandBuffer         *cmd;
    SDL_GPUCopyPass              *cp;
    SDL_GPUTransferBufferLocation src;
    SDL_GPUBufferRegion           dst;
    void                         *map;

    g->mesh_n = 0;
    if (!n)
        return 0;
    if (n > g->mbuf_cap)
    {
        if (g->mbuf)
            SDL_ReleaseGPUBuffer(g->dev, g->mbuf);
        g->mbuf = make_buffer(g, SDL_GPU_BUFFERUSAGE_VERTEX, bytes);
        if (!g->mbuf)
        {
            g->mbuf_cap = 0;
            return -1;
        }
        g->mbuf_cap = n;
    }
    tb = make_transfer(g, 0, bytes);
    if (!tb)
        return -1;
    map = SDL_MapGPUTransferBuffer(g->dev, tb, false);
    if (!map)
    {
        SDL_ReleaseGPUTransferBuffer(g->dev, tb);
        return -1;
    }
    memcpy(map, v, n_opaque * (uint32_t)sizeof(RMeshVert));
    if (nw)
        memcpy((uint8_t *)map + n_opaque * sizeof(RMeshVert), w, nw * sizeof(RMeshVert));
    SDL_UnmapGPUTransferBuffer(g->dev, tb);
    memset(&src, 0, sizeof src);
    memset(&dst, 0, sizeof dst);
    src.transfer_buffer = tb;
    dst.buffer          = g->mbuf;
    dst.size            = bytes;
    cmd                 = SDL_AcquireGPUCommandBuffer(g->dev);
    cp                  = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUBuffer(cp, &src, &dst, true);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_WaitForGPUIdle(g->dev);
    SDL_ReleaseGPUTransferBuffer(g->dev, tb);
    g->mesh_n        = n;
    g->mesh_n_opaque = n_opaque;
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

/* ---- drawing ---------------------------------------------------------- */

/*  The vertex uniform of every pipeline that places things on the
 *  canvas, sprites and mesh alike: the scroll and the scale, the tile
 *  projection, the altitude step and the free rotation.  The sprite
 *  shader reads the rotation too, to move each sprite with its tile. */
typedef struct
{
    float view[4]; /* scroll x, scroll y, 2*scale/target_w, 2*scale/target_h */
    float proj[4]; /* ox + tw/2, oy - th - 0.5, tw/2, th/2                   */
    float alt[4];  /* alt_step, depth divisor, cos, sin of the rotation      */
    float rot[4];  /* free camera (1 or 0), pivot column, pivot row, pitch, radians */
} CamU;

/*  The fragment uniform of every sprite pipeline.  sprite.frag and
 *  shadow.frag declare only the first member; sprite_water.frag reads all
 *  three.  One struct is pushed for all of them. */
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
    return v->pitch > 0.0f ? v->pitch : 30.0f;
}

/*  SC2K_NO_SHADOW=1: draw no silhouettes, so a frame at the snap and a
 *  frame a hair off it differ only in how they order what is left. */
static int g_no_shadow = -1;

/*  Is the camera off the game's own view -- turned, or raised? */
static int cam_free(const RGpuView *v)
{
    return v->angle != 0.0f || fabsf(view_pitch(v) - 30.0f) > 0.01f;
}

/*  A sprite's distance from a turned camera, in tiles along the way it
 *  looks.  It is the same number sprite.vert computes, and it exists
 *  twice for a reason: the sprite pass TESTS the depth buffer and never
 *  WRITES it, so no depth a sprite carries can order it against another
 *  sprite.  Art orders itself by the order it is drawn in, and that
 *  order is the sweep's, which is the unturned view's (the user: "it
 *  seems like you're using the depth from the main view and not
 *  calculating a new one").  Turned, the sprites have to be sorted
 *  again, farthest first, and this is the key. */
typedef struct
{
    float  k;
    uint32_t i;
    RInst  in;
} SortInst;

static float sprite_key(const RInst *in, const RGpuView *v, const RAtlasLevel *l)
{
    float ang = v->angle * 3.14159265f / 180.0f;
    float ca = cosf(ang), sa = sinf(ang);
    float pt = view_pitch(v) * 3.14159265f / 180.0f;
    float sp = sinf(pt), cp = cosf(pt);
    float nf = l && l->tile_w ? floorf((float)in->dst[2] / (float)l->tile_w + 0.5f) : 1.0f;
    float half, fx, fy, dx, dy, tx, ty, front;
    if (nf < 1.0f)
        nf = 1.0f;
    half = (nf - 1.0f) * 0.5f;
    /*  The middle of the footprint: the art is anchored at the block's
     *  leftmost tile, the greatest column and the least row. */
    fx = (float)in->under[2] + 0.5f - half;
    fy = (float)in->under[3] + 0.5f + half;
    dx = fx - v->pivot_c;
    dy = fy - v->pivot_r;
    tx = dx * ca - dy * sa + v->pivot_c;
    ty = dx * sa + dy * ca + v->pivot_r;
    front = half * (fabsf(ca + sa) + fabsf(ca - sa));
    return (tx + ty + front) * (cp / 0.8660254f) + in->misc[1] * (sp - 0.5f) +
           (ty - tx) * 0.0015f;
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
    static int off = -1;
    if (off < 0)
        off = getenv("SC2K_NO_SORT") != NULL; /* draw in the sweep's order, to show what it costs */
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
    memset(r, 0, sizeof *r);
    for (pass = 0; pass < 4; ++pass)
    {
        uint32_t start = n;
        if (g_no_shadow < 0)
            g_no_shadow = getenv("SC2K_NO_SHADOW") != NULL;
        for (k = 0; k < g->n_inst; ++k)
        {
            const RInst *in   = &g->inst[k];
            uint8_t      kind = g->kind[k];
            int          want;
            /*  Turned, the sprites still draw, each moved with its tile
             *  and unturned, as the original shows the same art at every
             *  one of its rotations; only the silhouettes, cast on the
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
                              ((kind == K_ROAD_ART || kind == K_CAR || kind == K_TRAIN) && !v->geometry));
            else
                /*  A silhouette is cast on the unturned canvas and its
                 *  depth is the sweep's own slot, so it is right at the
                 *  snap and nowhere else.  SC2K_NO_SHADOW drops them at
                 *  the snap too, which is how the two depth models are
                 *  measured against each other. */
                want = (kind == K_SHADOW) && v->angle == 0.0f && !g_no_shadow;
            if (!want)
                continue;
            if (v->angle == 0.0f &&
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
            if (cam_free(v) && sort_sprites(g, &g->vis[start], r->n_sprite, v, g->sw.level) != 0)
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
    /*  The canvas is drawn at the target's own resolution: a canvas pixel
     *  is view_factor() target pixels, so sprites magnify by nearest and
     *  the mesh and the water shade every target pixel. */
    u->view[2] = 2.0f * g->factor / (float)g->tw;
    u->view[3] = 2.0f * g->factor / (float)g->th;
    /*  A ground sprite's rise is its full height, so its diamond
     *  occupies the th + 1 rows ABOVE the tile's origin row: the top
     *  vertex at sy - th - 1, the bottom row at sy - 1.  The mesh
     *  rhombus is th tall and is centred on that hexagon, which is
     *  one pixel inside the sprite on the top and bottom rows, the
     *  residual tools/terrain_shapes.py records. */
    u->proj[0] = l ? (float)(g->sw.ox + l->tile_w / 2) : 0.0f;
    u->proj[1] = l ? (float)g->sw.oy - ((float)l->tile_h + 0.5f) : 0.0f;
    u->proj[2] = l ? (float)(l->tile_w / 2) : 16.0f;
    u->proj[3] = l ? (float)(l->tile_h / 2) : 8.0f;
    u->alt[0]  = l ? (float)l->alt_step : 12.0f;
    u->alt[1]  = (float)(2 * R_MAP * R_MAP + 2); /* the depth divisor */
    u->alt[2]  = cosf(v->angle * 3.14159265f / 180.0f);
    u->alt[3]  = sinf(v->angle * 3.14159265f / 180.0f);
    /*  The free camera: turned off the snap, or raised off the game's own
     *  pitch.  Either way the painter's slot stops ordering the tiles. */
    u->rot[0]  = (v->angle != 0.0f || fabsf(view_pitch(v) - 30.0f) > 0.01f) ? 1.0f : 0.0f;
    u->rot[1]  = v->pivot_c;
    u->rot[2]  = v->pivot_r;
    u->rot[3]  = view_pitch(v) * 3.14159265f / 180.0f;
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
        lu.params[3] = v->underground ? 1.0f : 0.0f;
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
        SDL_DrawGPUPrimitives(rp, g->mesh_n_opaque, 1, 0, 0);
        if (g->movers_n && g->mvbuf)
        {
            SDL_GPUBufferBinding mb;
            memset(&mb, 0, sizeof mb);
            mb.buffer = g->mvbuf;
            SDL_BindGPUVertexBuffers(rp, 0, &mb, 1);
            SDL_DrawGPUPrimitives(rp, g->movers_n, 1, 0, 0);
            SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
        }
        if (g->mesh_n > g->mesh_n_opaque)
        {
            SDL_BindGPUGraphicsPipeline(rp, g->pipe_mesh_blend);
            SDL_BindGPUFragmentSamplers(rp, 0, &fb, 1);
            SDL_PushGPUVertexUniformData(cmd, 0, cam, sizeof *cam);
            SDL_PushGPUFragmentUniformData(cmd, 0, &lu, sizeof lu);
            SDL_DrawGPUPrimitives(rp, g->mesh_n - g->mesh_n_opaque, 1, g->mesh_n_opaque, 0);
        }
    }
}

/*  Draw passes 1 and 2 (the frame target and the shadow mask) and then
 *  resolve to `final`, which is the swapchain texture or the offscreen
 *  one.  `sw`/`sh` are the final target's size in pixels. */
static int draw_frame(RGpu *g, SDL_GPUCommandBuffer *cmd, const RGpuView *v, SDL_GPUTexture *final, SDL_GPUGraphicsPipeline *resolve_pipe, int32_t sw, int32_t sh, float scale)
{
    Ranges                        r;
    SDL_GPUCopyPass              *cp;
    SDL_GPURenderPass            *rp;
    SDL_GPUColorTargetInfo        ct;
    SDL_GPUDepthStencilTargetInfo dt;
    SDL_GPUBufferBinding          vb;
    SDL_GPUTextureSamplerBinding  ts[3];
    CamU                          cam;
    FragU                         fu;
    uint32_t                      bytes, off;
    const RAtlasLevel            *l = g->sw.level;

    if (build_visible(g, v, &r) != 0)
        return -1;
    /*  Raised off the game's own pitch the mesh draws alone: the sprites
     *  are drawn for one camera and cannot be looked at from above, where
     *  they would only cover the world they are meant to show.  The
     *  structures the player placed are the exception, and they are kept:
     *  a map wants its landmarks on it, and the original's own art is
     *  what says which is which. */
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
    memset(&fu, 0, sizeof fu);
    fu.p[0]   = g->transparent;
    fu.p[1]   = v->geometry ? 1 : 0;    /* the water sprites drop their rim  */
    /*  and shade their water -- underground there is no sky to reflect. */
    fu.p[2]   = (v->geometry && !v->underground) ? 1 : 0;
    fu.f[0]   = v->time;
    fu.f[1]   = (float)(l->tile_w / 2);
    fu.f[2]   = (float)(l->tile_h / 2);
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

    SDL_PushGPUVertexUniformData(cmd, 0, &cam, sizeof cam);
    SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
    SDL_BindGPUFragmentSamplers(rp, 0, ts, 2);
    off = 0;
    if (r.n_terrain)
    {
        SDL_BindGPUGraphicsPipeline(rp, g->pipe_terrain);
        vb.buffer = g->ibuf;
        vb.offset = off * (uint32_t)sizeof(RInst);
        SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
        SDL_DrawGPUPrimitives(rp, 6, r.n_terrain, 0, 0);
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
        SDL_DrawGPUPrimitives(rp, 6, r.n_water, 0, 0);
        SDL_BindGPUFragmentSamplers(rp, 0, ts, 2);
    }
    off += r.n_water;
    if (r.n_sprite)
    {
        SDL_BindGPUGraphicsPipeline(rp, v->plain_sweep ? g->pipe_terrain : g->pipe_sprite);
        vb.buffer = g->ibuf;
        vb.offset = off * (uint32_t)sizeof(RInst);
        SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
        SDL_DrawGPUPrimitives(rp, 6, r.n_sprite, 0, 0);
    }
    off += r.n_sprite;
    /*  per frame, so it stays behind its own switch rather than
     *  riding on --verbose */
    if (getenv("SC2K_GPU_DEBUG"))
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
        SDL_PushGPUVertexUniformData(cmd, 0, &cam, sizeof cam);
        SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
        SDL_BindGPUFragmentSamplers(rp, 0, ts, 1);
        SDL_DrawGPUPrimitives(rp, 6, r.n_shadow, 0, 0);
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
        ts[0].sampler = g->linear; /* the fractional zoom filters; integer scales fetch texels */
        ts[1].texture = g->shadow;
        ts[2].texture = g->pal;
        ts[2].sampler = g->nearest;
        SDL_BindGPUFragmentSamplers(rp, 0, ts, 3);
        ru.screen[0] = (float)sw;
        ru.screen[1] = (float)sh;
        ru.screen[2] = scale;
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
 *  is cleared to that entry's colour, as the software paints it, and the
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
    /*  The palette snapped to the sky: use the phase-0 colours, which is
     *  what the software does with the palette it was given. */
    snap_background(g, sky);
    if (draw_frame(g, cmd, v, swap, g->pipe_resolve, (int32_t)sw, (int32_t)sh, 1.0f) != 0)
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
    if (draw_frame(g, cmd, v, g->offscreen, g->pipe_resolve_off, w, h, 1.0f) != 0)
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
