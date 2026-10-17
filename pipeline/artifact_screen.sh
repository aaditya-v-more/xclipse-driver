#!/bin/sh
# Screen runtime RADV flags against Heaven's blue streak artifacts: one Heaven RADV run per flag,
# with screenshots every 10 s from 35 s after rendering starts (the airship scene shows streaks
# in about half of the frames at 35 s). Pass labels to run a subset, e.g. `wave32 nongg`.
# Review the *-r*s.png screenshots in each results/<time>-game folder by eye.
cd "$(dirname "$0")/.." || exit 1
PY=tools/python/bin/python3
[ -x "$PY" ] || PY=tools/python/python.exe
SHOTS=35,45,55,65,75,85,95
run() {
  label=$1; shift
  echo "=== $label $*"
  "$PY" -u pipeline/game.py --bench heaven --drivers radv --runs 1 --seconds 150 \
      --rendered-shots $SHOTS --note "artifact screen: $label" "$@"
}
flags() {
  case $1 in
    baseline) ;;
    wave32) echo --prop debug.radv_perftest=pswave32,gewave32,cswave32 ;;
    syncshaders) echo --prop debug.radv_debug=syncshaders ;;
    nggreuse0) echo --prop debug.radv_xclipse_nggreuse=0 ;;
    mtype4) echo --prop debug.radv_xclipse_mtype=4 ;;
    nongg) echo --prop debug.radv_debug=nongg ;;
    *) echo "unknown label $1" >&2; exit 2 ;;
  esac
}
[ $# -gt 0 ] || set -- baseline wave32 syncshaders nggreuse0 mtype4 nongg
for label in "$@"; do
  f=$(flags "$label") || exit 2
  run "$label" $f || exit 1
done
echo "=== done"
