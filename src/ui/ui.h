/*  ui.h -- the in-game UI behind a C interface.
 *
 *  The widgets are Dear ImGui, hosted in ui.cpp, the one C++ file in the
 *  game; ui_none.c is the same interface doing nothing, for a build
 *  without it.  The app owns an RUiState: before the frame it fills in
 *  what the UI shows; the UI edits the switches in place and raises the
 *  commands; after the frame the app applies them and clears them.  The
 *  UI knows nothing of the city or the renderer, only this struct.
 *
 *  The look is the original's: its tool palette and menus from the
 *  resource fork's own art and menu resources, and System 7 window chrome
 *  drawn by hand.  Theming is kept in one place, apply_theme in ui.cpp,
 *  so a Kaleidoscope scheme can drive it later (docs/future.rst).
 */
#ifndef R_UI_H
#define R_UI_H

/*  the most cities the load menu will list from one directory */
#define R_MAX_CITIES 512

#include <stdint.h>

struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUCommandBuffer;
struct SDL_GPUTexture;
union SDL_Event;

#define RUI_LOG_LINES     24
#define RUI_LOG_LEN       96
#define RUI_N_GRAPH       16
#define RUI_MAX_THEMES    16
#define RUI_GRAPH_SAMPLES 52
#define RUI_N_DEPT        16

enum
{
    RUI_DISASTER_NONE = 0,
    RUI_DISASTER_FIRE,
    RUI_DISASTER_FLOOD,
    RUI_DISASTER_RIOT,
    RUI_DISASTER_TORNADO,
    RUI_DISASTER_MONSTER,
    RUI_DISASTER_EARTHQUAKE,
    RUI_DISASTER_HURRICANE,
    RUI_DISASTER_CHEMICAL,
    RUI_DISASTER_AIR_CRASH,
    RUI_DISASTER_MICROWAVE,
    RUI_DISASTER_MELTDOWN,
    RUI_DISASTER_VOLCANO,
    RUI_DISASTER_FIRESTORM,
    RUI_DISASTER_POLLUTION,
    RUI_N_DISASTERS
};

typedef struct
{
    /* --- shown: filled by the app before the frame --------------- */
    char        city_name[64];
    int32_t     year, month; /* calendar year, month 0..11        */
    int32_t     funds, population, stage;
    int32_t     demand[3]; /* R, C, I in -2000..2000            */
    int32_t     power_pct, water_pct, unemployment;
    int32_t     land_value_tot, crime_tot, traffic_tot, pollution_tot;
    float       fps, frame_ms;
    uint32_t    instances, culled, mesh_verts;
    const char *driver;
    /* the graph series, newest sample first, and their names */
    const int32_t *graph[RUI_N_GRAPH];
    const char    *graph_name[RUI_N_GRAPH];
    /* the budget departments */
    const char *dept_name[RUI_N_DEPT];
    int32_t     dept_amount[RUI_N_DEPT], dept_funding[RUI_N_DEPT],
        dept_accrued[RUI_N_DEPT];
    /* the tile under the cursor, when q_ok */
    int         q_ok;
    int32_t     q_col, q_row, q_alt, q_water_level;
    int32_t     q_size, q_ocol, q_orow; /* the footprint: its edge, its north-east tile */
    uint8_t     q_xter, q_xbld, q_xzon, q_xbit, q_xund, q_xtxt;
    uint16_t    q_altm;
    const char *q_building;   /* a name when one is known, else NULL */
    char        q_mesh[160];  /* what the mesh made of the tile        */
    float       q_poly[4][2]; /* the tile's outline on screen, points  */
    int         q_poly_ok;
    /* the message log, newest last */
    char    log[RUI_LOG_LINES][RUI_LOG_LEN];
    int32_t n_log;

    /* --- the canon interface: the tool palette ------------------ */
    int tool; /* the selected tool, 0..33, or -1   */
    int show_palette, show_demand;

    /* --- switches: shown and edited in place -------------------- */
    int32_t speed; /* 1 pause .. 5                      */
    /*  geometry is the one switch: the terrain mesh, the water shader
     *  and the road strips together.  plan is the map view, the camera
     *  raised to look straight down. */
    int geometry, plan, grid, plain_sweep, underground;
    /*  The road geometry's knobs, live: the two widths, the tightest and
     *  widest curve each family may take, the straight approach at a
     *  node, the corridor margin and the junction trim.  The app fills
     *  them from the mesh and takes them back when tune_changed. */
    float tune[13];
    int   tune_changed, show_tuning;
    /*  Tile numbers drawn on the map, so a cell can be named rather than
     *  pointed at (the user: "Give me a coordinate system and I can point
     *  out which cells are problematic"). */
    int     show_coords;
    int     coord_n;
    float   coord_a[64][2]; /* a whole column's, or row's, two ends on screen */
    float   coord_b[64][2];
    short   coord_v[64];    /* which column, or which row                    */
    char    coord_axis[64]; /* 'c' for a column, 'r' for a row               */
    int32_t view;           /* 0..11, the game's map views       */
    int     show_city, show_budget, show_graphs, show_disasters,
        show_query, show_renderer, show_log;

    /* --- commands: raised by the UI, cleared by the app ---------- */
    int want_quit, want_save, want_screenshot;
    /*  the load menu.  The app fills city_list by scanning city_dir and
     *  raises open_load when it wants the menu up; the UI raises
     *  want_load with a path in load_path when the player picks one. */
    int     want_load, open_load;
    char    load_path[512];
    char    city_dir[512];
    char    city_list[R_MAX_CITIES][80];
    int     n_cities;
    int32_t want_disaster;  /* RUI_DISASTER_*                    */
    int32_t want_zoom_step; /* +1 in, -1 out                     */
    int32_t want_rotate;    /* +1 clockwise, -1 anticlockwise    */
    int32_t want_sound;     /* a sound id to play, 0 none         */
    char    save_path[512];
    /*  The themes: every pack found under assets/themes, the one worn
     *  now by name ("none" for the hand-drawn look), and the request to
     *  change, which app applies and saves. */
    char theme_list[RUI_MAX_THEMES][64];
    int  n_themes;
    char theme_name[64];
    int  want_theme;
    /*  the music: on or off, and the request to flip it */
    int music_on, want_music;
} RUiState;

/*  The thirty-four buttons of the tool palette, in the order the
 *  original's help texts (TEXT 1100..1133) enumerate them. */
enum
{
    RUI_TOOL_BULLDOZER = 0,
    RUI_TOOL_LANDSCAPE,
    RUI_TOOL_DISPATCH,
    RUI_TOOL_POWER,
    RUI_TOOL_WATER,
    RUI_TOOL_BONUS,
    RUI_TOOL_ROADS,
    RUI_TOOL_RAIL,
    RUI_TOOL_PORTS,
    RUI_TOOL_RESIDENTIAL,
    RUI_TOOL_COMMERCIAL,
    RUI_TOOL_INDUSTRIAL,
    RUI_TOOL_EDUCATION,
    RUI_TOOL_HEALTH,
    RUI_TOOL_RECREATION,
    RUI_TOOL_SIGN,
    RUI_TOOL_QUERY,
    RUI_TOOL_CENTER,
    RUI_TOOL_ZOOM_OUT,
    RUI_TOOL_ZOOM_IN,
    RUI_TOOL_DEMAND,
    RUI_TOOL_ROTATE_CCW,
    RUI_TOOL_ROTATE_CW,
    RUI_TOOL_MAP,
    RUI_TOOL_POPULATION,
    RUI_TOOL_NEIGHBORS,
    RUI_TOOL_GRAPHS,
    RUI_TOOL_INDUSTRY,
    RUI_TOOL_BUDGET,
    RUI_TOOL_LAYER_BUILDINGS,
    RUI_TOOL_LAYER_SIGNS,
    RUI_TOOL_LAYER_ROADS,
    RUI_TOOL_LAYER_ZONES,
    RUI_TOOL_LAYER_UNDERGROUND,
    RUI_N_TOOLS
};
extern const char *const RUI_TOOL_NAME[RUI_N_TOOLS];

typedef struct RUi RUi;

/*  Create the UI on the window and device; `swap_fmt` is the swapchain's
 *  SDL_GPUTextureFormat, `dpi` the window's pixel density.  Returns
 *  NULL when the UI is not built in. */
RUi *ui_create(struct SDL_Window *win, struct SDL_GPUDevice *dev, int swap_fmt, float dpi, const char *assets_dir);
void ui_destroy(RUi *u);

/*  Dress the interface in a Kaleidoscope scheme: `dir` holds a theme
 *  pack (theme.png and theme.txt, written by tools/scheme.py from the
 *  scheme's resource fork).  Returns 0, or -1 if the pack is not there;
 *  without one the look is System 7's, drawn by hand. */
int ui_set_theme(RUi *u, const char *dir);

/*  Take the scheme off again: the hand-drawn look, and the style
 *  colours the scheme had changed put back. */
void ui_clear_theme(RUi *u);

/*  Hand the UI an event.  Returns 1 when the UI owns the pointer or
 *  the keyboard for it, so the app should not act on it too. */
int ui_event(RUi *u, const union SDL_Event *e);
int ui_wants_mouse(const RUi *u);
int ui_wants_keyboard(const RUi *u);

/*  Build this frame's widgets from and into `s`. */
void ui_frame(RUi *u, RUiState *s);

/*  Draw the frame's widgets onto `swap`, inside the command buffer the
 *  renderer is about to submit.  Matches RGpuOverlay in gpu.h. */
void ui_render(void *u, struct SDL_GPUCommandBuffer *cmd, struct SDL_GPUTexture *swap, uint32_t w, uint32_t h);

/*  Append a line to the state's log. */
void ui_log(RUiState *s, const char *fmt, ...);

#endif /* R_UI_H */
