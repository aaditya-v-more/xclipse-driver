# Test run 2026-10-18T02:58:09

- Driver: `driver/` at `c568733+changes`, `vulkan.radeon.so` SHA-256 `8af989c29e64c3e2`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 33.0 °C at the start

| Test | radv |
|---|---|
| textures | **FAIL** (device 2.0 ms) |

## Problems

- **radv / textures**: fail - wrong texels: BC5 10 fetched/0 filtered
  - `[xdtest] sampling BC3`
  - `[xdtest] sampling BC4`
  - `[xdtest] sampling BC5`
  - `[xdtest] sampling BC7`
  - `[xdtest] sampling BC7_SRGB`
  - `[xdtest] fail: wrong texels: BC5 10 fetched/0 filtered`
