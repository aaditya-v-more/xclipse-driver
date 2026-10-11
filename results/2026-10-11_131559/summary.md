# Test run 2026-10-11T13:15:59

- Driver: `driver/` at `f41503a+changes`, `vulkan.radeon.so` SHA-256 `8b62ca2cde9b164c`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 40.0 °C at the start
- Extra environment for our driver: `RADV_XCLIPSE_LOG=1`

| Test | stock | radv |
|---|---|---|
| compute | pass (gpu 0.9 ms) | pass (gpu 2.0 ms) |
| scratchcs | pass (gpu 15.5 ms) | pass (gpu 1.9 ms) |
