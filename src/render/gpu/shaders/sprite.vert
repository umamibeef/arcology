#version 450
/*  sprite.vert -- one op from the sweep as a screen-aligned quad.  Twelve
 *  vertices per instance, two quads side by side meeting under the art's
 *  middle, expanded from gl_VertexIndex, so there is no vertex buffer:
 *  every attribute is per instance.  Two quads because standing art has two
 *  faces meeting at the corner nearest the camera, and each face recedes a
 *  tile for every tile it runs from that corner.  Positions are canvas
 *  pixels, the same numbers the software rasteriser paints at, and the
 *  camera is the only thing that turns them into clip space -- so at the
 *  four snaps a frame is the software frame, pixel for pixel.  Resource
 *  sets follow SDL_GPU's SPIR-V convention: uniform buffers for the vertex
 *  stage in set 1. */
layout(location = 0) in ivec4 a_dst;   /* canvas x, y, w, h                   */
layout(location = 1) in ivec4 a_src;   /* atlas x, y, flip, stencil (-1 none) */
layout(location = 2) in ivec4 a_under; /* road: atlas x, y, canvas x, y       */
layout(location = 3) in vec4  a_misc;  /* order*256 + alt*4 + standing, road w, road flip, road h */

layout(location = 0) out vec2  v_tex;
layout(location = 1) out vec2  v_pos;
layout(location = 2) flat out ivec4 v_under;
layout(location = 3) flat out ivec4 v_flags; /* stencil, road w, road flip, road h */

layout(set = 1, binding = 0) uniform Cam
{
    vec4 view; /* scroll x, scroll y, 2*scale/target_w, 2*scale/target_h */
    vec4 proj; /* ox + tw/2, oy - th - 0.5, tw/2, th/2                   */
    vec4 alt;  /* alt_step, depth range, cos, sin of the rotation        */
    vec4 rot;  /* free camera (1 or 0), pivot column, pivot row, pitch  */
    vec4 mode; /* the sweep's painter (1) or the camera's depth (0), the painter's slot divisor, the translation D in grid units */
} cam;

void main()
{
    int  vi   = gl_VertexIndex % 6;
    int  side = gl_VertexIndex / 6; /* 0: the left quad, 1: the right */
    vec2 c    = vec2((vi == 1 || vi == 2 || vi == 4) ? 1.0 : 0.0,
                     (vi == 2 || vi == 4 || vi == 5) ? 1.0 : 0.0);
    c.x       = 0.5 * (c.x + float(side));
    vec2 pos  = vec2(a_dst.xy) + c * vec2(a_dst.zw);
    /*  The sprite's place in the world: the middle of its footprint, its
     *  tile's altitude, and the corner of its block nearest the camera,
     *  all turned with the camera.  The depth below needs them at every
     *  camera; the position needs them only off the original's own. */
    float n   = max(1.0, floor(float(a_dst.z) / (2.0 * cam.proj.z) + 0.5));
    vec2  fc  = vec2(a_under.zw) + vec2(0.5, 0.5) + (n - 1.0) * vec2(-0.5, 0.5);
    float order = floor(a_misc.x / 256.0);       /* the op's place in the sweep: its painter's slot */
    float rem   = a_misc.x - order * 256.0;
    float alt   = floor(rem / 4.0);               /* the tile's altitude in levels; misc.y is a stencilled car's road width */
    float standing = rem - 4.0 * alt;             /* 1: art that stands; 0: art that lies flat */
    float sp  = sin(cam.rot.w), cp = cos(cam.rot.w);
    vec2  t0  = fc - cam.rot.yz;
    vec2  t1  = vec2(t0.x * cam.alt.z - t0.y * cam.alt.w,
                     t0.x * cam.alt.w + t0.y * cam.alt.z) + cam.rot.yz + cam.mode.zw; /* the turn left from the sweep's quarter, and the translation onto the mesh's turn (frame.c sprite_turn) */
    float front = (n - 1.0) * 0.5 *
                  (abs(cam.alt.z + cam.alt.w) + abs(cam.alt.z - cam.alt.w));
    float depth;
    if (cam.rot.x > 0.5)
    {
        /*  Off the original's own camera the sprite cannot turn, so it
         *  follows its tile: the original shows the same art at every one
         *  of its four rotations, and so do we.  What it follows is the
         *  middle of its FOOTPRINT, not the tile it was emitted at.  A
         *  building of n by n tiles is emitted at one corner of its block
         *  -- the tile at the greatest column and the least row, where the
         *  art's top-left lands -- and its sprite is n tiles wide, so n
         *  falls out of the art's own width.  Following the corner instead
         *  is what walked every large building one or two tiles off its
         *  block as soon as the camera turned.  The tile's altitude rides
         *  in misc.y, so the move is the whole camera's and not just its
         *  yaw: at any pitch the sprite lands where its tile lands, which
         *  is what lets the map view keep the landmarks on it. */
        vec2  was = vec2((fc.y - fc.x) * cam.proj.z,
                         (fc.x + fc.y) * cam.proj.w - alt * cam.alt.x);
        vec2  now = vec2((t1.y - t1.x) * cam.proj.z,
                         (t1.x + t1.y) * cam.proj.w * (sp / 0.5) -
                             alt * cam.alt.x * (cp / 0.8660254));
        /*  Looking down, the art is too big for the tile it stands on: a
         *  tile's diamond is a full tile wide in the game's own view, and
         *  the same tile is a square only 1/sqrt(2) as wide once the camera
         *  is overhead and turned square.  One scale therefore fits every
         *  one of them, whatever its footprint, and it is taken about the
         *  middle of the footprint -- the middle of the art's own width,
         *  half a tile below the diamond's row -- so the art stays on its
         *  block while it shrinks.  It comes on with the tints as the
         *  camera rises. */
        float fit = mix(1.0, 0.70710678, smoothstep(0.7071, 0.9700, sp));
        vec2  mid = vec2(float(a_dst.x) + float(a_dst.z) * 0.5,
                         float(a_under.y) + cam.proj.w);
        pos = mid + (now - was) + (pos - mid) * fit;
    }
    if (cam.mode.x > 0.5)
    {
        /*  The sweep's own compositing, for the original's art drawn
         *  alone at the original's own camera: a sprite sits at the top
         *  of its tile's painter's slot and the tiles order each other by
         *  the slot, as the sweep drew them.  That is the original's
         *  algorithm and its frame is the original's, pixel for pixel;
         *  the art is a height field no quad can stand in for. */
        depth = 1.0 - (order + 0.999) / cam.mode.y;
    }
    else
    /*  The depth is the camera's, the measure terrain.vert gives the mesh:
     *  distance along the way the camera looks, larger nearer, and it is
     *  WRITTEN, so the art occludes the mesh and each other where it really
     *  stands.  What places a sprite is not its middle but the corner of
     *  its block NEAREST the camera: its art reaches down the screen to
     *  that corner, and the sweep itself emits a building at that tile.
     *  Turned, the nearest corner is a different one of the four, so the
     *  offset turns with the camera: over the corners (+-(n-1)/2,
     *  +-(n-1)/2) the turned row plus column is largest at (|cos+sin| +
     *  |cos-sin|)(n-1)/2, which is n-1 unturned, the frontmost tile as the
     *  sweep has it.  A twentieth of a tile nearer again, so the sprite
     *  wins over its own ground and stays behind the tile in front; flat
     *  art over the mesh, a levelled pad, a fifth.
     *
     *  Standing art -- a building, a tree, a car, a bridge -- is two faces
     *  rising from that corner, one to each side: each row of it is a point
     *  as many levels up as the rows above its bottom, and each column a
     *  point as many tiles back along its face as it lies from the corner,
     *  half a tile's width of art being a tile of face, so a tall building
     *  behind a low one shows over it exactly where it would, a hill hides
     *  what it really hides, and a wide building's far corner is as far as
     *  it is (a theatre's sign had covered the office in front of it).
     *  Flat art -- the ground, the water, the underground lattice, a
     *  silhouette -- lies down instead: its rows above the bottom recede
     *  along the ground.  The sweep's own order survives as the tie between
     *  equals, the last term. */
    {
        float rows  = (1.0 - c.y) * float(a_dst.w);
        float aside = abs(c.x - 0.5) * float(a_dst.z) / cam.proj.z; /* tiles along the face from the corner */
        float rise  = mix(-rows / (cam.proj.w * (sp / 0.5)) * cp,
                          rows / cam.alt.x * 0.8660254 * sp - aside * cp, standing);
        float ahead = mix(1.15, 1.05, standing);
        float near  = (t1.x + t1.y + front + ahead) * cp + alt * 0.8660254 * sp + rise +
                      (t1.y - t1.x) * 0.0015;
        depth = 1.0 - (near + 300.0) / cam.alt.y;
    }
    /*  A mirrored sprite reads its row backwards: the pixel at i takes
     *  texel w-1-i, which is what the interpolated coordinate floors to. */
    float u = (a_src.z != 0) ? (1.0 - c.x) : c.x;
    v_tex   = vec2(a_src.xy) + vec2(u * float(a_dst.z), c.y * float(a_dst.w));
    v_pos   = pos;
    v_under = a_under;
    v_flags = ivec4(a_src.w, int(a_misc.y), int(a_misc.z), int(a_misc.w));
    vec2 ndc = (pos - cam.view.xy) * cam.view.zw - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, depth, 1.0);
}
