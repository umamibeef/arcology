#version 450
/*  sprite.vert -- one op from the sweep as a screen-aligned quad.
 *
 *  Six vertices per instance, expanded from gl_VertexIndex, so there is no
 *  vertex buffer: every attribute is per instance.  Positions are canvas
 *  pixels, the same numbers the software rasteriser paints at, and the
 *  camera is the only thing that turns them into clip space -- so at the
 *  four snaps a frame is the software frame, pixel for pixel.
 *
 *  Resource sets follow SDL_GPU's SPIR-V convention: uniform buffers for
 *  the vertex stage in set 1.
 */
layout(location = 0) in ivec4 a_dst;   /* canvas x, y, w, h                   */
layout(location = 1) in ivec4 a_src;   /* atlas x, y, flip, stencil (-1 none) */
layout(location = 2) in ivec4 a_under; /* road: atlas x, y, canvas x, y       */
layout(location = 3) in vec4  a_misc;  /* depth, road w, road flip, road h    */

layout(location = 0) out vec2  v_tex;
layout(location = 1) out vec2  v_pos;
layout(location = 2) flat out ivec4 v_under;
layout(location = 3) flat out ivec4 v_flags; /* stencil, road w, road flip, road h */

layout(set = 1, binding = 0) uniform Cam
{
    vec4 view; /* scroll x, scroll y, 2*scale/target_w, 2*scale/target_h */
    vec4 proj; /* ox + tw/2, oy - th - 0.5, tw/2, th/2                   */
    vec4 alt;  /* alt_step, depth divisor, cos, sin of the rotation     */
    vec4 rot;  /* free camera (1 or 0), pivot column, pivot row, pitch  */
} cam;

void main()
{
    int  vi = gl_VertexIndex;
    vec2 c  = vec2((vi == 1 || vi == 2 || vi == 4) ? 1.0 : 0.0,
                   (vi == 2 || vi == 4 || vi == 5) ? 1.0 : 0.0);
    vec2 pos = vec2(a_dst.xy) + c * vec2(a_dst.zw);
    float depth = a_misc.x;
    if (cam.rot.x > 0.5)
    {
        /*  Off the original's own camera the sprite cannot turn, so it
         *  follows its tile: the original shows the same art at every one
         *  of its four rotations, and so do we.
         *
         *  What it follows is the middle of its FOOTPRINT, not the tile
         *  it was emitted at.  A building of n by n tiles is emitted at
         *  one corner of its block -- the tile at the greatest column and
         *  the least row, where the art's top-left lands -- and its
         *  sprite is n tiles wide, so n falls out of the art's own width.
         *  Following the corner instead is what walked every large
         *  building one or two tiles off its block as soon as the camera
         *  turned (the user: "your sprite rotations are pretty buggy
         *  right now", then "you still haven't fixed the sprites in the
         *  other orientations").
         *
         *  The tile's altitude rides in misc.y, so the move is the whole
         *  camera's and not just its yaw: at any pitch the sprite lands
         *  where its tile lands, which is what lets the map view keep the
         *  landmarks on it. */
        float n   = max(1.0, floor(float(a_dst.z) / (2.0 * cam.proj.z) + 0.5));
        vec2  fc  = vec2(a_under.zw) + vec2(0.5, 0.5) + (n - 1.0) * vec2(-0.5, 0.5);
        float alt = a_misc.y;
        float sp  = sin(cam.rot.w), cp = cos(cam.rot.w);
        vec2  t0  = fc - cam.rot.yz;
        vec2  t1  = vec2(t0.x * cam.alt.z - t0.y * cam.alt.w,
                         t0.x * cam.alt.w + t0.y * cam.alt.z) + cam.rot.yz;
        vec2  was = vec2((fc.y - fc.x) * cam.proj.z,
                         (fc.x + fc.y) * cam.proj.w - alt * cam.alt.x);
        vec2  now = vec2((t1.y - t1.x) * cam.proj.z,
                         (t1.x + t1.y) * cam.proj.w * (sp / 0.5) -
                             alt * cam.alt.x * (cp / 0.8660254));
        /*  Looking down, the art is too big for the tile it stands on: a
         *  tile's diamond is a full tile wide in the game's own view, and
         *  the same tile is a square only 1/sqrt(2) as wide once the
         *  camera is overhead and turned square.  One scale therefore
         *  fits every one of them, whatever its footprint (the user:
         *  "Please resize the sprite labels so they fit inside of their
         *  boxes, preferrably there's a single scale that would work for
         *  both"), and it is taken about the middle of the footprint --
         *  the middle of the art's own width, half a tile below the
         *  diamond's row -- so the art stays on its block while it
         *  shrinks.  It comes on with the tints as the camera rises. */
        float fit = mix(1.0, 0.70710678, smoothstep(0.7071, 0.9700, sp));
        vec2  mid = vec2(float(a_dst.x) + float(a_dst.z) * 0.5,
                         float(a_under.y) + cam.proj.w);
        pos = mid + (now - was) + (pos - mid) * fit;
        /*  What orders a sprite is not its middle but the corner of its
         *  block NEAREST the camera: its art reaches down the screen to
         *  that corner, and the sweep itself emits a building at that
         *  tile, which is what puts it in front of its neighbours.  Turned,
         *  the nearest corner is a different one of the four, so the
         *  offset has to turn with the camera or a big building sorts as
         *  though it stood at its centre and the wrong one of a pair wins
         *  (the user: "The sprites also have the wrong overlap too when
         *  rotated").  Over the corners (+-(n-1)/2, +-(n-1)/2) the turned
         *  row plus column is largest at (|cos+sin| + |cos-sin|)(n-1)/2,
         *  which is n-1 unturned: the frontmost tile, as the sweep has it.
         *
         *  Three quarters of a tile nearer again, so the sprite stands
         *  above its own ground and behind the tiles in front of it. */
        float front = (n - 1.0) * 0.5 *
                      (abs(cam.alt.z + cam.alt.w) + abs(cam.alt.z - cam.alt.w));
        depth = 1.0 - ((t1.x + t1.y + front) * (cp / 0.8660254) + 1.05 +
                       alt * (sp - 0.5) + (t1.y - t1.x) * 0.0015 +
                       300.0) / 800.0;
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
