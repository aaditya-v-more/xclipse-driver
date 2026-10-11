# Progress

Last updated: 2026-10-11

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
  - **Fixed** by forcing Wine's built-in runtime, which keeps its imports in a separate section. Both Heaven containers now set `WINEDLLOVERRIDES=msvcr100,msvcp100=b`, and Heaven-stock is back on FEXCore 2603 like the other three.
  - The next launch got as far as Unigine's own error box: it couldn't open `A:/bin/A:/data/heaven_4.0.cfg`. Heaven resolves `-data_path` and `-engine_config` against its `bin` folder even when they start with a drive letter. The Exec Arguments now use `../` and `../data/heaven_4.0.cfg`, as Heaven's own launcher does.
  - `game.py` now saves a phone screenshot when a run ends without frames, since that usually means a message box.
- [ ] **Heaven gets further on both drivers, and fails differently on each (2026-10-11, `results/2026-10-11_013033-game`).**
  - **Stock: Samsung's driver can't create Heaven's textures.**
    - Unigine's error box lists DXVK refusing DXT1/DXT5 (BC1/BC3) textures and a 2048x2048 D24 depth texture, all with "Direct3D11 error: invalid arg". DXVK returns that when the driver reports the image as unsupported.
    - In `auto` BCn mode GameNative's wrapper passes BC1–BC3 straight to Samsung's driver and emulates only BC4–BC7 (the `VK_DRIVER_ID_SAMSUNG_PROPRIETARY && format <= 138` case in `wrapper_physical_device.c`). `WRAPPER_NATIVE_BCN` changes nothing here, because Samsung's driver doesn't report `textureCompressionBC`.
    - To try next: BCn Emulation `full` on Heaven-stock, which emulates BC1–BC3 too. The D24 failure may need something else.
  - **Radv: our driver hangs Heaven on an allocation the kernel refuses.**
    - Textures are created, then the screen stays black. Wine's log shows `radv/amdgpu: Failed to allocate a buffer: size 256 bytes, alignment 4 bytes, domains 8`. Domain 8 is GDS.
    - The Xclipse 940 runs as GFX10_3, and on GFX10–10.3 RADV counts some query results from NGG shaders with GDS atomics. That covers geometry-shader primitives in pipeline statistics, primitives-generated queries, and mesh-shader queries.
    - D3D11 pipeline-statistics queries through DXVK trigger it, and the queue then allocates the GDS buffer (`radv_queue.c`). Samsung's `sgpu` kernel driver has no GDS, so the allocation and the submission fail.
    - Every D3D11 game that uses pipeline statistics would hit this. Fixed; see the next entry.
  - **`game.py` called the stock run "ran" at 8.8 FPS over 15 s.** Those frames were Wine drawing the error box. A run needs a minimum stretch of steady frames before it counts.
  - **Open question:** on radv, RADV's image log showed `R8G8B8A8_UNORM` images with BC1 view formats (`fmt 37 ... list [133,134]`). That looks like the wrapper still emulating BC1 despite `WRAPPER_NATIVE_BCN=1`. GameNative 0.9.2's wrapper may predate that variable; check the launch settings in the next run's `results.json`.
- [x] **GameNative's Vulkan wrapper emulated BC textures for RADV too (2026-10-11).**
  - In `auto` mode (`WRAPPER_EMULATE_BCN=3`), GameNative's wrapper (`src/vulkan/wrapper/wrapper_physical_device.c` in GameNative/mesa) uses native BC textures only for Qualcomm, Turnip, and NVIDIA. Every other driver gets emulation even when it reports `textureCompressionBC`, unless `WRAPPER_NATIVE_BCN=1` is set.
  - So the radv runs so far decompressed BC textures that our driver supports. Samsung's driver has no BC support, so it needs emulation regardless.
  - All four containers now set `WRAPPER_NATIVE_BCN=1`. It doesn't change the stock runs, so the containers stay identical apart from the driver.
  - Native BC is one of the main reasons RADV should be faster in DXVK games. It may also bring GravityMark's memory use on radv under the limit.

- [x] **Our driver's queries no longer need GDS, and their counts match Samsung's (2026-10-11, `results/2026-10-11_090345`).**
  - New `xdtest` case `queries`. It draws 5 triangles twice: plainly, and through a geometry shader that emits each triangle twice. Each draw runs inside a pipeline-statistics query with all 11 statistics and, where the driver has `VK_EXT_primitives_generated_query`, a primitives-generated query. The counts are checked against the expected values.
  - The first run hung our driver's GPU (the kernel recovered; no reboot). Bisecting with the test's `XD_QUERY_STATS`, `XD_QUERY_PG` and `XD_QUERY_GS` switches found three separate problems:
    - **GDS.** On Xclipse, NGG shaders now count query results with global atomics into the command buffer's shader query buffer, as on GFX11, and the CP copies from memory instead of GDS. The queue never allocates GDS.
    - **Geometry-shader primitives counted twice** (20 instead of 10). The 940 counts NGG geometry-shader primitives in hardware, as GFX11 does, and RADV's shader emulation added its own count on top. The emulation is off on Xclipse.
    - **The primitives-generated query hung the GPU.** RADV's GFX10.3 path enables streamout and samples the streamout statistics (`EVENT_WRITE` `SAMPLE_STREAMOUTSTATS`). With a temporary switch, skipping the sample stopped the hang; enabling streamout (`VGT_STRMOUT_CONFIG`) alone was fine. Xclipse now uses RADV's GFX11 path, which counts in the shaders only.
  - Result: all 11 statistics are identical to Samsung's driver for both draws (including fragment-shader invocations), and primitives generated is 5 and 10.
  - **Risk left: transform feedback.** RADV's GFX10.3 transform-feedback queries use the same `SAMPLE_STREAMOUTSTATS` event, so they would hang the GPU too. GFX10.3 transform feedback also runs on the legacy geometry path, which hung with `RADV_DEBUG=nongg`. Both drivers expose `VK_EXT_transform_feedback`, and DXVK uses it for D3D11 stream output. It needs its own `xdtest` case.
- [x] **Transform feedback hung our driver's GPU (2026-10-11, `results/2026-10-11_091548`).** Fixed; see "Transform feedback works on our driver" below.
  - New `xdtest` case `xfb`: 5 instances of a triangle, each vertex writing its index and instance, captured with a counter buffer and a transform-feedback query. Samsung's driver passes every check. Ours hangs on the draw; the kernel recovers and the stock health check passes.
  - Cause: RADV runs GFX10.3 transform feedback on the legacy geometry path, and the 940 has no legacy streamout hardware. The GPL kernel's register map for this chip has GFX11's streamout counters and no GDS, GWS or OA (`docs/findings.md`).
  - `run.py` runs `xfb` last, so its hang doesn't cost the rest of a run.
  - The fix planned here was GFX11's NGG streamout without the `ds_ordered_count` lock, which was thought to need OA. As built, the lock works.
- [ ] **Heaven runs to the end on our driver, at 2 FPS; GravityMark still runs out of memory on both (2026-10-11, `results/2026-10-11_091657-game`).**
  - Driver `xd-ea7b521` (queries without GDS) is imported and selected in both radv containers.
  - **Heaven-stock, with BCn Emulation `full`:** the BC1/BC3 errors are gone, but Unigine still stops with a fatal error on the 2048x2048 D24 depth texture. `game.py` called it `too-short` (9 s of frames), as intended.
    - Neither driver supports `D24_UNORM_S8_UINT`, so DXVK falls back to `D32_SFLOAT_S8_UINT`.
    - Samsung's driver reports that format without linear filtering (format features 0xc601; ours 0x1d601). That's the likely difference; not confirmed yet.
  - **Heaven-radv ran the whole 297 s.** 594 frames: the median frame took 66.6 ms, but the average is 2.0 FPS with 1% lows of 0.2 FPS. Half the frames come within 67 ms, and the other half take about a second each.
    - Our driver's CPU time stayed under 1% of each second (its `[CPUSHARE]` log), at about 8 submissions per second.
    - There were no driver or kernel errors.
    - The Wine debug log was only 43 KB.
    - So neither driver CPU time nor logging explains the stalls. Next: measure GPU busy time during a run, to tell GPU-bound from waiting.
  - **GravityMark:** lmkd killed both runs. GPU memory peaked at 3663 MB on stock (after 5 s of frames at 49 FPS) and 3573 MB on radv (before its first frame). DXVK's `maxDeviceMemory` was 0 (no limit). Next: Max Device Memory 2048 MB in both GravityMark containers.
  - `WRAPPER_NATIVE_BCN=1` reached all four launches (`launch_settings` in `results.json`).

- [x] **Transform feedback works on our driver (2026-10-11, `results/2026-10-11_103659`).**
  - Transform feedback now runs in the NGG shaders with GFX11's streamout counters, which is what this chip has. Geometry shaders always use NGG now; the legacy path is the one that hangs.
  - **The `ds_ordered_count` lock works without an OA allocation.** GFX11 uses it to order the workgroups' buffer writes. `RADV_XCLIPSE_XFB` (or `debug.radv_xclipse_xfb`) picks the variant, for A/B:
    - `1` (default): GFX11's lock as is. Writes follow draw order.
    - `2`: no lock. Workgroups take buffer space in arrival order: tens of thousands of vertices land out of order, and the two buffers disagree.
    - `3`: the lock encoded as on GFX10, with the GS shader type. Same results as `1`.
    - `0`: the legacy path, which hangs.
  - **New tests `xfbmany` and `xfbmanygs`.** They draw 24576 triangles in two draws, the second resuming from the counters the first stored (as DXVK does), from the vertex shader or from a geometry shader. Every vertex index is captured into two buffers. The tests check for missing, duplicated, torn and out-of-order vertices and for writes past the end, and they check the counters and the transform-feedback query.
  - **Results:**
    - In mode 1, all 32 runs pass: 20 with the default wave64 and 12 with wave32 vertex and geometry shaders (`RADV_PERFTEST=gewave32`). Mode 3 passes 10 of 10.
    - `xfb`, `xfbmany` and `xfbmanygs` pass on both drivers in the full run.
    - No GPU hang or page fault in those runs.
    - The three tests moved from `run.py`'s hang set to the correctness set.
  - **A bug found on the way:** RADV set the hardware VS streamout-enable bits in RSRC2 for NGG shaders too. NGG runs as the hardware GS, where those bits are EXCP_EN, so they turned on float exceptions. Fixed; it didn't change the test results.
  - Until the fix in the next entry, about half of the `xfbmany` and `xfbmanygs` runs still failed (`results/2026-10-11_094708`).
- [x] **Small wave64 pixel shaders overwrote other waves' registers (2026-10-11).**
  - **Symptom.** In about half the runs, a run of 10–32 triangles in some workgroups came out wrong, always in the lower 32 lanes of a wave.
    - From a geometry shader, the third corner held vertex 0 or nothing. `v1`, which holds the third vertex index, was wrong.
    - From the vertex shader in passthrough mode, whole triangles were missing: `v0`, the packed primitive, was wrong.
  - **Bisection with switches:**
    - Ruled out: the ordered lock (mode 2 failed the same way), missing LDS waits (`ACO_DEBUG=force-waitcnt`), the RSRC2 bits, and the number of input VGPRs the hardware loads for the geometry shader.
    - Giving every shader more VGPRs (`RADV_XCLIPSE_VGPRPAD=4`) fixed it. Padding only the vertex and geometry stages didn't; padding only the pixel shader did.
    - The test's pixel shader writes a constant color. It gets 8 VGPRs in wave64, with one dummy input VGPR (`LINE_STIPPLE_TEX`, which RADV enables when a pixel shader reads no inputs).
    - With two dummy input VGPRs (`PERSP_CENTER`), every run failed and `v0` was hit too. The GPU hung 4 times (`gpu_info` hang count 434→438, timeouts 4→8); the kernel recovered each time.
    - Never seen with a wave32 pixel shader, a 12- or 16-VGPR one, or a 16-VGPR one that reads 8 input VGPRs (`XD_XFB_FRAG=1`).
  - **Fix:** on Xclipse, every wave64 shader gets at least 16 VGPRs. A shader that small isn't limited by VGPRs (32 waves would fit, and the limit is 16 per SIMD), and the speed tests didn't change.
  - **Why it matters beyond transform feedback.** The overwritten registers are other waves' first inputs: NGG primitives and vertex indices, compute thread IDs, pixel barycentrics.
    - Any game that draws with a tiny pixel shader (constant colors, or a dummy color output on a depth pass) could get broken triangles, wrong compute results or GPU hangs.
    - The radv game runs so far, Heaven's 2 FPS included, used drivers without this fix.
- [x] **Heaven's 2 FPS on radv: a GPU page fault, and so a full GPU reset, about every frame (2026-10-11, `results/2026-10-11_104551-game`, `results/2026-10-11_105144-game`).**
  - **Driver `xd-3e88e3e` loads in GameNative.** Smoke run `104551`: Heaven on radv at 2.1 FPS, median frame 41.7 ms, 177 s of steady frames. The register fix didn't change Heaven.
  - **The timed A/B `105144` stopped after its first round,** when work moved to the Windows PC. `game.py` wrote its results only at the end, so only `progress.log` and the screenshots survive. It now keeps finished runs on Ctrl+C.
    - Heaven: **stock 40.0 FPS over 296 s, radv 2.0 FPS over 283 s.** Stock Heaven ran this time, with no D24 fatal error.
    - GravityMark: lmkd killed both runs again; radv rendered 17.9 FPS for 1 s first. **Max Device Memory 2048 MB has no effect** on it: GravityMark runs with `-vk`, native Vulkan, so DXVK's `maxDeviceMemory` doesn't apply.
  - **Cause of the stalls.** While Heaven runs on radv, the kernel's page-fault count (`gpu_info` GPF_S) rises by 1–1.5 a second, and the reset count with it.
    - The counts: 430 before the smoke run, 1283 after the A/B; mid-run, 1016 → 1032 in 10 s.
    - On this kernel every page fault queues a full GPU reset and cancels the faulting job (`docs/findings.md`). That's the 1 s stall after every frame or two.
    - The faulting buffer is unknown. The kernel gives the addresses only to the process that faulted, through an `AMDGPU_INFO` query, so the driver has to log them.
  - **New measurements in `game.py`:**
    - GPU busy % and clock once a second (`/sys/kernel/gpu/gpu_busy`, `gpu_clock`). The summary shows the mean busy share after warm-up.
    - The present timestamps, in `results.json`.
    - GPU page faults and resets per run, read from `gpu_info` before and after. They're a new summary column.
  - **Driver `xd-6563904` logs every fence or semaphore wait of 20 ms or more**: `[WAIT]` lines, tag `RADV_WAIT`, with the caller's offset in the library for `llvm-addr2line`. It's built but not on the phone. The faults explain the stalls, so it's a second-line tool now.
  - **GameNative's wrapper** (source: GameNative/mesa, branch `wrapper-25`, `src/vulkan/wrapper`; read, not verified on the phone):
    - Wine needs placed memory maps for 32-bit (WoW64) games like Heaven. The wrapper always advertises `VK_EXT_map_memory_placed`, unless `WRAPPER_DISABLE_PLACED=1`.
    - It backs host-visible memory with dma-bufs from `/dev/dma_heap/system-uncached` and imports them into the driver. `WRAPPER_DMAHEAP_CACHED=1` switches to the cached heap; `WRAPPER_RESOURCE_TYPE` picks dma-buf, AHB or opaque fd.
    - That's why `mem_info` shows Heaven with only about 60 MB of GPU memory.
    - GameNative 0.9.2 bundles four wrappers (`wrapper`, `wrapper-v2`, `wrapper-legacy`, `wrapper-leegao`); which one the containers use isn't known.
    - Heaven-radv's launch environment includes `WRAPPER_RESOURCE_TYPE=auto`, `WRAPPER_EMULATE_BCN=3`, `WRAPPER_NATIVE_BCN=1`, `WRAPPER_VK_VERSION=1.3.0`, `WRAPPER_MAX_IMAGE_COUNT=0`, `WRAPPER_DISABLE_PRESENT_WAIT=0`, `MESA_VK_WSI_PRESENT_MODE=mailbox`, `MESA_SHADER_CACHE_DISABLE=false` and `DXVK_ASYNC=1`.
- [x] **Heaven's page faults were all at GPU address 0, from three registers the port never wrote (2026-10-11, fix `fc066b9`).**
  - **The fault logger** (`f41503a`, `3d3e5ea`). With `debug.radv_xclipse_log` ≥ 1 (`game.py --radv-log`) or `RADV_XCLIPSE_LOG=1` (xdtest), the winsys asks the kernel for its page-fault addresses after every submission, and once more when the device is destroyed.
    - It logs one `[VMFAULT]` line per fault under the logcat tag `RADV_VMFAULT`: the buffer that holds the page, or the nearest buffers on either side of it, and any freed buffer that held it. Each buffer comes with its owner (the Vulkan object type) and the code that created it.
    - `pipeline/vmfault.py` groups the lines by buffer and names the creating code with `llvm-addr2line`. It needs the unstripped library from the same build.
    - The poll at teardown is there because the GPU runs a submission after the poll that follows it. A test that submits once and exits would never see its fault. xdtest now destroys the device when a test fails, so that poll runs.
  - **Heaven on radv with the logger** (driver `xd-f41503a`, `results/2026-10-11_124544-game`): 1.9 FPS over 297 s and 418 page faults by `gpu_info`. The log has 417, **every one at page 0**. So shaders were reading or writing through a null pointer, not running off the end of a buffer.
  - **Three new tests**, from the suspects list: `tess` (the render test's triangle, tessellated), `scratch` (4096 triangles whose vertex shader keeps positions in a 1 KiB local array, which has to live in scratch memory) and `scratchcs` (the same in a compute shader). All three faulted on radv and passed on stock.
  - **Cause 1, merged-stage pointers.** The HS stage (which runs LS+HS) and the GS stage (which runs NGG VS, TES and GS) load s[0:1], the pointer to their ring descriptors and scratch address, from `SPI_SHADER_PGM_LO/HI_HS` and `_GS`, as on GFX11. The port wrote it to GFX10's `SPI_SHADER_USER_DATA_ADDR_LO/HI_*`, so s[0:1] was 0.
    - `tess` faulted (`130000`) and passed with the pointer in the GFX11 registers (`RADV_XCLIPSE_RINGPTR=2`, `130152`).
  - **Cause 2, graphics scratch base.** The hardware takes each wave's scratch address from `SPI_GFX_SCRATCH_BASE_LO/HI`, as on GFX11. The port never wrote them, so scratch was at address 0 (`130349`).
    - With them written, the 3-vertex first version of `scratch` passed (`130653`), but the 4096-triangle one still faulted 27 MB past the end of a buffer (`130847`).
    - **`SPI_TMPRING_SIZE` counts WAVESIZE in 1 KiB units, as on GFX10**, not GFX11's 256 bytes. A sweep: 256-byte units failed (`130931`, `130946`); 1 KiB units passed, with WAVES per shader engine or in total (`130937`, `130941`). Those two are the same here, since the chip has one shader engine.
    - The shaders' own GFX10 scratch setup (`s_setreg` of `FLAT_SCR`) makes no difference: `scratch` passes with it and without it (`RADV_XCLIPSE_SCRATCHINIT=0`).
  - **Cause 3, compute scratch base.** Compute takes its scratch base from `COMPUTE_DISPATCH_SCRATCH_BASE_LO/HI`, which the port never wrote either. `scratchcs` faulted at 0x4e78ad652000, far from all of our buffers (`131643`), probably an address the previous process left there.
    - The first `scratchcs` passed without testing anything (`131559`). Its `uvec4[64]` array had only `.x` read, so the compiler shrank it to 256 bytes and kept it in VGPRs (`RADV_DEBUG=shaderstats`: scratch size 0). The test now uses `uint[256]` (scratch 64 KiB per wave).
  - **Fix (`fc066b9`, in `radv_queue.c`).** When `gfx11_shader_core` is set:
    - The merged-stage pointer goes to `PGM_LO_GS/HS` instead of the GFX10 registers.
    - Graphics scratch uses GFX11's three-register sequence, with the GFX10 WAVESIZE units it already had.
    - Compute scratch also writes `COMPUTE_DISPATCH_SCRATCH_BASE_LO/HI`. The scratch address stays in user data 0–1 too, since the shaders keep GFX10's argument layout.
    - **All 17 tests pass on both drivers, with no GPU fault** (`results/2026-10-11_131855`). The speed tests didn't change.
  - **Heaven on radv with `xd-fc066b9`: 51.8 FPS over 285 s, 0 GPU faults** (`results/2026-10-11_133724-game`, one run with `--radv-log`). Before the fix: 1.9 FPS. Stock ran Heaven at 40.0 FPS in the last A/B (`105144`); the timed A/B is next.
    - Median frame 16.7 ms (the 60 Hz display rate), GPU busy 61%. `gpu_info` and the fault log both show 0 faults.
    - 1% low 3.7 FPS: there are 15 frame gaps over 250 ms. The two longest (16 s and 14 s) are in the first 25 s, while Heaven still loads. Mid-run, the longest are 3.5 s, 2.0 s and 1.0 s.
  - Graphics scratch is still slower on radv: the `scratch` test takes 10.2 ms of GPU time against stock's 3.3 ms. Compute scratch is even (14.3 ms against 14.8 ms).
  - **`pipeline/gn_driver.py`** now does the GameNative steps: it imports a driver zip in Driver Manager and selects it in containers, reading each change back (`install ZIP GAME...`, `select ENTRY GAME...`, `show GAME...`).
  - **Heaven's black geometry on radv was GameNative's BCn emulation, not the driver.** Heaven on radv drew every surface near-black with the sky intact (`133724-game`). The driver's texture, mip, BC, MRT, vertex and depth/stencil paths were ruled out with new `vertex`, `depth` and `mips` tests, all passing on both drivers (`160236`, `162020`, `162500`, `163011`); so were compression (`RADV_DEBUG=nohiz,nodcc,nofastclears,notccompatcmask`, `161412-game`), ACO optimizations (`161746-game`) and pipeline libraries (`nogpl`, `162632-game`). The radv log of a run (`163403-game/radv-log.txt`) then showed no BC texture at all: 300+ RGBA8_UNORM and RGBA8_SNORM mip-mapped storage images with BC1/BC3 view-format lists, which is GameNative's Wrapper decoding the game's BC textures in compute. The Heaven-radv container had **BCn Emulation = auto** (`WRAPPER_EMULATE_BCN=3`), Heaven-stock **full** (`=2`). With Heaven-radv on **none**, radv samples the BC textures itself and Heaven renders correctly: `164613-game`, 37.7 FPS over 63 s, 0 faults. The 51.8 FPS at `133724-game` was measured with black textures, so it doesn't count.
    - The decoder is `Pipetto-crypto/bcn_layer` (MIT), GameNative's "BCn compute" Vulkan layer. In auto mode it only trusts native BC on Samsung's driver (BC1–3) and Qualcomm or Turnip (all), so RADV gets every BC format emulated. GameNative skips the layer only for Adreno or for the `wrapper-gamenative` driver on a Samsung GPU. Per upload region, it writes one mip of the substituted RGBA8/RGBA8_SNORM/RGBA16F storage image through a `writeonly image2D` with no format qualifier, reading the blocks from the app's staging buffer bound as a storage buffer at the region's `bufferOffset`, between its own barriers (TRANSFER_DST→GENERAL→TRANSFER_DST). Each decode allocates a descriptor set from a 32-set pool sized for one set, so a pool is created per decode and never freed.
    - **Heaven A/B with BCn Emulation = none on both containers** (`165352-game`, 3 runs each, 90 s): **stock 35.6 FPS, radv 37.2 FPS median (1.05x)**, 0 faults. Stock fell run by run (49.2, 35.6, 24.9) while radv held (42.4, 37.0, 37.2); the overlay showed stock at 3.3–4.7 W and radv at 2.0–2.1 W. The earlier stock-only run was 47.1 (`165122-game`). Stock renders Heaven's clouds as black blotches with its native BC decoding (none) but not with full emulation; radv renders them correctly. The 40.0 FPS stock number from `105144-game` was with full emulation, so the only fair pair is this A/B.
    - New `storage` test (`storage_write.comp`, `storage_read.comp`): seven storage formats (RGBA8, R8, RGBA8_SNORM, RG8_SNORM, RGBA16F, RGBA32F, R32_UINT) × 10 mip levels of 512², one dispatch per level through a view of that level, between the layer's barriers, the data hashed from a constant, a storage buffer, a uint texel buffer or an RGBA8 texel buffer; every level is copied out and compared byte for byte, then read through a sampler in SHADER_READ_ONLY_OPTIMAL. **Passes on both drivers** (`170957`), so the decode's storage writes, barriers and sampled reads aren't it.
    - Open: why the layer's decode is black on radv. Next split: Heaven on radv with BCn Emulation = full instead of auto.
    - `gn_driver.py options GAME LABEL` lists the choices of a dropdown on the Graphics tab, and `set GAME LABEL VALUE` picks one, saves, and reads it back (BCn Emulation: none, partial, full, auto). `args GAME...` sets Exec Arguments to game.py's.
    - `mips` now also reads every level with `textureGrad` (seven address operands, so GFX11's partial-NSA encoding, which the Xclipse 920 can't decode and gets `gfx10_nsa` for). It passes on the 940, which therefore decodes partial NSA.

## Optimization targets (from the speed tests)

1. **GPU per-draw overhead** (5× at first, 2× in `2026-10-11_103659`).
   - **NGG culling is most of it.** With `RADV_DEBUG=nonggc`, GPU cost per draw drops from 0.298 to 0.122 µs (Samsung: 0.055 µs; result `2026-10-10_184237`).
   - Next: decide when NGG culling is worth it on this GPU, for example only above a vertex-count threshold, as desktop RADV does on some chips. Then find the remaining 2×.
   - **Don't use `RADV_DEBUG=nongg`** (the legacy geometry path). It hung the 940's GPU on the first draw batch (result `2026-10-10_184254`). The kernel recovered, `gpu_info` showed hang +1 and timeout +1, and Samsung's driver passed right after. The runner now detects hangs from those counters, stops testing our driver for that run, and health-checks the GPU with the stock driver.
2. **CPU cost per draw and per pipeline bind** (2–3× at first; in `2026-10-11_103659`, 1.6× per draw and about equal with a pipeline switch). These matter for DXVK games that issue thousands of draws per frame.
3. **Opaque fill:** equal to Samsung's (25.5 Gpixel/s) in every full run since `2026-10-10_184607`. The −14% came from one earlier run.

## Notes for unattended runs

This PC's internet usually comes from the phone's hotspot. If a GPU hang reboots the phone, the hotspot stays off, and adb can't turn it back on without root. The runner keeps going locally, but the Claude session is offline until someone taps the hotspot on. For fully unattended runs, put the PC on other internet, such as home Wi-Fi or Ethernet.

## Next steps

1. **Check the fix in Heaven.** Run Heaven on radv once with `--radv-log`, then `vmfault.py`. If faults remain, the log names their buffers.
2. **Timed game A/B again** (`game.py --runs 3`). The new `GPU faults` column should read 0 for both drivers.
3. **GravityMark's memory:** Max Device Memory doesn't reach it (`-vk`). Try fewer asteroids (`-asteroids`) in both containers' Exec Arguments, or another native-Vulkan benchmark.
4. **GPU cost per draw:** decide when NGG culling pays off on this GPU (optimization target 1).
5. Report Winlator-style results upstream on JimVulkan/radv-xclipse PR #6, as the maintainer asked. Posting there needs the user's go-ahead.
