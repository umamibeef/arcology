/*  gpu/internal.h: what the device and the frame both need. gpu.c makes
 *  the device and puts data on it.  Gpu/frame.c draws with it.  The file
 *  always knew where that line fell, it carried a forward declaration of
 *  view_factor with the note "Defined with the frame".  This is that
 *  line, made into a file boundary.  Nothing here is public: gpu.h is
 *  what the rest of the renderer sees. */
#ifndef R_GPU_INT_H
#define R_GPU_INT_H

#include "gpu/gpu.h"

#include <SDL3/SDL.h>

/*  One instance = one op.  The layout is the vertex input state below
 *  and the attributes in sprite.vert, in that order.  `under` is the
 *  line a car is stencilled onto for a stencil op.  The tile's diamond
 *  origin and grid position for every other op, which the water shader
 *  reads. */
/*  A slot of the mesh buffers: one chunk range, with room to grow. */
typedef struct
{
    uint32_t vfirst, vcap, vcount;                             /* its distinct vertices: where they start, their room, how many */
    uint32_t ifirst, icap, icount;                             /* its corners in the index buffer, three a triangle, every level of detail end to end.  The key buffer holds a key a triangle beside them */
    uint32_t lfirst[MESH_LOD_LEVELS], lcount[MESH_LOD_LEVELS]; /* each level's corners: level 0 is the whole mesh, the one the edit show reads */
} GpuSlot;

typedef struct
{
    int32_t dst[4];   /* canvas x, y, w, h                     */
    int32_t src[4];   /* atlas x, y, flip, stencil             */
    int32_t under[4]; /* line atlas x, y, canvas x, y          */
    float   misc[4];  /* unused, line w, line flip, line h     */
    int32_t rank[4];  /* painter's slot, altitude in levels, 1 for art that stands, unused */
} RInst;

enum
{
    K_TERRAIN    = 0,  /* ground the sprites stand on: flat art           */
    K_SPRITE     = 1,  /* everything else: standing art, depth written    */
    K_SHADOW     = 2,  /* $19B76 silhouettes, into the mask               */
    K_LAND_ART   = 3,  /* land art the terrain mesh replaces              */
    K_WATER_ART  = 4,  /* water and shore art the water shader paints in  */
    K_WATER_COL  = 5,  /* the water column stacked at the map edge, 284    */
    K_UG_LATTICE = 7,  /* the underground view's empty-tile lattice, 318..331 */
    K_WATER_EDGE = 8,  /* water art on a cut edge: the mesh draws the surface  */
    K_CAR        = 10, /* a car, $19004's stencilled traffic sprite.  Off with the mesh */
    K_TRAIN      = 11, /* a train car, thing types 10 and 11, shapes 374..378.  Off with the mesh */
    K_LANDMARK   = 12, /* a structure the player placed, as tile_placed (mesh/ground_bytes.c) names one: the plants, the civic buildings, the ports and the landmarks.  The one kind of sprite the map view keeps, drawn over the tint on its own tile */
    /*  K_LINE_ART is not a label.  It is the SUPPRESSION LIST.  With the
     *  mesh on, a K_LINE_ART sprite is dropped.  A tile left out keeps its
     *  sprite, drawn over the mesh at the sprite's own height.
     *
     *  Which tiles those are is tile_meshed's, in
     *  mesh/ground_bytes.c beside the other tile tables.  --mesh-only
     *  shows what the mesh has. */
    K_LINE_ART = 9
};

/*  The color format everything is drawn into.  The swapchain's own is
 *  whatever SDL gives us.  This is the one every target and every
 *  pipeline of ours is made in. */
#define GPU_COLOR_FMT SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM

struct RGpu
{
    SDL_GPUDevice       *dev;
    SDL_Window          *win;
    SDL_GPUShaderFormat  fmt;
    SDL_GPUTextureFormat swap_fmt, depth_fmt;

    SDL_GPUGraphicsPipeline *pipe_terrain, *pipe_sprite, *pipe_sprite_depth, *pipe_shadow;
    SDL_GPUGraphicsPipeline *pipe_mesh, *pipe_mesh_blend, *pipe_resolve,
        *pipe_resolve_off;
    /*  The mesh's two again, moving the pieces an edit shows going and
     *  coming (show.h).  Bound only while some piece moves. */
    SDL_GPUGraphicsPipeline *pipe_mesh_show, *pipe_mesh_blend_show;
    /*  The mesh's opaque draw again, lighting the tiles the player's
     *  tool marks, plain and moving.  Bound only while a tile is marked. */
    SDL_GPUGraphicsPipeline *pipe_mesh_mark, *pipe_mesh_blend_mark, *pipe_mesh_settle_mark, *pipe_mesh_blend_settle_mark;
    /*  The mesh's two again, settling each chunk from the wave on the
     *  sheet to its own heights (terrain_settle.vert).  The sheet itself
     *  stands over every chunk not yet built (terrain_sheet.vert).  Bound
     *  only while the land settles. */
    SDL_GPUGraphicsPipeline *pipe_mesh_settle, *pipe_mesh_blend_settle, *pipe_sheet;
    /*  The mesh's opaque draw with each triangle in a color of its own
     *  (terrain_tris.vert): the triangle view, bound only while it is on. */
    SDL_GPUGraphicsPipeline *pipe_mesh_tris;
    SDL_GPUTexture          *settle; /* one texel a chunk, the edge `settle_n`: when it came onto the device, and whether it is built */
    SDL_GPUTexture          *land;   /* one texel a tile, the edge `land_n`: its four corners' heights in levels */
    int32_t                  settle_n, land_n;
    float                   *settle_cpu; /* the same state, for the art that falls onto the ground */
    uint8_t                 *shaped;     /* a byte a tile, the edge `shaped_n`, in the sweep's quarter: 1 where a piece is drawn from its own shape */
    int32_t                  shaped_n;
    /*  The art's three again, lighting the art on marked tiles. */
    SDL_GPUGraphicsPipeline *pipe_terrain_mark, *pipe_sprite_mark, *pipe_sprite_depth_mark;
    SDL_GPUTexture          *marks;       /* one texel a tile, the edge `marks_n`: what the tool marks there */
    SDL_GPUTexture          *marks_sweep; /* the same, in the sweep's quarter, for the art */
    int32_t                  marks_n;
    int                      marks_on; /* some tile is marked */

    SDL_GPUTexture *atlas[R_MAX_LEVELS];
    int32_t         atlas_transparent[R_MAX_LEVELS];
    int32_t         atlas_zoom[R_MAX_LEVELS];
    int32_t         n_atlas;
    SDL_GPUTexture *pal;
    SDL_GPUTexture *shore;
    int32_t         shore_n; /* the tiles a side it was made for */
    SDL_GPUTexture *show;    /* every piece's state as an edit shows it (show.h), by the piece's id */
    SDL_GPUSampler *nearest, *linear;

    SDL_GPUTexture *color, *shadow, *depth, *offscreen;
    /*  The frame as the camera sees it through the air (air.frag), which
     *  the resolve reads in place of `color` while the air pass runs. */
    SDL_GPUTexture          *air;
    SDL_GPUGraphicsPipeline *pipe_air;
    /*  The clouds alone, at one pixel for `cloud_res` of the frame's a
     *  side (air_clouds.frag), which the air pass filters up. */
    SDL_GPUTexture          *clouds;
    int32_t                  cw, ch;
    SDL_GPUGraphicsPipeline *pipe_air_clouds;
    /*  How much of the sky each point of the mesh sees past what stands
     *  round it (air_ao.frag).  One pixel covers `ao_res` of the frame's
     *  a side, and the air pass filters it up. */
    SDL_GPUTexture          *ao;
    int32_t                  aw, ah;
    SDL_GPUGraphicsPipeline *pipe_air_ao;
    /*  The sun's shadow map: the mesh's depth as the sun sees it, over
     *  a square of the map round the view (frame.c, sun_frame).  It is
     *  drawn again only when what it shows changes. */
    SDL_GPUTexture          *sun_map;
    SDL_GPUSampler          *sun_cmp;
    SDL_GPUGraphicsPipeline *pipe_sun;
    int32_t                  sun_n;       /* its texels a side */
    int                      sun_valid;   /* 1 once it is drawn for sun_key */
    float                    sun_m[3][4]; /* the frame it is drawn in, as sun_depth.vert reads it */
    float                    sun_box[5];  /* the square it covers in the sun's frame: low u, low v, high u, high v, its texel in tiles */
    float                    sun_key[6];  /* the sun's way, the level of detail, the mesh and the trees' range it is drawn for */
    uint32_t                 mesh_gen;    /* counts the mesh's uploads */
    /*  The light at the hour (frame.c, sun_clock): the sun's color and the
     *  ambient's against the fitted light, and the art's tint. */
    float           sun_rgb[3], amb_rgb[3], tint[4], exposure;
    int             tinted;
    SDL_GPUTexture *cloud;         /* the clouds' noise, CLOUD_NOISE_N a side (gpu/clouds.h), made on the first frame through the air */
    SDL_GPUTexture *sky_field;     /* the sky's field, SKY_FIELD_N a side (gpu/clouds.h), made with the noise */
    float           sky_stats[2];  /* the mean and spread of the clouds' mean of it, which equalize it */
    float           sky_origin[2]; /* where this run's clouds start in the sky's field, a share of its span each way */
    SDL_GPUTexture *smog;          /* the pollution, one byte a texel, the edge `smog_n` */
    int32_t         smog_n;
    SDL_GPUTexture *laid; /* what the build lays over the ground: the lamps' light and its height, the canopy and its species */
    int32_t         laid_n;
    float           leaf[GPU_LEAF_SPECIES][2][4]; /* the tree species' leaf colors (gpu_set_leaves) */
    float           leaf_rag[GPU_LEAF_SPECIES];
    SDL_GPUSampler *wrap; /* linear, and repeating, for the clouds' noise */
    int32_t         tw, th;
    int32_t         ow, oh; /* the offscreen's size */
    float           factor; /* canvas pixels to target pixels, this frame */

    SDL_GPUBuffer         *ibuf;
    SDL_GPUTransferBuffer *itb;
    uint32_t               ibuf_cap; /* instances */
    /*  The mesh, in slots, one per range.  Chunk k's land range r is at
     *  MESH_RANGES k + r, and its water at MESH_RANGES MESH_CHUNKS + k.  Each
     *  with room to grow, so an edit's build uploads only the ranges it
     *  changed and the rest stay where they are.  The buffer is laid out
     *  again, whole, only when a range outgrows its slot. */
    SDL_GPUBuffer         *mbuf;
    uint32_t               mbuf_cap; /* bytes */
    SDL_GPUBuffer         *mibuf;    /* the corners, places in mbuf */
    uint32_t               mibuf_cap;
    SDL_GPUBuffer         *mkbuf; /* the edit show's key of each triangle (show.h) */
    uint32_t               mkbuf_cap;
    SDL_GPUBuffer         *mtbuf; /* 1 + the tile each triangle belongs to, which a mark on it lights, or 0 */
    uint32_t               mtbuf_cap;
    GpuSlot                slot[(MESH_RANGES + 1) * MESH_CHUNKS_CAP];
    int                    slotted;    /* the slots are laid out and hold the mesh */
    uint32_t               vtop, itop; /* where the free room at the buffers' end begins, in vertices and corners */
    int                    lod_level;  /* the level of detail this frame draws the mesh at */
    uint32_t               mesh_n;     /* corners held at level 0, all slots: nothing to draw when 0 */
    SDL_GPUTransferBuffer *ptb;        /* palette */
    SDL_GPUTransferBuffer *dtb;        /* download */
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

/*  The device's own growable resources.  The frame asks for them by size
 *  at the top of a pass and does not otherwise know how they are made. */
int ensure_targets(RGpu *g, int32_t w, int32_t h);
int ensure_offscreen(RGpu *g, int32_t w, int32_t h);
int ensure_cloud_target(RGpu *g, int32_t w, int32_t h);
int ensure_sun_map(RGpu *g, int32_t n);
int ensure_ao_target(RGpu *g, int32_t w, int32_t h);
int ensure_instances(RGpu *g, uint32_t n);
/*  The air's textures on the device: the clouds' noise, and a clean smog
 *  field until the city gives its own. */
int                    gpu_air_ready(RGpu *g);
SDL_GPUTransferBuffer *make_transfer(RGpu *g, int download, uint32_t bytes);

#endif /* R_GPU_INT_H */
