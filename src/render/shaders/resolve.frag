#version 450
/*  resolve.frag -- the frame target onto the window.
 *
 *  Nearest-neighbour by an integer pixel scale, so pixel art stays pixel
 *  art on a high-density display.  Outside the target the sky shows.  The
 *  shadow mask is applied here, by $19B76's rule: only a destination in
 *  the dirt ramp darkens -- 79 becomes 84, 100..110 become 110 -- and the
 *  index is read back out of the alpha channel the sprite pass wrote.  A
 *  lit surface from the mesh path carries no palette colour, so it is
 *  darkened multiplicatively instead.
 */
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_col;

layout(set = 2, binding = 0) uniform sampler2D t_col;
layout(set = 2, binding = 1) uniform sampler2D t_shadow;
layout(set = 2, binding = 2) uniform sampler2D t_pal;
layout(set = 3, binding = 0) uniform Frag
{
    vec4 screen; /* screen w, screen h, pixel scale, 0 */
    vec4 sky;
} fr;

void main()
{
    vec2  sc = vec2(v_uv.x, 1.0 - v_uv.y) * fr.screen.xy;
    float f  = fr.screen.z;
    ivec2 tp = ivec2(floor(sc / f));
    ivec2 ts = textureSize(t_col, 0);
    vec4  c;
    int   idx;

    if (tp.x < 0 || tp.y < 0 || tp.x >= ts.x || tp.y >= ts.y)
    {
        o_col = vec4(fr.sky.rgb, 1.0);
        return;
    }
    c   = texelFetch(t_col, tp, 0);
    idx = int(round(c.a * 255.0));
    if (abs(f - round(f)) > 0.001)
    {
        /*  A fractional scale: the brief's texel-boundary filter (D4).
         *  Texel interiors stay exactly as drawn; within one screen pixel
         *  of a texel edge the two texels ramp, by exactly as much as the
         *  scale demands, so pixel art neither shimmers nor blurs. */
        vec2 pix = sc / f - 0.5;
        vec2 fx  = fract(pix);
        vec2 fw  = vec2(1.0 / f);
        fx       = clamp((fx - 0.5) / max(fw, vec2(1e-5)) + 0.5, 0.0, 1.0);
        vec2 uv  = (floor(pix) + 0.5 + fx) / vec2(ts);
        c.rgb    = texture(t_col, uv).rgb;
    }
    if (texelFetch(t_shadow, tp, 0).r > 0.5 &&
        (idx == 79 || (idx >= 100 && idx <= 110)))
    {
        int  d  = (idx == 79) ? 84 : 110;
        vec3 pc = texelFetch(t_pal, ivec2(idx, 0), 0).rgb;
        if (all(lessThan(abs(c.rgb - pc), vec3(0.003))))
            c.rgb = texelFetch(t_pal, ivec2(d, 0), 0).rgb;
        else
            c.rgb *= 0.6;
    }
    o_col = vec4(c.rgb, 1.0);
}
