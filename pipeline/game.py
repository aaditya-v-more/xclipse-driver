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
Unexpected process exits and Android crashes also invalidate a run, even after sustained frames.
Two consecutive failures skip the remaining attempts for that benchmark/driver. Finished results
are saved after every run, before cooling down; screenshots and logcat remain beside the report.

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
CRASH_MARKERS = r"Fatal signal|FATAL EXCEPTION|Unhandled exception|ANR in "
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


RING = 128  # frame records SurfaceFlinger keeps per layer


def present_times(layer: str) -> tuple[list[int], bool, int]:
    """Actual present timestamps (ns) of the layer's last frames; whether SurfaceFlinger's ring of
    RING records was full, so frames older than its oldest may be gone; and the compositor's
    refresh period in ns (its first output line)."""
    out = shell(f"dumpsys SurfaceFlinger --latency '{layer}'", 30).splitlines()
    period = int(out[0]) if out and out[0].strip().isdigit() else 0
    rows = [line.split() for line in out[1:]]
    rows = [p for p in rows if len(p) >= 2 and p[1].isdigit()]
    times = [int(p[1]) for p in rows if 0 < int(p[1]) < (1 << 62)]
    return times, len(rows) >= RING, period


def frame_stats(times_ns: list[int], holes: list[tuple[int, int]] = ()) -> dict:
    """Frame statistics from present timestamps. `holes` are (last seen, first seen again) pairs
    where SurfaceFlinger's ring overflowed between two reads: frames were presented in between but
    their times are gone, so those intervals count for nothing rather than as one long frame."""
    ts = sorted(set(times_ns))
    # GameNative's SurfaceView exists from its "Booting..." screen on and presents a frame now and
    # then while Wine starts. The game is rendering once 10 frames land within 2 s.
    rendering = next((t for i, t in enumerate(ts[:-10]) if ts[i + 10] - t < 2_000_000_000), None)
    if rendering is None:
        return {"frames": len(ts)}
    ts = [t for t in ts if t >= rendering + WARMUP_S * 1_000_000_000]
    if len(ts) < 10:
        return {"frames": len(ts)}
    def lost(a: int, b: int) -> bool:
        return any(a <= h0 and h1 <= b for h0, h1 in holes)
    pairs = list(zip(ts, ts[1:]))
    intervals = [(b - a) / 1e6 for a, b in pairs if not lost(a, b)]
    if not intervals:
        return {"frames": len(ts)}
    worst = sorted(intervals)[int(len(intervals) * 0.99):]
    steady, start = 0, ts[0]
    for a, b in pairs:
        if b - a > STALL_S * 1_000_000_000 and not lost(a, b):
            start = b
        steady = max(steady, b - start)
    covered_s = sum(intervals) / 1e3
    return {
        "frames": len(ts),
        "seconds": (ts[-1] - ts[0]) / 1e9,
        "steady_seconds": steady / 1e9,
        "fps_avg": len(intervals) / covered_s,
        "frametime_median_ms": statistics.median(intervals),
        "fps_1pct_low": 1000.0 / statistics.mean(worst) if worst else None,
        "sampling_gaps": sum(1 for a, b in pairs if lost(a, b)),
        "covered_share": covered_s / ((ts[-1] - ts[0]) / 1e9),
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


LOGCAT: dict = {}  # the run's log capture: "path", and "proc" and "file" while it runs


def logcat_start(path: Path) -> None:
    """Streams the phone log into `path` from now on. Reading the ring buffer after a five-minute
    Wine session loses its start, and with it the lines that say which driver loaded."""
    logcat_stop()
    LOGCAT["path"] = path
    LOGCAT["file"] = open(path, "wb")
    LOGCAT["proc"] = subprocess.Popen([ADB, "logcat", "-v", "threadtime", "-b", "main,system",
                                       "-T", "1"], stdout=LOGCAT["file"], stderr=subprocess.DEVNULL)


def logcat_stop() -> None:
    proc = LOGCAT.pop("proc", None)
    if proc:
        time.sleep(1)  # let the last lines arrive
        proc.terminate()
        try:
            proc.wait(10)
        except subprocess.TimeoutExpired:
            proc.kill()
    f = LOGCAT.pop("file", None)
    if f:
        f.close()


def logcat_since(since: str, pattern: str) -> list[str]:
    """Log lines since `since` (the phone's 'MM-DD hh:mm:ss.mmm') that match an extended regex;
    from the run's capture file when there is one."""
    path = LOGCAT.get("path")
    if path and Path(path).exists():
        rx = re.compile(pattern)
        lines = [ln for ln in Path(path).read_text(encoding="utf-8", errors="replace").splitlines()
                 if rx.search(ln)]
    else:
        out = shell(f"logcat -d -b main,system -T '{since}' | grep -E \"{pattern}\"", 60)
        lines = out.splitlines()
    # adbd logs each shell command it runs, this grep included, so its lines would always match.
    return [line for line in lines if line.strip() and "service requested" not in line]


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


def is_game_process(name: str, exe: str) -> bool:
    """Android can report a Wine path, a basename, or a 15-character process name."""
    name = re.split(r"[\\/]", name.strip())[-1].lower()
    exe = exe.lower()
    return name == exe or (len(name) == 15 and exe.startswith(name))


def runtime_crash(since: str, processes: dict[str, str]) -> list[str]:
    """Fatal log lines belonging to GameNative/Wine, never another Android app."""
    evidence = []
    for line in logcat_since(since, CRASH_MARKERS):
        fields = line.split()
        if ((len(fields) > 2 and fields[2] in processes)
                or re.search(rf"ANR in {re.escape(APP)}(?:\s|$)", line)):
            evidence.append(line.strip())
    return evidence


CPU_POLICIES = (0, 4, 7, 9)  # the Exynos 2400e's clusters: 4x A520, 3x A720, 2x A720, 1x X4


def gpu_sample() -> tuple | None:
    """How busy the GPU was over the kernel's last DVFS window (8 ms), in percent; its clock in kHz;
    its temperature in C; then each CPU cluster's clock in kHz. Sampled every loop turn, the busy
    share averages out to the share of time the GPU had work."""
    cpu = " ".join(f"/sys/devices/system/cpu/cpufreq/policy{p}/scaling_cur_freq" for p in CPU_POLICIES)
    try:
        v = shell(f"cat /sys/kernel/gpu/gpu_busy /sys/kernel/gpu/gpu_clock /sys/kernel/gpu/gpu_tmu {cpu}",
                  15).split()
        return (int(v[0]), int(v[1]), float(v[2]), *(int(x) for x in v[3:3 + len(CPU_POLICIES)]))
    except (ValueError, IndexError):
        return None


def battery_sample() -> dict:
    """Battery level, voltage (mV), charge counter (uAh) and temperature (C) from the battery
    service. The phone charges over USB during runs, so the counter's slope is net charging, not the
    system's draw; the temperature is a second thermal reading next to the GPU's."""
    out = shell("dumpsys battery", 15)
    keys = {"level": "level", "voltage": "voltage_mv", "Charge counter": "charge_uah",
            "temperature": "temperature_dc"}
    sample = {}
    for line in out.splitlines():
        k, _, v = line.strip().partition(": ")
        if k in keys and v.strip().lstrip("-").isdigit():
            sample["battery_" + keys[k]] = int(v)
    return sample


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
    logcat_start(run_dir / f"{tag}-{bench}-{driver}-logcat.txt")
    result["logcat_file"] = f"{tag}-{bench}-{driver}-logcat.txt"
    memory, processes, gpu = [], {}, []
    started = time.monotonic()
    shell(f"am start -n {APP}/.MainActivity -a {APP}.LAUNCH_GAME --ei app_id {app_id} "
          f"--es game_source CUSTOM_GAME")

    frames, layer, seen_at, gone_for, sampled = set(), None, None, 0, -MEMORY_EVERY_S
    holes, last_seen, periods = [], 0, Counter()  # lost-frame windows; newest frame; refresh periods
    exe_seen = exe_gone = app_gone = 0
    app_seen = False
    history, last_new, checked = [], 0.0, 0.0  # (seconds, frames so far); when the count last grew
    shots_left = sorted(shots)
    exe = cfg["exe"].lower()
    while time.monotonic() - started < (seconds or cfg["seconds"]):
        time.sleep(0.5)  # the adb calls below take the rest of the second; the ring holds ~1-2 s
        if phone_state() != "device":
            result["status"] = "phone-lost"
            break
        elapsed = time.monotonic() - started
        # New DXVK paths can fault continuously while the compositor still presents a few
        # frames. Check the kernel counters during the run, not just when its timer expires.
        faults_now = gpu_faults()
        deltas = {f"gpu_{name}": max(0, faults_now[name] - faults_before[name])
                  for name in faults_before.keys() & faults_now.keys()}
        if any(deltas.values()):
            result.update(deltas)
            result["status"] = "gpu-fault"
            result["gpu_fault_detected_s"] = round(elapsed, 1)
            shell(f"am force-stop {APP}")
            # Do not delay stopping for screenshots/UI dumps, or capture the home screen
            # afterwards. The log and any screenshots already taken remain available.
            break
        while shots_left and elapsed >= shots_left[0]:
            shot = run_dir / f"{tag}-{bench}-{driver}-{shots_left.pop(0)}s.png"
            if screenshot(shot):
                result.setdefault("screens", []).append(shot.name)
        sample = gpu_sample()
        if sample:
            gpu.append((round(elapsed, 1), *sample))
        if elapsed - sampled >= MEMORY_EVERY_S:
            now = {}
            memory.append({"t": round(elapsed), **memory_sample(user, now), **battery_sample()})
            processes.update(now)
            sampled = elapsed
            crash = runtime_crash(since, processes)
            if crash:
                result["status"], result["crash_log"] = "crashed", crash
                break
            if APP in now.values():
                app_seen, app_gone = True, 0
            else:
                app_gone += 1
                if app_gone >= 2 and (app_seen or elapsed >= 20):
                    result["status"] = "app-exited" if app_seen else "launch-failed"
                    result["app_exited_s"] = round(elapsed)
                    break
            # GameNative keeps showing Wine's desktop after the game exits, so the game's own
            # process tells when it's over. An exit alone is not proof of benchmark completion.
            if any(is_game_process(name, exe) for name in now.values()):
                exe_seen, exe_gone = exe_seen or round(elapsed), 0
            elif exe_seen:
                exe_gone += 1
                if exe_gone >= 2:
                    result["status"] = "game-exited"
                    result["game_exited_s"] = round(elapsed)
                    break
        check_start = exe_seen or seen_at
        if check_start is not None and elapsed - check_start >= CHECK_AFTER_S:
            before = next((n for t, n in history if t >= elapsed - CHECK_EVERY_S), len(frames))
            if (len(frames) - before < CHECK_BELOW_FPS * CHECK_EVERY_S
                    and elapsed - checked >= CHECK_EVERY_S):
                checked = elapsed
                box = message_box()
                if box:
                    result["status"], result["message_box"] = "error-box", box
                    break
            if elapsed - max(check_start, last_new) >= STUCK_S:
                result["status"] = "stalled"
                break
        layers = game_layers()
        if not layers:
            gone_for = gone_for + 1 if seen_at else 0
            if seen_at and gone_for >= 5:
                result["status"] = "surface-lost"
                break
            continue
        gone_for = 0
        new, full, period = present_times(layer) if layer in layers else ([], False, 0)
        if not new:
            for name in layers:
                new, full, period = present_times(name)
                if new:
                    layer = name
                    break
        if new:
            if seen_at is None:
                seen_at = time.monotonic() - started
            if period:
                periods[period] += 1
            lo, hi = min(new), max(new)
            # A full ring whose oldest frame is newer than the newest one seen means frames were
            # presented in between and their times are gone.
            if full and frames and lo > last_seen:
                holes.append((last_seen, lo))
            last_seen = max(last_seen, hi)
        frames.update(new)
        if len(frames) > (history[-1][1] if history else 0):
            last_new = elapsed
        history.append((elapsed, len(frames)))
    result["seconds_to_game_process"] = exe_seen or None
    result["seconds_to_first_frame"] = seen_at
    result["layer"] = layer
    stats = result["surfaceflinger"] = frame_stats(list(frames), holes)
    result["refresh_period_ns"] = periods.most_common(1)[0][0] if periods else None
    result["memory"] = memory
    result["gpu"] = gpu  # [seconds since launch, busy %, clock kHz, temp C, CPU cluster kHz...]
    result["present_ns"] = sorted(frames)  # CLOCK_MONOTONIC, as RADV's [WAIT] log lines
    after_warmup = [s for s in gpu if seen_at is not None and s[0] >= seen_at + WARMUP_S]
    if after_warmup:
        result["gpu_busy_pct"] = statistics.mean(s[1] for s in after_warmup)
        result["gpu_clock_mhz"] = statistics.mean(s[2] for s in after_warmup) / 1000
        result["gpu_temp_c_mean"] = statistics.mean(s[3] for s in after_warmup)
        result["cpu_clock_mhz"] = [statistics.mean(s[4 + i] for s in after_warmup) / 1000
                                   for i in range(len(CPU_POLICIES))]
    if result.get("status") not in {"phone-lost", "gpu-fault"}:
        crash = runtime_crash(since, processes)
        if crash:
            result["status"], result["crash_log"] = "crashed", crash
    counts = stats.get("steady_seconds", 0) >= MIN_STEADY_S
    if not exe_seen and "status" not in result:
        result["status"] = "launch-failed"
    # A run that stopped early or without enough frames has usually stopped at a message box, from
    # the game or from Wine.
    if result.get("status") not in {"phone-lost", "gpu-fault"} and ("status" in result or not counts):
        if "message_box" not in result:
            box = message_box()
            if box:
                result["status"], result["message_box"] = "error-box", box
        shot = run_dir / f"{tag}-{bench}-{driver}-screen.png"
        if screenshot(shot):
            result["screenshot"] = shot.name
    shell(f"am force-stop {APP}")
    logcat_stop()
    killed = logcat_since(since, LOW_MEMORY_KILL)
    if killed:
        result["status"] = "killed-low-memory"
        result["low_memory_kill"] = killed[0].strip()
    result["vulkan_drivers_seen"] = drivers_seen(since, processes)
    result["launch_settings"] = [line.split(": I: ", 1)[-1] for line in
                                 logcat_since(since, LAUNCH_SETTINGS)]
    result["processes"] = sorted(set(processes.values()))
    LOGCAT.pop("path", None)

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
        key = f"gpu_{name}"
        result[key] = max(result.get(key, 0), faults_after[name] - faults_before[name])
    if result.get("gpu_page_faults", 0) > 0 or result.get("gpu_resets", 0) > 0:
        result["status"] = "gpu-fault"
    result["gpu_temp_c_after"] = gpu_temp()
    return result


def summarize(meta: dict, results: list[dict]) -> str:
    out = [f"# Game benchmark run {meta['started']}", ""]
    if meta.get("radv_log"):
        out += ["RADV diagnostic logging enabled; this adds CPU work.", ""]
    for note in meta.get("notes", []):
        out += [note, ""]
    if meta.get("props"):
        out += ["Android properties set: " + ", ".join(f"`{k}={v}`" for k, v in meta["props"].items()),
                ""]
    out += [
           "Frames counted from SurfaceFlinger (what GameNative presents on screen), skipping the "
           f"first {WARMUP_S} s after the first frame. A run counts (`ran`) only with {MIN_STEADY_S} s "
           f"of frames in a row, none more than {STALL_S} s apart, with no detected failure or "
           "early process exit; the medians use only those. "
           "\"Lost windows\" counts gaps where SurfaceFlinger's 128-frame ring overflowed between two "
           "reads; those intervals are left out of every figure. Hz is the compositor's refresh rate "
           "during the run. GPU MHz, GPU C and X4 MHz (the big core's clock) are means after warm-up.",
           "",
           "| Benchmark | Driver | Run | Status | Avg FPS | 1% low FPS | Median frame time (ms) "
           "| Steady (s) | Hz | Lost windows | GPU busy (%) | GPU MHz | GPU C | X4 MHz | GPU faults "
           "| Driver seen | Least free memory (MB) | GameNative peak (MB) | Its GPU memory peak (MB) |",
           "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in results:
        s = r.get("surfaceflinger", {})
        fmt = lambda v: f"{v:.1f}" if isinstance(v, (int, float)) else ""  # noqa: E731
        mem = r.get("memory") or [{}]
        least = min((m.get("available_mb", 0) for m in mem), default=0)
        peak = max((m.get("app_rss_mb", 0) + m.get("app_swap_mb", 0) for m in mem), default=0)
        gpu_peak = max((m.get("app_gpu_mb", 0) for m in mem), default=0)
        hz = round(1e9 / r["refresh_period_ns"]) if r.get("refresh_period_ns") else ""
        cpu = r.get("cpu_clock_mhz") or []
        out.append(f"| {r['bench']} | {r['driver']} | {r['run']} | {r['status']} | "
                   f"{fmt(s.get('fps_avg'))} | {fmt(s.get('fps_1pct_low'))} | "
                   f"{fmt(s.get('frametime_median_ms'))} | {fmt(s.get('steady_seconds'))} | {hz} | "
                   f"{s.get('sampling_gaps', '')} | {fmt(r.get('gpu_busy_pct'))} | "
                   f"{fmt(r.get('gpu_clock_mhz'))} | {fmt(r.get('gpu_temp_c_mean'))} | "
                   f"{fmt(cpu[-1]) if cpu else ''} | {r.get('gpu_page_faults', '')} | "
                   f"{', '.join(r.get('vulkan_drivers_seen', [])) or 'none'} | {least} | {peak} | "
                   f"{gpu_peak} |")
    out += ["", "## Medians", ""]
    for bench in dict.fromkeys(r["bench"] for r in results):
        line = []
        for driver in ("stock", "radv"):
            fps = [r["surfaceflinger"]["fps_avg"] for r in results if r["bench"] == bench
                   and r["driver"] == driver and r["status"] == "ran"]
            if fps:
                line.append((driver, statistics.median(fps)))
        text = ", ".join(f"{d} {v:.1f} FPS" for d, v in line) or "no valid runs"
        if len(line) == 2 and line[0][1] > 0:
            text += f" (radv/stock {line[1][1] / line[0][1]:.2f}x)"
        out.append(f"- **{bench}**: {text}")
    why = {"error-box": "stopped at a message box (an error from the game or from Wine)",
           "stalled": f"no new frame for {STUCK_S} s",
           "no-frames": "no frames from the game",
           "too-short": f"under {MIN_STEADY_S} s of steady frames",
           "killed-low-memory": "Android's low-memory killer stopped GameNative",
           "crashed": "a fatal GameNative/Wine error was captured in logcat",
           "app-exited": "GameNative exited before the requested measurement ended",
           "game-exited": "the game exited; benchmark completion was not verified",
           "surface-lost": "GameNative's rendering surface disappeared before the run ended",
           "launch-failed": "GameNative or the game's executable did not start",
           "gpu-fault": "the GPU reported a page fault or reset",
           "skipped": "remaining attempt skipped after repeated failures or a GPU fault",
           "phone-lost": "the phone dropped off adb (a GPU hang can reboot it)"}
    failed = [r for r in results if r["status"] != "ran"]
    if failed:
        out += ["", "## Runs that didn't count", ""]
        for r in failed:
            shot = f" Screen: `{r['screenshot']}`." if r.get("screenshot") else ""
            out.append(f"- Run {r['run']}, {r['bench']} on {r['driver']}: {why.get(r['status'], r['status'])}."
                       f"{shot}")
    return "\n".join(out) + "\n"


def write_report(run_dir: Path, meta: dict, results: list[dict]) -> None:
    """Checkpoint completed runs so a later interruption cannot lose them."""
    for name, data in (("results.json", json.dumps({"meta": meta, "results": results}, indent=2)),
                       ("summary.md", summarize(meta, results))):
        temporary = run_dir / (name + ".tmp")
        temporary.write_text(data, encoding="utf-8", newline="\n")
        temporary.replace(run_dir / name)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bench", default="gravitymark,heaven")
    ap.add_argument("--drivers", default="stock,radv")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--max-failures", type=int, default=2,
                    help="skip a benchmark/driver after this many consecutive failures (default 2)")
    ap.add_argument("--radv-log", action="store_true",
                    help="turn on RADV's diagnostic log (debug.radv_xclipse_log=1) so radv runs "
                         "prove our driver loaded; it adds CPU work, so leave it off for timing")
    ap.add_argument("--note", action="append", default=[],
                    help="record an experiment setting or validation note in both reports; repeatable")
    ap.add_argument("--shots", default="",
                    help="seconds after launch to save screenshots at, comma-separated")
    ap.add_argument("--seconds", type=int, help="end each run this many seconds after launch")
    ap.add_argument("--prop", action="append", default=[], metavar="NAME=VALUE",
                    help="Android property to set for the session and clear afterwards")
    ap.add_argument("--print-args", action="store_true",
                    help="print the Exec Arguments for each container and exit")
    ap.add_argument("--hz", type=int, default=120,
                    help="pin the display to this refresh rate for the session (0 leaves it alone); "
                         "GameNative presents with vsync, so the panel's rate caps the FPS")
    args = ap.parse_args()
    if args.runs < 1 or args.max_failures < 1:
        ap.error("--runs and --max-failures must be positive")
    benches, drivers = args.bench.split(","), args.drivers.split(",")
    if any("=" not in p for p in args.prop):
        ap.error("--prop takes NAME=VALUE")
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
    stay_on = shell("settings get global stay_on_while_plugged_in").strip()
    shell("svc power stayon usb")
    # The panel idles at 60 Hz and SurfaceFlinger paces GameNative to it; min/peak_refresh_rate
    # pin it (verified: the compositor period goes from 16.67 to 8.33 ms). Both are restored.
    refresh = {name: shell(f"settings get system {name}").strip()
               for name in ("min_refresh_rate", "peak_refresh_rate")}
    if args.hz:
        for name in refresh:
            shell(f"settings put system {name} {args.hz}.0")
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
                "props": props, "display_hz": args.hz or None,
                "radv_log": args.radv_log, "notes": args.note,
                "max_consecutive_failures": args.max_failures,
                "battery_start": battery_sample()}
        shell(f"setprop debug.radv_xclipse_log {1 if args.radv_log else 0}")
        for name, value in props.items():
            shell(f"setprop {name} '{value}'")
            if shell(f"getprop {name}") != value:
                sys.exit(f"couldn't set {name}")
        results = run_all(args, benches, drivers, run_dir, app_user(), baseline, meta)
    finally:
        shell(f"am force-stop {APP}")
        if stay_on.isdigit():  # "null" when unset, "" when adb hiccuped: then leave it alone
            shell(f"settings put global stay_on_while_plugged_in {stay_on}")
        else:
            print(f"warning: stay_on_while_plugged_in read back as {stay_on!r}; not restored")
        for name, value in refresh.items():
            if args.hz:
                shell(f"settings delete system {name}" if value in ("", "null")
                      else f"settings put system {name} {value}")
        logcat_stop()
        shell("setprop debug.radv_xclipse_log 0")
        for name in props:
            shell(f"setprop {name} ''")

    write_report(run_dir, meta, results)
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
            baseline: float, meta: dict) -> list[dict]:
    results = []
    failures, blocked = Counter(), set()
    try:
        for n in range(1, args.runs + 1):
            for bench in benches:
                for driver in (drivers if n % 2 else list(reversed(drivers))):
                    key = bench, driver
                    if key in blocked or failures[key] >= args.max_failures:
                        print(f"run {n} {bench:<12} {driver:<5} skipped after failures", flush=True)
                        results.append({"bench": bench, "driver": driver, "run": n,
                                        "status": "skipped", "surfaceflinger": {}})
                        write_report(run_dir, meta, results)
                        continue
                    print(f"run {n} {bench:<12} {driver:<5}", end=" ", flush=True)
                    r = run_once(bench, driver, run_dir, f"run{n}", user,
                                 [int(t) for t in args.shots.split(",") if t], args.seconds)
                    r["run"] = n
                    s = r["surfaceflinger"]
                    print(f"{r['status']} {s.get('fps_avg', 0):.1f} fps over "
                          f"{s.get('seconds', 0):.0f} s, {r.get('gpu_page_faults', '?')} GPU faults"
                          + (f", screen {r['screenshot']}" if r.get("screenshot") else ""), flush=True)
                    results.append(r)
                    failures[key] = 0 if r["status"] == "ran" else failures[key] + 1
                    if r.get("gpu_page_faults", 0) > 0 or r.get("gpu_resets", 0) > 0:
                        blocked.add(key)
                    write_report(run_dir, meta, results)
                    if r["status"] == "phone-lost":
                        print("lost the phone; stopping")
                        return results
                    cool_down(target=baseline + 3)
    except KeyboardInterrupt:
        # Ctrl+C keeps the finished runs; the one in progress is dropped.
        print("\ninterrupted; saving the finished runs", flush=True)
        shell(f"am force-stop {APP}")
    finally:
        write_report(run_dir, meta, results)
    return results


if __name__ == "__main__":
    main()
