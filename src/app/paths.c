/*  paths.c -- finding the art, the cities, and one named city.
 *
 *  None of it is configuration: the game looks beside its binary, then up
 *  towards the repository root, then in the working directory, so a fresh
 *  clone runs with no arguments at all.
 */
#include <SDL3/SDL.h>

#include "app_int.h"
#include "log.h"
#include "opt.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*  an assets directory is one with the tile atlas in it */
int looks_like_assets(const char *p)
{
    char probe[1024];
    snprintf(probe, sizeof probe, "%s/atlas.json", p);
    return is_file(probe);
}

/*  Beside the binary, then up towards the repo root, then the working
 *  directory.  An explicit --assets never reaches here: the option parser
 *  fills the path itself and this only runs when none was given, so
 *  reading --assets again here was a second parser for one argument. */
int find_assets(char *out, size_t n, const char *argv0)
{
    char        base[1024], probe[1024];
    const char *slash;
    int         up;

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

/*  $--cities, then the usual install, then ./Cities */
/*  Where the cities are, most specific first.  `cities/` beside the
 *  build is the answer that needs no setup: the repository carries the
 *  collection, so a fresh clone can open one straight away.  The
 *  environment variable still wins, for a folder of your own. */
int find_cities(char *out, size_t n)
{
    static const char *const REL[] = {"cities", "../cities", "../../cities", "Cities", NULL};
    const char              *env   = opt_get("cities");
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
int scan_cities(RUiState *s)
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
int resolve_city(char *out, size_t n, const char *arg, const char *dir)
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
