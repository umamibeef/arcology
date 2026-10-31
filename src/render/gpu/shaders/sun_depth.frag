#version 450
/*  sun_depth.frag -- the sun's shadow map keeps depth alone.  A tree's
 *  card or frond is cut here as the leaf material cuts it, its rough
 *  outline without the fine edge, so its shadow is a crown's and not a
 *  square's. */
layout(location = 0) in vec3 v_cut;

void main()
{
    if (v_cut.z > 0.5)
    {
        float kind = floor(v_cut.y * 0.5) * 2.0 + 1.0;
        vec2  uv   = vec2(v_cut.x - 2.0 * floor(v_cut.x * 0.5), v_cut.y - kind);
        if (kind > 6.0 ? abs(uv.x - 0.5) * 2.0 > 1.0 - uv.y * 0.85 : dot(uv * 2.0 - 1.0, uv * 2.0 - 1.0) > 0.92)
            discard;
    }
}
