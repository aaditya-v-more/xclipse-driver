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
