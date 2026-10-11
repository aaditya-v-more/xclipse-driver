#version 450
// Second pass of the mrt test, drawn with additive blending: attachment k gets
// ((k + 1) / 16, (8 - k) / 32, 1 / 16, 1 / 2) added to its color, and keeps its alpha.
layout(location = 0) out vec4 o0;
layout(location = 1) out vec4 o1;
layout(location = 2) out vec4 o2;
layout(location = 3) out vec4 o3;
layout(location = 4) out vec4 o4;
layout(location = 5) out vec4 o5;
layout(location = 6) out vec4 o6;
layout(location = 7) out vec4 o7;

vec4 value(int k) { return vec4((k + 1) / 16.0, (8 - k) / 32.0, 0.0625, 0.5); }

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
