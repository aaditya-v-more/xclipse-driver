# Test run 2026-10-10T18:45:17

- Driver: `driver/` at `ce9f8ac+changes`, `vulkan.radeon.so` SHA-256 `feaa827b0135ca88`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 38.0 °C at the start

| Test | radv |
|---|---|
| draws | pass (device 1.8 ms) |

## Performance

Median of 5 runs after a warm-up, timed with GPU timestamps.

| Metric | radv |
|---|---|
| CPU per draw, µs | 0.154 |
| CPU per draw with a pipeline switch, µs | 0.95 |
| GPU per draw, µs | 0.537 |
| GPU per draw with a pipeline switch, µs | 0.559 |

GPU temperature before each test: 39-39 °C.
