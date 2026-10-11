# Test run 2026-10-11T13:01:52

- Driver: `driver/` at `f41503a+changes`, `vulkan.radeon.so` SHA-256 `877b7d4601d26629`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 40.0 °C at the start
- Extra environment for our driver: `RADV_XCLIPSE_LOG=1 RADV_XCLIPSE_RINGPTR=2`

| Test | radv | stock |
|---|---|---|
| tess | pass (gpu 3.0 ms) | not run |
| scratch | **GPU hang** (gpu 703.4 ms) | not run |
| health-check | not run | pass (empty_cmdbuf 1.8 ms) |
| render | pass (gpu 1.9 ms) | not run |

## Problems

- **radv / scratch**: gpu-hang - wrong pixels: center 0,0,0,0 corner 0,0,0,0, 16384 pixels neither color
  - `amdgpu: DRM version is 3.42.0 (< 3.54); continuing.`
  - `[xdtest] creating device`
  - `c:/share/libdrm/amdgpu.ids: No such file or directory`
  - `[xdtest] creating graphics pipeline`
  - `[xdtest] triangle draw`
  - `[xdtest] fail: wrong pixels: center 0,0,0,0 corner 0,0,0,0, 16384 pixels neither color`
  - kernel GPU events: hang +1, page fault +1
