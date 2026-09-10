/*  The incremental rebuild.  An edit changes a few tiles.  The mesh is
 *  kept in chunks (mesh.h, MESH_CHUNK), so a rebuild after an edit need
 *  only replace the chunks whose geometry changed.  Which those are is a
 *  closure over the tables the grading pass leaves behind.  A segment
 *  whose tiles touch a dirty tile has a new fit.  A junction at its end
 *  has a new box.  So every segment ending there has new trims.  A band
 *  band near the dirty tiles has a new fit.  So has every spur on it and
 *  the line at each spur's foot.  A spur beside a changed junction has a
 *  new join, and its band carries the lane drop.  Their tiles, dilated
 *  by a tile or two, name the chunks.  The building pass then runs as it
 *  always does.  Every table is complete, the traffic's graph and the
 *  lane model are whole.  But the terrain loop skips the other chunks'
 *  tiles.  The one triangle emitter drops what falls outside them
 *  (mesh_want_xy, by the same first-vertex rule the partition keys on).
 *  The result is sorted by chunk and spliced into the previous mesh
 *  range by range.  What decides an edit is a snapshot of the city the
 *  last build saw: the tiles that differ are the dirty ones.  A change
 *  of anything else the build reads is a different build key.  The view,
 *  the knobs and the furniture switch are all such changes, and each
 *  takes a full build.  So does an edit touching more than an eighth of
 *  the map, or the same city again (nothing to do). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "city.h"
#include "dump.h"
#include "log.h"
#include "mesh/internal.h"
#include "mesh/mesh.h"
#include "pipeline.h"
#include "opt.h"
#include "script.h"

/*  The incremental rebuild's own scratch, kept between builds because
 *  every build wants the same three arrays at about the same size.  They
 *  are the pass's, not a mesh's: mesh_incr_free gives them back when the
 *  program is done with meshes altogether. */
static uint16_t *s_key;
static uint32_t *s_auxtmp;
static uint32_t *s_idtmp;

void mesh_incr_free(void)
{
    free(s_key), s_key = NULL;
    free(s_auxtmp), s_auxtmp = NULL;
    free(s_idtmp), s_idtmp = NULL;
}


#define NT       (R_MAP * R_MAP)
#define SEGS_MAX 65536

int s_incr_on; /* an incremental build is under way: the emitter tests mesh_want_xy */

static uint8_t  s_want[MESH_CHUNKS];
static uint8_t  s_dirty[NT];
static uint8_t  s_near1[NT]; /* the dirty tiles and their neighbors, from the build's start */
static int      s_ndirty;
static uint32_t s_old_n, s_old_nw; /* the previous lists' counts, while they stand aside */
static struct
{
    int      hot, reemit, nseg, hotb, nband, chunks;
    uint32_t emitted;
} s_stat;

static void dilate_into(const uint8_t *src, uint8_t *dst, int r);

int mesh_chunk_of(int32_t col, int32_t row)
{
    if (col < 0)
        col = 0;
    if (row < 0)
        row = 0;
    if (col >= R_MAP)
        col = R_MAP - 1;
    if (row >= R_MAP)
        row = R_MAP - 1;
    return (row / MESH_CHUNK) * (R_MAP / MESH_CHUNK) + col / MESH_CHUNK;
}

int mesh_want_xy(float x, float y)
{
    return s_want[mesh_chunk_of((int32_t)floorf(x), (int32_t)floorf(y))];
}

/*  A tile's faces reach a quarter step past its edges (the seabed, the
 *  water's back faces) and its walls stand on its far edge.  So a tile
 *  builds when its own chunk or any neighbor's does. */
int mesh_want_tile(int32_t col, int32_t row)
{
    int32_t dc, dr;
    if (!s_incr_on)
        return 1;
    for (dr = -1; dr <= 1; ++dr)
        for (dc = -1; dc <= 1; ++dc)
            if (s_want[mesh_chunk_of(col + dc, row + dr)])
                return 1;
    return 0;
}

/* ---- the partition ------------------------------------------------------ */

/*  A stable counting sort of a triangle list by chunk.  The opaque list
 *  is sorted by terrain or network as well.  The triangles before
 *  `n_split` are the terrain's.  `start`/`count` come out in vertices. */
/*  ... and `aux`, one entry a triangle, is permuted the same way.  The
 *  component ids must follow their geometry through the chunk sort or
 *  the inspector would name whatever ended up in the slot. */
static int bucket_sort(const RMeshVert *src, uint32_t n, int split, uint32_t n_split, RMeshVert *dst, uint32_t *start, uint32_t *count, int nk, uint32_t *aux)
{
    static uint32_t  key_cap;
    uint32_t         fill[2 * MESH_CHUNKS], ntri = n / 3u, t, acc = 0;
    int              k;
    memset(count, 0, (size_t)nk * sizeof *count);
    memset(start, 0, (size_t)nk * sizeof *start);
    if (ntri == 0)
        return 0;
    if (key_cap < ntri)
    {
        uint16_t *nkey = (uint16_t *)realloc(s_key, ntri * sizeof *s_key);
        if (!nkey)
            return -1;
        s_key     = nkey;
        key_cap = ntri;
    }
    for (t = 0; t < ntri; ++t)
    {
        const RMeshVert *v0 = &src[3u * t];
        int              c  = mesh_chunk_of((int32_t)floorf(v0->pos[0]), (int32_t)floorf(v0->pos[1]));
        if (split) /* the opaque list: terrain before n_split, networks after (even when nothing follows) */
            c = 2 * c + (3u * t >= n_split ? 1 : 0);
        s_key[t] = (uint16_t)c;
        ++count[c];
    }
    for (k = 0; k < nk; ++k)
    {
        start[k] = acc * 3u;
        fill[k]  = start[k];
        acc += count[k];
    }
    {
        static uint32_t  auxcap;
        if (aux)
        {
            if (auxcap < ntri)
            {
                uint32_t *na = (uint32_t *)realloc(s_auxtmp, ntri * sizeof *na);
                if (!na)
                    return -1;
                s_auxtmp = na;
                auxcap = ntri;
            }
            memcpy(s_auxtmp, aux, ntri * sizeof *aux);
        }
        for (t = 0; t < ntri; ++t)
        {
            memcpy(&dst[fill[s_key[t]]], &src[3u * t], 3u * sizeof(RMeshVert));
            if (aux)
                aux[fill[s_key[t]] / 3u] = s_auxtmp[t];
            fill[s_key[t]] += 3u;
        }
    }
    for (k = 0; k < nk; ++k)
        count[k] *= 3u;
    return 0;
}

static int reserve(RMeshVert **buf, uint32_t *cap, uint32_t need)
{
    RMeshVert *nb;
    if (*cap >= need)
        return 0;
    nb = (RMeshVert *)realloc(*buf, (size_t)need * sizeof *nb);
    if (!nb)
        return -1;
    *buf = nb;
    *cap = need;
    return 0;
}

static void swap_lists(RMeshVert **a, uint32_t *ca, RMeshVert **b, uint32_t *cb)
{
    RMeshVert *t  = *a;
    uint32_t   tc = *ca;
    *a            = *b;
    *ca           = *cb;
    *b            = t;
    *cb           = tc;
}

/*  After a full build: both lists sorted by chunk, the ranges set. */
int mesh_partition(RMesh *m)
{
    m->ranged = 0;
    if (m->n_land == 0)
        return 0;
    if (reserve(&m->bucket, &m->cap_bucket, m->n_land) != 0 || reserve(&m->wbucket, &m->cap_wbucket, m->n_water) != 0)
        return -1;
    if (bucket_sort(m->land, m->n_land, 1, m->n_terrain, m->bucket, m->range_start, m->range_count, 2 * MESH_CHUNKS, m->tri_comp) != 0)
        return -1;
    swap_lists(&m->land, &m->cap_land, &m->bucket, &m->cap_bucket);
    if (bucket_sort(m->water, m->n_water, 0, 0, m->wbucket, m->wrange_start, m->wrange_count, MESH_CHUNKS, NULL) != 0)
        return -1;
    if (m->n_water)
        swap_lists(&m->water, &m->cap_water, &m->wbucket, &m->cap_wbucket);
    m->ranged = 1;
    memset(m->chunk_changed, 1, sizeof m->chunk_changed);
    return 0;
}

/* ---- the edit ----------------------------------------------------------- */

int mesh_incr_begin(RMesh *m, const RCity *c, const void *key, size_t keylen)
{
    const RCity *o = m->snap;
    int32_t      i;
    s_incr_on = 0;
    s_ndirty  = 0;
    if (g_dev.no_incr || !o || !m->snap_ok || !m->ranged || keylen != m->key_len || memcmp(key, m->key, keylen) != 0 ||
        o->rotation != c->rotation)
    {
        if (g_dev.times)
            dumpf("time  full build: %s\n", g_dev.no_incr ? "--no-incr" : !o || !m->snap_ok                                      ? "no snapshot"
                                                                      : !m->ranged                                               ? "not partitioned"
                                                                      : keylen != m->key_len || memcmp(key, m->key, keylen) != 0 ? "a different key"
                                                                                                                                 : "a different rotation");
        return 0;
    }
    {
        int by[6] = {0};
        for (i = 0; i < NT; ++i)
        {
            int     f[6] = {o->altm[i] != c->altm[i], o->xbld[i] != c->xbld[i], o->xzon[i] != c->xzon[i], o->xter[i] != c->xter[i], o->xund[i] != c->xund[i], o->xbit[i] != c->xbit[i]};
            uint8_t d    = (uint8_t)(f[0] | f[1] | f[2] | f[3] | f[4] | f[5]);
            int     k;
            for (k = 0; k < 6; ++k)
                by[k] += f[k];
            s_dirty[i] = d;
            s_ndirty += d;
        }
        if (s_ndirty == 0)
            return -1;
        if (s_ndirty > NT / 8)
        {
            if (g_dev.times)
            {
                dumpf("time  full build: %d tiles changed (altm %d, xbld %d, xzon %d, xter %d, xund %d, xbit %d)\n", s_ndirty, by[0], by[1], by[2], by[3], by[4], by[5]);
            }
            return 0;
        }
    }
    /*  The old lists step aside, still sorted and ranged.  The build
     *  emits into the scratch, which the last partition left as large. */
    s_old_n  = m->n_land;
    s_old_nw = m->n_water;
    swap_lists(&m->land, &m->cap_land, &m->bucket, &m->cap_bucket);
    swap_lists(&m->water, &m->cap_water, &m->wbucket, &m->cap_wbucket);
    memset(s_want, 0, sizeof s_want);
    memset(&s_stat, 0, sizeof s_stat);
    memset(s_near1, 0, sizeof s_near1);
    dilate_into(s_dirty, s_near1, 1);
    /*  The last build's component ids, kept while this one overwrites the
     *  array: the chunks that stand take theirs from here (meet). */
    {
        uint32_t ntri = s_old_n / 3u;
        if (m->cap_tri_comp_old < ntri)
        {
            uint32_t *no = (uint32_t *)realloc(m->tri_comp_old, (ntri ? ntri : 1u) * sizeof *no);
            if (!no)
                return 0; /* no room to remember them: a full build says everything again */
            m->tri_comp_old     = no;
            m->cap_tri_comp_old = ntri;
        }
        if (ntri && m->tri_comp)
            memcpy(m->tri_comp_old, m->tri_comp, (size_t)ntri * sizeof *m->tri_comp_old);
        m->n_tri_comp_old = ntri;
    }
    s_incr_on = 1;
    return 1;
}

/*  Did the edit come within a tile of this one?  Never, in a full build. */
int mesh_incr_near(int32_t col, int32_t row)
{
    if (!s_incr_on || col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return 0;
    return s_near1[row * R_MAP + col];
}

void mesh_incr_abort(RMesh *m)
{
    if (!s_incr_on)
        return;
    swap_lists(&m->land, &m->cap_land, &m->bucket, &m->cap_bucket);
    swap_lists(&m->water, &m->cap_water, &m->wbucket, &m->cap_wbucket);
    m->n_land  = s_old_n;
    m->n_water = s_old_nw;
    m->snap_ok = 0; /* the next build is a full one */
    s_incr_on  = 0;
}

int mesh_incr_snapshot(RMesh *m, const RCity *c, const void *key, size_t keylen)
{
    if (keylen > sizeof m->key)
        return -1;
    if (!m->snap)
    {
        m->snap = malloc(sizeof *c); /* one RCity.  The field is a void pointer, so size it by the city */
        if (!m->snap)
            return -1;
    }
    memcpy(m->snap, c, sizeof *c);
    memcpy(m->key, key, keylen);
    m->key_len = keylen;
    m->snap_ok = 1;
    return 0;
}

int mesh_incr_dirty(void)
{
    return s_incr_on ? s_ndirty : 0;
}

/* ---- the closure -------------------------------------------------------- */

static void dilate_into(const uint8_t *src, uint8_t *dst, int r)
{
    int32_t col, row, dc, dr;
    for (row = 0; row < R_MAP; ++row)
        for (col = 0; col < R_MAP; ++col)
        {
            if (!src[row * R_MAP + col])
                continue;
            for (dr = -r; dr <= r; ++dr)
                for (dc = -r; dc <= r; ++dc)
                {
                    int32_t nc = col + dc, nr = row + dr;
                    if (nc >= 0 && nr >= 0 && nc < R_MAP && nr < R_MAP)
                        dst[nr * R_MAP + nc] = 1;
                }
        }
}

static void mark_around(uint8_t *dst, int32_t col, int32_t row, int r)
{
    int32_t dc, dr;
    for (dr = -r; dr <= r; ++dr)
        for (dc = -r; dc <= r; ++dc)
        {
            int32_t nc = col + dc, nr = row + dr;
            if (nc >= 0 && nr >= 0 && nc < R_MAP && nr < R_MAP)
                dst[nr * R_MAP + nc] = 1;
        }
}

static int any_near(const uint8_t *mask, int32_t col, int32_t row, int r)
{
    int32_t dc, dr;
    for (dr = -r; dr <= r; ++dr)
        for (dc = -r; dc <= r; ++dc)
        {
            int32_t nc = col + dc, nr = row + dr;
            if (nc >= 0 && nr >= 0 && nc < R_MAP && nr < R_MAP && mask[nr * R_MAP + nc])
                return 1;
        }
    return 0;
}

static void want_all(void)
{
    memset(s_want, 1, sizeof s_want);
}

/*  After the grading pass, with its tables complete: which chunks the
 *  building pass emits into.  `lines` says the tables were filled. */
/*  How far the closure reaches, in tiles, and every distance in it the
 *  SCRIPT'S (scripts/incr.lua).  Read once a build: the closure runs
 *  once, so a rule here costs one call.
 *
 *  With no rule there are no distances, and a closure with no distances
 *  would predict too few chunks and leave the last build's triangles
 *  standing.  So the answer to a missing rule is every chunk, which is
 *  slow and correct, rather than a guess, which is fast and wrong. */
static const char *const REACH[] = {"band_fit", "spur", "band_ground", "band_spur", "band_margin", "segment"};
enum
{
    R_BAND_FIT = 0,
    R_SPUR,
    R_BAND_GROUND,
    R_BAND_SPUR,
    R_BAND_MARGIN,
    R_SEGMENT,
    R_N
};

void mesh_incr_closure(int lines)
{
    static uint8_t near2[NT], changed[NT], want_t[NT], hot[SEGS_MAX], reemit[SEGS_MAX], hotb[SEGS_MAX];
    const uint8_t *near1 = s_near1;
    float          reach[R_N];
    int32_t        t;
    int            i, k, ns = 0, nb = 0;
    if (!s_incr_on)
        return;
    memset(reach, 0, sizeof reach);
    if (!script_numbers("incr_reach", REACH, reach, R_N))
    {
        R_ERR("mesh", "no arc.numbers(\"incr_reach\"): every chunk is built again");
        want_all();
        s_stat.chunks = MESH_CHUNKS;
        return;
    }
    memset(near2, 0, sizeof near2);
    memset(changed, 0, sizeof changed);
    memset(want_t, 0, sizeof want_t);
    dilate_into(s_dirty, near2, (int)reach[R_BAND_FIT]);
    memcpy(changed, near1, sizeof changed);
    if (lines)
    {
        ns = seg_table_count();
        nb = band_count();
        if (ns > SEGS_MAX || nb > SEGS_MAX || nb < 0)
        {
            want_all(); /* beyond the closure's tables: every chunk, still correct */
            s_stat.chunks = MESH_CHUNKS;
            return;
        }
        /* a segment through the dirty tiles' neighborhood has a new fit.  Its ends have new boxes */
        for (i = 0; i < ns; ++i)
        {
            int32_t        col, row, cc, cr;
            const int32_t *tc, *tr;
            int            nt;
            hot[i] = 0;
            if (seg_table_get(i, &col, &row, &cc, &cr, &tc, &tr, &nt) != 0)
                continue;
            for (k = 0; k < nt && !hot[i]; ++k)
                if (tc[k] >= 0 && tr[k] >= 0 && tc[k] < R_MAP && tr[k] < R_MAP && near1[tr[k] * R_MAP + tc[k]])
                    hot[i] = 1;
            if (hot[i])
            {
                changed[row * R_MAP + col] = 1;
                changed[cr * R_MAP + cc]   = 1;
                ++s_stat.hot;
            }
        }
        /* a band near the dirty tiles has a new fit: the free-air check reads two tiles out */
        for (i = 0; i < nb; ++i)
        {
            const int32_t *bt;
            int            n;
            hotb[i] = 0;
            if (band_get(i, &bt, &n) != 0)
                continue;
            for (k = 0; k < n && !hotb[i]; ++k)
                if (near2[bt[k]])
                    hotb[i] = 1;
        }
        /* a spur beside a changed tile has a new join, and its band carries the lane drop */
        for (t = 0; t < NT; ++t)
        {
            int32_t col = t % R_MAP, row = t / R_MAP;
            if (!lane_spur_tile(col, row) || !any_near(changed, col, row, (int)reach[R_SPUR]))
                continue;
            for (i = 0; i < nb; ++i)
            {
                const int32_t *bt;
                int            n;
                if (hotb[i] || band_get(i, &bt, &n) != 0)
                    continue;
                for (k = 0; k < n; ++k)
                    if (abs(bt[k] % R_MAP - col) <= 1 && abs(bt[k] / R_MAP - row) <= 1)
                    {
                        hotb[i] = 2;
                        break;
                    }
            }
        }
        /*  A hot band's spurs and the lines at their feet, and the lines
         *  its ends become: two tiles out.  A hot band whose fit came
         *  out as before changes geometry only where the ground under it
         *  did.  Its profile follows the ground, eased at a sixth of a
         *  level a tile.  It also changes where a lane drop did.  Those
         *  tiles of it, eight out from the edit and six from a changed
         *  spur, with the same margin.  A band whose fit changed is
         *  drawn again whole. */
        for (i = 0; i < nb; ++i)
        {
            const int32_t *bt;
            int            n, whole;
            if (!hotb[i] || band_get(i, &bt, &n) != 0)
                continue;
            ++s_stat.hotb;
            whole = !seg_table_unchanged(seg_table_band_index(i));
            for (k = 0; k < n; ++k)
            {
                int32_t bc = bt[k] % R_MAP, br = bt[k] / R_MAP;
                if (!whole && !any_near(s_dirty, bc, br, (int)reach[R_BAND_GROUND]) &&
                    !(hotb[i] == 2 && any_near(changed, bc, br, (int)reach[R_BAND_SPUR])))
                    continue;
                mark_around(changed, bc, br, (int)reach[R_BAND_MARGIN]);
                mark_around(want_t, bc, br, (int)reach[R_BAND_MARGIN]);
            }
        }
        /* every segment through a changed tile is drawn again, a tile out */
        for (i = 0; i < ns; ++i)
        {
            int32_t        col, row, cc, cr;
            const int32_t *tc, *tr;
            int            nt;
            reemit[i] = 0;
            if (seg_table_get(i, &col, &row, &cc, &cr, &tc, &tr, &nt) != 0)
                continue;
            for (k = 0; k < nt && !reemit[i]; ++k)
                if (tc[k] >= 0 && tr[k] >= 0 && tc[k] < R_MAP && tr[k] < R_MAP && changed[tr[k] * R_MAP + tc[k]])
                    reemit[i] = 1;
            if (!reemit[i])
                continue;
            ++s_stat.reemit;
            for (k = 0; k < nt; ++k)
                if (tc[k] >= 0 && tr[k] >= 0 && tc[k] < R_MAP && tr[k] < R_MAP)
                    mark_around(want_t, tc[k], tr[k], (int)reach[R_SEGMENT]);
        }
    }
    s_stat.nseg  = ns;
    s_stat.nband = nb;
    /* the ground beside an edit: a face reads its neighbors' heights */
    for (t = 0; t < NT; ++t)
        if (near1[t])
            want_t[t] = 1;
    for (t = 0; t < NT; ++t)
        if (want_t[t])
            s_want[mesh_chunk_of(t % R_MAP, t / R_MAP)] = 1;
    for (k = 0; k < MESH_CHUNKS; ++k)
        s_stat.chunks += s_want[k];
}

/* ---- the meet --------------------------------------------------------- */

/*  `aux` and `aux_old` are the component ids of the new list and of the
 *  last build's.  A chunk that stands keeps its old ids, a chunk redrawn
 *  takes the new ones, exactly as its triangles do.  So the inspector
 *  names the component that actually drew each triangle. */
static int meet(RMeshVert **list, uint32_t *n, uint32_t *cap, int split, uint32_t n_split, const RMeshVert *old, uint32_t *start, uint32_t *count, int nk, uint32_t *aux, const uint32_t *aux_old)
{
    uint32_t   pstart[2 * MESH_CHUNKS], pcount[2 * MESH_CHUNKS], ostart[2 * MESH_CHUNKS], ocount[2 * MESH_CHUNKS];
    uint32_t   need = 0, pos = 0;
    RMeshVert *tmp = NULL;
    int        k;
    memcpy(ostart, start, (size_t)nk * sizeof *start);
    memcpy(ocount, count, (size_t)nk * sizeof *count);
    if (*n)
    {
        tmp = (RMeshVert *)malloc((size_t)*n * sizeof *tmp);
        if (!tmp)
            return -1;
    }
    /*  An edit's re-sort of one list: the component ids are the full
     *  build's and are left alone here.  So the inspector names them
     *  from the last full build until the next one. */
    if (bucket_sort(*list, *n, split, n_split, tmp, pstart, pcount, nk, aux) != 0)
    {
        free(tmp);
        return -1;
    }
    for (k = 0; k < nk; ++k)
        need += s_want[nk == MESH_CHUNKS ? k : k / 2] ? pcount[k] : ocount[k];
    if (reserve(list, cap, need) != 0)
    {
        free(tmp);
        return -1;
    }
    {
        static uint32_t  idcap;
        if (aux && aux_old)
        {
            uint32_t ntri = need / 3u;
            if (idcap < ntri)
            {
                uint32_t *ni = (uint32_t *)realloc(s_idtmp, (ntri ? ntri : 1u) * sizeof *ni);
                if (!ni)
                {
                    free(tmp);
                    return -1;
                }
                s_idtmp = ni;
                idcap = ntri;
            }
        }
        for (k = 0; k < nk; ++k)
        {
            int              w   = s_want[nk == MESH_CHUNKS ? k : k / 2];
            const RMeshVert *src = w ? &tmp[pstart[k]] : &old[ostart[k]];
            uint32_t         cnt = w ? pcount[k] : ocount[k];
            if (cnt)
                memcpy(&(*list)[pos], src, (size_t)cnt * sizeof **list);
            if (aux && aux_old && cnt)
            {
                const uint32_t *isrc = w ? &aux[pstart[k] / 3u] : &aux_old[ostart[k] / 3u];
                memcpy(&s_idtmp[pos / 3u], isrc, (size_t)(cnt / 3u) * sizeof *s_idtmp);
            }
            start[k] = pos;
            count[k] = cnt;
            pos += cnt;
        }
        if (aux && aux_old && pos)
            memcpy(aux, s_idtmp, (size_t)(pos / 3u) * sizeof *aux);
    }
    *n = pos;
    free(tmp);
    return 0;
}

int mesh_incr_end(RMesh *m)
{
    if (!s_incr_on)
        return -1;
    s_stat.emitted = m->n_land / 3u;
    if (meet(&m->land, &m->n_land, &m->cap_land, 1, m->n_terrain, m->bucket, m->range_start, m->range_count, 2 * MESH_CHUNKS, m->tri_comp, m->tri_comp_old) != 0 ||
        meet(&m->water, &m->n_water, &m->cap_water, 0, 0, m->wbucket, m->wrange_start, m->wrange_count, MESH_CHUNKS, NULL, NULL) != 0)
        return -1;
    m->ranged = 1;
    memcpy(m->chunk_changed, s_want, sizeof m->chunk_changed);
    if (g_dev.times)
        dumpf("time  incremental  %d dirty tiles; %d hot, %d drawn again of %d segments; %d of %d bands; %d of %d chunks; %u triangles emitted, %u now\n",
              s_ndirty,
              s_stat.hot,
              s_stat.reemit,
              s_stat.nseg,
              s_stat.hotb,
              s_stat.nband,
              s_stat.chunks,
              MESH_CHUNKS,
              (unsigned)s_stat.emitted,
              (unsigned)(m->n_land / 3u));
    s_incr_on = 0;
    return 0;
}

/* ---- the check ---------------------------------------------------------- */

/*  Every triangle in a range keys to that range's chunk, the ranges tile
 *  the list: what a meet must keep true. */
int mesh_ranges_check(const RMesh *m, int verbose)
{
    uint32_t bad = 0, pos = 0, i;
    int      k;
    if (!m->ranged)
    {
        if (verbose)
            dumpf("chunks  the mesh is not partitioned\n");
        return m->n_land ? 1 : 0;
    }
    for (k = 0; k < 2 * MESH_CHUNKS; ++k)
    {
        if (m->range_start[k] != pos)
            ++bad;
        pos += m->range_count[k];
        for (i = 0; i < m->range_count[k]; i += 3)
        {
            const RMeshVert *v = &m->land[m->range_start[k] + i];
            if (mesh_chunk_of((int32_t)floorf(v->pos[0]), (int32_t)floorf(v->pos[1])) != k / 2)
                ++bad;
        }
    }
    if (pos != m->n_land)
        ++bad;
    pos = 0;
    for (k = 0; k < MESH_CHUNKS; ++k)
    {
        if (m->wrange_start[k] != pos)
            ++bad;
        pos += m->wrange_count[k];
        for (i = 0; i < m->wrange_count[k]; i += 3)
        {
            const RMeshVert *v = &m->water[m->wrange_start[k] + i];
            if (mesh_chunk_of((int32_t)floorf(v->pos[0]), (int32_t)floorf(v->pos[1])) != k)
                ++bad;
        }
    }
    if (pos != m->n_water)
        ++bad;
    if (verbose)
        dumpf("chunks  %d land and %d water ranges, %u triangles out of place\n", 2 * MESH_CHUNKS, MESH_CHUNKS, (unsigned)bad);
    return bad != 0;
}
