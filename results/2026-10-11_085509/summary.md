# Test run 2026-10-11T08:55:09

- Driver: `driver/` at `a332f58+changes`, `vulkan.radeon.so` SHA-256 `10fc3ac0c7889d03`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 35.0 °C at the start
- Extra environment for our driver: `XD_QUERY_STATS=7ff XD_QUERY_PG=0 XD_QUERY_GS=1`

| Test | radv |
|---|---|
| queries | **FAIL** (gpu 1.5 ms) |

## Problems

- **radv / queries**: fail - wrong counts: gs gs_primitives 20 (want 10)
  - `/usr/local/share/libdrm/amdgpu.ids: No such file or directory`
  - `[xdtest] creating graphics pipelines`
  - `[xdtest] plain draw inside queries`
  - `[xdtest] geometry-shader draw inside queries`
  - `[xdtest] reading query results`
  - `[xdtest] fail: wrong counts: gs gs_primitives 20 (want 10)`
