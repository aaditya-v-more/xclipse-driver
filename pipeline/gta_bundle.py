#!/usr/bin/env python3
"""Builds the GTA V setup bundle for an S24 FE: driver zip, GameNative config, commandline.txt.

The config is our working GTA V container export (docs/games/GTAV.container.json) cut down to the
settings that carry over to another phone's copy of the game. GameNative's Import config applies
only known fields and never the screen size, drives or install path, so the import can't point the
container at the wrong folder. The fields dropped here are the ones tied to our copy: the
executable (GameNative launches Steam's Rockstar titles through its own stub) and the Steam and
DRM launch modes, which GameNative chooses for a Steam game. Its WINEDLLOVERRIDES entry loaded a
version.dll that only our copy's folder ships, so it's removed too.

usage: python pipeline/gta_bundle.py [--driver ZIP] [--out ZIP]
"""

from __future__ import annotations

import argparse
import json
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run import ROOT  # noqa: E402

GAMES = ROOT / "docs" / "games"
DRIVER = ROOT / "driver" / "dist" / "radv-xclipse-26.3.0-devel-xd-4f88558.zip"
CONFIG_NAME = "GTAV-S24FE.config.json"

# Fields GameNative's import applies that describe the graphics and emulation setup.
KEEP = [
    "graphicsDriver", "graphicsDriverConfig", "dxwrapper", "dxwrapperConfig",
    "containerVariant", "wineVersion", "emulator", "box64Version", "box64Preset",
    "fexcoreVersion", "fexcorePreset", "envVars", "cpuList", "cpuListWoW64",
    "audioDriver", "wincomponents", "startupSelection",
]
DROP_ENV = {"WINEDLLOVERRIDES=version=n,b"}


def config() -> dict:
    full = json.loads((GAMES / "GTAV.container.json").read_text())
    out = {key: full[key] for key in KEEP}
    out["envVars"] = " ".join(v for v in full["envVars"].split() if v not in DROP_ENV)
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--driver", type=Path, default=DRIVER)
    ap.add_argument("--out", type=Path, default=ROOT / "driver" / "dist" / "GTAV-S24FE-GameNative.zip")
    args = ap.parse_args()

    text = json.dumps(config(), indent=2) + "\n"
    (GAMES / CONFIG_NAME).write_text(text)
    version = json.loads(zipfile.ZipFile(args.driver).read("meta.json"))["name"]
    if version not in text:
        sys.exit(f"{args.driver.name} is {version!r}; the config names another driver")

    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(args.driver, args.driver.name, compress_type=zipfile.ZIP_STORED)
        z.writestr(CONFIG_NAME, text)
        z.write(GAMES / "GTAV.commandline.txt", "commandline.txt")
        z.write(GAMES / "GTAV-S24FE-README.txt", "README.txt")
    print(f"{args.out} ({args.out.stat().st_size / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
