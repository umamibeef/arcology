/*  shape.c -- the shape table: what each shape is, what it is part of,
 *  and which triangles are its own.  See mesh/shape.h.  Nothing here
 *  draws; the emitter files its triangles through shape_claim. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dump.h"
#include "mesh/internal.h"
#include "mesh/shape.h"

#define SHAPE_MAX  262144
#define SHAPE_STACK 32

typedef struct
{
    ShapeId     parent;
    uint8_t     depth;
    const char *where, *who; /* where it was opened, from the macro */
    uint32_t    name;        /* 1 + an offset into the arena, 0 for none */
    uint32_t    note;
    float       mat; /* its material, or -1 where it draws in more than one */
    uint32_t    tris;
    float       box[6];
} Shape;

static Shape   *s_shape;
static uint32_t s_n, s_cap;
static char    *s_text; /* the names and notes, end to end */
static uint32_t s_text_n, s_text_cap;
static ShapeId  s_stack[SHAPE_STACK];
static int      s_sp;
static int      s_on;

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

ShapeId shape_current(void)
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

ShapeId shape_open_under_at(ShapeId parent, const char *where, const char *who, const char *fmt, ...)
{
    va_list ap;
    ShapeId id;
    va_start(ap, fmt);
    id = shape_open_v(parent, where, who, fmt, ap);
    va_end(ap);
    return id;
}

static ShapeId shape_open_v(ShapeId parent, const char *where, const char *who, const char *fmt, va_list ap)
{
    char   name[160];
    Shape *sh;
    if (!s_on || s_pass == 1)
        return SHAPE_NONE; /* the grading pass draws nothing to claim */
    if (s_n >= s_cap)
    {
        uint32_t cap = s_cap ? s_cap * 2u : 8192u;
        Shape   *ns;
        if (cap > SHAPE_MAX)
            cap = SHAPE_MAX;
        if (s_n >= cap)
            return SHAPE_NONE;
        ns = (Shape *)realloc(s_shape, (size_t)cap * sizeof *ns);
        if (!ns)
            return SHAPE_NONE;
        s_shape = ns;
        s_cap   = cap;
    }
    vsnprintf(name, sizeof name, fmt, ap);
    sh         = &s_shape[s_n];
    sh->parent = parent;
    sh->depth  = (uint8_t)(s_sp < SHAPE_STACK ? s_sp : SHAPE_STACK - 1);
    sh->where  = where;
    sh->who    = who;
    sh->name   = text_add(name);
    sh->note   = 0;
    sh->mat    = -2.0f; /* nothing drawn yet */
    sh->tris   = 0;
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
        return; /* a shape the grading pass never opened is never closed */
    /*  Closed in the order they were opened.  A producer that closes the
     *  wrong one would leave the stack askew and file everything after it
     *  under a shape that has finished, so unwind to the one it names. */
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

void shape_note(const char *fmt, ...)
{
    char     buf[1024];
    va_list  ap;
    ShapeId  id = shape_current();
    if (!s_on || id == SHAPE_NONE || id >= s_n)
        return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    s_shape[id].note = text_add(buf);
}

static void box_grow_p(float b[6], const float p[3][3])
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
    box_grow_p(sh->box, p);
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
    float m;
    if (shape_get(id, &m, NULL, NULL) != 0)
        return 0;
    /*  The network materials and those alone.  Below them are the ground
     *  and the water; at MAT_ZONE and above are the tints laid over the
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
    dumpf("shapes  %u shapes; %u triangles claimed by none%s\n", s_n, s_unclaimed_tris,
          s_unclaimed_lost ? ", from more call sites than the report holds" : "");
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
        dumpf("shapes    %-24s %8u  %s\n", s_unclaimed[best].who ? s_unclaimed[best].who : "?", bt,
              s_unclaimed[best].where ? s_unclaimed[best].where : "");
        s_unclaimed[best].tris = 0;
    }
}
