# Game benchmark run 2026-10-12T12:12:40

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart; the medians use only those. "Lost windows" counts gaps where SurfaceFlinger's 128-frame ring overflowed between two reads; those intervals are left out of every figure. Hz is the compositor's refresh rate during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | stock | 1 | too-short | 13.5 | 0.3 | 16.6 | 3.0 | 120 | 0 | 0.7 | 46.9 | 46.1 | 902.0 | 0 | samsung | 1973 | 1079 | 62 |
| heaven | stock | 2 | too-short | 13.1 | 0.3 | 16.6 | 2.9 | 120 | 0 | 0.9 | 38.8 | 46.1 | 861.7 | 0 | samsung | 1936 | 1086 | 62 |

## Medians

- **heaven**: no valid runs

## Runs that didn't count

- Run 1, heaven on stock: under 30 s of steady frames. Screen: `run1-heaven-stock-screen.png`.
- Run 2, heaven on stock: under 30 s of steady frames. Screen: `run2-heaven-stock-screen.png`.

## Visual validation

No valid stock benchmark. Full BCn emulation removed the DXT errors in the inspected run-1 screenshot, but Heaven still failed to create its D24 depth texture. Both launches were classified `too-short` and are excluded from performance comparisons. FEXCore remained `Intermediate` and Box64 `Compatibility`.
