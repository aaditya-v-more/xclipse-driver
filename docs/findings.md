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
- **Transform feedback works on Samsung's driver and hangs ours** (`xdtest xfb`, result `2026-10-11_091548`). Stock captured all 15 vertices in order, wrote nothing past the end, stored a 240-byte counter, and its query gave 5 primitives written and 5 needed. RADV on GFX10.3 compiled transform-feedback pipelines for the legacy geometry path, and the draw hung. Ours now streams out from NGG shaders, as on GFX11, and passes.
- **The 940's streamout hardware is GFX11's** (GPL kernel, `gc_10_4_0_offset_m2.h`, the register map the kernel picks for MGFX2 chips like ours):
  - The legacy streamout registers are gone: `VGT_STRMOUT_CONFIG`, `VGT_STRMOUT_BUFFER_*`, `VGT_STRMOUT_VTX_STRIDE_*` and `CP_STRMOUT_CNTL`.
  - In their place, `SPI_STRMOUT_DWORDS_WRITTEN_0-3`, `SPI_GS_0-3` and `SPI_STRMOUT_PRIMS_NEEDED/WRITTEN_0-3` sit at 0x31088-0x310e4. Those are GFX11's `GDS_STRMOUT_*` and `GDS_GS_*` addresses under new names. `VGT_STRMOUT_DRAW_OPAQUE_*` stays.
  - **No GDS, GWS or OA at all.** The MGFX2 map has none of their registers, and `gfx_v10_0_set_gds_init` leaves all three heaps at size 0 on this chip.
- **`ds_ordered_count` orders workgroups anyway.** GFX11's NGG streamout takes each workgroup's buffer space under this lock, which on AMD's chips uses an OA allocation. Here, with no OA, it still keeps draw order: in `xdtest xfbmany` and `xfbmanygs` (24576 triangles in two draws) every vertex lands in order, with GFX11's encoding or GFX10's GS-type one. Without the lock, tens of thousands land out of order.

### Register file (probed with `xdtest`, 2026-10-11)

- **A wave64 pixel shader allocated 8 VGPRs corrupts other waves.** That's the fewest ACO gives a wave64 shader (RSRC1 `VGPRS` = 1). While such pixel waves run, `v0` and `v1` in lanes 0–31 of other waves are sometimes overwritten. The victims seen were NGG waves, where those registers hold vertex indices and the packed primitive.
  - More pixel-shader input VGPRs, more damage: with one (`LINE_STIPPLE_TEX`) only `v1` was hit; with two (`PERSP_CENTER`), `v0` and `v1`, and the GPU hung several times.
  - Not seen with a wave32 pixel shader, with 12 or 16 VGPRs allocated, or with 16 VGPRs and 8 input VGPRs.
  - The mechanism isn't known. Our driver gives every wave64 shader at least 16 VGPRs (`radv_postprocess_binary_config` in `radv_shader.c`). That costs no occupancy for shaders that small, since the SIMD's limit of 16 waves comes first.
- **Cross-half lane operations in wave64 compute shaders work.** `xdtest subgroup` (`results/2026-10-17_213155`) checks shuffles to the other half, reversed and XOR-32 shuffles, shuffle-down across lane 31, and subgroup sums and inclusive scans at subgroup size 64: no mismatches on either driver. The `[W64XLANE]` log line's "broken on this chip" is unconfirmed; pixel and geometry stages are untested.

### Shader setup registers (probed with `xdtest`, 2026-10-11)

The 940 runs GFX10.3 command streams, but its shader launch works like GFX11's in these places. Each was found from a test that page-faulted on our driver (`docs/PROGRESS.md`):

- **Merged stages take s[0:1] from `SPI_SHADER_PGM_LO/HI_HS` and `_GS`** (0xB420/0xB424, 0xB220/0xB224), not from `SPI_SHADER_USER_DATA_ADDR_LO/HI_HS` and `_GS` (0xB408, 0xB208). Those are HS (LS+HS) and GS (NGG VS, TES and GS). s[0:1] points to the table of ring descriptors (tessellation factor and off-chip rings) and, in graphics, the scratch ring. The programs' own addresses go in `PGM_LO/HI_LS` and `_ES`, as on GFX10 and GFX11.
- **Graphics scratch takes its base from `SPI_GFX_SCRATCH_BASE_LO/HI`** (context registers 0x286EC/0x286F0, address >> 8 and >> 40), as on GFX11. Unwritten, it's 0.
- **`SPI_TMPRING_SIZE` counts WAVESIZE in 1 KiB units, as on GFX10.** GFX11's 256-byte units fault past the end of the scratch buffer. WAVES per shader engine and in total are the same here.
- **Compute scratch takes its base from `COMPUTE_DISPATCH_SCRATCH_BASE_LO/HI`** (0xB840/0xB844). Unwritten, it held an address outside our process's buffers.
- **The hardware sets each wave's scratch address itself.** A graphics shader's GFX10 setup of `FLAT_SCRATCH` (`s_setreg`, from the address in s[0:1]) makes no difference: tests pass with it and without it (`RADV_XCLIPSE_SCRATCHINIT=0`).
- **One shader engine:** `num_se` = `max_se` = 1, 2 shader arrays, 6 CUs per array. RADV's scratch limit is 384 waves (`max_scratch_waves`).
- **The MGFX2 register map has nothing at 0xB130** (`SPI_SHADER_USER_DATA_VS_0`; there's no hardware VS stage left). RADV still writes the descriptor pointer there, apparently harmlessly.
- **Register remapping** (`ac_titan_remap`): every SH and context register RADV writes for this chip lands where the MGFX2 map has it. There's one gap, in `ac_pm4_set_reg`: it doesn't remap UCONFIG registers. So `GE_USER_VGPR_EN` lands on this chip's `VGT_TF_MEMORY_BASE_HI`. That's harmless today, because the tessellation ring setup writes that register afterwards.

### GPU faults, resets and load (GPL kernel source and sysfs, 2026-10-11)

- **Every GPU page fault ends in a full GPU reset.**
  - The fault interrupt (`gmc_v10_0_process_interrupt`, `gmc_v10_0.c`) records the fault, remembers the faulting VMID, and queues `page_fault_work` 100 ms later.
  - That work, `sgpu_page_fault_recovery` (`amdgpu_device.c`), calls `amdgpu_device_gpu_recover`. It resets the GPU, cancels the faulting VMID's pending jobs (`-ECANCELED`) and resubmits the others.
  - So a game that faults once a frame stalls for a reset every frame, and that frame's work is dropped.
- **`/sys/kernel/gpu/gpu_info` counts them:**
  - `GPF_S`: page faults.
  - `GHG_S`: `gpu_reset_counter`, every reset.
  - `GTO_S`: job timeouts.
  - Throughout 2026-10-11, GHG_S = GPF_S + GTO_S: every reset followed a page fault or a timeout.
- **The kernel keeps the faulting addresses for the process that faulted.**
  - It's an `AMDGPU_INFO` query, `SGPU_INFO_GPU_PAGE_FAULTS` (0x101, `include/uapi/drm/sgpu_drm.h`). It returns `struct drm_sgpu_query_gpu_page_faults { __u32 num_faults; __u64 fault_addr[4]; }`.
  - The records are per DRM file, so only the driver inside the faulting process can read them. Reading consumes them, and the kernel holds at most 4.
  - The addresses are 4 KiB page starts, 48-bit GPU VAs.
  - The kernel records a fault when the GPU hits it, which is after the submission that caused it has returned. So a driver that asks right after each submission sees each fault one submission late, or at teardown.
  - dmesg has the full fault status, but the shell can't read it without root.
- **GPU load:**
  - `/sys/kernel/gpu/gpu_busy` is the busy percentage over the kernel's last 8 ms DVFS window (`sgpu_devfreq_polling_ms`). Busy time is counted while jobs are in flight (`sgpu_utilization.c`).
  - `/sys/kernel/gpu/gpu_clock` is the clock in kHz, 0 while the GPU is idle or power-gated.
  - Both are world-readable.
- **`/sys/kernel/gpu/mem_info` misses imported memory.** Its per-process numbers count the process's GEM buffers, but skip imported and exported dma-bufs (`amdgpu_gem_bo_size`). GameNative's wrapper imports dma-bufs for host-visible memory in 32-bit games, so those don't show up.

### Caches and texture filtering (probed with Heaven and `xdtest`, 2026-10-17)

- **GL1 behaves like GFX10.3's, not GFX11's.** The 940 runs a GFX11 shader core, but a load with GLC alone doesn't bypass GL1. Device-scope (coherent) loads need GLC|DLC, as on GFX10.3. With GLC alone, TES reads of the tess offchip ring returned stale HS output: blue streaks in Heaven (fixed in `288a2c6`, see PROGRESS).
- **The texture unit can't linear-filter 32-bit float or 16-bit UNORM formats.**
  - The affected formats include R32F, RG32F, RGBA32F, R16_UNORM, D16 and D32F/D32FS8 sampled without compare. A linear sampler returns the nearest texel, the one at (x+1, y+1) for a corner sample (`results/2026-10-17_190027`).
  - RGBA8, sRGB, RGBA16F, R16F, R11G11B10F, RGB10A2, R8 and the BC formats filter correctly.
  - Samsung's driver withholds `SAMPLED_IMAGE_FILTER_LINEAR` for exactly the affected formats. RADV advertises it.
  - Depth comparison with a linear filter (PCF) is correct on D16, D32F and D32FS8, so shadow maps are fine.
  - RADV emulates the filter in the shader for those formats (`radv_nir_lower_xclipse_filter.c`, `debug.radv_xclipse_filteremu`), at about 2% in Heaven at fixed clocks and no measurable cost at normal clocks, where Heaven is CPU-bound. A gather4 placed at a texel center can pick a footprint other than `floor(uv*size-0.5)`, but an explicit-LOD sample at an exact texel center returns that texel, so the emulation loops over texel centers (4 or 8 samples).

## Build PC

- i5-13600 (14 cores / 20 threads), 15.7 GB RAM, about 214 GB free on C:. Hypervisor present.
- At survey time: no WSL, clang, cmake, ninja, meson, or Android NDK. Git 2.55 is installed; Python is only the Store alias.
- Internet goes through the phone's mobile hotspot.

## Implications

- **Base:** Mesa RADV with the ACO compiler. Its RDNA 2 (GFX10.3) support is the closest match. It needs a new kernel backend (an "sgpu winsys"), like Turnip's KGSL backend. The remaining unknown is where gfx1040 differs from gfx1030–1036.
- **Delivery:** like Turnip in Winlator. The details are pending research (`docs/research.md`).
