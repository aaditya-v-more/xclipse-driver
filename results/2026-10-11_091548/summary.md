# Test run 2026-10-11T09:15:48

- Driver: `driver/` at `ea7b521`, `vulkan.radeon.so` SHA-256 `5494607e3902f9a3`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 34.0 °C at the start

| Test | stock | radv |
|---|---|---|
| xfb | pass (gpu 0.9 ms) | **GPU hang** (device 1.7 ms) |
| health-check | pass (empty_cmdbuf 0.5 ms) | not run |

## Problems

- **radv / xfb**: gpu-hang - transform feedback draw: GPU did not finish within 5000 ms
  - `amdgpu: DRM version is 3.42.0 (< 3.54); continuing.`
  - `[xdtest] creating device`
  - `/usr/local/share/libdrm/amdgpu.ids: No such file or directory`
  - `[xdtest] creating graphics pipeline`
  - `[xdtest] transform feedback draw`
  - `[xdtest] fail: transform feedback draw: GPU did not finish within 5000 ms`
  - kernel GPU events: hang +1, timeout +1
