# Test run 2026-10-17T18:57:56

- Driver: `driver/` at `288a2c6`, `vulkan.radeon.so` SHA-256 `6e8633e7bedb8bd3`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 36.0 °C at the start

| Test | stock | radv |
|---|---|---|
| textures | pass (device 1.7 ms) | **FAIL** (device 1.9 ms) |

## Problems

- **radv / textures**: fail - wrong texels: R32F 0 fetched/63 filtered, RG32F 0 fetched/63 filtered, RGBA32F 0 fetched/63 filtered, R16 0 fetched/63 filtered, D16 0 fetched/63 filtered, D32F 0 fetched/63 filtered, D32FS8 0 fetched/63 filtered
  - `[xdtest] sampling BC2`
  - `[xdtest] sampling BC3`
  - `[xdtest] sampling BC4`
  - `[xdtest] sampling BC5`
  - `[xdtest] sampling BC7`
  - `[xdtest] fail: wrong texels: R32F 0 fetched/63 filtered, RG32F 0 fetched/63 filtered, RGBA32F 0 fetched/63 filtered, R16 0 fetched/63 filtered, D16 0 fetched/63 filtered, D32F 0 fetched/63 filtered, D32FS8 0 fetched/63 filtered`
