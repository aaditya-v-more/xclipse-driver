# Pinned reference sources

These repositories are tracked as Git submodules. Their Git links lock exact commits; none follow
a branch automatically. The driver/ directory remains our editable RADV subtree.

| Folder | Upstream | Pinned commit | Purpose |
|---|---|---|---|
| gamenative | [GameNative](https://github.com/utkarshdalal/GameNative) | 231d1365cf1a09b7f1763cfaebd3906628ec1381 | Official v1.3.0-prerelease APK source |
| gamenative-mesa | [GameNative/mesa](https://github.com/GameNative/mesa) | 8a01bd85f35a626181d7441a67ae7a1ad129c36b | wrapper-25 Vulkan wrapper and WSI research snapshot |
| dxvk | [DXVK](https://github.com/doitsujin/dxvk) | c3dd74be6baec53786d4e064a572185b70347a17 | DXVK 2.7.1, matching the content package |
| radv-xclipse | [JimVulkan/radv-xclipse](https://github.com/JimVulkan/radv-xclipse) | 313870eb1230c7ae65e25694538a02b282128e20 | Original upstream port |
| radv-xclipse940 | [yoro1836/radv-xclipse](https://github.com/yoro1836/radv-xclipse) | d3f2c124c346a5e2aebfe19e8082412126db039e | Xclipse 940 bring-up used by our subtree |
| Xclipse940-RADV | [Xclipse940-RADV](https://github.com/Matheus-TestUser1/Xclipse940-RADV) | 06546b365454d7b5b4c1986578cfd4262e8b3b8a | Earlier 940 build/reference work |
| mesa-unified-drivers | [mesa-unified-drivers](https://github.com/JimVulkan/mesa-unified-drivers) | a5010bb447ae34e697cb917c2d387272063e9fd8 | Android driver delivery/reference |
| kernel-r12s | [Samsung r12s kernel](https://github.com/Elchanz3/android_kernel_samsung_r12s) | 9526364f2502c6a110905c600c71db4c5ffb28b6 | GPL sgpu implementation and DRM UAPI, OS-16 |
| ExynosTools | [ExynosTools](https://github.com/WearyConcern1165/ExynosTools) | 3dcdbcb2034e5f19a1606e54668e6a87f92476c6 | Device capability/reference research |

gamenative-release.json binds the app source commit to the official APK URL, byte size and SHA256.
The wrapper source is a separate research snapshot; its pin does not assert that the APK's bundled
wrapper binary was built from that revision. The existing research revisions above were retained.

Restore on Windows with the portable Python:

~~~powershell
.\tools\python\python.exe pipeline\sync_sources.py
~~~

On macOS/Linux use tools/python/bin/python3 pipeline/sync_sources.py. The option
--only gamenative dxvk restores selected sources. The script uses shallow, filtered clones, checks
out exact Git-link commits, and refuses to change dirty worktrees. It configures sparse trees before checkout:

- kernel-r12s: Samsung GPU driver, DRM UAPI headers and arm64 defconfigs. The full kernel has
  filenames reserved by Windows, such as aux.c; do not run an unrestricted fresh checkout there.
- gamenative-mesa: src/vulkan/wrapper and src/vulkan/wsi.
- radv-xclipse940: src/amd, src/vulkan, src/util, and include.

GameNative's own submodules are initialized recursively at the commits recorded by its release:
adrenotools 8483dfdaa2abf97ee89ad0e5f337e7b508550c6b (including liblinkernsbypass), LSFG
feb7899a6d0f419e27bf47102b6df8ff17e50122, and their pinned dependencies. DXVK's header and
libdisplay-info submodules are restored at its release pins too. Check top-level pins with
`git submodule status`, then nested pins with
`git submodule status --recursive -- third_party/gamenative third_party/dxvk`.
On Windows, recursing indiscriminately into the kernel can make Git expand its sparse index and
reject reserved filenames even though those paths were not checked out. The restore helper
verifies the kernel's HEAD and clean worktree without that recursion. Do not use
`submodule update --remote` for reproducible tests.

Licensing:

- The RADV ports, DXVK and mesa-unified-drivers use MIT/Mesa licenses; consult each source tree.
- The kernel is GPL-2.0 and GameNative is GPL-3.0; consult their license files.
- ExynosTools states no license, so treat it as reference only and do not copy code from it.
- GameNative's wrapper files carry no license header; read them as reference only.
- We deliberately do not clone avavo/XclipseDecomp or avavo/MdiEx; see docs/research.md.
