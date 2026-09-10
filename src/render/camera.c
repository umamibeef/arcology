/*  camera.c: the camera, and the two directions between a tile and a
 *  pixel.  Pitch, yaw, zoom and scroll, the eased move between two of
 *  them, and the projection the query box reads.  The same arithmetic
 *  exists twice more, as GPU uniforms and again in the shader, which is
 *  the next thing here worth unifying. */
#include <SDL3/SDL.h>

#include "../app/internal.h"
#include "log.h"
#include "opt.h"
#include "mesh/internal.h"
#include "project.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/*  ==================================================================
 *  Camera and projection
 *
 *  Zoom, pitch, yaw and scroll, and the two directions between a tile
 *  and a pixel that the query box uses.
 *  ================================================================== */
/*  Continuous zoom, as the renderer brief has it (section 5).  The three
 *  art sets are levels of detail across one range, each used where it is
 *  closest to native.  8 px below 0.354x, 16 px to 0.707x, 32 px above,
 *  the switch points the geometric means.  And the canvas resolved by
 *  the remaining factor, never more than about 1.41x either way.  `mx`,
 *  `my` is the window point that stays put, in pixels. */
void zoom_to(App *a, float z, float mx, float my)
{
    int32_t set;
    float   native, f, f_old;
    float   cx, cy;
    if (z < 0.125f)
        z = 0.125f;
    if (z > 32.0f)
        z = 32.0f; /* past 4 the art is blocky.  The mesh repays a much closer look */
    set    = z >= 0.7071f ? 32 : (z >= 0.3536f ? 16 : 8);
    native = (float)set / 32.0f;
    f      = z / native;
    f_old  = a->gv.zoom > 0.0f ? a->gv.zoom : 1.0f;
    /* the canvas point under the pointer, in the current set's pixels */
    cx = (float)a->gv.scroll_x + mx / ((float)a->gv.scale * f_old);
    cy = (float)a->gv.scroll_y + my / ((float)a->gv.scale * f_old);
    if (set != a->opts.zoom)
    {
        float ratio = (float)set / (float)a->opts.zoom;
        cx *= ratio;
        cy *= ratio;
        /*  The sprite set changes.  The mesh does not.  It is the world
         *  in world units and reads the artwork it needs from the finest
         *  level.  So nothing about it depends on which set is drawn. */
        a->opts.zoom = set;
        a->dirty     = 1;
    }
    a->gv.zoom     = f;
    a->zoom_world  = z;
    a->gv.scroll_x = (int32_t)(cx - mx / ((float)a->gv.scale * f));
    a->gv.scroll_y = (int32_t)(cy - my / ((float)a->gv.scale * f));
}

/*  The tile under a window point, from the sweep's own projection run
 *  backwards.  Along a diamond row + col and row - col are linear in y
 *  and x.  The altitude term is settled by iterating on the tile found. */
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
    float              f = (float)a->gv.scale * (a->gv.zoom > 0.0f ? a->gv.zoom : 1.0f);
    cam_scales(a, &ysc, &hsc);
    cx  = mx * dens / f + (float)a->gv.scroll_x;
    cy  = my * dens / f + (float)a->gv.scroll_y;
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
        float f = (float)a->gv.scale * (a->gv.zoom > 0.0f ? a->gv.zoom : 1.0f);
        cx      = mx * dens / f + (float)a->gv.scroll_x;
        cy      = my * dens / f + (float)a->gv.scroll_y;
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
        if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
            return -1;
        idx  = row * R_MAP + col;
        altv = (float)(a->opts.underground
                           ? (a->view->altm[idx] & 0x1Fu)
                           : (is_water(a->view->xter[idx])
                                  ? ((a->view->altm[idx] >> 5) & 0x1Fu)
                                  : (a->view->altm[idx] & 0x1Fu)));
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
    a->q_mx = mx;
    a->q_my = my;
    if (screen_to_grid(a, mx, my, win, &fc, &fr, &alt) != 0)
        return;
    a->q_col = (int32_t)floorf(fc);
    a->q_row = (int32_t)floorf(fr);
    a->q_fx  = fc;
    a->q_fy  = fr;
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
    int   pw, ph;
    if (!win || !a->sw.level || !SDL_GetWindowSizeInPixels(win, &pw, &ph))
        return;
    f = (float)a->gv.scale * (a->gv.zoom > 0.0f ? a->gv.zoom : 1.0f);
    grid_to_canvas(a, a->anch_c, a->anch_r, a->anch_alt, &cx, &cy);
    a->gv.scroll_x = (int32_t)lroundf(cx - (float)pw * 0.5f / f);
    a->gv.scroll_y = (int32_t)lroundf(cy - (float)ph * 0.5f / f);
    a->anch_sx     = a->gv.scroll_x;
    a->anch_sy     = a->gv.scroll_y;
    a->anch_zoom   = a->gv.zoom;
    a->anch_ok     = 1;
}

/*  Take the point under the center as the anchor, and pivot on it.  It
 *  is measured once, at the start of a move, so a move cannot walk the
 *  view away a rounded pixel at a time. */
void cam_anchor(App *a, SDL_Window *win)
{
    float dens = win ? SDL_GetWindowPixelDensity(win) : 0.0f;
    float fc, fr, alt;
    int   pw, ph;
    if (!a->sw.level || dens <= 0.0f || !SDL_GetWindowSizeInPixels(win, &pw, &ph))
        return;
    /*  Still the point under the center.  Nothing has scrolled or zoomed
     *  since the last hold put it there.  Then it is kept as it is.
     *  Measured afresh each time, through an integer scroll and an
     *  iterated inverse, it drifted a little per turn.  Four quarters
     *  did not come back to the same view. */
    if (a->anch_ok && a->gv.scroll_x == a->anch_sx && a->gv.scroll_y == a->anch_sy && a->gv.zoom == a->anch_zoom)
    {
        a->gv.pivot_c = a->anch_c;
        a->gv.pivot_r = a->anch_r;
        cam_hold(a, win);
        return;
    }
    if (screen_to_grid(a, (float)pw * 0.5f / dens, (float)ph * 0.5f / dens, win, &fc, &fr, &alt) != 0)
        return;
    a->anch_c     = fc;
    a->anch_r     = fr;
    a->anch_alt   = alt;
    a->gv.pivot_c = fc;
    a->gv.pivot_r = fr;
    cam_hold(a, win);
}

/*  Put the camera at a pitch and a yaw, and hold the anchor. */
void cam_set(App *a, float pitch, float yaw, SDL_Window *win)
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

/*  Start a move to that pitch and yaw. */
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
