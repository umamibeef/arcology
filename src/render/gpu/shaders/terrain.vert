#version 450
/*  terrain.vert -- the terrain as geometry.  A vertex is a point on the
 *  grid: column, row and altitude in levels.  The projection is the
 *  original's, stated as numbers rather than derived: canvas x = ox + (row
 *  - col) * tw/2, canvas y = oy + (col + row) * th/2 minus alt_step per
 *  level, where alt_step is 0.75 of the tile height, the art's own choice.
 *  A tile corner therefore lands exactly where the sprite renderer put it,
 *  and the sprites can be superimposed as they are.  The depth is the
 *  camera's own: how far the point lies along the way it looks, the same
 *  measure sprite.vert gives the art, so the frame composes by where things
 *  are and not by which tile they were drawn from. */
layout(location = 0) in vec4 a_pos; /* col, row, alt, the tile's painter's index (unused here) */
layout(location = 1) in vec4 a_nrm; /* normal in world units, 0           */
layout(location = 2) in vec4 a_col; /* base colour, alpha = index / 255   */

layout(location = 0) out vec3 v_nrm;
layout(location = 1) out vec4 v_col;
layout(location = 2) out vec3 v_wpos;
layout(location = 3) out float v_curv;

layout(set = 1, binding = 0) uniform Cam
{
    vec4 view; /* scroll x, scroll y, 2*scale/target_w, 2*scale/target_h */
    vec4 proj; /* ox + tw/2, oy, tw/2, th/2                             */
    vec4 alt;  /* alt_step, depth range, cos, sin of the rotation        */
    vec4 rot;  /* free camera (1 or 0), pivot column, pivot row, pitch  */
    vec4 mode; /* the sweep's painter (1) or the camera's depth (0): the mesh is never drawn under the painter */
} cam;

void main()
{
    /*  The free rotation, for inspection: the grid turns about the pivot,
     *  the point under the view's centre, before the projection, so the
     *  same oblique camera sees the terrain from any side and what the
     *  view looks at stays where it is. */
    vec2 cr = a_pos.xy - cam.rot.yz;
    vec2 rr = vec2(cr.x * cam.alt.z - cr.y * cam.alt.w,
                   cr.x * cam.alt.w + cr.y * cam.alt.z) + cam.rot.yz;
    /*  The camera's pitch, in radians: how far it has been raised from the
     *  original's own view towards the zenith.  The original is 30 degrees
     *  -- a tile's diamond is twice as wide as it is tall, so the ground's
     *  foreshortening is sin 30 -- and the map view is the same camera
     *  raised to 90: the ground stops being foreshortened and height stops
     *  shifting a point up the screen.  One camera, one projection: at 30
     *  every number below is the original's. */
    float sp     = sin(cam.rot.w);
    float cp     = cos(cam.rot.w);
    vec2  canvas = vec2(cam.proj.x + (rr.y - rr.x) * cam.proj.z,
                        cam.proj.y + (rr.x + rr.y) * cam.proj.w * (sp / 0.5) -
                            a_pos.z * cam.alt.x * (cp / 0.8660254));
    v_nrm  = a_nrm.xyz;
    v_col  = a_col;
    v_wpos = a_pos.xyz;
    v_curv = a_nrm.w;
    vec2 ndc    = (canvas - cam.view.xy) * cam.view.zw - 1.0;
    /*  The depth is the camera's: how far the point lies along the way the
     *  camera looks, the turned column plus row into the screen at cos
     *  pitch and the height toward it at sin pitch, a level being 0.866 of
     *  a tile's diagonal step.  Larger is nearer.  At 30 degrees it is the
     *  isometric view's own distance; straight down it is the height alone.
     *  The sprites carry the same measure and write it too, so everything
     *  composes by where it stands and not by the tile it was drawn from --
     *  a deck over a road, a lane descending beside a deck, a column, an
     *  arc cutting a corner.  The last term breaks the ties: everything on
     *  one diagonal is the same distance from an oblique camera and the
     *  sweep orders those by row; a four hundredth of a tile per step
     *  across the diagonal is the sweep's own order and nothing more. */
    float near  = (rr.x + rr.y) * cp + a_pos.z * 0.8660254 * sp + (rr.y - rr.x) * 0.0015;
    float depth = 1.0 - (near + 300.0) / cam.alt.y;
    /*  A road strip lies on its tile's ground, a lane on a deck, a marking
     *  on a lane: nearly coplanar, and a strip can lie a hair under the
     *  grass between the tile's corners, so they take a bias by material
     *  to win over what they lie on, a third of a tile at most, cars over
     *  roads over ground; the free camera's own numbers.  Less than a
     *  fiftieth of a tile and the grass takes a stretch of asphalt.  The
     *  seabed under a water body is meant to be seen through the surface:
     *  a hair further, since a canal's bed can lie at its table (mesh.c,
     *  rule 4: "a quarter step behind the tile"). */
    if (a_col.z > 3.5 && a_col.z < 4.5)
        depth += 0.05 / cam.alt.y;
    if (a_col.z > 6.5)
        depth -= (0.3 / cam.alt.y) * (a_col.z > 20.25 ? 0.0 : a_col.z > 19.15 ? 1.1 : a_col.z > 18.5 ? 1.0 : a_col.z > 17.5 ? 0.3 : a_col.z > 16.5 ? 1.0 : a_col.z > 15.5 ? 1.3 : a_col.z > 14.5 ? 2.0 : a_col.z > 13.5 ? 1.5 : a_col.z > 12.5 ? 0.5 : a_col.z > 10.5 && a_col.z < 11.5 ? 1.2 : 1.0); /* a railway a shade nearer than the map's zone tint (1.1): a turnout's curve over a zoned corner shows */
    /*  A zone tint belongs to the map view: looking obliquely it would
     *  lie over the world like a stain, so below 45 degrees it is thrown
     *  outside the frustum instead of drawn, and from there it fades in
     *  as the camera rises, so the map's colours arrive with the map
     *  rather than snapping on at the end of the move.  The fade rides in
     *  col.g, which a tint quad does not otherwise use. */
    if (a_col.z > 19.5 && a_col.z < 20.25)
    {
        float fade = smoothstep(0.7071, 0.9700, sp);
        if (fade <= 0.0)
        {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            return;
        }
        v_col.g = fade;
    }
    gl_Position = vec4(ndc.x, -ndc.y, depth, 1.0);
}
