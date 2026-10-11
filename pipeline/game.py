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

A run stops early, with a screenshot, when the game shows a message box (an error from the game or
from Wine, found in a raw screenshot while frames are scarce) or presents no new frame for a minute.
The later runs still go ahead: stock Heaven's fatal error on its D24 depth texture comes and goes.

To see what a driver renders, --shots saves screenshots at set times after launch, and --seconds
ends each run early. --prop sets Android properties for the whole session, which is how RADV's
debug options reach the game (RADV_DEBUG is read from debug.radv_debug, for example).

usage: python pipeline/game.py [--bench gravitymark,heaven] [--runs 3] [--radv-log]
                               [--shots 60,90] [--seconds 100] [--prop debug.radv_debug=nodcc]
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import statistics
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run import ADB, RESULTS, adb, phone_state, shell  # noqa: E402

APP = "app.gamenative"
OUT_DIR = "/sdcard/Download/xclipse/out"  # D:\xclipse\out inside the containers
BENCHES = {
    "gravitymark": {"ids": {"stock": 2000940101, "radv": 2000940102}, "seconds": 300,
                    "exe": "GravityMark.exe",
                    "times": "gravitymark-{driver}-times.txt", "image": "gravitymark-{driver}.png"},
    "heaven": {"ids": {"stock": 2000940201, "radv": 2000940202}, "seconds": 330,
               "exe": "Heaven.exe"},
}
# Exec Arguments to set once per container (GameNative: Edit container -> General).
EXEC_ARGS = {
    "gravitymark": ("-vk -fullscreen 1 -benchmark 1 -close 1 -count 1 -asteroids 50000 -fps 1 "
                    "-info 0 -sensors 0 -times D:\\xclipse\\out\\gravitymark-{driver}-times.txt "
                    "-image D:\\xclipse\\out\\gravitymark-{driver}.png"),
    # Heaven resolves these paths against its bin folder even when they start with a drive letter
    # ("A:/data/x" became "A:/bin/A:/data/x"), so they're relative, as in Heaven's own launcher.
    "heaven": ("-project_name Heaven -data_path ../ -engine_config ../data/heaven_4.0.cfg "
               "-system_script heaven/unigine.cpp -sound_app null -video_app direct3d11 "
               "-video_multisample 0 -video_fullscreen 1 -video_mode -1 "
               "-extern_define PHORONIX,RELEASE,QUALITY_LOW"),
}
WARMUP_S = 10  # ignore frames from the first seconds after the game starts rendering (ramp-up)
# A run counts only with this many seconds of frames in a row, none of them more than STALL_S
# apart. Wine's error box presented 8.8 FPS for 15 s and once passed for a finished run.
MIN_STEADY_S = 30
STALL_S = 2
# Heaven on radv loads with gaps of up to 16 s between frames and renders steadily 48 s after
# launch. From CHECK_AFTER_S after the game's process appears, the screen is checked for a message
# box whenever the last CHECK_EVERY_S brought under CHECK_BELOW_FPS, and a run with no new frame for
# STUCK_S is stopped.
CHECK_AFTER_S = 30
CHECK_EVERY_S = 15
CHECK_BELOW_FPS = 10
STUCK_S = 60
MEMORY_EVERY_S = 5
# Log lines that show which Vulkan driver a run loaded. Samsung's driver logs OpenDevice on its
# own; RADV logs [BUILD] only with debug.radv_xclipse_log >= 1 (--radv-log).
DRIVER_MARKERS = {"samsung": "Samsung::Vulkan::OpenDevice", "radv": "RADV_BUILD"}
LOW_MEMORY_KILL = f"Reclaim '{APP}'|Kill '{APP}'"
# GameNative logs the settings it launches a container with; kept per run to show that the stock
# and radv containers differ only in the driver.
LAUNCH_SETTINGS = ("Graphics Driver:|DX Wrapper:|Box64 Version:|Box64 Preset:|FEXCore Preset:|"
                   "WoW64 Mode:|Screen Size:|Env Vars \\(Container Base\\):|Env Vars \\(Final Guest\\):")


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
    steady, start = 0, ts[0]
    for a, b in zip(ts, ts[1:]):
        if b - a > STALL_S * 1_000_000_000:
            start = b
        steady = max(steady, b - start)
    return {
        "frames": len(ts),
        "seconds": (ts[-1] - ts[0]) / 1e9,
        "steady_seconds": steady / 1e9,
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


def screenshot(path: Path) -> bool:
    png = subprocess.run([ADB, "exec-out", "screencap", "-p"], capture_output=True,
                         timeout=60).stdout
    if not png.startswith(b"\x89PNG"):
        return False
    path.write_bytes(png)
    return True


LIGHT = bytes(int(v >= 200) for v in range(256))


def find_box(w: int, h: int, px: bytes) -> list[int] | None:
    """Bounds [x0, y0, x1, y1] of a message box in RGBA pixels, or None. Wine draws its boxes as a
    light rectangle on a black desktop, so the rows with a light run of the same left and right
    edges are counted (text breaks up the others). Runs under an eighth of the screen's long side
    (icons) or over 70% of its width (light scenes) don't count."""
    runs = []
    for y in range(0, h, 2):
        row = px[y * w * 4:(y + 1) * w * 4].translate(LIGHT)
        mask = (int.from_bytes(row[0::4], "big") & int.from_bytes(row[1::4], "big")
                & int.from_bytes(row[2::4], "big")).to_bytes(w, "big")
        for m in re.finditer(rb"\x01{%d,}" % (max(w, h) // 8), mask):
            if m.end() - m.start() <= 0.7 * w:
                runs.append((m.start(), m.end(), y))
    if not runs:
        return None
    (x0, x1), _ = Counter((a, b) for a, b, _ in runs).most_common(1)[0]
    ys = [y for a, b, y in runs if (a, b) == (x0, x1)]
    if len(ys) >= 10 and ys[-1] - ys[0] >= max(w, h) // 24:
        return [x0, ys[0], x1, ys[-1]]
    return None


def message_box() -> list[int] | None:
    """Looks for a message box on screen, from raw pixels (no image library needed)."""
    raw = subprocess.run([ADB, "exec-out", "screencap"], capture_output=True, timeout=60).stdout
    w, h = int.from_bytes(raw[0:4], "little"), int.from_bytes(raw[4:8], "little")
    if len(raw) < 12 + w * h * 4:
        return None
    return find_box(w, h, raw[len(raw) - w * h * 4:])  # after a 12- or 16-byte header


def gpu_sample() -> tuple[int, int] | None:
    """How busy the GPU was over the kernel's last DVFS window (8 ms), in percent, and its clock in
    kHz. Sampled once a second, the busy share averages out to the share of time the GPU had work."""
    try:
        busy, clock = shell("cat /sys/kernel/gpu/gpu_busy /sys/kernel/gpu/gpu_clock", 15).split()
        return int(busy), int(clock)
    except ValueError:
        return None


def gpu_faults() -> dict[str, int]:
    """The sgpu kernel's lifetime counts of GPU page faults (GPF_S) and GPU resets (GHG_S). Every
    page fault ends in a full GPU reset (sgpu_page_fault_recovery), so a run that faults stalls."""
    info = shell("cat /sys/kernel/gpu/gpu_info", 15)
    counts = {}
    for key, name in (("GPF_S", "page_faults"), ("GHG_S", "resets")):
        m = re.search(rf'"{key}":"(\d+)"', info)
        if m:
            counts[name] = int(m.group(1))
    return counts


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


def run_once(bench: str, driver: str, run_dir: Path, tag: str, user: str,
             shots: list[int] = (), seconds: int | None = None) -> dict:
    cfg = BENCHES[bench]
    app_id = cfg["ids"][driver]
    shell(f"am force-stop {APP}")
    for key in ("times", "image"):
        if key in cfg:
            shell(f"rm -f {OUT_DIR}/{cfg[key].format(driver=driver)}")
    result = {"bench": bench, "driver": driver, "app_id": app_id, "gpu_temp_c_before": gpu_temp(),
              "available_mb_before": free_memory(user)}
    faults_before = gpu_faults()
    since = shell("date '+%m-%d %H:%M:%S.000'")
    memory, processes, gpu = [], {}, []
    started = time.monotonic()
    shell(f"am start -n {APP}/.MainActivity -a {APP}.LAUNCH_GAME --ei app_id {app_id} "
          f"--es game_source CUSTOM_GAME")

    frames, layer, seen_at, gone_for, sampled = set(), None, None, 0, -MEMORY_EVERY_S
    exe_seen = exe_gone = 0
    history, last_new, checked = [], 0.0, 0.0  # (seconds, frames so far); when the count last grew
    shots_left = sorted(shots)
    while time.monotonic() - started < (seconds or cfg["seconds"]):
        time.sleep(1)
        if phone_state() != "device":
            result["status"] = "phone-lost"
            break
        elapsed = time.monotonic() - started
        while shots_left and elapsed >= shots_left[0]:
            shot = run_dir / f"{tag}-{bench}-{driver}-{shots_left.pop(0)}s.png"
            if screenshot(shot):
                result.setdefault("screens", []).append(shot.name)
        sample = gpu_sample()
        if sample:
            gpu.append((round(elapsed, 1), *sample))
        if elapsed - sampled >= MEMORY_EVERY_S:
            now = {}
            memory.append({"t": round(elapsed), **memory_sample(user, now)})
            processes.update(now)
            sampled = elapsed
            # GameNative keeps showing Wine's desktop after the game exits, so the game's own
            # process tells when it's over.
            if any(cfg["exe"].lower() in name.lower() for name in now.values()):
                exe_seen, exe_gone = exe_seen or round(elapsed), 0
            elif exe_seen:
                exe_gone += 1
                if exe_gone >= 2:
                    result["game_exited_s"] = round(elapsed)
                    break
        if exe_seen and elapsed - exe_seen >= CHECK_AFTER_S:
            before = next((n for t, n in history if t >= elapsed - CHECK_EVERY_S), len(frames))
            if (len(frames) - before < CHECK_BELOW_FPS * CHECK_EVERY_S
                    and elapsed - checked >= CHECK_EVERY_S):
                checked = elapsed
                box = message_box()
                if box:
                    result["status"], result["message_box"] = "error-box", box
                    break
            if elapsed - max(exe_seen, last_new) >= STUCK_S:
                result["status"] = "stalled"
                break
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
        if len(frames) > (history[-1][1] if history else 0):
            last_new = elapsed
        history.append((elapsed, len(frames)))
    result["seconds_to_game_process"] = exe_seen or None
    result["seconds_to_first_frame"] = seen_at
    result["layer"] = layer
    stats = result["surfaceflinger"] = frame_stats(list(frames))
    result["memory"] = memory
    result["gpu"] = gpu  # [seconds since launch, busy %, clock kHz]
    result["present_ns"] = sorted(frames)  # CLOCK_MONOTONIC, as RADV's [WAIT] log lines
    busy = [b for t, b, _ in gpu if seen_at is not None and t >= seen_at + WARMUP_S]
    if busy:
        result["gpu_busy_pct"] = statistics.mean(busy)
    counts = stats.get("steady_seconds", 0) >= MIN_STEADY_S
    # A run that stopped early or without enough frames has usually stopped at a message box, from
    # the game or from Wine.
    if result.get("status") != "phone-lost" and ("status" in result or not counts):
        shot = run_dir / f"{tag}-{bench}-{driver}-screen.png"
        if screenshot(shot):
            result["screenshot"] = shot.name
    shell(f"am force-stop {APP}")
    killed = logcat_since(since, LOW_MEMORY_KILL)
    if killed:
        result["status"] = "killed-low-memory"
        result["low_memory_kill"] = killed[0].strip()
    result["vulkan_drivers_seen"] = drivers_seen(since, processes)
    result["launch_settings"] = [line.split(": I: ", 1)[-1] for line in
                                 logcat_since(since, LAUNCH_SETTINGS)]
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
    result.setdefault("status", "ran" if counts else "too-short" if "fps_avg" in stats else "no-frames")
    faults_after = gpu_faults()
    for name in faults_before.keys() & faults_after.keys():
        result[f"gpu_{name}"] = faults_after[name] - faults_before[name]
    result["gpu_temp_c_after"] = gpu_temp()
    return result


def summarize(meta: dict, results: list[dict]) -> str:
    out = [f"# Game benchmark run {meta['started']}", ""]
    if meta.get("props"):
        out += ["Android properties set: " + ", ".join(f"`{k}={v}`" for k, v in meta["props"].items()),
                ""]
    out += [
           "Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the "
           f"first {WARMUP_S} s after the first frame. A run counts (`ran`) only with {MIN_STEADY_S} s "
           f"of frames in a row, none more than {STALL_S} s apart; the medians use only those.", "",
           "| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) "
           "| Steady (s) | GPU busy (%) | GPU faults | Driver seen | Least free memory (MB) "
           "| GameNative peak (MB) | Its GPU memory peak (MB) |",
           "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in results:
        s = r.get("surfaceflinger", {})
        fmt = lambda v: f"{v:.1f}" if isinstance(v, (int, float)) else ""  # noqa: E731
        mem = r.get("memory") or [{}]
        least = min((m.get("available_mb", 0) for m in mem), default=0)
        peak = max((m.get("app_rss_mb", 0) + m.get("app_swap_mb", 0) for m in mem), default=0)
        gpu_peak = max((m.get("app_gpu_mb", 0) for m in mem), default=0)
        out.append(f"| {r['bench']} | {r['driver']} | {r['run']} | {r['status']} | "
                   f"{fmt(s.get('fps_avg'))} | {fmt(s.get('fps_1pct_low'))} | "
                   f"{fmt(s.get('frametime_median_ms'))} | {fmt(s.get('steady_seconds'))} | "
                   f"{fmt(r.get('gpu_busy_pct'))} | {r.get('gpu_page_faults', '')} | {', '.join(r.get('vulkan_drivers_seen', [])) or 'none'} | {least} | {peak} | "
                   f"{gpu_peak} |")
    out += ["", "## Medians", ""]
    for bench in dict.fromkeys(r["bench"] for r in results):
        line = []
        for driver in ("stock", "radv"):
            fps = [r["surfaceflinger"]["fps_avg"] for r in results if r["bench"] == bench
                   and r["driver"] == driver and r["status"] == "ran"]
            if fps:
                line.append((driver, statistics.median(fps)))
        text = ", ".join(f"{d} {v:.1f} FPS" for d, v in line)
        if len(line) == 2 and line[0][1] > 0:
            text += f" (radv/stock {line[1][1] / line[0][1]:.2f}x)"
        out.append(f"- **{bench}**: {text}")
    why = {"error-box": "stopped at a message box (an error from the game or from Wine)",
           "stalled": f"no new frame for {STUCK_S} s",
           "no-frames": "no frames from the game",
           "too-short": f"under {MIN_STEADY_S} s of steady frames",
           "killed-low-memory": "Android's low-memory killer stopped GameNative",
           "phone-lost": "the phone dropped off adb (a GPU hang can reboot it)"}
    failed = [r for r in results if r["status"] != "ran"]
    if failed:
        out += ["", "## Runs that didn't count", ""]
        for r in failed:
            shot = f" Screen: `{r['screenshot']}`." if r.get("screenshot") else ""
            out.append(f"- Run {r['run']}, {r['bench']} on {r['driver']}: {why.get(r['status'], r['status'])}."
                       f"{shot}")
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
    ap.add_argument("--shots", default="",
                    help="seconds after launch to save screenshots at, comma-separated")
    ap.add_argument("--seconds", type=int, help="end each run this many seconds after launch")
    ap.add_argument("--prop", action="append", default=[], metavar="NAME=VALUE",
                    help="Android property to set for the session and clear afterwards")
    ap.add_argument("--print-args", action="store_true",
                    help="print the Exec Arguments for each container and exit")
    args = ap.parse_args()
    benches, drivers = args.bench.split(","), args.drivers.split(",")
    props = dict(p.split("=", 1) for p in args.prop)

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
                "drivers": drivers, "runs": args.runs, "gpu_temp_c_start": baseline,
                "props": props}
        shell(f"setprop debug.radv_xclipse_log {1 if args.radv_log else 0}")
        for name, value in props.items():
            shell(f"setprop {name} '{value}'")
            if shell(f"getprop {name}") != value:
                sys.exit(f"couldn't set {name}")
        results = run_all(args, benches, drivers, run_dir, app_user(), baseline)
    finally:
        shell(f"settings put global stay_on_while_plugged_in {stay_on}")
        shell("setprop debug.radv_xclipse_log 0")
        for name in props:
            shell(f"setprop {name} ''")

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
    try:
        for n in range(1, args.runs + 1):
            for bench in benches:
                for driver in (drivers if n % 2 else list(reversed(drivers))):
                    print(f"run {n} {bench:<12} {driver:<5}", end=" ", flush=True)
                    r = run_once(bench, driver, run_dir, f"run{n}", user,
                                 [int(t) for t in args.shots.split(",") if t], args.seconds)
                    r["run"] = n
                    s = r["surfaceflinger"]
                    print(f"{r['status']} {s.get('fps_avg', 0):.1f} fps over "
                          f"{s.get('seconds', 0):.0f} s, {r.get('gpu_page_faults', '?')} GPU faults"
                          + (f", screen {r['screenshot']}" if r.get("screenshot") else ""), flush=True)
                    results.append(r)
                    if r["status"] == "phone-lost":
                        print("lost the phone; stopping")
                        return results
                    cool_down(target=baseline + 3)
    except KeyboardInterrupt:
        # Ctrl+C keeps the finished runs; the one in progress is dropped.
        print("\ninterrupted; saving the finished runs", flush=True)
        shell(f"am force-stop {APP}")
    return results


if __name__ == "__main__":
    main()
