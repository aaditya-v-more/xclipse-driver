# Handoff: 2026-10-18, on the Mac

Written at the user's request ("commit and push the things in this repo and update handoff") after
a night of autonomous work on Life is Strange: True Colors. Nothing is running on the phone. Read
`AGENTS.md`, then the 2026-10-17 and 2026-10-18 entries at the end of `docs/PROGRESS.md`, which
hold the evidence behind everything here. This replaces the 2026-10-13 handoff.

The goal is unchanged: an importable performance and compatibility package for this phone, like
ExynosTools, with repeatable gains and correct rendering across memory, speed, efficiency and
visual quality. `docs/package.md` is the package's user-facing page (install, validated settings,
driver switches).

## Phone and its state

Galaxy S24 FE SM-S721B, Exynos 2400e / Xclipse 940, Android 16, serial `RZCY90ZYQXZ`, 7.4 GB RAM
with 12 GB zram.

- Connected to this Mac over wireless debugging (paired) or USB. Start adb with
  `ADB_MDNS=0 adb start-server`; see `AGENTS.md` for the "No route to host" fix.
- `stay_on_while_plugged_in` is 3 (AC or USB); leave it set.
- `debug.radv_xclipse_log` and `debug.radv_xclipse_memwait` are 0, and no other `debug.radv*`
  property is set. `run.py` warns about stale ones.
- On the phone, kept as the user's files: driver zips and the `LiS3Engine.ini` test file (unused)
  in `/sdcard/Download/xclipse/`, and the poller `/data/local/tmp/poll1s.sh` (game RSS/swap and
  MemFree every second).

## App and containers

GameNative v1.3.0-prerelease, unchanged (source pinned at `third_party/gamenative`, checksums in
`third_party/gamenative-release.json`). Game folders live in
`/data/user/0/app.gamenative/CustomGames/<name>`, private to the app, so files can't be placed
there with adb.

| Container | Driver now |
|---|---|
| Heaven-radv | RADV Xclipse xd-85cd575 |
| Heaven-stock | System |
| GravityMark-radv | RADV Xclipse xd-fc066b9 |
| GravityMark-stock | System |
| LifeIsStrange3 | RADV Xclipse xd-ac77309 |

LifeIsStrange3: Wrapper, DXVK 2.7.1, BCn none, FEXCore Intermediate, Box64 Compatibility,
**Max Device Memory 512 MB**, Exec Arguments `-nostartupmovies -dx11`,
`DXVK_CONFIG=dxvk.enableDescriptorBuffer=False` (the DXVK HUD was removed). In-game: every graphics
option at its lowest, Anti-Aliasing High (its minimum), Resolution Scaling Yes at 80%, 1280x720.
In the game's menus never choose New Game (it could overwrite the save) or Buy Steph's Story.

## What the driver has gained since 2026-10-13

All on `main` and in the current builds (details and numbers in `PROGRESS.md`):

- **Heaven's blue streaks fixed** (`288a2c6`): coherent and volatile loads get DLC so they bypass
  GL1, as on GFX10.3. The stale data was the tessellation offchip ring. No FPS cost.
  `debug.radv_xclipse_gl1scope=0` turns it off.
- **Linear filtering emulated** in the shader for the formats the 940 point-samples (16-bit
  UNORM/SNORM, 32-bit float, depth without compare). It's on by default;
  `debug.radv_xclipse_filteremu=0` turns it off. It costs about 2% in fixed performance mode and
  nothing measurable at normal clocks.
- **NGG culling off by default** on the 940; opt in with `RADV_PERFTEST=nggc`. Heaven measured
  +28% average and +22% 1%-low FPS without it over five clean pairs in fixed mode.
- **Shader disk cache persists** across GameNative launches (`$XDG_CACHE_HOME` or `$HOME/.cache`
  before `$TMPDIR`, which GameNative empties).
- **BC memory**: BC7 now aliases a BC3 carrier (1x Samsung's size, was 2-5x). The BC5-to-EAC table
  is minimax-generated (`meta/eac_lut_gen.c`), halving BC5 error. BC4 stays on the exact BC3
  carrier (3x memory), since EAC_R11 missed by up to 8/255.
- **Diagnostics** under `debug.radv_xclipse_log 1`: `[BO_KIND]`, `[BO_BIG]`, `[IMG_MEM]`/`[IMG_BIG]`
  and `[BUF_MEM]`/`[BUF_BIG]` tallies (tag RADV_CHURN), `[SHADER]` register lines, `[PSOCACHE]`.
- **Opt-in allocation backpressure** `debug.radv_xclipse_memwait=N` (xd-ac77309). It showed no
  clear survival gain and slowed some loads; leave it off.

## Life is Strange: where it stands

It now reaches gameplay (about 26 FPS, renders correctly) and stays there in most runs:

- 4 of 5 runs with `am kill-all` through the load (what `game_input.py play` does by default).
- 4 of 6 with `play --no-kill`, the way a player launches. The failures are Android's lmkd killing
  GameNative ("min2x watermark is breached even after kill"), at the Continue load or soon after.

Why: at a scene load GPU memory (pinned GTT, not swappable) grows about 1.2-1.8 GB while the game's
CPU heap grows 0.55 GB and gets swapped to zram, and free pages fall to the watermark. In play the
game's gpumem (2134 MB) equals DXVK's heap (2081 MB) plus about 55 MB of driver BOs. Of DXVK's
1651 MB used, images are about 610 MB; the remaining ~1 GB is buffers that DXVK suballocates in its
chunks (likely UE4 mesh data), which the driver can't classify.

Tried and ruled out: `-ExecCmds` (ignored by this Shipping build); streaming-pool and mip-bias
cvars through `-ENGINEINI=D:/xclipse/LiS3Engine.ini` (the file is read and the cvars apply, but
texture memory didn't move); `dxvk.maxMemoryBudget` (chunk churn); a smaller reported heap
(crashed the phone earlier); memwait. `r.ScreenPercentage=50` via the ini turned the title's 3D
scene black; whether that's a driver bug is untested.

## Scripts added this stretch

- `pipeline/game_input.py`: presses GameNative's on-screen gamepad (`press A RB --times N`), and
  `play LifeIsStrange3 [--restart] [--until title] [--no-kill]` launches and gets past the intro
  screens to gameplay, reporting lmkd kills. D-pad taps are 40 ms; longer holds skip menu values.
- `pipeline/gn_driver.py`: routes between GameNative screens instead of restarting, works in
  landscape, and has `apply GAME --driver/--set/--env/--args [--launch]` and `launch`.
- `pipeline/artifact_screen.sh`, `pipeline/ssim.py`, and frame-time metrics (p95/p99, frames over
  50 ms) in `game.py`/`game_compare.py`.

## Next steps

1. **Life is Strange survival without `am kill-all`.** Options in order:
   - Patch the pinned DXVK to tally D3D11 buffers by bind flags and usage, to confirm what the
     ~1 GB is. Fable expects static-mesh data that no DXVK option cuts.
   - Propose to GameNative that it close background apps at launch
     (`ActivityManager.killBackgroundProcesses` needs only a normal permission). Needs the user's
     go-ahead before anything is posted.
   - Until then, tell players to close other apps first.
2. **Normal-clock confirmation** of the Heaven package settings (NGG culling off, filter emulation
   on), reporting FPS, 1% lows, p99 and memory, then a second game with many shaders to show the
   shader-cache gain.
3. **Check whether `r.ScreenPercentage=50` blacking out the title is a driver bug** (try the same
   setting on the stock driver if the game runs there).
4. Older open items from 2026-10-13, untouched since: direct RADV/X11 presentation exits after
   swapchain creation; DXVK descriptor buffers stay disabled; stock fails a 2048x2048 D24 image;
   GravityMark needs a lighter workload. `PROGRESS.md` has the details.

## Reproduction and rules

- Mac: `. tools/env.sh && driver/build.sh`. Set `PACKAGE_TAG=xd-<commit>` (or use
  `run.py --build-driver`) so the zip and driver name carry the commit.
  `gn_driver.py install driver/dist/<zip> GAME` imports and selects it.
- Tests: `tools/python/bin/python3 pipeline/run.py --tests textures,mips,depth,render,...`.
- Only each run's `results.json` and `summary.md` are tracked; screenshots, logs and exports stay
  local. Never commit raw logcats.
- Commit and push as you go; no attribution trailers. Update this file only when the user says
  they're leaving the device.
- One thing drives the phone at a time. Never tap Reset container, Uninstall, Import config,
  Delete variable, Driver Manager trash icons or Debug Clear buttons, and never enter credentials.
  Ask before force-stopping GameNative while the user may be playing.
- `RADV_DEBUG=nongg` hangs the 940; never use it.
