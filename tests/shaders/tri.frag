#version 450
// Solid orange, so the host can check pixels exactly (within rounding).
layout(location = 0) out vec4 color;

void main()
{
   color = vec4(1.0, 0.5, 0.0, 1.0);
}
