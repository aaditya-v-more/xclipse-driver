# Test run 2026-10-13T16:57:17

- Driver: `driver/` at `17785a9`, `vulkan.radeon.so` SHA-256 `8a18f2883927cb30`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 38.0 °C at the start
- Extra environment for our driver: `RADV_XCLIPSE_LOG=0`

| Test | stock | radv |
|---|---|---|
| render | pass (gpu 0.9 ms) | pass (gpu 0.9 ms) |
| descriptors | pass (device 1.4 ms) | pass (device 1.6 ms) |
| culling | pass (device 0.9 ms) | pass (device 1.5 ms) |
| queries | pass (gpu 0.8 ms) | pass (gpu 1.4 ms) |
| xfb | pass (gpu 0.8 ms) | pass (gpu 1.5 ms) |
| xfbmany | pass (gpu 2.0 ms) | pass (gpu 1.1 ms) |
| xfbmanygs | pass (gpu 1.5 ms) | pass (gpu 1.9 ms) |
| tess | pass (gpu 1.1 ms) | pass (gpu 0.7 ms) |
| scratch | pass (gpu 1.3 ms) | pass (gpu 3.9 ms) |
| scratchcs | pass (gpu 4.7 ms) | pass (gpu 4.9 ms) |
| draws | pass (device 1.3 ms) | pass (device 1.8 ms) |

## Performance

Median of 7 runs after at least 250 ms of warm-up, timed with GPU timestamps.

| Metric | stock | radv | radv vs stock |
|---|---|---|---|
| CPU per draw, µs | 0.18 | 0.25 | 0.72x worse |
| CPU per draw with a pipeline switch, µs | 0.473 | 0.587 | 0.81x worse |
| GPU per draw, µs | 0.14 | 0.186 | 0.75x worse |
| GPU per draw with a pipeline switch, µs | 0.369 | 0.366 | 1.01x better |

GPU temperature before each test: 37-39 °C.
