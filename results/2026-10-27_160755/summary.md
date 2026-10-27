# Test run 2026-10-27T16:07:55

- Driver: `driver/` at `299e141`, `vulkan.radeon.so` SHA-256 `dfdc165086d456b7`
- Phone: SM-S721B `S721BXXUEEZIF`, Android 17, GPU 44.0 °C at the start
- Extra environment for our driver: `RADV_XCLIPSE_VOPD=1 RADV_XCLIPSE_DELAYALU=1`

| Test | radv |
|---|---|
| info | pass (instance 3.3 ms) |
| submit | pass (empty_cmdbuf 0.6 ms) |
| compute | pass (gpu 1.4 ms) |
| render | pass (gpu 0.9 ms) |
| tess | pass (gpu 0.9 ms) |
| textures | pass (device 1.2 ms) |
| depth | pass (device 1.6 ms) |
| alu | pass (gpu 28.5 ms) |

## What each driver exposes

| | radv |
|---|---|
| GPU | AMD Radeon Graphics (RADV TITAN) |
| Vulkan | 1.4.358 |
| Driver | radv |
| Driver info | Mesa 26.3.0-devel |
| Vendor ID | 0x144d |
| Subgroup size | 64 |
| Device extensions | 224 |
| textureCompressionBC | yes |
| shaderFloat64 | yes |
| samplerFilterMinmax | yes |
| geometryShader | yes |
| tessellationShader | yes |
| depthBounds | yes |
| dualSrcBlend | yes |
| logicOp | yes |
| multiViewport | yes |
| pipelineStatisticsQuery | yes |
| shaderCullDistance | yes |
| fillModeNonSolid | yes |
| sample BC1_RGBA_UNORM | yes |
| sample BC3_UNORM | yes |
| sample BC5_UNORM | yes |
| sample BC7_UNORM | yes |
| VK_KHR_maintenance5 | yes |
| VK_KHR_maintenance6 | yes |
| VK_EXT_mutable_descriptor_type | yes |
| VK_EXT_transform_feedback | yes |
| VK_EXT_graphics_pipeline_library | yes |
| VK_EXT_robustness2 | yes |
| VK_KHR_robustness2 | yes |
| VK_EXT_custom_border_color | yes |
| VK_EXT_extended_dynamic_state3 | yes |
| VK_EXT_descriptor_buffer | yes |
| VK_EXT_shader_module_identifier | yes |
| VK_EXT_depth_clip_enable | yes |
| VK_EXT_line_rasterization | yes |
| VK_EXT_vertex_attribute_divisor | yes |
| VK_KHR_vertex_attribute_divisor | yes |
| VK_EXT_attachment_feedback_loop_layout | yes |
| VK_EXT_non_seamless_cube_map | yes |
| VK_EXT_memory_priority | yes |
| VK_EXT_memory_budget | yes |
| VK_EXT_conservative_rasterization | yes |
| VK_EXT_fragment_shader_interlock | yes |
| VK_EXT_shader_stencil_export | yes |
| VK_EXT_image_view_min_lod | yes |
| VK_EXT_depth_bias_control | yes |
| VK_KHR_fragment_shading_rate | yes |
| VK_EXT_mesh_shader | yes |
| VK_KHR_ray_tracing_pipeline | yes |
| VK_KHR_ray_query | yes |

## Performance

Median of 7 runs after at least 250 ms of warm-up, timed with GPU timestamps.

| Metric | radv |
|---|---|
| FP32 throughput, GFLOPS | 2.41e+03 |

GPU temperature before each test: 43-45 °C.
