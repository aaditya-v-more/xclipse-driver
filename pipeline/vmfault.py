#!/usr/bin/env python3
"""Summarizes our driver's [VMFAULT] log: what each GPU page fault hit, and the code behind it.

With debug.radv_xclipse_log >= 1 (game.py --radv-log, or RADV_XCLIPSE_LOG=1 for xdtest), the
driver asks the kernel for its GPU page-fault addresses after every submission and logs each one
under the logcat tag RADV_VMFAULT: the buffer that holds the page, or the nearest buffers on either
side, and recently freed buffers that held it. Each buffer comes with the code that created it, as
offsets into the driver library.

This script reads those lines (from the phone's logcat, or a saved log), groups the faults by what
they hit, and names the creating code with llvm-addr2line. The library has to be the unstripped
build of the driver that logged the faults: the same build ID as the vulkan.radeon.so in its zip.

usage: python pipeline/vmfault.py [--since 'MM-DD hh:mm:ss.mmm'] [--file LOG] [--lib SO] [--save LOG]
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run import NDK_BIN, ROOT, EXE, adb  # noqa: E402

DEFAULT_LIB = ROOT / "driver" / "build-android" / "src" / "amd" / "vulkan" / "libvulkan_radeon.so"
WINSYS_H = ROOT / "driver" / "src" / "amd" / "vulkan" / "radv_radeon_winsys.h"

# "<date> <time> <pid> <tid> W RADV_VMFAULT: [VMFAULT] ..." (threadtime, logcat's default) or
# "<date> <time> W/RADV_VMFAULT(<pid>): [VMFAULT] ..." (-v time).
LINE = re.compile(r"^(?P<when>\S+ \S+)\s+(?:(?P<pid>\d+)\s+\d+\s+\w\s+RADV_VMFAULT\s*|\w/RADV_VMFAULT\(\s*(?P<pid2>\d+)\)):"
                  r"\s*\[VMFAULT\] (?P<msg>.*)$")
DESC = (r"va 0x(?P<va>[0-9a-f]+)-0x(?P<end>[0-9a-f]+) size 0x(?P<size>[0-9a-f]+) (?P<kind>\S+) "
        r"flags 0x(?P<flags>[0-9a-f]+) dom (?P<dom>\d+) prio (?P<prio>\d+) "
        r"owner (?P<owner>\S+)(?P<internal> \(internal\))? 0x(?P<handle>[0-9a-f]+) "
        r"by \+0x(?P<by>[0-9a-f]+) winsys \+0x(?P<ws>[0-9a-f]+) (?P<agekind>age|lived) (?P<age>[\d.]+) s"
        r"(?:, parked in the IB cache (?P<parked>[\d.]+) s before)?")
PAGE = re.compile(r"^#(\d+) page 0x([0-9a-f]+) t=([\d.]+) live=(\d+) freed=(\d+)$")
IN_BO = re.compile(rf"^#(\d+) in {DESC}, at \+0x(?P<at>[0-9a-f]+)$")
PAST = re.compile(rf"^#(\d+) no live BO; 0x(?P<gap>[0-9a-f]+) past the end of {DESC}$")
BEFORE = re.compile(rf"^#(\d+) no live BO; 0x(?P<gap>[0-9a-f]+) before {DESC}$")
FREED = re.compile(rf"^#(\d+) freed (?P<ago>[\d.]+) s before: {DESC}, at \+0x(?P<at>[0-9a-f]+)$")
NONE = re.compile(r"^#(\d+) no live BO at all$")


def bit_names(prefix: str) -> dict[int, str]:
    """RADEON_FLAG_* or RADEON_DOMAIN_* values, from the winsys header."""
    names = {}
    for name, value in re.findall(rf"\b{prefix}(\w+)\s*=\s*([^,]+),", WINSYS_H.read_text(encoding="utf-8")):
        value = value.strip()
        m = re.fullmatch(r"\(1 << (\d+)\)", value)
        if m:
            names[1 << int(m.group(1))] = name
        elif value.isdigit():
            names[int(value)] = name
    return names


_NAMES: dict[str, dict[int, str]] = {}


def names(prefix: str) -> dict[int, str]:
    """The driver header's bit names for a prefix, read on first use (so a checkout without
    `driver/src` can still import this module)."""
    if prefix not in _NAMES:
        _NAMES[prefix] = bit_names(prefix)
    return _NAMES[prefix]


def decode(bits: int, names: dict[int, str]) -> str:
    out = [n for b, n in sorted(names.items()) if b & (b - 1) == 0 and bits & b]
    rest = bits & ~sum(b for b in names if b & (b - 1) == 0)
    return "|".join(out + ([hex(rest)] if rest else [])) or "0"


def read_lines(args) -> list[str]:
    if args.file:
        return Path(args.file).read_text(encoding="utf-8", errors="replace").splitlines()
    cmd = ["logcat", "-d", "-b", "main", "-v", "threadtime"]
    if args.since:
        cmd += ["-T", args.since]
    cmd += ["-s", "RADV_VMFAULT"]
    return adb(*cmd, timeout=120).stdout.splitlines()


def parse(lines: list[str]) -> tuple[list[dict], list[str]]:
    """Faults in log order, and the logger's other messages."""
    faults, by_key, notes = [], {}, []
    for line in lines:
        m = LINE.match(line.strip())
        if not m:
            continue
        pid, msg = m["pid"] or m["pid2"], m["msg"]
        if p := PAGE.match(msg):
            f = {"pid": pid, "id": int(p[1]), "when": m["when"], "page": int(p[2], 16), "t": float(p[3]),
                 "live": int(p[4]), "in": [], "below": None, "above": None, "freed": [], "older_freed": 0}
            by_key[(pid, f["id"])] = f
            faults.append(f)
            continue
        fid = re.match(r"^#(\d+) ", msg)
        f = by_key.get((pid, int(fid[1]))) if fid else None
        if not f:
            notes.append(f"{m['when']} pid {pid}: {msg}")
        elif x := IN_BO.match(msg):
            f["in"].append(x.groupdict())
        elif x := PAST.match(msg):
            f["below"] = x.groupdict()
        elif x := BEFORE.match(msg):
            f["above"] = x.groupdict()
        elif x := FREED.match(msg):
            f["freed"].append(x.groupdict())
        elif x := re.match(r"^#\d+ and (\d+) older freed BOs$", msg):
            f["older_freed"] = int(x[1])
        elif not NONE.match(msg):
            notes.append(f"{m['when']} pid {pid}: {msg}")
    return faults, notes


def symbolize(lib: Path, offsets: set[int]) -> dict[int, str]:
    """Return addresses -> 'function (file:line)', inlined frames outermost last. The offsets are
    return addresses, so each is looked up 4 bytes back, on the call instruction (AArch64)."""
    offsets = sorted(o for o in offsets if o)
    if not offsets:
        return {}
    tool = NDK_BIN / f"llvm-addr2line{EXE}"
    out = subprocess.run([str(tool), "--output-style=JSON", "-f", "-C", "-i", "-e", str(lib),
                          *(hex(o - 4) for o in offsets)], capture_output=True, text=True, check=True).stdout
    names = {}
    for off, entry in zip(offsets, json.loads(out)):
        frames = [f"{s['FunctionName']} ({Path(s['FileName']).name}:{s['Line']})" for s in entry.get("Symbol", [])
                  if s.get("FunctionName")]
        names[off] = " < ".join(frames) or "?"
    return names


def build_id(lib: Path) -> str:
    out = subprocess.run([str(NDK_BIN / f"llvm-readelf{EXE}"), "-n", str(lib)], capture_output=True, text=True).stdout
    m = re.search(r"Build ID: ([0-9a-f]+)", out)
    return m[1] if m else "?"


def what(f: dict) -> tuple[str, dict | None]:
    """How a fault relates to the buffers: the relation and the buffer it's about."""
    if f["in"]:
        return "inside a live BO", f["in"][0]
    if f["freed"]:
        return "inside a freed BO (no live BO there)", f["freed"][0]
    if f["below"] and (not f["above"] or int(f["below"]["gap"], 16) <= int(f["above"]["gap"], 16)):
        return "past the end of a live BO", f["below"]
    if f["above"]:
        return "before a live BO", f["above"]
    return "no BO anywhere", None


def describe(d: dict, names: dict[int, str]) -> list[str]:
    va, end = int(d["va"], 16), int(d["end"], 16)
    return [f"BO va 0x{va:012x}-0x{end:012x} (size 0x{int(d['size'], 16):x}, {d['kind']}, "
            f"dom {decode(int(d['dom']), names("RADEON_DOMAIN_"))}, prio {d['prio']})",
            f"flags {decode(int(d['flags'], 16), names("RADEON_FLAG_"))}",
            f"owner {d['owner']}{d['internal'] or ''} 0x{d['handle']}",
            f"made by {names.get(int(d['by'], 16), '-')}",
            f"winsys call from {names.get(int(d['ws'], 16), '-')}"]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--since", help="logcat -T time, e.g. '10-11 12:29:08.000' (phone time)")
    ap.add_argument("--file", help="read a saved log instead of the phone's logcat")
    ap.add_argument("--lib", default=str(DEFAULT_LIB), help="unstripped libvulkan_radeon.so of that build")
    ap.add_argument("--save", help="also write the raw [VMFAULT] lines here")
    args = ap.parse_args()

    lines = read_lines(args)
    if args.save:
        Path(args.save).write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    faults, notes = parse(lines)
    lib = Path(args.lib)
    print(f"{len(faults)} GPU page faults; library {lib} (build ID {build_id(lib)})")
    for n in notes[:10]:
        print(f"  {n}")
    if len(notes) > 10:
        print(f"  ... {len(notes) - 10} more messages")
    if not faults:
        return

    groups = defaultdict(list)
    for f in faults:
        rel, d = what(f)
        key = (rel,) + ((d["owner"], d["kind"], d["flags"], d["size"], d["by"], d["ws"]) if d else ())
        groups[key].append(f)
    offsets = {int(d[k], 16) for f in faults for d in [*f["in"], *f["freed"], f["below"], f["above"]] if d
               for k in ("by", "ws")}
    names = symbolize(lib, offsets)

    for key, fs in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        first = fs[0]
        rel, d = what(first)
        pages = sorted({f["page"] for f in fs})
        print(f"\n{len(fs)} faults {rel}; {len(pages)} distinct pages, "
              f"t {fs[0]['t']:.3f}-{fs[-1]['t']:.3f}, pids {','.join(sorted({f['pid'] for f in fs}))}")
        print(f"  pages: {', '.join(f'0x{p:012x}' for p in pages[:8])}{' ...' if len(pages) > 8 else ''}")
        if d:
            if "gap" in d:
                gaps = sorted({int(what(f)[1]['gap'], 16) for f in fs})
                print(f"  gap to the BO: 0x{gaps[0]:x}-0x{gaps[-1]:x}")
            if "at" in d:
                ats = sorted({int(what(f)[1]['at'], 16) for f in fs})
                print(f"  offset in the BO: 0x{ats[0]:x}-0x{ats[-1]:x}")
            for s in describe(d, names):
                print(f"  {s}")
        if first["freed"] and rel != "inside a freed BO (no live BO there)":
            print(f"  also held by a freed BO: {' / '.join(describe(first['freed'][0], names)[:4])}")


if __name__ == "__main__":
    main()
