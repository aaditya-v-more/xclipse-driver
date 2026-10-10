# Handoff: continue on any computer (written 2026-10-10, updated 2026-10-11)

The work moves between the user's Windows PC and Mac. Everything (toolchain, driver build, phone tests, game runs) works on both; the commands below give both forms. The phone stays the same: Galaxy S24 FE, SM-S721B, serial `RZCY90ZYQXZ`. Read this first, then `docs/PROGRESS.md` for the full history and numbers.

## Where things stand

- **The driver works on the phone.** `driver/` (radv-xclipse `xclipse940`, Mesa 26.3.0-devel) builds on Windows and macOS. Every on-device test passes on the Xclipse 940: info, device, submit, compute, render, and the five speed tests.
- **Speed tests, RADV against Samsung's driver:**
  - FP32 throughput, fill rate, and bandwidth are equal.
  - RADV compiles pipelines 2.6–3× faster.
  - RADV costs 1.4–1.7× more per draw, on both GPU and CPU. That's the main optimization target; details are in `docs/PROGRESS.md`.
- **The game-level A/B test runs, but no benchmark has finished yet.**
  - All four GameNative containers are set up, and our driver loads inside GameNative.
  - GravityMark runs out of memory on both drivers.
  - Heaven now starts on both drivers. Stock stops at a texture error, and radv hangs on a GDS allocation, a bug in our driver.
  - Details and the next steps are in the next section.

## Resume here (paused 2026-10-11 01:45)

Work through these in order. The phone was left idle: GameNative force-stopped, `stay_on_while_plugged_in` back to 0, `debug.radv_xclipse_log` back to 0.

1. **Fix the GDS hang in our driver.** This blocks every radv run of a D3D11 game that uses pipeline-statistics queries, Heaven included. The diagnosis is in `docs/PROGRESS.md`.
   - **Cause:** the Xclipse 940 runs as GFX10_3. On GFX10–10.3, RADV counts some query results from NGG shaders with GDS atomics. The first such query makes the queue allocate a 256-byte GDS buffer (`radv_queue.c`, `needs->gds`). Samsung's `sgpu` kernel driver has no GDS, so the allocation fails and the submission with it.
   - **Fix: on Xclipse (`pdev->info.xclipse_model != AC_XCLIPSE_NONE`), never take those paths:**
     - `radv_physical_device.c`, where `emulate_ngg_gs_query_pipeline_stat` and `emulate_mesh_shader_queries` are set (about line 3092): false on Xclipse.
     - `radv_query.c`, `uses_emulated_queries` in `radv_CreateQueryPool` (about line 1912): its `pdev->use_ngg && PRIMITIVES_GENERATED_EXT` term gets the same guard.
     - `radv_cmd_buffer.c`, `radv_get_ngg_state_query`: an inherited `GEOMETRY_SHADER_PRIMITIVES` statistic also turns on the shaders' GDS counters, so guard that term too.
     - Optional: in `radv_shader.c` (about lines 1042 and 1060), derive `has_gs_primitives_query` and `has_ms_gs_invocations_query` from `key.emulate_ngg_gs_query_pipeline_stat` instead of `gfx_level < GFX11`. Then no GDS instruction is compiled on Xclipse at all. Other GPUs are unaffected, since NGG shaders imply `use_ngg`.
   - **Cost:** those statistics then come from the hardware counters, which miss primitives from NGG shaders. Games don't depend on them.
   - **Test it before GameNative.** Add an `xdtest` case that wraps a draw in a pipeline-statistics query with every statistic bit set and checks that the submission succeeds. Run it with `run.py --build-driver`.
   - **Install the new build:**
     - `adb push` the new zip from `driver/dist/` to `/sdcard/Download/xclipse/`. Keep the old zip.
     - Import it in GameNative's Driver Manager.
     - Select it as Graphics Driver Version in Heaven-radv and GravityMark-radv.
2. **Stock Heaven: Samsung's driver refuses Heaven's textures.**
   - DXVK can't create DXT1/DXT5 (BC1/BC3) or 2048x2048 D24 textures (screenshot in `results/2026-10-11_013033-game`).
   - In `auto` mode the wrapper hands BC1–BC3 to Samsung's driver natively.
   - Try BCn Emulation `full` (Graphics tab) on Heaven-stock. Record what the stock side needs to run; the comparison is against the best working stock setup.
3. **`game.py`: don't count a few frames as a finished run.** The stock Heaven run printed "ran 8.8 fps over 15 s", but those frames were Wine drawing the error box.
   - Require a minimum stretch of steady frames, for example 30 s; otherwise report `too-short`.
   - Take the end-of-run screenshot for those runs too.
4. **Rerun once:** `game.py --bench heaven,gravitymark --runs 1 --radv-log`.
   - **GravityMark:** compare `Its GPU memory peak` between the drivers. If it still runs out of memory, try Max Device Memory 2048 MB in both GravityMark containers.
   - **Native BC on radv:** `launch_settings` in `results.json` shows whether `WRAPPER_NATIVE_BCN=1` reached the game. RADV's image log suggested the wrapper still emulates BC1 on radv; GameNative 0.9.2's wrapper may not know the variable.
5. **Before timed runs:** turn off GameNative's Settings → Debug → Enable Wine Debug Logs (it's on now). Then run the real A/B with `game.py --runs 3`.

**Phone UI work goes to a Sonnet subagent** (see `CLAUDE.md`). Container edits, Driver Manager imports, and settings changes are slow, mechanical taps.

- Give it the exact values and the UI rules from this file.
- Have it read every change back after saving.
- Only one agent may drive the phone at a time.

## What's on the phone (keep all of it)

| Location | What |
|---|---|
| `/sdcard/GameNative/CustomGames/GravityMark-stock` (228 MB) | GravityMark 1.89, unpacked. `.gamenative` = `{"appId":2000940101}` |
| `/sdcard/GameNative/CustomGames/GravityMark-radv` (228 MB) | Same, `{"appId":2000940102}` |
| `/sdcard/GameNative/CustomGames/Heaven-stock` (274 MB) | Unigine Heaven 4.0, unpacked. `{"appId":2000940201}` |
| `/sdcard/GameNative/CustomGames/Heaven-radv` (274 MB) | Same, `{"appId":2000940202}` |
| `/sdcard/Download/xclipse/radv-xclipse-26.3.0-devel-xd-c8fed77.zip` | Our driver package, already imported into GameNative's Driver Manager |
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
- The driver is imported as **"RADV Xclipse (Mesa 26.3.0-devel-xd-c8fed77)"**.
- All four containers are configured (2026-10-11), as in the table below. Exec Arguments and environment variables were read back from the phone and match exactly.
- Settings → Debug → **Enable Wine Debug Logs is on** (for the Heaven crash analysis). It writes `/sdcard/Android/data/app.gamenative/files/wine_logs/wine_debug.log`, and `adb` can read it. Turn it off before timed runs.

**Phone settings:** "Stay awake while charging" (`stay_on_while_plugged_in`) was temporarily set to 2 and is restored to its original 0. No other phone setting was changed.

**Removed:** only scratch files: `/sdcard/xd_ui.xml`, an unstripped driver copy, and simpleperf data. The profiling recipe in `docs/PROGRESS.md` regenerates them.

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

## Container settings (done 2026-10-11, kept for reference)

For each game: open it, then the menu → **Edit container**. Set:

| Game | General: Container Variant | Graphics: Graphics Driver | Graphics: Graphics Driver Version | General: Exec Arguments |
|---|---|---|---|---|
| GravityMark-stock | bionic | a Wrapper (the same one on all four) | System | `gravitymark-stock` line of `EXEC-ARGS.txt` |
| GravityMark-radv | bionic | same Wrapper | RADV Xclipse (Mesa 26.3.0-devel-xd-c8fed77) | `gravitymark-radv` line |
| Heaven-stock | bionic | same Wrapper | System | `heaven-stock` line |
| Heaven-radv | bionic | same Wrapper | RADV Xclipse (Mesa 26.3.0-devel-xd-c8fed77) | `heaven-radv` line |

Leave DX Wrapper at its default (DXVK `async-1.10.3` in GameNative 0.9.2) on all four, so both drivers get the same DXVK. Emulation is FEXCore 2603 for 64-bit and 32-bit code, preset Intermediate, on all four.

Environment tab, added on top of GameNative's defaults:

| Variable | Containers | Why |
|---|---|---|
| `WRAPPER_NATIVE_BCN=1` | all four | GameNative's wrapper otherwise emulates BC textures for every driver but Qualcomm's, Turnip and NVIDIA's, RADV included. Samsung's driver has no BC support, so the stock runs are unchanged. |
| `WINEDLLOVERRIDES=msvcr100,msvcp100=b` | Heaven-stock, Heaven-radv | The native `msvcr100.dll` crashes under 32-bit FEXCore emulation (`docs/PROGRESS.md`); Wine's built-in one works. |

Typing the long arguments on the phone is tedious. `pipeline/phone_ui.py` can type them over adb:

1. Tap into the Exec Arguments field.
2. Run `python pipeline/phone_ui.py type "<args>"`. It reads the field back and prints "typed and checked" only when it matches. The phone's keyboard, Gboard, inserts a space when a letter follows `.` or `,` (it broke `unigine.cpp` and `PHORONIX,RELEASE,QUALITY_LOW`), so the helper types around that. `phone_ui.py clear` empties the focused field.

The same helper can drive the whole UI:

- `dump` lists on-screen elements.
- `tap-text "Label"` taps an element by its text.
- **Nobody may touch the phone while it drives.** Two people tapping at once lands on the wrong buttons.
- The screen timeout is 30 s. Keep it awake with `adb shell svc power stayon usb`, and afterwards **restore** it with `adb shell settings put global stay_on_while_plugged_in 0`.
- Don't use repeated Shift key presses as a keep-alive: GameNative may treat them as controller input.

Don't try these shortcuts; we checked them in the v0.9.2 source:

- **Editing the config files:** containers live in `/data/data/app.gamenative/...`, which adb can't reach without root.
- **The `LAUNCH_GAME` intent's `container_config` extra:** it can set `execArgs` and `envVars`, but `ContainerUtils.applyToContainer` then sets `graphicsDriverConfig` to `""` for that launch. RADV silently falls back to System, and the wrapper settings, including `vulkanVersion`, reset.
- **Selecting the driver with `ADRENOTOOLS_*` environment variables:** GameNative's `EnvVars.putAll` splits on spaces, so it would need a driver name without spaces. It still couldn't fix the variant: the wrapper/adrenotools path needs `containerVariant=bionic` (`XServerScreen.kt:4577`), and the intent can't set that.

`pipeline/game.py` therefore launches with the plain intent (`app_id` and `game_source` only) and relies on each container's saved settings.

## Running the game A/B

- **Smoke test:** `python pipeline/game.py --bench heaven,gravitymark --runs 1 --radv-log`.
  - `--radv-log` makes RADV log `[BUILD] Mesa 26.3.0-devel`, which proves the radv runs loaded our driver. The `Driver seen` column shows it.
  - Leave it off for timed runs.
- **Before every run** `game.py` closes background apps with `am kill-all` and keeps the screen on. It also samples free memory, GameNative's memory, and its GPU memory every 5 s. It records low-memory kills as `killed-low-memory`.
- **Nobody may touch the phone during runs.** Rotating it made GameNative recreate its surface, and lmkd killed it.
- **The real A/B:** `python pipeline/game.py --runs 3`.
  - Results go to `results/<time>-game/summary.md` and `results.json`. `results.json` includes the settings GameNative launched each container with.
  - FPS comes from SurfaceFlinger, read from whichever GameNative SurfaceView layer has frames. GameNative presents with vsync, so FPS caps at the display rate.
  - GravityMark also writes its own frame-time file and a screenshot into `out\`, which `game.py` pulls.
- **Commit and push results** as usual. If RADV works in GameNative, report it on JimVulkan/radv-xclipse PR #6; the maintainer asked for exactly this test.

## Open work after that

- **Per-draw cost.**
  - NGG culling: compare in games with `adb shell setprop debug.radv_debug nonggc`.
  - Port-specific CPU overhead: about 4% from cache-flush emission, 2% from logging checks on the hot path, and 2% from register remapping. See `docs/PROGRESS.md`.
- **New driver builds.** Each one has to be imported through Driver Manager, since its private folder can't be reached over adb. Then set it as the radv containers' Graphics Driver Version. The package name includes our commit (`xd-<sha>`).
- **Unattended runs.** A GPU hang can reboot the phone. If the computer's internet is the phone's hotspot, the session goes offline until the hotspot is turned back on; on home Wi-Fi that doesn't apply.
