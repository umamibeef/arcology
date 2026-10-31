#version 450
/*  sun_depth.vert -- the mesh as the sun sees it, for the sun's shadow
 *  map.  A vertex is a point on the grid: column, row and altitude in
 *  levels.  The sun's frame is three rows (frame.c, sun_frame).  Its first
 *  two give the place across the map and its third how far the point
 *  lies from the sun, from 0 to 1.  The rows hold the tiles a level
 *  stands for, so a vertex goes in as it is.  A tree's foliage passes on
 *  where on its card it lies, so its shadow is cut as the card is. */
layout(location = 0) in vec4 a_pos;   /* col, row, alt, and how far a cliff's lean moved it (mesh/vert.h) */
layout(location = 2) in vec2 a_paint; /* across and along the paint */
layout(location = 4) in vec2 a_aux;   /* the ground's curvature, and the material */

layout(location = 0) out vec3 v_cut; /* across and along a leaf card, and 1 for a card */

layout(set = 1, binding = 0) uniform Sun
{
    vec4 m[3];
}
sun;

//#include "materials.glsl"

void main()
{
    /*  A zone's tint lies on its ground and casts nothing. */
    if (a_aux.y > MAT_ZONE - 0.5 && a_aux.y < MAT_ZONE + 0.25)
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    v_cut       = vec3(a_paint, MAT_IS(a_aux.y, MAT_LEAF) && a_paint.y > 2.5 ? 1.0 : 0.0);
    vec4 w      = vec4(a_pos.xyz, 1.0);
    gl_Position = vec4(dot(sun.m[0], w), dot(sun.m[1], w), dot(sun.m[2], w), 1.0);
}
