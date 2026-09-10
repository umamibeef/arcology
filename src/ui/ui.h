/*  ui.h: the in-game UI behind a C interface.  The widgets are Dear
 *  ImGui, hosted in ui.cpp, the one C++ file in the game.  Ui_none.c is
 *  the same interface doing nothing, for a build without it.  The app
 *  owns an RUiState: before the frame it fills in what the UI shows.
 *  The UI edits the switches in place and raises the commands.  After
 *  the frame the app applies them and clears them.  The UI knows nothing
 *  of the city or the renderer, only this struct.  The look is the
 *  original's.  Its tool palette and menus come from the resource fork's
 *  own art and menu resources.  The System 7 window chrome is drawn by
 *  hand.  Theming is kept in one place, apply_theme in ui.cpp.  So a
 *  Kaleidoscope scheme can drive it later (docs/future.rst). */
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
    /*  The map view's Shift-drag selection for the area report (app/area.c):
     *  its outline on screen, and its caption while the drag is on. */
    int         sel_ok;
    float       sel_poly[4][2];
    char        sel_text[200];
    /* the message log, newest last */
    char    log[RUI_LOG_LINES][RUI_LOG_LEN];
    int32_t n_log;

    /* --- the canon interface: the tool palette ------------------ */
    int tool; /* the selected tool, 0..33, or -1   */
    int show_palette, show_demand;

    /* --- switches: shown and edited in place -------------------- */
    int32_t speed; /* 1 pause .. 5                      */
    /*  geometry is the one switch: the terrain mesh, the water shader
     *  and the road strips together.  Plan is the map view, the camera
     *  raised to look straight down. */
    int geometry, plan, grid, plain_sweep, underground;
    int markings, furniture, margins; /* the road-marking, street-furniture and margin passes */
    /*  Outline: the roads and their works stand aside and the fitted
     *  curves and the margin network are drawn on bare ground.  A view of
     *  its own, from the View menu, --outline, or the last of --tune. */
    int outline;
    /*  The road geometry's knobs, live.
     *
     *      The two widths.
     *      The tightest and widest curve each family may take.
     *      The straight approach at a node.
     *      The corridor margin and the junction trim.
     *
     *  The app fills them from the mesh and takes them back when
     *  tune_changed. */
    float tune[19];
    int   tune_changed, show_tuning;
    /*  The script console: one line in, the last answers out.  The app
     *  owns the state, so the UI only shows what it is given and says
     *  when a line was typed. */
    int  show_script, script_run, script_reload;
    char script_line[512];
    char script_out[8192];
    char script_status[256];
    /*  Every cell's own number at its bottom-right corner, in the map
     *  view.  The app hands over the map's affine frame on screen.  It
     *  is the origin, and one column's and one row's step.  It holds
     *  while the camera looks straight down. */
    int     show_cells, cell_ok;
    float   cell_o[2], cell_c[2], cell_r[2];
    int32_t view; /* 0..11, the game's map views       */
    int     show_city, show_budget, show_graphs, show_disasters,
        show_query, show_renderer, show_log;
    /*  The component under the pointer, outlined: the edges of its
     *  silhouette as PAIRS of canvas points, in no order round it, with
     *  its name. */
    int     comp_ok, comp_n;
    float   comp_poly[4096][2];
    float   comp_at[2]; /* where the pointer is, in the same points: the name sits over it */
    char    comp_label[120];
    char    comp_who[64];   /* the function that drew what the pointer is on */
    char    comp_where[160];/* and the file and line it is written at, from src/ down */
    char    comp_gen[160];  /* what it is part of: its owner, and that owner's, up the tree */
    char    comp_gen_where[160];
    char    comp_note[1024]; /* and what that call said of the thing: lines of key, TAB, value, shown as rows */
    /*  Everything else the pointer is over, topmost first: one line each,
     *  with its material and the height it was met at.  Two surfaces at
     *  one point and one height are drawn over each other, which is what
     *  a silhouette cannot show. */
    char    comp_also[8][120];
    int     comp_n_also;
    float   comp_mat;       /* its material */
    uint32_t comp_tris;     /* how many triangles the component has ... */
    uint32_t comp_gen_tris; /* ... and the whole thing its generating call made.  0 when it was made under none */
    float   comp_box[6];    /* and the box it occupies */
    int     comp_box_ok;
    int     show_inspect; /* set by the app */
    int     want_inspect; /* raised by the UI, read and cleared by the app */
    char    inspect_text[8192];

    /* --- commands: raised by the UI, cleared by the app ---------- */
    int want_quit, want_save, want_screenshot;
    /*  the load menu.  The app fills city_list by scanning city_dir and
     *  raises open_load when it wants the menu up.  The UI raises
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
    /*  The themes.  They are every pack found under assets/themes, and
     *  the one worn now by name ("none" for the hand-drawn look).  A
     *  request to change comes back, which app applies and saves. */
    char theme_list[RUI_MAX_THEMES][64];
    int  n_themes;
    char theme_name[64];
    int  want_theme;
    /*  the music: on or off, and the request to flip it */
    int music_on, want_music;
    /*  LOADING.  Empty when nothing is.  The scripts are read in a few
     *  milliseconds and the world they describe takes a second and a
     *  half to build.  So what this announces is the BUILD: the frame
     *  carrying it is the last one drawn before the build blocks.  It is
     *  what the window shows for as long as that takes. */
    char loading[64];   /* what is happening, written ON the bar */
    char loading_note[64]; /* the detail under it */
    int  loading_step;  /* how far through the load, and how many steps it has */
    int  loading_steps;
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

/*  Create the UI on the window and device.  `swap_fmt` is the
 *  swapchain's SDL_GPUTextureFormat, `dpi` the window's pixel density.
 *  Returns NULL when the UI is not built in. */
RUi *ui_create(struct SDL_Window *win, struct SDL_GPUDevice *dev, int swap_fmt, float dpi, const char *assets_dir);
void ui_destroy(RUi *u);

/*  Dress the interface in a Kaleidoscope scheme: `dir` holds a theme
 *  pack (theme.png and theme.txt, written by tools/scheme.py from the
 *  scheme's resource fork).  Returns 0, or -1 if the pack is not there.
 *  Without one the look is System 7's, drawn by hand. */
int ui_set_theme(RUi *u, const char *dir);

/*  Take the scheme off again: the hand-drawn look, and the style colors
 *  the scheme had changed put back. */
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
