/*  geo.h: the numbers a script tunes the world with.
 *
 *  Two stores, and neither is any one directory's.  A KNOB is declared.
 *  It has a range, a default and a name the tuning window shows.  It
 *  keeps a float at an address that never moves.  A NUMBER is free: it
 *  exists from the moment a script sets it.  The scripts fill both.
 *
 *  Both are read by all three halves of the renderer, which is why they
 *  sit here rather than with any of them.  A primitive wants the step it
 *  samples at.  A store wants a width.  A stage wants a reserve.  All
 *  three ask this store, and they ask it the same way. */
#ifndef ARC_GEO_H
#define ARC_GEO_H

/*  ONE KNOB.  The name a script and the C set it by.  What the tuning
 *  window calls it, and the group it is drawn with.  The range it is
 *  held to, and the value it starts at.
 *
 *  A script declares every one of them (scripts/tune.lua).  So the range
 *  and the slider of a new number are a script's.  C holds the store and
 *  the names it reads itself.  A knob with no label gets no slider: it
 *  is a view or a switch some other control owns. */
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
float        geo_num(int *cache, const char *name);
int          geo_set(const char *name, float v);
const char  *geo_name(int i, float *v);
/*  One number by name, for a caller that keeps no place: answers
 *  whether the store holds it. */
int          geo_find(const char *name, float *v);

/*  The knobs.  A declaration that names a knob the store already holds
 *  keeps its value and moves its range.  So a reading of the scripts
 *  while the city runs does not throw a tuning session away. */
int          tune_declare(const RKnob *k);
int          tune_count(void);
int          tune_knob(int i, RKnob *out);
int          tune_set(const char *name, float v);
int          tune_set_at(int i, float v);
int          tune_find(const char *name, float *v);
/*  A family holds pointers at its width and its radii, so every use
 *  reads the live value. */
const float *tune_at(const char *name);
const char  *tune_name(int i, float *v);
/*  Every value, in the store's own order: what the build puts in its
 *  key.  Answers how many it wrote. */
int          tune_values(float *out, int max);

/*  THE KNOBS THE RENDERER READS ITSELF.  A read site says TUNE(margin).
 *  Each pointer is settled once, by tune_bind, after the scripts are
 *  read, and a knob's float never moves again.  So a read in a per-tile
 *  loop is a load.
 *
 *  This is the whole list of knobs C looks at.  Everything else a knob
 *  does, it does through a family's declaration or through a script. */
typedef struct
{
    const float *line_w, *thread_w, *line_rmin, *thread_rmin;
    const float *approach, *margin, *trim_cap, *show_curves;
    const float *band_w, *band_rmin, *band_rmax, *band_reach, *band_stair;
    const float *band_grade, *band_stiff, *corner_share, *spur_merge;
} RTuned;

extern RTuned s_tuned;

#define TUNE(k) (*s_tuned.k)

/*  Bind the list above to the store.  Answers whether the scripts
 *  declared every one.  A name they did not is a fault: the renderer
 *  would draw with a nought where a width belongs. */
int          tune_bind(void);

#endif /* ARC_GEO_H */
