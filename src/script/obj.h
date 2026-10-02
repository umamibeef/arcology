/*  obj.h: what the kinds of object share.
 *
 *  A script is handed a HANDLE on one of the pipeline's records.  The
 *  methods it has are its KIND'S.  api_object.c holds the handle itself
 *  and the one table of kinds.  Each kind's own methods are in the file
 *  that knows the thing: obj_world.c, obj_strip.c, obj_path.c,
 *  obj_node.c, obj_band.c, obj_lane.c, obj_ground.c and api_tile.c.
 *
 *  This is the whole of what those files need from each other. */
#ifndef ARC_SCRIPT_OBJ_H
#define ARC_SCRIPT_OBJ_H

#include <stddef.h>

#include "lauxlib.h"
#include "lua.h"

#include "city.h"

/*  The handle's record, if it is of the kind asked for.  A handle from a
 *  past call answers nothing. */
void *rec_of(lua_State *L, const char *kind);

/*  A field of a record, by name and by where it sits.  `info` is nearly
 *  always nothing but a list of these.  So the list IS the code.  One
 *  more field for a script to read is one row.  It is not a push and a
 *  setfield and two places to forget them. */
typedef enum
{
    FLD_NUM, /* a float, read as a number     */
    FLD_INT, /* an int, read as an integer    */
    FLD_BOOL /* an int, read as true or false */
} FieldType;

typedef struct
{
    const char *name;
    FieldType   type;
    size_t      off;
} Field;

/*  The fields as a fresh table, left on the stack. */
int api_fields(lua_State *L, const void *rec, const Field *f);

/*  THE METHODS EACH KIND OFFERS.  api_object.c builds one table from
 *  each of these, under the kind's own name. */
extern const luaL_Reg BAND[];
extern const luaL_Reg BANDS[];
extern const luaL_Reg BRIDGE[];
extern const luaL_Reg CAPS[];
extern const luaL_Reg CHAIN[];
extern const luaL_Reg CROSS[];
extern const luaL_Reg DROP[];
extern const luaL_Reg FIT[];
extern const luaL_Reg FRAME[];
extern const luaL_Reg GROUND[];
extern const luaL_Reg JUNCTION[];
extern const luaL_Reg LANE[];
extern const luaL_Reg LAP[];
extern const luaL_Reg LINKS[];
extern const luaL_Reg MARGIN[];
extern const luaL_Reg MEET[];
extern const luaL_Reg MOVING[];
extern const luaL_Reg NETWORK[];
extern const luaL_Reg ORIENT[];
extern const luaL_Reg OUTLINE[];
extern const luaL_Reg PATH[];
extern const luaL_Reg PIECES[];
extern const luaL_Reg PROFILE[];
extern const luaL_Reg RUNS[];
extern const luaL_Reg SHELF[];
extern const luaL_Reg SIGNS[];
extern const luaL_Reg SLIDE[];
extern const luaL_Reg SNAP[];
extern const luaL_Reg STAIR[];
extern const luaL_Reg STEP[];
extern const luaL_Reg STRIP[];
extern const luaL_Reg SWEEP[];
extern const luaL_Reg TARGET[];
extern const luaL_Reg TERRAIN[];
extern const luaL_Reg THREADS[];
extern const luaL_Reg TURNS[];
extern const luaL_Reg WORLD[];

/*  A handle one kind owns, handed out by another.  The world hands a
 *  rule the lanes within reach of a spur's end.  It also names the
 *  ground it is about to settle.  Both records belong to the file that
 *  answers them. */
void obj_snap_push(lua_State *L);
void obj_terrain_city_is(const RCity *c);

#endif /* ARC_SCRIPT_OBJ_H */
