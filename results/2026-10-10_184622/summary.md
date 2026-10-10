# Test run 2026-10-10T18:46:22

- Driver: `driver/` at `ce9f8ac+changes`, `vulkan.radeon.so` SHA-256 `feaa827b0135ca88`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 40.0 °C at the start
- Extra environment for our driver: `RADV_XCLIPSE_NGGC=0`

| Test | radv |
|---|---|
| fill | pass (device 2.0 ms) |
| draws | pass (device 1.5 ms) |

## Performance

Median of 7 runs after at least 250 ms of warm-up, timed with GPU timestamps.

| Metric | radv |
|---|---|
| Opaque fill at 1080p, Gpixel/s | 24.3 |
| Blended fill at 1080p, Gpixel/s | 12.8 |
| CPU per draw, µs | 0.198 |
| CPU per draw with a pipeline switch, µs | 0.606 |
| GPU per draw, µs | 0.191 |
| GPU per draw with a pipeline switch, µs | 0.361 |

GPU temperature before each test: 40-42 °C.
