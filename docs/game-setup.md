# Game benchmark setup (GameNative)

The game-level A/B test runs Windows benchmarks in GameNative (`app.gamenative`). Each benchmark is installed twice, as two custom games with matching emulator presets and driver-specific graphics settings: "System" (Samsung) or our RADV build. `pipeline/game.py` runs them over adb.

On 2026-10-12 the test phone was updated in place from 0.9.2 to the official GitHub
`v1.3.0-prerelease` APK. Its installed SHA256 matches the release asset; its source is pinned at
`231d1365cf1a09b7f1763cfaebd3906628ec1381`. Upstream still labels this APK `1.2.1` (versionCode 23).
Use [`../third_party/gamenative-release.json`](../third_party/gamenative-release.json) to identify
the exact build, rather than the app's displayed version. Earlier results remain 0.9.2 results.

## Already done over USB

- The benchmarks were unpacked on the PC without running their installers:
  - GravityMark 1.89 MSI with `pymsi`.
  - Unigine Heaven 4.0 with innoextract 1.9, verified by GPG signature and MD5.
  - `Browser.exe`, `browser_x86.exe`, and `vc_redist.x64.exe` were left out, so each copy has exactly one `.exe` for GameNative to auto-detect.
- The four game folders were pushed to `/sdcard/GameNative/CustomGames/` and registered as
  custom games. The `v1.3.0-prerelease` upgrade automatically migrated them to
  `/data/user/0/app.gamenative/CustomGames/` when opening their containers; exported drive
  mappings confirm all four new locations. Their appIds are unchanged:

  | Folder | `.gamenative` appId |
  |---|---|
  | `GravityMark-stock` | 2000940101 |
  | `GravityMark-radv` | 2000940102 |
  | `Heaven-stock` | 2000940201 |
  | `Heaven-radv` | 2000940202 |

- These files are in `/sdcard/Download/xclipse/` (`D:\xclipse\` inside containers):
  - the driver zips, `radv-xclipse-26.3.0-devel-xd-<sha>.zip`. The selected build is `xd-fc066b9`.
  - `EXEC-ARGS.txt`
  - `vc_redist.x64.exe`, in case GravityMark needs the Visual C++ runtime
  - `out/`, where the benchmarks write their results

## One-time steps in GameNative

1. **Menu (top right of the library) → Settings → Emulation → Driver Manager → Import ZIP from device.** Pick the newest `Download/xclipse/radv-xclipse-26.3.0-devel-xd-<sha>.zip`.
2. **Settings:** turn off "Auto-apply known config" if it's there. Otherwise GameNative may silently apply its own recommended settings to new containers.
3. **Library:** the four games appear by folder name. For each one, open the game menu and choose **Edit container**:
   - **General:**
     - Container Variant: `bionic`.
     - Exec Arguments: the line for that game from `EXEC-ARGS.txt`. Print them with `python pipeline/game.py --print-args` (the toolchain's Python, see `README.md`).
   - **Graphics:**
     - Graphics Driver: the same Wrapper on all four (Wrapper-gamenative if offered).
     - Graphics Driver Version: `System` for `*-stock`, and `RADV Xclipse (Mesa 26.3.0-devel-xd-<sha>)`, the newest, for `*-radv`.
     - DX Wrapper: DXVK, same version on all four.
   - Save.

## Inspecting and changing emulator presets

Before an app update, export each container to a new filename and keep the local and phone copies:

```sh
python pipeline/gn_driver.py export-config Heaven-radv tools/Heaven-radv-backup.json
```

The helper requires the Android file picker to be in `Download/xclipse`, refuses to overwrite
existing files, waits for GameNative's asynchronous JSON write, then pulls and verifies the copy.

`pipeline/gn_driver.py` reads dropdowns with `get`, lists their choices with `options`, and changes
them with `set`. It reopens the container after saving and verifies the value. The default tab is
Graphics; use `--tab Emulation` for FEXCore and Box64 presets. Use the toolchain's Python below
(`tools/python/bin/python3` on macOS/Linux, `tools\python\python.exe` on Windows):

```sh
python pipeline/gn_driver.py get Heaven-radv "FEXCore Preset" --tab Emulation
python pipeline/gn_driver.py options Heaven-radv "FEXCore Preset" --tab Emulation
python pipeline/gn_driver.py set Heaven-radv "FEXCore Preset" Performance --tab Emulation
python pipeline/gn_driver.py set Heaven-radv "Box64 Preset" Performance --tab Emulation
```

For a driver A/B, apply the same presets to `Heaven-stock` and `Heaven-radv`, record the previous
values, and keep the other settings fixed. Inspect screenshots as well as frame rates: faster
presets relax emulation behavior and a run with wrong rendering does not count. Nobody else may
touch the phone or run another phone script during these commands.

Text environment variables can be added or updated through the Environment tab:

```sh
python pipeline/gn_driver.py env Heaven-radv DXVK_CONFIG "dxvk.enableDescriptorBuffer=False"
```

For several changes, `envs` opens the container once, saves once, then reopens it and verifies
every value. Quote an entire `NAME=VALUE` argument when its value contains spaces:

```sh
python pipeline/gn_driver.py envs Heaven-radv DXVK_LOG_LEVEL=none WRAPPER_DISABLE_PLACED=0
```

The helper scrolls the tab row, uses the Add dialog to insert or replace the named value, saves,
and verifies it after reopening. It never deletes a variable. Record the old value before changing
it; this command replaces the entire value, so preserve any other settings in an existing
`DXVK_CONFIG`. Use `dxvk.enableDescriptorBuffer=Auto` to restore that option's default.

On the verified GameNative v1.3.0-prerelease APK and RADV `xd-fc066b9`, Heaven with DXVK 2.7.1
faults at startup with descriptor buffers enabled. Disabling them is a working compatibility
setting; BCn remains `none`, with FEXCore `Intermediate` and Box64 `Compatibility`. The exact
measurements and remaining uncertainty are in `PROGRESS.md`. This is not a general performance
recommendation for other games or drivers.

## Running

For GameNative 0.9.2, an unbundled DXVK version must first be installed in Contents Manager.
`pipeline/package_dxvk.py` builds a WCP from the verified upstream 2.7.1 release archive and LICENSE;
`pipeline/gn_driver.py import-content tools/dxvk-2.7.1-gamenative.wcp` imports and reads it back.
The importer verifies the phone copy's checksum and keeps existing files. Selecting a version in
the container dropdown alone does not install its DLLs. See the progress log for exact checksums.

```sh
tools/python/bin/python3 pipeline/game.py --runs 3
```

On Windows: `tools\python\python.exe pipeline\game.py --runs 3`.

For each run, the script:

- launches each game with `am start -n app.gamenative/.MainActivity -a app.gamenative.LAUNCH_GAME --ei app_id N --es game_source CUSTOM_GAME`
- records SurfaceFlinger present times of GameNative's SurfaceView, skipping the first 10 s
- pulls GravityMark's `-times` and `-image` files
- force-stops the app, then cools the GPU down before the next run
- detects Wine error dialogs, fatal app logs, unexpected process exits, lost rendering surfaces,
  and GPU faults; saves a failure screenshot and excludes that attempt from the FPS medians
- saves the completed results after every attempt, and skips a benchmark/driver after two
  consecutive failures (`--max-failures` changes the limit) or immediately after a GPU fault
- polls GPU fault/reset counters during each run and stops GameNative on the first observed
  increase; this does not wait for the run timer or a failure screenshot

Drivers alternate order from run to run. Results go to `results/<time>-game/`.
Use repeatable `--note "..."` arguments to record experiment settings in both reports.
The metadata and summary also record when `--radv-log` was enabled.

`game_compare.py` also reports 1% lows, RSS/swap peaks, memory pressure and battery temperature.
RSS is the sum across app/Wine processes and can count shared pages more than once; GPU accounting
can omit imported DMA-BUF allocations. Battery readings while USB is attached do not establish
power consumption. Fixed-mode pairs are excluded from timing comparisons when their observed
GPU/X4 clocks differ or change during a run. Inspect the reported clock profiles and screenshots
before treating any timing gain as usable; memory, frame-time and image-quality tradeoffs matter.

Notes:

- GameNative's compositor uses fifo (vsync) present mode by default, so SurfaceFlinger FPS is capped at the display refresh rate. That's fine for benchmarks running below it.
- Heaven runs with `-extern_define PHORONIX`, which should start the benchmark automatically and exit at the end (the Phoronix Test Suite's method). If it doesn't exit, the run stops at the time limit.
- An early game exit is recorded as `game-exited` and excluded because completion has not been
  independently verified. Use a fixed measurement window such as `--seconds 90` for Heaven.
