/*  GENERATED from src/render/mesh/materials.def.  Do not edit.
 *
 *  A material is the number a vertex colour's third component
 *  carries.  The mesh writes it, the shaders read it by range, and
 *  a script names it through arc.mat. */
#ifndef ARC_MATERIALS_H
#define ARC_MATERIALS_H

#define MAT_GROUND    0.0f   /* the terrain's own surface */
#define MAT_ENG_WALL  1.0f   /* an engineered wall: a retaining face */
#define MAT_SEDIMENT  2.0f   /* a river bed's layers */
#define MAT_WATER     3.0f   /* standing or flowing water */
#define MAT_SEABED    4.0f   /* the floor under water */
#define MAT_EARTH     5.0f   /* a cut face through soil */
#define MAT_SURFACE   6.0f   /* a leveled pad */
#define MAT_LINE      7.0f   /* a line's strip on the surface */
#define MAT_PROP      8.0f   /* street furniture: a pole, a sign, a box */
#define MAT_LAMP      9.0f   /* a lamp's or a signal's lit face */
#define MAT_ZEBRA     10.0f  /* a stripe's painted bands */
#define MAT_THREAD    11.0f  /* a family's own threads and their bed */
#define MAT_SKIRT     12.0f  /* a vehicle's clipped skirt */
#define MAT_WALK      13.0f  /* a margin */
#define MAT_THREAD_X  14.0f  /* a lap's panel bed */
#define MAT_VEHICLE   15.0f  /* a car's body */
#define MAT_XPANEL    16.0f  /* a lap's panel */
#define MAT_XAPPROACH 17.0f  /* a lap's approach markings */
#define MAT_PIER      18.0f  /* a raised band's bent and its cast concrete */
#define MAT_BAND      19.0f  /* a raised band's slab */
#define MAT_BAND_LANE 19.3f  /* a spur's lane, drawn as the band's outer lane */
#define MAT_ZONE      20.0f  /* a zone tint over the ground */
#define MAT_HILITE    20.5f  /* the pointer's highlight */

/*  A script's own materials are numbered from here, one per
 *  arc.mat.define, and shaded from their parameters rather than
 *  from a branch of their own. */
#define MAT_SCRIPT_BASE 32.0f
#define MAT_SCRIPT_MAX  32

/*  The built-in materials by name, for the script bridge. */
typedef struct
{
    const char *name;
    float       value;
} RMaterial;
extern const RMaterial r_materials[];
extern const int       r_materials_n;

#endif
