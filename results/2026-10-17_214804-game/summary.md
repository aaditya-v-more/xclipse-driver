# Game benchmark run 2026-10-17T21:48:04

xd-85cd575 warm shader cache

Android properties set: `debug.radv_xclipse_log=1`

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the game starts rendering for good (after loading stalls). A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart, with no detected failure or early process exit; the medians use only those. "Lost windows" counts gaps where SurfaceFlinger's frame ring overflowed between two reads; those intervals are left out of every figure. Hz is the compositor's refresh rate during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | radv | 1 | surface-lost | 57.8 | 10.9 | 16.6 | 252.8 | 120 | 0 | 70.3 | 642.3 | 73.9 | 1672.0 | 0 | radv | 1889 | 970 | 57 |
| heaven | radv | 2 | surface-lost | 57.4 | 11.0 | 16.6 | 252.1 | 120 | 5 | 74.6 | 601.8 | 75.4 | 1630.5 | 0 | radv | 1732 | 975 | 57 |

## Medians

- **heaven**: no valid runs

## Runs that didn't count

- Run 1, heaven on radv: GameNative's rendering surface disappeared before the run ended. Screen: `run1-heaven-radv-screen.png`.
- Run 2, heaven on radv: GameNative's rendering surface disappeared before the run ended. Screen: `run2-heaven-radv-screen.png`.
