#!/usr/bin/env python3
"""Puts a build of our driver into GameNative: imports the zip and selects it in containers.

GameNative has no command line for this, so the script taps through the app's screens over adb.
Every tap goes to an element found by its exact label in a fresh uiautomator dump, and every change
is read back. GameNative is restarted first, so the script always starts from the Steam sign-in
screen (it taps "Skip login" and never signs in) and the update offer ("Later").

It refuses to tap anything labeled Uninstall, Reset container, Import config, Delete variable,
Delete (Driver Manager's trash icons), Clear Preferences or Clear Local Database.

Nobody may touch the phone while it runs, and nothing else may drive it (game.py included). Turn on
Do Not Disturb first: a notification that pops up between a dump and a tap gets the tap. The script
stops if another app comes to the front. The screen is kept on while it runs, and the setting is
put back afterwards.

usage: python pipeline/gn_driver.py install ZIP GAME [GAME...]
       python pipeline/gn_driver.py select ENTRY GAME [GAME...]
       python pipeline/gn_driver.py show GAME [GAME...]

install copies ZIP (a build from driver/dist) to the phone's Download/xclipse folder, imports it in
Driver Manager unless it's there already, and makes it the Graphics Driver Version of each GAME (a
custom game's name in the library, such as Heaven-radv). select does the last step for an entry
that's already installed, named as the dropdown shows it ("RADV Xclipse (Mesa ...)", "System").
show prints each GAME's Graphics Driver Version and changes nothing.
"""

from __future__ import annotations

import json
import sys
import time
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from phone_ui import dump, tap  # noqa: E402
from run import adb, shell  # noqa: E402

APP = "app.gamenative"
PHONE_DIR = "/sdcard/Download/xclipse"  # where the file picker opens
NEVER_TAP = {"uninstall", "reset container", "import config", "delete variable", "delete",
             "clear preferences", "clear local database"}


def label(n: dict) -> str:
    return n["text"] or n["desc"]


def find(nodes: list[dict], text: str) -> dict | None:
    return next((n for n in nodes if text in (n["text"], n["desc"])), None)


def front_app() -> str:
    """The package of the app in front (its activity has focus, even under its own popups)."""
    return shell("dumpsys window | grep mFocusedApp=").split(" u0 ")[-1].split("/")[0]


def wait_for(*texts: str, limit_s: float = 20) -> tuple[str, dict, list[dict]]:
    """Dumps the screen until one of `texts` is on it (first listed wins): (text, node, dump)."""
    deadline = time.monotonic() + limit_s
    while True:
        nodes = dump()
        for t in texts:
            n = find(nodes, t)
            if n:
                return t, n, nodes
        if time.monotonic() > deadline:
            sys.exit(f"none of {list(texts)} on screen after {limit_s:.0f} s. In front: "
                     f"{front_app()}; on screen: {[label(n) for n in nodes if label(n)][:20]}")
        time.sleep(1)


def tap_node(n: dict, nodes: list[dict]) -> None:
    """Taps the middle of `n`, unless that point is on something we must never tap, or another app
    came to the front since the dump.

    A notification that pops up between the dump and the tap can still take the tap (a Teams one
    did, and opened an identity check), so keep Do Not Disturb on while this runs.
    """
    front = front_app()
    if front != APP and not front.endswith("documentsui"):
        sys.exit(f"not tapping {label(n)!r}: {front} is in front, not GameNative")
    for other in nodes:
        x1, y1, x2, y2 = other["box"]
        if label(other).lower() in NEVER_TAP and x1 <= n["x"] <= x2 and y1 <= n["y"] <= y2:
            sys.exit(f"refusing to tap {label(n)!r} at {n['x']},{n['y']}: it's on {label(other)!r}")
    tap(n["x"], n["y"])


def go(text: str, then: str, limit_s: float = 20) -> list[dict]:
    """Taps `text`, then waits for `then`, a label only the next screen has. Returns its dump."""
    _, n, nodes = wait_for(text)
    tap_node(n, nodes)
    return wait_for(then, limit_s=limit_s)[2]


def scroll_to(text: str, x: int, y_low: int, y_high: int, height: int,
              tries: int = 8) -> tuple[dict, list[dict]] | None:
    """Scrolls a list (swipes at x between y_low and y_high) until `text` is on screen, well inside
    it: down first, then up. Returns (node, dump), or None if the list doesn't have it."""
    for i in range(3 * tries):
        nodes = dump()
        n = find(nodes, text)
        if n and 100 < n["y"] < height - 100:
            return n, nodes
        if i < tries:
            shell(f"input swipe {x} {y_low} {x} {y_high} 400")
        else:
            shell(f"input swipe {x} {y_high} {x} {y_low} 400")
        time.sleep(1.5)
    return None


def screen_height() -> int:
    return int(shell("wm size").split()[-1].split("x")[1])


def open_library() -> None:
    """Restarts GameNative and gets past the sign-in screen and the update offer."""
    shell(f"am force-stop {APP}")
    shell(f"am start -n {APP}/.MainActivity")
    calm = 0
    while calm < 2:  # the update offer can show up a moment after the library
        t, n, nodes = wait_for("Skip login", "Later", "Menu", limit_s=40)
        if t == "Menu":
            calm += 1
        else:
            calm = 0
            tap_node(n, nodes)
        time.sleep(2)


def title_buttons(nodes: list[dict]) -> tuple[dict, dict]:
    """The container screen's unlabeled top buttons: close (left) and save (right)."""
    top = sorted((n for n in nodes if n["cls"] == "Button" and n["click"] and n["y"] < 200),
                 key=lambda n: n["x"])
    if len(top) != 2:
        sys.exit(f"expected 2 buttons beside the container title, found {len(top)}")
    return top[0], top[1]


def driver_field(nodes: list[dict]) -> str:
    """The value shown under the Graphics Driver Version label."""
    lab = find(nodes, "Graphics Driver Version")
    below = [n for n in nodes if n["text"] and 0 < n["y"] - lab["y"] < 120 and n is not lab]
    return min(below, key=lambda n: n["y"])["text"] if below else ""


def open_graphics(game: str, from_library: bool = True) -> list[dict]:
    """Opens GAME's container settings on the Graphics tab."""
    if from_library:
        open_library()
        go(game, "Game Information")
    go("Options", "Quick Actions")
    go("Edit container", f"{game} Config")
    return go("Graphics", "Graphics Driver Version")


def close_container(nodes: list[dict]) -> None:
    """Leaves the container settings without saving (only called when nothing changed)."""
    if any(label(n).endswith(" Config*") for n in nodes):
        sys.exit("refusing to close: the container has unsaved changes")
    tap_node(title_buttons(nodes)[0], nodes)
    wait_for("Game Information")


def show(game: str) -> str:
    nodes = open_graphics(game)
    value = driver_field(nodes)
    close_container(nodes)
    return value


def select(entry: str, game: str) -> None:
    nodes = open_graphics(game)
    before = driver_field(nodes)
    if before == entry:
        print(f"{game}: already on {entry}")
        close_container(nodes)
        return
    lab = find(nodes, "Graphics Driver Version")
    tap_node(lab, nodes)  # opens the dropdown
    time.sleep(1.5)
    found = scroll_to(entry, 641, 1800, 900, screen_height())
    if not found:
        sys.exit(f"{game}: {entry!r} isn't in the Graphics Driver Version list")
    tap_node(*found)
    _, _, nodes = wait_for("Graphics Driver Version")
    if driver_field(nodes) != entry or not find(nodes, f"{game} Config*"):
        sys.exit(f"{game}: the field reads {driver_field(nodes)!r} after picking {entry!r}")
    tap_node(title_buttons(nodes)[1], nodes)  # save
    wait_for("Game Information")
    nodes = open_graphics(game, from_library=False)
    after = driver_field(nodes)
    close_container(nodes)
    if after != entry:
        sys.exit(f"{game}: saved, but it reads {after!r} when reopened")
    print(f"{game}: {before} -> {after} (read back after saving)")


def import_zip(zip_name: str, entry: str) -> None:
    open_library()
    go("Menu", "Downloads & Storage")
    go("Settings", "Customize your experience")
    go("Driver Manager", "Import ZIP from device")
    height = screen_height()
    # The dialog shows 3 rows of installed drivers; swiping on their names (away from the trash
    # icons at the right) scrolls it.
    if scroll_to(entry, 400, 1720, 1400, height, tries=4):
        print(f"{entry} is installed already")
    else:
        _, n, nodes = wait_for("Import ZIP from device")
        tap_node(n, nodes)
        wait_for(f"Files in {Path(PHONE_DIR).name}", limit_s=20)
        found = scroll_to(zip_name, 540, 1900, 1000, height)
        if not found:
            sys.exit(f"{zip_name} isn't in the file picker; it should open in {PHONE_DIR}")
        tap_node(*found)
        wait_for("Import ZIP from device", limit_s=30)
        time.sleep(4)  # the entry shows up a few seconds after the pick
        if not scroll_to(entry, 400, 1720, 1400, height, tries=4):
            sys.exit(f"picked {zip_name}, but {entry!r} isn't in the installed drivers")
        print(f"imported {zip_name} as {entry}")
    go("Close", "Customize your experience")
    go("Back", "Menu")


def install(zip_path: Path, games: list[str]) -> None:
    entry = json.loads(zipfile.ZipFile(zip_path).read("meta.json"))["name"]
    remote = f"{PHONE_DIR}/{zip_path.name}"
    if shell(f"stat -c %s {remote} 2>/dev/null") != str(zip_path.stat().st_size):
        adb("push", str(zip_path), remote, timeout=300)
        print(f"copied {zip_path.name} to {PHONE_DIR}")
    import_zip(zip_path.name, entry)
    for game in games:
        select(entry, game)


def wait_awake_unlocked(limit_s: float = 300) -> None:
    deadline = time.monotonic() + limit_s
    asked = False
    while ("mWakefulness=Awake" not in shell("dumpsys power | grep mWakefulness=")
           or "isKeyguardShowing=true" in shell("dumpsys window | grep isKeyguardShowing")):
        if time.monotonic() > deadline:
            sys.exit("the phone stayed locked")
        if not asked:
            print("waiting for the phone to be woken and unlocked", flush=True)
            asked = True
        time.sleep(3)


def main() -> None:
    args = sys.argv[1:]
    if len(args) < 2 or args[0] not in ("install", "select", "show") or (
            args[0] != "show" and len(args) < 3):
        sys.exit(__doc__)
    stay_on = shell("settings get global stay_on_while_plugged_in")
    shell("svc power stayon usb")
    try:
        wait_awake_unlocked()
        if args[0] == "install":
            install(Path(args[1]), args[2:])
        elif args[0] == "select":
            for game in args[2:]:
                select(args[1], game)
        else:
            for game in args[1:]:
                print(f"{game}: {show(game)}")
    finally:
        shell(f"settings put global stay_on_while_plugged_in {stay_on}")


if __name__ == "__main__":
    main()
