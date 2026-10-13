# Test run 2026-10-13T16:58:55

- Driver: `driver/` at `17785a9`, `vulkan.radeon.so` SHA-256 `8a18f2883927cb30`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 37.0 °C at the start
- Extra environment for our driver: `RADV_XCLIPSE_LOG=0 RADV_XCLIPSE_CULL_MIN=128 RADV_XCLIPSE_CULL_NOFLUSH=1`

| Test | radv |
|---|---|
| culling | pass (device 1.9 ms) |
| draws | pass (device 1.7 ms) |
| render | pass (gpu 1.0 ms) |
| descriptors | pass (device 1.3 ms) |
| queries | pass (gpu 1.5 ms) |
| xfb | pass (gpu 0.9 ms) |
| xfbmany | pass (gpu 1.4 ms) |
| xfbmanygs | pass (gpu 1.3 ms) |
| tess | pass (gpu 0.8 ms) |
| scratch | pass (gpu 3.5 ms) |
| scratchcs | pass (gpu 4.8 ms) |

## Performance

Median of 7 runs after at least 250 ms of warm-up, timed with GPU timestamps.

| Metric | radv |
|---|---|
| CPU per draw, µs | 0.22 |
| CPU per draw with a pipeline switch, µs | 0.6 |
| GPU per draw, µs | 0.191 |
| GPU per draw with a pipeline switch, µs | 0.39 |

GPU temperature before each test: 37-38 °C.
