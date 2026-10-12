# Game benchmark run 2026-10-12T11:59:04

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart; the medians use only those. "Lost windows" counts gaps where SurfaceFlinger's 128-frame ring overflowed between two reads; those intervals are left out of every figure. Hz is the compositor's refresh rate during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | stock | 1 | ran | 48.1 | 2.3 | 16.7 | 58.2 | 120 | 0 | 58.8 | 610.7 | 60.8 | 1379.6 | 0 | samsung | 2102 | 1017 | 61 |

## Medians

- **heaven**: stock 48.1 FPS

## Visual validation

Failed: the stock-driver screenshots show black surfaces with BCn Emulation set to `none`. The run was stopped after the first stock result; its FPS is excluded from the preset comparison. The next baseline uses stock `auto` and RADV `none`.
