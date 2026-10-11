# Test run 2026-10-11T13:08:24

- Driver: `driver/` at `f41503a+changes`, `vulkan.radeon.so` SHA-256 `a8bb0abe89ae549c`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 41.0 °C at the start
- Extra environment for our driver: `RADV_XCLIPSE_RINGPTR=2 RADV_XCLIPSE_GFXSCRATCH=1`

| Test | radv | stock |
|---|---|---|
| scratch | **GPU hang** (gpu 707.7 ms) | not run |
| health-check | not run | pass (empty_cmdbuf 1.7 ms) |

## Problems

- **radv / scratch**: gpu-hang - 16384 of 16384 pixels differ from the grid drawn without scratch
  - `[xdtest] creating device`
  - `c:/share/libdrm/amdgpu.ids: No such file or directory`
  - `[xdtest] creating graphics pipelines`
  - `[xdtest] scratch_reference`
  - `[xdtest] scratch`
  - `[xdtest] fail: 16384 of 16384 pixels differ from the grid drawn without scratch`
  - kernel GPU events: hang +1, page fault +1
