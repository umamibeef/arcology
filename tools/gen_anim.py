#!/usr/bin/env python3
"""Render SC2K's palette animation as an animated GIF.

The art never changes: the game keeps two colour tables for the runs at
155..203 and 224..238, swaps them every 12 ticks and rebuilds one from the
other through a permutation ($9750, $9770, $97FA), then hands the result to
_AnimatePalette. The pixels stay put, which is why a whole map animates for
nothing.

Each frame is rendered by arcology --soft with `--phase k`, so the permutation has
exactly one implementation (r_atlas_animate) rather than a second one here
that could drift from it. `--indexed` writes the GAME's palette indices --
not lodepng's re-numbering, which silently turned this whole thing into a
no-op until it was noticed.

    python3 tools/gen_anim.py <city> out.gif --crop x,y,w,h [--frames 24]
"""
import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from sc2kpack import read_indexed_png
from gif import write_gif_anim

EXE = HERE.parent / "build/arcology"
ASSETS = HERE.parent / "assets"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("city")
    ap.add_argument("out")
    #  Any arcology --soft arguments, passed straight through, so an animated
    #  figure can be framed exactly like the still it replaces (--focus
    #  included) rather than only by --crop.
    ap.add_argument("--sc2k", nargs=argparse.REMAINDER, default=[])
    ap.add_argument("--crop", default=None)
    ap.add_argument("--scale", type=int, default=2)
    ap.add_argument("--frames", type=int, default=24)
    #  $9756 schedules the next step 12 ticks out: 5 a second.
    ap.add_argument("--delay", type=int, default=20, help="centiseconds")
    ap.add_argument("--underground", action="store_true")
    a = ap.parse_args()

    #  A private directory per run: two renders sharing one frame file
    #  would read each other's frames.
    tmp = Path(tempfile.mkdtemp(prefix="sc2kanim-"))
    #  The pixels are identical in every frame, so render them once and
    #  collect only the palettes; write_gif_anim then emits the picture
    #  once and the moving colours as tiny transparent overlays.
    palettes, rows, w, h = [], None, 0, 0
    for k in range(a.frames):
        f = tmp / ("f%d.png" % k)
        cmd = [str(EXE), "--soft", str(ASSETS), a.city, str(f), "--zoom", "32",
               "--indexed", "--phase", str(k)]
        if a.crop:
            cmd += ["--crop", a.crop, "--scale", str(a.scale)]
        cmd += a.sc2k
        if a.underground:
            cmd.append("--underground")
        subprocess.run(cmd, capture_output=True, check=True)
        w, h, r, pal = read_indexed_png(f)
        pal = [tuple(c) for c in pal]
        while len(pal) < 256:
            pal.append((0, 0, 0))
        if rows is None:
            rows = r
        palettes.append(pal)
    runs = [(x["first"], x["count"])
            for x in json.loads((ASSETS / "atlas.json").read_text())["animated"]]
    n = write_gif_anim(a.out, w, h, rows, palettes, runs, delay_cs=a.delay)
    shutil.rmtree(tmp, ignore_errors=True)
    print("%d frames, %dx%d, %.1f KB -> %s"
          % (len(palettes), w, h, n / 1024, a.out))


if __name__ == "__main__":
    main()
