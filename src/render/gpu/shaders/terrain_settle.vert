#version 450
/*  terrain_settle.vert -- terrain.vert, with each chunk's ground eased
 *  from the wave on the sheet to its own heights.  Drawn only while the
 *  land settles, so every frame after that is the plain shader's. */
#define LAND_SETTLE 1
//#include "terrain_body.vert"
