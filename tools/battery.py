#!/usr/bin/env python3
"""battery.py -- the renderer's exact check, against one reference file.

Everything the renderer can be held to is measured here and compared
with tools/battery_ref.json.  A number that differs from the file fails,
whichever way it moved: an improvement that is not recorded is a
regression waiting to pass.  `--update` rewrites the file and prints what
moved, so the diff of the file is the record of every accepted change.

  views     eight fixed shots at a fixed framebuffer, hashed.  Seven are
            Atlanta (the default view, three plan views, two with the
            traffic advanced so the gates and signals have moved, one
            interchange) and one is Maltron's lone interchange.  None
            frames water: the water shader moves every frame.
  mesh      one `--mesh-check` run a city over eight cities: triangles,
            shapes, free edges, samples clipped under the terrain, faces
            claimed by no shape, face pairs that collide, line works
            buried under their own margin, junction spurs, mouths
            reaching no margin, lane ends with nowhere to go, segments
            without geometry and the fit's hard corners.
  corners   the hard corners and the segments without geometry over
            every city in cities/.
  traffic   five cities advanced 26 seconds and hashed: every car, gate
            and train, and every vertex the frame uploads.  A headless
            shot does not draw the movers, so the views say nothing
            about them.

    python3 tools/battery.py                 # everything, against the reference
    python3 tools/battery.py --gate          # the views and Atlanta's mesh: a minute
    python3 tools/battery.py --only mesh     # one section
    python3 tools/battery.py --update        # record what the tree prints now
    python3 tools/battery.py --binary PATH   # another build

Two traps the script guards against itself.  A crashed run must not
leave an old PNG to be hashed, so every shot goes to a fresh file that is
removed first.  And the hashes are per FRAMEBUFFER: a display density
change makes `--win 1280x800` produce twice the pixels with nothing
moved, so the size the shot came out at is compared too.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
REF = os.path.join(HERE, "battery_ref.json")
CITIES = os.path.join(ROOT, "cities")

WIN = "1280x800"
VIEWS = {
    "v1": ("atlanta", []),
    "v2": ("atlanta", ["--plan", "--centre", "70,16", "--zoomf", "40"]),
    "v3": ("atlanta", ["--plan", "--centre", "68,54", "--zoomf", "26"]),
    "v4": ("atlanta", ["--outline", "--plan", "--centre", "70,16", "--zoomf", "16"]),
    "v5": ("atlanta", ["--centre", "63,50", "--zoomf", "26", "--traffic-t", "26"]),
    "v6": ("atlanta", ["--centre", "32,9", "--zoomf", "34", "--traffic-t", "26"]),
    "v7": ("atlanta", ["--centre", "104,63", "--zoomf", "6"]),
    "v8": ("maltron", ["--centre", "66,28", "--zoomf", "5"]),
}
MESH_CITIES = ["atlanta", "toronto", "tokyo", "flint", "babar", "maltron", "chicago", "manhattan"]
TRAFFIC_CITIES = ["atlanta", "toronto", "flint", "tokyo", "chicago"]
TRAFFIC_SECONDS = "26"

#  What one `--mesh-check` run says, line by line.
MESH_LINES = {
    "triangles":  re.compile(r"^mesh check: (\d+) triangles, (\d+) free edges, (\d+) free vertical spans", re.M),
    "clip":       re.compile(r"^line clip  (\d+) samples under the terrain on (\d+) tiles", re.M),
    "shapes":     re.compile(r"^shapes  (\d+) shapes; (\d+) triangles claimed by none", re.M),
    "collide":    re.compile(r"^collide  (\d+) face pairs pass through one another on (\d+) tiles", re.M),
    "nogeom":     re.compile(r"^line pieces without geometry: (\d+)", re.M),
    "buried":     re.compile(r"^overlap  .*?(\d+) of line works buried under a margin", re.M),
    "spurs":      re.compile(r"^outlines  (\d+) junctions; (\d+) spurs, (\d+) self-meets", re.M),
    "nomargin":   re.compile(r"^meets  .*?(\d+) reaching no margin", re.M),
    "nowhere":    re.compile(r"^lanes  .*?(\d+) ends with nowhere to go, (\d+) starts with nothing arriving", re.M),
}
CORNERS = re.compile(r"^tangent fit  (line|thread|band): .*?(\d+) corners", re.M)


#  A signal is a crash and an error the log reports is a build that did
#  not happen.  Either is a fault of its own, whatever the reference
#  says: the battery measures a world that was built.
CRASH = re.compile(r"^\[[^\]]*\]\[[^\]]*ERROR[^\]]*\]\[[^\]]*\] (.*)$", re.M)


def run(binary, args, timeout=300):
    """The exit code and everything the run printed.  A run that never
    ends answers as a crash would: a fault, not a traceback that takes
    the pool down."""
    try:
        p = subprocess.run([binary] + args, capture_output=True, text=True, errors="replace",
                           cwd=ROOT, timeout=timeout)
    except subprocess.TimeoutExpired:
        return 124, "[timeout][ERROR][battery] no answer in %d seconds" % timeout
    return p.returncode, p.stdout + p.stderr


def fault(rc, text):
    """What went wrong, or None where the world was built."""
    if rc == 124:
        return "timed out"
    if rc < 0 or rc > 1:
        return "exit %d: crashed" % rc
    m = CRASH.search(text)
    if m and ("the build FAILED" in text or "abandoned" in text
              or "runs away" in text or "attempt to" in text):
        return re.sub(r"\x1b\[[0-9;]*m", "", m.group(1))[:160]
    return None


def city_path(city):
    return os.path.join(CITIES, city + ".sc2")


#  ---- the sections -------------------------------------------------------


def view(binary, name):
    city, extra = VIEWS[name]
    fd, png = tempfile.mkstemp(prefix="battery-%s-" % name, suffix=".png")
    os.close(fd)
    os.remove(png)
    rc, text = run(binary, [city_path(city), "--mute", "--win", WIN] + extra + ["--shot", png])
    f = fault(rc, text)
    if f or not os.path.exists(png):
        return {"error": f or "no shot written"}
    data = open(png, "rb").read()
    os.remove(png)
    w, h = struct.unpack(">II", data[16:24])
    return {"hash": hashlib.sha1(data).hexdigest()[:10], "size": "%dx%d" % (w, h)}


def mesh(binary, city):
    rc, text = run(binary, [city_path(city), "--mute", "--mesh-check", "--run", "1"])
    out = {}
    m = MESH_LINES["triangles"].search(text)
    f = fault(rc, text)
    if f or not m:
        return {"error": f or "no mesh check line"}
    out["triangles"], out["free_edges"], out["free_spans"] = (int(x) for x in m.groups())
    m = MESH_LINES["clip"].search(text)
    out["clip_samples"], out["clip_tiles"] = (int(x) for x in m.groups()) if m else (None, None)
    m = MESH_LINES["shapes"].search(text)
    out["shapes"], out["unclaimed"] = (int(x) for x in m.groups()) if m else (None, None)
    m = MESH_LINES["collide"].search(text)
    out["collide_pairs"], out["collide_tiles"] = (int(x) for x in m.groups()) if m else (None, None)
    m = MESH_LINES["nogeom"].search(text)
    out["nogeom"] = int(m.group(1)) if m else None
    m = MESH_LINES["buried"].search(text)
    out["buried"] = int(m.group(1)) if m else None
    m = MESH_LINES["spurs"].search(text)
    out["junction_spurs"], out["self_meets"] = (int(m.group(2)), int(m.group(3))) if m else (None, None)
    m = MESH_LINES["nomargin"].search(text)
    out["nomargin"] = int(m.group(1)) if m else None
    m = MESH_LINES["nowhere"].search(text)
    out["nowhere"], out["nothing_arriving"] = (int(x) for x in m.groups()) if m else (None, None)
    out["corners"] = {fam: int(n) for fam, n in CORNERS.findall(text)}
    return out


def corners(binary, path):
    rc, text = run(binary, [path, "--mute", "--mesh-check", "--dump-to", "-"])
    f = fault(rc, text)
    if f or not MESH_LINES["triangles"].search(text):
        return {"error": f or "no mesh check line"}
    m = MESH_LINES["nogeom"].search(text)
    return {"corners": {fam: int(n) for fam, n in CORNERS.findall(text)},
            "nogeom": int(m.group(1)) if m else None}


def traffic(binary, city):
    fd, png = tempfile.mkstemp(prefix="battery-traffic-", suffix=".png")
    os.close(fd)
    rc, text = run(binary, [city_path(city), "--mute", "--win", WIN, "--centre", "63,50",
                            "--zoomf", "26", "--traffic-t", TRAFFIC_SECONDS, "--shot", png])
    if os.path.exists(png):
        os.remove(png)
    m = re.search(r"^traffic (\d+) cars, (\d+) trains, (\d+) vertices, digest ([0-9a-f]+)", text, re.M)
    f = fault(rc, text)
    if f or not m:
        return {"error": f or "the world said nothing about its traffic"}
    return {"cars": int(m.group(1)), "trains": int(m.group(2)),
            "vertices": int(m.group(3)), "digest": m.group(4)}


def measure(binary, sections, gate):
    """Every section asked for, run through one pool."""
    jobs = []
    if "views" in sections:
        for name in VIEWS:
            jobs.append(("views", name, lambda n=name: view(binary, n)))
    if "mesh" in sections:
        for city in (["atlanta"] if gate else MESH_CITIES):
            jobs.append(("mesh", city, lambda c=city: mesh(binary, c)))
    if "corners" in sections:
        for name in sorted(os.listdir(CITIES)):
            if name.lower().endswith(".sc2"):
                p = os.path.join(CITIES, name)
                jobs.append(("corners", name, lambda p=p: corners(binary, p)))
    if "traffic" in sections:
        for city in TRAFFIC_CITIES:
            jobs.append(("traffic", city, lambda c=city: traffic(binary, c)))
    got = {s: {} for s in sections}
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for (section, key, _), result in zip(jobs, pool.map(lambda j: j[2](), jobs)):
            got[section][key] = result
    return got


#  ---- the comparison -----------------------------------------------------


def flatten(d, prefix=""):
    out = {}
    for k, v in d.items():
        key = prefix + "/" + str(k) if prefix else str(k)
        if isinstance(v, dict):
            out.update(flatten(v, key))
        else:
            out[key] = v
    return out


def compare(want, got):
    """Every leaf that differs, as (key, want, got), over what was
    MEASURED: a city the reference holds and this run did not visit (the
    gate visits one) is not a difference, a value a visited city stopped
    printing is."""
    diffs = []
    for section in got:
        if section not in want:
            continue
        for item in got[section]:
            w, g = flatten(want[section].get(item, {})), flatten(got[section][item])
            for key in sorted(set(w) | set(g)):
                if w.get(key) != g.get(key):
                    diffs.append((section + "/" + item + "/" + key, w.get(key), g.get(key)))
    return diffs


def say(got):
    if "views" in got:
        print("views    " + " ".join(got["views"][v].get("hash", "ERROR") for v in VIEWS)
              + "  at " + got["views"]["v1"].get("size", "?"))
    if "mesh" in got:
        for city, m in got["mesh"].items():
            if "error" in m:
                print("mesh     %-9s %s" % (city, m["error"]))
                continue
            print("mesh     %-9s %8d triangles %6d shapes  clip %d/%d  unclaimed %d  collide %d  "
                  "buried %d  nogeom %d  spurs %d  nomargin %d  nowhere %d  corners %s"
                  % (city, m["triangles"], m["shapes"], m["clip_samples"], m["clip_tiles"],
                     m["unclaimed"], m["collide_pairs"], m["buried"], m["nogeom"],
                     m["junction_spurs"], m["nomargin"], m["nowhere"],
                     sum(m["corners"].values())))
    if "corners" in got:
        c = got["corners"]
        tot = sum(sum(v.get("corners", {}).values()) for v in c.values())
        ng = sum(v.get("nogeom") or 0 for v in c.values())
        above = sorted(k for k, v in c.items()
                       if sum(v.get("corners", {}).values()) or (v.get("nogeom") or 0))
        print("corners  %d cities; %d hard corners, %d segments without geometry; %d cities above zero"
              % (len(c), tot, ng, len(above)))
        for k in above:
            v = c[k]
            print("         %s: %s%s" % (k, ", ".join("%d %s" % (n, f) for f, n in v["corners"].items() if n),
                                         (", %d without geometry" % v["nogeom"]) if v.get("nogeom") else ""))
    if "traffic" in got:
        for city, t in got["traffic"].items():
            if "error" in t:
                print("traffic  %-9s %s" % (city, t["error"]))
            else:
                print("traffic  %-9s %5d cars %2d trains %6d vertices  %s"
                      % (city, t["cars"], t["trains"], t["vertices"], t["digest"]))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--binary", default=os.path.join(ROOT, "build", "arcology"))
    ap.add_argument("--only", default=None, help="comma-separated: views,mesh,corners,traffic")
    ap.add_argument("--gate", action="store_true", help="the views and Atlanta's mesh only")
    ap.add_argument("--update", action="store_true", help="record what the tree prints now")
    ap.add_argument("--note", default=None, help="with --update: why the reference moved")
    ap.add_argument("--json", default=None, help="also write the measurements here")
    a = ap.parse_args()
    if not os.path.exists(a.binary):
        print("battery: no binary at %s" % a.binary)
        return 2
    sections = ["views", "mesh"] if a.gate else ["views", "mesh", "corners", "traffic"]
    if a.only:
        sections = [s for s in a.only.split(",") if s]
    got = measure(a.binary, sections, a.gate)
    say(got)
    if a.json:
        json.dump(got, open(a.json, "w"), indent=1, sort_keys=True)
    faults = [(section, key, v["error"]) for section in got for key, v in got[section].items()
              if isinstance(v, dict) and "error" in v]
    for section, key, why in faults:
        print("fault    %-9s %-16s %s" % (section, key, why))
    ref = json.load(open(REF)) if os.path.exists(REF) else {}
    #  A faulted city is recorded as its fault, so a fix shows as a change.
    #  It fails all the same: a world that was not built has no numbers.
    diffs = compare(ref, got)
    if a.update:
        for key, w, g in diffs:
            print("moved    %-40s %s -> %s" % (key, w, g))
        for section in got:  # a gate run rewrites Atlanta and leaves the other cities
            ref.setdefault(section, {}).update(got[section])
        if a.note:
            ref["note"] = a.note
        json.dump(ref, open(REF, "w"), indent=1, sort_keys=True)
        open(REF, "a").write("\n")
        print("battery: reference written for %s (%d values moved)"
              % (", ".join(s for s in sections), len(diffs)))
        return 0
    if not all(s in ref for s in sections):
        print("battery: no reference for %s; run with --update"
              % ", ".join(s for s in sections if s not in ref))
        return 1
    if diffs:
        for key, w, g in diffs:
            print("differs  %-40s reference %s, tree %s" % (key, w, g))
        print("battery: %d value%s differ from tools/battery_ref.json; look, then --update to accept"
              % (len(diffs), "" if len(diffs) == 1 else "s"))
    if faults:
        print("battery: %d world%s not built (see the fault lines); a fault is never accepted"
              % (len(faults), "" if len(faults) == 1 else "s"))
    if diffs or faults:
        return 1
    print("battery: %s match the reference" % ", ".join(sections))
    return 0


if __name__ == "__main__":
    sys.exit(main())
