# Test run 2026-10-18T01:43:24

- Driver: `driver/` at `49804f2+changes`, `vulkan.radeon.so` SHA-256 `2d5351d72bee0c07`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 36.0 °C at the start

| Test | stock | radv |
|---|---|---|
| textures | pass (device 1.9 ms) | **FAIL** (device 1.7 ms) |
| bcn | pass (device 0.9 ms) | pass (device 2.2 ms) |
| mips | pass (device 0.9 ms) | **FAIL** (device 1.5 ms) |
| memreq | pass (device 0.9 ms) | pass (device 1.6 ms) |

## Problems

- **radv / textures**: fail - wrong texels: BC4 26 fetched/10 filtered
  - `[xdtest] sampling BC3`
  - `[xdtest] sampling BC4`
  - `[xdtest] sampling BC5`
  - `[xdtest] sampling BC7`
  - `[xdtest] sampling BC7_SRGB`
  - `[xdtest] fail: wrong texels: BC4 26 fetched/10 filtered`
- **radv / mips**: fail - wrong texels (fetch/sample/grad/cube or half-level): BC4 512x512 722/722/722/0
  - `[xdtest] BC5 1024x1024`
  - `[xdtest] BC7 512x512`
  - `[xdtest] BC1 cube 256`
  - `[xdtest] R32F 256x256 trilinear`
  - `[xdtest] R16 640x360 trilinear`
  - `[xdtest] fail: wrong texels (fetch/sample/grad/cube or half-level): BC4 512x512 722/722/722/0`
