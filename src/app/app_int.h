/*  app_int.h -- the application's own state, shared by the files that
 *  make it up.
 *
 *  app.c held the whole application: preferences, theme scanning, disk
 *  search, camera arithmetic, the tool, the interface glue and the frame
 *  loop, at two and a half thousand lines with banner comments doing a
 *  file split's work.  The pieces are separate files now and this is what
 *  they share -- the same arrangement mesh_int.h makes for the geometry
 *  family: a name appears here only because more than one file uses it.
 */
#ifndef R_APP_INT_H
#define R_APP_INT_H

#include <stdint.h>

#include <SDL3/SDL.h>

#include "fs.h"

#include "atlas.h"
#include "city.h"
#include "gpu.h"
#include "mesh.h"

/*  The simulation's city by name only.  This header is included by files
 *  on the renderer's side of the seam, and they may hold the pointer the
 *  app passes around without being able to read a single field of it --
 *  which is the whole point of adapt.c (tools/seam_check.py enforces it). */
struct City;
#include "soft.h"
#include "sound.h"
#include "music.h"
#include "ui.h"

typedef struct
{
    RAtlas       atlas;
    struct City *city;
    RCity       *view;
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
    int      quit;
    uint8_t  sky[3];

    RUi     *ui;
    RUiState us;
    RSound  *snd;
    float    fps, frame_ms;
    uint64_t last_ns;
    int      drag;              /* the left button drags the map     */
    float    drag_ax, drag_ay;  /* sub-pixel remainder of the drag    */
    float    drag_len;          /* how far the button travelled       */
    int32_t  q_col, q_row;      /* the tile under the pointer, or -1 */
    char     city_base[128];    /* the city file's name, for a city without one */
    float    zoom_world;        /* continuous zoom: 1 = the 32 px set at 1:1 */
    float    angle;             /* free rotation, degrees, 0 = the snap view */
    int      plan;              /* the map view: the camera raised to look
                                 * straight down at the world                */
    float cam_t;                /* the camera's move, 0..1; 1 when it is over */
    float pitch_from, pitch_to; /* the pitches it moves between, degrees */
    float yaw_from, yaw_to;     /* and the yaws                          */
    float anch_c, anch_r;       /* the grid point it keeps under the centre  */
    float anch_alt;
    float win_density;      /* window pixels per point                   */
    char  themes_dir[1024]; /* <assets>/themes, for the menu's picks */
    RMusic *mus; /* the music, on the effects' device */
} App;

#define DEFAULT_THEME "classic7" /* Apple's System 7 look, the game's own era */

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
void pick_tile(App *a, float mx, float my, SDL_Window *win);
void cam_hold(App *a, SDL_Window *win);
void cam_anchor(App *a, SDL_Window *win);
void cam_set(App *a, float pitch, float yaw, SDL_Window *win);
void cam_go(App *a, float pitch, float yaw, SDL_Window *win);
void cam_step(App *a, float dt, SDL_Window *win);
void set_plan(App *a, int on, SDL_Window *win);
void rotate_by(App *a, float deg, SDL_Window *win);
void zoom_to(App *a, float z, float mx, float my);
int  geometry_on(const App *a);

#endif /* R_APP_INT_H */
