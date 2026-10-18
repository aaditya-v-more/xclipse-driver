# Test run 2026-10-17T19:10:33

- Driver: `driver/` at `288a2c6+changes`, `vulkan.radeon.so` SHA-256 `8fc85be305980d0c`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 35.0 °C at the start
- Extra environment for our driver: `RADV_XCLIPSE_FILTEREMU=0`

| Test | radv |
|---|---|
| textures | **FAIL** (device 2.0 ms) |

## Problems

- **radv / textures**: fail - wrong texels: R32F 0 fetched/63 filtered, RG32F 0 fetched/63 filtered, RGBA32F 0 fetched/63 filtered, R16 0 fetched/63 filtered, RG16 0 fetched/63 filtered, RGBA16 0 fetched/63 filtered, R16_SNORM 0 fetched/63 filtered, RGBA16_SNORM 0 fetched/63 filtered, D16 0 fetched/63 filtered, D32F 0 fetched/63 filtered, D32FS8 0 fetched/63 filtered
  - `[xdtest] sampling BC2`
  - `[xdtest] sampling BC3`
  - `[xdtest] sampling BC4`
  - `[xdtest] sampling BC5`
  - `[xdtest] sampling BC7`
  - `[xdtest] fail: wrong texels: R32F 0 fetched/63 filtered, RG32F 0 fetched/63 filtered, RGBA32F 0 fetched/63 filtered, R16 0 fetched/63 filtered, RG16 0 fetched/63 filtered, RGBA16 0 fetched/63 filtered, R16_SNORM 0 fetched/63 filtered, RGBA16_SNORM 0 fetched/63 filtered, D16 0 fetched/63 filtered, D32F 0 fetched/63 filtered, D32FS8 0 fetched/63 filtered`
