#!/usr/bin/env python3
"""Puts a build of our driver into GameNative and changes container settings, over adb.

GameNative has no command line for this, so the script taps through the app's screens. It works out
which screen is showing from one uiautomator dump (a few labels per screen, the same in portrait and
landscape) and walks a graph of known moves to the screen it needs: library, Game Information, its
Quick Actions, the container editor and its tabs, Settings, Driver Manager, Contents Manager.
GameNative is restarted only when the screen is unknown (a game running, say), another app is in
front at the start, or a route fails. After a restart it passes the Steam sign-in screen with "Skip
login" (it never signs in), the update offer with "Later", and the membership prompt with "Close".

Every tap goes to an element found by its exact label, or by its place relative to one, in a fresh
dump, and every change is read back after saving. Positions, swipe paths and scroll areas come from
the dump's own bounds, so either orientation works. The script refuses to tap anything labeled
Uninstall, Reset container, Import config, Delete variable, Delete (Driver Manager's trash icons),
Clear Preferences, Clear Local Database or Update, or anything the on-screen keyboard covers.

Nobody may touch the phone while it runs, and nothing else may drive it (game.py included). Turn on
Do Not Disturb first: a notification that pops up between a dump and a tap gets the tap. The script
stops if another app comes to the front. The screen is kept on while it runs, and the setting is
put back afterwards.

usage: python pipeline/gn_driver.py [--stats] COMMAND ...
       python pipeline/gn_driver.py apply GAME [--driver ENTRY] [--set "LABEL=VALUE" ...]
                                    [--env NAME=VALUE ...] [--args TEXT] [--launch]
       python pipeline/gn_driver.py launch GAME
       python pipeline/gn_driver.py install ZIP GAME [GAME...]
       python pipeline/gn_driver.py select ENTRY GAME [GAME...]
       python pipeline/gn_driver.py show GAME [GAME...]
       python pipeline/gn_driver.py args GAME [GAME...]
       python pipeline/gn_driver.py get GAME LABEL [--tab TAB]
       python pipeline/gn_driver.py options GAME LABEL [--tab TAB]
       python pipeline/gn_driver.py set GAME LABEL VALUE [--tab TAB]
       python pipeline/gn_driver.py import-content WCP
       python pipeline/gn_driver.py export-config GAME DESTINATION
       python pipeline/gn_driver.py env GAME NAME VALUE
       python pipeline/gn_driver.py envs GAME NAME=VALUE [NAME=VALUE...]

apply makes several changes to GAME's container in one editor session with one save, then reopens
it and reads every change back: --driver picks the Graphics Driver Version, --set picks a dropdown
value (repeatable; "FEXCore Preset" and "Box64 Preset" are found on the Emulation tab, other labels
on Graphics, and a "TAB:LABEL=VALUE" prefix names the tab), --env adds or updates a text-valued
environment variable (repeatable), --args sets Exec Arguments, and --launch starts the game after.
Values that already match are left alone. launch starts GAME from its Game Information screen.
install copies ZIP (a build from driver/dist) to the phone's Download/xclipse folder, imports it in
Driver Manager unless it's there already, and makes it the Graphics Driver Version of each GAME (a
custom game's name in the library, such as Heaven-radv). select does the last step for an entry
that's already installed, named as the dropdown shows it ("RADV Xclipse (Mesa ...)", "System").
show prints each GAME's Graphics Driver Version and changes nothing. args sets each GAME's Exec
Arguments to game.py's EXEC_ARGS for it (GAME is <bench>-<driver>, such as GravityMark-radv).
get reads the value under LABEL; options prints its dropdown choices; set picks VALUE, saves, and
reads it back. These use the Graphics tab by default (such as "BCn Emulation"); --tab Emulation
reaches "FEXCore Preset" and "Box64 Preset". get and options change nothing.
import-content copies a DXVK WCP to Download/xclipse, verifies its checksum, imports it through
Contents Manager, and reopens the installed list to verify it. It does not select a DXVK version.
export-config saves a container's JSON to Download/xclipse and pulls it to DESTINATION on the PC.
env adds or updates a text-valued environment variable, saves, and verifies its value after reopening.
envs does the same for several variables with one save and one verification pass.
--stats prints the number of dumps, taps and restarts and the time taken to stderr.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import sys
import tarfile
import tempfile
import time
import zipfile
from collections import deque
from pathlib import Path
from typing import Callable, NamedTuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game import EXEC_ARGS  # noqa: E402
from phone_ui import Screen, clear, dump, sh, type_fast, type_text  # noqa: E402
from run import STAY_ON_CMD, adb  # noqa: E402

APP = "app.gamenative"
PHONE_DIR = "/sdcard/Download/xclipse"  # where the file picker opens
NEVER_TAP = {"uninstall", "reset container", "import config", "delete variable", "delete",
             "clear preferences", "clear local database", "update"}
TABS = ("General", "Graphics", "Emulation", "Controller", "Wine", "Win Components", "Environment",
        "Drives", "Advanced")
TAB_MARKERS = {"Graphics": "Graphics Driver Version", "Emulation": "FEXCore Version"}
LABEL_TABS = {"FEXCore Preset": "Emulation", "Box64 Preset": "Emulation",
              "FEXCore Version": "Emulation", "Box64 Version": "Emulation",
              "Exec Arguments": "General", "Executable Path": "General",
              "Container Variant": "General", "Wine Version": "General"}
DRIVER_LABEL = "Graphics Driver Version"
ADD_ENV = "Add environment variable"
ENV_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
STATS = {"dumps": 0, "taps": 0, "restarts": 0}
DEBUG = bool(os.environ.get("GN_DEBUG"))  # log each screen identified, to stderr
T0 = time.monotonic()


def label(n: dict) -> str:
    return n["text"] or n["desc"]


def find(nodes: list[dict], text: str) -> dict | None:
    return next((n for n in nodes if text in (n["text"], n["desc"])), None)


def front_app() -> str:
    """The package of the app in front (its activity has focus, even under its own popups)."""
    return sh("dumpsys window | grep mFocusedApp=").split(" u0 ")[-1].split("/")[0]


def area(box: tuple) -> int:
    return max(0, box[2] - box[0]) * max(0, box[3] - box[1])


def inside(x: int, y: int, box: tuple) -> bool:
    return box[0] <= x <= box[2] and box[1] <= y <= box[3]


def height(n: dict) -> int:
    return max(1, n["box"][3] - n["box"][1])


def in_view(n: dict, s: Screen) -> bool:
    """Whether `n` is wholly on screen: not clipped by the list it scrolls in (a clipped node's box
    ends at the list's edge) and not under a bar or button that sits over the list."""
    sc = n["scroll"]
    if not sc:
        return True
    x1, y1, x2, y2 = n["box"]
    if sc[2] - sc[0] > 2 * (sc[3] - sc[1]):  # a row that scrolls sideways
        clipped = x1 <= sc[0] or x2 >= sc[2] or y1 < sc[1] or y2 > sc[3]
    else:
        clipped = y1 <= sc[1] or y2 >= sc[3] or x1 < sc[0] or x2 > sc[2]
    if clipped:
        return False
    limit = s.width * s.height // 4
    return not any(o["scroll"] is None and (label(o) or o["click"]) and area(o["box"]) < limit
                   and inside(n["x"], n["y"], o["box"]) for o in s)


def find_view(s: Screen, text: str) -> dict | None:
    """The first node labeled exactly `text` that's wholly on screen."""
    return next((n for n in s if text in (n["text"], n["desc"]) and in_view(n, s)), None)


class State(NamedTuple):
    kind: str
    game: str | None = None
    tab: str | None = None
    dirty: bool = False

    def key(self) -> tuple:
        return self.kind, self.game, self.tab


def matches(state: State, target: State) -> bool:
    return (state.kind == target.kind and (target.game is None or state.game == target.game)
            and (target.tab is None or state.tab == target.tab))


def identify(s: Screen, game: str | None = None, last_game: str | None = None) -> State:
    """Which screen `s` is. `game` is the game being looked for (Game Information is matched
    against it); `last_game` is the game whose screens were showing before (Quick Actions
    doesn't name its game)."""
    if s.package and s.package != APP:
        return State("picker" if s.package.endswith("documentsui") else "other")

    def has(t: str) -> bool:
        return find(s, t) is not None

    if has("Skip login"):
        return State("signin")
    if has("Enjoying GameNative?"):
        return State("membership")
    if has("Later"):
        return State("update")
    if has("New Environment Variable"):
        return State("env_dialog", last_game)
    title = next((n for n in s if n["text"].endswith((" Config", " Config*"))), None)
    # Settings has "Modify Default Config" and an "Emulation" heading: count the tabs.
    if title and len({label(n) for n in s if label(n) in TABS}) >= 3:
        tab = next((label(n) for n in s if label(n) in TABS and n["selected"]), None)
        name = title["text"].rsplit(" Config", 1)[0]
        return State("editor", name, tab, title["text"].endswith("*"))
    if has("Quick Actions"):
        return State("quick_actions", last_game or "?")
    if has("Customize your experience"):
        return State("settings")
    # The import buttons scroll out of view in landscape; the dialog's title and Close stay.
    if has("Import ZIP from device") or (has("Driver Manager") and has("Close")):
        return State("driver_manager")
    if has("Import .wcp from device") or (has("Contents Manager") and has("Close")):
        return State("contents_manager")
    if has("Close options"):
        return State("menu")
    if has("Play") and has("Uninstall") and has("Options"):
        return State("game_info", game if game and has(game) else "?")
    if has("Add Game") and has("Menu"):
        return State("library")
    if s.window and area(s.window) < s.width * s.height * 9 // 10:
        return State("popup")
    # A game: its loading screen, or the performance overlay GameNative draws over it.
    if has("Launching game...") or any(re.fullmatch(r"FPS [\d.]+", label(n)) for n in s):
        return State("in_game")
    return State("unknown")


def neighbors(st: State, game: str | None):
    """(action, next state) for each move out of `st` that the graph knows."""
    k = st.kind
    if k == "library":
        if game:
            yield ("open_game", game), State("game_info", game)
        yield ("tap", "Menu"), State("menu")
    elif k == "menu":
        yield ("tap", "Settings"), State("settings")
        yield ("tap", "Close options"), State("library")
    elif k == "settings":
        yield ("seek_tap", "Driver Manager"), State("driver_manager")
        yield ("seek_tap", "Contents Manager"), State("contents_manager")
        yield ("tap", "Back"), State("library")
    elif k in ("driver_manager", "contents_manager"):
        yield ("tap", "Close"), State("settings")
    elif k == "game_info":
        if st.game == game:
            yield ("tap", "Options"), State("quick_actions", game)
        yield ("tap", "Back"), State("library")
    elif k == "quick_actions":
        if st.game == game:
            yield ("tap", "Edit container"), State("editor", game, "General")
        yield ("key", "KEYCODE_BACK"), State("game_info", st.game)
    elif k == "editor":
        for tab in TABS:
            if tab != st.tab:
                yield ("tab", tab), State("editor", st.game, tab)
        if not st.dirty:
            yield ("close",), State("game_info", st.game)


def route(start: State, target: State) -> list[tuple]:
    """The moves from `start` to a state matching `target` (breadth-first), or []."""
    seen, todo = {start.key()}, deque([(start, [])])
    while todo:
        st, path = todo.popleft()
        for action, nxt in neighbors(st, target.game):
            if matches(nxt, target):
                return path + [action]
            if nxt.key() not in seen and len(path) < 12:
                seen.add(nxt.key())
                todo.append((nxt, path + [action]))
    return []


def title_buttons(s: Screen) -> tuple[dict, dict]:
    """The container screen's unlabeled buttons beside its title: close (left) and save (right)."""
    title = next((n for n in s if n["text"].endswith((" Config", " Config*"))), None)
    if not title:
        sys.exit("no container title on screen")
    top = sorted((n for n in s if n["cls"] == "Button" and n["click"]
                  and abs(n["y"] - title["y"]) <= height(title)), key=lambda n: n["x"])
    if len(top) != 2:
        sys.exit(f"expected 2 buttons beside the container title, found {len(top)}")
    return top[0], top[1]


def value_node(s: Screen, lab: dict) -> dict | None:
    """The value shown under the label `lab`: the nearest text below it in the same row (the
    clickable area around both), else within a couple of label heights below and to its right."""
    h = height(lab)
    below = [n for n in s if n["text"] and n is not lab and n["cls"] != "EditText"
             and 0 < n["y"] - lab["y"] <= 3 * h and n["box"][0] < lab["box"][2]]
    row = [n for n in below if n["tapbox"] == lab["tapbox"] and area(lab["tapbox"]) > area(lab["box"])]
    pick = row or below
    return min(pick, key=lambda n: n["y"]) if pick else None


def field_value(s: Screen, text: str) -> str:
    """The value shown under the label `text`."""
    lab = find(s, text)
    if not lab:
        sys.exit(f"no {text!r} on screen")
    n = value_node(s, lab)
    return n["text"] if n else ""


def row_edit(s: Screen, lab: dict) -> dict | None:
    """The text field in the same row as the label `lab` (environment variables)."""
    row = lab["tapbox"] if area(lab["tapbox"]) > area(lab["box"]) else None
    fields = [n for n in s if n["cls"] == "EditText"
              and (inside(n["x"], n["y"], row) if row else abs(n["y"] - lab["y"]) < 1.5 * height(lab))]
    return fields[0] if len(fields) == 1 else None


def args_field(s: Screen, required: bool = True) -> dict | None:
    """The Exec Arguments text field (its label sits on its border, just above it, or inside it
    while empty)."""
    lab = find(s, "Exec Arguments")
    h = height(lab) if lab else 0
    near = [n for n in s if lab and n["cls"] == "EditText"
            and (inside(lab["x"], lab["y"], n["box"]) or 0 < n["y"] - lab["y"] <= 4 * h)]
    if not near:
        if required:
            sys.exit("no Exec Arguments field on screen")
        return None
    return min(near, key=lambda n: abs(n["y"] - lab["y"]))


def args_in_view(s: Screen) -> dict | None:
    n = args_field(s, required=False)
    return n if n and in_view(n, s) else None


def guarded(command: str) -> str:
    """`command`, run on the phone only if GameNative (or the document picker) is in front."""
    return ('f=$(dumpsys window | grep -m1 mFocusedApp=); case "$f" in '
            f'*" {APP}/"*|*documentsui/*) {command}; echo XD_OK;; *) echo "XD_FRONT $f";; esac')


class Nav:
    """Moves around GameNative, keeping the last dump until something is done to the screen."""

    def __init__(self) -> None:
        self._screen: Screen | None = None
        self.acted = False  # tapped, swiped or typed since this command started
        self.last_game: str | None = None
        self.restarted = 0

    # Looking ---------------------------------------------------------------------------------
    def look(self) -> Screen:
        if self._screen is None:
            self._screen = dump()
            STATS["dumps"] += 1
        return self._screen

    def changed(self) -> None:
        self._screen = None

    def state(self, game: str | None = None) -> State:
        st = identify(self.look(), game, self.last_game)
        if DEBUG:
            print(f"  [{time.monotonic() - T0:6.1f} s] {st.kind} {st.game or ''} {st.tab or ''}"
                  f"{' *' if st.dirty else ''}", file=sys.stderr, flush=True)
        if st.kind in ("game_info", "editor") and st.game != "?":
            self.last_game = st.game
        return st

    def wait(self, ok: Callable[[State], bool], game: str | None = None, limit_s: float = 20,
             what: str = "") -> State:
        """Dumps until the screen's state satisfies `ok`."""
        deadline = time.monotonic() + limit_s
        while True:
            st = self.state(game)
            if ok(st):
                return st
            if st.kind == "other" and self.acted:
                sys.exit(f"{front_app()} came to the front; stopping")
            if time.monotonic() > deadline:
                s = self.look()
                sys.exit(f"{what or 'the expected screen'} didn't show after {limit_s:.0f} s; on "
                         f"screen ({st.kind}): {[label(n) for n in s if label(n)][:20]}")
            self.changed()

    # Acting ----------------------------------------------------------------------------------
    def _run_guarded(self, command: str, what: str) -> None:
        out = sh(guarded(command))
        self.changed()
        self.acted = True
        if "XD_OK" not in out:
            sys.exit(f"not doing {what}: {out.split(' u0 ')[-1].split('/')[0] or out!r} is in "
                     f"front, not GameNative")

    def _ime_region(self) -> list[tuple] | None:
        """The keyboard's touchable rectangles while it's showing, else None."""
        out = sh("dumpsys input_method | grep -m1 mInputShown=; dumpsys window windows | "
                 "grep -A40 'u0 InputMethod}:' | grep -m1 'touchable region'")
        if "mInputShown=true" not in out:
            return None
        rects = re.findall(r"\((-?\d+),(-?\d+),(-?\d+),(-?\d+)\)", out)
        return [tuple(map(int, r)) for r in rects] or [(0, 0, 10 ** 5, 10 ** 5)]

    def wait_keyboard(self, limit_s: float = 3) -> None:
        """Waits for the keyboard to come up for a field just tapped, then a moment more: keys
        sent before Gboard connects to the field are lost (the first letter went missing)."""
        deadline = time.monotonic() + limit_s
        while "mInputShown=true" not in sh("dumpsys input_method | grep -m1 mInputShown="):
            if time.monotonic() > deadline:
                return  # type anyway; the read-back catches a field that didn't take focus
            time.sleep(0.05)
        time.sleep(0.3)

    def hide_keyboard(self) -> None:
        """Closes the on-screen keyboard with Back, checked just before: with the keyboard up,
        Back only closes it."""
        if self._ime_region() is None:
            return
        self._run_guarded("input keyevent KEYCODE_BACK", "Back to hide the keyboard")
        for _ in range(20):
            if self._ime_region() is None:
                return
            time.sleep(0.1)
        sys.exit("the keyboard stayed up")

    def press(self, finder: Callable[[Screen], dict | None], what: str) -> None:
        """Taps the node `finder` picks from the current dump, unless it is on something we must
        never tap, or under the keyboard (the keyboard is hidden and the dump retaken first)."""
        for _ in range(3):
            s = self.look()
            n = finder(s)
            if not n:
                sys.exit(f"no {what} on screen; on screen: {[label(o) for o in s if label(o)][:20]}")
            if area(n["box"]) == 0:  # off screen: the keyboard panned the window up
                if self._ime_region() is not None:
                    self.hide_keyboard()
                    continue
                sys.exit(f"not tapping {label(n) or what!r}: it's off screen")
            if n["scroll"] and not inside(n["x"], n["y"], n["scroll"]):
                sys.exit(f"not tapping {label(n) or what!r} at {n['x']},{n['y']}: it's scrolled "
                         f"out of its list")
            limit = s.width * s.height // 4
            for other in s:
                if label(other).strip().lower() in NEVER_TAP:
                    boxes = [other["box"]] + ([other["tapbox"]] if area(other["tapbox"]) < limit else [])
                    if any(inside(n["x"], n["y"], b) for b in boxes):
                        sys.exit(f"refusing to tap {label(n)!r} at {n['x']},{n['y']}: it's on "
                                 f"{label(other)!r}")
            if any(o["cls"] == "EditText" and o["focused"] for o in s):
                region = self._ime_region()
                if region and any(inside(n["x"], n["y"], r) for r in region):
                    self.hide_keyboard()
                    self.changed()
                    continue
            self._run_guarded(f"input tap {n['x']} {n['y']}", f"a tap on {label(n) or what!r}")
            STATS["taps"] += 1
            return
        sys.exit(f"{what} stayed under the keyboard")

    def press_label(self, text: str) -> None:
        self.press(lambda s: find_view(s, text) or find(s, text), repr(text))

    def key(self, code: str) -> None:
        self._run_guarded(f"input keyevent {code}", code)

    def _drag_box(self, box: tuple) -> tuple:
        """`box` less the bars that sit over its ends (a library's tab bar and button hints):
        a drag that starts on one of those doesn't scroll the list."""
        s = self.look()
        x1, y1, x2, y2 = box
        mid = (y1 + y2) // 2
        limit = s.width * s.height // 4
        for o in s:
            b = o["box"]
            if o["scroll"] is not None or not (label(o) or o["click"]) or area(b) >= limit:
                continue
            if b[2] <= x1 or b[0] >= x2 or b[3] <= y1 or b[1] >= y2:
                continue
            if b[3] <= mid:
                y1 = max(y1, b[3])
            elif b[1] >= mid:
                y2 = min(y2, b[1])
        return (x1, y1, x2, y2) if y2 - y1 > (box[3] - box[1]) // 3 else box

    def swipe(self, box: tuple, direction: str) -> None:
        """Scrolls the list in `box`: "down" shows what's below, "up" what's above, "left" what's
        to the right. The finger drags 80% of the list's size and holds still before lifting,
        so the list doesn't fling: it moves that far less the touch slop, and consecutive views
        overlap by a fifth of the list, enough for any label and the value under it. The drag
        runs off-centre, on the side where labels sit, away from switches and arrows."""
        x1, y1, x2, y2 = box
        if direction in ("down", "up"):
            x1, y1, x2, y2 = self._drag_box(box)
        w, h = x2 - x1, y2 - y1
        if direction in ("down", "up"):
            x = x1 + w * 3 // 10
            a, b = y1 + h * 9 // 10, y1 + h // 10
            if direction == "up":
                a, b = b, a
            points = [(x, a + (b - a) * i // 3) for i in range(4)]
        else:
            y = (y1 + y2) // 2
            a, b = x1 + w * 8 // 10, x1 + w * 2 // 10
            if direction == "right":
                a, b = b, a
            points = [(a + (b - a) * i // 3, y) for i in range(4)]
        (sx, sy), (ex, ey) = points[0], points[-1]
        cmd = (f"input motionevent DOWN {sx} {sy}; "
               + "".join(f"input motionevent MOVE {px} {py}; " for px, py in points[1:])
               + f"sleep 0.12; input motionevent UP {ex} {ey}")
        self._run_guarded(cmd, "a swipe")

    def seek(self, pick: Callable[[Screen], dict | None], box: tuple | None = None,
             max_swipes: int = 16, from_end: bool = False) -> tuple[dict, Screen] | None:
        """Scrolls a list until `pick` finds a node: (node, dump), or None once both ends are
        reached. It looks down first, then up; with `from_end` (for lists that grow at the end,
        such as installed drivers) it flings to the end first and looks up from there. The list
        is `box`, else the screen's tallest scrollable."""
        s = self.look()
        direction, swipes, ends = "down", 0, set()
        if from_end and not pick(s):
            area_box = box or self._list_box(s)
            if area_box:
                x1, y1, x2, y2 = self._drag_box(area_box)
                x, a, b = x1 + (x2 - x1) * 3 // 10, y1 + (y2 - y1) * 9 // 10, y1 + (y2 - y1) // 10
                self._run_guarded("; ".join([f"input swipe {x} {a} {x} {b} 60"] * 3), "a fling")
                time.sleep(0.5)  # let the fling settle before the dump's idle wait
                s = self.look()
                direction = "up"
        while True:
            n = pick(s)
            if n:
                return n, s
            area_box = box or self._list_box(s)
            if not area_box or swipes >= max_swipes:
                return None
            before = self._content(s, area_box)
            self.swipe(area_box, direction)
            swipes += 1
            s = self.look()
            if self._content(s, area_box) == before:  # that end of the list
                ends.add(direction)
                if len(ends) == 2:
                    return None
                direction = "up" if direction == "down" else "down"

    @staticmethod
    def _list_box(s: Screen) -> tuple | None:
        tall = [b for b in s.scrollables if b[3] - b[1] >= s.height // 3]
        return max(tall, key=area) if tall else None

    @staticmethod
    def _content(s: Screen, box: tuple) -> list:
        return [(label(n), n["box"]) for n in s if n["scroll"] == box and label(n)]

    # Routing ---------------------------------------------------------------------------------
    def restart(self) -> None:
        """Restarts GameNative and gets past the sign-in screen and the update offer."""
        if self.restarted >= 2:
            sys.exit("GameNative still isn't on a known screen after restarting it twice")
        self.restarted += 1
        STATS["restarts"] += 1
        # A document picker from an interrupted import survives force-stopping GameNative. Cancel
        # it first, otherwise am start can leave that picker covering the relaunched activity.
        for _ in range(6):
            if not front_app().endswith("documentsui"):
                break
            sh("input keyevent KEYCODE_BACK")
            time.sleep(1)
        else:
            sys.exit("the document picker stayed open after cancelling; not restarting GameNative")
        print("restarting GameNative", file=sys.stderr, flush=True)
        sh(f"am force-stop {APP}")
        sh(f"am start -n {APP}/.MainActivity")
        self.changed()
        self.last_game = None
        calm, deadline, offered = 0, time.monotonic() + 60, False
        # The update offer can show up a moment after the library: unless it has been passed
        # already, the library must be seen twice.
        while calm < (1 if offered else 2):
            st = self.state()
            if st.kind == "library":
                calm += 1
            elif st.kind in ("signin", "update", "membership"):
                calm = 0
                offered = offered or st.kind == "update"
                self.pass_prompt(st)
            elif time.monotonic() > deadline:
                sys.exit(f"GameNative didn't reach its library after restarting ({st.kind})")
            self.changed()

    def pass_prompt(self, st: State) -> None:
        if st.kind == "signin":
            self.press_label("Skip login")
        elif st.kind == "update":
            self.press_label("Later")
        elif st.kind == "membership":
            print("dismissing GameNative's membership prompt", flush=True)
            self.press(lambda s: find(s, "Close"), "membership prompt's Close button")

    def go(self, target: State, limit: int = 30) -> Screen:
        """Moves to a screen matching `target` and returns its dump."""
        unknown = 0
        for _ in range(limit):
            st = self.state(target.game)
            if matches(st, target):
                return self.look()
            if st.kind in ("signin", "update", "membership"):
                self.pass_prompt(st)
                continue
            if st.kind == "other":
                if self.acted:
                    sys.exit(f"{front_app()} came to the front; stopping")
                self.restart()
                continue
            if st.kind == "env_dialog":
                self.press_label("Cancel")
                continue
            path = route(st, target)
            if not path:
                # Unknown screens are re-read (something may still be loading), then GameNative
                # is restarted; so is it at once while a game runs.
                unknown += 1
                if st.kind in ("unknown", "popup") and unknown < 3:
                    self.changed()
                    continue
                self.restart()
                unknown = 0
                continue
            self.do(path[0], st)
        sys.exit(f"couldn't reach {target} in {limit} moves")

    def do(self, action: tuple, st: State) -> None:
        kind = action[0]
        if kind == "tap":
            self.press_label(action[1])
        elif kind == "key":
            self.key(action[1])
        elif kind == "close":
            self.press(lambda s: title_buttons(s)[0], "container close button")
        elif kind in ("seek_tap", "open_game"):
            found = self.seek(lambda s: find_view(s, action[1]))
            if not found:
                if kind == "open_game":
                    sys.exit(f"{action[1]!r} isn't in GameNative's library")
                sys.exit(f"no {action[1]!r} on this screen")
            self.press(lambda s: find_view(s, action[1]), repr(action[1]))
        elif kind == "tab":
            self.show_tab(action[1])

    def show_tab(self, tab: str) -> None:
        """Taps the editor tab `tab`, scrolling the tab row to it first if needed."""
        for _ in range(6):
            s = self.look()
            n = find(s, tab)
            if n and n["box"][0] >= 0 and n["box"][2] <= s.width and in_view(n, s):
                self.press(lambda s: find(s, tab), f"{tab} tab")
                return
            shown = [o for o in s if label(o) in TABS]
            if not shown:
                sys.exit("cannot find GameNative's container tab row")
            row = shown[0]["scroll"] or (0, min(o["box"][1] for o in shown), s.width,
                                         max(o["box"][3] for o in shown))
            later = TABS.index(tab) > max((TABS.index(label(o)) for o in shown if in_view(o, s)),
                                          default=-1)
            self.swipe(row, "left" if later else "right")
        sys.exit(f"the {tab} tab didn't appear after scrolling the tab row")

    # Container editor ------------------------------------------------------------------------
    def editor(self, game: str, tab: str | None = None) -> Screen:
        return self.go(State("editor", game, tab))

    def field(self, text: str, need_value: bool = True) -> tuple[dict, Screen]:
        """Scrolls the editor tab until the label `text` (and the value under it) is on screen."""
        def pick(s: Screen) -> dict | None:
            lab = find_view(s, text)
            if lab and need_value:
                v = value_node(s, lab)
                if not v or not in_view(v, s):
                    return None
            return lab
        found = self.seek(pick)
        if not found:
            sys.exit(f"no {text!r} on this tab")
        return found

    def close_editor(self, game: str) -> None:
        """Leaves the container settings without saving (only called when nothing changed). The
        result isn't waited for: the next move dumps anyway."""
        s = self.look()
        if any(label(n).endswith(" Config*") for n in s):
            sys.exit("refusing to close: the container has unsaved changes")
        self.press(lambda s: title_buttons(s)[0], "container close button")

    def save(self, game: str) -> None:
        s = self.look()
        if not find(s, f"{game} Config*"):
            sys.exit(f"{game}: nothing to save")
        self.press(lambda s: title_buttons(s)[1], "container save button")
        self.wait(lambda st: st.kind == "game_info", game, what="Game Information after saving")

    def dropdown(self, text: str) -> tuple[list[dict], Screen, Screen]:
        """Opens the dropdown under the label `text`: (entries, popup dump, page dump)."""
        lab, page = self.field(text, need_value=False)
        self.press(lambda s: find_view(s, text), repr(text))
        st = self.wait(lambda st: st.kind == "popup", what=f"the {text} dropdown", limit_s=10)
        del st
        s = self.look()
        entries = [n for n in s if label(n)]
        if not entries:
            sys.exit(f"tapping {text!r} opened no dropdown")
        return entries, s, page

    def close_dropdown(self, page: Screen) -> None:
        """Dismisses an open dropdown by tapping the container title behind it, if the dropdown
        doesn't cover it, else with Back."""
        popup = self.look()
        title = next((n for n in page if n["text"].endswith((" Config", " Config*"))), None)
        if title and not inside(title["x"], title["y"], popup.window):
            self.press(lambda s: title, "container title")
        else:
            self.key("KEYCODE_BACK")
        self.wait(lambda st: st.kind == "editor", what="the container editor")

    def pick(self, game: str, text: str, value: str) -> str:
        """Picks `value` in the dropdown under `text`; returns the value shown before."""
        lab, s = self.field(text)
        before = field_value_at(s, lab)
        if before == value:
            return before
        entries, popup, page = self.dropdown(text)
        # Installed drivers are appended to the driver list, so a new build is near its end.
        found = self.seek(lambda s: find_view(s, value), box=self._list_box(popup) or None,
                          from_end=text == DRIVER_LABEL and value.startswith("RADV"))
        if not found:
            choices = [label(n) for n in self.look() if label(n)]
            self.close_dropdown(page)
            sys.exit(f"{game}: {value!r} isn't a choice for {text}: {choices}")
        self.press(lambda s: find_view(s, value), repr(value))
        self.wait(lambda st: st.kind == "editor", what="the container editor")
        lab, s = self.field(text)
        now = field_value_at(s, lab)
        if now != value or not find(s, f"{game} Config*"):
            sys.exit(f"{game}: {text} reads {now!r} after picking {value!r}")
        return before

    def type_into(self, finder: Callable[[Screen], dict | None], text: str, what: str,
                  strip: bool = False) -> Screen:
        """Taps a text field, replaces its text with `text`, and reads it back from one dump.
        If Gboard garbled the quick way, the field is cleared and retyped a character at a
        time, checking as it goes."""
        s = self.look()
        n = finder(s)
        if not n:
            sys.exit(f"no {what} field on screen")
        old = len(n["text"])
        self.press(finder, f"{what} field")
        self.wait_keyboard()
        type_fast(text, old)
        self.acted = True
        self.changed()
        s = self.look()
        n = finder(s)
        got = n["text"] if n else None
        if got is not None and (got.rstrip() if strip else got) == text and n["focused"]:
            # With the keyboard up the window is panned, so other nodes' places are stale or off
            # screen: close it before anything else is tapped.
            self.hide_keyboard()
            return self.look()
        print(f"{what}: quick typing read back {got!r}; retyping slowly", file=sys.stderr)
        if not n or not n["focused"]:
            self.press(finder, f"{what} field")
        left = clear()
        got = type_text(text)
        self.changed()
        if left or got is None or (got.rstrip() if strip else got) != text:
            sys.exit(f"the {what} field reads {got!r}, not {text!r}; not saved")
        self.hide_keyboard()
        return self.look()


def field_value_at(s: Screen, lab: dict) -> str:
    n = value_node(s, lab)
    return n["text"] if n else ""


# Commands ----------------------------------------------------------------------------------------
NAV = Nav()


class Change(NamedTuple):
    kind: str  # "pick", "env", "args"
    tab: str
    label: str
    value: str


def env_values(nav: Nav, names: set[str]) -> dict[str, str | None]:
    """Flings the Environment tab to its Add button at the end, then scrolls up until each wanted
    variable's text value is read (None for one that isn't text-valued). Names not seen after
    reaching the top are missing."""
    seen: dict[str, str | None] = {}

    def collect(s: Screen) -> None:
        for n in s:
            if n["text"] in names and n["text"] not in seen and in_view(n, s):
                field = row_edit(s, n)
                seen[n["text"]] = field["text"] if field else None

    def add_button(s: Screen) -> dict | None:
        collect(s)
        return find_view(s, ADD_ENV)

    def all_seen(s: Screen) -> dict | None:
        collect(s)
        return s[0] if s and seen.keys() >= names else None

    # Add sits at the end of the list, and added variables are appended above it.
    if not nav.seek(add_button, from_end=True):
        sys.exit(f"no {ADD_ENV!r} on the Environment tab")
    if seen.keys() < names:  # the list may not have started at the top: look above too
        nav.seek(all_seen)
    return seen


def add_env(nav: Nav, game: str, name: str, value: str) -> None:
    """Adds NAME=VALUE through Add environment variable (GameNative replaces an existing one)."""
    found = nav.seek(lambda s: find_view(s, ADD_ENV))
    if not found:
        sys.exit(f"{ADD_ENV!r} was not found")
    nav.press(lambda s: find_view(s, ADD_ENV), repr(ADD_ENV))
    nav.wait(lambda st: st.kind == "env_dialog", game, what="the New Environment Variable dialog")

    def fields(s: Screen) -> list[dict]:
        found = sorted((n for n in s if n["cls"] == "EditText"), key=lambda n: n["y"])
        if len(found) != 2:
            sys.exit(f"expected Name and Value text fields; found {len(found)}")
        return found

    nav.type_into(lambda s: fields(s)[0], name, "Name")
    s = nav.type_into(lambda s: fields(s)[1], value, "Value")
    if [n["text"] for n in fields(s)] != [name, value]:
        sys.exit(f"the dialog reads {[n['text'] for n in fields(s)]}, not {[name, value]}; not saved")
    # Ok may be under the keyboard (landscape); press() then hides it and finds Ok again.
    nav.press(lambda s: find(s, "Ok") or find(s, "OK"), "Ok button")
    nav.wait(lambda st: st.kind == "editor", game, what="the container editor")


def read_change(nav: Nav, game: str, c: Change, env_cache: dict) -> str | None:
    """The current value of the setting `c` changes, on the current editor screen's tab."""
    if c.kind == "args":
        if not nav.seek(args_in_view):
            sys.exit("no Exec Arguments field on the General tab")
        return args_field(nav.look())["text"].rstrip()
    if c.kind == "pick":
        lab, s = nav.field(c.label)
        return field_value_at(s, lab)
    return env_cache.get(c.label)


def apply(game: str, changes: list[Change], launch_after: bool = False, nav: Nav = NAV) -> None:
    """Makes `changes` to GAME's container in one editor session, saves once, then reopens it and
    reads every change back."""
    order = sorted(changes, key=lambda c: TABS.index(c.tab))
    before: dict[Change, str | None] = {}
    todo: list[Change] = []
    for tab in dict.fromkeys(c.tab for c in order):
        nav.editor(game, tab)
        group = [c for c in order if c.tab == tab]
        env_now = env_values(nav, {c.label for c in group if c.kind == "env"}) \
            if any(c.kind == "env" for c in group) else {}
        for c in group:
            if c.kind == "args":
                if not nav.seek(args_in_view):
                    sys.exit("no Exec Arguments field on the General tab")
                was = args_field(nav.look())["text"]
                before[c] = was.rstrip()
                if was.rstrip() != c.value:
                    s = nav.type_into(args_in_view, c.value, "Exec Arguments", strip=True)
                    if not find(s, f"{game} Config*"):
                        sys.exit(f"{game}: the container isn't marked changed after typing")
                    todo.append(c)
            elif c.kind == "pick":
                was = nav.pick(game, c.label, c.value)
                before[c] = was
                if was != c.value:
                    todo.append(c)
            else:
                before[c] = env_now.get(c.label)
                if before[c] != c.value:
                    add_env(nav, game, c.label, c.value)
                    todo.append(c)
    s = nav.look()
    if find(s, f"{game} Config*"):
        nav.save(game)
    elif todo and any(c.kind != "env" for c in todo):
        sys.exit(f"{game}: changes were made but the container isn't marked changed")
    else:  # EnvVars.put may have set the value it already had.
        nav.close_editor(game)
    after: dict[Change, str | None] = {}
    if todo:
        for tab in dict.fromkeys(c.tab for c in todo):
            nav.editor(game, tab)
            group = [c for c in todo if c.tab == tab]
            env_now = env_values(nav, {c.label for c in group if c.kind == "env"}) \
                if any(c.kind == "env" for c in group) else {}
            for c in group:
                after[c] = read_change(nav, game, c, env_now)
        nav.close_editor(game)
    failed = [c for c in todo if after[c] != c.value]
    for c in order:
        if c in failed:
            continue
        if c.kind == "args":
            if c in todo:
                print(f"{game}: Exec Arguments set and read back:\n  was {before[c]}\n  now {after[c]}")
            else:
                print(f"{game}: Exec Arguments already set")
        elif c.kind == "pick" and c.label == DRIVER_LABEL:
            print(f"{game}: {before[c]} -> {after[c]} (read back after saving)" if c in todo
                  else f"{game}: already on {c.value}")
        elif c.kind == "pick":
            print(f"{game}: {c.label} {before[c]} -> {after[c]} (read back after saving)" if c in todo
                  else f"{game}: {c.label} already {c.value}")
        else:
            print(f"{game}: {c.label}={c.value} " +
                  ("(read back after saving)" if c in todo else "(already set; read on screen)"),
                  flush=True)
    if failed:
        sys.exit("\n".join(f"{game}: saved, but {c.label} reads {after[c]!r} when reopened, not "
                           f"{c.value!r}" for c in failed))
    if launch_after:
        launch(game, nav)


def launch(game: str, nav: Nav = NAV) -> None:
    """Starts GAME with Play on its Game Information screen, and waits until that screen goes."""
    nav.go(State("game_info", game))
    started = time.monotonic()
    nav.press(lambda s: find_view(s, "Play"), "Play button")
    st = nav.wait(lambda st: st.kind != "game_info" or st.game != game, game, limit_s=60,
                  what="the game")
    if st.kind in ("library", "quick_actions", "editor", "settings", "menu"):
        sys.exit(f"{game}: Play led to GameNative's {st.kind} screen, not the game")
    labels = [label(n) for n in nav.look() if label(n)][:8]
    print(f"{game}: launched; Game Information gave way after {time.monotonic() - started:.1f} s "
          f"(now showing {st.kind}: {labels})", flush=True)


def show(game: str, nav: Nav = NAV) -> str:
    s = nav.editor(game, "Graphics")
    lab, s = nav.field(DRIVER_LABEL)
    value = field_value_at(s, lab)
    nav.close_editor(game)
    return value


def get_option(game: str, text: str, tab: str = "Graphics", nav: Nav = NAV) -> str:
    nav.editor(game, tab)
    if text == "Exec Arguments":
        if not nav.seek(args_in_view):
            sys.exit("no Exec Arguments field on the General tab")
        value = args_field(nav.look())["text"]
    else:
        lab, s = nav.field(text)
        value = field_value_at(s, lab)
    nav.close_editor(game)
    return value


def options(game: str, text: str, tab: str = "Graphics", nav: Nav = NAV) -> list[str]:
    nav.editor(game, tab)
    entries, popup, page = nav.dropdown(text)
    choices = [label(n) for n in entries]
    box = nav._list_box(popup)
    if box:  # a long list: scroll through it for the rest
        for _ in range(16):
            before = nav._content(nav.look(), box)
            nav.swipe(box, "down")
            s = nav.look()
            if nav._content(s, box) == before:
                break
            choices += [label(n) for n in s if label(n) and label(n) not in choices]
    nav.close_dropdown(page)
    nav.close_editor(game)
    return choices


def set_option(game: str, text: str, value: str, tab: str = "Graphics") -> None:
    apply(game, [Change("pick", tab, text, value)])


def select(entry: str, game: str) -> None:
    apply(game, [Change("pick", "Graphics", DRIVER_LABEL, entry)])


def set_args(game: str, text: str) -> None:
    apply(game, [Change("args", "General", "Exec Arguments", text)])


def set_env(game: str, name: str, value: str) -> None:
    set_envs(game, {name: value})


def set_envs(game: str, values: dict[str, str]) -> None:
    """Update text values through Add, save once, then read all back; never delete rows."""
    if not values or any(not ENV_NAME.fullmatch(name) for name in values):
        sys.exit("provide at least one valid environment variable name")
    apply(game, [Change("env", "Environment", k, v) for k, v in values.items()])


def import_zip(zip_name: str, entry: str, nav: Nav = NAV) -> None:
    nav.go(State("driver_manager"))
    # Installed drivers are listed below the import button, in the dialog's scrolling area,
    # with a trash icon beside each; the swipes run over their names, away from the icons.
    if nav.seek(lambda s: find_view(s, entry), max_swipes=30, from_end=True):
        print(f"{entry} is installed already")
        return
    nav.seek(lambda s: find_view(s, "Import ZIP from device"))
    nav.press(lambda s: find_view(s, "Import ZIP from device"), "Import ZIP from device")
    nav.wait(lambda st: st.kind == "picker" and find(nav.look(), f"Files in {Path(PHONE_DIR).name}"),
             what=f"the file picker in {PHONE_DIR}")
    found = nav.seek(lambda s: find_view(s, zip_name))
    if not found:
        sys.exit(f"{zip_name} isn't in the file picker; it should open in {PHONE_DIR}")
    nav.press(lambda s: find_view(s, zip_name), zip_name)
    nav.wait(lambda st: st.kind == "driver_manager", limit_s=30, what="Driver Manager")
    deadline = time.monotonic() + 30  # the entry shows up a few seconds after the pick
    while not nav.seek(lambda s: find_view(s, entry), max_swipes=30, from_end=True):
        if time.monotonic() > deadline:
            sys.exit(f"picked {zip_name}, but {entry!r} isn't in the installed drivers")
        nav.changed()
    print(f"imported {zip_name} as {entry}")


def install(zip_path: Path, games: list[str]) -> None:
    entry = json.loads(zipfile.ZipFile(zip_path).read("meta.json"))["name"]
    remote = f"{PHONE_DIR}/{zip_path.name}"
    if sh(f"stat -c %s {remote} 2>/dev/null") != str(zip_path.stat().st_size):
        if adb("push", str(zip_path), remote, timeout=300).returncode != 0:
            sys.exit(f"couldn't copy {zip_path.name} to {PHONE_DIR}")
        print(f"copied {zip_path.name} to {PHONE_DIR}")
    import_zip(zip_path.name, entry)
    for game in games:
        select(entry, game)


def copy_content(path: Path) -> None:
    """Keep existing phone files; accept a matching checksum, refuse a name collision."""
    expected = hashlib.sha256(path.read_bytes()).hexdigest()
    remote = shlex.quote(f"{PHONE_DIR}/{path.name}")
    exists = sh(f"if [ -e {remote} ]; then echo exists; fi") == "exists"
    if exists:
        actual = sh(f"sha256sum {remote}").split()
        if not actual or actual[0] != expected:
            sys.exit(f"{PHONE_DIR}/{path.name} already exists with different content; keeping it")
    else:
        sh(f"mkdir -p {shlex.quote(PHONE_DIR)}")
        if adb("push", str(path), f"{PHONE_DIR}/{path.name}", timeout=300).returncode:
            sys.exit(f"couldn't copy {path.name}")
    actual = sh(f"sha256sum {remote}").split()
    if not actual or actual[0] != expected:
        sys.exit(f"checksum mismatch after copying {path.name}")
    print(f"{PHONE_DIR}/{path.name}: SHA256 {expected} (read back)", flush=True)


def import_content(path: Path, nav: Nav = NAV) -> None:
    with tarfile.open(path) as archive:
        profile = json.load(archive.extractfile("profile.json"))
    if profile.get("type") != "DXVK":
        sys.exit("import-content currently supports DXVK profiles only")
    entry = f"{profile['versionName']} ({profile['versionCode']})"
    copy_content(path)
    s = nav.go(State("contents_manager"))
    if not find(s, "DXVK"):
        sys.exit("Contents Manager is not showing the DXVK list")
    if not nav.seek(lambda s: find_view(s, entry)):
        nav.seek(lambda s: find_view(s, "Import .wcp from device"))
        nav.press(lambda s: find_view(s, "Import .wcp from device"), "Import .wcp from device")
        nav.wait(lambda st: st.kind == "picker" and find(nav.look(), f"Files in {Path(PHONE_DIR).name}"),
                 what=f"the file picker in {PHONE_DIR}")
        if not nav.seek(lambda s: find_view(s, path.name)):
            sys.exit(f"{path.name} isn't in the picker at {PHONE_DIR}")
        nav.press(lambda s: find_view(s, path.name), path.name)
        # GameNative installs trusted profiles immediately after validation. Never accept an
        # untrusted-file override or infer success from merely returning to the dialog.
        outcomes = (entry, "Untrusted files detected", "Content already exists",
                    "File cannot be recognized", "Profile not found in content",
                    "Profile cannot be recognized", "Content is incomplete",
                    "Content cannot be trusted", "Not enough space", "Failed to install content")
        nav.wait(lambda st: any(find(nav.look(), t) for t in outcomes), limit_s=90,
                 what="the import's outcome")
        state = next(t for t in outcomes if find(nav.look(), t))
        if state != entry:
            sys.exit(f"{path.name} wasn't installed: {state}")
    nav.press_label("Close")
    nav.wait(lambda st: st.kind == "settings", what="Settings")
    s = nav.go(State("contents_manager"))
    if not find(s, "DXVK") or not nav.seek(lambda s: find_view(s, entry)):
        sys.exit(f"{entry} missing after reopening Contents Manager")
    print(f"DXVK {entry} installed (read back after reopening)", flush=True)
    nav.press_label("Close")


def save_export_config(destination: Path, nav: Nav = NAV) -> None:
    """Finish an already-open Create Document picker, then verify/pull the exported JSON."""
    remote = f"{PHONE_DIR}/{destination.name}"
    if destination.exists() or sh(f"if [ -e {shlex.quote(remote)} ]; then echo exists; fi"):
        sys.exit(f"export destination already exists; keeping it: {destination.name}")
    nav.wait(lambda st: find(nav.look(), "SAVE") is not None, what="the export picker")
    if not find(nav.look(), f"Files in {Path(PHONE_DIR).name}"):
        sys.exit(f"export picker is not in {PHONE_DIR}")

    def name_field(s: Screen) -> dict:
        fields = [n for n in s if n["cls"] == "EditText"]
        if len(fields) != 1:
            sys.exit(f"expected one export filename field, found {len(fields)}")
        return fields[0]

    nav.type_into(name_field, destination.name, "export filename")
    nav.press(lambda s: find(s, "SAVE"), "SAVE")
    nav.wait(lambda st: st.kind in ("game_info", "quick_actions"), what="GameNative after saving")
    # The picker returns before GameNative's export coroutine writes the file. An existing
    # zero-byte document is not success; wait for a complete, nonempty JSON object first.
    deadline = time.monotonic() + 30
    while True:
        try:
            config = json.loads(sh(f"cat {shlex.quote(remote)} 2>/dev/null"))
        except json.JSONDecodeError:
            config = None
        if isinstance(config, dict) and config:
            break
        if time.monotonic() >= deadline:
            sys.exit(f"GameNative did not finish exporting valid JSON to {remote} within 30 s")
        time.sleep(1)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".gamenative-export-", dir=destination.parent) as temp:
        pulled = Path(temp) / destination.name
        if adb("pull", remote, str(pulled)).returncode:
            sys.exit(f"couldn't pull exported config from {remote}")
        if json.loads(pulled.read_text(encoding="utf-8")) != config:
            sys.exit("export changed while copying it; not saving an unverified local config")
        pulled.replace(destination)
    print(f"exported and verified {destination} (phone copy kept)", flush=True)


def export_config(game: str, destination: Path, nav: Nav = NAV) -> None:
    nav.go(State("quick_actions", game))
    if not nav.seek(lambda s: find_view(s, "Export config")):
        sys.exit("no 'Export config' in Quick Actions")
    nav.press(lambda s: find_view(s, "Export config"), "Export config")
    save_export_config(destination, nav)


# Compatibility with scripts that drive the old helpers directly ---------------------------------
def open_library() -> None:
    """Restarts GameNative and gets past the sign-in screen and the update offer."""
    NAV.restart()


def tap_node(n: dict, nodes: list[dict]) -> None:
    NAV.press(lambda s: n, label(n) or "node")


def wait_for(*texts: str, limit_s: float = 20) -> tuple[str, dict, list[dict]]:
    """Dumps the screen until one of `texts` is on it (first listed wins): (text, node, dump)."""
    NAV.changed()
    NAV.wait(lambda st: any(find(NAV.look(), t) for t in texts), limit_s=limit_s,
             what=f"one of {list(texts)}")
    s = NAV.look()
    t = next(t for t in texts if find(s, t))
    return t, find(s, t), s


def go(text: str, then: str, limit_s: float = 20) -> list[dict]:
    """Taps `text` (scrolling to it if needed), then waits for `then`. Returns its dump."""
    if not NAV.seek(lambda s: find_view(s, text)):
        sys.exit(f"no {text!r} on screen")
    NAV.press(lambda s: find_view(s, text), repr(text))
    return wait_for(then, limit_s=limit_s)[2]


def wait_awake_unlocked(limit_s: float = 300) -> None:
    deadline = time.monotonic() + limit_s
    asked = False
    while ("mWakefulness=Awake" not in sh("dumpsys power | grep mWakefulness=")
           or "isKeyguardShowing=true" in sh("dumpsys window | grep isKeyguardShowing")):
        if time.monotonic() > deadline:
            sys.exit("the phone stayed locked")
        if not asked:
            print("waiting for the phone to be woken and unlocked", flush=True)
            asked = True
        time.sleep(3)


def parse_set(text: str, parser: argparse.ArgumentParser) -> Change:
    if "=" not in text:
        parser.error(f"--set takes LABEL=VALUE, not {text!r}")
    name, value = text.split("=", 1)
    tab, sep, rest = name.partition(":")
    if sep and tab in TABS:
        name = rest
    else:
        tab = LABEL_TABS.get(name, "Graphics")
    if not name or not value:
        parser.error(f"--set takes LABEL=VALUE, not {text!r}")
    return Change("pick", tab, name, value)


def parse_envs(entries: list[str], parser: argparse.ArgumentParser) -> dict[str, str]:
    if any("=" not in entry for entry in entries):
        parser.error("environment entries are NAME=VALUE")
    values = dict(entry.split("=", 1) for entry in entries)
    if len(values) != len(entries):
        parser.error("environment variable names must be distinct")
    if any(not ENV_NAME.fullmatch(name) for name in values):
        parser.error("invalid environment variable name")
    return values


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--stats", action="store_true",
                        help="print dumps, taps, restarts and the time taken to stderr")
    commands = parser.add_subparsers(dest="command", required=True)
    apply_parser = commands.add_parser("apply", help="make several container changes with one save")
    apply_parser.add_argument("game")
    apply_parser.add_argument("--driver", metavar="ENTRY", help="Graphics Driver Version to select")
    apply_parser.add_argument("--set", action="append", default=[], metavar="[TAB:]LABEL=VALUE",
                              dest="sets", help="dropdown value to pick (repeatable)")
    apply_parser.add_argument("--env", action="append", default=[], metavar="NAME=VALUE",
                              dest="envs", help="text environment variable to set (repeatable)")
    apply_parser.add_argument("--args", metavar="TEXT", dest="exec_args", help="Exec Arguments")
    apply_parser.add_argument("--launch", action="store_true", help="start the game afterwards")
    commands.add_parser("launch", help="start a game from its Game Information screen").add_argument("game")
    install_parser = commands.add_parser("install", help="import a driver zip and select it")
    install_parser.add_argument("zip", type=Path)
    install_parser.add_argument("games", nargs="+")
    commands.add_parser("import-content", help="import a DXVK WCP and verify the installed list").add_argument(
        "wcp", type=Path)
    export_parser = commands.add_parser("export-config", help="export a container JSON and pull it to the PC")
    export_parser.add_argument("game")
    export_parser.add_argument("destination", type=Path)
    env_parser = commands.add_parser("env", help="add/update a text-valued environment variable and verify it")
    env_parser.add_argument("game")
    env_parser.add_argument("name")
    env_parser.add_argument("value")
    envs_parser = commands.add_parser("envs", help="update several environment values, save once, and verify all")
    envs_parser.add_argument("game")
    envs_parser.add_argument("entries", nargs="+", metavar="NAME=VALUE")
    select_parser = commands.add_parser("select", help="select an installed graphics driver")
    select_parser.add_argument("entry")
    select_parser.add_argument("games", nargs="+")
    for command in ("show", "args"):
        commands.add_parser(command).add_argument("games", nargs="+")
    for command in ("get", "options", "set"):
        sub = commands.add_parser(command)
        sub.add_argument("game")
        sub.add_argument("label")
        if command == "set":
            sub.add_argument("value")
        sub.add_argument("--tab", choices=TABS, default="Graphics")
    args = parser.parse_args()
    if args.command == "envs":
        env_values_arg = parse_envs(args.entries, parser)
    if args.command == "env" and not ENV_NAME.fullmatch(args.name):
        parser.error("invalid environment variable name")
    if args.command == "apply":
        changes = [parse_set(text, parser) for text in args.sets]
        if args.driver:
            changes.append(Change("pick", "Graphics", DRIVER_LABEL, args.driver))
        changes += [Change("env", "Environment", k, v)
                    for k, v in parse_envs(args.envs, parser).items()] if args.envs else []
        if args.exec_args is not None:
            changes.append(Change("args", "General", "Exec Arguments", args.exec_args))
        if len({(c.kind, c.label) for c in changes}) != len(changes):
            parser.error("each setting can be changed once per apply")
        if not changes and not args.launch:
            parser.error("apply needs --driver, --set, --env, --args or --launch")
    if args.command == "args":
        texts = {}
        for game in args.games:
            bench, _, driver = game.lower().partition("-")
            if bench not in EXEC_ARGS or not driver:
                sys.exit(f"{game}: not a <bench>-<driver> name for one of {sorted(EXEC_ARGS)}")
            texts[game] = EXEC_ARGS[bench].format(driver=driver)
    started = time.monotonic()
    stay_on = sh("settings get global stay_on_while_plugged_in").strip()
    sh(STAY_ON_CMD)
    try:
        wait_awake_unlocked()
        if args.command == "apply":
            if changes:
                apply(args.game, changes, args.launch)
            else:
                launch(args.game)
        elif args.command == "launch":
            launch(args.game)
        elif args.command == "install":
            install(args.zip, args.games)
        elif args.command == "import-content":
            import_content(args.wcp)
        elif args.command == "export-config":
            export_config(args.game, args.destination)
        elif args.command == "env":
            set_env(args.game, args.name, args.value)
        elif args.command == "envs":
            set_envs(args.game, env_values_arg)
        elif args.command == "select":
            for game in args.games:
                select(args.entry, game)
        elif args.command == "args":
            for game, text in texts.items():
                set_args(game, text)
        elif args.command == "get":
            print(f"{args.game}: {args.label}: {get_option(args.game, args.label, args.tab)}")
        elif args.command == "options":
            print(f"{args.game}: {args.label}: {options(args.game, args.label, args.tab)}")
        elif args.command == "set":
            set_option(args.game, args.label, args.value, args.tab)
        else:
            for game in args.games:
                print(f"{game}: {show(game)}")
    finally:
        if stay_on.isdigit():  # "null" when unset, "" when adb hiccuped: then leave it alone
            sh(f"settings put global stay_on_while_plugged_in {stay_on}")
        else:
            print(f"warning: stay_on_while_plugged_in read back as {stay_on!r}; not restored")
        if args.stats:
            print(f"gn_driver: {STATS['dumps']} dumps, {STATS['taps']} taps, "
                  f"{STATS['restarts']} restarts, {time.monotonic() - started:.1f} s",
                  file=sys.stderr)


if __name__ == "__main__":
    main()
