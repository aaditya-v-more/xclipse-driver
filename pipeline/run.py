#!/usr/bin/env python3
"""Builds, deploys and runs the on-device driver tests, then writes a report.

Every test runs against Samsung's stock driver and against our RADV build, so each result is an
A/B pair. A GPU hang can make the phone reboot: when the phone drops off USB, the runner waits for
it to come back, saves the kernel's crash record, and stops testing our driver for this run
(--keep-going continues anyway).

usage: python pipeline/run.py [--build-driver] [--tests info,device,...] [--drivers stock,radv]
                              [--keep-going]
       Use the toolchain's Python: tools\\python\\python.exe on Windows, tools/python/bin/python3
       on macOS and Linux (or plain python3 after ". tools/env.sh").
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
WINDOWS = sys.platform == "win32"
EXE = ".exe" if WINDOWS else ""
# The NDK ships x86_64 host binaries for every OS; on Apple Silicon they're universal.
NDK_HOST = {"win32": "windows-x86_64", "darwin": "darwin-x86_64"}.get(sys.platform, "linux-x86_64")
NDK_BIN = TOOLS / "android-ndk-r29" / "toolchains" / "llvm" / "prebuilt" / NDK_HOST / "bin"
GLSLANG = TOOLS / "glslang" / "bin" / f"glslangValidator{EXE}"
TEST_SRC = ROOT / "tests"
TEST_BUILD = TEST_SRC / "build"
DRIVER_SO = ROOT / "driver" / "build-android" / "vulkan.radeon.so"
RESULTS = ROOT / "results"
ADB = shutil.which("adb") or str(Path.home() / "platform-tools" / f"adb{EXE}")
PHONE_DIR = "/data/local/tmp/xd"
CORRECTNESS_TESTS = ["info", "device", "submit", "compute", "render", "tess", "scratch", "scratchcs",
                     "queries", "xfb", "xfbmany", "xfbmanygs", "textures", "mips", "mrt", "vertex", "depth",
                     "storage", "bcn", "descriptors"]
PERF_TESTS = ["alu", "bandwidth", "fill", "draws", "compile"]
# Tests that hang our driver's GPU today. They run last, because after a hang the run stops testing
# our driver.
ALL_TESTS = CORRECTNESS_TESTS + PERF_TESTS
SHADERS = {
    "fill.comp": "fill_comp", "tri.vert": "tri_vert", "tri.frag": "tri_frag",
    "alu.comp": "alu_comp", "copy.comp": "copy_comp", "complex.comp": "complex_comp",
    "screen.vert": "screen_vert", "flat.frag": "flat_frag", "tiny.vert": "tiny_vert",
    "tri.geom": "tri_geom", "xfb.vert": "xfb_vert", "xfb_many.vert": "xfb_many_vert",
    "grid.vert": "grid_vert", "xfb_many.geom": "xfb_many_geom", "inputs.frag": "inputs_frag",
    "tess.tesc": "tess_tesc", "tess.tese": "tess_tese", "scratch.vert": "scratch_vert",
    "fill_scratch.comp": "fill_scratch_comp", "sample.comp": "sample_comp",
    "mrt.frag": "mrt_frag", "mrt_add.frag": "mrt_add_frag", "mrt_read.comp": "mrt_read_comp",
    "vertex.vert": "vertex_vert", "depth.vert": "depth_vert", "depth_read.comp": "depth_read_comp",
    "mips_read.comp": "mips_read_comp", "storage_write.comp": "storage_write_comp",
    "storage_read.comp": "storage_read_comp",
    "bcn_s3tc.comp": "bcn_s3tc_comp",
}
# Performance metrics per test: (result key, label, higher is better).
PERF_METRICS = {
    "alu": [("gflops", "FP32 throughput, GFLOPS", True)],
    "bandwidth": [("gbps_shader_copy", "Shader copy, GB/s", True),
                  ("gbps_copy_command", "vkCmdCopyBuffer, GB/s", True)],
    "fill": [("gpix_s_opaque", "Opaque fill at 1080p, Gpixel/s", True),
             ("gpix_s_blend", "Blended fill at 1080p, Gpixel/s", True)],
    "draws": [("us_record_per_draw", "CPU per draw, µs", False),
              ("us_record_per_draw_switching", "CPU per draw with a pipeline switch, µs", False),
              ("us_gpu_per_draw", "GPU per draw, µs", False),
              ("us_gpu_per_draw_switching", "GPU per draw with a pipeline switch, µs", False)],
    "compile": [("ms_median", "Shader pipeline compile, ms (median of 16)", False),
                ("ms_first", "First pipeline compile, ms", False)],
}

# Extensions that decide what DXVK (Direct3D 9-11) and vkd3d-proton (Direct3D 12) can do.
GAME_EXTENSIONS = [
    "VK_KHR_maintenance5", "VK_KHR_maintenance6", "VK_EXT_mutable_descriptor_type",
    "VK_EXT_transform_feedback", "VK_EXT_graphics_pipeline_library", "VK_EXT_robustness2",
    "VK_KHR_robustness2", "VK_EXT_custom_border_color", "VK_EXT_extended_dynamic_state3",
    "VK_EXT_descriptor_buffer", "VK_EXT_shader_module_identifier", "VK_EXT_depth_clip_enable",
    "VK_EXT_line_rasterization", "VK_EXT_vertex_attribute_divisor",
    "VK_KHR_vertex_attribute_divisor", "VK_EXT_attachment_feedback_loop_layout",
    "VK_EXT_non_seamless_cube_map", "VK_EXT_memory_priority", "VK_EXT_memory_budget",
    "VK_EXT_conservative_rasterization", "VK_EXT_fragment_shader_interlock",
    "VK_EXT_shader_stencil_export", "VK_EXT_image_view_min_lod", "VK_EXT_depth_bias_control",
    "VK_KHR_fragment_shading_rate", "VK_EXT_mesh_shader", "VK_KHR_ray_tracing_pipeline",
    "VK_KHR_ray_query", "VK_KHR_present_wait",
]
KEY_FEATURES = [
    "textureCompressionBC", "shaderFloat64", "samplerFilterMinmax", "geometryShader",
    "tessellationShader", "depthBounds", "dualSrcBlend", "logicOp", "multiViewport",
    "pipelineStatisticsQuery", "shaderCullDistance", "fillModeNonSolid",
]
STATUS_TEXT = {"pass": "pass", "fail": "**FAIL**", "crash": "**crash**", "timeout": "**timeout**",
               "gpu-hang": "**GPU hang**", "phone-reboot": "**phone rebooted**"}
# Samsung's GPU driver counts hangs, timeouts and page faults in /sys/kernel/gpu/gpu_info.
GPU_EVENT_COUNTERS = {"GHG_S": "hang", "GTO_S": "timeout", "GPF_S": "page fault",
                      "GRH_S": "GRH", "GRF_S": "GRF", "GCF_S": "GCF"}


def run(cmd: list[str], timeout: float | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace",
                          timeout=timeout)


def adb(*args: str, timeout: float = 120) -> subprocess.CompletedProcess:
    return run([ADB, *args], timeout=timeout)


def shell(command: str, timeout: float = 60) -> str:
    return adb("shell", command, timeout=timeout).stdout.strip()


def phone_state() -> str:
    try:
        return adb("get-state", timeout=15).stdout.strip()
    except subprocess.TimeoutExpired:
        return ""


def wait_for_phone(limit_s: float = 600) -> bool:
    """Waits until the phone is back on adb and has finished booting."""
    deadline = time.monotonic() + limit_s
    while time.monotonic() < deadline:
        try:
            if phone_state() == "device" and shell("getprop sys.boot_completed", 15) == "1":
                return True
        except subprocess.TimeoutExpired:
            pass
        time.sleep(5)
    return False


def build_driver() -> None:
    log = ROOT / "driver" / "build-log.txt"
    head = run(["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"]).stdout.strip()
    dirty = run(["git", "-C", str(ROOT), "status", "--porcelain", "--", "driver"]).stdout.strip()
    env = dict(os.environ, PACKAGE_TAG=f"xd-{head}{'-dirty' if dirty else ''}")
    if WINDOWS:
        # subprocess.list2cmdline backslash-escapes embedded quotes, which cmd.exe does not
        # unescape. Relative script paths plus cwd avoid nested quoting (including spaced roots).
        cmd = ["cmd.exe", "/d", "/c", r"call tools\env.cmd && driver\build.cmd"]
    else:
        env["ANDROID_NDK_HOME"] = str(TOOLS / "android-ndk-r29")
        env["PATH"] = os.pathsep.join([str(TOOLS / "python" / "bin"), str(TOOLS / "glslang" / "bin"),
                                       env.get("PATH", "")])
        cmd = ["sh", str(ROOT / "driver" / "build.sh")]
    with log.open("w", encoding="utf-8") as out:
        r = subprocess.run(cmd, stdout=out, stderr=subprocess.STDOUT, env=env, cwd=ROOT)
    if r.returncode:
        sys.exit(f"driver build failed; see {log}")


def build_tests() -> None:
    TEST_BUILD.mkdir(parents=True, exist_ok=True)
    for source, name in SHADERS.items():
        r = run([str(GLSLANG), "-V", "--target-env", "vulkan1.1", "--vn", name,
                 "-o", str(TEST_BUILD / f"{name}.h"), str(TEST_SRC / "shaders" / source)])
        if r.returncode:
            sys.exit(f"glslang failed on {source}:\n{r.stdout}{r.stderr}")
    r = run([str(NDK_BIN / f"clang{EXE}"), "--target=aarch64-linux-android34", "-std=c11", "-O2",
             "-Wall", "-Wextra", "-Wno-unused-parameter",
             "-I", str(ROOT / "driver" / "include"),
             "-I", str(ROOT / "driver" / "include" / "android_stub"),
             "-I", str(TEST_BUILD), "-o", str(TEST_BUILD / "xdtest"), str(TEST_SRC / "xdtest.c"),
             "-ldl", "-lm"])
    if r.returncode:
        sys.exit(f"xdtest build failed:\n{r.stdout}{r.stderr}")
    if r.stderr.strip():
        print(r.stderr.strip())


def push(local: Path, remote: str) -> None:
    r = adb("push", str(local), remote, timeout=300)
    if r.returncode:
        sys.exit(f"adb push {local.name} failed: {(r.stderr or r.stdout).strip()}")


def deploy(with_driver: bool) -> None:
    shell(f"mkdir -p {PHONE_DIR}/out && rm -f {PHONE_DIR}/out/*")
    push(TEST_BUILD / "xdtest", f"{PHONE_DIR}/xdtest")
    shell(f"chmod 755 {PHONE_DIR}/xdtest")
    if with_driver:
        push(DRIVER_SO, f"{PHONE_DIR}/vulkan.radeon.so")


def phone_context() -> dict:
    queries = {
        "model": "getprop ro.product.model",
        "build": "getprop ro.build.PDA",
        "android": "getprop ro.build.version.release",
        "gpu_temp_c": "cat /sys/kernel/gpu/gpu_tmu",
        "gpu_clock_khz": "cat /sys/kernel/gpu/gpu_clock",
        "gpu_max_clock_khz": "cat /sys/kernel/gpu/gpu_max_clock",
        "battery": "dumpsys battery | grep -E '^  (level|temperature):'",
    }
    return {key: " ".join(shell(cmd, 20).split()) for key, cmd in queries.items()}


def gpu_event_counters() -> dict[str, int]:
    raw = shell("cat /sys/kernel/gpu/gpu_info", 15)
    return {k: int(v) for k, v in re.findall(r'"(\w+)":"(\d+)"', raw)}


def list_crash_records() -> list[str]:
    return shell("ls /data/log 2>/dev/null | grep -i lastkmsg", 30).split()


def pull_crash_records(names: list[str], dest: Path) -> list[str]:
    saved = []
    for name in names:
        if adb("pull", f"/data/log/{name}", str(dest / name), timeout=300).returncode == 0:
            saved.append(name)
    return saved


def driver_log(since: str) -> list[str]:
    lines = adb("logcat", "-d", "-v", "time", "-t", since, timeout=60).stdout.splitlines()
    keep = ("mesa", "radv", "amdgpu", "sgpu", "xclipse", "vulkan", "DEBUG", "libc")
    return [line for line in lines if any(k in line.lower() for k in keep)][-80:]


def run_test(driver: str, test: str, run_dir: Path, known_records: set[str],
             extra_env: list[str], *, library: str | None = None, cpu: int | None = None) -> dict:
    spec = library or ("stock" if driver == "stock" else f"{PHONE_DIR}/vulkan.radeon.so")
    assignments = []
    for value in (["MESA_SHADER_CACHE_DISABLE=true", *extra_env] if driver != "stock" else []):
        key, sep, val = value.partition("=")
        if not sep or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key):
            raise ValueError(f"invalid environment assignment: {value!r}")
        assignments.append(f"{key}={shlex.quote(val)}")
    if cpu is not None and not 0 <= cpu < 64:
        raise ValueError("CPU index must be between 0 and 63")
    prefix = f"taskset {1 << cpu:x} " if cpu is not None else ""
    command = (f"cd {PHONE_DIR} && {' '.join(assignments)} {prefix}timeout 30 ./xdtest "
               f"--driver {shlex.quote(spec)} --test {shlex.quote(test)} "
               f"--out {PHONE_DIR}/out --tag {shlex.quote(driver)}")
    since = shell("date '+%m-%d %H:%M:%S.000'", 15)
    result = {"test": test, "driver": driver,
              "gpu_temp_c_before": shell("cat /sys/kernel/gpu/gpu_tmu", 15)}
    counters_before = gpu_event_counters()
    try:
        p = adb("shell", command, timeout=90)
        stdout, stderr, code = p.stdout, p.stderr, p.returncode
    except subprocess.TimeoutExpired:
        stdout, stderr, code = "", "", None
    line = next((ln for ln in reversed(stdout.splitlines()) if ln.startswith("{")), None)
    if line:
        try:
            result.update(json.loads(line))
        except json.JSONDecodeError as e:
            result.update(status="crash", error=f"unreadable result: {e}")
    else:
        result.update(status="timeout" if code in (None, 124) else "crash",
                      error=f"no result (exit code {code})")
    if stderr.strip():
        result["progress"] = stderr.strip().splitlines()[-6:]

    if phone_state() != "device":
        result["status"] = "phone-reboot"
        result["phone_back"] = wait_for_phone()
        if result["phone_back"]:
            new = sorted(set(list_crash_records()) - known_records)
            result["crash_records"] = pull_crash_records(new, run_dir)
    else:
        counters_after = gpu_event_counters()
        # Without a snapshot from before the test, every counter would look like an event.
        events = {GPU_EVENT_COUNTERS.get(k, k): counters_after[k] - counters_before[k]
                  for k in counters_after if counters_after[k] > counters_before.get(k, counters_after[k])}
        if events:
            result["gpu_events"] = events
            if any(name in events for name in ("hang", "timeout", "page fault")):
                result["status"] = "gpu-hang"
        if result["status"] != "pass" and driver != "stock":
            result["driver_log"] = driver_log(since)
    return result


def ppm_to_png(ppm: Path) -> Path:
    header, size, _maxval, pixels = ppm.read_bytes().split(b"\n", 3)
    assert header == b"P6"
    w, h = map(int, size.split())
    rows = b"".join(b"\x00" + pixels[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff))

    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
    out = ppm.with_suffix(".png")
    out.write_bytes(png)
    ppm.unlink()
    return out


def pull_images(run_dir: Path) -> None:
    for name in shell(f"ls {PHONE_DIR}/out 2>/dev/null", 30).split():
        if name.endswith(".ppm") and adb("pull", f"{PHONE_DIR}/out/{name}",
                                         str(run_dir / name)).returncode == 0:
            ppm_to_png(run_dir / name)


def driver_identity() -> dict:
    head = run(["git", "-C", str(ROOT), "log", "-1", "--format=%h", "--", "driver"]).stdout.strip()
    dirty = run(["git", "-C", str(ROOT), "status", "--porcelain", "--", "driver"]).stdout.strip()
    sha = hashlib.sha256(DRIVER_SO.read_bytes()).hexdigest() if DRIVER_SO.exists() else ""
    return {"commit": head + ("+changes" if dirty else ""), "so_sha256": sha}


def timing(r: dict) -> str:
    for key in ("ms_gpu", "ms_empty_cmdbuf", "ms_device", "ms_instance"):
        if key in r:
            return f" ({key[3:]} {r[key]:.1f} ms)"
    return ""


def summarize(meta: dict, results: list[dict]) -> str:
    drivers = list(dict.fromkeys(r["driver"] for r in results))
    tests = list(dict.fromkeys(r["test"] for r in results))
    by = {(r["test"], r["driver"]): r for r in results}
    phone = meta["phone"]
    out = [f"# Test run {meta['started']}", "",
           f"- Driver: `driver/` at `{meta['driver']['commit']}`, "
           f"`vulkan.radeon.so` SHA-256 `{meta['driver']['so_sha256'][:16]}`",
           f"- Phone: {phone.get('model')} `{phone.get('build')}`, Android {phone.get('android')}, "
           f"GPU {phone.get('gpu_temp_c')} °C at the start"]
    if meta.get("env"):
        out.append(f"- Extra environment for our driver: `{' '.join(meta['env'])}`")
    out += ["",
           "| Test | " + " | ".join(drivers) + " |", "|---|" + "---|" * len(drivers)]
    for t in tests:
        cells = []
        for d in drivers:
            r = by.get((t, d))
            cells.append(STATUS_TEXT.get(r["status"], r["status"]) + timing(r) if r else "not run")
        out.append(f"| {t} | " + " | ".join(cells) + " |")

    infos = {d: by[("info", d)] for d in drivers if by.get(("info", d), {}).get("status") == "pass"}
    if infos:
        out += ["", "## What each driver exposes", "",
                "| | " + " | ".join(infos) + " |", "|---|" + "---|" * len(infos)]
        rows = [("GPU", "device_name"), ("Vulkan", "api_version"), ("Driver", "driver_name"),
                ("Driver info", "driver_info"), ("Vendor ID", "vendor_id"),
                ("Subgroup size", "subgroup_size"), ("Device extensions", "extension_count")]
        for label, key in rows:
            out.append(f"| {label} | " + " | ".join(str(i.get(key, "")) for i in infos.values()) + " |")
        for feature in KEY_FEATURES:
            out.append(f"| {feature} | " + " | ".join(
                "yes" if i.get("features", {}).get(feature) else "no" for i in infos.values()) + " |")
        for fmt in ("BC1_RGBA_UNORM", "BC3_UNORM", "BC5_UNORM", "BC7_UNORM"):
            out.append(f"| sample {fmt} | " + " | ".join(
                "yes" if int(i.get("format_optimal_features", {}).get(fmt, "0x0"), 16) & 1 else "no"
                for i in infos.values()) + " |")
        for ext in GAME_EXTENSIONS:
            present = ["yes" if ext in i.get("extensions", []) else "no" for i in infos.values()]
            if "yes" in present:
                out.append(f"| {ext} | " + " | ".join(present) + " |")

    perf_rows = []
    for test, metrics in PERF_METRICS.items():
        for key, label, higher_better in metrics:
            values = [by.get((test, d), {}).get(key) for d in drivers]
            if all(v is None for v in values):
                continue
            row = f"| {label} | " + " | ".join("" if v is None else f"{v:.3g}" for v in values)
            if len(drivers) == 2 and "stock" in drivers and None not in values and min(values) > 0:
                stock = values[drivers.index("stock")]
                ours = values[1 - drivers.index("stock")]
                ratio = ours / stock if higher_better else stock / ours
                row += f" | {ratio:.2f}x {'better' if ratio >= 1 else 'worse'}"
            perf_rows.append(row + " |")
    if perf_rows:
        out += ["", "## Performance", "",
                "Median of 7 runs after at least 250 ms of warm-up, timed with GPU timestamps.", "",
                "| Metric | " + " | ".join(drivers) + (" | " + drivers[-1] + " vs " + drivers[0]
                                                   if len(drivers) == 2 else "") + " |",
                "|---|" + "---|" * (len(drivers) + (1 if len(drivers) == 2 else 0))]
        out += perf_rows
        temps = [float(r["gpu_temp_c_before"]) for r in results
                 if r.get("gpu_temp_c_before", "").replace(".", "", 1).isdigit()]
        if temps:
            out += ["", f"GPU temperature before each test: {min(temps):.0f}-{max(temps):.0f} °C."]

    problems = [r for r in results if r["status"] not in ("pass", "skip")]
    if problems:
        out += ["", "## Problems", ""]
        for r in problems:
            out.append(f"- **{r['driver']} / {r['test']}**: {r['status']}"
                       + (f" - {r['error']}" if r.get("error") else ""))
            for line in r.get("progress", []):
                out.append(f"  - `{line}`")
            if r.get("gpu_events"):
                out.append("  - kernel GPU events: " + ", ".join(
                    f"{name} +{n}" for name, n in r["gpu_events"].items()))
            if r.get("crash_records"):
                out.append(f"  - crash records saved: {', '.join(r['crash_records'])}")
    return "\n".join(out) + "\n"


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-driver", action="store_true", help="rebuild the driver first")
    ap.add_argument("--tests", default=",".join(ALL_TESTS))
    ap.add_argument("--drivers", default="stock,radv")
    ap.add_argument("--keep-going", action="store_true",
                    help="keep testing our driver after it made the phone reboot")
    ap.add_argument("--env", action="append", default=[], metavar="KEY=VALUE",
                    help="environment variable for our driver's runs (repeatable), e.g. RADV_DEBUG=nongg")
    args = ap.parse_args()
    tests, drivers = args.tests.split(","), args.drivers.split(",")

    if args.build_driver:
        build_driver()
    build_tests()
    if "radv" in drivers and not DRIVER_SO.exists():
        sys.exit("no driver build yet; run with --build-driver")
    state = phone_state()
    if state != "device":
        sys.exit(f"phone not ready on adb (state: {state or 'not connected'})")

    started = dt.datetime.now()
    run_dir = RESULTS / started.strftime("%Y-%m-%d_%H%M%S")
    run_dir.mkdir(parents=True)
    deploy("radv" in drivers)
    known = set(list_crash_records())
    meta = {"started": started.isoformat(timespec="seconds"), "driver": driver_identity(),
            "phone": phone_context(), "tests": tests, "drivers": drivers, "env": args.env}

    # Test by test, and the driver order flips every test, so neither driver always runs on the
    # warmer GPU. The report is written even when a test or Ctrl+C ends the run early.
    results, stopped = [], set()
    try:
      for i, test in enumerate(tests):
        for driver in (drivers if i % 2 == 0 else drivers[::-1]):
            if driver in stopped:
                continue
            print(f"{driver:>6} {test:<9}", end=" ", flush=True)
            r = run_test(driver, test, run_dir, known, args.env)
            print(r["status"] + (f" - {r['error']}" if r.get("error") else ""), flush=True)
            results.append(r)
            if r["status"] in ("phone-reboot", "gpu-hang") and driver != "stock":
                if not args.keep_going:
                    print(f"no more {driver} tests this run after a {r['status']} (--keep-going continues)")
                    stopped.add(driver)
                if r["status"] == "gpu-hang":
                    health = run_test("stock", "submit", run_dir, known, [])
                    health["test"] = "health-check"
                    results.append(health)
                    print(f"  health check on the stock driver: {health['status']}", flush=True)
                    if health["status"] != "pass":
                        print("the GPU didn't recover; stopping. Reboot the phone before more tests.")
                        stopped.update(drivers)
    finally:
        if phone_state() == "device":
            pull_images(run_dir)
            meta["phone_after"] = phone_context()
        (run_dir / "results.json").write_text(json.dumps({"meta": meta, "results": results}, indent=2),
                                              encoding="utf-8", newline="\n")
        (run_dir / "summary.md").write_text(summarize(meta, results), encoding="utf-8", newline="\n")
        print(f"report: {run_dir / 'summary.md'}")


if __name__ == "__main__":
    main()
