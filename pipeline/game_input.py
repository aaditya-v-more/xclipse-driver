#!/usr/bin/env python3
"""Presses gamepad buttons in a game running in GameNative, and gets games past their intro screens.

GameNative passes an Android input device to the game's XInput pad only if it isn't virtual
(ExternalController.isGameController), and every event `adb shell input` makes comes from the
virtual device (id -1). So `input gamepad keyevent KEYCODE_BUTTON_A` reaches GameNative as a
keyboard key, not as the pad, and adding a real (uinput) device needs root. This script presses the
on-screen controller instead (InputControlsView, the "Virtual Gamepad" layout GameNative draws over
the game), which feeds the same pad: each press is `input swipe X Y X Y 120`, a touch held for
120 ms so a game that polls the pad once a frame at 20-50 FPS sees it. Button positions were
measured from a screenshot of the default layout on this phone in landscape (2340x1080) and are kept
as fractions of the screen. If the on-screen controls are hidden or the layout is edited, a press
lands on the game as a touch instead, so keep the default layout showing.

Every press first checks that GameNative is in front, in landscape, with a game running (its
wineserver process). Otherwise it stops: a press on the home screen or GameNative's library could
open something.

`play GAME` starts GAME with `gn_driver.py launch` and walks a per-game sequence (GAMES below) that
watches the picture's mean brightness in a box left of centre, clear of the on-screen controls and
GameNative's and DXVK's overlays, from raw screenshots (about 1 s each; 5 s or more when memory is
short). For LifeIsStrange3 that is: wait for the game's window; press RB every 2.5 s through the
Square Enix and Deck Nine logos, the health warning and the autosave notice (they show "Skip" on RB)
until the bright title screen; press A for "Press any button" and A again for Continue, until the
screen goes dark for loading; wait for the first scene to draw; then press RB three times, 2 s
apart, for the opening cutscene. It runs `am kill-all` (background processes only) before launching,
again before Continue, and every 3 s for 45 s of the scene load, to give the game memory. It refuses to restart a game that is already
running unless --restart is given. If GameNative dies on the way (Android's low-memory killer), it
says so from logcat.

usage: python pipeline/game_input.py press BUTTON [BUTTON...] [--times N] [--interval S]
       python pipeline/game_input.py play GAME [--restart] [--until title] [--no-kill]
       python pipeline/game_input.py look

BUTTON is one of A B X Y LB RB LT RT START SELECT L3 R3 UP DOWN LEFT RIGHT. press presses each
BUTTON in turn, N times over, S seconds apart (default 1, 0.8 s). look prints the brightness numbers
`play` works from, and whether a press would be allowed. Nobody may touch the phone while it runs.
"""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from phone_ui import sh  # noqa: E402
from run import ADB, STAY_ON_CMD, select_phone  # noqa: E402

# Centres of the default "Virtual Gamepad" on-screen buttons, as fractions of the landscape screen
# (measured on the S24 FE, 2340x1080, from the outlines in a screenshot over a black frame).
BUTTONS = {
    "A": (0.8650, 0.5213), "B": (0.9235, 0.3907), "X": (0.8068, 0.3907), "Y": (0.8650, 0.2602),
    "LB": (0.0530, 0.2065), "RB": (0.9470, 0.2065), "LT": (0.0782, 0.0741), "RT": (0.9218, 0.0741),
    "SELECT": (0.4624, 0.8907), "START": (0.5342, 0.8907), "L3": (0.0491, 0.7167),
    "R3": (0.9423, 0.7167), "UP": (0.1068, 0.3111), "DOWN": (0.1068, 0.4713),
    "LEFT": (0.0701, 0.3907), "RIGHT": (0.1432, 0.3907),
}
HOLD_MS = 120
# The D-pad repeats while held: in Life is Strange's option rows a 120 ms hold moved one to three
# steps, while 40 ms taps moved exactly one each (2026-10-18).
DPAD_HOLD_MS = 40
# The box whose brightness `play` watches: left of centre, inside the game's 16:9 picture.
BOX = (0.20, 0.30, 0.50, 0.65)
GAME_PROCESS = "wineserver"
# play's `am kill-all` calls; --no-kill turns them off to launch the way a player would.
CLEAR_BACKGROUND = True


class NotInGame(Exception):
    pass


def landscape_size() -> tuple[int, int]:
    """The screen's width and height as drawn now (wider than tall while a game runs)."""
    w, h = (int(v) for v in sh("wm size").split()[-1].split("x"))
    viewport = sh("dumpsys input 2>/dev/null | grep -m1 'Viewport INTERNAL'")
    if "orientation=1" in viewport or "orientation=3" in viewport:
        w, h = h, w
    return w, h


def check_in_game() -> tuple[int, int]:
    """Raises NotInGame unless GameNative is in front, in landscape, with a game running."""
    focus = sh("dumpsys window 2>/dev/null | grep -m1 mCurrentFocus")
    if "app.gamenative/" not in focus:
        raise NotInGame(f"GameNative is not in front ({focus.strip() or 'no focused window'})")
    if not sh(f"pidof {GAME_PROCESS}"):
        raise NotInGame(f"no game is running (no {GAME_PROCESS} process)")
    w, h = landscape_size()
    if w < h:
        raise NotInGame(f"the screen is in portrait ({w}x{h})")
    return w, h


def press(button: str) -> None:
    w, h = check_in_game()
    fx, fy = BUTTONS[button]
    x, y = round(fx * w), round(fy * h)
    hold = DPAD_HOLD_MS if button in ("UP", "DOWN", "LEFT", "RIGHT") else HOLD_MS
    sh(f"input swipe {x} {y} {x} {y} {hold}")


def brightness() -> float:
    """Mean green level (0-255) of BOX in a raw screenshot, sampled every 8 pixels."""
    select_phone()
    data = subprocess.run([ADB, "exec-out", "screencap"], capture_output=True, timeout=60).stdout
    w, h = struct.unpack("<II", data[:8])
    if len(data) < w * h * 4:
        return -1.0
    px = memoryview(data)[len(data) - w * h * 4:]
    x0, y0, x1, y1 = (round(f * d) for f, d in zip(BOX, (w, h, w, h)))
    total = count = 0
    for y in range(y0, y1, 8):
        row = px[(y * w + x0) * 4 + 1:(y * w + x1) * 4]
        total += sum(row[::32])
        count += len(row[::32])
    return total / count if count else -1.0


class Run:
    """One `play`: a log line per step with the time since the game was launched."""

    def __init__(self) -> None:
        self.t0 = time.monotonic()

    def log(self, text: str) -> None:
        print(f"[{time.monotonic() - self.t0:6.1f} s] {text}", flush=True)

    def wait(self, test, what: str, limit_s: float, press_button: str | None = None,
             press_every_s: float = 0.0, required: bool = True) -> float | None:
        """Polls brightness() about once a second until test(value) holds, pressing press_button
        every press_every_s meanwhile. At limit_s it stops the script, or returns None if not
        required."""
        deadline = time.monotonic() + limit_s
        last_press, presses = 0.0, 0
        while True:
            check_in_game()
            value = brightness()
            if test(value):
                pressed = f" after {presses} x {press_button}" if press_button else ""
                self.log(f"{what}: brightness {value:.1f}{pressed}")
                return value
            if time.monotonic() > deadline:
                if required:
                    sys.exit(f"gave up waiting for {what} after {limit_s:.0f} s "
                             f"(brightness {value:.1f})")
                return None
            if press_button and time.monotonic() - last_press >= press_every_s:
                press(press_button)
                presses += 1
                last_press = time.monotonic()
            time.sleep(0.5)


def life_is_strange_3(run: Run, until: str | None = None) -> None:
    """Brightness of BOX on xd-5c58dd1, Max Device Memory 1024 MB, -nostartupmovies -dx11
    (2026-10-18): 27 under GameNative's "Launching game..." screen, then black; the Square Enix and
    Deck Nine logos, the health warning and the autosave notice 9-32 (they skip on RB, or pass by
    themselves by 48 s after launch); title and menu 51-53; loading after Continue 7, then 0-3 for
    the chapter card; the first scene 19-45. RB on the title also counts as "any button", so the
    A presses allow for either screen."""
    run.wait(lambda v: 0 <= v < 1, "the game's window (black)", limit_s=240)
    run.wait(lambda v: v > 3, "the game's first picture", limit_s=120)
    run.wait(lambda v: v > 42, "the title screen", limit_s=120, press_button="RB",
             press_every_s=2.5)
    if until == "title":
        return
    if CLEAR_BACKGROUND:
        sh("am kill-all")  # background apps come back during the intros; the first scene needs the room
    time.sleep(1.0)
    for presses in range(1, 5):
        press("A")
        run.log("pressed A")
        if presses >= 2 and run.wait(lambda v: 0 <= v < 12, "loading after Continue",
                                     limit_s=8, required=False) is not None:
            break
        time.sleep(2.5)
    else:
        sys.exit("the menu did not start loading after four presses of A")
    # Background apps restart while the scene loads and the load is where lmkd kills GameNative, so
    # keep clearing them on the phone for the next 45 s without holding up the brightness polling.
    if CLEAR_BACKGROUND:
        sh("nohup sh -c 'for i in $(seq 15); do am kill-all; sleep 3; done' >/dev/null 2>&1 &")
    run.wait(lambda v: v > 12, "the first scene", limit_s=180)
    for _ in range(3):  # the opening cutscene
        time.sleep(2.0)
        press("RB")
        run.log("pressed RB")


GAMES = {"LifeIsStrange3": life_is_strange_3}


def play(game: str, restart: bool, until: str | None = None) -> None:
    if sh(f"pidof {GAME_PROCESS}") and not restart:
        sys.exit(f"a game is running already ({GAME_PROCESS}); pass --restart to stop it and start "
                 f"{game}")
    if CLEAR_BACKGROUND:
        sh("am kill-all")
    since = sh("date '+%m-%d %H:%M:%S.000'")
    run = Run()
    result = subprocess.run([sys.executable, str(HERE / "gn_driver.py"), "launch", game])
    if result.returncode:
        sys.exit(f"gn_driver.py launch {game} failed")
    run.log("launched")
    try:
        GAMES[game](run, until)
    except NotInGame as e:
        killed = sh(f"logcat -d -b main -s lmkd:I -t '{since}' | grep \"'app.gamenative'\"")
        if killed:
            sys.exit(f"{e}: the low-memory killer stopped GameNative:\n{killed.splitlines()[-1]}")
        raise
    run.log(f"{game} is {'at its ' + until + ' screen' if until else 'in play'}")


def look() -> None:
    try:
        w, h = check_in_game()
        state = f"presses allowed ({w}x{h})"
    except NotInGame as e:
        state = f"presses refused: {e}"
    print(f"brightness {brightness():.1f}; {state}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("press", help="press on-screen gamepad buttons")
    p.add_argument("buttons", nargs="+", type=str.upper, choices=sorted(BUTTONS), metavar="BUTTON")
    p.add_argument("--times", type=int, default=1)
    p.add_argument("--interval", type=float, default=0.8)
    p = commands.add_parser("play", help="start a game and get it past its intro screens")
    p.add_argument("game", choices=sorted(GAMES))
    p.add_argument("--restart", action="store_true", help="stop a game that is running first")
    p.add_argument("--until", choices=["title"], help="stop at this screen instead of play")
    p.add_argument("--no-kill", action="store_true",
                   help="don't run am kill-all to clear background apps")
    commands.add_parser("look", help="print what play sees and whether presses are allowed")
    args = parser.parse_args()

    stay_on = sh("settings get global stay_on_while_plugged_in").strip()
    sh(STAY_ON_CMD)
    try:
        if args.command == "press":
            for i in range(args.times):
                for j, button in enumerate(args.buttons):
                    if i or j:
                        time.sleep(args.interval)
                    press(button)
                    print(f"pressed {button}", flush=True)
        elif args.command == "play":
            global CLEAR_BACKGROUND
            CLEAR_BACKGROUND = not args.no_kill
            play(args.game, args.restart, args.until)
        else:
            look()
    except NotInGame as e:
        sys.exit(f"not pressing: {e}")
    finally:
        if stay_on.isdigit():
            sh(f"settings put global stay_on_while_plugged_in {stay_on}")


if __name__ == "__main__":
    main()
