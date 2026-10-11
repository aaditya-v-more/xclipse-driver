# Test run 2026-10-11T13:16:43

- Driver: `driver/` at `f41503a+changes`, `vulkan.radeon.so` SHA-256 `8b62ca2cde9b164c`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 40.0 °C at the start
- Extra environment for our driver: `RADV_XCLIPSE_LOG=1`

| Test | stock | radv |
|---|---|---|
| scratchcs | pass (gpu 9.6 ms) | **GPU hang** (gpu 696.8 ms) |
| health-check | pass (empty_cmdbuf 0.9 ms) | not run |

## Problems

- **radv / scratchcs**: gpu-hang - 65536 of 65536 elements wrong; first at 0: got 0xeeeeeeee, want 0x00000007
  - `amdgpu: DRM version is 3.42.0 (< 3.54); continuing.`
  - `[xdtest] creating device`
  - `c:/share/libdrm/amdgpu.ids: No such file or directory`
  - `[xdtest] creating compute pipeline`
  - `[xdtest] compute dispatch`
  - `[xdtest] fail: 65536 of 65536 elements wrong; first at 0: got 0xeeeeeeee, want 0x00000007`
  - kernel GPU events: hang +1, page fault +1
