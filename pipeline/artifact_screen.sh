#!/bin/sh
# Screen runtime RADV flags against Heaven's blue streak artifacts: one Heaven RADV run per flag,
# with screenshots every 4 s through the airship scene (32-64 s after rendering starts). Pass
# labels to run a subset, e.g. `wave32 nongg`. Review the *-r*s.png screenshots in each
# results/<time>-game folder by eye.
#
# The streaks showed in 7 of 12 airship shots in fixed performance mode but hardly at normal
# clocks, so FIXED=1 (the default) holds fixed performance mode for the screen and releases it on
# exit. FIXED=0 leaves clocks alone. SHOTS overrides the shot times.
cd "$(dirname "$0")/.." || exit 1
PY=tools/python/bin/python3
[ -x "$PY" ] || PY=tools/python/python.exe
SHOTS=${SHOTS:-32,36,40,44,48,52,56,60,64}
FIXED=${FIXED:-1}
if [ "$FIXED" = 1 ]; then
  trap 'adb shell cmd power set-fixed-performance-mode-enabled false' EXIT
  trap 'exit 130' INT TERM
  out=$(adb shell cmd power set-fixed-performance-mode-enabled true 2>&1)
  [ -z "$out" ] || { echo "fixed performance mode not confirmed: $out" >&2; exit 1; }
fi
run() {
  label=$1; shift
  echo "=== $label $*"
  "$PY" -u pipeline/game.py --bench heaven --drivers radv --runs 1 --seconds 120 \
      --rendered-shots $SHOTS --note "artifact screen: $label (fixed mode: $FIXED)" "$@"
}
flags() {
  case $1 in
    baseline) ;;
    wave32) echo --prop debug.radv_perftest=pswave32,gewave32,cswave32 ;;
    syncshaders) echo --prop debug.radv_debug=syncshaders ;;
    nggreuse0) echo --prop debug.radv_xclipse_nggreuse=0 ;;
    mtype4) echo --prop debug.radv_xclipse_mtype=4 ;;
    mtype1) echo --prop debug.radv_xclipse_mtype=1 ;;
    mtype2) echo --prop debug.radv_xclipse_mtype=2 ;;
    mtype3) echo --prop debug.radv_xclipse_mtype=3 ;;
    bufl2) echo --prop debug.radv_xclipse_bufl2=1 ;;  # L2 writeback+invalidate in buffer barriers
    ring1|ring2|ring4|ring8|ring16|ring32) echo --prop debug.radv_xclipse_ringuc=${1#ring} ;;  # UC for one ring class
    uc1|uc2|uc4|uc8) echo --prop debug.radv_xclipse_ucmask=${1#uc} ;;  # UC for one BO class
    gl1scope0) echo --prop debug.radv_xclipse_gl1scope=0 ;;  # GFX11 cache bits (GLC only) for device-scope loads
    filteremu0) echo --prop debug.radv_xclipse_filteremu=0 ;;  # hardware point-sampling for unfilterable formats
    filteremu2) echo --prop debug.radv_xclipse_filteremu=2 ;;  # emulate bilinear only, no second mip level
    filteremu3) echo --prop debug.radv_xclipse_filteremu=3 ;;  # diagnostic: check and branch, zero result
    *) echo "unknown label $1" >&2; exit 2 ;;
  esac
}
# Never add RADV_DEBUG=nongg: the legacy geometry path hangs the 940 (docs/findings.md).
[ $# -gt 0 ] || set -- baseline wave32 syncshaders nggreuse0 mtype4
for label in "$@"; do
  f=$(flags "$label") || exit 2
  run "$label" $f || exit 1
done
echo "=== done"
