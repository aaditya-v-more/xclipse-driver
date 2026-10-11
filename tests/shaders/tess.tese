#version 450
// The tessellated triangle covers exactly the patch's triangle.
layout(triangles, equal_spacing, ccw) in;

void main()
{
   gl_Position = gl_TessCoord.x * gl_in[0].gl_Position + gl_TessCoord.y * gl_in[1].gl_Position +
                 gl_TessCoord.z * gl_in[2].gl_Position;
}
