/*  prefs.c: what the game remembers between runs, and the themes it
 *  remembers a choice from.  It is settings.json, in the per-user place
 *  SDL knows for the platform: Application Support on macOS, AppData on
 *  Windows, ~/.local/share on Linux.  The file is one flat object of
 *  strings and numbers, read whole and written whole, by hand rather than
 *  with a library.  A missing file is no preference.  A nested value is
 *  left alone.  A quote or a backslash in a value is escaped on the way
 *  out and not unescaped on the way in. */
#include <SDL3/SDL.h>

#include "internal.h"
#include "log.h"
#include "opt.h"
#include <dirent.h>
#include <sys/stat.h>

#define PREFS_MAX 32

#define JSMN_STATIC
#include "jsmn.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char key[64];
    char val[192];
    int  is_str; /* written quoted.  A number or true/false goes bare */
} Pref;

int prefs_path(char *out, size_t n)
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

int prefs_get(const char *key, char *out, size_t n)
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

int prefs_set(const char *key, const char *value)
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

/*  THE SWITCHES REMEMBERED between runs: a view the player turns on stays
 *  on.  Each is a field of the interface's state, saved as "on" or "off"
 *  under its name.  A knob is a switch that --outline or --tune can name
 *  for one run instead. */
static const struct
{
    const char *name;
    size_t      field;
    int         knob;
} SWITCH[PREFS_SWITCHES] = {
    {"grid",          offsetof(RUiState, grid),        0},
    {"curves",        offsetof(RUiState, outline),     1},
    {"cells",         offsetof(RUiState, show_cells),  0},
    {"triangles",     offsetof(RUiState, tris),        0},
    {"no_depth",      offsetof(RUiState, plain_sweep), 0},
    {"fps",           offsetof(RUiState, show_fps),    0},
    {"free_rotation", offsetof(RUiState, free_turn),   0},
    {"no_clouds",     offsetof(RUiState, no_clouds),   0},
    {"no_air",        offsetof(RUiState, no_air),      0},
};

static int *switch_field(RUiState *s, int k)
{
    return (int *)(void *)((char *)s + SWITCH[k].field);
}

void prefs_switches_load(RUiState *s, int knobs)
{
    int  k;
    char hv[32];
    s->biome = -1;
    if (prefs_get("biome", hv, sizeof hv) && strcmp(hv, "city") != 0)
    {
        for (k = 0; k < s->n_biomes; ++k)
            if (strcmp(hv, s->biome_names[k]) == 0)
                s->biome = k;
    }
    if (prefs_get("hour", hv, sizeof hv))
    {
        float h = strtof(hv, NULL);
        if (h >= 0.0f && h <= 24.0f)
            s->hour = h;
    }
    for (k = 0; k < PREFS_SWITCHES; ++k)
    {
        char v[16];
        if ((knobs || !SWITCH[k].knob) && prefs_get(SWITCH[k].name, v, sizeof v) && strcmp(v, "on") == 0)
            *switch_field(s, k) = 1;
    }
}

void prefs_switches_seen(App *a)
{
    int k;
    a->hour_saved  = a->us.hour;
    a->biome_saved = a->us.biome;
    for (k = 0; k < PREFS_SWITCHES; ++k)
        a->remembered[k] = *switch_field(&a->us, k) != 0;
}

void prefs_switches_save(App *a)
{
    int k;
    if (a->us.biome != a->biome_saved)
    {
        if (a->prefs_ok)
            prefs_set("biome", a->us.biome >= 0 ? a->us.biome_names[a->us.biome] : "city");
        a->biome_saved = a->us.biome;
    }
    /*  The time of day, kept as a number.  A drag changes it every frame,
     *  so it is written at most once a second. */
    if (a->us.hour != a->hour_saved && SDL_GetTicks() - a->hour_saved_at >= 1000u)
    {
        char hv[32];
        snprintf(hv, sizeof hv, "%.3f", (double)a->us.hour);
        if (a->prefs_ok)
            prefs_set("hour", hv);
        a->hour_saved    = a->us.hour;
        a->hour_saved_at = SDL_GetTicks();
    }
    for (k = 0; k < PREFS_SWITCHES; ++k)
    {
        int now = *switch_field(&a->us, k) != 0;
        if (now == a->remembered[k])
            continue;
        if (a->prefs_ok)
            prefs_set(SWITCH[k].name, now ? "on" : "off");
        a->remembered[k] = now;
    }
}

static int name_cmp(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

/*  Every pack under <assets>/themes, a directory with a theme.txt, by
 *  name, sorted, for the menu. */
void scan_themes(App *a, const char *assets_dir)
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
 *  the hand-drawn look.  `why` is for the log: default, saved
 *  preference, --theme, chosen.  A default pack that is not there is not
 *  a warning.  Assets/ is built from the game's own files and a fresh
 *  checkout has no schemes yet. */
int apply_theme_choice(App *a, const char *name, const char *why)
{
    char        path[1200];
    const char *base;
    size_t      len;
    if (!a->ui)
        return 0;
    if (strcmp(name, "none") == 0)
    {
        ui_clear_theme(a->ui);
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
    if (ui_set_theme(a->ui, path) != 0)
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
