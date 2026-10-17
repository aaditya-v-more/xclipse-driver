# Test run 2026-10-18T01:09:55

- Driver: `driver/` at `7a21e93+changes`, `vulkan.radeon.so` SHA-256 `c1e197c9260a27a8`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 34.0 °C at the start

| Test | stock | radv |
|---|---|---|
| textures | pass (device 1.4 ms) | **FAIL** (device 1.9 ms) |
| bcn | pass (device 1.3 ms) | pass (device 1.7 ms) |
| mips | pass (device 2.1 ms) | pass (device 1.3 ms) |
| memreq | pass (device 1.0 ms) | pass (device 1.3 ms) |

## Problems

- **radv / textures**: fail - wrong texels: BC7_SRGB 3 fetched/0 filtered
  - `[xdtest] sampling BC3`
  - `[xdtest] sampling BC4`
  - `[xdtest] sampling BC5`
  - `[xdtest] sampling BC7`
  - `[xdtest] sampling BC7_SRGB`
  - `[xdtest] fail: wrong texels: BC7_SRGB 3 fetched/0 filtered`
