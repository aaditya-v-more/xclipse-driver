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
       python pipeline/gn_driver.py args GAME [GAME...]
       python pipeline/gn_driver.py get GAME LABEL [--tab Graphics|Emulation]
       python pipeline/gn_driver.py options GAME LABEL [--tab Graphics|Emulation]
       python pipeline/gn_driver.py set GAME LABEL VALUE [--tab Graphics|Emulation]

install copies ZIP (a build from driver/dist) to the phone's Download/xclipse folder, imports it in
Driver Manager unless it's there already, and makes it the Graphics Driver Version of each GAME (a
custom game's name in the library, such as Heaven-radv). select does the last step for an entry
that's already installed, named as the dropdown shows it ("RADV Xclipse (Mesa ...)", "System").
show prints each GAME's Graphics Driver Version and changes nothing. args sets each GAME's Exec
Arguments to game.py's EXEC_ARGS for it (GAME is <bench>-<driver>, such as GravityMark-radv).
get reads the value under LABEL; options prints its dropdown choices; set picks VALUE, saves, and
reads it back. These use the Graphics tab by default (such as "BCn Emulation"); --tab Emulation
reaches "FEXCore Preset" and "Box64 Preset". get and options change nothing.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game import EXEC_ARGS  # noqa: E402
from phone_ui import clear, dump, tap, type_text  # noqa: E402
from run import adb, shell  # noqa: E402

APP = "app.gamenative"
PHONE_DIR = "/sdcard/Download/xclipse"  # where the file picker opens
NEVER_TAP = {"uninstall", "reset container", "import config", "delete variable", "delete",
             "clear preferences", "clear local database"}
TAB_MARKERS = {"Graphics": "Graphics Driver Version", "Emulation": "FEXCore Version"}


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


def field_value(nodes: list[dict], text: str) -> str:
    """The value shown under the label `text`."""
    lab = find(nodes, text)
    if not lab:
        sys.exit(f"no {text!r} on screen")
    below = [n for n in nodes if n["text"] and 0 < n["y"] - lab["y"] < 120 and n is not lab]
    return min(below, key=lambda n: n["y"])["text"] if below else ""


def driver_field(nodes: list[dict]) -> str:
    """The value shown under the Graphics Driver Version label."""
    return field_value(nodes, "Graphics Driver Version")


def open_container(game: str, from_library: bool = True) -> list[dict]:
    """Opens GAME's container settings, on the General tab."""
    if from_library:
        open_library()
        go(game, "Game Information")
    go("Options", "Quick Actions")
    return go("Edit container", "Exec Arguments")


def open_tab(game: str, tab: str, from_library: bool = True) -> list[dict]:
    """Opens a supported container tab and waits for one of its own fields."""
    marker = TAB_MARKERS[tab]
    open_container(game, from_library)
    return go(tab, marker)


def open_graphics(game: str, from_library: bool = True) -> list[dict]:
    """Opens GAME's container settings on the Graphics tab."""
    return open_tab(game, "Graphics", from_library)


def args_field(nodes: list[dict]) -> dict:
    """The Exec Arguments text field (its label sits just above it)."""
    lab = find(nodes, "Exec Arguments")
    below = [n for n in nodes if n["cls"] == "EditText" and 0 < n["y"] - lab["y"] < 150]
    if not below:
        sys.exit("no Exec Arguments field on screen")
    return min(below, key=lambda n: n["y"])


def set_args(game: str, text: str) -> None:
    """Sets GAME's Exec Arguments (General tab) to `text`, saves, and reads it back."""
    nodes = open_container(game)
    before = args_field(nodes)["text"]
    if before == text:
        print(f"{game}: Exec Arguments already set")
        close_container(nodes)
        return
    tap_node(args_field(nodes), nodes)
    time.sleep(1)
    if clear():
        sys.exit(f"{game}: couldn't empty the Exec Arguments field")
    typed = type_text(text)
    if typed != text:
        sys.exit(f"{game}: the field reads {typed!r}, not {text!r}; not saved")
    _, _, nodes = wait_for(f"{game} Config*")
    tap_node(title_buttons(nodes)[1], nodes)  # save
    wait_for("Game Information")
    nodes = open_container(game, from_library=False)
    after = args_field(nodes)["text"]
    close_container(nodes)
    if after != text:
        sys.exit(f"{game}: saved, but it reads {after!r} when reopened")
    print(f"{game}: Exec Arguments set and read back:\n  was {before}\n  now {after}")


def close_container(nodes: list[dict]) -> None:
    """Leaves the container settings without saving (only called when nothing changed)."""
    if any(label(n).endswith(" Config*") for n in nodes):
        sys.exit("refusing to close: the container has unsaved changes")
    tap_node(title_buttons(nodes)[0], nodes)
    wait_for("Game Information")


def container_field(game: str, text: str, tab: str = "Graphics",
                    from_library: bool = True) -> tuple[dict, list[dict]]:
    """Opens GAME's tab scrolled so the label `text` is on screen: (node, dump)."""
    open_tab(game, tab, from_library)
    h = screen_height()
    found = scroll_to(text, 640, h - 500, h - 1300, h)
    if not found:
        sys.exit(f"{game}: no {text!r} on the {tab} tab")
    return found


def open_dropdown(lab: dict, nodes: list[dict]) -> list[dict]:
    """Taps the label `lab` to open its dropdown and returns the dropdown's entries: the nodes that
    weren't on screen before (the page behind it stays in the dump, with the same texts)."""
    page = {(label(n), tuple(n["box"])) for n in nodes}
    tap_node(lab, nodes)
    time.sleep(1.5)
    entries = [n for n in dump() if label(n) and (label(n), tuple(n["box"])) not in page]
    if not entries:
        sys.exit(f"tapping {label(lab)!r} opened no dropdown")
    return entries


def close_dropdown(nodes: list[dict]) -> None:
    """Dismisses an open dropdown by tapping the screen title behind it."""
    title = next((n for n in nodes if label(n).endswith(" Config") or label(n).endswith(" Config*")),
                 None)
    if not title:
        sys.exit("no container title on screen to dismiss the dropdown with")
    tap_node(title, nodes)
    time.sleep(1)


def get_option(game: str, text: str, tab: str = "Graphics") -> str:
    _, nodes = container_field(game, text, tab)
    value = field_value(nodes, text)
    close_container(nodes)
    return value


def options(game: str, text: str, tab: str = "Graphics") -> list[str]:
    lab, nodes = container_field(game, text, tab)
    entries = open_dropdown(lab, nodes)
    close_dropdown(nodes)
    close_container(dump())
    return [label(n) for n in entries]


def set_option(game: str, text: str, value: str, tab: str = "Graphics") -> None:
    """Picks `value` in GAME's dropdown, saves, and reads it back on the same tab."""
    lab, nodes = container_field(game, text, tab)
    before = field_value(nodes, text)
    if before == value:
        print(f"{game}: {text} already {value}")
        close_container(nodes)
        return
    entries = open_dropdown(lab, nodes)
    pick = next((n for n in entries if value in (n["text"], n["desc"])), None)
    if not pick:
        close_dropdown(nodes)
        close_container(dump())
        sys.exit(f"{game}: {value!r} isn't a choice for {text}: {[label(n) for n in entries]}")
    tap_node(pick, entries)
    _, _, nodes = wait_for(text)
    if field_value(nodes, text) != value or not find(nodes, f"{game} Config*"):
        sys.exit(f"{game}: {text} reads {field_value(nodes, text)!r} after picking {value!r}")
    tap_node(title_buttons(nodes)[1], nodes)  # save
    wait_for("Game Information")
    _, nodes = container_field(game, text, tab, from_library=False)
    after = field_value(nodes, text)
    close_container(nodes)
    if after != value:
        sys.exit(f"{game}: saved, but {text} reads {after!r} when reopened")
    print(f"{game}: {text} {before} -> {after} (read back after saving)")


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
        if adb("push", str(zip_path), remote, timeout=300).returncode != 0:
            sys.exit(f"couldn't copy {zip_path.name} to {PHONE_DIR}")
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
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    install_parser = commands.add_parser("install", help="import a driver zip and select it")
    install_parser.add_argument("zip", type=Path)
    install_parser.add_argument("games", nargs="+")
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
        sub.add_argument("--tab", choices=TAB_MARKERS, default="Graphics")
    args = parser.parse_args()
    if args.command == "args":
        texts = {}
        for game in args.games:
            bench, _, driver = game.lower().partition("-")
            if bench not in EXEC_ARGS or not driver:
                sys.exit(f"{game}: not a <bench>-<driver> name for one of {sorted(EXEC_ARGS)}")
            texts[game] = EXEC_ARGS[bench].format(driver=driver)
    stay_on = shell("settings get global stay_on_while_plugged_in").strip()
    shell("svc power stayon usb")
    try:
        wait_awake_unlocked()
        if args.command == "install":
            install(args.zip, args.games)
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
            shell(f"settings put global stay_on_while_plugged_in {stay_on}")
        else:
            print(f"warning: stay_on_while_plugged_in read back as {stay_on!r}; not restored")


if __name__ == "__main__":
    main()
