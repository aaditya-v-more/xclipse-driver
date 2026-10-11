# Game benchmark run 2026-10-11T16:53:52

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart; the medians use only those.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | GPU busy (%) | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | stock | 1 | ran | 49.2 | 2.6 | 16.7 | 59.9 | 66.7 | 0 | samsung | 2254 | 998 | 61 |
| heaven | radv | 1 | ran | 42.4 | 1.8 | 16.7 | 55.3 | 61.9 | 0 | none | 2315 | 942 | 66 |
| heaven | radv | 2 | ran | 37.0 | 1.2 | 16.7 | 46.2 | 53.6 | 0 | none | 2302 | 940 | 65 |
| heaven | stock | 2 | ran | 35.6 | 1.0 | 16.7 | 41.3 | 51.0 | 0 | samsung | 2478 | 996 | 60 |
| heaven | stock | 3 | ran | 24.9 | 0.8 | 25.0 | 43.1 | 53.8 | 0 | samsung | 2474 | 1001 | 60 |
| heaven | radv | 3 | ran | 37.2 | 1.3 | 16.7 | 42.4 | 50.2 | 0 | none | 2279 | 931 | 65 |

## Medians

- **heaven**: stock 35.6 FPS, radv 37.2 FPS (radv/stock 1.05x)
