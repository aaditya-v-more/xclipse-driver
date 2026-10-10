# Build tools

These are portable tools: no installers, no admin rights, no system PATH changes. Only this README and `env.cmd` are in git. To use them, run `call tools\env.cmd` in a cmd window.

| Folder | What | Source | Verified |
|---|---|---|---|
| `android-ndk-r29` | Android NDK r29 (29.0.14206865) | `https://dl.google.com/android/repository/android-ndk-r29-windows.zip` (795 MB) | SHA-1 `ab3bb30fbb9e6903666d60c55d11e78b04e07472`, matching Google's `repository2-3.xml` |
| `python` | Python 3.13.16, the official NuGet package's `tools/` folder | `https://api.nuget.org/v3-flatcontainer/python/3.13.16/python.3.13.16.nupkg` (14 MB) | `python.exe` Authenticode: Python Software Foundation |
| `python\Scripts` | meson 1.12.1, ninja 1.13.2, mako 1.4.3, packaging 26.3 | PyPI, installed with `python -m pip install meson ninja mako packaging` | — |
| `glslang` | glslang 16.6.0; `bin\glslangValidator.exe` is a copy of `glslang.exe` | `https://github.com/KhronosGroup/glslang/releases/download/16.6.0/glslang-16.6.0-windows-x86_64-release.zip` (13 MB) | SHA-256 `82bf434e69b9bb4829de7e2b4bc2c5e7a7861e53d66cf75e5cc70f5f694a8d9b`, matching GitHub's asset digest |

The NDK is covered by the Android SDK License Agreement, which the user accepted on 2026-10-10.

To rebuild this folder, download the three archives and unpack them:

- `android-ndk-r29-windows.zip` → `tools\android-ndk-r29`
- the nupkg's `tools\` folder → `tools\python`
- the glslang zip → `tools\glslang`

Then run the pip command above and copy `glslang.exe` to `glslangValidator.exe`.
