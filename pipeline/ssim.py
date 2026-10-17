#!/usr/bin/env python3
"""Compares a game_compare run's screenshots between variants with SSIM.

Heaven's camera is timed, so two screenshots taken at the same second differ a little even on the
same driver. The baseline's own run-to-run SSIM is therefore the noise floor: a variant whose SSIM
against the baseline sits well below that floor renders differently.

Screenshots are named pair<N>-<pair label>-<variant>-<game>-<driver>-r<seconds>s.png; warm-up pairs
are skipped. A variant is compared with the baseline from the same pair. Images
are compared in grayscale at a quarter of their size, in 8x8 windows. On macOS `sips` decodes the
PNGs; elsewhere a slower pure-Python decoder does.

usage: python pipeline/ssim.py results/<time>-game-compare [--baseline baseline]
"""
import argparse
import itertools
import re
import shutil
import statistics
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

SCALE = 4
WINDOW = 8
NAME = re.compile(r"^pair(\d+)-([^-]+)-([^-]+)-[^-]+-[^-]+-r(\d+)s\.png$")


def gray_from_rgb(rows, width, height, channels):
    """Box-downscales rows of RGB(A) bytes to a SCALE-times smaller grayscale grid."""
    w, h = width // SCALE, height // SCALE
    out = [[0.0] * w for _ in range(h)]
    for y in range(h * SCALE):
        row = rows[y]
        line = out[y // SCALE]
        for x in range(w * SCALE):
            o = x * channels
            line[x // SCALE] += 0.299 * row[o] + 0.587 * row[o + 1] + 0.114 * row[o + 2]
    n = SCALE * SCALE
    return [[v / n for v in line] for line in out]


def decode_png(data):
    pos, idat, ihdr = 8, [], None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", chunk)
        elif kind == b"IDAT":
            idat.append(chunk)
        pos += 12 + length
    width, height, depth, color, _, _, interlace = ihdr
    if depth != 8 or color not in (2, 6) or interlace:
        sys.exit(f"unsupported PNG: depth {depth}, color type {color}, interlace {interlace}")
    bpp = 4 if color == 6 else 3
    raw = zlib.decompress(b"".join(idat))
    stride = width * bpp
    prev = bytearray(stride)
    rows = []
    for y in range(height):
        base = y * (stride + 1)
        kind = raw[base]
        cur = bytearray(raw[base + 1:base + 1 + stride])
        if kind == 1:
            for i in range(bpp, stride):
                cur[i] = (cur[i] + cur[i - bpp]) & 255
        elif kind == 2:
            for i in range(stride):
                cur[i] = (cur[i] + prev[i]) & 255
        elif kind == 3:
            for i in range(stride):
                left = cur[i - bpp] if i >= bpp else 0
                cur[i] = (cur[i] + ((left + prev[i]) >> 1)) & 255
        elif kind == 4:
            for i in range(stride):
                a = cur[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                cur[i] = (cur[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(cur)
        prev = cur
    return gray_from_rgb(rows, width, height, bpp)


def decode_bmp(data):
    offset, = struct.unpack("<I", data[10:14])
    width, height = struct.unpack("<ii", data[18:26])
    bits, = struct.unpack("<H", data[28:30])
    channels = bits // 8
    stride = (width * channels + 3) & ~3
    rows = []
    for y in range(abs(height)):
        src = y if height < 0 else abs(height) - 1 - y
        row = data[offset + src * stride:offset + src * stride + width * channels]
        # BMP stores BGR(A); swap to RGB order for the luma weights.
        rgb = bytearray(row)
        rgb[0::channels], rgb[2::channels] = row[2::channels], row[0::channels]
        rows.append(rgb)
    return rows, width, abs(height), channels


def load(path, scratch):
    if shutil.which("sips"):
        out = Path(scratch) / (path.stem + ".bmp")
        subprocess.run(["sips", "-s", "format", "bmp", str(path), "--out", str(out)],
                       check=True, capture_output=True)
        return gray_from_rgb(*decode_bmp(out.read_bytes()))
    return decode_png(path.read_bytes())


def ssim(a, b):
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    h, w = min(len(a), len(b)), min(len(a[0]), len(b[0]))
    scores = []
    n = WINDOW * WINDOW
    for y in range(0, h - WINDOW + 1, WINDOW):
        for x in range(0, w - WINDOW + 1, WINDOW):
            xs = [a[y + j][x + i] for j in range(WINDOW) for i in range(WINDOW)]
            ys = [b[y + j][x + i] for j in range(WINDOW) for i in range(WINDOW)]
            mx, my = sum(xs) / n, sum(ys) / n
            vx = sum((v - mx) ** 2 for v in xs) / (n - 1)
            vy = sum((v - my) ** 2 for v in ys) / (n - 1)
            cov = sum((p - mx) * (q - my) for p, q in zip(xs, ys)) / (n - 1)
            scores.append(((2 * mx * my + c1) * (2 * cov + c2)) / ((mx * mx + my * my + c1) * (vx + vy + c2)))
    return statistics.fmean(scores)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder", type=Path)
    ap.add_argument("--baseline", default="baseline")
    args = ap.parse_args()

    shots = {}  # (variant, seconds) -> {pair: path}
    for path in sorted(args.folder.glob("pair*.png")):
        m = NAME.match(path.name)
        if not m or m.group(2) == "warmup":
            continue
        pair = (int(m.group(1)), m.group(2))
        shots.setdefault((m.group(3), int(m.group(4))), {})[pair] = path
    variants = sorted({v for v, _ in shots} - {args.baseline})
    times = sorted({t for v, t in shots if v == args.baseline})
    if not times:
        sys.exit(f"no {args.baseline} screenshots in {args.folder}")

    with tempfile.TemporaryDirectory() as scratch:
        cache = {}

        def image(path):
            if path not in cache:
                cache[path] = load(path, scratch)
            return cache[path]

        print(f"SSIM, grayscale at 1/{SCALE} size, {WINDOW}x{WINDOW} windows; 1.0 is identical.\n")
        print("| Shot | " + " | ".join([f"{args.baseline} vs itself"] + [f"{v} vs {args.baseline}" for v in variants]) + " |")
        print("|---" * (2 + len(variants)) + "|")
        for t in times:
            base = shots[(args.baseline, t)]
            floor = [ssim(image(base[p]), image(base[q])) for p, q in itertools.combinations(sorted(base), 2)]
            cells = [f"{min(floor):.3f}–{max(floor):.3f}" if floor else "-"]
            for v in variants:
                other = shots.get((v, t), {})
                vals = [ssim(image(base[p]), image(other[p])) for p in sorted(base) if p in other]
                cells.append(f"{min(vals):.3f}–{max(vals):.3f}" if vals else "-")
            print(f"| {t} s | " + " | ".join(cells) + " |", flush=True)


if __name__ == "__main__":
    main()
