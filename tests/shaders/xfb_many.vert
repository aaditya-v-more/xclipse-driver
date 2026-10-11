#version 450
// A grid of small triangles, each vertex's index captured by transform feedback into two buffers:
// as a float in a vec4 in buffer 0, and as a uint in buffer 1. The host checks that the draw's
// many workgroups wrote the vertices in draw order and that the buffers agree.
layout(location = 0, xfb_buffer = 0, xfb_offset = 0, xfb_stride = 16) out vec4 captured;
layout(location = 1, xfb_buffer = 1, xfb_offset = 0, xfb_stride = 4) flat out uint captured_index;

const vec2 corner[3] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0));

void main()
{
   uint tri = uint(gl_VertexIndex) / 3u;
   vec2 cell = vec2(tri % 64u, (tri / 64u) % 64u);
   gl_Position = vec4((cell + corner[gl_VertexIndex % 3]) / 32.0 - 1.0, 0.0, 1.0);
   captured = vec4(float(gl_VertexIndex), float(gl_InstanceIndex), 1.0, 2.0);
   captured_index = uint(gl_VertexIndex);
}
