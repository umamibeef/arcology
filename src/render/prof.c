/*  prof.c: see prof.h. */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "dump.h"
#include "opt.h"
#include "prof.h"

static int   s_work[WORK_N], s_work_out[WORK_N];
static int   s_work_on;
static float s_work_box[4];

void prof_work(int unit, float x0, float y0, float x1, float y1)
{
    ++s_work[unit];
    if (s_work_on && (x1 < s_work_box[0] || y1 < s_work_box[1] || x0 > s_work_box[2] || y0 > s_work_box[3]))
    {
        static const char *const KIND[WORK_N] = {"sweep", "ground"};
        ++s_work_out[unit];
        if (g_dev.work_window)
            dumpf("work out %s %.2f,%.2f %.2f,%.2f\n", KIND[unit], (double)x0, (double)y0, (double)x1, (double)y1);
    }
}

void prof_work_window(const int32_t *box, int window)
{
    memset(s_work, 0, sizeof s_work);
    memset(s_work_out, 0, sizeof s_work_out);
    s_work_on = box != 0;
    if (!box)
        return;
    s_work_box[0] = (float)(box[0] - window);
    s_work_box[1] = (float)(box[1] - window);
    s_work_box[2] = (float)(box[2] + 1 + window);
    s_work_box[3] = (float)(box[3] + 1 + window);
}

double prof_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

void prof_work_print(void)
{
    if (!g_dev.times)
        return;
    dumpf("work    sweeps %d, ground tiles %d", s_work[WORK_SWEEP], s_work[WORK_GROUND]);
    if (s_work_on)
        dumpf(" | outside the edit's window: sweeps %d, ground tiles %d", s_work_out[WORK_SWEEP], s_work_out[WORK_GROUND]);
    dumpf("\n");
}

/*  ---- how far the build has got ------------------------------------- */

static ProfWatcher s_watch;
static void       *s_watch_ud;

void prof_watch(ProfWatcher fn, void *ud)
{
    s_watch    = fn;
    s_watch_ud = ud;
}

/*  The step under way, kept so a tick inside it can name it again. */
static char s_step_name[64];
static int  s_step_i, s_step_n;

void prof_step(const char *step, int i, int n)
{
    if (!step)
        return;
    snprintf(s_step_name, sizeof s_step_name, "%s", step);
    s_step_i = i, s_step_n = n;
    if (s_watch)
        s_watch(s_watch_ud, step, i, n);
}
