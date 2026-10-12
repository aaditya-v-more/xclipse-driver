#!/usr/bin/env python3
"""Package verified upstream DXVK 2.7.1 DLLs for GameNative 0.9.2's Contents importer.

No phone access or downloads. Supply the release archive and upstream LICENSE:
https://github.com/doitsujin/dxvk/releases/download/v2.7.1/dxvk-2.7.1.tar.gz
https://raw.githubusercontent.com/doitsujin/dxvk/v2.7.1/LICENSE

usage: python pipeline/package_dxvk.py ARCHIVE LICENSE --output tools/dxvk-2.7.1.wcp
"""

import argparse
import hashlib
import io
import json
import struct
import tarfile
from pathlib import Path

ARCHIVE_SHA256 = "d85ce7c79f57ecd765aaa1b9e7007cb875e6fde9f6d331df799bce73d513ce87"
LICENSE_SHA256 = "a5cb1a6ded7d2d7e92d550ba28edd21be2d1d4044662b399887351023e30ce64"
DLLS = ("d3d10core.dll", "d3d11.dll", "d3d8.dll", "d3d9.dll", "dxgi.dll")


def verified(path: Path, expected: str) -> bytes:
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != expected:
        raise ValueError(f"checksum mismatch: {path}")
    return raw


def package(archive: Path, license_path: Path) -> bytes:
    raw = verified(archive, ARCHIVE_SHA256)
    license_bytes = verified(license_path, LICENSE_SHA256)
    contents, files = {}, []
    with tarfile.open(fileobj=io.BytesIO(raw), mode="r:gz") as source:
        for arch, target, machine in (("x32", "syswow64", 0x14c), ("x64", "system32", 0x8664)):
            for dll in DLLS:
                name = f"{arch}/{dll}"
                contents[name] = source.extractfile(f"dxvk-2.7.1/{name}").read()
                data = contents[name]
                pe = struct.unpack_from("<I", data, 0x3c)[0]
                if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0" or \
                        struct.unpack_from("<H", data, pe + 4)[0] != machine:
                    raise ValueError(f"wrong PE architecture: {name}")
                files.append({"source": name, "target": "${" + target + "}/" + dll})
    profile = {"type": "DXVK", "versionName": "2.7.1", "versionCode": 1,
               "description": "Unmodified upstream DXVK 2.7.1 x86 and x64 DLLs; "
                              "https://github.com/doitsujin/dxvk/releases/tag/v2.7.1",
               "files": files}
    contents["profile.json"] = (json.dumps(profile, indent=2) + "\n").encode()
    contents["LICENSE"] = license_bytes
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode="w:xz", preset=6) as destination:
        # GameNative 0.9.2 only creates parents from explicit directory entries. A valid tar
        # containing just x32/file.dll fails there with ENOENT despite extracting on Python.
        for directory in ("x32", "x64"):
            member = tarfile.TarInfo(directory + "/")
            member.type, member.mode = tarfile.DIRTYPE, 0o755
            destination.addfile(member)
        for name, data in contents.items():
            member = tarfile.TarInfo(name)
            member.size, member.mode = len(data), 0o644
            destination.addfile(member, io.BytesIO(data))
    result = output.getvalue()
    with tarfile.open(fileobj=io.BytesIO(result), mode="r:xz") as check:
        if {m.name: check.extractfile(m).read() for m in check.getmembers() if m.isfile()} != contents:
            raise ValueError("package readback differs from verified inputs")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("license", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = package(args.archive, args.license)
    if args.output.exists() and args.output.read_bytes() != result:
        parser.error(f"refusing to overwrite different content: {args.output}")
    args.output.write_bytes(result)
    print(f"{args.output}: {len(result)} bytes, SHA256 {hashlib.sha256(result).hexdigest()}")


if __name__ == "__main__":
    main()
