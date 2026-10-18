# Game benchmark run 2026-10-17T22:34:47

xd-85cd575 load hang hunt, cold compiles

Android properties set: `debug.radv_debug=nocache`

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the game starts rendering for good (after loading stalls). A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart, with no detected failure or early process exit; the medians use only those. "Lost windows" counts gaps where SurfaceFlinger's frame ring overflowed between two reads; those intervals are left out of every figure. Hz is the compositor's refresh rate during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | radv | 1 | ran | 57.5 | 15.0 | 16.7 | 59.7 | 120 | 1 | 76.0 | 636.0 | 70.7 | 1761.1 | 0 | none | 1684 | 984 | 57 |
| heaven | radv | 2 | ran | 58.9 | 16.5 | 16.7 | 60.2 | 120 | 0 | 75.3 | 673.4 | 75.8 | 1569.6 | 0 | none | 1806 | 982 | 57 |
| heaven | radv | 3 | ran | 58.9 | 15.5 | 16.7 | 60.5 | 120 | 0 | 80.5 | 592.7 | 74.5 | 1546.8 | 0 | none | 1916 | 973 | 55 |
| heaven | radv | 4 | ran | 59.0 | 16.3 | 16.7 | 61.3 | 120 | 0 | 80.0 | 599.0 | 73.5 | 1572.0 | 0 | none | 2018 | 989 | 57 |
| heaven | radv | 5 | ran | 58.9 | 16.4 | 16.7 | 60.7 | 120 | 0 | 75.8 | 670.3 | 74.5 | 1602.6 | 0 | none | 1949 | 985 | 57 |
| heaven | radv | 6 | ran | 58.9 | 15.9 | 16.7 | 61.4 | 120 | 0 | 78.5 | 576.4 | 73.7 | 1504.0 | 0 | none | 1785 | 969 | 57 |
| heaven | radv | 7 | ran | 58.4 | 14.0 | 16.7 | 60.9 | 120 | 0 | 80.3 | 604.7 | 74.2 | 1580.1 | 0 | none | 1826 | 983 | 57 |
| heaven | radv | 8 | ran | 58.9 | 16.6 | 16.7 | 61.5 | 120 | 0 | 74.6 | 635.4 | 74.6 | 1594.2 | 0 | none | 2004 | 988 | 57 |

## Medians

- **heaven**: radv 58.9 FPS
