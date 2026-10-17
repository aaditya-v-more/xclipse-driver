# xclipse-driver

Shared project instructions for Claude Code and Codex. Keep repository-wide guidance in this file; `CLAUDE.md` imports it for Claude Code.

Goal: an open-source Vulkan driver for the Samsung Xclipse 940 GPU (Galaxy S24 FE, Exynos 2400e), doing for Xclipse what Mesa's Turnip does for Adreno, so Windows games run measurably faster in Winlator-style emulators. Approach: Mesa RADV on Samsung's amdgpu-derived `sgpu` kernel driver, building on the existing radv-xclipse port.

Product direction (user, 2026-10-12): make a usable performance package for this phone, similar to
ExynosTools. Its importable driver or compatibility-layer bundle should deliver repeatable gains
with correct rendering. Research and benchmarks support that deliverable; they are not the end goal.

Optimization scope (user, 2026-10-13): pursue useful gains in RAM/memory use, speed, efficiency,
and visual quality. Average FPS is only one outcome; report frame-time stability, memory pressure,
and rendering defects too. State tradeoffs and distinguish measured energy use from USB charging
or temperature readings.

**Start here:**

1. `docs/HANDOFF.md`: current state, what's on the phone, new-PC setup, and the exact next step.
2. `docs/PROGRESS.md`: history and measured results.
3. `docs/findings.md`: verified device facts.
4. `docs/research.md`: existing efforts, emulator delivery, benchmark plan.
5. `docs/game-setup.md`: the GameNative A/B test.

Check the latest dated entries in `docs/PROGRESS.md` for work since the last handoff. Record durable decisions and measured results in the repo so either agent can resume without the other tool's private session memory.

## Repo and workflow

- Private GitHub repo: `aaditya-v-more/xclipse-driver`, default branch `main`.
- Inspect `git status` before editing or committing, and preserve changes from other sessions. Coordinate edits if both agents are active in the same checkout.
- **Standing instruction from the user (2026-10-10): commit and push as you work.** Push after each meaningful step instead of batching.
- Commits are authored as Aaditya More with GitHub's no-reply address, set in the repo-local git config:
  - `git config user.name "Aaditya More"`
  - `git config user.email "92464728+aaditya-v-more@users.noreply.github.com"`
  - `core.autocrlf=false`
  - `core.longpaths=true`

  Commit messages carry no attribution or Co-Authored-By trailers.

## Layout

- `driver/` is the RADV port we build and change. It's a git subtree of radv-xclipse at branch `xclipse940` (JimVulkan's port plus yoro1836's 940 bring-up, PR #6). To pull upstream updates: `git subtree pull --prefix=driver <url> <branch>`.
- `tests/xdtest.c` is the on-device Vulkan test harness: correctness and speed, stock or RADV. `pipeline/run.py` builds it, deploys it over adb, and writes `results/<time>/`.
- `pipeline/game.py` runs the GameNative benchmark A/B. `pipeline/phone_ui.py` provides adb UI helpers: `dump`, `tap-text`, `type`. `pipeline/gn_driver.py` imports drivers, changes container settings and launches games (`apply GAME --driver/--set/--env/--args [--launch]`, `launch`, `install`, `select`, `set`, `env`, ...); its docstring lists every command. `pipeline/game_input.py` presses gamepad buttons in a running game through GameNative's on-screen controller (`press A RB --times N`) and starts a game past its intro screens (`play LifeIsStrange3`).
- `docs/` holds the handoff, progress, findings, research, and game setup.
- `recon/` holds the read-only device survey scripts and their raw output (summarized in `docs/findings.md`).
- `third_party/` holds pinned source submodules; see `third_party/MANIFEST.md`. Restore them with
  `python pipeline/sync_sources.py` using the portable Python. It preserves the kernel's sparse
  checkout for Windows and initializes GameNative's and DXVK's nested pins. Never follow remote tips when
  reproducing an experiment. `third_party/gamenative-release.json` records the GameNative release
  commit and official APK checksum matching the phone.
- `tools/` holds the portable toolchain, kept out of git. Recreate it with `tools\setup.ps1` (Windows) or `tools/setup.sh` (macOS, Linux), described in `tools/README.md`.

## Build and test

The work moves between a Windows PC and a Mac; everything runs on both. Check the OS and shell before giving commands.

Windows (cmd):

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File tools\setup.ps1
call tools\env.cmd && driver\build.cmd
tools\python\python.exe pipeline\run.py --build-driver
tools\python\python.exe pipeline\game.py --runs 3
```

Windows (PowerShell):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\setup.ps1
cmd /d /c "call tools\env.cmd && driver\build.cmd"
.\tools\python\python.exe pipeline\run.py --build-driver
.\tools\python\python.exe pipeline\game.py --runs 3
```

Keep `env.cmd` and the driver build in the same `cmd` invocation: its environment changes do not carry back to PowerShell.

macOS or Linux:

```sh
sh tools/setup.sh
. tools/env.sh && driver/build.sh
tools/python/bin/python3 pipeline/run.py --build-driver
tools/python/bin/python3 pipeline/game.py --runs 3
```

- `setup.ps1` / `setup.sh` is a one-time toolchain setup per computer. Both produce the same `tools/` layout from pinned, checksummed downloads.
- `.gitattributes` keeps `*.sh` and `*.py` LF and `*.cmd` / `*.ps1` CRLF on every checkout.
- The build produces `driver\dist\*.zip` (`meta.json` plus `vulkan.radeon.so`).
- `run.py --build-driver` runs every on-device test on both drivers.
- `game.py` needs the four GameNative containers configured first; see `docs/HANDOFF.md`.

### Validation

- For documentation or agent-configuration changes, check references, configuration syntax, and `git diff --check`; a phone run is unnecessary.
- For Python changes, use the toolchain's Python to run `-m compileall -q pipeline` and check the affected command's behavior. `pipeline/run.py --help` and `pipeline/game.py --help` work without a phone.
- For driver, shader, or device-runner changes, build and run the relevant on-device tests when the phone is available. State which checks ran and which hardware checks remain unverified.

## Device access

- The phone (serial `RZCY90ZYQXZ`) connects over USB ADB. `pipeline/run.py` finds adb on PATH or in `~/platform-tools` (`%USERPROFILE%\platform-tools` on Windows).
- The user approved automated GPU testing on the phone. A GPU hang can reboot it; the runner detects hangs and reboots and stops testing our driver for that run.
- Put test binaries in `/data/local/tmp`. **Keep the user's files on the phone.** Don't delete benchmark folders, installers, or the driver zip; document them instead (user instruction, 2026-10-10).
- The phone can also connect over wireless debugging (2026-10-17, Mac). This Mac is paired; the phone's `adb pair` code is entered by the user. `pipeline/run.py` targets the transport whose `ro.serialno` is `RZCY90ZYQXZ` when several devices are attached (another phone may be on USB); set `ANDROID_SERIAL` to override. Start the adb server with `ADB_MDNS=0` (adb 37.0.1's mDNS bridge crashes it). On macOS, an adb server started by a previous Claude or terminal session can lose Local Network access: `adb connect` then fails with "No route to host" while `nc -z <ip> <port>` succeeds. Fix it with `adb kill-server; ADB_MDNS=0 adb start-server`, then `adb connect <ip>:<port>`. `dns-sd -B _adb-tls-connect._tcp` and `dns-sd -L <instance> _adb-tls-connect._tcp` find the current port.
- **Keep the phone awake while it's connected** (user instructions, 2026-10-11 and 2026-10-17): `adb shell settings put global stay_on_while_plugged_in 3` (AC or USB power), and leave it set. It only holds the screen on while the phone is charging, which covers a wall charger during wireless debugging. `game.py` and `gn_driver.py` put back whatever value they found. Don't use key presses as a keep-alive; GameNative treats them as input.
- **When driving the phone's UI, nobody else may touch it.**
- Never enter credentials. GameNative's Steam sign-in is skipped with "Skip login".
- **Script repeated phone UI work instead of using subagents, and keep token usage low** (standing instruction from the user, 2026-10-11). This replaces the earlier rule that sent UI navigation to Sonnet subagents.
  - Recurring work goes in scripts under `pipeline/`, built on `pipeline/phone_ui.py` (`dump`, `tap`, `type`, `clear`, `shot`). That covers importing a driver in Driver Manager, selecting it in a container, toggling settings, and reading values off screens. Check for an existing script before driving the UI by hand.
  - Scripts read every change back after saving. They tap by coordinates from a fresh dump or by exact labels, never substrings. They never tap Reset container, Uninstall, Delete variable, Import config, the trash icons in Driver Manager, or the Clear buttons under Settings → Debug.
  - Use a subagent only for one-off UI exploration that a script can't sensibly cover.
  - Only one thing drives the phone at a time. Don't run `game.py` while a script or an agent is driving.
- Don't make system changes, such as installing WSL, enabling Windows features, or changing macOS security settings. Give the user the command to run instead.

## Conventions

- Clone with `git clone -c core.autocrlf=false -c core.longpaths=true`. Use shallow or sparse clones for big repos, because internet may come through the phone's mobile hotspot.
- Scripts that run on the phone must have LF line endings.
- Stay clean-room. Learn the hardware from the GPL kernel source, MIT ports, and our own probing, never from decompiled Samsung code.
- Update `docs/PROGRESS.md` at each milestone, so work can resume after the session closes.
- **Update `docs/HANDOFF.md` only when the user says they're leaving the device** (user instruction, 2026-10-11). Until then, focus on the work, and write the handoff in one pass at the end.
