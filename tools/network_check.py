#!/usr/bin/env python3
"""The network is the script's, and it is a true network of the map.

GOAL 4 is that C offers cells and neighbours and the SCRIPT produces the
segments.  Three things have to hold at once.

  only copy   taking arc.rules.network and arc.rules.bands away takes the
              whole network away.  If any walk were left in C the lines would survive,
              and the two copies would drift the moment either changed.
  the script's  a rule of the script's own making produces a different
              network, and the build still checks out.
  a true network  every run the shipped rule hands over is checked
              against the planes it was given: consecutive cells are
              neighbours that link to each other, no edge is claimed
              twice, and every cell whose art claims a link is covered.

    python3 tools/network_check.py [--binary PATH] [--city NAME]
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

#  The discovery, taken away -- both of them, the lines' and the slab's.
NONE = ("arc.rules.network = function (o) return true end\n"
        "arc.rules.bands = function (o) return true end\n")

#  And the slab's runs, checked against the readings they were taken off:
#  every cell of a band is the primary of its own pair or a curve block,
#  and no cell is claimed by two bands.
BANDS = r"""
local shipped = arc.rules.bands
arc.rules.bands = function (o)
    local d = o:info()
    local n = d.size
    local xbld = arc.city.plane("xbld")
    local prim, axis, block = arc.band_readings(xbld, n, arc.band_tiles)
    local bad, bands, cells, claimed = {}, 0, 0, {}
    local function say(f, ...) bad[#bad + 1] = string.format(f, ...) end
    local proxy = {
        info = function () return d end,
        band = function (_, e, k, cell, ew, sign)
            bands = bands + 1
            cells = cells + k
            if k < 1 then say("band %d is empty", bands) end
            if sign ~= 1 and sign ~= -1 then say("band %d runs neither way", bands) end
            for i = 0, k - 1 do
                local at = e[i].cell
                if at < 0 or at >= n * n then
                    say("band %d leaves the map at %d", bands, at)
                elseif e[i].block then
                    if block[at] ~= at then say("band %d turns through a cell that is no block", bands) end
                elseif prim[at] ~= at then
                    say("band %d stands on a cell that is not its pair's primary", bands)
                elseif axis[at] ~= e[i].ew then
                    say("band %d has a cell lying the other way", bands)
                end
                if claimed[at] then say("band %d claims cell %d, already claimed", bands, at) end
                claimed[at] = true
            end
            o:band(e, k, cell, ew, sign)
        end,
    }
    local ok = shipped(proxy)
    arc.dump(string.format("bands %d %d %d", bands, cells, #bad))
    for _, b in ipairs(bad) do arc.dump("audit! " .. b) end
    return ok
end
"""

#  The runs the shipped rule hands over, checked against the planes it
#  was given.  The rule is called through a table of the same methods, so
#  what is checked is exactly what the pipeline is told.
AUDIT = r"""
local shipped = arc.rules.network
local DC = {[0] = 0, 1, 0, -1}
local DR = {[0] = -1, 0, 1, 0}
arc.rules.network = function (o)
    local d = o:info()
    local n = d.size
    local links, art = o:plane()
    local node = {}
    local bad, runs, isles, juncs, covered, claimed = {}, 0, 0, 0, {}, {}
    local function say(f, ...) bad[#bad + 1] = string.format(f, ...) end
    local proxy = {
        info = function () return d end,
        plane = function () return links, art end,
        nodes = function (_, t)
            for i = 0, n * n - 1 do node[i] = t[i] end
            o:nodes(t)
        end,
        junction = function (_, cell)
            juncs = juncs + 1
            covered[cell] = true
            if node[cell] ~= 2 then say("junction %d is not a node", cell) end
            o:junction(cell)
        end,
        island = function (_, cell, edge)
            isles = isles + 1
            covered[cell] = true
            if art[cell] == 0 then say("island %d has no art links", cell) end
            o:island(cell, edge)
        end,
        segment = function (_, cells, k, stop, exit)
            runs = runs + 1
            if k < 1 then say("run %d is empty", runs) end
            for i = 0, k - 1 do
                local at = cells[i]
                covered[at] = true
                if at < 0 or at >= n * n then say("run %d leaves the map at %d", runs, at) end
                if i > 0 then
                    local a, b = cells[i - 1], at
                    local ac, ar, bc, br = a % n, a // n, b % n, b // n
                    local e = bc > ac and 1 or bc < ac and 3 or br > ar and 2 or 0
                    if math.abs(ac - bc) + math.abs(ar - br) ~= 1 then
                        say("run %d steps from %d,%d to %d,%d", runs, ac, ar, bc, br)
                    elseif links[a] & (1 << e) == 0 or links[b] & (1 << ((e + 2) % 4)) == 0 then
                        say("run %d crosses an edge neither cell returns, %d,%d to %d,%d",
                            runs, ac, ar, bc, br)
                    end
                    if claimed[a * 4 + e] then
                        say("run %d claims %d,%d edge %d, already claimed", runs, ac, ar, e)
                    end
                    claimed[a * 4 + e] = true
                    claimed[b * 4 + (e + 2) % 4] = true
                end
            end
            if stop == "node" and k > 1 and node[cells[k - 1]] == 0
               and (links[cells[k - 1]] & 15) ~= 0 then
                local m, c = links[cells[k - 1]], 0
                for e = 0, 3 do if m & (1 << e) ~= 0 then c = c + 1 end end
                if c == 2 then say("run %d stopped at a cell that is not a node", runs) end
            end
            if stop ~= "node" and stop ~= "edge" and stop ~= "cut"
               and stop ~= "stuck" and stop ~= "loop" then
                say("run %d stopped for no reason the pipeline knows: %s", runs, tostring(stop))
            end
            o:segment(cells, k, stop, exit)
        end,
    }
    local ok = shipped(proxy)
    --  Every cell whose own art claims a link belongs to some run or is
    --  a lone piece: a network that misses one draws a line with a hole.
    local missed = 0
    for i = 0, n * n - 1 do
        if art[i] ~= 0 and not covered[i] then missed = missed + 1 end
    end
    if missed > 0 then say("%d cells with art links are in no run", missed) end
    arc.dump(string.format("audit %s %d %d %d %d", d.family, runs, isles, juncs, #bad))
    for _, b in ipairs(bad) do arc.dump("audit! " .. b) end
    return ok
end
"""


#  A network of the script's OWN making: the same walk, but only the runs
#  whose first cell sits on an even column.  Half a city's lines, and
#  nothing in the pipeline can put the other half back.
HALF = r"""
local DC = {[0] = 0, 1, 0, -1}
local DR = {[0] = -1, 0, 1, 0}
local COUNT, ONLY = {}, {}
for m = 0, 15 do
    local k, last = 0, 0
    for e = 0, 3 do if m & (1 << e) ~= 0 then k = k + 1; last = e end end
    COUNT[m], ONLY[m] = k, last
end
arc.rules.network = function (o)
    local d = o:info()
    local n = d.size
    local links, art = o:plane()
    local node = {}
    for i = 0, n * n - 1 do
        node[i] = COUNT[art[i]] >= 3 and 2 or COUNT[links[i]] == 1 and 1 or 0
    end
    o:nodes(node)
    local cells, seen = {}, {}
    local function keep(col, row, e)
        if seen[(row * n + col) * 4 + e] then return end
        local cc, cr, ee, k = col, row, e, 1
        seen[(row * n + col) * 4 + e] = true
        cells[0] = row * n + col
        while true do
            cc, cr = cc + DC[ee], cr + DR[ee]
            local back = (ee + 2) % 4
            if cc < 0 or cr < 0 or cc >= n or cr >= n then
                if col % 2 == 0 then o:segment(cells, k, "edge", ee) end
                return
            end
            local at, lk = cr * n + cc, links[cr * n + cc]
            if lk & (1 << back) == 0 then return end
            seen[at * 4 + back] = true
            if node[at] ~= 0 or COUNT[lk] ~= 2 then
                cells[k] = at
                if col % 2 == 0 then o:segment(cells, k + 1, "node", -1) end
                return
            end
            cells[k] = at; k = k + 1
            ee = ONLY[lk & ~(1 << back)]
            seen[at * 4 + ee] = true
            if cc == col and cr == row then
                if col % 2 == 0 then o:segment(cells, k, "loop", ee) end
                return
            end
        end
    end
    for row = 0, n - 1 do for col = 0, n - 1 do
        local at = row * n + col
        if art[at] ~= 0 and node[at] ~= 0 then
            for e = 0, 3 do if links[at] & (1 << e) ~= 0 then keep(col, row, e) end end
        end
    end end
    return true
end
"""


def run(binary, city, script=None, extra=()):
    path = os.path.join(ROOT, "cities", city + ".sc2")
    name = None
    if script:
        with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False,
                                         dir=os.environ.get("TMPDIR")) as f:
            f.write(script)
            name = f.name
    cmd = [binary, path, "--mute", "--mesh-check"] + (["--lua", name] if name else [])
    cmd += list(extra)
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    finally:
        if name:
            os.unlink(name)
    return p.returncode, p.stdout


def tris(out):
    m = re.search(r"^mesh check: (\d+) triangles", out, re.M)
    return int(m.group(1)) if m else -1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(ROOT, "build", "arcology"))
    ap.add_argument("--city", default="atlanta")
    a = ap.parse_args()
    if not os.path.exists(a.binary) or not os.path.exists(
            os.path.join(ROOT, "cities", a.city + ".sc2")):
        print("network: no binary or no city")
        return 0
    bad = []

    full_rc, full = run(a.binary, a.city)
    if full_rc != 0:
        print("network: the city does not check out at all")
        return 1
    #  With the discovery gone the mesh check REFUSES the build, because
    #  every line piece on the map is left without geometry.  That
    #  refusal is the proof, so what is read here is the count and the
    #  complaint, not the exit code.
    none_rc, none = run(a.binary, a.city, NONE)
    if tris(none) >= tris(full):
        bad.append("clearing arc.rules.network left %d triangles against %d: "
                   "something behind the script is still walking the map"
                   % (tris(none), tris(full)))
    missing = re.search(r"^(\w+) pieces without geometry: (\d+)", none, re.M)
    if not missing or int(missing.group(2)) == 0:
        bad.append("clearing arc.rules.network left every piece drawn: "
                   "the walk is not the script's")

    #  And a network the script wrote itself: neither the full one nor
    #  none of it, and the mesh still sound.
    #  It leaves the other half of the lines with no geometry, which the
    #  piece check refuses -- as it should -- so what is read is the
    #  structure: no free edge, and no triangle belonging to no shape.
    half_rc, half = run(a.binary, a.city, HALF)
    if not (tris(none) < tris(half) < tris(full)):
        bad.append("a network of the script's own making drew %d triangles, "
                   "between %d and %d expected" % (tris(half), tris(none), tris(full)))
    if not re.search(r"^mesh check: \d+ triangles, 0 free edges, 0 free vertical spans",
                     half, re.M):
        bad.append("a network of the script's own making left the mesh unsound")
    if not re.search(r"^shapes\s+\d+ shapes; 0 triangles claimed by none", half, re.M):
        bad.append("a network of the script's own making left triangles in no shape")

    #  And the slab's own bands, audited the same way.
    band_rc, band = run(a.binary, a.city, BANDS)
    if tris(band) != tris(full):
        bad.append("auditing the bands changed the build: %d triangles against %d"
                   % (tris(band), tris(full)))
    nbands = None
    for line in band.splitlines():
        if line.startswith("bands "):
            f = line.split()
            nbands = (int(f[1]), int(f[2]))
        elif line.startswith("audit! "):
            bad.append(line[7:])
    if nbands is None:
        bad.append("the band discovery rule was never asked")

    audit_rc, audit = run(a.binary, a.city, AUDIT)
    if audit_rc != 0:
        bad.append("the audited build does not check out")
    if tris(audit) != tris(full):
        bad.append("auditing the runs changed the build: %d triangles against %d"
                   % (tris(audit), tris(full)))
    seen = []
    for line in audit.splitlines():
        if line.startswith("audit "):
            f = line.split()
            seen.append((f[1], int(f[2]), int(f[3]), int(f[4]), int(f[5])))
        elif line.startswith("audit! "):
            bad.append(line[7:])
    if not seen:
        bad.append("the discovery rule was never asked")
    for family, runs, isles, juncs, nbad in seen:
        if runs == 0 and isles == 0 and juncs == 0:
            bad.append("%s: the script produced no network at all" % family)

    for line in bad:
        print("network:", line)
    if bad:
        return 1
    print("the network is the script's, and it holds:")
    for family, runs, isles, juncs, _ in dict.fromkeys(seen):
        print("  %-6s %5d runs, %4d junctions, %3d lone pieces -- every step over "
              "an edge both cells return, no edge claimed twice, no cell left out"
              % (family, runs, juncs, isles))
    if nbands and nbands[0]:
        print("  band  %5d bands over %d cells -- every cell the primary of its own "
              "pair or a curve block, none claimed twice" % nbands)
    print("  %d triangles with it, %d without, and %s pieces then left with no "
          "geometry: there is no walk in C behind it"
          % (tris(full), tris(none), missing.group(2)))
    print("  a network the script wrote itself draws %d, and the mesh holds"
          % tris(half))
    return 0


if __name__ == "__main__":
    sys.exit(main())
