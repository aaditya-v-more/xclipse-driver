#!/usr/bin/env python3
"""Small adb helpers for driving an app's UI on the phone: list on-screen elements, tap, type.

usage: python pipeline/phone_ui.py dump [filter]
       python pipeline/phone_ui.py tap X Y | tap-text TEXT
       python pipeline/phone_ui.py type TEXT | clear | key KEYCODE | shot PATH

Commands go through one persistent `adb shell` (sh()), which saves adb's start-up on every call
(about 40 ms over USB, more over Wi-Fi); it falls back to one adb call per command if that shell
breaks. A uiautomator dump costs about 1 s of start-up plus at least 1 s waiting for the screen to
go idle, which no flag avoids (--compressed saves nothing measurable), so callers should dump as
rarely as they can.
"""

from __future__ import annotations

import os
import queue
import re
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run import ADB, adb, select_phone, shell  # noqa: E402

BOUNDS = re.compile(r"\[(-?\d+),(-?\d+)\]\[(-?\d+),(-?\d+)\]")
DUMP_FILE = "/data/local/tmp/xd_ui.xml"


class _PersistentShell:
    """One `adb shell` kept open; each command's output ends at a marker line it echoes."""

    def __init__(self) -> None:
        self.proc: subprocess.Popen | None = None
        self.lines: queue.Queue = queue.Queue()
        self.count = 0
        self.broken = False

    def _start(self) -> None:
        select_phone()
        self.proc = subprocess.Popen([ADB, "shell"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, bufsize=0)
        self.lines = queue.Queue()
        threading.Thread(target=self._read, args=(self.proc, self.lines), daemon=True).start()

    @staticmethod
    def _read(proc: subprocess.Popen, lines: queue.Queue) -> None:
        for line in iter(proc.stdout.readline, b""):
            lines.put(line)
        lines.put(None)

    def close(self) -> None:
        if self.proc:
            try:
                self.proc.kill()
            except OSError:
                pass
            self.proc = None

    def run(self, command: str, timeout: float) -> str | None:
        """The command's output (stdout and stderr), or None if the shell broke or timed out."""
        if self.broken:
            return None
        try:
            if self.proc is None or self.proc.poll() is not None:
                self._start()
            self.count += 1
            marker = f"__xd_done_{os.getpid()}_{self.count}__"
            self.proc.stdin.write(f"{{ {command}\n}} </dev/null 2>&1; echo; echo {marker}\n".encode())
            self.proc.stdin.flush()
        except OSError:
            self.close()
            return None
        out, deadline = [], time.monotonic() + timeout
        while True:
            try:
                line = self.lines.get(timeout=max(0.01, deadline - time.monotonic()))
            except queue.Empty:
                line = None
            if line is None:  # timed out, or adb exited: start a new shell next time
                self.close()
                return None
            if line.rstrip(b"\r\n") == marker.encode():
                text = b"".join(out).decode("utf-8", "replace")
                return text[:-1] if text.endswith("\n") else text  # the echo before the marker
            out.append(line)


_SHELL = _PersistentShell()


def sh(command: str, timeout: float = 60) -> str:
    """Runs `command` on the phone and returns its stripped output (stdout and stderr)."""
    out = _SHELL.run(command, timeout)
    if out is None:
        return shell(command, timeout)
    return out.strip()


_SIZE: dict = {}


def physical_size() -> tuple[int, int]:
    """The display's natural (rotation 0) width and height, read once per process."""
    if "wh" not in _SIZE:
        m = re.search(r"(\d+)x(\d+)", sh("wm size").splitlines()[-1])
        _SIZE["wh"] = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
    return _SIZE["wh"]


class Screen(list):
    """A dump's nodes, plus what's known about the window: `rotation` (0-3), the display's
    `width` and `height` in that rotation, the dumped window's `window` box and `package`, and
    the boxes of its `scrollables`."""
    rotation = 0
    width = height = 0
    window: tuple = (0, 0, 0, 0)
    package = ""
    scrollables: list = []


def parse(xml: str) -> Screen:
    """Every node that has text, a description, or is clickable. Each node also records `tapbox`
    (the box of the nearest clickable node at or above it, the area a tap on it activates),
    `selected` (it or a node above it is selected) and `scroll` (the box of the nearest scrollable
    node above it, or None)."""
    root = ET.fromstring(xml)
    screen = Screen()
    screen.rotation = int(root.get("rotation", "0") or 0)
    w, h = physical_size()
    screen.width, screen.height = (h, w) if screen.rotation % 2 else (w, h)
    screen.scrollables = []

    def walk(el, clickbox, selected, scroll) -> None:
        m = BOUNDS.match(el.get("bounds", ""))
        box = tuple(map(int, m.groups())) if m else None
        selected = selected or el.get("selected") == "true"
        click = el.get("clickable") == "true"
        if box and not screen.package:
            screen.package, screen.window = el.get("package", ""), box
        if box:
            if click:
                clickbox = box
            node = {"text": el.get("text", ""), "desc": el.get("content-desc", ""),
                    "id": el.get("resource-id", ""), "cls": el.get("class", "").split(".")[-1],
                    "click": click, "checked": el.get("checked") == "true",
                    "focused": el.get("focused") == "true", "selected": selected,
                    "x": (box[0] + box[2]) // 2, "y": (box[1] + box[3]) // 2, "box": box,
                    "tapbox": clickbox or box, "scroll": scroll}
            if node["text"] or node["desc"] or node["click"]:
                screen.append(node)
            if el.get("scrollable") == "true":
                screen.scrollables.append(box)
                scroll = box
        for child in el:
            if child.tag == "node":
                walk(child, clickbox, selected, scroll)

    for top in root:
        if top.tag == "node":
            walk(top, None, False, None)
    return screen


def dump(tries: int = 4) -> Screen:
    """The screen's nodes (see parse()).

    uiautomator fails now and then ("could not get idle state" while something animates) and
    leaves the previous dump in place, so the old file is removed first and a failure is retried.
    The dump and the read are one shell command.
    """
    out = ""
    for _ in range(tries):
        out = sh(f"rm -f {DUMP_FILE}; uiautomator dump {DUMP_FILE} >/dev/null 2>&1; "
                 f"cat {DUMP_FILE} 2>/dev/null", 60)
        start = out.find("<?xml")
        if start >= 0 and out.rstrip().endswith("</hierarchy>"):
            return parse(out[start:])
        time.sleep(0.5)
    raise RuntimeError(f"uiautomator dump failed {tries} times: {out[-200:]!r}")


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
    sh(f"input tap {x} {y}")


def focused_text() -> str | None:
    return next((n["text"] for n in dump() if n["cls"] == "EditText" and n["focused"]), None)


def key(code: str) -> None:
    """One key press, then a pause. Gboard follows the cursor asynchronously: in a burst of arrow
    keys it loses moves, and a delete that follows lands in the wrong place."""
    sh(f"input keyevent {code}")
    time.sleep(0.4)


def _input_text_cmd(text: str) -> str:
    """`input text` needs %s for spaces; the rest is shell-quoted."""
    escaped = text.replace("%", "\\%").replace(" ", "%s").replace("'", "'\\''")
    return f"input text '{escaped}'"


def input_text(text: str) -> None:
    sh(_input_text_cmd(text), 60)


def clear() -> str | None:
    """Empties the focused field. Returns what's left in it. MOVE_END only reaches the end of a
    wrapped line, so it deletes backwards and then forwards."""
    for _ in range(3):
        n = len(focused_text() or "")
        if not n:
            break
        sh("input keyevent KEYCODE_MOVE_END " + "KEYCODE_DEL " * (n + 5)
           + "KEYCODE_FORWARD_DEL " * (n + 5), 120)
    return focused_text()


PUNCT = ".,:;!?"


def type_fast(text: str, existing: int = 0) -> None:
    """Deletes `existing` characters and types `text` into the focused field in one shell command,
    without reading the field back: the caller checks the result with one dump.

    Gboard puts a space after '.' or ',' when a letter or digit is typed next. Here the character
    after such punctuation is typed first, then the cursor steps back over it, the punctuation
    goes in before it, and the cursor returns to the end, so Gboard never sees punctuation
    followed by typing.
    """
    cmds = []
    if existing:  # MOVE_END stops at the end of a wrapped line, so delete forwards as well
        cmds.append("input keyevent KEYCODE_MOVE_END " + "KEYCODE_DEL " * (existing + 5)
                    + "KEYCODE_FORWARD_DEL " * (existing + 5))
    buf, i = "", 0
    while i < len(text):
        if text[i] in PUNCT and i + 1 < len(text) and text[i + 1].isalnum():
            cmds += [_input_text_cmd(buf + text[i + 1]), "input keyevent KEYCODE_DPAD_LEFT",
                     _input_text_cmd(text[i]), "input keyevent KEYCODE_MOVE_END"]
            buf, i = "", i + 2
        else:
            buf, i = buf + text[i], i + 1
    if buf:
        cmds.append(_input_text_cmd(buf))
    if cmds:
        sh("; ".join(cmds), 120)


def type_text(text: str) -> str | None:
    """Types at the end of the focused field and returns the field's final text, checking each
    step (slow: two dumps per punctuation mark; type_fast() is the quick way).

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
        sh(f"input keyevent {rest[0]}")
    elif cmd == "shot":
        screenshot(rest[0])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
