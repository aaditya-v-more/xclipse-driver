# Test run 2026-10-10T18:40:37

- Driver: `driver/` at `ce9f8ac`, `vulkan.radeon.so` SHA-256 `b44e72f04892d60e`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 39.0 °C at the start

| Test | stock | radv |
|---|---|---|
| alu | pass (gpu 12.6 ms) | pass (gpu 28.4 ms) |

## Performance

Median of 5 runs after a warm-up, timed with GPU timestamps.

| Metric | stock | radv | radv vs stock |
|---|---|---|---|
| FP32 throughput, GFLOPS | 5.45e+03 | 2.42e+03 | 0.45x worse |

GPU temperature before each test: 40-40 °C.
