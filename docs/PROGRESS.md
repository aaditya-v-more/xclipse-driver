# Progress

Last updated: 2026-10-10

## Goal

Play Windows games in Winlator (or a better Android PC-emulation app) on the Galaxy S24 FE with measurably higher performance than Samsung's own driver allows. The way to get there is an open-source Vulkan driver for the Xclipse 940, like Turnip for Adreno. Success means a repeatable benchmark A/B test in which our driver beats the best Samsung-driver setup, which is Samsung's driver plus a BCn layer (ExynosTools or Wrapper + bcn_layer).

## Decisions

- **Driver base:** `driver/`, a git subtree of radv-xclipse branch `xclipse940`. That's JimVulkan's RADV port (Mesa 26.3.0-devel; works on the 920 and 530) plus yoro1836's 940 bring-up from PR #6:
  - It uses the 530's register map and turns prefetches off.
  - It reportedly runs Eden without GPU hangs.
  - It hasn't been tested in Winlator; that's the test the maintainer asked for.
- **Second reference:** `third_party/Xclipse940-RADV`. Its 940 bring-up is deeper (Mesa 24.3.4) but only compute works so far. Mine it for fixes.
- **Build:** natively on Windows with the portable `tools/` folder and `driver\build.cmd`. No WSL is needed.
- **Why RADV should win:** Samsung's driver lacks BCn, maintenance5/6, mutable descriptors, and float64. That keeps it on DXVK 2.x and forces BCn emulation layers. Details are in `docs/research.md`.
- **Delivery:** a zip with `meta.json` and `vulkan.radeon.so`, loaded through adrenotools-style custom-driver support. That loader is vendor-agnostic.
- **Benchmarks:** GravityMark (Vulkan and DX11 modes) and Unigine Heaven 4.0 in Winlator, one fixed game scene with the DXVK HUD, and 3DMark Wild Life Extreme natively as a drift control. Use DXVK 2.7.1 for both drivers, at least 3 runs each, and report the median and 1% lows.
- **Stay clean-room** (see `docs/research.md`).

## Done

- [x] ADB is installed and on PATH; the phone is authorized over USB (serial `RZCY90ZYQXZ`).
- [x] Read-only GPU survey of the phone: `recon/` and `docs/findings.md`.
- [x] Research on existing efforts, the kernel driver, the emulation stack, and benchmarks: `docs/research.md`.
- [x] Reference repos cloned into `third_party/` (see `third_party/MANIFEST.md`).
- [x] Portable toolchain in `tools/`, all downloads verified: NDK r29, Python 3.13.16 with meson 1.12.1, ninja, mako, and packaging, and glslang 16.6.0.
- [x] Private GitHub repo `aaditya-v-more/xclipse-driver`.
- [x] **First driver build (2026-10-10).** `driver\dist\radv-xclipse-26.3.0-devel.zip`: Vulkan 1.4.358, 4.2 MB, SHA-256 `EB53754F…0847`. It contains `meta.json`, `vulkan.radeon.so`, and `NOTICE.txt`. A clean build takes about 1.5 minutes. The Windows build needed the NDK's `llvm-nm` added to `build.cmd` and PyYAML.

- [x] **Automated on-device pipeline (2026-10-10).**
  - `tests/xdtest.c` loads either driver: the stock one through `libvulkan.so`, ours through its Android HAL entry point.
  - `pipeline/run.py` builds, deploys, runs each test on both drivers, survives phone reboots, and writes `results/<time>/`.
  - The user approved automated GPU testing on the phone.
- [x] **Probe passes on the 940** (`results/2026-10-10_181408` and `results/2026-10-10_181508`):
  - `info`, `device`, `submit`, `compute` (65,536 elements), and `render` (an offscreen triangle, every pixel correct) all pass on both drivers, with no hangs.
  - RADV exposes Vulkan 1.4.358 and 224 extensions; stock exposes 1.3.279 and 154.
  - RADV compiles the compute pipeline in 2.7 ms against 54.6 ms, and the graphics pipeline in 5.0 ms against 8.1 ms.

- [x] **Driver-level speed tests** (`alu`, `bandwidth`, `fill`, `draws`, `compile`; results `2026-10-10_183930` and `2026-10-10_184117`). Each figure is the median of 5 runs, Samsung first, then RADV:

  | Metric | Samsung | RADV | |
  |---|---|---|---|
  | FP32 throughput | 2,423 GFLOPS | 2,420 GFLOPS | equal |
  | Shader-copy bandwidth | 48.4 GB/s | 47.2 GB/s | equal |
  | `vkCmdCopyBuffer` | 36 GB/s | 48.8 GB/s | RADV 1.35× |
  | Blended fill at 1080p | 12.8 Gpx/s | 12.8 Gpx/s | equal |
  | Opaque fill at 1080p | 25.5 Gpx/s | 22.0 Gpx/s | RADV 14% slower |
  | GPU cost per tiny draw | 0.055 µs | 0.298 µs | **RADV 5× slower** |
  | CPU cost per draw | 0.058 µs | 0.114 µs | RADV 2× slower |
  | CPU cost per draw with a pipeline switch | 0.29 µs | 0.80 µs | RADV 2.8× slower |
  | Pipeline compile (median / first) | 12.7 / 26.7 ms | 4.8 / 6.9 ms | **RADV 2.7–3.8× faster** |

  - The first FP32 shader let AMD's compiler (in Samsung's driver) merge identical chains and lanes, so it showed an impossible 6.9 TFLOPS. Now every lane is distinct.
  - Both drivers hit about 2.42 TFLOPS. The GPU's max clock read 800 MHz throughout.

## Optimization targets (from the speed tests)

1. **GPU per-draw overhead (5×).** Suspects:
   - the 940 branch turning prefetches off
   - extra packets per draw in the Xclipse context-init or "titan" register paths
   - how push constants are emitted

   Compare against `third_party/Xclipse940-RADV` and capture a command stream with `RADV_DEBUG`.
2. **CPU cost per draw and per pipeline bind (2–3×).** These matter for DXVK games that issue thousands of draws per frame.
3. **Opaque fill (−14%).** Check DCC and render-backend settings (`RADV_XCLIPSE_DCC*`).

## Notes for unattended runs

This PC's internet usually comes from the phone's hotspot. If a GPU hang reboots the phone, the hotspot stays off, and adb can't turn it back on without root. The runner keeps going locally, but the Claude session is offline until someone taps the hotspot on. For fully unattended runs, put the PC on other internet, such as home Wi-Fi or Ethernet.

## Next steps

1. **Driver-level performance tests** in `xdtest`, fully automatic:
   - compute ALU throughput
   - memory bandwidth
   - fill rate
   - draw-call CPU overhead
   - pipeline compile time over many shaders

   Results get tracked per run.
2. **Game-level A/B in GameNative** (`app.gamenative`, already installed; v1.3.0-prerelease is the latest). It's a Winlator-based Steam launcher with Winlator's `AdrenotoolsManager`, so it should accept our driver zip. The user does a one-time setup: install the zip, then make one container with our driver and one with the stock wrapper. After that, automation launches the benchmark over adb and collects FPS logs (`DXVK_HUD`). The fallback app is Bannerlator 3.1.6 (525 MB), which documents a hand-picked driver install.
3. **Benchmarks** need the user's approval to download: GravityMark (211 MB) and Unigine Heaven 4.0 (247 MB). Use DXVK 2.7.1 for both drivers, at least 3 runs each, and report the median and 1% lows.
4. Fix what breaks, mining `third_party/Xclipse940-RADV`. Report Winlator-style results upstream on JimVulkan/radv-xclipse PR #6; the maintainer asked for exactly that.
