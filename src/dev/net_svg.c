/*  dev/net_svg.c: THE ROAD NETWORK AS A DIAGRAM, written to an SVG file.
 *  The diagram is the lane graph's layout (net_graph.c), the same layout
 *  the outline view draws in the world, with every node a dot.  The file
 *  holds the network and a margin of two tiles round it, or the tiles of
 *  one box alone, drawn larger.  A key to the colors goes with it. */
#include <math.h>
#include <stdio.h>

#include "draw/net_graph.h"
#include "log.h"
#include "net_svg.h"
#include "network/network.h"
#include "network/topology.h"

#define PX_WHOLE 40.0f  /* pixels a tile, for the whole network */
#define PX_BOX   110.0f /* pixels a tile, for one box of tiles */

/*  A mark's color as SVG writes it. */
static const char *svg_rgb(int k)
{
    static char hex[NG_COLORS][8];
    snprintf(hex[k], sizeof hex[k], "#%06x", (unsigned)NG_RGB[k]);
    return hex[k];
}

typedef struct
{
    FILE *f;
    float x0, y0, x1, y1;
    float px;  /* pixels a tile */
    int   box; /* only what lies in the box is written */
} Svg;

static void grow(Svg *s, NgP p)
{
    s->x0 = fminf(s->x0, p.x), s->y0 = fminf(s->y0, p.y);
    s->x1 = fmaxf(s->x1, p.x), s->y1 = fmaxf(s->y1, p.y);
}

static int inside(const Svg *s, NgP p)
{
    return !s->box || (p.x >= s->x0 - 1.0f && p.x <= s->x1 + 1.0f && p.y >= s->y0 - 1.0f && p.y <= s->y1 + 1.0f);
}

static int extent_line(void *u, const char *what, const NgP *p, int n, float half, int color)
{
    int i;
    (void)what, (void)half, (void)color;
    for (i = 0; i < n; i++)
        grow((Svg *)u, p[i]);
    return 0;
}

static int extent_arrow(void *u, NgP at, NgP dir, float size, int color)
{
    (void)dir, (void)size, (void)color;
    grow((Svg *)u, at);
    return 0;
}

static int svg_line(void *u, const char *what, const NgP *p, int n, float half, int color)
{
    Svg  *s = (Svg *)u;
    float w = 2.0f * half * s->px;
    int   i, any = 0;
    (void)what;
    for (i = 0; i < n && !any; i++)
        any = inside(s, p[i]);
    if (!any)
        return 0;
    fprintf(s->f, "<polyline fill=\"none\" stroke=\"%s\" stroke-width=\"%.2f\" stroke-linejoin=\"round\" points=\"", svg_rgb(color), (double)(w < 1.2f ? 1.2f : w));
    for (i = 0; i < n; i++)
        fprintf(s->f, "%.1f,%.1f ", (double)(p[i].x * s->px), (double)(p[i].y * s->px));
    fprintf(s->f, "\"/>\n");
    return 0;
}

/*  An arrow: a triangle from its base, across the line, to its point. */
static int svg_arrow(void *u, NgP at, NgP d, float size, int color)
{
    Svg  *s = (Svg *)u;
    float h = size * 0.5f, w = size * 0.6f, px = s->px;
    if (!inside(s, at))
        return 0;
    fprintf(s->f, "<polygon fill=\"%s\" points=\"%.1f,%.1f %.1f,%.1f %.1f,%.1f\"/>\n", svg_rgb(color), (double)((at.x - d.x * h + d.y * w) * px), (double)((at.y - d.y * h - d.x * w) * px), (double)((at.x - d.x * h - d.y * w) * px), (double)((at.y - d.y * h + d.x * w) * px), (double)((at.x + d.x * h) * px), (double)((at.y + d.y * h) * px));
    return 0;
}

/*  A node: a dot, ringed darker inside a junction. */
static int svg_node(void *u, NgP at, int in_junction)
{
    Svg *s = (Svg *)u;
    if (!inside(s, at))
        return 0;
    fprintf(s->f, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\" fill=\"#ffd24d\" stroke=\"%s\" stroke-width=\"%.1f\"/>\n", (double)(at.x * s->px), (double)(at.y * s->px), (double)(0.035f * s->px), in_junction ? "#7a4b00" : "#4a4a4a", (double)(in_junction ? 0.012f * s->px : 0.006f * s->px));
    return 0;
}

int network_svg(const char *path, const char *box)
{
    int32_t side = topology_side();
    Svg     s    = {NULL, 0.0f, 0.0f, 0.0f, 0.0f, PX_WHOLE, 0};
    NgSink  k    = {&s, extent_line, extent_arrow, NULL, 1};
    int     unplaced, c0, r0, c1, r1;
    if (side <= 0 || !(s.f = fopen(path, "w")))
    {
        R_ERR("net", "%s: the network diagram could not be written", path);
        return -1;
    }
    if (box && sscanf(box, "%d,%d,%d,%d", &c0, &r0, &c1, &r1) == 4)
    {
        s.box = 1, s.px = PX_BOX;
        s.x0 = (float)c0, s.y0 = (float)r0, s.x1 = (float)c1 + 1.0f, s.y1 = (float)r1 + 1.0f;
    }
    else
    {
        s.x0 = s.y0 = (float)side, s.x1 = s.y1 = 0.0f;
        if (net_graph_draw(&k) < 0)
            goto fail;
        if (s.x1 < s.x0)
            s.x0 = s.y0 = 0.0f, s.x1 = s.y1 = (float)side;
        s.x0 = fmaxf(s.x0 - 2.0f, 0.0f), s.y0 = fmaxf(s.y0 - 2.0f, 0.0f);
        s.x1 = fminf(s.x1 + 2.0f, (float)side), s.y1 = fminf(s.y1 + 2.0f, (float)side);
    }
    fprintf(s.f, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"%.0f %.0f %.0f %.0f\">\n", (int)((s.x1 - s.x0) * s.px), (int)((s.y1 - s.y0) * s.px), (double)(s.x0 * s.px), (double)(s.y0 * s.px), (double)((s.x1 - s.x0) * s.px), (double)((s.y1 - s.y0) * s.px));
    fprintf(s.f, "<rect x=\"%.0f\" y=\"%.0f\" width=\"%.0f\" height=\"%.0f\" fill=\"#ffffff\"/>\n", (double)(s.x0 * s.px), (double)(s.y0 * s.px), (double)((s.x1 - s.x0) * s.px), (double)((s.y1 - s.y0) * s.px));
    if (s.box)
    {
        /*  The cell grid, faint, so a node can be read against its cell. */
        int c, r;
        for (c = c0; c <= c1 + 1; c++)
            fprintf(s.f, "<line x1=\"%.0f\" y1=\"%.0f\" x2=\"%.0f\" y2=\"%.0f\" stroke=\"#ececec\" stroke-width=\"1\"/>\n", (double)((float)c * s.px), (double)(s.y0 * s.px), (double)((float)c * s.px), (double)(s.y1 * s.px));
        for (r = r0; r <= r1 + 1; r++)
            fprintf(s.f, "<line x1=\"%.0f\" y1=\"%.0f\" x2=\"%.0f\" y2=\"%.0f\" stroke=\"#ececec\" stroke-width=\"1\"/>\n", (double)(s.x0 * s.px), (double)((float)r * s.px), (double)(s.x1 * s.px), (double)((float)r * s.px));
    }
    k.line = svg_line, k.arrow = svg_arrow, k.node = svg_node;
    unplaced = net_graph_draw(&k);
    if (unplaced < 0)
        goto fail;
    if (!s.box)
    {
        int i;
        fprintf(s.f, "<g transform=\"translate(%.0f %.0f)\" font-family=\"Helvetica\" font-size=\"14\">\n"
                     "<rect x=\"8\" y=\"8\" width=\"230\" height=\"202\" fill=\"#ffffff\" stroke=\"#909090\"/>\n",
                (double)(s.x0 * s.px),
                (double)(s.y0 * s.px));
        for (i = 0; i < NG_COLORS; i++)
            fprintf(s.f, "<text x=\"16\" y=\"%d\" fill=\"%s\">%s</text>\n", 28 + 18 * i, svg_rgb(i), NG_NAME[i]);
        fprintf(s.f, "</g>\n");
    }
    fprintf(s.f, "</svg>\n");
    fclose(s.f);
    R_NOTE("net", "%s: the road network, %d roads, %d lanes and %d nodes (%d lanes not placed)", path, network_roads(), network_lanes(), network_nodes(), unplaced);
    return 0;
fail:
    fclose(s.f);
    R_ERR("net", "%s: out of memory", path);
    return -1;
}
