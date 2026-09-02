#version 450
/*  sprite_water.frag -- a water or shore sprite, its water shaded with
 *  Gerstner waves.
 *
 *  With both flags off this is sprite.frag: the sprite as it is.  With the
 *  shader on, every pixel whose palette entry is water -- the palette
 *  texture's alpha says so -- is shaded as a water surface instead; with
 *  the mesh on, the pixels that are not water, the sand rim, are dropped
 *  so the mesh ground runs up to the water.  The shading: two drifting layers of value noise perturb the
 *  normal, a highlight follows the sun, a small Fresnel term brings in the
 *  sky, all in world coordinates recovered from the tile's diamond, so the
 *  surface runs continuously from tile to tile like the ocean.  The base
 *  colour is the palette's own blue, so the original's tone survives.
 */
layout(location = 0) in vec2  v_tex;
layout(location = 1) in vec2  v_pos;
layout(location = 2) flat in ivec4 v_under; /* diamond sx, sy, col, row     */
layout(location = 3) flat in ivec4 v_flags;

layout(location = 0) out vec4 o_col;

layout(set = 2, binding = 0) uniform usampler2D t_atlas;
layout(set = 2, binding = 1) uniform sampler2D  t_pal;
layout(set = 2, binding = 2) uniform sampler2D  t_shore; /* r: tiles to land / 16, g: depth in levels / 16 */
layout(set = 3, binding = 0) uniform Frag
{
    ivec4 p;   /* transparent index, strip the rim, shade the water, 0 */
    vec4  f;   /* time, tw/2, th/2, 0                                 */
    vec4  sun; /* direction xyz, 0                                    */
} fr;

float hash2(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float vnoise(vec2 p)
{
    vec2  i = floor(p);
    vec2  f = fract(p);
    float a = hash2(i);
    float b = hash2(i + vec2(1.0, 0.0));
    float c = hash2(i + vec2(0.0, 1.0));
    float d = hash2(i + vec2(1.0, 1.0));
    f       = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p)
{
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i)
    {
        v += a * vnoise(p);
        p  = p * 2.03 + vec2(1.7, 9.2);
        a *= 0.5;
    }
    return v;
}

/*  One Gerstner wave, as the classic formulation has it (and the Godot
 *  shader the user pointed at): wave number k = 2 pi / L, phase speed
 *  c = sqrt(g / k), phase f = k (d . p) - c t, amplitude a = S / k where S
 *  is the steepness.  The surface point moves by (d a cos f, a sin f) and
 *  the tangent and binormal are the derivatives of that, so the normal
 *  is analytic.  Only the normal and the height are used: the water is a
 *  decal on a flat tile and is not displaced. */
void gerstner(vec2 d, float L, float S, float t, vec2 p, inout vec3 T,
              inout vec3 B, inout float h, inout float hmax)
{
    const float g = 1.2; /* gravity, in tiles per second squared */
    float k = 6.2831853 / L;
    float c = sqrt(g / k);
    float f = k * (dot(d, p) - c * t);
    float a = S / k;
    float sf = sin(f), cf = cos(f);
    T += vec3(-d.x * d.x * S * sf, -d.x * d.y * S * sf, d.x * S * cf);
    B += vec3(-d.x * d.y * S * sf, -d.y * d.y * S * sf, d.y * S * cf);
    h    += a * sf;
    hmax += a;
}

void main()
{
    uint v = texelFetch(t_atlas, ivec2(floor(v_tex)), 0).r;
    vec4 pc;
    if (v == uint(fr.p.x))
        discard;
    pc = texelFetch(t_pal, ivec2(int(v), 0), 0);
    if (pc.a > 0.5)
    {
        /*  Not water: the sprite's own pixel, index in alpha as ever --
         *  unless the mesh is on, when the rim is dropped and the ground
         *  under the tile shows instead. */
        if (fr.p.y != 0)
            discard;
        o_col = vec4(pc.rgb, float(v) / 255.0);
        return;
    }
    if (fr.p.z == 0)
    {
        /* the water as the sprite paints it */
        o_col = vec4(pc.rgb, float(v) / 255.0);
        return;
    }

    /*  The pixel's place on the tile's plane.  (sx, sy) is the tile's
     *  origin: the sprite's left edge and the row under its diamond, whose
     *  top vertex is th + 0.5 rows above sy at x = sx + tw/2.  Along the
     *  diamond row + col and row - col are linear in y and x. */
    float t   = fr.f.x;
    vec2  d   = v_pos - vec2(float(v_under.x) + fr.f.y,
                             float(v_under.y) - 2.0 * fr.f.z - 0.5);
    float spl = d.y / fr.f.z;                 /* u + v                */
    float dif = d.x / fr.f.y;                 /* v - u                */
    float u   = 0.5 * (spl - dif);
    float w   = 0.5 * (spl + dif);
    vec2  p   = vec2(float(v_under.z) + u, float(v_under.w) + w);

    /*  Four waves at unrelated headings: a long swell, two shorter ones
     *  and a ripple, in tile units. */
    /*  The water field: how far this pixel is from land, in tiles, and
     *  how deep the water is, in levels of the bed ALTM carries.  A city
     *  whose beds are all at the surface still reads as water away from
     *  its banks, so the distance adds a little depth of its own. */
    /*  Tile (col, row) spans p in [col, col + 1); its texel's centre is
     *  (col + 0.5) / 128, so p / 128 lands on it at the tile's middle. */
    vec2  fld   = texture(t_shore, p / 128.0).rg * 16.0;
    float dist  = fld.x;
    float depth = fld.y + min(dist, 6.0) * 0.50; /* as the mesh lowers the bed */
    float shallow = 1.0 - smoothstep(0.0, 1.2, depth); /* 1 over a bed at the surface */

    /*  The surface: a noise height field at three scales, two of them
     *  drifting, differentiated for the normal; the ripples are calmer in
     *  the shallows.  No sinusoids: a handful of them interferes into a
     *  lattice, and water does not. */
    vec2  d1 = vec2(t * 0.06, t * 0.045), d2 = vec2(-t * 0.10, t * 0.07);
    float e  = 0.02;
#define HF(q) (fbm((q) * 1.5) * 0.5 + fbm((q) * 4.5 + d1) * 0.35 + fbm((q) * 12.0 + d2) * 0.15)
    float f0 = HF(p);
    float fx = HF(p + vec2(e, 0.0));
    float fy = HF(p + vec2(0.0, e));
#undef HF
    vec2  slope = vec2(fx - f0, fy - f0) / e * mix(0.10, 0.06, shallow);
    vec3  n     = normalize(vec3(-slope, 1.0));

    /*  The camera of a 2:1 isometric view sits about 27 degrees above
     *  the horizon, toward the south-east; the sun is over its shoulder. */
    vec3  L    = normalize(fr.sun.xyz);
    vec3  V    = normalize(vec3(0.60, 0.60, 0.45));
    vec3  H    = normalize(L + V);
    float nv   = max(dot(n, V), 0.0);
    float ndl  = max(dot(n, L), 0.0);
    float sheen   = pow(max(dot(n, H), 0.0), 16.0);
    float glitter = pow(max(dot(n, H), 0.0), 400.0);
    float F = 0.08 + 0.92 * pow(1.0 - nv, 4.0); /* Schlick, a little exaggerated */

    /*  Colour by depth: a turquoise over the shallows where the bed shows
     *  through, a deep blue-green offshore; the sky reflected on top by
     *  Fresnel, a broad sheen and sparse sun glitter. */
    vec3  deep    = vec3(0.02, 0.16, 0.36);
    vec3  mid     = vec3(0.07, 0.34, 0.54);
    vec3  shoal   = vec3(0.22, 0.60, 0.66);
    vec3  sky     = vec3(0.66, 0.80, 0.93);
    /*  The coast line lies at distance 0.5 in the field, so a bed one
     *  level down at the first water tile reads about 1.3 here and the
     *  open sea two levels down, well offshore, about 3.8. */
    vec3  base    = mix(shoal, mid, smoothstep(0.25, 2.2, depth));
    base          = mix(base, deep, smoothstep(2.2, 4.5, depth));
    base          = mix(base, pc.rgb, 0.08);
    vec3  c       = base * (0.80 + 0.30 * ndl);
    /*  Caustics in the shallows: two drifting noise webs, brightest where
     *  they cross, fading with depth. */
    float caus = vnoise(p * 9.0 + vec2(t * 0.30, t * 0.20)) *
                 vnoise(p * 11.0 - vec2(t * 0.25, -t * 0.35));
    c            += vec3(0.35, 0.45, 0.45) * smoothstep(0.30, 0.60, caus) * shallow * 0.6;
    c             = mix(c, sky, F * 0.8);
    c            += vec3(1.0, 0.98, 0.92) * (sheen * 0.08 + glitter * 0.6);
    /*  Foam: a breaking line just off the land, torn by noise and
     *  breathing with time, and a fainter second line behind it. */
    float edge  = 1.0 - smoothstep(0.5, 0.95, dist); /* the coast line is at 0.5 */
    float foamn = vnoise(p * 6.0 + vec2(t * 0.5, -t * 0.4)) * 0.6 +
                  vnoise(p * 15.0 - vec2(t * 0.3, t * 0.6)) * 0.4;
    float foam  = edge * smoothstep(0.35, 0.75, foamn + 0.25 * sin(t * 1.3 + dist * 6.0));
    float foam2 = (1.0 - smoothstep(0.9, 1.7, dist)) * smoothstep(0.62, 0.85, foamn);
    c            = mix(c, vec3(0.92, 0.96, 1.0), clamp(foam * 0.8 + foam2 * 0.35, 0.0, 1.0));
    o_col  = vec4(clamp(c, 0.0, 1.0), 0.0);
}
