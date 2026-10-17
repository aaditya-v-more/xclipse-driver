#!/usr/bin/env python3
"""Paired property experiments in ONE unchanged GameNative container.

Exports and checks the container, warms each variant's persistent shader cache, then runs
AB/BA pairs. Every score covers the same duration after game.py's rendering warm-up. A crash,
GPU fault, short run, missing screenshots/telemetry, or lost frame samples stops the experiment.
Screenshots require a separate human/agent review before accepting a performance claim.
Fixed performance mode is released in finally; begin with the phone in normal mode.
"""
from __future__ import annotations

import argparse
from collections import Counter
import datetime as dt
import hashlib
import json
import re
import shlex
from pathlib import Path

import compare
import game
import gn_driver
import run


def load_manifest(path):
    m = json.loads(path.read_text(encoding="utf-8"))
    if m["bench"] not in game.BENCHES or m["driver"] not in ("radv", "stock"):
        raise ValueError("unknown benchmark/container")
    variants = m["variants"]
    if len(variants) < 2 or len({v["name"] for v in variants}) != len(variants):
        raise ValueError("use at least two distinct variants")
    keys = set(variants[0]["props"])
    for v in variants:
        if not re.fullmatch(r"[a-z][a-z0-9_-]*", v["name"]) or set(v["props"]) != keys:
            raise ValueError("valid names and identical property keys are required")
        if any(not re.fullmatch(r"debug\.radv_[a-z0-9_]+", k) for k in keys):
            raise ValueError("only RADV debug properties are supported")
    return m


def set_prop(key, value):
    run.shell(f"setprop {shlex.quote(key)} {shlex.quote(str(value))}")
    if run.shell(f"getprop {shlex.quote(key)}").strip() != str(value):
        raise RuntimeError(f"property readback failed: {key}")


def accepted(r, duration, max_start_temp):
    s = r.get("surfaceflinger", {})
    return (r["status"] == "ran" and s.get("steady_seconds", 0) >= duration - .25
            and 0 < r.get("gpu_temp_c_before", 0) <= max_start_temp
            and s.get("sampling_gaps") == 0 and s.get("covered_share", 0) > .999
            and len(r.get("screens", [])) >= 2 and r.get("gpu")
            and not r.get("gpu_page_faults", 0) and not r.get("gpu_resets", 0))


def active_clocks(r, duration=None):
    """Observed GPU/X4 clocks in the measured window; omit GPU-off samples.

    The window starts WARMUP_S after the game renders for good (seconds_to_rendering, aligned to
    the frame window within an adb round trip) and lasts `duration` when given. Results recorded
    before 2026-10-17 lack that field and fall back to the first surface frame, which can precede
    loading. Other CPU clusters can still change frequency in fixed mode.
    """
    begin = r.get("seconds_to_rendering", r.get("seconds_to_first_frame"))
    if begin is None:
        return Counter()
    end = begin + game.WARMUP_S + duration if duration is not None else float("inf")
    return Counter((s[2], s[7]) for s in r.get("gpu", [])
                   if len(s) >= 8 and begin + game.WARMUP_S <= s[0] <= end and s[2] > 0 and s[7] > 0)


def scored_rows(meta, rows):
    """Keep raw attempts, but never attribute a fixed-mode clock mismatch to the driver."""
    adapted = [{**r, "test": "fps", "status": "pass" if r.get("accepted") and not r.get("warmup") else "excluded",
                "fps": r.get("surfaceflinger", {}).get("fps_avg", 0),
                "low": r.get("surfaceflinger", {}).get("fps_1pct_low", 0)} for r in rows]
    if meta["fixed_clocks"]:
        groups = {}
        for r in adapted:
            if r["status"] == "pass":
                groups.setdefault((r["repeat"], r["candidate"]), []).append(r)
        for group in groups.values():
            clocks = [active_clocks(r, meta.get("seconds")) for r in group]
            matched = (len(group) == 2 and {r["role"] for r in group} == {"baseline", "candidate"}
                       and all(len(c) == 1 and sum(c.values()) >= 10 for c in clocks)
                       and set(clocks[0]) == set(clocks[1]))
            if not matched:
                for r in group:
                    r["status"] = "clock-excluded"
    return adapted


def resource_stats(r):
    """Whole-attempt peaks include launch; RSS is summed processes, not unique physical RAM."""
    memory = r.get("memory", [])
    fields = {"rss_peak_mb": ("app_rss_mb", max), "swap_peak_mb": ("app_swap_mb", max),
              "gpu_peak_mb": ("app_gpu_mb", max), "available_min_mb": ("available_mb", min),
              "pressure_peak_pct": ("pressure_full_avg10", max),
              "battery_peak_dc": ("battery_temperature_dc", max)}
    out = {}
    for name, (key, aggregate) in fields.items():
        values = [s[key] for s in memory if s.get(key) is not None]
        out[name] = aggregate(values) if values else None
    return out


def summary(meta, rows):
    out = [f"# Game comparison {meta['started']}", "", meta["manifest"].get("purpose", ""), "",
           f"Same container: {meta['manifest']['bench']}-{meta['manifest']['driver']}. "
           f"Fixed mode: {meta['fixed_clocks']}; {meta['seconds']} s measured per run after warm-up.",
           "Each variant has an unscored warm-up run; persistent shader caches are retained.",
           "Only complete accepted pairs count. Fixed-mode scores additionally require identical single GPU/X4 clocks",
           "with at least ten active samples per run. This does not lock every CPU cluster or establish normal-mode performance.",
           "Positive gain means faster; range is observed, not a confidence interval.",
           f"Visual review: {meta.get('visual_review', 'PENDING; timings remain provisional')}", "",
           f"Clock review: {meta.get('clock_review', 'See observed GPU/X4 profiles below.')}", "",
           "| Candidate | Metric | Timing pairs | Baseline median | Candidate median | Paired gain median | Paired range |",
           "|---|---|---:|---:|---:|---:|---:|"]
    adapted = scored_rows(meta, rows)
    for v in meta["manifest"]["variants"][1:]:
        for metric, label in (("fps", "Average FPS"), ("low", "1% low FPS")):
            values = compare.compare_values(adapted, "fps", v["name"], metric, True)
            if values:
                n, b, c, gain, lo, hi = values
                out.append(f"| {v['name']} | {label} | {n} | {b:.2f} | {c:.2f} | {gain:+.2f}% | {lo:+.2f}..{hi:+.2f}% |")
    out += ["", "| Pair | Variant | Accepted | FPS | 1% low | GPU MHz | GPU C | Start C |",
            "|---|---|---|---:|---:|---:|---:|---:|"]
    for r in rows:
        s = r.get("surfaceflinger", {})
        pair = "warm-up" if r.get("warmup") else f"{r['repeat']}/{r['candidate']}"
        out.append(f"| {pair} | {r['variant']} | {r.get('accepted', False)} ({r['status']}) | "
                   f"{s.get('fps_avg', 0):.2f} | {s.get('fps_1pct_low', 0):.2f} | "
                   f"{r.get('gpu_clock_mhz', 0):.1f} | {r.get('gpu_temp_c_mean', 0):.1f} | "
                   f"{r.get('gpu_temp_c_before', 0):.1f} |")
    out += ["", "Whole-attempt memory peaks include launch. Summed process RSS can double-count shared pages;",
            "driver-accounted GPU memory can omit imported DMA-BUF allocations. Neither is total physical RAM.",
            "USB battery charge/voltage readings do not measure system power or energy per frame.", "",
            "| Pair | Variant | Peak RSS MiB | Peak swap MiB | Accounted GPU MiB | Min available MiB | Peak memory PSI % | Peak battery C |",
            "|---|---|---:|---:|---:|---:|---:|---:|"]
    for r in rows:
        p = resource_stats(r)
        def value(key, scale=1):
            return f"{p[key] / scale:.1f}" if p[key] is not None else "missing"
        pair = "warm-up" if r.get("warmup") else f"{r['repeat']}/{r['candidate']}"
        out.append(f"| {pair} | {r['variant']} | {value('rss_peak_mb')} | {value('swap_peak_mb')} | "
                   f"{value('gpu_peak_mb')} | {value('available_min_mb')} | {value('pressure_peak_pct')} | "
                   f"{value('battery_peak_dc', 10)} |")
    out += ["", "Observed GPU/X4 profiles in the measured window (from rendering start + warm-up; first surface frame for older results), excluding GPU-off samples.",
            "Telemetry aligns with the frame window within one adb round trip. Missing or changing clocks exclude fixed-mode pairs.", "",
            "| Pair | Variant | GPU/X4 MHz: samples | Timing status |", "|---|---|---|---|"]
    for r, scored in zip(rows, adapted):
        profile = ", ".join(f"{gpu / 1000:g}/{cpu / 1000:g}: {count}" for (gpu, cpu), count in sorted(active_clocks(r, meta.get("seconds")).items()))
        pair = "warm-up" if r.get("warmup") else f"{r['repeat']}/{r['candidate']}"
        out.append(f"| {pair} | {r['variant']} | {profile or 'missing'} | {scored['status']} |")
    if meta.get("error"):
        out += ["", f"Stopped: {meta['error']}"]
    return "\n".join(out) + "\n"


def save(folder, meta, rows):
    for name, data in (("results.json", json.dumps({"meta": meta, "results": rows}, indent=2)),
                       ("summary.md", summary(meta, rows))):
        tmp = folder / (name + ".tmp")
        tmp.write_text(data, encoding="utf-8", newline="\n")
        tmp.replace(folder / name)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("manifest", type=Path)
    ap.add_argument("--repetitions", type=int, default=6)
    ap.add_argument("--seconds", type=int, default=90)
    ap.add_argument("--max-start-temp", type=float, default=45)
    ap.add_argument("--fixed-clocks", action="store_true")
    args = ap.parse_args()
    if args.repetitions < 5 or args.seconds < 60:
        ap.error("use at least five pairs and 60 measured seconds")
    manifest = load_manifest(args.manifest)
    if run.phone_state() != "device":
        ap.error("phone not ready")
    started = dt.datetime.now()
    folder = run.RESULTS / started.strftime("%Y-%m-%d_%H%M%S-game-compare")
    folder.mkdir()
    meta = {"started": started.isoformat(timespec="seconds"), "manifest": manifest,
            "repetitions": args.repetitions, "seconds": args.seconds,
            "max_start_temp_c": args.max_start_temp, "fixed_clocks": args.fixed_clocks,
            "phone": run.phone_context()}
    rows = []
    keys = set(manifest["variants"][0]["props"]) | {"debug.radv_xclipse_log"}
    props = {k: run.shell(f"getprop {k}").strip() for k in keys}
    refresh = {k: run.shell(f"settings get system {k}").strip()
               for k in ("min_refresh_rate", "peak_refresh_rate")}
    zen = run.shell("settings get global zen_mode").strip()
    try:
        run.shell(run.STAY_ON_CMD)
        run.shell("input keyevent KEYCODE_WAKEUP")
        if not game.wait_unlocked(300):
            raise RuntimeError("phone stayed locked")
        # The phone retains every export; a unique basename also avoids overwriting an earlier one.
        config_path = folder / f"{folder.name}-container.json"
        title = {"heaven": "Heaven", "gravitymark": "GravityMark"}[manifest["bench"]]
        gn_driver.export_config(f"{title}-{manifest['driver']}", config_path)
        config = json.loads(config_path.read_text(encoding="utf-8"))
        meta["container_sha256"] = hashlib.sha256(config_path.read_bytes()).hexdigest()
        meta["container"] = {k: config[k] for k in ("screenSize", "graphicsDriver", "graphicsDriverConfig",
                            "dxwrapper", "dxwrapperConfig", "execArgs", "envVars", "box64Version",
                            "box64Preset", "fexcoreVersion", "fexcorePreset")}
        for key, value in manifest["container_contains"].items():
            if value not in str(config.get(key, "")):
                raise RuntimeError(f"container precondition failed: {key} must contain {value!r}")
        run.shell(f"am force-stop {game.APP}")
        for key in refresh:
            run.shell(f"settings put system {key} 120.0")
        if zen in ("0", "1", "2", "3"):
            run.shell("cmd notification set_dnd priority")
            if run.shell("settings get global zen_mode").strip() != "1":
                raise RuntimeError("DND readback failed")
        set_prop("debug.radv_xclipse_log", "0")
        if args.fixed_clocks:
            p = run.adb("shell", "cmd power set-fixed-performance-mode-enabled true")
            if p.returncode or (p.stdout + p.stderr).strip():
                raise RuntimeError("fixed performance mode was not confirmed")
        user = game.app_user()
        jobs = [(0, "warmup", [v]) for v in manifest["variants"]]
        jobs += list(compare.pairs(manifest["variants"], args.repetitions))
        for repeat, candidate, order in jobs:
            for v in order:
                for key, value in v["props"].items():
                    set_prop(key, value)
                tag = f"pair{repeat}-{candidate}-{v['name']}"
                print(f"{tag}: preparing launch (limit {args.max_start_temp:.1f} C)", flush=True)
                r = game.run_once(manifest["bench"], manifest["driver"], folder, tag, user,
                                  shots=[35, 65], seconds=args.seconds + 120, measure_seconds=args.seconds,
                                  max_start_temp=args.max_start_temp)
                r.update(repeat=repeat, candidate=candidate, variant=v["name"], warmup=repeat == 0,
                         role="baseline" if v is manifest["variants"][0] else "candidate",
                         props=v["props"], accepted=bool(accepted(r, args.seconds, args.max_start_temp)))
                rows.append(r)
                save(folder, meta, rows)
                print(f"{tag}: {r['status']}, {r['surfaceflinger'].get('fps_avg', 0):.2f} FPS, "
                      f"accepted={r['accepted']}", flush=True)
                if not r["accepted"]:
                    raise RuntimeError(f"{tag} failed measurement requirements; stopping")
    except BaseException as error:
        meta["error"] = str(error)
        raise
    finally:
        run.shell(f"am force-stop {game.APP}")
        game.logcat_stop()
        if args.fixed_clocks:
            p = run.adb("shell", "cmd power set-fixed-performance-mode-enabled false")
            meta["fixed_mode_release"] = {"returncode": p.returncode, "output": p.stdout + p.stderr}
        restore_errors = []
        for key, value in props.items():
            try:
                set_prop(key, value)
            except Exception as error:
                restore_errors.append(str(error))
        for key, value in refresh.items():
            run.shell(f"settings delete system {key}" if value in ("", "null")
                      else f"settings put system {key} {value}")
        if zen in ("0", "1", "2", "3"):
            run.shell("cmd notification set_dnd " + {"0": "all", "1": "priority", "2": "none", "3": "alarms"}[zen])
        if restore_errors:
            meta["restore_errors"] = restore_errors
        save(folder, meta, rows)
        print(f"report: {folder / 'summary.md'}", flush=True)


if __name__ == "__main__":
    main()
