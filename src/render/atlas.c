/*  atlas.c -- load the PNG atlases and JSON sidecars sc2kpack.py emits.
 *
 *  See atlas.h for the portability rules.  The short version: fixed-width
 *  types, binary-mode I/O, no POSIX, and nothing that assumes a byte order.
 */
#include "atlas.h"
#include "tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSMN_STATIC
#include "jsmn.h"
#include "lodepng.h"

/* ------------------------------------------------------------------ *
 *  small helpers
 * ------------------------------------------------------------------ */
static void fail(RAtlas *a, const char *fmt, const char *arg)
{
    if (arg)
        snprintf(a->err, sizeof a->err, fmt, arg);
    else
        snprintf(a->err, sizeof a->err, "%s", fmt);
}

/*  Forward slashes work on Win32 as well as everywhere else, so there is
 *  no need for a platform separator here. */
static int join(char *dst, size_t cap, const char *dir, const char *name)
{
    int n = snprintf(dst, cap, "%s/%s", dir, name);
    return (n > 0 && (size_t) n < cap) ? 0 : -1;
}

/*  Binary mode is not optional: on Windows a text-mode read eats \r and
 *  every offset after the first newline is wrong. */
static char *read_text(const char *path, size_t *out_len)
{
    FILE  *f = fopen(path, "rb");
    long   n;
    char  *buf;
    size_t got;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return NULL;
    }
    n = ftell(f);
    if (n < 0)
    {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (char *) malloc((size_t) n + 1u);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }
    got = fread(buf, 1, (size_t) n, f);
    fclose(f);
    if (got != (size_t) n)
    {
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    if (out_len)
        *out_len = (size_t) n;
    return buf;
}

/* ------------------------------------------------------------------ *
 *  jsmn navigation.  jsmn gives a flat token array; these walk it.
 * ------------------------------------------------------------------ */
static int tok_eq(const char *js, const jsmntok_t *t, const char *s)
{
    size_t n = (size_t) (t->end - t->start);
    return t->type == JSMN_STRING && strlen(s) == n &&
           strncmp(js + t->start, s, n) == 0;
}

/*  Index of the first token after the subtree rooted at i. */
static int tok_skip(const jsmntok_t *t, int i)
{
    int j, n;
    if (t[i].type == JSMN_OBJECT)
    {
        n = t[i].size;
        j = i + 1;
        while (n-- > 0)
        {
            j = tok_skip(t, j); /* key   */
            j = tok_skip(t, j); /* value */
        }
        return j;
    }
    if (t[i].type == JSMN_ARRAY)
    {
        n = t[i].size;
        j = i + 1;
        while (n-- > 0)
            j = tok_skip(t, j);
        return j;
    }
    return i + 1;
}

/*  Index of the value token for `key` inside the object at `obj`, or -1. */
static int obj_get(const char *js, const jsmntok_t *t, int obj, const char *key)
{
    int i, n;
    if (obj < 0 || t[obj].type != JSMN_OBJECT)
        return -1;
    n = t[obj].size;
    i = obj + 1;
    while (n-- > 0)
    {
        if (tok_eq(js, &t[i], key))
            return i + 1;
        i = tok_skip(t, i + 1);
    }
    return -1;
}

static long tok_long(const char *js, const jsmntok_t *t)
{
    char   buf[32];
    size_t n = (size_t) (t->end - t->start);
    if (n >= sizeof buf)
        n = sizeof buf - 1u;
    memcpy(buf, js + t->start, n);
    buf[n] = '\0';
    return strtol(buf, NULL, 10);
}

static long obj_long(const char *js, const jsmntok_t *t, int obj,
                     const char *key, long fallback)
{
    int v = obj_get(js, t, obj, key);
    return v < 0 ? fallback : tok_long(js, &t[v]);
}

/*  Parse a whole document.  Two passes: jsmn counts tokens when handed a
 *  NULL array, which saves guessing at a cap. */
static jsmntok_t *json_parse(const char *js, size_t len, int *n_out)
{
    jsmn_parser p;
    jsmntok_t  *toks;
    int         n;

    jsmn_init(&p);
    n = jsmn_parse(&p, js, len, NULL, 0);
    if (n <= 0)
        return NULL;
    toks = (jsmntok_t *) malloc(sizeof(jsmntok_t) * (size_t) n);
    if (!toks)
        return NULL;
    jsmn_init(&p);
    if (jsmn_parse(&p, js, len, toks, (unsigned int) n) != n)
    {
        free(toks);
        return NULL;
    }
    *n_out = n;
    return toks;
}

/* ------------------------------------------------------------------ *
 *  palette resolve
 * ------------------------------------------------------------------ */
void atlas_resolve_rect(RAtlas *a, RAtlasLevel *l, int32_t x0, int32_t y0,
                          int32_t w, int32_t h)
{
    int32_t x, y;
    if (!l->indices || !l->rgba)
        return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x0 + w > l->w) w = l->w - x0;
    if (y0 + h > l->h) h = l->h - y0;

    for (y = 0; y < h; ++y)
    {
        const uint8_t *src = l->indices + (size_t) (y0 + y) * (size_t) l->w + (size_t) x0;
        uint8_t       *dst = l->rgba + (((size_t) (y0 + y) * (size_t) l->w + (size_t) x0) << 2);
        for (x = 0; x < w; ++x)
        {
            const uint8_t *c  = a->palette[src[x]];
            uint32_t       al = c[3];
            /*  Premultiplied, so the bilinear taps at a sprite's edge blend
             *  toward transparent rather than toward black. */
            dst[0] = (uint8_t) ((c[0] * al + 127u) / 255u);
            dst[1] = (uint8_t) ((c[1] * al + 127u) / 255u);
            dst[2] = (uint8_t) ((c[2] * al + 127u) / 255u);
            dst[3] = (uint8_t) al;
            dst += 4;
        }
    }
}

void atlas_resolve(RAtlas *a, RAtlasLevel *l)
{
    atlas_resolve_rect(a, l, 0, 0, l->w, l->h);
}

/* ------------------------------------------------------------------ *
 *  one level
 * ------------------------------------------------------------------ */
static int load_level(RAtlas *a, const char *dir, const char *sheet_name,
                      RAtlasLevel *l)
{
    char           path[1024];
    char          *js = NULL;
    jsmntok_t     *t  = NULL;
    int            nt = 0, root, meta, sc2k, frames, tiles, size, i, n, rc = -1;
    unsigned char *png = NULL;
    unsigned       pw = 0, ph = 0;
    LodePNGState   st;
    int            img_tok;
    int32_t        k;

    if (join(path, sizeof path, dir, sheet_name) != 0)
    {
        fail(a, "path too long for %s", sheet_name);
        return -1;
    }
    js = read_text(path, NULL);
    if (!js)
    {
        fail(a, "cannot read %s", path);
        return -1;
    }
    t = json_parse(js, strlen(js), &nt);
    if (!t)
    {
        fail(a, "cannot parse %s", path);
        free(js);
        return -1;
    }

    root   = 0;
    meta   = obj_get(js, t, root, "meta");
    frames = obj_get(js, t, root, "frames");
    sc2k   = obj_get(js, t, meta, "sc2k");
    size   = obj_get(js, t, meta, "size");
    tiles  = obj_get(js, t, sc2k, "tiles");
    if (meta < 0 || frames < 0 || sc2k < 0 || size < 0 || tiles < 0)
    {
        fail(a, "%s: not an sc2kpack sheet", path);
        goto done;
    }

    l->zoom        = (int32_t) obj_long(js, t, sc2k, "zoom", 32);
    l->id_base     = (int32_t) obj_long(js, t, sc2k, "id_base", 0);
    l->tile_w      = (int32_t) obj_long(js, t, sc2k, "tile_w", l->zoom);
    l->tile_h      = (int32_t) obj_long(js, t, sc2k, "tile_h", l->zoom / 2);
    l->alt_step    = (int32_t) obj_long(js, t, sc2k, "alt_step", 12);
    l->transparent = (int32_t) obj_long(js, t, sc2k, "transparent", 0);
    l->w           = (int32_t) obj_long(js, t, size, "w", 0);
    l->h           = (int32_t) obj_long(js, t, size, "h", 0);

    /* ---- the image ------------------------------------------------ */
    img_tok = obj_get(js, t, meta, "image");
    if (img_tok < 0)
    {
        fail(a, "%s: no meta.image", path);
        goto done;
    }
    {
        char   name[256];
        size_t ln = (size_t) (t[img_tok].end - t[img_tok].start);
        if (ln >= sizeof name)
        {
            fail(a, "%s: image name too long", path);
            goto done;
        }
        memcpy(name, js + t[img_tok].start, ln);
        name[ln] = '\0';
        if (join(path, sizeof path, dir, name) != 0)
        {
            fail(a, "path too long for %s", name);
            goto done;
        }
    }

    lodepng_state_init(&st);
    st.info_raw.colortype  = LCT_PALETTE;
    st.info_raw.bitdepth   = 8;
    st.decoder.color_convert = 0; /* hand back the raw palette indices */
    {
        unsigned char *file = NULL;
        size_t         flen = 0;
        unsigned       err;
        if (lodepng_load_file(&file, &flen, path) != 0)
        {
            fail(a, "cannot read %s", path);
            lodepng_state_cleanup(&st);
            goto done;
        }
        err = lodepng_decode(&png, &pw, &ph, &st, file, flen);
        free(file);
        if (err)
        {
            fail(a, "%s: not a readable PNG", path);
            lodepng_state_cleanup(&st);
            goto done;
        }
    }
    if (st.info_png.color.colortype != LCT_PALETTE ||
        st.info_png.color.bitdepth != 8)
    {
        fail(a, "%s: expected an 8-bit indexed PNG", path);
        lodepng_state_cleanup(&st);
        goto done;
    }
    if ((int32_t) pw != l->w || (int32_t) ph != l->h)
    {
        fail(a, "%s: image size disagrees with the sidecar", path);
        lodepng_state_cleanup(&st);
        goto done;
    }
    /*  lodepng folds tRNS into the palette alpha for us. */
    memset(a->palette, 0, sizeof a->palette);
    /*  Keep phase 0 so atlas_animate can compose every later phase from
     *  it rather than from whatever the last rotation left behind. */
    for (i = 0; i < (int) st.info_png.color.palettesize && i < 256; ++i)
    {
        a->palette[i][0] = st.info_png.color.palette[i * 4 + 0];
        a->palette[i][1] = st.info_png.color.palette[i * 4 + 1];
        a->palette[i][2] = st.info_png.color.palette[i * 4 + 2];
        a->palette[i][3] = st.info_png.color.palette[i * 4 + 3];
        memcpy(a->palette0[i], a->palette[i], 4);
    }
    lodepng_state_cleanup(&st);

    l->indices = png;
    png        = NULL;
    l->rgba    = (uint8_t *) malloc((size_t) l->w * (size_t) l->h * 4u);
    if (!l->rgba)
    {
        fail(a, "out of memory for the atlas", NULL);
        goto done;
    }

    /* ---- the tile table ------------------------------------------- */
    for (k = 0; k < R_MAX_SHAPE; ++k)
        l->by_id[k] = -1;

    l->n_tiles = t[frames].size;
    l->tiles   = (RTile *) calloc((size_t) (l->n_tiles > 0 ? l->n_tiles : 1),
                                  sizeof(RTile));
    if (!l->tiles)
    {
        fail(a, "out of memory for the tile table", NULL);
        goto done;
    }

    /*  Pass one: geometry, from the Aseprite-shaped `frames` object. */
    n = t[frames].size;
    i = frames + 1;
    {
        int32_t idx = 0;
        while (n-- > 0)
        {
            int   key = i;
            int   val = i + 1;
            int   fr  = obj_get(js, t, val, "frame");
            long  id  = tok_long(js, &t[key]);
            RTile *rt = &l->tiles[idx];
            if (fr >= 0 && id > 0 && id < R_MAX_SHAPE)
            {
                rt->id   = (uint16_t) id;
                rt->x    = (uint16_t) obj_long(js, t, fr, "x", 0);
                rt->y    = (uint16_t) obj_long(js, t, fr, "y", 0);
                rt->w    = (uint16_t) obj_long(js, t, fr, "w", 0);
                rt->h    = (uint16_t) obj_long(js, t, fr, "h", 0);
                rt->tile = (uint16_t) (id - l->id_base);
                rt->foot = (uint8_t) (rt->w / (uint16_t) l->tile_w);
                if (rt->foot == 0)
                    rt->foot = 1;
                rt->ax = 0;
                rt->ay = (int16_t) ((int32_t) rt->h - l->tile_h);
                l->by_id[id] = idx;
                ++idx;
            }
            i = tok_skip(t, val);
        }
        l->n_tiles = idx;
    }

    /*  Pass two: the game's own semantics, which Aseprite knows nothing
     *  about, from meta.sc2k.tiles.  Anything absent keeps the derived
     *  value from pass one. */
    n = t[tiles].size;
    i = tiles + 1;
    while (n-- > 0)
    {
        int  key = i;
        int  val = i + 1;
        long id  = tok_long(js, &t[key]);
        if (id > 0 && id < R_MAX_SHAPE && l->by_id[id] >= 0)
        {
            RTile *rt = &l->tiles[l->by_id[id]];
            rt->foot  = (uint8_t) obj_long(js, t, val, "foot", rt->foot);
            rt->ax    = (int16_t) obj_long(js, t, val, "ax", rt->ax);
            rt->ay    = (int16_t) obj_long(js, t, val, "ay", rt->ay);
            rt->tile  = (uint16_t) obj_long(js, t, val, "tile", rt->tile);
        }
        i = tok_skip(t, val);
    }

    atlas_resolve(a, l);
    rc = 0;

done:
    free(png);
    free(t);
    free(js);
    return rc;
}

/* ------------------------------------------------------------------ *
 *  the whole atlas
 * ------------------------------------------------------------------ */
int atlas_load(RAtlas *a, const char *dir)
{
    char       path[1024];
    char      *js = NULL;
    jsmntok_t *t  = NULL;
    int        nt = 0, zooms, n, i, rc = -1;

    memset(a, 0, sizeof *a);

    if (join(path, sizeof path, dir, "atlas.json") != 0)
    {
        fail(a, "asset path too long", NULL);
        return -1;
    }
    js = read_text(path, NULL);
    if (!js)
    {
        fail(a, "cannot read %s -- run tools/sc2kpack.py extract first", path);
        return -1;
    }
    t = json_parse(js, strlen(js), &nt);
    if (!t)
    {
        fail(a, "cannot parse %s", path);
        free(js);
        return -1;
    }
    /*  The runs the game cycles with _AnimatePalette.  Optional: an atlas
     *  without them simply never animates. */
    {
        int an = obj_get(js, t, 0, "animated");
        if (an >= 0 && t[an].type == JSMN_ARRAY)
        {
            int k = t[an].size, j = an + 1;
            while (k-- > 0 && a->n_anim < 4)
            {
                RAnim *r = &a->anim[a->n_anim];
                r->first = (int32_t) obj_long(js, t, j, "first", -1);
                r->count = (int32_t) obj_long(js, t, j, "count", 0);
                r->clut  = (int32_t) obj_long(js, t, j, "clut", 0);
                if (r->first >= 0 && r->count > 1 &&
                    r->first + r->count <= 256)
                    a->n_anim++;
                j = tok_skip(t, j);
            }
        }
    }

    zooms = obj_get(js, t, 0, "zooms");
    if (zooms < 0 || t[zooms].type != JSMN_ARRAY)
    {
        fail(a, "%s: no zooms array", path);
        goto done;
    }

    n = t[zooms].size;
    i = zooms + 1;
    while (n-- > 0 && a->n_levels < R_MAX_LEVELS)
    {
        int    sheet = obj_get(js, t, i, "sheet");
        char   name[128];
        size_t ln;
        if (sheet < 0)
        {
            fail(a, "%s: a zoom entry has no sheet", path);
            goto done;
        }
        ln = (size_t) (t[sheet].end - t[sheet].start);
        if (ln >= sizeof name)
        {
            fail(a, "%s: sheet name too long", path);
            goto done;
        }
        memcpy(name, js + t[sheet].start, ln);
        name[ln] = '\0';
        if (load_level(a, dir, name, &a->level[a->n_levels]) != 0)
            goto done; /* load_level has already set a->err */
        ++a->n_levels;
        i = tok_skip(t, i);
    }
    rc = a->n_levels > 0 ? 0 : -1;
    if (rc != 0)
        fail(a, "%s: no levels loaded", path);

done:
    free(t);
    free(js);
    if (rc != 0)
        atlas_free(a);
    return rc;
}

/*  Advance the animated runs to `phase`.  The game does NOT rotate them:
 *  $9750 keeps two colour tables, swaps them every 12 ticks and rebuilds
 *  one from the other through a permutation ($9770, $97FA):
 *
 *      new[i] = prev[perm[i]]
 *
 *  The 49-entry run is three eight-cycles, a four-cycle, a fixed point and
 *  then an eight-cycle, a four-cycle and an eight-cycle running the other
 *  way -- several independent ramps, not one. The 15-entry run is seven
 *  swaps and a fixed point, which is a blink rather than a flow. Turning
 *  all 49 as a single block, which is what this used to do, mixes ramps
 *  that have nothing to do with each other.
 *
 *  No pixel is touched; only the table moves. 12 ticks is 5 steps a
 *  second. */
void atlas_animate(RAtlas *a, int32_t phase)
{
    uint8_t tmp[256][4];
    int32_t k, i, step;

    memcpy(a->palette, a->palette0, sizeof a->palette);
    if (phase < 0)
        phase = 0;
    for (step = 0; step < phase; ++step)
    {
        memcpy(tmp, a->palette, sizeof tmp);
        for (k = 0; k < a->n_anim; ++k)
        {
            const RAnim   *r = &a->anim[k];
            const uint8_t *perm =
                (r->count == 49) ? R_ANIM_PERM_A
                                 : (r->count == 15 ? R_ANIM_PERM_B : NULL);
            if (!perm)
                continue;
            for (i = 0; i < r->count; ++i)
                memcpy(a->palette[r->first + i],
                       tmp[r->first + perm[i]], 4);
        }
    }
    for (k = 0; k < a->n_levels; ++k)
        atlas_resolve(a, &a->level[k]);
}

/*  How many applications of the run's permutation bring it back to the
 *  identity: the least common multiple of its cycle lengths. */
static int32_t perm_period(const uint8_t *perm, int32_t n)
{
    int32_t period = 1, i;
    uint8_t seen[256];
    memset(seen, 0, sizeof seen);
    for (i = 0; i < n; ++i)
    {
        int32_t len = 0, j = i, g, x, y;
        if (seen[i])
            continue;
        while (!seen[j])
        {
            seen[j] = 1;
            j       = perm[j];
            ++len;
        }
        /* lcm(period, len) */
        x = period;
        y = len;
        while (y)
        {
            g = x % y;
            x = y;
            y = g;
        }
        period = period / x * len;
    }
    return period;
}

void atlas_animate_runs(RAtlas *a, int32_t steps_a, int32_t steps_b)
{
    uint8_t tmp[256][4];
    int32_t k, i, step;

    memcpy(a->palette, a->palette0, sizeof a->palette);
    for (k = 0; k < a->n_anim; ++k)
    {
        const RAnim   *r = &a->anim[k];
        const uint8_t *perm =
            (r->count == 49) ? R_ANIM_PERM_A
                             : (r->count == 15 ? R_ANIM_PERM_B : NULL);
        int32_t steps, period;
        if (!perm)
            continue;
        steps  = (r->count == 49) ? steps_a : steps_b;
        period = perm_period(perm, r->count);
        if (steps < 0)
            steps = 0;
        steps %= period;
        for (step = 0; step < steps; ++step)
        {
            memcpy(tmp, a->palette, sizeof tmp);
            for (i = 0; i < r->count; ++i)
                memcpy(a->palette[r->first + i], tmp[r->first + perm[i]], 4);
        }
    }
}

void atlas_free(RAtlas *a)
{
    int i;
    for (i = 0; i < R_MAX_LEVELS; ++i)
    {
        free(a->level[i].indices);
        free(a->level[i].rgba);
        free(a->level[i].tiles);
        a->level[i].indices = NULL;
        a->level[i].rgba    = NULL;
        a->level[i].tiles   = NULL;
    }
    a->n_levels = 0;
}

const RTile *atlas_tile(const RAtlasLevel *l, int32_t shap_id)
{
    int32_t idx;
    if (shap_id <= 0 || shap_id >= R_MAX_SHAPE)
        return NULL;
    idx = l->by_id[shap_id];
    return idx < 0 ? NULL : &l->tiles[idx];
}

const RAtlasLevel *atlas_level_for_scale(const RAtlas *a, float scale)
{
    /*  Switch at the geometric means of the neighbouring native scales
     *  (0.25, 0.5, 1.0), so no set is ever stretched by more than sqrt(2)
     *  in either direction. */
    int want = 32, i, best = 0;
    if (scale < 0.35355339f)
        want = 8;
    else if (scale < 0.70710678f)
        want = 16;

    for (i = 0; i < a->n_levels; ++i)
    {
        if (a->level[i].zoom == want)
            return &a->level[i];
        if (abs((int) a->level[i].zoom - want) <
            abs((int) a->level[best].zoom - want))
            best = i;
    }
    return a->n_levels ? &a->level[best] : NULL;
}
