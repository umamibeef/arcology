/*  script.h -- the scripting layer.  One Lua state, one script, and a
 *  table named `arc` through which the running program is reachable:
 *
 *      arc.tune    the live knobs the look is tuned with
 *      arc.geo     the geometry constants the road works are built from
 *      arc.rules   the decisions the C asks the script to make
 *      arc.city    what stands on a tile
 *      arc.mesh    what was drawn, and what the checks counted
 *      arc.log     a message; arc.dump a report line
 *
 *  The script is WATCHED: its file is looked at once a frame and reloaded
 *  when it changes, and the mesh is rebuilt on the reload, so a rule is
 *  changed by saving a file rather than by compiling.
 *
 *  Every rule is optional.  A rule the script does not set, a script that
 *  fails to load, and a build without Lua at all are the same thing to
 *  the caller: `script_rule_*` answers 0 and the C decides, which is the
 *  one path this layer must never fork.
 */
#ifndef ARC_SCRIPT_H
#define ARC_SCRIPT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*  Bring the state up and run the scripts.  `dir` is the folder the
 *  program ships -- the props' models live there and are always read --
 *  and `path` a file or folder of the player's own, read after it so it
 *  may change what the first set.  Either may be NULL.  Answers 0 when
 *  everything named loaded, -1 when something did not; the reason goes
 *  to the log either way. */
int script_open(const char *dir, const char *path);
void script_close(void);
/*  Is there a state at all: a build with Lua, brought up. */
int script_on(void);
/*  The script's path, or NULL. */
const char *script_path(void);

/*  Run one chunk of source, for the console and --lua-eval.  Its result,
 *  if it has one, is written through dumpf.  Answers 0 on success. */
int script_eval(const char *src);

/*  Has the watched file changed since it was read?  Cheap enough to ask
 *  once a frame. */
int script_stale(void);
/*  Read it again.  Answers 1 when the state changed and the mesh should
 *  be built again, 0 when nothing was reloaded. */
int script_reload(void);
/*  Has the script asked for the world to be drawn again -- by writing a
 *  knob, a constant, or calling arc.rebuild?  Cleared by the asking. */
int script_take_dirty(void);
/*  The last error, for the console and the log, or NULL. */
const char *script_error(void);
/*  How many times the script has been read, and how many rules it sets:
 *  what the console shows so a reload is visible. */
int script_generation(void);

/*  The materials a script declared with arc.mat.define, as a run of four
 *  floats each -- the colour, and roughness in the fourth.  The frame
 *  hands them to the shaders, which shade anything numbered from
 *  MAT_SCRIPT_BASE from them rather than from a branch of its own. */
int  script_materials(const float **out);
void script_material_reset(void);

/*  The families, as the scripts declare them (arc.family.define): each
 *  reading of the scripts starts from none, so what stands is exactly
 *  what this reading declared. */
void script_family_reset(void);

/*  The byte tables a script pushed with arc.bytes: what a byte of the
 *  save means, read straight out of 256 rather than asked for.  An
 *  unknown name answers a table of noughts, and a reading of the scripts
 *  clears the lot. */
const unsigned char *script_bytes(const char *name);
/*  And the named numbers it pushed with arc.numbers: `keys` and `out`
 *  are n long and line up, and a key the table does not name keeps
 *  whatever `out` held.  Answers 0 where no table of that name was
 *  pushed at all -- which is not an answer, and a caller that cannot go
 *  on without them must say so. */
int                  script_numbers(const char *name, const char *const *keys, float *out, int n);
/*  And the two whose entries have a SHAPE: which network a byte carries
 *  and where in the shared layout it sits, and what part of a highway it
 *  is and which way that runs.  Arrays of 256, pushed whole. */
void                 script_pieces(const unsigned char **fam, const signed char **piece,
                                   const unsigned char **fam2, const signed char **piece2);
void                 script_highways(const unsigned char **kind, const unsigned char **ew);
void                 script_data_reset(void);

/*  A rule that RAISED, or a script that would not load -- as against a
 *  rule the scripts never set.  An unset rule answers "the C decides",
 *  which is an answer; a rule that raised answered nothing, and a build
 *  that carried on past one would draw a city with pieces missing and
 *  report success.  script_fault names the first, script_fault_count
 *  says how many followed it, and the fault stands until the scripts are
 *  read again -- a broken rule cannot be got past by building twice. */
const char *script_fault(void);
int         script_fault_count(void);
int script_rules(void);
/*  How many script files the last reading read, and through `total` how
 *  many it found.  The two are equal after a reading that finished. */
int script_files(int *total);

/*  ---- the rules ------------------------------------------------------
 *
 *  Each answers 1 when the script decided and *out holds the answer, 0
 *  when it did not and the caller keeps its own. */

/*  Whether one arm's mouth carries a crossing, and how deep a band it
 *  asks for before the road it has to give up is known.  `pavement` says
 *  the outline has a footway on each side of the mouth, `cos` how the two
 *  run against one another -- -1 is one pavement squarely facing the
 *  other across the road -- and `span` how wide the mouth is in footway
 *  widths.  Answers 1 with *out the depth in tiles, 0 for no crossing. */
int script_rule_crossing_at(int col, int row, int arm, int ctrl, int pavement, float cos, float span, float *out);

/*  The world that MOVES asks these on its own beat, never on a frame:
 *  where a level crossing's gate arm has swung to, and the speed a car
 *  keeps for the car ahead of it and for the line it must stop at. */
float script_rule_gate(float angle, float near, float dt);
float script_rule_car_follow(float gap, float v, float stop, float free);
float script_rule_car_hold(float to_end, int hold, float line, float v, float dt);

/*  ---- the models ----------------------------------------------------
 *
 *  Every prop's shape is a file of its own under scripts/models, and the
 *  renderer reaches it through these.  A model that no file defines is
 *  not there, and the prop that wanted it draws nothing. */
struct ModelHead;
struct ModelPart;
int         script_model_count(void);
const char *script_model_name(int i);
int         script_model_find(const char *name);
void        script_model_reset(void);
/*  What the model is made of for a prop of that size.  Answers how many
 *  pieces were written into `parts`. */
int script_model_build(int model, float size, struct ModelHead *head, struct ModelPart *parts, int max);

/*  ---- the strips ----------------------------------------------------
 *
 *  A strip's centreline is fitted and graded by the pipeline; what it
 *  looks like is composed by arc.rules.strip, which walks the stations
 *  through arc.strip and lays the ribbon through arc.put.  The Loft and
 *  the heights pass as addresses: this header names neither the
 *  renderer's types nor Lua's.  What a deck builds beside its quads is
 *  the same script's (scripts/compose/deck.lua). */
void script_strip_open(void *loft, const char *family, float cls, const float *zorig);
void script_strip_close(void);
/*  Answers 1 when the script laid the ribbon.  With no rule a strip has
 *  no surface at all: nothing in C draws one. */
int  script_rule_strip(void);
/*  What the family builds beside one pair, which the rule asks for by
 *  index: a deck's gore before the quad and its underside after it. */
float script_strip_zorig(void *loft, int i);
/*  The pair as the pipeline builds it, for a check against the
 *  composition; `pair` is an RLoft-shaped record the caller owns. */
int   script_strip_pair(void *loft, int i, void *pair);
/*  The footway beside a strip, composed station by station by the same
 *  rule that lays the carriageway, so the two edges that meet along the
 *  band's inner line are worked out by one expression and cannot
 *  disagree.  The network's own bookkeeping -- which node and arm the
 *  path belongs to, which ports it names -- stays with the strip's
 *  record; only the geometry comes from here. */
void  script_walk_reset(void);
int   script_walk_at(int side, float ox, float oy, float ix, float iy, float z);
void  script_walk_ends(int side, float ax, float ay, float bx, float by);
int   script_walk_count(int side);
/*  Station k of that side, and the two ends: `out` takes ox, oy, ix, iy,
 *  z for a station and ax, ay, bx, by for the ends. */
void  script_walk_station(int side, int k, float *out);
void  script_walk_end_pts(int side, float *out);
/*  Hand a rule the thing itself.  A thing can be asked more than one
 *  question -- a strip is asked for its surface and for the footways
 *  beside it -- so `rule` names the question and `kind` the thing, which
 *  is what says which methods the handle has.  Answers 1 when the script
 *  answered and the C should not. */
int   script_rule_object(const char *rule, const char *kind, void *rec);

/*  ---- the props -----------------------------------------------------
 *
 *  A script may build a piece of the world itself, not only size the one
 *  the C builds.  While a prop is being drawn the layer holds the mesh
 *  it is drawn into, so `arc.put.box` and its neighbours have somewhere
 *  to put a face; outside that window they draw nothing.
 *
 *  script_rule_prop answers 1 when the script drew the prop and the C
 *  should not, 0 when it did not and the C draws its own. */
/*  The mesh and the city are the renderer's; this header names neither
 *  its types nor Lua's, so they pass as addresses and api_put.c reads
 *  them back. */
void script_emit_open(void *mesh, const void *city, uint8_t mask_bit, float order);
void script_emit_close(void);
/*  `at` is the prop's own place: where it stands, which way it faces,
 *  the ground under it and how big the thing it belongs to is.  The
 *  fields a prop has are named in src/script/api_put.c. */
typedef struct
{
    float x, y, z;   /* where it stands, and the ground there */
    float fx, fy;    /* the way it faces                      */
    float size;      /* the thing it belongs to, across       */
    int   col, row;  /* the tile                              */
    int   arm;       /* the arm it belongs to, or -1          */
    int   links;     /* which edges the tile joins, a bit an edge */
    float phase;     /* its own place in the signal's cycle   */
    float angle;     /* a moving part's angle, in degrees     */
    float len;       /* how far that part reaches             */
} ScriptProp;
int script_rule_prop(const char *name, const ScriptProp *at);

/*  Where the lamps stand along one strip: each is a distance along it,
 *  which side of the centreline the pole is on and how far in from the
 *  kerb it stands.  `cls` is the strip's road class and `len` how long
 *  it runs.  Answers how many were written; with no rule there are no
 *  lamps at all, since nothing in C spaces them. */
typedef struct
{
    float at;   /* along the strip, in tiles      */
    float side; /* 1 one hand of it, -1 the other */
    float in;   /* in from the kerb, as a part of the half width */
} ScriptLamp;





/*  Which way an on-ramp's taper lies along the deck.  `free_side` says
 *  both ways are open -- a road along the deck's axis blocks its own
 *  side, since the strip would run over the road.  `room` and `room_back`
 *  are how many deck tiles each way has.  1 keeps the id's own way, 0
 *  turns it about.
 *
 *  And how two ramps whose tapers face each other share the tiles
 *  between them: `gap` tiles lie between the two deck tiles and `cap` is
 *  the longest taper either may have.  The answer is what each is cut
 *  to, or -1 to leave them alone. */

/*  Where a ramp's descent runs along the deck: `at` is its station's
 *  distance along the band, `len` its taper in tiles, `leaves` whether it
 *  goes down from the deck or up onto it, and `sgn` whether the band's
 *  own direction runs with the ramp's.  Answers the top of the descent,
 *  its foot, how long it is, and which way along the band the taper
 *  lies. */
/*  Where a highway band's walk begins.  `back` and `on` say whether the
 *  band carries on the two ways along it from this cell.  1 walks forward
 *  from here, -1 backward, 0 leaves the cell to the sweep that walks a
 *  band with no end at all. */




/*  How the world that moves behaves: the trains, the cars that follow
 *  one another, the gates they wait at and the signals that blink.  None
 *  of it varies from one thing to the next, so it is asked once and kept
 *  for as long as the scripts stand -- the frame loop reads the answer
 *  and never the script.  With no rule nothing moves. */
typedef struct
{
    float blink;                     /* a signal's lit share of its cycle    */
    float train_speed, train_spread; /* the slowest train, and the spread    */
    float train_len, trail_step;     /* a car's length, and the trail's step */
    float xing_find;                 /* how near its middle a crossing is found */
    float gate_up, gate_watch;       /* the arm at rest, and the approach it watches */
    float density, car_len;          /* cars a tile, and how long one is     */
    float gap_stop, gap_free;        /* how near a car follows another       */
    float stop_junc, stop_hold;      /* where it stops at a junction         */
    float creep, probe;              /* and at a gate, and how far it looks ahead */
    float step_max;                  /* the longest frame the world is stepped by */
    float slot;                      /* where a moving thing sits in the stack */
    /*  The block a rail signal protects: how far behind it a train still
     *  holds it, and how far ahead of it the block runs. */
    float block_back, block_ahead;
} ScriptTraffic;
int script_rule_traffic(ScriptTraffic *out);

/*  What is true of every strip and every junction of one family: the
 *  footway beside its carriageway, the box its junctions are built in,
 *  the tracks a railway lays through one, and the band a level crossing
 *  fades over on its approach.  Asked once for each family and kept for
 *  the length of a build, since none of it varies from strip to strip.
 *  With no rule a family has no footway, no junction box and no tracks:
 *  nothing in C carries a second copy. */
typedef struct ScriptFamily
{
    int   walks; /* the family has a footway at all */
    /*  The footway, as fractions of the carriageway's half width. */
    float inner, edge, at_junction, parallel;
    float look;  /* how far past its end it looks for what it joins */
    float mouth; /* the slack a footway is allowed at a junction's mouth */
    float slot_strip, slot_junction, slot_cross; /* its bands in the painter's stack */
    /*  The junction box: how far inside the tile its surface is read,
     *  and how far out its outline may reach. */
    float junc_inset, junc_far;
    /*  A railway's tracks through a junction: the gauge between two, and
     *  where a through line runs -- the second one over the first. */
    float gauge, through, second;
    /*  A level crossing's approach: the band the road fades over on its
     *  way in, from `near` of the crossing to `far`. */
    float app_near, app_far;
    /*  The strip itself: how finely it is cut along its length, how far
     *  its surface stands over the ground it was graded into, how far
     *  under the ground a station has to fall to count as a cut, and the
     *  hairline the curve overlay draws it with. */
    float step_run, step_arc, lift, lift_min, cut, dip;
    float mark_wide, mark_lift, mark_high, mark_slot, line_wide;
    /*  The lanes drawn on it: how tight a connector may turn, how finely
     *  one is cut, where it sits in the stack, how wide it is drawn and
     *  how far it lifts; when two of them join, how near the map's edge
     *  one may run, and how far one reaches for what it meets. */
    float lane_rmin, lane_step_run, lane_step_arc, lane_step_ramp;
    float lane_slot, lane_wire, lane_lift;
    float lane_join, lane_aim, lane_edge, lane_reach;
    /*  Which way a run leaves a junction: a first piece at least this long
     *  gives its own tangent, and a shorter one is measured this far along
     *  the run instead. */
    float arm_own, arm_base;
    /*  The shelf a corridor cuts for itself: how far along the centreline
     *  a corner may take its grade from, how far past the band's own edge
     *  the shelf reaches, and how far past it the batter begins that
     *  blends the shelf back into the hillside. */
    float shelf_along, shelf_reach, shelf_batter;
    /*  How near a station must be to a tile's centre to count as standing
     *  AT the level crossing on it. */
    float crossing_centre;
    /*  A road's class from the traffic on its tile: the counts at which
     *  it reads as an avenue and as a boulevard. */
    float class_avenue, class_boulevard;
    /*  A strip: how far inside a tile a lone piece's ends sit, so the end
     *  stations read that tile's surface rather than its neighbour's; how
     *  far out from the centreline a dead end's kerb runs; and the most of
     *  an arm's road a crossing may take. */
    float tile_inset, cap_kerb, cross_share;
    /*  A lane's open end carries on into the facing lane across a
     *  crossing: how far it may reach for one, how far ahead and how far
     *  aside that one may sit, how nearly the two must run the same way,
     *  how near is the same point, and how far their offsets may differ. */
    /*  How nearly a lane must run the way asked for to be the lane meant. */
    float lane_pick_dot;
    float lane_cross_reach, lane_cross_ahead, lane_cross_aside;
    float lane_cross_dot, lane_cross_spot, lane_cross_off;
    float deck_lane[3];
    /*  A ramp: where the deck's outer lane sits across the deck, how near
     *  a recorded lane must be for a ramp end to snap to it, the cosine
     *  and sine of the angle a through road is met at, how far off the
     *  road's centreline its lanes run, how far along the road the merge
     *  sits from the foot, and how far past the road edge the taper
     *  runs. */
    float ramp_outer, ramp_snap, ramp_meet_cos, ramp_meet_sin;
    float ramp_lane_off, ramp_merge_along, ramp_taper;
    /*  Where a deck lane carries on into the next band's: how far it may
     *  reach, how far their offsets may differ, how nearly they must run
     *  the same way, how far behind and how far aside the other end may
     *  sit, and how near is the same point. */
    float band_reach, band_off, band_dot, band_ahead, band_aside, band_apart;
    /*  And where its middle and outer lanes taper into the inner one: how
     *  near another lane's end counts as beside this one, the offset above
     *  which a lane is the outer rather than the middle, how far back each
     *  tapers, the gap between the two ends of a taper, how much band a
     *  taper needs, and how nearly a road lane must face a band's end. */
    float band_abreast, band_outer, band_taper_far, band_taper_near;
    float band_taper_gap, band_taper_room, band_road_dot;
} ScriptFamily;
/*  The numbers a family is drawn by, as the script pushed them with
 *  arc.family.rules.  A family nobody pushed answers a table of noughts. */
const ScriptFamily *script_family_rules(const char *name);

/*  Which of the city's building bytes are a highway, and which way each
 *  runs.  `kind` answers 0 for a byte that is no highway, 1 for a deck
 *  tile, 2 for a ramp of the band, 3 for an on-ramp, 4 for a curve
 *  block, 5 for the interchange and 6 for a highway crossing something;
 *  `ew` answers 1 where the band runs east-west.
 *  Read once a generation and kept, since every tile of the map is
 *  looked up in it. */
int script_rule_hiway_tiles(unsigned char *kind, unsigned char *ew, int n);

/*  Which of the city's building bytes carry a NETWORK piece: the family
 *  it belongs to and its place in the shared fifteen-piece layout, and
 *  the same again for the second family a crossing carries on the other
 *  axis.  `piece` and `piece2` take -1 for a byte no family claims, and
 *  `fam`/`fam2` the family code beside it.  Read once a generation and
 *  kept, since every tile of the map is looked up in it. */
int script_rule_piece_tiles(unsigned char *fam, signed char *piece,
                            unsigned char *fam2, signed char *piece2, int n);

/*  Which of the city's building bytes carry a road, and how: 1 a road
 *  piece, 2 a road crossing something, 3 a road running under a deck
 *  that stands on the tile.  0 is no road.  Read once a generation and
 *  kept, since every tile of the map is looked up in it. */
int script_rule_road_tiles(unsigned char *carries, int n);

/*  A rule that answers a plain set of the city's building bytes: the
 *  table it returns is keyed by the byte and any true value puts it in.
 *  Read once a generation and kept. */
int script_rule_byte_set(const char *rule, unsigned char *set, int n);
/*  The same, where each byte answers a NUMBER rather than yes or no: a
 *  byte the rule does not name takes 0. */
int script_rule_byte_map(const char *rule, unsigned char *map, int n);

/*  A rule that answers a table of NAMED numbers, asked with nothing.
 *  `names` and `out` are n long and line up.  Answers 1 when the rule
 *  answered, 0 when there is no such rule -- and 0 is not an answer: a
 *  caller that cannot go on without the numbers must say so rather than
 *  carry on with whatever `out` held. */
int script_rule_numbers(const char *rule, const char *const *names, float *out, int n);

/*  How big a level crossing is, from the angle the road and the railway
 *  cross at.  `sin` is the sine of that angle, `road` and `rail` their
 *  widths.  Every measurement of the crossing follows: how far along the
 *  road the track bed reaches, how far out past the carriageway the
 *  masts stand, how wide the panel is across the rail, and the panel's
 *  own lift and painter's slot. */
typedef struct
{
    float reach; /* along the road from the middle, each way */
    float mast;  /* out from the centreline to the mast      */
    float bed;   /* across the rail, half the panel's width  */
    float lift;  /* over the ground it replaces              */
    float slot;  /* its place in the painter's stack         */
} ScriptXing;
int script_rule_crossing_frame(int col, int row, float sn, float road, float rail, ScriptXing *out);

/*  What one road approach to a level crossing carries: the gate's mast,
 *  the stop line and the second-train signs.  `reach` and `mast` are the
 *  frame's, and `limit` how far the road runs before a junction owns it.
 *  Each entry stands `out` along the road from the middle and `across`
 *  from its centreline; a stop line instead spans `across` and is `deep`
 *  thick, and is left off where the road cannot carry it. */
typedef struct
{
    float out, across;
    char  model[24]; /* the model that stands there */
} ScriptApproach;

/*  What the script is told about one approach before it decides what
 *  stands on it: the crossing's own reach and mast, how far the road
 *  runs before a junction owns it, its width, the middle of the panel,
 *  and the two directions -- along the approach and across it. */
typedef struct
{
    float reach, mast, limit, road;
    float x, y, fx, fy, gx, gy;
} ScriptApproachAsk;
int script_rule_crossing_marks(float reach, float mast, float limit, float road,
                               float cx, float cy, float fx, float fy, float gx, float gy,
                               ScriptApproach *out, int max);


/*  What stands beside a railway, and where: the signals along it and the
 *  whistle posts before its level crossings.  `len` is how long the
 *  strip runs, `ahead` and `behind` say whether it ends at a junction
 *  each way, and `cross` holds where along it the road crossings are.
 *  Answers how many places were written; with no rule a railway carries
 *  nothing, since nothing in C spaces them. */
typedef struct
{
    float at;      /* along the strip, in tiles                        */
    float side;    /* 1 the track to the right of the walk, -1 the left */
    float out;     /* out from the centreline, in tiles                */
    char  model[24];
    int   signal;  /* a rail signal the traffic lights: 1 absolute, 0 a block, -1 not one */
    int   to_map;  /* faces along the map's own axis rather than back along the track */
    int   clear;   /* dropped where a level crossing owns the tile     */
} ScriptMark;



/*  What a junction's outline does where two arms meet: rounds the corner
 *  off with a kerb return, leaves it square, or runs straight past it.
 *  `phi` is the angle the outline turns through, `grow` how far the
 *  junction has been let out and `width` its footway's.  `room`, `back`
 *  and `fwd` are the room the two mouths leave for a return and the
 *  distances to the outline's points either side, and each is NULL where
 *  it is not yet known -- the trim pass asks before there is an outline
 *  to measure.  With no rule every corner is square: nothing in C rounds
 *  one off. */
enum
{
    CORNER_DROP   = 0, /* no corner: the boundary runs straight past  */
    CORNER_SQUARE = 1, /* the point where the two edges meet          */
    CORNER_ROUND  = 2  /* a kerb return, as ScriptCorner describes it */
};
typedef struct
{
    float tangent; /* how far back along each edge the return starts */
    float radius;  /* the circle tangent to both edges there         */
    float steps;   /* how many pieces its arc is drawn in            */
} ScriptCorner;
int script_rule_corner(int col, int row, float phi, float grow, float width,
                       const float *room, const float *back, const float *fwd, ScriptCorner *out);

#ifdef __cplusplus
}
#endif
#endif
