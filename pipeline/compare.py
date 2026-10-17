#!/usr/bin/env python3
"""Repeated, alternating native-driver comparisons, with clock/temperature samples.

The manifest has a variants list: {name, library (repo-relative or 'stock'), sha256, env}.
Every candidate is paired with the first variant, in AB/BA order, at least five times.
Fixed performance mode is optional and ALWAYS disabled on exit: Android has no getter for it.
Use this only when the phone starts in its normal performance mode. A CPU affinity applies to
the test process and its workers. These are driver measurements, not game FPS predictions.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import re
import statistics
import threading
import time
from pathlib import Path

import game
import run

METRICS = {
    **run.PERF_METRICS,
    "scratch": [("ms_scratch_gpu_median", "Graphics scratch, GPU ms", False)],
    "scratchcs": [("ms_compute_gpu_median", "Compute scratch, GPU ms", False)],
}


def load_manifest(path: Path) -> dict:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    variants = manifest["variants"]
    if len(variants) < 2:
        raise ValueError("at least two variants are required")
    names = set()
    for v in variants:
        if not re.fullmatch(r"[a-z][a-z0-9_-]*", v["name"]) or v["name"] in names:
            raise ValueError(f"invalid/duplicate variant name: {v['name']}")
        names.add(v["name"])
        if v["library"] != "stock":
            lib = (run.ROOT / v["library"]).resolve()
            digest = hashlib.sha256(lib.read_bytes()).hexdigest()
            if digest != v["sha256"]:
                raise ValueError(f"{v['name']}: library does not match its pinned SHA256")
        elif v.get("env"):
            raise ValueError("stock variants do not accept RADV environment options")
        for key in v.get("env", {}):
            if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key):
                raise ValueError(f"invalid environment name: {key}")
    return manifest


def pairs(variants: list[dict], repetitions: int):
    for repeat in range(1, repetitions + 1):
        candidates = variants[1:] if repeat % 2 else variants[:0:-1]
        for candidate in candidates:
            order = [variants[0], candidate] if repeat % 2 else [candidate, variants[0]]
            yield repeat, candidate["name"], order


def compare_values(rows: list[dict], test: str, candidate: str, metric: str, higher: bool):
    grouped = {}
    for r in rows:
        if r["test"] == test and r["candidate"] == candidate and r["status"] == "pass" and metric in r:
            grouped.setdefault(r["repeat"], {})[r["role"]] = r[metric]
    matched = [p for p in grouped.values() if p.get("baseline", 0) > 0 and p.get("candidate", 0) > 0]
    if not matched:
        return None
    base = [p["baseline"] for p in matched]
    change = [p["candidate"] for p in matched]
    gain = [(b / c - 1 if not higher else c / b - 1) * 100 for b, c in zip(base, change)]
    return len(matched), statistics.median(base), statistics.median(change), statistics.median(gain), min(gain), max(gain)


def summary(meta: dict, rows: list[dict]) -> str:
    out = [f"# Native comparison {meta['started']}", "",
           f"Fixed performance mode: {meta['fixed_clocks']}. CPU affinity: {meta['cpu']}.",
           f"Start-temperature limit: {meta['max_start_temp_c']} C. AB/BA pairs: {meta['repetitions']} per candidate/test.",
           "Shader disk cache is disabled. Each timed microbenchmark also warms up internally.",
           "Positive gain means faster. Ranges are the observed paired gains, not confidence intervals.",
           "No game-FPS inference is made from these driver timings.", "",
           "| Candidate | Test / metric | Valid pairs | Baseline median | Candidate median | Paired gain median | Paired range |",
           "|---|---|---:|---:|---:|---:|---:|"]
    for variant in meta["manifest"]["variants"][1:]:
        for test in meta["tests"]:
            for key, label, higher in METRICS.get(test, []):
                values = compare_values(rows, test, variant["name"], key, higher)
                if values:
                    n, b, c, gain, lo, hi = values
                    out.append(f"| {variant['name']} | {label} | {n} | {b:.4f} | {c:.4f} | {gain:+.2f}% | {lo:+.2f}..{hi:+.2f}% |")
    out += ["", "## Attempts", "", "| Pair | Test | Variant | Status | GPU MHz range | X4 MHz range | GPU C range |",
            "|---|---|---|---|---|---|---|"]
    for r in rows:
        def span(index, scale=1):
            vals = [s[index] / scale for s in r.get("samples", []) if len(s) > index]
            return f"{min(vals):.1f}..{max(vals):.1f}" if vals else "missing"
        out.append(f"| {r['repeat']}/{r['candidate']} | {r['test']} | {r['variant']} | {r['status']} | "
                   f"{span(1, 1000)} | {span(6, 1000)} | {span(2)} |")
    if meta.get("error"):
        out += ["", f"Stopped: {meta['error']}"]
    return "\n".join(out) + "\n"


def save(folder, meta, rows):
    for name, content in (("results.json", json.dumps({"meta": meta, "results": rows}, indent=2)),
                          ("summary.md", summary(meta, rows))):
        tmp = folder / (name + ".tmp")
        tmp.write_text(content, encoding="utf-8", newline="\n")
        tmp.replace(folder / name)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("manifest", type=Path)
    ap.add_argument("--tests", default="draws,scratch,scratchcs")
    ap.add_argument("--repetitions", type=int, default=5)
    ap.add_argument("--fixed-clocks", action="store_true")
    ap.add_argument("--cpu", type=int, default=9)
    ap.add_argument("--max-start-temp", type=float, default=50)
    args = ap.parse_args()
    if args.repetitions < 5 or not 0 <= args.cpu < 64:
        ap.error("use at least five repetitions and a CPU index between 0 and 63")
    manifest = load_manifest(args.manifest)
    tests = args.tests.split(",")
    if any(t not in run.ALL_TESTS for t in tests):
        ap.error("unknown test")
    if run.phone_state() != "device":
        ap.error("phone is not ready")
    run.build_tests()
    run.deploy(False)
    for v in manifest["variants"]:
        if v["library"] == "stock":
            v["phone_library"] = "stock"
        else:
            v["phone_library"] = f"{run.PHONE_DIR}/compare-{v['sha256'][:16]}.so"
            run.push(run.ROOT / v["library"], v["phone_library"])
    started = dt.datetime.now()
    folder = run.RESULTS / started.strftime("%Y-%m-%d_%H%M%S-compare")
    folder.mkdir()
    meta = {"started": started.isoformat(timespec="seconds"), "manifest": manifest,
            "tests": tests, "repetitions": args.repetitions, "cpu": args.cpu,
            "fixed_clocks": args.fixed_clocks, "max_start_temp_c": args.max_start_temp,
            "phone": run.phone_context(), "sampler_columns": ["gpu_busy_pct", "gpu_khz", "gpu_c",
                "cpu0_khz", "cpu4_khz", "cpu7_khz", "cpu9_khz"],
            "test_sha256": hashlib.sha256((run.TEST_BUILD / "xdtest").read_bytes()).hexdigest()}
    rows = []
    known = set(run.list_crash_records())
    try:
        run.shell("am force-stop app.gamenative")
        run.shell(run.STAY_ON_CMD)
        if args.fixed_clocks:
            p = run.adb("shell", "cmd power set-fixed-performance-mode-enabled true")
            if p.returncode or (p.stdout + p.stderr).strip():
                raise RuntimeError(f"fixed performance mode not confirmed: {p.stdout}{p.stderr}")
        for test in tests:
            for repeat, candidate, order in pairs(manifest["variants"], args.repetitions):
                for v in order:
                    if game.cool_down(args.max_start_temp) > args.max_start_temp:
                        raise RuntimeError("cooldown target not reached; comparison stopped")
                    samples, errors, stop = [], [], threading.Event()
                    def sample():
                        while not stop.is_set():
                            try:
                                value = game.gpu_sample()
                                if value: samples.append(value)
                            except Exception as exc:
                                errors.append(str(exc))
                            stop.wait(0.25)
                    thread = threading.Thread(target=sample, daemon=True)
                    thread.start()
                    try:
                        env = [f"{k}={val}" for k, val in v.get("env", {}).items()]
                        r = run.run_test(v["name"] if v["library"] != "stock" else "stock", test,
                                         folder, known, env, library=v["phone_library"], cpu=args.cpu)
                    finally:
                        stop.set()
                        thread.join(timeout=20)
                    r.update(variant=v["name"], repeat=repeat, candidate=candidate,
                             role="baseline" if v is manifest["variants"][0] else "candidate",
                             samples=samples, sampler_errors=errors)
                    if not samples and r["status"] == "pass":
                        r["status"] = "missing-telemetry"
                    rows.append(r)
                    save(folder, meta, rows)
                    print(f"{test} pair {repeat}/{candidate} {v['name']}: {r['status']}", flush=True)
                    if r["status"] not in ("pass", "skip"):
                        raise RuntimeError(f"{v['name']}/{test}: {r['status']}")
    except (Exception, KeyboardInterrupt) as exc:
        meta["error"] = str(exc) or "interrupted"
        raise
    finally:
        if args.fixed_clocks:
            p = run.adb("shell", "cmd power set-fixed-performance-mode-enabled false")
            meta["fixed_mode_release"] = {"returncode": p.returncode, "output": (p.stdout + p.stderr).strip()}
        save(folder, meta, rows)
        print(f"report: {folder / 'summary.md'}", flush=True)


if __name__ == "__main__":
    main()
