# Test run 2026-10-18T02:58:30

- Driver: `driver/` at `c568733+changes`, `vulkan.radeon.so` SHA-256 `6f10e56920fe3c9c`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 32.0 °C at the start

| Test | radv |
|---|---|
| textures | **FAIL** (device 1.7 ms) |

## Problems

- **radv / textures**: fail - wrong texels: BC5 46 fetched/25 filtered
  - `[xdtest] sampling BC3`
  - `[xdtest] sampling BC4`
  - `[xdtest] sampling BC5`
  - `[xdtest] sampling BC7`
  - `[xdtest] sampling BC7_SRGB`
  - `[xdtest] fail: wrong texels: BC5 46 fetched/25 filtered`
