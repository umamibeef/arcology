//  GENERATED from src/render/mesh/materials.def.  Do not edit.
//
//  The shader reads a material by RANGE, so it needs the same numbers
//  and the same membership tests the C does.  gen_shaders.py splices
//  this file in where a shader asks for it.

#define MAT_GROUND    0.0    // the terrain's own surface
#define MAT_ENG_WALL  1.0    // an engineered wall: a retaining face
#define MAT_SEDIMENT  2.0    // a river bed's layers
#define MAT_WATER     3.0    // standing or flowing water
#define MAT_SEABED    4.0    // the floor under water
#define MAT_EARTH     5.0    // a cut face through soil
#define MAT_SURFACE   6.0    // a leveled pad
#define MAT_LINE      7.0    // a line's strip on the surface
#define MAT_PROP      8.0    // street furniture: a pole, a sign, a box
#define MAT_LAMP      9.0    // a lamp's or a signal's lit face
#define MAT_ZEBRA     10.0   // a stripe's painted bands
#define MAT_THREAD    11.0   // a family's own threads and their bed
#define MAT_SKIRT     12.0   // a vehicle's clipped skirt
#define MAT_WALK      13.0   // a margin
#define MAT_THREAD_X  14.0   // a lap's panel bed
#define MAT_VEHICLE   15.0   // a car's body
#define MAT_XPANEL    16.0   // a lap's panel
#define MAT_XAPPROACH 17.0   // a lap's approach markings
#define MAT_PIER      18.0   // a raised band's bent and its cast concrete
#define MAT_BAND      19.0   // a raised band's slab
#define MAT_BAND_LANE 19.3   // a spur's lane, drawn as the band's outer lane
#define MAT_ZONE      20.0   // a zone tint over the ground
#define MAT_HILITE    20.5   // the pointer's highlight

//  A material is a NUMBER, and a variant is shaded from the whole
//  number beside it.  So a test on a material is a test on the half
//  step around it.
#define MAT_IS(m, M)    ((m) > (M) - 0.5 && (m) < (M) + 0.5)
#define MAT_IN(m, A, B) ((m) > (A) - 0.5 && (m) < (B) + 0.5)
#define MAT_ABOVE(m, M) ((m) > (M) + 0.5)
#define MAT_FROM(m, M)  ((m) > (M) - 0.5)
#define MAT_UPTO(m, M)  ((m) < (M) + 0.5)

//  A script's own materials are numbered from here.
#define MAT_SCRIPT_BASE 32.0
#define MAT_SCRIPT_MAX  32
