/*  json.h: reading a JSON document.  jsmn gives a flat array of tokens,
 *  and these walk it.  A token is named by its index, and -1 is no token. */
#ifndef ARC_JSON_H
#define ARC_JSON_H

#include <stddef.h>

#define JSMN_HEADER
#include "jsmn.h"

/*  The tokens of a whole document, which the caller frees, and their count
 *  in `n`.  NULL where the document does not parse. */
jsmntok_t *json_parse(const char *js, size_t len, int *n);
/*  The index of the first token after the value at `i`. */
int json_skip(const jsmntok_t *t, int i);
/*  The value of `key` in the object at `obj`, or -1. */
int json_get(const char *js, const jsmntok_t *t, int obj, const char *key);
/*  The number at `i`, or `dflt` where `i` is -1. */
long   json_long(const char *js, const jsmntok_t *t, int i, long dflt);
double json_dbl(const char *js, const jsmntok_t *t, int i, double dflt);
/*  The string at `i` into `out`, cut to fit.  Empty where `i` is -1. */
void json_str(const char *js, const jsmntok_t *t, int i, char *out, size_t n);

#endif /* ARC_JSON_H */
