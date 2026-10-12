# Game benchmark run 2026-10-12T13:23:39

Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the first 10 s after the first frame. A run counts (`ran`) only with 30 s of frames in a row, none more than 2 s apart, with no detected failure or early process exit; the medians use only those. "Lost windows" counts gaps where SurfaceFlinger's 128-frame ring overflowed between two reads; those intervals are left out of every figure. Hz is the compositor's refresh rate during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.

| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) | Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults | Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| heaven | radv | 1 | ran | 56.8 | 13.6 | 16.7 | 95.5 | 120 | 0 | 80.8 | 680.3 | 73.7 | 1272.6 | 0 | radv | 2027 | 969 | 59 |

## Medians

- **heaven**: radv 56.8 FPS

## Upgrade smoke check

Official GameNative v1.3.0-prerelease, source `231d1365cf1a09b7f1763cfaebd3906628ec1381`; installed APK matches the SHA256 in `third_party/gamenative-release.json`. RADV `xd-fc066b9`, DXVK `async-1.10.3`, BCn `none`, FEXCore Intermediate / Box64 Compatibility. Diagnostic RADV logging enabled. The 120-second run passed with 95.5 seconds of steady frames, zero GPU faults and sampling gaps, and correct textures in both inspected screenshots. This single smoke run does not establish an app-version or stock/RADV speed difference.
