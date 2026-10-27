# Test run 2026-10-27T16:07:42

- Driver: `driver/` at `299e141`, `vulkan.radeon.so` SHA-256 `dfdc165086d456b7`
- Phone: SM-S721B `S721BXXUEEZIF`, Android 17, GPU 45.0 °C at the start
- Extra environment for our driver: `RADV_PERFTEST=pswave32,cswave32 RADV_XCLIPSE_VOPD=1`

| Test | radv | stock |
|---|---|---|
| info | pass (instance 2.9 ms) | not run |
| submit | pass (empty_cmdbuf 0.8 ms) | not run |
| compute | pass (gpu 0.8 ms) | not run |
| render | **GPU hang** (device 1.5 ms) | not run |
| health-check | not run | pass (empty_cmdbuf 0.7 ms) |

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

## Problems

- **radv / render**: gpu-hang - render: GPU did not finish within 5000 ms
  - `amdgpu: DRM version is 3.42.0 (< 3.54); continuing.`
  - `[xdtest] creating device`
  - `/usr/local/share/libdrm/amdgpu.ids: No such file or directory`
  - `[xdtest] creating graphics pipeline`
  - `[xdtest] render`
  - `[xdtest] fail: render: GPU did not finish within 5000 ms`
  - kernel GPU events: hang +1, timeout +1
