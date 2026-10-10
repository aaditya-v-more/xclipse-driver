# xclipse-driver

Open-source Vulkan driver work for the Samsung Xclipse 940 GPU (Galaxy S24 FE, Exynos 2400e): Mesa RADV running on Samsung's `sgpu` kernel driver. The aim is faster Windows games in Winlator-style Android apps, doing for Xclipse what Turnip does for Adreno.

## Status (2026-10-10)

- The driver builds on Windows and runs on the S24 FE. The instance, device, submit, compute, and offscreen render tests all pass.
- It exposes Vulkan 1.4 with 224 extensions, against Samsung's driver at 1.3 with 154. That includes BC texture compression, maintenance5/6, and mutable descriptors, which DXVK 3.x and vkd3d-proton use.
- Shader pipelines compile much faster than with Samsung's driver: 2.7 ms against 54.6 ms for the compute test.
- Next: game-level benchmarks in GameNative/Winlator. `docs/PROGRESS.md` has the plan.

## Quick start

Build the driver in a cmd window:

```bat
call tools\env.cmd && driver\build.cmd
```

The output is `driver\dist\radv-xclipse-*.zip`. Install it through an emulator's custom GPU driver setting.

Run the phone tests (USB debugging on, this PC authorized):

```bat
tools\python\python.exe pipeline\run.py
```

Each test runs on both Samsung's driver and ours. The report goes to `results\<time>\summary.md`. Options:

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
