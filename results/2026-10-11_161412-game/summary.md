# Game benchmark run 2026-10-11T16:14:12

Android properties set: `debug.radv_debug=nohiz,nodcc,nofastclears,notccompatcmask`

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart; the medians use only those.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | GPU busy (%) | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | radv | 1 | ran | 44.9 | 1.8 | 16.7 | 52.7 | 57.7 | 0 | none | 2141 | 962 | 65 |

## Medians

- **heaven**: radv 44.9 FPS
