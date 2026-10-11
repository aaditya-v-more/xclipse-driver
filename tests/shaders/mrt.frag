#version 450
// First pass of the mrt test: a different value in each of 8 color attachments, varying with the
// pixel. Channel i of attachment k at pixel (x, y) is ((x * (k + 1) + y * (2k + 3) + 40i) & 255) / 255.
layout(location = 0) out vec4 o0;
layout(location = 1) out vec4 o1;
layout(location = 2) out vec4 o2;
layout(location = 3) out vec4 o3;
layout(location = 4) out vec4 o4;
layout(location = 5) out vec4 o5;
layout(location = 6) out vec4 o6;
layout(location = 7) out vec4 o7;

vec4 value(int k)
{
   ivec2 p = ivec2(gl_FragCoord.xy);
   ivec4 v = (p.x * (k + 1) + p.y * (2 * k + 3) + ivec4(0, 40, 80, 120)) & 255;
   return vec4(v) / 255.0;
}

void main()
{
   o0 = value(0);
   o1 = value(1);
   o2 = value(2);
   o3 = value(3);
   o4 = value(4);
   o5 = value(5);
   o6 = value(6);
   o7 = value(7);
}
