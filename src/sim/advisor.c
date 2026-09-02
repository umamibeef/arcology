/*  advisor.c -- the board of advisors.
 *
 *  The lines are from Bob "BobServo" Mackey's "The SimCity Board of
 *  Advisors" (Something Awful, 16 August 2007).  They are quoted here
 *  as a joke layer over the reconstruction; they are his, not ours.
 *
 *  What IS ours is where each one fires.  Every trigger below is a
 *  point the original itself decides it has something to say -- the
 *  story ladder at $30E30, the disaster trigger at $310B0, the
 *  ordinance that passes itself at $2670A -- so the fire marshal turns
 *  up when something is actually alight and the transport man when the
 *  traffic has actually stopped.
 *
 *  Six of the thirty are left out: the four "Rape Street" ones, the
 *  Emancipation Proclamation one and the AIDS one.  Each is a single
 *  row if you want them back.
 */
#include "advisor.h"
#include <string.h>

/*  ---- the board ------------------------------------------------- */
#define FIRE   "Fire Marshall"
#define TRANS  "Transportation"
#define POLICE "Police Chief"
#define EDU    "Education"
#define HEALTH "Health"
#define PLAN   "City Planning"

/*  A line fires when `kind` and `id` match what the model just did.
 *  id == ANY means the kind alone is enough. */
#define ANY (-1)

typedef struct
{
    const char *who;
    int         kind; /* ADV_* */
    int         id;   /* story, disaster or ANY */
    const char *text;
} Line;

static const Line JOKE[] = {
    /*  ---- the fire marshall, who loves fire -------------------- */
    {FIRE,   ADV_DISASTER,  1,   "As fire marshall, I think someone should put out all of these "
                            "fires I keep hearing about on the radio."               },
    {FIRE,   ADV_DISASTER,  15,  "Let me be perfectly honest with you.  I got this job because I "
                             "looove fire."                                         },
    {FIRE,   ADV_DISASTER,  6,   "Listen, I have to tell you something.  But you have to promise "
                            "that you won't get mad!"                                },
    {FIRE,   ADV_DISASTER,  18,  "I have been starting a LOT of fires around the city.  Whew, that "
                             "felt good.  Irony, eh?"                               },
    {FIRE,   ADV_DISASTER,  8,   "JEEZ COME ON YOU SAID YOU WOULDN'T GET MAD"                                      },

    /*  ---- city planning, who is angry --------------------------- */
    {PLAN,   ADV_STORY,     1,   "I am angry.  ANGRY ABOUT CITIES"                                                 },
    {PLAN,   ADV_ORDINANCE, ANY, "YOU CAN'T TAKE THE MIKE AND IKES FROM OUR VENDING MACHINES!  YOU "
                               "WILL REGRET THIS!"                                 },
    {PLAN,   ADV_DISASTER,  7,   "QUIT HITTING THE TORNADO BUTTON YOU CHUCKLEFUCK"                                 },
    {PLAN,   ADV_DISASTER,  14,  "QUIT HITTING THE TORNADO BUTTON YOU CHUCKLEFUCK"                                 },
    {PLAN,   ADV_STORY,     2,   "I'M ACTUALLY IN A GOOD MOOD TODAY!  I JUST HAVE A VOICE "
                         "MODULATION PROBLEM!"                                       },

    /*  ---- transportation, who should not be driving ------------- */
    {TRANS,  ADV_STORY,     7,   "How the hell am I supposed to do my job without a driver's "
                          "license?  And where did you put my jug of drivin' wine?"  },
    {TRANS,  ADV_STORY,     13,  "A whiskey tax would keep poor people out of the liquor store when "
                           "I bring my family there Saturday afternoons."           },
    {TRANS,  ADV_STORY,     3,   "* chili burp *"                                                                  },
    {TRANS,  ADV_STORY,     14,  "These invisible bugs and lizards are getting out of control!  "
                           "Daddy needs his medicine!"                              },
    {TRANS,  ADV_DISASTER,  16,  "Someone's cigarette ash fell into the contents of my coffee cup "
                              "and now most of my office is on fire.  I suggest you build me a "
                              "new one."                                            },

    /*  ---- the police chief -------------------------------------- */
    {POLICE, ADV_STORY,     5,   "Shooting deaths are down, but we can't ignore the fact that "
                           "criminals are now sharpening their guns and stabbing people to "
                           "death with them."                                        },
    {POLICE, ADV_DISASTER,  3,   "Heh."                                                                            },
    {POLICE, ADV_DISASTER,  13,  "Heh."                                                                            },

    /*  ---- education --------------------------------------------- */
    {EDU,    ADV_STORY,     6,   "Some of our students can't locate our town on a map.  The rest "
                        "can't locate a map when it's pulled down in front of the "
                        "blackboard."                                                },
    {EDU,    ADV_STORY,     8,   "In theory, increasing the length of school buses should make our "
                        "children smarter."                                          },
    {EDU,    ADV_STORY,     9,   "I'm not Martin Lawrence.  Please stop saying \"Wazzuuuppp!\" to "
                        "me during board meetings."                                  },
    {EDU,    ADV_DISASTER,  2,   "It may have been one of the union's demands, but the school being "
                           "on fire is only prolonging the teachers' strike."        },

    /*  ---- health ------------------------------------------------ */
    {HEALTH, ADV_STORY,     4,   "We should check the mercury levels in the reservoir.  I took a "
                           "shower this morning and it was like Terminator 2: Judgment Day in "
                           "there."                                                  },
    {HEALTH, ADV_STORY,     10,  "Most of our senior citizens now have something called \"The "
                            "T-Virus.\""                                            },
    {HEALTH, ADV_DISASTER,  4,   "Why does our city have so many burn victims?  Half the people I "
                              "run into on the street have the complexion of salami!"},
    {HEALTH, ADV_STORY,     11,  "Boy am I ever sick of cancer!"                                                   },
};

#define N_JOKE ((int)(sizeof JOKE / sizeof JOKE[0]))

/*  The plain set.  Same triggers, no jokes -- what you want if the
 *  board is meant to be useful rather than funny. */
static const Line PLAIN[] = {
    {"Utilities",      ADV_STORY,     0,   "The grid is running past what it can make.  Something will go dark."},
    {"City Planning",  ADV_STORY,     1,   "Nobody is moving in.  Nobody at all."                               },
    {"City Planning",  ADV_STORY,     2,   "The shops are empty rather than closed."                            },
    {"City Planning",  ADV_STORY,     3,   "Industry has stopped asking for land."                              },
    {"Utilities",      ADV_STORY,     4,   "Water demand is past what the pumps can meet."                      },
    {"Police Chief",   ADV_STORY,     5,   "Crime is high enough to be worth saying out loud."                  },
    {"Education",      ADV_STORY,     6,   "Schooling is below what you set as the goal."                       },
    {"Transportation", ADV_STORY,     7,   "Traffic is heavy enough to be slowing growth."                      },
    {"Finance",        ADV_STORY,     8,   "The budget does not balance."                                       },
    {"Education",      ADV_STORY,     9,   "Schooling is still short of the goal."                              },
    {"Health",         ADV_STORY,     10,  "Life expectancy is below the goal you set."                         },
    {"Health",         ADV_STORY,     11,  "Health is poor and not improving."                                  },
    {"Finance",        ADV_STORY,     13,  "The treasury is overdrawn."                                         },
    {"Finance",        ADV_STORY,     14,  "Debt service is eating the budget."                                 },
    {"Public Safety",  ADV_DISASTER,  ANY, "A disaster has started."                                            },
    {"Finance",        ADV_ORDINANCE, ANY, "An ordinance was passed without a vote from you."                   },
};

#define N_PLAIN ((int)(sizeof PLAIN / sizeof PLAIN[0]))

static int advisor_which = ADVISOR_JOKE;

void advisor_set(int which) { advisor_which = which; }
int  advisor_get_set(void) { return advisor_which; }

/*  $30E30's ladder: which problem the city would lead with, or -1.
 *  The population gates are the original's, so a village is never told
 *  about things a village cannot have. */
int advisor_topic(const City *c)
{
    /*  $30E72 compares 98 against the meter and branches PAST the story
     *  while 98 >= it, so the story is for a grid running over 98 per
     *  cent of what it can make. */
    if (c->power_pct > 0x62)
        return 0; /* $30E78 */
    if (c->population < 1000)
        return -1; /* $30EB2 */
    if (c->water_pct > 0x62)
        return 4; /* $30F22 */
    if (c->population < 3000)
        return -1; /* $30F28 */
    if (c->graph[GRAPH_CRIME][0] > 0x30)
        return 5;
    if (c->population < 8000)
        return -1; /* $30F88 */
    if (c->graph[GRAPH_TRAFFIC][0] > 0x30)
        return 7;
    if (c->misc[MISC_AGE_W90] < c->misc[MISC_GOAL_EDU])
        return 6;
    if (c->misc[MISC_AGE_W65] < c->misc[MISC_GOAL_LIFE])
        return 10;
    if (c->funds < 0)
        return 13;
    return -1;
}

/*  pick the nth line matching a kind and id, rotating so a board with
 *  several things to say about one topic does not repeat itself */
static const Line *pick(const Line *tbl, int n_tbl, int kind, int id, int rot)
{
    int i, seen = 0, total = 0;
    for (i = 0; i < n_tbl; i++)
        if (tbl[i].kind == kind && (tbl[i].id == id || tbl[i].id == ANY))
            total++;
    if (total == 0)
        return NULL;
    rot %= total;
    for (i = 0; i < n_tbl; i++)
        if (tbl[i].kind == kind && (tbl[i].id == id || tbl[i].id == ANY))
        {
            if (seen == rot)
                return &tbl[i];
            seen++;
        }
    return NULL;
}

/*  the joke set first when it is on, the plain set when it has nothing
 *  to say about this trigger */
static const Line *match(int kind, int id, int rot)
{
    const Line *l = NULL;
    if (advisor_which == ADVISOR_JOKE)
        l = pick(JOKE, N_JOKE, kind, id, rot);
    if (!l)
        l = pick(PLAIN, N_PLAIN, kind, id, rot);
    return l;
}

int advisor_poll(const City *now, const City *prev, AdvisorMsg *out, int max)
{
    static int  rot = 0;
    const Line *l;
    int         n = 0, t;

    if (max <= 0)
        return 0;

    if (prev && now->disaster_kind != prev->disaster_kind && n < max)
    {
        l = match(ADV_DISASTER, now->disaster_kind, rot++);
        if (l)
        {
            out[n].kind = ADV_DISASTER;
            out[n].id   = now->disaster_kind;
            out[n].who  = l->who;
            out[n].text = l->text;
            n++;
        }
    }

    if (prev && (now->ordinances & ~prev->ordinances) && n < max)
    {
        l = match(ADV_ORDINANCE, ANY, rot++);
        if (l)
        {
            out[n].kind = ADV_ORDINANCE;
            out[n].id   = 0;
            out[n].who  = l->who;
            out[n].text = l->text;
            n++;
        }
    }

    t = advisor_topic(now);
    if (t >= 0 && n < max)
    {
        l = match(ADV_STORY, t, rot++);
        if (l)
        {
            out[n].kind = ADV_STORY;
            out[n].id   = t;
            out[n].who  = l->who;
            out[n].text = l->text;
            n++;
        }
    }
    return n;
}
