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
- **Build:** natively on Windows, macOS, or Linux with the portable `tools/` folder (`driver\build.cmd` or `driver/build.sh`). No WSL is needed.
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

- [x] **Steadier speed tests** (result `2026-10-10_184607`): at least 250 ms of warm-up, median of 7, 50,000 draws, GPU clock recorded.
  - FP32 throughput is equal (2,423 vs 2,420 GFLOPS).
  - Fill is **equal** (opaque 25.5 vs 25.5 Gpx/s, blended 12.8 vs 12.8). The earlier −14% was clock noise.
  - Bandwidth varies between about 36 and 49 GB/s for both drivers from run to run, because of memory clock scaling.
  - GPU per draw: Samsung 0.140 µs; RADV 0.237 µs, or 0.191 µs with `RADV_XCLIPSE_NGGC=0`.
  - CPU per draw: Samsung 0.152 µs; RADV 0.253 µs.
  - Compile: Samsung 13.1 ms; RADV 5.0 ms (median).
  - In the draws test the GPU clock ended at **500 MHz under RADV against 800 MHz under Samsung**. The sgpu governor (`sgpu_governor.c`) scales with busy time. RADV records commands more slowly, so the GPU idles longer between runs in this serial test and gets clocked down. This is partly a test artifact; game benchmarks will show whether it matters.
- [x] To compare NGG culling in games, use `adb shell setprop debug.radv_debug nonggc`, and clear it with `setprop debug.radv_debug ""`. The port reads `RADV_DEBUG`, `RADV_PERFTEST`, and `RADV_EXPERIMENTAL` from the `debug.radv_debug`, `debug.radv_perftest`, and `debug.radv_experimental` properties when an app can't set environment variables (`radv_instance.c`). A separate switch briefly added in 445fcf4 was removed as redundant.
- [x] **CPU profile of RADV's draw path** (simpleperf on the phone, `cpu-clock:u`, unstripped driver):
  - `radv_emit_all_graphics_states` 21%, `radv_CmdDraw` 12%, `radv_CmdBindPipeline` 9%, `radv_emit_graphics_shaders` 7%, `radv_emit_hw_ngg` 5%, `memcmp` 4.5%.
  - Push-constant path: about 12% in total.
  - Port-specific: cache-flush emission about 4%, `radv_log_msg` plus `ac_xclipse_log_level` about 2% (logging checks in the hot path while logging is off), and `ac_titan_sh_dw` (the register remap) about 2%.
  - To profile again: push `driver/build-android/src/amd/vulkan/libvulkan_radeon.so`, then run `simpleperf record -e cpu-clock:u -f 4000 ./xdtest --driver <that .so> --test draws`.
- [x] From the kernel source: `SGPU_INSTANCE_DATA` and `SGPU_MEM_PROFILE_ADD` only feed memory profiling to debugfs. `AMDGPU_WGP_GATING` sets device-wide WGP clock-on and always-on counts. It's global power state, so we leave it alone.

- [x] **Game-level test prepared** (see `docs/game-setup.md`):
  - GameNative 0.9.2 on the phone already has everything needed: Driver Manager zip import, the `CustomGames` scan folder, the `LAUNCH_GAME` intent, Exec Arguments, and per-container environment variables. There's no need to update it.
  - GravityMark 1.89 and Unigine Heaven 4.0 were unpacked on the PC without running their installers and pushed as four custom games (stock and radv each).
  - `pipeline/game.py` launches them over adb, counts presented frames from SurfaceFlinger, and pulls GravityMark's frame-time file and screenshot.
  - **Waiting on:** the one-time GameNative setup (import the driver zip, set Exec Arguments and driver per container).
- [x] **Phone prepared, then paused for a PC move (2026-10-10):**
  - The user added all four games with **Add Game** and imported the driver zip with Driver Manager.
  - The four containers still need their variant, driver, and Exec Arguments set.
  - The v0.9.2 source showed that the `LAUNCH_GAME` intent's `container_config` override can't carry the driver choice, because it resets `graphicsDriverConfig`. That per-container setup is therefore a one-time UI step.
  - Full state and the next steps are in `docs/HANDOFF.md`.
  - Added `tools/setup.ps1`, which recreates the verified toolchain on a new PC with one command.
- [x] **Works from Windows or Mac (2026-10-10):**
  - The session moved to a MacBook Air (Apple Silicon, macOS 27) with the same phone on USB.
  - Added `tools/setup.sh` and `tools/env.sh`, the macOS/Linux twins of `setup.ps1` and `env.cmd`. They install the same NDK r29, Python 3.13.16 (python-build-standalone), and glslang 16.6.0, with pinned checksums, into the same `tools/` layout.
  - `pipeline/run.py` picks the NDK host folder, `.exe` suffixes, and `build.cmd` or `build.sh` from the OS. `driver/build.sh` now names the NDK's `llvm-nm`, like `build.cmd`. Reports are written with LF on every OS, and `.gitattributes` pins script line endings.
  - First Mac build: a clean driver build plus three tests took 1 min 42 s. `run.py --build-driver --tests info,device,render` passes on both drivers (result `2026-10-10_234847`).
- [x] **GameNative containers configured (2026-10-11).** All four are `bionic`, with the same Wrapper. The stock containers use System; the radv containers use RADV Xclipse (Mesa 26.3.0-devel-xd-c8fed77). Exec Arguments were typed over adb and read back to confirm an exact match.
- [ ] **First GravityMark launch (stock): killed for lack of memory.**
  - The plain `LAUNCH_GAME` intent works. There's no sign-in screen in the way.
  - The first launch installs GameNative's Bionic components (about 30 s), then boots Wine, installs Mono, and runs the DRM step. GravityMark started 62 s after the intent and opened Samsung's Vulkan device 4 s later.
  - 18 s after GravityMark started, lmkd killed GameNative itself (`oom_score_adj 0`). The reason given: "device is low on swap ... and thrashing (300%)". By then it had already killed every background app, the launcher included.
  - The phone has 7.4 GB of RAM plus 8 GB of RAM Plus (zram). About 3 GB was available, with many of the user's apps open. The user was also using the phone during the launch.
  - Next: rerun with background apps closed and the phone left alone, logging free memory every few seconds. If it still runs out, cut GravityMark's load or find what in the Wine stack uses the memory.
  - `game.py` caveat: GameNative's SurfaceView exists from the "Booting..." screen on, so "first frame" includes loading. Count from the game's own first frames (GravityMark's `-times` file) before trusting averages.
- [x] **GravityMark doesn't fit in this phone's memory under Wine, on either driver (2026-10-11, result `2026-10-11_002143-game`).**
  - Both runs were killed by lmkd. After `am kill-all`, 3.1–3.4 GB was available. During loading, available memory fell by about 2.3 GB more than GameNative's processes grew (their RSS peaked at about 1 GB). That's GPU allocations, which RSS doesn't count.
  - Stock: GravityMark rendered (at about 25 FPS while initializing). The kill came 0.6 s after the user rotated the phone, which made GameNative recreate its surface on top of the load.
  - Radv: **our driver loaded in GameNative.** GravityMark's process logged `[BUILD] Mesa 26.3.0-devel` 28 s after launch. lmkd killed it 10 s later, with no rotation involved.
  - GravityMark's manual recommends "a dedicated GPU with 4+ GB of video memory". 50k asteroids is its smallest preset, and the Earth textures are a fixed cost. Moving to Heaven at low quality.
  - The phone reports GPU memory per process in `/sys/kernel/gpu/mem_info` and in total as `GpuTotal` in `/proc/meminfo`. `game.py` now samples both.
  - Fixed in `game.py`: on Android 16 the frames land on a child layer of GameNative's SurfaceView, so the script now reads whichever layer has frames. Before, a run could only end by timing out, and its FPS came out empty.
- [ ] **Heaven 4.0 crashes at startup under GameNative, on both drivers (2026-10-11).**
  - Heaven is 32-bit only (`Unigine_x86.dll`). GameNative runs it in Proton's WoW64 mode with FEXCore. The crash comes before any window or Vulkan device, so neither driver is involved.
  - GameNative's Wine log (Settings → Debug → Enable Wine Debug Logs, written to `/sdcard/Android/data/app.gamenative/files/wine_logs/wine_debug.log`) shows "page fault on execute access" at `msvcr100+0x1e80`, called from `unigine_x86+0x2d754`.
  - `msvcr100.dll` has its import table at RVA 0x1000, inside `.text`, and the crashing function is on that same 4 KB page. The loader writes that page, and afterwards the 32-bit emulation treats it as non-executable.
  - Didn't help:
    - Adding a 1280x720 size to Heaven's config file.
    - Renaming Heaven's bundled VC++ runtime. The prefix has the same native DLL.
    - FEXCore's "Denuvo" preset, which adds `FEX_SMCCHECKS=full`.
    - FEXCore 2609-0.
    - Box64 as the 32-bit emulator. Proton's WoW64 loader can't find its module (`c0000135`).
  - Next: force Wine's built-in runtime, which keeps its imports in a separate section. Both Heaven containers now set `WINEDLLOVERRIDES=msvcr100,msvcp100=b`, and Heaven-stock is back on FEXCore 2603 like the other three.
- [x] **GameNative's Vulkan wrapper emulated BC textures for RADV too (2026-10-11).**
  - In `auto` mode (`WRAPPER_EMULATE_BCN=3`), GameNative's wrapper (`src/vulkan/wrapper/wrapper_physical_device.c` in GameNative/mesa) uses native BC textures only for Qualcomm, Turnip, and NVIDIA. Every other driver gets emulation even when it reports `textureCompressionBC`, unless `WRAPPER_NATIVE_BCN=1` is set.
  - So the radv runs so far decompressed BC textures that our driver supports. Samsung's driver has no BC support, so it needs emulation regardless.
  - All four containers now set `WRAPPER_NATIVE_BCN=1`. It doesn't change the stock runs, so the containers stay identical apart from the driver.
  - Native BC is one of the main reasons RADV should be faster in DXVK games. It may also bring GravityMark's memory use on radv under the limit.

## Optimization targets (from the speed tests)

1. **GPU per-draw overhead (5×).**
   - **NGG culling is most of it.** With `RADV_DEBUG=nonggc`, GPU cost per draw drops from 0.298 to 0.122 µs (Samsung: 0.055 µs; result `2026-10-10_184237`).
   - Next: decide when NGG culling is worth it on this GPU, for example only above a vertex-count threshold, as desktop RADV does on some chips. Then find the remaining 2×.
   - **Don't use `RADV_DEBUG=nongg`** (the legacy geometry path). It hung the 940's GPU on the first draw batch (result `2026-10-10_184254`). The kernel recovered, `gpu_info` showed hang +1 and timeout +1, and Samsung's driver passed right after. The runner now detects hangs from those counters, stops testing our driver for that run, and health-checks the GPU with the stock driver.
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
