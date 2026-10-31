/*  land_settle.c: the land settling as the world is built.
 *
 *  Until a chunk is on the device, a sheet with waves running over it
 *  stands where its land will be (terrain_sheet.vert).  A sweep starts
 *  once the first of the world is on the device.  It runs out from the
 *  point under the view's center at settle_sweep tiles a second.  Each
 *  point of a built chunk eases from the waves to its own height once the
 *  sweep reaches it (terrain_settle.vert).  Each piece of art falls onto
 *  the ground once the sweep reaches its tile (frame.c).  Only the first
 *  build of a chunk settles.  An edit's build does not.  When every chunk
 *  has settled, the plain shaders draw again.  A run without a window
 *  settles only with --async-build, so a shot or a check is otherwise the
 *  still world. */
#include "adapt.h"
#include "geo.h"
#include "internal.h"
#include "log.h"
#include "shore.h"
#include "terrain.h"
#include "turn.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

/*  When each chunk came onto the device, in seconds from the settle's
 *  start.  Below nought: not yet. */
static float    s_at[MESH_CHUNKS_CAP];
static float    s_state[2 * MESH_CHUNKS_CAP];
static uint64_t s_t0;
static int      s_want, s_on;
static float    s_sheet;    /* the sheet's height: the mean of the land's corners, in levels */
static float    s_oc, s_or; /* the point the sweep runs out from */
static float    s_start;    /* the second the sweep starts: when the first of the world came onto the device */

void land_settle_want(App *a)
{
    s_want = a->resident;
}

/*  Each tile's corners at (0,0), (1,0), (1,1) and (0,1) from it, in
 *  levels.  Water lies at its surface.  Land is its ground level, with
 *  the corners its slope lifts one level higher. */
static int land_begin(App *a)
{
    const RCity *c = a->view;
    int32_t      i, n = r_map;
    double       sum = 0.0;
    float       *h   = (float *)malloc((size_t)n * (size_t)n * 4u * sizeof *h);
    if (!h)
        return -1;
    for (i = 0; i < n * n; ++i)
    {
        unsigned alt = c->map.altm[i];
        uint8_t  x   = c->map.xter[i];
        float   *o   = &h[4 * i];
        if (is_water(x))
            o[0] = o[1] = o[2] = o[3] = (float)rcity_alt_surface(alt, x);
        else
        {
            int   lift = corner_lifts(slope_code(x));
            float g    = (float)rcity_alt_ground(alt);
            o[0]       = g + (float)(lift & 1);
            o[1]       = g + (float)((lift >> 3) & 1);
            o[2]       = g + (float)((lift >> 2) & 1);
            o[3]       = g + (float)((lift >> 1) & 1);
        }
        sum += o[0] + o[1] + o[2] + o[3];
    }
    s_sheet = (float)(sum / (4.0 * (double)n * (double)n));
    i       = gpu_set_land(a->gpu, h, n);
    free(h);
    if (i != 0)
        return -1;
    /*  The ground's colors read how far each tile is from the water.  The
     *  sheet is drawn before any build sends that to the device. */
    {
        uint8_t *f = (uint8_t *)malloc((size_t)n * (size_t)n * 4u);
        if (!f)
            return -1;
        shore_field(c, f);
        i = gpu_set_shore(a->gpu, f, n);
        free(f);
    }
    return i;
}

/*  Seconds since the settle began. */
static float settle_now(void)
{
    return (float)((double)(SDL_GetTicksNS() - s_t0) / 1e9);
}

void land_settle_landed(App *a)
{
    int k, per = r_map / MESH_CHUNK;
    if (!s_on)
        return;
    for (k = 0; k < per * per; ++k)
        if (a->mesh.chunk_laid[k] && s_at[k] < 0.0f)
            s_at[k] = settle_now();
}

void land_settle_tick(App *a, SDL_Window *win)
{
    static int c_secs = -1, c_h = -1, c_len = -1, c_v = -1, c_drop = -1, c_sweep = -1, c_fall = -1, c_dim = -1, c_dim_in = -1;
    const int  per = r_map / MESH_CHUNK;
    float      now, secs = geo_num(&c_secs, "settle_seconds"), last = 0.0f;
    int        k, done = 1;
    if (s_want && a->gpu && a->view)
    {
        s_want = 0;
        s_on   = land_begin(a) == 0;
        s_t0   = SDL_GetTicksNS();
        for (k = 0; k < MESH_CHUNKS_CAP; ++k)
            s_at[k] = -1.0f;
        s_oc = s_or = (float)r_map * 0.5f;
        {
            int   pw, ph;
            float alt;
            if (win && SDL_GetWindowSizeInPixels(win, &pw, &ph))
                screen_to_grid(a, (float)pw * 0.5f, (float)ph * 0.5f, win, &s_oc, &s_or, &alt);
        }
        if (!s_on)
            R_WARN("app", "the land the sheet settles onto could not go to the device");
    }
    if (!s_on)
    {
        memset(a->settle_u, 0, sizeof a->settle_u);
        return;
    }
    now = settle_now();
    /*  Each chunk as the device reads it: the second it came onto the
     *  device, and whether it is built.  The settle is done when every
     *  chunk is built and its farthest corner has settled.  Every piece
     *  of art on it is down by then too.  A piece falls when the sweep
     *  reaches its tile, a little later by its seed (frame.c). */
    {
        float sweep = geo_num(&c_sweep, "settle_sweep"), fall = geo_num(&c_fall, "settle_fall");
        float tail = 0.15f + fall, start = 1e9f;
        for (k = 0; k < per * per; ++k)
            if (s_at[k] >= 0.0f && s_at[k] < start)
                start = s_at[k];
        s_start = start;
        for (k = 0; k < per * per; ++k)
        {
            float dc = fmaxf(fabsf((float)(k % per) * (float)MESH_CHUNK - s_oc), fabsf((float)(k % per + 1) * (float)MESH_CHUNK - s_oc));
            float dr = fmaxf(fabsf((float)(k / per) * (float)MESH_CHUNK - s_or), fabsf((float)(k / per + 1) * (float)MESH_CHUNK - s_or));
            float at = start + (sweep > 0.0f ? sqrtf(dc * dc + dr * dr) / sweep : 0.0f);
            if (s_at[k] < 0.0f)
                done = 0;
            at                 = at > s_at[k] ? at : s_at[k];
            last               = at > last ? at : last;
            s_state[2 * k]     = s_at[k] >= 0.0f ? s_at[k] : 1e9f;
            s_state[2 * k + 1] = s_at[k] >= 0.0f ? 1.0f : 0.0f;
        }
        last += tail > secs ? tail : secs;
    }
    if (done && now >= last)
    {
        s_on = 0;
        memset(a->settle_u, 0, sizeof a->settle_u);
        return;
    }
    if (gpu_set_settle(a->gpu, s_state, per) != 0)
    {
        s_on = 0;
        memset(a->settle_u, 0, sizeof a->settle_u);
        return;
    }
    a->settle_u[0] = s_sheet;
    a->settle_u[1] = geo_num(&c_h, "settle_wave_height");
    a->settle_u[2] = geo_num(&c_len, "settle_wave_length");
    a->settle_u[3] = geo_num(&c_v, "settle_wave_speed");
    a->settle_u[4] = now;
    a->settle_u[5] = (float)MESH_CHUNK;
    a->settle_u[6] = (float)per;
    a->settle_u[7] = (float)r_map;
    a->settle_u[8] = geo_num(&c_drop, "settle_drop");
    /*  The sweep's cells onto the map's, which the art is placed by: a
     *  quarter turn is affine, so three cells say all of it. */
    {
        int32_t q = a->sweep_quarter & 3, ox, oy, xx, xy, yx, yy;
        adapt_turned_src(0, 0, q, r_map, &ox, &oy);
        adapt_turned_src(1, 0, q, r_map, &xx, &xy);
        adapt_turned_src(0, 1, q, r_map, &yx, &yy);
        a->settle_u[9]  = (float)ox;
        a->settle_u[10] = (float)oy;
        a->settle_u[11] = (float)(xx - ox);
        a->settle_u[12] = (float)(xy - oy);
        a->settle_u[13] = (float)(yx - ox);
        a->settle_u[14] = (float)(yy - oy);
    }
    a->settle_u[15] = s_oc;
    a->settle_u[16] = s_or;
    a->settle_u[17] = geo_num(&c_sweep, "settle_sweep");
    a->settle_u[18] = geo_num(&c_fall, "settle_fall");
    a->settle_u[19] = secs;
    a->settle_u[20] = s_start;
    a->settle_u[21] = geo_num(&c_dim, "settle_dim");
    a->settle_u[22] = geo_num(&c_dim_in, "settle_dim_in");
}
