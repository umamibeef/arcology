/*  ui/frame.cpp: one frame of the interface.  The menu bar, the palette,
 *  the windows and the inspector are each drawn from here, over the base
 *  in ui.cpp they share. */
#include "ui.h"

#include <SDL3/SDL.h>

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <utility>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imgui_internal.h"
#include "internal.hpp"

/*  THE MARKS' PALETTE, one color a meaning (ui.h).  The device lights
 *  the marked tiles in these colors, and a label for a mark takes the
 *  same one. */
static const uint32_t MARK_COLOR[RUI_MARKS] = {
    0x000000, /* RUI_MARK_NONE */
    0xFFFFFF, /* RUI_MARK_HOVER: white */
    0xFF3A2E, /* RUI_MARK_REFUSED: red */
    0xFF942E, /* RUI_MARK_TAKEN: orange */
    0xFFDC50, /* RUI_MARK_SELECT: yellow */
};

extern "C" uint32_t ui_mark_color(int mark)
{
    return mark > 0 && mark < RUI_MARKS ? MARK_COLOR[mark] : 0u;
}

/*  A mark's color for ImGui, opaque. */
static ImU32 mark_col(int mark)
{
    const uint32_t c = ui_mark_color(mark);
    return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);
}

/*  THE LANE GRAPH VIEW'S TILES.  The device marks each tile of the road
 *  network in the color of its kind of sprite (uiglue.c).  Here the legend
 *  names the colors.  Each tile's own sprite is drawn small in its
 *  bottom-right corner on screen, whichever way the map is turned. */
static void lane_tiles(RUi *u, RUiState *s)
{
    ImDrawList     *dl   = ImGui::GetBackgroundDrawList();
    const ImVec2    disp = ImGui::GetIO().DisplaySize;
    const float     tsz = 14.0f, row = tsz + 6.0f;
    SDL_GPUTexture *art = ui_art_texture(u, s->spr_rgba, s->spr_w, s->spr_h);
    if (art && s->cell_ok && s->n_spr > 0)
    {
        /*  A tile's width on screen, from its two steps, and the corner
         *  lowest and rightmost.  The sprite is a third of the tile wide,
         *  and its height keeps the art's shape. */
        const float cw = fmaxf(fabsf(s->cell_c[0]) + fabsf(s->cell_r[0]), 1.0f);
        const float sw = cw / 3.0f;
        if (sw >= 6.0f)
            for (int k = 0; k < s->n_spr; ++k)
            {
                const RUiSprite *p  = &s->spr[k];
                float            bx = -1e9f, by = -1e9f, best = -1e9f;
                for (int q = 0; q < 4; ++q)
                {
                    float cc = (float)p->col + ((q & 1) ? 1.0f : 0.0f), rr = (float)p->row + ((q & 2) ? 1.0f : 0.0f);
                    float sx = s->cell_o[0] + cc * s->cell_c[0] + rr * s->cell_r[0] + (float)p->alt * s->cell_a[0];
                    float sy = s->cell_o[1] + cc * s->cell_c[1] + rr * s->cell_r[1] + (float)p->alt * s->cell_a[1];
                    if (sx + sy > best)
                        best = sx + sy, bx = sx, by = sy;
                }
                const float  sh = sw * (float)p->h / fmaxf((float)p->w, 1.0f);
                const ImVec2 p0(bx - sw - 2.0f, by - sh - 2.0f), p1(bx - 2.0f, by - 2.0f);
                if (p1.x < 0.0f || p1.y < 0.0f || p0.x > disp.x || p0.y > disp.y)
                    continue;
                float u0 = (float)p->x / (float)s->spr_w, u1 = (float)(p->x + p->w) / (float)s->spr_w;
                if (p->flip)
                    std::swap(u0, u1);
                dl->AddRectFilled(ImVec2(p0.x - 1.0f, p0.y - 1.0f), ImVec2(p1.x + 1.0f, p1.y + 1.0f), IM_COL32(18, 22, 24, 160), 2.0f);
                dl->AddImage((ImTextureID)(intptr_t)art, p0, p1, ImVec2(u0, (float)p->y / (float)s->spr_h), ImVec2(u1, (float)(p->y + p->h) / (float)s->spr_h));
            }
    }
    /*  The legend, at the bottom left of the window. */
    float w = 0.0f;
    for (int k = 0; k < s->tile_kinds; ++k)
        w = fmaxf(w, ImGui::GetFont()->CalcTextSizeA(tsz, FLT_MAX, 0.0f, s->tile_kind_name[k]).x);
    const ImVec2 at(12.0f, disp.y - 12.0f - row * (float)s->tile_kinds - 8.0f);
    dl->AddRectFilled(at, ImVec2(at.x + w + tsz + 26.0f, disp.y - 12.0f), IM_COL32(18, 22, 24, 210), 4.0f);
    for (int k = 0; k < s->tile_kinds; ++k)
    {
        const uint32_t c = s->tile_kind_rgb[k];
        const ImVec2   b(at.x + 8.0f, at.y + 6.0f + row * (float)k);
        dl->AddRectFilled(b, ImVec2(b.x + tsz, b.y + tsz), IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255), 2.0f);
        dl->AddText(ImGui::GetFont(), tsz, ImVec2(b.x + tsz + 8.0f, b.y - 1.0f), IM_COL32(235, 235, 235, 255), s->tile_kind_name[k]);
    }
}

/*  A label on the map.  The text goes over a dark copy of it a pixel
 *  down and to the right, so it reads on any ground. */
static void shadowed_text(ImDrawList *dl, float size, ImVec2 at, ImU32 col, const char *txt)
{
    dl->AddText(ImGui::GetFont(), size, ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 220), txt);
    dl->AddText(ImGui::GetFont(), size, at, col, txt);
}

extern "C" void ui_frame(RUi *u, RUiState *s)
{
    if (!u)
        return;
    s_look      = u;
    u->menu_pad = ImGui::GetStyle().WindowPadding;
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    g_ui_for_title = u;
    /*  --open-menu window:NAME: that window or dialog open from the
     *  first frame, for a shot. */
    if (s->open_menu && strncmp(s->open_menu, "window:", 7) == 0)
    {
        static const struct
        {
            const char *name;
            size_t      off;
        } W[] = {
            {"city",     offsetof(RUiState, show_city)    },
            {"budget",   offsetof(RUiState, show_budget)  },
            {"graphs",   offsetof(RUiState, show_graphs)  },
            {"query",    offsetof(RUiState, show_query)   },
            {"tuning",   offsetof(RUiState, show_tuning)  },
            {"shading",  offsetof(RUiState, show_shading) },
            {"renderer", offsetof(RUiState, show_renderer)},
            {"log",      offsetof(RUiState, show_log)     }
        };
        const char *w = s->open_menu + 7;
        for (const auto &e : W)
            if (strcmp(w, e.name) == 0)
                *(int *)(void *)((char *)s + e.off) = 1;
        if (strcmp(w, "load") == 0)
            s->open_load = 1;
        if (strcmp(w, "save") == 0)
            u->save_popup = true;
        s->open_menu = nullptr; /* opened once: the window then lives as any other */
    }
    menu_bar(u, s);
    if (s->show_fps)
        fps_overlay(s);
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
    if (s->show_picture)
        picture_window(u, s);
    if (s->insp.show)
        inspect_window(s);
    if (s->show_tuning)
        tuning_window(s);
    if (s->show_shading)
        shading_window(s);
    /*  The tiles the query, the tool, a drag and the selection mark are
     *  lit by the device, on the surfaces they lie on (ui_mark_color).
     *  Only their labels are drawn here: the tool's price beside the top
     *  corner of its area, in the area's color. */
    if (s->tool_poly_ok && s->tool_price > 0)
    {
        int top = 0;
        for (int k = 1; k < 4; ++k)
            if (s->tool_poly[k][1] < s->tool_poly[top][1])
                top = k;
        char price[24];
        snprintf(price, sizeof price, "$%d", (int)s->tool_price);
        const ImVec2 at(s->tool_poly[top][0] + 6.0f, s->tool_poly[top][1] - ImGui::GetFontSize() - 2.0f);
        shadowed_text(ImGui::GetBackgroundDrawList(), ImGui::GetFontSize(), at, mark_col(s->tool_refused ? RUI_MARK_REFUSED : RUI_MARK_HOVER), price);
    }
    inspect_draw(s);
    if (s->sel_ok && s->sel_text[0])
    {
        /*  The selection's caption, above its top corner, or below it near
         *  the top of the window.  After the release the area blinks and
         *  goes, and the log says what was written. */
        ImVec2 at(1e9f, 1e9f);
        for (int k = 0; k < 4; ++k)
            if (s->sel_poly[k][1] < at.y || (s->sel_poly[k][1] == at.y && s->sel_poly[k][0] < at.x))
                at = ImVec2(s->sel_poly[k][0], s->sel_poly[k][1]);
        ImDrawList  *dl  = ImGui::GetBackgroundDrawList();
        const float  tsz = 18.0f;
        const ImVec2 sz  = ImGui::GetFont()->CalcTextSizeA(tsz, FLT_MAX, 0.0f, s->sel_text);
        ImVec2       tp(at.x, at.y - sz.y - 6.0f);
        if (tp.y < 30.0f)
            tp.y = at.y + 6.0f;
        dl->AddRectFilled(ImVec2(tp.x - 4.0f, tp.y - 2.0f), ImVec2(tp.x + sz.x + 4.0f, tp.y + sz.y + 2.0f), IM_COL32(18, 22, 24, 210), 3.0f);
        dl->AddText(ImGui::GetFont(), tsz, tp, mark_col(RUI_MARK_SELECT), s->sel_text);
    }
    if (s->show_renderer)
        renderer_window(s);
    if (s->show_log)
        log_window(s);
    load_popup(u, s);
    save_popup(u, s);

    if (s->plan && s->show_cells && s->cell_ok)
    {
        /*  Every cell's own number, "col,row", tucked into its
         *  bottom-right corner on screen, in the map view only.  There
         *  the cells are squares and the frame below is exact.  Drawn
         *  through a transparent click-through window. */
        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        const float  cw   = sqrtf(s->cell_c[0] * s->cell_c[0] + s->cell_c[1] * s->cell_c[1]);
        const float  det  = s->cell_c[0] * s->cell_r[1] - s->cell_c[1] * s->cell_r[0];
        if (cw >= 30.0f && fabsf(det) > 1e-6f)
        {
            /*  The cells in view: the screen's corners back into col,row
             *  through the frame's inverse, then every cell between. */
            float cmin = 1e9f, cmax = -1e9f, rmin = 1e9f, rmax = -1e9f;
            for (int k = 0; k < 4; ++k)
            {
                float sx = (k & 1) ? disp.x : 0.0f, sy = (k & 2) ? disp.y : 0.0f;
                float dx = sx - s->cell_o[0], dy = sy - s->cell_o[1];
                float col = (dx * s->cell_r[1] - dy * s->cell_r[0]) / det;
                float row = (s->cell_c[0] * dy - s->cell_c[1] * dx) / det;
                cmin      = col < cmin ? col : cmin;
                cmax      = col > cmax ? col : cmax;
                rmin      = row < rmin ? row : rmin;
                rmax      = row > rmax ? row : rmax;
            }
            const float tsz = cw >= 90.0f ? 15.0f : cw >= 50.0f ? 12.0f
                                                                : 10.0f;
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(disp);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
            ImGui::Begin("##cells", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);
            ImDrawList *dl = ImGui::GetWindowDrawList();
            int         c0 = (int)floorf(cmin) - 1, c1 = (int)ceilf(cmax) + 1, r0 = (int)floorf(rmin) - 1, r1 = (int)ceilf(rmax) + 1;
            c0 = c0 < 0 ? 0 : c0;
            r0 = r0 < 0 ? 0 : r0;
            c1 = c1 > 127 ? 127 : c1;
            r1 = r1 > 127 ? 127 : r1;
            for (int r = r0; r <= r1; ++r)
                for (int c = c0; c <= c1; ++c)
                {
                    /*  The cell's four corners on screen.  The label
                     *  goes by the one lowest and rightmost, whichever
                     *  way the map is turned. */
                    float bx = -1e9f, by = -1e9f, best = -1e9f;
                    for (int k = 0; k < 4; ++k)
                    {
                        float cc = (float)c + ((k & 1) ? 1.0f : 0.0f), rr = (float)r + ((k & 2) ? 1.0f : 0.0f);
                        float sx = s->cell_o[0] + cc * s->cell_c[0] + rr * s->cell_r[0];
                        float sy = s->cell_o[1] + cc * s->cell_c[1] + rr * s->cell_r[1];
                        if (sx + sy > best)
                        {
                            best = sx + sy;
                            bx   = sx;
                            by   = sy;
                        }
                    }
                    char txt[24];
                    snprintf(txt, sizeof txt, "%d,%d", c, r);
                    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(tsz, FLT_MAX, 0.0f, txt);
                    ImVec2 at(bx - ts.x - 3.0f, by - ts.y - 2.0f);
                    if (at.x < -ts.x || at.y < -ts.y || at.x > disp.x || at.y > disp.y)
                        continue;
                    shadowed_text(dl, tsz, at, IM_COL32(255, 240, 120, 255), txt);
                }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }
    }
    if (s->tile_kinds > 0)
        lane_tiles(u, s);
    if (u->theme.loaded)
        themed_scrollbars();
    ImGui::Render();
    u->frame_open = true;
}
