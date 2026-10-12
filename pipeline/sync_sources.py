#!/usr/bin/env python3
"""Restore reference submodules at the commits pinned in this checkout's Git index.

Uses sparse checkouts for the kernel and large research trees, including on Windows where the
full Linux kernel contains reserved filenames. Preserves dirty worktrees and never follows a
remote branch. GameNative's and DXVK's nested dependencies are initialized at their recorded pins.
"""

import argparse
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SPARSE = {
    "kernel-r12s": ["drivers/gpu/drm/samsung", "include/uapi/drm", "arch/arm64/configs"],
    "gamenative-mesa": ["src/vulkan/wrapper", "src/vulkan/wsi"],
    "radv-xclipse940": ["src/amd", "src/vulkan", "src/util", "include"],
    "mesa-upstream": ["src/x11", "src/loader", "include/vulkan"],
}


def git(where: Path, *args: str, capture=False) -> str:
    result = subprocess.run(["git", "-C", str(where), *args], check=True, text=True,
                            stdout=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else ""


def modules(root: Path) -> list[tuple[str, str, str, str]]:
    result = []
    config = git(root, "config", "-f", ".gitmodules", "--get-regexp", r"submodule\..*\.path", capture=True)
    for line in config.splitlines():
        key, path = line.split(None, 1)
        name = key[len("submodule."):-len(".path")]
        url = git(root, "config", "-f", ".gitmodules", "--get", f"submodule.{name}.url", capture=True)
        entry = git(root, "ls-files", "--stage", "--", path, capture=True).split()
        if len(entry) != 4 or entry[0] != "160000" or entry[2] != "0":
            raise RuntimeError(f"{path}: expected one pinned submodule entry, found {entry}")
        result.append((name, path, url, entry[1]))
    return result


def sync(root: Path, name: str, path: str, url: str, commit: str) -> None:
    destination = root / path
    git(root, "submodule", "init", "--", path)
    if not (destination / ".git").exists():
        if destination.exists() and any(destination.iterdir()):
            raise RuntimeError(f"{destination}: not an empty directory; keeping its files")
        directory = Path(git(root, "rev-parse", "--git-path", f"modules/{name}", capture=True))
        directory = (root / directory).resolve() if not directory.is_absolute() else directory
        if directory.exists():  # A previously deinitialized submodule already has its objects.
            destination.mkdir(parents=True, exist_ok=True)
            (destination / ".git").write_text(f"gitdir: {directory.as_posix()}\n", encoding="utf-8")
            git(destination, "config", "core.worktree", str(destination.resolve()))
        else:
            directory.parent.mkdir(parents=True, exist_ok=True)
            git(root, "-c", "core.autocrlf=false", "-c", "core.longpaths=true", "clone",
                "--depth", "1", "--filter=blob:none", "--no-checkout",
                "--separate-git-dir", str(directory), url, str(destination))
    elif git(destination, "status", "--porcelain", capture=True):
        raise RuntimeError(f"{path}: has local changes; keeping them")
    git(destination, "config", "core.autocrlf", "false")
    git(destination, "config", "core.longpaths", "true")
    present = subprocess.run(["git", "-C", str(destination), "cat-file", "-e", f"{commit}^{{commit}}"],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    if not present:
        git(destination, "fetch", "--depth", "1", "--no-tags", "origin", commit)
    if destination.name in SPARSE:
        git(destination, "sparse-checkout", "set", "--cone", "--sparse-index", *SPARSE[destination.name])
    git(destination, "checkout", "--detach", commit)
    if destination.name in {"gamenative", "dxvk"}:
        git(destination, "submodule", "update", "--init", "--recursive", "--depth", "1", "--filter=blob:none")
    if git(destination, "rev-parse", "HEAD", capture=True) != commit:
        raise RuntimeError(f"{path}: checkout did not reach {commit}")
    if git(destination, "status", "--porcelain", capture=True):
        raise RuntimeError(f"{path}: checkout is not clean")
    print(f"{path}: {commit} (clean)", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", nargs="+", help="restore only these folder names")
    args = parser.parse_args()
    entries = modules(ROOT)
    if args.only:
        unknown = set(args.only) - {Path(p).name for _, p, _, _ in entries}
        if unknown:
            parser.error(f"unknown source names: {sorted(unknown)}")
    for entry in entries:
        if not args.only or Path(entry[1]).name in args.only:
            sync(ROOT, *entry)


if __name__ == "__main__":
    main()
