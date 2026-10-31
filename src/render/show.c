/*  show.c: what an edit's build shows going and coming.  See show.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "geo.h"
#include "hash.h"
#include "log.h"
#include "mesh/shape.h"
#include "opt.h"
#include "prof.h"
#include "show.h"

#include <SDL3/SDL.h>

/*  The show's numbers, read from the geo store as edit_show_NAME.
 *
 *      How far from the edited tiles a piece the build changed may go.
 *      How fast the going wave runs out from them, in tiles a second.
 *      How long one fragment takes to go, in seconds.
 *      How fast the coming wave runs, and how long one fragment takes to come.
 *      Whether the coming wave runs in from its far edge, 1, or out.
 *      How wide a fragment is, in tiles.
 *      How long a fragment may wait past the wave, at most.
 *      How far a fragment drifts, in tiles.
 *      How far it dissolves as it goes, 1 to nothing.
 *      How high a going fragment is thrown, and how deep it falls, in levels.
 *      How high a coming fragment drops from. */
static const char *const NUMS[] = {"reach", "go_speed", "go_time", "come_speed", "come_time", "come_inward", "piece", "jitter", "scatter", "dissolve", "hop", "depth", "drop"};
enum
{
    N_REACH = 0,
    N_GO_SPEED,
    N_GO_TIME,
    N_COME_SPEED,
    N_COME_TIME,
    N_COME_INWARD,
    N_PIECE,
    N_JITTER,
    N_SCATTER,
    N_DISSOLVE,
    N_HOP,
    N_DEPTH,
    N_DROP,
    N_N
};

typedef struct
{
    ShapeId id;
    float   dist, far; /* how far the piece's nearest and farthest points lie from the edit */
    uint8_t same, back, kept;
} Cand;

/*  A new piece and an old one that share a triangle. */
typedef struct
{
    ShapeId now, old;
} Pair;

static int      s_on;
static uint8_t  s_state[SHOW_PIECES];   /* what the device draws each piece as */
static uint8_t  s_come[SHOW_PIECES];    /* the pieces that come once the new world is up */
static uint8_t  s_live[SHOW_PIECES];    /* the pieces a mesh holds triangles of */
static uint8_t  s_differs[SHOW_PIECES]; /* a new piece with a triangle the last build did not have */
static uint8_t  s_lost[SHOW_PIECES];    /* an old piece with a triangle this build does not have */
static uint8_t  s_back[SHOW_PIECES];    /* a new piece with a triangle of a piece that went */
static float    s_far[SHOW_PIECES];     /* how far the farthest point of a piece lies from the edit */
static Pair    *s_pair;
static uint32_t s_n_pair, s_cap_pair;
static int      s_states_new; /* the states changed since the GPU took them */
static int      s_armed;      /* an edit's build is under way, its going phase done */
static int      s_held;       /* the coming is decided and waits for the new world */
static int      s_moving;     /* some piece is not still */
static ShapeId  s_before;     /* the ids the last build left: below this are old */
static float    s_area[4];    /* the edited tiles: first column, first row, last + 1 of each */
static float    s_num[N_N];
static double   s_t0;       /* when the going phase began, in ms */
static float    s_leave_at; /* seconds from s_t0: the build is done and what it changed leaves */
static float    s_gone_at;  /* the last piece has gone */
static float    s_come_at;  /* the new world is up and its pieces come */
static float    s_come_far; /* how far out the farthest point of a coming piece lies */
static float    s_end_at;   /* the last piece has come */
static int      s_phase;
static Cand    *s_cand;
static int      s_n_cand, s_cap_cand;
static double   s_meet_ms; /* what telling the changed pieces apart cost this build */
/*  THE PRINT of each piece in the chunks an edit's window replaced: its
 *  triangles, exactly, in the order they are laid.  A piece of the last
 *  world by its old id and a piece of the new by its new one. */
static uint64_t s_print[SHOW_PIECES];
/*  The network slots the window replaced, the last world's list and the
 *  new one's, kept until the window is settled. */
static const RMeshList *s_old_l[SHOW_SLOTS], *s_now_l[SHOW_SLOTS];
static uint32_t         s_now_base[SHOW_SLOTS];
static uint8_t          s_fed[SHOW_SLOTS];
/*  WHICH TRIANGLES MOVE, of the mesh an edit's build made, by the
 *  triangle's place in the land list.  A triangle of a piece that changed
 *  is 1, and comes.  One of a piece that stands is 0.  A triangle the
 *  window did not replace is 2, and stands.  Read as the new world goes
 *  up (show_tri_key). */
static uint8_t *s_moves;
static uint32_t s_n_moves, s_cap_moves;
static int      s_keyed; /* s_moves holds this edit's answer */
static int      s_going; /* the old triangles the build did not make again are going */
/*  A key for each of the last world's triangles in each network slot the build drew again.  The device's key buffer is written again with them.  The key is SHOW_KEY_GONE where the build did not make the triangle again, else none. */
static uint32_t *s_old_id;
static uint32_t  s_n_old, s_cap_old_id;
static uint32_t  s_old_first[SHOW_SLOTS], s_old_n[SHOW_SLOTS];
static uint8_t   s_old_has[SHOW_SLOTS];
static uint32_t  s_n_gone; /* how many of them go */
static float     s_go_far; /* how far the farthest point of one lies from the edit */
static int       s_rekey;  /* the device's copy is still to be keyed again */

static float since(void)
{
    return (float)((prof_now() - s_t0) / 1000.0);
}

/*  The moment the pieces are drawn at: the clock, or the moment
 *  --show-at names, so a --shot can stand anywhere in the waves. */
static float drawn_at(void)
{
    return g_dev.show_at ? (float)atof(g_dev.show_at) : since();
}

static void reset_now(void)
{
    s_armed = 0;
    s_held  = 0;
    if (s_moving || s_end_at > 0.0f)
    {
        memset(s_state, 0, sizeof s_state);
        s_states_new = 1;
    }
    memset(s_come, 0, sizeof s_come);
    s_moving = 0;
    s_end_at = 0.0f;
    s_going  = 0;
    s_keyed  = 0;
}

/*  How far a piece's box lies from the edited tiles, on the ground. */
static float piece_dist(ShapeId id)
{
    float box[6], dx, dy;
    if (shape_get(id, NULL, NULL, box) != 0)
        return 1e9f;
    dx = fmaxf(0.0f, fmaxf(s_area[0] - box[3], box[0] - s_area[2]));
    dy = fmaxf(0.0f, fmaxf(s_area[1] - box[4], box[1] - s_area[3]));
    return sqrtf(dx * dx + dy * dy);
}

/*  How far a point lies from the edited tiles. */
static float point_dist(float x, float y)
{
    float dx = fmaxf(0.0f, fmaxf(s_area[0] - x, x - s_area[2]));
    float dy = fmaxf(0.0f, fmaxf(s_area[1] - y, y - s_area[3]));
    return sqrtf(dx * dx + dy * dy);
}

/*  How far the farthest MOVING point of each piece lies from the edited
 *  tiles: of the triangles with no twin in the last world.  The rest of
 *  the piece stands, so the coming wave need not start out there.  The
 *  triangles of no piece are counted under SHOW_KEY_COME. */
static void far_of(const RMesh *m)
{
    int k;
    for (k = MESH_RANGE_STANDS; k < m->n_rl; k += MESH_RANGES)
    {
        const RMeshList *l    = &m->rl[k];
        uint32_t         base = m->range_start[k] / 3u, i;
        for (i = 0; i < l->n / 3u; ++i)
        {
            uint32_t id = l->comp[i] < SHOW_PIECES && l->comp[i] < SHOW_KEY_COME ? l->comp[i] : SHOW_KEY_COME;
            uint32_t t  = base + i;
            int      v;
            if (!s_keyed || t >= s_n_moves || s_moves[t] != 1)
                continue;
            for (v = 0; v < 3; ++v)
            {
                const RMeshVert *c = mesh_list_corner(l, 3u * i + (uint32_t)v);
                s_far[id]          = fmaxf(s_far[id], point_dist(c->pos[0], c->pos[1]));
            }
        }
    }
}

static int cand_add(ShapeId id, float dist, int same, int back, int kept)
{
    if (s_n_cand >= s_cap_cand)
    {
        int   cap = s_cap_cand ? s_cap_cand * 2 : 1024;
        Cand *nc  = (Cand *)realloc(s_cand, (size_t)cap * sizeof *nc);
        if (!nc)
            return -1;
        s_cand     = nc;
        s_cap_cand = cap;
    }
    s_cand[s_n_cand].id   = id;
    s_cand[s_n_cand].dist = dist;
    s_cand[s_n_cand].far  = s_far[id];
    s_cand[s_n_cand].same = (uint8_t)same;
    s_cand[s_n_cand].back = (uint8_t)back;
    s_cand[s_n_cand].kept = (uint8_t)kept;
    ++s_n_cand;
    return 0;
}

/*  Which pieces a mesh holds triangles of in its network ranges.  The
 *  ground is drawn in shapes too, and a shape of grass and walls reads
 *  as a thing of parts.  So the ranges say what is built, not the
 *  material. */
static void live_of(const RMesh *m)
{
    int k;
    memset(s_live, 0, sizeof s_live);
    for (k = MESH_RANGE_STANDS; k < m->n_rl; k += MESH_RANGES)
    {
        const RMeshList *l = &m->rl[k];
        uint32_t         i;
        for (i = 0; i < l->n / 3u; ++i)
            if (l->comp[i] < SHOW_PIECES)
                s_live[l->comp[i]] = 1;
    }
}

/*  The going phase's candidates: what the edit is drawn on.  `edit` is
 *  the box round the tiles it changed.  Answers how many, or -1 where
 *  there is no edit to show. */
static int offer_going(const RMesh *m, const int32_t edit[4])
{
    ShapeId id, n = (ShapeId)shape_count();
    reset_now();
    if (!s_on || !m || !edit)
        return -1;
    memset(s_num, 0, sizeof s_num);
    for (int k = 0; k < N_N; ++k)
    {
        char name[48];
        snprintf(name, sizeof name, "edit_show_%s", NUMS[k]);
        geo_find(name, &s_num[k]);
    }
    if (s_num[N_GO_SPEED] <= 0.0f || s_num[N_GO_TIME] <= 0.0f ||
        s_num[N_COME_SPEED] <= 0.0f || s_num[N_COME_TIME] <= 0.0f)
        return -1;
    s_area[0] = (float)edit[0], s_area[1] = (float)edit[1];
    s_area[2] = (float)edit[2] + 1.0f, s_area[3] = (float)edit[3] + 1.0f;
    s_before   = n;
    s_t0       = prof_now();
    s_leave_at = 1e9f;
    s_gone_at  = 0.0f;
    s_come_at  = 1e9f;
    s_meet_ms  = 0.0;
    s_n_pair   = 0;
    memset(s_differs, 0, sizeof s_differs);
    memset(s_lost, 0, sizeof s_lost);
    memset(s_back, 0, sizeof s_back);
    memset(s_far, 0, sizeof s_far);
    memset(s_print, 0, sizeof s_print);
    memset(s_fed, 0, sizeof s_fed);
    /*  Nothing goes until the build is done.  Only then is it known which
     *  of the last world's triangles the build made again: those stand,
     *  and the rest go (offer_leaving).  So no piece is offered here. */
    (void)id, (void)n;
    s_n_cand  = 0;
    s_going   = 0;
    s_keyed   = 0;
    s_rekey   = 0;
    s_n_moves = 0;
    s_n_old   = 0;
    s_n_gone  = 0;
    s_go_far  = 0.0f;
    memset(s_old_has, 0, sizeof s_old_has);
    s_phase = SHOW_GOING;
    s_armed = 1;
    R_DBG("show", "going: armed until the build is done");
    return s_n_cand;
}

static int grow(void **p, uint32_t *cap, uint32_t want, size_t size)
{
    uint32_t c = *cap ? *cap : 4096u;
    void    *np;
    if (want <= *cap)
        return 0;
    while (c < want)
        c *= 2u;
    np = realloc(*p, (size_t)c * size);
    if (!np)
        return -1;
    *p   = np;
    *cap = c;
    return 0;
}

static void pair_add(ShapeId now, ShapeId old)
{
    if (s_n_pair && s_pair[s_n_pair - 1u].now == now && s_pair[s_n_pair - 1u].old == old)
        return;
    if (s_n_pair >= s_cap_pair)
    {
        uint32_t cap = s_cap_pair ? s_cap_pair * 2u : 4096u;
        Pair    *np  = (Pair *)realloc(s_pair, (size_t)cap * sizeof *np);
        if (!np)
            return;
        s_pair     = np;
        s_cap_pair = cap;
    }
    s_pair[s_n_pair].now = now;
    s_pair[s_n_pair].old = old;
    ++s_n_pair;
}

/*  One network slot an edit's window replaced: the last world's list
 *  and the new one's.  Each piece's print takes in its triangles there. */
static void chunk_now(int slot, const RMeshList *old, const RMeshList *now, uint32_t base)
{
    const RMeshList *l[2] = {old, now};
    int              w;
    if (!s_armed || slot < 0 || slot >= SHOW_SLOTS || !old || !now)
        return;
    s_old_l[slot] = old, s_now_l[slot] = now, s_now_base[slot] = base, s_fed[slot] = 1;
    for (w = 0; w < 2; ++w)
    {
        uint32_t i;
        for (i = 0; i < l[w]->n / 3u; ++i)
        {
            uint32_t  id = l[w]->comp[i];
            RMeshVert v[3];
            if (id >= SHOW_PIECES)
                continue;
            mesh_list_tri(l[w], i, v);
            s_print[id] = hash_bytes(s_print[id] ? s_print[id] : HASH_SEED, v, sizeof v);
        }
    }
}

/*  One piece by its NAME and its print, for the settle. */
typedef struct
{
    uint64_t name, print;
    ShapeId  id;
    uint8_t  used;
} Named;

static int named_cmp(const void *a, const void *b)
{
    const Named *x = (const Named *)a, *y = (const Named *)b;
    if (x->name != y->name)
        return x->name < y->name ? -1 : 1;
    if (x->print != y->print)
        return x->print < y->print ? -1 : 1;
    return x->id < y->id ? -1 : x->id > y->id;
}

static uint64_t name_of(ShapeId id)
{
    const char *s = shape_name(id);
    return hash_bytes(HASH_SEED, s ? s : "", s ? strlen(s) : 0);
}

/*  THE WINDOW SETTLED.  In the chunks replaced, a piece is the same
 *  piece where both worlds hold a piece of its name with its print.
 *  Every other piece of the last world there is lost and goes, and every
 *  other piece of the new one changed and comes.  A new piece comes back
 *  where a piece of its name went. */
static void settle_now(void)
{
    Named         *old = NULL, *now = NULL;
    uint32_t       n_old = 0, n_now = 0, cap_old = 0, cap_now = 0, i, j;
    double         t0 = prof_now();
    int            k;
    static uint8_t seen[SHOW_PIECES];
    if (!s_armed)
        return;
    memset(seen, 0, sizeof seen);
    /*  Each piece once, from the lists it lies in. */
    for (k = 0; k < SHOW_SLOTS; ++k)
    {
        int w;
        if (!s_fed[k])
            continue;
        for (w = 0; w < 2; ++w)
        {
            const RMeshList *l = w ? s_now_l[k] : s_old_l[k];
            for (i = 0; i < l->n / 3u; ++i)
            {
                ShapeId id = l->comp[i];
                Named  *x;
                if (id >= SHOW_PIECES || seen[id])
                    continue;
                seen[id] = 1;
                if (w ? grow((void **)&now, &cap_now, n_now + 1u, sizeof *now) : grow((void **)&old, &cap_old, n_old + 1u, sizeof *old))
                    continue;
                x        = w ? &now[n_now++] : &old[n_old++];
                x->name  = name_of(id);
                x->print = s_print[id];
                x->id    = id;
                x->used  = 0;
            }
        }
    }
    qsort(old, n_old, sizeof *old, named_cmp);
    qsort(now, n_now, sizeof *now, named_cmp);
    /*  The same name and the same print, one for one, in order. */
    for (i = 0, j = 0; i < n_old && j < n_now;)
    {
        if (old[i].name == now[j].name && old[i].print == now[j].print)
        {
            old[i].used = now[j].used = 1;
            pair_add(now[j].id, old[i].id);
            ++i, ++j;
        }
        else if (named_cmp(&old[i], &now[j]) < 0)
            ++i;
        else
            ++j;
    }
    for (i = 0; i < n_old; ++i)
        if (!old[i].used)
            s_lost[old[i].id] = 1;
    for (j = 0; j < n_now; ++j)
        if (!now[j].used)
            s_differs[now[j].id] = 1;
    /*  A changed piece comes back where a piece of its name went. */
    for (i = 0, j = 0; i < n_old && j < n_now;)
    {
        if (old[i].name == now[j].name)
        {
            if (!old[i].used && !now[j].used)
                pair_add(now[j].id, old[i].id);
            ++i, ++j;
        }
        else if (old[i].name < now[j].name)
            ++i;
        else
            ++j;
    }
    free(old), free(now);
    /*  Which triangles move, and the keys of the last world's. */
    for (k = 0; k < SHOW_SLOTS; ++k)
    {
        const RMeshList *o = s_old_l[k], *p = s_now_l[k];
        uint32_t         base, n_o, n_p;
        if (!s_fed[k])
            continue;
        base = s_now_base[k], n_o = o->n / 3u, n_p = p->n / 3u;
        if (grow((void **)&s_moves, &s_cap_moves, base + n_p, 1) != 0 ||
            grow((void **)&s_old_id, &s_cap_old_id, s_n_old + n_o, sizeof *s_old_id) != 0)
            continue;
        if (base + n_p > s_n_moves)
        {
            memset(s_moves + s_n_moves, 2, base + n_p - s_n_moves);
            s_n_moves = base + n_p;
        }
        s_keyed = 1;
        for (i = 0; i < n_p; ++i)
        {
            ShapeId id        = p->comp[i];
            s_moves[base + i] = (uint8_t)(id >= SHOW_PIECES || s_differs[id]);
        }
        s_old_first[k] = s_n_old;
        s_old_n[k]     = n_o;
        s_old_has[k]   = 1;
        for (i = 0; i < n_o; ++i)
        {
            ShapeId id = o->comp[i];
            if (id < SHOW_PIECES && !s_lost[id])
            {
                s_old_id[s_n_old + i] = SHOW_KEY_NONE;
                continue;
            }
            s_old_id[s_n_old + i] = SHOW_KEY_GONE;
            {
                int v;
                for (v = 0; v < 3; ++v)
                {
                    const RMeshVert *c = mesh_list_corner(o, 3u * i + (uint32_t)v);
                    s_go_far           = fmaxf(s_go_far, point_dist(c->pos[0], c->pos[1]));
                }
            }
            ++s_n_gone;
        }
        s_n_old += n_o;
    }
    memset(s_fed, 0, sizeof s_fed);
    s_meet_ms += prof_now() - t0;
}

/*  The leaving phase's candidates, once the build is done: the old
 *  pieces it lost a triangle of, other than those already going.  Answers
 *  how many, or -1 where the build was no edit being shown. */
static int offer_leaving(void)
{
    ShapeId id;
    if (!s_armed)
        return -1;
    s_leave_at = since();
    s_n_cand   = 0;
    /*  The old triangles the build did not make again go now, as a wave
     *  out from the edited tiles.  The device's copy of them is keyed
     *  again first (show_rekey). */
    if (s_n_gone)
    {
        s_going   = 1;
        s_moving  = 1;
        s_rekey   = 1;
        s_gone_at = s_leave_at + s_go_far / s_num[N_GO_SPEED] + s_num[N_JITTER] + s_num[N_GO_TIME];
    }
    else
        s_gone_at = s_leave_at;
    for (id = 0; id < s_before && id < SHOW_PIECES; ++id)
        if (s_lost[id] && s_state[id] != SHOW_GOING && cand_add(id, piece_dist(id), 0, 0, 0) != 0)
            break;
    s_phase = SHOW_LEAVING;
    R_DBG("show", "leaving: %u triangles go, %d pieces the build changed; told apart in %.1f ms; the build took %.2f s", s_n_gone, s_n_cand, s_meet_ms, (double)s_leave_at);
    return s_n_cand;
}

/*  The coming phase's candidates: the pieces the build made, and those
 *  that went and still stand.  The new world they come in waits for the
 *  last piece to go. */
static int offer_coming(const RMesh *m)
{
    ShapeId  id, n = (ShapeId)shape_count();
    uint32_t p;
    if (!s_armed)
        return -1;
    s_armed = 0;
    /*  A new piece comes back where it shares a triangle with one that
     *  went. */
    for (p = 0; p < s_n_pair; ++p)
        if (s_state[s_pair[p].old] == SHOW_GOING || s_state[s_pair[p].old] == SHOW_LEAVING)
            s_back[s_pair[p].now] = 1;
    live_of(m);
    far_of(m);
    s_n_cand = 0;
    for (id = s_before; id < n && id < SHOW_PIECES; ++id)
        if (s_live[id] && cand_add(id, piece_dist(id), !s_differs[id], s_back[id], 0) != 0)
            break;
    for (id = 0; id < s_before && id < SHOW_PIECES; ++id)
        if ((s_state[id] == SHOW_GOING || s_state[id] == SHOW_LEAVING) && s_live[id] && cand_add(id, piece_dist(id), 1, 1, 1) != 0)
            break;
    s_phase    = SHOW_COMING;
    s_come_far = 0.0f;
    s_held     = 1;
    R_DBG("show", "coming: %d pieces; the last goes at %.2f s", s_n_cand, (double)s_gone_at);
    return s_n_cand;
}

static int holds_now(void)
{
    return s_on && s_held && drawn_at() < s_gone_at;
}

static void arrive_now(void)
{
    ShapeId id;
    if (!s_held)
        return;
    s_held   = 0;
    s_moving = 0;
    s_going  = 0;
    s_keyed  = 0;
    for (id = 0; id < SHOW_PIECES; ++id)
    {
        s_state[id] = s_come[id] ? SHOW_COMING : SHOW_STILL;
        s_moving |= s_come[id];
    }
    memset(s_come, 0, sizeof s_come);
    s_state[SHOW_KEY_COME] = SHOW_COMING;
    s_moving               = 1;
    s_come_far             = fmaxf(s_come_far, s_far[SHOW_KEY_COME]);
    /*  The new world goes up on the first frame after the last piece has
     *  gone, or at once where nothing was going. */
    s_come_at    = fmaxf(since(), s_gone_at);
    s_end_at     = s_moving ? s_come_at + s_come_far / s_num[N_COME_SPEED] + s_num[N_JITTER] + s_num[N_COME_TIME] + 0.1f : 0.0f;
    s_states_new = 1;
}

static int count_now(void)
{
    return s_n_cand;
}

static int candidate_now(int i, float *dist, int *same, int *back, int *kept)
{
    if (i < 0 || i >= s_n_cand)
        return 0;
    *dist = s_cand[i].dist;
    *same = s_cand[i].same;
    *back = s_cand[i].back;
    *kept = s_cand[i].kept;
    return 1;
}

/*  A mark moves the clocks as it is made.  The last piece has gone once
 *  the wave has reached the far end of every piece that goes and that
 *  piece has sunk.  The coming wave starts from the far end of the
 *  farthest piece that comes. */
static void mark_now(int i)
{
    if (i < 0 || i >= s_n_cand)
        return;
    if (s_phase == SHOW_COMING)
    {
        s_come[s_cand[i].id] = 1;
        s_come_far           = fmaxf(s_come_far, s_cand[i].far);
        return;
    }
    s_state[s_cand[i].id] = (uint8_t)s_phase;
    s_gone_at             = fmaxf(s_gone_at, (s_phase == SHOW_LEAVING ? s_leave_at : 0.0f) + s_cand[i].far / s_num[N_GO_SPEED] + s_num[N_JITTER] + s_num[N_GO_TIME]);
    s_states_new          = 1;
    s_moving              = 1;
}

static int active_now(void)
{
    return s_on && (s_armed || s_held || s_moving);
}

static void uniform_now(float u[24])
{
    memset(u, 0, 24 * sizeof *u);
    if (!s_on || !s_moving)
        return;
    u[0]  = drawn_at();
    u[1]  = s_leave_at;
    u[2]  = s_come_at;
    u[3]  = s_leave_at;
    u[4]  = s_area[0];
    u[5]  = s_area[1];
    u[6]  = s_area[2];
    u[7]  = s_area[3];
    u[8]  = s_num[N_GO_SPEED];
    u[9]  = s_num[N_COME_SPEED];
    u[10] = s_num[N_GO_TIME];
    u[11] = s_num[N_COME_TIME];
    u[12] = s_come_far;
    u[13] = s_num[N_COME_INWARD] > 0.5f ? 1.0f : 0.0f;
    u[14] = s_going ? 1.0f : 0.0f; /* the old triangles keyed SHOW_KEY_GONE are going */
    u[15] = 1.0f;
    u[16] = fmaxf(0.05f, s_num[N_PIECE]);
    u[17] = s_num[N_JITTER];
    u[18] = s_num[N_SCATTER];
    u[19] = s_num[N_DISSOLVE];
    u[20] = s_num[N_HOP];
    u[21] = s_num[N_DEPTH];
    u[22] = s_num[N_DROP];
}

/* ---- the door ------------------------------------------------------------
 *
 *  An edit's build may run on a thread of its own while the frames go on.
 *  What it does here, and what compose.c does here, takes the lock
 *  for that call alone.  A frame never waits for it.  Where the build
 *  holds it, the frame takes the numbers it took last and leaves the rest
 *  to the next frame. */
static SDL_Mutex *s_mx;

static void lock(void)
{
    if (s_mx)
        SDL_LockMutex(s_mx);
}

static void unlock(void)
{
    if (s_mx)
        SDL_UnlockMutex(s_mx);
}

static int try_lock(void)
{
    return !s_mx || SDL_TryLockMutex(s_mx);
}

void show_enable(int on)
{
    if (!s_mx)
        s_mx = SDL_CreateMutex();
    lock();
    s_on = on;
    if (!on)
        reset_now();
    unlock();
}

void show_reset(void)
{
    lock();
    reset_now();
    unlock();
}

int show_offer(int phase, const RMesh *m, const int32_t edit[4])
{
    int n;
    lock();
    n = phase == SHOW_GOING ? offer_going(m, edit) : phase == SHOW_LEAVING ? offer_leaving()
                                                                           : offer_coming(m);
    unlock();
    return n;
}

int show_count(void)
{
    int n;
    lock();
    n = count_now();
    unlock();
    return n;
}

int show_candidate(int i, float *dist, int *same, int *back, int *kept)
{
    int rc;
    lock();
    rc = candidate_now(i, dist, same, back, kept);
    unlock();
    return rc;
}

void show_mark(int i)
{
    lock();
    mark_now(i);
    unlock();
}

void show_chunk(int slot, const RMeshList *old, const RMeshList *now, uint32_t base)
{
    lock();
    chunk_now(slot, old, now, base);
    unlock();
}

void show_settle(void)
{
    lock();
    settle_now();
    unlock();
}

uint32_t show_tri_key(uint32_t t, uint32_t id)
{
    uint32_t key = SHOW_KEY_NONE;
    lock();
    if (s_keyed && t < s_n_moves && s_moves[t] == 1)
        key = id < SHOW_PIECES && id < SHOW_KEY_COME ? id : SHOW_KEY_COME;
    unlock();
    return key;
}

int show_rekey(const uint32_t **keys, uint32_t first[SHOW_SLOTS], uint32_t n[SHOW_SLOTS])
{
    int k, any = 0;
    lock();
    if (s_rekey)
        for (k = 0; k < SHOW_SLOTS; ++k)
        {
            first[k] = s_old_has[k] ? s_old_first[k] : 0u;
            n[k]     = s_old_has[k] ? s_old_n[k] : 0u;
            any |= s_old_has[k];
        }
    s_rekey = 0;
    *keys   = s_old_id;
    unlock();
    return any;
}

int show_holds(void)
{
    int on;
    lock();
    on = holds_now();
    unlock();
    return on;
}

void show_arrive(void)
{
    lock();
    arrive_now();
    unlock();
}

int show_active(void)
{
    int on;
    if (!try_lock())
        return 1; /* a phase is running, so something is */
    on = active_now();
    unlock();
    return on;
}

int show_tick(void)
{
    int ended = 0;
    if (!try_lock())
        return 0;
    if (s_end_at > 0.0f && since() > s_end_at)
        reset_now(), ended = 1;
    unlock();
    return ended;
}

int show_states(uint8_t *out)
{
    int fresh;
    if (!try_lock())
        return 0;
    fresh = s_states_new;
    if (fresh)
        memcpy(out, s_state, sizeof s_state);
    s_states_new = 0;
    unlock();
    return fresh;
}

void show_uniform(float u[24])
{
    static float last[24];
    if (!try_lock())
    {
        memcpy(u, last, sizeof last);
        return;
    }
    uniform_now(u);
    memcpy(last, u, sizeof last);
    unlock();
}
