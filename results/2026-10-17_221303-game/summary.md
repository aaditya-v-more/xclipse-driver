# Game benchmark run 2026-10-17T22:13:03

xd-85cd575 load hang hunt

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the game starts rendering for good (after loading stalls). A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart, with no detected failure or early process exit; the medians use only those. "Lost windows" counts gaps where SurfaceFlinger's frame ring overflowed between two reads; those intervals are left out of every figure. Hz is the compositor's refresh rate during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | radv | 1 | ran | 59.4 | 24.3 | 16.7 | 56.9 | 120 | 0 | 72.8 | 688.7 | 71.8 | 1698.3 | 0 | none | 1682 | 969 | 47 |
| heaven | radv | 2 | ran | 58.4 | 14.8 | 16.7 | 59.7 | 120 | 0 | 74.2 | 672.4 | 71.1 | 1703.2 | 0 | none | 1874 | 976 | 57 |
| heaven | radv | 3 | ran | 58.9 | 16.7 | 16.7 | 61.3 | 120 | 0 | 73.6 | 693.5 | 74.1 | 1673.4 | 0 | none | 2000 | 966 | 57 |
| heaven | radv | 4 | ran | 59.0 | 17.5 | 16.7 | 61.2 | 120 | 0 | 72.5 | 696.6 | 74.7 | 1699.5 | 0 | none | 1905 | 964 | 57 |
| heaven | radv | 5 | ran | 58.9 | 16.4 | 16.7 | 60.8 | 120 | 0 | 73.6 | 680.9 | 73.4 | 1653.3 | 0 | none | 1735 | 961 | 57 |
| heaven | radv | 6 | ran | 58.9 | 16.2 | 16.7 | 61.3 | 120 | 0 | 75.1 | 661.3 | 75.3 | 1613.7 | 0 | none | 1775 | 959 | 57 |
| heaven | radv | 7 | ran | 58.9 | 16.5 | 16.7 | 59.8 | 120 | 0 | 71.1 | 667.1 | 72.4 | 1422.4 | 0 | none | 1921 | 971 | 50 |
| heaven | radv | 8 | ran | 58.8 | 16.1 | 16.7 | 60.1 | 120 | 0 | 70.7 | 660.7 | 73.8 | 1489.6 | 0 | none | 1782 | 955 | 55 |

## Medians

- **heaven**: radv 58.9 FPS
