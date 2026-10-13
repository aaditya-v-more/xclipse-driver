# Progress

Last updated: 2026-10-12

## Goal

Play Windows games in Winlator (or a better Android PC-emulation app) on the Galaxy S24 FE with measurably higher performance than Samsung's own driver allows. The way to get there is an open-source Vulkan driver for the Xclipse 940, like Turnip for Adreno. Success means a repeatable benchmark A/B test in which our driver beats the best Samsung-driver setup, which is Samsung's driver plus a BCn layer (ExynosTools or Wrapper + bcn_layer).

**Restated by the user on 2026-10-11:** "the best performance for PC game emulation on this device, no matter what we have to do, without root." The driver is one lever. The emulator app and its settings, the DXVK version, the Vulkan wrapper, BCn handling, display pacing and thermals are all in scope, and a GameNative fork is acceptable. Root is the only exclusion.

## Decisions

- **Driver base:** `driver/`, a git subtree of radv-xclipse branch `xclipse940`. That's JimVulkan's RADV port (Mesa 26.3.0-devel; works on the 920 and 530) plus yoro1836's 940 bring-up from PR #6:
  - It uses the 530's register map and turns prefetches off.
  - It reportedly runs Eden without GPU hangs.
  - It hasn't been tested in Winlator; that's the test the maintainer asked for.
- **Second reference:** `third_party/Xclipse940-RADV`. Its 940 bring-up is deeper (Mesa 24.3.4) but only compute works so far. Mine it for fixes.
- **Build:** natively on Windows, macOS, or Linux with the portable `tools/` folder (`driver\build.cmd` or `driver/build.sh`). No WSL is needed.
- **Why RADV should win:** Samsung's driver lacks BCn, maintenance5/6, mutable descriptors, and float64. That keeps it on DXVK 2.x and forces BCn emulation layers. Details are in `docs/research.md`.
- **Delivery:** a zip with `meta.json` and `vulkan.radeon.so`, loaded through adrenotools-style custom-driver support. That loader is vendor-agnostic.
- **Benchmarks:** GravityMark (Vulkan and DX11 modes) and Unigine Heaven 4.0 in Winlator, one fixed game scene with the DXVK HUD, and 3DMark Wild Life Extreme natively as a drift control. Use DXVK 2.7.1 for both drivers, at least 3 runs each, and report the median and 1% lows. **As of 2026-10-11 the containers run DXVK `async-1.10.3` (2022), the only DXVK that GameNative 0.9.2 ships; selecting 2.7.1 from its dropdown extracts nothing (see the night section).**
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
    - Heaven on radv with BCn Emulation = **full** is black too (`171504-game`, 36.7 FPS), so it isn't the auto logic.
    - The layer hooks `vkCmdCopyBufferToImage` only (not `…2`), but radv's verbose log of a run (`debug.radv_xclipse_log=2`, `172034-game`) shows no `[COPY_BUF2IMG]` into any of the 290 substituted images, so its decodes were dispatched, on DXVK's upload command buffers, which radv runs on its compute queue family (qf=1).
    - New `bcn` test (`bcn_s3tc.comp`, the layer's own BC1/BC3 shader with its includes inlined): the layer's descriptor layout, one-set pools (32 sets, descriptors for one), the blocks in a storage buffer at 256-byte offsets, its barriers, one dispatch per mip of a 512² RGBA8 image, compared with a CPU decode within 2/255. **Passes on both drivers** (`172933`).
    - With radv's compute queue disabled (`debug.mesa.radv.queue.disable=compute`; the earlier `debug.radv_queue_disable` run, `172501-game`, used a name Mesa doesn't read) Heaven is still black (`173124-game`, 32.1 FPS). Not queue-specific.
    - Still open. What's left: whether stock ever runs the layer (GameNative skips it for `wrapper-gamenative` on Samsung GPUs), and the state the layer leaves on DXVK's command buffer (compute pipeline, set 0, 28 bytes of push constants, shared with the graphics bind point). The fix for users is in the layer or GameNative: treat `VK_DRIVER_ID_MESA_RADV` like Turnip, or set BCn Emulation to none for radv.
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

## Review and direction (2026-10-11, evening)

A read of the whole repo, the Heaven numbers and GameNative 0.9.2's source (`utkarshdalal/GameNative`, tag `v0.9.2`; `GameNative/GameNative` is not a repo), against the restated goal.

**What the Heaven A/B really says.** In `165352-game` the GPU was 50–67% busy on both drivers, 1% lows were under 3 FPS on both, and stock fell 49 → 36 → 25 FPS across its three runs while radv held 42 → 37 → 37. Heaven is 32-bit, so it runs through WoW64 and FEXCore's slowest path, and every frame is paced to 16.7 ms although the panel is in its 120 Hz mode (`dumpsys display` mode 1). So the run is CPU-bound, stall-bound and thermally drifting, and the 1.05× ratio is inside the noise. A GPU driver can't show a GPU-side win on that workload; its CPU cost per draw (1.6–2× Samsung's) is what counts there. The one striking observation, radv at 2.0–2.1 W against stock at 3.3–4.7 W for the same frame rate, is from single overlay samples and unverified.

**Both BC emulators were active in the black runs.** The launch environment of `171504-game` has `ENABLE_BCN_COMPUTE=1` (the `bcn_layer` Vulkan layer) *and* `WRAPPER_EMULATE_BCN=2` (the wrapper's own emulation, which has a GPU transcoder too: `WRAPPER_BCN_GPU`, `WRAPPER_BCN_POLICY`, `WRAPPER_BCN_UPLOAD` in `third_party/gamenative-mesa`). `WRAPPER_NATIVE_BCN=1` is meant to stand the wrapper down, but which decoder produced the RGBA8 images in the log was inferred from its hooks, not proven. In 0.9.2 the layer is enabled for every GPU except Qualcomm's (`XServerScreen.kt`, `getVendorID() != 0x5143`), so stock's working `full` run did go through it. The cheap check: `adb pull` GameNative's APK (`pm path app.gamenative`), unpack `graphics_driver/*.tzst`, and read the wrapper's and layer's strings and log tags. `none` is the right setting for radv either way.

**The wrapper is in the way for radv.** In 0.9.2 every custom driver loads through `wrapper_icd.aarch64.json`; only bundled Turnip gets a direct ICD. The wrapper caps the advertised Vulkan version at `WRAPPER_VK_VERSION=1.3.0` (so DXVK 3.x, which needs 1.4, can't run on radv through it), emulates `VK_EXT_map_memory_placed` with dma-bufs from `/dev/dma_heap/system-uncached` whenever the app enables it, even though RADV has the extension natively, patches SPIR-V, and masks features. Those are the gaps it exists to fill for Samsung's driver, and none of them applies to ours.

**Order of work, by expected payoff per hour:**

1. **Make the measurement mean something** (half a day, `game.py`): sample battery power (`/sys/class/power_supply/battery/current_now` and `voltage_now`), CPU and GPU clocks and the thermal zones once a second; a fixed cooldown between runs and alternating order; five runs; find what paces presentation at 60 Hz when the panel is at 120 (the X server, the wrapper's swapchain or Samsung's game settings) and lift it, or record Heaven's own frame counter from its log instead of SurfaceFlinger. Compare each driver's *best working* setup (stock with the layer on `auto`, radv on `none`), not identical settings. Confirm or refute the 2 W reading; if it holds, it is the headline.
2. **DXVK 2.7.1 on both containers** (a dropdown in 0.9.2; the plan chose it in `research.md`). It is the first test of the thesis that RADV's extensions pay off; `async-1.10.3` uses none of them.
3. **Take the wrapper out of radv's path.** First the env A/Bs it allows: `WRAPPER_DMAHEAP_CACHED=1`, `WRAPPER_DISABLE_PLACED=1` (if Wine's WoW64 copes), a 1.4 `vulkanVersion`. Then the real fix, a GameNative fork (base: `utkarshdalal/GameNative`, 1.3.0-prerelease, 2026-10-04) with a "RADV (Xclipse)" graphics-driver option: an ICD file pointing at the imported `vulkan.radeon.so`, no wrapper, BCn `none` by default. That also removes the black-texture problem for users and opens DXVK 3.x and vkd3d-proton (mutable descriptors) on radv only. The fork is the "thing" the user asked for if the measurements favour radv.
4. **Driver CPU overhead**, once the stack is right: the port's logging checks on the hot path (2%), cache-flush emission (4%) and register remapping (2%) from the simpleperf profile; then an NGG-culling threshold (GPU cost per tiny draw 2×) and graphics scratch (3×). Use `RADV_XCLIPSE_PERF` in a game run to see whether it's CPU- or GPU-bound before each change.
5. **A 64-bit DX11 benchmark** (Heaven is 32-bit and unrepresentative) and one D3D12 sample through vkd3d-proton, which Samsung's driver can't run well. GravityMark stays out unless `-asteroids` brings its memory under the limit.
6. **Close the BCn-black question with at most one experiment** (the push-constant diagnostic in `docs/HANDOFF.md`), then stop; the fix lives in GameNative or the layer, and the fork makes it moot.
7. **Upstream.** PR #6 got its first Winlator-style result from its author on 2026-10-11 (Ludashi 4.1, FurMark, through the wrapper), still open. Our four fixes (queries without GDS, NGG streamout, the wave64 VGPR floor, the GFX11 shader-setup registers) aren't there. Posting needs the user's go-ahead.

**Repo hygiene found in the same review:** `.claude/settings.json` is tracked (it syncs tool settings between the two computers, but it names the model and must go before the repo is made public); 37 commits carry attribution trailers and the subtree import includes one commit authored as "Claude" (upstream's, `fc1d219`), which matter the day this goes public; the local git identity differs between the two computers (no-reply address on the PC, Gmail on the Mac, enforced there by a pre-commit hook); the GravityMark screenshot `results/2026-10-11_105144-game/run1-gravitymark-radv-screen.png` shows the home screen; `results/` holds 87 MB of PNGs and grows with every run.

## Harness fixes, DXVK 2.7.1, and the phone's clocks (2026-10-11, night)

**Harness** (commit `0aad69b`, verified by `results/2026-10-11_212032` on both drivers and the smoke run `212059-game`): `game.py` detects SurfaceFlinger's 128-frame ring overflowing between reads and leaves those intervals out of every figure (`Lost windows` column) instead of counting them as one long frame; captures the phone log to a file for the whole run; pins the panel to 120 Hz (`--hz`) and restores the setting; samples GPU clock and temperature, the four CPU clusters' clocks and the battery; restores `stay_on_while_plugged_in` only when it read back a number; validates `--prop`. `run.py` flips the driver order every test, writes the report even when a run stops early, computes the ratio by driver name, and no longer lists skips as problems. `xdtest` frees its one-time command buffers. pip packages are pinned in both setup scripts. Only `summary.md` and `results.json` of each run stay in git from now on.

**Smoke run, radv, Heaven, 120 Hz** (`212059-game`): 37.9 FPS over 54 s, median 16.65 ms, 0 faults, GPU 50% busy at a mean 486 MHz and 57 °C, X4 mean 1230 MHz, A720s 1785 and 1819 MHz. Frame intervals after warm-up: 405 at ~8.3 ms, 1236 at ~16.7, 288 at ~25, 72 at ~33, 23 stalls over 100 ms. So the 16.7 ms median of the earlier runs was Heaven's own speed, not a 60 Hz cap: GameNative launches the wrapper with `MESA_VK_WSI_PRESENT_MODE=mailbox`, and its X server view redraws on damage (`com.winlator.renderer.GLRenderer`, `requestRender`), so SurfaceFlinger counts the game's frames up to the display rate. The workload is CPU-bound at mid clocks.

**The phone's clocks, read from the shell:** GPU table 252–1095 MHz, but `/sys/kernel/gpu/gpu_max_clock` (the SKI interface, `exynos_gpu_interface.c`) is 800 MHz and devfreq `max_freq` 1000 MHz; `trans_stat` shows 1.5 s at 1000 MHz over the phone's uptime and never 1095. CPU `scaling_max_freq`: A520 1824 of 1959 MHz, A720 2304 of 2592 and 2784 of 2900, X4 2784 of 3110; all set by the system at boot and owned by `system`. `cmd power set-fixed-performance-mode-enabled true` works from adb and locks the CPUs to 1536–1632 MHz and the GPU to 400 MHz (a reproducibility tool, not a speed-up); `sem_enhanced_cpu_responsiveness=1` changes nothing; `cmd game mode performance app.gamenative` is refused because the app doesn't declare the mode (available: standard, custom). `dumpsys thermalservice` reports AP, skin and battery temperatures. The kernel's `cl_boost` (`sgpu_governor.c`, to `cl_boost_freq` 999 MHz) only fires when the whole busy time is CU busy time, i.e. pure compute.

**DXVK 2.7.1 doesn't exist in GameNative 0.9.2.** `gn_driver.py set` picked it in both Heaven containers, and the A/B (`213525-game`) failed on both: radv black at 1.5 FPS, stock back at the home screen with no frames. The log says why: `ContentsManager: Total profiles for type DXVK: 0`, then `Extracting DXVK/D8VK DLLs for dxwrapper: dxvk-2.7.1` with nothing to extract. The APK bundles only `async-1.10.3`; the downloader (`DXWrapperDownloader.kt`, `dxwrapper_download.json`) was added after 0.9.2. Both containers are back on `async-1.10.3`, read back after saving.

**The emulator presets matter more than the driver for Heaven.** The containers run FEXCore preset Intermediate (`FEX_TSOENABLED=1`, `FEX_VECTORTSOENABLED=0`, `FEX_MEMCPYSETTSOENABLED=0`, `FEX_HALFBARRIERTSOENABLED=1`, `FEX_X87REDUCEDPRECISION=1`, `FEX_MULTIBLOCK=1`) and Box64 0.3.6 preset Compatibility (`BOX64_DYNAREC_SAFEFLAGS=2`, `FASTNAN=0`, `FASTROUND=0`, `X87DOUBLE=1`, `BIGBLOCK=0`, `STRONGMEM=1`), from `FEXCorePresetManager.java` and `Box86_64PresetManager.java` at `v0.9.2`. Performance turns TSO off (`FEX_TSOENABLED=0`, `FEX_HALFBARRIERTSOENABLED=0`) and for Box64 sets `SAFEFLAGS=1`, `FASTNAN=1`, `FASTROUND=1`, `X87DOUBLE=0`, `BIGBLOCK=3`, `STRONGMEM=0`, `CALLRET=1`.

## Claude Code and Codex support (2026-10-12)

Pulled `main` to `f23ca58` before making changes. Moved the existing project instructions into root `AGENTS.md` and made `CLAUDE.md` import it, so both agents share the same workflow, device rules, git identity, and commit/push convention. Claude's settings remain in `.claude/settings.json`. Added PowerShell build commands, guidance for switching agents, and checks that do not need the phone to `AGENTS.md` and the README.

Validation: all prior project instructions were preserved, the Claude import resolves, local documentation links and Claude's JSON settings validate, and `git diff --check` passes. From PowerShell, both pipeline `--help` commands and the `cmd /d /c` toolchain environment check passed (Python 3.13.16, Meson 1.12.1, Ninja 1.13.2). No driver rebuild or on-device run was needed for this documentation change.

## Preset automation and baseline checks (2026-10-12)

Resumed on Windows with the same S24 FE (`RZCY90ZYQXZ`), GameNative 0.9.2, and driver `xd-fc066b9`. Both Heaven containers were read back: DXVK `async-1.10.3`, BCn Emulation `none`, FEXCore 2603 with both 32-bit and 64-bit emulators set to FEXCore, FEXCore preset `Intermediate`, Box64 0.3.6 preset `Compatibility`. USB stay-awake remains 2. Do Not Disturb was temporarily enabled for UI automation (previously off).

`gn_driver.py` now accepts `get`, `options`, and `set` with `--tab Emulation` for the preset dropdowns; Graphics remains the default. All existing commands retain their arguments, and argparse adds help and rejects unsupported tabs before accessing the phone. The new tab navigation and a saved FEXCore preset change were verified on the phone; Python compilation and `git diff --check` passed. Commands are in `docs/game-setup.md`.

Baseline checks, before changing emulator presets:

- `results/2026-10-12_115904-game`: stock at BCn `none` reported 48.1 FPS, but screenshots show black surfaces. Stopped and excluded from the comparison.
- `results/2026-10-12_120311-game`: stock at BCn `auto` failed twice with Heaven texture-creation errors (DXT1, DXT5, D24). RADV at BCn `none` completed two runs at **46.03 and 45.17 FPS**, with 0 GPU faults and no sampling gaps; the inspected screenshot was correctly textured. GPU means were 65.7 and 66.0 C, with runs starting at 45 and 44 C. Interrupted during the third stock launch after the repeated failures.
- `results/2026-10-12_121240-game`: stock at BCn `full` failed both runs. The inspected screenshot no longer had DXT errors, but still failed to create the D24 depth texture. There is no usable stock baseline for a speed comparison yet. Each report records the exact container settings and the visual-validation limitation.

The preset comparison will use the valid RADV baseline and separately report whether stock launches. In this GameNative version, [FEXCore Performance](https://github.com/utkarshdalal/GameNative/blob/v0.9.2/app/src/main/java/com/winlator/fexcore/FEXCorePresetManager.java) disables TSO and half-barrier TSO; [Box64 Performance](https://github.com/utkarshdalal/GameNative/blob/v0.9.2/app/src/main/java/com/winlator/box86_64/Box86_64PresetManager.java) changes several emulation settings, so the combined preset test cannot isolate one knob's contribution.

## Preset result and reliable failure detection (2026-10-12)

`results/2026-10-12_122122-game`: FEXCore and Box64 Performance together measured **44.60 and 44.55 FPS** on RADV (median **44.57**, about 2.3% below the earlier Intermediate/Compatibility median of 45.60). Both runs had zero GPU faults and sampling gaps. The inspected RADV screenshot rendered normally. Stock, still at BCn `full`, failed both attempts with the D24 depth-texture error. This small sample demonstrates no preset speed gain; it does not support a stock/RADV speed comparison. Both containers were restored to FEXCore `Intermediate` and Box64 `Compatibility`, with all four settings read back after saving.

The user reported the error dialog and asked that automation handle failures. The old runner's executable check expected `Heaven.exe`, but Android reported `A:\\bin\\Heaven.exe`, leaving `seconds_to_game_process` unset and the early dialog/stall checks inactive. `game.py` now recognizes Windows paths and truncated process basenames. It detects GameNative exits, game exits, lost rendering surfaces, fatal GameNative/Wine log lines, and GPU faults; none count as successful runs merely because frames were captured. A process exit without verified benchmark completion is conservatively excluded. Two consecutive failed attempts skip the remaining runs for that benchmark/driver (`--max-failures`); a GPU fault skips it immediately. Reports are checkpointed after each completed run and on interruption.

Validation: five offline regression tests cover recorded Wine names, unrelated-app log filtering, failures after sustained frames, bounded retries, and report checkpoints. Compilation, command help, and diff checks passed. On the phone, `results/2026-10-12_123434-game` recognized Heaven's process at 6 seconds and caught the known stock fatal dialog as **`error-box`**, saving its screenshot and bounds `[934, 418, 1401, 658]`; zero GPU faults. This verifies the actual dialog path, while crash/exit paths are covered by simulated lifecycle checks. GameNative's normal force-stop between runs remains intentional.

## DXVK content import on GameNative 0.9.2 (2026-10-12)

With the user's approval, packaged the unmodified upstream DXVK 2.7.1 x86/x64 DLLs as a Winlator content package. `pipeline/package_dxvk.py` checks the release archive and license SHA256, checks PE architectures, and verifies every packaged byte. The initial archive lacked explicit directory entries: GameNative's extractor returned ENOENT opening `x32/d3d10core.dll`. Adding `x32/` and `x64/` directory entries fixed the import. The working `dxvk-2.7.1-gamenative.wcp` is 4,322,028 bytes, SHA256 `62a29ba39e38c2c150030c4872ce1191473e1d4cdaa9909b61ae61fc7c83d903`.

`gn_driver.py import-content` copies and checksum-verifies the file, refuses to overwrite a different existing phone file, imports through Contents Manager, and reopens that screen to verify installation. It also cancels a leftover document picker before restarting GameNative. The phone successfully listed **DXVK 2.7.1 (1)** after reopening. Both WCP attempts remain in `/sdcard/Download/xclipse/` as instructed. No new DXVK version has been selected or benchmarked: the user then requested updating GameNative from the latest GitHub release APK and pinning its matching source as a submodule. Both Heaven containers remain on bundled `async-1.10.3` and their original emulator presets; stock BCn is `full`, RADV BCn is `none`.

## Official GameNative update and pinned sources (2026-10-12)

Installed the newest published GitHub release, [GameNative v1.3.0-prerelease](https://github.com/utkarshdalal/GameNative/releases/tag/v1.3.0-prerelease) (2026-10-04), in place over 0.9.2. The user specifically requested the official release APK and its matching source revision. Before installation, backed up the installed 0.9.2 APK and exported all four benchmark container configurations to `tools/gamenative-backup-2026-10-12/`; the exported JSON copies also remain in `Download/xclipse` on the phone. Play Protect blocked installation, and the user selected **Install anyway**. Play Protect was not disabled.

The installed standard APK is **572,505,268 bytes**, SHA256 **`90cb39b8ff546265a37b0b66b42d9ab92b5a361aa8ab6af188dd26878aed0d32`**, matching GitHub's release asset. The release tag resolves to **`231d1365cf1a09b7f1763cfaebd3906628ec1381`**, now pinned as `third_party/gamenative`. Upstream's prerelease still declares **versionName 1.2.1 / versionCode 23**, also reported by the installed package; that display version is not the release identifier. `third_party/gamenative-release.json` records the release tag, source commit, APK URL, size, checksum, and Android package metadata.

All four game IDs survived the update. Post-update exports show unchanged graphics-driver selection/configuration, DXVK selection/configuration, FEXCore and Box64 versions/presets, environment variables, and executable arguments. The only changed existing fields are `drives` and `wincomponents`: the app migrated the benchmark folders from shared storage into `/data/user/0/app.gamenative/CustomGames/<name>`, updated `A:`, and added the new default DirectInput entries. No exported keys were removed; the new app added 16 settings. This migration is implemented by the pinned `CustomGameScanner.migrateToInternalStorage`, called when resolving containers. Future automation must not assume those game folders still live in `/sdcard/GameNative/CustomGames`. Both Heaven containers still use DXVK `async-1.10.3`, FEXCore `Intermediate`, and Box64 `Compatibility`; RADV uses BCn `none`, stock `full`.

Added `gn_driver.py export-config GAME DESTINATION` for reproducible backups without importing or resetting a container. It refuses existing destinations and verifies the phone export and local copy. The new app returns from its document picker before writing JSON; an initial immediate pull produced an empty file. The helper now waits up to 30 seconds for a valid nonempty object and only publishes a verified local copy. Three subsequent post-update exports completed successfully with this fix; the first export was recovered and verified after the asynchronous write completed.

Nine top-level reference repositories are now pinned Git submodules: GameNative, GameNative's Mesa wrapper snapshot, DXVK 2.7.1, both RADV upstreams, Xclipse940-RADV, mesa-unified-drivers, the Samsung r12s kernel, and ExynosTools. Existing research pins were retained; `driver/` remains the editable subtree. GameNative's and DXVK's nested dependencies are also checked out at their recorded commits. `third_party/MANIFEST.md` lists exact SHAs, purposes, licensing, and restore instructions. The wrapper research pin is independent of the APK's bundled wrapper binary; it is not asserted to reproduce that binary.

`pipeline/sync_sources.py` restores the exact Git-link commits, refuses dirty worktrees, and applies sparse checkouts before checking out the Windows-incompatible kernel tree. Validation passed for all nine clean pins, nested GameNative/DXVK pins, the release-lock/source match, and a fresh kernel restore from the official remote with a deliberately dirty-worktree refusal check. On Windows, a blanket `git submodule status --recursive` can expand the kernel index and reject `aux.c`; the documented checks recurse only into GameNative and DXVK. Python compilation, export command help, all five runner regression tests, and diff checks passed. No driver code changed.

Post-upgrade Heaven/RADV smoke checks used the existing `xd-fc066b9` driver and container settings, with diagnostic RADV logging enabled to confirm the driver loaded. `results/2026-10-12_132058-game` spent about 70 seconds extracting the updated runtime and migrating the container overlay before its first frame. It rendered correctly with zero GPU faults, but only 13 seconds of steady frames remained; the runner correctly excluded it as `too-short`. The 120-second retry, `results/2026-10-12_132339-game`, **passed**: 95.5 seconds of steady frames at **56.8 FPS**, zero GPU faults and sampling gaps, and correct textures in both inspected screenshots. This is one diagnostic smoke run, not an app-version or stock/RADV performance comparison. The launch log also finds the previously imported `DXVK-2.7.1-1` profile; the selected version remains `async-1.10.3`.

End state verified: GameNative stopped, Do Not Disturb restored off (`zen_mode=0`), USB stay-awake remains 2, temporary min/peak refresh-rate overrides are removed (`null`), and RADV diagnostic logging is off.

## DXVK 2.7.1 fault and continuous monitoring (2026-10-12)

The user clarified the product goal: a usable performance package for this S24 FE, similar to ExynosTools. Recorded this in `AGENTS.md`; performance claims still require correct rendering and repeatable measurements.

Selected the installed DXVK 2.7.1 in Heaven-radv, with readback after saving. `results/2026-10-12_135017-game` confirms extraction of `dxvk-2.7.1`, unchanged FEXCore 2603 Intermediate / Box64 0.3.6 Compatibility, and the RADV driver loading. Heaven remained black and the kernel recorded **153 GPU page faults** during the 120-second diagnostic run. It was excluded as `gpu-fault`. The first RADV VM fault was at page `0x800000200000`, outside the recorded live BO ranges, near a command-buffer allocation at `0x8000001c0000–0x8000001e0000`; later fault addresses varied widely. This establishes invalid GPU accesses, not their cause. Driver, newer DXVK feature paths, and wrapper memory handling remain candidates. DXVK was restored to `async-1.10.3` and read back before further testing; stock was never switched to 2.7.1.

The run exposed a monitoring gap: `game.py` checked GPU fault counters only after its timer ended. It now polls the kernel counters during every sampling iteration, force-stops GameNative on the first observed increase, records the detection time, and avoids delaying shutdown for UI dumps or capturing the home screen afterward. Existing logic skips later attempts for the affected container. Added regression coverage for both page faults and resets occurring while frame statistics claim a valid run; all six tests pass. Reports now retain `--radv-log` and repeatable `--note` annotations. A GameNative membership prompt also blocked navigation; `gn_driver.py` recognizes its exact title and closes it through its labeled Close button.

`results/2026-10-12_135643-game` checked the restored configuration and the updated runner on the phone. Stock, BCn `full`, still fails Heaven's **D24 2048×2048 depth-texture creation** on the new app; the runner caught the error dialog and excluded it, with zero GPU faults. RADV, BCn `none`, completed **88.7 steady seconds at 57.4 FPS**, zero faults and sampling gaps; the inspected 90-second screenshot renders correctly. Both runs used the original emulator presets, DXVK `async-1.10.3`, and diagnostic RADV logging. These are validation runs, not evidence of a stock/RADV speed advantage. Early fault shutdown is covered by simulated counter changes; the subsequent live run verifies the fault-free path without intentionally repeating the fault storm.

## DXVK descriptor-buffer isolation (2026-10-12)

Added `gn_driver.py env GAME NAME VALUE` to add or replace a text environment variable through
the container's Environment tab, save, reopen, and verify it. The tab row scrolls horizontally;
the helper locates it from the current UI dump. No variable is deleted. Live add/update/readback
checks passed for `WRAPPER_DISABLE_PLACED` and `DXVK_CONFIG`. The latter is a whole-value update,
so other options in an existing value must be preserved when applying it elsewhere.

Verified the actual standard wrapper from the installed release APK: SHA256
`b598a54166790a6fc071f08495f141f92dc192f1eb339e029bfc7a303c7486ce`. It contains
`WRAPPER_DISABLE_PLACED` and `WRAPPER_DMAHEAP_CACHED`, but not
`WRAPPER_BYPASS_SWAPCHAIN_PLACED`. The APK's bundled DXVK 2.7.1 x86/x64 `d3d11.dll` and
`dxgi.dll` are byte-for-byte identical to the imported upstream package. Recorded their hashes
in `third_party/gamenative-release.json`; a different DXVK binary does not explain the fault.

Focused Heaven-radv experiments, all with DXVK 2.7.1, BCn `none`, the original emulator presets,
and the unchanged `xd-fc066b9` driver:

| Result directory | Change | Outcome |
|---|---|---|
| `2026-10-12_141607-game` | `WRAPPER_DISABLE_PLACED=1` | Fault/reset detected at 7.6 s; stopped after one of each |
| `2026-10-12_142156-game` | Placed mapping restored to 0; `dxvk.enableGraphicsPipelineLibrary=False` | Fault/reset detected at 6.0 s; stopped after one of each |
| `2026-10-12_142913-game` | GPL back to default; `dxvk.enableDescriptorBuffer=False` | **Pass: 57.4 FPS, 95.5 steady seconds, zero faults/gaps** |
| `2026-10-12_143223-game` | Same descriptor-buffer setting, diagnostic logging off, two runs | **Both pass: 52.9 and 46.8 FPS, 95.7 and 94.0 steady seconds, zero faults/gaps** |

All three passing runs have correctly textured inspected screenshots. The live fault cases
also verify that the new monitor stops after the first observed kernel counter increase, without
waiting for its 120-second timer or a UI screenshot. Disabling descriptor buffers is now a
repeatable compatibility workaround for this stack. These runs do not establish a performance
gain: the confirmation runs' mean GPU clocks drifted from 625 to 506 MHz, and stock's D24 failure
still prevents a fair driver comparison.

The pinned DXVK source enables descriptor buffers by default on RADV. RADV requires their memory
in its 32-bit address window (`radv_get_buffer_memory_requirements`) and reconstructs shader
pointers with `address32_hi`. Its fd imports use a separate address range, and
`GetMemoryFdPropertiesKHR` excludes 32-bit memory types. The reference wrapper's dma-heap path
replaces the requested memory type. This is a plausible integration mismatch, **not a proven
root cause in the APK binary**. Disabling placed mapping alone still failed. The nearest BO in
the fault log is only a nearby allocation, not evidence that it caused the access.

The extracted phone-driver ELF and existing unstripped build have the same build ID
`6268e19c8538a2bcb4a6fd69134bec31c8a4946f`; the deployed ELF's SHA256 is
`62f186db960ad64c161118687513a73a560b5a2ac15f3628ac4700a52f5534aa`. The nearby command-buffer
allocation resolves to `radv_cmd_buffer_resize_upload_buf`, but no driver change is justified by
that proximity alone.

Added the native `descriptors` correctness test to `xdtest` and the default `run.py` list. It
enables descriptor buffers and buffer device addresses, generates storage-buffer descriptors at
two nonzero offsets, dispatches to two outputs, and checks all 8,192 output words plus guards.
It records the memory-type mask and GPU addresses. In `results/2026-10-12_143852`, **both Samsung
and RADV pass**, along with the existing compute and render tests. RADV's descriptor address is
`0xffff800000000000`, while its output buffer is at `0xffff800100020000`, consistent with the
separate address windows. This validates this native compute path; it does not cover all DXVK
descriptor types or prove which integration component is faulty. The native driver binary was
unchanged. The harness builds without warnings; Python compilation, six runner regression tests,
and diff checks pass.

Heaven-radv is left on the verified DXVK **2.7.1** configuration with
`DXVK_CONFIG=dxvk.enableDescriptorBuffer=False`, `WRAPPER_DISABLE_PLACED=0`, BCn `none`, and the
original emulator presets. Stock remains on `async-1.10.3`, BCn `full`; GravityMark is unchanged.
Final device checks: GameNative stopped, DND restored off, USB stay-awake 2, RADV logging 0,
and min/peak refresh overrides unset.

## Controlled optimization experiments (2026-10-12, afternoon)

The user requested the five optimization targets in priority order and proper comparisons across
experiments. Added `pipeline/compare.py`: pinned library hashes and environment variants in a
manifest, six AB/BA pairs per test/candidate, X4 CPU affinity, optional Android fixed performance
mode, a start-temperature limit, telemetry during every attempt, checkpointed raw results, and
paired medians/ranges. Failed or unmatched attempts never contribute to gains. Fixed mode is
disabled in `finally`; Android exposes no getter, so use this from normal mode. GPU counter
increases now override even an `xdtest`-reported pass in `run.py`. Nine regression checks pass.

The old scratch `ms_gpu` was a cold, host-observed submission time. The harness now also records
GPU timestamps after at least 250 ms of warm-up, seven timed iterations, while retaining its
pixel/word correctness checks. Game FPS comparisons remain separate from native-driver timings.

`results/2026-10-12_160406-compare` (manifest `experiments/2026-10-12/native-initial.json`):
72 attempts pass, no GPU events; active GPU clock 400 MHz and X4 1632 MHz throughout the samples
(GPU 0 means idle). With NGG culling disabled, tiny-draw median GPU time drops **0.375 to
0.121 microseconds** (3.10x throughput), CPU recording **0.105 to 0.089 microseconds** (about 15%
less time). Graphics scratch's paired median speedup is 1.73x, with substantial variation.
Disabling the extra scratch initialization gives no consistent improvement; do not promote that
setting. Compute scratch also varies even under an option that should affect graphics only,
so its small differences are not a demonstrated optimization. Memory clocks were not controlled.
Fixed performance mode was successfully released afterward.

Direct RADV loading needs X11 WSI first: the current driver builds only `platforms=android`.
GameNative's wrapper supplies Xlib/XCB surfaces for Wine. The library exports the ICD entry
points, but changing the JSON alone would not provide those missing surface extensions. The
official GameNative submodule and installed app remain the unchanged reference baseline.

### Call-site logging guards

Driver source `01f09ed` moves the verbose log-level guard to the call site, so disabled draw
logging no longer prepares its variadic arguments. It preserves the existing levels and the
unconditional error path. No shader or synchronization behavior changes.
`results/2026-10-12_160836` passes render, descriptors, queries, transform feedback, graphics
scratch and compute scratch. `results/2026-10-12_160846-compare` has six valid AB/BA pairs:
CPU draw recording drops **0.1055 to 0.0905 microseconds** (14.2% less time; paired throughput
gain median 17.68%, all pairs positive). Pipeline-switch recording improves about 2%; GPU draw
timings are unchanged within the observed variation. Active GPU is 400 MHz, X4 1632 MHz, with
zero GPU events. This is a native CPU improvement, not yet a game FPS claim. Manifest:
`experiments/2026-10-12/log-guard.json`; ELF SHA256
`9d00f8100328c33726fa0a4b95e1be2856dbf91d9d7974cfffff09f11a082d9d`.
Also corrected the Windows build invocation in `run.py`: literal relative batch paths avoid
Python's escaped quotes being interpreted as part of a command name by `cmd.exe`.

### Adaptive-culling experiment and game comparison runner

Source `f3ae7c4` adds **opt-in** `RADV_XCLIPSE_CULL_MIN` / `debug.radv_cull_min` (default 0).
It disables the shader's existing runtime culling branch for small single direct vertex draws,
and restores normal culling for larger, indirect, multi-draw, tessellated/geometry and mesh draws.
The Xclipse VGT flush workaround is preserved. The new `culling` test alternates 3/126/129/192/6/384
vertices in one command buffer and compares every pixel with a single-draw reference.

`results/2026-10-12_162242-compare`: **108 passing attempts**, six pairs per candidate/test,
zero GPU events. A 128-vertex threshold gives **no measurable tiny-draw GPU gain** (both 0.372
microseconds) and pipeline-switch CPU timing is slightly worse. Keep the setting off. Compiling
culling out with `nonggc` still reduces tiny-draw GPU time to **0.121 microseconds**, and warmed
graphics scratch to **1.8875 ms from 3.528 ms** (paired throughput +86.2%, every pair positive).
This suggests compiled culling/synchronization cost, not the extra scratch-base initialization,
is worth pursuing. It does not justify removing the known-needed flush without separate tests.
Manifest: `experiments/2026-10-12/adaptive-culling.json`.

`pipeline/game_compare.py` compares property variants in one unchanged container, exports and
checks its settings, warms each persistent shader cache, alternates AB/BA order, uses the same
90-second rendered window after warm-up, and checkpoints every attempt. It rejects crashes,
faults, short windows, missing screenshots/telemetry, and lost frame samples; screenshots also
require visual review. It restores properties, DND, refresh rate, and fixed performance mode.
The initial 42 C cooling target could not be reached after the two warm-up runs
(`results/2026-10-12_161530-game-compare`); no pairs were scored. The next attempt exposed the
phone export's retained `container.json` basename, so exports now include the unique run name.
The corrected comparison uses a consistent 45 C start limit. Eleven Python checks pass,
including measurement-window clipping and failure exclusion. Full game results follow separately.

## Checkpoint at user-requested stop (2026-10-12, 16:48 IST)

The user stopped the work and requested a handoff, commit and push. The running Heaven study
`results/2026-10-12_162819-game-compare` was stopped during pair 2 baseline. Two warm-ups and
three scored attempts completed; only **one complete pair** exists: baseline 38.49 FPS versus
`nonggc` 49.73 FPS (+29.20%) at fixed performance mode. This is provisional, with screenshot
review pending, and does not establish repeatability or normal-clock gains. Pair 2 candidate
has no baseline and started at a reported 46 C despite the 45 C cooldown limit. Recheck temperature
at the actual launch boundary before running a fresh study. The interrupted attempt is excluded;
raw metadata records the stop and manual cleanup.

Hard termination bypassed the runner's `finally`. GameNative was manually force-stopped; fixed
performance mode disabled successfully; RADV debug cleared and logging set to 0; refresh overrides
deleted; DND restored off; USB stay-awake retained at 2. Readbacks are in the result JSON. No driver
was newly imported into GameNative, and its saved containers remain as documented in HANDOFF.md.

### Experimental direct-X11 build, not deployed

Added pinned `third_party/mesa-upstream` at `1b75efe0014c0d71163a01589bafb510460e7049`, restoring
MIT X11 helpers, the loader's X11 screen helper and Vulkan platform headers into the trimmed
driver tree with notices retained. `driver/android/x11-deps.json` pins GameNative's Bionic runtime
and official Termux header packages. `prepare_x11.py` downloads/verifies/extracts a local SDK;
`build_x11.py` builds separately in `driver/build-android-x11`; `deploy_x11.py` verifies the pinned
ELF library closure and supports deployment to a hash-named native-test directory.

SDK preparation, the X11 build, and dependency `--check-only` succeeded. Nothing from this path
was deployed to the phone or imported into GameNative. The compiled dirty package was
`radv-xclipse-26.3.0-devel-xd-0a61e5b-dirty-x11.zip`, ELF SHA256
`f42f14be96a42dca7940ff41ffd550622139cafa634c9bd754d1bcb826f05064`.
It predates the final no-flush edits below: rebuild from the checkpoint commit before testing.

`RADV_XCLIPSE_GAMENATIVE_WSI=1` is an opt-in Xclipse 940 adaptation using linear PRIME blits,
render-completion waits and PresentIdleNotify recycling, without shared X fences, scanout,
modifiers or the SDMA blit queue. Native loading, presentation, buffer mapping/cache coherence
and protocol compatibility are unverified. A container `VK_ICD_FILENAMES` override may avoid
needing a new APK, based on the pinned app's environment order, but this is not runtime-proven.
No ICD file or container override has been installed. Descriptor buffers must remain disabled
for the first presentation tests. This checkpoint does not fix the descriptor-buffer issue.

### Unbuilt no-flush experiment

Final source edits add `RADV_XCLIPSE_CULL_NOFLUSH` / `debug.radv_cull_noflush`, default false.
With a nonzero small-draw threshold, it attempts to omit the extra VGT flush only between two
consecutive runtime-unculled direct vertex draws; transitions retain flushing and no other
flush request is cleared. **These edits have not been built or tested.** The known culling hang
makes isolated correctness/fault testing essential before any game use. Earlier threshold
measurements retained the flush and do not validate this new option.

Checkpoint verification: Python compilation and all 11 offline runner regression tests pass.
No further phone tests or driver builds were run after the stop request.

## Next steps when the user resumes

1. Rebuild from the checkpoint and validate the latest C source; compare no-flush separately,
   default off. Preserve baseline artifacts and stop immediately on GPU events.
2. Validate native loading of the separate X11 build and its dependencies, then direct
   GameNative presentation with descriptor buffers still disabled. Do not infer WSI correctness
   from a compute pass. Investigate descriptor integration only after presentation works.
3. Fix the launch-temperature drift, run a fresh complete six-pair Heaven study, review images,
   and repeat promising results at normal clocks. The partial fixed-mode pair is not a final gain.
4. Fix stock's D24 path before a fair Samsung comparison. Keep FEXCore/Box64 original presets
   and the verified DXVK 2.7.1 descriptor-buffer workaround.
5. Deliver a package only from repeatably correct improvements. Any upstream message requires
   explicit user instruction to send. See the rewritten `docs/HANDOFF.md` for exact state,
   source/artifact hashes, portable commands and remaining limitations.

## Resumed checkpoint validation and launch-temperature fix (2026-10-13)

Pulled `main` before resuming; `17785a9` was already the remote tip and the checkout was clean.
Rebuilt its normal driver with the portable Windows toolchain: package
`radv-xclipse-26.3.0-devel-xd-17785a9.zip`, ELF SHA256
`8a18f2883927cb3087a520a364c69544303ca6020902b10c7f64e8a918c7a38d` (retained as
`tools/driver-17785a9.so`). `results/2026-10-13_165717` passes all 22 attempts on Samsung/RADV:
render, descriptors, culling transitions, queries, transform feedback (including multi-draw
and geometry variants), tessellation, graphics/compute scratch, and draws. No GPU events.

`results/2026-10-13_165855` then passes the same 11 RADV tests with
`RADV_XCLIPSE_CULL_MIN=128` and `RADV_XCLIPSE_CULL_NOFLUSH=1`, again with no GPU events.
These are initial correctness checks, not exhaustive synchronization validation or a reason
to enable the option by default. The paired experiment is pinned in
`experiments/2026-10-13/culling-noflush.json`.

Fixed the previous Heaven comparison's launch-temperature drift: `run_once` now cools and
records temperature after memory/log setup, immediately before its launch intent. The comparison
passes its limit into that check and independently rejects missing/invalid/over-limit temperature
readings when accepting scores. An unmet limit returns a checkpointable `temperature-not-ready`
attempt without launching GameNative. Regression coverage verifies the launch ordering, the exact
limit, invalid readings, and score exclusion. Python compilation, all 12 offline regression tests,
both affected CLI help commands, and `git diff --check` pass. A fresh phone comparison is still needed.

### Small-draw no-flush measurements and native X11 loading

`results/2026-10-13_170025-compare` completes six AB/BA pairs for culling, draws and graphics
scratch (36 passing attempts, zero GPU events). Fixed performance mode, CPU 9 affinity and a
45 C start limit match the pinned manifest. Against threshold 128 with the flush retained,
the opt-in no-flush path reduces tiny-draw GPU time **0.372 to 0.121 microseconds** (3.07x
throughput) and CPU recording **0.091 to 0.078 microseconds**. Pipeline-switch GPU time improves
**0.5035 to 0.4845 microseconds**. All six paired draw gains are positive. Graphics scratch shows
no consistent benefit (paired range -4.01% to +16.89%). Fixed mode release succeeded. The change
remains default-off: these tests do not establish game correctness or general synchronization safety.

Rebuilt X11 package `radv-xclipse-26.3.0-devel-xd-17785a9-x11.zip`, ELF SHA256
`9c54a02cb96027f672923968e8bc75e5f9defd7a73047b2caa912f3dc2caac51`, retained as
`tools/driver-17785a9-x11.so`. `deploy_x11.py` verified and deployed its 11 pinned runtime libraries
to `/data/local/tmp/xd/x11-9c54a02cb96027f6/`, with every phone checksum read back.
`results/2026-10-13_170238-compare` passes all 47 attempted offscreen compute, render, descriptor
and culling checks (24 X11, 23 normal Android). It stops at the 45 C cooldown limit before the
last normal-build culling attempt; this is an incomplete comparison, not a test failure. The phone
subsequently read 38 C, zero fault/reset/timeout counters. Manifest: `x11-native.json` beside the
no-flush manifest. Loading is proven; X11 presentation is still untested at this milestone.

### Direct X11 reaches swapchain creation, but not a displayed frame

Backed up Heaven-radv as `tools/Heaven-radv-before-x11-20261013.json`, keeping the matching
phone export. Imported the X11 ZIP (SHA256
`cf943bd574ae1e32207ba3fb062891ae9cc23cbac0865038e90219166da2d485`) with phone checksum verification
and driver selection readback. The tracked `experiments/2026-10-13/radv-x11-17785a9.json` points
to that imported library; its phone copy is under `/sdcard/Download/xclipse/`. Set
`VK_ICD_FILENAMES` to this JSON and `RADV_XCLIPSE_GAMENATIVE_WSI=1` through the container UI.
Kept descriptor buffers disabled and retained the original emulator presets.

Three diagnostic attempts (`171447-game`, `171735-game`, `172115-game` on 2026-10-13) all end
`game-exited`, with no captured frames and no GPU faults or resets. The first coincided with
an incoming call; the repeat with DND `none` behaves the same. The third enables DXVK
info logging. Final guest environment confirms the ICD override; RADV's instance entry and
texture-upload submissions appear in the log. DXVK reports successful creation of a 1280x720
BGRA8 swapchain with five images. This proves direct loading and swapchain creation on the
official APK without an app rebuild, **not working presentation**.

GameNative reports `BadImplementation` for Present's major opcode 153. Its pinned source
does not implement QueryCapabilities, which Mesa calls, but Mesa already tolerates an absent
capability reply; this alone does not prove the exit cause. Later GameNative reports
`processes_exited` and terminates the remaining guest processes (status 137). That cleanup
status is not evidence of an OOM or GPU hang. No driver change is justified by the logs yet;
the next direct-path investigation needs focused acquire/submit/present tracing or a small
X11 presentation probe. No X11 performance claim is made.

Restored and read back the original `xd-fc066b9` driver, `RADV_XCLIPSE_GAMENATIVE_WSI=0`,
`DXVK_LOG_LEVEL=none`, and `VK_ICD_FILENAMES` pointing to GameNative's original
`/data/user/0/app.gamenative/files/imagefs/usr/share/vulkan/icd.d/wrapper_icd.aarch64.json`.
Those last three variables remain as explicit defaults; no environment row was deleted. All
imports, ICD JSONs and backups are retained. Added `gn_driver.py envs GAME NAME=VALUE ...` to
batch environment updates with one save and readback of every value after reopening. The
three-value restoration is its on-device validation; Python compilation, the 12 offline
regression checks, help/invalid-argument checks, and diff checks pass.

### Actual-launch thermal gate verified on the phone

`results/2026-10-13_173017-game-compare` uses the restored fc066b9 container and the original
45 C limit. Both unscored warm-ups complete (38.30 / 49.69 FPS, starts 44 / 45 C, no lost samples
or GPU events; all four sampled screenshots render normally). Before the first scored launch,
the phone fails to reach the limit within 180 seconds: the final reading is **46 C**. The new
gate records `temperature-not-ready`, does not launch the game, excludes the attempt and stops.
Thus this dataset contains **no scored pair**. Fixed performance mode release succeeds and RADV
debug is restored empty. A fresh study will use a declared 50 C limit; it must not merge this
rejected study or the October 12 partial comparison into its scores.
