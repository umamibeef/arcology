/*  GENERATED from src/render/mesh/materials.def.  Do not edit.
 *
 *  A material is the number a vertex colour's third component
 *  carries.  The mesh writes it, the shaders read it by range, and
 *  a report names it by the name in r_materials. */
#ifndef ARC_MATERIALS_H
#define ARC_MATERIALS_H

#define MAT_GROUND   0.0f   /* the terrain's own surface */
#define MAT_ENG_WALL 1.0f   /* an engineered wall: a retaining face */
#define MAT_SEDIMENT 2.0f   /* a river bed's layers */
#define MAT_WATER    3.0f   /* standing or flowing water */
#define MAT_SEABED   4.0f   /* the floor under water */
#define MAT_EARTH    5.0f   /* a cut face through soil */
#define MAT_SURFACE  6.0f   /* a leveled pad */
#define MAT_PROP     8.0f   /* a prop: a pole, a pylon, a box */
#define MAT_LAMP     9.0f   /* a lamp's or a signal's lit face */
#define MAT_PIER     10.0f  /* a highway's column: concrete, standing on the ground */
#define MAT_LEAF     11.0f  /* a tree's foliage: mottled, shaded by its crown, cut ragged at its edge */
#define MAT_ZONE     20.0f  /* a zone tint over the ground */

/*  A material is a NUMBER, and a variant is shaded from the whole
 *  number beside it.  So a test on a material is a test on the half
 *  step around it.  MAT_IS names one material with its variants.
 *  MAT_IN names a run of them, from the first to the last.
 *  MAT_ABOVE, MAT_FROM and MAT_UPTO each name everything on one
 *  side of one. */
#define MAT_IS(m, M)    ((m) > (M) -0.5f && (m) < (M) + 0.5f)
#define MAT_IN(m, A, B) ((m) > (A) -0.5f && (m) < (B) + 0.5f)
#define MAT_ABOVE(m, M) ((m) > (M) + 0.5f)
#define MAT_FROM(m, M)  ((m) > (M) -0.5f)
#define MAT_UPTO(m, M)  ((m) < (M) + 0.5f)

/*  The materials named at run time are numbered from here, one
 *  per material_define.  Each is shaded from its parameters
 *  rather than from a branch of its own. */
#define MAT_RUNTIME_BASE 32.0f
#define MAT_RUNTIME_MAX  32

/*  The most tones the layers of a cut are dealt from (STRATA
 *  in gpu/frame.c). */
#define MAT_STRATA_MAX  8

/*  How many built-in materials there are, for a table with a
 *  slot for each. */
#define MAT_COUNT 12

/*  The built-in materials by name, for a report to print. */
typedef struct
{
    const char *name;
    float       value;
} RMaterial;
extern const RMaterial r_materials[];
extern const int       r_materials_n;

#endif
