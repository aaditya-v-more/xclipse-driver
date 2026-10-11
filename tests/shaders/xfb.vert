#version 450
// tri.vert's triangle, plus each vertex's index and instance written out by transform feedback,
// so the host can check exactly what the GPU captured.
const vec2 pos[3] = vec2[](vec2(0.0, -0.8), vec2(0.8, 0.8), vec2(-0.8, 0.8));

layout(location = 0, xfb_buffer = 0, xfb_offset = 0, xfb_stride = 16) out vec4 captured;

void main()
{
   gl_Position = vec4(pos[gl_VertexIndex], 0.0, 1.0);
   captured = vec4(float(gl_VertexIndex), float(gl_InstanceIndex), 1.0, 2.0);
}
