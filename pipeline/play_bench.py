#!/usr/bin/env python3
"""A/B of driver settings in a game's first playable scene on the phone.

Heaven and GravityMark are synthetic. Life is Strange: True Colors's first scene after Continue is
GPU-bound (about 26 FPS at 1280x720 with 80% resolution scaling) and its camera holds still once
the opening cutscene is skipped, so the frames GameNative presents compare run to run. Each run:
`game_input.py play GAME --restart` launches the game cold and gets it into the scene; then for
--seconds the present timestamps of GameNative's SurfaceView are read about once a second, with the
GPU's busy share, clocks and temperature (`game.py`'s helpers), and memory, battery and thermal
state every 5 s; a screenshot ends the run, for SSIM against the baseline variant's screenshots.

A variant is a set of Android properties, the way RADV reads its options under GameNative
(`debug.radv_perftest`, `debug.radv_debug`, `debug.radv_xclipse_*`), and optionally container
settings applied with `gn_driver.py apply`: `driver:ENTRY` (the Graphics Driver Version, such as
`System` for Samsung's driver), `env:NAME=VALUE` (an environment variable, such as `DXVK_CONFIG`)
and `set:LABEL=VALUE` (a dropdown, such as `BCn Emulation`). Every property any variant names is
cleared before each run, so runs can't inherit a setting; a container setting can't be cleared, so
when any variant names one, the first variant must give its value too. Variants take turns, in
reverse order every other round, and the GPU cools to --max-temp before each run, so heat and memory
state affect them alike. A run the low-memory killer ends, one in which the game never reaches the
scene, and one with a GPU fault are reported as such, not as results.

usage: python pipeline/play_bench.py [--game LifeIsStrange3] [--rounds 3] [--seconds 75]
           [--variant NAME[=ITEM[,ITEM...]]]... [--no-kill] [--max-temp 42] [--max-skin 38]

ITEM is debug.<prop>=<value>, driver:<entry>, env:<NAME>=<VALUE> or set:<LABEL>=<VALUE>.
Without --variant it compares `default` against `nggc=debug.radv_perftest=nggc` and
`filteremu0=debug.radv_xclipse_filteremu=0`. The first variant is the baseline for SSIM. Writes
results/<time>-play/results.json and summary.md (tracked); screenshots and the per-run present
times stay local. Nobody may touch the phone while it runs.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import statistics
import subprocess
import sys
import tempfile
import time
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ssim as ssim_mod  # noqa: E402
from game import (APP, CPU_POLICIES, LAUNCH_SETTINGS, LOW_MEMORY_KILL, MEMORY_EVERY_S,  # noqa: E402
                  MIN_STEADY_S, WARMUP_S, app_user, battery_sample, cool_down, drivers_seen,
                  frame_stats, game_layers, gpu_faults, gpu_sample, gpu_temp, logcat_since,
                  memory_sample, present_times, screenshot, thermal_sample)
from run import RESULTS, STAY_ON_CMD, phone_context, phone_state, select_phone, shell  # noqa: E402

DEFAULT_VARIANTS = ["default", "nggc=debug.radv_perftest=nggc",
                    "filteremu0=debug.radv_xclipse_filteremu=0"]
MEASURE_S = 60  # frames counted after frame_stats' warm-up; --seconds must leave room for both
PLAY_LIMIT_S = 600  # launch, intros, and the Continue load take 110-200 s
# GameNative draws its performance overlay (FPS, CPU, GPU, RAM, BAT, PWR) along the top of the
# picture; its numbers change every run, so SSIM skips that band.
OVERLAY_SHARE = 0.10
VARIANT_METRICS = (("fps_avg", "FPS avg", 1), ("fps_1pct_low", "1% low", 1),
                   ("frametime_p95_ms", "p95 ms", 1), ("frametime_p99_ms", "p99 ms", 1),
                   ("stutters_per_min", "stutters/min", 1), ("gpu_busy_pct", "GPU busy %", 0),
                   ("gpu_clock_mhz", "GPU MHz", 0), ("cpu_x4_mhz", "X4 MHz", 0),
                   ("app_gpu_mb_max", "GPU MB max", 0), ("app_rss_mb_max", "RSS MB max", 0),
                   ("available_mb_min", "avail MB min", 0), ("skin_c_max", "skin C max", 1))


def parse_variant(text: str) -> tuple[str, dict[str, str]]:
    """Items keep their prefix as part of the key: `debug.radv_perftest`, `driver:`, `env:NAME`,
    `set:LABEL`; `props_of` and `container_of` split them."""
    name, _, rest = text.partition("=")
    items = {}
    for item in filter(None, rest.split(",")):
        if item.startswith("driver:"):
            items["driver:"] = item[len("driver:"):]
            continue
        key, sep, value = item.partition("=")
        if not sep or not (key.startswith("debug.") or key.startswith(("env:", "set:"))):
            sys.exit(f"variant {name}: expected debug.<prop>=<value>, driver:<entry>, "
                     f"env:<NAME>=<VALUE> or set:<LABEL>=<VALUE>, got {item!r}")
        items[key] = value
    return name, items


def props_of(items: dict[str, str]) -> dict[str, str]:
    return {k: v for k, v in items.items() if k.startswith("debug.")}


def container_of(items: dict[str, str]) -> dict[str, str]:
    return {k: v for k, v in items.items() if not k.startswith("debug.")}


APPLIED: dict = {}  # the container settings last applied, to skip a no-op editor session


def apply_container(game: str, settings: dict[str, str]) -> None:
    """Sets the variant's container settings through GameNative's editor (gn_driver.py apply reads
    every change back and fails loudly), unless they are already in place."""
    if not settings or settings == APPLIED:
        return
    cmd = [sys.executable, str(HERE / "gn_driver.py"), "apply", game]
    for key, value in settings.items():
        if key == "driver:":
            cmd += ["--driver", value]
        elif key.startswith("env:"):
            cmd += ["--env", f"{key[4:]}={value}"]
        else:
            cmd += ["--set", f"{key[4:]}={value}"]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    if proc.returncode:
        sys.exit(f"gn_driver.py apply failed:\n{proc.stdout}{proc.stderr}")
    APPLIED.clear()
    APPLIED.update(settings)


def set_props(props: dict[str, str], known: set[str]) -> dict[str, str]:
    """Clears every property in `known`, sets `props`, and returns what the phone reads back. RADV
    reads a property through os_get_option, which takes an empty value as unset."""
    if not known:
        return {}
    cmd = [f"setprop {key} ''" for key in sorted(known - props.keys())]
    cmd += [f"setprop {key} '{value}'" for key, value in props.items()]
    shell("; ".join(cmd))
    out = shell("; ".join(f"echo {key}=$(getprop {key})" for key in sorted(known)))
    read = dict(line.split("=", 1) for line in out.splitlines() if "=" in line)
    for key in known:
        if read.get(key, "") != props.get(key, ""):
            sys.exit(f"{key} reads {read.get(key)!r} after setting {props.get(key, '')!r}")
    return read


def play(game: str, no_kill: bool) -> tuple[int, list[str]]:
    """Runs game_input.py play and returns its exit code and output lines."""
    cmd = [sys.executable, str(HERE / "game_input.py"), "play", game, "--restart"]
    if no_kill:
        cmd.append("--no-kill")
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=PLAY_LIMIT_S)
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
        return -1, out.splitlines() + [f"play did not finish within {PLAY_LIMIT_S} s"]
    return proc.returncode, (proc.stdout + proc.stderr).splitlines()


def run_once(game: str, name: str, items: dict[str, str], known: set[str], run_dir: Path,
             tag: str, user: str, args) -> dict:
    props = props_of(items)
    result = {"tag": tag, "variant": name, "props": props, "container": container_of(items)}
    result["props_read"] = set_props(props, known)
    shell(f"am force-stop {APP}")  # every run starts the game cold
    apply_container(game, container_of(items))
    time.sleep(2)
    result["gpu_temp_c_before"] = (cool_down(args.max_temp, args.cool_limit, args.max_skin)
                                   if args.max_temp else gpu_temp())
    result.update({f"{k}_before": v for k, v in thermal_sample().items()})
    result["memory_before"] = memory_sample(user)
    faults_before = gpu_faults()
    since = shell("date '+%m-%d %H:%M:%S.000'")
    t0 = time.monotonic()
    code, lines = play(game, args.no_kill)
    result["seconds_to_play"] = round(time.monotonic() - t0, 1)
    result["play_log"] = [ln for ln in lines if ln.strip()][-12:]
    processes: dict[str, str] = {}
    if code:
        result["status"] = "play-failed"
        shot = run_dir / f"{tag}-failed.png"
        if screenshot(shot):
            result["screenshot"] = shot.name
    else:
        memory, gpu = [], []
        frames: set[int] = set()
        holes: list[tuple[int, int]] = []
        lags: list[float] = []
        layer, last_seen, sampled = None, 0, -MEMORY_EVERY_S
        periods: Counter = Counter()
        started = time.monotonic()
        while time.monotonic() - started < args.seconds:
            time.sleep(0.5)
            if phone_state() != "device":
                result["status"] = "phone-lost"
                break
            elapsed = time.monotonic() - started
            faults_now = gpu_faults()
            if any(faults_now.get(k, 0) > faults_before.get(k, 0) for k in faults_before):
                result["status"] = "gpu-fault"
                result["gpu_fault_detected_s"] = round(elapsed, 1)
                break
            sample = gpu_sample()
            if sample:
                gpu.append((round(elapsed, 1), *sample))
            if elapsed - sampled >= MEMORY_EVERY_S:
                now: dict[str, str] = {}
                memory.append({"t": round(elapsed), **memory_sample(user, now), **battery_sample(),
                               **thermal_sample()})
                processes.update(now)
                sampled = elapsed
                if APP not in now.values():
                    result["status"] = "app-exited"
                    result["app_exited_s"] = round(elapsed)
                    break
            layers = game_layers()
            new, period = [], 0
            if layer in layers:
                new, _, period = present_times(layer)
            if not new:
                for candidate in layers:
                    new, _, period = present_times(candidate)
                    if new:
                        layer = candidate
                        break
            if new:
                if period:
                    periods[period] += 1
                lo, hi = min(new), max(new)
                lags.append(time.monotonic() - started - hi / 1e9)
                if frames and lo > last_seen:
                    holes.append((last_seen, lo))
                last_seen = max(last_seen, hi)
                frames.update(new)
        shot = run_dir / f"{tag}.png"
        if screenshot(shot):
            result["screenshot"] = shot.name
        stats = result["surfaceflinger"] = frame_stats(sorted(frames), holes, MEASURE_S)
        result["refresh_period_ns"] = periods.most_common(1)[0][0] if periods else None
        result["layer"] = layer
        result["memory"] = memory
        result["gpu"] = gpu  # [seconds since capture start, busy %, clock kHz, temp C, CPU kHz...]
        (run_dir / f"{tag}-present.json").write_text(json.dumps({"present_ns": sorted(frames),
                                                                 "holes_ns": holes}))
        rendering = stats.get("rendering_ns")
        begin = rendering / 1e9 + min(lags) if rendering is not None and lags else None
        if begin is not None:
            window = [s for s in gpu if begin + WARMUP_S <= s[0] <= begin + WARMUP_S + MEASURE_S]
            if window:
                result["gpu_busy_pct"] = statistics.mean(s[1] for s in window)
                result["gpu_clock_mhz"] = statistics.mean(s[2] for s in window) / 1000
                result["gpu_temp_c_mean"] = statistics.mean(s[3] for s in window)
                result["cpu_clock_mhz"] = [statistics.mean(s[4 + i] for s in window) / 1000
                                           for i in range(len(CPU_POLICIES))]
                result["cpu_x4_mhz"] = result["cpu_clock_mhz"][-1]
        if memory:
            for key, pick in (("app_gpu_mb", max), ("app_rss_mb", max), ("app_swap_mb", max),
                              ("available_mb", min), ("skin_c", max)):
                values = [m[key] for m in memory if m.get(key) is not None]
                if values:
                    result[f"{key}_{pick.__name__}"] = pick(values)
    shell(f"am force-stop {APP}")
    killed = logcat_since(since, LOW_MEMORY_KILL)
    if killed:
        result["status"] = "killed-low-memory"
        result["low_memory_kill"] = killed[-1].strip()
    result["vulkan_drivers_seen"] = drivers_seen(since, processes)
    result["launch_settings"] = [line.split(": I: ", 1)[-1] for line in
                                 logcat_since(since, LAUNCH_SETTINGS)]
    # GameNative logs the driver it extracts for the container on every launch.
    driver = logcat_since(since, "Adrenotools DriverID: ")
    if driver:
        result["driver"] = driver[0].split("Adrenotools DriverID: ", 1)[-1].strip()
    faults_after = gpu_faults()
    for key in faults_before.keys() & faults_after.keys():
        result[f"gpu_{key}"] = faults_after[key] - faults_before[key]
        if result[f"gpu_{key}"] > 0:
            result["status"] = "gpu-fault"
    stats = result.get("surfaceflinger", {})
    result.setdefault("status", "ran" if stats.get("steady_seconds", 0) >= MIN_STEADY_S
                      and stats.get("seconds", 0) >= 0.9 * MEASURE_S else "too-short")
    result["gpu_temp_c_after"] = gpu_temp()
    return result


def gray(path: Path, scratch: str) -> list:
    rows = ssim_mod.load(path, scratch)
    return rows[int(len(rows) * OVERLAY_SHARE):]


def similarity(results: list[dict], run_dir: Path, baseline: str) -> dict:
    """SSIM of each run's screenshot against every baseline run's (not itself). The floor is the
    lowest baseline-to-baseline score: a variant below it changed the picture more than the
    scene's own run-to-run variation does."""
    shots = [(r["variant"], run_dir / r["screenshot"]) for r in results
             if r.get("status") == "ran" and r.get("screenshot")]
    base = [p for v, p in shots if v == baseline]
    if len(base) < 1:
        return {}
    scores: dict[str, list[float]] = {}
    with tempfile.TemporaryDirectory() as scratch:
        cache: dict[Path, list] = {}

        def image(path: Path) -> list:
            if path not in cache:
                cache[path] = gray(path, scratch)
            return cache[path]
        for variant, path in shots:
            for ref in base:
                if ref != path:
                    scores.setdefault(variant, []).append(ssim_mod.ssim(image(path), image(ref)))
    return {v: {"mean": statistics.fmean(s), "min": min(s), "pairs": len(s)}
            for v, s in scores.items()}


def mean_of(results: list[dict], key: str) -> float | None:
    values = [r.get(key, r.get("surfaceflinger", {}).get(key)) for r in results]
    values = [v for v in values if isinstance(v, (int, float))]
    return statistics.fmean(values) if values else None


def summarize(meta: dict, results: list[dict], sim: dict) -> str:
    variants = list(meta["variants"])
    baseline = variants[0]
    lines = [f"# {meta['game']} play benchmark, {meta['started']}", "",
             f"Scene capture {meta['seconds']} s per run ({WARMUP_S} s warm-up, {MEASURE_S} s "
             f"measured), {meta['rounds']} rounds, variants in turn. "
             f"`am kill-all` through the load: {'no' if meta['no_kill'] else 'yes'}. "
             f"Cool-down to GPU {meta.get('max_temp')} C, skin {meta.get('max_skin')} C; fixed "
             f"performance mode: {'yes' if meta.get('fixed_clocks') else 'no'}. "
             f"Phone: {meta['phone'].get('model')} {meta['phone'].get('build')}, "
             f"Android {meta['phone'].get('android')}, GPU max clock "
             f"{meta['phone'].get('gpu_max_clock_khz')} kHz.", ""]
    if meta.get("launch_settings"):
        keep = ("Screen Size", "Graphics Driver", "DX Wrapper", "Box64 Preset", "FEXCore Preset")
        lines += ["Container: " + "; ".join(s.split(" (Config", 1)[0] for s in meta["launch_settings"]
                                            if s.startswith(keep)), ""]
    if meta.get("driver"):
        lines += [f"Driver: {meta['driver']}", ""]
    lines += ["| variant | properties | ran | " + " | ".join(t for _, t, _ in VARIANT_METRICS)
              + " | SSIM vs " + baseline + " |",
              "|---|---|---|" + "---|" * (len(VARIANT_METRICS) + 1)]
    base_means = {}
    for variant in variants:
        ran = [r for r in results if r["variant"] == variant and r["status"] == "ran"]
        total = sum(1 for r in results if r["variant"] == variant)
        cells = []
        for key, _, digits in VARIANT_METRICS:
            value = mean_of(ran, key)
            if variant == baseline:
                base_means[key] = value
            text = "-" if value is None else f"{value:.{digits}f}"
            if (variant != baseline and value is not None and base_means.get(key)
                    and key in ("fps_avg", "fps_1pct_low", "app_gpu_mb_max")):
                text += f" ({(value / base_means[key] - 1) * 100:+.1f}%)"
            cells.append(text)
        s = sim.get(variant)
        ssim_text = "-" if not s else f"{s['mean']:.3f} (min {s['min']:.3f}, {s['pairs']} pairs)"
        props = ", ".join(f"{k}{'' if k.endswith(':') else '='}{v}"
                          for k, v in meta["variants"][variant].items()) or "none"
        lines.append(f"| {variant} | {props} | {len(ran)}/{total} | " + " | ".join(cells)
                     + f" | {ssim_text} |")
    if sim.get(baseline):
        lines += ["", f"SSIM floor (baseline against itself): min {sim[baseline]['min']:.3f}; "
                  f"GameNative's overlay band (top {OVERLAY_SHARE:.0%}) is excluded, the on-screen "
                  f"controls are the same in every shot."]
    lines += ["", "## Runs", "",
              "| run | variant | status | to play s | FPS avg | 1% low | p95 | p99 | stutters/min "
              "| frames | GPU busy % | GPU MHz | X4 MHz | GPU MB max | RSS MB max | avail MB min "
              "| GPU C before | skin C max |", "|" + "---|" * 18]
    for r in results:
        s = r.get("surfaceflinger", {})

        def f(v, d=1):
            return "-" if not isinstance(v, (int, float)) else f"{v:.{d}f}"
        lines.append(f"| {r['tag']} | {r['variant']} | {r['status']} | {f(r.get('seconds_to_play'), 0)} | "
                     f"{f(s.get('fps_avg'))} | {f(s.get('fps_1pct_low'))} | {f(s.get('frametime_p95_ms'))} | "
                     f"{f(s.get('frametime_p99_ms'))} | {f(s.get('stutters_per_min'))} | {s.get('frames', '-')} | "
                     f"{f(r.get('gpu_busy_pct'), 0)} | {f(r.get('gpu_clock_mhz'), 0)} | "
                     f"{f(r.get('cpu_x4_mhz'), 0)} | {f(r.get('app_gpu_mb_max'), 0)} | "
                     f"{f(r.get('app_rss_mb_max'), 0)} | {f(r.get('available_mb_min'), 0)} | "
                     f"{f(r.get('gpu_temp_c_before'))} | {f(r.get('skin_c_max'))} |")
    failed = [r for r in results if r["status"] != "ran"]
    if failed:
        lines += ["", "## Failed runs", ""]
        for r in failed:
            detail = r.get("low_memory_kill") or (r["play_log"][-1] if r.get("play_log") else "")
            lines.append(f"- {r['tag']} ({r['variant']}): {r['status']}. {detail}")
    return "\n".join(lines) + "\n"


def write(run_dir: Path, meta: dict, results: list[dict], final: bool = False) -> None:
    sim = similarity(results, run_dir, list(meta["variants"])[0]) if final else {}
    (run_dir / "results.json").write_text(json.dumps({"meta": meta, "ssim": sim, "results": results},
                                                     indent=1))
    (run_dir / "summary.md").write_text(summarize(meta, results, sim))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", default="LifeIsStrange3")
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--seconds", type=float, default=75, help="scene capture per run")
    ap.add_argument("--variant", action="append", help="NAME[=prop=value[,prop=value...]]")
    ap.add_argument("--no-kill", action="store_true", help="launch without am kill-all, as a player would")
    ap.add_argument("--max-temp", type=float, default=42, help="GPU C to cool to before a run; 0 skips")
    ap.add_argument("--max-skin", type=float, default=38)
    ap.add_argument("--cool-limit", type=float, default=900, help="longest cool-down in seconds")
    ap.add_argument("--fixed", action="store_true",
                    help="Android's fixed performance mode for the whole session (steadier clocks)")
    args = ap.parse_args()
    if args.seconds < WARMUP_S + MEASURE_S + 5:
        sys.exit(f"--seconds must be at least {WARMUP_S + MEASURE_S + 5}")
    variants = dict(parse_variant(v) for v in (args.variant or DEFAULT_VARIANTS))
    known = {key for items in variants.values() for key in props_of(items)}
    baseline = next(iter(variants))
    container_keys = {key for items in variants.values() for key in container_of(items)}
    missing = container_keys - container_of(variants[baseline]).keys()
    if missing:
        sys.exit(f"the first variant ({baseline}) must also give {sorted(missing)}, since a "
                 f"container setting can't be cleared between runs")
    for items in variants.values():  # every variant states every container setting in play
        for key in container_keys:
            items.setdefault(key, variants[baseline][key])
    select_phone()
    shell(STAY_ON_CMD)
    if args.fixed:
        out = shell("cmd power set-fixed-performance-mode-enabled true; echo rc=$?")
        if out.strip() != "rc=0":  # silent on success
            sys.exit(f"fixed performance mode not confirmed: {out!r}")
    user = app_user()
    run_dir = RESULTS / (dt.datetime.now().strftime("%Y%m%d-%H%M%S") + "-play")
    run_dir.mkdir(parents=True)
    meta = {"game": args.game, "started": dt.datetime.now().isoformat(timespec="seconds"),
            "variants": variants, "rounds": args.rounds, "seconds": args.seconds,
            "measure_s": MEASURE_S, "no_kill": args.no_kill, "max_temp": args.max_temp or None,
            "max_skin": args.max_skin if args.max_temp else None, "fixed_clocks": args.fixed,
            "phone": phone_context()}
    results: list[dict] = []
    print(f"writing {run_dir}", flush=True)
    try:
        for rnd in range(args.rounds):
            order = list(variants) if rnd % 2 == 0 else list(reversed(variants))
            for name in order:
                # A launch the low-memory killer ends says nothing about the variant (the Continue
                # load fails about one time in five on any driver), so the slot gets a second try.
                for attempt in ("", "-retry"):
                    tag = f"r{rnd}-{name}{attempt}"
                    print(f"{tag}: starting", flush=True)
                    r = run_once(args.game, name, variants[name], known, run_dir, tag, user, args)
                    results.append(r)
                    if r.get("launch_settings") and "launch_settings" not in meta:
                        meta["launch_settings"] = r["launch_settings"]
                    if r.get("driver"):
                        meta.setdefault("driver", r["driver"])
                    s = r.get("surfaceflinger", {})
                    print(f"{tag}: {r['status']}, to play {r.get('seconds_to_play')} s, "
                          f"fps {s.get('fps_avg', 0):.1f}, 1% low {s.get('fps_1pct_low') or 0:.1f}, "
                          f"p99 {s.get('frametime_p99_ms') or 0:.1f} ms, GPU {r.get('app_gpu_mb_max')} MB, "
                          f"GPU {r.get('gpu_clock_mhz') or 0:.0f} MHz, skin {r.get('skin_c_before')}-"
                          f"{r.get('skin_c_max')} C, available {r['memory_before']['available_mb']} MB before",
                          flush=True)
                    write(run_dir, meta, results)
                    if r["status"] not in ("play-failed", "killed-low-memory", "app-exited"):
                        break
    finally:
        set_props({}, known)
        shell(f"am force-stop {APP}")
        if args.fixed:
            shell("cmd power set-fixed-performance-mode-enabled false")
        if container_keys:  # leave the container as the baseline variant has it
            apply_container(args.game, container_of(variants[baseline]))
        write(run_dir, meta, results, final=True)
    print((run_dir / "summary.md").read_text())


if __name__ == "__main__":
    main()
