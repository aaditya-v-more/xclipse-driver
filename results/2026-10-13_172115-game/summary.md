# Game benchmark run 2026-10-13T17:21:15

RADV diagnostic logging enabled; this adds CPU work.

Direct X11 diagnostic with DXVK info logging: identify repeatable exit before rendering; descriptor buffers remain disabled

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart, with no detected failure or early process exit; the medians use only those. "Lost windows" counts gaps where SurfaceFlinger's 128-frame ring overflowed between two reads; those intervals are left out of every figure. Hz is the compositor's refresh rate during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | radv | 1 | game-exited |  |  |  |  |  |  |  |  |  |  | 0 | radv | 2339 | 799 | 403 |

## Medians

- **heaven**: no valid runs

## Runs that didn't count

- Run 1, heaven on radv: the game exited; benchmark completion was not verified. Screen: `run1-heaven-radv-screen.png`.
