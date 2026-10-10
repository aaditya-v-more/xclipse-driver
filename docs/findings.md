# Findings

Verified facts only. Raw survey output is in `recon/device-gpu-recon.txt` and `recon/device-gpu-recon2.txt`.

## Phone (surveyed 2026-10-10 over USB ADB, read-only)

| Item | Value |
|---|---|
| Model | SM-S721B (Galaxy S24 FE), codename `r12s`, product `r12sxins` |
| Build | `S721BXXSEDZI1` (BP4A.251205.006), Android 16 (SDK 36), One UI 8.5 (`80500`), patch 2026-09-05 |
| SoC | `s5e9945` (Exynos 2400e), board `erd9945` |
| Kernel | `6.1.157-android14-11` (GKI), SELinux enforcing |
| Lock state | Bootloader locked (`ro.boot.flash.locked=1`), Knox warranty bit 0, verified boot green |
| GPU | Samsung Xclipse 940; DVFS table 252–1095 MHz (max clock reported as 800 MHz during the survey) |

### Kernel driver

- The GPU driver is `sgpu`: platform device `22200000.sgpu`, compatible `samsung-sgpu`. Its DRM nodes are `/dev/dri/card0` and `/dev/dri/renderD128` (`crw-rw-rw-`, SELinux label `gpu_device`).
- Display uses a separate driver, `exynos-drm` (`card1`, `renderD129`).
- sgpu's module parameters are amdgpu's: `aspm`, `async_gfx_ring`, `cwsr_enable`, `gpu_recovery`, `ip_block_mask`, `mcbp`, `vm_*`, `sched_*`, `tmz`, and more. Their values aren't readable from the shell.
- There is no `ip_discovery` sysfs directory. sysfs exposes `devfreq`, `fw_version`, and an empty `product_name`.

### Vendor userspace

- **Vulkan:** `/vendor/lib64/hw/vulkan.samsung.so` (44 MB) is built on AMD PAL, the core of AMDVLK. It contains AMD's proprietary shader compiler path (AMD IL, `ILInstIterator.cpp`, `SCOption_*`) and LLVM AMDGPU symbols (`_amdgpu_ps_main`, etc.). It reports Vulkan 1.3.279, driver 24.1.307.
- **OpenGL ES:** ANGLE on top of that Vulkan driver.
- **OpenCL:** `libSGPUOpenCL.so` (70 MB).
- **`libdrm_sgpu.so`** is a fork of libdrm_amdgpu with 111 exports:
  - 80 standard `amdgpu_*` functions: device init, BO alloc/map/VA, CS context and `submit_raw2`, syncobjs including timelines, `query_info`/`hw_ip_info`/firmware/heap/sensor/video-caps queries, VA ranges.
  - 31 Samsung `sgpu_*` variants, including `sgpu_bo_alloc`, `sgpu_cs_submit_raw`, `sgpu_va_range_alloc`, `sgpu_instance_data_create`, `sgpu_mem_profile_update`, `sgpu_query_gpu_page_faults`, and `sgpu_query_dump_bpmd`.
  - Three `amdgpu_wgp_gating_*` additions.
  - It is labeled `same_process_hal_file`.

### GPU architecture hints

- Both vendor libraries map the chip as `VANGOGHLITE:gfx1040` (also `VANGOGHLITE_EVT1:gfx1040`). The string `M3_BETA1` sits nearby.
- Only GFX10-family targets appear (gfx1010–gfx1036 and gfx1040). There are no gfx11xx targets.
- Compiler options include `SCOption_GFX104_USE_NORMALIZED_COORD_INPUT` and `SCOption_GFX104_FORCE_PACKED_MATH`.
- Ray-tracing performance counters exist (`TA_PERF_SEL_NUM_OF_BVH_VALID_FIRST_TRI`).
- **Working hypothesis:** the shader ISA is a GFX10.x (RDNA 2-like) variant, "gfx1040", not RDNA 3's GFX11. Confirm with the sgpu kernel source or an `AMDGPU_INFO` query.

### SELinux

- `/dev/dri(/.*)?` is labeled `gpu_device`.
- The generic rule grants `(and appdomain (not isolated_app_all))` full `chr_file` permissions, including `ioctl`.
- No `allowx` (ioctl allowlist) rule mentions `gpu_device` in the plat, vendor, system_ext, or product policy.
- The adb shell can open `renderD128`.
- **Conclusion:** a userspace driver loaded into a normal app (Winlator, an emulator) can issue sgpu ioctls without root.

### Tools that didn't work

- `cmd gpu vkjson` fails with "Failed transaction", probably the binder size limit.
- `cmd gpu vkprofiles` exists but hasn't been tried yet.

## Build PC

- i5-13600 (14 cores / 20 threads), 15.7 GB RAM, about 214 GB free on C:. Hypervisor present.
- At survey time: no WSL, clang, cmake, ninja, meson, or Android NDK. Git 2.55 is installed; Python is only the Store alias.
- Internet goes through the phone's mobile hotspot.

## Implications

- **Base:** Mesa RADV with the ACO compiler. Its RDNA 2 (GFX10.3) support is the closest match. It needs a new kernel backend (an "sgpu winsys"), like Turnip's KGSL backend. The remaining unknown is where gfx1040 differs from gfx1030–1036.
- **Delivery:** like Turnip in Winlator. The details are pending research (`docs/research.md`).
