# xclipse-driver

Open-source Vulkan driver work for the Samsung Xclipse 940 GPU (Galaxy S24 FE, Exynos 2400e): Mesa RADV running on Samsung's `sgpu` kernel driver. The aim is faster Windows games in Winlator-style Android apps, doing for Xclipse what Turnip does for Adreno.

## Status (2026-10-11)

- The driver builds on Windows, macOS, and Linux, and runs on the S24 FE. All 22 on-device tests in `pipeline/run.py` pass on both drivers: instance, device, submit, compute, rendering, textures (BC1–7, cube maps, mip chains), MRT, vertex fetch, depth/stencil, storage images, tessellation, scratch memory, queries, transform feedback, and the BCn layer's own decode shader.
- Fixed on the Xclipse 940 so far: pipeline-statistics and primitives-generated queries without GDS (the chip has none), transform feedback through NGG with GFX11's streamout counters, a wave64 register-clobbering bug (every wave64 shader now gets at least 16 VGPRs), and the GPU page faults from GFX11-style shader setup registers (merged-stage pointers, graphics and compute scratch bases).
- It exposes Vulkan 1.4 with 224 extensions, against Samsung's driver at 1.3 with 154. That includes BC texture compression, maintenance5/6, and mutable descriptors, which DXVK 2.x and 3.x and vkd3d-proton use.
- Speed, driver level: FP32 throughput, fill rate and bandwidth equal Samsung's; pipelines compile 2.5–3.6× faster; tiny draws cost 1.6–2× more CPU; graphics scratch is 3× slower.
- Speed, game level: Unigine Heaven 4.0 (DX11, 1280×720, Low) in GameNative 0.9.2 runs at a median of 37.2 FPS on our driver against 35.6 FPS on Samsung's, with no GPU faults (3 runs each, `results/2026-10-11_165352-game`). The run-to-run spread is large, and the 60 Hz display caps the frame rate, so treat the ratio as "no slower", not as a measured win yet.
- Known issue outside the driver: GameNative's BCn compute layer (`bcn_layer`) emulates BC textures for every driver except Qualcomm's and Turnip, and its emulated textures come out black on our driver. Set the container's BCn Emulation to `none`; our driver samples BC textures natively.
- `docs/HANDOFF.md` has the exact next step; `docs/PROGRESS.md` has the history and numbers.

## Quick start

Every step works on Windows, macOS, and Linux. Set up the toolchain once (it goes into `tools/`, see `tools/README.md`):

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File tools\setup.ps1
```

```sh
sh tools/setup.sh
```

Build the driver. On Windows, in a cmd window:

```bat
call tools\env.cmd && driver\build.cmd
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

## Layout

- `driver/`: the RADV port. It's JimVulkan's radv-xclipse with yoro1836's Xclipse 940 bring-up (MIT, based on Mesa), imported with `git subtree`.
- `tests/`: the on-device test harness (`xdtest.c`) and its shaders.
- `pipeline/`: the build, deploy, and test runner.
- `results/`: one folder per test run.
- `docs/`: progress, device findings, and research.
- `tools/`: the portable toolchain, not in git. See `tools/README.md`.
- `third_party/`: reference repos, not in git. See `third_party/MANIFEST.md`.
