# Game benchmark run 2026-10-17T20:41:39

shader stats: filteremu=1

Android properties set: `debug.radv_xclipse_log=1`, `debug.radv_xclipse_filteremu=1`

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the game starts rendering for good (after loading stalls). A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart, with no detected failure or early process exit; the medians use only those. "Lost windows" counts gaps where SurfaceFlinger's frame ring overflowed between two reads; those intervals are left out of every figure. Hz is the compositor's refresh rate during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | radv | 1 | ran | 58.7 | 15.8 | 16.6 | 89.9 | 120 | 0 | 85.7 | 788.3 | 70.1 | 1251.2 | 0 | radv | 1646 | 957 | 47 |

## Medians

- **heaven**: radv 58.7 FPS
