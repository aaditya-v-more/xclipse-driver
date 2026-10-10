# xclipse-driver

Goal: an open-source Vulkan driver for the Samsung Xclipse 940 GPU (Galaxy S24 FE, Exynos 2400e), doing for Xclipse what Mesa's Turnip does for Adreno, so Windows games run measurably faster in Winlator-style emulators. Approach: Mesa RADV on Samsung's amdgpu-derived `sgpu` kernel driver, building on the existing radv-xclipse port.

**Start here:**

1. `docs/HANDOFF.md`: current state, what's on the phone, new-PC setup, and the exact next step.
2. `docs/PROGRESS.md`: history and measured results.
3. `docs/findings.md`: verified device facts.
4. `docs/research.md`: existing efforts, emulator delivery, benchmark plan.
5. `docs/game-setup.md`: the GameNative A/B test.

## Repo and workflow

- Private GitHub repo: `aaditya-v-more/xclipse-driver`, default branch `main`.
- **Standing instruction from the user (2026-10-10): commit and push as you work.** Push after each meaningful step instead of batching.
- Commits are authored as Aaditya More with GitHub's no-reply address, set in the repo-local git config:
  - `git config user.name "Aaditya More"`
  - `git config user.email "92464728+aaditya-v-more@users.noreply.github.com"`
  - `core.autocrlf=false`
  - `core.longpaths=true`

  End each commit message with the Co-Authored-By line.

## Layout

- `driver/` is the RADV port we build and change. It's a git subtree of radv-xclipse at branch `xclipse940` (JimVulkan's port plus yoro1836's 940 bring-up, PR #6). To pull upstream updates: `git subtree pull --prefix=driver <url> <branch>`.
- `tests/xdtest.c` is the on-device Vulkan test harness: correctness and speed, stock or RADV. `pipeline/run.py` builds it, deploys it over adb, and writes `results/<time>/`.
- `pipeline/game.py` runs the GameNative benchmark A/B. `pipeline/phone_ui.py` provides adb UI helpers: `dump`, `tap-text`, `type`.
- `docs/` holds the handoff, progress, findings, research, and game setup.
- `recon/` holds the read-only device survey scripts and their raw output (summarized in `docs/findings.md`).
- `third_party/` holds reference clones, kept out of git; re-clone them with `third_party/MANIFEST.md`.
- `tools/` holds the portable toolchain, kept out of git. Recreate it with `tools\setup.ps1`, described in `tools/README.md`.

## Build and test

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File tools\setup.ps1
call tools\env.cmd && driver\build.cmd
tools\python\python.exe pipeline\run.py --build-driver
tools\python\python.exe pipeline\game.py --runs 3
```

- `setup.ps1` is a one-time toolchain setup.
- The build produces `driver\dist\*.zip` (`meta.json` plus `vulkan.radeon.so`).
- `run.py --build-driver` runs every on-device test on both drivers.
- `game.py` needs the four GameNative containers configured first; see `docs/HANDOFF.md`.

## Device access

- The phone (serial `RZCY90ZYQXZ`) connects over USB ADB. `pipeline/run.py` finds adb on PATH or at `%USERPROFILE%\platform-tools\adb.exe`.
- The user approved automated GPU testing on the phone. A GPU hang can reboot it; the runner detects hangs and reboots and stops testing our driver for that run.
- Put test binaries in `/data/local/tmp`. **Keep the user's files on the phone.** Don't delete benchmark folders, installers, or the driver zip; document them instead (user instruction, 2026-10-10).
- **When driving the phone's UI, nobody else may touch it.** Keep the screen on with `adb shell svc power stayon usb`, and restore it afterwards with `adb shell settings put global stay_on_while_plugged_in 0` (the original value). Don't use key presses as a keep-alive; GameNative treats them as input.
- Never enter credentials. GameNative's Steam sign-in is skipped with "Skip login".
- Don't make Windows system changes, such as installing WSL or enabling Windows features. Give the user the command to run instead.

## Conventions

- Clone with `git clone -c core.autocrlf=false -c core.longpaths=true`. Use shallow or sparse clones for big repos, because internet may come through the phone's mobile hotspot.
- Scripts that run on the phone must have LF line endings.
- Stay clean-room. Learn the hardware from the GPL kernel source, MIT ports, and our own probing, never from decompiled Samsung code.
- Update `docs/PROGRESS.md` (and `docs/HANDOFF.md` when stopping) at each milestone, so work can resume after the session closes.
