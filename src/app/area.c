/*  The area report.  The map view's Shift-drag selects the tiles (app.c);
 *  on release the report goes to debug/<city>-<area>.txt under the working
 *  directory and to the clipboard, and the Messages window says so. --area
 *  C0,R0-C1,R1 makes the same report headless, to the dump sink, so it can
 *  be made again from the command line. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "dump.h"
#include "internal.h"
#include "mesh/mesh.h"
#include "net/report.h"

static int area_text(App *a, int32_t c0, int32_t r0, int32_t c1, int32_t r1, char **text)
{
    if (!a->view)
        return -1;
    return net_area_report(a->view, c0, r0, c1, r1, text);
}

void area_report_ui(App *a)
{
    char   *text = NULL, path[256];
    int32_t c0 = a->sel_c0 < a->sel_c1 ? a->sel_c0 : a->sel_c1, c1 = a->sel_c0 < a->sel_c1 ? a->sel_c1 : a->sel_c0;
    int32_t r0 = a->sel_r0 < a->sel_r1 ? a->sel_r0 : a->sel_r1, r1 = a->sel_r0 < a->sel_r1 ? a->sel_r1 : a->sel_r0;
    FILE   *fp;
    if (a->sel_c0 < 0 || a->sel_c1 < 0)
        return;
    if (area_text(a, c0, r0, c1, r1, &text) != 0 || !text)
    {
        ui_log(&a->us, "Area %d,%d to %d,%d: no report (the networks are not built)", (int)c0, (int)r0, (int)c1, (int)r1);
        return;
    }
    mkdir("debug", 0755);
    snprintf(path, sizeof path, "debug/%s-%d,%d-%d,%d.txt", a->city_base[0] ? a->city_base : "city", (int)c0, (int)r0, (int)c1, (int)r1);
    fp = fopen(path, "w");
    if (fp)
    {
        fputs(text, fp);
        fclose(fp);
    }
    SDL_SetClipboardText(text);
    ui_log(&a->us, "Area %d,%d to %d,%d (%d x %d): %s%s, and copied to the clipboard", (int)c0, (int)r0, (int)c1, (int)r1, (int)(c1 - c0 + 1), (int)(r1 - r0 + 1), fp ? "written to " : "could not write ", path);
    a->sel_flash_until = SDL_GetTicksNS() + 480000000ull; /* two blinks, then gone; the message is the log's */
    free(text);
}

int area_report_cli(App *a, const char *spec)
{
    int   c0, r0, c1, r1;
    char *text = NULL;
    if (!spec || sscanf(spec, "%d,%d-%d,%d", &c0, &r0, &c1, &r1) != 4)
    {
        fprintf(stderr, "--area wants C0,R0-C1,R1\n");
        return -1;
    }
    if (area_text(a, c0, r0, c1, r1, &text) != 0 || !text)
        return -1;
    dumpf("%s", text);
    {
        /*  And who drew the mesh on each tile of the area: the inspector
         *  shows the same for the tile under the pointer. */
        int32_t cc, rr;
        for (rr = r0; rr <= r1; ++rr)
            for (cc = c0; cc <= c1; ++cc)
            {
                char  who[1024], label[120];
                static float pts[512];
                int   np = 0;
                if (a->view && a->sw.level && net_component_at(a->view, a->sw.level, cc, rr, pts, 256, &np, label, sizeof label) == 0)
                    dumpf("the component at %d,%d: %s, outlined by %d points\n", (int)cc, (int)rr, label, np);
                if (mesh_origins(cc, rr, who, sizeof who) > 0)
                    dumpf("what drew the mesh on %d,%d\n%s", (int)cc, (int)rr, who);
            }
    }
    free(text);
    return 0;
}
