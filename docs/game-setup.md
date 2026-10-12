# Game benchmark setup (GameNative)

The game-level A/B test runs Windows benchmarks in GameNative (`app.gamenative`, 0.9.2 on the test phone). Each benchmark is installed twice, as two custom games with identical containers except for the Vulkan driver: "System" (Samsung) or our RADV build. `pipeline/game.py` runs them over adb.

## Already done over USB

- The benchmarks were unpacked on the PC without running their installers:
  - GravityMark 1.89 MSI with `pymsi`.
  - Unigine Heaven 4.0 with innoextract 1.9, verified by GPG signature and MD5.
  - `Browser.exe`, `browser_x86.exe`, and `vc_redist.x64.exe` were left out, so each copy has exactly one `.exe` for GameNative to auto-detect.
- The four game folders are pushed to `/sdcard/GameNative/CustomGames/`, which GameNative scans automatically:

  | Folder | `.gamenative` appId |
  |---|---|
  | `GravityMark-stock` | 2000940101 |
  | `GravityMark-radv` | 2000940102 |
  | `Heaven-stock` | 2000940201 |
  | `Heaven-radv` | 2000940202 |

- These files are in `/sdcard/Download/xclipse/` (`D:\xclipse\` inside containers):
  - the driver zips, `radv-xclipse-26.3.0-devel-xd-<sha>.zip`. The newest is `xd-3e88e3e`.
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

## Running

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

Drivers alternate order from run to run. Results go to `results/<time>-game/`.

Notes:

- GameNative's compositor uses fifo (vsync) present mode by default, so SurfaceFlinger FPS is capped at the display refresh rate. That's fine for benchmarks running below it.
- Heaven runs with `-extern_define PHORONIX`, which should start the benchmark automatically and exit at the end (the Phoronix Test Suite's method). If it doesn't exit, the run stops at the time limit.
- An early game exit is recorded as `game-exited` and excluded because completion has not been
  independently verified. Use a fixed measurement window such as `--seconds 90` for Heaven.
