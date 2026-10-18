# Test run 2026-10-17T19:10:25

- Driver: `driver/` at `288a2c6+changes`, `vulkan.radeon.so` SHA-256 `8fc85be305980d0c`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 34.0 °C at the start

| Test | stock | radv |
|---|---|---|
| textures | pass (device 1.8 ms) | **crash** |
| mips | pass (device 1.5 ms) | **crash** |
| depth | pass (device 0.9 ms) | pass (device 1.7 ms) |

## Problems

- **radv / textures**: crash - no result (exit code 134)
  - `[xdtest] creating instance`
  - `/usr/local/share/libdrm/amdgpu.ids: No such file or directory`
  - `amdgpu: DRM version is 3.42.0 (< 3.54); continuing.`
  - `[xdtest] creating device`
  - `/usr/local/share/libdrm/amdgpu.ids: No such file or directory`
  - `FORTIFY: %w0 is unsupported`
- **radv / mips**: crash - no result (exit code 134)
  - `/usr/local/share/libdrm/amdgpu.ids: No such file or directory`
  - `amdgpu: DRM version is 3.42.0 (< 3.54); continuing.`
  - `[xdtest] creating device`
  - `/usr/local/share/libdrm/amdgpu.ids: No such file or directory`
  - `[xdtest] creating pipeline`
  - `FORTIFY: %w0 is unsupported`
