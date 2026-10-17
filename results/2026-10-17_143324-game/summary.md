# Game benchmark run 2026-10-17T14:33:24

start-boundary diagnostic: dense screenshots to identify true Heaven benchmark start

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart, with no detected failure or early process exit; the medians use only those. "Lost windows" counts gaps where SurfaceFlinger's 128-frame ring overflowed between two reads; those intervals are left out of every figure. Hz is the compositor's refresh rate during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | radv | 1 | too-short | 36.4 | 0.9 | 16.7 | 24.2 | 120 | 0 | 87.1 | 758.8 | 76.1 | 1733.6 | 0 | none | 1736 | 965 | 48 |

## Medians

- **heaven**: no valid runs

## Runs that didn't count

- Run 1, heaven on radv: under 30 s of steady frames. Screen: `run1-heaven-radv-screen.png`.
