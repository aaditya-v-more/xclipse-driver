# Research notes

Collected 2026-10-10 by web research. Each item notes whether a source confirmed it. Re-check anything marked "inferred" before relying on it.

## Existing open-driver efforts for Xclipse

None of this is in upstream Mesa. All of it is third-party, mostly started in mid-2026, and much of it AI-assisted.

| Repo (github.com/…) | What it is | GPUs | License | Last activity | Cloned to |
|---|---|---|---|---|---|
| Matheus-TestUser1/Xclipse940-RADV | RADV + ACO port aimed at the 940 | 940 | MIT | 2026-10-07 | `third_party/Xclipse940-RADV` |
| JimVulkan/radv-xclipse | The main RADV port | 920 + 530 | MIT | 2026-10-05 | `third_party/radv-xclipse` (`main`) |
| yoro1836/radv-xclipse, branch `xclipse940` (PR #6 to JimVulkan) | Adds the 940 to JimVulkan's tree | 940 | MIT | 2026-10-09 | `third_party/radv-xclipse`, branch `xclipse940` |
| JimVulkan/mesa-unified-drivers | RADV + RadeonSI (OpenGL) + PanVK + Panfrost | 920 + 530 | MIT | 2026-09-30 | `third_party/mesa-unified-drivers` |
| WearyConcern1165/ExynosTools | Vulkan *layer* in front of `vulkan.samsung.so`: BCn texture virtualization for DXVK/vkd3d (this is "Exynos Tools") | 920–960 | none stated | 2026-06-06 | `third_party/ExynosTools` (reference only, see licensing notes) |
| Elchanz3/android_kernel_samsung_r12s (`OS-16`) | Readable S24 FE kernel source, 6.1.157 | 940 | GPL | — | `third_party/kernel-r12s` (sparse: GPU driver, DRM UAPI, defconfigs) |

Status as reported by each project (confirmed from source):

- **JimVulkan v1.5.0:** "Ray tracing works on the Xclipse 920" (3–4 fps). The release notes say "Other Xclipse models are not compatible for now." Minecraft, Eden, and MGSV are reported working. See https://github.com/JimVulkan/radv-xclipse/releases.
- **Xclipse940-RADV:** "Status: active development." Compute (SSBO and dispatch) is verified. Two-vertex `LINE_LIST` draws still hang the GPU intermittently. There is no vulkaninfo, triangle, or game result yet.
- **yoro1836 PR #6** (open, unmerged): on a 940, Eden and Super Mario Maker 2 ran "no GPU reset … in runs of 67-210 s" after CP-DMA prefetches were dropped. It hasn't been tested in Winlator. The maintainer asked for a Winlator test and a build before merging.

### Known but not cloned

- **avavo/WinXclipse** (MIT, about 486 MB): a Winlator CMOD fork tuned for Xclipse. It's a candidate test app; prefer installing its release APK over cloning the source.
- **leegao/bionic-vulkan-wrapper** (about 407 MB): the Vulkan-on-Vulkan wrapper Winlator forks use with the vendor driver. It provides the baseline path.
- **denilksongolvink-debug/Xclipse_940_Zink:** OpenGL via Zink on Samsung's proprietary driver. No license.
- **aztec16/mesa-samsung-xclipse** and **mxxme-dev/radv-xclipse-patches:** older 920 attempts, superseded by the repos above.
- **souza60029-wq/Xclipse-Open-Project:** documentation, archived 2026-10-01.
- **Deliberately avoided:**
  - **avavo/XclipseDecomp:** notes from static analysis of Samsung's proprietary driver.
  - **avavo/MdiEx:** bundles Samsung's proprietary `.so` files.

  Mesa's Code of Conduct committee removed a link to a decompiled-driver repo and asked that reverse engineering be clean-room (Mesa issue #15753). Keep this project clean-room: learn the hardware from the GPL kernel source, MIT ports, and our own probing on the device.

## Samsung's sgpu kernel driver (confirmed from the kernel mirror)

- It's an amdgpu fork that reports `DRIVER_NAME "amdgpu"` and DRM version 3.42.0. Source lives in `drivers/gpu/drm/samsung/gpu/sgpu/` (on the S22 it was `drivers/gpu/drm/samsung/sgpu/`).
- The UAPI header is `include/uapi/drm/sgpu_drm.h`.
  - Ioctl opcodes 0x00–0x15 are byte-for-byte AMDGPU's, with `DRM_IOCTL_AMDGPU_*` defined identically.
  - Samsung adds `SGPU_GEM_CREATE` (0x16), `SGPU_INSTANCE_DATA` (0x5d), `AMDGPU_WGP_GATING` (0x5e), and `SGPU_MEM_PROFILE_ADD` (0x5f).
- It's a loadable module (`CONFIG_DRM_SGPU=m`, license "GPL and additional rights"). The device-tree compatible is `samsung-sgpu`, and the nodes are `/dev/dri/card0` and `renderD128`.
- Chip IDs:
  - The chip is `CHIP_VANGOGH_LITE`.
  - Xclipse 920 is MGFX0, family 144 (VGH).
  - Xclipse 940 is **MGFX2**, family **147** (`AMDGPU_FAMILY_MGFX`), `chip_rev 0x02600200`, device ID **0x73A0**.
  - The register headers are `gc_10_4_0_*`, with `_m1`/`_m2` variants.
- The 940 has 12 CUs (6 WGPs), 2 shader arrays, 4 RBs, and 4 TCCs (920: 6 CUs). The clock tops out around 1095 MHz, which matches our DVFS table.
- Memory is unified and shared, with no dedicated VRAM, so RADV's VRAM assumptions need adjusting. The register layout is Samsung's own `gc_10_4` (`_m2`), so stock RADV register offsets don't all match. The existing ports run the 940 on RADV's GFX10.3 code path.

## Kernel source download

- opensource.samsung.com offers `SM-S721B_EUR_16_Opensource.zip` (388.25 MB, Android 16) along with Android 14 and 15 packages.
- No login is needed, but scripted downloads hit a Cloudflare 403. A real browser works.
- The GitHub mirror above is enough for reading the GPU driver.

## Upstream stance

- Mesa issue #9524 (2023): Samuel Pitoiset said "In theory that should work… ask Samsung." Timur Kristóf said RADV developers don't support Xclipse.
- Mesa issue #15214 (2026): Timur Kristóf said "we are happy to help" with a port, but Mesa won't do it without the manufacturer.
- AMD discontinued AMDVLK on 2025-09-15 in favor of RADV (GPUOpen-Drivers/AMDVLK discussion #416). The community believes Samsung's proprietary Vulkan driver derives from AMDVLK/PAL, which our survey's PAL strings support.

## What the clones contain (read 2026-10-10)

- **`radv-xclipse` (JimVulkan):** Mesa 26.3.0-devel, trimmed to what RADV needs.
  - `build.sh` (Linux) and `build.cmd` (Windows) cross-compile with NDK r29 for API 34 arm64. They pull libdrm 2.4.133 (with an Xclipse patch), zlib, and expat as static Meson subprojects, and package `dist/*.zip` containing `meta.json`, `vulkan.radeon.so`, and `NOTICE.txt`.
  - Runtime switches are environment variables or `debug.*` Android properties:
    - logging (`RADV_XCLIPSE_LOG`)
    - CPU vs GPU bound reporting (`RADV_XCLIPSE_PERF`)
    - BC4–BC7 emulation by GPU decode at upload (`RADV_XCLIPSE_NO_BC_EMU` hides it); BC5 is aliased with an EAC copy
    - DCC on/off
    - memory heaps
    - a per-frame CPU/GPU profiler (`MESA_XCLIPSE_PROF`)
  - The README says it was "developed with heavy use of AI tools" and tested on an S22 Ultra (920) and an A55 (530).
- **`radv-xclipse@xclipse940` (yoro1836, 4 commits):**
  - Recognizes the 940 and its kernel identity (`ac_gpu_info.c`, `amd_family.h`).
  - Uses the 530's ("titan") register map, controlled by `RADV_XCLIPSE_TITAN`.
  - Enables Xclipse context init by default.
  - Turns shader and vertex-descriptor prefetches off by default (`RADV_XCLIPSE_PREFETCH`), because they hang the 940. The vendor driver doesn't issue them either.
- **`Xclipse940-RADV` (Matheus-TestUser1):** a full Mesa 24.3.4 tree plus `build-xclipse940-android.sh`, probe programs (`*_probe*.c`), and patches.
  - It was tested on the same SM-S721B, on older firmware (`S721BXXS3AYB8`, kernel 6.1.75).
  - It uses a GFX10.3 compatibility path. Its notes say generic AMD register layouts and encodings can't be assumed to match MGFX2.
- **`mesa-unified-drivers` (JimVulkan):** the same base plus RadeonSI (OpenGL 4.6 / ES 3.2) for the 920 and 530, and PanVK/Panfrost for Mali. A possible later source for native OpenGL.
- **`ExynosTools`:** a C/CMake Vulkan layer (`VkLayer_vortek_xclipse`) that sits in front of `vulkan.samsung.so`. It virtualizes BCn formats and patches fragile structures. Its driver bundle ships Samsung's own `.so` next to the layer. This is the baseline to beat.

## Emulation stack and benchmarks (research report, 2026-10-10)

### How PC games run on Xclipse today

- **Winlator official (glibc):** non-Adreno GPUs go through **Vortek**, a client/server Vulkan forwarder on top of the system driver. Release notes: 10.0 added `textureCompressionBC` emulation, 10.1 added AdrenoTools to Vortek, and 11.2 (2026-08-19) says "Some improvements were added for the Xclipse GPU". Sources: github.com/brunodev85/winlator/releases and leegao.github.io/winlator-internals.
- **Bionic forks** (Cmod, Ludashi, Bannerlator, GameNative, WinXclipse) use leegao's **bionic-vulkan-wrapper**, described as a "Vk-on-Vk wrapper for Mali, old Adreno drivers, and Xclipse". Bannerlator's guide recommends "Wrapper + bcn_layer" for Xclipse.
- **ExynosTools layer:** GPU BCn decode, v2.1 (2026-06-06).
- **GameHub** can't load custom drivers. **Mobox** was archived on 2025-06-20.
- No head-to-head benchmark exists. The best anecdote is an S25 FE (Exynos 2400) with ExynosTools: Fallout 3 at 120+ fps, Stray at about 20 fps at 640×480, Cyberpunk 2077 at 15–20 fps (Bannerlator issue #134).

### Gaps in Samsung's driver

Data from vulkan.gpuinfo.org, vendor `0x144D`, device `0x2600200`:

- The S24 FE report (id 48812, SM-S721B, Android 16) shows Vulkan 1.3.279, driver 24.0.534. Android 17 reports show 1.3.304 / 25.3.3.
- **Present:** transformFeedback (4 streams), geometryShader, graphics_pipeline_library with fastLinking, robustness2 with nullDescriptor, customBorderColorWithoutFormat, descriptor_buffer, and update-after-bind limits of 4294967295.
- **Missing in every report, Android 14 through 17:** `textureCompressionBC`, `VK_EXT_mutable_descriptor_type`, `VK_KHR_maintenance5` and `6`, `samplerFilterMinmax`, `shaderFloat64`.
- **Consequences:**
  - DXVK 3.x (released 2026-06-25) needs Vulkan 1.4 and maintenance5/6, so the stock driver is stuck on DXVK 2.x.
  - vkd3d-proton calls mutable descriptors "highly recommended".
  - BCn textures have to be emulated by a layer.
- Eden's docs (stock driver): "No Xclipse GPUs except the very latest (e.g. Xclipse 950) are known to work with Eden at all."

### Loading a custom driver without root

- libadrenotools loads a private copy of `/system/lib64/libvulkan.so` in its own linker namespace (linkernsbypass), then swaps any driver whose file name contains "vulkan." for the custom one. That mechanism doesn't care about the vendor. Only BCn patching, KGSL GPU-mapping import, and turbo mode are Adreno-only.
- The package format is a zip with `meta.json` plus the driver `.so`: Turnip uses `vulkan.adreno.so`, radv-xclipse uses `vulkan.radeon.so`.
- Eden warns on non-KGSL GPUs ("Your GPU does not support driver injection…") but loads the driver anyway.
- Termux's Mesa package builds Turnip but not RADV.

### RADV internals relevant to the port

- Kernel calls go through `ac_drm_*` in `src/amd/common/ac_linux_drm.c`, which picks libdrm_amdgpu or virtio based on `dev->is_virtio`. That's the natural place for an sgpu backend. Upstream removed the null winsys in 2025-11.
- Upstream RADV hard-requires several things that radv-xclipse patches around:
  - a PCI bus with the ATI vendor ID (a platform bus is accepted only for virtio)
  - the DRM driver name `amdgpu` (sgpu already reports that)
  - PCI bus info
  - `AC_AMDGPU_DRM_MINOR 59` (sgpu is 3.42)
- radv-xclipse also sets a GTT-only memory heap, the integrated-GPU flag, and the TCC count and cache sizes. It reports Samsung's vendor ID `0x144d` because "With AMD's 0x1002 Windows games take AMD-specific paths that fail under Wine/Winlator".

### SELinux

AOSP gives apps (except isolated ones) read/write access to `gpu_device` with no platform-level ioctl allowlist. Android 16 QPR2's `set_xperm_filter` is used only for Mali on Pixel. No Samsung sgpu filtering turned up, which matches our device survey.

### Benchmark plan

1. **GravityMark** (Windows x64, 211 MB) in Winlator: one run in Vulkan mode (a direct driver test) and one in DX11 mode (through DXVK). It has a command line for repeatable runs. Whether it runs under Winlator is untested.
2. **Unigine Heaven 4.0** (247 MB): DX11, 1280×720, Low preset.
3. One fixed scene in a real game, recorded with `DXVK_HUD=fps,frametimes` (or MangoHud on glibc builds).
4. **3DMark Wild Life Extreme**, native Android on the stock driver, as a control for thermal and firmware drift between sessions.

Method:

- Pin the Wine, Box64, and DXVK versions.
- Use DXVK 2.7.1 for both drivers, because the stock driver can't run DXVK 3.x.
- Do at least 3 runs and report the median and 1% lows.
- Record RADV with DXVK 3.x as an extra data point.
