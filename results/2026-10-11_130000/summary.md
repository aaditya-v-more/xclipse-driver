# Test run 2026-10-11T13:00:00

- Driver: `driver/` at `f41503a`, `vulkan.radeon.so` SHA-256 `7260c78c76a0f2f2`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 40.0 °C at the start
- Extra environment for our driver: `RADV_XCLIPSE_LOG=1`

| Test | stock | radv |
|---|---|---|
| tess | pass (gpu 1.9 ms) | **GPU hang** (gpu 687.1 ms) |
| health-check | pass (empty_cmdbuf 1.9 ms) | not run |
| scratch | pass (gpu 3.4 ms) | **GPU hang** (gpu 698.5 ms) |
| render | pass (gpu 1.8 ms) | pass (gpu 2.1 ms) |

## Problems

- **radv / tess**: gpu-hang - wrong pixels: center 0,0,0,0 corner 0,0,0,0, 16384 pixels neither color
  - `amdgpu: DRM version is 3.42.0 (< 3.54); continuing.`
  - `[xdtest] creating device`
  - `c:/share/libdrm/amdgpu.ids: No such file or directory`
  - `[xdtest] creating tessellation pipeline`
  - `[xdtest] triangle draw`
  - `[xdtest] fail: wrong pixels: center 0,0,0,0 corner 0,0,0,0, 16384 pixels neither color`
  - kernel GPU events: hang +1, page fault +1
- **radv / scratch**: gpu-hang - wrong pixels: center 0,0,0,0 corner 0,0,0,0, 16384 pixels neither color
  - `amdgpu: DRM version is 3.42.0 (< 3.54); continuing.`
  - `[xdtest] creating device`
  - `c:/share/libdrm/amdgpu.ids: No such file or directory`
  - `[xdtest] creating graphics pipeline`
  - `[xdtest] triangle draw`
  - `[xdtest] fail: wrong pixels: center 0,0,0,0 corner 0,0,0,0, 16384 pixels neither color`
  - kernel GPU events: hang +1, page fault +1
