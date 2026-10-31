/*  camera.c: the camera, and the two directions between a tile and a
 *  pixel.  Pitch, yaw, zoom and scroll, the eased move between two of
 *  them, and the projection the query box reads.  The same arithmetic
 *  exists twice more, as GPU uniforms and again in the shader, which is
 *  the next thing here worth unifying. */
#include <SDL3/SDL.h>

#include "internal.h"
#include "log.h"
#include "opt.h"
#include "project.h"
#include "terrain.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void cam_set(App *a, float pitch, float yaw, SDL_Window *win);

/*  The anchor made current: measured again from the view where something
 *  other than the camera moved it, and left alone otherwise. */
static void cam_look_fresh(App *a);

/*  The grid point a canvas point shows on a level plane at altitude
 *  `alt`. */
static void canvas_to_grid_plane(const App *a, float cx, float cy, float alt, float *ofc, float *ofr);

/*  The scroll set to a canvas point given to a fraction of a pixel, the
 *  fraction kept.  And the fraction dropped where the scroll moved by other
 *  means. */
static void scroll_set(App *a, float x, float y);

/*  ==================================================================
 *  Camera and projection
 *
 *  Zoom, pitch, yaw and scroll, and the two directions between a tile
 *  and a pixel that the query box uses.
 *  ================================================================== */
/*  Continuous zoom.  The art is the 32 px set at every zoom, scaled by
 *  the zoom itself.  `mx`, `my` is the window point that stays put, in
 *  pixels. */
void zoom_to(App *a, float z, float mx, float my)
{
    float f_old, z_old = a->zoom_world > 0.0f ? a->zoom_world : 1.0f;
    float pc, pr;
    if (z < 0.125f)
        z = 0.125f;
    if (z > 32.0f)
        z = 32.0f; /* past 4 the art is blocky.  The mesh repays a much closer look */
    f_old = cam_px(a);
    /*  The map point under the pointer, on the anchor's level, stays
     *  under it: the anchor moves toward it as the view closes in. */
    cam_look_fresh(a);
    if (a->anch_ok && a->sw.level)
    {
        canvas_to_grid_plane(a, (float)a->gv.scroll_x + scroll_fx(a) + mx / f_old, (float)a->gv.scroll_y + scroll_fy(a) + my / f_old, a->anch_alt, &pc, &pr);
        a->anch_c = pc + (a->anch_c - pc) * (z_old / z);
        a->anch_r = pr + (a->anch_r - pr) * (z_old / z);
    }
    a->gv.zoom    = z;
    a->zoom_world = z;
    cam_hold(a, NULL);
}

/*  The window's pixels to one of the canvas's: the pixel scale times the
 *  zoom. */
float cam_px(const App *a)
{
    return (float)a->gv.scale * (a->gv.zoom > 0.0f ? a->gv.zoom : 1.0f);
}

/*  Move the view by (dx, dy) window pixels, as a pan key or a drag moves
 *  it.  The anchor moves across the map to the point the screen's center
 *  would reach on the anchor's level, and the view is held on it.  So a
 *  pan covers the same ground on the screen at every zoom. */
void cam_pan(App *a, SDL_Window *win, float dx, float dy)
{
    float cx, cy, f = cam_px(a);
    if (!a->sw.level)
        return;
    cam_look_fresh(a);
    grid_to_canvas(a, a->anch_c, a->anch_r, a->anch_alt, &cx, &cy);
    canvas_to_grid_plane(a, cx + dx / f, cy + dy / f, a->anch_alt, &a->anch_c, &a->anch_r);
    cam_hold(a, win);
}

/*  THE EASED ZOOM, as strategy and city games zoom.  A wheel's notch or
 *  a key asks for a zoom, and the view moves toward it each frame.  Each
 *  frame moves 1 - e^(-ZOOM_RATE dt) of the way left in the zoom's
 *  logarithm.  So it is the same at any frame rate, and looks the same at
 *  any zoom.  The point under the pointer stays put all the way. */
#define ZOOM_RATE 12.0f

void zoom_toward(App *a, float z, float mx, float my)
{
    a->zoom_aim = z < 0.125f ? 0.125f : z > 32.0f ? 32.0f
                                                  : z;
    a->zoom_mx = mx, a->zoom_my = my;
}

/*  Zoom by `factor` from where the view is heading, eased.  The window
 *  point (mx, my) keeps still.  A wheel notch, a key and a menu item all
 *  zoom this way. */
void zoom_by(App *a, float factor, float mx, float my)
{
    zoom_toward(a, (a->zoom_aim > 0.0f ? a->zoom_aim : a->zoom_world) * factor, mx, my);
}

void zoom_ease(App *a, float dt)
{
    float z = a->zoom_world > 0.0f ? a->zoom_world : 1.0f, k;
    if (a->zoom_aim <= 0.0f || dt <= 0.0f)
        return;
    k = logf(a->zoom_aim / z);
    /*  The last hair arrives at once, so the ease ends. */
    if (fabsf(k) < 1e-4f)
    {
        zoom_to(a, a->zoom_aim, a->zoom_mx, a->zoom_my);
        a->zoom_aim = 0.0f;
        return;
    }
    zoom_to(a, z * expf(k * (1.0f - expf(-ZOOM_RATE * dt))), a->zoom_mx, a->zoom_my);
}

/*  How far a held pan key moves the view, in screen pixels a second. */
#define PAN_SCREEN_PER_SECOND 900.0f
/*  How fast a held turn key turns the view, in degrees a second, and the
 *  step a turn comes to rest on, in degrees. */
#define TURN_PER_SECOND 90.0f
#define TURN_STEP       90.0f
/*  How long a press may last and still count as a tap, in seconds.  And
 *  the step a tap turns with free rotation on, in degrees. */
#define TAP_SECONDS 0.2f
#define FREE_NUDGE  15.0f
/*  About how long a held pan key takes to reach its speed, and to
 *  come to rest once let go, in seconds. */
#define EASE_SECONDS 0.12f

/*  The turn keys.  A press commits a step at once: the next TURN_STEP
 *  in its way, or a FREE_NUDGE with free rotation on.  Held past
 *  TAP_SECONDS, the view goes on turning at TURN_PER_SECOND.  Let go, a
 *  snapped turn finishes on the next TURN_STEP in its way.  A free one
 *  glides to rest.  The yaw follows its aim as a critically damped
 *  spring, so it eases in and out and never overshoots.  An eased move
 *  of the camera (cam_go) is left alone. */
static void cam_turn_keys(App *a, SDL_Window *win, float dt, int right, int left)
{
    const float w   = 2.0f / (EASE_SECONDS * 1.5f); /* the spring's rate */
    const int   dir = right - left;
    float       x;
    if (dt <= 0.0f || (!a->yaw_on && dir == 0) || a->cam_t < 1.0f)
    {
        a->yaw_held = dir;
        return;
    }
    if (!a->yaw_on)
    {
        a->yaw_on  = 1;
        a->yaw_now = a->yaw_aim = a->angle;
        a->yaw_v                = 0.0f;
    }
    if (dir != 0 && dir != a->yaw_held)
    {
        /*  A press: one step at once. */
        float s     = a->yaw_aim / TURN_STEP;
        a->yaw_hold = 0.0f;
        a->yaw_way  = dir;
        if (a->us.free_turn)
            a->yaw_aim += (float)dir * FREE_NUDGE;
        else
            a->yaw_aim = (dir > 0 ? floorf(s + 1e-4f) + 1.0f : ceilf(s - 1e-4f) - 1.0f) * TURN_STEP;
    }
    if (dir != 0)
    {
        /*  Held: past a tap, the aim runs on. */
        a->yaw_hold += dt;
        if (a->yaw_hold > TAP_SECONDS)
            a->yaw_aim += (float)dir * TURN_PER_SECOND * dt;
    }
    else if (a->yaw_held != 0 && a->yaw_hold > TAP_SECONDS)
    {
        /*  Let go after a hold. */
        float s = a->yaw_aim / TURN_STEP;
        if (a->us.free_turn)
            a->yaw_aim = a->yaw_now + a->yaw_v / w;
        else
            a->yaw_aim = (a->yaw_way > 0 ? ceilf(s - 1e-4f) : floorf(s + 1e-4f)) * TURN_STEP;
    }
    a->yaw_held = dir;
    /*  The spring, solved exactly over the frame. */
    x = a->yaw_now - a->yaw_aim;
    {
        float e    = expf(-w * dt);
        float c2   = a->yaw_v + w * x;
        a->yaw_now = a->yaw_aim + (x + c2 * dt) * e;
        a->yaw_v   = (c2 - w * (x + c2 * dt)) * e;
    }
    if (dir == 0 && fabsf(a->yaw_now - a->yaw_aim) < 0.01f && fabsf(a->yaw_v) < 0.5f)
    {
        a->yaw_now = a->yaw_aim;
        a->yaw_on  = 0;
        a->yaw_v   = 0.0f;
    }
    /*  The point under the view's center, measured again where a pan or a
     *  zoom moved the view this frame, is what the turn holds.  So a pan
     *  and a turn together both take effect. */
    cam_look_fresh(a);
    cam_set(a, a->gv.pitch, a->yaw_now, win);
    a->yaw_from = a->yaw_to = a->angle;
}

/*  The arrows, and W A S D beside them while no command key is held,
 *  so the letter shortcuts still work.  Each key sets a speed to reach:
 *  a set distance on the screen a second, whatever the zoom.  Each speed
 *  eases toward its aim over about EASE_SECONDS, so a move starts and
 *  stops softly. */
void cam_keys(App *a, SDL_Window *win)
{
    static uint64_t last;
    const bool     *ks  = SDL_GetKeyboardState(NULL);
    const int       wsd = (SDL_GetModState() & (KMOD_CMD | SDL_KMOD_ALT)) == 0;
    const uint64_t  now = SDL_GetTicksNS();
    float           dt  = last ? (float)((double)(now - last) / 1e9) : 0.0f, ease, want[2];
    int             k;
    last    = now;
    dt      = dt > 0.1f ? 0.1f : dt;
    want[0] = (float)((ks[SDL_SCANCODE_RIGHT] || (wsd && ks[SDL_SCANCODE_D])) -
                      (ks[SDL_SCANCODE_LEFT] || (wsd && ks[SDL_SCANCODE_A]))) *
              PAN_SCREEN_PER_SECOND;
    want[1] = (float)((ks[SDL_SCANCODE_DOWN] || (wsd && ks[SDL_SCANCODE_S])) -
                      (ks[SDL_SCANCODE_UP] || (wsd && ks[SDL_SCANCODE_W]))) *
              PAN_SCREEN_PER_SECOND;
    ease    = 1.0f - expf(-dt / EASE_SECONDS);
    for (k = 0; k < 2; ++k)
    {
        a->cam_v[k] += (want[k] - a->cam_v[k]) * ease;
        if (want[k] == 0.0f && fabsf(a->cam_v[k]) < 1.0f)
            a->cam_v[k] = 0.0f;
    }
    if (a->cam_v[0] != 0.0f || a->cam_v[1] != 0.0f)
        cam_pan(a, win, a->cam_v[0] * dt, a->cam_v[1] * dt);
    if (geometry_on(a))
        cam_turn_keys(a, win, dt, wsd && ks[SDL_SCANCODE_E], wsd && ks[SDL_SCANCODE_Q]);
}

float scroll_fx(const App *a)
{
    return a->gv.scroll_x == a->scroll_kept[0] && a->gv.scroll_y == a->scroll_kept[1] ? a->pan_x : 0.0f;
}

float scroll_fy(const App *a)
{
    return a->gv.scroll_x == a->scroll_kept[0] && a->gv.scroll_y == a->scroll_kept[1] ? a->pan_y : 0.0f;
}

static void scroll_set(App *a, float x, float y)
{
    float fx = floorf(x), fy = floorf(y);
    a->gv.scroll_x = (int32_t)fx, a->gv.scroll_y = (int32_t)fy;
    a->pan_x = x - fx, a->pan_y = y - fy;
    a->scroll_kept[0] = a->gv.scroll_x, a->scroll_kept[1] = a->gv.scroll_y;
}

/*  The camera's two scales on the canvas, the same numbers terrain.vert
 *  projects with.  They are pixels down the screen per unit of column
 *  plus row, and pixels up it per altitude level.  At the game's own
 *  pitch of 30 they are half a tile height and one alt_step.  Raised to
 *  the map view's 90 the ground stops being foreshortened and height
 *  stops shifting a point at all. */
void cam_scales(const App *a, float *ysc, float *hsc)
{
    const RAtlasLevel *l  = a->sw.level;
    float              pt = (a->gv.pitch > 0.0f ? a->gv.pitch : ARC_PITCH_DEG) * ARC_DEG2RAD;
    *ysc                  = arc_y_scale((float)l->tile_h, pt);
    *hsc                  = arc_alt_scale((float)l->alt_step, pt);
}

/*  Where a grid point lands on the canvas, at its own altitude.  It is
 *  turned about the pivot first when the view is turned, exactly as the
 *  shaders turn the mesh and as screen_to_grid turns back.  Projecting
 *  the point unturned is right only while the point IS the pivot.  With
 *  the pivot at the map's center at the quarters, the anchor hold
 *  re-scrolls to the anchor's unturned place.  The map then shifts on
 *  every turn. */
void grid_to_canvas(const App *a, float fc, float fr, float alt, float *ocx, float *ocy)
{
    const RAtlasLevel *l = a->sw.level;
    float              ysc, hsc;
    cam_scales(a, &ysc, &hsc);
    if (a->angle != 0.0f)
    {
        float ang = a->angle * ARC_DEG2RAD, pc, pr, xc, yc;
        gpu_view_pivot(&a->gv, &pc, &pr);
        xc = fc - pc;
        yc = fr - pr;
        fc = xc * cosf(ang) - yc * sinf(ang) + pc;
        fr = xc * sinf(ang) + yc * cosf(ang) + pr;
    }
    *ocx = arc_origin_x((float)a->sw.ox, (float)l->tile_w) + (fr - fc) * arc_half_w((float)l->tile_w);
    *ocy = arc_origin_y((float)a->sw.oy, (float)l->tile_h) + (fc + fr) * ysc - alt * hsc;
}

/*  Where a grid point lands in the window, in window points: the inverse
 *  of canvas_to_grid_at.  It takes the pan's fraction off as the camera
 *  does, so what is drawn over the window lies on what the GPU draws. */
void grid_to_window(const App *a, float fc, float fr, float alt, float dens, float *wx, float *wy)
{
    float f = cam_px(a), cx, cy;
    grid_to_canvas(a, fc, fr, alt, &cx, &cy);
    *wx = (cx - (float)a->gv.scroll_x - scroll_fx(a)) * f / dens;
    *wy = (cy - (float)a->gv.scroll_y - scroll_fy(a)) * f / dens;
}

/*  The grid point that projects to a window point at a GIVEN altitude:
 *  the inverse of grid_to_canvas, turned back about the pivot.  For a
 *  fixed window point the difference row - col is settled by the canvas
 *  x.  Only the sum moves with altitude, so walking the altitude walks a
 *  diagonal up the grid.  Which is the line of every tile whose geometry
 *  could be under the pointer. */
void canvas_to_grid_at(const App *a, float mx, float my, float dens, float alt, float *ofc, float *ofr)
{
    const RAtlasLevel *l = a->sw.level;
    float              cx, cy, dif, spl, fc, fr, ysc, hsc;
    float              f = cam_px(a);
    cam_scales(a, &ysc, &hsc);
    cx  = mx * dens / f + (float)a->gv.scroll_x + scroll_fx(a);
    cy  = my * dens / f + (float)a->gv.scroll_y + scroll_fy(a);
    dif = (cx - arc_origin_x((float)a->sw.ox, (float)l->tile_w)) / arc_half_w((float)l->tile_w);
    spl = (cy - arc_origin_y((float)a->sw.oy, (float)l->tile_h) + alt * hsc) / ysc;
    fc  = (spl - dif) * 0.5f;
    fr  = (spl + dif) * 0.5f;
    if (a->angle != 0.0f)
    {
        float ang = -a->angle * ARC_DEG2RAD, pc, pr, xc, yc;
        gpu_view_pivot(&a->gv, &pc, &pr);
        xc = fc - pc;
        yc = fr - pr;
        fc = xc * cosf(ang) - yc * sinf(ang) + pc;
        fr = xc * sinf(ang) + yc * cosf(ang) + pr;
    }
    *ofc = fc;
    *ofr = fr;
}

static void canvas_to_grid_plane(const App *a, float cx, float cy, float alt, float *ofc, float *ofr)
{
    const RAtlasLevel *l = a->sw.level;
    float              dif, spl, fc, fr, ysc, hsc;
    cam_scales(a, &ysc, &hsc);
    dif = (cx - arc_origin_x((float)a->sw.ox, (float)l->tile_w)) / arc_half_w((float)l->tile_w);
    spl = (cy - arc_origin_y((float)a->sw.oy, (float)l->tile_h) + alt * hsc) / ysc;
    fc  = (spl - dif) * 0.5f;
    fr  = (spl + dif) * 0.5f;
    if (a->angle != 0.0f)
    {
        float ang = -a->angle * ARC_DEG2RAD, pc, pr, xc, yc;
        gpu_view_pivot(&a->gv, &pc, &pr);
        xc = fc - pc;
        yc = fr - pr;
        fc = xc * cosf(ang) - yc * sinf(ang) + pc;
        fr = xc * sinf(ang) + yc * cosf(ang) + pr;
    }
    *ofc = fc;
    *ofr = fr;
}

/*  The grid point under a window point.  The altitude it was found at.
 *
 *      The ground diamond at its own drawn altitude.
 *      Found by iterating on the altitude.
 *      Turned back about the pivot when the view is turned.
 *
 *  Runs the camera backwards, at whatever pitch it is at.  So the query
 *  tool reads the map view as it reads the game's own.  Returns 0, or -1
 *  off the map. */
int screen_to_grid(App *a, float mx, float my, SDL_Window *win, float *ofc, float *ofr, float *oalt)
{
    const RAtlasLevel *l    = a->sw.level;
    float              dens = SDL_GetWindowPixelDensity(win);
    float              cx, cy, dif, altv = 0.0f, used = 0.0f, fc = 0.0f, fr = 0.0f;
    float              ysc, hsc;
    int                it;
    if (!l || dens <= 0.0f)
        return -1;
    cam_scales(a, &ysc, &hsc);
    {
        float f = cam_px(a);
        cx      = mx * dens / f + (float)a->gv.scroll_x + scroll_fx(a);
        cy      = my * dens / f + (float)a->gv.scroll_y + scroll_fy(a);
    }
    /*  The inverse of grid_to_canvas, and it takes its origin from the
     *  same place rather than restating it. */
    dif = (cx - arc_origin_x((float)a->sw.ox, (float)l->tile_w)) /
          arc_half_w((float)l->tile_w); /* row - col */
    for (it = 0; it < 4; ++it)
    {
        float   spl;
        int32_t col, row, idx;
        used = altv;
        spl  = (cy - arc_origin_y((float)a->sw.oy, (float)l->tile_h) + used * hsc) /
               ysc; /* row + col */
        fc   = (spl - dif) * 0.5f;
        fr   = (spl + dif) * 0.5f;
        if (a->angle != 0.0f)
        {
            /* the free rotation: turn back about the pivot */
            float ang = -a->angle * ARC_DEG2RAD;
            float pc, pr, xc, yc;
            gpu_view_pivot(&a->gv, &pc, &pr);
            xc = fc - pc;
            yc = fr - pr;
            fc = xc * cosf(ang) - yc * sinf(ang) + pc;
            fr = xc * sinf(ang) + yc * cosf(ang) + pr;
        }
        col = (int32_t)floorf(fc);
        row = (int32_t)floorf(fr);
        if (col < 0 || row < 0 || col >= r_map || row >= r_map)
            return -1;
        idx  = row * r_map + col;
        altv = (float)(a->opts.underground
                           ? (a->view->map.altm[idx] & 0x1Fu)
                           : (is_water(a->view->map.xter[idx])
                                  ? ((a->view->map.altm[idx] >> 5) & 0x1Fu)
                                  : (a->view->map.altm[idx] & 0x1Fu)));
    }
    *ofc  = fc;
    *ofr  = fr;
    *oalt = used;
    return 0;
}

/*  The grid cell under the pointer, so every cell can be targeted on its
 *  own.  A sprite standing in front of a cell does not take the pick.
 *  The footprint it belongs to is shown by the highlight instead. */
void pick_tile(App *a, float mx, float my, SDL_Window *win)
{
    float fc, fr, alt;
    a->q_col = a->q_row = -1;
    a->q_mx             = mx;
    a->q_my             = my;
    if (screen_to_grid(a, mx, my, win, &fc, &fr, &alt) != 0)
        return;
    a->q_col = (int32_t)floorf(fc);
    a->q_row = (int32_t)floorf(fr);
}

/*  The camera's anchor: the grid point under the view's center.  It is
 *  the pivot the camera turns about and the point it holds in the middle
 *  of the window while it moves.  So turning or raising the camera
 *  leaves what the view looks at where it is.  The pivot never moves
 *  under the rotation, so the scroll only has to put its unturned canvas
 *  position back at the center. */
void cam_hold(App *a, SDL_Window *win)
{
    float cx, cy, f;
    int   pw = a->win_pw, ph = a->win_ph;
    if (win)
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
    if (!a->sw.level || pw <= 0 || ph <= 0)
        return;
    f = cam_px(a);
    grid_to_canvas(a, a->anch_c, a->anch_r, a->anch_alt, &cx, &cy);
    scroll_set(a, cx - (float)pw * 0.5f / f, cy - (float)ph * 0.5f / f);
    a->anch_sx   = a->gv.scroll_x;
    a->anch_sy   = a->gv.scroll_y;
    a->anch_zoom = a->gv.zoom;
    a->anch_ok   = 1;
}

static void cam_look_fresh(App *a)
{
    float f, fc, fr, cx, cy;
    int   it;
    /*  While a new sprite set's sweep is to come, the anchor is the
     *  truth and the view is not yet made from it. */
    if (!a->sw.level || a->win_pw <= 0 || a->win_ph <= 0)
        return;
    if (a->anch_ok && a->gv.scroll_x == a->anch_sx && a->gv.scroll_y == a->anch_sy && a->gv.zoom == a->anch_zoom)
        return;
    /*  Something moved the view the camera did not: measure the point
     *  under the center afresh, on the ground there. */
    f           = cam_px(a);
    cx          = (float)a->gv.scroll_x + scroll_fx(a) + (float)a->win_pw * 0.5f / f;
    cy          = (float)a->gv.scroll_y + scroll_fy(a) + (float)a->win_ph * 0.5f / f;
    a->anch_alt = a->anch_ok ? a->anch_alt : 0.0f;
    for (it = 0; it < 4; ++it)
    {
        int32_t col, row;
        canvas_to_grid_plane(a, cx, cy, a->anch_alt, &fc, &fr);
        col = (int32_t)floorf(fc), row = (int32_t)floorf(fr);
        if (col < 0 || row < 0 || col >= r_map || row >= r_map)
            break;
        a->anch_alt = (float)(a->view->map.altm[row * r_map + col] & 0x1Fu);
    }
    canvas_to_grid_plane(a, cx, cy, a->anch_alt, &a->anch_c, &a->anch_r);
    a->anch_sx   = a->gv.scroll_x;
    a->anch_sy   = a->gv.scroll_y;
    a->anch_zoom = a->gv.zoom;
    a->anch_ok   = 1;
}

/*  Take the point under the center as the anchor, and pivot on it.  It
 *  is measured once, at the start of a move, so a move cannot walk the
 *  view away a rounded pixel at a time. */
void cam_anchor(App *a, SDL_Window *win)
{
    if (win)
        SDL_GetWindowSizeInPixels(win, &a->win_pw, &a->win_ph);
    cam_look_fresh(a);
    if (!a->anch_ok)
        return;
    a->gv.pivot_c = a->anch_c;
    a->gv.pivot_r = a->anch_r;
    cam_hold(a, win);
}

/*  Put the camera at a pitch and a yaw, and hold the anchor. */
static void cam_set(App *a, float pitch, float yaw, SDL_Window *win)
{
    int turned = a->angle != 0.0f;
    /*  Stored in [0, 360): a move may land on -45 or 360 after taking
     *  the short way round.  The snap and the quarters are tested
     *  against exact values. */
    yaw = fmodf(yaw, 360.0f);
    if (yaw < 0.0f)
        yaw += 360.0f;
    if (yaw > 359.999f)
        yaw = 0.0f;
    a->gv.pitch = pitch;
    a->angle    = yaw;
    a->gv.angle = yaw;
    /*  The mesh is the full one, built once, every water surface and all
     *  four cuts, so a turn never rebuilds it. */
    (void)turned;
    cam_hold(a, win);
}

/*  Start a move to that pitch and yaw.  The camera moves rather than
 *  cuts.  One move carries both of its angles at once: the pitch it
 *  looks down at and the yaw it looks from.  Both ease in and out over
 *  the same fifth of a second, about the anchor.  So the point the view
 *  looks at stays under the middle of the window the whole way.
 *
 *  Anything that puts the camera somewhere comes through here, and a
 *  headless run arrives on the frame it asked for.  The map view is this
 *  camera raised to 90 and turned 45, which is what makes the city
 *  square rather than a diamond.  Leaving takes both back, to the
 *  nearest of the original's own four rotations. */
void cam_go(App *a, float pitch, float yaw, SDL_Window *win)
{
    float d;
    cam_anchor(a, win);
    a->pitch_from = a->gv.pitch > 0.0f ? a->gv.pitch : ARC_PITCH_DEG;
    a->pitch_to   = pitch;
    a->yaw_from   = a->angle;
    /*  The short way round: the yaw asked for, at whichever multiple of 360
     *  lies nearest where the camera is.  A half turn stays a half turn the
     *  way it was asked. */
    d = fmodf(yaw - a->yaw_from, 360.0f);
    if (d > 180.0f)
        d -= 360.0f;
    if (d <= -180.0f)
        d += 360.0f;
    a->yaw_to = a->yaw_from + d;
    a->cam_t  = win ? 0.0f : 1.0f;
    if (a->cam_t >= 1.0f)
        cam_set(a, pitch, yaw, win);
}

/*  A frame's worth of the move. */
void cam_step(App *a, float dt, SDL_Window *win)
{
    float u;
    if (a->cam_t >= 1.0f || dt <= 0.0f)
        return;
    a->cam_t += dt / CAM_SECONDS;
    /*  The last hair of the ease arrives at once.  A frame a hair short
     *  of a quarter reads as the quarter, within a hundredth of a
     *  degree, while the move has not ended.  So nothing settles.  It is
     *  drawn in the quarter's regime with the sweep of the quarter it
     *  left: the city turned twice, for one frame.  A hundredth of the
     *  move is a third of a degree, which no eye sees. */
    if (a->cam_t > 0.99f)
        a->cam_t = 1.0f;
    u = a->cam_t * a->cam_t * (3.0f - 2.0f * a->cam_t);
    cam_set(a,
            a->pitch_from + (a->pitch_to - a->pitch_from) * u,
            a->yaw_from + (a->yaw_to - a->yaw_from) * u,
            win);
    if (a->cam_t >= 1.0f)
    {
        /*  Arrived: the yaw comes back into 0..360, and a hair off the
         *  snap is the snap. */
        float y = fmodf(a->yaw_to + 360.0f, 360.0f);
        if (fabsf(y) < 0.01f || fabsf(y - 360.0f) < 0.01f)
            y = 0.0f;
        a->yaw_to = y;
        cam_set(a, a->pitch_to, y, win);
    }
}

/*  Raise the camera to the map view, or lower it back to the game's own.
 *  The map view draws the mesh whatever the 3D switch says.  So entering
 *  it may have to build the mesh first. */
void set_plan(App *a, int on, SDL_Window *win)
{
    float yaw = a->cam_t < 1.0f ? a->yaw_to : a->angle;
    int   was = geometry_on(a);
    if (!!on == !!a->plan)
        return;
    a->plan = on;
    /*  The map view is a MAP: north up, east right, whatever the city view
     *  was turned to.  Leaving it returns to the quarter the city view was
     *  on. */
    if (on)
        a->plan_from = yaw; /* the city view to come back to */
    yaw = on ? PLAN_YAW : 90.0f * floorf(a->plan_from / 90.0f + 0.5f);
    cam_go(a, on ? 90.0f : ARC_PITCH_DEG, yaw, win);
    if (geometry_on(a) != was)
        a->mesh_dirty = 1;
    a->dirty = 1;
    ui_log(&a->us, on ? "Map view: the camera looks straight down" : "Back to the city view");
}

/*  Turn the camera by `deg`, from where it is going if it is already on
 *  its way.  Keys pressed in a row then add up instead of fighting. */
void rotate_by(App *a, float deg, SDL_Window *win)
{
    float yaw = (a->cam_t < 1.0f ? a->yaw_to : a->angle) + deg;
    cam_go(a, a->gv.pitch, yaw, win);
    if (a->yaw_to != 0.0f)
        ui_log(&a->us, "Turned to %.0f degrees about the view's centre", (double)fmodf(a->yaw_to + 360.0f, 360.0f));
}

/*  Whether the frame draws geometry.  It is one switch.  The ground
 *  mesh, the water shader and the roads move together.  And the map view
 *  forces it on whatever the switch says, because looking straight down
 *  at the sprites shows nothing. */
int geometry_on(const App *a)
{
    /*  Turned or raised off the game's own camera, the mesh is forced
     *  on.  The land art is one diamond drawn for one camera, and
     *  scattering those diamonds to turned positions is what makes a
     *  turned sprite view wrong.  The ground and the roads turn because
     *  they are geometry.  The buildings are still art, and still stand
     *  upright on their tiles, as the original's own four rotations draw
     *  them.  Only off the quarter turns, though: at 90, 180 and 270 the
     *  sweep runs on the view turned the original's way, art and all.
     *  The switch means what it says. */
    float turn        = fmodf(fabsf(a->angle), 90.0f);
    int   off_quarter = turn > 0.01f && turn < 89.99f;
    return a->gv.geometry || a->plan || a->gv.pitch > 30.01f || off_quarter;
}

/*  The camera at a quarter turn on the game's own pitch, 0 to 3.  Such a
 *  turn is a change of PERSPECTIVE and nothing else.  The grid and what
 *  stands on it are the same, the mesh is the one built once from them,
 *  and only the camera moves.  The sprites alone want the original's art
 *  for that orientation.  This is what the original gets by rewriting
 *  the map.  The sweep runs on a turned COPY of the view, projected
 *  unturned.  It composes with the mesh.  A turn of the map and a
 *  quarter turn of the camera about the map's center are the same map on
 *  every cell. */
static int quarter_of(float yaw)
{
    float turn = fmodf(fabsf(yaw), 90.0f);
    if (!(turn < 0.01f || turn > 89.99f))
        return 0;
    return (int)lroundf(yaw / 90.0f) & 3;
}

int view_quarter(const App *a)
{
    return quarter_of(a->angle);
}

/*  The quarter the sweep should be made for: at rest, the camera's.  On
 *  the move, the quarter the camera left until halfway, then the quarter
 *  it goes to.  So the art the settle draws is the art the second half
 *  of the swing drew, moved with its tiles.  The settle draws nothing
 *  new.  Halfway is where the swing is fastest, where one art's cut to
 *  the other's is least seen. */
int sweep_wanted(const App *a)
{
    if (a->cam_t < 1.0f)
        return quarter_of(a->cam_t < 0.5f ? a->yaw_from : a->yaw_to);
    /*  Off a quarter at the game's own pitch, as a key's turn leaves it,
     *  the quarter nearest the camera is wanted.  So the art changes
     *  halfway between two quarters, as an eased turn's does. */
    if (!view_quarter(a) && a->angle != 0.0f && arc_is_game_pitch(a->gv.pitch > 0.0f ? a->gv.pitch : ARC_PITCH_DEG))
        return (int)lroundf(a->angle / 90.0f) & 3;
    return view_quarter(a);
}
