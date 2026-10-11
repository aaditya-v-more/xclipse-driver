# Game benchmark run 2026-10-11T15:52:47

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart; the medians use only those.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | GPU busy (%) | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | stock | 1 | error-box | 11.0 | 0.2 | 16.6 | 3.0 | 1.4 | 0 | samsung | 1965 | 1076 | 59 |
| heaven | radv | 1 | ran | 46.7 | 2.0 | 16.7 | 81.4 | 48.7 | 0 | none | 1986 | 969 | 60 |

## Medians

- **heaven**: radv 46.7 FPS

## Runs that didn't count

- Run 1, heaven on stock: stopped at a message box (an error from the game or from Wine). Screen: `run1-heaven-stock-screen.png`.
