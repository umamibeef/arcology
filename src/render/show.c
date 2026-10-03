/*  show.c: what an edit's build shows going and coming.  See show.h. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "incr.h"
#include "log.h"
#include "mesh/shape.h"
#include "opt.h"
#include "prof.h"
#include "script.h"
#include "show.h"

#include <SDL3/SDL.h>

/*  The script's numbers, arc.numbers("edit_show").
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

static int       s_on;
static uint8_t   s_state[SHOW_PIECES];   /* what the device draws each piece as */
static uint8_t   s_come[SHOW_PIECES];    /* the pieces that come once the new world is up */
static uint8_t   s_live[SHOW_PIECES];    /* the pieces a mesh holds triangles of */
static uint8_t   s_differs[SHOW_PIECES]; /* a new piece with a triangle the last build did not have */
static uint8_t   s_lost[SHOW_PIECES];    /* an old piece with a triangle this build does not have */
static uint8_t   s_back[SHOW_PIECES];    /* a new piece with a triangle of a piece that went */
static float     s_far[SHOW_PIECES];     /* how far the farthest point of a piece lies from the edit */
static Pair     *s_pair;
static uint32_t  s_n_pair, s_cap_pair;
static int       s_states_new; /* the states changed since the GPU took them */
static int       s_armed;      /* an edit's build is under way, its going phase done */
static int       s_held;       /* the coming is decided and waits for the new world */
static int       s_moving;     /* some piece is not still */
static ShapeId   s_before;     /* the ids the last build left: below this are old */
static float     s_area[4];    /* the edited tiles: first column, first row, last + 1 of each */
static float     s_num[N_N];
static double    s_t0;       /* when the going phase began, in ms */
static float     s_leave_at; /* seconds from s_t0: the build is done and what it changed leaves */
static float     s_gone_at;  /* the last piece has gone */
static float     s_come_at;  /* the new world is up and its pieces come */
static float     s_come_far; /* how far out the farthest point of a coming piece lies */
static float     s_end_at;   /* the last piece has come */
static int       s_phase;
static Cand     *s_cand;
static int       s_n_cand, s_cap_cand;
static uint32_t *s_slot;  /* the old triangles of one chunk, by hash: 1 + the index, 0 empty */
static uint8_t  *s_taken; /* which of them a new triangle matched */
static uint32_t  s_cap_slot, s_cap_taken;
static double    s_meet_ms; /* what telling the changed pieces apart cost this build */

void show_key_tri(RMeshVert v[3], uint32_t id)
{
    const float    mx = (v[0].pos[0] + v[1].pos[0] + v[2].pos[0]) / 3.0f;
    const float    my = (v[0].pos[1] + v[1].pos[1] + v[2].pos[1]) / 3.0f;
    const int      nx = (int)floorf(mx * 16.0f + 0.5f), ny = (int)floorf(my * 16.0f + 0.5f);
    const uint32_t piece = id < SHOW_KEY_NONE ? id : SHOW_KEY_NONE;
    int            k;
    for (k = 0; k < 3; ++k)
    {
        int      ox = nx - (int)floorf(v[k].pos[0] * 16.0f + 0.5f);
        int      oy = ny - (int)floorf(v[k].pos[1] * 16.0f + 0.5f);
        uint32_t key;
        ox  = ox < -64 ? -64 : ox > 63 ? 63
                                       : ox;
        oy  = oy < -64 ? -64 : oy > 63 ? 63
                                       : oy;
        key = piece | (uint32_t)(ox + 64) << 18 | (uint32_t)(oy + 64) << 25;
        memcpy(&v[k].pos[3], &key, sizeof key);
    }
}

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

/*  How far the farthest point of each piece lies from the edited tiles,
 *  from the triangles a list holds in its network ranges.  A long piece
 *  is still moving at its far end when its near end has gone.  A box
 *  round a bent piece reaches further than any of its points. */
static void far_of(const uint32_t *ids, uint32_t ntri, const RMesh *m, const RMeshVert *verts)
{
    int k;
    for (k = 1; ids && verts && k < 2 * MESH_CHUNKS; k += 2)
    {
        uint32_t t = m->range_start[k] / 3u, e = t + m->range_count[k] / 3u;
        for (; t < e && t < ntri; ++t)
        {
            uint32_t id = ids[t];
            int      v;
            if (id >= SHOW_PIECES)
                continue;
            for (v = 0; v < 3; ++v)
                s_far[id] = fmaxf(s_far[id], point_dist(verts[3u * t + (uint32_t)v].pos[0], verts[3u * t + (uint32_t)v].pos[1]));
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
 *  material.  With `verts`, only the pieces with a triangle that starts
 *  on an edited tile. */
static void live_of(const uint32_t *ids, uint32_t ntri, const RMesh *m, const RMeshVert *verts)
{
    int k;
    memset(s_live, 0, sizeof s_live);
    for (k = 1; ids && k < 2 * MESH_CHUNKS; k += 2)
    {
        uint32_t t = m->range_start[k] / 3u, e = t + m->range_count[k] / 3u;
        for (; t < e && t < ntri; ++t)
            if (ids[t] < SHOW_PIECES &&
                (!verts || incr_dirty_tile((int32_t)floorf(verts[3u * t].pos[0]), (int32_t)floorf(verts[3u * t].pos[1]))))
                s_live[ids[t]] = 1;
    }
}

/*  The going phase's candidates: what the edit is drawn on.  Answers
 *  how many, or -1 where there is no edit to show. */
static int offer_going(const RMesh *m)
{
    int32_t box[4];
    ShapeId id, n = (ShapeId)shape_count();
    reset_now();
    if (!s_on || !m || !incr_editing())
        return -1;
    memset(s_num, 0, sizeof s_num);
    if (!script_numbers("edit_show", NUMS, s_num, N_N) || s_num[N_GO_SPEED] <= 0.0f || s_num[N_GO_TIME] <= 0.0f ||
        s_num[N_COME_SPEED] <= 0.0f || s_num[N_COME_TIME] <= 0.0f)
        return -1;
    if (incr_dirty_box(box) != 0)
        return -1;
    s_area[0] = (float)box[0], s_area[1] = (float)box[1];
    s_area[2] = (float)box[2] + 1.0f, s_area[3] = (float)box[3] + 1.0f;
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
    far_of(m->tri_comp_old, m->n_tri_comp_old, m, m->bucket);
    /*  What the edit is drawn on goes at once: the build cannot leave it
     *  as it was.  The old list stands aside in `bucket`, still ranged. */
    live_of(m->tri_comp_old, m->n_tri_comp_old, m, m->bucket);
    s_n_cand = 0;
    for (id = 0; id < n && id < SHOW_PIECES; ++id)
        if (s_live[id] && cand_add(id, piece_dist(id), 0, 0, 0) != 0)
            break;
    s_phase = SHOW_GOING;
    s_armed = 1;
    R_DBG("show", "going: %d pieces on the edited tiles", s_n_cand);
    return s_n_cand;
}

static uint32_t tri_hash(const RMeshVert *v)
{
    uint32_t h = 2166136261u, w;
    int      k, a;
    for (k = 0; k < 3; ++k)
        for (a = 0; a < 3; ++a)
        {
            memcpy(&w, &v[k].pos[a], sizeof w);
            h = (h ^ w) * 16777619u;
        }
    return h;
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

static void meet_now(const RMeshVert *old, const uint32_t *old_ids, uint32_t n_old, const RMeshVert *now, const uint32_t *now_ids, uint32_t n_now)
{
    uint32_t size = 1, mask, i;
    double   t0   = prof_now();
    if (!s_armed || !old_ids || !now_ids)
        return;
    while (size < 2u * n_old + 2u)
        size *= 2u;
    if (size > s_cap_slot)
    {
        uint32_t *ns = (uint32_t *)realloc(s_slot, (size_t)size * sizeof *ns);
        if (!ns)
            return;
        s_slot     = ns;
        s_cap_slot = size;
    }
    if (n_old > s_cap_taken)
    {
        uint8_t *nt = (uint8_t *)realloc(s_taken, n_old);
        if (!nt)
            return;
        s_taken     = nt;
        s_cap_taken = n_old;
    }
    mask = size - 1u;
    memset(s_slot, 0, (size_t)size * sizeof *s_slot);
    if (n_old)
        memset(s_taken, 0, n_old);
    for (i = 0; i < n_old; ++i)
    {
        uint32_t h = tri_hash(&old[3u * i]) & mask;
        while (s_slot[h])
            h = (h + 1u) & mask;
        s_slot[h] = i + 1u;
    }
    /*  Each new triangle takes an old one the same as it, one each.  So
     *  an old triangle left over is one the build did not make again. */
    for (i = 0; i < n_now; ++i)
    {
        uint32_t id = now_ids[i], h, twin = 0;
        for (h = tri_hash(&now[3u * i]) & mask; s_slot[h]; h = (h + 1u) & mask)
            if (!s_taken[s_slot[h] - 1u] && memcmp(&old[3u * (s_slot[h] - 1u)], &now[3u * i], 3u * sizeof *now) == 0)
            {
                twin               = s_slot[h];
                s_taken[twin - 1u] = 1;
                break;
            }
        if (id >= SHOW_PIECES)
            continue;
        if (!twin)
            s_differs[id] = 1;
        else if (old_ids[twin - 1u] < SHOW_PIECES)
            pair_add(id, old_ids[twin - 1u]);
    }
    for (i = 0; i < n_old; ++i)
        if (!s_taken[i] && old_ids[i] < SHOW_PIECES)
            s_lost[old_ids[i]] = 1;
    s_meet_ms += prof_now() - t0;
}

/*  The leaving phase's candidates, once the build is done: the old
 *  pieces it lost a triangle of, other than those already going.  Answers
 *  how many, or -1 where the build was no edit being shown. */
static int offer_leaving(void)
{
    ShapeId id;
    if (!s_armed || script_fault())
        return -1;
    s_leave_at = since();
    s_n_cand   = 0;
    for (id = 0; id < s_before && id < SHOW_PIECES; ++id)
        if (s_lost[id] && s_state[id] != SHOW_GOING && cand_add(id, piece_dist(id), 0, 0, 0) != 0)
            break;
    s_phase = SHOW_LEAVING;
    R_DBG("show", "leaving: %d pieces the build changed; told apart in %.1f ms; the build took %.2f s", s_n_cand, s_meet_ms, (double)s_leave_at);
    return s_n_cand;
}

/*  The coming phase's candidates: the pieces the build made, and those
 *  that went and still stand.  The new world they come in waits for the
 *  last piece to go. */
static int offer_coming(const RMesh *m)
{
    ShapeId  id, n = (ShapeId)shape_count();
    uint32_t p;
    if (!s_armed || script_fault())
        return -1;
    s_armed = 0;
    /*  A new piece comes back where it shares a triangle with one that
     *  went. */
    for (p = 0; p < s_n_pair; ++p)
        if (s_state[s_pair[p].old] == SHOW_GOING || s_state[s_pair[p].old] == SHOW_LEAVING)
            s_back[s_pair[p].now] = 1;
    live_of(m->tri_comp, m->n_land / 3u, m, NULL);
    far_of(m->tri_comp, m->n_land / 3u, m, m->land);
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
    for (id = 0; id < SHOW_PIECES; ++id)
    {
        s_state[id] = s_come[id] ? SHOW_COMING : SHOW_STILL;
        s_moving |= s_come[id];
    }
    memset(s_come, 0, sizeof s_come);
    /*  The new world goes up on the first frame after the last piece has
     *  gone, or at once where nothing was going. */
    s_come_at    = fmaxf(since(), s_gone_at);
    s_end_at     = s_moving ? s_come_at + s_come_far / s_num[N_COME_SPEED] + s_num[N_JITTER] + s_num[N_COME_TIME] + 0.1f : 0.0f;
    s_states_new = 1;
}

static int phase_now(void)
{
    return s_phase;
}

static int count_now(void)
{
    return s_n_cand;
}

static int area_now(float area[4])
{
    memcpy(area, s_area, sizeof s_area);
    return 0;
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
    u[1]  = 0.0f;
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
 *  What it does here, and what the rule it asks does here, takes the lock
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

int show_offer(int phase, const RMesh *m)
{
    int n;
    lock();
    n = phase == SHOW_GOING ? offer_going(m) : phase == SHOW_LEAVING ? offer_leaving()
                                                                     : offer_coming(m);
    unlock();
    return n;
}

int show_phase(void)
{
    int p;
    lock();
    p = phase_now();
    unlock();
    return p;
}

int show_count(void)
{
    int n;
    lock();
    n = count_now();
    unlock();
    return n;
}

int show_area(float area[4])
{
    int rc;
    lock();
    rc = area_now(area);
    unlock();
    return rc;
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

void show_meet(const RMeshVert *old, const uint32_t *old_ids, uint32_t n_old, const RMeshVert *now, const uint32_t *now_ids, uint32_t n_now)
{
    lock();
    meet_now(old, old_ids, n_old, now, now_ids, n_now);
    unlock();
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

void show_tick(void)
{
    if (!try_lock())
        return;
    if (s_end_at > 0.0f && since() > s_end_at)
        reset_now();
    unlock();
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
