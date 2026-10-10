# Reference repos

These are separate git repos, kept out of this project's history. The driver itself is imported into `driver/` with `git subtree`, so it is in this repo. They were cloned on 2026-10-10 at these commits:

| Folder | Source | Branch | Commit |
|---|---|---|---|
| `Xclipse940-RADV` | https://github.com/Matheus-TestUser1/Xclipse940-RADV | `main` | `06546b3` (2026-10-07) |
| `radv-xclipse` | https://github.com/JimVulkan/radv-xclipse | `main` | `313870e` (2026-10-05) |
| `radv-xclipse` (remote `yoro1836`) | https://github.com/yoro1836/radv-xclipse | `xclipse940` | `d3f2c12` (2026-10-09) |
| `mesa-unified-drivers` | https://github.com/JimVulkan/mesa-unified-drivers | `main` | `a5010bb` (2026-09-30) |
| `ExynosTools` | https://github.com/WearyConcern1165/ExynosTools | `main` | `3dcdbcb` (2026-05-21) |
| `kernel-r12s` | https://github.com/Elchanz3/android_kernel_samsung_r12s | `OS-16` | `9526364` (2026-07-02) |

To re-create them, run this from `third_party/` in Git Bash:

```bash
export GIT_TERMINAL_PROMPT=0
C="git clone -c core.autocrlf=false -c core.longpaths=true"
$C https://github.com/Matheus-TestUser1/Xclipse940-RADV.git
$C https://github.com/JimVulkan/radv-xclipse.git
git -C radv-xclipse remote add yoro1836 https://github.com/yoro1836/radv-xclipse.git
git -C radv-xclipse fetch yoro1836 xclipse940 && git -C radv-xclipse branch xclipse940 yoro1836/xclipse940
$C https://github.com/JimVulkan/mesa-unified-drivers.git
$C https://github.com/WearyConcern1165/ExynosTools.git
# Kernel: only the GPU driver, DRM UAPI headers and defconfigs. The sparse index keeps Windows
# from choking on file names it reserves (aux.c etc.) elsewhere in the tree.
$C --depth 1 --filter=blob:none --sparse --no-checkout --branch OS-16 https://github.com/Elchanz3/android_kernel_samsung_r12s.git kernel-r12s
git -C kernel-r12s sparse-checkout set --cone --sparse-index drivers/gpu/drm/samsung include/uapi/drm arch/arm64/configs
git -C kernel-r12s checkout
```

Licensing:

- The RADV ports and `mesa-unified-drivers` are MIT, plus Mesa's own licenses.
- The kernel is GPL-2.0.
- ExynosTools states no license, so treat it as reference only and don't copy code from it.
- We deliberately don't clone `avavo/XclipseDecomp` or `avavo/MdiEx`. `docs/research.md` explains why.
