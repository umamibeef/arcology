/*  dump.c: the developer dumps' one sink.  See dump.h. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dump.h"

static FILE         *s_sink;
static char         *s_cap;
static unsigned long s_cap_max, s_cap_n;

void dump_capture(char *buf, unsigned long cap)
{
    s_cap     = cap > 1 ? buf : NULL;
    s_cap_max = cap;
    s_cap_n   = 0;
    if (s_cap)
        s_cap[0] = 0;
}

void dump_open(const char *path)
{
    dump_close();
    if (!path || strcmp(path, "-") == 0)
        return;
    s_sink = fopen(path, "w");
    if (!s_sink)
        fprintf(stderr, "arcology: cannot write the dump file %s; dumping to stdout\n", path);
    else
        atexit(dump_close);
}

void dumpf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (s_cap)
    {
        int n = vsnprintf(s_cap + s_cap_n, s_cap_max - s_cap_n, fmt, ap);
        if (n > 0)
            s_cap_n += (unsigned long)n;
        if (s_cap_n >= s_cap_max)
            s_cap_n = s_cap_max - 1;
    }
    else
        vfprintf(s_sink ? s_sink : stdout, fmt, ap);
    va_end(ap);
}

void dump_close(void)
{
    if (s_sink)
        fclose(s_sink);
    s_sink = NULL;
}
