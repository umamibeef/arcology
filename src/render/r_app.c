/*  sc2kgpu -- the game: the reconstruction simulated and drawn on the GPU.
 *
 *      sc2kgpu <assets dir> <city file> [--zoom 8|16|32] [--scale N]
 *              [--check out.png]
 *
 *  The simulation is the verified reconstruction, run as the original's
 *  main loop runs it: a phase of the 25-phase clock when the speed's
 *  deadline passes, the moving things every fifteen ticks, the palette
 *  runs every twelve and ninety.  The renderer is the software sweep's
 *  op list drawn through SDL_GPU, and --check draws one frame both ways
 *  and says whether they agree.
 *
 *  Keys
 *      1..5        speed: paused, turtle, llama, cheetah, african swallow
 *      space       pause / resume
 *      arrows      pan          + / -   zoom level        [ ]  pixel scale
 *      t           terrain: sprites / mesh         y   water: sprites / shader
 *      v / shift-v data views                      u   underground
 *      g           the sprites' grid outline on the mesh
 *      m           debug: draw the sweep with no depth plane
 *      p           screenshot and check against the software rasteriser
 *      escape      quit
 */
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include <SDL3/SDL.h>

#include "arc_version.h"
#include "arco.h"
#define JSMN_STATIC
#include "jsmn.h"
#include "r_adapt.h"
#include "r_atlas.h"
#include "r_city.h"
#include "r_gpu.h"
#include "r_log.h"
#include "r_mesh.h"
#include "r_soft.h"
#include "r_sound.h"
#include "r_ui.h"
#ifndef _WIN32
    #include <sys/utsname.h>
#endif
#include "sc2k.h"

/*  The key every letter shortcut needs: the Mac's command key, and
 *  Ctrl everywhere else.  The bare keys that remain -- the digits for
 *  the speeds, space, the zoom and scale keys, the arrows -- are game
 *  controls, not menu shortcuts. */
#ifdef __APPLE__
    #define KMOD_CMD SDL_KMOD_GUI
#else
    #define KMOD_CMD SDL_KMOD_CTRL
#endif

/*  The platform, for the banner: what SDL calls it, the kernel and
 *  the machine where uname can say, and the SDL this is running on. */
static void platform_line(char *out, size_t n)
{
    const int v = SDL_GetVersion();
#ifdef _WIN32
    snprintf(out, n, "%s %s, SDL %d.%d.%d", SDL_GetPlatform(), sizeof(void *) == 8 ? "x64" : "x86", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
#else
    struct utsname u;
    if (uname(&u) == 0)
        snprintf(out, n, "%s, %s %s %s, SDL %d.%d.%d", SDL_GetPlatform(), u.sysname, u.release, u.machine, SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
    else
        snprintf(out, n, "%s, SDL %d.%d.%d", SDL_GetPlatform(), SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
#endif
}

/*  Nanoseconds from SDL_GetTicksNS as milliseconds, for the log. */
#define NS_MS(ns) ((double)(ns) / 1e6)

/*  The original's clock.  TickCount is 60 Hz; the speed's delay per phase
 *  is the word table at A5+0xC9A indexed by MISC[1019]: 0, 0, 36, 12, 0.
 *  Speeds 0 and 1 never tick, 2 to 4 wait for their deadline, and 5 runs a
 *  phase every time round the loop without a deadline at all ($2A..$56). */
static const int32_t SPEED_DELAY[6] = {0, 0, 36, 12, 0, 0};
#define THINGS_TICKS      15      /* $376: the mover steps every 15 ticks     */
#define ANIM_A_TICKS      12      /* $9756: the 49-entry run turns every 12   */
#define ANIM_B_TICKS      90      /* $97E0: the 15-entry run every 90         */
#define SWALLOW_BUDGET_NS 4000000 /* speed 5: phases per frame, by time */

static const char *MONTHS[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

typedef struct
{
    RAtlas    atlas;
    City     *city;
    RCity    *view;
    ROpList   ops;
    RSweep    sw;
    RSoftOpts opts;
    RGpu     *gpu;
    RGpuView  gv;
    RMesh     mesh;
    RTraffic  traffic;
    float     traffic_time;

    int32_t  speed, last_speed;
    uint64_t t0_ns;
    int64_t  deadline, things_deadline, anim_a_deadline, anim_b_deadline;
    int32_t  anim_a, anim_b;
    int      dirty;      /* the city changed: sweep again          */
    int      mesh_dirty; /* the terrain changed: rebuild the mesh  */
    int      quit;
    uint8_t  sky[3];

    RUi     *ui;
    RUiState us;
    RSound  *snd;
    float    fps, frame_ms;
    uint64_t last_ns;
    int      drag;             /* the left button drags the map     */
    float    drag_ax, drag_ay; /* sub-pixel remainder of the drag    */
    float    drag_len;         /* how far the button travelled       */
    int32_t  q_col, q_row;     /* the tile under the pointer, or -1 */
    char     city_base[128];   /* the city file's name, for a city without one */
    float    zoom_world;       /* continuous zoom: 1 = the 32 px set at 1:1 */
    float    angle;            /* free rotation, degrees, 0 = the snap view */
    float    win_density;      /* window pixels per point                   */
    char     themes_dir[1024]; /* <assets>/themes, for the menu's picks */
} App;

static int64_t ticks_now(const App *a)
{
    uint64_t ns = SDL_GetTicksNS() - a->t0_ns;
    return (int64_t)(ns * 60u / 1000000000u);
}

static void title(App *a, SDL_Window *win)
{
    char        buf[512];
    const City *c = a->city;
    uint32_t    inst, culled;
    const char *speeds[6] = {"paused", "paused", "turtle", "llama", "cheetah", "african swallow"};
    r_gpu_stats(a->gpu, &inst, &culled);
    snprintf(buf, sizeof buf, "%s  |  %s %d  |  $%d  |  pop %d  |  %s  |  zoom %d  |  "
                              "terrain %s  water %s%s%s  |  %u drawn",
             a->view->name[0] ? a->view->name : "SimCity 2000",
             MONTHS[(c->date / 25) % 12],
             (int)(c->year_founded + c->date / 300),
             (int)c->funds,
             (int)c->population,
             speeds[a->speed >= 0 && a->speed <= 5 ? a->speed : 1],
             (int)a->opts.zoom,
             a->gv.terrain3d ? "mesh" : "sprites",
             a->gv.water3d ? "shader" : "sprites",
             a->opts.underground ? "  underground" : "",
             a->opts.view ? "  data view" : "",
             (unsigned)inst);
    SDL_SetWindowTitle(win, buf);
}

/*  A phase of the clock, then whatever the renderer needs to know. */
static void run_phase(App *a)
{
    int ev = sim_tick(a->city);
    if (ev == SIM_EV_STAGE)
    {
        R_NOTE("event", "the city is promoted to stage %d", (int)a->city->misc[MISC_STAGE]);
        r_ui_log(&a->us, "The city is promoted to stage %d", (int)a->city->misc[MISC_STAGE]);
        r_sound_play(a->snd, R_SND_CHEERS);
    }
    else if (ev == SIM_EV_SCEN_WON)
    {
        R_NOTE("event", "scenario won");
        r_ui_log(&a->us, "Scenario won");
        r_sound_play(a->snd, R_SND_CHEERS);
    }
    else if (ev == SIM_EV_SCEN_LOST)
    {
        R_NOTE("event", "scenario lost");
        r_ui_log(&a->us, "Scenario lost");
        r_sound_play(a->snd, R_SND_BOOS);
    }
    else if (ev == SIM_EV_BANKRUPT)
    {
        R_NOTE("event", "the city is bankrupt");
        r_ui_log(&a->us, "The city is bankrupt");
        r_sound_play(a->snd, R_SND_BOOS);
    }
    a->dirty = 1;
    /*  Terrain only moves in a disaster or under the player's tools; the
     *  sim's own phases never write ALTM or XTER.  The mesh stays. */
}

/*  the window title and the default save name follow the city */
/* ---- preferences ------------------------------------------------------- */

/*  settings.json, in the per-user place SDL knows for the platform --
 *  Application Support on macOS, AppData on Windows, ~/.local/share on
 *  Linux: one flat object of strings and numbers, JSON because that is
 *  what everything else this program writes is.  Read whole and
 *  written whole; the theme is the first key, and a few more will not
 *  need anything cleverer.  A missing file is simply no preference.
 *  Nested values are not ours and are left alone; a quote or a
 *  backslash in a value is escaped on the way out and not unescaped on
 *  the way in, which for theme names and numbers never arises. */
#define PREFS_MAX 32

typedef struct
{
    char key[64];
    char val[192];
    int  is_str; /* written quoted; a number or true/false goes bare */
} Pref;

static int prefs_path(char *out, size_t n)
{
    char *p = SDL_GetPrefPath("", "arcology");
    if (!p)
        return 0;
    snprintf(out, n, "%ssettings.json", p);
    SDL_free(p);
    return 1;
}

static int prefs_read(Pref *p, int *n)
{
    char        path[1024];
    FILE       *f;
    long        len;
    char       *js;
    jsmn_parser jp;
    jsmntok_t   t[2 * PREFS_MAX + 2];
    int         nt, i;
    *n = 0;
    if (!prefs_path(path, sizeof path) || !(f = fopen(path, "rb")))
        return 0;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || len > 65536)
    {
        fclose(f);
        return 0;
    }
    rewind(f);
    js = (char *)malloc((size_t)len + 1);
    if (!js || fread(js, 1, (size_t)len, f) != (size_t)len)
    {
        fclose(f);
        free(js);
        return 0;
    }
    fclose(f);
    js[len] = 0;
    jsmn_init(&jp);
    nt = jsmn_parse(&jp, js, (size_t)len, t, sizeof t / sizeof *t);
    if (nt < 1 || t[0].type != JSMN_OBJECT)
    {
        free(js);
        return 0;
    }
    for (i = 1; i + 1 < nt && *n < PREFS_MAX; i += 2)
    {
        const jsmntok_t *k = &t[i], *v = &t[i + 1];
        Pref            *q = &p[*n];
        if (k->type != JSMN_STRING || (v->type != JSMN_STRING && v->type != JSMN_PRIMITIVE))
            break; /* nested: not a settings file this build wrote */
        snprintf(q->key, sizeof q->key, "%.*s", k->end - k->start, js + k->start);
        snprintf(q->val, sizeof q->val, "%.*s", v->end - v->start, js + v->start);
        q->is_str = v->type == JSMN_STRING;
        (*n)++;
    }
    free(js);
    return 1;
}

static int prefs_get(const char *key, char *out, size_t n)
{
    Pref p[PREFS_MAX];
    int  np, i;
    out[0] = 0;
    if (!prefs_read(p, &np))
        return 0;
    for (i = 0; i < np; i++)
        if (strcmp(p[i].key, key) == 0)
        {
            snprintf(out, n, "%s", p[i].val);
            return 1;
        }
    return 0;
}

static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++)
    {
        if (*s == '"' || *s == '\\')
            fputc('\\', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

static int prefs_set(const char *key, const char *value)
{
    Pref  p[PREFS_MAX];
    char  path[1024];
    FILE *f;
    int   np = 0, i, found = 0;
    if (!prefs_path(path, sizeof path))
        return 0;
    prefs_read(p, &np); /* nothing there yet is fine */
    for (i = 0; i < np; i++)
        if (strcmp(p[i].key, key) == 0)
        {
            snprintf(p[i].val, sizeof p[i].val, "%s", value);
            p[i].is_str = 1;
            found       = 1;
        }
    if (!found && np < PREFS_MAX)
    {
        snprintf(p[np].key, sizeof p[np].key, "%s", key);
        snprintf(p[np].val, sizeof p[np].val, "%s", value);
        p[np].is_str = 1;
        np++;
    }
    if (!(f = fopen(path, "w")))
        return 0;
    fputs("{\n", f);
    for (i = 0; i < np; i++)
    {
        fputs("  ", f);
        json_str(f, p[i].key);
        fputs(": ", f);
        if (p[i].is_str)
            json_str(f, p[i].val);
        else
            fputs(p[i].val, f);
        fputs(i + 1 < np ? ",\n" : "\n", f);
    }
    fputs("}\n", f);
    fclose(f);
    return 1;
}

/* ---- themes ------------------------------------------------------------ */

#define DEFAULT_THEME "classic7" /* Apple's System 7 look, the game's own era */

static int name_cmp(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

/*  Every pack under <assets>/themes -- a directory with a theme.txt --
 *  by name, sorted, for the menu. */
static void scan_themes(App *a, const char *assets_dir)
{
    DIR           *d;
    struct dirent *e;
    RUiState      *s = &a->us;
    snprintf(a->themes_dir, sizeof a->themes_dir, "%s/themes", assets_dir);
    s->n_themes = 0;
    if (!(d = opendir(a->themes_dir)))
        return;
    while ((e = readdir(d)) && s->n_themes < RUI_MAX_THEMES)
    {
        char        p[1200];
        struct stat st;
        if (e->d_name[0] == '.')
            continue;
        snprintf(p, sizeof p, "%s/%s/theme.txt", a->themes_dir, e->d_name);
        if (stat(p, &st) != 0)
            continue;
        snprintf(s->theme_list[s->n_themes++], sizeof s->theme_list[0], "%s", e->d_name);
    }
    closedir(d);
    qsort(s->theme_list, (size_t)s->n_themes, sizeof s->theme_list[0], name_cmp);
}

/*  Put a scheme on: by name from assets/themes, by path, or "none" for
 *  the hand-drawn look.  `why` is for the log -- default, saved
 *  preference, --theme, chosen.  A default pack that is not there is
 *  not a warning: assets/ is built from the game's own files and a
 *  fresh checkout has no schemes yet. */
static int apply_theme_choice(App *a, const char *name, const char *why)
{
    char        path[1200];
    const char *base;
    size_t      len;
    if (!a->ui)
        return 0;
    if (strcmp(name, "none") == 0)
    {
        r_ui_clear_theme(a->ui);
        snprintf(a->us.theme_name, sizeof a->us.theme_name, "none");
        R_NOTE("theme", "none (%s)", why);
        return 1;
    }
    if (strchr(name, '/'))
        snprintf(path, sizeof path, "%s", name);
    else
        snprintf(path, sizeof path, "%s/%s", a->themes_dir, name);
    for (len = strlen(path); len > 1 && path[len - 1] == '/'; len--)
        path[len - 1] = 0;
    if (r_ui_set_theme(a->ui, path) != 0)
    {
        if (strcmp(why, "default") == 0)
            R_DBG("theme", "no %s pack in %s; the hand-drawn look", name, a->themes_dir);
        else
            R_WARN("theme", "%s: not found (%s)", path, why);
        return 0;
    }
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    snprintf(a->us.theme_name, sizeof a->us.theme_name, "%s", base);
    R_NOTE("theme", "%s (%s)", base, why);
    return 1;
}

/*  What a load brought in, for the log: the file and its shape, then
 *  the city the file describes.  Both load paths call this, so a city
 *  picked from the menu reports exactly what one named on the command
 *  line does.  ms is the time the load took. */
static void log_city_loaded(const City *c, const char *path, double ms)
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

static void set_city_name(App *a, const char *path)
{
    const char *base = strrchr(path, '/');
    base             = base ? base + 1 : path;
    snprintf(a->city_base, sizeof a->city_base, "%s", base);
    snprintf(a->us.save_path, sizeof a->us.save_path, "%s-saved.sc2", base);
}

static void set_speed(App *a, int32_t s)
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
static void step_clock(App *a)
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
            r_atlas_animate_runs(&a->atlas, a->anim_a, a->anim_b);
            r_gpu_set_palette(a->gpu, &a->atlas);
        }
    }
}

static int resweep(App *a)
{
    r_adapt_city(a->view, a->city);
    if (r_sweep(&a->atlas, a->view, &a->opts, &a->ops, &a->sw) != 0)
        return -1;
    if (r_gpu_set_ops(a->gpu, &a->ops, &a->sw) != 0)
        return -1;
    a->dirty = 0;
    return 0;
}

/*  The terrain field: per tile, a water tile's distance to the nearest
 *  land and a land tile's distance to the nearest water, both in tiles by
 *  a two-pass chamfer transform (3-4 weights, so diagonals count about
 *  1.4), and the water's depth in levels from ALTM.  The water shader
 *  lays foam by the first and grades colour by the depth; the ground
 *  shader lays sand and damp ground by the third. */
static void chamfer(int32_t *d)
{
    int32_t r, cc;
    for (r = 0; r < R_MAP; ++r)
        for (cc = 0; cc < R_MAP; ++cc)
        {
            int32_t i = r * R_MAP + cc, v = d[i];
            if (cc > 0 && d[i - 1] + 3 < v)
                v = d[i - 1] + 3;
            if (r > 0 && d[i - R_MAP] + 3 < v)
                v = d[i - R_MAP] + 3;
            if (r > 0 && cc > 0 && d[i - R_MAP - 1] + 4 < v)
                v = d[i - R_MAP - 1] + 4;
            if (r > 0 && cc < R_MAP - 1 && d[i - R_MAP + 1] + 4 < v)
                v = d[i - R_MAP + 1] + 4;
            d[i] = v;
        }
    for (r = R_MAP - 1; r >= 0; --r)
        for (cc = R_MAP - 1; cc >= 0; --cc)
        {
            int32_t i = r * R_MAP + cc, v = d[i];
            if (cc < R_MAP - 1 && d[i + 1] + 3 < v)
                v = d[i + 1] + 3;
            if (r < R_MAP - 1 && d[i + R_MAP] + 3 < v)
                v = d[i + R_MAP] + 3;
            if (r < R_MAP - 1 && cc < R_MAP - 1 && d[i + R_MAP + 1] + 4 < v)
                v = d[i + R_MAP + 1] + 4;
            if (r < R_MAP - 1 && cc > 0 && d[i + R_MAP - 1] + 4 < v)
                v = d[i + R_MAP - 1] + 4;
            d[i] = v;
        }
}

static void shore_field(const RCity *c, uint8_t *out)
{
    static int32_t d[R_MAP * R_MAP], w[R_MAP * R_MAP];
    const int32_t  big = 1 << 20;
    int32_t        k;
    for (k = 0; k < R_MAP * R_MAP; ++k)
    {
        d[k] = c->xter[k] >= 0x10u ? big : 0; /* water: distance to land */
        w[k] = c->xter[k] >= 0x10u ? 0 : big; /* land: distance to water */
    }
    chamfer(d);
    chamfer(w);
    for (k = 0; k < R_MAP * R_MAP; ++k)
    {
        int32_t v     = d[k] * R_SHORE_SCALE / 3; /* tiles * scale */
        int32_t u     = w[k] * R_SHORE_SCALE / 3;
        int32_t depth = 0;
        if (c->xter[k] >= 0x10u)
        {
            /*  ALTM's two heights: the table in bits 5..9 over the bed in
             *  bits 0..4.  The map edge draws this column with 284. */
            depth = (int32_t)((c->altm[k] >> 5) & 0x1Fu) -
                    (int32_t)(c->altm[k] & 0x1Fu);
            if (depth < 0)
                depth = 0;
            /*  Surface water on the ground, XTER 0x30 on, has no bed of
             *  its own (its table is its level) and would read as the
             *  shallows' turquoise; the original paints a stream the
             *  sea's blue, so it reads as a level deep.  Colour only. */
            if (c->xter[k] >= 0x30u && depth < 1)
                depth = 1;
            depth *= R_SHORE_SCALE;
        }
        out[k * 4]     = (uint8_t)(v > 255 ? 255 : v);
        out[k * 4 + 1] = (uint8_t)(depth > 255 ? 255 : depth);
        out[k * 4 + 2] = (uint8_t)(u > 255 ? 255 : u);
        out[k * 4 + 3] = 0;
    }
}

/*  The traffic, advanced to `time` and rebuilt into the movers' buffer;
 *  with the road mesh off, or underground, the buffer is empty. */
static int traffic_frame(App *a, float time)
{
    int on = a->gv.terrain3d && a->gv.roads3d && !a->opts.underground;
    if (!on)
        return r_gpu_set_movers(a->gpu, NULL, 0);
    if (a->traffic_time > 0.0f && time > a->traffic_time)
        r_traffic_step(&a->traffic, &a->mesh, time - a->traffic_time, time);
    a->traffic_time = time;
    if (r_traffic_build(&a->traffic, &a->mesh, a->view) != 0)
        return -1;
    return r_gpu_set_movers(a->gpu, a->traffic.scratch.land, a->traffic.scratch.n_land);
}

static int remesh(App *a)
{
    const RAtlasLevel *l = a->sw.level;
    static uint8_t     shore[R_MAP * R_MAP * 4];
    if (!l)
        return -1;
    if (r_mesh_build(&a->mesh, a->view, &a->atlas, l, a->opts.underground, a->angle != 0.0f, a->gv.roads3d && !a->opts.underground) != 0)
        return -1;
    shore_field(a->view, shore);
    /*  SC2K_FIELD_DUMP=path writes the field as a binary PPM (r: water's
     *  distance to land, g: depth, b: land's distance to water; all x16). */
    if (getenv("SC2K_FIELD_DUMP"))
    {
        FILE *fp = fopen(getenv("SC2K_FIELD_DUMP"), "wb");
        if (fp)
        {
            int32_t k;
            fprintf(fp, "P6\n%d %d\n255\n", R_MAP, R_MAP);
            for (k = 0; k < R_MAP * R_MAP; ++k)
                fwrite(&shore[k * 4], 1, 3, fp);
            fclose(fp);
        }
    }
    if (r_gpu_set_shore(a->gpu, shore, R_MAP) != 0)
        return -1;
    if (r_gpu_set_mesh(a->gpu, a->mesh.land, a->mesh.n_land, a->mesh.water, a->mesh.n_water) != 0)
        return -1;
    if (r_traffic_init(&a->traffic, &a->mesh, a->view) != 0)
        return -1;
    a->traffic_time = 0.0f;
    /*  SC2K_TRAFFIC_T=seconds advances the traffic that far on the
     *  build, so a headless frame shows it under way. */
    if (getenv("SC2K_TRAFFIC_T"))
    {
        float t = (float)atof(getenv("SC2K_TRAFFIC_T")), at = 0.0f;
        while (at < t)
        {
            r_traffic_step(&a->traffic, &a->mesh, 0.05f, at);
            at += 0.05f;
        }
    }
    if (traffic_frame(a, a->gv.time) != 0)
        return -1;
    a->mesh_dirty = 0;
    R_DBG("mesh", "%u vertices, %u retaining walls; canvas %dx%d origin %d,%d "
                  "tile %dx%d step %d",
          (unsigned)a->mesh.n_land,
          (unsigned)a->mesh.n_walls,
          (int)a->sw.w,
          (int)a->sw.h,
          (int)a->sw.ox,
          (int)a->sw.oy,
          (int)l->tile_w,
          (int)l->tile_h,
          (int)l->alt_step);
    return 0;
}

/*  Continuous zoom, as the renderer brief has it (section 5): the three
 *  art sets are levels of detail across one range, each used where it is
 *  closest to native -- 8 px below 0.354x, 16 px to 0.707x, 32 px above,
 *  the switch points the geometric means -- and the canvas resolved by
 *  the remaining factor, never more than about 1.41x either way.  `mx`,
 *  `my` is the window point that stays put, in pixels. */
static void zoom_to(App *a, float z, float mx, float my)
{
    int32_t set;
    float   native, f, f_old;
    float   cx, cy;
    if (z < 0.125f)
        z = 0.125f;
    if (z > 4.0f)
        z = 4.0f;
    set    = z >= 0.7071f ? 32 : (z >= 0.3536f ? 16 : 8);
    native = (float)set / 32.0f;
    f      = z / native;
    f_old  = a->gv.zoom > 0.0f ? a->gv.zoom : 1.0f;
    /* the canvas point under the pointer, in the current set's pixels */
    cx = (float)a->gv.scroll_x + mx / ((float)a->gv.scale * f_old);
    cy = (float)a->gv.scroll_y + my / ((float)a->gv.scale * f_old);
    if (set != a->opts.zoom)
    {
        float ratio = (float)set / (float)a->opts.zoom;
        cx *= ratio;
        cy *= ratio;
        a->opts.zoom  = set;
        a->dirty      = 1;
        a->mesh_dirty = 1;
    }
    a->gv.zoom     = f;
    a->zoom_world  = z;
    a->gv.scroll_x = (int32_t)(cx - mx / ((float)a->gv.scale * f));
    a->gv.scroll_y = (int32_t)(cy - my / ((float)a->gv.scale * f));
}

/*  The backdrop: the sky, except in the underground view, which the
 *  original fills white ($15452 _FillRect) before drawing. */
static const uint8_t *backdrop(const App *a)
{
    static const uint8_t white[3] = {255, 255, 255};
    return a->opts.underground ? white : a->sky;
}

/*  Crop the software frame to the target and compare with the GPU frame. */
static int check_frame(App *a, SDL_Window *win, const char *out_path)
{
    RImage  soft, gpu, crop;
    int32_t w = 0, h = 0, x, y;
    size_t  same = 0, npx;
    int     rc;

    {
        int ww, wh;
        if (!SDL_GetWindowSizeInPixels(win, &ww, &wh))
        {
            fprintf(stderr, "check: window size: %s\n", SDL_GetError());
            return -1;
        }
        w = ww / a->gv.scale;
        h = wh / a->gv.scale;
    }
    RGpuView fv = a->gv;
    if (a->opts.underground)
    {
        fv.water3d = 0;
        if (fv.terrain3d)
            fv.underground = 1;
    }
    if (r_gpu_readback(a->gpu, &fv, backdrop(a), w, h, 1, &gpu, NULL, NULL) != 0)
    {
        fprintf(stderr, "check: readback failed: %s\n", SDL_GetError());
        return -1;
    }
    {
        RSoftOpts so = a->opts;
        so.mesh      = 1;
        if (r_soft_render(&soft, &a->atlas, a->view, &so) != 0)
        {
            fprintf(stderr, "check: the software render failed\n");
            r_image_free(&gpu);
            return -1;
        }
    }
    memset(&crop, 0, sizeof crop);
    crop.w    = w;
    crop.h    = h;
    npx       = (size_t)w * (size_t)h;
    crop.rgb  = (uint8_t *)malloc(npx * 3u);
    crop.idx  = (uint8_t *)calloc(npx, 1);
    crop.prov = (uint16_t *)calloc(npx, sizeof(uint16_t));
    if (!crop.rgb || !crop.idx || !crop.prov)
    {
        r_image_free(&gpu);
        r_image_free(&soft);
        r_image_free(&crop);
        return -1;
    }
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x)
        {
            int32_t  sx = x + a->gv.scroll_x, sy = y + a->gv.scroll_y;
            uint8_t *d = crop.rgb + ((size_t)y * (size_t)w + (size_t)x) * 3u;
            if (sx >= 0 && sy >= 0 && sx < soft.w && sy < soft.h)
                memcpy(d, soft.rgb + ((size_t)sy * (size_t)soft.w + (size_t)sx) * 3u, 3u);
            else
                memcpy(d, soft.rgb, 3u); /* the snapped background */
        }
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x)
        {
            size_t k = ((size_t)y * (size_t)w + (size_t)x) * 3u;
            if (memcmp(crop.rgb + k, gpu.rgb + k, 3u) == 0)
                ++same;
        }
    printf("check     %dx%d at canvas %d,%d zoom %d: %zu of %zu pixels "
           "identical (%.4f%%), software crc %#010x, gpu crc %#010x\n",
           (int)w,
           (int)h,
           (int)a->gv.scroll_x,
           (int)a->gv.scroll_y,
           (int)a->opts.zoom,
           same,
           npx,
           100.0 * (double)same / (double)npx,
           r_image_crc(&crop),
           r_image_crc(&gpu));
    rc = 0;
    if (out_path)
    {
        char path[1024];
        if (r_image_write_png(&gpu, out_path) != 0)
            rc = -1;
        snprintf(path, sizeof path, "%s.soft.png", out_path);
        if (r_image_write_png(&crop, path) != 0)
            rc = -1;
        R_NOTE("write", "%s and %s", out_path, path);
    }
    r_image_free(&gpu);
    r_image_free(&soft);
    r_image_free(&crop);
    return rc;
}

/*  ---- the UI ------------------------------------------------------ */

static const char *GRAPH_NAMES[RUI_N_GRAPH] = {
    "City size", "Residents", "Commerce", "Industry", "Traffic", "Pollution", "Land value", "Crime", "Power shortfall", "Water shortfall", "Health", "Education", "Unemployment", "National GNP", "National population", "Federal rate"};
/*  The budget block's departments as sc2k.h names them; the first three
 *  slots the reconstruction has not named. */
static const char *DEPT_NAMES[RUI_N_DEPT] = {
    NULL, NULL, NULL, "Ordinances", "Bonds", "Police", "Fire", "Health", "Schools", "Colleges", "Roads", "Highways", "Subways", "Rail", "Transit", "Power"};

/*  The tile under a window point, from the sweep's own projection run
 *  backwards.  Along a diamond row + col and row - col are linear in y
 *  and x; the altitude term is settled by iterating on the tile found. */
/*  The grid point under a window point, and the altitude it was found
 *  at: the ground diamond at its own drawn altitude, found by iterating
 *  on the altitude, and turned back about the pivot when the view is
 *  turned.  Returns 0, or -1 off the map. */
static int screen_to_grid(App *a, float mx, float my, SDL_Window *win, float *ofc, float *ofr, float *oalt)
{
    const RAtlasLevel *l    = a->sw.level;
    float              dens = SDL_GetWindowPixelDensity(win);
    float              cx, cy, dif, altv = 0.0f, used = 0.0f, fc = 0.0f, fr = 0.0f;
    int                it;
    if (!l || dens <= 0.0f)
        return -1;
    {
        float f = (float)a->gv.scale * (a->gv.zoom > 0.0f ? a->gv.zoom : 1.0f);
        cx      = mx * dens / f + (float)a->gv.scroll_x;
        cy      = my * dens / f + (float)a->gv.scroll_y;
    }
    dif = (cx - (float)a->sw.ox - (float)l->tile_w * 0.5f) /
          ((float)l->tile_w * 0.5f); /* row - col */
    for (it = 0; it < 4; ++it)
    {
        float   spl;
        int32_t col, row, idx;
        used = altv;
        spl  = (cy - (float)a->sw.oy + used * (float)l->alt_step +
                (float)l->tile_h + 0.5f) /
               ((float)l->tile_h * 0.5f); /* row + col */
        fc   = (spl - dif) * 0.5f;
        fr   = (spl + dif) * 0.5f;
        if (a->angle != 0.0f)
        {
            /* the free rotation: turn back about the pivot */
            float ang = -a->angle * 3.14159265f / 180.0f;
            float xc = fc - a->gv.pivot_c, yc = fr - a->gv.pivot_r;
            fc = xc * cosf(ang) - yc * sinf(ang) + a->gv.pivot_c;
            fr = xc * sinf(ang) + yc * cosf(ang) + a->gv.pivot_r;
        }
        col = (int32_t)floorf(fc);
        row = (int32_t)floorf(fr);
        if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
            return -1;
        idx  = row * R_MAP + col;
        altv = (float)(a->opts.underground
                           ? (a->view->altm[idx] & 0x1Fu)
                           : (a->view->xter[idx] >= 0x10u
                                  ? ((a->view->altm[idx] >> 5) & 0x1Fu)
                                  : (a->view->altm[idx] & 0x1Fu)));
    }
    *ofc  = fc;
    *ofr  = fr;
    *oalt = used;
    return 0;
}

/*  The grid cell under the pointer, so every cell can be targeted on its
 *  own (the user: "it needs to be able to target individual grids").  A
 *  sprite standing in front of a cell does not take the pick; the
 *  footprint it belongs to is shown by the highlight instead. */
static void pick_tile(App *a, float mx, float my, SDL_Window *win)
{
    float fc, fr, alt;
    a->q_col = a->q_row = -1;
    if (screen_to_grid(a, mx, my, win, &fc, &fr, &alt) != 0)
        return;
    a->q_col = (int32_t)floorf(fc);
    a->q_row = (int32_t)floorf(fr);
}

/*  Pivot the rotation on the point under the view's centre, and keep
 *  that point where it is (the user: "rotations centered around the
 *  current view").  The pivot itself never moves when the view turns, so
 *  the scroll is set to put its unturned canvas position at the centre;
 *  after that any angle leaves the centre in place. */
static void pivot_on_view(App *a, SDL_Window *win)
{
    const RAtlasLevel *l    = a->sw.level;
    float              dens = SDL_GetWindowPixelDensity(win);
    float              fc, fr, alt, f, cx, cy;
    int                pw, ph;
    if (!l || dens <= 0.0f || !SDL_GetWindowSizeInPixels(win, &pw, &ph))
        return;
    if (screen_to_grid(a, (float)pw * 0.5f / dens, (float)ph * 0.5f / dens, win, &fc, &fr, &alt) != 0)
        return;
    a->gv.pivot_c  = fc;
    a->gv.pivot_r  = fr;
    f              = (float)a->gv.scale * (a->gv.zoom > 0.0f ? a->gv.zoom : 1.0f);
    cx             = (float)a->sw.ox + (float)l->tile_w * 0.5f + (fr - fc) * (float)l->tile_w * 0.5f;
    cy             = (float)a->sw.oy - ((float)l->tile_h + 0.5f) + (fc + fr) * (float)l->tile_h * 0.5f -
                     alt * (float)l->alt_step;
    a->gv.scroll_x = (int32_t)lroundf(cx - (float)pw * 0.5f / f);
    a->gv.scroll_y = (int32_t)lroundf(cy - (float)ph * 0.5f / f);
}

/*  What the UI shows this frame. */
static void ui_fill(App *a)
{
    RUiState   *s = &a->us;
    const City *c = a->city;
    uint32_t    inst, culled;
    int         k;
    if (a->view->name[0])
        snprintf(s->city_name, sizeof s->city_name, "%s", a->view->name);
    else
        snprintf(s->city_name, sizeof s->city_name, "%s", a->city_base);
    s->year       = c->year_founded + c->date / 300;
    s->month      = (c->date / 25) % 12;
    s->funds      = c->funds;
    s->population = c->population;
    s->stage      = c->misc[MISC_STAGE];
    for (k = 0; k < 3; ++k)
        s->demand[k] = c->rci_demand[k];
    s->power_pct      = c->power_pct;
    s->water_pct      = c->water_pct;
    s->unemployment   = c->unemployment;
    s->land_value_tot = c->land_value_tot;
    s->crime_tot      = c->crime_tot;
    s->traffic_tot    = c->traffic_tot;
    s->pollution_tot  = c->pollution_tot;
    s->fps            = a->fps;
    s->frame_ms       = a->frame_ms;
    r_gpu_stats(a->gpu, &inst, &culled);
    s->instances  = inst;
    s->culled     = culled;
    s->mesh_verts = a->mesh.n_land;
    s->driver     = r_gpu_driver(a->gpu);
    for (k = 0; k < RUI_N_GRAPH && k < N_GRAPH; ++k)
    {
        s->graph[k]      = c->graph[k];
        s->graph_name[k] = GRAPH_NAMES[k];
    }
    for (k = 0; k < RUI_N_DEPT && k < N_DEPT; ++k)
    {
        s->dept_name[k]    = DEPT_NAMES[k];
        s->dept_amount[k]  = c->dept[k].amount;
        s->dept_funding[k] = c->dept[k].funding;
        s->dept_accrued[k] = c->dept[k].accrued;
    }
    s->q_ok = a->q_col >= 0 && a->q_row >= 0;
    if (s->q_ok)
    {
        int y = a->q_row, x = a->q_col;
        s->q_col         = x;
        s->q_row         = y;
        s->q_xter        = c->xter[y][x];
        s->q_xbld        = c->xbld[y][x];
        s->q_xzon        = c->xzon[y][x];
        s->q_xbit        = c->xbit[y][x];
        s->q_xund        = c->xund[y][x];
        s->q_xtxt        = c->xtxt[y][x];
        s->q_altm        = c->altm[y][x];
        s->q_alt         = s->q_xter >= 0x10 ? (int32_t)((s->q_altm >> 5) & 0x1F)
                                             : (int32_t)(s->q_altm & 0x1F);
        s->q_water_level = c->water_level;
        s->q_building    = BUILDING[s->q_xbld].name;
        if (r_mesh_query(a->view, x, y, s->q_mesh, sizeof s->q_mesh) != 0)
            s->q_mesh[0] = 0;
        /*  The footprint the cell belongs to, as the original finds it
         *  ($763A): its edge n and its north-east tile, so it covers rows
         *  oy..oy+n-1 and columns ox-n+1..ox.  The highlight outlines the
         *  whole footprint (the user: "the entire footprint was also
         *  highlighted"), on the surface the mesh draws -- a building's
         *  pad is flat, a lone cell follows its own plane -- or, in the
         *  underground view, on the ground drawn there, the seabed under
         *  water (the user: "highlighting the terrain, not the water"). */
        {
            int n, x0, y0;
            s->q_orow = y; /* $763A moves the cell's own coordinates to the origin */
            s->q_ocol = x;
            n         = sim_footprint_origin(c, &s->q_orow, &s->q_ocol, c->xbld[y][x]);
            x0        = s->q_ocol - n + 1;
            y0        = s->q_orow;
            if (n < 1 || n > 4 || x0 < 0 || y0 < 0 || s->q_ocol >= R_MAP || y0 + n > R_MAP)
            {
                n         = 1;
                x0        = x;
                y0        = y;
                s->q_ocol = x;
                s->q_orow = y;
            }
            s->q_size    = n;
            s->q_poly_ok = 0;
            {
                const RAtlasLevel *lv = a->sw.level;
                int                ug = a->opts.underground;
                float              t[4], z[4];
                int                ok = lv != NULL;
                /* NW, NE, SE, SW of the footprint, each from its corner tile */
                ok   = ok && r_mesh_tile_corners(a->view, x0, y0, ug, t) == 0;
                z[0] = t[0];
                ok   = ok && r_mesh_tile_corners(a->view, x0 + n - 1, y0, ug, t) == 0;
                z[1] = t[1];
                ok   = ok && r_mesh_tile_corners(a->view, x0 + n - 1, y0 + n - 1, ug, t) == 0;
                z[2] = t[2];
                ok   = ok && r_mesh_tile_corners(a->view, x0, y0 + n - 1, ug, t) == 0;
                z[3] = t[3];
                if (ok)
                {
                    float f    = (float)a->gv.scale * (a->gv.zoom > 0.0f ? a->gv.zoom : 1.0f);
                    float dens = a->win_density > 0.0f ? a->win_density : 1.0f;
                    float ang  = a->angle * 3.14159265f / 180.0f;
                    int   kq;
                    for (kq = 0; kq < 4; ++kq)
                    {
                        static const int dc[4] = {0, 1, 1, 0}, dr[4] = {0, 0, 1, 1};
                        float            cc = (float)(x0 + dc[kq] * n) - a->gv.pivot_c, rr = (float)(y0 + dr[kq] * n) - a->gv.pivot_r;
                        float            rc = cc * cosf(ang) - rr * sinf(ang) + a->gv.pivot_c;
                        float            rw = cc * sinf(ang) + rr * cosf(ang) + a->gv.pivot_r;
                        float            cx = (float)a->sw.ox + (float)lv->tile_w * 0.5f + (rw - rc) * (float)lv->tile_w * 0.5f;
                        float            cy = (float)a->sw.oy - ((float)lv->tile_h + 0.5f) + (rc + rw) * (float)lv->tile_h * 0.5f -
                                              z[kq] * (float)lv->alt_step;
                        s->q_poly[kq][0]    = (cx - (float)a->gv.scroll_x) * f / dens;
                        s->q_poly[kq][1]    = (cy - (float)a->gv.scroll_y) * f / dens;
                    }
                    s->q_poly_ok = 1;
                }
            }
        }
    }
    /* the switches */
    s->speed       = a->speed;
    s->zoom        = a->opts.zoom;
    s->scale       = a->gv.scale;
    s->terrain3d   = a->gv.terrain3d;
    s->water3d     = a->gv.water3d;
    s->roads3d     = a->gv.roads3d;
    s->grid        = a->gv.grid;
    s->plain_sweep = a->gv.plain_sweep;
    s->underground = a->opts.underground;
    s->view        = a->opts.view;
}

/*  What a tile sounds like when queried: the original lets a building be
 *  heard under the query tool.  By the building's name where the table
 *  has one and by the piece's range where it has not; 0 for silence. */
static int building_sound(const City *c, int32_t row, int32_t col)
{
    uint8_t     b    = c->xbld[row][col];
    uint8_t     zone = c->xzon[row][col] & 0x0F;
    const char *name = BUILDING[b].name;
    if (zone == 8)
        return R_SND_TAKEOFF;
    if (zone == 9)
        return R_SND_SHIP;
    if (name)
    {
        if (strstr(name, "Police"))
            return R_SND_POLICE;
        if (strstr(name, "Fire"))
            return R_SND_FIRETRUCK;
        if (strstr(name, "School") || strstr(name, "College"))
            return R_SND_SCHOOLBELL;
        if (strstr(name, "Prison"))
            return R_SND_PRISON;
        if (strstr(name, "Arco"))
            return R_SND_ARCO;
        if (strstr(name, "Wind"))
            return R_SND_WIND;
        if (strstr(name, "Nuclear") || strstr(name, "Fusion") || strstr(name, "Microwave"))
            return R_SND_ZZAP;
        if (strstr(name, "Stadium"))
            return R_SND_CHEERS;
        if (strstr(name, "Hospital"))
            return R_SND_SIREN;
    }
    if (b >= 0x1D && b <= 0x2B)
        return R_SND_HORNS; /* roads */
    if (b >= 0x2C && b <= 0x3A)
        return R_SND_TRAIN; /* rail  */
    if (b >= 0x0E && b <= 0x1C)
        return R_SND_ZZAP; /* power lines */
    if (b >= 0x51 && b <= 0x5F)
        return R_SND_HORNS; /* bridges and ramps */
    return 0;
}

/*  The selected tool on the tile under the pointer. */
static void use_tool(App *a, SDL_Window *win)
{
    RUiState *s   = &a->us;
    int32_t   col = a->q_col, row = a->q_row;
    int       pw, ph;
    (void)win;
    if (s->tool < 0 || col < 0)
        return;
    switch (s->tool)
    {
        case RUI_TOOL_BULLDOZER:
            sim_demolish_tile(a->city, row, col, 0, 0);
            r_ui_log(s, "Bulldozed %d, %d", (int)col, (int)row);
            r_sound_play(a->snd, R_SND_BULLDOZE);
            a->dirty      = 1;
            a->mesh_dirty = 1;
            break;
        case RUI_TOOL_QUERY:
            s->show_query = 1;
            r_sound_play(a->snd, building_sound(a->city, row, col));
            break;
        case RUI_TOOL_CENTER:
            SDL_GetWindowSizeInPixels(win, &pw, &ph);
            {
                const RAtlasLevel *l   = a->sw.level;
                int32_t            idx = row * R_MAP + col;
                int32_t            alt = a->view->xter[idx] >= 0x10u
                                             ? (int32_t)((a->view->altm[idx] >> 5) & 0x1Fu)
                                             : (int32_t)(a->view->altm[idx] & 0x1Fu);
                int32_t            cx  = a->sw.ox + (row - col) * (l->tile_w / 2) + l->tile_w / 2;
                int32_t            cy  = a->sw.oy + (row + col) * (l->tile_h / 2) - alt * l->alt_step -
                                         l->tile_h / 2;
                a->gv.scroll_x         = cx - (pw / a->gv.scale) / 2;
                a->gv.scroll_y         = cy - (ph / a->gv.scale) / 2;
            }
            break;
        default:
            r_ui_log(s, "%s: not yet ported", RUI_TOOL_NAME[s->tool]);
            r_sound_play(a->snd, R_SND_ERROR);
            break;
    }
}

/*  Turn the free rotation by `deg`; entering or leaving it rebuilds the
 *  mesh, which draws every water surface and all four cuts when turned. */
static void rotate_by(App *a, float deg, SDL_Window *win)
{
    float was = a->angle;
    pivot_on_view(a, win); /* about the point the view looks at */
    a->angle = fmodf(a->angle + deg + 360.0f, 360.0f);
    if (fabsf(a->angle) < 0.01f || fabsf(a->angle - 360.0f) < 0.01f)
        a->angle = 0.0f;
    a->gv.angle = a->angle;
    if ((was == 0.0f) != (a->angle == 0.0f))
        a->mesh_dirty = 1;
    if (a->angle != 0.0f)
        r_ui_log(&a->us, "Rotated %.0f degrees about the view's centre", (double)a->angle);
}

/*  What the UI changed or asked for. */
static void ui_apply(App *a, SDL_Window *win, int pw, int ph)
{
    RUiState *s = &a->us;
    if (s->speed != a->speed)
        set_speed(a, s->speed);
    if (s->zoom != a->opts.zoom)
        zoom_to(a, (float)s->zoom / 32.0f, (float)pw * 0.5f, (float)ph * 0.5f);
    if (s->scale >= 1 && s->scale <= 4)
        a->gv.scale = s->scale;
    a->gv.terrain3d = s->terrain3d;
    a->gv.water3d   = s->water3d;
    if (s->roads3d != a->gv.roads3d)
        a->mesh_dirty = 1;
    a->gv.roads3d     = s->roads3d;
    a->gv.grid        = s->grid;
    a->gv.plain_sweep = s->plain_sweep;
    if (s->underground != a->opts.underground || s->view != a->opts.view)
    {
        if (s->underground != a->opts.underground)
            a->mesh_dirty = 1;
        a->opts.underground = s->underground;
        a->opts.view        = s->view;
        a->dirty            = 1;
    }
    if (s->want_zoom_step > 0)
        zoom_to(a, a->zoom_world * 2.0f, (float)pw * 0.5f, (float)ph * 0.5f);
    else if (s->want_zoom_step < 0)
        zoom_to(a, a->zoom_world * 0.5f, (float)pw * 0.5f, (float)ph * 0.5f);
    if (s->want_rotate)
        rotate_by(a, s->want_rotate > 0 ? -90.0f : 90.0f, win);
    if (s->want_quit)
        a->quit = 1;
    if (s->want_screenshot)
    {
        check_frame(a, win, "sc2kgpu-check.png");
        r_ui_log(s, "Wrote sc2kgpu-check.png");
    }
    if (s->want_load && s->load_path[0])
    {
        /*  r_adapt_city runs from a->city every frame, so swapping the
         *  city is all a load has to do. */
        City    *fresh = (City *)calloc(1, sizeof *fresh);
        uint64_t t0    = SDL_GetTicksNS();
        R_NOTE("city", "loading %s", s->load_path);
        if (fresh && city_load(s->load_path, fresh))
        {
            city_free(a->city);
            free(a->city);
            a->city = fresh;
            set_city_name(a, s->load_path);
            set_speed(a, (int32_t)a->city->misc[MISC_SPEED]);
            /*  the sweep and the terrain mesh are both cached, so a new
             *  city is not visible until they are asked to rebuild --
             *  without this the map keeps drawing the old one */
            a->dirty      = 1;
            a->mesh_dirty = 1;
            /*  and the UI's own copy of the speed, or the next frame
             *  compares the two and puts the old speed back */
            s->speed = a->speed;
            a->q_col = a->q_row = -1; /* the query box is about a gone tile */
            log_city_loaded(a->city, s->load_path, NS_MS(SDL_GetTicksNS() - t0));
            r_ui_log(s, "Loaded %s", a->city_base);
        }
        else
        {
            free(fresh);
            R_WARN("city", "could not load %s", s->load_path);
            r_ui_log(s, "Could not load %s", s->load_path);
        }
        s->want_load = 0;
    }
    if (s->want_theme)
    {
        char pick[64];
        snprintf(pick, sizeof pick, "%s", s->theme_name);
        if (apply_theme_choice(a, pick, "chosen"))
        {
            if (prefs_set("theme", s->theme_name))
                R_DBG("prefs", "theme=%s saved", s->theme_name);
            else
                R_WARN("prefs", "could not save the theme preference");
            r_ui_log(s, "Theme: %s", s->theme_name);
        }
        s->want_theme = 0;
    }
    if (s->want_save)
    {
        if (city_save(s->save_path, a->city))
            r_ui_log(s, "Saved %s", s->save_path);
        else
            r_ui_log(s, "Could not save %s", s->save_path);
    }
    if (s->want_disaster)
    {
        static const char *names[RUI_N_DISASTERS] = {
            "", "Fire", "Flood", "Riot", "Tornado", "Monster", "Earthquake", "Hurricane", "Chemical spill", "Air crash", "Microwave", "Meltdown", "Volcano", "Firestorm", "Pollution"};
        int r = 0;
        switch (s->want_disaster)
        {
            case RUI_DISASTER_FIRE:
                r = sim_disaster_fire(a->city);
                break;
            case RUI_DISASTER_FLOOD:
                r = sim_disaster_flood(a->city);
                break;
            case RUI_DISASTER_RIOT:
                r = sim_disaster_riot(a->city);
                break;
            case RUI_DISASTER_TORNADO:
                r = sim_disaster_tornado(a->city);
                break;
            case RUI_DISASTER_MONSTER:
                r = sim_disaster_monster(a->city);
                break;
            case RUI_DISASTER_EARTHQUAKE:
                r = sim_disaster_earthquake(a->city);
                break;
            case RUI_DISASTER_HURRICANE:
                r = sim_disaster_hurricane(a->city);
                break;
            case RUI_DISASTER_CHEMICAL:
                r = sim_disaster_chemical(a->city);
                break;
            case RUI_DISASTER_AIR_CRASH:
                r = sim_disaster_air_crash(a->city);
                break;
            case RUI_DISASTER_MICROWAVE:
                r = sim_disaster_microwave(a->city);
                break;
            case RUI_DISASTER_MELTDOWN:
                r = sim_disaster_meltdown(a->city);
                break;
            case RUI_DISASTER_VOLCANO:
                r = sim_disaster_volcano(a->city);
                break;
            case RUI_DISASTER_FIRESTORM:
                r = sim_disaster_firestorm(a->city);
                break;
            case RUI_DISASTER_POLLUTION:
                r = sim_disaster_pollution(a->city);
                break;
            default:
                break;
        }
        r_ui_log(s, "%s: %s", names[s->want_disaster], r ? "started" : "nothing happened");
        if (r)
        {
            static const int sounds[RUI_N_DISASTERS] = {
                0, R_SND_FIRE_LOOP, R_SND_FLOOD, R_SND_SHOT, R_SND_SIREN, R_SND_ROAR, R_SND_SIREN, R_SND_SIREN, R_SND_EXPLODE, R_SND_MAYDAY, R_SND_ZZAP, R_SND_EXPLODE, R_SND_EXPLODE, R_SND_FIRE_LOOP, 0};
            r_sound_play(a->snd, sounds[s->want_disaster]);
        }
        a->dirty      = 1;
        a->mesh_dirty = 1; /* a disaster may move the ground */
    }
    if (s->want_sound)
        r_sound_play(a->snd, s->want_sound);
    s->want_quit = s->want_screenshot = s->want_save = 0;
    s->want_load                                     = 0;
    s->want_disaster                                 = 0;
    s->want_zoom_step = s->want_rotate = s->want_sound = 0;
}

/*  One frame with the UI on it, read back to a PNG. */
static int shot_frame(App *a, SDL_Window *win, const char *path)
{
    RImage img;
    int    pw, ph, rc;
    if (!SDL_GetWindowSizeInPixels(win, &pw, &ph))
        return -1;
    a->gv.time = (float)((double)(SDL_GetTicksNS() - a->t0_ns) / 1e9);
    /*  Two UI frames: ImGui keeps a window hidden on the frame that
     *  first sizes it, so the first frame settles the layout and the
     *  second is the one drawn. */
    ui_fill(a);
    r_ui_frame(a->ui, &a->us);
    ui_fill(a);
    r_ui_frame(a->ui, &a->us);
    RGpuView fv = a->gv;
    if (a->opts.underground)
    {
        fv.water3d     = 0;
        fv.underground = 1;
    }
    if (r_gpu_readback(a->gpu, &fv, backdrop(a), pw, ph, a->gv.scale, &img, a->ui ? r_ui_render : NULL, a->ui) != 0)
    {
        fprintf(stderr, "shot: readback failed: %s\n", SDL_GetError());
        return -1;
    }
    rc = r_image_write_png(&img, path);
    R_NOTE("write", "%s (%dx%d)", path, (int)img.w, (int)img.h);
    r_image_free(&img);
    return rc;
}

/* ---- finding things ----------------------------------------------------
 *
 *  The old invocation wanted the assets directory and the city spelled
 *  out every time.  Both can be found: the assets sit beside the binary
 *  or a few directories above it, and the cities live wherever the game
 *  was installed.  Either can still be given outright.
 * ------------------------------------------------------------------ */

static int is_dir(const char *p)
{
    struct stat st;
    return p && *p && stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int is_file(const char *p)
{
    struct stat st;
    return p && *p && stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/*  an assets directory is one with the tile atlas in it */
static int looks_like_assets(const char *p)
{
    char probe[1024];
    snprintf(probe, sizeof probe, "%s/atlas.json", p);
    return is_file(probe);
}

/*  $SC2K_ASSETS, then beside the binary, then up towards the repo root,
 *  then the working directory. */
static int find_assets(char *out, size_t n, const char *argv0)
{
    const char *env = getenv("SC2K_ASSETS");
    char        base[1024], probe[1024];
    const char *slash;
    int         up;

    if (env && looks_like_assets(env))
    {
        snprintf(out, n, "%s", env);
        return 1;
    }
    slash = argv0 ? strrchr(argv0, '/') : NULL;
    if (slash)
    {
        size_t len = (size_t)(slash - argv0);
        if (len >= sizeof base)
            len = sizeof base - 1;
        memcpy(base, argv0, len);
        base[len] = 0;
    }
    else
    {
        snprintf(base, sizeof base, ".");
    }
    for (up = 0; up < 6; ++up)
    {
        char walk[1024];
        int  k;
        snprintf(walk, sizeof walk, "%s", base);
        for (k = 0; k < up; ++k)
        {
            size_t l = strlen(walk);
            snprintf(walk + l, sizeof walk - l, "/..");
        }
        snprintf(probe, sizeof probe, "%s/assets", walk);
        if (looks_like_assets(probe))
        {
            snprintf(out, n, "%s", probe);
            return 1;
        }
    }
    if (looks_like_assets("assets"))
    {
        snprintf(out, n, "%s", "assets");
        return 1;
    }
    return 0;
}

/*  $SC2K_CITIES, then the usual install, then ./Cities */
/*  Where the cities are, most specific first.  `cities/` beside the
 *  build is the answer that needs no setup: the repository carries the
 *  collection, so a fresh clone can open one straight away.  The
 *  environment variable still wins, for a folder of your own. */
static int find_cities(char *out, size_t n)
{
    static const char *const REL[] = {"cities", "../cities", "../../cities", "Cities", NULL};
    const char              *env   = getenv("SC2K_CITIES");
    const char              *home  = getenv("HOME");
    char                     probe[1024];
    int                      i;

    if (is_dir(env))
    {
        snprintf(out, n, "%s", env);
        return 1;
    }
    for (i = 0; REL[i]; i++)
        if (is_dir(REL[i]))
        {
            snprintf(out, n, "%s", REL[i]);
            return 1;
        }
    if (home)
    {
        snprintf(probe, sizeof probe, "%s/Downloads/SimCity 2000\xc2\xae Collection/Cities", home);
        if (is_dir(probe))
        {
            snprintf(out, n, "%s", probe);
            return 1;
        }
    }
    return 0;
}

/*  Everything in the cities directory that is a file, sorted, so the
 *  load menu has something to show. */
static int scan_cities(RUiState *s)
{
    DIR           *d;
    struct dirent *e;
    int            i, j;

    s->n_cities = 0;
    if (!s->city_dir[0] || !(d = opendir(s->city_dir)))
        return 0;
    while ((e = readdir(d)) && s->n_cities < R_MAX_CITIES)
    {
        char full[1024];
        if (e->d_name[0] == '.')
            continue;
        snprintf(full, sizeof full, "%s/%s", s->city_dir, e->d_name);
        if (!is_file(full))
            continue;
        snprintf(s->city_list[s->n_cities], sizeof s->city_list[0], "%s", e->d_name);
        s->n_cities++;
    }
    closedir(d);
    for (i = 1; i < s->n_cities; ++i) /* the list is short; keep it simple */
        for (j = i; j > 0 && strcasecmp(s->city_list[j - 1], s->city_list[j]) > 0;
             --j)
        {
            char t[80];
            memcpy(t, s->city_list[j - 1], sizeof t);
            memcpy(s->city_list[j - 1], s->city_list[j], sizeof t);
            memcpy(s->city_list[j], t, sizeof t);
        }
    return s->n_cities;
}

/*  A city argument may be a path or just a name; a name is looked for in
 *  the cities directory. */
static int resolve_city(char *out, size_t n, const char *arg, const char *dir)
{
    char probe[1024];
    if (is_file(arg))
    {
        snprintf(out, n, "%s", arg);
        return 1;
    }
    if (dir && *dir)
    {
        snprintf(probe, sizeof probe, "%s/%s", dir, arg);
        if (is_file(probe))
        {
            snprintf(out, n, "%s", probe);
            return 1;
        }
        snprintf(probe, sizeof probe, "%s/%s.sc2", dir, arg);
        if (is_file(probe))
        {
            snprintf(out, n, "%s", probe);
            return 1;
        }
    }
    return 0;
}

int r_game_main(int argc, char **argv)
{
    App         a;
    SDL_Window *win;
    const char *check_out = NULL, *shot_out = NULL, *theme_dir = NULL;
    int         check = 0, i, ww = 1280, wh = 800;
    int         run_frames = 0, run_speed = 0, sprites = 0, geometry = 0;
    float       zoomf      = 0.0f;
    int         sound_test = 0, mesh_check = 0;
    int         have_scroll = 0, scroll_x = 0, scroll_y = 0;
    int         have_centre = 0, centre_col = 0, centre_row = 0;
    int         have_pick = 0;
    float       pick_x = 0.0f, pick_y = 0.0f;
    /*  Phase clocks for the init log.  SDL3's ticks need no SDL_Init. */
    const uint64_t t_start = SDL_GetTicksNS();
    uint64_t       t_phase = t_start, t_atlas = 0, t_city = 0, t_gpu = 0, t_sweep = 0;
#define PHASE_MS(var)                    \
    do                                   \
    {                                    \
        uint64_t now = SDL_GetTicksNS(); \
        (var)        = now - t_phase;    \
        t_phase      = now;              \
    } while (0)
    int32_t pixel_scale = 0;
    char    err[256];

    char assets_dir[1024] = {0}, city_path[1024] = {0};
    int  first_opt = 1;

    /*  Two positional arguments used to be required.  Now both are
     *  optional and either can be given:
     *
     *      sc2kgpu                     the load menu
     *      sc2kgpu Bayview             by name, from the cities directory
     *      sc2kgpu path/to/City        by path
     *      sc2kgpu assets path/City    the old form, still understood
     *
     *  --assets DIR overrides the search, as do $SC2K_ASSETS and
     *  $SC2K_CITIES. */
    if (argc >= 2 && argv[1][0] != '-')
    {
        if (looks_like_assets(argv[1]))
        {
            snprintf(assets_dir, sizeof assets_dir, "%s", argv[1]);
            first_opt = 2;
            if (argc >= 3 && argv[2][0] != '-')
            {
                snprintf(city_path, sizeof city_path, "%s", argv[2]);
                first_opt = 3;
            }
        }
        else
        {
            snprintf(city_path, sizeof city_path, "%s", argv[1]);
            first_opt = 2;
        }
    }
    for (i = first_opt; i < argc; ++i)
        if (strcmp(argv[i], "--assets") == 0 && i + 1 < argc)
            snprintf(assets_dir, sizeof assets_dir, "%s", argv[++i]);
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            printf("usage: %s [city] [--assets DIR] [--zoom 8|16|32] "
                   "[--scale N]\n"
                   "       with no city, the load menu opens.\n"
                   "\n"
                   "  --sprites     the original's terrain and water art\n"
                   "  --geometry    the terrain mesh and water shader,\n"
                   "                even in a headless --run\n"
                   "  --centre C,R  put map tile (col,row) in the middle\n"
                   "  --scroll X,Y  the canvas pixel at the top-left\n"
                   "  --shot FILE   render one frame to a PNG and exit\n"
                   "  --run N       advance N frames headless\n"
                   "  --version     the version, one line\n"
                   "  --theme NAME  a Kaleidoscope scheme: a pack under assets/themes, a path, or none;\n"
                   "                the default is classic7, and Options > Theme remembers a choice\n"
                   "\n"
                   "  arcology --modes   lists the developer modes\n",
                   argv[0]);
            return 0;
        }
    memset(&a, 0, sizeof a);
    r_soft_defaults(&a.opts);
    a.gv.pivot_c = a.gv.pivot_r = 64.0f; /* until the view turns: the map's centre */
    a.sky[0]                    = 16;
    a.sky[1]                    = 20;
    a.sky[2]                    = 22;
    for (i = first_opt; i < argc; ++i)
    {
        if (strcmp(argv[i], "--zoom") == 0 && i + 1 < argc)
            a.opts.zoom = atoi(argv[++i]);
        else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
            pixel_scale = atoi(argv[++i]);
        else if (strcmp(argv[i], "--check") == 0 && i + 1 < argc)
        {
            check     = 1;
            check_out = argv[++i];
        }
        else if (strcmp(argv[i], "--scroll") == 0 && i + 1 < argc &&
                 sscanf(argv[++i], "%d,%d", &scroll_x, &scroll_y) == 2)
            have_scroll = 1;
        else if (strcmp(argv[i], "--no-things") == 0)
            a.opts.draw_things = 0;
        else if (strcmp(argv[i], "--run") == 0 && i + 1 < argc)
            run_frames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc)
            run_speed = atoi(argv[++i]);
        else if (strcmp(argv[i], "--terrain3d") == 0)
            a.gv.terrain3d = 1;
        else if (strcmp(argv[i], "--mesh-only") == 0)
            a.gv.mesh_only = 1;
        else if (strcmp(argv[i], "--mesh-check") == 0)
            mesh_check = 1; /* build the terrain and prove it has no free edge */
        else if (strcmp(argv[i], "--pick") == 0 && i + 1 < argc &&
                 sscanf(argv[++i], "%f,%f", &pick_x, &pick_y) == 2)
            have_pick = 1; /* what the query tool reads at a window point */
        else if (strcmp(argv[i], "--grid") == 0)
            a.gv.grid = 1;
        else if (strcmp(argv[i], "--water3d") == 0)
            a.gv.water3d = 1;
        else if (strcmp(argv[i], "--roads3d") == 0)
            a.gv.roads3d = 1;
        else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc)
            shot_out = argv[++i];
        else if (strcmp(argv[i], "--theme") == 0 && i + 1 < argc)
            theme_dir = argv[++i];
        else if (strcmp(argv[i], "--sprites") == 0)
            sprites = 1;
        else if (strcmp(argv[i], "--geometry") == 0)
            geometry = 1;
        else if (strcmp(argv[i], "--centre") == 0 && i + 1 < argc &&
                 sscanf(argv[++i], "%d,%d", &centre_col, &centre_row) == 2)
            have_centre = 1;
        else if (strcmp(argv[i], "--center") == 0 && i + 1 < argc &&
                 sscanf(argv[++i], "%d,%d", &centre_col, &centre_row) == 2)
            have_centre = 1;
        else if (strcmp(argv[i], "--underground") == 0)
            a.opts.underground = 1;
        else if (strcmp(argv[i], "--zoomf") == 0 && i + 1 < argc)
            zoomf = (float)atof(argv[++i]);
        else if (strcmp(argv[i], "--angle") == 0 && i + 1 < argc)
            a.angle = a.gv.angle = (float)atof(argv[++i]);
        else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0)
            r_log_set_level(R_LOG_DEBUG);
        else if (strcmp(argv[i], "--no-colour") == 0 ||
                 strcmp(argv[i], "--no-color") == 0)
            r_log_set_colour(0);
        else if (strcmp(argv[i], "--sound-test") == 0 && i + 1 < argc)
            sound_test = atoi(argv[++i]); /* play one effect in a headless run */
    }
    /*  The geometry and the water shader are the game's look; the
     *  sprites are the check's baseline and an option (--sprites, or
     *  the t and y keys).
     *
     *  A headless run defaults to sprites because --check and --run are
     *  comparison harnesses and the sprites are what they compare
     *  against.  --geometry overrides that: it is how a headless run
     *  produces a picture of the game as it actually looks, which is
     *  what tools/gen_showcase.py wants. */
    if (geometry || (!check && !run_frames && !sprites))
        a.gv.terrain3d = a.gv.water3d = a.gv.roads3d = 1;
    a.q_col = a.q_row = -1;
    a.us.show_palette = 1;
    a.us.show_log     = 0; /* under Windows > Messages */
    a.us.tool         = -1;
    {
        static const char *const BANNER[] = {
            "   ___                __",
            "  / _ | ___________  / /__  ___ ___ __",
            " / __ |/ __/ __/ _ \\/ / _ \\/ _ `/ // /",
            "/_/ |_/_/  \\__/\\___/_/\\___/\\_, /\\_, /",
            "                          /___//___/",
        };
        char plat[160], line[256];
        r_log_banner(BANNER, 5);
        platform_line(plat, sizeof plat);
        snprintf(line, sizeof line, "\nArcology %s -- the SimCity 2000 simulation, reconstructed\n%s\n\n", ARC_VERSION_FULL, plat);
        r_log_raw(line);
        r_log_raw("SimCity 2000 is copyright (c) 1993-1995 Maxis, now part of Electronic Arts Inc.\n"
                  "Not affiliated with, endorsed by, or connected to Electronic Arts or Maxis.\n"
                  "Arcology is copyright (c) 2026 the Arcology authors, MIT licence.\n"
                  "https://github.com/umamibeef/arcology\n");
    }
    if (check)
        R_NOTE("init", "arcology, check against %s", check_out ? check_out : "the original");
    else if (run_frames)
    {
        char speed[32];
        if (run_speed)
            snprintf(speed, sizeof speed, "speed %d", run_speed);
        else
            snprintf(speed, sizeof speed, "the city's speed");
        R_NOTE("init", "arcology, headless: %d frames at %s%s%s", run_frames, speed, shot_out ? ", shot " : "", shot_out ? shot_out : "");
    }
    else if (shot_out)
        R_NOTE("init", "arcology, one frame to %s", shot_out);
    else
        R_NOTE("init", "arcology, %dx%d window", ww, wh);
    R_DBG("init", "zoom %d, scale %s, %s", (int)a.opts.zoom, pixel_scale < 1 ? "auto" : pixel_scale == 1 ? "1"
                                                                                                         : "2",
          a.gv.terrain3d ? "geometry" : "sprites");

    /*  where the cities are, for resolving a name and for the menu */
    find_cities(a.us.city_dir, sizeof a.us.city_dir);
    scan_cities(&a.us);
    if (a.us.city_dir[0])
        R_DBG("cities", "%s (%d)", a.us.city_dir, a.us.n_cities);
    else
        R_DBG("cities", "none found");

    if (city_path[0])
    {
        char resolved[1024];
        if (!resolve_city(resolved, sizeof resolved, city_path, a.us.city_dir))
        {
            R_ERR("city", "no city called %s", city_path);
            return 1;
        }
        snprintf(city_path, sizeof city_path, "%s", resolved);
    }

    if (!assets_dir[0] && !find_assets(assets_dir, sizeof assets_dir, argv[0]))
    {
        R_ERR("assets", "not found; pass --assets DIR or set SC2K_ASSETS");
        return 1;
    }
    R_DBG("assets", "%s", assets_dir);
    if (r_atlas_load(&a.atlas, assets_dir) != 0)
    {
        R_ERR("atlas", "%s", a.atlas.err);
        return 1;
    }
    PHASE_MS(t_atlas);
    {
        int32_t tiles = 0;
        char    zooms[32];
        int     zn = 0;
        zooms[0]   = 0;
        for (i = 0; i < a.atlas.n_levels; i++)
        {
            tiles += a.atlas.level[i].n_tiles;
            zn += snprintf(zooms + zn, sizeof zooms - (size_t)zn, "%s%d", i ? "/" : "", (int)a.atlas.level[i].zoom);
        }
        R_NOTE("atlas", "%d levels (%s px), %d tiles, %d animated runs, %.0f ms", (int)a.atlas.n_levels, zooms, (int)tiles, (int)a.atlas.n_anim, NS_MS(t_atlas));
        for (i = 0; i < a.atlas.n_levels; i++)
            R_DBG("atlas", "%d px: %d tiles in %dx%d, tile %dx%d, level step %d", (int)a.atlas.level[i].zoom, (int)a.atlas.level[i].n_tiles, (int)a.atlas.level[i].w, (int)a.atlas.level[i].h, (int)a.atlas.level[i].tile_w, (int)a.atlas.level[i].tile_h, (int)a.atlas.level[i].alt_step);
    }
    a.city = (City *)calloc(1, sizeof *a.city);
    a.view = (RCity *)calloc(1, sizeof *a.view);
    if (!a.city || !a.view)
        return 1;
    if (city_path[0])
    {
        R_NOTE("city", "loading %s", city_path);
        if (!city_load(city_path, a.city))
        {
            R_ERR("city", "%s is not a city", city_path);
            return 1;
        }
        PHASE_MS(t_city);
        set_city_name(&a, city_path);
        log_city_loaded(a.city, city_path, NS_MS(t_city));
    }
    else
    {
        /*  Nothing asked for, so start on an empty map with the load
         *  menu up.  calloc has already made a blank city, which the
         *  view is happy to draw. */
        snprintf(a.city_base, sizeof a.city_base, "%s", "Untitled");
        snprintf(a.us.save_path, sizeof a.us.save_path, "Untitled.sc2");
        a.us.open_load = 1;
    }
    /*  The generator's seed is not saved ($11DC never reaches MISC), so
     *  a run starts from the clock like the original does. */
    rng_seed((int32_t)time(NULL), (uint16_t)(time(NULL) & 0xFFFF));

    t_phase = SDL_GetTicksNS();
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        R_ERR("sdl", "%s", SDL_GetError());
        return 1;
    }
    R_DBG("sdl", "video %s, driver %s", SDL_GetRevision(), SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "none");
    win = SDL_CreateWindow("SimCity 2000", ww, wh, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | ((check || run_frames || shot_out) ? SDL_WINDOW_HIDDEN : 0));
    if (!win)
    {
        R_ERR("sdl", "window: %s", SDL_GetError());
        return 1;
    }
    {
        int pw, ph;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        R_DBG("sdl", "window %dx%d, %dx%d px%s", ww, wh, pw, ph, (check || run_frames || shot_out) ? ", hidden" : "");
    }
    a.gpu = r_gpu_create(win, &a.atlas, err, sizeof err);
    if (!a.gpu)
    {
        R_ERR("gpu", "%s", err);
        return 1;
    }
    PHASE_MS(t_gpu);
    R_NOTE("gpu", "%s, %.0f ms", r_gpu_driver(a.gpu), NS_MS(t_gpu));
    if (!check && !run_frames)
    {
        a.ui = r_ui_create(win, r_gpu_device(a.gpu), shot_out ? r_gpu_offscreen_format(a.gpu) : r_gpu_swapchain_format(a.gpu), 1.0f, assets_dir);
        R_DBG("ui", "%s", a.ui ? "imgui" : "none");
        if (a.ui)
        {
            /*  --theme for this run beats the saved preference, which
             *  beats the default; a saved pack that has gone falls back
             *  to the default rather than to nothing. */
            char saved[64], ppath[1024];
            scan_themes(&a, assets_dir);
            R_DBG("theme", "%d packs in %s", a.us.n_themes, a.themes_dir);
            if (prefs_path(ppath, sizeof ppath))
                R_DBG("prefs", "%s", ppath);
            if (theme_dir)
                apply_theme_choice(&a, theme_dir, "--theme");
            else if (prefs_get("theme", saved, sizeof saved) && saved[0] && apply_theme_choice(&a, saved, "saved preference"))
                ;
            else
                apply_theme_choice(&a, DEFAULT_THEME, "default");
        }
    }
    if (city_path[0])
        r_ui_log(&a.us, "Loaded %s", a.city_base);
    if ((!check && !run_frames && !shot_out) || sound_test)
    {
        a.snd = r_sound_create(assets_dir);
        if (a.snd)
            R_DBG("sound", "%d effects", r_sound_loaded(a.snd));
        else
            R_WARN("sound", "no audio device");
        if (sound_test)
            r_sound_play(a.snd, sound_test);
    }

    if (pixel_scale < 1)
    {
        int pw, ph;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        pixel_scale = pw >= 2 * ww ? 2 : 1;
    }
    a.gv.scale = pixel_scale;
    a.t0_ns    = SDL_GetTicksNS();
    set_speed(&a, (int32_t)a.city->misc[MISC_SPEED]);
    a.last_speed = a.speed > 1 ? a.speed : 3;
    if (check && !run_frames)
        a.speed = 1;

    t_phase = SDL_GetTicksNS();
    if (resweep(&a) != 0)
    {
        R_ERR("sweep", "no %d px art set", (int)a.opts.zoom);
        return 1;
    }
    PHASE_MS(t_sweep);
    R_DBG("sweep", "canvas %dx%d at %d px, %.0f ms", (int)a.sw.w, (int)a.sw.h, (int)a.opts.zoom, NS_MS(t_sweep));
    {
        int pw, ph;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        a.gv.scroll_x = a.sw.w / 2 - (pw / a.gv.scale) / 2;
        a.gv.scroll_y = a.sw.h / 2 - (ph / a.gv.scale) / 2;
        /*  --centre puts a MAP TILE in the middle of the window, which is
         *  how a person describes a view; --scroll takes canvas pixels,
         *  which is how the renderer stores one.  The sweep has already
         *  run by here, so it knows where the tile landed and no caller
         *  has to work the isometric projection out for itself. */
        if (have_centre)
        {
            int32_t fx, fy;
            a.opts.focus_col = centre_col;
            a.opts.focus_row = centre_row;
            a.dirty          = 1;
            if (resweep(&a) == 0 && r_soft_focus_result(&fx, &fy))
            {
                a.gv.scroll_x = fx - (pw / a.gv.scale) / 2;
                a.gv.scroll_y = fy - (ph / a.gv.scale) / 2;
            }
            else
                R_WARN("view", "tile %d,%d is off the canvas; "
                               "centring on the map instead",
                       centre_col,
                       centre_row);
        }
        if (have_scroll)
        {
            a.gv.scroll_x = scroll_x;
            a.gv.scroll_y = scroll_y;
        }
    }
    a.mesh_dirty = 1;
    a.zoom_world = (float)a.opts.zoom / 32.0f;
    a.gv.zoom    = 1.0f;
    if (zoomf > 0.0f)
    {
        int pw, ph;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        zoom_to(&a, zoomf, (float)pw * 0.5f, (float)ph * 0.5f);
        if (a.dirty && resweep(&a) != 0)
            return 1;
    }
    if (a.angle != 0.0f)
    {
        /* --angle: the view --scroll and the zoom give, turned about its centre */
        float ang = a.angle;
        a.angle   = 0.0f;
        pivot_on_view(&a, win);
        a.angle = ang;
    }

    R_NOTE("init", "ready in %.0f ms (atlas %.0f, city %.0f, gpu %.0f, sweep %.0f)", NS_MS(SDL_GetTicksNS() - t_start), NS_MS(t_atlas), NS_MS(t_city), NS_MS(t_gpu), NS_MS(t_sweep));
#undef PHASE_MS

    if (run_frames > 0)
    {
        /*  Headless: run the loop for a number of frames at a speed and
         *  report how far the clock got, so the schedule can be checked
         *  without watching the window. */
        int32_t  date0 = a.city->date;
        uint64_t t0    = SDL_GetTicksNS();
        int      f;
        if (run_speed)
            set_speed(&a, run_speed);
        for (f = 0; f < run_frames; ++f)
        {
            step_clock(&a);
            if (a.dirty && resweep(&a) != 0)
                break;
            a.gv.time = (float)((double)(SDL_GetTicksNS() - a.t0_ns) / 1e9);
            traffic_frame(&a, a.gv.time);
            r_gpu_frame(a.gpu, &a.gv, a.sky, NULL, NULL);
        }
        printf("run       %d frames at speed %d in %.2f s: date %d -> %d "
               "(%d phases), funds %d, population %d, palette steps %d/%d\n",
               run_frames,
               (int)a.speed,
               (double)(SDL_GetTicksNS() - t0) / 1e9,
               (int)date0,
               (int)a.city->date,
               (int)(a.city->date - date0),
               (int)a.city->funds,
               (int)a.city->population,
               (int)a.anim_a,
               (int)a.anim_b);
        a.dirty = 1;
        if (resweep(&a) != 0)
            fprintf(stderr, "sweep failed\n");
    }
    if (have_pick)
    {
        char what[160] = "";
        int  rc        = 0;
        if (remesh(&a) != 0)
            fprintf(stderr, "mesh build failed\n");
        pick_tile(&a, pick_x, pick_y, win);
        if (a.q_col >= 0)
            r_mesh_query(a.view, a.q_col, a.q_row, what, sizeof what);
        a.win_density = SDL_GetWindowPixelDensity(win);
        ui_fill(&a);
        printf("pick      window %g,%g: column %d row %d  %s\n", (double)pick_x, (double)pick_y, (int)a.q_col, (int)a.q_row, what);
        if (a.us.q_ok)
            printf("pick      footprint %d x %d, north-east tile column %d row %d; "
                   "outline %.0f,%.0f %.0f,%.0f %.0f,%.0f %.0f,%.0f\n",
                   (int)a.us.q_size,
                   (int)a.us.q_size,
                   (int)a.us.q_ocol,
                   (int)a.us.q_orow,
                   (double)a.us.q_poly[0][0],
                   (double)a.us.q_poly[0][1],
                   (double)a.us.q_poly[1][0],
                   (double)a.us.q_poly[1][1],
                   (double)a.us.q_poly[2][0],
                   (double)a.us.q_poly[2][1],
                   (double)a.us.q_poly[3][0],
                   (double)a.us.q_poly[3][1]);
        if (shot_out && a.ui)
        {
            /* --pick with --shot: the frame with the query window and the outline */
            a.us.show_query = 1;
            rc              = shot_frame(&a, win, shot_out);
        }
        r_ui_destroy(a.ui);
        r_gpu_destroy(a.gpu);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return rc ? 1 : 0;
    }
    if (mesh_check)
    {
        int bad;
        /*  The turned build cuts all four edges: a closed surface.  With
         *  SC2K_CHECK_OPEN set the plain build is checked instead, whose
         *  two uncut edges are open by design: the checker's own test. */
        a.angle = getenv("SC2K_CHECK_OPEN") ? 0.0f : 1.0f;
        if (remesh(&a) != 0)
            fprintf(stderr, "mesh build failed\n");
        bad = r_mesh_check(&a.mesh, 1);
        if (a.gv.roads3d)
        {
            int cut = r_mesh_check_roads(&a.mesh, 1);
            if (cut != 0)
                bad = 1;
        }
        r_gpu_destroy(a.gpu);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return bad != 0;
    }
    if (shot_out)
    {
        int rc;
        if (remesh(&a) != 0)
            fprintf(stderr, "mesh build failed\n");
        rc = shot_frame(&a, win, shot_out);
        r_ui_destroy(a.ui);
        r_gpu_destroy(a.gpu);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return rc ? 1 : 0;
    }
    if (check)
    {
        int rc;
        if (remesh(&a) != 0)
            fprintf(stderr, "mesh build failed\n");
        rc = check_frame(&a, win, check_out);
        r_gpu_destroy(a.gpu);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return rc ? 1 : 0;
    }

    if (run_frames > 0)
        a.quit = 1;

    while (!a.quit)
    {
        SDL_Event e;
        int       pw, ph;
        while (SDL_PollEvent(&e))
        {
            int ui_owns = r_ui_event(a.ui, &e);
            if (e.type == SDL_EVENT_QUIT)
                a.quit = 1;
            else if (ui_owns)
                continue;
            else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     e.button.button == SDL_BUTTON_LEFT)
            {
                a.drag     = 1;
                a.drag_len = 0.0f;
            }
            else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                     e.button.button == SDL_BUTTON_LEFT)
            {
                a.drag = 0;
                /*  A click, not a drag: the selected tool acts on the
                 *  tile under the pointer. */
                if (a.drag_len < 4.0f)
                {
                    pick_tile(&a, e.button.x, e.button.y, win);
                    if (a.q_col >= 0)
                        use_tool(&a, win);
                }
            }
            else if (e.type == SDL_EVENT_MOUSE_MOTION)
            {
                if (a.drag && (e.motion.state & SDL_BUTTON_LMASK))
                {
                    float dens = SDL_GetWindowPixelDensity(win);
                    int   dx, dy;
                    a.drag_ax += e.motion.xrel * dens / (float)a.gv.scale;
                    a.drag_ay += e.motion.yrel * dens / (float)a.gv.scale;
                    a.drag_len += fabsf(e.motion.xrel) + fabsf(e.motion.yrel);
                    dx = (int)a.drag_ax;
                    dy = (int)a.drag_ay;
                    a.drag_ax -= (float)dx;
                    a.drag_ay -= (float)dy;
                    a.gv.scroll_x -= dx;
                    a.gv.scroll_y -= dy;
                }
                pick_tile(&a, e.motion.x, e.motion.y, win);
            }
            else if (e.type == SDL_EVENT_MOUSE_WHEEL)
            {
                float dens = SDL_GetWindowPixelDensity(win);
                float step = powf(1.12f, e.wheel.y);
                zoom_to(&a, a.zoom_world * step, e.wheel.mouse_x * dens, e.wheel.mouse_y * dens);
            }
            else if (e.type == SDL_EVENT_KEY_DOWN)
            {
                SDL_Keycode k     = e.key.key;
                int         shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
                int         mod   = (e.key.mod & KMOD_CMD) != 0;
                SDL_GetWindowSizeInPixels(win, &pw, &ph);
                if (k == SDLK_ESCAPE)
                    a.quit = 1;
                else if (k >= SDLK_1 && k <= SDLK_5)
                    set_speed(&a, (int32_t)(k - SDLK_0));
                else if (k == SDLK_SPACE)
                    set_speed(&a, a.speed > 1 ? 1 : a.last_speed);
                else if (k == SDLK_EQUALS || k == SDLK_PLUS ||
                         k == SDLK_KP_PLUS)
                    zoom_to(&a, a.zoom_world * 2.0f, (float)pw * 0.5f, (float)ph * 0.5f);
                else if (k == SDLK_MINUS || k == SDLK_KP_MINUS)
                    zoom_to(&a, a.zoom_world * 0.5f, (float)pw * 0.5f, (float)ph * 0.5f);
                else if (k == SDLK_LEFTBRACKET && a.gv.scale > 1)
                    a.gv.scale--;
                else if (k == SDLK_RIGHTBRACKET && a.gv.scale < 4)
                    a.gv.scale++;
                /*  the original's own shortcuts, its letters: File and Windows */
                else if (mod && !shift && k == SDLK_L)
                    a.us.open_load = 1;
                else if (mod && !shift && k == SDLK_S)
                    a.us.want_save = 1;
                else if (mod && !shift && k == SDLK_Q)
                    a.quit = 1;
                else if (mod && !shift && k == SDLK_B)
                    a.us.show_budget = 1;
                else if (mod && !shift && k == SDLK_C)
                    a.us.show_city = 1;
                else if (mod && !shift && k == SDLK_G)
                    a.us.show_graphs = 1;
                else if (mod && !shift && k == SDLK_M)
                    r_ui_log(&a.us, "Map window: not yet ported");
                else if (mod && shift && k == SDLK_T)
                    a.gv.terrain3d = !a.gv.terrain3d;
                else if (mod && shift && k == SDLK_Y)
                    a.gv.water3d = !a.gv.water3d;
                else if (mod && shift && k == SDLK_R)
                {
                    a.gv.roads3d = !a.gv.roads3d;
                    a.mesh_dirty = 1;
                }
                else if (mod && shift && k == SDLK_G)
                    a.gv.grid = !a.gv.grid;
                else if (mod && shift && k == SDLK_M)
                    a.gv.plain_sweep = !a.gv.plain_sweep;
                else if (mod && shift && k == SDLK_U)
                {
                    a.opts.underground = !a.opts.underground;
                    a.dirty            = 1;
                    a.mesh_dirty       = 1;
                }
                else if (mod && k == SDLK_V)
                {
                    a.opts.view = shift ? (a.opts.view + 11) % 12
                                        : (a.opts.view + 1) % 12;
                    a.dirty     = 1;
                }
                else if (mod && shift && k == SDLK_P)
                    a.us.want_screenshot = 1;
                else if (k == SDLK_COMMA)
                    rotate_by(&a, -15.0f, win);
                else if (k == SDLK_PERIOD)
                    rotate_by(&a, 15.0f, win);
                else if (k == SDLK_0)
                    rotate_by(&a, -a.angle, win);
            }
        }
        if (!r_ui_wants_keyboard(a.ui))
        {
            const bool *ks  = SDL_GetKeyboardState(NULL);
            int32_t     pan = 24;
            if (ks[SDL_SCANCODE_LEFT])
                a.gv.scroll_x -= pan;
            if (ks[SDL_SCANCODE_RIGHT])
                a.gv.scroll_x += pan;
            if (ks[SDL_SCANCODE_UP])
                a.gv.scroll_y -= pan;
            if (ks[SDL_SCANCODE_DOWN])
                a.gv.scroll_y += pan;
        }

        step_clock(&a);
        if (a.dirty && resweep(&a) != 0)
            break;
        if (a.mesh_dirty && remesh(&a) != 0)
            fprintf(stderr, "mesh build failed\n");
        a.gv.time = (float)((double)(SDL_GetTicksNS() - a.t0_ns) / 1e9);
        if (traffic_frame(&a, a.gv.time) != 0)
            fprintf(stderr, "traffic build failed\n");
        {
            uint64_t now = SDL_GetTicksNS();
            if (a.last_ns)
            {
                float ms   = (float)((double)(now - a.last_ns) / 1e6);
                a.frame_ms = a.frame_ms > 0.0f ? a.frame_ms * 0.9f + ms * 0.1f : ms;
                a.fps      = a.frame_ms > 0.0f ? 1000.0f / a.frame_ms : 0.0f;
            }
            a.last_ns = now;
        }
        if (a.ui)
        {
            a.win_density = SDL_GetWindowPixelDensity(win);
            ui_fill(&a);
            r_ui_frame(a.ui, &a.us);
        }
        {
            /*  The underground view is the original's: the ground at its
             *  own altitude, the seabed under water, in the underground
             *  art, with the pipes and subways on it.  The surface mesh
             *  and the water are not part of it. */
            RGpuView fv = a.gv;
            if (a.opts.underground)
            {
                fv.water3d     = 0;
                fv.underground = 1;
            }
            if (r_gpu_frame(a.gpu, &fv, backdrop(&a), a.ui ? r_ui_render : NULL, a.ui) != 0)
                fprintf(stderr, "frame: %s\n", SDL_GetError());
        }
        if (a.ui)
        {
            SDL_GetWindowSizeInPixels(win, &pw, &ph);
            ui_apply(&a, win, pw, ph);
        }
        title(&a, win);
    }

    r_mesh_free(&a.mesh);
    r_ops_free(&a.ops);
    r_ui_destroy(a.ui);
    r_sound_destroy(a.snd);
    r_gpu_destroy(a.gpu);
    SDL_DestroyWindow(win);
    SDL_Quit();
    city_free(a.city);
    free(a.city);
    free(a.view);
    r_atlas_free(&a.atlas);
    return 0;
}
