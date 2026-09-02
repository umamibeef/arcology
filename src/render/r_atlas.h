/*  r_atlas.h -- the tile art, as the renderer wants it.
 *
 *  sc2kpack.py turns the game's MIFF/SC2K art into one palette-indexed PNG
 *  per zoom level plus a JSON sidecar.  This reads those back.  Nothing here
 *  knows about the resource fork, the 68k binary, or the simulation: it is
 *  a loader for standard files, which is the point of the exercise.
 *
 *  Portability rules this file lives by, and the reason for each:
 *    - fixed-width types only; `long` is 32 bits on Win64 and 64 elsewhere
 *    - every file opened in binary mode, or Windows rewrites \n on the way in
 *    - no POSIX-only calls; the build forces -std=c99 rather than -std=gnu99
 *      so that a stray strdup or unistd.h fails at compile time, not on a
 *      user's machine
 *    - all multi-byte values are read a byte at a time, never memcpy'd over
 *      a struct, so a big-endian host reads the same numbers
 */
#ifndef R_ATLAS_H
#define R_ATLAS_H

#include <stddef.h>
#include <stdint.h>

#define R_MAX_LEVELS 3   /* the 8, 16 and 32 pixel art sets           */
#define R_MAX_SHAPE  1500 /* SHAP ids run 1..1499                     */

/*  One tile's place in its atlas.  `ax`/`ay` are the blit origin relative
 *  to the left corner of the ground diamond: `ay` is how far the art rises
 *  above the diamond, which is `h - tile_h` for the shipped art but is
 *  stored rather than derived so mod art may be taller. */
typedef struct
{
    uint16_t id;   /* SHAP id, 1..1499                        */
    uint16_t tile; /* id minus the level's id_base, 0..499    */
    uint16_t x, y; /* position in the atlas                   */
    uint16_t w, h; /* size in pixels                          */
    uint8_t  foot; /* building footprint, 1..4 (1x1 .. 4x4)   */
    int16_t  ax;   /* blit origin, x                          */
    int16_t  ay;   /* blit origin, y                          */
} RTile;

typedef struct
{
    int32_t zoom;     /* 8, 16 or 32: the tile width in pixels     */
    int32_t id_base;  /* 0, 500 or 1000                            */
    int32_t tile_w;   /* ground diamond width                      */
    int32_t tile_h;   /* ground diamond height, always tile_w / 2  */
    int32_t alt_step; /* pixels per altitude level at this zoom    */
    int32_t transparent; /* the reserved palette index             */

    int32_t  w, h;     /* atlas dimensions                          */
    uint8_t *indices;  /* w*h palette indices; kept for re-resolve  */
    uint8_t *rgba;     /* w*h*4, premultiplied, ready for upload    */

    RTile   *tiles;
    int32_t  n_tiles;
    int32_t  by_id[R_MAX_SHAPE]; /* SHAP id -> index into tiles, or -1 */
} RAtlasLevel;

/*  A run of palette entries the game cycles with _AnimatePalette.  The
 *  art is static; the shimmer on water, the blinking on some buildings and
 *  the traffic lights are all one rotating colour ramp. */
typedef struct
{
    int32_t first; /* first index in the run          */
    int32_t count; /* how many entries rotate         */
    int32_t clut;  /* the resource the run came from  */
} RAnim;

typedef struct
{
    RAtlasLevel level[R_MAX_LEVELS]; /* ordered 8, 16, 32            */
    int32_t     n_levels;
    uint8_t     palette[256][4];     /* RGBA; the reserved index is 0 alpha */
    uint8_t     palette0[256][4];    /* phase 0, so phases compose from it */
    RAnim       anim[4];
    int32_t     n_anim;
    char        err[256];
} RAtlas;

/*  Load every level named by <dir>/atlas.json.  Returns 0 on success, or
 *  -1 with a human-readable reason in a->err. */
int r_atlas_load(RAtlas *a, const char *dir);

void r_atlas_free(RAtlas *a);

/*  The level whose art is closest to `scale` (1.0 = 32 px tiles), and the
 *  tile record for a SHAP id, or NULL if this level has no such shape. */
const RAtlasLevel *r_atlas_level_for_scale(const RAtlas *a, float scale);
const RTile       *r_atlas_tile(const RAtlasLevel *l, int32_t shap_id);

/*  Re-resolve `indices` into `rgba` through the current palette.  Cheap
 *  enough per frame for the handful of palette-cycled tiles, which is how
 *  water shimmer and a day/night tint come for free. */
/*  Rotate the animated runs to `phase`.  Phase 0 restores the art exactly
 *  as the atlas was built.  Rotating the palette is all the game does --
 *  no pixel is touched -- so it is one memcpy plus a few dozen entries per
 *  frame however big the map is. */
void r_atlas_animate(RAtlas *a, int32_t phase);
/*  The same, with each run on its own clock: idlePump ($9728) turns the
 *  49-entry run every 12 ticks and the 15-entry run every 90.  Steps are
 *  reduced by each permutation's period, so the counters may grow without
 *  bound.  Only the palette is rewritten; the levels' rgba is left alone,
 *  which is what the GPU path wants (it resolves through the palette on
 *  the GPU).  r_atlas_resolve brings rgba back in step if needed. */
void r_atlas_animate_runs(RAtlas *a, int32_t steps_a, int32_t steps_b);

void r_atlas_resolve(RAtlas *a, RAtlasLevel *l);
void r_atlas_resolve_rect(RAtlas *a, RAtlasLevel *l, int32_t x, int32_t y,
                          int32_t w, int32_t h);

#endif /* R_ATLAS_H */
