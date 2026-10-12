#!/usr/bin/env python3
"""Prepare a pinned, isolated SDK for experimental GameNative X11 WSI builds.

No packages are installed. Only public headers are read from Termux packages; the Android
libraries come from GameNative's own runtime. This never modifies the phone or third_party.
"""
from __future__ import annotations

import hashlib
import io
import json
import tarfile
import urllib.request
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "tools/gamenative-x11"
LOCK = ROOT / "driver/android/x11-deps.json"


def fetch(url, sha):
    destination = ROOT / "tools" / url.rsplit("/", 1)[-1]
    if not destination.exists():
        print(f"download {url}", flush=True)
        temporary = destination.with_suffix(destination.suffix + ".partial")
        with urllib.request.urlopen(url, timeout=60) as response, temporary.open("wb") as output:
            while data := response.read(1 << 20):
                output.write(data)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != sha:
            raise ValueError(f"download checksum mismatch: {temporary}")
        temporary.replace(destination)
    if hashlib.sha256(destination.read_bytes()).hexdigest() != sha:
        raise ValueError(f"checksum mismatch: {destination}")
    return destination


def deb_data(path):
    data = path.read_bytes()
    if not data.startswith(b"!<arch>\n"):
        raise ValueError("not a Debian ar archive")
    pos = 8
    while pos + 60 <= len(data):
        name = data[pos:pos + 16].decode().strip().rstrip("/")
        size = int(data[pos + 48:pos + 58])
        payload = data[pos + 60:pos + 60 + size]
        if name.startswith("data.tar"):
            return tarfile.open(fileobj=io.BytesIO(payload), mode="r:*")
        pos += 60 + size + size % 2
    raise ValueError("no data archive")


def put(relative, data):
    path = SDK / relative
    if SDK.resolve() not in path.resolve().parents:
        raise ValueError("archive path escapes SDK")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def main():
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    for package in lock["headers"]:
        archive = fetch("https://packages.termux.dev/apt/termux-main/" + package["file"], package["sha256"])
        with deb_data(archive) as tar:
            for member in tar:
                if not member.isfile():
                    continue
                name = member.name
                if "/usr/include/" in name:
                    put("include/" + name.split("/usr/include/", 1)[1], tar.extractfile(member).read())
                elif "/usr/share/doc/" in name and name.endswith(("copyright", "LICENSE")):
                    put("licenses/" + package["package"] + "-" + PurePosixPath(name).name,
                        tar.extractfile(member).read())

    runtime = fetch(lock["runtime"]["url"], lock["runtime"]["sha256"])
    files, links = {}, {}
    prefixes = ("libxcb", "libX11", "libxshmfence", "libXau", "libXdmcp", "libandroid-")
    # Stream once: seeking backwards into xz separately for every symlink is extremely slow.
    with tarfile.open(runtime, mode="r|xz") as tar:
        for member in tar:
            name = PurePosixPath(member.name)
            if str(name.parent) != "usr/lib" or not name.name.startswith(prefixes):
                continue
            if member.isfile():
                files[name.name] = tar.extractfile(member).read()
            elif member.issym():
                links[name.name] = PurePosixPath(member.linkname).name
    for name, target in links.items():
        seen = {name}
        while target in links:
            if target in seen:
                raise ValueError("circular library symlink")
            seen.add(target)
            target = links[target]
        if target not in files:
            raise ValueError(f"missing symlink target: {target}")
        files[name] = files[target]
    for name, data in files.items():
        put("lib/" + name, data)
    put("manifest.json", json.dumps({"inputs": lock, "runtime_library_sha256": {
        name: hashlib.sha256(data).hexdigest() for name, data in files.items()}}, indent=2).encode())
    print(f"SDK ready: {SDK} ({len(files)} runtime library names)", flush=True)


if __name__ == "__main__":
    main()
