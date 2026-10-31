/*  ui/internal.hpp: what ui.cpp shares with the windows kept in files of
 *  their own. */
#ifndef ARC_UI_INTERNAL_HPP
#define ARC_UI_INTERNAL_HPP

#include <cfloat>

#include <SDL3/SDL.h>

#include "imgui.h"
#include "ui.h"

/*  The interface's own state, its art and the scheme over it. */
const ImU32 BLACK = IM_COL32(0, 0, 0, 255);
const ImU32 WHITE = IM_COL32(255, 255, 255, 255);

/*  Where each picture sits in assets/ui.png. */
struct Pict
{
    int id, x, y, w, h;
};

/*  A Kaleidoscope scheme, as tools/scheme.py packs it.  It holds
 *  elements cut from the scheme's color icons, with the corner size that
 *  stretches them.  It also holds the colors their info table names, and
 *  the parts of the window frames. */
struct ThemeEl
{
    char name[32];
    int  x, y, w, h, corner, side;
    int  bg[3], text[3];
};
struct ThemePart
{
    char window[24];
    int  part, x, y, w, h;
};
struct Theme
{
    bool            loaded;
    SDL_GPUTexture *tex;
    int             tw, th;
    ThemeEl         els[96];
    int             n_els;
    ThemePart       parts[64];
    int             n_parts;
};

struct RUi
{
    SDL_GPUDevice  *dev;
    SDL_GPUTexture *atlas;
    int             atlas_w, atlas_h;
    Pict            picts[80];
    int             n_picts;
    bool            frame_open;
    bool            save_popup;
    float           dpi;
    bool            help_mode;
    int             sub_for;        /* the tool whose sub-tool menu is open */
    int             held_open = -1; /* the tool whose button is held and opened its menu: its release is not a click */
    ImVec2          menu_pad;       /* the style's window padding, which a menu or a tooltip keeps inside a window that has none */
    Theme           theme;
    SDL_GPUTexture *art;      /* the tile art atlas, for the lane graph view's sprites */
    const uint8_t  *art_from; /* the RGBA it was uploaded from */
};

/*  The tile art atlas as a texture, uploaded again when `rgba` changes. */
SDL_GPUTexture *ui_art_texture(RUi *u, const uint8_t *rgba, int w, int h);

const float TITLE_H = 14.0f;

/*  The interface the menus and the window titles are drawn for, set each
 *  frame. */
extern const RUi *s_look;
extern RUi       *g_ui_for_title;

/*  What the files of the interface share: the art and the scheme's
 *  pieces, the window chrome, the menus' look and the themed controls
 *  (ui.cpp), the palette (palette.cpp), the menu bar (menubar.cpp) and the
 *  windows (windows.cpp). */
void           draw_pict(const RUi *u, ImDrawList *dl, int id, ImVec2 at, int sx = 0, int sy = 0, int sw = -1, int sh = -1);
const Pict    *find_pict(const RUi *u, int id);
bool           mac_title(const char *title);
void           menu_frame(void);
bool           menu_item(const char *label, const char *shortcut = nullptr, bool selected = false, bool enabled = true);
bool           menu_item(const char *label, const char *shortcut, bool *p_selected, bool enabled = true);
void           menu_look_pop(int n);
int            menu_look_push(void);
bool           theme_title(RUi *u, const char *window, const char *title, bool *drawn);
void           tooltip(const char *fmt, ...);
const ThemeEl *look_el(const char *name);
bool           begin_submenu(const char *label);
void           menu_divider(void);
void           menu_row_lit(float y0, float y1);
ImU32          theme_col(const int rgb[3], ImU32 fallback);
const ThemeEl *theme_el(const RUi *u, const char *name);
void           theme_nine(const RUi *u, ImDrawList *dl, const ThemeEl *e, ImVec2 a, ImVec2 b);
ImVec2         frame_pad(RUi *u, const char *window, ImVec2 margin);
bool           theme_frame(RUi *u, const char *window, const char *title, bool *drawn, float *top);
bool           ui_button(const char *label, ImVec2 size = ImVec2(0.0f, 0.0f), bool def = false);
bool           ui_check(const char *label, bool *v);
void           ui_progress(float f, float w, const char *over);
bool           ui_row(const char *label);
bool           ui_slider(const char *label, float *v, float lo, float hi, const char *fmt, float w);
void           palette(RUi *u, RUiState *s);
void           demand(RUi *u, RUiState *s);
void           fps_overlay(const RUiState *s);
void           menu_bar(RUi *u, RUiState *s);
const char    *sc(char key, bool shift = false);
void           budget_window(RUiState *s);
void           city_window(RUiState *s);
void           graphs_window(RUiState *s);
void           load_popup(RUi *u, RUiState *s);
void           log_window(RUiState *s);
void           picture_window(RUi *u, RUiState *s);
void           query_window(RUiState *s);
void           renderer_window(RUiState *s);
void           save_popup(RUi *u, RUiState *s);
void           tuning_window(RUiState *s);
void           shading_window(RUiState *s);

/*  A window as the Mac draws one, with its title bar and close box.
 *  `most` is the largest the window may grow, and a window held at one
 *  width names it there as well.  False while it is not drawn. */
bool mac_begin(const char *title, int *show, float w, ImVec2 first_pos, ImGuiWindowFlags extra = 0, ImVec2 most = ImVec2(FLT_MAX, FLT_MAX));

/*  The inspector (inspect.cpp): its window, and its outlines over the
 *  map. */
void inspect_window(RUiState *s);
void inspect_draw(RUiState *s);

/*  The scheme's scrollbars over ImGui's own (ui.cpp). */
void themed_scrollbars(void);

#endif /* ARC_UI_INTERNAL_HPP */
