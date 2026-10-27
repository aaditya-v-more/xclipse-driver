# Handoff: 2026-10-27, on the Mac

Written at the user's request while they play on the phone. Nothing of ours is running on the phone
and no `debug.radv*` property is set. Read `AGENTS.md`, then the 2026-10-19 and 2026-10-27 entries
at the end of `docs/PROGRESS.md`, which hold the evidence behind everything here. This replaces the
2026-10-18 handoff.

The goal is unchanged: an importable performance and compatibility package for this phone, like
ExynosTools, with repeatable gains and correct rendering across memory, speed, efficiency and
visual quality. `docs/package.md` is the package's user-facing page (install, validated settings,
driver switches).

## Phone and its state

Galaxy S24 FE SM-S721B, Exynos 2400e / Xclipse 940, **Android 17** (SDK 37, build S721BXXUEEZIF,
updated since 2026-10-18), serial `RZCY90ZYQXZ`, 7.4 GB RAM with 12 GB zram.

- Connects to this Mac over USB (last used) or wireless debugging. Start adb with
  `ADB_MDNS=0 adb start-server`; see `AGENTS.md` for the "No route to host" fix. Wireless
  debugging was off when the phone came back after a week; the user turns it on.
- `stay_on_while_plugged_in` is 3 (AC or USB); leave it set.
- No `debug.radv*` property is set. `run.py` warns about stale ones.
- On the phone, kept as the user's files: driver zips (now including `xd-299e141`) and the
  `LiS3Engine.ini` test file in `/sdcard/Download/xclipse/`, and `/data/local/tmp/poll1s.sh`.
- The GPU governor is `conservative`, the table runs 252-1095 MHz with a normal maximum of
  800 MHz. Under Life is Strange the phone sits thermally capped near 285-307 MHz (skin 43 C,
  `gpu_max_clock` read 315-450 MHz during the day). Android's fixed performance mode
  (`cmd power set-fixed-performance-mode-enabled true`, `play_bench.py --fixed`) pins 400 MHz.

## App and containers

GameNative v1.3.0-prerelease, unchanged (source pinned at `third_party/gamenative`, checksums in
`third_party/gamenative-release.json`).

| Container | Driver now |
|---|---|
| Heaven-radv | RADV Xclipse xd-85cd575 |
| Heaven-stock | System |
| GravityMark-radv | RADV Xclipse xd-fc066b9 |
| GravityMark-stock | System |
| LifeIsStrange3 | **RADV Xclipse xd-299e141** |

LifeIsStrange3 settings are as on 2026-10-18 (Wrapper, DXVK 2.7.1, BCn none, FEXCore
Intermediate, Box64 Compatibility, Max Device Memory 512 MB, `-nostartupmovies -dx11`,
`DXVK_CONFIG=dxvk.enableDescriptorBuffer=False`; in-game lowest settings, 80% scaling, 1280x720).
In the game's menus never choose New Game or Buy Steph's Story.

`driver/dist/radv-xclipse-26.3.0-devel-xd-4f88558.zip` is built from `main` (the VOPD knob
removed) but **not imported yet**; `gn_driver.py install driver/dist/<zip> LifeIsStrange3` does it.
It behaves like xd-299e141 with every knob unset.

## What changed since 2026-10-18

- **`pipeline/play_bench.py`** (new): the in-game A/B. `--variant NAME[=ITEM,...]` with
  `debug.<prop>=<value>` (values may hold commas), `driver:<entry>`, `env:<NAME>=<VALUE>`,
  `set:<LABEL>=<VALUE>`; `--rounds`, `--seconds`, `--no-kill`, `--fixed`, cool-down options. It
  retries kills, keeps each run's logcat beside the results (local only), records MemFree,
  GpuSwap, GPU clock, thermal and SSIM against the baseline, and writes `results.json` and
  `summary.md` after every run. The game must not be touched while it runs: it measures the
  fixed opening frame, and any input changes the scene (two runs on 2026-10-27 were played by
  hand and are marked unusable in `PROGRESS.md`).
- **Life is Strange in play**: 21-26 FPS, 1% low 6-8, p99 85-117 ms at the thermal cap, GPU
  96-98% busy, 2.22 GB GPU memory. No shader compiles happen in play (1470 compiles, all in the
  load; pipeline cache 100% hits); the stutter is frame-time variance under the cap. DXVK submits
  about 17 times per frame, 130 us each (5% of one core).
- **lmkd kills vary with the phone's state**: 1 of 6 launches survived on 2026-10-19, 4 of 5 on
  2026-10-27, same driver and settings. Not a driver signal at this sample size.
- **The kernel can swap GPU memory but apps may not ask for it.** sgpu's Graphic Memory Reclaim
  (`sgpu_swap.c`) gives each process `/sys/kernel/gpu/proc/<tgid>/swap`, but it carries the
  `sysfs_gpu` SELinux label, read-only for `untrusted_app`. `debug.radv_xclipse_gpuswap` (xd-299e141)
  is therefore a no-op that logs `Permission denied; off`. Only the system swaps GPU memory.
- **VOPD hangs the 940**; the knob is removed. **wave32 halves ALU throughput** (xdtest `alu`
  28.4 ms in wave64, 56.7 ms with `cswave32`), so `RADV_PERFTEST=pswave32,cswave32` is a loss.
  **`s_delay_alu`** (`debug.radv_xclipse_delayalu=1`) passes every xdtest and changed nothing in
  the ALU test; its one Life is Strange run (18.2 against 21.2 FPS) is a single noisy sample.
- `r.ScreenPercentage=50` renders the title; the black scene of 2026-10-18 isn't a driver bug.
- `memwait`, `gpuswap` and `gpuswap_mb` are excluded from the shader-cache key.

## Next steps

1. **Finish the delay_alu A/B cleanly**: import xd-4f88558, then with the phone untouched
   `play_bench.py --rounds 3 --variant default --variant delayalu=debug.radv_xclipse_delayalu=1`.
   Keep it only if FPS and 1% lows hold or improve; it costs an issue slot per hint if the core
   ignores it.
2. **Energy at normal clocks**: the game is thermally capped, so CPU heat costs GPU clock. A/B
   container variants with `set:FEXCore Preset=Performance`, `set:Box64 Preset=Performance`, and
   driver knobs `debug.radv_xclipse_dcc=0`, `debug.radv_xclipse_cbgl2=1`, reading GPU MHz and
   skin temperature alongside FPS (`gn_driver.py options LifeIsStrange3 "<label>"` lists values).
3. **RADV against native in a game**: Tomb Raider (2013)'s built-in benchmark (DX11, deterministic,
   a Winlator-community standard) if the user installs it; otherwise Life is Strange with
   `--variant "radv=driver:RADV Xclipse (Mesa 26.3.0-devel-xd-4f88558),set:BCn Emulation=none"
   --variant "stock=driver:System,set:BCn Emulation=full"` (the first variant must state every
   container key). Stock failed Heaven's 2048x2048 D24 texture before, so it may not reach play.
4. **Memory**: the kill is a one-second burst at the Continue load that outruns reclaim; memwait
   and GPU swap are out. What remains: tallying DXVK's ~1 GB of suballocated buffers in the pinned
   DXVK, and proposing to GameNative that it close background apps at launch (needs the user's
   go-ahead before anything is posted). Until then, players close other apps first.
5. Older open items, untouched: Heaven normal-clock confirmation (`game_compare.py` with
   `experiments/2026-10-17/heaven-85cd575.json`); direct RADV/X11 presentation exits after
   swapchain creation; DXVK descriptor buffers stay disabled; GravityMark needs a lighter workload.

## Reproduction and rules

- Mac: `. tools/env.sh && PACKAGE_TAG=xd-<commit> driver/build.sh`;
  `gn_driver.py install driver/dist/<zip> GAME` imports and selects it.
- Tests: `tools/python/bin/python3 pipeline/run.py --drivers radv --tests textures,mips,depth,render,alu
  --env RADV_XCLIPSE_DELAYALU=1` (env switches reach only xdtest; games take the `debug.` properties).
- Only each run's `results.json` and `summary.md` are tracked; screenshots, logs and exports stay
  local. Never commit raw logcats. `results/20261027-160852-play/` is scrapped (the user was
  playing) and stays untracked.
- Commit and push as you go; no attribution trailers. Update this file only when the user says
  they're leaving the device.
- One thing drives the phone at a time, and nobody else may touch it while a benchmark runs.
  Never tap Reset container, Uninstall, Import config, Delete variable, Driver Manager trash icons
  or Debug Clear buttons, and never enter credentials. Ask before force-stopping GameNative while
  the user may be playing; `play_bench.py` force-stops the app at every slot, so never start it
  while the user plays.
- `RADV_DEBUG=nongg` hangs the 940; never use it. `RADV_PERFTEST=pswave32,cswave32` halves ALU
  throughput; don't ship it.
