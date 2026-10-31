/*  ui/menubar.cpp: the menu bar, title by title and item by item from the
 *  MENU resources.  Each item has its shortcut at the right and its check
 *  at the left.  The frame rate is drawn over it. */
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

static const char *SPEED_NAME[6] = {"Pause", "Pause", "Turtle", "Llama", "Cheetah", "African Swallow"};
static const char *VIEW_NAME[12] = {"Normal", "Traffic", "Density", "Growth and value", "Crime", "Police", "Pollution", "Land value", "Fire", "Power", "Water", "Growth"};

/* ---- the menu bar: the shortcut at the right, the check at the left ------ */

/*  The Mac's menus put a check mark at the left of an item.  They put
 *  its key at the far right, with the command mark that every one of
 *  them needs.  ImGui's MenuItem puts the key in a column just past the
 *  widest label and the mark to the right of that.  So an item is a
 *  Selectable that spans the row.  The highlight covers it, the layout
 *  width is the label's and the key's.  With the three pieces drawn on
 *  it by hand.  Every shortcut here takes the platform's command key: ⌘
 *  on the Mac, Ctrl elsewhere, and the labels say which. */
#ifdef __APPLE__
    #define SC_MOD   "\xE2\x8C\x98" /* ⌘ U+2318 */
    #define SC_SHIFT "\xE2\x87\xA7" /* ⇧ U+21E7 */
#else
    #define SC_MOD   "Ctrl+"
    #define SC_SHIFT "Shift+"
#endif
#define CHECK_MARK "\xE2\x9C\x93" /* ✓ U+2713, in Chicago */

/*  "⌘L", or "⇧⌘T" with shift: the Mac writes the shift first.  Windows
 *  writes Ctrl+Shift+T.  A ring of buffers so several can be live in one
 *  menu. */
const char *sc(char key, bool shift)
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

/*  A menu item as the Mac draws one.  The palette's sub-tool menus are
 *  drawn with it too. */
bool menu_item(const char *label, const char *shortcut, bool selected, bool enabled)
{
    const ImGuiStyle &st      = ImGui::GetStyle();
    const float       check_w = ImGui::CalcTextSize(CHECK_MARK).x + st.ItemInnerSpacing.x;
    const float       lw      = ImGui::CalcTextSize(label).x;
    const float       sw      = shortcut ? ImGui::CalcTextSize(shortcut).x : 0.0f;
    /*  the layout width: the check column, the label, and room for the
     *  key.  SpanAvailWidth then stretches the highlight to the row */
    const float need = check_w + lw + (shortcut ? 4.0f * st.ItemSpacing.x + sw : 0.0f);
    ImGui::PushID(label);
    if (!enabled)
        ImGui::BeginDisabled();
    const bool pressed = ImGui::Selectable("##item", false, ImGuiSelectableFlags_SelectOnRelease | ImGuiSelectableFlags_SetNavIdOnHover | ImGuiSelectableFlags_SpanAvailWidth, ImVec2(need, 0.0f));
    if (!enabled)
        ImGui::EndDisabled();
    ImGui::PopID();
    ImDrawList    *dl = ImGui::GetWindowDrawList();
    const ImVec2   a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const ThemeEl *sel = look_el("menu_item_selected");
    /*  The item under the pointer is lit with the scheme's selected
     *  element, and its text takes that element's color. */
    const bool lit = sel && enabled && ImGui::IsItemHovered();
    if (lit)
        menu_row_lit(a.y - st.ItemSpacing.y * 0.5f, b.y + st.ItemSpacing.y * 0.5f);
    const ImU32 col = lit ? theme_col(sel->text, WHITE) : ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    const float ty  = a.y + (b.y - a.y - ImGui::GetTextLineHeight()) * 0.5f;
    if (selected)
        dl->AddText(ImVec2(a.x, ty), col, CHECK_MARK);
    dl->AddText(ImVec2(a.x + check_w, ty), col, label);
    if (shortcut)
        dl->AddText(ImVec2(b.x - sw, ty), col, shortcut);
    return pressed;
}

/*  The toggle form, as ImGui's: flips *p_selected on a pick. */
bool menu_item(const char *label, const char *shortcut, bool *p_selected, bool enabled)
{
    const bool pressed = menu_item(label, shortcut, p_selected && *p_selected, enabled);
    if (pressed && p_selected)
        *p_selected = !*p_selected;
    return pressed;
}

/*  A menu item from the command table: its label, its key at the right,
 *  and its check where it carries one.  Choosing it runs the command, as
 *  its key does. */
static void command_item(RUiState *s, int cmd)
{
    const RUiCommand *c = ui_command(cmd);
    if (menu_item(c->label, c->key ? sc(c->key, c->shift != 0) : nullptr, ui_command_shown(s, cmd) != 0))
        ui_command_do(s, cmd);
}

/*  A menu of the menu bar, opened by --open-menu as well as by the
 *  mouse.  Its title is lit while it is open. */
static bool begin_menu(const RUiState *s, const char *label)
{
    if (s->open_menu && strcmp(s->open_menu, label) == 0)
        ImGui::OpenPopup(label);
    const ThemeEl *hl   = look_el("menu_bar_selected");
    const bool     open = ImGui::IsPopupOpen(label);
    if (open && hl)
    {
        /*  The title's span as BeginMenu lays it in a bar: half the item
         *  spacing in, the label, and a full spacing either side. */
        const float  sp = ImGui::GetStyle().ItemSpacing.x;
        const ImVec2 at = ImGui::GetCursorScreenPos(), wp = ImGui::GetWindowPos(), wz = ImGui::GetWindowSize();
        const float  x0 = at.x + (float)(int)(sp * 0.5f) - sp;
        const float  x1 = at.x + (float)(int)(sp * 0.5f) + ImGui::CalcTextSize(label).x + sp;
        theme_nine(s_look, ImGui::GetWindowDrawList(), hl, ImVec2(x0, wp.y), ImVec2(x1, wp.y + wz.y - 1.0f));
    }
    const int n = menu_look_push();
    if (open && hl && n)
        ImGui::PushStyleColor(ImGuiCol_Text, theme_col(hl->text, WHITE));
    const bool r = ImGui::BeginMenu(label);
    if (open && hl && n)
        ImGui::PopStyleColor();
    menu_look_pop(n);
    if (r)
        menu_frame();
    return r;
}

/*  A tooltip in a frame as a menu's. */
void tooltip(const char *fmt, ...)
{
    char    text[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    /*  A tooltip opened inside a window with no padding of its own, as
     *  the palette is, keeps the style's padding all the same. */
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, s_look ? s_look->menu_pad : ImGui::GetStyle().WindowPadding);
    const int n = menu_look_push();
    ImGui::BeginTooltip();
    menu_look_pop(n);
    ImGui::PopStyleVar();
    menu_frame();
    ImGui::TextUnformatted(text);
    ImGui::EndTooltip();
}

/*  The frame rate and the time a frame takes, in the interface's own font.
 *  They stand in a black box at the top left of the view, under the menu bar. */
void fps_overlay(const RUiState *s)
{
    char        line[64];
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImVec2      p  = ImVec2(ImGui::GetMainViewport()->WorkPos.x + 8.0f, ImGui::GetMainViewport()->WorkPos.y + 8.0f);
    ImVec2      sz;
    snprintf(line, sizeof line, "%.0f fps  %.1f ms", (double)s->fps, (double)s->frame_ms);
    sz = ImGui::CalcTextSize(line);
    dl->AddRectFilled(ImVec2(p.x - 4.0f, p.y - 2.0f), ImVec2(p.x + sz.x + 4.0f, p.y + sz.y + 2.0f), IM_COL32(0, 0, 0, 200));
    dl->AddText(p, IM_COL32(255, 255, 255, 255), line);
}

void menu_bar(RUi *u, RUiState *s)
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
     *  not drawn.  Its one item, About SimCity 2000, waits for a home.
     *  MENU 1001 is the first that is. */
    if (begin_menu(s, "File"))
    {
        menu_item("Load Tile Set", nullptr, false, false);
        menu_divider();
        command_item(s, RUI_CMD_LOAD);
        menu_item("New City", sc('N'), false, false);
        menu_item("Edit New Map", sc('E'), false, false);
        menu_item("Load Scenario", sc('Z'), false, false);
        menu_divider();
        command_item(s, RUI_CMD_SAVE);
        if (menu_item("Save City As...", nullptr))
            u->save_popup = true;
        menu_divider();
        command_item(s, RUI_CMD_QUIT);
        ImGui::EndMenu();
    }
    /* MENU 1002 */
    if (begin_menu(s, "Speed"))
    {
        const char *keys[6] = {"", "1", "2", "3", "4", "5"}; /* bare digits, as the keys are */
        for (int k = 1; k <= 5; ++k)
            if (menu_item(SPEED_NAME[k], keys[k], s->speed == k))
                s->speed = k;
        ImGui::EndMenu();
    }
    /* MENU 1003 */
    if (begin_menu(s, "Options"))
    {
        menu_item("Auto-Budget", nullptr, false, false);
        menu_item("Auto-Goto", nullptr, true, false);
        menu_item("Sound Effects", nullptr, true, false);
        if (menu_item("Music", nullptr, s->music_on != 0))
            s->want.music = 1;
        menu_divider();
        /*  The Kaleidoscope schemes: None, then every pack found under
         *  assets/themes.  A pick is a request.  App puts the scheme on
         *  and writes the preference. */
        if (begin_submenu("Theme"))
        {
            const bool none = s->theme_name[0] == 0 || strcmp(s->theme_name, "none") == 0;
            if (menu_item("None", nullptr, none) && !none)
            {
                snprintf(s->theme_name, sizeof s->theme_name, "none");
                s->want.theme = 1;
            }
            if (s->n_themes)
                menu_divider();
            for (int k = 0; k < s->n_themes; ++k)
            {
                const bool cur = strcmp(s->theme_list[k], s->theme_name) == 0;
                if (menu_item(s->theme_list[k], nullptr, cur) && !cur)
                {
                    snprintf(s->theme_name, sizeof s->theme_name, "%s", s->theme_list[k]);
                    s->want.theme = 1;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    /* MENU 1004 */
    if (begin_menu(s, "Disasters"))
    {
        /*  The menu's eight, in the menu's order. */
        static const int ITEM[8] = {RUI_DISASTER_FIRE, RUI_DISASTER_FLOOD, RUI_DISASTER_AIR_CRASH, RUI_DISASTER_TORNADO, RUI_DISASTER_EARTHQUAKE, RUI_DISASTER_MONSTER, RUI_DISASTER_HURRICANE, RUI_DISASTER_RIOT};
        for (int k = 0; k < 8; ++k)
            if (menu_item(RUI_DISASTER_NAME[ITEM[k]]))
                s->want.disaster = ITEM[k];
        menu_divider();
        menu_item("No Disasters", nullptr, false, false);
        ImGui::EndMenu();
    }
    /* MENU 1005 */
    if (begin_menu(s, "Windows"))
    {
        menu_item("Map", sc('M'), false, false);
        command_item(s, RUI_CMD_BUDGET);
        menu_item("Ordinances", sc('O'), false, false);
        command_item(s, RUI_CMD_POPULATION);
        menu_item("Industry", sc('I'), false, false);
        command_item(s, RUI_CMD_GRAPHS);
        menu_item("Neighbors", sc('H'), false, false);
        menu_divider();
        if (menu_item("Tools", nullptr, s->show_palette != 0))
            s->show_palette = !s->show_palette;
        if (menu_item("Query", nullptr, s->show_query != 0))
            s->show_query = !s->show_query;
        ImGui::EndMenu();
    }
    /* MENU 1006 */
    if (begin_menu(s, "Newspaper"))
    {
        menu_item("Subscription", nullptr, false, false);
        menu_item("Extra!!!", nullptr, false, false);
        ImGui::EndMenu();
    }
    /* the view switches, ours, at the right */
    if (begin_menu(s, "View"))
    {
        /*  No art set and no pixel scale here: the zoom is continuous
         *  and picks the set that is closest to native by itself.  The
         *  pixel scale is a developer's knob, not a view.  The keys and
         *  the command line still reach both. */
        bool b;
        command_item(s, RUI_CMD_GEOMETRY);
        command_item(s, RUI_CMD_PLAN);
        command_item(s, RUI_CMD_UNDERGROUND);
        b = s->free_turn != 0;
        if (menu_item("Free rotation", nullptr, &b))
            s->free_turn = b;
        b = s->no_clouds == 0;
        if (menu_item("Clouds", nullptr, &b))
            s->no_clouds = !b;
        b = s->no_air == 0;
        if (menu_item("Air", nullptr, &b))
            s->no_air = !b;
        b = s->outline != 0;
        if (menu_item("Lane graph", nullptr, &b))
            s->outline = b;
        if (begin_submenu("Trees"))
        {
            if (menu_item("By the city's place", nullptr, s->biome < 0))
                s->biome = -1;
            menu_divider();
            for (int k = 0; k < s->n_biomes; ++k)
                if (menu_item(s->biome_names[k], nullptr, s->biome == k))
                    s->biome = k;
            ImGui::EndMenu();
        }
        if (begin_submenu("Time of day"))
        {
            static const struct
            {
                const char *name;
                float       hour;
            } AT[] = {
                {"Dawn",      6.5f   },
                {"Morning",   9.0f   },
                {"Midday",    13.052f},
                {"Afternoon", 16.0f  },
                {"Dusk",      17.6f  },
                {"Night",     22.0f  }
            };
            for (int k = 0; k < (int)(sizeof AT / sizeof AT[0]); ++k)
                if (menu_item(AT[k].name, nullptr, fabsf(s->hour - AT[k].hour) < 0.05f))
                    s->hour = AT[k].hour;
            menu_divider();
            ui_slider("hour", &s->hour, 0.0f, 24.0f, "%.1f", 160.0f);
            ImGui::EndMenu();
        }
        menu_divider();
        if (begin_submenu("Data view"))
        {
            for (int k = 0; k < 12; ++k)
                if (menu_item(VIEW_NAME[k], nullptr, s->view == k))
                    s->view = k;
            ImGui::EndMenu();
        }
        menu_divider();
        command_item(s, RUI_CMD_SCREENSHOT);
        ImGui::EndMenu();
    }
    /*  Debug: the views and windows for looking at how the world is built,
     *  not at the city. */
    if (begin_menu(s, "Debug"))
    {
        bool b;
        command_item(s, RUI_CMD_GRID);
        command_item(s, RUI_CMD_OUTLINE);
        command_item(s, RUI_CMD_TRIANGLES);
        b = s->show_cells != 0;
        if (menu_item("Tile coordinates", nullptr, &b))
            s->show_cells = b;
        command_item(s, RUI_CMD_NO_DEPTH);
        b = s->show_fps != 0;
        if (menu_item("Frame rate", nullptr, &b))
            s->show_fps = b;
        menu_divider();
        if (menu_item("Tuning", nullptr, s->show_tuning != 0))
            s->show_tuning = !s->show_tuning;
        if (menu_item("Shading", nullptr, s->show_shading != 0))
            s->show_shading = !s->show_shading;
        command_item(s, RUI_CMD_INSPECT);
        if (menu_item("Renderer stats", nullptr, s->show_renderer != 0))
            s->show_renderer = !s->show_renderer;
        if (menu_item("Messages", nullptr, s->show_log != 0))
            s->show_log = !s->show_log;
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
