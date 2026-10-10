#!/system/bin/sh
# Read-only survey of this phone's GPU stack, for planning an open-source Vulkan driver.
section() { echo; echo "=== $1 ==="; }

section "Build and platform"
for p in ro.product.model ro.product.device ro.product.name ro.build.PDA ro.build.display.id ro.bootloader \
    ro.build.version.release ro.build.version.sdk ro.build.version.oneui ro.build.version.security_patch \
    ro.board.platform ro.hardware ro.soc.manufacturer ro.soc.model ro.chipname \
    ro.hardware.vulkan ro.hardware.egl ro.gfx.angle.supported ro.gfx.driver.0 ro.gfx.driver.1 \
    ro.vendor.api_level ro.product.first_api_level ro.vndk.version \
    ro.boot.verifiedbootstate ro.boot.flash.locked ro.boot.warranty_bit; do
  echo "$p=$(getprop $p)"
done
echo "kernel: $(uname -a)"
echo "selinux: $(getenforce)"

section "Graphics-related properties"
getprop | grep -i -E 'vulkan|egl|gpu|gfx|angle|graphics|sgpu' | head -60

section "GPU device nodes"
ls -lZ /dev/dri 2>&1
ls -lZ /dev 2>/dev/null | grep -i -E 'gpu|mali|kgsl|dri|dma_heap'

section "DRM sysfs"
ls -l /sys/class/drm 2>&1
for d in /sys/class/drm/*; do
  if [ -e "$d/device/uevent" ]; then echo "--- $d/device/uevent"; cat "$d/device/uevent" 2>&1; fi
done

section "Can the shell user open the GPU nodes?"
for n in /dev/dri/renderD* /dev/dri/card*; do
  [ -e "$n" ] || continue
  if ( exec 3<>"$n" ) 2>/dev/null; then echo "$n: open OK"; else echo "$n: open DENIED"; fi
done

section "Loaded kernel modules (GPU related)"
for m in $(ls /sys/module | grep -i -E 'sgpu|gpu|drm|amd'); do
  echo "$m: params: $(ls /sys/module/$m/parameters 2>/dev/null | tr '\n' ' ')"
done
for dir in /vendor_dlkm/lib/modules /vendor/lib/modules /system_dlkm/lib/modules; do
  [ -d "$dir" ] && ls -l "$dir" 2>&1 | grep -i -E 'gpu|drm|amd|denied' | sed "s|^|$dir: |"
done

section "GPU sysfs (/sys/kernel/gpu)"
if [ -d /sys/kernel/gpu ]; then
  for f in /sys/kernel/gpu/*; do
    [ -f "$f" ] && echo "$(basename "$f")=$(head -c 300 "$f" 2>&1 | tr '\n' ' ')"
  done
else
  echo "not present"
fi

section "Vendor graphics libraries"
ls -l /vendor/lib64/hw 2>&1 | grep -i -E 'vulkan|gralloc|hwcomposer|mapper'
ls -l /vendor/lib64/egl 2>&1
ls -l /vendor/lib64 2>&1 | grep -i -E 'sgpu|xclipse|amd|vulkan|pal|llvm|gpu|drm|angle|mali'
ls -l /system/lib64 2>&1 | grep -i -E 'angle|vulkan'

section "Strings in the vendor Vulkan driver"
for f in /vendor/lib64/hw/vulkan.*.so; do
  [ -f "$f" ] || continue
  echo "--- $f"
  strings "$f" 2>&1 | grep -i -E 'llpc|xgl|amdvlk|pal::|libdrm|amdgpu|sgpu|gfx1[0-2][0-9]|vangogh|navi[0-9]|rdna|llvm version|clang version|radv|mesa' | sort | uniq -c | sort -rn | head -60
done

section "Firmware files (GPU related)"
for dir in /vendor/firmware /vendor/firmware_mnt /odm/firmware /vendor/etc/firmware; do
  [ -d "$dir" ] && ls -l "$dir" 2>&1 | grep -i -E 'gpu|gfx|rlc|mec|pfp|mes|imu|amd|denied' | sed "s|^|$dir: |"
done

section "SELinux rules mentioning the GPU device"
for f in /vendor/etc/selinux/vendor_sepolicy.cil /system/etc/selinux/plat_sepolicy.cil /odm/etc/selinux/odm_sepolicy.cil; do
  [ -f "$f" ] && grep -i -E 'gpu_device|sgpu' "$f" 2>&1 | grep -i -E 'untrusted_app|appdomain|shell|allowx|ioctl' | head -60 | sed "s|^|$(basename $f): |"
done
grep -i -E 'dri|gpu' /vendor/etc/selinux/vendor_file_contexts 2>/dev/null | head -20

section "SurfaceFlinger GL driver"
dumpsys SurfaceFlinger 2>/dev/null | grep -i -E 'GLES|vulkan' | head -5
