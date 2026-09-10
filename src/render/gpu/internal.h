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
/*  A slot of the mesh buffer: one chunk range's vertices, with room to grow. */
typedef struct
{
    uint32_t first, cap, count; /* vertices: where it starts, its room, what it holds */
} GpuSlot;

typedef struct
{
    int32_t dst[4];   /* canvas x, y, w, h                     */
    int32_t src[4];   /* atlas x, y, flip, stencil             */
    int32_t under[4]; /* line atlas x, y, canvas x, y          */
    float   misc[4];  /* depth, line w, line flip, line h      */
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
    K_CAR        = 10, /* a car, $19004's stencilled traffic sprite.  Off with the line mesh */
    K_TRAIN      = 11, /* a train car, thing types 10 and 11, shapes 374..378.  Off with the line mesh */
    K_LANDMARK   = 12, /* a structure the player placed, XBLD 0xC6 and up: the plants, the civic buildings, the ports and the landmarks.  The one kind of sprite the map view keeps, drawn over the tint on its own tile */
    /*  K_LINE_ART is not a label.  It is the SUPPRESSION LIST.  With the
     *  mesh on, a K_LINE_ART sprite is dropped because the geometry
     *  stands in for it.  A tile left out keeps its sprite.  This is
     *  then drawn over the mesh at the sprite's own height.  A piece of
     *  thread hanging in the air above a surface that is already there.
     *
     *  Which tiles those are is the SCRIPT'S: `meshed_tiles` in
     *  scripts/ground_tiles.lua, declared beside the other tile tables.
     *  It must name exactly what the mesh draws as a strip.  A piece the
     *  mesh learns to draw is then enabled by editing that table and
     *  nothing else.  --mesh-only checks what the mesh has. */
    K_LINE_ART   = 9
};

struct RGpu
{
    SDL_GPUDevice       *dev;
    SDL_Window          *win;
    SDL_GPUShaderFormat  fmt;
    SDL_GPUTextureFormat swap_fmt, depth_fmt, color_fmt;

    SDL_GPUGraphicsPipeline *pipe_terrain, *pipe_sprite, *pipe_sprite_depth, *pipe_shadow;
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
    /*  The mesh, in slots, one per range.  Chunk k's terrain is at 2k,
     *  its networks at 2k+1, and its water at 2*MESH_CHUNKS + k.  Each
     *  with room to grow, so an edit's build uploads only the ranges it
     *  changed and the rest stay where they are.  The buffer is laid out
     *  again, whole, only when a range outgrows its slot. */
    SDL_GPUBuffer         *mbuf;
    uint32_t               mbuf_cap; /* vertices */
    GpuSlot                slot[3 * MESH_CHUNKS];
    int                    slotted; /* the slots are laid out and hold the mesh */
    uint32_t               mesh_n;  /* vertices held, all slots: nothing to draw when 0 */
    SDL_GPUBuffer         *mvbuf;   /* the movers */
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

/*  Canvas pixels to screen pixels: the integer pixel scale times the
 *  continuous zoom.  The frame defines it.  The device uses it too. */
float view_factor(const RGpuView *v);

/*  The device's own growable resources.  The frame asks for them by size
 *  at the top of a pass and does not otherwise know how they are made. */
int                    ensure_targets(RGpu *g, int32_t w, int32_t h);
int                    ensure_offscreen(RGpu *g, int32_t w, int32_t h);
int                    ensure_instances(RGpu *g, uint32_t n);
SDL_GPUTransferBuffer *make_transfer(RGpu *g, int download, uint32_t bytes);

#endif /* R_GPU_INT_H */
