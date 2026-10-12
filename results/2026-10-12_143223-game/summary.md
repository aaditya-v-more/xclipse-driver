# Game benchmark run 2026-10-12T14:32:23

Confirmation: GameNative v1.3.0-prerelease; RADV xd-fc066b9; DXVK 2.7.1; BCn none; FEXCore Intermediate; Box64 Compatibility.

DXVK_CONFIG=dxvk.enableDescriptorBuffer=False; WRAPPER_DISABLE_PLACED=0; diagnostic driver logging off.

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart, with no detected failure or early process exit; the medians use only those. "Lost windows" counts gaps where SurfaceFlinger's 128-frame ring overflowed between two reads; those intervals are left out of every figure. Hz is the compositor's refresh rate during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | radv | 1 | ran | 52.9 | 13.2 | 16.7 | 95.7 | 120 | 0 | 93.1 | 624.9 | 75.6 | 1404.6 | 0 | none | 2201 | 992 | 51 |
| heaven | radv | 2 | ran | 46.8 | 11.6 | 16.7 | 94.0 | 120 | 0 | 94.7 | 506.2 | 71.0 | 1131.6 | 0 | none | 2337 | 996 | 57 |

## Medians

- **heaven**: radv 49.9 FPS
