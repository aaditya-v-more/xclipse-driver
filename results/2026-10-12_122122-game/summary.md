# Game benchmark run 2026-10-12T12:21:22

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart, with no detected failure or early process exit; the medians use only those. "Lost windows" counts gaps where SurfaceFlinger's 128-frame ring overflowed between two reads; those intervals are left out of every figure. Hz is the compositor's refresh rate during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | radv | 1 | ran | 44.6 | 1.8 | 16.7 | 54.0 | 120 | 0 | 62.2 | 568.6 | 63.0 | 1128.0 | 0 | none | 2258 | 955 | 66 |
| heaven | stock | 1 | too-short | 14.6 | 0.3 | 16.6 | 2.8 | 120 | 0 | 0.7 | 44.0 | 47.9 | 882.0 | 0 | samsung | 1894 | 1079 | 61 |
| heaven | stock | 2 | too-short | 12.4 | 0.3 | 16.6 | 2.8 | 120 | 0 | 3.1 | 87.6 | 48.7 | 1116.8 | 0 | samsung | 1771 | 1073 | 61 |
| heaven | radv | 2 | ran | 44.5 | 2.0 | 16.7 | 55.8 | 120 | 0 | 64.9 | 532.5 | 66.6 | 1176.3 | 0 | none | 1930 | 929 | 64 |

## Medians

- **heaven**: radv 44.6 FPS

## Runs that didn't count

- Run 1, heaven on stock: under 30 s of steady frames. Screen: `run1-heaven-stock-screen.png`.
- Run 2, heaven on stock: under 30 s of steady frames. Screen: `run2-heaven-stock-screen.png`.

## Preset experiment

Both containers used FEXCore and Box64 Performance presets. RADV remained at BCn none; stock at full. RADV measured 44.60 and 44.55 FPS (median 44.57), versus the earlier Intermediate/Compatibility median of 45.60 FPS: about 2.3% lower in this small sample. There is no demonstrated speed gain. GPU faults and sampling gaps were zero in both RADV runs. Stock failed with a D24 depth texture error; it is not a usable speed baseline. Original presets were restored before testing DXVK.

The previous runner failed to recognize Wine process names containing Windows paths, so its dialog checks were inactive. Raw statuses are preserved here; the screenshots confirm the stock failures. The runner was corrected after this experiment.
