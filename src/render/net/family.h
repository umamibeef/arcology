/*  net/family.h: what family.c answers for.
 *
 *  the families, as the scripts declare them.
 *
 *  Nothing declared here decides anything. */
#ifndef ARC_NET_FAMILY_H
#define ARC_NET_FAMILY_H

#include "net/types.h"

/*  Declare one, replacing any of the same name.  Answers 0, or -1 with
 *  the reason logged.  A name no knob, loft kind, tile family or stage
 *  answers to is a fault the script has to hear about. */
int  net_family_define(const NetFamilyDecl *d);
/*  The same reading, declaring nothing: what the LINT does.  `rule`
 *  takes 1 at each stage the declaration answered with a rule rather
 *  than a primitive.  So the lint knows which rule names a family
 *  invented and can stop calling them unknown. */
int  net_family_check(const NetFamilyDecl *d, int *rule);
void net_family_reset(void); /* before a reading of the scripts: what stands is what this reading declares */
int  net_family_count(void);
const NetFamily *net_family_at(int i);
/*  Whether a family supplies a stage at all, and the stages themselves.
 *  A stage a script answers is called through the same door as one the C
 *  answers. */
int  net_family_has(const NetFamily *fam, NetHook h);
void net_family_control_ask(const NetFamily *fam, const RCity *c, int32_t col, int32_t row, int links);
const char *net_family_stage_rule(const NetFamily *fam, NetHook h);
int  net_family_stage_primitive(const NetFamily *fam, NetHook h);
int  net_family_record_done(const NetFamily *fam, Loft *x);
int  net_family_furniture_done(const NetFamily *fam, Loft *x);
int  net_family_profile_done(const NetFamily *fam, Loft *x);
const char *net_family_stage_rule_after(const NetFamily *fam, NetHook h);
const char *net_family_props(const NetFamily *fam);
int  net_family_stations(const NetFamily *fam);
int  net_family_laps(const NetFamily *fam);
int  net_family_paved(const NetFamily *fam);
int  net_family_threads(const NetFamily *fam);
int  net_family_graphed(const NetFamily *fam);
int  net_family_record(const NetFamily *fam, Loft *x);
/*  The height a strip stands clear of the ground past, the family's own
 *  answer, settled before anything is graded (net_flies_run_*). */
float net_family_flies_over(const NetFamily *fam, const RLoft *d);
void  net_flies_runs_reset(void);
int   net_flies_runs(void);
const char *net_flies_run_at(int i, int *structure);
void net_flies_run_is(int i, float over);
void net_family_taper(const NetFamily *fam, Loft *x);
int  net_family_profile(const NetFamily *fam, Loft *x);
int  net_family_works(const NetFamily *fam, Loft *x);
void net_family_traffic(const NetFamily *fam, const RLoft *d, int cls, float *lane_in, float *lane_out);
void        net_traffic_runs_reset(void);
int         net_traffic_runs(void);
const char *net_traffic_run_at(int i, int *cls);
void        net_traffic_run_is(int i, float in, float out);
int  net_family_furniture(const NetFamily *fam, Loft *x);
void net_hook_add(NetHook h, const char *name, NetHookFn fn);
void net_hook_add_split2(NetHook h, const char *name, NetHookFn fn, NetHookFn after, const char *ask, const char *ask_after);
extern const NetFamily *net_walked[NET_FAM_MAX]; /* the families the walk visits, in the order their declarations asked for */
extern int              net_n_walked;
const NetFamily        *net_family(Family f); /* by tile family (net/family.c) */

#endif /* ARC_NET_FAMILY_H */
