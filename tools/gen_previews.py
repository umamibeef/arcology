#!/usr/bin/env python3
"""Render every preview picture the docs carry, and check the docs against it.

Every figure declared in FIGURES is rendered into docs/img/preview/<key>.<ext>
and nothing else lives in that directory.  The four preview pages under
docs/ are hand-written prose that embed those files by key, and each
picture is preceded by the command that made it.  Run it after any change
to the renderer:

    python3 tools/gen_previews.py            # render everything, then check
    python3 tools/gen_previews.py --reuse    # render only what is missing
    python3 tools/gen_previews.py --check    # check the pages, render nothing

The check reads docs/img/preview/manifest.json, written by a render, and
fails if a page embeds a file that is not a declared figure, prints a
command that is not the one that made the picture, or leaves a declared
figure out.  That is what makes "the command above each picture is the one
that made it" a statement of fact rather than an intention.
"""
import json
import re
import shutil
import subprocess
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
EXE = ROOT / "build/arcology"
ASSETS = ROOT / "assets"
OUT = ROOT / "docs/img/preview"
MANIFEST = OUT / "manifest.json"
DOCS = ROOT / "docs"
GAME = Path(os.environ.get(
    "SC2K_CITIES",
    Path.home() / "Downloads" / "SimCity 2000\u00ae Collection"))


def city(name):
    for p in (GAME / name, GAME / "Cities" / name):
        if p.exists():
            return p
    raise SystemExit("no such city: %s" % name)


def run(args, out=None):
    cmd = [str(EXE), "--soft", str(ASSETS)] + args
    if out:
        cmd.insert(3, str(out))
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("FAILED: %s\n%s" % (" ".join(cmd), r.stderr))
    return r.stdout


def dump(city_path, extra=()):
    """-> [(row, col, shape, x, y, flip), ...]"""
    txt = run([str(city_path), "--zoom", "32", "--dump-blits"] + list(extra))
    rows = []
    for line in txt.splitlines():
        f = line.split()
        if len(f) == 6:
            rows.append(tuple(int(v) for v in f))
    return rows


def tile_h(zoom=32):
    d = json.loads((ASSETS / ("tiles%d.json" % zoom)).read_text())
    return {int(k): v["frame"]["h"] for k, v in d["frames"].items()}


def find(city_path, pred, extra=(), margin=8):
    """The first tile, away from the map border, whose blits satisfy pred."""
    best = None
    for (r, c, sh, x, y, fl) in dump(city_path, extra):
        if not (margin <= r < 128 - margin and margin <= c < 128 - margin):
            continue
        if pred(sh):
            if best is None:
                best = (r, c)
    return best


#  ---------------------------------------------------------------- figures
MANHATTAN = city("Manhattan")
BAYVIEW = city("Bayview")

H = tile_h()
#  The tallest thing Manhattan actually draws: the clearest test of where a
#  multi-tile building's art sits relative to its anchor tile.
tall = max((H.get(sh - 1000, 0), r, c)
           for (r, c, sh, x, y, f) in dump(MANHATTAN)
           if 1112 <= sh < 1500 and 8 <= r < 120 and 8 <= c < 120)
TALL_RC = (tall[1], tall[2])

#  The aircraft: type 1, shapes 359..363.  It is drawn 120 px above its own
#  tile, which is why a tight crop kept missing it.
AIR_RC = find(MANHATTAN, lambda sh: 1359 <= sh <= 1363, margin=4)

#  A subway junction that both turns and changes altitude.
JUNC_RC = (3, 28)

#  Land that is zoned but still empty: the $17192 lot art, shape 290 + zone.
ZONE_RC = find(MANHATTAN, lambda sh: 1291 <= sh <= 1298) or (64, 64)

FIGURES = [
    # ---- tab 1: things I cannot judge -----------------------------------
    dict(tab="Settled", key="judge-drop-a", anim=5,
         title="A tall building, anchored",
         city=MANHATTAN,
         args=["--zoom", "32", "--focus", "%d,%d,150" % TALL_RC, "--scale", "3"],
         cap="the tallest building in Manhattan, 3&times;",
         note="There is no separate &ldquo;footprint drop&rdquo;. The game "
              "positions every sprite by subtracting the $1226 "
              "descriptor's +4, which $18E96 uses as the sprite's height, "
              "so a taller sprite reaches further down by construction."),
    dict(tab="Settled", key="judge-drop-b", anim=5,
         title="B &mdash; the same tile with the height subtraction removed",
         city=MANHATTAN,
         args=["--zoom", "32", "--no-drop", "--focus", "%d,%d,150" % TALL_RC,
               "--scale", "3"],
         cap="same tile, drop disabled, 3&times;",
         note="The same tile with that subtraction removed, which is what "
              "a zeroed $1226 gives on both sides. The table carries the "
              "real sprite sizes, so the anchor is inside the comparison."),
    dict(tab="Settled", key="judge-plane", anim=5,
         title="An aircraft at its own altitude",
         city=MANHATTAN,
         args=["--zoom", "32",
               "--focus", "%d,%d,90,0,-72" % AIR_RC, "--scale", "6"],
         cap="type 1 at row %d col %d, 6&times;" % AIR_RC,
         note="A thing is not pinned to its tile. $A232 reads an altitude "
              "from the XTHG record at +5 and scales it by 2&lt;&lt;zoom, "
              "half the terrain step; this aircraft carries 9, so it "
              "belongs 72 px up. The sub-tile position at +6 and +7 "
              "($A2F6, $A322) is what lets cars and boats sit between "
              "tiles rather than snapping to them."),
    dict(tab="Settled", key="judge-underground", anim=5,
         title="Subway junction that turns and changes altitude",
         city=BAYVIEW, ug=True,
         args=["--zoom", "32", "--underground",
               "--focus", "%d,%d,150" % JUNC_RC, "--scale", "3"],
         cap="Bayview, row %d col %d, 3&times;" % JUNC_RC,
         note="A subway junction that both turns and changes altitude, "
              "drawn by $161DC."),
    dict(tab="Open", key="judge-anim", ext=".gif", tool="anim",
         title="Palette animation, running",
         city=BAYVIEW,
         args=["--crop", "1380,1300,180,140", "--scale", "2",
               "--frames", "16"],
         cap="16 frames at the game's own 5 steps a second, 2&times; &mdash; "
             "the pixels never change, only the palette",
         note="The art is completely static; only the palette moves, and "
              "it is a permutation rather than a rotation. $9750 keeps "
              "TWO colour tables, swaps them every 12 ticks and rebuilds one "
              "from the other through a permutation ($9770, $97FA). The "
              "49-entry run is three eight-cycles, a four-cycle, a fixed "
              "point, then an eight-cycle, a four-cycle and an eight-cycle "
              "running the OTHER way; the 15-entry run is seven swaps, a "
              "blink rather than a flow. Turning all 49 as one block mixed "
              "ramps that have nothing to do with each other. The timing is "
              "the game's own: 12 ticks, 5 steps a second. A palette-only "
              "effect leaves every index identical, so the pixel "
              "comparison has nothing to compare; the permutation and the "
              "cadence are read from the listing."),

    dict(tab="Settled", key="judge-hotspot", tool="pixel_sbs", anim_sbs=5,
         title="Road, rail and power crossing",
         city=MANHATTAN,
         args=["--crop", "1920,1620,120,110", "--scale", "5", "--whole"],
         cap="ours | the game's own renderer | any differing pixel in magenta",
         note="Traffic is stencilled, not layered. The game writes a "
              "car pixel only where the destination is still index 0x91, "
              "the road surface ($1987E), so a power line or a building "
              "already covering the road erases the car underneath. That "
              "is how the original gets correct occlusion for traffic "
              "with no depth information, and it is invisible to a blit "
              "list &mdash; the list is identical either way."),

    dict(tab="Settled", key="judge-bend", anim=5,
         title="Subway bend &mdash; the notch is the original's",
         city=BAYVIEW,
         args=["--zoom", "32", "--underground",
               "--crop", "480,1350,240,180", "--scale", "4"],
         cap="Bayview, 4&times;",
         note="The notch at the bend is the original's. Rendering this "
              "rectangle through the game's own renderer produces the "
              "same notch, so it is a property of the art rather than of "
              "the reconstruction."),

    dict(tab="Settled", key="judge-corner", anim=5,
         title="Subway corner, 5&times;",
         city=BAYVIEW,
         args=["--zoom", "32", "--underground",
               "--focus", "0,127,520,470,120"],
         cap="Bayview west corner, underground, 1:1",
         note="Tile (0,127), where the underground view meets the map "
              "edge. Shapes 1319-1333 are the green tunnels, 1334-1350 "
              "the blue pipes, 1354-1358 the zone markers."),

    dict(tab="Settled", key="judge-edge", anim=5,
         title="Map edge",
         city=BAYVIEW,
         args=["--zoom", "32", "--focus", "120,127,170", "--scale", "3"],
         cap="the far corner, 3&times;",
         note="$17008 gates the whole skirt on col == 127 || row == 127, so "
              "only these two edges get the dirt cliff and the water side."),

    # ---- tab 2: Manhattan ------------------------------------------------
    dict(tab="Manhattan", key="manhattan-full", anim=4, title="Whole map",
         city=MANHATTAN, args=["--zoom", "32"],
         cap="whole map at maximum zoom, 1:1 &mdash; scroll inside the box",
         note=""),
    dict(tab="Manhattan", key="manhattan-detail", anim=5, title="Downtown, 2&times;",
         city=MANHATTAN,
         args=["--zoom", "32", "--focus", "%d,%d,330" % TALL_RC, "--scale", "2"],
         cap="2&times;", note=""),
    dict(tab="Manhattan", key="manhattan-zones", anim=5,
         title="Empty zoned lots",
         city=MANHATTAN,
         args=["--zoom", "32", "--focus", "%d,%d,120" % ZONE_RC,
               "--scale", "4"],
         cap="zoned land with nothing built on it yet",
         note="Shape 290 + zone, drawn where the tile is zoned, flat and "
              "has no building ($17192)."),

    # ---- tab 4: views ----------------------------------------------------
    dict(tab="Views", key="view-pollution", anim=4, title="Pollution",
         city=MANHATTAN, args=["--zoom", "32", "--view", "6"],
         cap="--view 6, whole map at maximum zoom, 1:1", note=""),
    dict(tab="Views", key="view-power", anim=4, title="Power",
         city=MANHATTAN, args=["--zoom", "32", "--view", "9"],
         cap="--view 9, whole map at maximum zoom, 1:1", note=""),
    dict(tab="Views", key="view-traffic", anim=4, title="Traffic",
         city=MANHATTAN, args=["--zoom", "32", "--view", "1"],
         cap="--view 1, whole map at maximum zoom, 1:1", note=""),
    dict(tab="Views", key="view-under", anim=4, title="Underground &mdash; Bayview",
         city=BAYVIEW, args=["--zoom", "32", "--underground"],
         cap="--underground, whole map at maximum zoom, 1:1", note=""),
    dict(tab="Views", key="view-under-mh", anim=4, title="Underground &mdash; Manhattan",
         city=MANHATTAN, args=["--zoom", "32", "--underground"],
         cap="--underground, whole map at maximum zoom, 1:1", note=""),
]

#  The remaining disagreements, found by scanning rather than listed by
#  hand: a hard-coded crop is stale the moment something is fixed, which is
#  exactly what happened.  This runs the pixel oracle over a slice of
#  Manhattan, clusters what still differs and emits a panel per cluster, so
#  the tab always shows what is wrong NOW and empties itself when nothing is.
def find_differences(city, crop, top=6, gap=24):
    import json as _json
    from pixel_diff import read_rgb_png
    from render_pixels import render as _render
    cx, cy, cw, ch = crop
    import tempfile
    tmp = Path(tempfile.mkdtemp(prefix="sc2kdiff-"))
    run([str(city), "--zoom", "32",
         "--crop", "%d,%d,%d,%d" % crop], out=tmp / "mine.png")
    _w, _h, mine = read_rgb_png(tmp / "mine.png")
    pal = [tuple(c) for c in
           _json.loads((ASSETS / "atlas.json").read_text())["palette"]]
    #  Driven by $16B74 over the whole map, so the sweep order is the
    #  game's own rather than one I picked -- the per-tile driver could
    #  never have exposed an ordering or a compositing bug.
    game = _render(str(city), crop, False, 0, bg=0, whole=True)
    #  0, not 255: 255 is black and the game paints it (every dark window
    #  in a tower), so using it as the unpainted marker quietly excluded
    #  those pixels from the scan.  Index 0 is the one it leaves alone.
    painted = [(x, y) for y in range(64, ch - 64)
               for x in range(64, cw - 64) if game[y][x] != 0]
    pts = [(x + cx, y + cy) for x, y in painted
           if pal[game[y][x]] != mine[y][x]]
    used, groups = set(), []
    for pt in pts:
        if pt in used:
            continue
        grp = [pt]
        used.add(pt)
        i = 0
        while i < len(grp):
            ax, ay = grp[i]
            for q in pts:
                if q not in used and abs(q[0] - ax) <= gap and abs(q[1] - ay) <= gap:
                    used.add(q)
                    grp.append(q)
            i += 1
        groups.append(grp)
    groups.sort(key=len, reverse=True)
    out = []
    for g in groups[:top]:
        xs = [q[0] for q in g]
        ys = [q[1] for q in g]
        out.append(("%d,%d,%d,%d" % (min(xs) - 30, min(ys) - 30,
                                     max(xs) - min(xs) + 60,
                                     max(ys) - min(ys) + 60), len(g)))
    return out, len(pts), len(groups), len(painted)


_DIFFS, _NPX, _NGRP, _NTOT = find_differences(MANHATTAN, (1500, 1200, 900, 700))

#  Standing three-way panels at the three places the last disagreement
#  lived, kept whether or not anything differs there now, so the page
#  shows the comparison rather than only reporting a number.  The crops
#  are the clusters the scan above returned before the traffic stencil
#  was found; if any of them regresses it shows up here first.
VERIFY = [("2018,1634,90,70", "Traffic beneath the power lines"),
          ("1762,1570,90,70", "Downtown blocks"),
          ("1570,1506,90,70", "Waterfront and rail")]
for _n, (_crop, _what) in enumerate(VERIFY, 1):
    FIGURES.append(dict(
        tab="Open", key="verify-%d" % _n, tool="pixel_sbs", anim_sbs=5,
        title="%s" % _what, city=MANHATTAN,
        args=["--crop", _crop, "--scale", "5", "--whole"],
        cap="ours | the game's own renderer | any differing pixel in magenta",
        note="" if _n > 1 else
             "The middle panel is the original game's renderer: its own "
             "sweep ($16B74), its own per-tile routine, its own blitter, "
             "run under the 68k interpreter. Nothing about tile order or "
             "compositing is assumed by the measurement. Over a 900x700 "
             "window of Manhattan that is %d pixels compared and %d "
             "differing. Traffic is the case a blit list cannot judge: "
             "cars do not go through the ordinary blitter at all. All "
             "five call "
             "sites use $19004, whose inner loops read the destination "
             "and write only where it is index 0x91, the road surface "
             "($1987E). A car is stencilled onto asphalt rather than "
             "layered over it, so a power line, a building or a bridge "
             "rail already covering the road erases the car underneath "
             "it. The blit list is identical either way; only the "
             "write differs."
             % (_NTOT, _NPX)))
for _n, (_crop, _px) in enumerate(_DIFFS, 1):
    FIGURES.append(dict(
        tab="Open", key="diff-%d" % _n, tool="pixel_sbs", anim_sbs=5,
        title="Disagreement %d &mdash; %d pixels" % (_n, _px),
        city=MANHATTAN, args=["--crop", _crop, "--scale", "5", "--whole"],
        cap="ours | the game's own renderer | differences in magenta",
        note="" if _n > 1 else
             "Found by scanning, not chosen: %d pixels in %d clusters, "
             "largest first." % (_NPX, _NGRP)))

TERRAIN = ["Volcano City", "Puntjak Putih", "Soutien Gorge", "Seven Spires",
           "St. Christopher", "Egypt Falls", "Bayview", "Hawaii"]
#  The docs pages that embed the figures, by tab.  The pages are prose and
#  are written by hand; this table is what --check walks.
PAGES = {
    "renderer-previews.rst": ["Open", "Manhattan", "Settled"],
    "renderer-views.rst": ["Views"],
    "renderer-terrain.rst": ["Terrain A"],
    "renderer-terrain-b.rst": ["Terrain B"],
}
for _ti, nm in enumerate(TERRAIN):
    FIGURES.append(dict(
        tab=("Terrain A" if _ti < 4 else "Terrain B"), key="city-" + nm.lower().replace(" ", "-").replace(".", ""),
        anim=4, title=nm, city=city(nm), args=["--zoom", "32"],
        cap="whole map at maximum zoom, 1:1 &mdash; scroll inside the box",
        note=""))



def cmd_of(f):
    """The command that makes figure f, exactly as the docs print it."""
    tool = f.get("tool")
    return ("python3 tools/gen_anim.py '%s' out.gif %s" if tool == "anim"
            else "python3 tools/pixel_sbs.py '%s' out.png %s"
            if tool == "pixel_sbs"
            else "arcology --soft assets '%s' out.png %s") % (
        f["city"].name, " ".join(f["args"]))


def _dims_of(path):
    """width x height straight out of the file header, so --reuse does
    not have to re-run the tool that made it just to learn the size."""
    b = path.read_bytes()[:26]
    if b[:3] == b"GIF":
        return "%dx%d" % (b[6] | b[7] << 8, b[8] | b[9] << 8)
    if b[:8] == b"\x89PNG\r\n\x1a\n":
        return "%dx%d" % (int.from_bytes(b[16:20], "big"),
                          int.from_bytes(b[20:24], "big"))
    return "-"


def main(reuse=False):
    if not EXE.exists():
        raise SystemExit("build it first: cmake --build build")
    if OUT.exists() and not reuse:
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True, exist_ok=True)

    total = 0
    for f in FIGURES:
        png = OUT / (f["key"] + f.get("ext", ".png"))
        if reuse:
            for cand in (OUT / (f["key"] + ".gif"), png):
                if cand.exists():
                    png = cand
                    break
            #  A figure whose image is missing (a new or reworked panel)
            #  still gets rendered; --reuse only skips what is already made.
            if png.exists():
                f["png"], f["dims"] = png, _dims_of(png)
                f["cmd"] = cmd_of(f)
                total += png.stat().st_size
                continue
        if f.get("anim"):
            #  The same figure, animated: gen_anim renders one frame per
            #  palette phase using exactly the arguments the still used.
            png = OUT / (f["key"] + ".gif")
            cmd = ([sys.executable, str(HERE / "gen_anim.py"),
                    str(f["city"]), str(png), "--frames", str(f["anim"]),
                    "--sc2k"] + [a for a in f["args"] if a != "--zoom"
                                 and a != "32"])
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0 or not png.exists():
                raise SystemExit("FAILED: %s\n%s" % (" ".join(cmd), r.stderr))
            out = "image %s\n" % r.stdout.split(",")[1].strip()
        elif f.get("tool") == "anim":
            #  The animation is a palette rotation, so one render feeds
            #  every frame and the GIF carries a colour table per frame.
            cmd = [sys.executable, str(HERE / "gen_anim.py"),
                   str(f["city"]), str(png)] + f["args"]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0 or not png.exists():
                raise SystemExit("FAILED: %s\n%s" % (" ".join(cmd), r.stderr))
            out = "image %s\n" % r.stdout.split(",")[1].strip()
        elif f.get("tool") == "pixel_sbs":
            #  A three-panel comparison against the original renderer,
            #  which needs the emulator rather than arcology --soft.
            if f.get("anim_sbs"):
                png = OUT / (f["key"] + ".gif")
            cmd = ([sys.executable, str(HERE / "pixel_sbs.py"),
                    str(f["city"]), str(png)] + f["args"] +
                   (["--frames", str(f["anim_sbs"])] if f.get("anim_sbs")
                    else []))
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0 or not png.exists():
                raise SystemExit("FAILED: %s\n%s" % (" ".join(cmd), r.stderr))
            out = "image %s\n" % r.stdout.strip().split()[0]
        else:
            out = run([str(f["city"])] + f["args"] + ["--indexed"], out=png)
        if not png.exists():
            raise SystemExit("no image for %s" % f["key"])
        f["png"] = png
        f["dims"] = next((l.split()[1] for l in out.splitlines()
                          if l.startswith("cropped")), None) or \
                    next((l.split()[1] for l in out.splitlines()
                          if l.startswith("image")), "-")
        f["cmd"] = cmd_of(f)
        total += png.stat().st_size
        print("  %-22s %-12s %8.1f KB" % (f["key"], f["dims"],
                                          png.stat().st_size / 1024))
    print("%d figures, %.1f MB of PNG" % (len(FIGURES), total / 1e6))

    MANIFEST.write_text(json.dumps(dict(
        built=build_fingerprint(),
        figures=[dict(key=f["key"], file=f["png"].name, cmd=f["cmd"],
                      dims=f["dims"], tab=f["tab"], title=f["title"],
                      cap=f["cap"]) for f in FIGURES]), indent=1))
    print("manifest: %s" % MANIFEST.relative_to(ROOT))
    if not check():
        raise SystemExit(1)
    return FIGURES


#  What every picture is rendered from.  Recorded in the manifest so that
#  --check can say "these pictures predate the current build" instead of
#  leaving that for a reader to discover.
BUILD_INPUTS = [EXE, ASSETS / "atlas.json", ASSETS / "tiles32.png",
                ASSETS / "tiles16.png", ASSETS / "tiles8.png"]


def build_fingerprint():
    import hashlib
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in BUILD_INPUTS if p.exists()}


def pictures_in(rst):
    """-> [(line, printed command, image path)] for every picture on a page,
    where the command is the literal block that directly precedes it."""
    L = rst.read_text().split("\n")
    out, cmd = [], None
    for i, l in enumerate(L):
        #  The command is a literal block, either bare ("::") or a
        #  code-block directive; either way it is the first non-blank
        #  line after the introducer.
        if l.strip() == "::" or re.match(r"\s*\.\. code-block::", l):
            j = i + 1
            while j < len(L) and (not L[j].strip() or L[j].strip().startswith(":")):
                j += 1
            cmd = L[j].strip() if j < len(L) else None
        m = re.match(r"\s*\.\. (?:thumbnail|figure):: (\S+)", l)
        if m:
            out.append((i + 1, cmd, m.group(1)))
            cmd = None
    return out


def check():
    """Every picture on the four pages is a declared figure, under its own
    key, preceded by the command that made it; every figure is on a page."""
    if not MANIFEST.exists():
        print("no manifest: render first (python3 tools/gen_previews.py)")
        return False
    manifest = json.loads(MANIFEST.read_text())
    by_file = {m["file"]: m for m in manifest["figures"]}
    seen, bad, n = {}, [], 0
    now = build_fingerprint()
    for name, digest in manifest.get("built", {}).items():
        if now.get(name) != digest:
            bad.append("%s has changed since the pictures were rendered: "
                       "run python3 tools/gen_previews.py" % name)
    for page, tabs in PAGES.items():
        rst = DOCS / page
        for line, cmd, path in pictures_in(rst):
            n += 1
            where = "%s:%d" % (page, line)
            name = path.rsplit("/", 1)[-1]
            m = by_file.get(name) if path.startswith("img/preview/") else None
            if m is None:
                bad.append("%s embeds %s, which is not a declared figure"
                           % (where, path))
                continue
            if not (OUT / name).exists():
                bad.append("%s: %s is missing from %s"
                           % (where, name, OUT.relative_to(ROOT)))
            if cmd != m["cmd"]:
                bad.append("%s prints\n      %s\n    but %s was made by\n      %s"
                           % (where, cmd, name, m["cmd"]))
            if m["tab"] not in tabs:
                bad.append("%s embeds %s from tab %r, which belongs on another page"
                           % (where, name, m["tab"]))
            seen.setdefault(name, []).append(where)
    for name, m in by_file.items():
        if name not in seen:
            bad.append("figure %s (%s) is on no page" % (m["key"], name))
        elif len(seen[name]) > 1:
            bad.append("figure %s is embedded twice: %s"
                       % (m["key"], ", ".join(seen[name])))
    stray = sorted(p.name for p in OUT.iterdir()
                   if p.name != MANIFEST.name and p.name not in by_file)
    if stray:
        bad.append("not declared figures, but in %s: %s"
                   % (OUT.relative_to(ROOT), ", ".join(stray)))
    for b in bad:
        print("  CHECK FAILED: " + b)
    print("check: %d pictures on %d pages against %d figures: %s"
          % (n, len(PAGES), len(by_file), "FAILED" if bad else "ok"))
    return not bad


if __name__ == "__main__":
    if "--check" in sys.argv:
        sys.exit(0 if check() else 1)
    main(reuse="--reuse" in sys.argv)
