#version 450
/*  resolve.vert -- one triangle covering the screen. */
layout(location = 0) out vec2 v_uv;

void main()
{
    vec2 p = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0,
                  (gl_VertexIndex == 2) ? 3.0 : -1.0);
    v_uv        = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
