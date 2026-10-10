# Build tools

These are portable tools: no installers, no admin rights, no system PATH changes. Only this README, the `env` scripts, and the `setup` scripts are in git. Both setups produce the same layout, so the rest of the project doesn't care which computer it's on.

| | Windows | macOS, Linux |
|---|---|---|
| Set up once | `powershell -NoProfile -ExecutionPolicy Bypass -File tools\setup.ps1` | `sh tools/setup.sh` |
| Use in a shell | `call tools\env.cmd` | `. tools/env.sh` |
| Python | `tools\python\python.exe` | `tools/python/bin/python3` |
| Build the driver | `driver\build.cmd` | `driver/build.sh` |

Each setup script downloads and verifies everything below, and installs ADB to `~/platform-tools` if adb isn't on PATH. Anything already present is skipped, so it's safe to run again. The toolchain takes about 3 GB unpacked.

## Windows (`setup.ps1`)

| Folder | What | Source | Verified |
|---|---|---|---|
| `android-ndk-r29` | Android NDK r29 (29.0.14206865) | `https://dl.google.com/android/repository/android-ndk-r29-windows.zip` (795 MB) | SHA-1 `ab3bb30fbb9e6903666d60c55d11e78b04e07472`, matching Google's `repository2-3.xml` |
| `python` | Python 3.13.16, the official NuGet package's `tools/` folder | `https://api.nuget.org/v3-flatcontainer/python/3.13.16/python.3.13.16.nupkg` (14 MB) | `python.exe` Authenticode: Python Software Foundation |
| `python\Scripts` | meson 1.12.1, ninja 1.13.2, mako 1.4.3, packaging 26.3, PyYAML 6.0.3 (Mesa needs it, though the port's README doesn't list it) | PyPI, installed with `python -m pip install meson ninja mako packaging pyyaml` | — |
| `glslang` | glslang 16.6.0; `bin\glslangValidator.exe` is a copy of `glslang.exe` | `https://github.com/KhronosGroup/glslang/releases/download/16.6.0/glslang-16.6.0-windows-x86_64-release.zip` (13 MB) | SHA-256 `82bf434e69b9bb4829de7e2b4bc2c5e7a7861e53d66cf75e5cc70f5f694a8d9b`, matching GitHub's asset digest |

The NDK is covered by the Android SDK License Agreement, which the user accepted on 2026-10-10.

## macOS and Linux (`setup.sh`)

The same versions, from each project's own release for the host. Checksums are pinned in the script.

| Folder | What | Source | Verified |
|---|---|---|---|
| `android-ndk-r29` | Android NDK r29 | `https://dl.google.com/android/repository/android-ndk-r29-darwin.zip` (1.0 GB; `-linux.zip` on Linux) | SHA-1 from Google's `repository2-3.xml`: darwin `03d29fbb57e3c05a7d53597dd011d856c1456a4f`, linux `87e2bb7e9be5d6a1c6cdf5ec40dd4e0c6d07c30b` |
| `python` | Python 3.13.16, a relocatable build from python-build-standalone `20261009` | `https://github.com/astral-sh/python-build-standalone/releases` (25 MB on macOS) | SHA-256 per host, matching GitHub's asset digest |
| `python/bin` | meson, ninja, mako, packaging, PyYAML, pymsi | PyPI, the same pip command as on Windows | — |
| `glslang` | glslang 16.6.0; `bin/glslangValidator` is a copy of `glslang` | `glslang-16.6.0-macos-universal-release.zip` (14 MB) or `-linux-x86_64-release.zip` | SHA-256 matching GitHub's asset digest |

The NDK's macOS binaries are universal, so Apple Silicon needs no Rosetta. On macOS, `setup.sh` checks a downloaded adb is signed by Google LLC; Homebrew's `android-platform-tools` works too.

## Rebuilding by hand on Windows

To rebuild this folder, download the three archives and unpack them:

- `android-ndk-r29-windows.zip` → `tools\android-ndk-r29`
- the nupkg's `tools\` folder → `tools\python`
- the glslang zip → `tools\glslang`

Then run the pip command above and copy `glslang.exe` to `glslangValidator.exe`.

`driver\build.cmd` points Meson at the NDK's `llvm-nm.exe`, because libdrm's build needs an `nm` and Windows doesn't have one.
