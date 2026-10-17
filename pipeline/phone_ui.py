#!/usr/bin/env python3
"""Small adb helpers for driving an app's UI on the phone: list on-screen elements, tap, type.

usage: python pipeline/phone_ui.py dump [filter]
       python pipeline/phone_ui.py tap X Y | tap-text TEXT
       python pipeline/phone_ui.py type TEXT | clear | key KEYCODE | shot PATH
"""

from __future__ import annotations

import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run import ADB, adb, select_phone, shell  # noqa: E402

BOUNDS = re.compile(r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]")


def dump(tries: int = 4) -> list[dict]:
    """Every node on screen that has text, a description, or is clickable.

    uiautomator fails now and then ("could not get idle state" while something animates) and
    leaves the previous dump in place, so the old file is removed first and a failure is retried.
    """
    for _ in range(tries):
        out = shell("rm -f /sdcard/xd_ui.xml; uiautomator dump /sdcard/xd_ui.xml 2>&1", 60)
        if "dumped to" in out:
            break
        time.sleep(1.5)
    else:
        raise RuntimeError(f"uiautomator dump failed {tries} times: {out[-200:]!r}")
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


def find(nodes: list[dict], text: str, contains: bool = True) -> dict | None:
    """First node whose text or description equals `text`; with `contains`, else the first that
    contains it."""
    t = text.lower()
    for exact in ((True, False) if contains else (True,)):
        for n in nodes:
            for value in (n["text"], n["desc"]):
                v = value.lower()
                if v and (v == t if exact else t in v):
                    return n
    return None


def tap(x: int, y: int) -> None:
    shell(f"input tap {x} {y}")


def focused_text() -> str | None:
    return next((n["text"] for n in dump() if n["cls"] == "EditText" and n["focused"]), None)


def key(code: str) -> None:
    """One key press, then a pause. Gboard follows the cursor asynchronously: in a burst of arrow
    keys it loses moves, and a delete that follows lands in the wrong place."""
    shell(f"input keyevent {code}")
    time.sleep(0.4)


def input_text(text: str) -> None:
    """`input text` needs %s for spaces; the rest is shell-quoted."""
    escaped = text.replace("%", "\\%").replace(" ", "%s").replace("'", "'\\''")
    shell(f"input text '{escaped}'", 60)


def clear() -> str | None:
    """Empties the focused field. Returns what's left in it."""
    for _ in range(3):
        n = len(focused_text() or "")
        if not n:
            break
        shell("input keyevent KEYCODE_MOVE_END " + "KEYCODE_DEL " * (n + 5), 120)
    return focused_text()


def type_text(text: str) -> str | None:
    """Types at the end of the focused field and returns the field's final text.

    Gboard inserts a space when a letter follows '.' or ',' (as in "unigine.cpp" or "A,B"), even
    in typed key events. So the text goes in pieces split after such punctuation; the first
    character after it is typed alone, and if Gboard put a space before it, that space is deleted
    before typing on.
    """
    done = focused_text() or ""
    for i, piece in enumerate(re.split(r"(?<=[.,:;!?])(?=[A-Za-z0-9])", text)):
        if i:
            input_text(piece[0])
            if focused_text() == done + " " + piece[0]:
                key("KEYCODE_DPAD_LEFT")
                key("KEYCODE_DEL")
                key("KEYCODE_MOVE_END")
            done += piece[0]
            if focused_text() != done:
                return focused_text()
            piece = piece[1:]
        if piece:
            input_text(piece)
            done += piece
    return focused_text()


def screenshot(path: str) -> None:
    select_phone()
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
        # Exact labels only, so "Import" can't land on "Import config"; --contains opts in.
        contains = "--contains" in rest
        rest = [r for r in rest if r != "--contains"]
        n = find(dump(), " ".join(rest), contains=contains)
        if not n:
            sys.exit(f"not on screen: {' '.join(rest)!r}")
        tap(n["x"], n["y"])
        print(f"tapped {n['text'] or n['desc']!r} at {n['x']},{n['y']}")
    elif cmd == "type":
        want = " ".join(rest)
        got = type_text(want)
        if got != want:
            sys.exit(f"the field reads {got!r}, not {want!r}")
        print("typed and checked")
    elif cmd == "clear":
        left = clear()
        if left:
            sys.exit(f"the field still reads {left!r}")
        print("cleared")
    elif cmd == "key":
        shell(f"input keyevent {rest[0]}")
    elif cmd == "shot":
        screenshot(rest[0])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
