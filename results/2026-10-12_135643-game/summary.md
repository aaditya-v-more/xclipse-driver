# Game benchmark run 2026-10-12T13:56:43

RADV diagnostic logging enabled; this adds CPU work.

GameNative v1.3.0-prerelease / 231d1365; DXVK async-1.10.3, original FEXCore Intermediate / Box64 Compatibility; stock BCn full, RADV BCn none.

Stock: inspected fatal D24 texture-creation dialog. RADV: inspected 90-second screenshot renders correctly. Diagnostic validation only; no usable stock/RADV performance comparison.

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart, with no detected failure or early process exit; the medians use only those. "Lost windows" counts gaps where SurfaceFlinger's 128-frame ring overflowed between two reads; those intervals are left out of every figure. Hz is the compositor's refresh rate during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | stock | 1 | error-box |  |  |  |  | 120 |  | 0.0 | 0.0 | 44.0 | 672.0 | 0 | samsung | 1825 | 1101 | 44 |
| heaven | radv | 1 | ran | 57.4 | 13.3 | 16.7 | 88.7 | 120 | 0 | 82.6 | 729.7 | 73.5 | 1262.0 | 0 | radv | 2078 | 999 | 61 |

## Medians

- **heaven**: radv 57.4 FPS

## Runs that didn't count

- Run 1, heaven on stock: stopped at a message box (an error from the game or from Wine). Screen: `run1-heaven-stock-screen.png`.
