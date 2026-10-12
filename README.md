# xclipse-driver

Open-source Vulkan driver work for the Samsung Xclipse 940 GPU (Galaxy S24 FE, Exynos 2400e): Mesa RADV running on Samsung's `sgpu` kernel driver. The aim is faster Windows games in Winlator-style Android apps, doing for Xclipse what Turnip does for Adreno.

## Status (2026-10-12)

- The driver builds on Windows, macOS, and Linux, and runs on the S24 FE. All 22 on-device tests in `pipeline/run.py` pass on both drivers: instance, device, submit, compute, rendering, textures (BC1–7, cube maps, mip chains), MRT, vertex fetch, depth/stencil, storage images, tessellation, scratch memory, queries, transform feedback, and the BCn layer's own decode shader.
- Fixed on the Xclipse 940 so far: pipeline-statistics and primitives-generated queries without GDS (the chip has none), transform feedback through NGG with GFX11's streamout counters, a wave64 register-clobbering bug (every wave64 shader now gets at least 16 VGPRs), and the GPU page faults from GFX11-style shader setup registers (merged-stage pointers, graphics and compute scratch bases).
- It exposes Vulkan 1.4 with 224 extensions, against Samsung's driver at 1.3 with 154. That includes BC texture compression, maintenance5/6, and mutable descriptors, which DXVK 2.x and 3.x and vkd3d-proton use.
- Speed, driver level: FP32 throughput, fill rate and bandwidth equal Samsung's; pipelines compile 2.5–3.6× faster; tiny draws cost 1.6–2× more CPU; graphics scratch is 3× slower.
- Speed, game level: the latest valid Unigine Heaven 4.0 baseline on GameNative 0.9.2 measured 46.03 and 45.17 FPS on RADV with correct textures and zero GPU faults (`results/2026-10-12_120311-game`). On the updated app, stock still fails D24 texture creation; RADV passes diagnostic runs at about 57 FPS with the older DXVK. There is no usable stock baseline for a speed comparison. The 120 Hz measurements show that the earlier 16.7 ms median was not a 60 Hz display cap.
- Known issue outside the driver: GameNative's BCn compute layer (`bcn_layer`) emulates BC textures for every driver except Qualcomm's and Turnip, and its emulated textures come out black on our driver. Set the container's BCn Emulation to `none`; our driver samples BC textures natively.
- The phone now has the official GameNative `v1.3.0-prerelease` APK, verified by SHA256 and pinned to source commit `231d1365cf1a09b7f1763cfaebd3906628ec1381`. This release still displays version `1.2.1` internally. Heaven passed a RADV smoke check with correct textures and zero GPU faults. Nine reference source trees and GameNative's/DXVK's nested dependencies are pinned as submodules.
- The working Heaven configuration remains DXVK `async-1.10.3`. DXVK 2.7.1 imports correctly but caused GPU page faults on RADV; it was restored to the working version. The benchmark runner now stops as soon as it observes a GPU fault/reset counter increase.
- `docs/HANDOFF.md` has the exact next step; `docs/PROGRESS.md` has the history and numbers.

## Quick start

Every step works on Windows, macOS, and Linux. Set up the toolchain once (it goes into `tools/`, see `tools/README.md`):

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File tools\setup.ps1
```

```sh
sh tools/setup.sh
```

The reference sources are pinned submodules. Restore their exact revisions after cloning or pulling:

```powershell
.\tools\python\python.exe pipeline\sync_sources.py
```

On macOS/Linux, use `tools/python/bin/python3 pipeline/sync_sources.py`. This keeps the kernel's
Windows-safe sparse checkout and restores GameNative's and DXVK's nested dependencies. Use `--only gamenative`
for just the app source. The matching official APK URL and SHA256 are recorded in
[`third_party/gamenative-release.json`](third_party/gamenative-release.json); the other pins are in
[`third_party/MANIFEST.md`](third_party/MANIFEST.md).

Build the driver. On Windows, in a cmd window:

```bat
call tools\env.cmd && driver\build.cmd
```

From PowerShell, run the environment setup and build in the same cmd process:

```powershell
cmd /d /c "call tools\env.cmd && driver\build.cmd"
```

On macOS or Linux:

```sh
. tools/env.sh && driver/build.sh
```

The output is `driver/dist/radv-xclipse-*.zip`. Install it through an emulator's custom GPU driver setting.

Run the phone tests (USB debugging on, this computer authorized). The Python is `tools\python\python.exe` on Windows and `tools/python/bin/python3` on macOS and Linux (or `python3` after `. tools/env.sh`):

```sh
tools/python/bin/python3 pipeline/run.py
```

Each test runs on both Samsung's driver and ours. The report goes to `results/<time>/summary.md`. Options:

- `--build-driver` rebuilds the driver first.
- `--tests info,device,submit,compute,render` picks tests.
- `--drivers stock,radv` picks drivers.
- `--keep-going` keeps testing our driver even after it made the phone reboot.

## Working with Claude Code or Codex

Open this repository folder in either tool, or run your installed `claude` or `codex` CLI from the repo root. Both use the same portable toolchain, build scripts, and phone-test pipeline.

- [AGENTS.md](AGENTS.md) is the shared source for project instructions, commands, device rules, and commit conventions. Codex [loads it automatically](https://learn.chatgpt.com/docs/agent-configuration/agents-md).
- [CLAUDE.md](CLAUDE.md) [imports](https://code.claude.com/docs/en/memory#import-additional-files) `AGENTS.md`, so edits to the shared guidance apply to Claude Code too. Start a fresh session after changing the instructions.
- [.claude/settings.json](.claude/settings.json) holds Claude-specific settings. Codex uses its own model, permission, and integration settings; no repo-local Codex configuration is needed for these instructions.
- [docs/PROGRESS.md](docs/PROGRESS.md) and [docs/HANDOFF.md](docs/HANDOFF.md) carry project state between tools and computers. Read the latest progress entries after the handoff, and update the handoff only when the user says they are leaving the device.

Check `git status` when switching tools and coordinate concurrent edits. Only one process or agent may drive or test the phone at a time.

To check the pipeline entry points without a connected phone, run these from PowerShell after toolchain setup:

```powershell
.\tools\python\python.exe pipeline\run.py --help
.\tools\python\python.exe pipeline\game.py --help
```

On macOS or Linux, use `tools/python/bin/python3` in place of `.\tools\python\python.exe`.

## Layout

- `driver/`: the RADV port. It's JimVulkan's radv-xclipse with yoro1836's Xclipse 940 bring-up (MIT, based on Mesa), imported with `git subtree`.
- `tests/`: the on-device test harness (`xdtest.c`) and its shaders.
- `pipeline/`: the build, deploy, and test runner.
- `results/`: one folder per test run.
- `docs/`: progress, device findings, and research.
- `tools/`: the portable toolchain, not in git. See `tools/README.md`.
- `third_party/`: pinned reference submodules and the official GameNative APK lock. See `third_party/MANIFEST.md`.
