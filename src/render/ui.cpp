/*  ui.cpp -- the canon interface, hosted by Dear ImGui.
 *
 *  The original's interface, from its own resources: the tool palette is
 *  PICT 500 with the thirty-four buttons the help texts enumerate, in a
 *  floating windoid; the menus are the MENU resources, title by title
 *  and item by item; the windows wear System 7 chrome, a striped title
 *  bar with a close box, drawn here.  ImGui is the engine under it:
 *  layout, hit-testing, text and the draw lists.  The widgets see an
 *  RUiState and nothing else; the app fills it, they edit it, the app
 *  applies it.  apply_theme is the one place the look is set, for a
 *  Kaleidoscope scheme to drive later.
 *
 *  Not the original's yet: the text is ImGui's bitmap font, not Chicago
 *  (the game uses the system's fonts, which are not in its resources);
 *  the windows the palette opens are the game's stand-ins in the
 *  original's chrome, not the original's windows.
 */
extern "C" {
#include "ui.h"
/* lodepng is compiled as C in the vendor library; only this call is used */
unsigned lodepng_decode32_file(unsigned char **out, unsigned *w, unsigned *h, const char *filename);
}

#include <SDL3/SDL.h>

#include <cfloat>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imgui_internal.h" /* the Selectable flags MenuItem itself is built on */
#ifdef IMGUI_ENABLE_FREETYPE
    #include "imgui_freetype.h"
#endif

extern "C" const char *const RUI_TOOL_NAME[RUI_N_TOOLS] = {
    "Bulldozer", "Landscape", "Dispatch", "Power", "Water", "City Bonus", "Roads", "Rail", "Ports", "Residential", "Commercial", "Industrial", "Education", "Health & Safety", "Recreation", "Place Sign", "Query", "Center Display", "Zoom Out", "Zoom In", "Zone Demand", "Rotate CCW", "Rotate CW", "Map", "Population", "Neighbors", "Graphs", "Industry", "Budget", "Buildings", "Signs", "Roads & Trees", "Zones", "Under-view"};

static const ImU32 BLACK = IM_COL32(0, 0, 0, 255);
static const ImU32 WHITE = IM_COL32(255, 255, 255, 255);

/*  Where each picture sits in assets/ui.png. */
struct Pict
{
    int id, x, y, w, h;
};

/*  A Kaleidoscope scheme, as tools/scheme.py packs it: elements cut
 *  from the scheme's colour icons with the corner size that stretches
 *  them and the colours their info table names, and the parts of the
 *  window frames. */
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
    ThemeEl         els[48];
    int             n_els;
    ThemePart       parts[48];
    int             n_parts;
};

struct RUi
{
    SDL_Window     *win;
    SDL_GPUDevice  *dev;
    SDL_GPUTexture *atlas;
    int             atlas_w, atlas_h;
    Pict            picts[80];
    int             n_picts;
    bool            frame_open;
    bool            save_popup;
    float           dpi;
    bool            help_mode;
    Theme           theme;
};

static const char *SPEED_NAME[6]  = {"Pause", "Pause", "Turtle", "Llama", "Cheetah", "African Swallow"};
static const char *STAGE_NAME[10] = {"Village", "Town", "City", "Capital", "Metropolis", "Megalopolis", "Stage 6", "Stage 7", "Stage 8", "Stage 9"};
static const char *VIEW_NAME[12]  = {"Normal", "Traffic", "Density", "Growth and value", "Crime", "Police", "Pollution", "Land value", "Fire", "Power", "Water", "Growth"};
static const char *ZONE_NAME[16]  = {
    "none", "light residential", "dense residential", "light commercial", "dense commercial", "light industrial", "dense industrial", "military", "airport", "seaport", "zone 10", "zone 11", "zone 12", "zone 13", "zone 14", "zone 15"};

/*  The palette's buttons: rectangles in PICT 500, measured from its
 *  bevels (the light top and left edges, the dark bottom and right),
 *  and the tool each is.  The RCI column is the demand button. */
struct PalButton
{
    int x0, y0, x1, y1, tool;
};
static const PalButton PALETTE[] = {
    {1,  1,   22, 22,  RUI_TOOL_BULLDOZER        },
    {24, 1,   45, 22,  RUI_TOOL_LANDSCAPE        },
    {47, 1,   68, 22,  RUI_TOOL_DISPATCH         },
    {1,  25,  22, 46,  RUI_TOOL_POWER            },
    {24, 25,  45, 46,  RUI_TOOL_WATER            },
    {47, 25,  68, 46,  RUI_TOOL_BONUS            },
    {1,  49,  22, 70,  RUI_TOOL_ROADS            },
    {24, 49,  45, 70,  RUI_TOOL_RAIL             },
    {47, 49,  68, 70,  RUI_TOOL_PORTS            },
    {1,  73,  22, 94,  RUI_TOOL_RESIDENTIAL      },
    {24, 73,  45, 94,  RUI_TOOL_COMMERCIAL       },
    {47, 73,  68, 94,  RUI_TOOL_INDUSTRIAL       },
    {1,  97,  22, 118, RUI_TOOL_EDUCATION        },
    {24, 97,  45, 118, RUI_TOOL_HEALTH           },
    {47, 97,  68, 118, RUI_TOOL_RECREATION       },
    {6,  125, 33, 145, RUI_TOOL_SIGN             },
    {36, 125, 63, 145, RUI_TOOL_QUERY            },
    {6,  152, 33, 172, RUI_TOOL_ROTATE_CCW       },
    {36, 152, 63, 172, RUI_TOOL_ROTATE_CW        },
    {2,  174, 23, 195, RUI_TOOL_ZOOM_OUT         },
    {25, 174, 46, 195, RUI_TOOL_ZOOM_IN          },
    {48, 174, 69, 195, RUI_TOOL_CENTER           },
    {2,  202, 23, 219, RUI_TOOL_MAP              },
    {26, 202, 46, 219, RUI_TOOL_GRAPHS           },
    {2,  222, 23, 239, RUI_TOOL_POPULATION       },
    {26, 222, 46, 239, RUI_TOOL_INDUSTRY         },
    {2,  242, 23, 259, RUI_TOOL_NEIGHBORS        },
    {26, 242, 46, 259, RUI_TOOL_BUDGET           },
    {49, 202, 69, 259, RUI_TOOL_DEMAND           },
    {2,  266, 33, 279, RUI_TOOL_LAYER_BUILDINGS  },
    {37, 266, 67, 279, RUI_TOOL_LAYER_SIGNS      },
    {2,  281, 33, 294, RUI_TOOL_LAYER_ROADS      },
    {37, 281, 67, 294, RUI_TOOL_LAYER_ZONES      },
    {2,  296, 43, 309, RUI_TOOL_LAYER_UNDERGROUND},
    {47, 296, 67, 309, -1 /* help */             },
};
static const int N_PALETTE = (int)(sizeof PALETTE / sizeof PALETTE[0]);

/*  Tools that stay selected for clicks on the map, as against buttons
 *  that act at once. */
static bool is_map_tool(int tool)
{
    return tool >= 0 && tool <= RUI_TOOL_CENTER;
}

/* ---- the atlas -------------------------------------------------------- */

static const Pict *find_pict(const RUi *u, int id)
{
    for (int k = 0; k < u->n_picts; ++k)
        if (u->picts[k].id == id)
            return &u->picts[k];
    return nullptr;
}

/*  An RGBA image as a sampled texture. */
static SDL_GPUTexture *upload_rgba(SDL_GPUDevice *dev, const unsigned char *rgba, unsigned w, unsigned h)
{
    SDL_GPUTextureCreateInfo ti;
    memset(&ti, 0, sizeof ti);
    ti.type                 = SDL_GPU_TEXTURETYPE_2D;
    ti.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width                = w;
    ti.height               = h;
    ti.layer_count_or_depth = 1;
    ti.num_levels           = 1;
    SDL_GPUTexture *tex     = SDL_CreateGPUTexture(dev, &ti);
    if (!tex)
        return nullptr;
    SDL_GPUTransferBufferCreateInfo tb;
    memset(&tb, 0, sizeof tb);
    tb.usage                    = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tb.size                     = w * h * 4;
    SDL_GPUTransferBuffer *xfer = SDL_CreateGPUTransferBuffer(dev, &tb);
    void                  *map  = xfer ? SDL_MapGPUTransferBuffer(dev, xfer, false) : nullptr;
    if (!map)
    {
        SDL_ReleaseGPUTexture(dev, tex);
        return nullptr;
    }
    memcpy(map, rgba, w * h * 4);
    SDL_UnmapGPUTransferBuffer(dev, xfer);
    SDL_GPUCommandBuffer      *cmd = SDL_AcquireGPUCommandBuffer(dev);
    SDL_GPUCopyPass           *cp  = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src;
    SDL_GPUTextureRegion       dst;
    memset(&src, 0, sizeof src);
    memset(&dst, 0, sizeof dst);
    src.transfer_buffer = xfer;
    src.pixels_per_row  = w;
    src.rows_per_layer  = h;
    dst.texture         = tex;
    dst.w               = w;
    dst.h               = h;
    dst.d               = 1;
    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(dev, xfer);
    return tex;
}

static const ThemeEl *theme_el(const RUi *u, const char *name)
{
    if (!u->theme.loaded)
        return nullptr;
    for (int k = 0; k < u->theme.n_els; ++k)
        if (strcmp(u->theme.els[k].name, name) == 0)
            return &u->theme.els[k];
    return nullptr;
}

static const ThemePart *theme_part(const RUi *u, const char *window, int part)
{
    if (!u->theme.loaded)
        return nullptr;
    for (int k = 0; k < u->theme.n_parts; ++k)
        if (u->theme.parts[k].part == part && strcmp(u->theme.parts[k].window, window) == 0)
            return &u->theme.parts[k];
    return nullptr;
}

static ImU32 theme_col(const int rgb[3], ImU32 fallback)
{
    if (rgb[0] < 0)
        return fallback;
    return IM_COL32(rgb[0], rgb[1], rgb[2], 255);
}

/*  A piece of the theme atlas drawn into a screen rectangle, nearest. */
static void theme_blit(const RUi *u, ImDrawList *dl, int sx, int sy, int sw, int sh, ImVec2 p0, ImVec2 p1)
{
    if (sw <= 0 || sh <= 0 || p1.x <= p0.x || p1.y <= p0.y)
        return;
    ImVec2 uv0((float)sx / (float)u->theme.tw, (float)sy / (float)u->theme.th);
    ImVec2 uv1((float)(sx + sw) / (float)u->theme.tw, (float)(sy + sh) / (float)u->theme.th);
    dl->AddImage((ImTextureID)(intptr_t)u->theme.tex, p0, p1, uv0, uv1);
}

/*  An element stretched over a rectangle: the corners fixed, the edges
 *  and the middle stretched, by the scheme's corner size. */
static void theme_nine(const RUi *u, ImDrawList *dl, const ThemeEl *e, ImVec2 a, ImVec2 b)
{
    ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
    if (pio.DrawCallback_SetSamplerNearest)
        dl->AddCallback(pio.DrawCallback_SetSamplerNearest, nullptr);
    /*  The icon's centre carries the scheme's colour samples -- the
     *  pixel the info table names for the background, the one for the
     *  text -- so the centre is never stretched: it is filled with the
     *  background colour, and each edge is the one-pixel strip next to
     *  its corners, stretched along the edge. */
    int   c = e->corner;
    float W = b.x - a.x, H = b.y - a.y;
    if (c <= 0 || 2 * c >= e->w || 2 * c >= e->h || W < (float)(2 * c) || H < (float)(2 * c))
        theme_blit(u, dl, e->x, e->y, e->w, e->h, a, b);
    else
    {
        float fc = (float)c;
        ImU32 bg = theme_col(e->bg, IM_COL32(221, 221, 221, 255));
        dl->AddRectFilled(ImVec2(a.x + fc, a.y + fc), ImVec2(b.x - fc, b.y - fc), bg);
        /* corners */
        theme_blit(u, dl, e->x, e->y, c, c, a, ImVec2(a.x + fc, a.y + fc));
        theme_blit(u, dl, e->x + e->w - c, e->y, c, c, ImVec2(b.x - fc, a.y), ImVec2(b.x, a.y + fc));
        theme_blit(u, dl, e->x, e->y + e->h - c, c, c, ImVec2(a.x, b.y - fc), ImVec2(a.x + fc, b.y));
        theme_blit(u, dl, e->x + e->w - c, e->y + e->h - c, c, c, ImVec2(b.x - fc, b.y - fc), b);
        /* edges, from the strip beside each corner */
        theme_blit(u, dl, e->x + c, e->y, 1, c, ImVec2(a.x + fc, a.y), ImVec2(b.x - fc, a.y + fc));
        theme_blit(u, dl, e->x + c, e->y + e->h - c, 1, c, ImVec2(a.x + fc, b.y - fc), ImVec2(b.x - fc, b.y));
        theme_blit(u, dl, e->x, e->y + c, c, 1, ImVec2(a.x, a.y + fc), ImVec2(a.x + fc, b.y - fc));
        theme_blit(u, dl, e->x + e->w - c, e->y + c, c, 1, ImVec2(b.x - fc, a.y + fc), ImVec2(b.x, b.y - fc));
    }
    if (pio.DrawCallback_SetSamplerLinear)
        dl->AddCallback(pio.DrawCallback_SetSamplerLinear, nullptr);
}

static bool load_atlas(RUi *u, const char *assets_dir)
{
    char           path[1024];
    unsigned char *rgba = nullptr;
    unsigned       w = 0, h = 0;
    snprintf(path, sizeof path, "%s/ui.png", assets_dir);
    if (lodepng_decode32_file(&rgba, &w, &h, path) != 0 || !rgba)
        return false;
    snprintf(path, sizeof path, "%s/ui.json", assets_dir);
    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        free(rgba);
        return false;
    }
    char  *json = (char *)malloc(65536);
    size_t n    = fread(json, 1, 65535, fp);
    fclose(fp);
    json[n]    = 0;
    u->n_picts = 0;
    for (const char *p = json; (p = strchr(p, '"')) != nullptr && u->n_picts < 80;)
    {
        int id, x, y, pw, ph;
        if (sscanf(p, "\"%d\": [%d, %d, %d, %d]", &id, &x, &y, &pw, &ph) == 5 ||
            sscanf(p, "\"%d\":[%d,%d,%d,%d]", &id, &x, &y, &pw, &ph) == 5)
        {
            Pict &pc = u->picts[u->n_picts++];
            pc.id    = id;
            pc.x     = x;
            pc.y     = y;
            pc.w     = pw;
            pc.h     = ph;
        }
        p++;
        const char *q = strchr(p, '"');
        p             = q ? q + 1 : p;
    }
    free(json);
    u->atlas = upload_rgba(u->dev, rgba, w, h);
    free(rgba);
    if (!u->atlas)
        return false;
    u->atlas_w = (int)w;
    u->atlas_h = (int)h;
    return true;
}

extern "C" int ui_set_theme(RUi *u, const char *dir)
{
    if (!u)
        return -1;
    char path[1024];
    snprintf(path, sizeof path, "%s/theme.txt", dir);
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return -1;
    Theme &t = u->theme;
    t.n_els = t.n_parts = 0;
    char line[512];
    while (fgets(line, sizeof line, fp))
    {
        if (strncmp(line, "element ", 8) == 0 && t.n_els < 48)
        {
            ThemeEl &e = t.els[t.n_els];
            if (sscanf(line + 8, "%31s %d %d %d %d %d %d %d %d %d %d %d %d", e.name, &e.x, &e.y, &e.w, &e.h, &e.corner, &e.side, &e.bg[0], &e.bg[1], &e.bg[2], &e.text[0], &e.text[1], &e.text[2]) == 13)
                t.n_els++;
        }
        else if (strncmp(line, "part ", 5) == 0 && t.n_parts < 48)
        {
            ThemePart &p = t.parts[t.n_parts];
            if (sscanf(line + 5, "%23s %d %d %d %d %d", p.window, &p.part, &p.x, &p.y, &p.w, &p.h) == 6)
                t.n_parts++;
        }
    }
    fclose(fp);
    snprintf(path, sizeof path, "%s/theme.png", dir);
    unsigned char *rgba = nullptr;
    unsigned       w = 0, h = 0;
    if (lodepng_decode32_file(&rgba, &w, &h, path) != 0 || !rgba)
        return -1;
    if (t.tex)
        SDL_ReleaseGPUTexture(u->dev, t.tex);
    t.tex = upload_rgba(u->dev, rgba, w, h);
    free(rgba);
    if (!t.tex)
        return -1;
    t.tw     = (int)w;
    t.th     = (int)h;
    t.loaded = true;
    /* the colours the scheme names go into the style */
    ImGuiStyle    &st  = ImGui::GetStyle();
    const ThemeEl *bar = theme_el(u, "menu_bar"), *item = theme_el(u, "menu_item"),
                  *sel = theme_el(u, "menu_item_selected"), *bg = theme_el(u, "menu_background");
    if (bar && bar->bg[0] >= 0)
        st.Colors[ImGuiCol_MenuBarBg] = ImGui::ColorConvertU32ToFloat4(theme_col(bar->bg, WHITE));
    if (bg && bg->bg[0] >= 0)
        st.Colors[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(theme_col(bg->bg, WHITE));
    if (sel && sel->bg[0] >= 0)
    {
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(theme_col(sel->bg, BLACK));
        /* a highlight the text stays readable on: the scheme's, lightened */
        c.x                               = 0.6f + 0.4f * c.x;
        c.y                               = 0.6f + 0.4f * c.y;
        c.z                               = 0.6f + 0.4f * c.z;
        st.Colors[ImGuiCol_HeaderHovered] = c;
        st.Colors[ImGuiCol_Header]        = c;
    }
    if (item && item->text[0] >= 0)
        st.Colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(theme_col(item->text, BLACK));
    return 0;
}

static void apply_theme(float dpi); /* below: the hand-drawn look */

extern "C" void ui_clear_theme(RUi *u)
{
    if (!u)
        return;
    Theme &t = u->theme;
    if (t.tex)
        SDL_ReleaseGPUTexture(u->dev, t.tex); /* deferred by SDL until the GPU is done with it */
    t.tex    = nullptr;
    t.loaded = false;
    t.n_els = t.n_parts = 0;
    apply_theme(u->dpi); /* the style colours the scheme set, back to the hand-drawn look's */
}

/*  Draw a picture, or a part of it, at a screen position, one texel per
 *  point, sampled nearest so the pixels stay pixels. */
static void draw_pict(const RUi *u, ImDrawList *dl, int id, ImVec2 at, int sx = 0, int sy = 0, int sw = -1, int sh = -1)
{
    const Pict *p = find_pict(u, id);
    if (!p || !u->atlas)
        return;
    if (sw < 0)
        sw = p->w;
    if (sh < 0)
        sh = p->h;
    ImVec2           uv0((float)(p->x + sx) / (float)u->atlas_w,
                         (float)(p->y + sy) / (float)u->atlas_h);
    ImVec2           uv1((float)(p->x + sx + sw) / (float)u->atlas_w,
                         (float)(p->y + sy + sh) / (float)u->atlas_h);
    ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
    if (pio.DrawCallback_SetSamplerNearest)
        dl->AddCallback(pio.DrawCallback_SetSamplerNearest, nullptr);
    dl->AddImage((ImTextureID)(intptr_t)u->atlas, at, ImVec2(at.x + (float)sw, at.y + (float)sh), uv0, uv1);
    if (pio.DrawCallback_SetSamplerLinear)
        dl->AddCallback(pio.DrawCallback_SetSamplerLinear, nullptr);
}

/* ---- the look --------------------------------------------------------- */

/*  System 7: white windows with a black line round them, square, and
 *  black text.  Menus are white with black text; a menu drops down as a
 *  white box with a shadow.  Fonts are the bitmap default until Chicago
 *  is at hand. */
static void apply_theme(float dpi)
{
    ImGuiStyle &st = ImGui::GetStyle();
    ImGui::StyleColorsLight(&st);
    st.WindowRounding    = 0.0f;
    st.FrameRounding     = 0.0f;
    st.ChildRounding     = 0.0f;
    st.PopupRounding     = 0.0f;
    st.ScrollbarRounding = 0.0f;
    st.GrabRounding      = 0.0f;
    st.TabRounding       = 0.0f;
    st.WindowBorderSize  = 1.0f;
    st.FrameBorderSize   = 1.0f;
    st.PopupBorderSize   = 1.0f;
    st.WindowPadding     = ImVec2(8.0f, 6.0f);
    st.ItemSpacing       = ImVec2(6.0f, 4.0f);
    st.WindowTitleAlign  = ImVec2(0.5f, 0.5f);
    ImVec4 white(1, 1, 1, 1), black(0, 0, 0, 1), grey(0.8f, 0.8f, 0.8f, 1);
    st.Colors[ImGuiCol_WindowBg]       = white;
    st.Colors[ImGuiCol_PopupBg]        = white;
    st.Colors[ImGuiCol_Border]         = black;
    st.Colors[ImGuiCol_Text]           = black;
    st.Colors[ImGuiCol_TextDisabled]   = ImVec4(0.55f, 0.55f, 0.55f, 1);
    st.Colors[ImGuiCol_MenuBarBg]      = white;
    st.Colors[ImGuiCol_Header]         = grey;
    st.Colors[ImGuiCol_HeaderHovered]  = grey;
    st.Colors[ImGuiCol_HeaderActive]   = ImVec4(0.6f, 0.6f, 0.6f, 1);
    st.Colors[ImGuiCol_FrameBg]        = white;
    st.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.93f, 0.93f, 0.93f, 1);
    st.Colors[ImGuiCol_Button]         = white;
    st.Colors[ImGuiCol_ButtonHovered]  = grey;
    st.Colors[ImGuiCol_ButtonActive]   = black;
    st.Colors[ImGuiCol_TitleBg]        = white;
    st.Colors[ImGuiCol_TitleBgActive]  = white;
    st.Colors[ImGuiCol_PlotLines]      = black;
    st.Colors[ImGuiCol_PlotHistogram]  = black;
    st.Colors[ImGuiCol_ScrollbarBg]    = white;
    st.Colors[ImGuiCol_ScrollbarGrab]  = grey;
    st.Colors[ImGuiCol_CheckMark]      = black;
    st.Colors[ImGuiCol_SliderGrab]     = black;
    if (dpi > 0.0f && dpi != 1.0f)
        st.ScaleAllSizes(dpi);
}

/*  A System 7 title bar over the top of the current window: the striped
 *  bar, the title in a white box, the close box at the left.  Returns
 *  true if the close box was clicked.  Windows are begun without
 *  ImGui's own title bar and reserve TITLE_H at the top for this. */
static const float TITLE_H = 14.0f;

/*  The scheme's title bar, when one is loaded: the frame's left part up
 *  to its one-pixel title tile, the tile stretched across, the right
 *  part anchored right, and the title in the middle; the close box is
 *  where the scheme's window definition puts it.  Returns true if the
 *  close box was clicked, false if there is no scheme. */
static bool theme_title(RUi *u, const char *window, const char *title, bool *drawn)
{
    *drawn            = false;
    const ThemeEl *fr = theme_el(u, strcmp(window, "utility") == 0 ? "utility_active" : "window_active");
    if (!fr)
        return false;
    /* the tile column: the part one pixel wide; the close box: part 1 */
    const ThemePart *tile = nullptr, *close = theme_part(u, window, 1);
    for (int k = 0; k < u->theme.n_parts; ++k)
        if (u->theme.parts[k].w == 1 && strcmp(u->theme.parts[k].window, window) == 0)
            tile = &u->theme.parts[k];
    if (!tile)
        return false;
    ImDrawList *dl    = ImGui::GetWindowDrawList();
    ImVec2      p     = ImGui::GetWindowPos();
    ImVec2      s     = ImGui::GetWindowSize();
    float       bar_h = (float)(tile->y + tile->h + 2);
    if (bar_h > (float)fr->h)
        bar_h = (float)fr->h;
    ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
    if (pio.DrawCallback_SetSamplerNearest)
        dl->AddCallback(pio.DrawCallback_SetSamplerNearest, nullptr);
    int lw = tile->x, rw = fr->w - tile->x - 1;
    theme_blit(u, dl, fr->x, fr->y, lw, (int)bar_h, ImVec2(p.x, p.y), ImVec2(p.x + (float)lw, p.y + bar_h));
    theme_blit(u, dl, fr->x + tile->x, fr->y, 1, (int)bar_h, ImVec2(p.x + (float)lw, p.y), ImVec2(p.x + s.x - (float)rw, p.y + bar_h));
    theme_blit(u, dl, fr->x + tile->x + 1, fr->y, rw, (int)bar_h, ImVec2(p.x + s.x - (float)rw, p.y), ImVec2(p.x + s.x, p.y + bar_h));
    if (pio.DrawCallback_SetSamplerLinear)
        dl->AddCallback(pio.DrawCallback_SetSamplerLinear, nullptr);
    if (title && title[0])
    {
        ImVec2         ts  = ImGui::CalcTextSize(title);
        float          tx  = p.x + (s.x - ts.x) * 0.5f;
        float          ty  = p.y + (float)tile->y + ((float)tile->h - ts.y) * 0.5f;
        const ThemeEl *bar = theme_el(u, "menu_bar");
        dl->AddRectFilled(ImVec2(tx - 6.0f, p.y + (float)tile->y), ImVec2(tx + ts.x + 6.0f, p.y + (float)(tile->y + tile->h)), bar ? theme_col(bar->bg, WHITE) : WHITE);
        dl->AddText(ImVec2(tx, ty), BLACK, title);
    }
    *drawn      = true;
    bool closed = false;
    if (close)
    {
        ImVec2 c0(p.x + (float)close->x, p.y + (float)close->y);
        ImGui::SetCursorScreenPos(c0);
        closed = ImGui::InvisibleButton("##close", ImVec2((float)close->w, (float)close->h));
        if (ImGui::IsItemActive())
            dl->AddRectFilled(c0, ImVec2(c0.x + (float)close->w, c0.y + (float)close->h), IM_COL32(0, 0, 0, 110));
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + ImGui::GetStyle().WindowPadding.x, p.y + bar_h + 6.0f));
    return closed;
}

static RUi *g_ui_for_title = nullptr; /* the interface whose theme mac_title draws */

static bool mac_title(const char *title)
{
    if (g_ui_for_title)
    {
        bool drawn  = false;
        bool closed = theme_title(g_ui_for_title, "document", title, &drawn);
        if (drawn)
            return closed;
    }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2      p  = ImGui::GetWindowPos();
    ImVec2      s  = ImGui::GetWindowSize();
    float       x0 = p.x, y0 = p.y, x1 = p.x + s.x;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + TITLE_H), WHITE);
    dl->AddLine(ImVec2(x0, y0 + TITLE_H), ImVec2(x1, y0 + TITLE_H), BLACK);
    for (int k = 0; k < 6; ++k)
    {
        float y = y0 + 2.0f + (float)k * 2.0f;
        dl->AddLine(ImVec2(x0 + 2.0f, y), ImVec2(x1 - 2.0f, y), BLACK);
    }
    ImVec2 ts = ImGui::CalcTextSize(title);
    float  tx = x0 + (s.x - ts.x) * 0.5f;
    dl->AddRectFilled(ImVec2(tx - 6.0f, y0), ImVec2(tx + ts.x + 6.0f, y0 + TITLE_H - 1.0f), WHITE);
    dl->AddText(ImVec2(tx, y0 + (TITLE_H - ts.y) * 0.5f), BLACK, title);
    /* the close box */
    ImVec2 c0(x0 + 7.0f, y0 + 2.0f), c1(x0 + 18.0f, y0 + 13.0f);
    dl->AddRectFilled(ImVec2(c0.x - 1.0f, c0.y - 1.0f), ImVec2(c1.x + 1.0f, c1.y + 1.0f), WHITE);
    dl->AddRect(c0, c1, BLACK);
    ImGui::SetCursorScreenPos(c0);
    bool closed = ImGui::InvisibleButton("##close", ImVec2(c1.x - c0.x, c1.y - c0.y));
    if (ImGui::IsItemActive())
        dl->AddRectFilled(ImVec2(c0.x + 1.0f, c0.y + 1.0f), ImVec2(c1.x - 1.0f, c1.y - 1.0f), BLACK);
    ImGui::SetCursorScreenPos(ImVec2(x0 + ImGui::GetStyle().WindowPadding.x, y0 + TITLE_H + 6.0f));
    return closed;
}

static bool mac_begin(const char *title, int *show, float w, ImVec2 first_pos, ImGuiWindowFlags extra = 0)
{
    bool open = *show != 0;
    ImGui::SetNextWindowSize(ImVec2(w, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(first_pos, ImGuiCond_FirstUseEver);
    char id[128];
    snprintf(id, sizeof id, "##%s", title);
    bool vis = ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | extra);
    if (mac_title(title))
        open = false;
    *show = open ? 1 : 0;
    return vis;
}

/* ---- the palette ------------------------------------------------------- */

static void palette(RUi *u, RUiState *s)
{
    if (!s->show_palette)
        return;
    const Pict *pal = find_pict(u, 500);
    if (!pal)
        return;
    const float W = (float)pal->w + 2.0f, H = (float)pal->h + TITLE_H + 2.0f;
    ImGui::SetNextWindowPos(ImVec2(10.0f, 28.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##palette", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2      p  = ImGui::GetWindowPos();
    /* the windoid's title bar: the scheme's utility frame, else stripes */
    float x0 = p.x, y0 = p.y, x1 = p.x + W;
    bool  themed = false;
    if (theme_title(u, "utility", "", &themed) && themed)
        s->show_palette = 0;
    if (!themed)
    {
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + TITLE_H), WHITE);
        for (int k = 0; k < 6; ++k)
        {
            float y = y0 + 2.0f + (float)k * 2.0f;
            dl->AddLine(ImVec2(x0 + 2.0f, y), ImVec2(x1 - 2.0f, y), BLACK);
        }
        dl->AddLine(ImVec2(x0, y0 + TITLE_H), ImVec2(x1, y0 + TITLE_H), BLACK);
        ImVec2 c0(x0 + 7.0f, y0 + 2.0f), c1(x0 + 18.0f, y0 + 13.0f);
        dl->AddRectFilled(ImVec2(c0.x - 1.0f, c0.y - 1.0f), ImVec2(c1.x + 1.0f, c1.y + 1.0f), WHITE);
        dl->AddRect(c0, c1, BLACK);
        ImGui::SetCursorScreenPos(c0);
        if (ImGui::InvisibleButton("##pclose", ImVec2(11.0f, 11.0f)))
            s->show_palette = 0;
    }
    ImVec2 at(x0 + 1.0f, y0 + TITLE_H + 1.0f);
    draw_pict(u, dl, 500, at);
    /* the buttons */
    for (int k = 0; k < N_PALETTE; ++k)
    {
        const PalButton &b = PALETTE[k];
        ImVec2           b0(at.x + (float)b.x0, at.y + (float)b.y0);
        ImVec2           sz((float)(b.x1 - b.x0 + 1), (float)(b.y1 - b.y0 + 1));
        char             id[16];
        snprintf(id, sizeof id, "##pb%d", k);
        ImGui::SetCursorScreenPos(b0);
        bool clicked = ImGui::InvisibleButton(id, sz);
        bool down    = ImGui::IsItemActive();
        bool pressed = down || (b.tool >= 0 && b.tool == s->tool && is_map_tool(b.tool)) ||
                       (b.tool == RUI_TOOL_DEMAND && s->show_demand) ||
                       (b.tool == RUI_TOOL_LAYER_UNDERGROUND && s->underground) ||
                       (b.tool < 0 && u->help_mode);
        if (pressed)
            draw_pict(u, dl, 100500, b0, b.x0, b.y0, b.x1 - b.x0 + 1, b.y1 - b.y0 + 1);
        if (ImGui::IsItemHovered() && b.tool >= 0)
            ImGui::SetTooltip("%s", RUI_TOOL_NAME[b.tool]);
        if (!clicked)
            continue;
        s->want_sound = 505; /* Click */
        if (b.tool < 0)
        {
            u->help_mode = !u->help_mode;
            ui_log(s, "Shift-click help: not yet ported");
            continue;
        }
        if (u->help_mode)
        {
            ui_log(s, "%s", RUI_TOOL_NAME[b.tool]);
            u->help_mode = false;
            continue;
        }
        switch (b.tool)
        {
            case RUI_TOOL_ZOOM_OUT:
                s->want_zoom_step = -1;
                break;
            case RUI_TOOL_ZOOM_IN:
                s->want_zoom_step = 1;
                break;
            case RUI_TOOL_DEMAND:
                s->show_demand = !s->show_demand;
                break;
            case RUI_TOOL_ROTATE_CCW:
                s->want_rotate = -1;
                break;
            case RUI_TOOL_ROTATE_CW:
                s->want_rotate = 1;
                break;
            case RUI_TOOL_MAP:
                ui_log(s, "Map window: not yet ported");
                break;
            case RUI_TOOL_POPULATION:
                s->show_city = 1;
                break;
            case RUI_TOOL_NEIGHBORS:
                ui_log(s, "Neighbors window: not yet ported");
                break;
            case RUI_TOOL_GRAPHS:
                s->show_graphs = 1;
                break;
            case RUI_TOOL_INDUSTRY:
                ui_log(s, "Industry window: not yet ported");
                break;
            case RUI_TOOL_BUDGET:
                s->show_budget = 1;
                break;
            case RUI_TOOL_LAYER_UNDERGROUND:
                s->underground = !s->underground;
                break;
            case RUI_TOOL_LAYER_BUILDINGS:
            case RUI_TOOL_LAYER_SIGNS:
            case RUI_TOOL_LAYER_ROADS:
            case RUI_TOOL_LAYER_ZONES:
                ui_log(s, "%s layer toggle: not yet ported", RUI_TOOL_NAME[b.tool]);
                break;
            default:
                s->tool = (s->tool == b.tool) ? -1 : b.tool;
                if (s->tool == RUI_TOOL_QUERY)
                    s->show_query = 1;
                break;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

/*  The zone demand indicator: three bars from a centre line, up for
 *  demand and down for surplus, residential green, commercial blue,
 *  industrial yellow, as the original's popup draws them. */
static void demand(RUi *u, RUiState *s)
{
    (void)u;
    if (!s->show_demand)
        return;
    ImGui::SetNextWindowPos(ImVec2(96.0f, 240.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(92.0f, 96.0f + TITLE_H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##demand", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
    if (mac_title("RCI"))
        s->show_demand = 0;
    ImDrawList *dl  = ImGui::GetWindowDrawList();
    ImVec2      p   = ImGui::GetWindowPos();
    float       top = p.y + TITLE_H + 8.0f, mid = top + 40.0f, bottom = mid + 40.0f;
    dl->AddLine(ImVec2(p.x + 8.0f, mid), ImVec2(p.x + 84.0f, mid), BLACK);
    const ImU32 cols[3] = {IM_COL32(0, 160, 0, 255), IM_COL32(0, 0, 220, 255), IM_COL32(230, 200, 0, 255)};
    const char *lab[3]  = {"R", "C", "I"};
    for (int k = 0; k < 3; ++k)
    {
        float x = p.x + 16.0f + (float)k * 26.0f;
        float v = (float)s->demand[k] / 2000.0f;
        if (v > 1.0f)
            v = 1.0f;
        if (v < -1.0f)
            v = -1.0f;
        float y = mid - v * 38.0f;
        dl->AddRectFilled(ImVec2(x, y < mid ? y : mid), ImVec2(x + 14.0f, y < mid ? mid : y), cols[k]);
        dl->AddRect(ImVec2(x, y < mid ? y : mid), ImVec2(x + 14.0f, y < mid ? mid : y), BLACK);
        dl->AddText(ImVec2(x + 3.0f, bottom + 2.0f), BLACK, lab[k]);
    }
    (void)top;
    /*  Everything above went straight into the draw list, so this
     *  window submits no item at all -- and mac_title left the cursor
     *  below the title bar, which ImGui reads as a boundary extension
     *  nobody closed ("Code uses SetCursorPos()... please submit an
     *  item e.g. Dummy() afterwards").  One Dummy the size of the
     *  content says what we actually covered. */
    {
        ImVec2 ws = ImGui::GetWindowSize();
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + TITLE_H));
        ImGui::Dummy(ImVec2(ws.x, ws.y - TITLE_H));
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

/* ---- the menu bar ------------------------------------------------------ */

/* ---- menus: the shortcut at the right, the check at the left ------------- */

/*  The Mac's menus put a check mark at the left of an item and its key
 *  at the far right, with the ⌘ that every one of them needs; ImGui's
 *  MenuItem puts the key in a column just past the widest label and the
 *  mark to the right of that (the user: "they need to be right
 *  aligned").  So an item is a Selectable that spans the row -- the
 *  highlight covers it, the layout width is the label's and the key's
 *  -- with the three pieces drawn on it by hand.  Every shortcut here
 *  takes the platform's command key: ⌘ on the Mac, Ctrl elsewhere, and
 *  the labels say which (the user: "gated behind command/ctrl, and show
 *  the respective symbol"). */
#ifdef __APPLE__
    #define SC_MOD   "\xE2\x8C\x98" /* ⌘ U+2318 */
    #define SC_SHIFT "\xE2\x87\xA7" /* ⇧ U+21E7 */
#else
    #define SC_MOD   "Ctrl+"
    #define SC_SHIFT "Shift+"
#endif
#define CHECK_MARK "\xE2\x9C\x93" /* ✓ U+2713, in Chicago */

/*  "⌘L", or "⇧⌘T" with shift -- the Mac writes the shift first; Windows
 *  writes Ctrl+Shift+T.  A ring of buffers so several can be live in
 *  one menu. */
static const char *sc(char key, bool shift = false)
{
    static char buf[16][24];
    static int  n;
    char       *b = buf[n++ & 15];
#ifdef __APPLE__
    snprintf(b, sizeof buf[0], "%s%s%c", shift ? SC_SHIFT : "", SC_MOD, key);
#else
    snprintf(b, sizeof buf[0], "%s%s%c", SC_MOD, shift ? SC_SHIFT : "", key);
#endif
    return b;
}

static bool menu_item(const char *label, const char *shortcut = nullptr, bool selected = false, bool enabled = true)
{
    const ImGuiStyle &st      = ImGui::GetStyle();
    const float       check_w = ImGui::CalcTextSize(CHECK_MARK).x + st.ItemInnerSpacing.x;
    const float       lw      = ImGui::CalcTextSize(label).x;
    const float       sw      = shortcut ? ImGui::CalcTextSize(shortcut).x : 0.0f;
    /*  the layout width: the check column, the label, and room for the
     *  key; SpanAvailWidth then stretches the highlight to the row */
    const float need = check_w + lw + (shortcut ? 4.0f * st.ItemSpacing.x + sw : 0.0f);
    ImGui::PushID(label);
    if (!enabled)
        ImGui::BeginDisabled();
    const bool pressed = ImGui::Selectable("##item", false, ImGuiSelectableFlags_SelectOnRelease | ImGuiSelectableFlags_SetNavIdOnHover | ImGuiSelectableFlags_SpanAvailWidth, ImVec2(need, 0.0f));
    if (!enabled)
        ImGui::EndDisabled();
    ImGui::PopID();
    ImDrawList  *dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const ImU32  col = ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    const float  ty  = a.y + (b.y - a.y - ImGui::GetTextLineHeight()) * 0.5f;
    if (selected)
        dl->AddText(ImVec2(a.x, ty), col, CHECK_MARK);
    dl->AddText(ImVec2(a.x + check_w, ty), col, label);
    if (shortcut)
        dl->AddText(ImVec2(b.x - sw, ty), col, shortcut);
    return pressed;
}

/*  The toggle form, as ImGui's: flips *p_selected on a pick. */
static bool menu_item(const char *label, const char *shortcut, bool *p_selected, bool enabled = true)
{
    const bool pressed = menu_item(label, shortcut, p_selected && *p_selected, enabled);
    if (pressed && p_selected)
        *p_selected = !*p_selected;
    return pressed;
}

static void menu_bar(RUi *u, RUiState *s)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
    if (!ImGui::BeginMainMenuBar())
    {
        ImGui::PopStyleVar();
        return;
    }
    /* the bar: the scheme's menu bar element stretched over it, else
     * white with a black underline */
    {
        ImDrawList    *dl  = ImGui::GetWindowDrawList();
        ImVec2         p   = ImGui::GetWindowPos();
        ImVec2         sz  = ImGui::GetWindowSize();
        const ThemeEl *bar = theme_el(u, "menu_bar");
        if (bar)
            theme_nine(u, dl, bar, p, ImVec2(p.x + sz.x, p.y + sz.y));
        else
            dl->AddLine(ImVec2(p.x, p.y + sz.y - 1.0f), ImVec2(p.x + sz.x, p.y + sz.y - 1.0f), BLACK);
    }
    /*  MENU 1000 is the Apple menu: the Mac's, not the game's, so it is
     *  not drawn; its one item, About SimCity 2000, waits for a home. */
    /* MENU 1001 */
    if (ImGui::BeginMenu("File"))
    {
        menu_item("Load Tile Set", nullptr, false, false);
        ImGui::Separator();
        if (menu_item("Load City", sc('L')))
            s->open_load = 1;
        menu_item("New City", sc('N'), false, false);
        menu_item("Edit New Map", sc('E'), false, false);
        menu_item("Load Scenario", sc('Z'), false, false);
        ImGui::Separator();
        if (menu_item("Save City", sc('S')))
            s->want_save = 1;
        if (menu_item("Save City As...", nullptr))
            u->save_popup = true;
        ImGui::Separator();
        if (menu_item("Quit", sc('Q')))
            s->want_quit = 1;
        ImGui::EndMenu();
    }
    /* MENU 1002 */
    if (ImGui::BeginMenu("Speed"))
    {
        const char *keys[6] = {"", "1", "2", "3", "4", "5"}; /* bare digits, as the keys are */
        for (int k = 1; k <= 5; ++k)
            if (menu_item(SPEED_NAME[k], keys[k], s->speed == k))
                s->speed = k;
        ImGui::EndMenu();
    }
    /* MENU 1003 */
    if (ImGui::BeginMenu("Options"))
    {
        menu_item("Auto-Budget", nullptr, false, false);
        menu_item("Auto-Goto", nullptr, true, false);
        menu_item("Sound Effects", nullptr, true, false);
        menu_item("Music", nullptr, true, false);
        ImGui::Separator();
        /*  The Kaleidoscope schemes: None, then every pack found under
         *  assets/themes.  A pick is a request; app puts the scheme
         *  on and writes the preference. */
        if (ImGui::BeginMenu("Theme"))
        {
            const bool none = s->theme_name[0] == 0 || strcmp(s->theme_name, "none") == 0;
            if (menu_item("None", nullptr, none) && !none)
            {
                snprintf(s->theme_name, sizeof s->theme_name, "none");
                s->want_theme = 1;
            }
            if (s->n_themes)
                ImGui::Separator();
            for (int k = 0; k < s->n_themes; ++k)
            {
                const bool cur = strcmp(s->theme_list[k], s->theme_name) == 0;
                if (menu_item(s->theme_list[k], nullptr, cur) && !cur)
                {
                    snprintf(s->theme_name, sizeof s->theme_name, "%s", s->theme_list[k]);
                    s->want_theme = 1;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    /* MENU 1004 */
    if (ImGui::BeginMenu("Disasters"))
    {
        struct
        {
            const char *name;
            int         id;
        } items[8] = {
            {"Fire",       RUI_DISASTER_FIRE      },
            {"Flood",      RUI_DISASTER_FLOOD     },
            {"Air Crash",  RUI_DISASTER_AIR_CRASH },
            {"Tornado",    RUI_DISASTER_TORNADO   },
            {"Earthquake", RUI_DISASTER_EARTHQUAKE},
            {"Monster",    RUI_DISASTER_MONSTER   },
            {"Hurricane",  RUI_DISASTER_HURRICANE },
            {"Rioters",    RUI_DISASTER_RIOT      }
        };
        for (int k = 0; k < 8; ++k)
            if (menu_item(items[k].name))
                s->want_disaster = items[k].id;
        ImGui::Separator();
        menu_item("No Disasters", nullptr, false, false);
        ImGui::EndMenu();
    }
    /* MENU 1005 */
    if (ImGui::BeginMenu("Windows"))
    {
        if (menu_item("Map", sc('M')))
            ui_log(s, "Map window: not yet ported");
        if (menu_item("Budget", sc('B')))
            s->show_budget = 1;
        menu_item("Ordinances", sc('O'), false, false);
        if (menu_item("Population", sc('C')))
            s->show_city = 1;
        menu_item("Industry", sc('I'), false, false);
        if (menu_item("Graphs", sc('G')))
            s->show_graphs = 1;
        menu_item("Neighbors", sc('H'), false, false);
        ImGui::Separator();
        if (menu_item("Road tuning", nullptr, s->show_tuning != 0))
            s->show_tuning = !s->show_tuning;
        if (menu_item("Tools", nullptr, s->show_palette != 0))
            s->show_palette = !s->show_palette;
        if (menu_item("Query", nullptr, s->show_query != 0))
            s->show_query = !s->show_query;
        if (menu_item("Messages", nullptr, s->show_log != 0))
            s->show_log = !s->show_log;
        if (menu_item("Renderer", nullptr, s->show_renderer != 0))
            s->show_renderer = !s->show_renderer;
        ImGui::EndMenu();
    }
    /* MENU 1006 */
    if (ImGui::BeginMenu("Newspaper"))
    {
        menu_item("Subscription", nullptr, false, false);
        menu_item("Extra!!!", nullptr, false, false);
        ImGui::EndMenu();
    }
    /* the view switches, ours, at the right */
    if (ImGui::BeginMenu("View"))
    {
        /*  No art set and no pixel scale here: the zoom is continuous and
         *  picks the set that is closest to native by itself, and the
         *  pixel scale is a developer's knob, not a view (the user: "You
         *  can get rid of zoom and pixel scale options too").  The keys
         *  and the command line still reach both. */
        bool b;
        b = s->geometry != 0;
        if (menu_item("Geometry", sc('T', true), &b))
            s->geometry = b;
        b = s->plan != 0;
        if (menu_item("Map view", sc('N', true), &b))
            s->plan = b;
        b = s->grid != 0;
        if (menu_item("Grid", sc('G', true), &b))
            s->grid = b;
        b = s->show_coords != 0;
        if (menu_item("Coordinates", nullptr, &b))
            s->show_coords = b;
        ImGui::Separator();
        if (ImGui::BeginMenu("Data view"))
        {
            for (int k = 0; k < 12; ++k)
                if (menu_item(VIEW_NAME[k], nullptr, s->view == k))
                    s->view = k;
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (menu_item("Screenshot", sc('P', true)))
            s->want_screenshot = 1;
        ImGui::EndMenu();
    }
    /* the status, at the right of the bar, as the window title carries it */
    {
        char right[128];
        snprintf(right, sizeof right, "%s   $%d   %d", s->city_name[0] ? s->city_name : "", (int)s->funds, (int)s->population);
        float w = ImGui::CalcTextSize(right).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - w - 12.0f);
        ImGui::TextUnformatted(right);
    }
    ImGui::EndMainMenuBar();
    ImGui::PopStyleVar();
}

/* ---- the windows -------------------------------------------------------- */

static void city_window(RUiState *s)
{
    if (!mac_begin("Population", &s->show_city, 300.0f, ImVec2(110.0f, 40.0f)))
    {
        ImGui::End();
        return;
    }
    int stage = s->stage >= 0 && s->stage < 10 ? (int)s->stage : 0;
    ImGui::Text("%s", s->city_name[0] ? s->city_name : "(unnamed)");
    ImGui::Text("%s of %d", STAGE_NAME[stage], (int)s->population);
    ImGui::Text("Funds $%d", (int)s->funds);
    ImGui::Separator();
    const char *rci[3] = {"Residential", "Commercial", "Industrial"};
    for (int k = 0; k < 3; ++k)
    {
        char  ov[32];
        float f = ((float)s->demand[k] + 2000.0f) / 4000.0f;
        snprintf(ov, sizeof ov, "%+d", (int)s->demand[k]);
        ImGui::TextUnformatted(rci[k]);
        ImGui::SameLine(110.0f);
        ImGui::ProgressBar(f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f), ImVec2(-FLT_MIN, 0.0f), ov);
    }
    ImGui::Separator();
    ImGui::Text("Power %d%%   Water %d%%", (int)s->power_pct, (int)s->water_pct);
    ImGui::Text("Unemployment %d", (int)s->unemployment);
    ImGui::Text("Land value %d   Crime %d", (int)s->land_value_tot, (int)s->crime_tot);
    ImGui::Text("Traffic %d   Pollution %d", (int)s->traffic_tot, (int)s->pollution_tot);
    ImGui::End();
}

static void budget_window(RUiState *s)
{
    if (!mac_begin("Budget", &s->show_budget, 440.0f, ImVec2(120.0f, 80.0f)))
    {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTable("depts", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Department");
        ImGui::TableSetupColumn("Amount");
        ImGui::TableSetupColumn("Funding");
        ImGui::TableSetupColumn("Accrued");
        ImGui::TableHeadersRow();
        for (int k = 0; k < RUI_N_DEPT; ++k)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (s->dept_name[k])
                ImGui::TextUnformatted(s->dept_name[k]);
            else
                ImGui::Text("(slot %d)", k);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", (int)s->dept_amount[k]);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", (int)s->dept_funding[k]);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", (int)s->dept_accrued[k]);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

static void graphs_window(RUiState *s)
{
    if (!mac_begin("Graphs", &s->show_graphs, 340.0f, ImVec2(140.0f, 60.0f)))
    {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("The last twelve months");
    for (int k = 0; k < RUI_N_GRAPH; ++k)
    {
        float vals[12];
        char  ov[48];
        if (!s->graph[k])
            continue;
        for (int i = 0; i < 12; ++i)
            vals[i] = (float)s->graph[k][11 - i];
        snprintf(ov, sizeof ov, "%d", (int)s->graph[k][0]);
        ImGui::PlotLines(s->graph_name[k] ? s->graph_name[k] : "?", vals, 12, 0, ov, FLT_MAX, FLT_MAX, ImVec2(0.0f, 36.0f));
    }
    ImGui::End();
}

static void query_window(RUiState *s)
{
    if (!mac_begin("Query", &s->show_query, 280.0f, ImVec2(110.0f, 300.0f)))
    {
        ImGui::End();
        return;
    }
    if (!s->q_ok)
        ImGui::TextUnformatted("Point at a tile.");
    else
    {
        int water = s->q_xter >= 0x10;
        ImGui::Text("Tile column %d, row %d", (int)s->q_col, (int)s->q_row);
        if (water)
            ImGui::Text("Water, table at level %d over a bed at %d", (int)s->q_alt, (int)(s->q_altm & 0x1F));
        else
            ImGui::Text("Land at level %d", (int)s->q_alt);
        if (s->q_building)
            ImGui::Text("%s", s->q_building);
        else if (s->q_xbld)
            ImGui::Text("Building %02X", (unsigned)s->q_xbld);
        else
            ImGui::TextUnformatted("Nothing built");
        if (s->q_size > 1)
            ImGui::Text("Footprint %d x %d, north-east tile column %d, row %d",
                        (int)s->q_size,
                        (int)s->q_size,
                        (int)s->q_ocol,
                        (int)s->q_orow);
        ImGui::Text("Zone: %s", ZONE_NAME[s->q_xzon & 0x0F]);
        ImGui::Text("Power %s, water %s", (s->q_xbit & 0x40) ? "supplied" : "none", (s->q_xbit & 0x10) ? "supplied" : "none");
        ImGui::Text("Terrain %02X   XBLD %02X   XZON %02X", (unsigned)s->q_xter, (unsigned)s->q_xbld, (unsigned)s->q_xzon);
        if (s->q_mesh[0])
            ImGui::TextWrapped("%s", s->q_mesh);
    }
    ImGui::End();
}

/*  Road tuning: the geometry's knobs, live.  Every slider rebuilds the
 *  mesh, which takes about a fifth of a second, so a change is seen as it
 *  is made rather than described (the user: "how about you give me knobs
 *  to tweak live?"). */
static void tuning_window(RUiState *s)
{
    /*  The road geometry's knobs, live.  Every slider rebuilds the mesh,
     *  which takes about a fifth of a second, so a change is seen as it
     *  is made rather than described (the user: "how about you give me
     *  knobs to tweak live?").  The window is the game's own, through
     *  mac_begin, so it wears whatever theme is on. */
    static const char *NAME[9] = {
        "road width", "rail width", "road min radius", "rail min radius",
        "road max sweep", "rail max sweep", "node approach",
        "corridor margin", "junction trim"};
    static const float LO[9]  = {0.15f, 0.15f, 0.05f, 0.05f, 0.5f, 0.5f, 0.0f, 0.0f, 0.10f};
    static const float HI[9]  = {1.00f, 1.00f, 4.00f, 8.00f, 12.0f, 16.0f, 2.0f, 0.3f, 0.60f};
    static const float DEF[9] = {0.50f, 0.62f, 0.90f, 3.00f, 6.00f, 8.00f, 0.8f, 0.04f, 0.45f};
    int                k;
    if (!mac_begin("Road tuning", &s->show_tuning, 340.0f, ImVec2(110.0f, 300.0f)))
    {
        ImGui::End();
        return;
    }
    ImGui::PushItemWidth(150.0f);
    for (k = 0; k < 9; ++k)
        if (ImGui::SliderFloat(NAME[k], &s->tune[k], LO[k], HI[k], "%.3f"))
            s->tune_changed = 1;
    ImGui::PopItemWidth();
    ImGui::Separator();
    if (ImGui::Button("Defaults"))
    {
        for (k = 0; k < 9; ++k)
            s->tune[k] = DEF[k];
        s->tune_changed = 1;
    }
    ImGui::SameLine();
    {
        bool cv = s->tune[9] > 0.5f;
        if (ImGui::Checkbox("show curves", &cv))
        {
            s->tune[9]      = cv ? 1.0f : 0.0f;
            s->tune_changed = 1;
        }
    }
    {
        /*  How the line is held inside its corridor.  Off nudges a point
         *  out of a violation; on projects it back into the room the
         *  road's own width allows, which keeps its shape as the width
         *  is dragged instead of falling apart. */
        bool wf = s->tune[10] > 0.5f;
        if (ImGui::Checkbox("wide fit", &wf))
        {
            s->tune[10]     = wf ? 1.0f : 0.0f;
            s->tune_changed = 1;
        }
    }
    {
        /*  The corridor fit as it is solved elsewhere: control points
         *  boxed into the corridor, curvature minimised, no fillets. */
        bool sp = s->tune[11] > 0.5f;
        if (ImGui::Checkbox("spline fit", &sp))
        {
            s->tune[11]     = sp ? 1.0f : 0.0f;
            s->tune_changed = 1;
        }
    }
    ImGui::TextUnformatted("in tiles; each change rebuilds the mesh");
    ImGui::End();
}

static void renderer_window(RUiState *s)
{
    if (!mac_begin("Renderer", &s->show_renderer, 260.0f, ImVec2(110.0f, 420.0f)))
    {
        ImGui::End();
        return;
    }
    ImGui::Text("Driver %s", s->driver ? s->driver : "?");
    ImGui::Text("%.1f fps, %.2f ms", (double)s->fps, (double)s->frame_ms);
    ImGui::Text("%u sprites drawn, %u culled", (unsigned)s->instances, (unsigned)s->culled);
    ImGui::Text("%u mesh vertices", (unsigned)s->mesh_verts);
    ImGui::Separator();
    bool b = s->plain_sweep != 0;
    if (ImGui::Checkbox("Plain sweep (debug: painter's order, no depth)", &b))
        s->plain_sweep = b;
    ImGui::End();
}

static void log_window(RUiState *s)
{
    if (!mac_begin("Messages", &s->show_log, 420.0f, ImVec2(110.0f, ImGui::GetIO().DisplaySize.y - 150.0f)))
    {
        ImGui::End();
        return;
    }
    for (int k = 0; k < s->n_log; ++k)
        ImGui::TextUnformatted(s->log[k]);
    ImGui::End();
}

/*  The load menu.  The app has already filled city_list by scanning
 *  city_dir, so this only has to show it.  A path can be typed for a
 *  city that lives somewhere else. */
static void load_popup(RUi *u, RUiState *s)
{
    (void)u;
    if (s->open_load)
    {
        ImGui::OpenPopup("Load City");
        s->open_load = 0;
    }
    if (!ImGui::BeginPopupModal("Load City", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    if (s->n_cities > 0)
    {
        ImGui::TextUnformatted(s->city_dir);
        ImGui::Separator();
        ImGui::BeginChild("##cities", ImVec2(360.0f, 320.0f), true);
        for (int k = 0; k < s->n_cities; ++k)
        {
            if (ImGui::Selectable(s->city_list[k]))
            {
                snprintf(s->load_path, sizeof s->load_path, "%s/%s", s->city_dir, s->city_list[k]);
                s->want_load = 1;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndChild();
    }
    else
    {
        ImGui::TextUnformatted("No cities directory found.");
        ImGui::TextUnformatted("Set SC2K_CITIES, or type a path below.");
    }

    ImGui::Separator();
    ImGui::SetNextItemWidth(360.0f);
    if (ImGui::InputText("##path", s->load_path, sizeof s->load_path, ImGuiInputTextFlags_EnterReturnsTrue))
    {
        s->want_load = 1;
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button("Open", ImVec2(100.0f, 0.0f)) && s->load_path[0])
    {
        s->want_load = 1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

static void save_popup(RUi *u, RUiState *s)
{
    if (u->save_popup)
    {
        ImGui::OpenPopup("Save City As");
        u->save_popup = false;
    }
    if (ImGui::BeginPopupModal("Save City As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText("Save city as", s->save_path, sizeof s->save_path);
        if (ImGui::Button("Save", ImVec2(100.0f, 0.0f)))
        {
            s->want_save = 1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

/* ---- the interface ----------------------------------------------------- */

extern "C" RUi *ui_create(SDL_Window *win, SDL_GPUDevice *dev, int swap_fmt, float dpi, const char *assets_dir)
{
    RUi *u = new RUi();
    memset(u, 0, sizeof *u);
    u->win = win;
    u->dev = dev;
    u->dpi = dpi;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io    = ImGui::GetIO();
    io.IniFilename = nullptr;
    /*  The system font: Chicago 12, as Chicago-Kare, Duane King's
     *  pixel-for-pixel reproduction of Susan Kare's bitmap (MIT), whose
     *  outlines lie on a sixteen-pixel-per-em grid, so drawn at sixteen
     *  pixels with no hinting and no anti-aliasing every pixel is the
     *  bitmap's.  ChicagoFLF, an outline revival, stands in if it is
     *  absent, and ImGui's own pixel font after that. */
    {
        char         fpath[1024];
        ImFontConfig fc;
        fc.PixelSnapH  = true;
        fc.OversampleH = fc.OversampleV = 1;
        bool loaded                     = false;
        snprintf(fpath, sizeof fpath, "%s/fonts/ChicagoKare-Regular.ttf", assets_dir);
        FILE *probe = fopen(fpath, "rb");
        if (probe)
        {
            fclose(probe);
#ifdef IMGUI_ENABLE_FREETYPE
            fc.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_Monochrome | ImGuiFreeTypeLoaderFlags_NoHinting;
#endif
            loaded = io.Fonts->AddFontFromFileTTF(fpath, 16.0f, &fc) != nullptr;
            /*  Chicago-Kare has no ⌘ and no ⇧: Susan Kare's bitmap never
             *  needed them, the Menu Manager drew them itself.  ChicagoFLF
             *  has both, so it is merged in behind Kare for the glyphs
             *  Kare lacks, and only those.  The ✓ Kare has. */
            if (loaded)
            {
                char fpath2[1024];
                snprintf(fpath2, sizeof fpath2, "%s/fonts/ChicagoFLF.ttf", assets_dir);
                FILE *p2 = fopen(fpath2, "rb");
                if (p2)
                {
                    ImFontConfig mc;
                    fclose(p2);
                    mc.MergeMode   = true;
                    mc.PixelSnapH  = true;
                    mc.OversampleH = mc.OversampleV = 1;
#ifdef IMGUI_ENABLE_FREETYPE
                    mc.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_Monochrome | ImGuiFreeTypeLoaderFlags_MonoHinting;
#endif
                    io.Fonts->AddFontFromFileTTF(fpath2, 16.0f, &mc);
                }
            }
        }
        if (!loaded)
        {
            snprintf(fpath, sizeof fpath, "%s/fonts/ChicagoFLF.ttf", assets_dir);
            probe = fopen(fpath, "rb");
            if (probe)
            {
                fclose(probe);
#ifdef IMGUI_ENABLE_FREETYPE
                fc.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_Monochrome | ImGuiFreeTypeLoaderFlags_MonoHinting;
#endif
                loaded = io.Fonts->AddFontFromFileTTF(fpath, 12.0f, &fc) != nullptr;
            }
        }
        if (!loaded)
            io.Fonts->AddFontDefaultBitmap();
    }
    apply_theme(dpi);

    if (!ImGui_ImplSDL3_InitForSDLGPU(win))
    {
        ImGui::DestroyContext();
        delete u;
        return nullptr;
    }
    ImGui_ImplSDLGPU3_InitInfo ii;
    ii.Device            = dev;
    ii.ColorTargetFormat = (SDL_GPUTextureFormat)swap_fmt;
    ii.MSAASamples       = SDL_GPU_SAMPLECOUNT_1;
    if (!ImGui_ImplSDLGPU3_Init(&ii))
    {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        delete u;
        return nullptr;
    }
    if (!load_atlas(u, assets_dir))
        fprintf(stderr, "ui: no %s/ui.png -- the palette is not drawn "
                        "(python3 tools/pict.py --atlas rsrc/sc2k.rsrc assets)\n",
                assets_dir);
    return u;
}

extern "C" void ui_destroy(RUi *u)
{
    if (!u)
        return;
    SDL_WaitForGPUIdle(u->dev);
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (u->atlas)
        SDL_ReleaseGPUTexture(u->dev, u->atlas);
    if (u->theme.tex)
        SDL_ReleaseGPUTexture(u->dev, u->theme.tex);
    delete u;
}

extern "C" int ui_event(RUi *u, const SDL_Event *e)
{
    if (!u)
        return 0;
    ImGui_ImplSDL3_ProcessEvent(e);
    const ImGuiIO &io = ImGui::GetIO();
    switch (e->type)
    {
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
            return io.WantCaptureMouse ? 1 : 0;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
            /* only a text field being typed in owns the keys; the game's keys
             * keep working after a click on a window */
            return io.WantTextInput ? 1 : 0;
        default:
            return 0;
    }
}

extern "C" int ui_wants_mouse(const RUi *u)
{
    return u && ImGui::GetIO().WantCaptureMouse ? 1 : 0;
}

extern "C" int ui_wants_keyboard(const RUi *u)
{
    return u && ImGui::GetIO().WantTextInput ? 1 : 0;
}

extern "C" void ui_frame(RUi *u, RUiState *s)
{
    if (!u)
        return;
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    g_ui_for_title = u;
    menu_bar(u, s);
    palette(u, s);
    demand(u, s);
    if (s->show_city)
        city_window(s);
    if (s->show_budget)
        budget_window(s);
    if (s->show_graphs)
        graphs_window(s);
    if (s->show_query)
        query_window(s);
    if (s->show_tuning)
        tuning_window(s);
    if (s->show_query && s->q_poly_ok)
    {
        /*  The footprint under the query, outlined on the map: a white
         *  line over a dark one, so it reads on any ground as the
         *  original's inverting outline did. */
        ImVec2 pts[4];
        for (int k = 0; k < 4; ++k)
            pts[k] = ImVec2(s->q_poly[k][0], s->q_poly[k][1]);
        /*  On the background list: the outline belongs to the map, so
         *  every window sits above it (the user: "query box needs to be
         *  under the UI and UI needs to be above everything"). */
        ImDrawList *dl = ImGui::GetBackgroundDrawList();
        dl->AddPolyline(pts, 4, IM_COL32(0, 0, 0, 220), ImDrawFlags_Closed, 3.0f);
        dl->AddPolyline(pts, 4, IM_COL32(255, 255, 255, 255), ImDrawFlags_Closed, 1.0f);
    }
    if (s->show_renderer)
        renderer_window(s);
    if (s->show_log)
        log_window(s);
    load_popup(u, s);
    save_popup(u, s);

    if (s->show_coords && s->coord_n > 0)
    {
        /*  A margin ruler, along the BOTTOM and the RIGHT: the game's own
         *  menu bar owns the top edge and its tool palette the left, and
         *  a ruler drawn there is covered by them -- windows draw over
         *  the background list, so it was there and invisible.
         *
         *  Each labelled column and row arrives as a line across the
         *  city; its number goes where that line meets the edge, with the
         *  line itself faint over the map so a cell can be counted off. */
        /*  Drawn through a window's own list.  Neither the background nor
         *  the foreground list reaches the output in this build -- the
         *  calls issue and nothing appears -- so the ruler rides in a
         *  borderless, transparent, click-through window that covers the
         *  view, which is the same path the menu bar and palette take. */
        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        /*  Sized to be read, not just present: the numbers are the point
         *  of the ruler and the default font at this distance is too
         *  small to pick off a screenshot (the user: "Resolution is too
         *  low"). */
        const float  band = 34.0f;
        const float  tsz  = 22.0f;
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(disp);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
        ImGui::Begin("##ruler", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const float  ey   = disp.y - band; /* columns are numbered here */
        const float  ex   = disp.x - band; /* and rows here            */
        dl->AddRectFilled(ImVec2(0, ey), ImVec2(disp.x, disp.y), IM_COL32(18, 22, 24, 190));
        dl->AddRectFilled(ImVec2(ex, 0), ImVec2(disp.x, disp.y), IM_COL32(18, 22, 24, 190));
        for (int k = 0; k < s->coord_n; ++k)
        {
            ImVec2 p0(s->coord_a[k][0], s->coord_a[k][1]);
            ImVec2 p1(s->coord_b[k][0], s->coord_b[k][1]);
            /*  Which edge a line is numbered on follows how it lies ON
             *  SCREEN, not which world axis it is: the map view turns the
             *  city forty-five degrees, so a column can run across the
             *  screen and never meet the bottom edge at all -- every label
             *  was being culled for that. */
            const bool  col  = fabsf(p1.y - p0.y) >= fabsf(p1.x - p0.x);
            const float want = col ? ey : ex;
            float       den  = col ? (p1.y - p0.y) : (p1.x - p0.x);
            float       t    = 0.0f;
            char        txt[8];
            if (fabsf(den) > 1e-3f)
            {
                t = ((col ? want - p0.y : want - p0.x)) / den;
                t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
            }
            ImVec2 at(p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * t);
            snprintf(txt, sizeof txt, "%c%d", s->coord_axis[k], (int)s->coord_v[k]);
            dl->AddLine(p0, p1, IM_COL32(255, 240, 120, 45), 1.0f);
            if (col)
            {
                if (at.x < 2.0f || at.x > ex - 22.0f)
                    continue;
                dl->AddLine(ImVec2(at.x, ey - 8.0f), ImVec2(at.x, ey + 5.0f),
                            IM_COL32(255, 240, 120, 230), 2.0f);
                dl->AddText(ImGui::GetFont(), tsz, ImVec2(at.x + 3.0f, ey + 5.0f),
                            IM_COL32(255, 240, 120, 255), txt);
            }
            else
            {
                if (at.y < 2.0f || at.y > ey - 22.0f)
                    continue;
                dl->AddLine(ImVec2(ex - 8.0f, at.y), ImVec2(ex + 5.0f, at.y),
                            IM_COL32(255, 240, 120, 230), 2.0f);
                dl->AddText(ImGui::GetFont(), tsz, ImVec2(ex + 6.0f, at.y - tsz * 0.5f),
                            IM_COL32(255, 240, 120, 255), txt);
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
    ImGui::Render();
    u->frame_open = true;
}

extern "C" void ui_render(void *ud, SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *target, uint32_t w, uint32_t h)
{
    RUi *u = (RUi *)ud;
    (void)w;
    (void)h;
    if (!u || !u->frame_open)
        return;
    ImDrawData *dd = ImGui::GetDrawData();
    if (!dd)
        return;
    ImGui_ImplSDLGPU3_PrepareDrawData(dd, cmd);
    SDL_GPUColorTargetInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.texture            = target;
    ci.load_op            = SDL_GPU_LOADOP_LOAD;
    ci.store_op           = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &ci, 1, nullptr);
    if (rp)
    {
        ImGui_ImplSDLGPU3_RenderDrawData(dd, cmd, rp);
        SDL_EndGPURenderPass(rp);
    }
    u->frame_open = false;
}

extern "C" void ui_log(RUiState *s, const char *fmt, ...)
{
    va_list ap;
    if (s->n_log == RUI_LOG_LINES)
    {
        memmove(s->log[0], s->log[1], sizeof s->log - sizeof s->log[0]);
        s->n_log--;
    }
    va_start(ap, fmt);
    vsnprintf(s->log[s->n_log++], RUI_LOG_LEN, fmt, ap);
    va_end(ap);
}
