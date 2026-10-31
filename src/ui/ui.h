/*  ui.h: the in-game UI behind a C interface.  The widgets are Dear
 *  ImGui, hosted in ui.cpp and inspect.cpp.  ui_none.c is the same
 *  interface doing nothing, for a build without it.  The app
 *  owns an RUiState: before the frame it fills in what the UI shows.
 *  The UI edits the switches in place and raises the commands.  After
 *  the frame the app applies them and clears them.  The UI knows nothing
 *  of the city or the renderer, only this struct.  The look is the
 *  original's.  Its tool palette and menus come from the resource fork's
 *  own art and menu resources.  The System 7 window chrome is drawn by
 *  hand.  The look is apply_theme's in ui.cpp, and a Kaleidoscope
 *  scheme lays over it (ui_set_theme). */
#ifndef R_UI_H
#define R_UI_H

/*  the most cities the load menu will list from one directory */
#define R_MAX_CITIES 512

#include <stddef.h>
#include <stdint.h>

/*  One tile's sprite, drawn small in its corner under the lane graph.
 *  It holds the tile, its altitude in levels, the art's rectangle in the
 *  atlas, and whether the art is mirrored. */
typedef struct
{
    int16_t  col, row, alt;
    uint16_t x, y, w, h;
    uint8_t  flip;
} RUiSprite;

#ifdef __cplusplus
extern "C" {
#endif

struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUCommandBuffer;
struct SDL_GPUTexture;
union SDL_Event;

#define RUI_LOG_LINES   24
#define RUI_LOG_LEN     96
#define RUI_N_GRAPH     16
#define RUI_MAX_THEMES  16
#define RUI_N_MAP_TOOLS 18 /* the palette's tools that act on the map, Bulldozer to Center */
#define RUI_N_SUB       12 /* the most sub-tools a tool has room for                       */
#define RUI_N_DEPT      16

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
/*  Each disaster's name.  The ones the Disasters menu holds are spelled as
 *  its MENU resource spells them. */
extern const char *const RUI_DISASTER_NAME[RUI_N_DISASTERS];

/*  ONE SLIDER of the tuning window.  The app fills these from the
 *  renderer's own store, which geo_defaults declared.  So the window
 *  keeps no list of knobs, no ranges and no defaults of its own.  A knob
 *  with no label gets no slider: some other control owns it. */
#define RUI_MAX_KNOBS 32
typedef struct
{
    const char *label;
    const char *group; /* a rule is drawn where this changes */
    float       v, lo, hi, def;
} RUiKnob;

/*  THE REQUESTS the interface raises in a frame.  The app acts on each
 *  and clears them all at once when the frame is applied (ui_apply). */
typedef struct
{
    int     quit, save, screenshot;
    int     load;      /* a city to load, in load_path */
    int32_t disaster;  /* RUI_DISASTER_* */
    int32_t zoom_step; /* +1 in, -1 out */
    int32_t rotate;    /* +1 clockwise, -1 anticlockwise */
    int32_t sound;     /* a sound id to play, 0 none */
    int     theme;     /* the scheme named in theme_name */
    int     music;     /* the music flipped on or off */
    int     inspect;   /* the inspector's switch flipped */
    int     remark;    /* the remark typed filed: 1 to the file and the clipboard, 2 to the clipboard alone */
    int     level;     /* 1 + NPICK_*, a level of the record clicked */
} RUiWant;

/*  What the inspector shows, filled by the app each frame. */
typedef struct
{
    int show; /* the inspector is up: set by the app */
    /*  The shape under the pointer.  It has its name, the function that
     *  drew it and where that is written, and what it is part of up the
     *  tree.  The note is the drawing call's own account of it, lines of
     *  key, TAB, value. */
    float at[2]; /* the pointer, in window points: the name sits over it */
    char  label[120];
    char  who[64];
    char  where[160];
    char  part_of[160];
    char  part_of_where[160];
    char  note[1024];
    /*  Everything else the pointer is over, topmost first, one line each
     *  with its material and the height it was met at.  Two surfaces
     *  that share a point and a height are drawn over each other. */
    char     also[8][120];
    int      n_also;
    float    mat;          /* its material */
    uint32_t tris;         /* its triangles ... */
    uint32_t part_of_tris; /* ... and those of the whole thing it is part of, 0 for none */
    float    box[6];       /* the box it occupies */
    int      box_ok;
    /*  The road network's record under the pointer: its levels from the
     *  lane up, each named, the one shown, and the records' rows. */
    int         rec_ok, pinned, level;
    int         depth, ndepth; /* how far down what is under the pointer the object shown lies, of how many */
    int         has[4];
    char        name[4][120];
    const char *text; /* the app's rows of key, TAB, value */
    /*  The objects shown, each outlined.  Their triangles are in window
     *  points, six floats a triangle, and the app holds them.  Each group
     *  is one component of the network, or one thing of the mesh.  It is
     *  outlined whole in its own color, RUI_OBJ_*.  A bright group is the
     *  record the window describes. */
    const float *tri;
    int          ngrp;
    int          grp[512][4]; /* first triangle, count, color, bright */
    /*  A remark on the record shown, typed in the window: the UI raises
     *  want.remark, and the app files it and says where. */
    char remark[512];
    char remark_said[200];
} RUiInspect;

typedef struct
{
    /* --- shown: filled by the app before the frame --------------- */
    char        city_name[64];
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
    int32_t     q_col, q_row, q_alt;
    int32_t     q_size, q_ocol, q_orow; /* the footprint: its edge, its north-east tile */
    uint8_t     q_xter, q_xbld, q_xzon, q_xbit;
    uint16_t    q_altm;
    const char *q_building;   /* a name when one is known, else NULL */
    char        q_mesh[160];  /* what the mesh made of the tile        */
    float       q_poly[4][2]; /* the footprint's corners on screen     */
    /*  The selected tool's area under the pointer.  The device lights its
     *  tiles (the marks, below).  Its corners on screen place the price
     *  beside it, and the price takes the color of the area's mark. */
    float   tool_poly[4][2];
    int     tool_poly_ok, tool_refused;
    int32_t tool_price;
    /*  The map view's Shift-drag selection for the area report (app/area.c).
     *  The device lights its tiles.  Its corners on screen place its
     *  caption. */
    int   sel_ok;
    float sel_poly[4][2];
    char  sel_text[200];
    /* the message log, newest last */
    char    log[RUI_LOG_LINES][RUI_LOG_LEN];
    int32_t n_log;

    /* --- the canon interface: the tool palette ------------------ */
    int tool; /* the selected tool, 0..33, or -1   */
    /*  The sub-tool each map tool has selected.  The original keeps the
     *  same at A5+0x1F88.  The app fills in what each sub-tool costs, and
     *  one bit a sub-tool for those the simulation can do. */
    int      sub[RUI_N_MAP_TOOLS];
    int32_t  sub_cost[RUI_N_MAP_TOOLS][RUI_N_SUB];
    uint16_t sub_ready[RUI_N_MAP_TOOLS];
    int      show_palette, show_demand;

    /* --- switches: shown and edited in place -------------------- */
    int32_t speed; /* 1 pause .. 5                      */
    /*  geometry is the one switch: the terrain mesh and the water shader
     *  together.  Plan is the map view, the camera raised to look
     *  straight down. */
    int geometry, plan, grid, plain_sweep, underground;
    /*  Debug: each triangle of the mesh in a color of its own, and the
     *  frame rate over the corner of the view. */
    int tris, show_fps;
    /*  Rotation free: the turn keys stop wherever they are let go.  Off,
     *  a turn comes to rest on a multiple of 90 degrees. */
    int free_turn;
    /*  No clouds: the sky's shadows are not drawn over the mesh. */
    int no_clouds;
    /*  No air: the frame is not seen through the air's haze and smog. */
    int no_air;
    /*  The trees' biome (the View menu): -1 for the one the city's place
     *  gives, else one of the biome names. */
    int         biome, n_biomes;
    const char *biome_names[16];
    /*  The time of day, from 0 to 24 (the View menu).  The app puts it in
     *  the store and keeps it between runs. */
    float hour;
    /*  Outline: the road network's lane graph laid over the bare ground.
     *  A view of its own, from the View menu, --outline, or --tune
     *  show_curves=1. */
    int outline;
    /*  Under the lane graph, each tile of the road network is marked in
     *  the color of its kind of sprite, and the legend names them.  Its
     *  sprite is drawn small in the tile's bottom-right corner, from the
     *  art atlas, `spr_w` by `spr_h` RGBA texels.  `cell_a` is what one
     *  level of altitude moves a point on screen. */
    int                tile_kinds;
    const char *const *tile_kind_name;
    const uint32_t    *tile_kind_rgb;
    const uint8_t     *spr_rgba;
    int                spr_w, spr_h;
    const RUiSprite   *spr;
    int                n_spr;
    float              cell_a[2];
    /*  The look's knobs, live, as geo_defaults declares them.  The app
     *  fills them from the store.  It takes them back when
     *  tune_changed. */
    RUiKnob knob[RUI_MAX_KNOBS];
    int     n_knob;
    int     tune_changed, show_tuning;
    /*  The shading numbers, live: the light, the clouds, the ground and
     *  the water.  The app fills each from the store, with its range, and
     *  takes them back when shading_changed.  They move the next frame and
     *  rebuild nothing. */
    RUiKnob shade[RUI_MAX_KNOBS];
    int     n_shade, shading_changed, show_shading;
    /*  Every cell's own number at its bottom-right corner, in the map
     *  view.  The app hands over the map's affine frame on screen.  It
     *  is the origin, and one column's and one row's step.  It holds
     *  while the camera looks straight down. */
    int     show_cells, cell_ok;
    float   cell_o[2], cell_c[2], cell_r[2];
    int32_t view; /* 0..11, the game's map views       */
    int     show_city, show_budget, show_graphs, show_query, show_renderer, show_log;
    /*  A picture the game shows in a window of its own: a PICT id, or 0.
     *  $154A opens one, for the protest that stops the bulldozer. */
    int32_t    show_picture;
    RUiInspect insp; /* the inspector: filled by the app (app/inspect.c), drawn by ui/inspect.cpp */

    /* --- commands: raised by the UI, cleared by the app ---------- */
    RUiWant want;
    /*  The title of a menu that is open from the first frame, so a shot
     *  can show it.  The name tool:N opens the sub-tool menu of tool N,
     *  and tip:N shows the tooltip of tool N. */
    const char *open_menu;
    /*  the load menu.  The app fills city_list by scanning city_dir and
     *  raises open_load when it wants the menu up.  The UI raises
     *  want.load with a path in load_path when the player picks one. */
    int  open_load;
    char load_path[512];
    char city_dir[512];
    char city_list[R_MAX_CITIES][80];
    int  n_cities;
    char save_path[512];
    /*  The themes.  They are every pack found under assets/themes, and
     *  the one worn now by name ("none" for the hand-drawn look).  A
     *  request to change comes back, which app applies and saves. */
    char theme_list[RUI_MAX_THEMES][64];
    int  n_themes;
    char theme_name[64];
    /*  the music: on or off */
    int music_on;
} RUiState;

/*  THE COMMANDS a menu item and a key both reach (commands.c).  Each is
 *  one change to the state above, and the app applies it as it applies
 *  every other change the interface makes.  The menu bar draws an item
 *  from its row, with the key at the right.  A key press runs the same
 *  row. */
enum
{
    RUI_CMD_LOAD = 0,
    RUI_CMD_SAVE,
    RUI_CMD_QUIT,
    RUI_CMD_BUDGET,
    RUI_CMD_POPULATION,
    RUI_CMD_GRAPHS,
    RUI_CMD_GEOMETRY,
    RUI_CMD_PLAN,
    RUI_CMD_UNDERGROUND,
    RUI_CMD_SCREENSHOT,
    RUI_CMD_GRID,
    RUI_CMD_OUTLINE,
    RUI_CMD_TRIANGLES,
    RUI_CMD_NO_DEPTH,
    RUI_CMD_INSPECT,
    RUI_N_CMD
};
typedef struct
{
    const char *label;
    char        key;    /* the letter with the command key, 0 for none */
    int         shift;  /* and with shift too */
    int         toggle; /* 1: the command flips its field.  0: it sets it to 1 */
    size_t      field;  /* the field of RUiState it changes */
    int         checks; /* the item carries a check mark, read from `shown` */
    size_t      shown;
} RUiCommand;
const RUiCommand *ui_command(int cmd);
/*  The command the command key and letter `key` (and shift) name, or -1. */
int  ui_command_key(char key, int shift);
void ui_command_do(RUiState *s, int cmd);
/*  Whether the command's item carries its check mark. */
int ui_command_shown(const RUiState *s, int cmd);

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
extern const char *const RUI_SUB_NAME[RUI_N_MAP_TOOLS][RUI_N_SUB]; /* NULL past the last */

/*  The color an outlined object is drawn in: what it is, or what it is
 *  to the record shown. */
enum
{
    RUI_OBJ_HIGHWAY = 0, /* a highway's driving lane */
    RUI_OBJ_STREET,      /* a street's driving lane */
    RUI_OBJ_RAMP,        /* a ramp's lane, or an interchange's */
    RUI_OBJ_CHANGE,      /* an entry or an exit lane: a lane gained or dropped */
    RUI_OBJ_SHOULDER,
    RUI_OBJ_MEDIAN,
    RUI_OBJ_SIDEWALK,
    RUI_OBJ_RAIL,
    RUI_OBJ_BORDER, /* a railway's gravel */
    RUI_OBJ_JUNCTION,
    RUI_OBJ_STRUCTURE, /* what is drawn for a road beside its lanes: a deck's sides, its columns, its caps */
    RUI_OBJ_THING,     /* anything else the mesh holds */
    RUI_OBJ_COLORS
};

/*  THE MARKS: what a highlighted tile on the map means.  The device lights
 *  every marked tile in its mark's color, in the mesh and on the art
 *  alike.  A rim goes round the outside of each marked area
 *  (tile_marks.glsl).
 *  A label that goes with a mark is drawn in the same color. */
enum
{
    RUI_MARK_NONE,
    RUI_MARK_HOVER,   /* the query's footprint, or what the tool would work on */
    RUI_MARK_REFUSED, /* what the tool would refuse */
    RUI_MARK_TAKEN,   /* what a drag has taken, until the world built after it is up */
    RUI_MARK_SELECT,  /* the Shift-drag area selection */
    RUI_MARKS
};

/*  The one color of a mark, as 0xRRGGBB. */
uint32_t ui_mark_color(int mark);

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

#ifdef __cplusplus
}
#endif

#endif /* R_UI_H */
