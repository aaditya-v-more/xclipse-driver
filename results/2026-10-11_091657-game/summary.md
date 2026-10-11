# Game benchmark run 2026-10-11T09:16:57

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart; the medians use only those.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | stock | 1 | too-short | 14.0 | 0.3 | 16.6 | 2.9 | samsung | 1927 | 1088 | 60 |
| heaven | radv | 1 | ran | 2.0 | 0.2 | 66.6 | 176.6 | radv | 1826 | 962 | 68 |
| gravitymark | stock | 1 | killed-low-memory | 49.2 | 3.5 | 16.7 | 4.8 | samsung | 619 | 1116 | 3663 |
| gravitymark | radv | 1 | killed-low-memory |  |  |  |  | radv | 575 | 593 | 3573 |

## Medians

- **heaven**: radv 2.0 FPS
- **gravitymark**: 
