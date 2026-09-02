/*  advisor.h -- the board of advisors.
 *
 *  This is NOT part of the reconstruction.  Everything else in sc2k/ is
 *  answerable to the original's own code; this file is a layer on top
 *  that reads the model and says something about it.  It writes nothing
 *  back, so the simulation stays exactly as verified.
 *
 *  What is faithful here is WHEN an advisor speaks.  The triggers are
 *  the points where the original itself decides it has something to
 *  say: the story ladder at $30E30, the disaster trigger at $310B0, and
 *  the ordinance that passes itself at $2670A.  The words are ours.
 */
#ifndef ADVISOR_H
#define ADVISOR_H
#include "sc2k.h"

enum
{
    ADV_NONE = 0,
    ADV_STORY,    /* the newspaper's topic, id 0..14 from $30E30   */
    ADV_DISASTER, /* the trigger chose one, id 0..18 from $31166   */
    ADV_ORDINANCE /* one passed itself at $26748, id 0..19         */
};

typedef struct
{
    int         kind; /* ADV_*                                      */
    int         id;   /* which story, disaster or ordinance         */
    const char *who;  /* which advisor is talking                   */
    const char *text;
} AdvisorMsg;

/*  Two sets of words share the same triggers.  The plain set states
 *  what the model is doing; the joke set is the Something Awful board.
 *  The joke set is on by default and falls back to the plain line when
 *  it has nothing for a particular trigger, so turning it off never
 *  loses coverage and turning it on never loses a message. */
enum
{
    ADVISOR_PLAIN = 0,
    ADVISOR_JOKE  = 1
};
void advisor_set(int which); /* default ADVISOR_JOKE */
int  advisor_get_set(void);

/*  $30E30's ladder: which problem the city would lead with, or -1 for
 *  nothing worth saying.  Ordered by severity and gated by population,
 *  so a village is never told about things a village cannot have. */
int advisor_topic(const City *c);

/*  Compare a city against how it looked before the tick and fill in up
 *  to `max` messages.  Returns how many.  `prev` may be NULL, in which
 *  case only the standing topic is reported. */
int advisor_poll(const City *now, const City *prev, AdvisorMsg *out, int max);

#endif
