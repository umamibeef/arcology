/*  ui/windows.cpp: the game's windows in the original's chrome.  They are
 *  the city, the budget, the graphs, a picture, the query, the tuning, the
 *  renderer and the messages, and the load and save dialogs. */
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

static const char *STAGE_NAME[10] = {"Village", "Town", "City", "Capital", "Metropolis", "Megalopolis", "Stage 6", "Stage 7", "Stage 8", "Stage 9"};
static const char *ZONE_NAME[16]  = {
    "none", "light residential", "dense residential", "light commercial", "dense commercial", "light industrial", "dense industrial", "military", "airport", "seaport", "zone 10", "zone 11", "zone 12", "zone 13", "zone 14", "zone 15"};

/* ---- the windows -------------------------------------------------------- */

void city_window(RUiState *s)
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
        ui_progress(f, -1.0f, ov);
    }
    ImGui::Separator();
    ImGui::Text("Power %d%%   Water %d%%", (int)s->power_pct, (int)s->water_pct);
    ImGui::Text("Unemployment %d", (int)s->unemployment);
    ImGui::Text("Land value %d   Crime %d", (int)s->land_value_tot, (int)s->crime_tot);
    ImGui::Text("Traffic %d   Pollution %d", (int)s->traffic_tot, (int)s->pollution_tot);
    ImGui::End();
}

void budget_window(RUiState *s)
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

void graphs_window(RUiState *s)
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

/*  A picture in a window of its own, as $154A opens one.  The window
 *  closes when its close box is clicked. */
void picture_window(RUi *u, RUiState *s)
{
    const Pict *p    = find_pict(u, (int)s->show_picture);
    int         open = 1;
    if (!p)
    {
        s->show_picture = 0;
        return;
    }
    if (mac_begin("", &open, (float)p->w + 16.0f, ImVec2(200.0f, 160.0f)))
    {
        draw_pict(u, ImGui::GetWindowDrawList(), (int)s->show_picture, ImGui::GetCursorScreenPos());
        ImGui::Dummy(ImVec2((float)p->w, (float)p->h));
    }
    ImGui::End();
    if (!open)
        s->show_picture = 0;
}

void query_window(RUiState *s)
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

/*  Tuning: the look's knobs, live.  Every slider rebuilds the
 *  mesh, which takes about a fifth of a second, so a change is seen as it
 *  is made rather than described.
 *
 *  The window holds no list of them.  The app hands over what
 *  geo_defaults declared: the label, the range and the value each starts
 *  at.  So a new knob gets a slider with no change here.  The
 *  window is the game's own, through mac_begin, so it wears whatever
 *  theme is on. */
void tuning_window(RUiState *s)
{
    const char *group = NULL;
    int         k, shown = 0;
    if (!mac_begin("Tuning", &s->show_tuning, 340.0f, ImVec2(110.0f, 300.0f)))
    {
        ImGui::End();
        return;
    }
    ImGui::PushItemWidth(150.0f);
    for (k = 0; k < s->n_knob; ++k)
    {
        RUiKnob *n = &s->knob[k];
        if (!n->label)
            continue; /* a view or a switch some other control owns */
        if (shown && n->group && (!group || strcmp(group, n->group) != 0))
            ImGui::Separator();
        group = n->group;
        ++shown;
        if (ui_slider(n->label, &n->v, n->lo, n->hi, "%.3f", 150.0f))
            s->tune_changed = 1;
    }
    ImGui::PopItemWidth();
    ImGui::Separator();
    if (ui_button("Defaults"))
    {
        for (k = 0; k < s->n_knob; ++k)
            if (s->knob[k].label)
                s->knob[k].v = s->knob[k].def;
        s->tune_changed = 1;
    }
    ImGui::TextUnformatted("in tiles; each change rebuilds the mesh");
    ImGui::End();
}

/*  THE SHADING WINDOW: a slider for each of the shading numbers, under
 *  the title of its section.  They move the next frame.  Defaults puts
 *  back the values the program started with. */
void shading_window(RUiState *s)
{
    int k;
    if (!mac_begin("Shading", &s->show_shading, 380.0f, ImVec2(110.0f, 300.0f)))
    {
        ImGui::End();
        return;
    }
    ImGui::PushItemWidth(160.0f);
    for (k = 0; k < s->n_shade; ++k)
    {
        RUiKnob *n = &s->shade[k];
        if (n->group && (!k || !s->shade[k - 1].group || strcmp(n->group, s->shade[k - 1].group) != 0))
        {
            if (k)
                ImGui::Separator();
            ImGui::TextUnformatted(n->group);
        }
        if (ui_slider(n->label, &n->v, n->lo, n->hi, "%.3f", 160.0f))
            s->shading_changed = 1;
    }
    ImGui::PopItemWidth();
    ImGui::Separator();
    if (ui_button("Defaults"))
    {
        for (k = 0; k < s->n_shade; ++k)
            s->shade[k].v = s->shade[k].def;
        s->shading_changed = 1;
    }
    ImGui::TextUnformatted("each change shows at once");
    ImGui::End();
}

void renderer_window(RUiState *s)
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
    if (ui_check("Plain sweep (debug: painter's order, no depth)", &b))
        s->plain_sweep = b;
    ImGui::End();
}

void log_window(RUiState *s)
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

/*  A DIALOG: a modal window in the scheme's movable dialog frame, its
 *  title in the frame's band.  Answers whether it is open, and the
 *  caller then ends it with ImGui::EndPopup. */
static bool dialog_begin(RUi *u, const char *title)
{
    ImGui::PushStyleColor(ImGuiCol_PopupBg, WHITE);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, frame_pad(u, "movable_dialog", ImVec2(12.0f, 10.0f)));
    const bool open = ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (!open)
        return false;
    bool drawn = false;
    theme_frame(u, "movable_dialog", title, &drawn, nullptr);
    if (!drawn)
    {
        ImGui::TextUnformatted(title);
        ImGui::Separator();
    }
    return true;
}

/*  The load menu.  The app has already filled city_list by scanning
 *  city_dir, so this only has to show it.  A path can be typed for a
 *  city that lives somewhere else. */
void load_popup(RUi *u, RUiState *s)
{
    if (s->open_load)
    {
        ImGui::OpenPopup("Load City");
        s->open_load = 0;
    }
    if (!dialog_begin(u, "Load City"))
        return;

    if (s->n_cities > 0)
    {
        ImGui::TextUnformatted(s->city_dir);
        ImGui::Separator();
        ImGui::BeginChild("##cities", ImVec2(360.0f, 320.0f), true);
        for (int k = 0; k < s->n_cities; ++k)
        {
            if (ui_row(s->city_list[k]))
            {
                snprintf(s->load_path, sizeof s->load_path, "%s/%s", s->city_dir, s->city_list[k]);
                s->want.load = 1;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndChild();
    }
    else
    {
        ImGui::TextUnformatted("No cities directory found.");
        ImGui::TextUnformatted("Set --cities, or type a path below.");
    }

    ImGui::Separator();
    ImGui::SetNextItemWidth(360.0f);
    if (ImGui::InputText("##path", s->load_path, sizeof s->load_path, ImGuiInputTextFlags_EnterReturnsTrue))
    {
        s->want.load = 1;
        ImGui::CloseCurrentPopup();
    }
    if (ui_button("Open", ImVec2(100.0f, 0.0f), true) && s->load_path[0])
    {
        s->want.load = 1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine(0.0f, 12.0f);
    if (ui_button("Cancel", ImVec2(100.0f, 0.0f)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void save_popup(RUi *u, RUiState *s)
{
    if (u->save_popup)
    {
        ImGui::OpenPopup("Save City As");
        u->save_popup = false;
    }
    if (dialog_begin(u, "Save City As"))
    {
        ImGui::TextUnformatted("Save city as:");
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText("##save", s->save_path, sizeof s->save_path);
        if (ui_button("Save", ImVec2(100.0f, 0.0f), true))
        {
            s->want.save = 1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0.0f, 12.0f);
        if (ui_button("Cancel", ImVec2(100.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
