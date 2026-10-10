# Test run 2026-10-10T18:46:07

- Driver: `driver/` at `ce9f8ac+changes`, `vulkan.radeon.so` SHA-256 `feaa827b0135ca88`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 38.0 °C at the start

| Test | stock | radv |
|---|---|---|
| alu | pass (gpu 28.4 ms) | pass (gpu 28.4 ms) |
| bandwidth | pass (device 1.4 ms) | pass (device 1.6 ms) |
| fill | pass (device 1.4 ms) | pass (device 1.7 ms) |
| draws | pass (device 1.4 ms) | pass (device 1.6 ms) |
| compile | pass (device 1.4 ms) | pass (device 1.5 ms) |

## Performance

Median of 7 runs after at least 250 ms of warm-up, timed with GPU timestamps.

| Metric | stock | radv | radv vs stock |
|---|---|---|---|
| FP32 throughput, GFLOPS | 2.42e+03 | 2.42e+03 | 1.00x worse |
| Shader copy, GB/s | 35.7 | 49.3 | 1.38x better |
| vkCmdCopyBuffer, GB/s | 36.1 | 35.3 | 0.98x worse |
| Opaque fill at 1080p, Gpixel/s | 25.5 | 25.5 | 1.00x worse |
| Blended fill at 1080p, Gpixel/s | 12.8 | 12.8 | 1.00x worse |
| CPU per draw, µs | 0.152 | 0.253 | 0.60x worse |
| CPU per draw with a pipeline switch, µs | 0.53 | 0.638 | 0.83x worse |
| GPU per draw, µs | 0.14 | 0.237 | 0.59x worse |
| GPU per draw with a pipeline switch, µs | 0.356 | 0.408 | 0.87x worse |
| Shader pipeline compile, ms (median of 16) | 13.1 | 4.97 | 2.63x better |
| First pipeline compile, ms | 18 | 5.97 | 3.02x better |

GPU temperature before each test: 38-46 °C.
