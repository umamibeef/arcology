/*  shape.c: the shape table: what each shape is, what it is part of, and
 *  which triangles are its own.  See mesh/shape.h.  Nothing here draws.
 *  The emitter files its triangles through shape_claim. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "city.h"
#include "dump.h"
#include "hash.h"
#include "mesh/internal.h"
#include "mesh/shape.h"

#define SHAPE_MAX_128 262144
#define SHAPE_MAX     r_scaled(SHAPE_MAX_128)
#define SHAPE_STACK   32

typedef struct
{
    ShapeId     parent;
    uint8_t     depth;
    const char *where, *who; /* where it was opened, from the macro */
    uint32_t    name;        /* 1 + an offset into the arena, 0 for none */
    uint32_t    note;
    int         ref_kind; /* the record it draws, as its producer names it: 0 for none */
    uint64_t    ref_key;
    uint32_t    tile0; /* its tiles in s_tile, 1 + the first, 0 for none */
    uint32_t    ntile;
    uint8_t     span; /* the side of each of its tiles' units, in cells */
    int8_t      net;  /* 1 + the network it draws, whose nearest cell a triangle belongs to, or 0 */
    float       mat;  /* its material, or -1 where it draws in more than one */
    uint32_t    tris;
    float       box[6];
} Shape;

static Shape       *s_shape;
static uint32_t     s_n, s_cap;
static char        *s_text; /* the names and notes, end to end */
static uint32_t     s_text_n, s_text_cap;
static int32_t     *s_tile; /* the shapes' tiles, end to end, for the build under way */
static uint32_t     s_tile_n, s_tile_cap;
static const RCity *s_city;         /* the city the build under way lays */
#define SHAPE_NET_NONE ((int8_t)-1) /* a shape's `net` where it belongs to no tile (shape_unmarked) */
static ShapeId s_stack[SHAPE_STACK];
static int     s_sp;
static int     s_on;

void mesh_box_grow(float b[6], const float p[3][3])
{
    int k, a;
    for (k = 0; k < 3; ++k)
        for (a = 0; a < 3; ++a)
        {
            if (p[k][a] < b[a])
                b[a] = p[k][a];
            if (p[k][a] > b[3 + a])
                b[3 + a] = p[k][a];
        }
}

/*  The call sites that emitted a triangle with no shape open.  Kept by
 *  site so the report can name them rather than only count. */
#define UNCLAIMED_MAX 64
static struct
{
    const char *where, *who;
    uint32_t    tris;
} s_unclaimed[UNCLAIMED_MAX];
static int      s_n_unclaimed;
static uint32_t s_unclaimed_tris, s_unclaimed_lost;

static uint32_t text_add(const char *s)
{
    size_t len;
    if (!s || !*s)
        return 0;
    len = strlen(s) + 1;
    if (s_text_n + len > s_text_cap)
    {
        uint32_t cap = s_text_cap ? s_text_cap : 65536u;
        char    *nb;
        while (cap < s_text_n + len)
            cap *= 2u;
        nb = (char *)realloc(s_text, cap);
        if (!nb)
            return 0;
        s_text     = nb;
        s_text_cap = cap;
    }
    memcpy(s_text + s_text_n, s, len);
    s_text_n += (uint32_t)len;
    return s_text_n - (uint32_t)len + 1u;
}

static const char *text_get(uint32_t at)
{
    return at ? s_text + (at - 1u) : NULL;
}

void shape_reset(void)
{
    s_n              = 0;
    s_text_n         = 0;
    s_tile_n         = 0;
    s_sp             = 0;
    s_n_unclaimed    = 0;
    s_unclaimed_tris = 0;
    s_unclaimed_lost = 0;
    memset(s_unclaimed, 0, sizeof s_unclaimed);
}

void shape_record(int on)
{
    s_on = on;
    if (!on)
        s_sp = 0; /* a build that ended leaves nothing open */
}

static ShapeId shape_current(void)
{
    return s_sp > 0 ? s_stack[s_sp - 1] : SHAPE_NONE;
}

static ShapeId shape_open_v(ShapeId parent, const char *where, const char *who, const char *fmt, va_list ap);

ShapeId shape_open_at(const char *where, const char *who, const char *fmt, ...)
{
    va_list ap;
    ShapeId id;
    va_start(ap, fmt);
    id = shape_open_v(shape_current(), where, who, fmt, ap);
    va_end(ap);
    return id;
}

static ShapeId shape_open_v(ShapeId parent, const char *where, const char *who, const char *fmt, va_list ap)
{
    char   name[160];
    Shape *sh;
    if (!s_on)
        return SHAPE_NONE;
    if (s_n >= s_cap)
    {
        uint32_t cap = s_cap ? s_cap * 2u : 8192u;
        Shape   *ns;
        if (cap > (uint32_t)SHAPE_MAX)
            cap = (uint32_t)SHAPE_MAX;
        if (s_n >= cap)
            return SHAPE_NONE;
        ns = (Shape *)realloc(s_shape, (size_t)cap * sizeof *ns);
        if (!ns)
            return SHAPE_NONE;
        s_shape = ns;
        s_cap   = cap;
    }
    vsnprintf(name, sizeof name, fmt, ap);
    sh           = &s_shape[s_n];
    sh->parent   = parent;
    sh->depth    = (uint8_t)(s_sp < SHAPE_STACK ? s_sp : SHAPE_STACK - 1);
    sh->where    = where;
    sh->who      = who;
    sh->name     = text_add(name);
    sh->note     = 0;
    sh->ref_kind = 0;
    sh->ref_key  = 0;
    sh->tile0    = 0;
    sh->ntile    = 0;
    sh->span     = 1;
    sh->net      = 0;
    sh->mat      = -2.0f; /* nothing drawn yet */
    sh->tris     = 0;
    sh->box[0] = sh->box[1] = sh->box[2] = 1e9f;
    sh->box[3] = sh->box[4] = sh->box[5] = -1e9f;
    if (s_sp < SHAPE_STACK)
        s_stack[s_sp] = s_n;
    ++s_sp;
    return s_n++;
}

void shape_close(ShapeId id)
{
    if (!s_on || s_sp <= 0 || id == SHAPE_NONE)
        return; /* a shape never opened is never closed */
    /*  Closed in the order they were opened.  A producer that closes the
     *  wrong one would leave the stack askew.  Everything after it would
     *  then be filed under a shape that has finished, so unwind to the
     *  one it names. */
    while (s_sp > 0)
    {
        int last = s_sp - 1;
        --s_sp;
        if (last < SHAPE_STACK && s_stack[last] == id)
            break;
        if (id == SHAPE_NONE)
            break;
    }
}

void shape_enter(ShapeId id)
{
    if (!s_on || id == SHAPE_NONE || id >= s_n)
        return;
    if (s_sp < SHAPE_STACK)
        s_stack[s_sp] = id;
    ++s_sp;
}

void shape_note(const char *fmt, ...)
{
    char    buf[1024];
    va_list ap;
    ShapeId id = shape_current();
    if (!s_on || id == SHAPE_NONE || id >= s_n)
        return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    s_shape[id].note = text_add(buf);
}

void shape_ref(int kind, uint64_t key)
{
    ShapeId id = shape_current();
    if (!s_on || id == SHAPE_NONE || id >= s_n)
        return;
    s_shape[id].ref_kind = kind;
    s_shape[id].ref_key  = key;
}

ShapeId shape_ref_of(ShapeId id, int *kind, uint64_t *key)
{
    int depth = 0;
    while (id != SHAPE_NONE && id < s_n && depth++ < SHAPE_STACK)
    {
        if (s_shape[id].ref_kind)
        {
            *kind = s_shape[id].ref_kind;
            *key  = s_shape[id].ref_key;
            return id;
        }
        id = s_shape[id].parent;
    }
    return SHAPE_NONE;
}

void shape_tiles(const int32_t *cells, int n, int span)
{
    ShapeId id = shape_current();
    if (!s_on || id == SHAPE_NONE || id >= s_n || n <= 0)
        return;
    if (s_tile_n + (uint32_t)n > s_tile_cap)
    {
        uint32_t cap = s_tile_cap ? s_tile_cap : 65536u;
        int32_t *nt;
        while (cap < s_tile_n + (uint32_t)n)
            cap *= 2u;
        nt = (int32_t *)realloc(s_tile, (size_t)cap * sizeof *nt);
        if (!nt)
            return;
        s_tile     = nt;
        s_tile_cap = cap;
    }
    memcpy(s_tile + s_tile_n, cells, (size_t)n * sizeof *cells);
    s_shape[id].tile0 = 1u + s_tile_n;
    s_shape[id].ntile = (uint32_t)n;
    s_shape[id].span  = (uint8_t)(span > 1 ? span : 1);
    s_tile_n += (uint32_t)n;
}

void shape_unmarked(void)
{
    ShapeId id = shape_current();
    if (!s_on || id == SHAPE_NONE || id >= s_n)
        return;
    s_shape[id].net = SHAPE_NET_NONE;
}

void shape_city(const RCity *c)
{
    s_city = c;
}

/*  The cell nearest the point (x, y) that carries a piece of network
 *  `net` (RNET_*), or -1 for none.  The search reaches NET_CELL_REACH
 *  cells. */
#define NET_CELL_REACH 4
static int32_t net_cell(int net, float x, float y)
{
    const RCity *c  = s_city;
    int32_t      cx = (int32_t)floorf(x), cy = (int32_t)floorf(y), best = -1;
    float        bd   = 1e30f;
    int          ring = 0;
    if (!c)
        return -1;
    /*  Ring by ring out from the cell under the point.  A cell of ring r
     *  lies at least r - 1 cells away, so a ring past the best is not
     *  read. */
    for (ring = 0; ring <= NET_CELL_REACH && (float)(ring - 1) * (float)(ring - 1) < bd; ++ring)
    {
        int32_t dc, dr;
        for (dr = -ring; dr <= ring; ++dr)
            for (dc = -ring; dc <= ring; ++dc)
            {
                int32_t col = cx + dc, row = cy + dr, idx;
                RPart   part[2];
                int     n, k, has = 0;
                float   ex, ey, d;
                if ((dc != -ring && dc != ring && dr != -ring && dr != ring) || col < 0 || row < 0 || col >= r_map || row >= r_map)
                    continue;
                idx = row * r_map + col;
                n   = rcity_parts(c->xbld[idx], c->xbit[idx], part);
                for (k = 0; k < n; ++k)
                    has |= part[k].net == net;
                if (!has)
                    continue;
                ex = x<(float)col ? (float)col - x : x>(float) col + 1.0f ? x - (float)col - 1.0f : 0.0f;
                ey = y<(float)row ? (float)row - y : y>(float) row + 1.0f ? y - (float)row - 1.0f : 0.0f;
                d  = ex * ex + ey * ey;
                if (d < bd)
                    bd = d, best = idx;
            }
    }
    return best;
}

void shape_net(int net)
{
    ShapeId id = shape_current();
    if (!s_on || id == SHAPE_NONE || id >= s_n)
        return;
    s_shape[id].net = (int8_t)(net + 1);
}

int32_t shape_tile_of(ShapeId id, const float p[3][3])
{
    int depth = 0;
    while (id != SHAPE_NONE && id < s_n && depth++ < SHAPE_STACK && !s_shape[id].tile0 && !s_shape[id].net)
        id = s_shape[id].parent;
    if (id != SHAPE_NONE && id < s_n && s_shape[id].net == SHAPE_NET_NONE)
        return SHAPE_TILE_NONE;
    if (id != SHAPE_NONE && id < s_n && s_shape[id].net)
        return net_cell(s_shape[id].net - 1, (p[0][0] + p[1][0] + p[2][0]) / 3.0f, (p[0][1] + p[1][1] + p[2][1]) / 3.0f);
    if (id == SHAPE_NONE || id >= s_n || !s_shape[id].tile0 || s_shape[id].tile0 - 1u + s_shape[id].ntile > s_tile_n)
        return -1;
    {
        /*  The nearest of the shape's tiles to the triangle's middle, by
         *  the distance from the middle to the tile's square.  Within a
         *  unit of several cells, the cell the middle lies over. */
        const Shape   *sh   = &s_shape[id];
        const int32_t *cell = s_tile + (sh->tile0 - 1u);
        const float    mx = (p[0][0] + p[1][0] + p[2][0]) / 3.0f, my = (p[0][1] + p[1][1] + p[2][1]) / 3.0f;
        const float    span = (float)sh->span;
        float          best = 1e30f;
        int32_t        tile = -1;
        uint32_t       i;
        for (i = 0; i < sh->ntile; ++i)
        {
            float c0 = (float)(cell[i] % r_map), r0 = (float)(cell[i] / r_map);
            float dx = mx < c0 ? c0 - mx : mx > c0 + span ? mx - c0 - span
                                                          : 0.0f;
            float dy = my < r0 ? r0 - my : my > r0 + span ? my - r0 - span
                                                          : 0.0f;
            float d  = dx * dx + dy * dy;
            if (d < best)
            {
                float cx = floorf(mx < c0 ? c0 : mx >= c0 + span ? c0 + span - 1.0f
                                                                 : mx);
                float cy = floorf(my < r0 ? r0 : my >= r0 + span ? r0 + span - 1.0f
                                                                 : my);
                best     = d;
                tile     = (int32_t)cy * r_map + (int32_t)cx;
                if (d <= 0.0f)
                    break;
            }
        }
        return tile;
    }
}

static void unclaimed_note(const char *where, const char *who)
{
    int i;
    ++s_unclaimed_tris;
    for (i = 0; i < s_n_unclaimed; ++i)
        if (s_unclaimed[i].where == where && s_unclaimed[i].who == who)
        {
            ++s_unclaimed[i].tris;
            return;
        }
    if (s_n_unclaimed >= UNCLAIMED_MAX)
    {
        ++s_unclaimed_lost;
        return;
    }
    s_unclaimed[s_n_unclaimed].where = where;
    s_unclaimed[s_n_unclaimed].who   = who;
    s_unclaimed[s_n_unclaimed].tris  = 1;
    ++s_n_unclaimed;
}

ShapeId shape_claim(const char *where, const char *who, const float p[3][3], float mat)
{
    ShapeId id = shape_current();
    Shape  *sh;
    if (!s_on)
        return SHAPE_NONE;
    if (id == SHAPE_NONE || id >= s_n)
    {
        unclaimed_note(where, who);
        return SHAPE_NONE;
    }
    sh = &s_shape[id];
    if (sh->mat < -1.5f)
        sh->mat = mat;
    else if (sh->mat != mat)
        sh->mat = -1.0f; /* more than one material: it is a shape of parts */
    ++sh->tris;
    mesh_box_grow(sh->box, p);
    return id;
}

int shape_count(void)
{
    return (int)s_n;
}

const char *shape_name(ShapeId id)
{
    return id < s_n ? text_get(s_shape[id].name) : NULL;
}

const char *shape_note_of(ShapeId id)
{
    return id < s_n ? text_get(s_shape[id].note) : NULL;
}

const char *shape_where(ShapeId id)
{
    return id < s_n ? s_shape[id].where : NULL;
}

const char *shape_who(ShapeId id)
{
    return id < s_n ? s_shape[id].who : NULL;
}

ShapeId shape_parent(ShapeId id)
{
    return id < s_n ? s_shape[id].parent : SHAPE_NONE;
}

int shape_get(ShapeId id, float *mat, uint32_t *tris, float box[6])
{
    if (id >= s_n)
        return -1;
    if (mat)
        *mat = s_shape[id].mat;
    if (tris)
        *tris = s_shape[id].tris;
    if (box)
        memcpy(box, s_shape[id].box, sizeof s_shape[id].box);
    return 0;
}

int shape_is_ancestor(ShapeId anc, ShapeId id)
{
    int guard = 0;
    if (anc >= s_n || id >= s_n)
        return 0;
    while (id != SHAPE_NONE && guard++ < SHAPE_STACK + 2)
    {
        if (id == anc)
            return 1;
        id = s_shape[id].parent;
    }
    return 0;
}

int shape_get_deep(ShapeId id, uint32_t *tris, float box[6])
{
    uint32_t i, n = 0;
    float    b[6] = {1e9f, 1e9f, 1e9f, -1e9f, -1e9f, -1e9f};
    if (id >= s_n)
        return -1;
    /*  A shape's own triangles and every descendant's.  The table is in
     *  the order it was opened, so a descendant always follows it. */
    for (i = id; i < s_n; ++i)
    {
        int a;
        if (i != id && !shape_is_ancestor(id, i))
            continue;
        if (!s_shape[i].tris)
            continue;
        n += s_shape[i].tris;
        for (a = 0; a < 3; ++a)
        {
            if (s_shape[i].box[a] < b[a])
                b[a] = s_shape[i].box[a];
            if (s_shape[i].box[3 + a] > b[3 + a])
                b[3 + a] = s_shape[i].box[3 + a];
        }
    }
    if (tris)
        *tris = n;
    if (box)
        memcpy(box, b, sizeof b);
    return 0;
}

int shape_built(ShapeId id)
{
    float    m;
    int      kind;
    uint64_t key;
    if (shape_get(id, &m, NULL, NULL) != 0)
        return 0;
    /*  A shape that names the record it draws is built, whatever its
     *  material. */
    if (shape_ref_of(id, &kind, &key) != SHAPE_NONE)
        return 1;
    /*  The network materials and those alone.  Below them are the ground
     *  and the water.  At MAT_ZONE and above are the tints laid over the
     *  ground, which cover the map and are not things. */
    if (m < -1.5f)
        return 0;
    if (m < -0.5f)
        return 1; /* a shape of several materials is a thing of parts */
    return m > MAT_SURFACE + 0.5f && m < MAT_ZONE - 0.5f;
}

void shape_unclaimed_report(void)
{
    int i, j;
    dumpf("shapes  %u shapes; %u triangles claimed by none%s\n", s_n, s_unclaimed_tris, s_unclaimed_lost ? ", from more call sites than the report holds" : "");
    /* the worst offenders first, so the count can be walked down */
    for (j = 0; j < s_n_unclaimed && j < 12; ++j)
    {
        int      best = -1;
        uint32_t bt   = 0;
        for (i = 0; i < s_n_unclaimed; ++i)
            if (s_unclaimed[i].tris > bt)
                bt = s_unclaimed[i].tris, best = i;
        if (best < 0)
            break;
        dumpf("shapes    %-24s %8u  %s\n", s_unclaimed[best].who ? s_unclaimed[best].who : "?", bt, s_unclaimed[best].where ? s_unclaimed[best].where : "");
        s_unclaimed[best].tris = 0;
    }
}

/*  One list's shapes.  With no map it marks in `keep` each shape the
 *  list's triangles claim.  With one it renumbers them, and counts each
 *  kept shape's triangles and box again.  `old_n` is the table's size the
 *  ids were given under. */
static void list_shapes(RMeshList *l, const uint32_t *map, uint8_t *keep, uint32_t old_n)
{
    uint32_t i;
    if (!l || !l->comp)
        return;
    for (i = 0; i < l->n / 3u; ++i)
    {
        uint32_t id = l->comp[i];
        float    p[3][3];
        int      v, a;
        if (!map)
        {
            if (id < old_n)
                keep[id] = 1;
            continue;
        }
        id         = id < old_n ? map[id] : SHAPE_NONE;
        l->comp[i] = id;
        if (id == SHAPE_NONE)
            continue;
        for (v = 0; v < 3; ++v)
            for (a = 0; a < 3; ++a)
                p[v][a] = mesh_list_corner(l, 3u * i + (uint32_t)v)->pos[a];
        ++s_shape[id].tris;
        mesh_box_grow(s_shape[id].box, p);
    }
}

/*  Every list the mesh holds, and the last build's while an edit builds. */
static void mesh_shapes_each(RMesh *m, const uint32_t *map, uint8_t *keep, uint32_t old_n)
{
    int k;
    for (k = 0; k < m->n_rl; ++k)
        list_shapes(&m->rl[k], map, keep, old_n);
    for (k = 0; k < m->n_wl; ++k)
        list_shapes(&m->wl[k], map, keep, old_n);
    for (k = 0; m->rl_old && k < m->n_rl; ++k)
        list_shapes(&m->rl_old[k], map, keep, old_n);
    for (k = 0; m->wl_old && k < m->n_wl; ++k)
        list_shapes(&m->wl_old[k], map, keep, old_n);
}

/*  THE TABLE KEPT TO WHAT THE MESH HOLDS.  A shape stands while a
 *  triangle of some list claims it or a shape under it.  Two shapes of
 *  one name under one parent are one thing, named by where it is, and
 *  are kept as one.  So a window build, which opens again the shapes it
 *  lays and leaves the chunks beyond it standing, holds the table a whole
 *  build holds.  The ids in every list are renumbered and each shape's
 *  triangles and box counted again from the lists. */
int shape_compact(RMesh *m)
{
    uint8_t  *keep;
    uint32_t *map, *slot, i, n = 0, nslot = 1, old_n = s_n;
    uint64_t *slot_key;
    char     *text = s_text;
    if (s_sp > 0 || !s_n)
        return 0; /* a shape is open: a build is under way */
    keep = (uint8_t *)calloc(s_n, 1);
    map  = (uint32_t *)malloc(s_n * sizeof *map);
    while (nslot < 2u * s_n)
        nslot *= 2u;
    slot     = (uint32_t *)malloc(nslot * sizeof *slot);
    slot_key = (uint64_t *)malloc(nslot * sizeof *slot_key);
    if (!keep || !map || !slot || !slot_key)
    {
        free(keep), free(map), free(slot), free(slot_key);
        return -1;
    }
    mesh_shapes_each(m, NULL, keep, old_n);
    /*  A parent is opened before what it holds, so one pass down the
     *  table keeps every ancestor of a shape kept. */
    for (i = s_n; i-- > 0;)
        if (keep[i] && s_shape[i].parent != SHAPE_NONE && s_shape[i].parent < s_n)
            keep[s_shape[i].parent] = 1;
    for (i = 0; i < nslot; ++i)
        slot[i] = SHAPE_NONE;
    /*  The names and notes go into an arena of their own. */
    s_text = NULL, s_text_n = s_text_cap = 0;
    for (i = 0; i < s_n; ++i)
    {
        Shape       sh = s_shape[i];
        const char *nm = sh.name ? text + (sh.name - 1u) : "";
        uint32_t    parent, h, at;
        uint64_t    key;
        map[i] = SHAPE_NONE;
        if (!keep[i])
            continue;
        parent = sh.parent != SHAPE_NONE && sh.parent < s_n ? map[sh.parent] : SHAPE_NONE;
        key    = hash_bytes(HASH_SEED, nm, strlen(nm));
        key    = hash_bytes(key, &parent, sizeof parent);
        for (h = (uint32_t)key & (nslot - 1u);; h = (h + 1u) & (nslot - 1u))
        {
            at = slot[h];
            if (at == SHAPE_NONE)
                break;
            /*  The name is compared only where the slot's own key matches. */
            if (slot_key[h] == key && s_shape[at].parent == parent &&
                strcmp(text_get(s_shape[at].name) ? text_get(s_shape[at].name) : "", nm) == 0)
                break;
        }
        if (at != SHAPE_NONE)
        {
            map[i] = at;
            continue;
        }
        sh.parent = parent;
        sh.name   = text_add(nm);
        sh.note   = sh.note ? text_add(text + (sh.note - 1u)) : 0;
        sh.tile0  = 0; /* the tiles serve the build that claimed the triangles */
        sh.net    = 0;
        sh.ntile  = 0;
        sh.tris   = 0;
        sh.box[0] = sh.box[1] = sh.box[2] = 1e9f;
        sh.box[3] = sh.box[4] = sh.box[5] = -1e9f;
        s_shape[n]                        = sh;
        map[i]                            = n;
        slot_key[h]                       = key;
        slot[h]                           = n++;
    }
    s_n = n;
    free(text);
    mesh_shapes_each(m, map, NULL, old_n);
    free(keep), free(map), free(slot), free(slot_key);
    return 0;
}
