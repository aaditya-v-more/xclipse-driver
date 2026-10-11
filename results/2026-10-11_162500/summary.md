# Test run 2026-10-11T16:25:00

- Driver: `driver/` at `fc066b9`, `vulkan.radeon.so` SHA-256 `62f186db960ad64c`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 40.0 °C at the start

| Test | stock | radv |
|---|---|---|
| mips | pass (device 1.6 ms) | pass (device 1.5 ms) |
| textures | pass (device 1.4 ms) | **FAIL** (device 1.6 ms) |

## Problems

- **radv / textures**: fail - wrong texels: R32F 0 fetched/63 filtered, D16 0 fetched/63 filtered, D32F 0 fetched/63 filtered, D32FS8 0 fetched/63 filtered
  - `[xdtest] sampling BC2`
  - `[xdtest] sampling BC3`
  - `[xdtest] sampling BC4`
  - `[xdtest] sampling BC5`
  - `[xdtest] sampling BC7`
  - `[xdtest] fail: wrong texels: R32F 0 fetched/63 filtered, D16 0 fetched/63 filtered, D32F 0 fetched/63 filtered, D32FS8 0 fetched/63 filtered`
