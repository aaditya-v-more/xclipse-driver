#!/usr/bin/env python3
"""Small adb helpers for driving an app's UI on the phone: list on-screen elements, tap, type.

usage: tools\\python\\python.exe pipeline\\phone_ui.py dump [filter]
       tools\\python\\python.exe pipeline\\phone_ui.py tap X Y | tap-text TEXT
       tools\\python\\python.exe pipeline\\phone_ui.py type TEXT | key KEYCODE | shot PATH
"""

from __future__ import annotations

import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run import ADB, adb, shell  # noqa: E402

BOUNDS = re.compile(r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]")


def dump() -> list[dict]:
    """Every node on screen that has text, a description, or is clickable."""
    shell("uiautomator dump /sdcard/xd_ui.xml >/dev/null 2>&1", 60)
    xml = shell("cat /sdcard/xd_ui.xml", 30)
    nodes = []
    for el in ET.fromstring(xml).iter("node"):
        m = BOUNDS.match(el.get("bounds", ""))
        if not m:
            continue
        x1, y1, x2, y2 = map(int, m.groups())
        node = {"text": el.get("text", ""), "desc": el.get("content-desc", ""),
                "id": el.get("resource-id", ""), "cls": el.get("class", "").split(".")[-1],
                "click": el.get("clickable") == "true", "checked": el.get("checked") == "true",
                "focused": el.get("focused") == "true", "x": (x1 + x2) // 2, "y": (y1 + y2) // 2,
                "box": (x1, y1, x2, y2)}
        if node["text"] or node["desc"] or node["click"]:
            nodes.append(node)
    return nodes


def find(nodes: list[dict], text: str) -> dict | None:
    """First node whose text or description equals `text`, else the first that contains it."""
    t = text.lower()
    for exact in (True, False):
        for n in nodes:
            for value in (n["text"], n["desc"]):
                v = value.lower()
                if v and (v == t if exact else t in v):
                    return n
    return None


def tap(x: int, y: int) -> None:
    shell(f"input tap {x} {y}")


def type_text(text: str) -> None:
    """Types into the focused field. `input text` needs %s for spaces; the rest is shell-quoted."""
    escaped = text.replace("%", "\\%").replace(" ", "%s").replace("'", "'\\''")
    shell(f"input text '{escaped}'", 60)


def screenshot(path: str) -> None:
    with open(path, "wb") as f:
        subprocess.run([ADB, "exec-out", "screencap", "-p"], stdout=f, check=True)


def main() -> None:
    cmd, rest = (sys.argv[1], sys.argv[2:]) if len(sys.argv) > 1 else ("dump", [])
    if cmd == "dump":
        flt = rest[0].lower() if rest else ""
        for n in dump():
            label = n["text"] or n["desc"]
            if flt and flt not in (label + n["id"]).lower():
                continue
            flags = "".join(f for f, on in (("C", n["click"]), ("V", n["checked"]),
                                             ("F", n["focused"])) if on)
            print(f"{n['x']:5d},{n['y']:<5d} {flags:3} {n['cls']:<14.14} {label[:70]!r} {n['id'][-30:]}")
    elif cmd == "tap":
        tap(int(rest[0]), int(rest[1]))
    elif cmd == "tap-text":
        n = find(dump(), " ".join(rest))
        if not n:
            sys.exit(f"not on screen: {' '.join(rest)!r}")
        tap(n["x"], n["y"])
        print(f"tapped {n['text'] or n['desc']!r} at {n['x']},{n['y']}")
    elif cmd == "type":
        type_text(" ".join(rest))
    elif cmd == "key":
        shell(f"input keyevent {rest[0]}")
    elif cmd == "shot":
        screenshot(rest[0])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
