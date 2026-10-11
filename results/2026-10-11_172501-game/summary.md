# Game benchmark run 2026-10-11T17:25:01

Android properties set: `debug.radv_queue_disable=compute`

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart; the medians use only those.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | GPU busy (%) | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | radv | 1 | too-short | 35.8 | 1.0 | 16.7 | 18.3 | 41.6 | 0 | none | 2212 | 940 | 63 |

## Medians

- **heaven**: 

## Runs that didn't count

- Run 1, heaven on radv: under 30 s of steady frames. Screen: `run1-heaven-radv-screen.png`.
