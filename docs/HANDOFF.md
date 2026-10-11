# Handoff: continue on any computer (written 2026-10-10, updated 2026-10-11 22:00)

The work moves between the user's Windows PC and Mac. Everything (toolchain, driver build, phone tests, game runs) works on both; the commands below give both forms. The phone stays the same: Galaxy S24 FE, SM-S721B, serial `RZCY90ZYQXZ`. Read this first, then `docs/PROGRESS.md` for the full history and numbers.

## Where things stand

- **The driver works on the phone.** `driver/` (radv-xclipse `xclipse940`, Mesa 26.3.0-devel) builds on Windows and macOS. Every on-device test in `pipeline/run.py` passes on both drivers: the 17 of `results/2026-10-11_131855` plus `vertex`, `depth`, `mips`, `storage` and `bcn`, added since (`160236`, `162020`, `162500`, `163011`, `170957`, `172933`).
- **Speed tests, RADV against Samsung's driver:** FP32 throughput, fill rate and bandwidth are equal; RADV compiles pipelines 2.5–3.6× faster; RADV costs 1.6–2× more per tiny draw, and graphics scratch (spilling shaders) is 3× slower (`scratch` 10.2 ms against 3.3 ms). Details in `docs/PROGRESS.md`.
- **Fixed 2026-10-11:** queries without GDS, transform feedback, wave64 register clobbering (`xd-3e88e3e`); then the GPU page faults that reset the GPU once a second in Heaven, which were the shaders' scratch setup (`xd-fc066b9`, GFX11's three-register sequence). **`xd-fc066b9` is the driver on the phone**, in both radv containers.
- **Heaven A/B, the fair one** (`results/2026-10-11_165352-game`, 3 runs each, BCn Emulation `none` on both containers): **stock 35.6 FPS, radv 37.2 FPS median (1.05×), 0 GPU faults.** Stock fell run by run (49.2, 35.6, 24.9) while radv held (42.4, 37.0, 37.2); the overlay showed stock at 3.3–4.7 W and radv at 2.0–2.1 W (single samples). Stock draws Heaven's clouds as black blotches with its native BC decoding; radv draws them correctly. The 51.8 FPS at `133724-game` was measured with black textures and doesn't count.
- **Heaven's black geometry on radv was GameNative, not the driver.** GameNative runs `Pipetto-crypto/bcn_layer` (MIT), a Vulkan layer that decodes BC textures in compute into RGBA8 storage images. In `auto` mode it trusts native BC only on Samsung's driver (BC1–3) and Qualcomm or Turnip (all), so RADV gets every BC format emulated, and the emulated textures come out black on radv. With **BCn Emulation = `none`** radv samples BC itself and Heaven renders correctly. Why the layer's decode is black on radv is still open (next section), but every piece of it passes in isolation.
- GravityMark: lmkd kills it on both drivers (it needs more memory than the phone has free). Not pursued further.
- **The harness was reworked on 2026-10-11 evening** (commit `0aad69b`): `game.py` detects SurfaceFlinger ring overflows and leaves those intervals out, writes the phone log to a file for the whole run, pins the display to 120 Hz and restores it, samples GPU clock and temperature, the CPU clusters' clocks and the battery, and restores `stay_on_while_plugged_in` only when it read back a number; `run.py` alternates the driver order per test and writes the report even when a run stops early. Smoke run `results/2026-10-11_212059-game`: radv 37.9 FPS over 54 s, 0 faults, Hz 120, 0 lost windows.
- **The "60 Hz pacing" was not a cap.** GameNative launches the wrapper with `MESA_VK_WSI_PRESENT_MODE=mailbox` and its X server view redraws on damage (`GLRenderer.requestRender`), so SurfaceFlinger counts the game's own frames up to the display rate. With the panel at 120 Hz Heaven's intervals were 405 × 8.3 ms, 1236 × 16.7 ms, 288 × 25 ms, 72 × 33 ms: it simply runs at about 60 FPS. Heaven is CPU-bound: GPU 50% busy at a mean 486 MHz, the X4 core at a mean 1.2 GHz, A720s at 1.8 GHz.
- **DXVK 2.7.1 can't be used in GameNative 0.9.2.** The dropdown lists it, but the APK ships no DXVK content profiles (`ContentsManager: Total profiles for type DXVK: 0`) and the 0.9.2 source has no downloader (`dxwrapper_download.json` and `DXWrapperDownloader.kt` exist only in later versions). The launch logs `Extracting DXVK/D8VK DLLs for dxwrapper: dxvk-2.7.1` and extracts nothing: radv showed a black screen at 1.5 FPS, stock fell back to the home screen (`results/2026-10-11_213525-game`, stopped after run 1). Both Heaven containers are back on `async-1.10.3`.
- **Clocks and what adb can do about them** (all read-only files are owned by `system`; nothing here needs root, nothing here can be written): the GPU table goes to 1095 MHz but `/sys/kernel/gpu/gpu_max_clock` is 800 MHz and devfreq's `max_freq` 1000 MHz; `trans_stat` shows 1000 MHz for 1.5 s in the phone's whole uptime and 1095 never. CPU `scaling_max_freq` is capped below the hardware table on every cluster (A520 1824 of 1959 MHz, A720 2304 of 2592 and 2784 of 2900, X4 2784 of 3110). `cmd power set-fixed-performance-mode-enabled true` works from the shell and locks the CPUs at 1.5–1.6 GHz and the GPU at 400 MHz: useful for reproducible driver-only A/Bs, useless for peak. Samsung's "Enhanced processing" setting (`sem_enhanced_cpu_responsiveness`) changes no limit. Android's `cmd game mode performance app.gamenative` is refused (the app doesn't declare it); a fork could declare it and send ADPF performance hints. `dumpsys thermalservice` gives AP, skin and battery temperatures. The kernel's `cl_boost` (to 999 MHz) fires only for pure compute loads.

## Resume here (updated 2026-10-11 22:00, work paused by the user)

The phone was left clean: GameNative closed, `debug.radv_xclipse_log` 0, `min_refresh_rate` and `peak_refresh_rate` unset (the panel is back on its default 60 Hz behaviour), `stay_on_while_plugged_in` 2. **Both Heaven containers: BCn Emulation `none`, DXVK `async-1.10.3`, driver `xd-fc066b9` on Heaven-radv** (each read back after saving by `gn_driver.py`). The GravityMark containers are untouched (`auto`).

**Next, in order:**

1. **FEXCore and Box64 presets on Heaven** (the biggest CPU lever for a 32-bit game, 2 runs each side): the containers run FEXCore preset *Intermediate* (`FEX_TSOENABLED=1`, `FEX_HALFBARRIERTSOENABLED=1`, `FEX_X87REDUCEDPRECISION=1`, `FEX_MULTIBLOCK=1`) and Box64 preset *Compatibility*. *Performance* turns TSO off (`FEX_TSOENABLED=0`, `FEX_HALFBARRIERTSOENABLED=0`), which is where the time goes in memory-ordering emulation. Set it on both Heaven containers (same preset on both, so the driver comparison stays fair), run `game.py --bench heaven --runs 3 --shots 30,45,60,75 --seconds 90`, and watch for hangs or wrong rendering; TSO off can break games. The preset lives in the container editor; `gn_driver.py options Heaven-radv "FEXCore Preset"` tells whether the Graphics-tab scripting reaches it, otherwise extend `gn_driver.py` for that tab first.
2. **A DXVK newer than async-1.10.3 on radv**, which is the test of RADV's extension set. Three routes: (a) a Winlator-format DXVK content package installed through GameNative's Contents screen (a download to the phone, so ask the user first); (b) update GameNative to 1.3.0-prerelease, which has the downloader, accepting that the container format changes; (c) the fork, item 3. Don't select 2.7.1 (or any non-bundled version) in 0.9.2 again.
3. **The wrapper env A/Bs, then the fork decision.** Knobs the wrapper reads (`third_party/gamenative-mesa`, `wrapper-25`): `WRAPPER_DISABLE_PLACED`, `WRAPPER_DMAHEAP_CACHED`, `WRAPPER_BYPASS_SWAPCHAIN_PLACED`, `WRAPPER_VK_VERSION`, `WRAPPER_EMULATE_VULKAN_1_3`, `WRAPPER_EMULATE_PUSH_DESCRIPTOR`, `WRAPPER_EMULATE_IMAGELESS_FRAMEBUFFER`, `WRAPPER_EMULATE_HOST_QUERY_RESET`, `WRAPPER_NO_PATCH_OPCONSTCOMP`, `WRAPPER_NO_REMOVE_CLIP_DISTANCE`, `WRAPPER_EXTENSION_BLACKLIST`, `WRAPPER_RESOURCE_TYPE`, `WRAPPER_VMEM_MAX_SIZE`, `WRAPPER_DIAG`, `WRAPPER_LOG_LEVEL`/`WRAPPER_LOG_FILE`, and the BCn set (`WRAPPER_EMULATE_BCN`, `WRAPPER_NATIVE_BCN`, `WRAPPER_BCN_GPU`, `WRAPPER_BCN_POLICY`, `WRAPPER_BCN_UPLOAD`, `WRAPPER_USE_BCN_CACHE`, `WRAPPER_MARK_BCN`, `WRAPPER_NO_BCN_THREAD`). Set them on the Environment tab of Heaven-radv.
4. **A `--fixed-clocks` option for `game.py` and `run.py`** (`cmd power set-fixed-performance-mode-enabled true`, restored afterwards) for driver-only comparisons; the game-level numbers stay at normal clocks.
5. **Upstream** (JimVulkan/radv-xclipse PR #6): ask the user before posting.

### Parked: why the BCn layer's compute decode is black on radv

Everything below was checked, in this order; the results are in `docs/PROGRESS.md`. The fix for users is in GameNative or the layer (treat `VK_DRIVER_ID_MESA_RADV` like Turnip in `is_supported_bcn_format` and `BCnLayer_GetPhysicalDeviceFormatProperties`, `bcn_layer/src/bcn.cpp`, `bcn_layer.cpp`), or BCn Emulation `none`, which is what the containers use. At most one more experiment is worth it: the push-constant diagnostic in the last bullet.

1. Not the driver's own texture paths: `textures` (BC1–7, cubes), `mips` (incl. `textureGrad`), `mrt`, `vertex`, `depth` all pass; compression flags, ACO optimizations and pipeline libraries don't change the picture.
2. Not `auto`-specific: `full` is black too (`171504-game`).
3. Not storage-image writes: `storage` writes 7 formats × 10 mips through per-level views, from a constant, a storage buffer and texel buffers, between the layer's barriers, then reads them back through a sampler. Passes (`170957`).
4. Not the layer's decode itself: `bcn` runs the layer's own BC1/BC3 shader (`tests/shaders/bcn_s3tc.comp`, MIT) with its descriptor layout, its one-set pools, the blocks read from a storage buffer at a non-zero offset, its barriers, one dispatch per mip; compared with a CPU decode, byte for byte. Passes on both drivers (`172933`).
5. Not a bypassed hook: the layer only intercepts `vkCmdCopyBufferToImage` (not `…2`), but the verbose radv log of a run (`debug.radv_xclipse_log=2`, `[COPY_BUF2IMG]`) shows no buffer-to-image copy into any of the 290 substituted RGBA8/RGBA8_SNORM images, so the decodes were dispatched.
6. Not the queue: DXVK records its uploads on radv's compute queue family (qf=1); with it disabled (`debug.mesa.radv.queue.disable=compute`, `173124-game`) it's still black.
7. Both BC emulators were active in those runs (`ENABLE_BCN_COMPUTE=1` and `WRAPPER_EMULATE_BCN=2`); which one produced the black images was never separated. In 0.9.2 the layer runs for every GPU except Qualcomm's (`XServerScreen.kt`, `getVendorID() != 0x5143`), so stock's working `full` run did go through it.
- **State the layer leaves behind** (the one experiment left): it binds its compute pipeline, descriptor set 0 and 28 bytes of push constants on DXVK's command buffer and never restores DXVK's state. Push constants are shared between the graphics and compute bind points, so DXVK's graphics push data can be overwritten by `{format, width, height, offset, ...}`. To test it without DXVK: in a radv build, keep a separate push-constant copy for compute-only pushes behind a diagnostic knob (`radv_CmdPushConstants` / `radv_flush_constants`, `radv_cmd_buffer.c`) and see whether Heaven renders with `auto`.

**Script repeated phone UI work** (see `CLAUDE.md`). Container edits, Driver Manager imports and settings changes go through `pipeline/gn_driver.py` and `pipeline/phone_ui.py`, not subagents. Only one thing drives the phone at a time; don't run `run.py` or `gn_driver.py` while `game.py` runs.

## What's on the phone (keep all of it)

| Location | What |
|---|---|
| `/sdcard/GameNative/CustomGames/GravityMark-stock` (228 MB) | GravityMark 1.89, unpacked. `.gamenative` = `{"appId":2000940101}` |
| `/sdcard/GameNative/CustomGames/GravityMark-radv` (228 MB) | Same, `{"appId":2000940102}` |
| `/sdcard/GameNative/CustomGames/Heaven-stock` (274 MB) | Unigine Heaven 4.0, unpacked. `{"appId":2000940201}` |
| `/sdcard/GameNative/CustomGames/Heaven-radv` (274 MB) | Same, `{"appId":2000940202}` |
| `/sdcard/Download/xclipse/radv-xclipse-26.3.0-devel-xd-*.zip` | Our driver packages, all imported into GameNative's Driver Manager: `c8fed77` (first), `ea7b521` (queries without GDS), `3e88e3e` (transform feedback and the register fix), `f41503a` (GPU page-fault logger), `fc066b9` (the page-fault fix; selected in both radv containers) |
| `/sdcard/Download/xclipse/EXEC-ARGS.txt` | Exec Arguments for each container (same as `pipeline\game.py --print-args`). Refreshed 2026-10-11 with Heaven's `../` paths |
| `/sdcard/Download/xclipse/vc_redist.x64.exe` | Visual C++ runtime installer, in case GravityMark needs it in Wine |
| `/sdcard/Download/xclipse/GravityMark_1.89.msi`, `Unigine_Heaven-4.0.exe` | The original installers |
| `/sdcard/GameNative/CustomGames/Heaven-*/data/heaven_4.0.cfg` | Edited 2026-10-11: added `video_width` 1280 and `video_height` 720, because the Exec Arguments pass `-video_mode -1` (custom size) without them. The originals are kept next to them as `heaven_4.0.cfg.orig` |
| `/sdcard/Download/xclipse/out/` | Where the benchmarks write results (`D:\xclipse\out` in containers) |
| `/data/local/tmp/xd/` | Test harness `xdtest`, the last `vulkan.radeon.so`, and `out/`. `pipeline/run.py` redeploys these every run |

**GameNative state:** 0.9.2, the legacy GitHub APK; we chose not to update.

- On every start it shows the Steam sign-in screen. Tap **Skip login**; there's no account on this phone.
- It also offers an update to 1.2.1. Tap **Later**.
- All four games are in the library, added with **Add Game**. 0.9.2 does *not* auto-scan `GameNative/CustomGames`; after adding, refresh the library.
- Driver Manager lists each import as **"RADV Xclipse (Mesa 26.3.0-devel-xd-<sha>)"**. Both radv containers use `xd-fc066b9` (set and read back by `pipeline/gn_driver.py`, 2026-10-11 13:35).
- All four containers are configured, as in the table below. Exec Arguments and environment variables were read back from the phone and match exactly.
- Settings → Debug → **Enable Wine Debug Logs is off** (turned off 2026-10-11 before timed runs). When on, it writes `/sdcard/Android/data/app.gamenative/files/wine_logs/wine_debug.log`, which `adb` can read. **Clear Preferences and Clear Local Database sit right under it; never tap them.**
- Do Not Disturb was on during the UI work (the user turned it on for 4 hours around 16:00). A notification that pops up between a dump and a tap takes the tap; `gn_driver.py` stops when another app comes to the front.

**Phone settings:** `stay_on_while_plugged_in` is 2 (the user wants the phone awake while it's connected). No other phone setting was changed.

**Removed:** only scratch files: `/sdcard/xd_ui.xml`, an unstripped driver copy, and simpleperf data. The profiling recipe in `docs/PROGRESS.md` regenerates them.

**Results in git:** since 2026-10-11 evening only `summary.md` and `results.json` of each run are tracked (`.gitignore`); screenshots and logs stay on the computer that made them. The home-screen screenshot of `105144-game` was deleted (it remains in the history).

## Switching computers

Before leaving one computer, commit and push. On the other one:

1. **Get the repo.** First time: `gh repo clone aaditya-v-more/xclipse-driver` (private, under the user's GitHub account), then set the local git identity as in `CLAUDE.md`. After that: `git pull`.
2. **Toolchain, once per computer** (every download verified; about 3 GB unpacked):
   - Windows: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\setup.ps1`
   - macOS/Linux: `sh tools/setup.sh`
3. **Phone:** plug it in over USB and accept the "Allow USB debugging" prompt for that computer. Tick "Always allow". `adb devices` should list `RZCY90ZYQXZ`.
4. **Check everything works:**
   - Windows: `tools\python\python.exe pipeline\run.py --build-driver --tests info,device,render`
   - macOS/Linux: `tools/python/bin/python3 pipeline/run.py --build-driver --tests info,device,render`

   It should pass on both drivers. Both computers have done this (the Mac: result `2026-10-10_234847`).
5. **Optional:** re-clone the reference repos with the commands in `third_party/MANIFEST.md`.

In the commands below, `python` means the toolchain's Python for the computer you're on, and paths work with either slash direction on Windows.

## GameNative scripts

- **`pipeline/gn_driver.py`** drives GameNative over adb, reading every change back after saving:
  - `install driver/dist/radv-xclipse-26.3.0-devel-xd-<sha>.zip Heaven-radv GravityMark-radv`: copies the zip to `Download/xclipse`, imports it in Driver Manager unless it's there already, and selects it in the containers. Commit before building, so the package name `xd-<sha>` matches.
  - `select ENTRY GAME...` and `show GAME...` select or read the Graphics Driver Version.
  - `options GAME LABEL` lists a Graphics-tab dropdown's choices; `set GAME LABEL VALUE` picks one (BCn Emulation: none, partial, full, auto).
  - `args GAME...` sets Exec Arguments to `game.py`'s.
  - Never taps Reset container, Uninstall, Delete variable, Import config, the trash icons in Driver Manager (x=854), or the Clear buttons under Settings → Debug.
- **`pipeline/phone_ui.py`** is what it's built on: `dump`, `tap`, `tap-text "Label"`, `type` (works around Gboard's auto-spacing), `clear`, `shot`.
- The tap paths, for reference (phone pixels, 2026-10-11): library Menu (1006,169) → Settings (500,699) → Driver Manager (540,1942) → Import ZIP from device (443,1129). Game card → Options (754,655) → Edit container (521,405) → Graphics tab (383,247) → Graphics Driver Version (540,619) → the entry → save (1001,89, the right-hand title button; the editor shows `Config*` until saved; the left-hand button (78,89) closes without saving).

## Container settings (2026-10-11)

| Game | Container Variant | Graphics Driver | Graphics Driver Version | BCn Emulation | Exec Arguments |
|---|---|---|---|---|---|
| GravityMark-stock | bionic | a Wrapper (the same one on all four) | System | auto | `gravitymark-stock` line of `EXEC-ARGS.txt` |
| GravityMark-radv | bionic | same Wrapper | RADV Xclipse (Mesa 26.3.0-devel-xd-fc066b9) | auto | `gravitymark-radv` line |
| Heaven-stock | bionic | same Wrapper | System | none | `heaven-stock` line |
| Heaven-radv | bionic | same Wrapper | RADV Xclipse (Mesa 26.3.0-devel-xd-fc066b9) | **full (set it to none)** | `heaven-radv` line |

DX Wrapper is DXVK `async-1.10.3` on all four, the only DXVK that 0.9.2 actually ships (the other dropdown entries extract nothing; see "Where things stand"). Emulation is FEXCore 2603 for 64-bit and 32-bit code, preset Intermediate, Box64 0.3.6 preset Compatibility, WoW64 on, on all four.

- BCn Emulation sets `ENABLE_BCN_COMPUTE`, `BCN_COMPUTE_AUTO` and `WRAPPER_EMULATE_BCN` (none 0, partial 1, full 2, auto 3) for the launch.
- **GravityMark-stock and GravityMark-radv: Max Device Memory 2048 MB.** It's DXVK's `maxDeviceMemory`, so it does nothing for GravityMark, which runs native Vulkan (`-vk`); lmkd still killed both runs.
- Seen in GravityMark-stock and both radv containers: "Use Adrenotools Turnip" on, present mode mailbox, Memory Resource Type auto. GravityMark-stock also has Wine Video Memory Size 2048 MB.
- Heaven-radv's launch environment, from logcat: `WRAPPER_RESOURCE_TYPE=auto`, `WRAPPER_NATIVE_BCN=1`, `WRAPPER_VK_VERSION=1.3.0`, `MESA_VK_WSI_PRESENT_MODE=mailbox`, `DXVK_ASYNC=1`, and `VK_ICD_FILENAMES` pointing at GameNative's wrapper, which loads our driver through adrenotools.

Environment tab, added on top of GameNative's defaults:

| Variable | Containers | Why |
|---|---|---|
| `WRAPPER_NATIVE_BCN=1` | all four | Tells the wrapper to leave BC to the driver. It doesn't stop the BCn compute layer; only BCn Emulation `none` does. |
| `WINEDLLOVERRIDES=msvcr100,msvcp100=b` | Heaven-stock, Heaven-radv | The native `msvcr100.dll` crashes under 32-bit FEXCore emulation (`docs/PROGRESS.md`); Wine's built-in one works. |

Don't try these shortcuts; we checked them in the v0.9.2 source:

- **Editing the config files:** containers live in `/data/data/app.gamenative/...`, which adb can't reach without root.
- **The `LAUNCH_GAME` intent's `container_config` extra:** it can set `execArgs` and `envVars`, but `ContainerUtils.applyToContainer` then sets `graphicsDriverConfig` to `""` for that launch. RADV silently falls back to System, and the wrapper settings reset.
- **Selecting the driver with `ADRENOTOOLS_*` environment variables:** GameNative's `EnvVars.putAll` splits on spaces, and the wrapper/adrenotools path needs `containerVariant=bionic`, which the intent can't set.

`pipeline/game.py` therefore launches with the plain intent (`app_id` and `game_source` only) and relies on each container's saved settings.

## Running the game A/B

- **Smoke test:** `python pipeline/game.py --bench heaven --runs 1 --radv-log`. `--radv-log` makes RADV log `[BUILD] Mesa 26.3.0-devel`, which proves the radv runs loaded our driver (the `Driver seen` column). Leave it off for timed runs.
- **The real A/B:** `python pipeline/game.py --bench heaven --runs 3 --shots 30,45,60,75 --seconds 90`. Results go to `results/<time>-game/summary.md`, `results.json` (with the settings GameNative launched each container with) and the screenshots. Medians use only runs with 30 s of steady frames.
- `--prop NAME=VALUE` sets an Android property for the run and clears it afterwards. `--seconds N` ends each run early; `--shots s1,s2` takes screenshots at those seconds.
- **Before every run** `game.py` closes background apps, keeps the screen on, samples memory and GPU load, counts GPU page faults and resets from `gpu_info`, and records low-memory kills. Ctrl+C stops it and keeps the finished runs. **Nobody may touch the phone during runs.**
- FPS comes from SurfaceFlinger. GameNative presents in mailbox mode and its X server view redraws on damage, so the count is the game's own frames up to the display rate. `game.py` pins the panel to 120 Hz for the run (`--hz`), so the cap is 120; the stock Android setting leaves it at 60.
- The screenshots of both drivers at the same second show different scenes, because Heaven's load time varies; compare scenes, not seconds.

## Driver switches and logging

Set them as environment variables in `xdtest` runs, or as `debug.*` properties for apps that can't set variables.

- **Property names.** The port maps `RADV_DEBUG`, `RADV_PERFTEST` and `RADV_EXPERIMENTAL` to `debug.radv_debug`, `debug.radv_perftest`, `debug.radv_experimental` (`radv_instance.c`). Every other Mesa option goes through `os_get_option`, which on Android reads `debug.mesa.<name>` with `_` replaced by `.`: `RADV_QUEUE_DISABLE=compute` is `debug.mesa.radv.queue.disable=compute`. The `RADV_XCLIPSE_*` knobs below have their own `debug.radv_xclipse_*` properties.
- `RADV_XCLIPSE_LOG` (`debug.radv_xclipse_log`; `game.py --radv-log` sets 1): level 1 logs image creation (`[DCC]`, tag `RADV_DCC`: compression decisions, format, size, usage, view-format list), BC decode census and submits (`RADV_KILL`), wave64 cross-half lane ops (`RADV_ARM`, `[W64XLANE]`), builds, GPU page faults (`[VMFAULT]`, `RADV_VMFAULT`; summarize with `python pipeline/vmfault.py --since 'MM-DD hh:mm:ss.000'`, which needs the unstripped `libvulkan_radeon.so` of the same build). Level 2 adds the bring-up trace under tag `RADV_XCLIPSE` (`[DRAW]` per draw, `[COPY_BUF2IMG]`, `[EMIT_CS]`, layouts): about 290,000 lines in 90 s of Heaven, enough to time out `game.py`'s own logcat reads; capture it with `adb logcat -v time -T 1 -s RADV_XCLIPSE:V RADV_DCC:V RADV_KILL:V` started before the run.
- `RADV_XCLIPSE_XFB` (`debug.radv_xclipse_xfb`): how transform feedback orders its writes. `1` (default) GFX11's `ds_ordered_count` lock, `2` no lock, `3` the lock with GFX10's GS encoding, `0` the legacy path, which hangs.
- `RADV_XCLIPSE_VGPRPAD=<n>` with `RADV_XCLIPSE_VGPRPAD_MASK` (`1 << mesa_shader_stage`): gives shaders `n` more VGPRs. A diagnostic for register corruption; use it with `MESA_SHADER_CACHE_DISABLE=true`.
- `RADV_XCLIPSE_SCRATCHINIT=0`: leaves out the shaders' GFX10 scratch setup. Graphics tests pass either way.
- The 940 decodes GFX11's partial NSA encoding (`mips` passes with `textureGrad`), so it doesn't need the 920's `gfx10_nsa` workaround.

## Open work after that

- **The BCn layer question** above, and the upstream report (ask first).
- **Stock's black clouds** with native BC in Heaven (`165352-game` screenshots): Samsung's driver, not ours; worth a line in the report.
- **Per-draw cost.** NGG culling: compare in games with `debug.radv_debug=nonggc`. Port-specific CPU overhead: about 4% from cache-flush emission, 2% from logging checks on the hot path, 2% from register remapping (`docs/PROGRESS.md`).
- **Graphics scratch** is 3× slower than stock; compute scratch is even.
- **`[W64XLANE]`**: Heaven's VS/TES shaders use wave64 cross-half lane reads (`read_invocation` from NGG/tessellation repacking). The port says `v_permlane64` reads back 0 on the 920; unverified on the 940. A suspect if geometry ever looks wrong.
- **GravityMark** needs less memory (`-lod`, fewer asteroids) to run at all on this phone.
- **Driver `xd-6563904`** logs every fence or semaphore wait of 20 ms or more (`[WAIT]`, tag `RADV_WAIT`). It's not on the phone; rebuild it from that commit if a stall remains.
- **Unattended runs.** A GPU hang can reboot the phone. If the computer's internet is the phone's hotspot, the session goes offline until the hotspot is turned back on; on home Wi-Fi that doesn't apply.
