#!/usr/bin/env python3
"""Runs Windows benchmarks in GameNative on the phone and compares Samsung's driver with ours.

Each benchmark is installed as two GameNative custom games: one container on the stock driver
("System") and one on our RADV build, otherwise identical. For every run the script launches the
game over adb, records the frames GameNative presents on screen (SurfaceFlinger timestamps), reads
the benchmark's own frame-time file when it writes one, and stops the app. Drivers alternate run
by run and the phone cools down in between, so heat affects both alike.

Before each run it closes background apps (`am kill-all`), because GravityMark under Wine needs
most of the phone's memory: on the first try Android's low-memory killer stopped GameNative 18 s
into the benchmark. Free memory, swap, and GameNative's own memory are sampled during the run, and
a run that the low-memory killer ends is reported as such, not as a finished benchmark.

usage: python pipeline/game.py [--bench gravitymark,heaven] [--runs 3] [--radv-log]
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
WARMUP_S = 10  # ignore frames from the first seconds after the game starts rendering (ramp-up)
MEMORY_EVERY_S = 5
# Log lines that show which Vulkan driver a run loaded. Samsung's driver logs OpenDevice on its
# own; RADV logs [BUILD] only with debug.radv_xclipse_log >= 1 (--radv-log).
DRIVER_MARKERS = {"samsung": "Samsung::Vulkan::OpenDevice", "radv": "RADV_BUILD"}
LOW_MEMORY_KILL = f"Reclaim '{APP}'|Kill '{APP}'"


def game_layers() -> list[str]:
    """Names of GameNative's SurfaceView layers. The frames land on one of them (on Android 16, a
    child layer of the SurfaceView); `present_times` is empty for the others."""
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
    ts = sorted(set(times_ns))
    # GameNative's SurfaceView exists from its "Booting..." screen on and presents a frame now and
    # then while Wine starts. The game is rendering once 10 frames land within 2 s.
    rendering = next((t for i, t in enumerate(ts[:-10]) if ts[i + 10] - t < 2_000_000_000), None)
    if rendering is None:
        return {"frames": len(ts)}
    ts = [t for t in ts if t >= rendering + WARMUP_S * 1_000_000_000]
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


def app_user() -> str:
    """GameNative's Linux user name (u0_aNNN), which its Wine processes run as too."""
    uid = int(shell(f"cmd package list packages -U {APP}").split("uid:")[1].split()[0])
    return f"u0_a{uid % 100000 - 10000}"


def memory_sample(user: str, processes: dict[str, str] | None = None) -> dict:
    """System memory, and the resident and swapped memory of every GameNative process (its Wine
    processes included), in MB. Adds those processes to `processes` (pid -> name)."""
    out = shell("cat /proc/meminfo /proc/pressure/memory /sys/kernel/gpu/mem_info; "
                f"ps -A -u {user} -o PID=,NAME=; "
                f"for p in $(ps -A -u {user} -o PID=); do grep -h -E '^Vm(RSS|Swap):' /proc/$p/status; "
                "done", 30)
    kb = {"MemAvailable:": 0, "SwapFree:": 0, "GpuTotal:": 0, "VmRSS:": 0, "VmSwap:": 0}
    psi_full = None
    gpu_bytes, pids = {}, set()
    for line in out.splitlines():
        parts = line.split(None, 1)
        if not parts:
            continue
        if parts[0] in kb:
            kb[parts[0]] += int(parts[1].split()[0])
        elif parts[0] == "full":
            psi_full = float(parts[1].split()[0].split("=")[1])
        elif parts[0] == "pid:":  # /sys/kernel/gpu/mem_info: "pid: <pid> <bytes>"
            pid, size = parts[1].split()
            gpu_bytes[pid] = int(size)
        elif parts[0].isdigit() and len(parts) == 2:
            pids.add(parts[0])
            if processes is not None:
                processes[parts[0]] = parts[1].strip()
    return {"available_mb": kb["MemAvailable:"] // 1024, "swap_free_mb": kb["SwapFree:"] // 1024,
            "app_rss_mb": kb["VmRSS:"] // 1024, "app_swap_mb": kb["VmSwap:"] // 1024,
            "gpu_total_mb": kb["GpuTotal:"] // 1024,
            "app_gpu_mb": sum(gpu_bytes.get(pid, 0) for pid in pids) // (1 << 20),
            "pressure_full_avg10": psi_full}


def free_memory(user: str) -> int:
    """Closes background apps: `am kill-all` ends every process Android considers safe to kill
    (cached apps), the ones it would reclaim first anyway. Returns available memory in MB."""
    shell("am kill-all", 60)
    time.sleep(3)
    return memory_sample(user)["available_mb"]


def logcat_since(since: str, pattern: str) -> list[str]:
    """Log lines since `since` (the phone's 'MM-DD hh:mm:ss.mmm') that match an extended regex."""
    out = shell(f"logcat -d -b main,system -T '{since}' | grep -E \"{pattern}\"", 60)
    # adbd logs each shell command it runs, this grep included, so its lines would always match.
    return [line for line in out.splitlines() if line.strip() and "service requested" not in line]


def drivers_seen(since: str, processes: dict[str, str]) -> list[str]:
    """Which Vulkan drivers the game's Wine processes loaded, from their log lines. GameNative's
    own process is left out: Android's UI renderer can open the system Vulkan driver in any app."""
    wine = {pid for pid, name in processes.items() if name != APP}
    seen = []
    for name, marker in DRIVER_MARKERS.items():
        for line in logcat_since(since, marker):
            fields = line.split()  # threadtime format: date time pid tid level tag: message
            if len(fields) > 2 and fields[2] in wine:
                seen.append(name)
                break
    return seen


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


def run_once(bench: str, driver: str, run_dir: Path, tag: str, user: str) -> dict:
    cfg = BENCHES[bench]
    app_id = cfg["ids"][driver]
    shell(f"am force-stop {APP}")
    for key in ("times", "image"):
        if key in cfg:
            shell(f"rm -f {OUT_DIR}/{cfg[key].format(driver=driver)}")
    result = {"bench": bench, "driver": driver, "app_id": app_id, "gpu_temp_c_before": gpu_temp(),
              "available_mb_before": free_memory(user)}
    since = shell("date '+%m-%d %H:%M:%S.000'")
    memory, processes = [], {}
    started = time.monotonic()
    shell(f"am start -n {APP}/.MainActivity -a {APP}.LAUNCH_GAME --ei app_id {app_id} "
          f"--es game_source CUSTOM_GAME")

    frames, layer, seen_at, gone_for, sampled = set(), None, None, 0, -MEMORY_EVERY_S
    while time.monotonic() - started < cfg["seconds"]:
        time.sleep(1)
        if phone_state() != "device":
            result["status"] = "phone-lost"
            break
        elapsed = time.monotonic() - started
        if elapsed - sampled >= MEMORY_EVERY_S:
            memory.append({"t": round(elapsed), **memory_sample(user, processes)})
            sampled = elapsed
        layers = game_layers()
        if not layers:
            gone_for = gone_for + 1 if seen_at else 0
            if seen_at and gone_for >= 5:  # the game closed itself (benchmark finished)
                break
            continue
        gone_for = 0
        new = present_times(layer) if layer in layers else []
        if not new:
            for name in layers:
                new = present_times(name)
                if new:
                    layer = name
                    break
        if new and seen_at is None:
            seen_at = time.monotonic() - started
        frames.update(new)
    result["seconds_to_first_frame"] = seen_at
    result["layer"] = layer
    result["surfaceflinger"] = frame_stats(list(frames))
    result["memory"] = memory
    shell(f"am force-stop {APP}")
    killed = logcat_since(since, LOW_MEMORY_KILL)
    if killed:
        result["status"] = "killed-low-memory"
        result["low_memory_kill"] = killed[0].strip()
    result["vulkan_drivers_seen"] = drivers_seen(since, processes)
    result["processes"] = sorted(set(processes.values()))

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
           "| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) "
           "| Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |",
           "|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in results:
        s = r.get("surfaceflinger", {})
        fmt = lambda v: f"{v:.1f}" if isinstance(v, (int, float)) else ""  # noqa: E731
        mem = r.get("memory") or [{}]
        least = min((m.get("available_mb", 0) for m in mem), default=0)
        peak = max((m.get("app_rss_mb", 0) + m.get("app_swap_mb", 0) for m in mem), default=0)
        gpu_peak = max((m.get("app_gpu_mb", 0) for m in mem), default=0)
        out.append(f"| {r['bench']} | {r['driver']} | {r['run']} | {r['status']} | "
                   f"{fmt(s.get('fps_avg'))} | {fmt(s.get('fps_1pct_low'))} | "
                   f"{fmt(s.get('frametime_median_ms'))} | "
                   f"{', '.join(r.get('vulkan_drivers_seen', [])) or 'none'} | {least} | {peak} | "
                   f"{gpu_peak} |")
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
    ap.add_argument("--radv-log", action="store_true",
                    help="turn on RADV's diagnostic log (debug.radv_xclipse_log=1) so radv runs "
                         "prove our driver loaded; it adds CPU work, so leave it off for timing")
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

    # The screen must stay on between runs (the timeout is 30 s), and a game can't start on a
    # locked phone. The setting is restored at the end.
    stay_on = shell("settings get global stay_on_while_plugged_in")
    shell("svc power stayon usb")
    try:
        shell("input keyevent KEYCODE_WAKEUP")  # handled by the system, not passed to apps
        if not wait_unlocked(600):
            sys.exit("the phone stayed locked; unlock it, then run again")
        started = dt.datetime.now()
        run_dir = RESULTS / (started.strftime("%Y-%m-%d_%H%M%S") + "-game")
        run_dir.mkdir(parents=True)
        shell(f"mkdir -p {OUT_DIR}")
        baseline = cool_down(target=45.0)
        meta = {"started": started.isoformat(timespec="seconds"), "benches": benches,
                "drivers": drivers, "runs": args.runs, "gpu_temp_c_start": baseline}
        shell(f"setprop debug.radv_xclipse_log {1 if args.radv_log else 0}")
        results = run_all(args, benches, drivers, run_dir, app_user(), baseline)
    finally:
        shell(f"settings put global stay_on_while_plugged_in {stay_on}")
        shell("setprop debug.radv_xclipse_log 0")

    (run_dir / "results.json").write_text(json.dumps({"meta": meta, "results": results}, indent=2),
                                          encoding="utf-8", newline="\n")
    (run_dir / "summary.md").write_text(summarize(meta, results), encoding="utf-8", newline="\n")
    print(f"report: {run_dir / 'summary.md'}")


def wait_unlocked(limit_s: float) -> bool:
    deadline = time.monotonic() + limit_s
    asked = False
    while "isKeyguardShowing=true" in shell("dumpsys window | grep isKeyguardShowing"):
        if time.monotonic() > deadline:
            return False
        if not asked:
            print("waiting for the phone to be unlocked", flush=True)
            asked = True
        time.sleep(3)
    return True


def run_all(args, benches: list[str], drivers: list[str], run_dir: Path, user: str,
            baseline: float) -> list[dict]:
    results = []
    for n in range(1, args.runs + 1):
        for bench in benches:
            for driver in (drivers if n % 2 else list(reversed(drivers))):
                print(f"run {n} {bench:<12} {driver:<5}", end=" ", flush=True)
                r = run_once(bench, driver, run_dir, f"run{n}", user)
                r["run"] = n
                s = r["surfaceflinger"]
                print(f"{r['status']} {s.get('fps_avg', 0):.1f} fps over {s.get('seconds', 0):.0f} s",
                      flush=True)
                results.append(r)
                if r["status"] == "phone-lost":
                    print("lost the phone; stopping")
                    return results
                cool_down(target=baseline + 3)
    return results


if __name__ == "__main__":
    main()
