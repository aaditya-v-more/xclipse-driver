#version 450
// grid.vert's 4096 small triangles, with each position stored to and loaded from a 1 KiB local
// array at indices only known at run time. An array that size, indexed that way, has to live in
// scratch memory.
const vec2 corner[3] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0));

void main()
{
   uint tri = uint(gl_VertexIndex) / 3u;
   vec2 cell = vec2(tri % 64u, (tri / 64u) % 64u);
   vec4 v[64];
   for (int i = 0; i < 64; i++)
      v[i] = vec4(float(i + gl_VertexIndex));
   int slot = (gl_VertexIndex * 7 + 3) & 63;
   v[slot] = vec4((cell + corner[gl_VertexIndex % 3]) / 32.0 - 1.0, 0.0, 1.0);
   // gl_InstanceIndex is 0, but the compiler can't know that, so this load reads memory.
   gl_Position = v[(slot + gl_InstanceIndex) & 63];
}
