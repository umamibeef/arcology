#!/usr/bin/env python3
"""gen_showcase.py -- the animated screenshots in the README.

    python3 tools/gen_showcase.py                 # Bayview, the defaults
    python3 tools/gen_showcase.py --city Hawaii --frames 40

Each frame is a real frame of the real game: the script runs `arcology`
with `--run N --shot`, letting the simulation advance between shots, so
what the GIF shows is the renderer and the reconstruction actually
running, not a mock-up.  Regenerate it whenever either changes -- the
image is only as current as the last run, which is why `cmake --build
build --target showcase` exists.

The GPU renderer draws from the game's own palette, so a frame holds
around 140 distinct colours and maps straight onto a GIF's 256-entry
table with no quantiser in the way.  If a frame ever exceeds 256 the
script says so rather than dithering silently.
"""
import argparse
import collections
import struct
import subprocess
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
from gif import write_gif  # noqa: E402

BIN = ROOT / "build" / "arcology"
ASSETS = ROOT / "assets"


def read_rgb_png(path):
    """(w, h, bytes) for a colour-type-2 8-bit PNG -- what --shot writes."""
    b = path.read_bytes()
    assert b[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat, w, h, ctype = 8, b"", 0, 0, 0
    while pos < len(b):
        n = struct.unpack(">I", b[pos:pos + 4])[0]
        tag, dat = b[pos + 4:pos + 8], b[pos + 8:pos + 8 + n]
        pos += 12 + n
        if tag == b"IHDR":
            w, h, _, ctype = struct.unpack(">IIBB", dat[:10])
        elif tag == b"IDAT":
            idat += dat
        elif tag == b"IEND":
            break
    if ctype != 2:
        raise SystemExit("%s is not RGB (colour type %d)" % (path, ctype))
    raw = zlib.decompress(idat)
    stride = w * 3
    out, prev, pos = bytearray(), bytearray(stride), 0
    for _ in range(h):
        f = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if f == 1:
            for x in range(3, stride):
                line[x] = (line[x] + line[x - 3]) & 255
        elif f == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                a = line[x - 3] if x >= 3 else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x - 3] if x >= 3 else 0
                bb = prev[x]
                c = prev[x - 3] if x >= 3 else 0
                p = a + bb - c
                pa, pb, pc = abs(p - a), abs(p - bb), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (bb if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        out += line
        prev = line
    return w, h, bytes(out)


def downscale(w, h, rgb, factor):
    """Point-sample by an integer factor.

    NOT a box filter.  Averaging four palette colours produces a fifth
    that is in no palette, and a 140-colour frame becomes a
    several-thousand-colour one that will not fit a GIF at all.  Point
    sampling keeps the exact palette, and on pixel art it also keeps the
    edges hard, which is what this art wants anyway.
    """
    if factor <= 1:
        return w, h, rgb
    nw, nh = w // factor, h // factor
    out = bytearray(nw * nh * 3)
    for y in range(nh):
        src = (y * factor) * w * 3
        dst = y * nw * 3
        for x in range(nw):
            o = src + x * factor * 3
            out[dst + x * 3:dst + x * 3 + 3] = rgb[o:o + 3]
    return nw, nh, bytes(out)


def join(w, h, left, right, gap=4):
    """Two frames side by side with a dark seam between them."""
    nw = w * 2 + gap
    out = bytearray(nw * h * 3)
    for y in range(h):
        d = y * nw * 3
        sl = y * w * 3
        out[d:d + w * 3] = left[sl:sl + w * 3]
        d2 = d + (w + gap) * 3
        out[d2:d2 + w * 3] = right[sl:sl + w * 3]
    return nw, h, bytes(out)


def median_cut(hist, want=256):
    """An adaptive palette for the frames, and the map onto it.

    The sprite renderer draws straight from the game's 256-entry table
    and needs none of this.  The geometry renderer shades the terrain
    mesh, so a single frame carries around twenty thousand colours and
    the 256 commonest cover only about seventy per cent of the picture
    -- taking the top 256 bands every hillside.

    Median cut instead: hold the colours in boxes, repeatedly split the
    box with the largest spread along its widest channel at the median,
    and stop at `want` boxes.  Each box becomes one palette entry, the
    pixel-weighted mean of what fell in it, so common colours get
    represented exactly and rare ones share.

    No dithering.  Dither noise does not correlate between frames, so a
    dithered GIF shimmers where the picture is perfectly still.
    """
    boxes = [list(hist.items())]
    while len(boxes) < want:
        #  Split the box that spans the most, weighted by how many pixels
        #  are in it: a wide box holding four pixels is not worth an entry.
        best, best_score = -1, -1.0
        for i, box in enumerate(boxes):
            if len(box) < 2:
                continue
            lo = [min(c[ch] for c, _ in box) for ch in range(3)]
            hi = [max(c[ch] for c, _ in box) for ch in range(3)]
            spread = max(hi[ch] - lo[ch] for ch in range(3))
            score = spread * (sum(n for _, n in box) ** 0.5)
            if score > best_score:
                best, best_score = i, score
        if best < 0:
            break
        box = boxes.pop(best)
        lo = [min(c[ch] for c, _ in box) for ch in range(3)]
        hi = [max(c[ch] for c, _ in box) for ch in range(3)]
        ch = max(range(3), key=lambda k: hi[k] - lo[k])
        box.sort(key=lambda cn: cn[0][ch])
        #  Split at the median PIXEL, not the median colour, so a box
        #  holding one huge flat area and many stray colours divides where
        #  the picture actually is.
        half, run, cut = sum(n for _, n in box) / 2.0, 0, 1
        for i, (_, n) in enumerate(box):
            run += n
            if run >= half:
                cut = max(1, min(i, len(box) - 1))
                break
        boxes += [box[:cut], box[cut:]]

    palette, lut = [], {}
    for i, box in enumerate(boxes):
        tot = sum(n for _, n in box) or 1
        palette.append(tuple(
            int(sum(c[ch] * n for c, n in box) / tot) for ch in range(3)))
        for c, _ in box:
            lut[c] = i
    while len(palette) < 256:
        palette.append((0, 0, 0))
    return palette, lut


def index(w, h, rgb, lut):
    rows = []
    for y in range(h):
        row = bytearray(w)
        base = y * w * 3
        for x in range(w):
            row[x] = lut[rgb[base + x * 3:base + x * 3 + 3]]
        rows.append(row)
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--city", default="Bayview")
    ap.add_argument("--cities-dir", default=None,
                    help="where the city files are (default: $SC2K_CITIES)")
    ap.add_argument("--out", default=str(ROOT / "media" / "bayview.gif"))
    ap.add_argument("--frames", type=int, default=24)
    ap.add_argument("--step", type=int, default=45,
                    help="game frames to advance between shots")
    ap.add_argument("--speed", type=int, default=4)
    ap.add_argument("--scale", type=int, default=2, help="downscale factor")
    ap.add_argument("--delay", type=int, default=12, help="centiseconds")
    ap.add_argument("--width", type=int, default=1280)
    ap.add_argument("--height", type=int, default=800)
    ap.add_argument("--centre", default=None,
                    help="COL,ROW -- put that map tile in the middle of the "
                         "frame.  Easier to reason about than --scroll, and "
                         "it survives a change of zoom or window size")
    ap.add_argument("--scroll", default="1750,1400",
                    help="canvas pixel at the window's top-left.  The "
                         "default frames Bayview on its suspension bridge, "
                         "with both developed shores and the terrain cut "
                         "that shows the seabed geometry")
    ap.add_argument("--compare", action="store_true",
                    help="the original sprite terrain beside the new "
                         "geometry, same city, same frame")
    a = ap.parse_args()

    if not BIN.exists():
        raise SystemExit("build it first: cmake --build build")
    cities = a.cities_dir
    if cities is None:
        import os
        cities = os.environ.get("SC2K_CITIES")
        if cities:
            cities = str(Path(cities) / "Cities")
    if not cities:
        raise SystemExit("say where the cities are: --cities-dir, or set "
                         "SC2K_CITIES to the game folder")
    city = Path(cities) / a.city
    if not city.exists():
        raise SystemExit("no such city: %s" % city)

    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.parent / ".frames"
    tmp.mkdir(exist_ok=True)

    #  Collect every frame as RGB first.  The palette has to be shared
    #  across the whole animation -- a per-frame palette makes the still
    #  parts of the picture crawl -- so it cannot be built until all the
    #  frames are in.
    shots = []

    def shoot(k, png, sprites):
        cmd = [str(BIN), str(ASSETS), str(city),
               "--run", str(1 + k * a.step), "--speed", str(a.speed),
               "--shot", str(png)]
        cmd += (["--centre", a.centre] if a.centre
                else ["--scroll", a.scroll])
        #  --sprites is the original's own terrain and water art, from the
        #  same atlas the 1995 build drew from.  --geometry is the mesh and
        #  the water shader.  Both are named explicitly because a headless
        #  --run defaults to sprites -- it is a comparison harness first --
        #  and a showcase that quietly rendered the old art on both sides
        #  is exactly the mistake this comment exists to stop.
        cmd.append("--sprites" if sprites else "--geometry")
        r = subprocess.run(cmd, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
        if r.returncode != 0 or not png.exists():
            raise SystemExit("frame %d failed: %s" % (k, " ".join(cmd)))
        w, h, rgb = read_rgb_png(png)
        return downscale(w, h, rgb, a.scale)

    hist = collections.Counter()
    for k in range(a.frames):
        w, h, rgb = shoot(k, tmp / ("f%03d.png" % k), False)
        if a.compare:
            _, _, old = shoot(k, tmp / ("o%03d.png" % k), True)
            w, h, rgb = join(w, h, old, rgb)
        shots.append(rgb)
        for i in range(0, len(rgb), 3):
            hist[rgb[i:i + 3]] += 1
        sys.stdout.write("\r  frame %d/%d" % (k + 1, a.frames))
        sys.stdout.flush()
    print()

    print("  %d distinct colours -> 256" % len(hist))
    palette, lut = median_cut(hist)
    frames = [index(w, h, rgb, lut) for rgb in shots]

    n = write_gif(str(out), w, h, [(f, palette) for f in frames],
                  delay_cs=a.delay)
    print("wrote %s  %dx%d, %d frames, %d colours, %d bytes"
          % (out, w, h, len(frames), len(palette), n))

    #  A still as well, for anywhere a GIF is wrong -- the SAME first
    #  frame the animation opens on, joined pair and all, not the raw
    #  single-renderer shot it was built from.
    still = out.with_suffix(".png")
    write_gif(str(still.with_suffix(".still.gif")), w, h,
              [(frames[0], palette)], delay_cs=0, loop=0)
    print("wrote %s" % still.with_suffix(".still.gif"))


if __name__ == "__main__":
    main()
