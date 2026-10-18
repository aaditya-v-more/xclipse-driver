# Test run 2026-10-17T21:31:55

- Driver: `driver/` at `7a21e93`, `vulkan.radeon.so` SHA-256 `7ee7edb81d1500d7`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 37.0 °C at the start

| Test | stock | radv |
|---|---|---|
| subgroup | pass (device 1.5 ms) | pass (device 1.7 ms) |
| textures | pass (device 1.4 ms) | pass (device 1.8 ms) |
| mips | pass (device 1.4 ms) | pass (device 1.5 ms) |
| depth | pass (device 1.6 ms) | pass (device 1.8 ms) |
| culling | pass (device 1.0 ms) | pass (device 1.4 ms) |
| render | pass (gpu 1.0 ms) | pass (gpu 0.8 ms) |
| xfb | pass (gpu 0.8 ms) | pass (gpu 1.0 ms) |
| compute | pass (gpu 0.8 ms) | pass (gpu 1.1 ms) |
