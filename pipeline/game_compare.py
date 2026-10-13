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


def summary(meta, rows):
    out = [f"# Game comparison {meta['started']}", "", meta["manifest"].get("purpose", ""), "",
           f"Same container: {meta['manifest']['bench']}-{meta['manifest']['driver']}. "
           f"Fixed mode: {meta['fixed_clocks']}; {meta['seconds']} s measured per run after warm-up.",
           "Each variant has an unscored warm-up run; persistent shader caches are retained.",
           "Only complete accepted pairs count. Positive gain means faster; range is observed, not a confidence interval.",
           f"Visual review: {meta.get('visual_review', 'PENDING; timings remain provisional')}", "",
           "| Candidate | Valid pairs | Baseline FPS median | Candidate FPS median | Paired gain median | Paired range |",
           "|---|---:|---:|---:|---:|---:|"]
    for v in meta["manifest"]["variants"][1:]:
        adapted = [{**r, "test": "fps", "status": "pass" if r.get("accepted") and not r.get("warmup") else "excluded",
                    "fps": r.get("surfaceflinger", {}).get("fps_avg", 0)} for r in rows]
        values = compare.compare_values(adapted, "fps", v["name"], "fps", True)
        if values:
            n, b, c, gain, lo, hi = values
            out.append(f"| {v['name']} | {n} | {b:.2f} | {c:.2f} | {gain:+.2f}% | {lo:+.2f}..{hi:+.2f}% |")
    out += ["", "| Pair | Variant | Accepted | FPS | 1% low | GPU MHz | GPU C | Start C |",
            "|---|---|---|---:|---:|---:|---:|---:|"]
    for r in rows:
        s = r.get("surfaceflinger", {})
        pair = "warm-up" if r.get("warmup") else f"{r['repeat']}/{r['candidate']}"
        out.append(f"| {pair} | {r['variant']} | {r.get('accepted', False)} ({r['status']}) | "
                   f"{s.get('fps_avg', 0):.2f} | {s.get('fps_1pct_low', 0):.2f} | "
                   f"{r.get('gpu_clock_mhz', 0):.1f} | {r.get('gpu_temp_c_mean', 0):.1f} | "
                   f"{r.get('gpu_temp_c_before', 0):.1f} |")
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
        run.shell("svc power stayon usb")
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
