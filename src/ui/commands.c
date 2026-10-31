/*  commands.c: the commands a menu item and a key both reach, one row a
 *  command.  Both builds of the interface use them, with Dear ImGui and
 *  without it. */
#include "ui.h"

#include <ctype.h>
#include <stddef.h>

#define FLAG(label, key, shift, field)   {label, key, shift, 0, offsetof(RUiState, field), 0, 0}
#define TOGGLE(label, key, shift, field) {label, key, shift, 1, offsetof(RUiState, field), 1, offsetof(RUiState, field)}

static const RUiCommand COMMAND[RUI_N_CMD] = {
    [RUI_CMD_LOAD]        = FLAG("Load City", 'L', 0, open_load),
    [RUI_CMD_SAVE]        = FLAG("Save City", 'S', 0, want.save),
    [RUI_CMD_QUIT]        = FLAG("Quit", 'Q', 0, want.quit),
    [RUI_CMD_BUDGET]      = FLAG("Budget", 'B', 0, show_budget),
    [RUI_CMD_POPULATION]  = FLAG("Population", 'C', 0, show_city),
    [RUI_CMD_GRAPHS]      = FLAG("Graphs", 'G', 0, show_graphs),
    [RUI_CMD_GEOMETRY]    = TOGGLE("Geometry", 'T', 1, geometry),
    [RUI_CMD_PLAN]        = TOGGLE("Map view", 'N', 1, plan),
    [RUI_CMD_UNDERGROUND] = TOGGLE("Underground", 'U', 1, underground),
    [RUI_CMD_SCREENSHOT]  = FLAG("Screenshot", 'P', 1, want.screenshot),
    [RUI_CMD_GRID]        = TOGGLE("Tile grid", 'G', 1, grid),
    [RUI_CMD_OUTLINE]     = TOGGLE("Lane graph", 'O', 1, outline),
    [RUI_CMD_TRIANGLES]   = TOGGLE("Mesh triangles", 'W', 1, tris),
    [RUI_CMD_NO_DEPTH]    = TOGGLE("Draw in list order (no depth test)", 'M', 1, plain_sweep),
    /*  A request the app reads and clears.  The check is whether the
     *  inspector is up. */
    [RUI_CMD_INSPECT] = {"Inspect", 'I', 1, 0, offsetof(RUiState, want.inspect), 1, offsetof(RUiState, insp.show)},
};

const RUiCommand *ui_command(int cmd)
{
    return cmd >= 0 && cmd < RUI_N_CMD ? &COMMAND[cmd] : NULL;
}

int ui_command_key(char key, int shift)
{
    int k;
    for (k = 0; k < RUI_N_CMD; ++k)
        if (COMMAND[k].key && COMMAND[k].key == toupper((unsigned char)key) && !COMMAND[k].shift == !shift)
            return k;
    return -1;
}

void ui_command_do(RUiState *s, int cmd)
{
    const RUiCommand *c = ui_command(cmd);
    int              *f;
    if (!c)
        return;
    f  = (int *)(void *)((char *)s + c->field);
    *f = c->toggle ? !*f : 1;
}

int ui_command_shown(const RUiState *s, int cmd)
{
    const RUiCommand *c = ui_command(cmd);
    return c && c->checks ? *(const int *)(const void *)((const char *)s + c->shown) != 0 : 0;
}
