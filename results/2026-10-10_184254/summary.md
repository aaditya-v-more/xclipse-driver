# Test run 2026-10-10T18:42:54

- Driver: `driver/` at `ce9f8ac`, `vulkan.radeon.so` SHA-256 `b44e72f04892d60e`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 39.0 °C at the start
- Extra environment for our driver: `RADV_DEBUG=nongg`

| Test | radv |
|---|---|
| draws | **FAIL** (device 1.8 ms) |

## Problems

- **radv / draws**: fail - draws (warm-up): GPU did not finish within 5000 ms
  - `c:/share/libdrm/amdgpu.ids: No such file or directory`
  - `amdgpu: DRM version is 3.42.0 (< 3.54); continuing.`
  - `[xdtest] creating device`
  - `c:/share/libdrm/amdgpu.ids: No such file or directory`
  - `[xdtest] draws (warm-up)`
  - `[xdtest] fail: draws (warm-up): GPU did not finish within 5000 ms`
