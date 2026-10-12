# Handoff: work stopped at the user's request, 2026-10-12

Updated on the Windows PC at 16:48 IST. The user explicitly said: **stop the work, update the handoff, commit and push**. No benchmark or build is running. Resume experiments only after a new instruction to continue. Read `AGENTS.md` first; it is shared by Claude Code and Codex, with `CLAUDE.md` importing it. `docs/PROGRESS.md` contains the detailed history.

The product goal is a usable performance package for this phone, similar to ExynosTools: repeatable gains with correct rendering. The five requested investigations are direct RADV loading, CPU work per draw, adaptive geometry culling, shader scratch, and DXVK descriptor-buffer integration. Some experiments remain incomplete; this checkpoint is not a validated release.

## Phone left clean

Galaxy S24 FE **SM-S721B**, Exynos 2400e / Xclipse 940, Android 16, serial **RZCY90ZYQXZ**. The benchmark Python process and its child processes were stopped during pair 2 of the Heaven comparison. Because hard termination bypassed `finally`, cleanup was performed manually and recorded in `results/2026-10-12_162819-game-compare/results.json`.

Verified at 16:47 IST:

- GameNative force-stopped; `pidof app.gamenative` empty; Home key sent.
- `cmd power set-fixed-performance-mode-enabled false` succeeded, return code 0 and no output. Android exposes no getter for this mode.
- `debug.radv_debug` empty; `debug.radv_xclipse_log=0`.
- `min_refresh_rate` and `peak_refresh_rate` deleted; both read back `null`.
- Do Not Disturb restored off (`zen_mode=0`).
- USB stay-awake retained (`stay_on_while_plugged_in=2`), as the user requested.
- No new driver was imported into GameNative during this optimization session. No container settings or user files were removed.

## Installed app and saved containers

The user requested an official-release reinstall and matching source pin. The phone now has **GameNative v1.3.0-prerelease**, release dated 2026-10-04, source commit **231d1365cf1a09b7f1763cfaebd3906628ec1381**, pinned at `third_party/gamenative`. Upstream's APK reports versionName **1.2.1**, versionCode **23**, target SDK 28; this is expected for that release, not evidence of a mismatched install.

`third_party/gamenative-release.json` records the official URL and verification. Local APK `tools/gamenative-v1.3.0-prerelease.apk`: 572505268 bytes, SHA256 `90cb39b8ff546265a37b0b66b42d9ab92b5a361aa8ab6af188dd26878aed0d32`. The previous APK and all four container exports before/after migration remain in `tools/gamenative-backup-2026-10-12/`. Play Protect blocked the APK; **the user selected Install anyway**. Play Protect was not disabled.

GameNative migrated the game folders into `/data/user/0/app.gamenative/CustomGames/<name>`. Old `/sdcard/GameNative/CustomGames/` paths are stale; do not replace or delete either location. Use the saved app IDs and the plain launch intent, not private-file edits or the intent's `container_config` override (which previously reset driver settings).

| Container | App ID | Driver | DXVK | BCn |
|---|---:|---|---|---|
| Heaven-stock | 2000940201 | Wrapper / System | async-1.10.3 | full |
| Heaven-radv | 2000940202 | Wrapper / RADV Xclipse (Mesa 26.3.0-devel-xd-fc066b9) | 2.7.1 | none |
| GravityMark-stock | 2000940101 | Wrapper / System | unchanged | auto |
| GravityMark-radv | 2000940102 | Wrapper / xd-fc066b9 | unchanged | auto |

Heaven-radv's working environment includes:

```
DXVK_CONFIG=dxvk.enableDescriptorBuffer=False
WRAPPER_DISABLE_PLACED=0
WRAPPER_NATIVE_BCN=1
WRAPPER_MAX_IMAGE_COUNT=0
WINEDLLOVERRIDES=msvcr100,msvcp100=b
```

It uses 1280x720, Heaven low quality/no MSAA, FEXCore 2603 **Intermediate**, Box64 0.3.6 **Compatibility**. The Performance preset experiment is complete and did not help; the original presets were restored. The exact exported configuration and its checksum are embedded in the latest game comparison's `results.json`. `nonggc` was a temporary Android property, **not** a saved container setting, and is cleared.

Stock currently fails creation of a 2048x2048 D24 depth image in the updated app. Fix that before attempting a fair current Samsung-versus-RADV game comparison. The historical October 11 stock/RADV FPS numbers used the old app and must not be presented as this release's comparison. GravityMark still encounters low-memory kills; it needs a smaller workload before it can provide useful timings.

Keep every driver ZIP, DXVK package, APK, installer, exported JSON, game folder, and original config on the phone. `/sdcard/Download/xclipse/` contains those artifacts and benchmark output. The latest retained export is `2026-10-12_162819-game-compare-container.json`; an earlier `container.json` also remains. New exports use unique basenames to avoid collisions. `/data/local/tmp/xd/` is the native harness workspace and does not select the app's driver.

## Results that are complete

| Experiment | Evidence | Interpretation |
|---|---|---|
| Logging argument guards, source `01f09ed` | `results/2026-10-12_160836` correctness; `160846-compare`, six AB/BA pairs | CPU draw recording 0.1055 to 0.0905 microseconds, 14.2% less time. GPU timings unchanged. Retained improvement; no game FPS claim yet. |
| Disable NGG culling on baseline | `results/2026-10-12_160406-compare`, 72 passing attempts | Tiny-draw GPU time 0.375 to 0.121 microseconds. Scratch initialization off gave no consistent benefit; leave initialization enabled. |
| Adaptive threshold 128, source `f3ae7c4` | `results/2026-10-12_162242-compare`, 108 passing attempts | No tiny-draw GPU gain (0.372 microseconds both ways), slight pipeline-switch CPU regression. Keep threshold default 0. |
| Compile culling out (`RADV_DEBUG=nonggc`) on logging-guard build | Same `162242-compare`, six pairs per test/variant | Tiny draws 0.121 microseconds; warmed graphics scratch 3.528 to 1.8875 ms, all paired gains positive. Promising, but not evidence for removing synchronization generally. |

These native comparisons used Android fixed performance mode: active GPU 400 MHz, X4 1632 MHz, taskset on CPU 9, alternating AB/BA order and zero GPU events. Memory clocks were not controlled. Manifests and hashes are in `experiments/2026-10-12/` and the reports.

**Correction to the old handoff:** scratch's old 10.2 ms RADV versus 3.3 ms Samsung metric was cold, host-observed submission time, not a reliable steady GPU timestamp comparison. The harness now warms for at least 250 ms and reports seven GPU-timed iterations with pixel/word correctness checks. Do not repeat the old 3x GPU claim.

### Heaven comparison is incomplete

`results/2026-10-12_162819-game-compare` planned six 90-second pairs on the **same fc066b9 / DXVK 2.7.1 container**, comparing baseline against `nonggc`. Two unscored warm-ups and three scored attempts completed before the user stopped pair 2 baseline.

Only **one complete pair** exists: 38.49 versus 49.73 FPS (+29.20%) in fixed performance mode. Pair 2 candidate reached 49.32 FPS, but has no completed baseline. **This is provisional, not a repeatable improvement or a normal-mode FPS claim. Visual review is still pending for this dataset.** The interrupted attempt is excluded and the stop/cleanup are recorded in metadata.

A measurement issue also needs fixing before rerunning: pair 2 candidate reports **46 C immediately before launch despite the 45 C cooldown limit**. The runner checks cooldown before later launch preparation; recheck the temperature at the actual launch boundary. Do not silently accept this drift. Run a fresh complete study after fixing it. The earlier `161530-game-compare` stopped after warm-ups because 42 C was unattainable; it has no scored pairs. `162433-game-compare` stopped on an export filename collision, now fixed.

## Experimental source in this checkpoint

### Direct RADV / X11 path: built, never deployed or tested on the phone

The normal driver builds `platforms=android`; Wine in GameNative needs Xlib/XCB WSI, currently supplied by the wrapper. An ICD JSON alone cannot replace that missing support.

Added a separate build path using pinned dependencies:

- `third_party/mesa-upstream` is the tenth reference submodule, pinned to **1b75efe0014c0d71163a01589bafb510460e7049** (Mesa 26.3.0-devel), sparse `src/x11`, `src/loader`, `include/vulkan`.
- Restored upstream MIT X11 helpers, the loader's X11 screen helper, and Vulkan platform headers into `driver/`; notices retained in the source and `driver/android/NOTICE.txt`. The loader helper is an adapted subset to avoid unrelated GL dependencies.
- `driver/android/x11-deps.json` pins GameNative's Bionic runtime archive (SHA256 `368db62bfc58b72c97e5169bda9aa64d4246f07964c447e27c79a065e7e9c48b`) and official Termux header packages. `pipeline/prepare_x11.py` verifies downloads and extracts the SDK into `tools/gamenative-x11/`; nothing is installed system-wide or copied to the phone.
- `pipeline/build_x11.py` builds Android + X11 in `driver/build-android-x11/`, leaving the normal build separate. SDK dependency overrides live in `driver/android/x11-sdk/meson.build`. The packaged ELF has host build RPATH removed.
- `pipeline/deploy_x11.py` verifies the ELF's pinned library dependency closure and can deploy it into a hash-named `/data/local/tmp/xd/x11-...` folder. **Only `--check-only` has run**, successfully; actual deployment has not.

The X11 build succeeded before the later no-flush edits below. Its local ELF SHA256 is `f42f14be96a42dca7940ff41ffd550622139cafa634c9bd754d1bcb826f05064`; package `driver/dist/radv-xclipse-26.3.0-devel-xd-0a61e5b-dirty-x11.zip`. **This dirty package does not represent the final checkpoint source. Rebuild after the checkpoint commit before testing.** The normal local ELF still corresponds to `f3ae7c4`.

An opt-in `RADV_XCLIPSE_GAMENATIVE_WSI=1` path is limited to Xclipse 940. It uses linear PRIME blits without scanout/modifiers or the SDMA blit queue. It avoids shared X fences unsupported by the referenced GameNative server, waits for rendering completion before presentation, and relies on PresentIdleNotify for recycling. **Actual presentation, buffer mapping/cache coherence, protocol compatibility, and rendering are unverified.** The option defaults off.

Source inspection suggests the container's environment can override `VK_ICD_FILENAMES` after GameNative sets its graphics environment, so a new APK might be unnecessary. The proposed ICD would point to the imported driver's app-private path under `/data/user/0/app.gamenative/files/contents/adrenotools/<meta.name>/vulkan.radeon.so`. **No ICD file was created and no such override was saved.** Prove this through runtime logs and correct rendering before claiming direct loading works. First keep descriptor buffers disabled.

### Small-draw no-flush experiment: not built or tested

The final C edits add **`RADV_XCLIPSE_CULL_NOFLUSH` / `debug.radv_cull_noflush`**, default false. With an enabled small-draw threshold, this attempts to omit the port's extra VGT flush only between two consecutive runtime-unculled direct vertex draws. It retains the flush on transitions and never clears flush bits requested elsewhere.

**No build or hardware test includes these final edits yet.** The chip has a known hang without VGT flushing between culling draws. The uncull branch's shader behavior motivates this experiment but does not establish its safety. Start with isolated native correctness, especially alternating culling thresholds and repeated draws; stop on any GPU event. Do not enable this in a game or promote it by default without validation.

The existing `RADV_XCLIPSE_CULL_MIN` / `debug.radv_cull_min` threshold also defaults to 0. Source `f3ae7c4` tested that threshold with the proven flush always retained; those results do not validate the new no-flush option.

## Descriptor-buffer integration remains open

DXVK 2.7.1 with descriptor buffers enabled produces GPU faults in GameNative. Disabling placed mapping or graphics pipeline libraries did not resolve it. `dxvk.enableDescriptorBuffer=False` rendered correctly in three runs; retain that compatibility workaround.

The native `descriptors` test passes on both Samsung and RADV (8192 output words and guard checks). This does not exercise every DXVK descriptor type or prove the wrapper is responsible. A suspected integration boundary is descriptor buffers requiring 32-bit VA memory types while imported DMA-BUF memory properties/address mapping differ. The wrapper source is a separate reference snapshot; its commit does not establish how the official APK's bundled binary was built.

The direct-X11 path can isolate this boundary after it renders correctly. Another untested reference knob is `WRAPPER_RESOURCE_TYPE=opaque`; verify the installed binary supports it before use. No descriptor-buffer fix has been delivered. GameNative wrapper files lack license headers: reference only, do not copy their implementation. ExynosTools likewise has no stated license.

## Resume sequence

1. Read this checkpoint and inspect `git status`. Fetch/pull before new work, preserving any local changes. Rebuild from the committed source before associating an artifact with its SHA.
2. Validate the latest C changes with the normal build and relevant native correctness tests. Evaluate no-flush separately as an opt-in experiment; preserve the baseline and run at least six paired repetitions after correctness passes.
3. Rebuild the separate X11 variant, verify/deploy its library closure for native tests with the reported `LD_LIBRARY_PATH`, and only then attempt direct loading/presentation in GameNative. Keep descriptor buffers disabled initially; inspect screenshots and faults. A native compute pass alone does not prove WSI works.
4. Fix the actual-launch temperature check, then rerun a fresh full Heaven baseline/nonggc comparison. Review screenshots and reject faults, app exits, stalls, lost samples or invalid windows. Repeat promising results at normal clocks before making game-performance claims.
5. Isolate descriptor buffers after direct loading works. Revisit scratch using warmed GPU timestamps; scratch initialization off has not helped. Wave32 variants remain untested in this session.
6. Fix stock's D24 launch failure before a current Samsung comparison. Package only improvements supported by correctness and repeatable results. Keep the official app/source baseline pinned. Upstream comments/messages need explicit user instruction to send; none have been sent.

## Reproduce on Windows or Mac

Portable tools are under ignored `tools/`; no WSL or system installs. On a new computer, clone with `core.autocrlf=false` and `core.longpaths=true`, set the author from `AGENTS.md`, then run `tools/setup.ps1` (Windows) or `sh tools/setup.sh` (Mac/Linux). Connect the same phone and authorize USB debugging. Restore exact reference pins with `pipeline/sync_sources.py`; never use `submodule update --remote`. The kernel must remain sparse because some filenames are invalid on Windows.

Windows PowerShell examples **for the next authorized work session**:

```powershell
.\tools\python\python.exe pipeline/sync_sources.py
cmd /d /c "call tools\env.cmd && driver\build.cmd"
.\tools\python\python.exe pipeline/run.py --tests render,descriptors,culling,queries,xfb,scratch,scratchcs
.\tools\python\python.exe pipeline/compare.py experiments/2026-10-12/adaptive-culling.json --fixed-clocks
.\tools\python\python.exe pipeline/game_compare.py experiments/2026-10-12/heaven-culling.json --fixed-clocks
# Separate experimental X11 build; native deployment is a later, explicit step.
.\tools\python\python.exe pipeline/prepare_x11.py
.\tools\python\python.exe pipeline/build_x11.py
.\tools\python\python.exe pipeline/deploy_x11.py driver/build-android-x11/vulkan.radeon.so --check-only
```

The existing native manifests deliberately name/hash **older reference binaries**; adapt a new manifest to the new artifact instead of pretending they benchmark the current build. On Mac/Linux use `tools/python/bin/python3` and `. tools/env.sh && driver/build.sh`. The normal build/test flow was validated on both systems previously; the new X11 scripts have only been exercised on Windows so far.

Local reference ELF hashes, not tracked binaries:

| File under tools/ | SHA256 |
|---|---|
| driver-fc066b9-reference.so | 62f186db960ad64c161118687513a73a560b5a2ac15f3628ac4700a52f5534aa |
| driver-01f09ed.so | 9d00f8100328c33726fa0a4b95e1be2856dbf91d9d7974cfffff09f11a082d9d |
| driver-f3ae7c4.so | 6c7bfe873607628eeafc325f469bfa2ad911d6fa7270ce250a9aaef7d4dd7824 |

Only `results.json` and `summary.md` are tracked per run. Screenshots, logs, binaries, exports and downloaded archives remain on this Windows PC (or on the phone where noted). Restore/rebuild rather than assuming those artifacts exist on another computer.

## Automation and safety constraints for the next session

- One phone owner at a time. Do not touch the phone or run other ADB/UI commands during a benchmark.
- `pipeline/game.py` continuously detects GPU faults, Android/Windows error dialogs, app/game exits and rendering stalls; it stops failed attempts and records evidence. `game_compare.py` requires complete frame windows, both screenshots, telemetry and no sample gaps. Its `accepted` field is automated measurement acceptance, **not visual correctness approval**.
- Normal interruption goes through cleanup; forced process termination may not. Restore props, refresh, DND and fixed mode manually if needed, and preserve USB stay-awake 2.
- Repeated UI work belongs in `pipeline/gn_driver.py` / `phone_ui.py`. `gn_driver.py` exports configs, imports/selects drivers and edits settings with readback. Commit before building an importable package so its name identifies the source.
- Never tap Reset container, Uninstall, Import config, driver trash icons or Clear Preferences/Database. Never enter credentials; skip Steam login.
- Logging level 2 is very expensive and unsuitable for benchmarks. Use level 1 only for short identity/fault diagnostics; restore level 0 afterward.
- Environment/property mappings: `RADV_DEBUG` -> `debug.radv_debug`, `RADV_PERFTEST` -> `debug.radv_perftest`; generic Mesa options use `debug.mesa.<lowercase.name>` with underscores changed to dots. Xclipse knobs can have explicit mappings as listed above; do not guess them.
- Keep the known scratch setup, query/GDS and transform-feedback fixes. Native correctness coverage and prior BCn investigations are in `docs/PROGRESS.md`; BCn `none` remains the verified RADV game configuration.
- Pinned `third_party/` trees are clean reference sources; edit `driver/` or new project code. Preserve license notices and the clean-room boundary.
