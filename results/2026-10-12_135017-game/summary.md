# Game benchmark run 2026-10-12T13:50:17

RADV diagnostic logging enabled; this adds CPU work.

GameNative v1.3.0-prerelease / 231d1365; RADV xd-fc066b9; DXVK 2.7.1, BCn none, original FEXCore Intermediate / Box64 Compatibility.

Black screen and 153 GPU page faults. Excluded. This run predates continuous GPU-fault monitoring; restored async-1.10.3 afterward.

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart, with no detected failure or early process exit; the medians use only those. "Lost windows" counts gaps where SurfaceFlinger's 128-frame ring overflowed between two reads; those intervals are left out of every figure. Hz is the compositor's refresh rate during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | radv | 1 | gpu-fault | 1.7 | 0.6 | 649.3 | 93.6 | 120 | 0 | 42.8 | 427.5 | 44.0 | 1125.2 | 153 | radv | 2006 | 988 | 52 |

## Medians

- **heaven**: no valid runs

## Runs that didn't count

- Run 1, heaven on radv: the GPU reported a page fault or reset.
