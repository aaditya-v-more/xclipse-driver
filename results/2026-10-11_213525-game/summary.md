# Game benchmark run 2026-10-11T21:35:25

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart; the medians use only those. "Lost windows" counts gaps where SurfaceFlinger's 128-frame ring overflowed between two reads; those intervals are left out of every figure. Hz is the compositor's refresh rate during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | stock | 1 | no-frames |  |  |  |  |  |  |  |  |  |  | 0 | none | 2653 | 241 | 39 |
| heaven | radv | 1 | ran | 2.8 | 0.1 | 16.7 | 41.5 | 120 | 0 | 28.1 | 322.8 | 45.1 | 1306.1 | 68 | none | 2282 | 935 | 63 |

## Medians

- **heaven**: radv 2.8 FPS

## Runs that didn't count

- Run 1, heaven on stock: no frames from the game. Screen: `run1-heaven-stock-screen.png`.
