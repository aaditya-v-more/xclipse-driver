# Game benchmark run 2026-10-11T16:17:46

Android properties set: `debug.mesa.aco.debug=noopt,nosched,force-waitcnt,force-waitdeps`, `debug.radv_debug=nocache`

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart; the medians use only those.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | GPU busy (%) | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | radv | 1 | ran | 34.7 | 1.5 | 16.8 | 54.8 | 68.7 | 0 | none | 1908 | 947 | 64 |

## Medians

- **heaven**: radv 34.7 FPS
