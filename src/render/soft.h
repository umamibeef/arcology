/*  soft.h -- the reference rasteriser.
 *
 *  Not a fallback.  This is the renderer the GPU path is checked against:
 *  it has no driver, no shader compiler and no window, so it runs in CI on
 *  a machine with no display, and it is the only thing that can say whether
 *  a change to the fast path changed what is drawn.
 *
 *  At zoom 32, rotation 0, it must reproduce the original renderer pixel for
 *  pixel.  That is the test.
 */
#ifndef R_SOFT_H
#define R_SOFT_H

#include <stdint.h>

#include "atlas.h"
#include "city.h"

typedef struct
{
    int32_t  w, h;
    uint8_t *rgb; /* w*h*3, 8 bits per channel            */
    /*  The same picture as palette indices.  $19B76 -- the shadow pass --
     *  READS the destination and only darkens it when it is already in the
     *  dirt ramp, so the renderer has to know what index it painted, not
     *  just the colour: index 104 shares its RGB with three entries in the
     *  animated range. */
    uint8_t *idx; /* w*h                                   */
    /*  Which sprite painted each pixel: the tile id within its art set
     *  (0..499; 0 is the background, which no sprite has) with
     *  R_PROV_SHADOW set where $19B76 darkened it afterwards.  This is
     *  the plane the 2.5D terrain is checked against: every pixel a
     *  non-terrain sprite painted must survive the mesh unchanged. */
    uint16_t *prov; /* w*h                                 */
} RImage;

#define R_PROV_SHADOW 0x8000u
#define R_PROV_ID(p)  ((p) & 0x7FFFu)

/*  The map views, numbered as the game numbers them: $2C34 indexes a
 *  12-entry jump table at $168C8, one arm per data layer.  Mode 0 is the
 *  normal view. */
typedef enum
{
    R_VIEW_NORMAL = 0,
    R_VIEW_TRAFFIC = 1,
    R_VIEW_DENSITY = 2,
    R_VIEW_GROWTH_VALUE = 3,
    R_VIEW_CRIME = 4,
    R_VIEW_POLICE = 5,
    R_VIEW_POLLUTION = 6,
    R_VIEW_LANDVALUE = 7,
    R_VIEW_FIRE = 8,
    R_VIEW_POWER = 9,
    R_VIEW_WATER = 10,
    R_VIEW_GROWTH = 11
} RView;

typedef struct
{
    int32_t zoom;      /* 8, 16 or 32; which art set to draw with  */
    int32_t view;      /* RView; tints flat land from a data layer */
    int     underground; /* the pipes-and-subway view, $161DC      */
    /*  Debug: instead of drawing, print every blit as
     *      row col shape x y mirror
     *  with x,y in the game's own origin, so the list can be diffed
     *  against tools/render_oracle.py.  See tools/render_diff.py. */
    int     dump_blits;
    /*  Draw multi-tile buildings at the anchor's own y, with no footprint
     *  drop -- what the game does once its shape-descriptor table is
     *  zeroed.  For A/B comparison only. */
    int     no_drop;
    /*  Frame a preview on one tile: the renderer records where this tile
     *  landed on the canvas so the caller can crop around it.  A sprite
     *  may stand far above its own tile -- an aircraft is drawn 120 px up
     *  -- so cropping by tile and padding generously is the only way to
     *  be sure the whole thing is in frame.  -1 for neither. */
    int32_t focus_row, focus_col;
    int32_t x0, y0;    /* top-left tile of the region to draw      */
    int32_t n;         /* region is n x n tiles                    */
    uint8_t sky[3];    /* the background                           */
    int     draw_things;
    int     draw_terrain;
    int     draw_buildings;
    /*  The 2.5D composition.  The terrain is drawn first, on its own, as
     *  the geometry the sprites stand on, and it writes a depth plane
     *  holding the painter's index of the tile that owns each pixel.
     *  Everything else is then drawn in the original's anti-diagonal
     *  order, tested against that plane and never writing it.  A hill
     *  in front hides the building behind it exactly as the sweep did,
     *  and sprite-on-sprite order stays the sweep's.  Off, the single
     *  interleaved sweep of the original runs unchanged. */
    int     mesh;
    /*  Debug: draw the terrain pass back to front, which must change
     *  nothing -- the depth plane, not the loop, carries the ordering. */
    int     mesh_reverse;
} RSoftOpts;

void soft_defaults(RSoftOpts *o);

/*  One blit the original would make.  The sweep emits these in the
 *  original's order and never paints: see soft_sweep. */
typedef enum
{
    R_OP_BLIT   = 0, /* a sprite, through $18E96 or the stencilled $19004 */
    R_OP_SHADOW = 1  /* $19B76: darkens the destination under a silhouette */
} ROpKind;

typedef struct
{
    int32_t  shape;   /* SHAP id, already in the level's art set          */
    int32_t  x, y;    /* the art's top-left on the canvas                 */
    uint32_t order;   /* painter's index of the emitting tile, from 1     */
    int32_t  stencil; /* -1, or the palette index a car may paint over    */
    uint8_t  kind;    /* ROpKind                                          */
    uint8_t  flip;    /* mirrored                                         */
    uint8_t  terrain; /* ground the sprites stand on: the terrain pass    */
    uint8_t  under_flip; /* the road sprite's own mirror flag          */
    int16_t  row, col; /* the emitting tile                               */
    int16_t  alt;      /* and its altitude in levels, as the sweep drew it,
                        *  so a camera off the original's own can put the
                        *  sprite back where its tile went               */
    /*  For a stencilled op: the road sprite it was stencilled onto, so a
     *  consumer that cannot read its destination can test that sprite's
     *  own texel instead.  under_shape is 0 when there is none. */
    int32_t under_shape, under_x, under_y;
} ROp;

typedef struct
{
    ROp   *v;
    size_t n, cap;
} ROpList;

/*  What the sweep decided about the canvas. */
typedef struct
{
    int32_t            w, h;       /* canvas size                          */
    int32_t            ox, oy;     /* the game's origin on the canvas      */
    int32_t            zoom_level; /* 0, 1, 2 for the 8, 16 and 32 px sets */
    const RAtlasLevel *level;
    int32_t            focus_x, focus_y;
    int                focus_ok;
} RSweep;

/*  The original's whole-map sweep as a list of ops, in the original's
 *  order.  `ops` starts empty or is reused; `info` is filled.  Returns 0,
 *  or -1 if the atlas has no level for the requested zoom or memory ran
 *  out.  The rasteriser below and the GPU path both consume this. */
int  soft_sweep(const RAtlas *a, const RCity *c, const RSoftOpts *o,
             ROpList *ops, RSweep *info);
void ops_free(ROpList *ops);

/*  Render into `out`, which is allocated here.  Returns 0, or -1 if the
 *  atlas has no level for the requested zoom. */
int  soft_render(RImage *out, const RAtlas *a, const RCity *c,
                   const RSoftOpts *o);
void image_free(RImage *im);

/*  CRC32 of the raw RGB buffer.  Comparing this with the same number
 *  computed in Python is the pixel-exactness test; comparing PNG bytes
 *  would only test that two deflate implementations agree, which they
 *  do not have to. */
uint32_t image_crc(const RImage *im);

/*  Where the --focus tile landed, valid after soft_render.  Returns 0
 *  if no focus tile was set or it fell outside the canvas. */
int  soft_focus_result(int32_t *x, int32_t *y);
/*  In the 2.5D mode: how many sprite pixels the depth plane rejected in
 *  the last render, i.e. how much terrain-over-sprite occlusion the
 *  plane carried.  Zero would mean the test proved nothing. */
uint32_t soft_depth_blocked(void);
/*  The depth plane of the last --mesh render, full frame, before any crop. */
int soft_write_depth_png(const RImage *im, const char *path);

int image_write_png(const RImage *im, const char *path);
int image_write_png_indices(const RImage *im, const RAtlas *a,
                              const char *path);
int image_write_png_indexed(const RImage *im, const char *path);
/*  The provenance plane as a 16-bit greyscale PNG, one sample per pixel. */
int image_write_png_provenance(const RImage *im, const char *path);

#endif /* R_SOFT_H */
