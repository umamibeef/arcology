/*  app/internal.h -- the application's own state, shared by the files that
 *  make it up.
 *
 *  app.c held the whole application: preferences, theme scanning, disk
 *  search, camera arithmetic, the tool, the interface glue and the frame
 *  loop, at two and a half thousand lines with banner comments doing a
 *  file split's work.  The pieces are separate files now and this is what
 *  they share -- the same arrangement mesh/internal.h makes for the geometry
 *  family: a name appears here only because more than one file uses it.
 */
#ifndef R_APP_INT_H
#define R_APP_INT_H

#include "arc_version.h"
#include <stdint.h>

#include <SDL3/SDL.h>

#include "fs.h"

#include "atlas/atlas.h"
#include "city.h"
#include "gpu/gpu.h"
#include "mesh/mesh.h"

/*  The simulation's city by name only.  This header is included by files
 *  on the renderer's side of the seam, and they may hold the pointer the
 *  app passes around without being able to read a single field of it --
 *  which is the whole point of adapt.c (tools/seam_check.py enforces it). */
struct City;
#include "music.h"
#include "soft/soft.h"
#include "sound.h"
#include "ui.h"

typedef struct
{
    RAtlas       atlas;
    struct City *city;
    RCity       *view;
    RCity       *view_rot;      /* the view turned a quarter for the sprite sweep: the art is the original's own for that orientation */
    int          sweep_quarter; /* the quarter turn the current sweep was made for: it must follow the camera */
    ROpList      ops;
    RSweep       sw;
    RSoftOpts    opts;
    RGpu        *gpu;
    RGpuView     gv;
    RMesh        mesh;
    RTraffic     traffic;
    float        traffic_time;

    int32_t  speed, last_speed;
    uint64_t t0_ns;
    int64_t  deadline, things_deadline, anim_a_deadline, anim_b_deadline;
    int32_t  anim_a, anim_b;
    int      dirty;      /* the city changed: sweep again          */
    int      mesh_dirty; /* the terrain changed: rebuild the mesh  */
    /*  A reading of the scripts asked for a rebuild, and the frame that
     *  announces it has not been drawn yet.  The build blocks for about
     *  a second and a half; the reading itself takes about four
     *  milliseconds, so a bar over the reading alone would never be
     *  seen.  Holding the rebuild for one frame is what lets the panel
     *  reach the screen before the window stops repainting, and it needs
     *  no second frame path to do it. */
    int      reload_pending;
    int      quit;
    uint8_t  sky[3];

    RUi     *ui;
    RUiState us;
    RSound  *snd;
    float    fps, frame_ms;
    uint64_t last_ns;
    int      drag;             /* the left button drags the map     */
    float    drag_ax, drag_ay; /* sub-pixel remainder of the drag    */
    float    drag_len;         /* how far the button travelled       */
    int32_t  q_col, q_row;     /* the tile under the pointer, or -1 */
    float    q_fx, q_fy;       /* ... and the world point itself, for picking what stands there */
    float    q_mx, q_my;       /* ... and the pointer, in the window's own points */
    int      prefs_ok;         /* the run may read and write the saved preferences: an interactive one, never a headless check or shot */
    int      live;             /* a run with a window and a person at it: no check, no shot, no dump.  A build may be held a frame to say what it is doing only here -- a one-shot run would then write out the frame that says it. */
    int      offscreen;        /* every frame is drawn offscreen and read back: the interface's pipeline is made for that format (a --shot) */
    int      sel;              /* the map view's Shift-drag: an area being selected for a debug report (area.c) */
    int      inspect;          /* the inspector: the tile under the pointer outlined and reported on */
    int32_t  inspect_c, inspect_r; /* the tile its text was made for, so it is made again only on a move */
    int      inspect_ok;           /* a report was made for it: the window says so when nothing is under the pointer */
    uint32_t inspect_comp;         /* the component last reported: the log says a component once, not once a frame */
    int32_t  sel_c0, sel_r0, sel_c1, sel_r1;
    uint64_t sel_flash_until;  /* the selection flashes briefly after the release, then goes */
    char     city_base[128];   /* the city file's name, for a city without one */
    float    zoom_world;       /* continuous zoom: 1 = the 32 px set at 1:1 */
    float    angle;            /* free rotation, degrees, 0 = the snap view */
    int      plan;
    float    plan_from; /* the yaw the city view had when the map view opened */ /* the map view: the camera raised to look
                                                                                  * straight down at the world                */
    float   cam_t;                                                               /* the camera's move, 0..1; 1 when it is over */
    float   pitch_from, pitch_to;                                                /* the pitches it moves between, degrees */
    float   yaw_from, yaw_to;                                                    /* and the yaws                          */
    float   anch_c, anch_r;                                                      /* the grid point it keeps under the centre  */
    float   anch_alt;
    int     cells_pref; /* the cell-coordinates switch as last saved, to notice a change */
    int32_t anch_sx, anch_sy; /* the scroll the last hold produced, and the zoom then: while they stand, the anchor is still the point under the centre and is not measured again */
    float   anch_zoom;
    int     anch_ok;
    float   win_density;      /* window pixels per point                   */
    char    themes_dir[1024]; /* <assets>/themes, for the menu's picks */
    RMusic *mus;              /* the music, on the effects' device */
} App;

/*  The window's title: the game and the build, nothing of the city's state. */
#define APP_TITLE "Arcology | " ARC_VERSION_FULL

#define DEFAULT_THEME "classic7" /* Apple's System 7 look, the game's own era */

#define NS_MS(ns) ((double)(ns) / 1e6)

static const char *const GRAPH_NAMES[RUI_N_GRAPH] = {
    "City size", "Residents", "Commerce", "Industry", "Traffic", "Pollution", "Land value", "Crime", "Power shortfall", "Water shortfall", "Health", "Education", "Unemployment", "National GNP", "National population", "Federal rate"};
static const char *const DEPT_NAMES[RUI_N_DEPT] = {
    NULL, NULL, NULL, "Ordinances", "Bonds", "Police", "Fire", "Health", "Schools", "Colleges", "Roads", "Highways", "Subways", "Rail", "Transit", "Power"};

RGpuView frame_view(const App *a);

/*  The interface, both directions -- uiglue.c */
void ui_fill(App *a);
void ui_apply(App *a, SDL_Window *win, int pw, int ph);
void use_tool(App *a, SDL_Window *win);

/*  Building what gets drawn -- world.c */
int            resweep(App *a);
int            remesh(App *a);
/*  Where the scripts are: the models the props are drawn from live
 *  there (app/paths.c). */
int            find_scripts(char *out, size_t n, const char *argv0);
int            traffic_frame(App *a, float time);
int            check_frame(App *a, SDL_Window *win, const char *out_path);
int            shot_frame(App *a, SDL_Window *win, const char *path);
void           shore_field(const RCity *c, uint8_t *out);
const uint8_t *backdrop(const App *a);

/*  The clock, as the original paces it -- clock.c */
void    run_phase(App *a);
void    step_clock(App *a);
void    set_speed(App *a, int32_t s);
void    set_city_name(App *a, const char *path);
int64_t ticks_now(const App *a);
void    log_city_loaded(const struct City *c, const char *path, double ms);

/*  Preferences and themes -- prefs.c */
int  prefs_path(char *out, size_t n);
int  prefs_get(const char *key, char *out, size_t n);
int  prefs_set(const char *key, const char *value);
void scan_themes(App *a, const char *assets_dir);
int  apply_theme_choice(App *a, const char *name, const char *why);

/*  Where things are on disk -- paths.c */
int looks_like_assets(const char *p);
int find_assets(char *out, size_t n, const char *argv0);
int find_cities(char *out, size_t n);
int scan_cities(RUiState *s);
int resolve_city(char *out, size_t n, const char *arg, const char *dir);

/*  How long the eased move between two camera positions takes. */
#define CAM_SECONDS 0.45f

/*  The camera and the two directions between a tile and a pixel -- camera.c */
void cam_scales(const App *a, float *ysc, float *hsc);
void grid_to_canvas(const App *a, float fc, float fr, float alt, float *cx, float *cy);
int  screen_to_grid(App *a, float mx, float my, SDL_Window *win, float *ofc, float *ofr, float *oalt);
void canvas_to_grid_at(const App *a, float mx, float my, float dens, float alt, float *ofc, float *ofr);
void pick_tile(App *a, float mx, float my, SDL_Window *win);
void area_report_ui(App *a);                    /* area.c: the selected area's debug report to a file and the clipboard */
/*  The inspector's report on whatever is under the pointer, as text on
 *  the clipboard: shift-click while inspecting.  Everything the window
 *  shows, in the order it shows it. */
void inspect_copy(App *a);
int  area_report_cli(App *a, const char *spec); /* ... and --area C0,R0-C1,R1's, to the dump sink */
void cam_hold(App *a, SDL_Window *win);
void cam_anchor(App *a, SDL_Window *win);
#define PLAN_YAW 315.0f /* the map view: north (decreasing row) up, east (decreasing col) right */
void cam_set(App *a, float pitch, float yaw, SDL_Window *win);
void cam_go(App *a, float pitch, float yaw, SDL_Window *win);
void cam_step(App *a, float dt, SDL_Window *win);
void set_plan(App *a, int on, SDL_Window *win);
void rotate_by(App *a, float deg, SDL_Window *win);
void zoom_to(App *a, float z, float mx, float my);
int  geometry_on(const App *a);
void settle_quarter_turn(App *a, SDL_Window *win);
int  app_advance(App *a, SDL_Window *win, float dt, float time); /* one frame's world and camera; -1 when the sweep fails */
/*  THE frame, the live loop's and every headless mode's alike: the world
 *  and the camera advanced, the interface filled and laid out, the frame
 *  drawn with the interface on it -- to the window, or read back into
 *  `out` -- and the interface's commands applied.  A frame taken headless
 *  is a frame of the game because there is no other. */
int   app_frame(App *a, SDL_Window *win, float dt, float time, RImage *out);
float app_frame_dt(App *a); /* the frame's seconds from the wall clock, the frame rate kept up; -1 on the first frame */
void app_turn(App *a, float deg, SDL_Window *win); /* a key's or the menu's turn: a quarter at once with the geometry off */
int  view_quarter(const App *a);
int  sweep_wanted(const App *a); /* the quarter the sweep should be made for, at rest or on the move */                   /* 0..3: the camera at a quarter turn on the game's pitch, else 0 */

#endif /* R_APP_INT_H */
