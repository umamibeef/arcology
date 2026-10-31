/*  ui.cpp: the canon interface, hosted by Dear ImGui, and what its parts
 *  share.  This file holds the art, the System 7 chrome, the menus' look,
 *  the themed controls and the frame.  The palette is in palette.cpp, the
 *  menu bar in menubar.cpp, the windows in windows.cpp and the inspector
 *  in inspect.cpp.  ImGui is the engine under it: layout, hit-testing,
 *  text and the draw lists.  The widgets see an RUiState and nothing
 *  else.  The app fills it, they edit it, and the app applies it.  The
 *  System 7 look is apply_theme's, and ui_set_theme lays a Kaleidoscope
 *  scheme over it.  The text is in ChicagoKare, the game's system font
 *  redrawn, because the game's own fonts are not in its resources. */
#include "ui.h"
#include "log.h"
/* lodepng is compiled as C in the vendor library.  Only this call is used */
extern "C" unsigned lodepng_decode32_file(unsigned char **out, unsigned *w, unsigned *h, const char *filename);

#include <SDL3/SDL.h>

#include <cfloat>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imgui_internal.h" /* the Selectable flags MenuItem itself is built on */
#include "internal.hpp"
#ifdef IMGUI_ENABLE_FREETYPE
    #include "imgui_freetype.h"
#endif

/* ---- the atlas -------------------------------------------------------- */

const Pict *find_pict(const RUi *u, int id)
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

const ThemeEl *theme_el(const RUi *u, const char *name)
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

ImU32 theme_col(const int rgb[3], ImU32 fallback)
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
static void theme_nine_at(const RUi *u, ImDrawList *dl, const ThemeEl *e, ImVec2 a, ImVec2 b, int c);

void theme_nine(const RUi *u, ImDrawList *dl, const ThemeEl *e, ImVec2 a, ImVec2 b)
{
    theme_nine_at(u, dl, e, a, b, e->corner);
}

/*  The same, with the fixed edge `c` pixels thick.  A frame the scheme
 *  stretches by its side thickness rather than its corner is drawn with
 *  this. */
static void theme_nine_at(const RUi *u, ImDrawList *dl, const ThemeEl *e, ImVec2 a, ImVec2 b, int c)
{
    ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
    if (pio.DrawCallback_SetSamplerNearest)
        dl->AddCallback(pio.DrawCallback_SetSamplerNearest, nullptr);
    /*  The icon's center carries the scheme's color samples.  The pixel
     *  the info table names for the background, the one for the text.
     *  So the center is never stretched: it is filled with the
     *  background color.  Each edge is the one-pixel strip next to its
     *  corners, stretched along the edge. */
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
        if (strncmp(line, "element ", 8) == 0 && t.n_els < 96)
        {
            ThemeEl &e = t.els[t.n_els];
            if (sscanf(line + 8, "%31s %d %d %d %d %d %d %d %d %d %d %d %d", e.name, &e.x, &e.y, &e.w, &e.h, &e.corner, &e.side, &e.bg[0], &e.bg[1], &e.bg[2], &e.text[0], &e.text[1], &e.text[2]) == 13)
                t.n_els++;
        }
        else if (strncmp(line, "part ", 5) == 0 && t.n_parts < 64)
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
    /*  The menu's shadow is a ramp of grays that darkens what lies under
     *  it: white leaves it, black blacks it out.  Black at the alpha of
     *  one less the gray does the same under the usual blend. */
    for (int k = 0; k < t.n_els; ++k)
        if (strcmp(t.els[k].name, "menu_shadow") == 0)
        {
            const ThemeEl &e = t.els[k];
            for (int y = e.y; y < e.y + e.h && y < (int)h; ++y)
                for (int x = e.x; x < e.x + e.w && x < (int)w; ++x)
                {
                    unsigned char *q = rgba + ((size_t)y * w + (size_t)x) * 4u;
                    if (q[3] == 0)
                        continue;
                    const int gray = (q[0] + q[1] + q[2]) / 3;
                    q[0] = q[1] = q[2] = 0;
                    q[3]               = (unsigned char)(255 - gray);
                }
        }
    if (t.tex)
        SDL_ReleaseGPUTexture(u->dev, t.tex);
    t.tex = upload_rgba(u->dev, rgba, w, h);
    free(rgba);
    if (!t.tex)
        return -1;
    t.tw     = (int)w;
    t.th     = (int)h;
    t.loaded = true;
    /* the colors the scheme names go into the style */
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
    /*  What the scheme carries no art for takes its colors.  A table's
     *  header is a placard's, and its rows alternate the list's two.  Its
     *  lines and the separators are the frame's gray.  The scroll bars
     *  are the scheme's own, drawn over ImGui's (themed_scrollbars),
     *  whose own are left with no color. */
    {
        const ThemeEl *pl = theme_el(u, "placard"), *li = theme_el(u, "list_color");
        ImVec4         clear(0, 0, 0, 0), gray(0.4f, 0.4f, 0.4f, 1.0f), black(0, 0, 0, 1), white(1, 1, 1, 1);
        if (pl && pl->bg[0] >= 0)
            st.Colors[ImGuiCol_TableHeaderBg] = ImGui::ColorConvertU32ToFloat4(theme_col(pl->bg, WHITE));
        st.Colors[ImGuiCol_TableRowBg]        = white;
        st.Colors[ImGuiCol_TableRowBgAlt]     = li && li->bg[0] >= 0 ? ImGui::ColorConvertU32ToFloat4(theme_col(li->bg, WHITE)) : white;
        st.Colors[ImGuiCol_TableBorderStrong] = black;
        st.Colors[ImGuiCol_TableBorderLight]  = gray;
        st.Colors[ImGuiCol_Separator]         = gray;
        st.Colors[ImGuiCol_WindowBg]          = white;
        st.Colors[ImGuiCol_ChildBg]           = white;
        st.Colors[ImGuiCol_FrameBg]           = white;
        st.Colors[ImGuiCol_ScrollbarBg] = st.Colors[ImGuiCol_ScrollbarGrab] = clear;
        st.Colors[ImGuiCol_ScrollbarGrabHovered] = st.Colors[ImGuiCol_ScrollbarGrabActive] = clear;
        const ThemeEl *vs                                                                  = theme_el(u, "vscroll");
        if (vs)
            st.ScrollbarSize = (float)vs->w;
    }
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
    apply_theme(u->dpi); /* the style colors the scheme set, back to the hand-drawn look's */
}

/*  Draw a picture, or a part of it, at a screen position, one texel per
 *  point, sampled nearest so the pixels stay pixels. */
void draw_pict(const RUi *u, ImDrawList *dl, int id, ImVec2 at, int sx, int sy, int sw, int sh)
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
 *  black text.  Menus are white with black text.  A menu drops down as a
 *  white box with a shadow. */
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

/*  A WINDOW'S FRAME as the scheme draws it, round the rectangle `a` to
 *  `b`.  Each kind of window is an icon whose part 0 is where the content
 *  goes: everything outside it is the frame.
 *
 *      The top band: the icon's left end, its title tile (part 4)
 *      repeated across, and its right end anchored right.
 *      The sides: the column of the frame either side of the content,
 *      stretched down.
 *      The bottom: its corners, and the column under the content
 *      stretched across.
 *      The title, centered on the band over the scheme's bar color.
 *
 *  A kind with no title tile is a frame alone.  The close box is part 1
 *  where the kind has one.  Answers whether it was clicked, and sets
 *  `*drawn` where there is a scheme to draw from.  The cursor is left
 *  inside the frame, under the band. */
static const char *frame_icon(const char *window)
{
    return strcmp(window, "utility") == 0          ? "utility_active"
           : strcmp(window, "movable_dialog") == 0 ? "movable_modal_active"
           : strcmp(window, "dialog") == 0         ? "modal_active"
                                                   : "window_active";
}

bool theme_frame(RUi *u, const char *window, const char *title, bool *drawn, float *top)
{
    *drawn                 = false;
    const ThemeEl   *fr    = theme_el(u, frame_icon(window));
    const ThemePart *body  = theme_part(u, window, 0);
    const ThemePart *tile  = theme_part(u, window, 4);
    const ThemePart *close = theme_part(u, window, 1);
    if (!fr || !body)
        return false;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2      p  = ImGui::GetWindowPos();
    ImVec2      s  = ImGui::GetWindowSize();
    const int   cx = body->x, cy = body->y, R = fr->w - body->x - body->w, B = fr->h - body->y - body->h;
    const float x0 = p.x, y0 = p.y, x1 = p.x + s.x, y1 = p.y + s.y;
    if (s.x < (float)(cx + R + 2) || s.y < (float)(cy + B + 2))
        return false;
    ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
    if (pio.DrawCallback_SetSamplerNearest)
        dl->AddCallback(pio.DrawCallback_SetSamplerNearest, nullptr);
    /* the top band */
    if (tile)
    {
        const int lw = tile->x, rw = fr->w - tile->x - tile->w;
        theme_blit(u, dl, fr->x, fr->y, lw, cy, ImVec2(x0, y0), ImVec2(x0 + (float)lw, y0 + (float)cy));
        for (float x = x0 + (float)lw; x < x1 - (float)rw; x += (float)tile->w)
        {
            float w = x + (float)tile->w > x1 - (float)rw ? x1 - (float)rw - x : (float)tile->w;
            theme_blit(u, dl, fr->x + tile->x, fr->y, (int)w, cy, ImVec2(x, y0), ImVec2(x + w, y0 + (float)cy));
        }
        theme_blit(u, dl, fr->x + fr->w - rw, fr->y, rw, cy, ImVec2(x1 - (float)rw, y0), ImVec2(x1, y0 + (float)cy));
    }
    else
    {
        theme_blit(u, dl, fr->x, fr->y, cx, cy, ImVec2(x0, y0), ImVec2(x0 + (float)cx, y0 + (float)cy));
        theme_blit(u, dl, fr->x + cx, fr->y, 1, cy, ImVec2(x0 + (float)cx, y0), ImVec2(x1 - (float)R, y0 + (float)cy));
        theme_blit(u, dl, fr->x + fr->w - R, fr->y, R, cy, ImVec2(x1 - (float)R, y0), ImVec2(x1, y0 + (float)cy));
    }
    /* the sides */
    theme_blit(u, dl, fr->x, fr->y + cy, cx, 1, ImVec2(x0, y0 + (float)cy), ImVec2(x0 + (float)cx, y1 - (float)B));
    theme_blit(u, dl, fr->x + fr->w - R, fr->y + cy, R, 1, ImVec2(x1 - (float)R, y0 + (float)cy), ImVec2(x1, y1 - (float)B));
    /* the bottom */
    theme_blit(u, dl, fr->x, fr->y + fr->h - B, cx, B, ImVec2(x0, y1 - (float)B), ImVec2(x0 + (float)cx, y1));
    theme_blit(u, dl, fr->x + cx, fr->y + fr->h - B, 1, B, ImVec2(x0 + (float)cx, y1 - (float)B), ImVec2(x1 - (float)R, y1));
    theme_blit(u, dl, fr->x + fr->w - R, fr->y + fr->h - B, R, B, ImVec2(x1 - (float)R, y1 - (float)B), ImVec2(x1, y1));
    if (pio.DrawCallback_SetSamplerLinear)
        dl->AddCallback(pio.DrawCallback_SetSamplerLinear, nullptr);
    if (tile && title && title[0])
    {
        ImVec2         ts  = ImGui::CalcTextSize(title);
        float          tx  = x0 + (s.x - ts.x) * 0.5f;
        float          ty  = y0 + (float)tile->y + ((float)tile->h - ts.y) * 0.5f;
        const ThemeEl *bar = theme_el(u, "menu_bar");
        dl->AddRectFilled(ImVec2(tx - 6.0f, y0 + (float)tile->y), ImVec2(tx + ts.x + 6.0f, y0 + (float)(tile->y + tile->h)), bar ? theme_col(bar->bg, WHITE) : WHITE);
        dl->AddText(ImVec2(tx, ty), BLACK, title);
    }
    *drawn = true;
    if (top)
        *top = (float)cy;
    bool closed = false;
    if (close && strcmp(window, "movable_dialog") != 0)
    {
        ImVec2 c0(x0 + (float)close->x, y0 + (float)close->y);
        ImGui::SetCursorScreenPos(c0);
        closed = ImGui::InvisibleButton("##close", ImVec2((float)close->w, (float)close->h));
        if (ImGui::IsItemActive())
            dl->AddRectFilled(c0, ImVec2(c0.x + (float)close->w, c0.y + (float)close->h), IM_COL32(0, 0, 0, 110));
    }
    ImGui::SetCursorScreenPos(ImVec2(x0 + ImGui::GetCurrentWindow()->WindowPadding.x, y0 + (float)cy + 6.0f));
    return closed;
}

/*  The title band alone, for the kinds that are drawn as windows. */
bool theme_title(RUi *u, const char *window, const char *title, bool *drawn)
{
    return theme_frame(u, window, title, drawn, nullptr);
}

RUi *g_ui_for_title = nullptr; /* the interface whose theme mac_title draws */

/*  A System 7 title bar over the top of the current window.
 *
 *      The striped bar.
 *      The title in a white box.
 *      The close box at the left.
 *
 *  Returns true if the close box was clicked.  Windows are begun without
 *  ImGui's own title bar and reserve TITLE_H at the top for this. */
bool mac_title(const char *title)
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
    ImGui::SetCursorScreenPos(ImVec2(x0 + ImGui::GetCurrentWindow()->WindowPadding.x, y0 + TITLE_H + 6.0f));
    return closed;
}

/*  The narrowest a frame can be and still show its title between the
 *  boxes at its two ends.  That is the title, and on each side the
 *  wider end and a margin. */
static float frame_min_width(RUi *u, const char *window, const char *title)
{
    const ThemeEl   *fr   = u ? theme_el(u, frame_icon(window)) : nullptr;
    const ThemePart *tile = u ? theme_part(u, window, 4) : nullptr;
    float            ends = 24.0f;
    if (fr && tile)
    {
        const int rw = fr->w - tile->x - tile->w;
        ends         = (float)(tile->x > rw ? tile->x : rw);
    }
    return ImGui::CalcTextSize(title).x + 2.0f * (ends + 8.0f);
}

/*  The padding inside a frame: the margin, beyond the frame's own
 *  border on each side. */
ImVec2 frame_pad(RUi *u, const char *window, ImVec2 margin)
{
    const ThemeEl   *fr   = u ? theme_el(u, frame_icon(window)) : nullptr;
    const ThemePart *body = u ? theme_part(u, window, 0) : nullptr;
    if (!fr || !body)
        return margin;
    const int side = body->x > fr->w - body->x - body->w ? body->x : fr->w - body->x - body->w;
    return ImVec2(margin.x + (float)side, margin.y + (float)(fr->h - body->y - body->h));
}

/*  `most` is the largest the window may grow, and a window held at one
 *  width names it there as well. */
bool mac_begin(const char *title, int *show, float w, ImVec2 first_pos, ImGuiWindowFlags extra, ImVec2 most)
{
    bool  open  = *show != 0;
    float least = frame_min_width(g_ui_for_title, "document", title);
    ImGui::SetNextWindowSizeConstraints(ImVec2(most.x < least ? most.x : (most.x < FLT_MAX ? most.x : least), 0.0f), most);
    ImGui::SetNextWindowSize(ImVec2(w, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(first_pos, ImGuiCond_FirstUseEver);
    char id[128];
    snprintf(id, sizeof id, "##%s", title);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, frame_pad(g_ui_for_title, "document", ImGui::GetStyle().WindowPadding));
    bool vis = ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | extra);
    ImGui::PopStyleVar();
    if (mac_title(title))
        open = false;
    *show = open ? 1 : 0;
    return vis;
}

/* ---- the menus' look ---------------------------------------------------- */

/*  The menus as the scheme draws them.  A menu window is begun with no
 *  background and no border of ImGui's own.  Over it go the scheme's
 *  shadow, its border and its background, and then the items.  An item
 *  under the pointer, and a title whose menu is open, take the scheme's
 *  selected element and its text color.  With no scheme loaded, ImGui's
 *  own look stands. */
const RUi *s_look; /* the interface the menus are drawn for, set each frame */

const ThemeEl *look_el(const char *name)
{
    return s_look ? theme_el(s_look, name) : nullptr;
}

/*  Before a menu window begins: no background and no border of ImGui's,
 *  and no highlight of its own on the items.  Returns how many colors
 *  were pushed.  Pop them, and one style variable when any were, once
 *  the window has begun. */
int menu_look_push(void)
{
    if (!look_el("menu_border") || !look_el("menu_background"))
        return 0;
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    return 4;
}

void menu_look_pop(int n)
{
    if (!n)
        return;
    ImGui::PopStyleColor(n);
    ImGui::PopStyleVar();
}

/*  The current window, a menu, framed as the scheme frames one.  The
 *  border and the shadow share one picture size.  Their top two rows
 *  and left three columns are clear, and the shadow takes the two
 *  columns and rows past the right and bottom edges.  The border is 4
 *  pixels thick on every side, which is the scheme's side thickness.
 *  The background fills the border's inside. */
void menu_frame(void)
{
    const ThemeEl *bd = look_el("menu_border"), *sh = look_el("menu_shadow"), *bg = look_el("menu_background");
    if (!bd || !bg)
        return;
    const ImVec2 p = ImGui::GetWindowPos(), z = ImGui::GetWindowSize();
    ImDrawList  *dl = ImGui::GetWindowDrawList();
    const ImVec2 a(p.x - 2.0f, p.y - 3.0f), b(p.x + z.x + 2.0f, p.y + z.y + 2.0f);
    dl->PushClipRectFullScreen();
    if (sh)
        theme_nine_at(s_look, dl, sh, a, b, sh->side);
    theme_nine_at(s_look, dl, bd, a, b, bd->side);
    theme_nine(s_look, dl, bg, ImVec2(p.x + 2.0f, p.y + 1.0f), ImVec2(p.x + z.x - 2.0f, p.y + z.y - 2.0f));
    dl->PopClipRect();
}

/*  The selected element over a row of the current menu, from its top
 *  `y0` to its bottom `y1`, across the inside of the border. */
void menu_row_lit(float y0, float y1)
{
    const ThemeEl *sel = look_el("menu_item_selected");
    if (!sel)
        return;
    const ImVec2 p = ImGui::GetWindowPos(), z = ImGui::GetWindowSize();
    ImDrawList  *dl = ImGui::GetWindowDrawList();
    dl->PushClipRectFullScreen();
    theme_nine(s_look, dl, sel, ImVec2(p.x + 3.0f, y0), ImVec2(p.x + z.x - 3.0f, y1));
    dl->PopClipRect();
}

/*  A line between groups of items: the scheme's divider across the
 *  menu, else ImGui's separator. */
void menu_divider(void)
{
    const ThemeEl *dv = look_el("menu_divider");
    if (!dv)
    {
        ImGui::Separator();
        return;
    }
    const ImVec2 p = ImGui::GetWindowPos(), z = ImGui::GetWindowSize(), at = ImGui::GetCursorScreenPos();
    /*  A strip at its own height: the end columns, as many as the
     *  corner, fixed, and the column next to them stretched between.
     *  Its ends carry the background's outline and bevel, so it spans
     *  the background (menu_frame) and they lie over them. */
    ImDrawList      *dl  = ImGui::GetWindowDrawList();
    ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
    const float      c = (float)dv->corner, x0 = p.x + 2.0f, x1 = p.x + z.x - 2.0f, y1 = at.y + (float)dv->h;
    if (pio.DrawCallback_SetSamplerNearest)
        dl->AddCallback(pio.DrawCallback_SetSamplerNearest, nullptr);
    theme_blit(s_look, dl, dv->x, dv->y, dv->corner, dv->h, ImVec2(x0, at.y), ImVec2(x0 + c, y1));
    theme_blit(s_look, dl, dv->x + dv->corner, dv->y, 1, dv->h, ImVec2(x0 + c, at.y), ImVec2(x1 - c, y1));
    theme_blit(s_look, dl, dv->x + dv->w - dv->corner, dv->y, dv->corner, dv->h, ImVec2(x1 - c, at.y), ImVec2(x1, y1));
    if (pio.DrawCallback_SetSamplerLinear)
        dl->AddCallback(pio.DrawCallback_SetSamplerLinear, nullptr);
    ImGui::Dummy(ImVec2(1.0f, (float)dv->h - ImGui::GetStyle().ItemSpacing.y));
}

/*  A menu inside a menu, framed as the menu it hangs from.  Its row is
 *  lit while it is open. */
bool begin_submenu(const char *label)
{
    const bool     open = ImGui::IsPopupOpen(label);
    const ImVec2   at   = ImGui::GetCursorScreenPos();
    const float    h    = ImGui::GetTextLineHeight();
    const float    sp   = ImGui::GetStyle().ItemSpacing.y;
    const ThemeEl *sel  = look_el("menu_item_selected");
    if (open && sel)
        menu_row_lit(at.y - sp * 0.5f, at.y + h + sp * 0.5f);
    const int n = menu_look_push();
    if (open && sel && n)
        ImGui::PushStyleColor(ImGuiCol_Text, theme_col(sel->text, WHITE));
    const bool r = ImGui::BeginMenu(label);
    if (open && sel && n)
        ImGui::PopStyleColor();
    menu_look_pop(n);
    if (r)
        menu_frame();
    return r;
}

/* ---- the controls -------------------------------------------------------- */

/*  The scheme draws its windows, menus, scroll bars, sliders, progress
 *  bars and placards.  It carries no push button, check box or text
 *  field.  Those are the system's own, and this file draws them as
 *  System 7 does, in black on the scheme's colors. */

/*  A piece of an element drawn at its own size, nearest. */
static void el_blit(ImDrawList *dl, const ThemeEl *e, int sx, int sy, int sw, int sh, ImVec2 at)
{
    ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
    if (pio.DrawCallback_SetSamplerNearest)
        dl->AddCallback(pio.DrawCallback_SetSamplerNearest, nullptr);
    theme_blit(s_look, dl, e->x + sx, e->y + sy, sw, sh, at, ImVec2(at.x + (float)sw, at.y + (float)sh));
    if (pio.DrawCallback_SetSamplerLinear)
        dl->AddCallback(pio.DrawCallback_SetSamplerLinear, nullptr);
}

/*  An element stretched along one axis.  Its two ends `c` long keep
 *  their own size, and the column (or row) after the first end is
 *  stretched between them.  The other axis is the element's own. */
static void el_three(ImDrawList *dl, const ThemeEl *e, ImVec2 a, float len, bool across, int c)
{
    ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
    if (pio.DrawCallback_SetSamplerNearest)
        dl->AddCallback(pio.DrawCallback_SetSamplerNearest, nullptr);
    const float fc = (float)c;
    if (across)
    {
        theme_blit(s_look, dl, e->x, e->y, c, e->h, a, ImVec2(a.x + fc, a.y + (float)e->h));
        theme_blit(s_look, dl, e->x + c, e->y, 1, e->h, ImVec2(a.x + fc, a.y), ImVec2(a.x + len - fc, a.y + (float)e->h));
        theme_blit(s_look, dl, e->x + e->w - c, e->y, c, e->h, ImVec2(a.x + len - fc, a.y), ImVec2(a.x + len, a.y + (float)e->h));
    }
    else
    {
        theme_blit(s_look, dl, e->x, e->y, e->w, c, a, ImVec2(a.x + (float)e->w, a.y + fc));
        theme_blit(s_look, dl, e->x, e->y + c, e->w, 1, ImVec2(a.x, a.y + fc), ImVec2(a.x + (float)e->w, a.y + len - fc));
        theme_blit(s_look, dl, e->x, e->y + e->h - c, e->w, c, ImVec2(a.x, a.y + len - fc), ImVec2(a.x + (float)e->w, a.y + len));
    }
    if (pio.DrawCallback_SetSamplerLinear)
        dl->AddCallback(pio.DrawCallback_SetSamplerLinear, nullptr);
}

/*  A PUSH BUTTON: a round-cornered box with a black line round it, the
 *  label in the middle, and drawn inverted while it is held.  The
 *  default button of a dialog carries the heavy ring round it. */
bool ui_button(const char *label, ImVec2 size, bool def)
{
    const ImVec2 ts = ImGui::CalcTextSize(label, nullptr, true);
    if (size.x <= 0.0f)
        size.x = ts.x + 24.0f;
    if (size.y <= 0.0f)
        size.y = ts.y + 8.0f;
    /*  The default button's ring lies round it, inside the item.  Every
     *  button keeps the room for one, so a row of them lines up. */
    const float ring = 4.0f;
    ImVec2      o    = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    const bool pressed = ImGui::InvisibleButton("##button", ImVec2(size.x + 2.0f * ring, size.y + 2.0f * ring));
    const bool held    = ImGui::IsItemActive() && ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2      at(o.x + ring, o.y + ring), b(at.x + size.x, at.y + size.y);
    dl->AddRectFilled(at, b, held ? BLACK : WHITE, 4.0f);
    dl->AddRect(at, b, BLACK, 4.0f);
    if (def)
        dl->AddRect(ImVec2(o.x + 1.0f, o.y + 1.0f), ImVec2(b.x + ring - 1.0f, b.y + ring - 1.0f), BLACK, 7.0f, 0, 3.0f);
    const char *end = ImGui::FindRenderedTextEnd(label);
    dl->AddText(ImVec2(at.x + (size.x - ts.x) * 0.5f, at.y + (size.y - ts.y) * 0.5f), held ? WHITE : BLACK, label, end);
    return pressed;
}

/*  A CHECK BOX: a square with a black line round it and a cross in it
 *  when it is on, the label beside it. */
bool ui_check(const char *label, bool *v)
{
    const float  side = 12.0f;
    const ImVec2 at   = ImGui::GetCursorScreenPos();
    const ImVec2 ts   = ImGui::CalcTextSize(label, nullptr, true);
    ImGui::PushID(label);
    const bool pressed = ImGui::InvisibleButton("##check", ImVec2(side + 6.0f + ts.x, side > ts.y ? side : ts.y));
    const bool held    = ImGui::IsItemActive() && ImGui::IsItemHovered();
    ImGui::PopID();
    if (pressed)
        *v = !*v;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float y  = at.y + ((ts.y > side ? ts.y : side) - side) * 0.5f;
    ImVec2      a(at.x, y), b(at.x + side, y + side);
    dl->AddRectFilled(a, b, WHITE);
    dl->AddRect(a, b, BLACK, 0.0f, 0, held ? 2.0f : 1.0f);
    if (*v)
    {
        dl->AddLine(ImVec2(a.x + 1.0f, a.y + 1.0f), ImVec2(b.x - 1.0f, b.y - 1.0f), BLACK);
        dl->AddLine(ImVec2(b.x - 2.0f, a.y + 1.0f), ImVec2(a.x, b.y - 1.0f), BLACK);
    }
    dl->AddText(ImVec2(b.x + 6.0f, at.y), ImGui::GetColorU32(ImGuiCol_Text), label, ImGui::FindRenderedTextEnd(label));
    return pressed;
}

/*  A SLIDER: the scheme's track, its thumb at the value, and the value
 *  and the label beside it.  Dragged anywhere on the track. */
bool ui_slider(const char *label, float *v, float lo, float hi, const char *fmt, float w)
{
    const ThemeEl *tr = look_el("slider_track"), *th = look_el("slider_thumb");
    const ImVec2   at = ImGui::GetCursorScreenPos();
    const float    h = tr ? (float)tr->h : 16.0f, tw = th ? (float)th->w : 11.0f;
    ImGui::PushID(label);
    ImGui::InvisibleButton("##slider", ImVec2(w, h));
    const bool active = ImGui::IsItemActive();
    bool       moved  = false;
    ImGui::PopID();
    if (active && hi > lo)
    {
        float t  = (ImGui::GetIO().MousePos.x - at.x - tw * 0.5f) / (w - tw);
        t        = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f
                                              : t;
        float nv = lo + (hi - lo) * t;
        if (nv != *v)
            *v = nv, moved = true;
    }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (tr)
        el_three(dl, tr, at, w, true, tr->corner > 0 ? tr->corner : 5);
    else
        dl->AddRect(ImVec2(at.x, at.y + h * 0.5f - 2.0f), ImVec2(at.x + w, at.y + h * 0.5f + 2.0f), BLACK);
    {
        float t = hi > lo ? (*v - lo) / (hi - lo) : 0.0f;
        t       = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f
                                             : t;
        ImVec2 p(at.x + t * (w - tw), at.y);
        if (th)
            el_blit(dl, th, 0, active ? 32 : 16, th->w, 16, p);
        else
            dl->AddRectFilled(p, ImVec2(p.x + tw, p.y + h), BLACK);
    }
    char val[48];
    snprintf(val, sizeof val, fmt, (double)*v);
    ImGui::SameLine();
    ImGui::Text("%s  %s", val, label);
    return moved;
}

/*  A PROGRESS BAR: the scheme's track, filled to the fraction with its
 *  fill, and the words over it. */
void ui_progress(float f, float w, const char *over)
{
    const ThemeEl *tr = look_el("progress_track"), *fl = look_el("progress_fill");
    if (w < 0.0f)
        w = ImGui::GetContentRegionAvail().x;
    const float  h  = tr ? (float)tr->h : 14.0f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    f              = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f
                                                : f;
    if (tr && fl)
    {
        el_three(dl, tr, at, w, true, 2);
        if (f * w >= 4.0f)
            el_three(dl, fl, at, f * w, true, 2);
    }
    else
    {
        dl->AddRect(at, ImVec2(at.x + w, at.y + h), BLACK);
        dl->AddRectFilled(at, ImVec2(at.x + f * w, at.y + h), BLACK);
    }
    if (over)
    {
        ImVec2 ts = ImGui::CalcTextSize(over);
        ImVec2 tp(at.x + (w - ts.x) * 0.5f, at.y + (h - ts.y) * 0.5f);
        dl->AddText(ImVec2(tp.x + 1.0f, tp.y), WHITE, over);
        dl->AddText(tp, BLACK, over);
    }
}

/*  A ROW of a list: the label, lit as the menus light a row while the
 *  pointer is on it.  Answers whether it was clicked. */
bool ui_row(const char *label)
{
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float  w = ImGui::GetContentRegionAvail().x, h = ImGui::GetTextLineHeight() + 2.0f;
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("##row", ImVec2(w, h));
    const bool lit     = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList    *dl  = ImGui::GetWindowDrawList();
    const ThemeEl *sel = look_el("menu_item_selected");
    if (lit)
        dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), sel ? theme_col(sel->bg, BLACK) : BLACK);
    dl->AddText(ImVec2(at.x + 4.0f, at.y + 1.0f), lit ? (sel ? theme_col(sel->text, WHITE) : WHITE) : BLACK, label);
    return clicked;
}

/*  THE SCROLL BARS of every window, drawn over ImGui's own once the
 *  windows are laid out.  Each is the scheme's track, with its thumb
 *  where the window is scrolled to.  ImGui's own bar still takes the pointer and
 *  is drawn in no color at all. */
void themed_scrollbars(void)
{
    const ThemeEl *vt = look_el("vscroll"), *ht = look_el("hscroll");
    const ThemeEl *vh = look_el("vthumb"), *hh = look_el("hthumb");
    ImGuiContext  &g = *GImGui;
    if (!vt || !ht || !vh || !hh)
        return;
    for (ImGuiWindow *w : g.Windows)
    {
        if (!w->Active || w->Hidden)
            continue;
        for (int axis = 0; axis < 2; ++axis)
        {
            const bool on = axis ? w->ScrollbarX : w->ScrollbarY;
            if (!on)
                continue;
            const ImRect   r  = ImGui::GetWindowScrollbarRect(w, axis ? ImGuiAxis_X : ImGuiAxis_Y);
            const ThemeEl *tk = axis ? ht : vt, *tb = axis ? hh : vh;
            const float    len = axis ? r.GetWidth() : r.GetHeight();
            ImDrawList    *dl  = w->DrawList;
            /*  The track: the scheme's pattern tiled along the bar, and the
             *  bar's own frame round it.  A scheme with no pattern shows
             *  the bar's empty face. */
            {
                const ThemeEl   *pat = look_el(axis ? "hscroll_pattern" : "vscroll_pattern");
                const ThemeEl   *emp = look_el(axis ? "hscroll_empty" : "vscroll_empty");
                ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
                if (pio.DrawCallback_SetSamplerNearest)
                    dl->AddCallback(pio.DrawCallback_SetSamplerNearest, nullptr);
                if (pat)
                {
                    dl->PushClipRect(r.Min, r.Max, true);
                    for (float y = r.Min.y; y < r.Max.y; y += (float)pat->h)
                        for (float x = r.Min.x; x < r.Max.x; x += (float)pat->w)
                            theme_blit(s_look, dl, pat->x, pat->y, pat->w, pat->h, ImVec2(x, y), ImVec2(x + (float)pat->w, y + (float)pat->h));
                    dl->PopClipRect();
                    theme_nine_at(s_look, dl, tk, r.Min, r.Max, 1);
                }
                else if (emp)
                    theme_nine_at(s_look, dl, emp, r.Min, r.Max, 2);
                if (pio.DrawCallback_SetSamplerLinear)
                    dl->AddCallback(pio.DrawCallback_SetSamplerLinear, nullptr);
            }
            /*  The thumb, sized and placed as ImGui places its grab. */
            const float avail    = axis ? w->InnerRect.GetWidth() : w->InnerRect.GetHeight();
            const float contents = (axis ? w->ContentSize.x : w->ContentSize.y) + (axis ? w->WindowPadding.x : w->WindowPadding.y) * 2.0f;
            const float smax     = axis ? w->ScrollMax.x : w->ScrollMax.y;
            const float sc       = axis ? w->Scroll.x : w->Scroll.y;
            float       grab     = len * (avail / (contents > avail ? contents : avail));
            const float least    = (float)(axis ? tb->w : tb->h);
            if (grab < least)
                grab = least;
            if (grab > len)
                grab = len;
            const float t   = smax > 0.0f ? sc / smax : 0.0f;
            const float off = t * (len - grab);
            if (axis)
                el_three(dl, tb, ImVec2(r.Min.x + off, r.Min.y), grab, true, 3);
            else
                el_three(dl, tb, ImVec2(r.Min.x, r.Min.y + off), grab, false, 3);
            /*  And the grip, at its own size in the thumb's middle. */
            if (axis && grab >= (float)tb->w)
                el_blit(dl, tb, 3, 0, tb->w - 6, tb->h, ImVec2(r.Min.x + off + (grab - (float)(tb->w - 6)) * 0.5f, r.Min.y));
            else if (!axis && grab >= (float)tb->h)
                el_blit(dl, tb, 0, 3, tb->w, tb->h - 6, ImVec2(r.Min.x, r.Min.y + off + (grab - (float)(tb->h - 6)) * 0.5f));
        }
    }
}

/* ---- the interface ----------------------------------------------------- */

extern "C" RUi *ui_create(SDL_Window *win, SDL_GPUDevice *dev, int swap_fmt, float dpi, const char *assets_dir)
{
    RUi *u = new RUi();
    memset(u, 0, sizeof *u);
    u->dev = dev;
    u->dpi = dpi;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io    = ImGui::GetIO();
    io.IniFilename = nullptr;
    /*  The system font is Chicago 12, as Chicago-Kare.  That is Duane
     *  King's pixel-for-pixel reproduction of Susan Kare's bitmap (MIT).
     *  Its outlines lie on a sixteen-pixel-per-em grid, so drawn at
     *  sixteen pixels with no hinting and no anti-aliasing every pixel
     *  is the bitmap's.  ChicagoFLF, an outline revival, stands in if it
     *  is absent, and ImGui's own pixel font after that. */
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
        R_WARN("ui", "no %s/ui.png: the palette is not drawn (python3 tools/pict.py --atlas rsrc/sc2k.rsrc assets)", assets_dir);
    return u;
}

SDL_GPUTexture *ui_art_texture(RUi *u, const uint8_t *rgba, int w, int h)
{
    if (rgba != u->art_from || !u->art)
    {
        if (u->art)
            SDL_ReleaseGPUTexture(u->dev, u->art);
        u->art      = rgba && w > 0 && h > 0 ? upload_rgba(u->dev, rgba, (unsigned)w, (unsigned)h) : nullptr;
        u->art_from = rgba;
    }
    return u->art;
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
    if (u->art)
        SDL_ReleaseGPUTexture(u->dev, u->art);
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
            /* only a text field being typed in owns the keys.  The
             * game's keys keep working after a click on a window */
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
