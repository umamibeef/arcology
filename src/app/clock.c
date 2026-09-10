/*  clock.c: the city's clock, as the original paces it.  TickCount is 60
 *  Hz and the speed's delay per phase is a word table indexed by
 *  MISC[1019].  Speeds 0 and 1 never tick, 2 to 4 wait for their
 *  deadline, and 5 runs a phase every time round the loop.  The
 *  simulation itself knows none of this.  Sim_tick is one switch on the
 *  date.  So the pacing lives here. */
#include "internal.h"
#include "project.h"
#include "adapt.h"
#include "sim.h"
#include "arco.h"
#include "log.h"
#include "opt.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int32_t SPEED_DELAY[6] = {0, 0, 36, 12, 0, 0};
#define THINGS_TICKS      15      /* $376: the mover steps every 15 ticks     */
#define ANIM_A_TICKS      12      /* $9756: the 49-entry run turns every 12   */
#define ANIM_B_TICKS      90      /* $97E0: the 15-entry run every 90         */
#define SWALLOW_BUDGET_NS 4000000 /* speed 5: phases per frame, by time */

int64_t ticks_now(const App *a)
{
    uint64_t ns = SDL_GetTicksNS() - a->t0_ns;
    return (int64_t)(ns * 60u / 1000000000u);
}


/*  A phase of the clock, then whatever the renderer needs to know. */
void run_phase(App *a)
{
    int ev = sim_tick(a->city);
    if (ev == SIM_EV_STAGE)
    {
        R_NOTE("event", "the city is promoted to stage %d", (int)a->city->misc[MISC_STAGE]);
        ui_log(&a->us, "The city is promoted to stage %d", (int)a->city->misc[MISC_STAGE]);
        sound_play(a->snd, R_SND_CHEERS);
    }
    else if (ev == SIM_EV_SCEN_WON)
    {
        R_NOTE("event", "scenario won");
        ui_log(&a->us, "Scenario won");
        sound_play(a->snd, R_SND_CHEERS);
    }
    else if (ev == SIM_EV_SCEN_LOST)
    {
        R_NOTE("event", "scenario lost");
        ui_log(&a->us, "Scenario lost");
        sound_play(a->snd, R_SND_BOOS);
    }
    else if (ev == SIM_EV_BANKRUPT)
    {
        R_NOTE("event", "the city is bankrupt");
        ui_log(&a->us, "The city is bankrupt");
        sound_play(a->snd, R_SND_BOOS);
    }
    a->dirty = 1;
    /*  Terrain only moves in a disaster or under the player's tools.
     *  The sim's own phases never write ALTM or XTER.  The mesh stays. */
}

/*  ==================================================================
 *  The city and its clock
 *
 *  Loading a save, naming it, and running the phases at the chosen
 *  speed.
 *  ================================================================== */
/*  What a load brought in, for the log: the file and its shape, then the
 *  city the file describes.  Both load paths call this, so a city picked
 *  from the menu reports exactly what one named on the command line
 *  does.  Ms is the time the load took. */
void log_city_loaded(const City *c, const char *path, double ms)
{
    const char *base  = strrchr(path, '/');
    const char *fmt   = arco_is_arco(path) ? ".arco" : ".sc2";
    long        bytes = -1;
    char        name[40];
    char        chunks[24 * 6 + 1];
    int         i, n = 0;
    FILE       *f = fopen(path, "rb");
    base          = base ? base + 1 : path;
    if (f)
    {
        if (fseek(f, 0, SEEK_END) == 0)
            bytes = ftell(f);
        fclose(f);
    }
    /*  CNAM is a Pascal string: the length, then the characters. */
    name[0] = 0;
    if (c->cnam && c->cnam_len > 1)
    {
        size_t k = c->cnam[0];
        if (k > c->cnam_len - 1)
            k = c->cnam_len - 1;
        if (k > sizeof name - 1)
            k = sizeof name - 1;
        memcpy(name, c->cnam + 1, k);
        name[k] = 0;
    }
    R_NOTE("city", "%s: %s, %ld bytes, %d chunks, %.0f ms", base, fmt, bytes, c->n_chunks, ms);
    R_NOTE("city", "\"%s\": founded %d, year %d day %d, $%d, population %d, rotation %d, speed %d", name[0] ? name : "(unnamed)", (int)c->year_founded, (int)c->year_founded + (int)c->years, (int)c->date, (int)c->funds, (int)c->population, (int)(c->misc[2] & 3), (int)c->misc[MISC_SPEED]);
    chunks[0] = 0;
    for (i = 0; i < c->n_chunks && i < 24; i++)
        n += snprintf(chunks + n, sizeof chunks - (size_t)n, "%s%s", i ? " " : "", c->order[i]);
    R_DBG("city", "chunks: %s", chunks);
}

void set_city_name(App *a, const char *path)
{
    const char *base = strrchr(path, '/');
    base             = base ? base + 1 : path;
    snprintf(a->city_base, sizeof a->city_base, "%s", base);
    snprintf(a->us.save_path, sizeof a->us.save_path, "%s-saved.sc2", base);
}

void set_speed(App *a, int32_t s)
{
    if (s < 1)
        s = 1;
    if (s > 5)
        s = 5;
    if (s > 1)
        a->last_speed = s;
    a->speed                  = s;
    a->city->misc[MISC_SPEED] = s;
    a->deadline               = ticks_now(a);
}

/*  The original's main loop, once ($C..$D6 and $3A4..$430, $9728). */
void step_clock(App *a)
{
    int64_t now = ticks_now(a);
    int32_t sp  = a->speed;

    if (sp == 5)
    {
        uint64_t start = SDL_GetTicksNS();
        do
            run_phase(a);
        while (SDL_GetTicksNS() - start < SWALLOW_BUDGET_NS);
        a->deadline = now + SPEED_DELAY[5];
    }
    else if (sp > 1 && now > a->deadline)
    {
        a->deadline = ticks_now(a) + SPEED_DELAY[sp];
        run_phase(a);
    }
    if (sp > 1 && now > a->things_deadline)
    {
        a->things_deadline = now + THINGS_TICKS;
        sim_step_things(a->city); /* $9E0A */
        a->dirty = 1;
    }
    /*  idlePump $9728: speed 5 skips the animation, paused skips it too. */
    if (sp > 1 && sp < 5)
    {
        int turned = 0;
        if (now >= a->anim_a_deadline)
        {
            a->anim_a_deadline = now + ANIM_A_TICKS;
            a->anim_a++;
            turned = 1;
        }
        if (now >= a->anim_b_deadline)
        {
            a->anim_b_deadline = now + ANIM_B_TICKS;
            a->anim_b++;
            turned = 1;
        }
        if (turned)
        {
            atlas_animate_runs(&a->atlas, a->anim_a, a->anim_b);
            gpu_set_palette(a->gpu, &a->atlas);
        }
    }
}
