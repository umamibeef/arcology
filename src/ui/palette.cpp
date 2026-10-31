/*  ui/palette.cpp: the tool palette, PICT 500 with the thirty-four
 *  buttons the help texts enumerate, in a floating windoid.  Its RCI
 *  column opens the demand window, which is here too. */
#include <SDL3/SDL.h>

#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "imgui.h"
#include "imgui_internal.h"
#include "internal.hpp"

/*  The palette's buttons.
 *
 *      Rectangles in PICT 500.
 *      Measured from its bevels (the light top and left edges.
 *      The dark bottom and right).
 *      The tool each is.
 *
 *  The RCI column is the demand button. */
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

/* ---- the palette ------------------------------------------------------- */

/*  How long a palette button is held, in seconds, before its sub-tool
 *  menu opens rather than the press being a click. */
static const float SUB_HOLD = 0.25f;

void palette(RUi *u, RUiState *s)
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
        /*  A tool with sub-tools works as in the original.  A CLICK picks the tool with the sub-tool it last had.  A button HELD down opens the menu of its sub-tools beside it.  The release that ends a
         *  hold is not a click. */
        const bool has_subs = b.tool >= 0 && b.tool < RUI_N_MAP_TOOLS && RUI_SUB_NAME[b.tool][1];
        int        asked    = -1;
        if (s->open_menu && strncmp(s->open_menu, "tool:", 5) == 0)
            asked = atoi(s->open_menu + 5);
        const bool want = has_subs && ((down && !u->help_mode && ImGui::GetIO().MouseDownDuration[0] >= SUB_HOLD) || asked == b.tool);
        if (want && ((u->held_open != b.tool && !ImGui::IsPopupOpen("##subtool")) || asked == b.tool))
        {
            u->held_open = b.tool;
            u->sub_for   = b.tool;
            s->tool      = b.tool;
            ImGui::SetNextWindowPos(ImVec2(b0.x + sz.x, b0.y));
            ImGui::OpenPopup("##subtool");
        }
        bool pressed = down || (b.tool >= 0 && b.tool == s->tool && is_map_tool(b.tool)) ||
                       (b.tool == RUI_TOOL_DEMAND && s->show_demand) ||
                       (b.tool == RUI_TOOL_LAYER_UNDERGROUND && s->underground) ||
                       (b.tool < 0 && u->help_mode);
        if (pressed)
            draw_pict(u, dl, 100500, b0, b.x0, b.y0, b.x1 - b.x0 + 1, b.y1 - b.y0 + 1);
        const bool tip_asked = s->open_menu && strncmp(s->open_menu, "tip:", 4) == 0 && atoi(s->open_menu + 4) == b.tool;
        if ((ImGui::IsItemHovered() || tip_asked) && b.tool >= 0)
        {
            if (b.tool < RUI_N_MAP_TOOLS && RUI_SUB_NAME[b.tool][1])
                tooltip("%s: %s", RUI_TOOL_NAME[b.tool], RUI_SUB_NAME[b.tool][s->sub[b.tool]]);
            else
                tooltip("%s", RUI_TOOL_NAME[b.tool]);
        }
        /*  The menu takes the focus as it opens, and the button then
         *  stops being active, so its release is not a click.  The hold
         *  ends when the mouse comes up, wherever it is. */
        if (u->held_open == b.tool && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && asked != b.tool)
        {
            u->held_open = -1;
            continue;
        }
        if (!clicked)
            continue;
        s->want.sound = 505; /* Click */
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
                s->want.zoom_step = -1;
                break;
            case RUI_TOOL_ZOOM_IN:
                s->want.zoom_step = 1;
                break;
            case RUI_TOOL_DEMAND:
                s->show_demand = !s->show_demand;
                break;
            case RUI_TOOL_ROTATE_CCW:
                s->want.rotate = -1;
                break;
            case RUI_TOOL_ROTATE_CW:
                s->want.rotate = 1;
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
                if (has_subs)
                {
                    /*  A click picks the tool, with the sub-tool it last
                     *  had, and a second click puts it down. */
                    s->tool = (s->tool == b.tool) ? -1 : b.tool;
                    break;
                }
                s->tool = (s->tool == b.tool) ? -1 : b.tool;
                if (s->tool == RUI_TOOL_QUERY)
                    s->show_query = 1;
                break;
        }
    }
    /*  The sub-tool menu, drawn as the menu bar's menus are: the same
     *  item, check mark and right-hand column, the same padding.  The
     *  price stands where a menu's key would. */
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, u->menu_pad);
    const int  look     = menu_look_push();
    const bool sub_open = ImGui::BeginPopup("##subtool");
    menu_look_pop(look);
    if (sub_open)
    {
        menu_frame();
        /*  A sub-tool the simulation cannot do yet is shown and greyed
         *  out.  Its price is the original's all the same. */
        const int t = u->sub_for;
        for (int k = 0; k < RUI_N_SUB && RUI_SUB_NAME[t][k]; ++k)
        {
            char price[24] = "";
            if (s->sub_cost[t][k] > 0)
                snprintf(price, sizeof price, "$%d", (int)s->sub_cost[t][k]);
            const bool ready = (s->sub_ready[t] >> k) & 1;
            if (menu_item(RUI_SUB_NAME[t][k], price[0] ? price : nullptr, s->sub[t] == k, ready))
            {
                s->sub[t]     = k;
                s->tool       = t;
                s->want.sound = 505; /* Click */
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::End();
    ImGui::PopStyleVar();
}

/*  The zone demand indicator.
 *
 *      Three bars from a center line.
 *      Up for demand and down for surplus.
 *      Residential green.
 *      Commercial blue.
 *      Industrial yellow.
 *      As the original's popup draws them. */
void demand(RUi *u, RUiState *s)
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
    /*  Everything above went straight into the draw list, so this window
     *  submits no item at all: and mac_title left the cursor below the
     *  title bar.  This imGui reads as a boundary extension nobody
     *  closed ("Code uses SetCursorPos()... please submit an item e.g.
     *  Dummy() afterwards").  One Dummy the size of the content says
     *  what we actually covered. */
    {
        ImVec2 ws = ImGui::GetWindowSize();
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + TITLE_H));
        ImGui::Dummy(ImVec2(ws.x, ws.y - TITLE_H));
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
