# Test run 2026-10-10T18:42:37

- Driver: `driver/` at `ce9f8ac`, `vulkan.radeon.so` SHA-256 `b44e72f04892d60e`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 39.0 °C at the start
- Extra environment for our driver: `RADV_DEBUG=nonggc`

| Test | stock | radv |
|---|---|---|
| draws | pass (device 1.4 ms) | pass (device 1.9 ms) |

## Performance

Median of 5 runs after a warm-up, timed with GPU timestamps.

| Metric | stock | radv | radv vs stock |
|---|---|---|---|
| CPU per draw, µs | 0.058 | 0.129 | 0.45x worse |
| CPU per draw with a pipeline switch, µs | 0.418 | 0.865 | 0.48x worse |
| GPU per draw, µs | 0.055 | 0.122 | 0.45x worse |
| GPU per draw with a pipeline switch, µs | 0.471 | 0.661 | 0.71x worse |

GPU temperature before each test: 40-40 °C.
