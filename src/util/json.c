/*  json.c: see json.h.  jsmn itself is compiled here, once. */
/*  jsmn whole, before json.h asks for its declarations alone. */
#include "jsmn.h"

#include "json.h"

#include <stdlib.h>
#include <string.h>

jsmntok_t *json_parse(const char *js, size_t len, int *n_out)
{
    jsmn_parser p;
    jsmntok_t  *toks;
    int         n;
    /*  Two passes: jsmn counts the tokens when handed no array. */
    jsmn_init(&p);
    n = jsmn_parse(&p, js, len, NULL, 0);
    if (n <= 0)
        return NULL;
    toks = (jsmntok_t *)malloc(sizeof(jsmntok_t) * (size_t)n);
    if (!toks)
        return NULL;
    jsmn_init(&p);
    if (jsmn_parse(&p, js, len, toks, (unsigned int)n) != n)
    {
        free(toks);
        return NULL;
    }
    *n_out = n;
    return toks;
}

/*  Whether token `t` is the string `s`. */
static int json_eq(const char *js, const jsmntok_t *t, const char *s)
{
    size_t n = (size_t)(t->end - t->start);
    return t->type == JSMN_STRING && strlen(s) == n && memcmp(js + t->start, s, n) == 0;
}

int json_skip(const jsmntok_t *t, int i)
{
    int k, n = t[i].size;
    if (t[i].type == JSMN_OBJECT)
    {
        i++;
        for (k = 0; k < n; k++)
            i = json_skip(t, i + 1); /* the key, then its value */
        return i;
    }
    if (t[i].type == JSMN_ARRAY)
    {
        i++;
        for (k = 0; k < n; k++)
            i = json_skip(t, i);
        return i;
    }
    return i + 1;
}

int json_get(const char *js, const jsmntok_t *t, int obj, const char *key)
{
    int i = obj + 1, k;
    if (obj < 0 || t[obj].type != JSMN_OBJECT)
        return -1;
    for (k = 0; k < t[obj].size; k++)
    {
        if (json_eq(js, &t[i], key))
            return i + 1;
        i = json_skip(t, i + 1);
    }
    return -1;
}

/*  The text of token `i`, cut to `out`. */
static void text(const char *js, const jsmntok_t *t, int i, char *out, size_t cap)
{
    size_t n = (size_t)(t[i].end - t[i].start);
    if (n >= cap)
        n = cap - 1;
    memcpy(out, js + t[i].start, n);
    out[n] = 0;
}

long json_long(const char *js, const jsmntok_t *t, int i, long dflt)
{
    char buf[32];
    if (i < 0)
        return dflt;
    text(js, t, i, buf, sizeof buf);
    return strtol(buf, NULL, 10);
}

double json_dbl(const char *js, const jsmntok_t *t, int i, double dflt)
{
    char buf[64];
    if (i < 0)
        return dflt;
    text(js, t, i, buf, sizeof buf);
    return strtod(buf, NULL);
}

void json_str(const char *js, const jsmntok_t *t, int i, char *out, size_t n)
{
    if (i < 0)
    {
        out[0] = 0;
        return;
    }
    text(js, t, i, out, n);
}
