#version 450
// One triangle covering the whole target; each instance is one layer of overdraw.
layout(location = 0) flat out vec4 color;

void main()
{
   vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
   gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
   float k = float(gl_InstanceIndex & 15) / 15.0;
   color = vec4(k, 1.0 - k, 0.5, 0.5);
}
