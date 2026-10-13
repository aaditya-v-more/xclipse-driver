# Handoff: stopped at the user's request, 2026-10-13

Updated on the Windows PC after the 18:03 IST native run. The user said **stop the work, update handoff, commit and push**. No benchmark or build is running. Resume experiments only after a new instruction to continue. Read `AGENTS.md` and the latest `docs/PROGRESS.md` entries. This supersedes the October 12 checkpoint.

The goal is an importable performance/compatibility package for this phone, similar to ExynosTools, with repeatable gains and correct rendering. The user expanded the scope today to **RAM/memory, speed, efficiency and visual quality**. Average FPS is only one outcome; state tradeoffs. Research and benchmarks support the package; they are not the deliverable. No new validated release was promoted today.

## Phone and stopped state

Galaxy S24 FE **SM-S721B**, Exynos 2400e / Xclipse 940, Android 16, serial **RZCY90ZYQXZ**.

- Heaven stopped itself at pair 4 because its captured frame window was too short. Its `finally` completed: GameNative force-stopped, fixed performance mode release returned 0 with no output, debug properties and refresh settings restored. Android has no getter for fixed mode.
- Subsequent readback confirmed `debug.radv_debug` empty, USB stay-awake **2**, and all six GPU event counters **0**. The final native run also completed without a fault.
- DND was temporarily `none` during direct-X11 diagnostics. It was restored to the original **off** state and read back as `zen_mode=0` before the final native run.
- Final run: `results/2026-10-13_180339`. Its ending telemetry: GPU 46 C, battery 42.7 C, charge 41%.
- No phone files, imports, game folders, installers or environment rows were removed. Leave USB stay-awake enabled.

## App, containers and saved settings

The official **GameNative v1.3.0-prerelease** (release 2026-10-04) is unchanged. Source **231d1365cf1a09b7f1763cfaebd3906628ec1381** is pinned at `third_party/gamenative`. APK versionName **1.2.1**, versionCode **23**, target SDK 28 are expected. `third_party/gamenative-release.json` records verification. Local APK `tools/gamenative-v1.3.0-prerelease.apk`: 572505268 bytes, SHA256 `90cb39b8ff546265a37b0b66b42d9ab92b5a361aa8ab6af188dd26878aed0d32`. Previous APK/exports remain in `tools/gamenative-backup-2026-10-12/`. The user accepted Play Protect's install prompt; Play Protect was not disabled.

Game folders are `/data/user/0/app.gamenative/CustomGames/<name>`. Old `/sdcard/GameNative/CustomGames/` paths are stale; preserve both locations. Use plain launch intents and saved IDs, not private-file edits or the `container_config` override that previously reset settings.

| Container | App ID | Driver | DXVK | BCn |
|---|---:|---|---|---|
| Heaven-stock | 2000940201 | Wrapper / System | async-1.10.3 | full |
| Heaven-radv | 2000940202 | Wrapper / RADV Xclipse (Mesa 26.3.0-devel-xd-fc066b9) | 2.7.1 | none |
| GravityMark-stock | 2000940101 | Wrapper / System | unchanged | auto |
| GravityMark-radv | 2000940102 | Wrapper / xd-fc066b9 | unchanged | auto |

Heaven-radv stays at 1280x720, Low/no MSAA, FEXCore 2603 **Intermediate**, Box64 0.3.6 **Compatibility**. Earlier Performance presets did not help. The original driver was restored and read back after the X11 trial. Saved environment values include:

```text
DXVK_CONFIG=dxvk.enableDescriptorBuffer=False
WRAPPER_DISABLE_PLACED=0
WRAPPER_NATIVE_BCN=1
WRAPPER_MAX_IMAGE_COUNT=0
WINEDLLOVERRIDES=msvcr100,msvcp100=b
RADV_XCLIPSE_GAMENATIVE_WSI=0
VK_ICD_FILENAMES=/data/user/0/app.gamenative/files/imagefs/usr/share/vulkan/icd.d/wrapper_icd.aarch64.json
DXVK_LOG_LEVEL=none
```

The last three remain as explicit restored defaults; no variable was deleted. `nonggc` was a temporary Android property, not saved in the container, and is cleared. Exact config/hash: `results/2026-10-13_173942-game-compare/results.json`. The export remains locally and on the phone as `2026-10-13_173942-game-compare-container.json`. Pre-X11 backup: `tools/Heaven-radv-before-x11-20261013.json`, with its phone copy under `/sdcard/Download/xclipse/`.

Stock still fails a 2048x2048 D24 depth-image creation in the updated app. GravityMark needs a smaller workload to avoid low-memory kills. Neither was retested today. October 11 stock/RADV numbers used the old app and are not a current comparison.

## Completed native work

`main` was already at `17785a9` when pulled. Rebuilt that committed normal driver and its separate X11 variant. Later source changes are harness/reporting changes, not driver changes.

| Experiment | Evidence under results/ | Result and limitation |
|---|---|---|
| Normal `17785a9` correctness | `2026-10-13_165717` | All 22 stock/RADV attempts pass: render, descriptors, culling, queries, XFB variants, tessellation, scratch and draws; zero GPU events. |
| Threshold 128 + no-flush | `2026-10-13_165855`, `2026-10-13_170025-compare` | Initial 11 RADV tests and 36 paired attempts pass. Against threshold 128 with flush retained, tiny-draw GPU time 0.372 to 0.121 us (3.07x throughput); CPU 0.091 to 0.078 us. No consistent graphics-scratch gain. Default-off; no game/general synchronization-safety claim. |
| X11 ELF offscreen | `2026-10-13_170238-compare` | 24 X11 + 23 normal compute/render/descriptor/culling passes, no events. Cooldown stopped the last normal attempt. Native loading proven, presentation not proven. |
| New `placed` test | `2026-10-13_180339` | RADV passes three map/copy/fill/unmap-reserve/remap cycles below 4 GiB, 65536 words each, at `0x10001000`, with guards/persistence checked. Stock skips unavailable extensions. Descriptors/culling pass on both. X11 has not run this new test. |

The placed-map probe reserves its own host range using non-fixed mmap hints; Vulkan replaces only that reservation. It does not reproduce Wine's allocator, wrapper imports or X11 presentation. Source/binary hashes are recorded in the final native result.

Manifests: `experiments/2026-10-13/culling-noflush.json`, `x11-native.json`. `RADV_XCLIPSE_CULL_NOFLUSH` / `debug.radv_cull_noflush` defaults false. `RADV_XCLIPSE_CULL_MIN` / `debug.radv_cull_min` defaults 0. The no-flush test used 1 and 128 respectively. Keep the VGT flush workaround for culling draws: the chip has previously hung without it. No game test of this new no-flush option was run.

Earlier retained findings: logging guards (`01f09ed`) reduced CPU draw recording by 14.2%; threshold alone did not improve tiny draws; compiling culling out with `nonggc` helped native draw/scratch timings. Scratch initialization off did not help. The old 10.2 versus 3.3 ms scratch result was cold host submission time, not a steady GPU comparison. See `PROGRESS.md` for complete evidence.

## Heaven: incomplete study, unresolved rendering and measurement issues

The launch-temperature bug is fixed: cooldown/readback occurs after preparation, immediately before launch; invalid/over-limit readings are rejected. `results/2026-10-13_173017-game-compare` verifies it on the phone: two warm-ups passed, then the first scored launch was refused at 46 C against a 45 C limit. No scored pair exists there.

Fresh study `results/2026-10-13_173942-game-compare` declared **50 C** and six 90-second pairs. It completed two warm-ups, three pairs and one rejected candidate. All starts met the limit; fault/reset counts stayed zero. It stopped automatically on a **89.745288593 s** captured window, below the existing **89.75 s** requirement. Do not relax that threshold after seeing scores.

Three limitations prevent promoting a repeatable improvement:

1. Fixed mode changed operating points. Pair 1: baseline GPU/X4 **400/1632 MHz**, candidate **252/1056 MHz**; excluded by the post-run report filter. Pair 2 matched at 252/1056 (24.81 / 33.03 FPS, +33.13%); pair 3 matched at 400/1632 (38.35 / 49.55 FPS, +29.21%). These are two different operating points, not six repetitions at one state. Other CPU clusters still vary.
2. All **18 screenshots were reviewed**. Unexplained blue streaks/lines appear in baseline pair 1 at 65 s and pair 3 at 35/65 s. A fine dotted blue line also appears in candidate pair 3 at 65 s. Cause unproven; `nonggc` has not established a quality fix. Original captures/contact sheets stay locally; hashes/findings are tracked.
3. Both pair 2 captures at 35 s, and pair 4's candidate at 35 s, still show loading. First SurfaceView activity plus ten seconds can precede true benchmark rendering. Establish a repeatable benchmark-start boundary before another long study. Clock telemetry is launch-relative, not precisely aligned with the frame window either.

The report now includes 1% lows, RSS/swap peaks, GPU accounting, available memory, pressure and battery temperature. Scored baseline RSS peaks were 948-958 MiB, candidate peaks 942-956 MiB: no established RAM gain. Shared pages can be counted repeatedly, and imported DMA-BUF memory is incompletely accounted. USB battery counters/HUD power readings do not establish system energy per frame. No efficiency gain is proven.

No normal-clock confirmation was run. Do not merge these studies with the provisional October 12 comparison. The new report filters fixed-mode pairs with absent/changing/different GPU/X4 clocks, preserving raw acceptance separately. All **13 offline regression tests**, Python compilation, relevant CLI checks and diff checks passed before stopping.

## Direct RADV/X11: loads and creates a swapchain, then exits

The separate Android+X11 build uses pinned Bionic/Termux dependencies and restored MIT Mesa helpers. `third_party/mesa-upstream` is pinned at **1b75efe0014c0d71163a01589bafb510460e7049**. Normal builds stay Android-only. License/dependency details: `driver/android/NOTICE.txt`, `driver/android/x11-deps.json`, `PROGRESS.md`. No reference-wrapper implementation was copied.

Package `driver/dist/radv-xclipse-26.3.0-devel-xd-17785a9-x11.zip` was imported with checksum verification and selection readback. It stays installed but **not selected**. `deploy_x11.py` verified/deployed 11 pinned runtime libraries to `/data/local/tmp/xd/x11-9c54a02cb96027f6/`; native X11 tests need that `LD_LIBRARY_PATH`.

Tracked ICD `experiments/2026-10-13/radv-x11-17785a9.json` and phone copy `/sdcard/Download/xclipse/radv-x11-17785a9.json` point to the imported app-private driver. The container's `VK_ICD_FILENAMES` override reaches its final guest environment. Set `RADV_XCLIPSE_GAMENATIVE_WSI=1` as an environment variable, **not a guessed generic Android property**: the generic property-name buffer is too short. This default-off WSI experiment uses linear PRIME blits, a GPU fence wait and PresentIdle recycling instead of unsupported shared X fences.

Three October 13 probes (`171447-game`, `171735-game`, `172115-game`) exited without a displayed frame, faults or resets. The first coincided with a call; repeating with DND `none` behaved the same. The final info-log probe proves direct RADV creation and a **1280x720 BGRA8, five-image swapchain**. GameNative logs Present opcode 153 `BadImplementation`; its pinned server lacks QueryCapabilities, but Mesa tolerates an absent reply, so that is not a demonstrated cause. Later `processes_exited` triggers guest-process termination (137), not proof of OOM or a GPU hang.

Do not claim direct presentation works. Investigate acquire/submit/present or use a minimal X11 presentation probe. Keep descriptor buffers disabled until this renders correctly. The normal-driver placed-map pass does not resolve the exit.

## Remaining integration boundaries

DXVK 2.7.1 descriptor buffers fault in the current wrapper path; retain `dxvk.enableDescriptorBuffer=False`. Native tests pass but do not cover every DXVK descriptor type or prove wrapper responsibility. RADV descriptor buffers need the 32-bit GPU address window; imported DMA-BUF memory types/mapping remain a suspected mismatch. The wrapper source snapshot does not establish the official APK binary's exact source. `WRAPPER_RESOURCE_TYPE=opaque` remains unverified on that binary.

Scratch/query/GDS/transform-feedback fixes remain intact; Wave32 variants remain untested. GameNative wrapper files and ExynosTools lack a stated reusable license: reference only, no copying. No upstream comments/messages have been sent; sending requires explicit user instruction.

## Exact next work after authorization to resume

1. Pull `main`, inspect status and latest progress. Preserve artifacts/exports and keep USB stay-awake 2. One phone owner at a time.
2. Verify/fix the true game-start and frame-window boundary before another six-pair study. Do not loosen acceptance or select favorable clocks. Resolve blue lines with matched scene/reference captures; keep `nonggc` and no-flush experimental.
3. Run the new `placed` probe on the X11 ELF, then trace acquire/submit/present to locate the first-frame exit. Reuse the ICD/library closure and reversible UI env changes. Restore wrapper defaults after a failed diagnostic.
4. Compare FPS/1% lows, memory pressure and quality for a rendering-correct candidate at declared settings, then confirm performance at normal clocks. Measure actual power separately before efficiency claims.
5. Isolate descriptor buffers after direct presentation works. Fix stock D24 before current stock/RADV comparisons; reduce GravityMark's workload before measuring it. Package only validated improvements.

## Artifacts and reproduction

Portable tools live under ignored `tools/`; no WSL/system installs. On a new computer use `core.autocrlf=false`, `core.longpaths=true`, the author in `AGENTS.md`, and `tools/setup.ps1` (Windows) or `sh tools/setup.sh` (Mac/Linux). Restore exact references with `pipeline/sync_sources.py`, never remote tips. Keep the kernel sparse for Windows filenames.

PowerShell examples **for a future authorized session**:

```powershell
.\tools\python\python.exe pipeline/sync_sources.py
cmd /d /c "call tools\env.cmd && driver\build.cmd"
.\tools\python\python.exe pipeline/run.py --tests placed,descriptors,culling,render,queries,xfb,scratch,scratchcs
.\tools\python\python.exe pipeline/build_x11.py
.\tools\python\python.exe pipeline/deploy_x11.py driver/build-android-x11/vulkan.radeon.so --check-only
```

On Mac/Linux use `tools/python/bin/python3` and `. tools/env.sh && driver/build.sh`. X11 scripts have only run on Windows. Use `run.build_driver()` for a commit-tagged normal package; direct `build.cmd` may not supply the tag. Existing manifests pin specific old binaries: make a new manifest for a new ELF, not relabeled evidence.

| Local reference ELF under tools/ | SHA256 |
|---|---|
| driver-fc066b9-reference.so | 62f186db960ad64c161118687513a73a560b5a2ac15f3628ac4700a52f5534aa |
| driver-01f09ed.so | 9d00f8100328c33726fa0a4b95e1be2856dbf91d9d7974cfffff09f11a082d9d |
| driver-f3ae7c4.so | 6c7bfe873607628eeafc325f469bfa2ad911d6fa7270ce250a9aaef7d4dd7824 |
| driver-17785a9.so | 8a18f2883927cb3087a520a364c69544303ca6020902b10c7f64e8a918c7a38d |
| driver-17785a9-x11.so | 9c54a02cb96027f672923968e8bc75e5f9defd7a73047b2caa912f3dc2caac51 |

Normal/X11 build directories contain the last two ELFs. X11 ZIP SHA256: `cf943bd574ae1e32207ba3fb062891ae9cc23cbac0865038e90219166da2d485`. Keep all ZIPs, ICDs, backups/installers on the phone. Only each run's `results.json` and `summary.md` are tracked; captures, logs, exports and binaries stay on this PC/phone. Raw logcats can include unrelated private app data: filter by relevant app PIDs/tags; never commit or dump them wholesale.

## Automation constraints

- Repeated UI work uses `gn_driver.py` / `phone_ui.py`. New `envs GAME NAME=VALUE ...` batches updates, saves once and reopens to verify every value. No other phone command during UI automation or a benchmark.
- Never tap Reset container, Uninstall, Import config, trash icons or debug Clear buttons; never enter credentials. Skip Steam login. Preserve every user artifact.
- `game.py` stops on GPU events, crashes, exits and stalls. `game_compare.py` requires complete windows, screenshots, telemetry and no sample gaps. Raw `accepted` is measurement acceptance, not visual correctness; fixed-mode scoring also filters clock profiles.
- Forced termination needs manual property/refresh/DND/fixed-mode cleanup; preserve USB stay-awake 2. No keys as keep-alive.
- Logging level 2 is unsuitable for benchmarks. Level 1 is for short diagnostics; restore 0. Explicit Xclipse property mappings differ from generic Mesa mappings; do not guess long names.
- Edit `driver/`/project code, not pinned reference trees. Preserve licenses/clean-room boundaries. Commit and push meaningful steps; update this handoff only when the user requests leaving/stopping.
