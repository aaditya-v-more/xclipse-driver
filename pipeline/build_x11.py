#!/usr/bin/env python3
"""Build the experimental Android + X11 RADV library, leaving the normal build intact.

Run prepare_x11.py first. Uses the repository's portable NDK, Python/Meson/Ninja and glslang.
The resulting driver is for GameNative's Bionic runtime, which supplies its X11 libraries.
"""
import os
import subprocess
import sys

from mesonbuild.scripts.depfixer import DT_RPATH, DT_RUNPATH, Elf

import run


def main():
    source = run.ROOT / "driver"
    build = source / "build-android-x11"
    sdk = run.TOOLS / "gamenative-x11"
    if not (sdk / "manifest.json").exists():
        sys.exit("run pipeline/prepare_x11.py first")
    build.mkdir(exist_ok=True)
    suffix = ".cmd" if run.WINDOWS else ""
    cross = f"""[binaries]
c = '{run.NDK_BIN.as_posix()}/aarch64-linux-android34-clang{suffix}'
cpp = '{run.NDK_BIN.as_posix()}/aarch64-linux-android34-clang++{suffix}'
ar = '{run.NDK_BIN.as_posix()}/llvm-ar{run.EXE}'
strip = '{run.NDK_BIN.as_posix()}/llvm-strip{run.EXE}'
nm = '{run.NDK_BIN.as_posix()}/llvm-nm{run.EXE}'
[properties]
cpp_link_args = ['-static-libstdc++']
xclipse_x11_sdk = '{sdk.as_posix()}'
[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
"""
    (build / "cross.ini").write_text(cross, encoding="utf-8", newline="\n")
    env = os.environ.copy()
    bindir = run.TOOLS / "python" / ("Scripts" if run.WINDOWS else "bin")
    env["PATH"] = os.pathsep.join((str(bindir), str(run.GLSLANG.parent), env.get("PATH", "")))
    options = ["-Dbuildtype=debugoptimized", "-Db_ndebug=true", "-Dplatforms=android,x11",
               "-Dplatform-sdk-version=34", "-Dandroid-stub=true", "-Dandroid-strict=false",
               "-Dandroid-libbacktrace=disabled", "-Dvulkan-drivers=amd", "-Dgallium-drivers=",
               "-Dvulkan-layers=", "-Dtools=", "-Dllvm=disabled", "-Dzstd=disabled",
               "-Dlmsensors=disabled", "-Dperfetto=false", "-Dglx=disabled", "-Dgbm=disabled",
               "-Degl=disabled", "-Dgles1=disabled", "-Dgles2=disabled", "-Dxlib-lease=disabled",
               "-Dallow-fallback-for=libdrm,perfetto", "--force-fallback-for=expat,libdrm,zlib",
               "-Dlibdrm:default_library=static", "-Dexpat:default_library=static",
               "-Dzlib:default_library=static", "-Dc_args=-march=armv8.2-a", "-Dcpp_args=-march=armv8.2-a"]
    with (build / "build-log.txt").open("w", encoding="utf-8") as log:
        def call(args):
            subprocess.run(args, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        setup = [sys.executable, "-m", "mesonbuild.mesonmain", "setup", str(build),
                 "--cross-file", str(build / "cross.ini")]
        if (build / "build.ninja").exists():
            setup.append("--reconfigure")
        call(setup + options)
        call([str(bindir / f"ninja{run.EXE}"), "-C", str(build), "src/amd/vulkan/libvulkan_radeon.so"])
        call([str(run.NDK_BIN / f"llvm-strip{run.EXE}"), "-o", str(build / "vulkan.radeon.so"),
              str(build / "src/amd/vulkan/libvulkan_radeon.so")])
        # Meson's build tree embeds host-only library paths. Use its own ELF installer helper
        # to remove them from the packaged copy, as meson install would do.
        with Elf(str(build / "vulkan.radeon.so"), verbose=False) as elf:
            for tag in (DT_RPATH, DT_RUNPATH):
                if elf.get_entry_offset(tag) is not None:
                    elf.remove_rpath_entry(tag)
        head = run.run(["git", "rev-parse", "--short", "HEAD"]).stdout.strip()
        dirty = run.run(["git", "status", "--porcelain", "--", "driver"]).stdout.strip()
        env["PACKAGE_TAG"] = f"xd-{head}{'-dirty' if dirty else ''}-x11"
        call([sys.executable, str(source / "android/package.py"), str(build / "vulkan.radeon.so"),
              str(source / "dist")])
    print(f"X11 build complete: {build / 'vulkan.radeon.so'}")


if __name__ == "__main__":
    main()
