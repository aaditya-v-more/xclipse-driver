# Test run 2026-10-11T08:53:36

- Driver: `driver/` at `a332f58+changes`, `vulkan.radeon.so` SHA-256 `10fc3ac0c7889d03`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 36.0 °C at the start

| Test | stock | radv |
|---|---|---|
| info | pass (instance 111.3 ms) | pass (instance 1.6 ms) |
| render | pass (gpu 0.8 ms) | pass (gpu 0.6 ms) |
| queries | pass (gpu 0.8 ms) | **GPU hang** (device 2.7 ms) |
| health-check | pass (empty_cmdbuf 0.5 ms) | not run |

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

## Problems

- **radv / queries**: gpu-hang - draws inside queries: GPU did not finish within 5000 ms
  - `amdgpu: DRM version is 3.42.0 (< 3.54); continuing.`
  - `[xdtest] creating device`
  - `/usr/local/share/libdrm/amdgpu.ids: No such file or directory`
  - `[xdtest] creating graphics pipelines`
  - `[xdtest] draws inside queries`
  - `[xdtest] fail: draws inside queries: GPU did not finish within 5000 ms`
  - kernel GPU events: hang +1, timeout +1
