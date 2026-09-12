/*  script.h: the scripting layer.  One Lua state, one script, and a
 *  table named `arc` through which the running program is reachable:
 *
 *      arc.tune the live knobs the look is tuned with arc.geo the
 *      geometry constants the line works are built from arc.rules the
 *      decisions the C asks the script to make arc.city what stands on a
 *      tile arc.mesh what was drawn, and what the checks counted arc.log
 *      a message.  Arc.dump a report line
 *
 *  The script is WATCHED: its file is looked at once a frame and
 *  reloaded when it changes.  The mesh is rebuilt on the reload, so a
 *  rule is changed by saving a file rather than by compiling.
 *
 *  Every rule is optional.  Three things are the same to the caller.  A
 *  rule the script does not set, a script that fails to load, and a
 *  build without Lua at all.  `script_rule_*` answers 0 and the C
 *  decides.  This is the one path this layer must never fork. */
#ifndef ARC_SCRIPT_H
#define ARC_SCRIPT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*  Bring the state up and run the scripts.  `dir` is the folder the
 *  program ships.  The props' models live there and are always read.
 *  And `path` a file or folder of the player's own, read after it so it
 *  may change what the first set.  Either may be NULL.  Answers 0 when
 *  everything named loaded, -1 when something did not.  The reason goes
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
/*  Has the script asked for the world to be drawn again: by writing a
 *  knob, a constant, or calling arc.rebuild?  Cleared by the asking. */
int script_take_dirty(void);
/*  The last error, for the console and the log, or NULL. */
const char *script_error(void);
/*  How many times the script has been read, and how many rules it sets:
 *  what the console shows so a reload is visible. */
int script_generation(void);

/*  WHAT THE SCRIPTS DESCRIBE, as a number.  It changes on every reading
 *  of them and on every arc.rebuild.  A build puts it in its key, so a
 *  mesh built under another reading is never taken as one that still
 *  stands.  Saving a rule is what draws the world again, and that works
 *  only while the build can tell two readings apart. */
int script_stamp(void);

/*  The materials a script declared with arc.mat.define, as a run of four
 *  floats each: the color, and roughness in the fourth.  The frame hands
 *  them to the shaders, which shade anything numbered from
 *  MAT_SCRIPT_BASE from them rather than from a branch of its own. */
int  script_materials(const float **out);
void script_material_reset(void);

/*  The families, as the scripts declare them (arc.family.define): each
 *  reading of the scripts starts from none, so what stands is exactly
 *  what this reading declared. */
void script_family_reset(void);

/*  The city a build is reading, offered to the scripts through
 *  arc.city.  It is an address here for the same reason the mesh is one
 *  below: this header names neither the renderer's types nor Lua's. */
void script_city_is(const void *city);

/*  The byte tables a script pushed with arc.bytes: what a byte of the
 *  save means, read straight out of 256 rather than asked for.  An
 *  unknown name answers a table of noughts, and a reading of the scripts
 *  clears the lot. */
const unsigned char *script_bytes(const char *name);
/*  And the named numbers it pushed with arc.numbers: `keys` and `out`
 *  are n long and line up.  A key the table does not name keeps whatever
 *  `out` held.  Answers 0 where no table of that name was pushed at all:
 *  which is not an answer.  A caller that cannot go on without them must
 *  say so. */
int                  script_numbers(const char *name, const char *const *keys, float *out, int n);
/*  And the two whose entries have a SHAPE.  They give which network a
 *  byte carries, and where in the shared layout it sits.  They also give
 *  what part of a band it is and which way that runs.  Arrays of 256,
 *  pushed whole. */
void                 script_pieces(const unsigned char **fam, const signed char **piece,
                                   const unsigned char **fam2, const signed char **piece2);
void                 script_bandtiles(const unsigned char **kind, const unsigned char **ew);
void                 script_data_reset(void);

/*  A rule that RAISED, or a script that would not load: as against a
 *  rule the scripts never set.  An unset rule answers "the C decides",
 *  which is an answer.  A rule that raised answered nothing.  A build
 *  that carried on past one would draw a city with pieces missing.
 *  Report success. script_fault names the first, script_fault_count says
 *  how many followed it.  The fault stands until the scripts are read
 *  again: a broken rule cannot be got past by building twice. */
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

/*  Whether one arm's mouth carries a meet, and how deep a band it asks
 *  for before the line it has to give up is known.  `margin` says the
 *  outline has a margin on each side of the mouth.  `cos` says how the
 *  two run against one another, and -1 is one margin squarely facing the
 *  other across the line.  And `span` how wide the mouth is in margin
 *  widths.  Answers 1 with *out the depth in tiles, 0 for no meet. */
int script_rule_meet_at(int col, int row, int arm, int ctrl, int margin, float cos, float span, float *out);

/*  The world that MOVES asks these on its own beat, never on a frame:
 *  where a level meet's gate arm has swung to.  The speed a car keeps
 *  for the car ahead of it and for the line it must stop at. */
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
int script_model_build(int model, float size, struct ModelHead *head, struct ModelPart *parts);

/*  ---- the strips ----------------------------------------------------
 *
 *  A strip's centerline is fitted and graded by the pipeline.  What it
 *  looks like is composed by arc.rules.strip, which walks the stations
 *  through arc.strip and lays the ribbon through arc.put.  The Loft and
 *  the heights pass as addresses: this header names neither the
 *  renderer's types nor Lua's.  What a slab builds beside its quads is
 *  the same script's (scripts/compose/slab.lua). */
void script_strip_open(void *loft, const char *family, float cls, const float *zorig);
void script_strip_close(void);
/*  Answers 1 when the script laid the ribbon.  With no rule a strip has
 *  no surface at all: nothing in C draws one. */
int  script_rule_strip(void);
/*  What the family builds beside one pair, which the rule asks for by
 *  index: a slab's gore before the quad and its underside after it. */
float script_strip_zorig(void *loft, int i);
/*  The pair as the pipeline builds it, for a check against the
 *  composition.  `pair` is an RLoft-shaped record the caller owns. */
int   script_strip_pair(void *loft, int i, void *pair);
/*  The margin beside a strip.  The same rule that lays the way composes
 *  it station by station.  So the two edges that meet along the band's
 *  inner line are worked out by one expression and cannot disagree.  The
 *  network's own bookkeeping.  Which node and arm the path belongs to,
 *  which ports it names.  Stays with the strip's record.  Only the
 *  geometry comes from here. */
void  script_walk_reset(void);
int   script_walk_at(int side, float ox, float oy, float ix, float iy, float z);
void  script_walk_ends(int side, float ax, float ay, float bx, float by);
int   script_walk_count(int side);
/*  Station k of that side, and the two ends.  `out` takes ox, oy, ix,
 *  iy, z for a station.  It takes ax, ay, bx, by for the ends. */
void  script_walk_station(int side, int k, float *out);
void  script_walk_end_pts(int side, float *out);
/*  Hand a rule the thing itself.  A thing can be asked more than one
 *  question.  A strip is asked for its surface and for the margins
 *  beside it.  So `rule` names the question and `kind` the thing, which
 *  is what says which methods the handle has.  Answers 1 when the script
 *  answered and the C should not. */
int   script_rule_object(const char *rule, const char *kind, void *rec);

/*  ---- the props -----------------------------------------------------
 *
 *  A script may build a piece of the world itself, not only size the one
 *  the C builds.  While a prop is being drawn the layer holds the mesh
 *  it is drawn into.  So `arc.put.box` and its neighbors have somewhere
 *  to put a face.  Outside that window they draw nothing.
 *
 *  script_rule_prop answers 1 when the script drew the prop and the C
 *  should not, 0 when it did not and the C draws its own. */
/*  The mesh and the city are the renderer's.  This header names neither
 *  its types nor Lua's, so they pass as addresses and api_put.c reads
 *  them back. */
void script_emit_open(void *mesh, const void *city, uint8_t mask_bit, float order);
void script_emit_close(void);
/*  `at` is the prop's own place.  It gives where it stands and which way
 *  it faces.  It also gives the ground under it, and how big the thing
 *  it belongs to is.  The fields a prop has are named in
 *  src/script/api_put.c. */
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

/*  Where the lamps stand along one strip.  Each is a distance along it.
 *  It also gives which side of the centerline the pole is on, and how
 *  far in from the lip it stands.  `cls` is the strip's line class and
 *  `len` how long it runs.  Answers how many were written.  With no rule
 *  there are no lamps at all, since nothing in C spaces them. */
typedef struct
{
    float at;   /* along the strip, in tiles      */
    float side; /* 1 one hand of it, -1 the other */
    float in;   /* in from the lip, as a part of the half width */
} ScriptLamp;





/*  Which way an on-spur's taper lies along the slab.  `free_side` says
 *  both ways are open: a line along the slab's axis blocks its own side,
 *  since the strip would run over the line.  `room` and `room_back` are
 *  how many slab tiles each way has. 1 keeps the id's own way, 0 turns
 *  it about.
 *
 *  And how two spurs whose tapers face each other share the tiles
 *  between them.  `gap` tiles lie between the two slab tiles, and `cap`
 *  is the longest taper either may have.  The answer is what each is cut
 *  to, or -1 to leave them alone. */

/*  Where a spur's descent runs along the slab.  `at` is its station's
 *  distance along the band.  `len` is its taper in tiles.  `leaves` says
 *  whether it goes down from the slab or up onto it.  `sgn` says whether
 *  the band's own direction runs with the spur's.  Answers the top of
 *  the descent, its foot, how long it is, and which way along the band
 *  the taper lies. */
/*  Where a band's walk begins.  `back` and `on` say whether the
 *  band carries on the two ways along it from this cell.  1 walks forward
 *  from here, -1 backward, 0 leaves the cell to the sweep that walks a
 *  band with no end at all. */




/*  How the world that moves behaves: the trains, the cars that follow
 *  one another, the gates they wait at and the signals that blink.  None
 *  of it varies from one thing to the next.  So it is asked once and
 *  kept for as long as the scripts stand: the frame loop reads the
 *  answer and never the script.  With no rule nothing moves. */
typedef struct
{
    float blink;                     /* a signal's lit share of its cycle    */
    float train_speed, train_spread; /* the slowest train, and the spread    */
    float train_len, trail_step;     /* a car's length, and the trail's step */
    float lap_find;                 /* how near its middle a meet is found */
    float gate_up, gate_watch;       /* the arm at rest, and the approach it watches */
    float density, car_len;          /* cars a tile, and how long one is     */
    float gap_stop, gap_free;        /* how near a car follows another       */
    float stop_junc, stop_hold;      /* where it stops at a junction         */
    float creep, probe;              /* and at a gate, and how far it looks ahead */
    float step_max;                  /* the longest frame the world is stepped by */
    float slot;                      /* where a moving thing sits in the stack */
    /*  The block a thread signal protects: how far behind it a train still
     *  holds it, and how far ahead of it the block runs. */
    float block_back, block_ahead;
} ScriptTraffic;
int script_rule_traffic(ScriptTraffic *out);

/*  What is true of every strip and every junction of one family.
 *
 *      The margin beside its way.
 *      The box its junctions are built in.
 *      The threads a railway lays through one.
 *      The band a level meet fades over on its approach.
 *
 *  Asked once for each family and kept for the length of a build, since
 *  none of it varies from strip to strip.  With no rule a family has no
 *  margin, no junction box and no threads: nothing in C carries a second
 *  copy. */
typedef struct ScriptFamily
{
    int   walks; /* the family has a margin at all */
    /*  The margin, as fractions of the way's half width. */
    float inner, edge, at_junction, parallel;
    float look;  /* how far past its end it looks for what it joins */
    float mouth; /* the slack a margin is allowed at a junction's mouth */
    float slot_strip, slot_junction, slot_cross; /* its bands in the painter's stack */
    /*  The junction box: how far inside the tile its surface is read,
     *  and how far out its outline may reach. */
    float junc_inset, junc_far;
    /*  A railway's threads through a junction: the gauge between two,
     *  and where a through line runs: the second one over the first. */
    float gauge, through, second;
    /*  A level meet's approach: the band the line fades over on its
     *  way in, from `near` of the meet to `far`. */
    float app_near, app_far;
    /*  The strip itself.  It says how finely it is cut along its length.
     *  It says how far its surface stands over the ground it was graded
     *  into.  It says how far under the ground a station has to fall to
     *  count as a cut.  It gives the hairline the curve overlay draws it
     *  with. */
    float step_run, step_arc, lift, lift_min, cut, dip;
    float mark_wide, mark_lift, mark_high, mark_slot, line_wide;
    /*  The lanes drawn on it.
     *
     *      How tight a connector may turn.
     *      How finely one is cut.
     *      Where it sits in the stack.
     *      How wide it is drawn and how far it lifts.
     *
     *  When two of them join, how near the map's edge one may run, and
     *  how far one reaches for what it meets. */
    float lane_rmin, lane_step_run, lane_step_arc, lane_step_spur;
    float lane_slot, lane_wire, lane_lift;
    float lane_join, lane_aim, lane_edge, lane_reach;
    /*  Which way a run leaves a junction: a first piece at least this
     *  long gives its own tangent.  A shorter one is measured this far
     *  along the run instead. */
    float arm_own, arm_base;
    /*  The shelf a corridor cuts for itself.  It says how far along the
     *  centerline a corner may take its grade from.  It says how far
     *  past the band's own edge the shelf reaches.  It says how far past
     *  it the batter begins that blends the shelf back into the
     *  hillside. */
    float shelf_along, shelf_reach, shelf_batter;
    /*  How near a station must be to a tile's center to count as
     *  standing AT the level meet on it. */
    float lap_centre;
    /*  A line's class from the traffic on its tile: the counts at which
     *  it reads as an avenue and as a boulevard. */
    float class_avenue, class_boulevard;
    /*  A strip: how far inside a tile a lone piece's ends sit.  So the
     *  end stations read that tile's surface rather than its neighbor's.
     *  How far out from the centerline a dead end's lip runs.  And the
     *  most of an arm's line a meet may take. */
    float tile_inset, cap_lip, cross_share;
    /*  A lane's open end carries on into the facing lane across a meet.
     *  How far it may reach for one.  How far ahead and how far aside
     *  that one may sit.  How nearly the two must run the same way, how
     *  near is the same point.  How far their offsets may differ. */
    /*  How nearly a lane must run the way asked for to be the lane meant. */
    float lane_pick_dot;
    float lane_cross_reach, lane_cross_ahead, lane_cross_aside;
    float lane_cross_dot, lane_cross_spot, lane_cross_off;
    /*  The slab's lane centers, from its centerline outward, as a
     *  fraction of the half width.  And HOW MANY there are, because how
     *  many lanes a way carries is the script's answer and not a number
     *  this file may fix.  None is a slab with no lanes on it. */
#define NET_SLAB_LANES_MAX 8
    float slab_lane[NET_SLAB_LANES_MAX];
    int   slab_lanes;
    /*  A spur.  It says where the slab's outer lane sits across the
     *  slab.  It says how near a recorded lane must be for a spur end to
     *  snap to it.  It gives the cosine and sine of the angle a through
     *  line is met at.  It gives how far off the line's centerline its
     *  lanes run.  It gives how far along the line the merge sits from
     *  the foot, and how far past the line edge the taper runs. */
    float spur_outer, spur_snap, spur_meet_cos, spur_meet_sin;
    float spur_lane_off, spur_merge_along, spur_taper;
    /*  Where a slab lane carries on into the next band's.  How far it
     *  may reach, and how far their offsets may differ.  How nearly they
     *  must run the same way, how far behind and how far aside the other
     *  end may sit.  How near is the same point. */
    float band_reach, band_off, band_dot, band_ahead, band_aside, band_apart;
    /*  And where its middle and outer lanes taper into the inner one.
     *  How near another lane's end counts as beside this one.  The
     *  offset above which a lane is the outer rather than the middle.
     *  How far back each tapers, and the gap between the two ends of a
     *  taper.  How much band a taper needs, and how nearly a line lane
     *  must face a band's end. */
    float band_abreast, band_outer, band_taper_far, band_taper_near;
    float band_taper_gap, band_taper_room, band_road_dot;
} ScriptFamily;
/*  The numbers a family is drawn by, as the script pushed them with
 *  arc.family.rules.  A family nobody pushed answers a table of noughts. */
const ScriptFamily *script_family_rules(const char *name);

/*  Which of the city's building bytes are a band, and which way each
 *  runs.  `kind` answers 0 for a byte that is no band, and 1 for a slab
 *  tile.  It answers 2 for an incline of the band, and 3 for a spur.  It
 *  answers 4 for a curve block, 5 for the interchange and 6 for a band
 *  meet something.  `ew` answers 1 where the band runs east-west.  Read
 *  once a generation and kept, since every tile of the map is looked up
 *  in it. */
int script_rule_band_tiles(unsigned char *kind, unsigned char *ew, int n);

/*  Which of the city's building bytes carry a NETWORK piece.  The family
 *  it belongs to, and its place in the shared fifteen-piece layout.  It
 *  gives the same again for the second family a lap carries on the other
 *  axis.  `piece` and `piece2` take -1 for a byte no family claims, and
 *  `fam`/`fam2` the family code beside it.  Read once a generation and
 *  kept, since every tile of the map is looked up in it. */
int script_rule_piece_tiles(unsigned char *fam, signed char *piece,
                            unsigned char *fam2, signed char *piece2, int n);

/*  Which of the city's building bytes carry a line, and how.  1 is a
 *  line piece.  2 is a line lapped by something, and 3 a line running
 *  under a slab that stands on the tile. 0 is no line.  Read once a
 *  generation and kept, since every tile of the map is looked up in it. */
int script_rule_road_tiles(unsigned char *carries, int n);

/*  A rule that answers a plain set of the city's building bytes.  The
 *  table it returns is keyed by the byte and any true value puts it in.
 *  Read once a generation and kept. */
int script_rule_byte_set(const char *rule, unsigned char *set, int n);
/*  The same, where each byte answers a NUMBER rather than yes or no: a
 *  byte the rule does not name takes 0. */
int script_rule_byte_map(const char *rule, unsigned char *map, int n);

/*  A rule that answers a table of NAMED numbers, asked with nothing.
 *  `names` and `out` are n long and line up.  Answers 1 when the rule
 *  answered, 0 when there is no such rule.  And 0 is not an answer.  A
 *  caller that cannot go on without the numbers must say so rather than
 *  carry on with whatever `out` held. */
int script_rule_numbers(const char *rule, const char *const *names, float *out, int n);

/*  How big a level meet is, from the angle the line and the railway
 *  cross at.  `sin` is the sine of that angle, `line` and `thread` their
 *  widths.  Every measurement of the meet follows.
 *
 *      How far along the line the thread bed reaches.
 *      How far out past the way the masts stand.
 *      How wide the panel is across the thread.
 *      The panel's own lift and painter's slot. */
typedef struct
{
    float reach; /* along the line from the middle, each way */
    float mast;  /* out from the centerline to the mast      */
    float bed;   /* across the thread, half the panel's width  */
    float lift;  /* over the ground it replaces              */
    float slot;  /* its place in the painter's stack         */
} ScriptLap;
int script_rule_meet_frame(int col, int row, float sn, float line, float thread, ScriptLap *out);

/*  What one line approach to a level meet carries: the gate's mast, the
 *  stop line and the second-train signs.  `reach` and `mast` are the
 *  frame's, and `limit` how far the line runs before a junction owns it.
 *  Each entry stands `out` along the line from the middle and `across`
 *  from its centerline.  A stop line instead spans `across` and is
 *  `deep` thick, and is left off where the line cannot carry it. */
typedef struct
{
    float out, across;
    float fx, fy;    /* the way it faces, in the world */
    char  model[24]; /* the model that stands there */
} ScriptApproach;

/*  What the script is told about one approach, before it decides what
 *  stands on it.  It gives the lap's own reach and mast.  It gives how
 *  far the line runs before a junction owns it, and its width.  It gives
 *  the middle of the panel, and the two directions: along the approach
 *  and across it. */
typedef struct
{
    float reach, mast, limit, line;
    float x, y, fx, fy, gx, gy;
    int   ns; /* the line runs north-south, for a mark that faces the line's own axis */
} ScriptApproachAsk;
int script_rule_meet_marks(float reach, float mast, float limit, float line,
                               float cx, float cy, float fx, float fy, float gx, float gy,
                               ScriptApproach *out, int max);


/*  What stands beside a railway, and where: the signals along it and the
 *  whistle posts before its level meets.  `len` is how long the strip
 *  runs, `ahead` and `behind` say whether it ends at a junction each
 *  way.  `cross` holds where along it the line meets are.  Answers how
 *  many places were written.  With no rule a railway carries nothing,
 *  since nothing in C spaces them. */
typedef struct
{
    float at;      /* along the strip, in tiles                        */
    float side;    /* 1 the thread to the right of the walk, -1 the left */
    float out;     /* out from the centerline, in tiles                */
    char  model[24];
    int   signal;  /* a thread signal the traffic lights: 1 absolute, 0 a block, -1 not one */
    int   to_map;  /* faces along the map's own axis rather than back along the thread */
    int   clear;   /* dropped where a level meet owns the tile     */
} ScriptMark;



/*  What a junction's outline does where two arms meet: rounds the corner
 *  off with a lip return, leaves it square.  Runs straight past it.
 *  `phi` is the angle the outline turns through, `grow` how far the
 *  junction has been let out and `width` its margin's.  `room`, `back`
 *  and `fwd` are the room the two mouths leave for a return and the
 *  distances to the outline's points either side.  Each is NULL where it
 *  is not yet known: the trim pass asks before there is an outline to
 *  measure.  With no rule every corner is square: nothing in C rounds
 *  one off. */
enum
{
    CORNER_DROP   = 0, /* no corner: the boundary runs straight past  */
    CORNER_SQUARE = 1, /* the point where the two edges meet          */
    CORNER_ROUND  = 2  /* a lip return, as ScriptCorner describes it */
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
