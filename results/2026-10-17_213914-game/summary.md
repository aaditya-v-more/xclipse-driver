# Game benchmark run 2026-10-17T21:39:14

xd-85cd575 persistent shader cache check

Android properties set: `debug.radv_xclipse_log=1`

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the game starts rendering for good (after loading stalls). A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart, with no detected failure or early process exit; the medians use only those. "Lost windows" counts gaps where SurfaceFlinger's frame ring overflowed between two reads; those intervals are left out of every figure. Hz is the compositor's refresh rate during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | radv | 1 | stalled |  |  |  |  |  |  |  |  |  |  | 0 | radv | 1954 | 893 | 51 |
| heaven | radv | 2 | surface-lost | 57.9 | 11.2 | 16.6 | 252.9 | 120 | 0 | 68.3 | 659.2 | 71.2 | 1448.2 | 0 | radv | 1713 | 1004 | 57 |
| heaven | radv | 3 | skipped |  |  |  |  |  |  |  |  |  |  |  | none | 0 | 0 | 0 |

## Medians

- **heaven**: no valid runs

## Runs that didn't count

- Run 1, heaven on radv: no new frame for 60 s. Screen: `run1-heaven-radv-screen.png`.
- Run 2, heaven on radv: GameNative's rendering surface disappeared before the run ended. Screen: `run2-heaven-radv-screen.png`.
- Run 3, heaven on radv: remaining attempt skipped after repeated failures or a GPU fault.
