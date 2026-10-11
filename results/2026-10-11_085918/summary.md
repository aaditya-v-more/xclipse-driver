# Test run 2026-10-11T08:59:18

- Driver: `driver/` at `a332f58+changes`, `vulkan.radeon.so` SHA-256 `5f27bb33972641d9`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 34.0 °C at the start
- Extra environment for our driver: `XD_QUERY_STATS=0 XD_QUERY_PG=1 XD_QUERY_GS=0`

| Test | radv | stock |
|---|---|---|
| queries | **GPU hang** (device 1.7 ms) | not run |
| health-check | not run | pass (empty_cmdbuf 1.1 ms) |

## Problems

- **radv / queries**: gpu-hang - plain draw inside queries: GPU did not finish within 5000 ms
  - `amdgpu: DRM version is 3.42.0 (< 3.54); continuing.`
  - `[xdtest] creating device`
  - `/usr/local/share/libdrm/amdgpu.ids: No such file or directory`
  - `[xdtest] creating graphics pipelines`
  - `[xdtest] plain draw inside queries`
  - `[xdtest] fail: plain draw inside queries: GPU did not finish within 5000 ms`
  - kernel GPU events: hang +1, timeout +1
