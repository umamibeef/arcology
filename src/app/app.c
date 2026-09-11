/*  arcology: the game: the reconstruction simulated and drawn on the
 *  GPU.
 *
 *      arcology <assets dir> <city file> [--zoom 8|16|32] [--scale N]
 *              [--check out.png]
 *
 *  The simulation is the verified reconstruction, run as the original's
 *  main loop runs it.  It runs a phase of the 25-phase clock when the
 *  speed's deadline passes.  It runs the moving things every fifteen
 *  ticks, the palette runs every twelve and ninety.  The renderer is the
 *  software sweep's op list drawn through SDL_GPU, and --check draws one
 *  frame both ways and says whether they agree.
 *
 *  Separated by task, each section behind a banner:
 *
 *      The process, its window and its phases Preferences and themes The
 *      city and its clock Building what gets drawn Camera and projection
 *      The interface, filled and applied Finding things on disk Startup
 *      and the frame loop
 *
 *  Keys
 *      1..5        speed: paused, turtle, llama, cheetah, african swallow
 *      space       pause / resume
 *      arrows      pan          + / -   zoom level        [ ]  pixel scale
 *      t           geometry: the art, or the mesh (terrain, water, roads)
 *      n           the map view: the camera straight down
 *      , . 0       turn the camera, and back to the snap
 *      v / shift-v data views                      u   underground
 *      g           the sprites' grid outline on the mesh
 *      m           debug: draw the sweep with no depth plane
 *      p           screenshot and check against the software rasteriser
 *      escape      quit */
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include <SDL3/SDL.h>

#include "arc_version.h"
#include "arco.h"
#define JSMN_STATIC
#include "adapt.h"
#include "internal.h"
#include "mesh/model.h"
#include "script.h"
#include "atlas/atlas.h"
#include "city.h"
#include "gpu/gpu.h"
#include "jsmn.h"
#include "log.h"
#include "mesh/mesh.h"
#include "music.h"
#include "opt.h"
#include "dump.h"
#include "options.h"
#include "project.h"
#include "soft/soft.h"
#include "sound.h"
#include "ui.h"
#ifndef _WIN32
    #include <sys/utsname.h>
#endif
#include "sim.h"
#include "net/net.h"
#include "build.h"
#include "incr.h"

/*  The key every letter shortcut needs: the Mac's command key, and Ctrl
 *  everywhere else.  The bare keys that remain, the digits for the
 *  speeds, space, the zoom and scale keys, the arrows, are game
 *  controls, not menu shortcuts. */
#ifdef __APPLE__
    #define KMOD_CMD SDL_KMOD_GUI
#else
    #define KMOD_CMD SDL_KMOD_CTRL
#endif

/*  ==================================================================
 *  The process, its window and its phases
 *
 *  What to call the build, what the title bar says, and where the time
 *  went while it started.
 *  ================================================================== */

/*  Nanoseconds from SDL_GetTicksNS as milliseconds, for the log. */

/*  The original's clock.  TickCount is 60 Hz.  The speed's delay per
 *  phase is the word table at A5+0xC9A indexed by MISC[1019]: 0, 0, 36,
 *  12, 0.  Speeds 0 and 1 never tick, 2 to 4 wait for their deadline,
 *  and 5 runs a phase every time round the loop without a deadline at
 *  all ($2A..$56). */

/*  the window title and the default save name follow the city */
/* ---- preferences ------------------------------------------------------- */

/*  settings.json, in the per-user place SDL knows for the platform:
 *  Application Support on macOS, AppData on Windows, ~/.local/share on
 *  Linux.  One flat object of strings and numbers, JSON because that is
 *  what everything else this program writes is.  Read whole and written
 *  whole.  The theme is the first key, and a few more will not need
 *  anything cleverer.  A missing file is simply no preference.  Nested
 *  values are not ours and are left alone.  A quote or a backslash in a
 *  value is escaped on the way out and not unescaped on the way in.
 *  This for theme names and numbers never arises. */

/* ---- themes ------------------------------------------------------------ */

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

/*  The view a frame is drawn with: the switches, the map view's
 *  overrides, and the underground, which is the ground alone. */
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
    return view_quarter(a);
}

/*  A turn, from a key or the menu.  With the geometry off it is the
 *  original's: a quarter at once, no camera move and no mesh in between.
 *  With it on, the camera turns by `deg` and a quarter settles when it
 *  comes to rest. */
void app_turn(App *a, float deg, SDL_Window *win)
{
    if (!geometry_on(a))
    {
        float y = fmodf(a->angle + (deg < 0.0f ? -90.0f : 90.0f) + 360.0f, 360.0f);
        cam_anchor(a, win);
        a->angle = a->gv.angle = y;
        a->cam_t               = 1.0f;
        settle_quarter_turn(a, win);
    }
    else
        rotate_by(a, deg, win);
}

/*  A quarter turn come to rest.
 *
 *      The camera pivots on the map's center from here on.
 *      So the mesh and the turned sweep meet.
 *      The anchor is held under the view's center.
 *
 *  The sweep is redone at once. */
void settle_quarter_turn(App *a, SDL_Window *win)
{
    float turn = fmodf(fabsf(a->angle), 90.0f);
    if (!(turn < 0.01f || turn > 89.99f))
        return; /* not on a quarter */
    /*  The snap is a quarter too: the fourth turn of four must hold the
     *  anchor and redo the sweep like the other three. */
    a->angle = a->gv.angle = (float)(view_quarter(a) * 90);
    a->yaw_to = a->yaw_from = a->angle;
    if (view_quarter(a))
        a->gv.pivot_c = a->gv.pivot_r = (float)(MAP_W / 2);
    else
    {
        a->gv.pivot_c = a->anch_c;
        a->gv.pivot_r = a->anch_r;
    }
    a->q_col = a->q_row = -1;
    a->dirty            = 1;
    /*  The sweep first, then the hold: the hold measures the anchor's
     *  canvas position against the sweep's origin, and a quarter's sweep
     *  has its own.  Held against the old one, the settle's frame showed
     *  another part of the city entirely before the next frame's hold put
     *  it right. */
    if (win && a->sw.level && resweep(a) != 0)
        return;
    cam_hold(a, win);
    ui_log(&a->us, "Turned to %d degrees", view_quarter(a) * 90);
}

RGpuView frame_view(const App *a)
{
    RGpuView fv      = a->gv;
    fv.geometry      = geometry_on(a);
    fv.sweep_quarter = a->sweep_quarter;
    /*  The map view is read tile by tile.  So it carries the grid
     *  whether or not the city view does, from the moment the camera
     *  starts to rise. */
    fv.grid = a->gv.grid || a->plan || a->gv.pitch > 30.01f;
    fv.plan = a->plan; /* the network tints are the map view's alone */
    fv.markings  = a->gv.markings;
    fv.furniture = a->gv.furniture;
    fv.margins = a->gv.margins;
    if (a->opts.underground && fv.geometry)
        fv.underground = 1;
    return fv;
}

/*  ---- the UI ------------------------------------------------------ */

/*  The budget block's departments as sim.h names them.  The first three
 *  slots the reconstruction has not named. */

/*  The camera moves rather than cuts.  One move carries both of the
 *  camera's angles at once.  They are the pitch it looks down at and the
 *  yaw it looks from.  Both ease in and out over the same fifth of a
 *  second, about the anchor.  So the point the view looks at stays under
 *  the middle of the window the whole way.  Anything that puts the
 *  camera somewhere goes through cam_go, and a headless run arrives
 *  there on the frame it asked.  The map view is that camera raised to
 *  90 and turned 45, which is what makes the city square rather than a
 *  diamond.  Leaving takes both back, to the nearest of the original's
 *  own four rotations. */

/*  ==================================================================
 *  Finding things on disk
 *
 *  Where the assets are, where the cities are, and what a name on the
 *  command line resolves to.
 *  ================================================================== */
/* ---- finding things ----------------------------------------------------
 *
 *  The old invocation wanted the assets directory and the city spelled
 *  out every time.  Both can be found: the assets sit beside the binary
 *  or a few directories above it, and the cities live wherever the game
 *  was installed.  Either can still be given outright.
 * ------------------------------------------------------------------ */

/*  ==================================================================
 *  Startup and the frame loop
 *
 *  Options are read once, into Startup, and the loop that follows shares
 *  nothing with them.
 *  ================================================================== */
/*  The frame loop: events, the clock, the camera, and the frame itself.
 *  It shares nothing with the startup options, checked, not assumed.  So
 *  it takes only the app and its window. */
/*  One frame's advance of the world and the camera, the live loop's and
 *  the headless turn's alike.  It runs the clock, and redoes the sweep
 *  when the camera's quarter is not the one it was made for, the mesh
 *  rebuilt when dirty, the traffic.  Then the camera's move by `dt`
 *  seconds (none when negative) and a quarter turn come to rest settled.
 *  -1 when the sweep fails. */
/*  The mesh build was abandoned.  A rule that raised has already named
 *  itself.  This says what became of the frame.  In the per-frame path
 *  the state holds for as long as the script stays broken.  So the line
 *  is said when it CHANGES rather than once a frame, and the build
 *  coming back says so too. */
static int s_build_bad;

static void build_failed(void)
{
    if (!s_build_bad)
        R_ERR("mesh", "the build was abandoned; the mesh it would have replaced still stands");
    s_build_bad = 1;
}

static void build_ok(void)
{
    if (s_build_bad)
        R_NOTE("mesh", "the build is whole again");
    s_build_bad = 0;
}

int app_advance(App *a, SDL_Window *win, float dt, float time)
{
    step_clock(a);
    /*  The sweep is made for one orientation.  Whenever the camera's
     *  quarter is not the one it was made for.  A turn come to rest on
     *  another, or leaving one for a free turn or the snap.  It is
     *  redone before the frame: drawn with any other camera it shows the
     *  city turned twice, or turned at the snap. */
    if (sweep_wanted(a) != a->sweep_quarter)
        a->dirty = 1;
    if (a->dirty && resweep(a) != 0)
        return -1;
    if (a->mesh_dirty)
    {
        if (remesh(a) != 0)
            build_failed();
        else
            build_ok();
        /*  The bar comes down with the build that raised it.  Only a
         *  build puts it up, and the last thing it says is said while
         *  the mesh goes to the card.  So the end of the build is where
         *  it goes, and nothing else has to take it down. */
        a->us.loading[0] = 0;
    }
    a->gv.time = time;
    music_update(a->mus); /* the original's scheduler, once a pass */
    if (traffic_frame(a, a->gv.time) != 0)
        fprintf(stderr, "traffic build failed\n");
    if (dt >= 0.0f)
    {
        /*  The camera's travel, a frame's worth. */
        cam_step(a, dt, win);
        /*  A quarter turn come to rest turns the CITY, as the original
         *  does (its rotate rewrites the map).  The camera goes back to
         *  the snap: art and mesh alike are then drawn as at rotation 0,
         *  whatever the switches say.  Off the quarters the camera turn
         *  stays a camera turn. */
        if (a->cam_t >= 1.0f && view_quarter(a) && (a->gv.pivot_c != (float)(MAP_W / 2) || a->gv.pivot_r != (float)(MAP_W / 2)))
            settle_quarter_turn(a, win);
        else if (a->cam_t >= 1.0f && !view_quarter(a) && fmodf(fabsf(a->angle), 90.0f) < 0.01f && view_quarter(a) != a->sweep_quarter)
            settle_quarter_turn(a, win); /* come to rest at the snap from a quarter: hold and re-sweep */
    }
    return 0;
}

float app_frame_dt(App *a)
{
    uint64_t now = SDL_GetTicksNS();
    float    dt  = -1.0f; /* the first frame moves no camera */
    if (a->last_ns)
    {
        float ms    = (float)((double)(now - a->last_ns) / 1e6);
        a->frame_ms = a->frame_ms > 0.0f ? a->frame_ms * 0.9f + ms * 0.1f : ms;
        a->fps      = a->frame_ms > 0.0f ? 1000.0f / a->frame_ms : 0.0f;
        dt          = ms > 100.0f ? 0.1f : ms / 1000.0f; /* capped so a stall does not jump the camera */
    }
    a->last_ns = now;
    return dt;
}

/*  THE BAR, MOVED FROM INSIDE THE BUILD.
 *
 *  A build holds the frame for a second or more.  So the only way a bar
 *  moves while it runs is if something paints from in there.  The GPU
 *  still holds the world the LAST build left, so what this paints is
 *  that world under a bar that is going up.  Nothing of the build is on
 *  the screen until it finishes, which is what a loading screen is.
 *
 *  The composing script names the steps (scripts/compose/world.lua), so
 *  what the bar counts is the script's own, not a guess made here.
 *
 *  Never on a headless run.  There the frame is read back into an image,
 *  and one readback a step would cost more than the build it is
 *  reporting on. */
static void build_bar(void *ud, const char *step, int i, int n)
{
    App     *a = (App *)ud;
    RGpuView fv;
    if (!a || !a->ui || a->offscreen)
        return;
    snprintf(a->us.loading, sizeof a->us.loading, "Building the world");
    snprintf(a->us.loading_note, sizeof a->us.loading_note, "%s", step ? step : "");
    a->us.loading_step  = i;
    a->us.loading_steps = n;
    /*  The state the rest of the window reads was filled on the last
     *  frame the app drew, and the build moves none of it.  So the bar
     *  is the only thing to put in. */
    ui_frame(a->ui, &a->us);
    fv = frame_view(a);
    gpu_frame(a->gpu, &fv, backdrop(a), ui_render, a->ui);
}

int app_frame(App *a, SDL_Window *win, float dt, float time, RImage *out)
{
    RGpuView fv;
    int      rc = 0;
    /*  The script is WATCHED.  Its file is looked at once a frame and
     *  read again when it changes.  The world is drawn again on that or
     *  on the script asking for it.  Which is what makes a change to a
     *  rule a file save rather than a compile. */
    if (script_stale() && script_reload())
    {
        R_NOTE("lua", "read again: %d rules", script_rules());
        /*  The rebuild waits a frame where there is a window to say so
         *  on, and the frame in between carries the panel.  Headless
         *  there is nobody to tell, so it goes straight on. */
        if (a->live && a->ui)
        {
            /*  Two steps to a reading: the scripts, then the world they
             *  describe.  The first is done by the time this is drawn.
             *  It takes a few milliseconds.  And the second is what the
             *  frame after this one spends a second and a half on, so
             *  that is what the bar names. */
            int files = script_files(NULL);
            snprintf(a->us.loading, sizeof a->us.loading, "Building the world");
            snprintf(a->us.loading_note, sizeof a->us.loading_note,
                     "%d script%s read", files, files == 1 ? "" : "s");
            a->us.loading_step  = 1;
            a->us.loading_steps = 2;
            a->reload_pending   = 1;
        }
        else
            a->mesh_dirty = 1;
    }
    else if (a->reload_pending)
    {
        a->reload_pending  = 0;
        a->us.loading[0]   = 0;
        a->mesh_dirty      = 1;
    }
    if (script_take_dirty())
        a->mesh_dirty = 1;
    if (app_advance(a, win, dt, time) != 0)
        return -1;
    if (a->ui)
    {
        a->win_density = SDL_GetWindowPixelDensity(win);
        ui_fill(a);
        ui_frame(a->ui, &a->us);
    }
    /*  The underground view is the original's.
     *
     *      The ground at its own altitude.
     *      The seabed under water.
     *      In the underground art.
     *      With the pipes and subways on it.
     *
     *  The surface mesh and the water are not part of it. */
    fv = frame_view(a);
    if (out || a->offscreen)
    {
        /*  Read back: into the caller's image, or into one thrown away
         *  when the frame is only for what it settles and fills in. */
        RImage scratch, *img = out ? out : &scratch;
        int    pw, ph;
        if (!SDL_GetWindowSizeInPixels(win, &pw, &ph))
            return -1;
        rc = gpu_readback(a->gpu, &fv, backdrop(a), pw, ph, a->gv.scale, img, a->ui ? ui_render : NULL, a->ui);
        if (rc != 0)
            R_ERR("frame", "readback failed: %s", SDL_GetError());
        else if (!out)
            image_free(&scratch);
    }
    else if (gpu_frame(a->gpu, &fv, backdrop(a), a->ui ? ui_render : NULL, a->ui) != 0)
        R_ERR("frame", "%s", SDL_GetError());
    if (a->ui)
    {
        int pw, ph;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        ui_apply(a, win, pw, ph);
    }
    return rc;
}

static void frame_loop(App *a, SDL_Window *win)
{
    while (!a->quit)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            int ui_owns = ui_event(a->ui, &e);
            if (g_dev.input_log)
                switch (e.type)
                {
                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    case SDL_EVENT_MOUSE_BUTTON_UP:
                        dumpf("input  button %s %d at %.0f,%.0f  ui_owns %d\n", e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? "down" : "up", e.button.button, (double)e.button.x, (double)e.button.y, ui_owns);
                        break;
                    case SDL_EVENT_MOUSE_MOTION:
                        dumpf("input  motion %.0f,%.0f rel %.0f,%.0f  state %02x  queried %02x  ui_owns %d  drag %d\n", (double)e.motion.x, (double)e.motion.y, (double)e.motion.xrel, (double)e.motion.yrel, (unsigned)e.motion.state, (unsigned)SDL_GetMouseState(NULL, NULL), ui_owns, a->drag);
                        break;
                    case SDL_EVENT_MOUSE_WHEEL:
                        dumpf("input  wheel %.2f,%.2f  ui_owns %d\n", (double)e.wheel.x, (double)e.wheel.y, ui_owns);
                        break;
                    case SDL_EVENT_FINGER_DOWN:
                    case SDL_EVENT_FINGER_UP:
                    case SDL_EVENT_FINGER_MOTION:
                        dumpf("input  finger %d at %.3f,%.3f\n", (int)e.type, (double)e.tfinger.x, (double)e.tfinger.y);
                        break;
                    default: break;
                }
            if (e.type == SDL_EVENT_QUIT)
                a->quit = 1;
            else if (ui_owns)
                continue;
            else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     e.button.button == SDL_BUTTON_LEFT)
            {
                if (a->inspect && (SDL_GetModState() & SDL_KMOD_SHIFT))
                {
                    /*  Shift-click while inspecting: the report on what is
                     *  under the pointer, on the clipboard.  The pick and
                     *  the fill are done again here so what is copied is
                     *  what the click was over, not the frame before it. */
                    pick_tile(a, e.button.x, e.button.y, win);
                    a->win_density = SDL_GetWindowPixelDensity(win);
                    ui_fill(a);
                    inspect_copy(a);
                }
                else if (a->plan && (SDL_GetModState() & SDL_KMOD_SHIFT))
                {
                    /*  Shift-drag in the map view: select an area for a
                     *  debug report (area.c), tile to tile. */
                    pick_tile(a, e.button.x, e.button.y, win);
                    a->sel    = a->q_col >= 0;
                    a->sel_c0 = a->sel_c1 = a->q_col;
                    a->sel_r0 = a->sel_r1 = a->q_row;
                }
                else
                {
                    a->drag     = 1;
                    a->drag_len = 0.0f;
                }
            }
            else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                     e.button.button == SDL_BUTTON_LEFT && a->sel)
            {
                a->sel = 0;
                area_report_ui(a);
            }
            else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                     e.button.button == SDL_BUTTON_LEFT)
            {
                a->drag = 0;
                /*  A click, not a drag: the selected tool acts on the
                 *  tile under the pointer. */
                if (a->drag_len < 4.0f)
                {
                    pick_tile(a, e.button.x, e.button.y, win);
                    if (a->q_col >= 0)
                        use_tool(a, win);
                }
            }
            else if (e.type == SDL_EVENT_MOUSE_MOTION)
            {
                /*  The left button held, by the event's own account or
                 *  the mouse's. macOS synthesises a three-finger drag as
                 *  a left-button drag.  The motion can arrive without
                 *  the button in the event's state: and sometimes
                 *  without the press that would have started the drag.
                 *  Either report is enough.  The first such motion
                 *  starts the drag itself. */
                int held = (e.motion.state & SDL_BUTTON_LMASK) || (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK);
                if (held && !a->sel && !a->drag)
                    a->drag = 1, a->drag_len = 0.0f;
                if (a->drag && held)
                {
                    float dens = SDL_GetWindowPixelDensity(win);
                    int   dx, dy;
                    a->drag_ax += e.motion.xrel * dens / (float)a->gv.scale;
                    a->drag_ay += e.motion.yrel * dens / (float)a->gv.scale;
                    a->drag_len += fabsf(e.motion.xrel) + fabsf(e.motion.yrel);
                    dx = (int)a->drag_ax;
                    dy = (int)a->drag_ay;
                    a->drag_ax -= (float)dx;
                    a->drag_ay -= (float)dy;
                    a->gv.scroll_x -= dx;
                    a->gv.scroll_y -= dy;
                }
                pick_tile(a, e.motion.x, e.motion.y, win);
                if (a->sel && a->q_col >= 0)
                {
                    a->sel_c1 = a->q_col;
                    a->sel_r1 = a->q_row;
                }
            }
            else if (e.type == SDL_EVENT_MOUSE_WHEEL)
            {
                float dens = SDL_GetWindowPixelDensity(win);
                float step = powf(1.12f, e.wheel.y);
                zoom_to(a, a->zoom_world * step, e.wheel.mouse_x * dens, e.wheel.mouse_y * dens);
            }
            else if (e.type == SDL_EVENT_KEY_DOWN)
            {
                SDL_Keycode k     = e.key.key;
                int         shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
                int         mod   = (e.key.mod & KMOD_CMD) != 0;
                int         pw, ph;
                SDL_GetWindowSizeInPixels(win, &pw, &ph);
                if (k == SDLK_ESCAPE)
                    a->quit = 1;
                else if (k >= SDLK_1 && k <= SDLK_5)
                    set_speed(a, (int32_t)(k - SDLK_0));
                else if (k == SDLK_SPACE)
                    set_speed(a, a->speed > 1 ? 1 : a->last_speed);
                else if (k == SDLK_EQUALS || k == SDLK_PLUS ||
                         k == SDLK_KP_PLUS)
                    zoom_to(a, a->zoom_world * 2.0f, (float)pw * 0.5f, (float)ph * 0.5f);
                else if (k == SDLK_MINUS || k == SDLK_KP_MINUS)
                    zoom_to(a, a->zoom_world * 0.5f, (float)pw * 0.5f, (float)ph * 0.5f);
                else if (k == SDLK_LEFTBRACKET && a->gv.scale > 1)
                    a->gv.scale--;
                else if (k == SDLK_RIGHTBRACKET && a->gv.scale < 4)
                    a->gv.scale++;
                /*  the original's own shortcuts, its letters: File and Windows */
                else if (mod && !shift && k == SDLK_L)
                    a->us.open_load = 1;
                else if (mod && !shift && k == SDLK_S)
                    a->us.want_save = 1;
                else if (mod && !shift && k == SDLK_Q)
                    a->quit = 1;
                else if (mod && !shift && k == SDLK_B)
                    a->us.show_budget = 1;
                else if (mod && !shift && k == SDLK_C)
                    a->us.show_city = 1;
                else if (mod && !shift && k == SDLK_I)
                {
                    a->inspect   = !a->inspect;
                    a->inspect_c = a->inspect_r = -1;
                }
                else if (mod && !shift && k == SDLK_G)
                    a->us.show_graphs = 1;
                else if (mod && !shift && k == SDLK_M)
                    ui_log(&a->us, "Map window: not yet ported");
                else if (mod && shift && k == SDLK_T)
                {
                    /*  One switch for all of the geometry: the ground, the
                     *  water and the roads on it. */
                    a->gv.geometry = !a->gv.geometry;
                    a->mesh_dirty  = 1;
                }
                else if (mod && shift && k == SDLK_G)
                {
                    a->gv.grid = !a->gv.grid;
                    if (a->prefs_ok)
                        prefs_set("grid", a->gv.grid ? "on" : "off"); /* remembered */
                }
                else if (mod && shift && k == SDLK_M)
                    a->gv.plain_sweep = !a->gv.plain_sweep;
                else if (mod && shift && k == SDLK_U)
                {
                    a->opts.underground = !a->opts.underground;
                    a->dirty            = 1;
                    a->mesh_dirty       = 1;
                }
                else if (mod && k == SDLK_V)
                {
                    a->opts.view = shift ? (a->opts.view + 11) % 12
                                         : (a->opts.view + 1) % 12;
                    a->dirty     = 1;
                }
                else if (mod && shift && k == SDLK_P)
                    a->us.want_screenshot = 1;
                else if (mod && shift && k == SDLK_N)
                    set_plan(a, !a->plan, win); /* N for the map view */
                else if (k == SDLK_COMMA || k == SDLK_PERIOD)
                    app_turn(a, k == SDLK_COMMA ? -15.0f : 15.0f, win);
                else if (k == SDLK_0)
                    cam_go(a, a->gv.pitch, a->plan ? PLAN_YAW : 0.0f, win); /* back to the snap */
            }
        }
        if (!ui_wants_keyboard(a->ui))
        {
            const bool *ks  = SDL_GetKeyboardState(NULL);
            int32_t     pan = 24;
            if (ks[SDL_SCANCODE_LEFT])
                a->gv.scroll_x -= pan;
            if (ks[SDL_SCANCODE_RIGHT])
                a->gv.scroll_x += pan;
            if (ks[SDL_SCANCODE_UP])
                a->gv.scroll_y -= pan;
            if (ks[SDL_SCANCODE_DOWN])
                a->gv.scroll_y += pan;
        }

        {
            float dt = app_frame_dt(a);
            if (app_frame(a, win, dt, (float)((double)(a->last_ns - a->t0_ns) / 1e9), NULL) != 0)
                break;
        }
    }
}

int game_main(int argc, char **argv)
{
    /*  Zeroed: the struct is a few hundred fields and only some are set
     *  below, so anything else began as whatever the stack held.  The
     *  selection's flash timer was one of them.  A large enough garbage
     *  value held `flash` true for minutes at a time, which suppressed
     *  the hover outline the inspector draws. */
    App         a  = {0};
    int         rc = 0; /* how the run went, whichever mode it was */
    SDL_Window *win;
    Startup     o;
    char        err[256];
    /*  Phase clocks for the init log.  SDL3's ticks need no SDL_Init. */
    const uint64_t t_start = SDL_GetTicksNS();
    uint64_t       t_phase = t_start, t_atlas = 0, t_city = 0, t_gpu = 0, t_sweep = 0;
#define PHASE_MS(var)                    \
    do                                   \
    {                                    \
        uint64_t now = SDL_GetTicksNS(); \
        (var)        = now - t_phase;    \
        t_phase      = now;              \
    } while (0)
    const char *check_out, *shot_out, *theme_dir;
    int         check, ww, wh, headless;
    int         run_frames, run_speed;
    float       zoomf;
    int         sound_test, want_mesh_check;
    const char *song_arg, *song_out;
    int         have_scroll, scroll_x, scroll_y;
    int         have_centre, centre_col, centre_row;
    int         have_pick;
    float       pick_x, pick_y;
    int32_t     pixel_scale;
    char        assets_dir[1024], city_path[1024];
    int         i;
    {
        int opt_rc = parse_options(&o, &a, argc, argv);
        if (opt_rc != 0)
            return opt_rc < 0 ? 0 : opt_rc;
    }
    dump_open(g_dev.dump_to); /* the developer dumps' sink, before anything dumps */
    furniture_enable(a.gv.furniture);
    margin_enable(a.gv.margins);
    check_out       = o.check_out;
    shot_out        = o.shot_out;
    theme_dir       = o.theme_dir;
    check           = o.check;
    ww              = o.ww;
    wh              = o.wh;
    run_frames      = o.run_frames;
    run_speed       = o.run_speed;
    zoomf           = o.zoomf;
    sound_test      = o.sound_test;
    song_arg        = o.song_arg;
    song_out        = o.song_out;
    want_mesh_check = o.want_mesh_check;
    have_scroll     = o.have_scroll;
    scroll_x        = o.scroll_x;
    scroll_y        = o.scroll_y;
    have_centre     = o.have_centre;
    centre_col      = o.centre_col;
    centre_row      = o.centre_row;
    have_pick       = o.have_pick;
    pick_x          = o.pick_x;
    pick_y          = o.pick_y;
    pixel_scale     = o.pixel_scale;
    memcpy(assets_dir, o.assets_dir, sizeof assets_dir);
    memcpy(city_path, o.city_path, sizeof city_path);

    /*  where the cities are, for resolving a name and for the menu */
    find_cities(a.us.city_dir, sizeof a.us.city_dir);
    scan_cities(&a.us);
    if (a.us.city_dir[0])
        R_DBG("cities", "%s (%d)", a.us.city_dir, a.us.n_cities);
    else
        R_DBG("cities", "none found");

    if (city_path[0])
    {
        char resolved[1024];
        if (!resolve_city(resolved, sizeof resolved, city_path, a.us.city_dir))
        {
            R_ERR("city", "no city called %s", city_path);
            return 1;
        }
        snprintf(city_path, sizeof city_path, "%s", resolved);
    }

    if (!assets_dir[0] && !find_assets(assets_dir, sizeof assets_dir, argv[0]))
    {
        R_ERR("assets", "not found; pass --assets DIR");
        return 1;
    }
    R_DBG("assets", "%s", assets_dir);
    if (song_out)
    {
        /*  A song to a WAV and out: no window, no device.  The check in
         *  tests/music_check.py runs this.  So can anyone who wants to
         *  hear a song without the game. */
        RMusic *mus = music_create(assets_dir, 0);
        int     wav_rc = mus ? music_render_wav(mus, song_arg, song_out, 44100) : -1;
        if (wav_rc == 0)
            R_NOTE("music", "%s -> %s", song_arg, song_out);
        else
            R_ERR("music", "could not render %s%s", song_arg, mus ? "" : " (no assets/music: run tools/import_assets.py)");
        music_destroy(mus);
        return wav_rc ? 1 : 0;
    }
    if (atlas_load(&a.atlas, assets_dir) != 0)
    {
        R_ERR("atlas", "%s", a.atlas.err);
        return 1;
    }
    PHASE_MS(t_atlas);
    {
        int32_t tiles = 0;
        char    zooms[32];
        int     zn = 0;
        zooms[0]   = 0;
        for (i = 0; i < a.atlas.n_levels; i++)
        {
            tiles += a.atlas.level[i].n_tiles;
            zn += snprintf(zooms + zn, sizeof zooms - (size_t)zn, "%s%d", i ? "/" : "", (int)a.atlas.level[i].zoom);
        }
        R_NOTE("atlas", "%d levels (%s px), %d tiles, %d animated runs, %.0f ms", (int)a.atlas.n_levels, zooms, (int)tiles, (int)a.atlas.n_anim, NS_MS(t_atlas));
        for (i = 0; i < a.atlas.n_levels; i++)
            R_DBG("atlas", "%d px: %d tiles in %dx%d, tile %dx%d, level step %d", (int)a.atlas.level[i].zoom, (int)a.atlas.level[i].n_tiles, (int)a.atlas.level[i].w, (int)a.atlas.level[i].h, (int)a.atlas.level[i].tile_w, (int)a.atlas.level[i].tile_h, (int)a.atlas.level[i].alt_step);
    }
    a.city     = (City *)calloc(1, sizeof *a.city);
    a.view     = (RCity *)calloc(1, sizeof *a.view);
    a.view_rot = (RCity *)calloc(1, sizeof *a.view_rot);
    /*  Three views of one city.
     *
     *      The city as the simulation holds it.
     *      The renderer's reading of it.
     *      That reading turned a quarter for the sprite sweep.
     *
     *  Every way out from here gives all three back, which is what
     *  `fail` is for. */
    if (!a.city || !a.view || !a.view_rot)
        goto fail;
    if (city_path[0])
    {
        R_NOTE("city", "loading %s", city_path);
        if (!city_load(city_path, a.city))
        {
            R_ERR("city", "%s is not a city", city_path);
            goto fail;
        }
        PHASE_MS(t_city);
        set_city_name(&a, city_path);
        log_city_loaded(a.city, city_path, NS_MS(t_city));
    }
    else
    {
        /*  Nothing asked for, so start on an empty map with the load
         *  menu up.  Calloc has already made a blank city, which the
         *  view is happy to draw. */
        snprintf(a.city_base, sizeof a.city_base, "%s", "Untitled");
        snprintf(a.us.save_path, sizeof a.us.save_path, "Untitled.sc2");
        a.us.open_load = 1;
    }
    /*  The generator's seed is not saved ($11DC never reaches MISC).  So a run starts from the clock, like the original.  Or it starts from --seed N.  Two builds can then be compared on the traffic they draw. */
    {
        int32_t seed = g_dev.seed ? (int32_t)atoi(g_dev.seed) : (int32_t)time(NULL);
        rng_seed(seed, (uint16_t)(seed & 0xFFFF));
    }

    t_phase = SDL_GetTicksNS();
    /*  A headless run, a check, a shot, a dump, a report, shows no
     *  window and must not show on the Dock either.  Every test launch
     *  was an icon bouncing up and gone.  The hint has to precede
     *  SDL_Init.  The system then lists the process as BackgroundOnly. */
    headless = check || run_frames || shot_out || want_mesh_check || have_pick || g_dev.area != NULL;
    a.live   = !headless;
    if (headless)
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        R_ERR("sdl", "%s", SDL_GetError());
        return 1;
    }
    R_DBG("sdl", "video %s, driver %s", SDL_GetRevision(), SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "none");
    win = SDL_CreateWindow(APP_TITLE, ww, wh, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (headless ? SDL_WINDOW_HIDDEN : 0));
    if (!win)
    {
        R_ERR("sdl", "window: %s", SDL_GetError());
        return 1;
    }
    {
        int pw, ph;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        R_DBG("sdl", "window %dx%d, %dx%d px%s", ww, wh, pw, ph, (check || run_frames || shot_out) ? ", hidden" : "");
    }
    a.gpu = gpu_create(win, &a.atlas, err, sizeof err);
    if (!a.gpu)
    {
        R_ERR("gpu", "%s", err);
        return 1;
    }
    PHASE_MS(t_gpu);
    R_NOTE("gpu", "%s, %.0f ms", gpu_driver(a.gpu), NS_MS(t_gpu));
    if (!check && !run_frames)
    {
        a.offscreen = shot_out != NULL; /* the interface's pipeline is made for the readback's format, so every frame is read back */
        a.ui        = ui_create(win, gpu_device(a.gpu), a.offscreen ? gpu_offscreen_format() : gpu_swapchain_format(a.gpu), 1.0f, assets_dir);
        build_watch(build_bar, &a); /* the bar moves from inside the build */
        R_DBG("ui", "%s", a.ui ? "imgui" : "none");
        if (a.ui)
        {
            /*  --theme for this run beats the saved preference, which
             *  beats the default.  A saved pack that has gone falls back
             *  to the default rather than to nothing. */
            char saved[64], ppath[1024];
            scan_themes(&a, assets_dir);
            R_DBG("theme", "%d packs in %s", a.us.n_themes, a.themes_dir);
            /*  The display preferences are the interactive run's alone.
             *  A check, a shot, a probe, a dump or a report must see the
             *  city as the code draws it.  It must not see it as the
             *  last session left it.  Show curves is the one that
             *  matters: with it saved on the roads stand aside and only
             *  the fitted centerline is drawn.  So a measurement answers
             *  about an overlay rather than about the world: an answer
             *  that is wrong without looking wrong.  One the clip check
             *  reads as road faces under the terrain. */
            a.prefs_ok = !(check || run_frames || shot_out || want_mesh_check || have_pick ||
                           g_dev.area != NULL || g_dev.probe != NULL || g_dev.lua_eval != NULL);
            if (a.prefs_ok)
            {
                /* the sprites' grid outline on the mesh, as it was left */
                char g[16];
                if (prefs_get("grid", g, sizeof g) && strcmp(g, "on") == 0)
                    a.gv.grid = 1;
                /*  Outline too, unless --tune or --outline set it for this run. */
                if (!g_dev.tune && !o.outline && prefs_get("curves", g, sizeof g) && strcmp(g, "on") == 0)
                    tune_array()[9] = 1.0f;
                /*  and the cell coordinates */
                if (prefs_get("cells", g, sizeof g) && strcmp(g, "on") == 0)
                    a.us.show_cells = 1;
                a.cells_pref = a.us.show_cells;
                /* the passes, remembered: off stays off */
                if (prefs_get("markings", g, sizeof g) && strcmp(g, "off") == 0)
                    a.gv.markings = 0;
                if (prefs_get("furniture", g, sizeof g) && strcmp(g, "off") == 0)
                    a.gv.furniture = 0;
                if (prefs_get("margins", g, sizeof g) && strcmp(g, "off") == 0)
                    a.gv.margins = 0;
                furniture_enable(a.gv.furniture);
                margin_enable(a.gv.margins);
            }
            if (prefs_path(ppath, sizeof ppath))
                R_DBG("prefs", "%s", ppath);
            if (theme_dir)
                apply_theme_choice(&a, theme_dir, "--theme");
            else if (prefs_get("theme", saved, sizeof saved) && saved[0] && apply_theme_choice(&a, saved, "saved preference"))
                ;
            else
                apply_theme_choice(&a, DEFAULT_THEME, "default");
        }
    }
    if (city_path[0])
        ui_log(&a.us, "Loaded %s", a.city_base);
    /*  The script, before anything is built: its knobs and its rules are
     *  read by the first build, not applied to the second.  Without
     *  --lua the state still comes up, so the console has one to talk
     *  to and a script may be loaded into it later. */
    {
        /*  The scripts, before anything is built: the props' models are
         *  theirs.  So a run that cannot find them draws no street
         *  furniture.  --lua names a file or a folder of its own.  With
         *  none, the one beside the build. */
        char        sdir[1024];
        const char *ship = find_scripts(sdir, sizeof sdir, argv[0]) ? sdir : NULL;
        script_open(ship, g_dev.lua);
        if (ship || g_dev.lua)
            R_NOTE("lua", "%s%s%s: %d rules, %d models", ship ? ship : "",
                   ship && g_dev.lua ? " and " : "", g_dev.lua ? g_dev.lua : "",
                   script_rules(), net_model_count());
        else
            R_WARN("lua", "no scripts: the props have no models to draw from");
    }
    /*  --mute: no audio device at all, for a run under test.  A mesh check
     *  or a headless frame opens the app for a second and the music started
     *  every time. */
    if (((!check && !run_frames && !shot_out) || sound_test) && !g_dev.mute)
    {
        a.snd = sound_create(assets_dir);
        if (a.snd)
        {
            char on[16];
            a.mus = music_create(assets_dir, sound_device(a.snd));
            if (a.mus)
            {
                music_set_rand(a.mus, lib_rand); /* the game's own stream, as the original shares it */
                /*  on unless the preference says off: the original's
                 *  Options menu starts with Music checked */
                int want = !(prefs_get("music", on, sizeof on) && strcmp(on, "off") == 0);
                R_DBG("music", "%d songs, %d instruments", music_n_songs(a.mus), 17);
                music_set_enabled(a.mus, want && !sound_test);
                a.us.music_on = music_enabled(a.mus);
            }
            else
                R_DBG("music", "none: run tools/import_assets.py for assets/music");
        }
        if (a.snd)
            R_DBG("sound", "%d effects", sound_loaded(a.snd));
        else
            R_WARN("sound", "no audio device");
        if (sound_test)
            sound_play(a.snd, sound_test);
    }

    if (pixel_scale < 1)
    {
        int pw, ph;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        pixel_scale = pw >= 2 * ww ? 2 : 1;
    }
    a.gv.scale = pixel_scale;
    a.t0_ns    = SDL_GetTicksNS();
    if (o.rotate_turns)
        sim_rotate(a.city, o.rotate_turns); /* --rotate N: the original's own turn, for a look at the other orientations */
    if (o.outline)
        tune_array()[9] = 1.0f; /* --outline: the view the flag asks for, preferences or none */
    if (view_quarter(&a))
        settle_quarter_turn(&a, NULL); /* --angle 90/180/270: the camera at that quarter, pivoting on the map's center */
    /*  Every city opens paused.  The speed it was saved at is what
     *  unpausing resumes, and stays in the save. */
    {
        int32_t saved = (int32_t)a.city->misc[MISC_SPEED];
        set_speed(&a, 1);
        a.last_speed             = saved > 1 ? saved : 3;
        a.city->misc[MISC_SPEED] = (uint16_t)saved;
    }

    t_phase = SDL_GetTicksNS();
    if (resweep(&a) != 0)
    {
        R_ERR("sweep", "no %d px art set", (int)a.opts.zoom);
        return 1;
    }
    PHASE_MS(t_sweep);
    R_DBG("sweep", "canvas %dx%d at %d px, %.0f ms", (int)a.sw.w, (int)a.sw.h, (int)a.opts.zoom, NS_MS(t_sweep));
    {
        int pw, ph;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        a.gv.scroll_x = a.sw.w / 2 - (pw / a.gv.scale) / 2;
        a.gv.scroll_y = a.sw.h / 2 - (ph / a.gv.scale) / 2;
        /*  --center puts a MAP TILE in the middle of the window.  This
         *  is how a person describes a view.  --scroll takes canvas
         *  pixels, which is how the renderer stores one.  The sweep has
         *  already run by here.  So it knows where the tile landed and
         *  no caller has to work the isometric projection out for
         *  itself. */
        if (have_centre)
        {
            int32_t fx, fy;
            a.opts.focus_col = centre_col;
            a.opts.focus_row = centre_row;
            a.dirty          = 1;
            if (resweep(&a) == 0 && soft_focus_result(&fx, &fy))
            {
                a.gv.scroll_x = fx - (pw / a.gv.scale) / 2;
                a.gv.scroll_y = fy - (ph / a.gv.scale) / 2;
            }
            else
                R_WARN("view", "tile %d,%d is off the canvas; "
                               "centring on the map instead",
                       centre_col,
                       centre_row);
            /*  The map view is a different camera, and the sweep's
             *  canvas is not its canvas: put the tile under the center
             *  through the camera itself. */
            if (a.plan && centre_col >= 0 && centre_col < R_MAP &&
                centre_row >= 0 && centre_row < R_MAP)
            {
                int32_t idx = centre_row * R_MAP + centre_col;
                a.anch_c    = (float)centre_col + 0.5f;
                a.anch_r    = (float)centre_row + 0.5f;
                a.anch_alt  = (float)rcity_alt_surface(a.view->altm[idx], a.view->xter[idx]);
                /*  The hold turns the anchor about the view's pivot, as
                 *  the frame turns the world.  In the app the anchor IS
                 *  the pivot (cam_anchor), and here it has to be too, or
                 *  the tile lands where the map's center turns it (88,80
                 *  asked, 92,58 got). */
                a.gv.pivot_c = a.anch_c;
                a.gv.pivot_r = a.anch_r;
                cam_hold(&a, win);
            }
        }
        if (have_scroll)
        {
            a.gv.scroll_x = scroll_x;
            a.gv.scroll_y = scroll_y;
        }
    }
    a.mesh_dirty = 1;
    a.zoom_world = (float)a.opts.zoom / 32.0f;
    a.gv.zoom    = 1.0f;
    if (a.gv.pitch <= 0.0f)
        a.gv.pitch = ARC_PITCH_DEG; /* the game's own camera */
    a.cam_t = 1.0f;
    if (zoomf > 0.0f)
    {
        int pw, ph;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        zoom_to(&a, zoomf, (float)pw * 0.5f, (float)ph * 0.5f);
        if (a.dirty && resweep(&a) != 0)
            return 1;
    }
    if (a.angle != 0.0f)
    {
        /* --angle: the view --scroll and the zoom give, turned about its center */
        float ang = a.angle;
        a.angle   = 0.0f;
        cam_anchor(&a, win);
        a.angle = ang;
    }
    /*  --plan with nowhere named looks at the middle of the map: the
     *  sweep's canvas is the city view's, not this camera's. */
    if (a.plan && !have_centre && !have_scroll)
    {
        int32_t idx = (R_MAP / 2) * R_MAP + R_MAP / 2;
        a.anch_c = a.anch_r = (float)(R_MAP / 2);
        a.anch_alt          = (float)rcity_alt_surface(a.view->altm[idx], a.view->xter[idx]);
        a.gv.pivot_c        = a.anch_c;
        a.gv.pivot_r        = a.anch_r;
        cam_hold(&a, win);
    }

    R_NOTE("init", "ready in %.0f ms (atlas %.0f, city %.0f, gpu %.0f, sweep %.0f)", NS_MS(SDL_GetTicksNS() - t_start), NS_MS(t_atlas), NS_MS(t_city), NS_MS(t_gpu), NS_MS(t_sweep));
#undef PHASE_MS

    if (run_frames > 0)
    {
        /*  Headless: the game's own frame a number of times at a speed,
         *  and how far the clock got.  So the schedule can be checked
         *  without watching the window. */
        int32_t  date0 = a.city->date;
        uint64_t t0    = SDL_GetTicksNS();
        int      f;
        if (run_speed)
            set_speed(&a, run_speed);
        for (f = 0; f < run_frames; ++f)
        {
            float dt = app_frame_dt(&a);
            if (app_frame(&a, win, dt, (float)((double)(a.last_ns - a.t0_ns) / 1e9), NULL) != 0)
                break;
        }
        dumpf("run       %d frames at speed %d in %.2f s: date %d -> %d "
               "(%d phases), funds %d, population %d, palette steps %d/%d\n",
               run_frames,
               (int)a.speed,
               (double)(SDL_GetTicksNS() - t0) / 1e9,
               (int)date0,
               (int)a.city->date,
               (int)(a.city->date - date0),
               (int)a.city->funds,
               (int)a.city->population,
               (int)a.anim_a,
               (int)a.anim_b);
        a.dirty = 1;
        if (resweep(&a) != 0)
            fprintf(stderr, "sweep failed\n");
    }
    /*  --edit C,R[;C,R...]: build, demolish those tiles, and let the
     *  path below build again: the incremental rebuild's test
     *  (mesh/incr.c). */
    if (g_dev.edit)
    {
        const char *s = g_dev.edit;
        int         col, row, n;
        if (remesh(&a) != 0)
            build_failed();
        while (sscanf(s, "%d,%d%n", &col, &row, &n) == 2)
        {
            sim_demolish_tile(a.city, row, col, 0, 0);
            s += n;
            if (*s != ';')
                break;
            ++s;
        }
        a.dirty = 1;
        if (resweep(&a) != 0)
            fprintf(stderr, "sweep failed\n");
        a.mesh_dirty = 1;
    }
    if (have_pick)
    {
        char what[160] = "";

        pick_tile(&a, pick_x, pick_y, win);
        /*  The game's own frames with the pointer there.  The interface
         *  is laid out and filled.  With --shot the frame is written,
         *  with the query window on it, and then what they filled in,
         *  dumped. */
        if (shot_out && a.ui)
            a.us.show_query = 1;
        rc = shot_frame(&a, win, shot_out);
        if (a.q_col >= 0)
            mesh_query(a.view, a.q_col, a.q_row, what, sizeof what);
        dumpf("pick      window %g,%g: column %d row %d  %s\n", (double)pick_x, (double)pick_y, (int)a.q_col, (int)a.q_row, what);
        if (a.us.q_ok)
            dumpf("pick      footprint %d x %d, north-east tile column %d row %d; "
                   "outline %.0f,%.0f %.0f,%.0f %.0f,%.0f %.0f,%.0f\n",
                   (int)a.us.q_size,
                   (int)a.us.q_size,
                   (int)a.us.q_ocol,
                   (int)a.us.q_orow,
                   (double)a.us.q_poly[0][0],
                   (double)a.us.q_poly[0][1],
                   (double)a.us.q_poly[1][0],
                   (double)a.us.q_poly[1][1],
                   (double)a.us.q_poly[2][0],
                   (double)a.us.q_poly[2][1],
                   (double)a.us.q_poly[3][0],
                   (double)a.us.q_poly[3][1]);
        if (a.us.comp_who[0])
        {
            /* --pick with --inspect: what the inspector says of the mesh under the point, and how many edges its outline has */
            const char *p;
            dumpf("inspect   %s: drawn by %s at %s, part of %s, material %g, %u triangles (%u with what is under it), %d outline edges\n", a.us.comp_label, a.us.comp_who, a.us.comp_where, a.us.comp_gen[0] ? a.us.comp_gen : "nothing", (double)a.us.comp_mat, a.us.comp_tris, a.us.comp_gen_tris > a.us.comp_tris ? a.us.comp_gen_tris : a.us.comp_tris, a.us.comp_n / 2);
            for (p = a.us.comp_note; *p;)
            {
                const char *e   = strchr(p, '\n');
                int         len = (int)(e ? (size_t)(e - p) : strlen(p));
                dumpf("inspect     %.*s\n", len, p);
                p = e ? e + 1 : p + len;
            }
            if (a.us.comp_box_ok)
                dumpf("inspect     extent\t%.3f,%.3f to %.3f,%.3f, height %.3f to %.3f\n",
                      (double)a.us.comp_box[0], (double)a.us.comp_box[1], (double)a.us.comp_box[3], (double)a.us.comp_box[4],
                      (double)a.us.comp_box[2], (double)a.us.comp_box[5]);
            for (rc = 0; rc < a.us.comp_n_also; ++rc)
                dumpf("inspect     drawn over\t%s\n", a.us.comp_also[rc]);
            rc = 0;
        }
        rc = rc ? 1 : 0;
        goto done;
    }
    if (g_dev.probe)
    {
        float px, py;
        if (remesh(&a) != 0)
            build_failed();
        if (sscanf(g_dev.probe, "%f,%f", &px, &py) == 2)
            mesh_probe(&a.mesh, px, py);
        else
            fprintf(stderr, "--probe wants X,Y in tiles\n");
        goto done;
    }
    if (g_dev.lua_eval)
    {
        /*  One chunk, against a world that has been built, so a script
         *  may ask what its own rules produced. */
        if (remesh(&a) != 0)
            build_failed();
        int rc_eval = script_eval(g_dev.lua_eval);
        rc = rc_eval != 0;
        goto done;
    }
    if (g_dev.area)
    {

        if (remesh(&a) != 0)
            build_failed();
        rc = area_report_cli(&a, g_dev.area);
        rc = rc ? 1 : 0;
        goto done;
    }
    if (want_mesh_check)
    {
        int bad;
        /*  The turned build cuts all four edges: a closed surface.  With
         *  --check-open set the plain build is checked instead, whose
         *  two uncut edges are open by design: the checker's own test. */
        a.angle = g_dev.check_open ? 0.0f : 1.0f;
        /*  A mesh the build abandoned is not a mesh to check: it is
         *  missing whatever the rule that raised was to draw.  The
         *  checks would report on the hole rather than on the city. */
        if (remesh(&a) != 0)
        {
            build_failed();
            rc = 1;
            goto done;
        }
        bad = mesh_check(&a.mesh, 1);
        if (geometry_on(&a))
        {
            int cut = mesh_check_clip(&a.mesh, 1);
            build_reports();
            if (cut != 0)
                bad = 1;
            /*  The buried faces are counted and reported, and held from
             *  getting worse by tools/buried_check.py.  The corpus is
             *  not at zero, so they do not fail the run. */
            mesh_check_overlap(&a.mesh, 1);
            /*  And geometry that COLLIDES: one surface driven through
             *  another.  Held from getting worse by
             *  tools/collide_check.py, which carries the corpus's own
             *  counts, since the corpus is not at zero. */
            mesh_check_collide(&a.mesh, 1);
            /*  A junction's outline must be a simple ring.  A spur or a
             *  meet there inverts everything taken from it, so this
             *  one IS a failure. */
            if (junction_outline_faults() != 0)
                bad = 1;
            /*  A crosswalk with no margin at one end is bars painted
             *  across a road nobody can step off.  The offer that put it
             *  there was wrong.  A failure too. */
            if (walk_net_faults() != 0)
                bad = 1;
        }
        rc = bad != 0;
        goto done;
    }
    if (shot_out)
    {

        if (g_dev.turn)
        {
            /*  --turn N: a quarter turn frame by frame, headless,
             *  through the live loop's own frame at 60 Hz, each frame to
             *  the shot's name numbered.  So a jump at the settle is
             *  found by diffing neighbors. */
            int    n  = atoi(g_dev.turn), f;
            size_t bl = strlen(shot_out);
            char   base[1024], path[1100];
            if (bl > 4 && strcmp(shot_out + bl - 4, ".png") == 0)
                bl -= 4;
            snprintf(base, sizeof base, "%.*s", (int)bl, shot_out);
            rc = app_frame(&a, win, -1.0f, 0.0f, NULL);
            app_turn(&a, 90.0f, win);
            for (f = 0; f < n && rc == 0; ++f)
            {
                RImage img;
                if (app_frame(&a, win, 1.0f / 60.0f, (float)(f + 1) / 60.0f, &img) != 0)
                {
                    rc = 1;
                    break;
                }
                snprintf(path, sizeof path, "%s_%03d.png", base, f);
                dumpf("turn %3d: angle %.3f cam_t %.3f quarter %d swept %d pivot %.1f,%.1f scroll %d,%d origin %d,%d anchor %.2f,%.2f\n", f, (double)a.angle, (double)a.cam_t, view_quarter(&a), a.sweep_quarter, (double)a.gv.pivot_c, (double)a.gv.pivot_r, (int)a.gv.scroll_x, (int)a.gv.scroll_y, (int)a.sw.ox, (int)a.sw.oy, (double)a.anch_c, (double)a.anch_r);
                rc = image_write_png(&img, path);
                image_free(&img);
            }
        }
        else
            rc = shot_frame(&a, win, shot_out);
        rc = rc ? 1 : 0;
        goto done;
    }
    if (check)
    {

        if (remesh(&a) != 0)
            build_failed();
        rc = check_frame(&a, win, check_out);
        rc = rc ? 1 : 0;
        goto done;
    }

    if (run_frames > 0)
        a.quit = 1;

    frame_loop(&a, win);

done:
    /*  The one way out, whichever mode the run was.  Every headless mode
     *  builds the same world as the game does.  So it gives back the
     *  same world: a mode that let itself out early left the whole mesh
     *  behind. */
    traffic_free(&a.traffic); /* the cars, the trains and their own scratch mesh */
    mesh_free(&a.mesh);
    incr_free(); /* the incremental rebuild's scratch (mesh/incr.c) */
    mesh_emit_free(); /* and the emitter's own (mesh/emit.c) */
    net_table_free(); /* the segment table's sample arenas (net/table.c) */
    ops_free(&a.ops);
    ui_destroy(a.ui);
    music_destroy(a.mus);
    sound_destroy(a.snd);
    gpu_destroy(a.gpu);
    SDL_DestroyWindow(win);
    SDL_Quit();
    city_free(a.city);
    free(a.city);
    free(a.view);
    free(a.view_rot);
    atlas_free(&a.atlas);
    return rc;

fail:
    /*  Anything the run had opened by the point it gave up is its own to
     *  close.  What is always ours by here is the three views. */
    city_free(a.city);
    free(a.city);
    free(a.view);
    free(a.view_rot);
    atlas_free(&a.atlas);
    return 1;
}
