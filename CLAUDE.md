# xclipse-driver

Goal: an open-source Vulkan driver for the Samsung Xclipse 940 GPU (Galaxy S24 FE, Exynos 2400e), doing for Xclipse what Mesa's Turnip does for Adreno, so Windows games run measurably faster in Winlator-style emulators. Approach: Mesa RADV on Samsung's amdgpu-derived `sgpu` kernel driver, building on the existing radv-xclipse port.

Start here: read `docs/PROGRESS.md` (status and next steps), then `docs/findings.md` (verified device facts) and `docs/research.md` (existing efforts, Winlator delivery, benchmark plan).

## Repo and workflow

- Private GitHub repo: `aaditya-v-more/xclipse-driver`, default branch `main`.
- **Standing instruction from the user (2026-10-10): commit and push as you work.** Push after each meaningful step instead of batching.
- Commits are authored as Aaditya More with GitHub's no-reply address; this is set in the repo-local git config. End each commit message with the Co-Authored-By line.

## Layout

- `driver/` is the RADV port we build and change. It's a git subtree of radv-xclipse at branch `xclipse940` (JimVulkan's port plus yoro1836's 940 bring-up, PR #6). To pull upstream updates: `git subtree pull --prefix=driver <url> <branch>`.
- `docs/` holds the progress log, findings, and research notes.
- `recon/` holds the read-only device survey scripts and their raw output.
- `third_party/` holds reference clones, kept out of git. `third_party/MANIFEST.md` lists URLs and commits and how to re-clone.
- `tools/` holds the portable toolchain, kept out of git: NDK r29, Python 3.13 with meson/ninja/mako/packaging, and glslang. See `tools/README.md`.

## Build

From the repo root, in cmd:

```bat
call tools\env.cmd && driver\build.cmd
```

The output is `driver\dist\*.zip`: `meta.json` plus `vulkan.radeon.so`, for emulators that load custom Vulkan drivers.

## Device access

- The phone is connected over USB ADB (serial `RZCY90ZYQXZ`). adb is `C:\Users\Administrator\platform-tools\adb.exe` and is on the user PATH; shells opened before 2026-10-10 need the full path.
- Keep on-device actions read-only unless the user agrees otherwise. Put test binaries in `/data/local/tmp` and remove them afterwards.
- Don't make Windows system changes, such as installing WSL or enabling Windows features. Give the user the command to run instead.

## Conventions

- Clone with `git clone -c core.autocrlf=false -c core.longpaths=true`. Use shallow or sparse clones for big repos, because internet often comes through the phone's mobile hotspot.
- Scripts that run on the phone must have LF line endings.
- Stay clean-room. Learn the hardware from the GPL kernel source, MIT ports, and our own probing, never from decompiled Samsung code.
- Update `docs/PROGRESS.md` at each milestone so work can resume after the session closes.
