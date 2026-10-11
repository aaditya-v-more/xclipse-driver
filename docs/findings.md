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

### Queries and streamout (probed with `xdtest`, 2026-10-11)

- **sgpu has no GDS.** RADV's 256-byte GDS allocation (domain 8) fails, and the submission with it.
- **The GE counts NGG geometry-shader primitives in the pipeline statistics**, as GFX11 does and GFX10.3 doesn't. With hardware counters only, all 11 statistics match Samsung's driver.
- **`EVENT_WRITE` `SAMPLE_STREAMOUTSTATS` hangs the GPU.** The kernel recovers without a reboot. Writing `VGT_STRMOUT_CONFIG` doesn't hang, but that register doesn't exist on this chip (below).
- **The legacy (non-NGG) geometry path hangs on the first draw batch** (`RADV_DEBUG=nongg`, 2026-10-10).
- **Transform feedback works on Samsung's driver and hangs ours** (`xdtest xfb`, result `2026-10-11_091548`). Stock captured all 15 vertices in order, wrote nothing past the end, stored a 240-byte counter, and its query gave 5 primitives written and 5 needed. RADV on GFX10.3 compiles transform-feedback pipelines for the legacy geometry path, and the draw hung.
- **The 940's streamout hardware is GFX11's** (GPL kernel, `gc_10_4_0_offset_m2.h`, the register map the kernel picks for MGFX2 chips like ours):
  - The legacy streamout registers are gone: `VGT_STRMOUT_CONFIG`, `VGT_STRMOUT_BUFFER_*`, `VGT_STRMOUT_VTX_STRIDE_*` and `CP_STRMOUT_CNTL`.
  - In their place, `SPI_STRMOUT_DWORDS_WRITTEN_0-3`, `SPI_GS_0-3` and `SPI_STRMOUT_PRIMS_NEEDED/WRITTEN_0-3` sit at 0x31088-0x310e4. Those are GFX11's `GDS_STRMOUT_*` and `GDS_GS_*` addresses under new names. `VGT_STRMOUT_DRAW_OPAQUE_*` stays.
  - **No GDS, GWS or OA at all.** The MGFX2 map has none of their registers, and `gfx_v10_0_set_gds_init` leaves all three heaps at size 0 on this chip. GFX11's NGG streamout orders workgroups with `ds_ordered_count`, which needs an OA allocation, so that path can't run unchanged either.

## Build PC

- i5-13600 (14 cores / 20 threads), 15.7 GB RAM, about 214 GB free on C:. Hypervisor present.
- At survey time: no WSL, clang, cmake, ninja, meson, or Android NDK. Git 2.55 is installed; Python is only the Store alias.
- Internet goes through the phone's mobile hotspot.

## Implications

- **Base:** Mesa RADV with the ACO compiler. Its RDNA 2 (GFX10.3) support is the closest match. It needs a new kernel backend (an "sgpu winsys"), like Turnip's KGSL backend. The remaining unknown is where gfx1040 differs from gfx1030–1036.
- **Delivery:** like Turnip in Winlator. The details are pending research (`docs/research.md`).
