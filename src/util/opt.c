/*  opt.c -- see opt.h. */
#include "opt.h"
#include <stddef.h>
#include <string.h>

static int          s_argc;
static char *const *s_argv;

void opt_init(int argc, char **argv)
{
    s_argc = argc;
    s_argv = argv;
}

/*  --name, --name=value, or --name value.
 *
 *  Scanned from the END so a repeated switch takes its last value, which
 *  is what assigning the same environment variable twice used to do.
 *
 *  A following argument counts as the value unless it opens with "--",
 *  which is a switch; a single dash is not, so a negative number reads as
 *  a value (--traffic-t -1) instead of being silently dropped. */
static const char *find(const char *name, int *have)
{
    size_t n = strlen(name);
    int    i;
    *have = 0;
    for (i = s_argc - 1; i >= 1; --i)
    {
        const char *a = s_argv[i];
        if (!a || a[0] != '-' || a[1] != '-')
            continue;
        if (strncmp(a + 2, name, n) != 0)
            continue;
        if (a[2 + n] == '=')
        {
            *have = 1;
            return a + 2 + n + 1;
        }
        if (a[2 + n] == '\0')
        {
            *have = 1;
            if (i + 1 < s_argc && s_argv[i + 1] &&
                !(s_argv[i + 1][0] == '-' && s_argv[i + 1][1] == '-'))
                return s_argv[i + 1];
            return NULL;
        }
    }
    return NULL;
}

int opt_set(const char *name)
{
    int have;
    find(name, &have);
    return have;
}

const char *opt_get(const char *name)
{
    int have;
    return find(name, &have);
}
