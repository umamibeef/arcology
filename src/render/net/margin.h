/*  net/margin.h: what margin.c answers for.
 *
 *  the margin bands, gathered and offered.
 *
 *  Nothing declared here decides anything. */
#ifndef ARC_NET_MARGIN_H
#define ARC_NET_MARGIN_H

#include "net/types.h"

void                margin_enable(int on);
int                 margin_on(void);
/*  What is true of every strip and junction of the family.
 *
 *      The margin's share of the band.
 *      The junction box.
 *      A family's threads and a level meet's approach.
 *
 *  The script's answer, kept for a build. */
const ScriptFamily *net_family_rules(Family f);
/*  The knobs of the family the scripts named `line`: the lane model's,
 *  the spur's and the margin's reach are filed there. */
const ScriptFamily *net_line_rules(void);
void                margin_reset(const RCity *c);
int                 margin_add(int kind, V2 a, V2 b, V2 oa, V2 ob); /* oa, ob: the way out past each end, or zero */
void                margin_stats_print(void);
/*  The margins, from the network once it is complete.  The PASS is the
 *  script's (scripts/compose/world.lua): these gather one path at a time
 *  and it composes them.  `outline` says which rule to ask: in outline
 *  the bands stand aside and the network is drawn in their place. */
int                 margin_count(void);
int                 margin_outline(void);
int                 margin_gather(RMesh *m, const RCity *c, uint8_t mask_bit, int i, WalkFan *out, ShapeId *sh);
int                 margin_junction(const JBox *jb, const V2 *poly, const JuncArm *arms, int np);
/*  The outline the junction's FILL is laid on.  It is the box's outline,
 *  with every side that carries a margin drawn in to the margin's own
 *  inner edge.  So the two meet along it instead of the fill being laid
 *  under the band and hiding it.  A mouth keeps the outline, since the
 *  line runs on through.  Up to 2*np points. 0 when the box carries no
 *  margin at all, and the fill then reaches the outline. */
int margin_junction_inset(const JBox *jb, const V2 *poly, const JuncArm *arms, int np, V2 *out, int max);
/*  Which of a junction's arms may carry a meet, from its outline alone:
 *  `want[e]` is the band a stripe asks for there, 0 for none.  How much
 *  of it the arm can actually spare is walk.c's.  This knows how long
 *  the line beyond the mouth is. */
int margin_junction_wants(const RCity *c, Family f, int32_t col, int32_t row, const V2 *poly, const JuncArm *arms, int np, float lw, float *want);
/*  The margin round one junction, as the DRIVE has it answered: the ring
 *  handed to arc.rules.band, the answer taken.  Then each mouth read and
 *  answered by arc.rules.lap_at.  Nothing in the margin calls up.  A
 *  script that answers neither leaves the junction bare. */
void *margin_band_ask(const JBox *jb, const V2 *poly, const JuncArm *arms, int np, float lw);
void  margin_band_answered(void);
int   margin_mouths_ask(const V2 *poly, int np, float lw);
int   margin_mouth_at(int k, int32_t *col, int32_t *row, int *arm, int *ctrl, int *pave, float *cs, float *span);
void  margin_mouth_is(int k, int marked, float deep);

#endif /* ARC_NET_MARGIN_H */
