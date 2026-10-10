#!/usr/bin/env python3
"""Runs Windows benchmarks in GameNative on the phone and compares Samsung's driver with ours.

Each benchmark is installed as two GameNative custom games: one container on the stock driver
("System") and one on our RADV build, otherwise identical. For every run the script launches the
game over adb, records the frames GameNative presents on screen (SurfaceFlinger timestamps), reads
the benchmark's own frame-time file when it writes one, and stops the app. Drivers alternate run
by run and the phone cools down in between, so heat affects both alike.

usage: python pipeline/game.py [--bench gravitymark,heaven] [--runs 3]
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import statistics
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run import RESULTS, adb, phone_state, shell  # noqa: E402

APP = "app.gamenative"
OUT_DIR = "/sdcard/Download/xclipse/out"  # D:\xclipse\out inside the containers
BENCHES = {
    "gravitymark": {"ids": {"stock": 2000940101, "radv": 2000940102}, "seconds": 300,
                    "times": "gravitymark-{driver}-times.txt", "image": "gravitymark-{driver}.png"},
    "heaven": {"ids": {"stock": 2000940201, "radv": 2000940202}, "seconds": 330},
}
# Exec Arguments to set once per container (GameNative: Edit container -> General).
EXEC_ARGS = {
    "gravitymark": ("-vk -fullscreen 1 -benchmark 1 -close 1 -count 1 -asteroids 50000 -fps 1 "
                    "-info 0 -sensors 0 -times D:\\xclipse\\out\\gravitymark-{driver}-times.txt "
                    "-image D:\\xclipse\\out\\gravitymark-{driver}.png"),
    "heaven": ("-project_name Heaven -data_path A:/ -engine_config A:/data/heaven_4.0.cfg "
               "-system_script heaven/unigine.cpp -sound_app null -video_app direct3d11 "
               "-video_multisample 0 -video_fullscreen 1 -video_mode -1 "
               "-extern_define PHORONIX,RELEASE,QUALITY_LOW"),
}
WARMUP_S = 10  # ignore frames from the first seconds after the first frame (loading, ramp-up)


def game_layers() -> list[str]:
    """Names of GameNative's SurfaceView layers, which carry the game's frames."""
    names = []
    for line in shell("dumpsys SurfaceFlinger --list", 30).splitlines():
        name = line.strip()
        if name.startswith("RequestedLayerState{"):  # Android 15+ prints the layer state
            name = name[len("RequestedLayerState{"):].split(" parentId=")[0].rstrip("}")
        if APP in name and "SurfaceView" in name and "Background" not in name:
            names.append(name)
    return names


def present_times(layer: str) -> list[int]:
    """Actual present timestamps (ns) of the layer's last ~128 frames."""
    out = shell(f"dumpsys SurfaceFlinger --latency '{layer}'", 30).splitlines()
    times = []
    for line in out[1:]:
        parts = line.split()
        if len(parts) >= 2 and parts[1].isdigit():
            t = int(parts[1])
            if 0 < t < (1 << 62):
                times.append(t)
    return times


def frame_stats(times_ns: list[int]) -> dict:
    if len(times_ns) < 10:
        return {"frames": len(times_ns)}
    ts = sorted(set(times_ns))
    start = ts[0] + WARMUP_S * 1_000_000_000
    ts = [t for t in ts if t >= start]
    if len(ts) < 10:
        return {"frames": len(ts)}
    intervals = [(b - a) / 1e6 for a, b in zip(ts, ts[1:])]
    worst = sorted(intervals)[int(len(intervals) * 0.99):]
    return {
        "frames": len(ts),
        "seconds": (ts[-1] - ts[0]) / 1e9,
        "fps_avg": (len(ts) - 1) / ((ts[-1] - ts[0]) / 1e9),
        "frametime_median_ms": statistics.median(intervals),
        "fps_1pct_low": 1000.0 / statistics.mean(worst) if worst else None,
    }


def gpu_temp() -> float:
    try:
        return float(shell("cat /sys/kernel/gpu/gpu_tmu", 15))
    except ValueError:
        return 0.0


def cool_down(target: float, limit_s: float = 120) -> float:
    deadline = time.monotonic() + limit_s
    while gpu_temp() > target and time.monotonic() < deadline:
        time.sleep(5)
    return gpu_temp()


def run_once(bench: str, driver: str, run_dir: Path, tag: str) -> dict:
    cfg = BENCHES[bench]
    app_id = cfg["ids"][driver]
    shell(f"am force-stop {APP}")
    for key in ("times", "image"):
        if key in cfg:
            shell(f"rm -f {OUT_DIR}/{cfg[key].format(driver=driver)}")
    result = {"bench": bench, "driver": driver, "app_id": app_id, "gpu_temp_c_before": gpu_temp()}
    started = time.monotonic()
    shell(f"am start -n {APP}/.MainActivity -a {APP}.LAUNCH_GAME --ei app_id {app_id} "
          f"--es game_source CUSTOM_GAME")

    frames, layer, seen_at, gone_for = set(), None, None, 0
    while time.monotonic() - started < cfg["seconds"]:
        time.sleep(1)
        if phone_state() != "device":
            result["status"] = "phone-lost"
            break
        layers = game_layers()
        if not layers:
            gone_for = gone_for + 1 if seen_at else 0
            if seen_at and gone_for >= 5:  # the game closed itself (benchmark finished)
                break
            continue
        gone_for = 0
        if layer not in layers:
            layer = layers[0]
        new = present_times(layer)
        if new and seen_at is None:
            seen_at = time.monotonic() - started
        frames.update(new)
    result["seconds_to_first_frame"] = seen_at
    result["layer"] = layer
    result["surfaceflinger"] = frame_stats(list(frames))
    shell(f"am force-stop {APP}")

    if "times" in cfg:
        name = cfg["times"].format(driver=driver)
        local = run_dir / f"{tag}-{name}"
        if adb("pull", f"{OUT_DIR}/{name}", str(local)).returncode == 0:
            result["times_file"] = local.name
    if "image" in cfg:
        name = cfg["image"].format(driver=driver)
        local = run_dir / f"{tag}-{name}"
        if adb("pull", f"{OUT_DIR}/{name}", str(local)).returncode == 0:
            result["image_file"] = local.name
    result.setdefault("status", "ran" if result["surfaceflinger"].get("frames", 0) >= 10
                      else "no-frames")
    result["gpu_temp_c_after"] = gpu_temp()
    return result


def summarize(meta: dict, results: list[dict]) -> str:
    out = [f"# Game benchmark run {meta['started']}", "",
           "Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the "
           f"first {WARMUP_S} s after the first frame.", "",
           "| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) |",
           "|---|---|---|---|---|---|---|"]
    for r in results:
        s = r.get("surfaceflinger", {})
        fmt = lambda v: f"{v:.1f}" if isinstance(v, (int, float)) else ""  # noqa: E731
        out.append(f"| {r['bench']} | {r['driver']} | {r['run']} | {r['status']} | "
                   f"{fmt(s.get('fps_avg'))} | {fmt(s.get('fps_1pct_low'))} | "
                   f"{fmt(s.get('frametime_median_ms'))} |")
    out += ["", "## Medians", ""]
    for bench in dict.fromkeys(r["bench"] for r in results):
        line = []
        for driver in ("stock", "radv"):
            fps = [r["surfaceflinger"]["fps_avg"] for r in results if r["bench"] == bench
                   and r["driver"] == driver and "fps_avg" in r.get("surfaceflinger", {})]
            if fps:
                line.append((driver, statistics.median(fps)))
        text = ", ".join(f"{d} {v:.1f} FPS" for d, v in line)
        if len(line) == 2 and line[0][1] > 0:
            text += f" (radv/stock {line[1][1] / line[0][1]:.2f}x)"
        out.append(f"- **{bench}**: {text}")
    return "\n".join(out) + "\n"


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bench", default="gravitymark,heaven")
    ap.add_argument("--drivers", default="stock,radv")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--print-args", action="store_true",
                    help="print the Exec Arguments for each container and exit")
    args = ap.parse_args()
    benches, drivers = args.bench.split(","), args.drivers.split(",")

    if args.print_args:
        for bench in benches:
            for driver in drivers:
                print(f"{bench}-{driver}: {EXEC_ARGS[bench].format(driver=driver)}")
        return
    if phone_state() != "device":
        sys.exit("phone not ready on adb")

    started = dt.datetime.now()
    run_dir = RESULTS / (started.strftime("%Y-%m-%d_%H%M%S") + "-game")
    run_dir.mkdir(parents=True)
    shell(f"mkdir -p {OUT_DIR}")
    baseline = cool_down(target=45.0)
    meta = {"started": started.isoformat(timespec="seconds"), "benches": benches,
            "drivers": drivers, "runs": args.runs, "gpu_temp_c_start": baseline}

    results = []
    for n in range(1, args.runs + 1):
        for bench in benches:
            for driver in (drivers if n % 2 else list(reversed(drivers))):
                print(f"run {n} {bench:<12} {driver:<5}", end=" ", flush=True)
                r = run_once(bench, driver, run_dir, f"run{n}")
                r["run"] = n
                s = r["surfaceflinger"]
                print(f"{r['status']} {s.get('fps_avg', 0):.1f} fps over {s.get('seconds', 0):.0f} s",
                      flush=True)
                results.append(r)
                if r["status"] == "phone-lost":
                    sys.exit("lost the phone; stopping")
                cool_down(target=baseline + 3)

    (run_dir / "results.json").write_text(json.dumps({"meta": meta, "results": results}, indent=2),
                                          encoding="utf-8", newline="\n")
    (run_dir / "summary.md").write_text(summarize(meta, results), encoding="utf-8", newline="\n")
    print(f"report: {run_dir / 'summary.md'}")


if __name__ == "__main__":
    main()
