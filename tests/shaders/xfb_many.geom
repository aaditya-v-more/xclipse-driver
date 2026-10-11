#version 450
// Passes each triangle through and captures its vertices as xfb_many.vert does, so the same checks
// cover transform feedback from a geometry shader.
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) flat in uint vertex_index[];
layout(location = 0, xfb_buffer = 0, xfb_offset = 0, xfb_stride = 16) out vec4 captured;
layout(location = 1, xfb_buffer = 1, xfb_offset = 0, xfb_stride = 4) flat out uint captured_index;

void main()
{
   for (int i = 0; i < 3; i++) {
      gl_Position = gl_in[i].gl_Position;
      captured = vec4(float(vertex_index[i]), 0.0, 1.0, 2.0);
      captured_index = vertex_index[i];
      EmitVertex();
   }
   EndPrimitive();
}
