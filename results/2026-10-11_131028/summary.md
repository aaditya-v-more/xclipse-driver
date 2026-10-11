# Test run 2026-10-11T13:10:28

- Driver: `driver/` at `f41503a+changes`, `vulkan.radeon.so` SHA-256 `8b62ca2cde9b164c`
- Phone: SM-S721B `S721BXXSEDZI1`, Android 16, GPU 40.0 °C at the start
- Extra environment for our driver: `RADV_XCLIPSE_RINGPTR=2 RADV_XCLIPSE_GFXSCRATCH=2`

| Test | radv |
|---|---|
| scratch | pass (gpu 10.3 ms) |
| tess | pass (gpu 2.7 ms) |
