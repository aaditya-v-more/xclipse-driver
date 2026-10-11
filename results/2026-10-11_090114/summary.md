# Test run 2026-10-11T09:01:14

- Driver: `driver/` at `a332f58+changes`, `vulkan.radeon.so` SHA-256 `6335fbbbefa59009`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 36.0 °C at the start
- Extra environment for our driver: `XD_QUERY_STATS=0 XD_QUERY_PG=1 XD_QUERY_GS=0 XD_PG_EXP=2`

| Test | radv | stock |
|---|---|---|
| queries | **GPU hang** (device 3.5 ms) | not run |
| health-check | not run | pass (empty_cmdbuf 0.8 ms) |

## Problems

- **radv / queries**: gpu-hang - plain draw inside queries: GPU did not finish within 5000 ms
  - `[xdtest] creating device`
  - `/usr/local/share/libdrm/amdgpu.ids: No such file or directory`
  - `[xdtest] creating graphics pipelines`
  - `XD_PG_EXP begin 2`
  - `[xdtest] plain draw inside queries`
  - `[xdtest] fail: plain draw inside queries: GPU did not finish within 5000 ms`
  - kernel GPU events: hang +1, timeout +1
