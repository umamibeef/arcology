/*  ui/inspect.cpp: the inspector's window and its outlines over the map.
 *  The app fills what they show (app/inspect.c). */
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "imgui.h"
#include "internal.hpp"
#include "ui.h"

/*  The color of an outlined object, RUI_OBJ_*. */
static const ImU32 OBJ_COLOR[RUI_OBJ_COLORS] = {
    IM_COL32(70, 210, 255, 255),  /* a highway's driving lane: cyan */
    IM_COL32(255, 225, 60, 255),  /* a street's driving lane: yellow */
    IM_COL32(255, 80, 210, 255),  /* a ramp's lane: magenta */
    IM_COL32(255, 60, 60, 255),   /* an entry or an exit lane: red */
    IM_COL32(170, 175, 185, 255), /* a shoulder: gray */
    IM_COL32(200, 170, 120, 255), /* a median: tan */
    IM_COL32(255, 170, 200, 255), /* a sidewalk: pink */
    IM_COL32(190, 150, 255, 255), /* a rail: lavender */
    IM_COL32(150, 120, 90, 255),  /* a railway's gravel: brown */
    IM_COL32(255, 130, 40, 255),  /* a junction: orange */
    IM_COL32(110, 140, 175, 255), /* a road's structure: steel blue */
    IM_COL32(255, 255, 255, 255), /* anything else: white */
};
static const char *const OBJ_WORD[RUI_OBJ_COLORS] = {"highway lane", "street lane", "ramp lane", "entry/exit", "shoulder", "median", "sidewalk", "rail", "gravel", "junction", "structure", "other"};

/*  One object outlined whole, from its triangles in window points.  The
 *  triangles are laid into a mask a point a cell, over the box round
 *  them that the window shows.  The mask is filled faintly, a run a row,
 *  and its border is drawn: dark under the color, so it reads on any
 *  ground.  So the object is one shape, whatever triangles it is made
 *  of. */
static void obj_outline(ImDrawList *dl, const float *tri, int n, int color, int bright)
{
    static unsigned char *mask;
    static size_t         cap;
    const ImVec2          ds = ImGui::GetIO().DisplaySize;
    float                 x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX;
    for (int k = 0; k < 3 * n; ++k)
    {
        x0 = tri[2 * k] < x0 ? tri[2 * k] : x0, x1 = tri[2 * k] > x1 ? tri[2 * k] : x1;
        y0 = tri[2 * k + 1] < y0 ? tri[2 * k + 1] : y0, y1 = tri[2 * k + 1] > y1 ? tri[2 * k + 1] : y1;
    }
    x0 = x0 < 0.0f ? 0.0f : x0, y0 = y0 < 0.0f ? 0.0f : y0;
    x1 = x1 > ds.x ? ds.x : x1, y1 = y1 > ds.y ? ds.y : y1;
    if (n <= 0 || x1 <= x0 || y1 <= y0)
        return;
    const int ox = (int)x0 - 1, oy = (int)y0 - 1, w = (int)x1 - ox + 2, h = (int)y1 - oy + 2;
    if ((size_t)w * (size_t)h > cap)
    {
        unsigned char *nm = (unsigned char *)realloc(mask, (size_t)w * (size_t)h);
        if (!nm)
            return;
        mask = nm, cap = (size_t)w * (size_t)h;
    }
    memset(mask, 0, (size_t)w * (size_t)h);
    for (int t = 0; t < n; ++t)
    {
        const float *p    = tri + 6 * t;
        const float  area = (p[2] - p[0]) * (p[5] - p[1]) - (p[4] - p[0]) * (p[3] - p[1]);
        if (area > -1e-6f && area < 1e-6f)
            continue;
        const float sg = area > 0.0f ? 1.0f : -1.0f;
        int         i0 = (int)floorf(fminf(p[0], fminf(p[2], p[4]))) - ox, i1 = (int)ceilf(fmaxf(p[0], fmaxf(p[2], p[4]))) - ox;
        int         j0 = (int)floorf(fminf(p[1], fminf(p[3], p[5]))) - oy, j1 = (int)ceilf(fmaxf(p[1], fmaxf(p[3], p[5]))) - oy;
        i0 = i0 < 0 ? 0 : i0, j0 = j0 < 0 ? 0 : j0, i1 = i1 >= w ? w - 1 : i1, j1 = j1 >= h ? h - 1 : j1;
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i)
            {
                const float x = (float)(ox + i) + 0.5f, y = (float)(oy + j) + 0.5f;
                const float e0 = sg * ((p[2] - p[0]) * (y - p[1]) - (p[3] - p[1]) * (x - p[0]));
                const float e1 = sg * ((p[4] - p[2]) * (y - p[3]) - (p[5] - p[3]) * (x - p[2]));
                const float e2 = sg * ((p[0] - p[4]) * (y - p[5]) - (p[1] - p[5]) * (x - p[4]));
                if (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f)
                    mask[(size_t)j * (size_t)w + (size_t)i] = 1;
            }
    }
    const ImU32 line = OBJ_COLOR[color % RUI_OBJ_COLORS], fill = (line & 0x00FFFFFFu) | ((bright ? 110u : 30u) << 24);
    const auto  on = [&](int i, int j) { return i >= 0 && j >= 0 && i < w && j < h && mask[(size_t)j * (size_t)w + (size_t)i]; };
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w;)
        {
            if (!on(i, j))
            {
                ++i;
                continue;
            }
            int e = i;
            while (e < w && on(e, j))
                ++e;
            dl->AddRectFilled(ImVec2((float)(ox + i), (float)(oy + j)), ImVec2((float)(ox + e), (float)(oy + j + 1)), fill);
            i = e;
        }
    /*  The border: each run of cell edges between a cell inside and one
     *  outside, along the rows and then down the columns. */
    for (int pass = 0; pass < 2; ++pass)
    {
        const ImU32 c  = pass ? line : IM_COL32(0, 0, 0, bright ? 220 : 120);
        const float th = pass ? (bright ? 2.5f : 1.0f) : (bright ? 5.0f : 2.5f);
        for (int j = 0; j <= h; ++j)
            for (int i = 0; i < w;)
            {
                if (on(i, j) == on(i, j - 1))
                {
                    ++i;
                    continue;
                }
                int e = i;
                while (e < w && on(e, j) != on(e, j - 1) && on(e, j) == on(i, j))
                    ++e;
                dl->AddLine(ImVec2((float)(ox + i), (float)(oy + j)), ImVec2((float)(ox + e), (float)(oy + j)), c, th);
                i = e;
            }
        for (int i = 0; i <= w; ++i)
            for (int j = 0; j < h;)
            {
                if (on(i, j) == on(i - 1, j))
                {
                    ++j;
                    continue;
                }
                int e = j;
                while (e < h && on(i, e) != on(i - 1, e) && on(i, e) == on(i, j))
                    ++e;
                dl->AddLine(ImVec2((float)(ox + i), (float)(oy + j)), ImVec2((float)(ox + i), (float)(oy + e)), c, th);
                j = e;
            }
    }
}

/*  Rows of key, TAB, value, one a line, as a table of two columns.  A
 *  line with no tab is a value under the key before it. */
static void key_rows(const char *id, const char *text)
{
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
        return;
    ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("value");
    for (const char *p = text; p && *p;)
    {
        const char *e   = strchr(p, '\n');
        size_t      len = e ? (size_t)(e - p) : strlen(p);
        const char *t   = (const char *)memchr(p, '\t', len);
        char        key[64], val[512];
        if (t)
        {
            snprintf(key, sizeof key, "%.*s", (int)(t - p), p);
            snprintf(val, sizeof val, "%.*s", (int)(len - (size_t)(t - p) - 1u), t + 1);
        }
        else
        {
            key[0] = 0;
            snprintf(val, sizeof val, "%.*s", (int)len, p);
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(key);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("%s", val);
        p = e ? e + 1 : p + len;
    }
    ImGui::EndTable();
}

/*  The road network's record under the pointer, in the inspector.  It
 *  shows the record's levels from the lane up, and a click on one shows
 *  it.  Below them are a box for a remark and what the records say. */
static void inspect_network(RUiState *s)
{
    static const char *const LEVEL[4] = {"lane", "lane section", "road", "junction"};
    ImGui::TextWrapped("%s", s->insp.pinned ? "Selected. Click again for the next road here. Enter: the lane inside. Shift-Enter: the parent. Esc: clear." : "Under the pointer. Click to select it. Enter: the lane inside. Shift-Enter: the parent.");
    for (int k = 0; k < 4; ++k)
    {
        char label[160];
        if (!s->insp.has[k])
            continue;
        snprintf(label, sizeof label, "%-13s %s##lv%d", LEVEL[k], s->insp.name[k], k);
        if (ImGui::Selectable(label, s->insp.level == k))
            s->want.level = 1 + k;
    }
    if (s->insp.ndepth > 1)
        ImGui::Text("%d of %d under the pointer (click again for the next)", s->insp.depth + 1, s->insp.ndepth);
    /*  What each color means: a swatch, then its word. */
    for (int k = 0; k < RUI_OBJ_COLORS; ++k)
    {
        const float sz = ImGui::GetTextLineHeight();
        if (k)
            ImGui::SameLine();
        ImGui::ColorButton(OBJ_WORD[k], ImGui::ColorConvertU32ToFloat4(OBJ_COLOR[k]), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(sz, sz));
        ImGui::SameLine(0.0f, 3.0f);
        ImGui::TextUnformatted(OBJ_WORD[k]);
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("What is wrong with the one shown:");
    /*  The box is no stop for Tab, so a key meant for the map never moves
     *  into it and is taken as typing. */
    ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
    ImGui::InputTextMultiline("##remark", s->insp.remark, sizeof s->insp.remark, ImVec2(-1.0f, ImGui::GetTextLineHeight() * 4.0f));
    ImGui::PopItemFlag();
    ImGui::BeginDisabled(!s->insp.remark[0]);
    if (ImGui::Button("File the remark"))
        s->want.remark = 1;
    ImGui::SameLine();
    if (ImGui::Button("Copy"))
        s->want.remark = 2;
    ImGui::EndDisabled();
    if (s->insp.remark_said[0])
    {
        ImGui::SameLine();
        ImGui::TextWrapped("%s", s->insp.remark_said);
    }
    ImGui::Separator();
    key_rows("network", s->insp.text);
    ImGui::Separator();
    ImGui::TextUnformatted("The mesh under the pointer");
}

/*  The inspector's window: what the mesh under the pointer is, and who
 *  drew it.  Its own window, drawn from ui_frame: it lived inside the
 *  query window's function and so appeared only when that window was
 *  open. */
void inspect_window(RUiState *s)
{
    /*  What the mesh under the pointer is, in a table.
     *
     *      The thing.
     *      The function that drew it.
     *      What asked for it.
     *      Its material.
     *      Its triangles.
     *      The box it occupies.
     *      The generating call's note of the thing.
     *      A row a line.
     *
     *  The width is held: the values wrap, and a window fitted to its
     *  content would never widen for them.  The height follows the rows
     *  to the bottom of the screen and scrolls past it.  It opens at the
     *  top right, clear of the palette and of the middle, where what is
     *  being inspected usually is. */
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    const float          w = 600.0f, margin = 24.0f;
    int                  open = 1;
    if (mac_begin("Inspect", &open, w, ImVec2(vp->WorkPos.x + vp->WorkSize.x - w - margin, vp->WorkPos.y + margin), 0, ImVec2(w, vp->WorkSize.y - 2.0f * margin)))
    {
        if (s->insp.rec_ok)
            inspect_network(s);
        if (!s->insp.who[0])
            ImGui::TextUnformatted("Point at something.");
        else if (ImGui::BeginTable("mesh", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
        {
            char buf[160];
            ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("value");
            const auto row = [&](const char *k, const char *v) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(k);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", v);
            };
            row("component", s->insp.label[0] ? s->insp.label : "-");
            row("rendered by", s->insp.who[0] ? s->insp.who : "-");
            row("written at", s->insp.where[0] ? s->insp.where : "-");
            if (s->insp.part_of[0])
            {
                row("part of", s->insp.part_of);
                row("written at", s->insp.part_of_where[0] ? s->insp.part_of_where : "-");
            }
            snprintf(buf, sizeof buf, "%g", (double)s->insp.mat);
            row("material", buf);
            if (s->insp.part_of_tris > s->insp.tris)
                snprintf(buf, sizeof buf, "%u of the thing's %u", s->insp.tris, s->insp.part_of_tris);
            else
                snprintf(buf, sizeof buf, "%u", s->insp.tris);
            row("triangles", buf);
            snprintf(buf, sizeof buf, "%d,%d", (int)s->q_col, (int)s->q_row);
            row("tile", buf);
            if (s->insp.box_ok)
            {
                snprintf(buf, sizeof buf, "%.2f,%.2f to %.2f,%.2f", (double)s->insp.box[0], (double)s->insp.box[1], (double)s->insp.box[3], (double)s->insp.box[4]);
                row("extent", buf);
                snprintf(buf, sizeof buf, "%.2f to %.2f", (double)s->insp.box[2], (double)s->insp.box[5]);
                row("height", buf);
            }
            /*  The generating call's note, a row a line: the key before
             *  the tab, the value after it. */
            for (const char *p = s->insp.note; *p;)
            {
                const char *e   = strchr(p, '\n');
                size_t      len = e ? (size_t)(e - p) : strlen(p);
                const char *t   = (const char *)memchr(p, '\t', len);
                char        key[64], val[512];
                if (t)
                {
                    snprintf(key, sizeof key, "%.*s", (int)(t - p), p);
                    snprintf(val, sizeof val, "%.*s", (int)(len - (size_t)(t - p) - 1u), t + 1);
                }
                else
                {
                    key[0] = 0;
                    snprintf(val, sizeof val, "%.*s", (int)len, p);
                }
                row(key, val);
                p = e ? e + 1 : p + len;
            }
            /*  What the thing is drawn over.  An entry at the same height
             *  as the thing in front shares the ground with it. */
            for (int k = 0; k < s->insp.n_also; ++k)
                row(k ? "" : "drawn over", s->insp.also[k]);
            ImGui::EndTable();
        }
    }
    ImGui::End();
    if (!open)
        s->want.inspect = 1; /* the close box asks for the same toggle */
}

/*  Every component shown, each outlined whole in its own color: the faint
 *  ones first, the bright ones over them.  The name of the record shown
 *  sits over the pointer. */
void inspect_draw(RUiState *s)
{
    if (s->insp.ngrp <= 0)
        return;
    ImDrawList *dl   = ImGui::GetBackgroundDrawList();
    int         lead = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (int g = 0; g < s->insp.ngrp; ++g)
            if (!s->insp.grp[g][3] == !pass)
            {
                obj_outline(dl, s->insp.tri + 6 * s->insp.grp[g][0], s->insp.grp[g][1], s->insp.grp[g][2], pass);
                lead = pass ? g : lead;
            }
    const char *nm = s->insp.rec_ok ? s->insp.name[s->insp.level >= 0 && s->insp.level < 4 ? s->insp.level : 0] : s->insp.label;
    if (nm && nm[0])
    {
        const ImVec2 at(s->insp.at[0], s->insp.at[1]);
        const float  tsz = 18.0f;
        const ImVec2 sz  = ImGui::GetFont()->CalcTextSizeA(tsz, FLT_MAX, 0.0f, nm);
        ImVec2       tp(at.x - sz.x * 0.5f, at.y - sz.y - 24.0f);
        if (tp.y < 30.0f)
            tp.y = at.y + 24.0f;
        dl->AddRectFilled(ImVec2(tp.x - 4.0f, tp.y - 2.0f), ImVec2(tp.x + sz.x + 4.0f, tp.y + sz.y + 2.0f), IM_COL32(18, 22, 24, 220), 3.0f);
        dl->AddText(ImGui::GetFont(), tsz, tp, OBJ_COLOR[s->insp.grp[lead][2] % RUI_OBJ_COLORS], nm);
    }
}
