# Game benchmark run 2026-10-12T12:03:11

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart; the medians use only those. "Lost windows" counts gaps where SurfaceFlinger's 128-frame ring overflowed between two reads; those intervals are left out of every figure. Hz is the compositor's refresh rate during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | stock | 1 | too-short | 12.7 | 0.3 | 16.6 | 3.0 | 120 | 0 | 0.9 | 58.9 | 43.6 | 956.5 | 0 | samsung | 1955 | 1026 | 60 |
| heaven | radv | 1 | ran | 46.0 | 2.0 | 16.7 | 55.5 | 120 | 0 | 63.5 | 591.3 | 65.7 | 1207.2 | 0 | none | 1952 | 935 | 63 |
| heaven | radv | 2 | ran | 45.2 | 2.0 | 16.7 | 55.5 | 120 | 0 | 62.3 | 581.3 | 66.0 | 1156.7 | 0 | none | 2395 | 931 | 64 |
| heaven | stock | 2 | too-short | 13.5 | 0.3 | 16.6 | 2.7 | 120 | 0 | 0.5 | 51.0 | 48.5 | 942.9 | 0 | samsung | 1953 | 1027 | 61 |

## Medians

- **heaven**: radv 45.6 FPS

## Runs that didn't count

- Run 1, heaven on stock: under 30 s of steady frames. Screen: `run1-heaven-stock-screen.png`.
- Run 2, heaven on stock: under 30 s of steady frames. Screen: `run2-heaven-stock-screen.png`.

## Visual validation

The two stock runs show Heaven fatal-error dialogs (DXT1, DXT5 and D24 texture creation failures) with BCn Emulation `auto`; neither counts as a benchmark. The run was interrupted during the third stock launch. RADV rendered correctly in the inspected run-1 screenshot, at 46.0 and 45.2 FPS for the two completed baseline runs. Both used FEXCore `Intermediate`, Box64 `Compatibility`, and BCn Emulation `none`.
