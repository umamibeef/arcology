//  GENERATED from src/render/mesh/materials.def.  Do not edit.
//
//  The shader reads a material by RANGE, so it needs the same numbers
//  and the same membership tests the C does.  gen_shaders.py splices
//  this file in where a shader asks for it.

#define MAT_GROUND   0.0    // the terrain's own surface
#define MAT_ENG_WALL 1.0    // an engineered wall: a retaining face
#define MAT_SEDIMENT 2.0    // a river bed's layers
#define MAT_WATER    3.0    // standing or flowing water
#define MAT_SEABED   4.0    // the floor under water
#define MAT_EARTH    5.0    // a cut face through soil
#define MAT_SURFACE  6.0    // a leveled pad
#define MAT_PROP     8.0    // a prop: a pole, a pylon, a box
#define MAT_LAMP     9.0    // a lamp's or a signal's lit face
#define MAT_PIER     10.0   // a highway's column: concrete, standing on the ground
#define MAT_LEAF     11.0   // a tree's foliage: mottled, shaded by its crown, cut ragged at its edge
#define MAT_ZONE     20.0   // a zone tint over the ground

//  A material is a NUMBER, and a variant is shaded from the whole
//  number beside it.  So a test on a material is a test on the half
//  step around it.
#define MAT_IS(m, M)    ((m) > (M) - 0.5 && (m) < (M) + 0.5)
#define MAT_IN(m, A, B) ((m) > (A) - 0.5 && (m) < (B) + 0.5)
#define MAT_ABOVE(m, M) ((m) > (M) + 0.5)
#define MAT_FROM(m, M)  ((m) > (M) - 0.5)
#define MAT_UPTO(m, M)  ((m) < (M) + 0.5)

//  The materials named at run time are numbered from here.
#define MAT_RUNTIME_BASE 32.0
#define MAT_RUNTIME_MAX  32

//  The most tones the layers of a cut are dealt from.
#define MAT_STRATA_MAX  8
