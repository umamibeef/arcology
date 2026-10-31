/*  geo.h: the numbers the world is built and tuned with.
 *
 *  Two stores, and neither is any one directory's.  A KNOB is declared.
 *  It has a range, a default and a name the tuning window shows.  It
 *  keeps a float at an address that never moves.  A NUMBER is free: it
 *  exists from the moment something sets it.  geo_defaults fills both.
 *
 *  Both are read by all three halves of the renderer, which is why they
 *  sit here rather than with any of them.  A primitive wants the step it
 *  samples at.  A store wants a width.  A stage wants a reserve.  All
 *  three ask this store, and they ask it the same way. */
#ifndef ARC_GEO_H
#define ARC_GEO_H

/*  ONE KNOB.  The name the C sets it by.  What the tuning window calls
 *  it, and the group it is drawn with.  The range it is held to, and the
 *  value it starts at.
 *
 *  geo_defaults declares every one of them, so a knob is added in one
 *  place.  A knob with no label gets no slider: it is a view or a switch
 *  some other control owns. */
typedef struct
{
    const char *name;
    const char *label;
    const char *group;
    float       v, lo, hi, def;
} RKnob;

/*  The most knobs the store keeps.  A build's key and the tuning window
 *  both hold one slot each, so the ceiling is declared here. */
#define ARC_TUNE_MAX 32

/*  A number by name, remembering where it was found.  The cache is the
 *  reader's own and starts at -1.  A name nothing has set reads as
 *  nought. */
float geo_num(int *cache, const char *name);
/*  One number by name, for a caller that keeps no place: answers
 *  whether the store holds it. */
int geo_find(const char *name, float *v);
/*  Set the number `name` to `v`, while the program runs.  A reader of it
 *  sees the new value from its next reading.  0 where the store is full. */
int geo_put(const char *name, float v);

int tune_count(void);
int tune_knob(int i, RKnob *out);
int tune_set(const char *name, float v);
int tune_set_at(int i, float v);
int tune_find(const char *name, float *v);
/*  Every value, in the store's own order: what the build puts in its
 *  key.  Answers how many it wrote. */
int tune_values(float *out, int max);

/*  THE KNOBS THE RENDERER READS ITSELF.  A read site says TUNE(corner_share).
 *  Each pointer is settled once, by tune_bind, after the knobs are
 *  declared, and a knob's float never moves again.  So a read in a
 *  per-tile loop is a load.
 *
 *  This is the whole list of knobs the C reads through a pointer. */
typedef struct
{
    const float *show_curves, *corner_share;
} RTuned;

extern RTuned s_tuned;

#define TUNE(k) (*s_tuned.k)

/*  Every number and knob, set to its value as the program starts. */
void geo_defaults(void);

#endif /* ARC_GEO_H */
