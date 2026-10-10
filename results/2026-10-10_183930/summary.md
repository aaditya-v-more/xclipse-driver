# Test run 2026-10-10T18:39:30

- Driver: `driver/` at `ce9f8ac`, `vulkan.radeon.so` SHA-256 `b44e72f04892d60e`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 40.0 °C at the start

| Test | stock | radv |
|---|---|---|
| alu | pass (gpu 9.9 ms) | pass (gpu 31.1 ms) |
| bandwidth | pass (device 1.4 ms) | pass (device 1.5 ms) |
| fill | pass (device 1.3 ms) | pass (device 1.8 ms) |
| draws | pass (device 1.5 ms) | pass (device 1.4 ms) |
| compile | pass (device 1.4 ms) | pass (device 1.5 ms) |

## Performance

Median of 5 runs after a warm-up, timed with GPU timestamps.

| Metric | stock | radv | radv vs stock |
|---|---|---|---|
| FP32 throughput, GFLOPS | 6.92e+03 | 2.21e+03 | 0.32x worse |
| Shader copy, GB/s | 48.4 | 47.2 | 0.97x worse |
| vkCmdCopyBuffer, GB/s | 36 | 48.8 | 1.35x better |
| Opaque fill at 1080p, Gpixel/s | 25.5 | 22 | 0.86x worse |
| Blended fill at 1080p, Gpixel/s | 12.8 | 12.8 | 1.00x worse |
| CPU per draw, µs | 0.058 | 0.114 | 0.51x worse |
| CPU per draw with a pipeline switch, µs | 0.285 | 0.801 | 0.36x worse |
| GPU per draw, µs | 0.055 | 0.298 | 0.18x worse |
| GPU per draw with a pipeline switch, µs | 0.477 | 0.539 | 0.88x worse |
| Shader pipeline compile, ms (median of 16) | 12.7 | 4.81 | 2.65x better |
| First pipeline compile, ms | 26.7 | 6.94 | 3.84x better |

GPU temperature before each test: 40-43 °C.
