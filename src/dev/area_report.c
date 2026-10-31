/*  dev/area_report.c: the debug report of an area.  It says what the city holds
 *  on every tile of the area, as a map of two-letter marks with a
 *  legend. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "area_report.h"
#include "dump.h"
#include "mesh/mesh.h"
#include "terrain.h"

typedef struct
{
    char  *s;
    size_t n, cap;
} Text;

static int text_printf(Text *t, const char *fmt, ...)
{
    va_list ap;
    int     need;
    va_start(ap, fmt);
    need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0)
        return -1;
    if (t->n + (size_t)need + 1 > t->cap)
    {
        size_t cap = t->cap ? t->cap : 4096;
        char  *s;
        while (cap < t->n + (size_t)need + 1)
            cap *= 2;
        s = (char *)realloc(t->s, cap);
        if (!s)
            return -1;
        t->s   = s;
        t->cap = cap;
    }
    va_start(ap, fmt);
    vsnprintf(t->s + t->n, t->cap - t->n, fmt, ap);
    va_end(ap);
    t->n += (size_t)need;
    return 0;
}

/*  The mark a byte carries by what stands on the tile. */
static const char *mark_of(uint8_t b)
{
    if (b == 0x00)
        return "..";
    if (b >= 0x1D && b <= 0x2B)
        return "rr";
    if (b >= 0x2C && b <= 0x3A)
        return "tt";
    if (b >= 0x3B && b <= 0x42)
        return "pp";
    if (b >= 0x43 && b <= 0x48)
        return "xx";
    if (b >= 0x6C)
        return "bb";
    return ",,";
}

/*  A tile's two-letter mark.  A highway piece carries its own id, which
 *  stands whatever the terrain is, so that one highway piece can be told
 *  from another.  Water comes next, which is how water shows through what
 *  stands on it.  Then what stands there. */
static void tile_mark(const RCity *c, int32_t col, int32_t row, char out[3])
{
    int32_t at = row * r_map + col;
    uint8_t b  = c->xbld[at];
    if (b >= 0x49 && b <= 0x6B)
        snprintf(out, 3, "%02x", b);
    else if (c->map.xter[at] >= 0x10)
        snprintf(out, 3, "~~");
    else
        snprintf(out, 3, "%s", mark_of(b));
}

/*  And the word for a byte. */
static const char *tile_kind(uint8_t b)
{
    if (b == 0x00)
        return "ground";
    if (b <= 0x1C)
        return "trees or rubble";
    if (b <= 0x2B)
        return "line";
    if (b <= 0x3A)
        return "thread";
    if (b <= 0x42)
        return "power line";
    if (b <= 0x48)
        return "level meet";
    if (b == 0x49)
        return "band straight, along its row";
    if (b == 0x4A)
        return "band straight, along its column";
    if (b <= 0x50)
        return "band over a line or thread";
    if (b <= 0x5C)
        return "band piece";
    if (b <= 0x60)
        return "on-spur";
    if (b <= 0x64)
        return "band section";
    if (b <= 0x68)
        return "band curve block";
    if (b == 0x69)
        return "band interchange";
    if (b <= 0x6B)
        return "band piece";
    return "building";
}

int net_area_report(const RCity *c, int32_t c0, int32_t r0, int32_t c1, int32_t r1, char **out)
{
    Text    t = {NULL, 0, 0};
    int32_t col, row, tmp;
    char    buf[200], mark[3];
    if (!c || !out)
        return -1;
    if (c0 > c1)
        tmp = c0, c0 = c1, c1 = tmp;
    if (r0 > r1)
        tmp = r0, r0 = r1, r1 = tmp;
    c0 = c0 < 0 ? 0 : c0 > r_map - 1 ? r_map - 1
                                     : c0;
    c1 = c1 < 0 ? 0 : c1 > r_map - 1 ? r_map - 1
                                     : c1;
    r0 = r0 < 0 ? 0 : r0 > r_map - 1 ? r_map - 1
                                     : r0;
    r1 = r1 < 0 ? 0 : r1 > r_map - 1 ? r_map - 1
                                     : r1;
    text_printf(&t, "area  %s  cols %d..%d  rows %d..%d  (%d x %d tiles)\n", c->name[0] ? c->name : "(unnamed)", (int)c0, (int)c1, (int)r0, (int)r1, (int)(c1 - c0 + 1), (int)(r1 - r0 + 1));
    text_printf(&t, "  north is decreasing row, east is decreasing col; a label is col,row\n");
    /*  The map of marks. */
    /*  The legend, built from the same tables the marks come from: each
     *  mark once, with the word of the first byte that carries it.  A
     *  list written out here would be a second opinion about what a
     *  mark stands for. */
    text_printf(&t, "tiles  (");
    {
        const char *said[16];
        int         n_said = 0, byte, seen;
        for (byte = 0; byte < 512; ++byte)
        {
            /*  The terrain's marks first, then what stands on a tile,
             *  which is the order a mark is settled in. */
            const char *m = byte < 256 ? (byte >= 0x10 ? "~~" : NULL) : mark_of((uint8_t)(byte - 256));
            if (!m)
                continue;
            for (seen = 0; seen < n_said; ++seen)
                if (strcmp(said[seen], m) == 0)
                    break;
            if (seen < n_said || n_said >= (int)(sizeof said / sizeof said[0]))
                continue;
            said[n_said++] = m;
            text_printf(&t, "%s%s %s", n_said > 1 ? "  " : "", m, byte < 256 ? "water" : tile_kind((uint8_t)(byte - 256)));
        }
    }
    text_printf(&t, "; a band piece by its id)\n     ");
    for (col = c0; col <= c1; ++col)
        text_printf(&t, " %3d", (int)col);
    text_printf(&t, "\n");
    for (row = r0; row <= r1; ++row)
    {
        text_printf(&t, "  %3d", (int)row);
        for (col = c0; col <= c1; ++col)
        {
            tile_mark(c, col, row, mark);
            text_printf(&t, "  %s", mark);
        }
        text_printf(&t, "\n");
    }
    /*  What stands there, tile by tile, with the mesh's word on it. */
    text_printf(&t, "what stands there (every tile that is not plain ground)\n");
    for (row = r0; row <= r1; ++row)
        for (col = c0; col <= c1; ++col)
        {
            int32_t idx = row * r_map + col;
            uint8_t b   = c->xbld[idx];
            if (b == 0 && !is_water(c->map.xter[idx]))
                continue;
            buf[0] = 0;
            mesh_query(c, col, row, buf, sizeof buf);
            text_printf(&t, "  %3d,%-3d xbld 0x%02x %-30s alt %2d  xter 0x%02x  %s\n", (int)col, (int)row, (unsigned)b, tile_kind(b), (int)rcity_alt_ground(c->map.altm[idx]), (unsigned)c->map.xter[idx], buf);
        }
    text_printf(&t, "end of area\n");
    *out = t.s;
    return t.s ? 0 : -1;
}
