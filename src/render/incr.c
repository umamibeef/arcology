/*  incr.c: THE BUILD BY WINDOWS.  The world is kept in chunks (mesh.h,
 *  MESH_CHUNK), and every build lays a WINDOW of them.  A whole build is
 *  the window that is every chunk.  A window laid into the mesh standing
 *  replaces its chunks whole, and every other chunk stands as it is.
 *
 *  An edit is such a window: the chunks within net_halo (geo.c) of the
 *  tiles it changed.  What decides that is a snapshot of the city the
 *  last build saw, and the tiles that differ are the dirty ones.  A
 *  change of anything else the build reads is a different build key.
 *  The view and the knobs are both such changes, and
 *  each takes a whole build.  So does a change to more than an eighth of
 *  the map, and the same city again needs nothing. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "city.h"
#include "dump.h"
#include "geo.h"
#include "incr.h"
#include "mesh/ground_bytes.h"
#include "mesh/mesh.h"
#include "opt.h"
#include "show.h"

#define NT (r_map * r_map)

/*  Each tile this build changed, INCR_CHANGE_* (incr.h). */
R_MAP_BUF(static, uint8_t, s_dirty, 1)
static int      s_ndirty;
static uint32_t s_old_n, s_old_nw; /* the previous lists' counts, while they stand aside */
/*  The window this build lays (incr_window_set, incr_window_next) and
 *  whether it is laid into the mesh standing.  An edit's window is boxes
 *  of its own, one round each group of the tiles it changed. */
static int     s_win_on, s_win_read, s_win_at; /* s_win_at: where the next of the switch's list starts */
static int32_t s_win[4];
static int     s_win_build; /* this build lays a window into the mesh standing */
static int     s_win_edit;  /* and the window is an edit's, s_edit */
static int32_t s_edit[INCR_BOXES][4];
static int     s_n_edit;
static int     s_quiet, s_quiet_next; /* this build, and the next, are the clock's: they show nothing */

/*  The INCR_CHANGE_ value of tile i, from the city o to c.
 *  Bare land, rubble and the forest's stages are one ground to every
 *  reader but the trees, and a tile's trees read only the tile.  So that
 *  change reaches its own chunk alone.  `f` says which layers moved:
 *  ALTM, XBLD, XZON, XTER, XUND, XBIT and the street layer. */
static uint8_t dirty_of(const RCity *o, const RCity *c, int32_t i, int f[7])
{
    f[0] = o->map.altm[i] != c->map.altm[i], f[1] = o->xbld[i] != c->xbld[i], f[2] = o->xzon[i] != c->xzon[i];
    f[3] = o->map.xter[i] != c->map.xter[i], f[4] = o->xund[i] != c->xund[i], f[5] = o->xbit[i] != c->xbit[i];
    f[6] = o->street[i] != c->street[i];
    if (!(f[0] | f[1] | f[2] | f[3] | f[4] | f[5] | f[6]))
        return 0;
    return f[0] | f[2] | f[3] | f[4] | f[5] | f[6] || tile_built(o->xbld[i]) || tile_built(c->xbld[i]) ? INCR_CHANGE_FAR : INCR_CHANGE_NEAR;
}

int incr_pending(const RMesh *m, const RCity *c)
{
    const RCity *o = m->snap;
    int32_t      i;
    int          most = 0;
    if (!o || !m->snap_ok)
        return INCR_CHANGE_FAR;
    for (i = 0; i < NT && most < INCR_CHANGE_FAR; ++i)
    {
        int     f[7];
        uint8_t d = dirty_of(o, c, i, f);
        most      = d > most ? d : most;
    }
    return most;
}

void incr_quiet_next(void)
{
    s_quiet_next = 1;
}

int incr_dirty_box(int32_t box[4])
{
    int32_t t, n = 0;
    box[0] = box[1] = r_map;
    box[2] = box[3] = -1;
    for (t = 0; t < NT; ++t)
        if (s_dirty[t])
        {
            int32_t col = t % r_map, row = t / r_map;
            box[0] = col < box[0] ? col : box[0];
            box[1] = row < box[1] ? row : box[1];
            box[2] = col > box[2] ? col : box[2];
            box[3] = row > box[3] ? row : box[3];
            ++n;
        }
    return n ? 0 : -1;
}

/*  THE WINDOW a build lays, in chunks, as the tiles of the chunks it
 *  covers.  --build-window names a list of them in tiles, and the first
 *  is in use until incr_window_next steps on.  incr_window_set names
 *  one outright.  The whole map where none is named. */

void incr_window_set(const int32_t tiles[4])
{
    s_win_read = 1;
    s_win_on   = tiles != NULL;
    if (!tiles)
        return;
    s_win[0] = (tiles[0] < tiles[2] ? tiles[0] : tiles[2]) / MESH_CHUNK * MESH_CHUNK;
    s_win[1] = (tiles[1] < tiles[3] ? tiles[1] : tiles[3]) / MESH_CHUNK * MESH_CHUNK;
    s_win[2] = ((tiles[0] < tiles[2] ? tiles[2] : tiles[0]) / MESH_CHUNK + 1) * MESH_CHUNK;
    s_win[3] = ((tiles[1] < tiles[3] ? tiles[3] : tiles[1]) / MESH_CHUNK + 1) * MESH_CHUNK;
}

int incr_window_next(void)
{
    const char *s = g_dev.build_window;
    int32_t     t[4];
    int         n = 0;
    if (!s || sscanf(s + s_win_at, "%d,%d,%d,%d%n", &t[0], &t[1], &t[2], &t[3], &n) != 4)
        return 0;
    s_win_at += n;
    if (s[s_win_at] == ';')
        ++s_win_at;
    incr_window_set(t);
    return 1;
}

int incr_window(int32_t box[INCR_BOXES][4])
{
    if (!s_win_read)
    {
        s_win_read = 1;
        incr_window_next();
    }
    if (s_win_edit)
    {
        memcpy(box, s_edit, (size_t)s_n_edit * sizeof s_edit[0]);
        return s_n_edit;
    }
    if (s_win_on)
        memcpy(box[0], s_win, sizeof s_win);
    return s_win_on;
}

/*  Whether a tile lies in one of the boxes, grown by `out` tiles on every
 *  side.  Every tile where there are none. */
static int in_boxes(int32_t col, int32_t row, int out)
{
    int32_t b[INCR_BOXES][4];
    int     n = incr_window(b), k;
    if (!n)
        return 1;
    for (k = 0; k < n; ++k)
        if (col >= b[k][0] - out && row >= b[k][1] - out && col < b[k][2] + out && row < b[k][3] + out)
            return 1;
    return 0;
}

/* ---- the partition ------------------------------------------------------ */

/*  Whether chunk `ch` lies in the window: every chunk where there is
 *  none. */
static int chunk_in(int ch)
{
    return in_boxes(ch % (r_map / MESH_CHUNK) * MESH_CHUNK, ch / (r_map / MESH_CHUNK) * MESH_CHUNK, 0);
}

/*  After a full build: each list packed, the ranges set.  The emitter
 *  filed every triangle in its chunk's list as it laid it, so there is
 *  nothing to sort. */
int mesh_partition(RMesh *m)
{
    m->ranged = 0;
    {
        int ch;
        for (ch = 0; ch < MESH_CHUNKS; ++ch)
            m->chunk_laid[ch] = (uint8_t)chunk_in(ch);
    }
    {
        double t0 = tms();
        mesh_lists_pack(m);
        if (g_dev.times)
            dumpf("time  packed and cut into levels of detail: %.1f ms\n", tms() - t0);
    }
    mesh_ranges_set(m);
    if (m->n_land == 0)
        return 0;
    m->ranged = 1;
    memset(m->chunk_changed, 1, sizeof m->chunk_changed);
    return 0;
}

/* ---- an edit's window ---------------------------------------------------- */

/*  THE BOXES OF AN EDIT.  Each tile it changed marks the chunks within
 *  net_halo (geo.c) of it.  A INCR_CHANGE_NEAR tile marks its own chunk.  Nothing a build lays reads the map further
 *  than that.  Each group of marked chunks that touch, sides or corners,
 *  is one box round them.  Boxes that overlap are one box.  Where there
 *  are more than INCR_BOXES, the two nearest are one.  So an edit in two
 *  places far apart lays the chunks near each, and none between. */
static void edit_boxes(void)
{
    static int     ix   = -1;
    int32_t        halo = (int32_t)geo_num(&ix, "net_halo"), per = r_map / MESH_CHUNK, t, k;
    static uint8_t mark[MESH_CHUNKS_CAP];
    static int     stack[MESH_CHUNKS_CAP];
    int32_t        box[MESH_CHUNKS_CAP > 64 ? 64 : MESH_CHUNKS_CAP][4];
    int            nb = 0, merged;
    memset(mark, 0, sizeof mark);
    for (t = 0; t < NT; ++t)
        if (s_dirty[t])
        {
            int32_t col = t % r_map, row = t / r_map, cx, cy, h = s_dirty[t] == INCR_CHANGE_NEAR ? 0 : halo;
            int32_t x0 = (col - h < 0 ? 0 : col - h) / MESH_CHUNK, x1 = (col + h >= r_map ? r_map - 1 : col + h) / MESH_CHUNK;
            int32_t y0 = (row - h < 0 ? 0 : row - h) / MESH_CHUNK, y1 = (row + h >= r_map ? r_map - 1 : row + h) / MESH_CHUNK;
            for (cy = y0; cy <= y1; ++cy)
                for (cx = x0; cx <= x1; ++cx)
                    mark[cy * per + cx] = 1;
        }
    /*  The groups, each as the chunks round it, by a flood over the marks. */
    for (t = 0; t < per * per; ++t)
    {
        int     sp = 0;
        int32_t b[4];
        if (mark[t] != 1)
            continue;
        b[0] = b[2] = t % per, b[1] = b[3] = t / per;
        mark[t] = 2, stack[sp++] = (int)t;
        while (sp)
        {
            int     u  = stack[--sp], dx, dy;
            int32_t ux = u % per, uy = u / per;
            b[0] = ux < b[0] ? ux : b[0], b[2] = ux > b[2] ? ux : b[2];
            b[1] = uy < b[1] ? uy : b[1], b[3] = uy > b[3] ? uy : b[3];
            for (dy = -1; dy <= 1; ++dy)
                for (dx = -1; dx <= 1; ++dx)
                {
                    int32_t vx = ux + dx, vy = uy + dy;
                    if (vx < 0 || vy < 0 || vx >= per || vy >= per || mark[vy * per + vx] != 1)
                        continue;
                    mark[vy * per + vx] = 2, stack[sp++] = vy * per + vx;
                }
        }
        if (nb < (int)(sizeof box / sizeof box[0]))
            memcpy(box[nb++], b, sizeof b);
        else
        {
            /*  More groups than boxes: this one joins the last. */
            box[nb - 1][0] = b[0] < box[nb - 1][0] ? b[0] : box[nb - 1][0];
            box[nb - 1][1] = b[1] < box[nb - 1][1] ? b[1] : box[nb - 1][1];
            box[nb - 1][2] = b[2] > box[nb - 1][2] ? b[2] : box[nb - 1][2];
            box[nb - 1][3] = b[3] > box[nb - 1][3] ? b[3] : box[nb - 1][3];
        }
    }
    /*  Boxes that overlap are one, and then the nearest two while there
     *  are too many.  A box is in chunks here, last column and row
     *  included. */
    do
    {
        int     i, j, bi = -1, bj = -1;
        int32_t gap_best = 1 << 30;
        merged           = 0;
        for (i = 0; i < nb && !merged; ++i)
            for (j = i + 1; j < nb && !merged; ++j)
            {
                int32_t gx  = box[j][0] > box[i][2] ? box[j][0] - box[i][2] - 1 : box[i][0] > box[j][2] ? box[i][0] - box[j][2] - 1
                                                                                                        : -1;
                int32_t gy  = box[j][1] > box[i][3] ? box[j][1] - box[i][3] - 1 : box[i][1] > box[j][3] ? box[i][1] - box[j][3] - 1
                                                                                                        : -1;
                int32_t gap = (gx > gy ? gx : gy);
                if (gap < 0 || (nb > INCR_BOXES && gap < gap_best))
                {
                    bi = i, bj = j, gap_best = gap;
                    if (gap < 0)
                        merged = 1;
                }
            }
        if (bi >= 0 && (merged || nb > INCR_BOXES))
        {
            box[bi][0] = box[bj][0] < box[bi][0] ? box[bj][0] : box[bi][0];
            box[bi][1] = box[bj][1] < box[bi][1] ? box[bj][1] : box[bi][1];
            box[bi][2] = box[bj][2] > box[bi][2] ? box[bj][2] : box[bi][2];
            box[bi][3] = box[bj][3] > box[bi][3] ? box[bj][3] : box[bi][3];
            memmove(box[bj], box[bj + 1], (size_t)(nb - bj - 1) * sizeof box[0]);
            --nb;
            merged = 1;
        }
    } while (merged);
    /*  In tiles, last column and row + 1, as every window is. */
    for (k = 0; k < nb; ++k)
    {
        s_edit[k][0] = box[k][0] * MESH_CHUNK, s_edit[k][1] = box[k][1] * MESH_CHUNK;
        s_edit[k][2] = (box[k][2] + 1) * MESH_CHUNK, s_edit[k][3] = (box[k][3] + 1) * MESH_CHUNK;
    }
    s_n_edit = nb;
    if (g_dev.times)
        dumpf("time  edit window: %d box%s\n", nb, nb == 1 ? "" : "es");
}

/* ---- the build's begin ------------------------------------------------ */

int incr_begin(RMesh *m, const RCity *c, const void *key, size_t keylen)
{
    const RCity *o = m->snap;
    int32_t      i;
    int32_t      win[INCR_BOXES][4];
    int          app_win;
    s_ndirty    = 0;
    s_win_build = 0;
    s_win_edit  = 0;
    s_quiet = s_quiet_next, s_quiet_next = 0;
    app_win = incr_window(win);
    if (g_dev.no_incr || !o || !m->snap_ok || !m->ranged || m->n_rl != MESH_RANGES * MESH_CHUNKS || keylen != m->key_len || memcmp(key, m->key, keylen) != 0)
    {
        if (g_dev.times)
            dumpf("time  full build%s: %s\n", app_win ? " of a window" : "", g_dev.no_incr ? "--disable incremental" : !o || !m->snap_ok ? "no snapshot"
                                                                                                                   : !m->ranged          ? "not partitioned"
                                                                                                                                         : "a different key");
        return INCR_WHOLE;
    }
    {
        int by[7] = {0};
        for (i = 0; i < NT; ++i)
        {
            int     f[7];
            uint8_t d = dirty_of(o, c, i, f);
            int     k;
            for (k = 0; k < 7; ++k)
                by[k] += f[k];
            s_dirty[i] = d;
            s_ndirty += d != 0;
        }
        /*  A change too large for an edit.  With a window named, every
         *  chunk is out of date, and each stands until a window lays it
         *  again.  With none, the build is whole. */
        if (s_ndirty > NT / 8)
        {
            if (g_dev.times)
                dumpf("time  full build%s: %d tiles changed (altm %d, xbld %d, xzon %d, xter %d, xund %d, xbit %d, street %d)\n",
                      app_win ? " of a window" : "",
                      s_ndirty,
                      by[0],
                      by[1],
                      by[2],
                      by[3],
                      by[4],
                      by[5],
                      by[6]);
            if (!app_win)
                return INCR_WHOLE;
            memset(m->chunk_laid, 0, sizeof m->chunk_laid);
            s_ndirty = 0;
        }
    }
    /*  AN EDIT IS A WINDOW.  Nothing a build lays reads the map further
     *  than net_halo (geo.c) tiles from it.  So an edit can change only
     *  the chunks that near its tiles.  Those chunks are laid again
     *  whole, and every other chunk stands. */
    if (s_ndirty > 0)
    {
        edit_boxes();
        s_win_edit = 1;
    }
    /*  A WINDOW INTO THE MESH STANDING, under the same key: the window's
     *  chunks are laid again whole and every other chunk stands as it
     *  is. */
    if (s_win_edit || (app_win && s_ndirty == 0))
    {
        s_old_n   = m->n_land;
        s_old_nw  = m->n_water;
        m->rl_old = m->rl, m->wl_old = m->wl;
        m->rl = m->wl = NULL;
        m->n_rl = m->n_wl = 0;
        s_win_build       = 1;
        return s_win_edit ? INCR_EDIT : INCR_WINDOW;
    }
    return INCR_STANDS;
}

/*  A window's build that failed leaves the mesh standing as it was. */
void incr_abort(RMesh *m)
{
    if (!s_win_build)
        return;
    mesh_lists_free(m->rl, m->n_rl);
    mesh_lists_free(m->wl, m->n_wl);
    m->rl = m->rl_old, m->wl = m->wl_old;
    m->n_rl = MESH_RANGES * MESH_CHUNKS, m->n_wl = MESH_CHUNKS;
    m->rl_old = m->wl_old = NULL;
    m->n_land             = s_old_n;
    m->n_water            = s_old_nw;
    m->snap_ok            = 0; /* the next build is a whole one */
    s_win_build           = 0;
    s_win_edit            = 0;
}

void incr_free(RMesh *m)
{
    if (m->snap)
        rcity_free((RCity *)m->snap);
    free(m->snap);
    m->snap    = NULL;
    m->snap_ok = 0;
}

int incr_snapshot(RMesh *m, const RCity *c, const void *key, size_t keylen)
{
    if (keylen > sizeof m->key)
        return -1;
    if (!m->snap)
    {
        m->snap = calloc(1, sizeof *c); /* one RCity.  The field is a void pointer, so size it by the city */
        if (!m->snap)
            return -1;
    }
    if (rcity_copy((RCity *)m->snap, c) != 0)
        return -1;
    memcpy(m->key, key, keylen);
    m->key_len = keylen;
    m->snap_ok = 1;
    return 0;
}

/*  The window's lists meet the mesh standing.  A chunk in the window
 *  takes what this build laid.  Every other chunk keeps its own lists,
 *  moved and not copied.  So a mesh built window by window is the mesh a
 *  whole build lays, chunk for chunk. */
int incr_window_end(RMesh *m)
{
    int32_t b[INCR_BOXES][4];
    int     ch;
    if (!s_win_build || !incr_window(b) || m->n_rl != MESH_RANGES * MESH_CHUNKS || m->n_wl != MESH_CHUNKS)
        return -1;
    s_win_build = 0;
    {
        double t0 = tms();
        mesh_lists_pack(m);
        if (g_dev.times)
            dumpf("time  packed and cut into levels of detail: %.1f ms\n", tms() - t0);
    }
    for (ch = 0; ch < MESH_CHUNKS; ++ch)
    {
        int in               = chunk_in(ch);
        m->chunk_changed[ch] = (uint8_t)in;
        m->chunk_laid[ch] |= (uint8_t)in;
        if (!in)
        {
            RMeshList q;
            int       r;
            for (r = MESH_RANGES * ch; r < MESH_RANGES * (ch + 1); ++r)
                q = m->rl[r], m->rl[r] = m->rl_old[r], m->rl_old[r] = q;
            q = m->wl[ch], m->wl[ch] = m->wl_old[ch], m->wl_old[ch] = q;
        }
    }
    mesh_ranges_set(m);
    m->ranged = m->n_land > 0;
    /*  An edit's network ranges, the last build's lists and this one's,
     *  for what the edit shows going and coming. */
    if (s_win_edit && !s_quiet)
    {
        for (ch = 0; ch < MESH_CHUNKS; ++ch)
            if (chunk_in(ch))
            {
                int r = MESH_RANGES * ch + MESH_RANGE_STANDS;
                show_chunk(r, &m->rl_old[r], &m->rl[r], m->range_start[r] / 3u);
            }
        show_settle();
    }
    s_win_edit = 0;
    mesh_lists_free(m->rl_old, MESH_RANGES * MESH_CHUNKS);
    mesh_lists_free(m->wl_old, MESH_CHUNKS);
    m->rl_old = m->wl_old = NULL;
    return 0;
}

int incr_window_building(void)
{
    return s_win_build;
}

int incr_window_edit(void)
{
    return s_win_edit && !s_quiet;
}
