/*  app/internal.h: the application's own state, shared by the files that
 *  make it up.
 *
 *  app.c held the whole application.  That was preferences, theme
 *  scanning, disk search and camera arithmetic.  It also held the tool,
 *  the interface glue and the frame loop.  That was two and a half
 *  thousand lines, with banner comments doing a file split's work.  The
 *  pieces are separate files now and this is what they share.  The same
 *  arrangement mesh/internal.h makes for the geometry family.  A name
 *  appears here only because more than one file uses it. */
#ifndef R_APP_INT_H
#define R_APP_INT_H

#include "arc_version.h"
#include <stdint.h>

#include <SDL3/SDL.h>

#include "fs.h"

#include "atlas/atlas.h"
#include "city.h"
#include "gpu/gpu.h"
#include "inspect.h"
#include "mesh/mesh.h"

/*  The simulation's city by name only.  This header is included by files
 *  on the renderer's side of the seam.  They may hold the pointer the
 *  app passes around without being able to read a single field of it.
 *  Which is the whole point of adapt.c (tools/seam_check.py enforces
 *  it). */
struct City;
struct World;
struct Startup;
#include "music.h"
#include "soft/soft.h"
#include "sound.h"
#include "ui.h"

/*  The key every letter shortcut needs: the Mac's command key, and Ctrl
 *  everywhere else.  The bare keys that remain are game controls, not menu
 *  shortcuts.  They are the digits for the speeds, space, the zoom and scale
 *  keys, the arrows, W A S D and Q E. */
#ifdef __APPLE__
    #define KMOD_CMD SDL_KMOD_GUI
#else
    #define KMOD_CMD SDL_KMOD_CTRL
#endif

/*  How many switches the game remembers between runs (prefs.c). */
#define PREFS_SWITCHES 9

typedef struct
{
    RAtlas        atlas;
    struct City  *city;  /* the city the player is in: world->city[city_i] */
    struct World *world; /* the map and every city on it */
    int           city_i;
    RCity        *view;
    RCity         view_one;      /* one city's view, before it is tiled over the map (resweep) */
    RCity        *view_rot;      /* the view turned a quarter for the sprite sweep: the art is the original's own for that orientation */
    int           sweep_quarter; /* the quarter turn the current sweep was made for: it must follow the camera */
    ROpList       ops;
    RSweep        sw;
    RSoftOpts     opts;
    RGpu         *gpu;
    RGpuView      gv;
    RMesh         mesh;

    int32_t  speed, last_speed;
    uint64_t t0_ns;
    int64_t  deadline, things_deadline, anim_a_deadline, anim_b_deadline;
    int32_t  anim_a, anim_b;
    int      dirty;       /* the city changed: sweep again          */
    int      mesh_dirty;  /* the terrain changed: rebuild the mesh  */
    int      trees_stale; /* a forest tile changed since the last build: rebuilt at the month's turn (clock.c) */
    int      quit;
    int      want_shot; /* a screenshot asked for, taken once the frame is drawn */
    uint8_t  sky[3];

    RUi     *ui;
    RUiState us;
    RSound  *snd;
    float    fps, frame_ms;
    uint64_t last_ns;
    uint64_t show_painted; /* when a frame was last drawn from inside a build that shows an edit */
    int      upload_held;  /* a built world waits for an edit's last piece to go before the device takes it */
    /*  The drawn ground's corner heights, four a tile in NW, NE, SE, SW
     *  order, as the world on the device has them.  The outlines read
     *  them, so they work while the next build rewrites the mesh's own
     *  stores.  `ground_next` is the built world's, which the device has
     *  not taken yet. */
    float  *ground, *ground_next;
    int32_t ground_n;                        /* the map edge both were made for */
    int     ground_under, ground_next_under; /* the view each was built for */
    /*  A build on a thread of its own (world.c, remesh_begin): the frames
     *  go on while it runs.  It reads `build_view`, a copy of the city
     *  taken as it began.  It owns the mesh, the stores and the build's
     *  state until it is done. */
    struct SDL_Thread *builder;
    SDL_AtomicInt      build_done;
    int                building, build_rc, build_under, build_lines;
    int                build_frames; /* the frames drawn while it ran */
    uint64_t           build_t0;
    RCity             *build_view;
    SDL_ThreadID       main_thread;
    /*  THE WORLD A BLOCK AT A TIME (world.c, resident_aim).  The live
     *  window builds the block of chunks nearest the camera that is not
     *  yet built.  Then it builds the next, until every chunk is built.
     *  The build under way is one of them. */
    int resident;
    /*  The scroll's fraction of a canvas pixel past scroll_x and scroll_y.
     *  It is kept from one zoom or pan step to the next, so small steps
     *  add up.  It holds for the scroll it was kept with, scroll_kept. */
    float   pan_x, pan_y;
    int32_t scroll_kept[2];
    float   cam_v[2]; /* the held keys' pan, in screen pixels a second */
    /*  The held turn keys.  They hold the yaw the view is drawn toward and
     *  the yaw it is at, both unwrapped, in degrees.  They also hold its
     *  speed in degrees a second, and whether a turn is under way. */
    float yaw_aim, yaw_now, yaw_v;
    int   yaw_on;
    /*  The key held last frame, -1, 0 or 1, how long it has been held, in
     *  seconds, and the way the last press turned. */
    int      yaw_held, yaw_way;
    float    yaw_hold;
    float    settle_u[23]; /* the land settling while it is built, for the view (land_settle.c) */
    uint8_t *shaped;       /* the tiles whose piece the world on the device draws from its own shape (roads.c), a byte a tile */
    int32_t  shaped_n;     /* the map's side `shaped` is for */
    int      drag;         /* a mouse button drags the map      */
    int      gesturing;    /* the left button works the selected tool: uiglue.c holds the gesture */
    /*  The areas the tool took in the drag under way.  After it they stand until the world built after it is up, and world.c clears them.  Each is its first column, first row, last column and last row. */
    int32_t taken[256][4];
    int     n_taken;
    /*  The footprints the bulldozer brought down, collapsing on screen
     *  ($71A4).  Each has its least row, its greatest column and its
     *  edge.  It also keeps when it began, how many of its frames are
     *  drawn, and the seed of its debris. */
    struct Collapse
    {
        int32_t  row, col, n;
        uint64_t t0;
        int      shown;
        uint32_t seed;
    } collapse[32];
    int n_collapse;
    int taken_gen; /* counts the drags that ended: the marks belong to the last */
    /*  The tiles the device lights, the marks, four bytes a tile
     *  (gpu_set_marks).  The digest is of what they were made from.  So
     *  they go to the device only when they change. */
    uint8_t  *marks, *marks_sweep; /* the second in the sweep's quarter, for the art */
    int32_t   marks_n;
    uint32_t  marks_digest;
    int       build_gen, shown_gen; /* the drags the build under way and the world on the device were built after */
    float     drag_len;             /* how far the button traveled       */
    int32_t   q_col, q_row;         /* the tile under the pointer, or -1 */
    float     q_mx, q_my;           /* ... and the pointer, in the window's own points */
    int       prefs_ok;             /* the run may read and write the saved preferences: an interactive one, never a headless check or shot */
    int       live;                 /* a run with a window and a person at it: no check, no shot, no dump.  A build may be held a frame to say what it is doing only here.  A one-shot run would otherwise write out the frame that says it. */
    int       offscreen;            /* every frame is drawn offscreen and read back: the interface's pipeline is made for that format (a --shot) */
    int       sel;                  /* the map view's Shift-drag: an area being selected for a debug report (area.c) */
    Inspector insp;                 /* the inspector (inspect.c) */
    int32_t   sel_c0, sel_r0, sel_c1, sel_r1;
    int32_t   mark_fixed[4]; /* --mark: tiles marked as the pointer marks them, first column and row, then last */
    int       have_mark_fixed;
    uint64_t  sel_flash_until;  /* the selection flashes briefly after the release, then goes */
    char      city_base[128];   /* the city file's name, for a city without one */
    float     zoom_world;       /* continuous zoom: 1 = the 32 px set at 1:1 */
    float     zoom_aim;         /* the zoom the view eases toward (zoom_ease), nought for none */
    float     zoom_mx, zoom_my; /* the window point, in pixels, that stays put while it does */
    float     angle;            /* free rotation, degrees, 0 = the snap view */
    int       plan;
    float     plan_from; /* the yaw the city view had when the map view opened */ /* the map view: the camera raised to look
                                                                                   * straight down at the world                */
    float    cam_t;                                                               /* the camera's move, 0..1.  1 when it is over */
    float    pitch_from, pitch_to;                                                /* the pitches it moves between, degrees */
    float    yaw_from, yaw_to;                                                    /* and the yaws                          */
    float    anch_c, anch_r;                                                      /* the grid point it keeps under the center  */
    float    anch_alt;
    int      remembered[PREFS_SWITCHES]; /* each remembered switch as last saved, to notice a change (prefs.c) */
    float    hour_saved;                 /* the time of day as last saved (prefs.c) */
    int      biome_city;                 /* the biome the city's file names (trees.c) */
    int      biome_shown;                /* the biome the mesh was built with, -2 before any */
    int      biome_saved;                /* the biome choice as last saved (prefs.c) */
    uint64_t hour_saved_at;              /* when, in SDL ticks */
    int32_t  anch_sx, anch_sy;           /* the scroll the last hold produced, and the zoom then.  While they stand the anchor is still the point under the center, and is not measured again */
    float    anch_zoom;
    int      anch_ok;
    int32_t  win_pw, win_ph;   /* the window in pixels, as the last frame had it */
    float    win_density;      /* window pixels per point                   */
    char     themes_dir[1024]; /* <assets>/themes, for the menu's picks */
    RMusic  *mus;              /* the music, on the effects' device */
} App;

/*  The window's title: the game and the build, nothing of the city's state. */
#define APP_TITLE "Arcology | " ARC_VERSION_FULL

#define DEFAULT_THEME "classic7" /* Apple's System 7 look, the game's own era */

#define NS_MS(ns) ((double)(ns) / 1e6)

/*  ---- app.c: startup and the frame ------------------------------------ */
int check_frame(App *a, SDL_Window *win, const char *out_path);

RGpuView frame_view(const App *a);
/*  THE frame, the live loop's and every headless mode's alike.
 *
 *      The world and the camera advanced.
 *      The interface filled and laid out.
 *      The frame drawn with the interface on it.
 *
 *  To the window, or read back into `out`.  And the interface's commands
 *  applied.  A frame taken headless is a frame of the game because there
 *  is no other. */
int app_frame(App *a, SDL_Window *win, float dt, float time, RImage *out);

/*  The frame's seconds from the wall clock, the frame rate kept up.  -1
 *  on the first frame. */
float app_frame_dt(App *a);
/*  The build was abandoned, and what became of the frame said once. */
void build_failed(void);
/*  A step of the build begun: the frame drawn from inside it where an
 *  edit's build is shown (prof_watch). */
void build_shown(void *ud, const char *step, int i, int n);

/*  ---- headless.c: the runs that show no window --------------------------- */
int check_frame(App *a, SDL_Window *win, const char *out_path);
/*  The headless modes: a build and one thing done with it.  1 where a mode
 *  ran, with how it went in `out`.  0 where none was asked for. */
int run_mode(App *a, SDL_Window *win, const struct Startup *o, int *out);

/*  ---- input.c: one event of the window -------------------------------- */
void input_event(App *a, SDL_Window *win, const SDL_Event *e);

/*  ---- uiglue.c: the interface, both directions, and the player's tools - */

void ui_fill(App *a);
void ui_apply(App *a, SDL_Window *win, int pw, int ph);
void use_tool(App *a, SDL_Window *win);
/*  The selected tool, worked tile by tile while the left button is down.
 *  Ready says the simulation can do the tool and sub-tool selected. */
int  gesture_ready(const App *a);
void gesture_begin(App *a, int32_t col, int32_t row, int cmd_down);
void gesture_step(App *a, int32_t col, int32_t row);
void gesture_end(App *a, int32_t col, int32_t row);
int  gesture_script(App *a, const char *spec);

/*  ---- inspect.c: the inspector ---------------------------------------- */

/*  Its part of a frame, what the window asked of it, and its switch.
 *  Then a click and a key while it is up, 1 where it took them.  Then
 *  --pick's report of it to the dump, 1 where the mesh under the point
 *  was inspected. */
void inspect_fill(App *a, RUiState *s);
void inspect_apply(App *a, RUiState *s);
int  inspect_click(App *a, float x, float y, SDL_Window *win);
int  inspect_key(App *a, SDL_Keycode k, int shift);
int  inspect_dump(const App *a);
/*  The inspector's report on whatever is under the pointer, as text on
 *  the clipboard: shift-click while inspecting.  Everything the window
 *  shows, in the order it shows it. */
void inspect_copy(App *a);

/*  ---- world.c: the world, and building what gets drawn ---------------- */
void app_turn(App *a, float deg, SDL_Window *win); /* a key's or the menu's turn: a quarter at once with the geometry off */
void settle_quarter_turn(App *a, SDL_Window *win); /* a turn that came to rest on a quarter: the sweep redone, the anchor held */

int resweep(App *a);
int remesh(App *a);
/*  The same build on a thread of its own, for the live window.  It is
 *  begun, then asked once a frame whether it is done.  The poll answers 1
 *  while it runs, 0 once its world is up, and -1 for a build that
 *  failed.  The wait waits for one still running, as the program ends. */
int  remesh_begin(App *a);
int  remesh_poll(App *a);
int  remesh_wait_done(App *a); /* a build on its own thread taken to its end, as a poll takes it: 0, or -1 where it failed */
void remesh_wait(App *a);
/*  Aim the next build at the block of chunks nearest the camera that is
 *  not yet built.  Answers 1 where it named one, 0 where every chunk is
 *  built or the live window builds the world whole.  And mark every
 *  chunk unbuilt, its triangles kept until a block covers it again. */
int  resident_aim(App *a, SDL_Window *win);
void resident_stale(App *a);
/*  The built world to the device, when an edit's last piece has gone:
 *  remesh holds it back until then (render/show.h). */
int world_upload(App *a);
/*  A save, in the program's own format (.arco).  A path with any other
 *  ending is refused. */
int world_save(App *a, const char *path);
/*  Read the world a path names into the app, and put the player in its
 *  first city.  An empty path gives an empty map with one blank city.
 *  The world before it is freed.  Answers 1 when it is read. */
int  app_world_load(App *a, const char *path);
void app_world_free(App *a);
void app_streets_import(App *a);
/*  --net-check: the road network built from the cells alone and checked,
 *  and with --net-svg written as a diagram.  Nothing is drawn.  Nonzero
 *  where a lane fails the check. */
int  app_net_check(App *a);
void app_views_free(App *a);
/*  The city whose window holds a tile of the view, made the one the
 *  player is in, and the tile in that window.  Answers 0 where no city's
 *  window holds the tile. */
int app_city_at(App *a, int32_t *col, int32_t *row);
/*  The city whose window holds a tile of the view, the player's own
 *  first, and where its window lies in the view.  -1 where none does. */
int app_city_find(const App *a, int32_t col, int32_t row, int32_t *dc, int32_t *dr);
/*  Where the player's city's window lies in the view: a tile of the
 *  city is the view's tile less (dc, dr). */
void app_city_offset(const App *a, int32_t *dc, int32_t *dr);
/*  The renderer's map side for the world: its bounds, laid --map-tile
 *  times over. */
int32_t app_world_side(const App *a);
/*  The city's limits, as --limits-glow asks for them. */
void           city_limits_from_args(struct City *c);
void           collapse_begin(App *a, int32_t row, int32_t col, int32_t n);
void           collapse_step(App *a);
const uint8_t *backdrop(const App *a);

/*  ---- land_settle.c: the land settling into place after a build -------- */

void land_settle_want(App *a);
void land_settle_tick(App *a, SDL_Window *win);
void land_settle_landed(App *a);

/*  ---- clock.c: the city's clock, and its name ------------------------- */

void step_clock(App *a);
void set_speed(App *a, int32_t s);
void open_paused(App *a);
void set_city_name(App *a, const char *path);
void log_city_loaded(const struct City *c, const char *path, double ms);

/*  ---- prefs.c: preferences and themes --------------------------------- */

int prefs_path(char *out, size_t n);
int prefs_get(const char *key, char *out, size_t n);
int prefs_set(const char *key, const char *value);
/*  The switches the last session left on, set in the interface's state.
 *  `knobs` also sets the switches that are knobs, which --outline and
 *  --tune name instead. */
void prefs_switches_load(RUiState *s, int knobs);
/*  Each switch as it stands, taken as the one saved. */
void prefs_switches_seen(App *a);
/*  Each switch that changed since it was saved, written. */
void prefs_switches_save(App *a);
void scan_themes(App *a, const char *assets_dir);
int  apply_theme_choice(App *a, const char *name, const char *why);

/*  ---- paths.c: where things are on disk ------------------------------- */

int looks_like_assets(const char *p);
int find_assets(char *out, size_t n, const char *argv0);
int find_cities(char *out, size_t n);
int scan_cities(RUiState *s);
int resolve_city(char *out, size_t n, const char *arg, const char *dir);

/*  ---- area.c: the debug report of an area ----------------------------- */

void area_report_ui(App *a);                    /* the selected area's report to a file and the clipboard */
int  area_report_cli(App *a, const char *spec); /* ... and --area C0,R0-C1,R1's, to the dump sink */

/*  ---- camera.c: the camera, and the two directions between a tile and a
 *  pixel --------------------------------------------------------------- */

/*  How long the eased move between two camera positions takes. */
#define CAM_SECONDS 0.45f
#define PLAN_YAW    315.0f /* the map view: north (decreasing row) up, east (decreasing col) right */

void cam_scales(const App *a, float *ysc, float *hsc);
void grid_to_canvas(const App *a, float fc, float fr, float alt, float *cx, float *cy);
int  screen_to_grid(App *a, float mx, float my, SDL_Window *win, float *ofc, float *ofr, float *oalt);
void canvas_to_grid_at(const App *a, float mx, float my, float dens, float alt, float *ofc, float *ofr);
/*  Where a grid point lands in the window, in window points. */
void grid_to_window(const App *a, float fc, float fr, float alt, float dens, float *wx, float *wy);
/*  The scroll's fraction past scroll_x and scroll_y, where it still holds. */
float scroll_fx(const App *a);
float scroll_fy(const App *a);
void  pick_tile(App *a, float mx, float my, SDL_Window *win);
/*  One frame of the held pan and turn keys, eased. */
void  cam_keys(App *a, SDL_Window *win);
float cam_px(const App *a);
void  cam_pan(App *a, SDL_Window *win, float dx, float dy);
void  cam_hold(App *a, SDL_Window *win);
void  cam_anchor(App *a, SDL_Window *win);
/*  The camera turned by `deg`, eased. */
void rotate_by(App *a, float deg, SDL_Window *win);
void cam_go(App *a, float pitch, float yaw, SDL_Window *win);
void cam_step(App *a, float dt, SDL_Window *win);
void set_plan(App *a, int on, SDL_Window *win);
void zoom_to(App *a, float z, float mx, float my);
/*  Ask for the zoom `z` about the window point `mx`, `my`: the view eases
 *  to it frame by frame (zoom_ease). */
void zoom_toward(App *a, float z, float mx, float my);
void zoom_by(App *a, float factor, float mx, float my);
/*  A frame's worth of the ease toward the zoom asked for. */
void zoom_ease(App *a, float dt);
int  geometry_on(const App *a);
int  view_quarter(const App *a); /* 0..3: the camera at a quarter turn on the game's pitch, else 0 */
int  sweep_wanted(const App *a); /* the quarter the sweep should be made for, at rest or on the move */

#endif /* R_APP_INT_H */
