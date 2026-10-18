# Test run 2026-10-17T19:14:50

- Driver: `driver/` at `288a2c6+changes`, `vulkan.radeon.so` SHA-256 `945dbcaf6e90c0a6`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 33.0 °C at the start

| Test | stock | radv |
|---|---|---|
| textures | pass (device 2.0 ms) | pass (device 1.9 ms) |
| mips | **FAIL** (device 1.3 ms) | **FAIL** (device 1.6 ms) |
| depth | pass (device 1.0 ms) | pass (device 1.8 ms) |

## Problems

- **radv / mips**: fail - wrong texels (fetch/sample/grad/cube or half-level): R16 640x360 trilinear 0/751/751/751
  - `[xdtest] BC5 1024x1024`
  - `[xdtest] BC7 512x512`
  - `[xdtest] BC1 cube 256`
  - `[xdtest] R32F 256x256 trilinear`
  - `[xdtest] R16 640x360 trilinear`
  - `[xdtest] fail: wrong texels (fetch/sample/grad/cube or half-level): R16 640x360 trilinear 0/751/751/751`
- **stock / mips**: fail - wrong texels (fetch/sample/grad/cube or half-level): R32F 256x256 trilinear 0/0/0/1364, R16 640x360 trilinear 0/0/0/1518
  - `[xdtest] BC1 1024x1024`
  - `[xdtest] BC3 1024x1024`
  - `[xdtest] BC1 cube 256`
  - `[xdtest] R32F 256x256 trilinear`
  - `[xdtest] R16 640x360 trilinear`
  - `[xdtest] fail: wrong texels (fetch/sample/grad/cube or half-level): R32F 256x256 trilinear 0/0/0/1364, R16 640x360 trilinear 0/0/0/1518`
