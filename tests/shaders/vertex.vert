#version 450
// Reads one attribute in each of 16 vertex formats D3D11 games use and stores what it read, for
// the host to check against the bytes it wrote. The host draws each way of setting up vertex input
// as its own instance of 64 vertices; attribute k of vertex v of instance i goes to
// o[(i * 64 + v) * 16 + k].
layout(location = 0) in vec4 a0;    // R32G32B32_SFLOAT
layout(location = 1) in vec4 a1;    // R32G32_SFLOAT
layout(location = 2) in vec4 a2;    // R16G16B16A16_SFLOAT
layout(location = 3) in vec4 a3;    // R16G16_SFLOAT
layout(location = 4) in vec4 a4;    // R8G8B8A8_UNORM
layout(location = 5) in vec4 a5;    // R8G8B8A8_SNORM
layout(location = 6) in uvec4 a6;   // R8G8B8A8_UINT
layout(location = 7) in vec4 a7;    // B8G8R8A8_UNORM
layout(location = 8) in vec4 a8;    // R16G16B16A16_SNORM
layout(location = 9) in vec4 a9;    // R16G16_SNORM
layout(location = 10) in vec4 a10;  // R16G16_UNORM
layout(location = 11) in vec4 a11;  // A2B10G10R10_UNORM_PACK32
layout(location = 12) in uvec4 a12; // R32_UINT
layout(location = 13) in uvec4 a13; // R16G16B16A16_UINT
layout(location = 14) in ivec4 a14; // R16G16B16A16_SINT
layout(location = 15) in vec4 a15;  // R16G16B16A16_UNORM
layout(std430, binding = 0) buffer Out { vec4 o[]; };

void main()
{
   int b = (gl_InstanceIndex * 64 + gl_VertexIndex) * 16;
   o[b + 0] = a0;
   o[b + 1] = a1;
   o[b + 2] = a2;
   o[b + 3] = a3;
   o[b + 4] = a4;
   o[b + 5] = a5;
   o[b + 6] = vec4(a6);
   o[b + 7] = a7;
   o[b + 8] = a8;
   o[b + 9] = a9;
   o[b + 10] = a10;
   o[b + 11] = a11;
   o[b + 12] = vec4(a12);
   o[b + 13] = vec4(a13);
   o[b + 14] = vec4(a14);
   o[b + 15] = a15;
   gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
   gl_PointSize = 1.0;
}
