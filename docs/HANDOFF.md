# Handoff: continue on any computer (written 2026-10-10, updated 2026-10-11)

The work moves between the user's Windows PC and Mac. Everything (toolchain, driver build, phone tests, game runs) works on both; the commands below give both forms. The phone stays the same: Galaxy S24 FE, SM-S721B, serial `RZCY90ZYQXZ`. Read this first, then `docs/PROGRESS.md` for the full history and numbers.

## Where things stand

- **The driver works on the phone.** `driver/` (radv-xclipse `xclipse940`, Mesa 26.3.0-devel) builds on Windows and macOS. All 14 on-device tests pass on both drivers (`results/2026-10-11_103659`): info, device, submit, compute, render, queries, the three transform-feedback tests, and the five speed tests.
- **Speed tests, RADV against Samsung's driver:**
  - FP32 throughput, fill rate, and bandwidth are equal.
  - RADV compiles pipelines 2.5–3.6× faster.
  - RADV costs 1.6–2× more per tiny draw, on both GPU and CPU. That's the main optimization target; details are in `docs/PROGRESS.md`.
- **Fixed 2026-10-11, all in driver `xd-3e88e3e`:**
  - Queries no longer need GDS, which the kernel doesn't have. That hung Heaven on radv.
  - Transform feedback (D3D11 stream output) works instead of hanging the GPU.
  - Small wave64 pixel shaders overwrote registers of other waves. That could break geometry or compute, or hang the GPU, in any game, so **every radv game result before `xd-3e88e3e` is suspect.**
- **The game-level A/B test runs, but no timed benchmark has finished yet.**
  - Heaven ran to the end on radv (driver `xd-ea7b521`), at 2 FPS with stalls of about a second.
  - Stock Heaven stops at Unigine's fatal error on a 2048x2048 D24 depth texture, even with BCn Emulation `full`.
  - GravityMark ran out of memory on both drivers. Both GravityMark containers now cap Max Device Memory at 2048 MB; not tested yet.

## Resume here (updated 2026-10-11 11:00)

Steps 1–4 of the earlier list are done (query fix installed, stock Heaven tried with BCn `full`, `game.py`'s 30 s rule, one rerun). Wine debug logs are off. Next:

1. **Check that driver `xd-3e88e3e` loads:** `game.py --bench heaven --runs 1 --radv-log`. `Driver seen` should show Mesa, and Heaven's FPS shows whether the register fix changed the 2 FPS.
2. **The timed A/B:** `game.py --runs 3`, without `--radv-log`. Commit and push the results.
3. **If Heaven is still about 2 FPS on radv:** find a GPU busy-time source (sysfs has a `devfreq` node for the GPU) and sample it during a run, to tell GPU-bound from waiting.
4. **Stock Heaven:** the D24 failure blocks the stock side. Neither driver supports `D24_UNORM_S8_UINT`, so DXVK falls back to `D32_SFLOAT_S8_UINT`, which Samsung reports without linear filtering. Unconfirmed; a small `xdtest` image-creation case would settle it.

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
| `/sdcard/Download/xclipse/radv-xclipse-26.3.0-devel-xd-*.zip` | Our driver packages, all imported into GameNative's Driver Manager: `c8fed77` (first), `ea7b521` (queries without GDS), `3e88e3e` (transform feedback and the register fix, selected in the radv containers) |
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
- Driver Manager lists each import as **"RADV Xclipse (Mesa 26.3.0-devel-xd-<sha>)"**. The radv containers use `xd-3e88e3e`.
- All four containers are configured, as in the table below. Exec Arguments and environment variables were read back from the phone and match exactly.
- Settings → Debug → **Enable Wine Debug Logs is off** (turned off 2026-10-11 before timed runs). When on, it writes `/sdcard/Android/data/app.gamenative/files/wine_logs/wine_debug.log`, which `adb` can read. **Clear Preferences and Clear Local Database sit right under it; never tap them.**

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

## Container settings (done 2026-10-11, driver updated since; kept for reference)

For each game: open it, then the menu → **Edit container**. Set:

| Game | General: Container Variant | Graphics: Graphics Driver | Graphics: Graphics Driver Version | General: Exec Arguments |
|---|---|---|---|---|
| GravityMark-stock | bionic | a Wrapper (the same one on all four) | System | `gravitymark-stock` line of `EXEC-ARGS.txt` |
| GravityMark-radv | bionic | same Wrapper | RADV Xclipse (Mesa 26.3.0-devel-xd-3e88e3e) | `gravitymark-radv` line |
| Heaven-stock | bionic | same Wrapper | System | `heaven-stock` line |
| Heaven-radv | bionic | same Wrapper | RADV Xclipse (Mesa 26.3.0-devel-xd-3e88e3e) | `heaven-radv` line |

Leave DX Wrapper at its default (DXVK `async-1.10.3` in GameNative 0.9.2) on all four, so both drivers get the same DXVK. Emulation is FEXCore 2603 for 64-bit and 32-bit code, preset Intermediate, on all four.

Graphics-tab settings that differ from the defaults, or that were checked:

- **Heaven-stock: BCn Emulation `full`** (was `auto`). It fixed the BC1/BC3 errors but not the D24 one. The other three containers have `auto`, type `compute`.
- **GravityMark-stock and GravityMark-radv: Max Device Memory 2048 MB** (was 0 MB, no limit). Path: the game's card → Options (754,655) → Edit container (521,405) → Graphics tab (383,247) → Max Device Memory row (540,1431) → 2048 MB (840,1172) → save (1001,89). Whether this is DXVK's `maxDeviceMemory` or a wrapper setting is unknown; the next GravityMark run's GPU memory peak will tell.
- Seen in GravityMark-stock and both radv containers: "Use Adrenotools Turnip" on, present mode mailbox, Memory Resource Type auto, DXVK `async-1.10.3`. GravityMark-stock also has Wine Video Memory Size 2048 MB.

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

## Driver switches for A/B tests

Set them as environment variables in `xdtest` runs, or as `debug.*` properties for apps that can't set variables.

- `RADV_XCLIPSE_XFB` (or `debug.radv_xclipse_xfb`): how transform feedback orders its writes. `1` (default) GFX11's `ds_ordered_count` lock, `2` no lock (out of order, as expected), `3` the lock with GFX10's GS encoding, `0` the legacy path, which hangs.
- `RADV_XCLIPSE_VGPRPAD=<n>` with `RADV_XCLIPSE_VGPRPAD_MASK` (`1 << mesa_shader_stage`: VS 0x1, GS 0x8, FS 0x10, CS 0x20): gives shaders `n` more VGPRs. A diagnostic for register corruption; use it with `MESA_SHADER_CACHE_DISABLE=true`.

## Open work after that

- **Heaven's 2 FPS on radv,** if the new driver doesn't fix it (see "Resume here").
- **Per-draw cost.**
  - NGG culling: compare in games with `adb shell setprop debug.radv_debug nonggc`.
  - Port-specific CPU overhead: about 4% from cache-flush emission, 2% from logging checks on the hot path, and 2% from register remapping. See `docs/PROGRESS.md`.
- **New driver builds.** Each one has to be imported through Driver Manager, since its private folder can't be reached over adb. Then set it as the radv containers' Graphics Driver Version. The package name includes our commit (`xd-<sha>`).
  - Import path (phone pixels, 2026-10-11): library Menu (1006,169) → Settings (500,699) → Driver Manager row under Emulation (540,1942) → Import ZIP from device (443,1168) → the zip in the picker, which opens in `Download/xclipse`. There's no confirmation; the entry appears in the list after about 4 s. The dialog re-centres after an import, so look up Close again before tapping it. **Each driver row has a trash icon (x=854); never tap it.**
  - Selecting it: game card → Options (754,655) → Edit container (521,405) → Graphics tab (383,247) → Graphics Driver Version (540,619) → the entry → save (1001,89). The editor title shows `Config*` until saved.
- **Unattended runs.** A GPU hang can reboot the phone. If the computer's internet is the phone's hotspot, the session goes offline until the hotspot is turned back on; on home Wi-Fi that doesn't apply.
