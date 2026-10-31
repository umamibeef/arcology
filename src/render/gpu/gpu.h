/*  gpu.h: the GPU path.  The same op list the software rasteriser
 *  paints, uploaded as instances and drawn through SDL_GPU.
 *
 *      One atlas bind per level.  The terrain first with depth writes.
 *      The sprites after it tested against that depth.  The shadows into
 *      a mask.  A resolve onto the window.
 *
 *  At the four snaps and an integer scale the frame is the software
 *  frame, pixel for pixel.  gpu_readback exists so that claim can be
 *  measured.  One switch replaces art with shading.  RGpuView.geometry
 *  gives the land tiles' art over to a mesh.  That mesh replicates the
 *  sprites' geometry. gpu_set_mesh takes it, drawn in the depth pass
 *  where the art was so everything else composes unchanged.  It shades
 *  the water inside the water sprites' outline.  The camera is the
 *  original's.  RGpuView.pitch is how far it has been raised from
 *  it. 30 degrees is the game's own view, 90 is the map view, straight
 *  down. */
#ifndef R_GPU_H
#define R_GPU_H

#include <stdint.h>

#include "atlas/atlas.h"
#include "mesh/mesh.h"
#include "mesh/vert.h"
#include "show.h"
#include "soft/soft.h"

struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUCommandBuffer;
struct SDL_GPUTexture;
typedef struct RGpu RGpu;

typedef struct
{
    int32_t scroll_x, scroll_y;   /* the canvas pixel at the target's top-left */
    float   scroll_fx, scroll_fy; /* and the fraction of a canvas pixel past it, so the view moves smoothly */
    int32_t scale;                /* integer pixel scale, target to screen     */
    float   zoom;                 /* continuous zoom on top of it: the canvas
                                   * is resolved by scale * zoom.  1 or 0 is none */
    int geometry;                 /* the mesh instead of the land art: the ground, and the water shader inside the water sprites */
    int plain_sweep;              /* debug: no depth at all, list order only   */
    int mesh_only;                /* debug: draw no sprite at all              */
    int grid;                     /* the sprites' edge outline on the mesh     */
    int tris;                     /* debug: each triangle of the mesh in a
                                   * color of its own                          */
    int plan;                     /* the map view: the network tints show      */
    int underground;              /* the underground view: the mesh as a white
                                   * ground with a hairline grid                */
    float pitch;                  /* the camera above the ground, degrees: 30
                                   * the original's view, 90 the map view      */
    float angle;                  /* free rotation, degrees, about the pivot   */
    float pivot_c, pivot_r;       /* the grid point the view turns about: the
                                   * point under the view's center             */
    int sweep_quarter;            /* the quarter the sweep was made for: its art
                                   * is drawn with the turn left from there    */
    float time;                   /* seconds, for the water                    */
    int   wind;                   /* 1 while the trees sway: with the clouds   */
    int   air;                    /* 1 to see the frame through the air: its
                                   * haze, its smog and the light it scatters
                                   * (air.frag)                                */
    float sky_cover;              /* the share of the sky under cloud: the
                                   * clouds the air pass marches through
                                   * (air.frag).  Nought: no clouds            */
    float sky_cursor[2];          /* the pointer, in the frame's pixels, which
                                   * the clouds keep clear round.  Far off the
                                   * window with no pointer                    */
    float show[24];               /* what an edit shows going and coming: the
                                   * clocks, the edited tiles, the waves and how
                                   * a piece moves (show.h).  All nought:
                                   * nothing moves */
    float settle[23];             /* the land settling while the world is built
                                   * a block at a time.  First the sheet's
                                   * height and the waves' height, length and
                                   * speed.  Then the seconds, tiles a chunk,
                                   * chunks a side and tiles a side.  Then the
                                   * levels the art falls from.  Then the
                                   * sweep's cells onto the map's: the cell
                                   * (0,0) goes to, and where one more column
                                   * and one more row go.  Then the point the
                                   * sweep runs out from, its tiles a second,
                                   * the seconds a piece of art falls and the
                                   * seconds a point settles.  Then the second
                                   * the sweep starts.  Last how much darker the
                                   * land still being built is, and the seconds
                                   * that takes to come in.  All nought: the
                                   * plain shaders draw */
} RGpuView;

/*  The point the view turns about.  A quarter turn on the game's own
 *  pitch always pivots on the map's center.  The sprite sweep is then
 *  run on the view turned the original's way, which turns the grid about
 *  its center.  The mesh, built once, from the untouched grid, must turn
 *  about the same point to meet it.  Any other turn pivots on the point
 *  under the view's center, the anchor. */
static inline int gpu_view_quarter(const RGpuView *v)
{
    float turn = v->angle < 0.0f ? -v->angle : v->angle;
    while (turn >= 90.0f)
        turn -= 90.0f;
    return turn < 0.01f || turn > 89.99f;
}
static inline void gpu_view_pivot(const RGpuView *v, float *pc, float *pr)
{
    if (gpu_view_quarter(v) && v->angle != 0.0f)
    {
        *pc = 64.0f;
        *pr = 64.0f;
    }
    else
    {
        *pc = v->pivot_c;
        *pr = v->pivot_r;
    }
}

RGpu *gpu_create(struct SDL_Window *win, const RAtlas *a, char *err, size_t err_len);
void  gpu_destroy(RGpu *g);

/*  The palette as it stands now (after atlas_animate).  Uploaded on the
 *  next frame.  Entries that are water are marked, for the water shader. */
void gpu_set_palette(RGpu *g, const RAtlas *a);

/*  Replace the op list.  `sw` is the sweep that produced it.  The level
 *  it names selects the atlas. */
int gpu_set_ops(RGpu *g, const ROpList *ops, const RSweep *sw);

/*  Replace the terrain mesh (n vertices, triangles). */
/*  The edit show's key for triangle `tri` of the mesh, shape `id` (show.h).
 *  The caller hands it in, so the device asks nothing above it. */
typedef uint32_t (*RGpuTriKey)(uint32_t tri, uint32_t id);
/*  The mesh into its slots on the GPU: every range after a full build,
 *  the changed chunks' after an edit's.  Each triangle of level 0 is keyed
 *  by `key`, or by none where `key` is NULL. */
int gpu_set_mesh(RGpu *g, const RMesh *m, RGpuTriKey key);
/*  The last world's slots an edit drew again, keyed so its triangles with
 *  no twin in the new one go.  `keys`, `at` and `n` are the show's
 *  (show_rekey). */
int gpu_show_rekey(RGpu *g, const uint32_t *keys, const uint32_t at[SHOW_SLOTS], const uint32_t n[SHOW_SLOTS]);

/*  The terrain field for the water and ground shaders: four bytes per
 *  tile.  The first is the distance in tiles from a water tile to the
 *  nearest land, 0 on land.  The second the water's depth in levels, the
 *  table over the bed that ALTM carries.  The third the distance from a
 *  land tile to the nearest water, 0 on water.  The fourth unused.  All
 *  scaled by R_SHORE_SCALE and clamped, and sampled bilinearly, so they
 *  vary smoothly.  `n` is the map side, r_map. */
#define R_SHORE_SCALE 16
int gpu_set_shore(RGpu *g, const uint8_t *field, int32_t n);
/*  What the build lays over the ground, n texels a side over the map,
 *  four bytes a texel.  The first two are the street lamps' light and the
 *  height it falls at (draw/lamps.h).  The last two are the trees'
 *  canopy and the tile's main species (trees.h). */
int gpu_set_laid(RGpu *g, const uint8_t *field, int32_t n);
/*  The tree species' leaf colors, light and dark, and how ragged each is,
 *  for the leaf material: GPU_LEAF_SPECIES of each. */
#define GPU_LEAF_SPECIES 8
void gpu_set_leaves(RGpu *g, const float colors[GPU_LEAF_SPECIES][2][4], const float rag[GPU_LEAF_SPECIES]);
/*  The pollution, one byte a texel of an `n` by `n` field over the whole
 *  map: the city's own layer at half resolution.  The smog in the air
 *  (air.frag) is as thick as it says. */
int gpu_set_smog(RGpu *g, const uint8_t *field, int32_t n);
/*  What the player's tool marks on each tile of an `n` by `n` map, four
 *  bytes a tile.  A marked tile holds its mark's color, and in alpha how
 *  far the tile goes to it.  A tile with no mark holds nought.  `marks` is in the map's own frame, for the mesh, and `sweep`
 *  is the same in the sweep's quarter, for the art.  Both light those
 *  tiles (tile_marks.glsl).  NULL marks nothing, and the plain shaders
 *  draw. */
int gpu_set_marks(RGpu *g, const uint8_t *marks, const uint8_t *sweep, int32_t n);
/*  Every piece's state as an edit shows it, SHOW_PIECES of them by the
 *  piece's id (show.h): still, going or coming. */
int gpu_set_show(RGpu *g, const uint8_t *state);
/*  The land the sheet settles onto, four floats a tile of an `n` by `n`
 *  map.  They are the tile's corner heights in levels, at (0,0), (1,0),
 *  (1,1) and (0,1) from it. */
int gpu_set_land(RGpu *g, const float *corners, int32_t n);
/*  Each chunk of a `per` by `per` grid, two floats a chunk.  The first is
 *  the second of the settle it came onto the device.  The second is 1
 *  once it is built. */
int gpu_set_settle(RGpu *g, const float *state, int32_t per);
/*  The tiles of an `n` by `n` map, in the sweep's quarter, whose piece is
 *  drawn from its own shape, one byte a tile.  With the mesh on, the art
 *  of such a tile is not drawn.  NULL marks none. */
int gpu_set_shaped(RGpu *g, const uint8_t *shaped, int32_t n);

/*  Draw and present one frame. */
/*  An overlay drawn on the finished frame, the UI, inside the command
 *  buffer the renderer is about to submit.  `target` is the swapchain
 *  texture (or the readback's offscreen one), `w` by `h` pixels.  The
 *  resolve pass has ended when it is called. */
typedef void (*RGpuOverlay)(void *ud, struct SDL_GPUCommandBuffer *cmd, struct SDL_GPUTexture *target, uint32_t w, uint32_t h);

int gpu_frame(RGpu *g, const RGpuView *v, const uint8_t sky[3], RGpuOverlay overlay, void *ud);

/*  Draw one frame of `w` x `h` target pixels at scale 1 into `out`,
 *  allocated here, without presenting.  The check against the software
 *  rasteriser reads this. */
int gpu_readback(RGpu *g, const RGpuView *v, const uint8_t sky[3], int32_t w, int32_t h, int32_t scale, RImage *out, RGpuOverlay overlay, void *ud);

/*  The backend SDL picked: "metal", "vulkan", "direct3d12". */
const char *gpu_driver(const RGpu *g);
/*  The device and the SDL_GPUTextureFormat of the swapchain and of the
 *  readback's offscreen target, for a UI that draws on them. */
struct SDL_GPUDevice *gpu_device(const RGpu *g);
int                   gpu_swapchain_format(const RGpu *g);
int                   gpu_offscreen_format(void);

/*  What the last frame drew, for the title bar. */
void gpu_stats(const RGpu *g, uint32_t *instances, uint32_t *culled);

#endif /* R_GPU_H */
