# Handoff: continue on another PC (written 2026-10-10)

The work moves to the user's home PC. The phone stays the same: Galaxy S24 FE, SM-S721B, serial `RZCY90ZYQXZ`. Read this first, then `docs/PROGRESS.md` for the full history and numbers.

## Where things stand

- **The driver works on the phone.** `driver/` (radv-xclipse `xclipse940`, Mesa 26.3.0-devel) builds on Windows. Every on-device test passes on the Xclipse 940: info, device, submit, compute, render, and the five speed tests.
- **Speed tests, RADV against Samsung's driver:**
  - FP32 throughput, fill rate, and bandwidth are equal.
  - RADV compiles pipelines 2.6–3× faster.
  - RADV costs 1.4–1.7× more per draw, on both GPU and CPU. That's the main optimization target; details are in `docs/PROGRESS.md`.
- **The game-level A/B test is prepared but hasn't run yet.** The only missing piece is the one-time container setup in GameNative, below.

## What's on the phone (keep all of it)

| Location | What |
|---|---|
| `/sdcard/GameNative/CustomGames/GravityMark-stock` (228 MB) | GravityMark 1.89, unpacked. `.gamenative` = `{"appId":2000940101}` |
| `/sdcard/GameNative/CustomGames/GravityMark-radv` (228 MB) | Same, `{"appId":2000940102}` |
| `/sdcard/GameNative/CustomGames/Heaven-stock` (274 MB) | Unigine Heaven 4.0, unpacked. `{"appId":2000940201}` |
| `/sdcard/GameNative/CustomGames/Heaven-radv` (274 MB) | Same, `{"appId":2000940202}` |
| `/sdcard/Download/xclipse/radv-xclipse-26.3.0-devel-xd-c8fed77.zip` | Our driver package, already imported into GameNative's Driver Manager |
| `/sdcard/Download/xclipse/EXEC-ARGS.txt` | Exec Arguments for each container (same as `pipeline\game.py --print-args`) |
| `/sdcard/Download/xclipse/vc_redist.x64.exe` | Visual C++ runtime installer, in case GravityMark needs it in Wine |
| `/sdcard/Download/xclipse/GravityMark_1.89.msi`, `Unigine_Heaven-4.0.exe` | The original installers |
| `/sdcard/Download/xclipse/out/` | Where the benchmarks write results (`D:\xclipse\out` in containers) |
| `/data/local/tmp/xd/` | Test harness `xdtest`, the last `vulkan.radeon.so`, and `out/`. `pipeline/run.py` redeploys these every run |

**GameNative state:** 0.9.2, the legacy GitHub APK; we chose not to update.

- On every start it shows the Steam sign-in screen. Tap **Skip login**; there's no account on this phone.
- It also offers an update to 1.2.1. Tap **Later**.
- All four games are in the library, added with **Add Game**. 0.9.2 does *not* auto-scan `GameNative/CustomGames`; after adding, refresh the library.
- The driver is imported as **"RADV Xclipse (Mesa 26.3.0-devel-xd-c8fed77)"**.
- The containers are **not configured yet**.

**Phone settings:** "Stay awake while charging" (`stay_on_while_plugged_in`) was temporarily set to 2 and is restored to its original 0. No other phone setting was changed.

**Removed:** only scratch files: `/sdcard/xd_ui.xml`, an unstripped driver copy, and simpleperf data. The profiling recipe in `docs/PROGRESS.md` regenerates them.

## On the home PC

1. **Clone the repo:** `gh repo clone aaditya-v-more/xclipse-driver`. It's a private repo under the user's GitHub account. Set the local git identity as in `CLAUDE.md`.
2. **Toolchain** (about 830 MB, every download verified):

   ```bat
   powershell -NoProfile -ExecutionPolicy Bypass -File tools\setup.ps1
   ```

3. **Phone:** plug it in over USB and accept the "Allow USB debugging" prompt for the new PC. Tick "Always allow".
4. **Check everything works:**

   ```bat
   tools\python\python.exe pipeline\run.py --build-driver --tests info,device,render
   ```

   It should pass on both drivers.
5. **Optional:** re-clone the reference repos with the commands in `third_party/MANIFEST.md`.

## Next: configure the four containers (one time, in GameNative)

For each game: open it, then the menu → **Edit container**. Set:

| Game | General: Container Variant | Graphics: Graphics Driver | Graphics: Graphics Driver Version | General: Exec Arguments |
|---|---|---|---|---|
| GravityMark-stock | bionic | a Wrapper (the same one on all four) | System | `gravitymark-stock` line of `EXEC-ARGS.txt` |
| GravityMark-radv | bionic | same Wrapper | RADV Xclipse (Mesa 26.3.0-devel-xd-c8fed77) | `gravitymark-radv` line |
| Heaven-stock | bionic | same Wrapper | System | `heaven-stock` line |
| Heaven-radv | bionic | same Wrapper | RADV Xclipse (Mesa 26.3.0-devel-xd-c8fed77) | `heaven-radv` line |

Leave DX Wrapper at its default (DXVK `2.6.1-gplasync`) on all four, so both drivers get the same DXVK.

Typing the long arguments on the phone is tedious. `pipeline/phone_ui.py` can type them over adb:

1. Tap into the Exec Arguments field.
2. Run `tools\python\python.exe pipeline\phone_ui.py type "<args>"`.

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

## Then: first game runs

1. **Smoke test** each benchmark once:

   ```bat
   tools\python\python.exe pipeline\game.py --bench gravitymark --runs 1
   ```

   Watch the phone and check these:
   - **Does GravityMark find its data when started from `bin\`?** Its `.bat` files `cd` into `bin` first. If it fails, set the container's Executable Path, or add `-data` style arguments.
   - **Does GravityMark start at all?** If not, it probably needs the VC++ runtime. Run `D:\xclipse\vc_redist.x64.exe` inside the container.
   - **Heaven:** `-extern_define PHORONIX` should start the benchmark by itself and exit at the end, which is the Phoronix Test Suite's method. If it doesn't exit, the run stops at the time limit.
   - **Is our driver really loaded in the radv runs?** Run `adb shell setprop debug.radv_xclipse_log 1`, then check that logcat shows `[BUILD] Mesa 26.3.0-devel` during radv runs. Turn it off with `setprop debug.radv_xclipse_log 0`.
2. **The real A/B:**

   ```bat
   tools\python\python.exe pipeline\game.py --runs 3
   ```

   Results go to `results\<time>-game\summary.md`.
   - FPS comes from SurfaceFlinger (GameNative's `SurfaceView` layer). GameNative's compositor uses vsync (fifo), so FPS caps at the display rate.
   - GravityMark also writes its own frame-time file and a screenshot into `out\`.
3. **Commit and push results** as usual. If RADV works in GameNative, report it on JimVulkan/radv-xclipse PR #6; the maintainer asked for exactly this test.

## Open work after that

- **Per-draw cost.**
  - NGG culling: compare in games with `adb shell setprop debug.radv_debug nonggc`.
  - Port-specific CPU overhead: about 4% from cache-flush emission, 2% from logging checks on the hot path, and 2% from register remapping. See `docs/PROGRESS.md`.
- **New driver builds.** Each one has to be imported through Driver Manager, since its private folder can't be reached over adb. Then set it as the radv containers' Graphics Driver Version. The package name includes our commit (`xd-<sha>`).
- **Unattended runs.** A GPU hang can reboot the phone. If the PC's internet is the phone's hotspot, the session goes offline until the hotspot is turned back on; on home Wi-Fi that doesn't apply.
