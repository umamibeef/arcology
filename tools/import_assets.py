#!/usr/bin/env python3
"""import_assets.py -- build the asset pack from your own copy of the game.

    python3 tools/import_assets.py "/path/to/SimCity 2000 Collection"

Arcology ships no art, sound or interface graphics.  They are read out of
a real SimCity 2000 installation every time, which is why this script
exists and why `assets/` is not something you can download.

It runs the three extractors in turn:

    sc2kpack.py extract   SHAP  ->  tiles8/16/32.png + JSON      the map art
    pict.py --atlas       PICT  ->  ui.png + ui.json            the interface
    snd.py                snd   ->  sounds/*.wav                the effects

All three read the game's RESOURCE FORK.  On macOS that fork is still
attached to the file and this script finds it; on any other system you
need the fork as a separate file (see --rsrc).
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

#  The application's name has varied across releases, and the ® is real.
CANDIDATES = (
    "SimCity 2000® 1.2",
    "SimCity 2000®",
    "SimCity 2000 1.2",
    "SimCity 2000",
)


def find_fork(game_dir):
    """The resource fork of the application inside `game_dir`."""
    d = Path(game_dir)
    #  A file given directly -- either the application or an already
    #  extracted fork.
    if d.is_file():
        return d
    names = [n for n in CANDIDATES if (d / n).exists()]
    #  Fall back to anything that looks like the application, so a
    #  renamed copy still works.
    if not names:
        names = [p.name for p in d.iterdir()
                 if p.is_file() and p.name.lower().startswith("simcity")]
    for n in names:
        fork = d / n / "..namedfork" / "rsrc"
        if fork.exists() and fork.stat().st_size > 0:
            return fork
        #  Not macOS, or the fork was already split out beside it.
        plain = d / n
        if plain.is_file() and plain.stat().st_size > 1_000_000:
            return plain
    return None


def run(step, args):
    print("== %s" % step)
    r = subprocess.run([sys.executable] + args)
    if r.returncode != 0:
        raise SystemExit("%s failed (exit %d)" % (step, r.returncode))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("game", nargs="?",
                    help="the SimCity 2000 folder (or the application itself)")
    ap.add_argument("--rsrc", help="an already extracted resource fork, "
                                   "for systems that do not keep one")
    ap.add_argument("--out", default=str(HERE.parent / "assets"),
                    help="where to write the pack (default: assets/)")
    a = ap.parse_args()

    if a.rsrc:
        fork = Path(a.rsrc)
    elif a.game:
        fork = find_fork(a.game)
    else:
        ap.error("give the game folder, or --rsrc")
    if fork is None or not fork.exists():
        raise SystemExit(
            "no resource fork found.\n"
            "  On macOS, point at the folder holding the application.\n"
            "  Elsewhere, extract the fork first and pass it with --rsrc.")

    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    print("reading %s (%d bytes)" % (fork, fork.stat().st_size))
    print("writing  %s\n" % out)

    run("map art  (SHAP)", [str(HERE / "sc2kpack.py"), "extract",
                            "--rsrc", str(fork), "--out", str(out)])
    run("interface (PICT)", [str(HERE / "pict.py"), "--atlas",
                             str(fork), str(out)])
    run("sound     (snd )", [str(HERE / "snd.py"), str(fork),
                             str(out / "sounds")])
    run("music     (MIDI, INST, SONG)", [str(HERE / "music.py"), str(fork),
                             str(out / "music")])

    print("\nassets are in %s" % out)
    missing = [n for n in ("tiles32.png", "ui.png", "atlas.json")
               if not (out / n).exists()]
    if missing:
        raise SystemExit("but these are missing: %s" % ", ".join(missing))
    print("run the game with:  ./build/arcology")


if __name__ == "__main__":
    main()
