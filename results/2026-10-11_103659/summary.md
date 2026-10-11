# Test run 2026-10-11T10:36:59

- Driver: `driver/` at `8acb984`, `vulkan.radeon.so` SHA-256 `0c6836773295dc6e`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 33.0 °C at the start

| Test | stock | radv |
|---|---|---|
| info | pass (instance 144.9 ms) | pass (instance 3.1 ms) |
| device | pass (device 1.3 ms) | pass (device 1.9 ms) |
| submit | pass (empty_cmdbuf 0.6 ms) | pass (empty_cmdbuf 0.6 ms) |
| compute | pass (gpu 1.0 ms) | pass (gpu 1.2 ms) |
| render | pass (gpu 0.8 ms) | pass (gpu 0.9 ms) |
| queries | pass (gpu 0.8 ms) | pass (gpu 1.5 ms) |
| xfb | pass (gpu 0.8 ms) | pass (gpu 1.4 ms) |
| xfbmany | pass (gpu 2.0 ms) | pass (gpu 1.5 ms) |
| xfbmanygs | pass (gpu 1.3 ms) | pass (gpu 1.9 ms) |
| alu | pass (gpu 28.4 ms) | pass (gpu 28.4 ms) |
| bandwidth | pass (device 1.4 ms) | pass (device 1.4 ms) |
| fill | pass (device 1.4 ms) | pass (device 1.6 ms) |
| draws | pass (device 1.4 ms) | pass (device 1.7 ms) |
| compile | pass (device 1.3 ms) | pass (device 1.5 ms) |

## What each driver exposes

| | stock | radv |
|---|---|---|
| GPU | Samsung Xclipse 940 | AMD Radeon Graphics (RADV TITAN) |
| Vulkan | 1.3.279 | 1.4.358 |
| Driver | Samsung Proprietary driver | radv |
| Driver info | Driver version: 24.0.534, git hash: 1900168dcb  | Mesa 26.3.0-devel |
| Vendor ID | 0x144d | 0x144d |
| Subgroup size | 64 | 64 |
| Device extensions | 154 | 224 |
| textureCompressionBC | no | yes |
| shaderFloat64 | no | yes |
| samplerFilterMinmax | no | yes |
| geometryShader | yes | yes |
| tessellationShader | yes | yes |
| depthBounds | yes | yes |
| dualSrcBlend | yes | yes |
| logicOp | yes | yes |
| multiViewport | yes | yes |
| pipelineStatisticsQuery | yes | yes |
| shaderCullDistance | yes | yes |
| fillModeNonSolid | yes | yes |
| sample BC1_RGBA_UNORM | yes | yes |
| sample BC3_UNORM | yes | yes |
| sample BC5_UNORM | no | yes |
| sample BC7_UNORM | no | yes |
| VK_KHR_maintenance5 | no | yes |
| VK_KHR_maintenance6 | no | yes |
| VK_EXT_mutable_descriptor_type | no | yes |
| VK_EXT_transform_feedback | yes | yes |
| VK_EXT_graphics_pipeline_library | yes | yes |
| VK_EXT_robustness2 | yes | yes |
| VK_KHR_robustness2 | no | yes |
| VK_EXT_custom_border_color | yes | yes |
| VK_EXT_extended_dynamic_state3 | yes | yes |
| VK_EXT_descriptor_buffer | yes | yes |
| VK_EXT_shader_module_identifier | yes | yes |
| VK_EXT_depth_clip_enable | yes | yes |
| VK_EXT_line_rasterization | yes | yes |
| VK_EXT_vertex_attribute_divisor | yes | yes |
| VK_KHR_vertex_attribute_divisor | no | yes |
| VK_EXT_attachment_feedback_loop_layout | yes | yes |
| VK_EXT_non_seamless_cube_map | yes | yes |
| VK_EXT_memory_priority | no | yes |
| VK_EXT_memory_budget | yes | yes |
| VK_EXT_conservative_rasterization | yes | yes |
| VK_EXT_fragment_shader_interlock | no | yes |
| VK_EXT_shader_stencil_export | yes | yes |
| VK_EXT_image_view_min_lod | yes | yes |
| VK_EXT_depth_bias_control | no | yes |
| VK_KHR_fragment_shading_rate | yes | yes |
| VK_EXT_mesh_shader | no | yes |
| VK_KHR_ray_tracing_pipeline | yes | yes |
| VK_KHR_ray_query | yes | yes |

## Performance

Median of 7 runs after at least 250 ms of warm-up, timed with GPU timestamps.

| Metric | stock | radv | radv vs stock |
|---|---|---|---|
| FP32 throughput, GFLOPS | 2.42e+03 | 2.42e+03 | 1.00x worse |
| Shader copy, GB/s | 35.8 | 46.1 | 1.29x better |
| vkCmdCopyBuffer, GB/s | 36.1 | 46.4 | 1.28x better |
| Opaque fill at 1080p, Gpixel/s | 25.5 | 25.5 | 1.00x worse |
| Blended fill at 1080p, Gpixel/s | 12.8 | 12.8 | 1.00x worse |
| CPU per draw, µs | 0.162 | 0.264 | 0.61x worse |
| CPU per draw with a pipeline switch, µs | 0.513 | 0.551 | 0.93x worse |
| GPU per draw, µs | 0.14 | 0.273 | 0.51x worse |
| GPU per draw with a pipeline switch, µs | 0.359 | 0.363 | 0.99x worse |
| Shader pipeline compile, ms (median of 16) | 12.5 | 4.91 | 2.54x better |
| First pipeline compile, ms | 21.5 | 5.96 | 3.61x better |

GPU temperature before each test: 33-42 °C.
