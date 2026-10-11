#version 450
// Draws a quad over the whole target at a depth that varies across it: z = p.x + p.y * u + p.z * v,
// with u going from 0 to 1 left to right and v top to bottom.
layout(push_constant) uniform Plane { vec4 p; };
void main()
{
   const vec2 corners[6] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
                                  vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
   vec2 uv = corners[gl_VertexIndex];
   gl_Position = vec4(uv * 2.0 - 1.0, p.x + p.y * uv.x + p.z * uv.y, 1.0);
}
