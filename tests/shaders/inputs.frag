#version 450
// Reads as many interpolated and system inputs as it can, so the hardware fills many input VGPRs in
// every pixel wave. The xfb tests use it (XD_XFB_FRAG=1) to look for pixel waves that write input
// VGPRs outside their own allocation.
layout(location = 0) in vec4 captured;
layout(location = 0) out vec4 color;

void main()
{
   vec4 c = captured + interpolateAtCentroid(captured) + interpolateAtSample(captured, 0);
   color = c * 1e-6 + gl_FragCoord * (1.0 / 256.0) +
           vec4(gl_FrontFacing ? 0.25 : 0.5, float(gl_SampleMaskIn[0] & 1), 0.0, 0.0);
}
