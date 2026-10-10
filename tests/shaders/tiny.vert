#version 450
// A few-pixel triangle placed by push constants, so each draw costs the CPU and GPU almost
// nothing but its own overhead.
layout(push_constant) uniform Push { vec4 offset; } pc;
layout(location = 0) flat out vec4 color;
const vec2 pos[3] = vec2[](vec2(0.0, 0.0), vec2(0.02, 0.0), vec2(0.0, 0.02));

void main()
{
   gl_Position = vec4(pos[gl_VertexIndex] + pc.offset.xy, 0.0, 1.0);
   color = vec4(pc.offset.zw, 0.0, 1.0);
}
