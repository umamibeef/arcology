/*  model.c -- the walk that draws a model, and the cache behind it.
 *  See geo/model.h.  Each model is its own file under scripts/models. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "mesh/internal.h"
#include "net/internal.h"
#include "geo/model.h"
#include "script.h"

/*  Every model is a SCRIPT'S.  Nothing here ships a prop's shape: a name
 *  appears the moment a file under scripts/models defines it, and a prop
 *  whose model no name matches draws nothing -- which is what a build
 *  that cannot find its scripts looks like. */
#define MODEL_MAX 64

/*  The last size each model was asked for, and what it answered.  A prop
 *  drawn every frame is built once; one whose size follows the junction
 *  it stands at is built again when the junction changes. */
static ModelPart s_part[MODEL_MAX][MODEL_PARTS];
static ModelHead s_head[MODEL_MAX];
static int       s_n[MODEL_MAX];
static float     s_size[MODEL_MAX];
static int       s_have[MODEL_MAX];
static int       s_gen = -1;

int net_model_count(void)
{
    return script_model_count();
}

const char *net_model_name(int i)
{
    return script_model_name(i);
}

int net_model_find(const char *name)
{
    return script_model_find(name);
}

int net_model_shape(int model, float size, const ModelHead **head, const ModelPart **parts)
{
    if (model < 0 || model >= MODEL_MAX)
        return 0;
    if (s_gen != script_generation())
    {
        memset(s_have, 0, sizeof s_have);
        s_gen = script_generation();
    }
    if (!s_have[model] || s_size[model] != size)
    {
        s_n[model] = script_model_build(model, size, &s_head[model], s_part[model], MODEL_PARTS);
        s_size[model] = size;
        s_have[model] = 1;
    }
    *head  = &s_head[model];
    *parts = s_part[model];
    return s_n[model];
}

/*  One model, at (x, y) facing (fx, fy).  A part's `w` is across the
 *  facing and `d` along it, so a prop turned to face another way keeps
 *  its shape; with the facings on the tile's own axes that is the two
 *  swapping over. */
int net_model_put(int model, RMesh *m, const RCity *c, uint8_t mask_bit, float order,
                  float x, float y, float fx, float fy, float size, float phase, float group)
{
    return net_model_put_on(model, m, c, mask_bit, order, x, y, fx, fy, size, phase, group, 0.0f, 0.0f, 0);
}

int net_model_put_on(int model, RMesh *m, const RCity *c, uint8_t mask_bit, float order,
                     float x, float y, float fx, float fy, float size, float phase, float group,
                     float base, float base2, int on_base)
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
        float            wx = fx != 0.0f ? p->d : p->w, wy = fx != 0.0f ? p->w : p->d;
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
                float len = fabsf(px - ox) + fabsf(py - oy) + p->d;
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
                    if (put_lamp_face(m, order, px, py, g, p->z0 - p->z1 * (float)k, fx, fy, p->w, phase, group + (float)k) != 0)
                        return -1;
                break;
            }
            case M_PRISM:
            {
                /*  Along the facing rather than square to the map, and
                 *  cut on the tile folds, so a piece reaching into the
                 *  next tile keeps that tile's ground.  Its two feet may
                 *  stand at different heights, which is how an arm rises
                 *  as it reaches out. */
                float g0 = on_base ? base : surface_at_world(c, mask_bit, x, y);
                float g1 = on_base ? base2 : g0;
                if (put_prism_clip_m(m, c, mask_bit, order, x + fx * p->ax, y + fy * p->ax, fx, fy,
                                     p->d, p->w, g0 + (g1 - g0) * p->f0 + p->ac,
                                     g0 + (g1 - g0) * p->f1 + p->ac2,
                                     p->z0, p->z1, phase, p->mat) != 0)
                    return -1;
                break;
            }
            case M_FACE:
            {
                float g = on_base ? base : surface_at_world(c, mask_bit, px, py);
                if (put_lamp_face(m, order, px, py, g, p->z0, fx, fy, p->w, phase, p->code) != 0)
                    return -1;
                break;
            }
            default: break;
        }
    }
    return 0;
}
