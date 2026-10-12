# Game benchmark run 2026-10-12T14:29:13

RADV diagnostic logging enabled; this adds CPU work.

GameNative v1.3.0-prerelease; RADV xd-fc066b9; DXVK 2.7.1; BCn none; original emulator presets.

Diagnostic experiment: DXVK_CONFIG=dxvk.enableDescriptorBuffer=False; WRAPPER_DISABLE_PLACED=0; graphics pipeline library back to default.

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart, with no detected failure or early process exit; the medians use only those. "Lost windows" counts gaps where SurfaceFlinger's 128-frame ring overflowed between two reads; those intervals are left out of every figure. Hz is the compositor's refresh rate during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | radv | 1 | ran | 57.4 | 12.7 | 16.7 | 95.5 | 120 | 0 | 84.0 | 752.6 | 74.4 | 1349.1 | 0 | radv | 2006 | 1016 | 58 |

## Medians

- **heaven**: radv 57.4 FPS
