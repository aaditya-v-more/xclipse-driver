# Game benchmark run 2026-10-12T14:16:07

RADV diagnostic logging enabled; this adds CPU work.

GameNative v1.3.0-prerelease; RADV xd-fc066b9; DXVK 2.7.1; BCn none; original FEXCore Intermediate/Box64 Compatibility.

Diagnostic experiment: WRAPPER_DISABLE_PLACED=1. Other container settings retained. Stops on the first observed GPU fault/reset.

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart, with no detected failure or early process exit; the medians use only those. "Lost windows" counts gaps where SurfaceFlinger's 128-frame ring overflowed between two reads; those intervals are left out of every figure. Hz is the compositor's refresh rate during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | radv | 1 | gpu-fault |  |  |  |  |  |  |  |  |  |  | 1 | radv | 2648 | 596 | 40 |

## Medians

- **heaven**: no valid runs

## Runs that didn't count

- Run 1, heaven on radv: the GPU reported a page fault or reset.
