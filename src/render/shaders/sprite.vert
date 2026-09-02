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
    vec4 rot;  /* rotating (1 or 0), pivot column, pivot row, 0         */
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
        /*  The free rotation: the sprite cannot turn, so it follows its
         *  tile.  The tile's centre turns about the pivot as the mesh
         *  does, and the sprite moves by the same canvas offset,
         *  unturned -- the original shows the same art at every one of
         *  its four rotations.  Its depth is the mesh's rotated depth at
         *  the tile's centre, three quarters of a tile nearer, so it
         *  stands above its own ground and behind the tiles in front. */
        vec2 t0 = vec2(a_under.zw) + 0.5 - cam.rot.yz;
        vec2 t1 = vec2(t0.x * cam.alt.z - t0.y * cam.alt.w,
                       t0.x * cam.alt.w + t0.y * cam.alt.z) + cam.rot.yz;
        t0 += cam.rot.yz;
        pos += vec2(((t1.y - t1.x) - (t0.y - t0.x)) * cam.proj.z,
                    ((t1.x + t1.y) - (t0.x + t0.y)) * cam.proj.w);
        depth = 1.0 - (t1.x + t1.y + 0.75 + 300.0) / 800.0;
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
