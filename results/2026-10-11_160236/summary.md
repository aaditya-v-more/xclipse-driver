# Test run 2026-10-11T16:02:36

- Driver: `driver/` at `fc066b9`, `vulkan.radeon.so` SHA-256 `62f186db960ad64c`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 39.0 °C at the start

| Test | stock | radv |
|---|---|---|
| mrt | **FAIL** (gpu 1.1 ms) | **FAIL** (gpu 1.1 ms) |
| textures | pass (device 1.4 ms) | **FAIL** (device 1.7 ms) |

## Problems

- **stock / mrt**: fail - wrong texels (drawn/drawn+added/cleared/cleared+added): target 7 RGBA8_SRGB 144/144/0/0
  - `[xdtest] loading driver`
  - `[xdtest] creating instance`
  - `[xdtest] creating device`
  - `[xdtest] creating pipelines`
  - `[xdtest] mrt`
  - `[xdtest] fail: wrong texels (drawn/drawn+added/cleared/cleared+added): target 7 RGBA8_SRGB 144/144/0/0`
- **radv / mrt**: fail - wrong texels (drawn/drawn+added/cleared/cleared+added): target 7 RGBA8_SRGB 144/144/0/0
  - `amdgpu: DRM version is 3.42.0 (< 3.54); continuing.`
  - `[xdtest] creating device`
  - `c:/share/libdrm/amdgpu.ids: No such file or directory`
  - `[xdtest] creating pipelines`
  - `[xdtest] mrt`
  - `[xdtest] fail: wrong texels (drawn/drawn+added/cleared/cleared+added): target 7 RGBA8_SRGB 144/144/0/0`
- **radv / textures**: fail - wrong texels: R32F 0 fetched/63 filtered, D16 0 fetched/63 filtered, D32F 0 fetched/63 filtered, D32FS8 0 fetched/63 filtered
  - `[xdtest] sampling BC2`
  - `[xdtest] sampling BC3`
  - `[xdtest] sampling BC4`
  - `[xdtest] sampling BC5`
  - `[xdtest] sampling BC7`
  - `[xdtest] fail: wrong texels: R32F 0 fetched/63 filtered, D16 0 fetched/63 filtered, D32F 0 fetched/63 filtered, D32FS8 0 fetched/63 filtered`
