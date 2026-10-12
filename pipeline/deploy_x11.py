#!/usr/bin/env python3
"""Deploy an X11 driver and its pinned library closure for native tests (no app/UI changes)."""
import argparse
import hashlib
import json
import re
import shlex
from pathlib import Path

import run

SDK = run.TOOLS / "gamenative-x11"
SYSTEM = {"libc.so", "libm.so", "libdl.so", "liblog.so", "libandroid.so", "libhardware.so",
          "libnativewindow.so", "libsync.so"}


def needed(path):
    result = run.run([str(run.NDK_BIN / f"llvm-readelf{run.EXE}"), "-d", str(path)])
    if result.returncode:
        raise RuntimeError(result.stderr)
    return re.findall(r"\(NEEDED\).*\[([^\]]+)\]", result.stdout)


def libraries(driver):
    pins = json.loads((SDK / "manifest.json").read_text())["runtime_library_sha256"]
    selected, pending = {}, needed(driver)
    while pending:
        name = pending.pop()
        if name in SYSTEM or name in selected:
            continue
        if name not in pins:
            raise RuntimeError(f"unresolved/unpinned library: {name}")
        path = SDK / "lib" / name
        if hashlib.sha256(path.read_bytes()).hexdigest() != pins[name]:
            raise RuntimeError(f"changed SDK library: {name}")
        selected[name] = path
        pending += needed(path)
    return selected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("driver", type=Path)
    parser.add_argument("--check-only", action="store_true")
    args = parser.parse_args()
    dependencies = libraries(args.driver)
    digest = hashlib.sha256(args.driver.read_bytes()).hexdigest()
    target = f"{run.PHONE_DIR}/x11-{digest[:16]}"
    if not args.check_only:
        if run.phone_state() != "device":
            parser.error("phone not ready")
        run.shell(f"mkdir -p {shlex.quote(target)}")
        for name, local in {**dependencies, "vulkan.radeon.so": args.driver}.items():
            remote = f"{target}/{name}"
            run.push(local, remote)
            if run.shell(f"sha256sum {shlex.quote(remote)}").split()[0] != hashlib.sha256(local.read_bytes()).hexdigest():
                raise RuntimeError(f"phone checksum mismatch: {name}")
    print(json.dumps({"driver_sha256": digest, "library": target + "/vulkan.radeon.so",
                      "env": {"LD_LIBRARY_PATH": target}, "dependencies": sorted(dependencies)}, indent=2))


if __name__ == "__main__":
    main()
