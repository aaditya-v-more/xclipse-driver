#version 450
// xfb_many.vert's triangle grid without transform feedback: each vertex's index goes on to the
// geometry shader (xfb_many.geom), which captures it.
layout(location = 0) flat out uint vertex_index;

const vec2 corner[3] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0));

void main()
{
   uint tri = uint(gl_VertexIndex) / 3u;
   vec2 cell = vec2(tri % 64u, (tri / 64u) % 64u);
   gl_Position = vec4((cell + corner[gl_VertexIndex % 3]) / 32.0 - 1.0, 0.0, 1.0);
   vertex_index = uint(gl_VertexIndex);
}
