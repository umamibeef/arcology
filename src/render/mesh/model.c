/*  model.c: the models, the walk that draws one, and the cache behind it.
 *  See mesh/model.h. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "mesh/emit.h"
#include "mesh/internal.h"
#include "mesh/model.h"
#include "mesh/shapes.h"
#include "mesh/surface.h"

/*  The pieces one model is made of at that size.  The model's build
 *  function makes them.  They are kept until it is asked for another
 *  size.  Answers how many pieces there are. */
static int net_model_shape(int model, float size, const ModelHead **head, const ModelPart **parts);

/*  THE MODELS.  Each is built at a size into its head and its parts.
 *  The pylon is a power line's: a pole with a crossarm turned to the way
 *  the line runs.  The wires between pylons are the line's (compose.c). */
static int pylon(float size, ModelHead *head, ModelPart *parts)
{
    (void)size;
    memset(head, 0, sizeof *head);
    memset(parts, 0, sizeof *parts * 2);
    parts[0].kind = M_BOX, parts[0].w = 0.04f, parts[0].d = 0.04f, parts[0].z0 = 0.0f, parts[0].z1 = 1.45f;
    parts[1].kind = M_BOX, parts[1].w = 0.24f, parts[1].d = 0.04f, parts[1].z0 = 1.33f, parts[1].z1 = 1.37f;
    parts[0].f1 = parts[1].f1 = 1.0f;
    parts[0].mat = parts[1].mat = MAT_PROP;
    return 2;
}

/*  A street lamp: a pole `size` levels tall, and an arm reaching out
 *  over the road along the way it faces.  At the arm's end hangs the
 *  head, a dark housing whose lens faces the ground and is lit at night
 *  (MAT_LAMP, code LAMP_HEAD).  The twin reaches both ways. */
#define LAMP_HEAD 14.0f
static int lamp_parts(float size, ModelPart *parts, int twin)
{
    const float reach = 0.15f, thick = 0.02f, head = 0.07f, head_w = 0.035f, head_h = 0.018f;
    int         n = 0, k;
    parts[n].kind = M_PRISM, parts[n].d = thick, parts[n].w = thick, parts[n].z0 = 0.0f, parts[n].z1 = size, parts[n].f1 = 1.0f, parts[n].mat = MAT_PROP;
    ++n;
    for (k = 0; k < (twin ? 2 : 1); ++k)
    {
        float way     = k ? -1.0f : 1.0f;
        parts[n].kind = M_PRISM, parts[n].ax = way * reach * 0.5f, parts[n].d = reach, parts[n].w = thick * 0.7f, parts[n].z0 = size - thick, parts[n].z1 = size, parts[n].f1 = 1.0f, parts[n].mat = MAT_PROP;
        ++n;
        parts[n].kind = M_PRISM, parts[n].ax = way * (reach + head * 0.4f), parts[n].d = head, parts[n].w = head_w, parts[n].z0 = size - head_h, parts[n].z1 = size + 0.004f, parts[n].f1 = 1.0f, parts[n].mat = MAT_PROP;
        ++n;
        parts[n].kind = M_UNDER, parts[n].ax = way * (reach + head * 0.4f), parts[n].d = head * 0.85f, parts[n].w = head_w * 0.8f, parts[n].z0 = size - head_h - 0.001f, parts[n].code = LAMP_HEAD;
        ++n;
    }
    return n;
}

static int lamp(float size, ModelHead *head, ModelPart *parts)
{
    memset(head, 0, sizeof *head);
    memset(parts, 0, sizeof *parts * 4);
    return lamp_parts(size, parts, 0);
}

static int lamp_twin(float size, ModelHead *head, ModelPart *parts)
{
    memset(head, 0, sizeof *head);
    memset(parts, 0, sizeof *parts * 7);
    return lamp_parts(size, parts, 1);
}

static const struct
{
    const char *name;
    int (*build)(float size, ModelHead *head, ModelPart *parts);
} MODELS[] = {
    {"pylon",             pylon    },
    {"street lamp",       lamp     },
    {"street lamp, twin", lamp_twin},
};
#define MODEL_MAX ((int)(sizeof MODELS / sizeof MODELS[0]))

/*  The last size each model was asked for, and what it answered.  A prop
 *  drawn every frame is built once, and again only when it is asked for
 *  another size. */
static ModelPart s_part[MODEL_MAX][MODEL_PARTS];
static ModelHead s_head[MODEL_MAX];
static int       s_n[MODEL_MAX];
static float     s_size[MODEL_MAX];
static int       s_have[MODEL_MAX];

int net_model_find(const char *name)
{
    int i;
    for (i = 0; name && i < MODEL_MAX; ++i)
        if (strcmp(MODELS[i].name, name) == 0)
            return i;
    return -1;
}

static int net_model_shape(int model, float size, const ModelHead **head, const ModelPart **parts)
{
    if (model < 0 || model >= MODEL_MAX)
        return 0;
    if (!s_have[model] || s_size[model] != size)
    {
        s_n[model]    = MODELS[model].build(size, &s_head[model], s_part[model]);
        s_size[model] = size;
        s_have[model] = 1;
    }
    *head  = &s_head[model];
    *parts = s_part[model];
    return s_n[model];
}

/*  One model, at (x, y) facing (fx, fy).  A part's `w` is across the
 *  facing and `d` along it, so a prop turned to face another way keeps
 *  its shape.  With the facings on the tile's own axes that is the two
 *  swapping over. */
int net_model_put_on(int model, RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy, float size, float phase, float group, float base, float base2, int on_base)
{
    const ModelHead *h;
    const ModelPart *part;
    int              i, n = net_model_shape(model, size, &h, &part);
    float            rx = -fy, ry = fx; /* across the facing */
    if (n <= 0)
        return 0;
    /*  The model's own place in the stack, its own lift and its own
     *  origin, so the caller hands over a position and nothing else. */
    order += h->slot;
    base += h->lift;
    base2 += h->lift;
    x += fx * h->ax + rx * h->ac;
    y += fy * h->ax + ry * h->ac;
    for (i = 0; i < n; ++i)
    {
        const ModelPart *p  = &part[i];
        float            px = x + fx * p->ax + rx * p->ac, py = y + fy * p->ax + ry * p->ac;
        /*  Across and along, laid on the map's own axes. */
        float wx = fx != 0.0f ? p->d : p->w, wy = fx != 0.0f ? p->w : p->d;
        switch (p->kind)
        {
            case M_BOX:
                if (put_box(m, c, mask_bit, order, px, py, wx, wy, p->z0, p->z1, p->mat, phase) != 0)
                    return -1;
                break;
            case M_ARM:
                {
                    /*  A box spanning across from the far end to this one,
                     *  half way between them and lapping on to each by `d`. */
                    float ox = x + fx * p->ax + rx * p->ac2, oy = y + fy * p->ax + ry * p->ac2;
                    float mx = 0.5f * (ox + px), my = 0.5f * (oy + py);
                    float len     = fabsf(px - ox) + fabsf(py - oy) + p->d;
                    int   along_x = fabsf(px - ox) > fabsf(py - oy);
                    if (put_box(m, c, mask_bit, order, mx, my, along_x ? len : p->w, along_x ? p->w : len, p->z0, p->z1, p->mat, phase) != 0)
                        return -1;
                    break;
                }
            case M_LENS:
                {
                    /*  Three lenses down from z0, one every z1, facing the
                     *  way the prop does. */
                    float g = on_base ? base : surface_at_world(c, mask_bit, px, py);
                    int   k;
                    for (k = 0; k < 3; ++k)
                        if (put_lamp_face(m, order, px, py, g, p->z0 - p->z1 * (float)k, fx, fy, p->w, phase, group + (float)k, 0) != 0)
                            return -1;
                    break;
                }
            case M_PRISM:
                {
                    /*  Along the facing rather than square to the map, and
                     *  cut on the tile folds.  A piece reaching into the
                     *  next tile then keeps that tile's ground.  Its two
                     *  feet may stand at different heights, which is how an
                     *  arm rises as it reaches out. */
                    float g0 = on_base ? base : surface_at_world(c, mask_bit, x, y);
                    float g1 = on_base ? base2 : g0;
                    if (put_prism_clip_m(m, c, mask_bit, order, x + fx * p->ax, y + fy * p->ax, fx, fy, p->d, p->w, g0 + (g1 - g0) * p->f0 + p->ac, g0 + (g1 - g0) * p->f1 + p->ac2, p->z0, p->z1, phase, p->mat) != 0)
                        return -1;
                    break;
                }
            case M_UNDER:
                {
                    float g = on_base ? base : surface_at_world(c, mask_bit, px, py);
                    if (put_lamp_under(m, order, px, py, g, p->z0, fx, fy, p->d, p->w, p->code) != 0)
                        return -1;
                    break;
                }
            case M_FACE:
                {
                    float g = on_base ? base : surface_at_world(c, mask_bit, px, py);
                    if (put_lamp_face(m, order, px, py, g, p->z0, fx, fy, p->w, phase, p->code, p->uv) != 0)
                        return -1;
                    break;
                }
            default:
                break;
        }
    }
    return 0;
}
