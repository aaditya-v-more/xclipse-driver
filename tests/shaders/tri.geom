#version 450
// Passes each triangle through and adds a smaller copy of it, so every input triangle comes out
// as two. The pipeline statistics then have a known geometry-shader output to check.
layout(triangles) in;
layout(triangle_strip, max_vertices = 6) out;

void main()
{
   for (int copy = 0; copy < 2; copy++) {
      for (int i = 0; i < 3; i++) {
         gl_Position = gl_in[i].gl_Position * vec4(vec2(copy == 0 ? 1.0 : 0.5), 1.0, 1.0);
         EmitVertex();
      }
      EndPrimitive();
   }
}
