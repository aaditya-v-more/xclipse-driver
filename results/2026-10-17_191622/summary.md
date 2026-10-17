# Test run 2026-10-17T19:16:22

- Driver: `driver/` at `288a2c6+changes`, `vulkan.radeon.so` SHA-256 `4aafb6beae52ef9b`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 33.0 °C at the start
- Extra environment for our driver: `RADV_XCLIPSE_FILTEREMU=0`

| Test | radv |
|---|---|
| textures | **FAIL** (device 1.8 ms) |
| mips | **FAIL** (device 1.6 ms) |
| depth | pass (device 1.7 ms) |

## Problems

- **radv / textures**: fail - wrong texels: R32F 0 fetched/63 filtered, RG32F 0 fetched/63 filtered, RGBA32F 0 fetched/63 filtered, R16 0 fetched/63 filtered, RG16 0 fetched/63 filtered, RGBA16 0 fetched/63 filtered, R16_SNORM 0 fetched/63 filtered, RGBA16_SNORM 0 fetched/63 filtered, D16 0 fetched/63 filtered, D32F 0 fetched/63 filtered, D32FS8 0 fetched/63 filtered
  - `[xdtest] sampling BC2`
  - `[xdtest] sampling BC3`
  - `[xdtest] sampling BC4`
  - `[xdtest] sampling BC5`
  - `[xdtest] sampling BC7`
  - `[xdtest] fail: wrong texels: R32F 0 fetched/63 filtered, RG32F 0 fetched/63 filtered, RGBA32F 0 fetched/63 filtered, R16 0 fetched/63 filtered, RG16 0 fetched/63 filtered, RGBA16 0 fetched/63 filtered, R16_SNORM 0 fetched/63 filtered, RGBA16_SNORM 0 fetched/63 filtered, D16 0 fetched/63 filtered, D32F 0 fetched/63 filtered, D32FS8 0 fetched/63 filtered`
- **radv / mips**: fail - wrong texels (fetch/sample/grad/cube or half-level): R32F 256x256 trilinear 0/0/0/1364, R16 640x360 trilinear 0/0/0/1518
  - `[xdtest] BC5 1024x1024`
  - `[xdtest] BC7 512x512`
  - `[xdtest] BC1 cube 256`
  - `[xdtest] R32F 256x256 trilinear`
  - `[xdtest] R16 640x360 trilinear`
  - `[xdtest] fail: wrong texels (fetch/sample/grad/cube or half-level): R32F 256x256 trilinear 0/0/0/1364, R16 640x360 trilinear 0/0/0/1518`
