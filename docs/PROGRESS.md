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

## Next steps

1. Build `driver/` with `call tools\env.cmd && driver\build.cmd` to get `driver\dist\*.zip`.
2. Pick and install the test app: a bionic Winlator fork that loads custom Vulkan driver zips on non-Adreno phones (WinXclipse, Cmod, or Bannerlator). Downloading the APK needs the user's approval.
3. Baseline: Samsung's driver with Wrapper + BCn layer or ExynosTools, run through the benchmark suite. Benchmark downloads (about 460 MB) need the user's approval.
4. Load our driver zip and run the same suite.
5. Fix what breaks, mining `third_party/Xclipse940-RADV`. Report results upstream on JimVulkan/radv-xclipse PR #6.
