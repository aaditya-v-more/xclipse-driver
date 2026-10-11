# Test run 2026-10-11T09:47:08

- Driver: `driver/` at `ea7b521+changes`, `vulkan.radeon.so` SHA-256 `e90ae2d78f86eeb2`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 35.0 °C at the start

| Test | stock | radv |
|---|---|---|
| xfb | pass (gpu 0.6 ms) | pass (gpu 0.9 ms) |
| xfbmany | pass (gpu 2.2 ms) | **FAIL** (gpu 1.2 ms) |
| xfbmanygs | pass (gpu 1.3 ms) | **FAIL** (gpu 1.4 ms) |

## Problems

- **radv / xfbmany**: fail - 192 of 73728 slots hold no vertex, 192 vertices missing, 0 captured twice, 64 triangles torn apart
  - `[xdtest] creating device`
  - `/usr/local/share/libdrm/amdgpu.ids: No such file or directory`
  - `[xdtest] creating graphics pipeline`
  - `[xdtest] transform feedback draws`
  - `[xdtest] reading results`
  - `[xdtest] fail: 192 of 73728 slots hold no vertex, 192 vertices missing, 0 captured twice, 64 triangles torn apart`
- **radv / xfbmanygs**: fail - 85 vertices missing, 85 captured twice, 85 vertices out of draw order (slot 20855 holds vertex 0), 85 triangles torn apart
  - `[xdtest] creating device`
  - `/usr/local/share/libdrm/amdgpu.ids: No such file or directory`
  - `[xdtest] creating graphics pipeline`
  - `[xdtest] transform feedback draws`
  - `[xdtest] reading results`
  - `[xdtest] fail: 85 vertices missing, 85 captured twice, 85 vertices out of draw order (slot 20855 holds vertex 0), 85 triangles torn apart`
