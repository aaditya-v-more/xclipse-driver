# Test run 2026-10-10T18:45:20

- Driver: `driver/` at `ce9f8ac+changes`, `vulkan.radeon.so` SHA-256 `feaa827b0135ca88`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 39.0 °C at the start
- Extra environment for our driver: `RADV_XCLIPSE_NGGC=0`

| Test | radv |
|---|---|
| draws | pass (device 1.8 ms) |

## Performance

Median of 5 runs after a warm-up, timed with GPU timestamps.

| Metric | radv |
|---|---|
| CPU per draw, µs | 0.113 |
| CPU per draw with a pipeline switch, µs | 0.731 |
| GPU per draw, µs | 0.192 |
| GPU per draw with a pipeline switch, µs | 0.671 |

GPU temperature before each test: 39-39 °C.
