# Test run 2026-10-17T20:58:29

- Driver: `driver/` at `0e7eac3+changes`, `vulkan.radeon.so` SHA-256 `22914ad2cecbbbb1`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 31.0 °C at the start
- Extra environment for our driver: `RADV_XCLIPSE_FILTEREMU=2`

| Test | radv |
|---|---|
| mips | **FAIL** (device 1.9 ms) |

## Problems

- **radv / mips**: fail - wrong texels (fetch/sample/grad/cube or half-level): R32F 256x256 trilinear 0/0/0/1364, R16 640x360 trilinear 0/0/0/1516
  - `[xdtest] BC5 1024x1024`
  - `[xdtest] BC7 512x512`
  - `[xdtest] BC1 cube 256`
  - `[xdtest] R32F 256x256 trilinear`
  - `[xdtest] R16 640x360 trilinear`
  - `[xdtest] fail: wrong texels (fetch/sample/grad/cube or half-level): R32F 256x256 trilinear 0/0/0/1364, R16 640x360 trilinear 0/0/0/1516`
