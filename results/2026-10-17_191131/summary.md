# Test run 2026-10-17T19:11:31

- Driver: `driver/` at `288a2c6+changes`, `vulkan.radeon.so` SHA-256 `3661876e2933316f`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 36.0 °C at the start

| Test | radv |
|---|---|
| textures | **FAIL** (device 2.5 ms) |
| mips | **FAIL** (device 1.8 ms) |

## Problems

- **radv / textures**: fail - wrong texels: RGBA8 0 fetched/64 filtered, BGRA8 0 fetched/64 filtered, RGBA8_SRGB 0 fetched/64 filtered, RGBA16F 0 fetched/64 filtered, R11G11B10F 0 fetched/64 filtered, RGB10A2 0 fetched/64 filtered, R8 0 fetched/64 filtered, R16F 0 fetched/64 filtered, BC1 0 fetched/64 filtered, BC2 0 fetched/64 filtered, BC3 0 fetched/64 filtered, BC4 0 fetched/64 filtered, BC5 0 fetched/64 filtered, BC7 0 fetched/60 filte
  - `[xdtest] sampling BC2`
  - `[xdtest] sampling BC3`
  - `[xdtest] sampling BC4`
  - `[xdtest] sampling BC5`
  - `[xdtest] sampling BC7`
  - `[xdtest] fail: wrong texels: RGBA8 0 fetched/64 filtered, BGRA8 0 fetched/64 filtered, RGBA8_SRGB 0 fetched/64 filtered, RGBA16F 0 fetched/64 filtered, R11G11B10F 0 fetched/64 filtered, RGB10A2 0 fetched/64 filtered, R8 0 fetched/64 filtered, R16F 0 fetched/64 filtered, BC1 0 fetched/64 filtered, BC2 0 fetched/64 filtered, BC3 0 fetched/64 filtered, BC4 0 fetched/64 filtered, BC5 0 fetched/64 filtered, BC7 0 fetched/60 filte`
- **radv / mips**: fail - wrong texels (fetch/sample/grad/cube): RGBA8 64x64 0/853/853/0, RGBA8 2048x2048 0/2133/2133/0, RGBA8 640x360 0/1519/1519/0, RGBA8 cube 128 0/6654/6654/0, BC1 1024x1024 0/1580/1580/0, BC3 1024x1024 0/1877/1877/0, BC4 512x512 0/1621/1621/0, BC5 1024x1024 0/1877/1877/0, BC7 512x512 0/1580/1580/0, BC1 cube 256 0/7289/7289/0
  - `[xdtest] BC3 1024x1024`
  - `[xdtest] BC4 512x512`
  - `[xdtest] BC5 1024x1024`
  - `[xdtest] BC7 512x512`
  - `[xdtest] BC1 cube 256`
  - `[xdtest] fail: wrong texels (fetch/sample/grad/cube): RGBA8 64x64 0/853/853/0, RGBA8 2048x2048 0/2133/2133/0, RGBA8 640x360 0/1519/1519/0, RGBA8 cube 128 0/6654/6654/0, BC1 1024x1024 0/1580/1580/0, BC3 1024x1024 0/1877/1877/0, BC4 512x512 0/1621/1621/0, BC5 1024x1024 0/1877/1877/0, BC7 512x512 0/1580/1580/0, BC1 cube 256 0/7289/7289/0`
