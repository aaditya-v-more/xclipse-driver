#!/system/bin/sh
# Second read-only pass: SELinux ioctl filtering, sgpu IP versions, libdrm_sgpu API, vendor driver hints.
section() { echo; echo "=== $1 ==="; }
SGPU=/sys/devices/platform/22200000.sgpu

section "ioctl allowlists (allowx) on the GPU device"
for f in /vendor/etc/selinux/vendor_sepolicy.cil /system/etc/selinux/plat_sepolicy.cil \
         /system_ext/etc/selinux/system_ext_sepolicy.cil /product/etc/selinux/product_sepolicy.cil \
         /odm/etc/selinux/odm_sepolicy.cil; do
  [ -f "$f" ] || continue
  echo "$(basename $f): allowx total=$(grep -c allowx $f), allowx mentioning gpu_device=$(grep allowx $f | grep -c gpu_device)"
done

section "Domains behind the generic GPU allow rules"
grep -E '^\(typeattributeset base_typeattr_265 ' /system/etc/selinux/plat_sepolicy.cil | head -c 1500; echo
grep -E '^\(typeattributeset base_typeattr_586_34_0 ' /vendor/etc/selinux/vendor_sepolicy.cil | head -c 1500; echo

section "sgpu module"
for f in version srcversion initstate taint coresize; do echo "$f=$(cat /sys/module/sgpu/$f 2>&1)"; done
for dir in /vendor_dlkm/lib/modules /vendor/lib/modules /system_dlkm/lib/modules /lib/modules; do
  [ -d "$dir" ] && echo "$dir: $(ls $dir 2>&1 | grep -c .) entries; gpu-related: $(ls $dir 2>&1 | grep -i -E 'sgpu|gpu|denied' | tr '\n' ' ')"
done

section "sgpu module parameter values"
for p in /sys/module/sgpu/parameters/*; do echo "$(basename $p)=$(cat $p 2>&1 | head -c 120)"; done

section "sgpu sysfs device attributes"
ls $SGPU 2>&1 | tr '\n' ' '; echo
for a in mem_info_vram_total mem_info_gtt_total mem_info_vis_vram_total gpu_busy_percent product_name \
         product_number serial_number vbios_version unique_id; do
  [ -e $SGPU/$a ] && echo "$a=$(cat $SGPU/$a 2>&1 | head -c 200)"
done
if [ -d $SGPU/ip_discovery ]; then
  find $SGPU/ip_discovery -maxdepth 7 -name major 2>/dev/null | head -60 | while read f; do
    d=$(dirname $f); echo "${d#$SGPU/}: $(cat $d/major 2>/dev/null).$(cat $d/minor 2>/dev/null).$(cat $d/revision 2>/dev/null)"
  done
else
  echo "no ip_discovery directory"
fi

section "libdrm_sgpu.so API (exported names)"
strings /vendor/lib64/libdrm_sgpu.so | grep -E '^(amdgpu|sgpu)_[a-z0-9_]+$' | sort -u | tr '\n' ' '; echo
echo "--- ids/data file references:"
strings /vendor/lib64/libdrm_sgpu.so | grep -i -E '\.ids|/vendor/|/system/|hwdata' | sort -u
for f in /vendor/etc/hwdata/amdgpu.ids /vendor/etc/sgpu.ids /vendor/etc/hwdata/sgpu.ids /system/etc/hwdata/amdgpu.ids; do
  [ -f "$f" ] && { echo "--- $f"; grep -v '^#' "$f" | head -40; }
done

section "GFX IP and toolchain hints in vulkan.samsung.so"
strings /vendor/lib64/hw/vulkan.samsung.so | grep -o -i -E 'gfx1[0-2][0-9a-f]{1,2}|gfxip[ _]?[0-9.]+|navi[0-9]+|phoenix|strix|rembrandt|vangogh[a-z_]*|xclipse[ 0-9a-z]*|llvm version [0-9.]+|clang version [0-9.]+|amdvlk[a-z0-9 _.-]*' | sort | uniq -c | sort -rn | head -40

section "gpu service commands"
cmd gpu help 2>&1 | head -30
