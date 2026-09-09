/*  gpu.h -- the GPU path.  The same op list the software rasteriser paints,
 *  uploaded as instances and drawn through SDL_GPU: one atlas bind per
 *  level, the terrain first with depth writes, the sprites after it tested
 *  against that depth, the shadows into a mask, and a resolve onto the
 *  window.  At the four snaps and an integer scale the frame is meant to be
 *  the software frame pixel for pixel, and gpu_readback exists so that
 *  claim can be measured.  One switch replaces art with shading.
 *  RGpuView.geometry gives the land tiles' art over to a mesh that
 *  replicates the sprites' geometry (gpu_set_mesh), drawn in the depth pass
 *  where the art was so everything else composes unchanged; it shades the
 *  water inside the water sprites' outline, and it draws the roads, their
 *  traffic and their rails as strips on that mesh in place of the road art.
 *  They were three switches and are one.  The camera is the original's, and
 *  RGpuView.pitch is how far it has been raised from it: 30 degrees is the
 *  game's own view, 90 is the map view, straight down. */
#ifndef R_GPU_H
#define R_GPU_H

#include <stdint.h>

#include "atlas/atlas.h"
#include "mesh/mesh.h"
#include "mesh/vert.h"
#include "soft/soft.h"

struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUCommandBuffer;
struct SDL_GPUTexture;
typedef struct RGpu RGpu;

typedef struct
{
    int32_t scroll_x, scroll_y; /* the canvas pixel at the target's top-left */
    int32_t scale;              /* integer pixel scale, target to screen     */
    float   zoom;               /* continuous zoom on top of it: the canvas
                                 * is resolved by scale * zoom; 1 or 0 = none */
    int geometry;               /* the mesh instead of the land art: the
                                 * ground, the water shader inside the water
                                 * sprites, and the roads, rails and traffic
                                 * as strips on it                            */
    int plain_sweep;            /* debug: no depth at all, list order only   */
    int mesh_only;              /* debug: draw no sprite at all              */
    int grid;                   /* the sprites' edge outline on the mesh     */
    int plan;                   /* the map view: the network tints show      */
    int markings;               /* the road-marking pass: lines and dashes  */
    int furniture;              /* the street furniture pass (built in)      */
    int sidewalks;              /* the sidewalk pass: the strips' outer fifth  */
    int underground;            /* the underground view: the mesh as a white
                                 * ground with a hairline grid                */
    float pitch;                /* the camera above the ground, degrees: 30
                                 * the original's view, 90 the map view      */
    float angle;                /* free rotation, degrees, about the pivot   */
    float pivot_c, pivot_r;     /* the grid point the view turns about: the
                                 * point under the view's centre             */
    int sweep_quarter;          /* the quarter the sweep was made for: its art
                                 * is drawn with the turn left from there    */
    float time;                 /* seconds, for the water                    */
} RGpuView;

/*  The point the view turns about.  A quarter turn on the game's own
 *  pitch always pivots on the map's centre: the sprite sweep is then run
 *  on the view turned the original's way, which turns the grid about its
 *  centre, and the mesh -- built once, from the untouched grid -- must
 *  turn about the same point to meet it.  Any other turn pivots on the
 *  point under the view's centre, the anchor. */
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

/*  The palette as it stands now (after atlas_animate); uploaded on the
 *  next frame.  Entries that are water are marked, for the water shader. */
void gpu_set_palette(RGpu *g, const RAtlas *a);

/*  Replace the op list.  `sw` is the sweep that produced it; the level it
 *  names selects the atlas. */
int gpu_set_ops(RGpu *g, const ROpList *ops, const RSweep *sw);

/*  Replace the terrain mesh (n vertices, triangles). */
int gpu_set_mesh(RGpu *g, const RMesh *m); /* the mesh into its slots on the GPU: every range after a full build, the changed chunks' after an edit's */
/*  The movers, the traffic's cars, in a buffer of their own refilled each
 *  frame and drawn with the mesh, after it. */
int gpu_set_movers(RGpu *g, const RMeshVert *v, uint32_t n);

/*  The terrain field for the water and ground shaders: four bytes per
 *  tile.  The first is the distance in tiles from a water tile to the
 *  nearest land, 0 on land; the second the water's depth in levels, the
 *  table over the bed that ALTM carries; the third the distance from a
 *  land tile to the nearest water, 0 on water; the fourth unused.  All
 *  scaled by R_SHORE_SCALE and clamped, and sampled bilinearly, so they
 *  vary smoothly.  `n` is the map side, R_MAP. */
#define R_SHORE_SCALE 16
int gpu_set_shore(RGpu *g, const uint8_t *field, int32_t n);

/*  The sun for the lit material: a direction toward the light, in world
 *  units (x along columns, y along rows, z up), plus ambient and diffuse. */
void gpu_set_light(RGpu *g, float x, float y, float z, float ambient, float diffuse);

/*  Draw and present one frame. */
/*  An overlay drawn on the finished frame -- the UI -- inside the command
 *  buffer the renderer is about to submit; `target` is the swapchain
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
int                   gpu_offscreen_format(const RGpu *g);

/*  What the last frame drew, for the title bar. */
void gpu_stats(const RGpu *g, uint32_t *instances, uint32_t *culled);

#endif /* R_GPU_H */
