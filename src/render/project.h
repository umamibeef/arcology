/*  project.h -- the one projection, shared by everything that uses it.
 *
 *  A tile becomes a canvas pixel in four places: here for picking and the
 *  overlays, in gpu.c as the uniforms a frame is drawn with, in
 *  terrain.vert, and in soft.c, which is the one that decides where every
 *  sprite actually lands.  The first inventory of this said three and
 *  left out the sweep -- the one place the answer is compared with the
 *  original pixel for pixel.  They were three separate transcriptions of the
 *  same arithmetic, and they have to agree exactly or the query box stops
 *  landing where the pointer is -- nothing checked that they did, and one
 *  had already drifted: the canvas origin was `tile_w * 0.5f` in one and
 *  integer `tile_w / 2` in the other, which agree only because every
 *  tile width the game ships is even.
 *
 *  The shader cannot include this, so it takes the same numbers as
 *  uniforms, and tools/project_check.py (the `projection` test) reads the
 *  values below and fails if any C or GLSL restates one.
 *
 *  ARC_PITCH_SIN0 and ARC_PITCH_COS0 are the sine and cosine of the
 *  game's own camera pitch, 30 degrees.  Every other pitch is expressed
 *  as a ratio against them, so at 30 the ratios are 1 and the projection
 *  is the original's to the pixel.
 */
#ifndef R_PROJECT_H
#define R_PROJECT_H

#include <math.h>

/*  The game's own camera pitch, and its sine and cosine.  The angle was
 *  spelled out in eight places and its sine and cosine in three more, so
 *  a change of pitch meant finding all eleven; the check that guards the
 *  two ratios could not see the angle they came from. */
#define ARC_PITCH_DEG  30.0f
#define ARC_PITCH_SIN0 0.5f
#define ARC_PITCH_COS0 0.8660254f

/*  The largest art set, and the fallback when no level is loaded yet: a
 *  32 pixel tile, 16 tall, 12 to the altitude level.  These were three
 *  bare numbers sitting where a missing level was handled, which is the
 *  one moment nobody looks at. */
#define ARC_TILE_W_MAX   32.0f
#define ARC_TILE_H_MAX   16.0f
#define ARC_ALT_STEP_MAX 12.0f

/*  The sprite depth skew: a sliver per tile along the difference axis, so
 *  two sprites the painter's order cannot separate still resolve the same
 *  way the sweep would have drawn them.  Written once here, and once more
 *  in sprite.vert, which cannot include this -- project_check.py holds
 *  the two together. */
#define ARC_SPRITE_SKEW 0.0015f

/*  Degrees to radians, for the camera angles that arrive in degrees. */
#define ARC_DEG2RAD 0.017453292f

/*  Is a view at the game's own pitch, to within a hundredth of a degree?
 *  Below it the painter's slot still orders the tiles; above it, or
 *  turned, it cannot and the depth buffer takes over. */
static inline int arc_is_game_pitch(float pitch_deg)
{
    return fabsf(pitch_deg - ARC_PITCH_DEG) <= 0.01f;
}

/*  Half a tile, the unit both axes step by: the difference axis moves by
 *  half a width per tile, the sum axis by half a height before pitch.
 *  Written as a float halving on purpose -- the integer form agrees only
 *  while every tile size is even, and nothing enforces that. */
static inline float arc_half_w(float tile_w)
{
    return tile_w * 0.5f;
}

static inline float arc_half_h(float tile_h)
{
    return tile_h * 0.5f;
}

/*  The same halves in integers, for the sweep.  soft.c places sprites at
 *  whole canvas pixels and its output is compared with the original's
 *  pixel for pixel, so it must truncate exactly where the original did --
 *  the arithmetic stays integer on purpose, and only the definition of
 *  "half a tile" is shared.  (The sweep places a sprite's ORIGIN, not a
 *  tile's centre, which is why it takes no half-tile offset the way
 *  arc_origin_x does.) */
static inline int arc_half_wi(int tile_w)
{
    return tile_w / 2;
}

static inline int arc_half_hi(int tile_h)
{
    return tile_h / 2;
}

/*  Where the canvas puts map (0,0): half a tile right of the sweep's
 *  origin, and a tile and a half-pixel above it -- a ground sprite's
 *  diamond occupies the rows ABOVE its tile's origin row. */
static inline float arc_origin_x(float sweep_ox, float tile_w)
{
    return sweep_ox + arc_half_w(tile_w);
}

static inline float arc_origin_y(float sweep_oy, float tile_h)
{
    return sweep_oy - (tile_h + 0.5f);
}

/*  How far a step of one tile along the sum axis moves down the canvas,
 *  and how far one level of altitude moves up it, at a given pitch. */
static inline float arc_y_scale(float tile_h, float pitch_rad)
{
    return arc_half_h(tile_h) * (sinf(pitch_rad) / ARC_PITCH_SIN0);
}

static inline float arc_alt_scale(float alt_step, float pitch_rad)
{
    return alt_step * (cosf(pitch_rad) / ARC_PITCH_COS0);
}

#endif /* R_PROJECT_H */
