#!/usr/bin/env python3
"""The test cities: every combination of ways the network has to read, one
city a section and one block a case, packed close.

    python3 tools/test_city.py [--list] [--out cities/test.arco]

One world, cities/test.arco, with a city a section: roads, railways,
highways (with their ramps and interchanges), power, crossings (where two
kinds meet), and heights (each kind over hills, valleys, terraces and
cliffs).  The cities lie side by side on the one map, packed by what each
holds, so their windows overlap where nothing of theirs does.  A city's
window is 128 by 128 tiles, so a section that grows past one window goes
on in a second city.  The world is composed by `arcology --compose`.

Each block is drawn as text.  The pieces are FITTED to their neighbors the
way the game's own tools fit them (src/sim/sim_road.c, src/sim/sim_rail.c,
src/sim/sim_power.c), so a block holds what a player could build, unless
it says otherwise: a pin sets a cell to a piece of its own, to hold the
art a player can leave behind (a T whose arm points at nothing, a lone
piece).

The characters:

    =  line          #  thread         *  power
    |  band, north-south slab           -  band, east-west slab
    +  slab over a line                 %  slab over a thread
    &  slab over power                  C  curve block (2x2)
    I  interchange (2x2)                R  ramp, beside a slab, by a line
    x  line over a thread               ^  line under power
    r  thread under power               .  open land
    b  a house                          B  a 2x2 block over two levels

A block's GROUND is a second picture of the same size, one digit a cell:
how many levels the cell stands over the city's floor.  A cell beside a
higher one slopes up to it, and a step of two or more is a cliff.  A way
climbs only along its own length, as the game lets it, and the generator
says so when a block asks for one across a slope.

The ids are the ones the original's fitters and placers write
(src/sim/net_cell.c).  `--list` prints every block's name and its first cell.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import sc2  # noqa: E402

N = 128
#  The open land between two blocks, and between a block and the map edge.
GAP, EDGE = 2, 2

#  A5-0x42C2, the piece for a join mask: 8 row-1, 2 row+1, 1 col-1, 4 col+1.
NET_PIECE = [0, 0, 1, 6, 0, 0, 7, 11, 1, 9, 1, 10, 8, 13, 12, 14]
#  The slope piece for terrain 1 to 4, and which terrain codes a way
#  climbs on (NET_SLOPE_PIECE, NET_SLOPED).
SLOPE_PIECE = {1: 2, 2: 3, 3: 4, 4: 5}
LINE, THREAD, POWER = 0x1D, 0x2C, 0x0E
#  A one-tile house, and a 2x2 warehouse that stands over two levels.
HOUSE, BLOCK_TALL = 0x70, 0xA8
#  The highway pieces that are one piece over a 2x2: the four curve blocks
#  and the interchange.
HIGHWAY_2X2 = {0x65, 0x66, 0x67, 0x68, 0x69}
#  The highway pieces of one tile each: the slabs, and a slab over a road,
#  a railway or power.
HIGHWAY_1X1 = set(range(0x49, 0x51))
#  Which corners a terrain code lifts, bit 0 the cell's own corner, bit 1
#  the one a row on, bit 2 a row and a column on, bit 3 a column on
#  (src/render/mesh/ground_bytes.c, corner_lifts), and the code for a mask.
LIFTS = [0, 9, 3, 6, 12, 11, 7, 14, 13, 1, 2, 4, 8, 5]
CODE = {m: k for k, m in enumerate(LIFTS)}


class Block:
    def __init__(self, name, rows, pins=None, ground=None, streets=None):
        self.name, self.rows, self.pins, self.ground = name, rows, pins or [], ground or []
        self.streets = streets or []


#  THE STREET LAYER a block asks for: runs of street, each a straight line
#  of cells from its first to its last, with a class and a direction.
#  "forward" is one way from the first cell to the last, "two" both ways.
#  A street no run names is a street, both ways.
CLASS = {"street": 0, "avenue": 1, "boulevard": 2}
STREET_OUT, STREET_IN = 1, 2


def street_edges(placed, x0, y0):
    """The street layer's edges a city's blocks ask for, in world tiles:
    {(col, row, along_col): edge byte}, kept by the lower of the two
    tiles as the world keeps them (src/sim/world.c, world_street_edge)."""
    out = {}
    for b, c0, r0 in placed:
        for (ca, ra), (cb, rb), cls, way in b.streets:
            dc, dr = (cb > ca) - (cb < ca), (rb > ra) - (rb < ra)
            assert dc == 0 or dr == 0, "%s: a street run is straight" % b.name
            c, r = ca, ra
            while (c, r) != (cb, rb):
                n = (c + dc, r + dr)
                keep = min((c, r), n, key=lambda t: (t[1], t[0])) if dr else min((c, r), n)
                d = 0
                if way == "forward":
                    d = STREET_OUT if keep == (c, r) else STREET_IN
                out[(x0 + c0 + keep[0], y0 + r0 + keep[1], dr == 0)] = CLASS[cls] | (d << 2)
                c, r = n
    return out


def put_streets(path, edges):
    """The street layer written into a composed world, a byte a tile:
    the edge to the next column in the low nibble, to the next row in the
    high one.  Every chunk gets one, so the world reads it as the player's
    rather than working it out again."""
    tmp = path + ".tmp"
    with zipfile.ZipFile(path) as zin, zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as zout:
        chunks = set()
        for info in zin.infolist():
            part = info.filename.split("/")
            if part[0] == "chunks" and len(part) == 3:
                chunks.add(part[1])
                if part[2] == "street.bin":
                    continue
            zout.writestr(info, zin.read(info.filename))
        for name in sorted(chunks):
            cx, cy = (int(v) for v in name.split("_"))
            lay = bytearray(32 * 32)
            for (x, y, along_col), e in edges.items():
                if x >> 5 == cx and y >> 5 == cy:
                    i = (y & 31) * 32 + (x & 31)
                    lay[i] = (lay[i] & 0xF0) | e if along_col else (lay[i] & 0x0F) | (e << 4)
            zout.writestr("chunks/%s/street.bin" % name, bytes(lay))
    os.replace(tmp, path)


#  THE HEIGHTS: each kind of way over the ground's shapes.
HEIGHTS = ("heights", [
    Block("road over a hill", [
        "..............",
        "..............",
        "==============",
        "..............",
        "..............",
    ], ground=[
        "..............",
        "....111111....",
        "....111111....",
        "....111111....",
        "..............",
    ]),
    Block("road up two terraces", [
        "..............",
        "==============",
        "..............",
    ], ground=[
        "...11112222222",
        "...11112222222",
        "...11112222222",
    ]),
    Block("road through a valley", [
        "..............",
        "..............",
        "==============",
        "..............",
        "..............",
    ], ground=[
        "11111111111111",
        "22221111112222",
        "22221111112222",
        "22221111112222",
        "11111111111111",
    ]),
    Block("crossroads on a hilltop", [
        "......=.......",
        "......=.......",
        "......=.......",
        "......=.......",
        "......=.......",
        "......=.......",
        "=============.",
        "......=.......",
        "......=.......",
        "......=.......",
        "......=.......",
        "......=.......",
    ], ground=[
        "..............",
        "..............",
        "..............",
        "..............",
        "....11111.....",
        "....11111.....",
        "....11111.....",
        "....11111.....",
        "....11111.....",
        "..............",
        "..............",
        "..............",
    ]),
    Block("T at the foot of a slope", [
        "......=.......",
        "......=.......",
        "......=.......",
        "......=.......",
        "......=.......",
        "=============.",
    ], ground=[
        "....22222.....",
        "....22222.....",
        "....11111.....",
        "....11111.....",
        "..............",
        "..............",
    ]),
    Block("road at the foot of a cliff", [
        "..............",
        "..............",
        "..............",
        "..............",
        "==============",
        "..............",
    ], ground=[
        "33333333333333",
        "33333333333333",
        "33333333333333",
        "..............",
        "..............",
        "..............",
    ]),
    Block("railway up a slope and down", [
        "..............",
        "##############",
        "..............",
    ], ground=[
        "...11111111...",
        "...11111111...",
        "...11111111...",
    ]),
    Block("crossing on a plateau", [
        "......#.......",
        "......#.......",
        "......#.......",
        "=====x=======.",
        "......#.......",
        "......#.......",
        "......#.......",
    ], ground=[
        "..............",
        "....11111.....",
        "....11111.....",
        "....11111.....",
        "....11111.....",
        "....11111.....",
        "..............",
    ]),
    Block("highway over a valley", [
        "......||......",
    ] * 14, ground=[
        "33333333333333",
        "33333333333333",
        "33333333333333",
        "22222222222222",
        "11111111111111",
        "..............",
        "..............",
        "..............",
        "..............",
        "11111111111111",
        "22222222222222",
        "33333333333333",
        "33333333333333",
        "33333333333333",
    ]),
    Block("highway over a ridge", [
        "......||......",
    ] * 14, ground=[
        "..............",
        "..............",
        "11111111111111",
        "22222222222222",
        "33333333333333",
        "33333333333333",
        "33333333333333",
        "22222222222222",
        "11111111111111",
        "..............",
        "..............",
        "..............",
        "..............",
        "..............",
    ]),
    Block("highway climbing a mountain", [
        "......||......",
    ] * 18, ground=[row * 14 for row in "..11223344556677777"[:18]]),
    Block("highway over a cliff", [
        "......||......",
    ] * 12, ground=["." * 14] * 6 + ["4" * 14] * 6),
    Block("highway beside a hill", [
        "..||..........",
    ] * 12, ground=[
        "..............",
        "..............",
        ".....111111...",
        ".....122221...",
        ".....123321...",
        ".....123321...",
        ".....123321...",
        ".....122221...",
        ".....111111...",
        "..............",
        "..............",
        "..............",
    ]),
    Block("highway ending on a hillside", [
        "......||......",
        "......||......",
        "......||......",
        "......||......",
        "......||......",
        "......||......",
        "......||......",
        "......||......",
        "..............",
    ], ground=[
        "..............",
        "..............",
        "..............",
        "11111111111111",
        "22222222222222",
        "33333333333333",
        "33333333333333",
        "33333333333333",
        "33333333333333",
    ]),
    Block("interchange on uneven ground", [
        "......||......",
        "......||......",
        "......||......",
        "......||......",
        "------II------",
        "------II------",
        "......||......",
        "......||......",
        "......||......",
        "......||......",
    ], ground=[
        "..........2222",
        "..........2222",
        ".........12222",
        ".........12222",
        ".........12222",
        ".........12222",
        ".........12222",
        ".........12222",
        "..........2222",
        "..........2222",
    ]),
    Block("ramps down to a road below", [
        "......||......",
        "......||......",
        ".....R||......",
        "..====||......",
        "..=...||......",
        "..=...||......",
        "..=...||......",
        "..=...||......",
        "..====||......",
        ".....R||......",
        "......||......",
        "......||......",
    ], ground=[
        "22222222222222",
        "22222222222222",
        "22222222222222",
        "22222222222222",
        "11111111111111",
        "11111111111111",
        "..............",
        "..............",
        "..............",
        "..............",
        "..............",
        "..............",
    ]),
    Block("ramp to a road above the deck", [
        "......||......",
        "......||......",
        "......||......",
        "......||......",
        "......||R.....",
        "......||====..",
        "......||......",
        "......||......",
    ], ground=[
        "..............",
        ".........33333",
        ".........33333",
        ".........33333",
        ".........33333",
        "........333333",
        "........333333",
        "........333333",
    ]),
])

#  THE ARMS of an interchange run on past its block's own picture.  Every
#  highway that reaches the edge of a block with an interchange goes on
#  ARM_REACH cells more, straight out, over ground that goes on as the
#  edge has it.  The figure stays even, so a highway stays on even cells.
ARM_REACH = 8


def long_arms(b, n=ARM_REACH):
    """The block with every highway at its edge carried n cells out."""
    rows = [list(r) for r in b.rows]
    h, w = len(rows), len(rows[0])
    out = [["."] * (w + 2 * n) for _ in range(h + 2 * n)]
    for r in range(h):
        for c in range(w):
            out[r + n][c + n] = rows[r][c]
    for c in range(w):
        if rows[0][c] in "|I":
            for k in range(n):
                out[k][c + n] = "|"
        if rows[h - 1][c] in "|I":
            for k in range(n):
                out[h + n + k][c + n] = "|"
    for r in range(h):
        if rows[r][0] in "-I":
            for k in range(n):
                out[r + n][k] = "-"
        if rows[r][w - 1] in "-I":
            for k in range(n):
                out[r + n][w + n + k] = "-"
    ground = []
    if b.ground:
        g = [line.ljust(w, ".") for line in b.ground] + ["." * w] * max(0, h - len(b.ground))
        cell = lambda r, c: g[min(max(r, 0), h - 1)][min(max(c, 0), w - 1)]
        ground = ["".join(cell(r - n, c - n) for c in range(w + 2 * n)) for r in range(h + 2 * n)]
    pins = [(c + n, r + n, v) for c, r, v in b.pins]
    streets = [((ca + n, ra + n), (cb + n, rb + n), cls, way) for (ca, ra), (cb, rb), cls, way in b.streets]
    return Block(b.name, ["".join(line) for line in out], pins, ground, streets)


#  The sections, one city each, every block a case.
SECTIONS = [
    ('roads', [
        Block('straight and dead ends', [
            '..............',
            '.============.',
            '..............',
            '.=............',
            '.=............',
            '.=............',
            '.=............',
            '.=............',
        ]),
        Block('bend', [
            '..............',
            '.=======......',
            '.......=......',
            '.......=......',
            '.......=......',
            '.......=======',
        ]),
        Block('T', [
            '..............',
            '.=============',
            '.......=......',
            '.......=......',
            '.......=......',
            '.......=......',
        ]),
        Block('crossroads', [
            '.......=......',
            '.......=......',
            '.......=......',
            '.=============',
            '.......=......',
            '.......=......',
            '.......=......',
        ]),
        Block('T on a bend', [
            '..............',
            '.======.......',
            '......=.......',
            '......=======.',
            '......=.......',
            '......=.......',
        ]),
        Block('dead end of one', [
            '..............',
            '.=======......',
            '....=.........',
            '..............',
            '.=............',
        ]),
        Block('lone pieces', [
            '..............',
            '..=.....=.....',
            '..............',
            '..............',
            '..=.....=.....',
            '..............',
        ], pins=[(2, 1, LINE + 0), (8, 1, LINE + 1), (2, 4, LINE + 14), (8, 4, LINE + 13)]),
        Block('T with an arm to nothing', [
            '..............',
            '.=============',
            '..............',
            '..............',
            '.=............',
            '.=............',
            '.=............',
            '.=............',
        ], pins=[(7, 1, LINE + 13), (1, 5, LINE + 12)]),
        Block('two crossroads abreast', [
            '.....=.=......',
            '.....=.=......',
            '.=============',
            '.....=.=......',
            '.....=.=......',
        ]),
        Block('two abreast', [
            '..............',
            '.============.',
            '.============.',
            '..............',
            '..............',
            '.=.....=......',
            '.=.....=......',
            '.=======......',
            '.=.....=......',
        ]),
        Block('ring', [
            '..............',
            '.==========...',
            '.=........=...',
            '.=........=...',
            '.=........=...',
            '.==========...',
        ]),
        Block('grid', [
            '..=...=...=...',
            '.=============',
            '..=...=...=...',
            '..=...=...=...',
            '..=...=...=...',
            '.=============',
            '..=...=...=...',
            '..=...=...=...',
            '..=...=...=...',
            '.=============',
            '..=...=...=...',
        ]),
        Block('corner, open and hemmed', [
            '..............',
            '.======.......',
            '......=.......',
            '......=.......',
            '......=.......',
            '......=.......',
            '..............',
            '.======.......',
            '.bbbbb=.......',
            '.bbbbb=.......',
            '.bbbbb=.......',
            '.bbbbb=.......',
        ]),
        Block('corner by a junction', [
            '......=.......',
            '......=.......',
            '.=============',
            '......=.......',
            '......========',
            '..............',
        ]),
        Block('jog of one', [
            '..............',
            '.======.......',
            '......=======.',
            '..............',
            '.=............',
            '.=............',
            '.=............',
            '..=...........',
            '..=...........',
            '..=...........',
        ]),
        Block('jog of two', [
            '..............',
            '.=====........',
            '.....=........',
            '.....=======..',
            '..............',
        ]),
        Block('jogs of one, two, three', [
            '======........',
            '.....=========',
            '..............',
            '=====.........',
            '....=.........',
            '....==========',
            '..............',
            '====..........',
            '...=..........',
            '...=..........',
            '...===========',
        ]),
        Block('jog with short legs', [
            '..............',
            '.===..........',
            '...====.......',
            '......===.....',
            '..............',
            '.======.......',
            '.bbbbb======..',
            '..............',
        ]),
        Block('U and Z', [
            '..............',
            '.=======......',
            '.......=......',
            '.=======......',
            '..............',
            '.====.........',
            '....=.........',
            '....=.........',
            '....=======...',
        ]),
        Block('stair one by one', [
            '==............',
            '.==...........',
            '..==..........',
            '...==.........',
            '....==........',
            '.....==.......',
            '......==......',
            '.......==.....',
            '........==....',
            '.........=====',
        ]),
        Block('stair two by one', [
            '===...........',
            '..===.........',
            '....===.......',
            '......===.....',
            '........===...',
            '..........====',
        ]),
        Block('stairs of three and irregular', [
            '====..........',
            '...====.......',
            '......====....',
            '.........=====',
            '..............',
            '===...........',
            '..==..........',
            '...====.......',
            '......==......',
            '.......=======',
        ]),
        Block('straight into a diagonal and out', [
            '======........',
            '.....==.......',
            '......==......',
            '.......==.....',
            '........==....',
            '.........=====',
            '..............',
            '..............',
        ]),
        Block('diagonal into a T', [
            '==............',
            '.==...........',
            '..==..........',
            '...==.........',
            '....==........',
            '.....=........',
            '=============.',
            '..............',
        ]),
        Block('meeting at a stair', [
            '=.............',
            '==............',
            '.==...........',
            '..==..........',
            '...=====......',
            '...=..........',
            '...=..........',
        ]),
        Block('one-way street between two junctions', [
            '...=.....=....',
            '...=.....=....',
            '==============',
            '...=.....=....',
        ], streets=[((3, 2), (9, 2), 'street', 'forward')]),
        Block('one-way pair across a street', [
            '..=....=......',
            '..=....=......',
            '==============',
            '..=....=......',
            '..=....=......',
        ], streets=[((2, 0), (2, 4), 'street', 'forward'), ((7, 4), (7, 0), 'street', 'forward')]),
        Block('one-way streets meeting head on', [
            '==============',
        ], streets=[((0, 0), (7, 0), 'street', 'forward'), ((13, 0), (7, 0), 'street', 'forward')]),
        Block('one-way streets leaving each other', [
            '==============',
        ], streets=[((7, 0), (0, 0), 'street', 'forward'), ((7, 0), (13, 0), 'street', 'forward')]),
        Block('one-way street from a dead end to a dead end', [
            '==========',
        ], streets=[((0, 0), (9, 0), 'street', 'forward')]),
        Block('avenue across a street', [
            '......=.......',
            '......=.......',
            '==============',
            '......=.......',
            '......=.......',
        ], streets=[((0, 2), (13, 2), 'avenue', 'two')]),
        Block('a street that becomes an avenue', [
            '==============',
        ], streets=[((0, 0), (6, 0), 'avenue', 'two')]),
        Block('boulevard into a T', [
            '==============',
            '......=.......',
            '......=.......',
            '......=.......',
        ], streets=[((6, 0), (6, 3), 'boulevard', 'two')]),
        Block('one-way avenue through a crossroads', [
            '......=.......',
            '==============',
            '......=.......',
        ], streets=[((0, 1), (13, 1), 'avenue', 'forward')]),
        Block('boulevard crossing an avenue', [
            '......=.......',
            '......=.......',
            '==============',
            '......=.......',
            '......=.......',
        ], streets=[((0, 2), (13, 2), 'avenue', 'two'), ((6, 0), (6, 4), 'boulevard', 'two')]),
    ]),
    ('railways', [
        Block('straight and bend', [
            '..............',
            '.#######......',
            '.......#......',
            '.......#......',
            '.......#......',
            '.......######.',
        ]),
        Block('turnout', [
            '..............',
            '.#############',
            '.......#......',
            '.......#......',
            '.......#......',
            '.......#......',
        ]),
        Block('crossing', [
            '.......#......',
            '.......#......',
            '.......#......',
            '.#############',
            '.......#......',
            '.......#......',
            '.......#......',
        ]),
        Block('stair', [
            '##............',
            '.##...........',
            '..##..........',
            '...##.........',
            '....##........',
            '.....#########',
        ]),
        Block('two abreast', [
            '..............',
            '.############.',
            '.############.',
            '..............',
        ]),
        Block('loop', [
            '..............',
            '.##########...',
            '.#........#...',
            '.#........#...',
            '.##########...',
        ]),
        Block('bends, stair, jog', [
            '..............',
            '.#######......',
            '.......#......',
            '.......#......',
            '.......######.',
            '..............',
            '##............',
            '.##...........',
            '..##..........',
            '...###########',
            '..............',
            '#######.......',
            '......#######.',
        ]),
    ]),
    ('highways', [
        Block('straight', [
            '......||......',
            '......||......',
            '......||......',
            '......||......',
            '......||......',
            '......||......',
            '......||......',
            '......||......',
            '......||......',
            '......||......',
            '......||......',
            '......||......',
        ]),
        Block('curve', [
            '..||..........',
            '..||..........',
            '..||..........',
            '..||..........',
            '..CC----------',
            '..CC----------',
        ]),
        Block('S', [
            '..||..........',
            '..||..........',
            '..CC----CC....',
            '..CC----CC....',
            '........||....',
            '........||....',
            '........||....',
            '........||....',
        ]),
        Block('S with a long middle', [
            '..||..........',
            '..||..........',
            '..CC--------CC',
            '..CC--------CC',
            '............||',
            '............||',
        ]),
        Block('jog, open', [
            '..||..........',
            '..||..........',
            '..CC--CC......',
            '..CC--CC......',
            '......||......',
            '......||......',
            '......||......',
            '......||......',
        ]),
        Block('jog, hemmed', [
            'BB||BB........',
            'BB||BB........',
            '..CC--CC......',
            '..CC--CC......',
            'BBBBBB||BB....',
            'BBBBBB||BB....',
            '......||......',
            '......||......',
        ]),
        Block('corner, hemmed', [
            '..||..........',
            '..||..........',
            '..||..........',
            '..||..........',
            '..CC----------',
            '..CC----------',
            '....BBBBBBBB..',
            '....BBBBBBBB..',
        ]),
        Block('staircase', [
            '||............',
            '||............',
            'CCCC..........',
            'CCCC..........',
            '..CCCC........',
            '..CCCC........',
            '....CCCC......',
            '....CCCC......',
            '......CC------',
            '......CC------',
        ]),
        Block('long staircase', [
            '||............',
            '||............',
            'CCCC..........',
            'CCCC..........',
            '..CCCC........',
            '..CCCC........',
            '....CCCC......',
            '....CCCC......',
            '......CCCC....',
            '......CCCC....',
            '........CC----',
            '........CC----',
        ]),
        Block('two abreast', [
            '....||||......',
            '....||||......',
            '....||||......',
            '....||||......',
            '....||||......',
            '....||||......',
            '....||||......',
            '....||||......',
        ]),
        Block('ring', [
            'CC--------CC..',
            'CC--------CC..',
            '||........||..',
            '||........||..',
            '||........||..',
            '||........||..',
            'CC--------CC..',
            'CC--------CC..',
        ]),
        Block('end by a road', [
            '......||......',
            '......||......',
            '......||......',
            '......||......',
            '.=============',
            '..............',
            '..----........',
            '..----=======.',
            '..............',
        ]),
        Block('interchange', [
            '......||......',
            '......||......',
            '......||......',
            '......||......',
            '------II------',
            '------II------',
            '......||......',
            '......||......',
            '......||......',
            '......||......',
        ]),
        Block('interchange of three', [
            '..............',
            '..............',
            '..............',
            '..............',
            '------II------',
            '------II------',
            '......||......',
            '......||......',
            '......||......',
            '......||......',
        ]),
        Block('ramps', [
            '......||......',
            '......||......',
            '......||......',
            '.....R||......',
            '..====||......',
            '......||......',
            '......||......',
            '..====||......',
            '.....R||......',
            '......||......',
            '......||R.....',
            '......||====..',
            '......||......',
            '......||......',
        ]),
        Block('ramps close', [
            '......||......',
            '......||......',
            '.....R||......',
            '..====||......',
            '......||......',
            '..====||......',
            '.....R||......',
            '......||R.....',
            '......||====..',
            '......||......',
        ]),
        Block('ramps on a curve', [
            '..||..........',
            '..||R.........',
            '..||=====.....',
            '..||..........',
            '..CC----------',
            '..CC----------',
            '..====R.......',
            '..............',
        ]),
        Block('ramps by an interchange', [
            '......||......',
            '......||......',
            '......||R.....',
            '......||====..',
            '------II------',
            '------II------',
            '..====||......',
            '.....R||......',
            '......||......',
            '......||......',
        ]),
        Block('interchange with ramps both ways', [
            '......||......',
            '......||......',
            '.....R||R.....',
            '..====||====..',
            '------II------',
            '------II------',
            '..====||====..',
            '.....R||R.....',
            '......||......',
            '......||......',
        ]),
        Block('four interchanges joined', [
            '....||......',
            '....||......',
            '....||......',
            '....||......',
            '----IIII....',
            '----IIII....',
            '....IIII----',
            '....IIII----',
            '......||....',
            '......||....',
            '......||....',
            '......||....',
        ]),
        Block('four interchanges joined, with ramps', [
            '....||......',
            '====||====..',
            '...R||R.....',
            '==R.||......',
            '----IIII....',
            '----IIII.R==',
            '==R.IIII----',
            '....IIII----',
            '......||.R==',
            '.....R||R...',
            '..====||====',
            '......||....',
        ]),
        Block('interchange with a stub', [
            '......||......',
            '......||......',
            '......||......',
            '......||......',
            '------II------',
            '------II------',
            '......||......',
            '......||......',
        ]),
        Block('interchange of two arms', [
            '......||',
            '......||',
            '......||',
            '......||',
            '------II',
            '------II',
        ]),
        Block('interchange beside a curve', [
            '......||........',
            '......||........',
            '------IICC......',
            '------IICC......',
            '........||......',
            '........||......',
            '........||......',
            '........||......',
        ]),
        Block('two interchanges a unit apart', [
            '..||....||....',
            '..||....||....',
            '--II----II----',
            '--II----II----',
            '..||....||....',
            '..||....||....',
        ]),
        Block('highways touching at a corner', [
            '||......',
            '||......',
            '||......',
            '||......',
            '..||....',
            '..||....',
            '..||....',
            '..||....',
        ]),
        Block('curves touching at a corner', [
            '||......',
            '||......',
            'CC--....',
            'CC--....',
            '....CC--',
            '....CC--',
            '....||..',
            '....||..',
        ]),
        Block('on and off ramp in one unit', [
            '....||......',
            '====||====..',
            '...R||R.....',
            '...R||R.....',
            '====||====..',
            '....||......',
        ]),
        Block('on and off ramp in neighbouring units', [
            '....||......',
            '....||......',
            '====||====..',
            '...R||R.....',
            '...R||R.....',
            '====||====..',
            '....||......',
            '....||......',
        ]),
        Block('ramps a long way apart', [
            '....||......',
            '====||====..',
            '...R||R.....',
            '....||......',
            '....||......',
            '....||......',
            '....||......',
            '....||......',
            '....||......',
            '...R||R.....',
            '====||====..',
            '....||......',
        ]),
        Block('ramps at a highway end, in line', [
            '..||..',
            '..||..',
            '..||..',
            '..||..',
            '==RR==',
        ]),
        Block('ramp beside a highway end', [
            '..||',
            '..||',
            '.R||',
            '.=||',
            '.=..',
            '.=..',
        ]),
        Block('ramp road through a level crossing to a T', [
            '=.#...||..',
            '==x===||..',
            '=.#..R||..',
            '=.#...||..',
        ]),
    ]),
    ('power', [
        Block('straight, bend, T and cross', [
            '..............',
            '.*******......',
            '.......*......',
            '.......*******',
            '.......*......',
            '..*****.......',
            '...*..........',
            '...*..........',
        ]),
        Block('beside a road and a railway', [
            '.************.',
            '.============.',
            '..............',
            '.************.',
            '.############.',
        ]),
        Block('over a hill', [
            '..............',
            '**************',
            '..............',
        ], ground=[
            '....111111....',
            '....111111....',
            '....111111....',
        ]),
    ]),
    ('crossings', [
        Block('road over a railway', [
            '......#.......',
            '......#.......',
            '......#.......',
            '.=====x======.',
            '......#.......',
            '......#.......',
            '..........#...',
            '..........#...',
            '..=======.x===',
            '..........#...',
        ]),
        Block('road over a railway by a junction', [
            '......#.......',
            '......#.......',
            '.=====x======.',
            '......#.=.....',
            '......#.=.....',
            '......#.=.....',
        ]),
        Block('power under a road and over a railway', [
            '......*.......',
            '......*.......',
            '.=====^======.',
            '......*.......',
            '......*.......',
            '.#####r######.',
            '......*.......',
            '......*.......',
        ]),
        Block('highway over a road, a railway, power', [
            '...||.........',
            '...||.........',
            '.==++======...',
            '...||.........',
            '.##%%######...',
            '...||.........',
            '.**&&******...',
            '...||.........',
            '...||.........',
            '...||......',
        ]),
        Block('road beside a highway', [
            '..||..........',
            '..||..........',
            '.=||..........',
            '.=||..........',
            '.=CC----------',
            '.=CC----------',
            '.=============',
        ]),
        Block('street ending at a level crossing', [
            '......=.......',
            '......=.......',
            '######x#######',
        ]),
        Block('one-way street over a railway', [
            '......=.......',
            '......=.......',
            '######x#######',
            '......=.......',
            '......=.......',
        ], streets=[((6, 0), (6, 4), 'street', 'forward')]),
    ]),
    HEIGHTS,
]
#  Every block with an interchange has its arms carried on.
SECTIONS = [(name, [long_arms(b) if any("I" in line for line in b.rows) else b for b in blocks]) for name, blocks in SECTIONS]


def fit(grid, pins, ter):
    """The id of every cell, from what the grid holds and the terrain under it."""
    at = lambda c, r: grid.get((c, r), ".")
    band = set("|-+%&CI")
    line = set("=x^+R")
    thread = set("#xr%")  # a slab over a thread carries it
    power = set("*^r&")
    out = {}

    def axis(c, r):
        for dc, dr in ((0, -1), (0, 1)):
            if at(c + dc, r + dr) == "|":
                return "ns"
        for dc, dr in ((-1, 0), (1, 0)):
            if at(c + dc, r + dr) == "-":
                return "ew"
        return "ns" if at(c, r - 1) in band and at(c, r + 1) in band else "ew"

    def mask(c, r, joins):
        m = 0
        if at(c, r - 1) in joins: m += 8
        if at(c, r + 1) in joins: m += 2
        if at(c - 1, r) in joins: m += 1
        if at(c + 1, r) in joins: m += 4
        return m

    for (c, r), ch in grid.items():
        code = ter.get((c, r), 0)
        if ch in "=#*" and code in SLOPE_PIECE:
            out[(c, r)] = {"=": LINE, "#": THREAD, "*": POWER}[ch] + SLOPE_PIECE[code]
        elif ch == "=":
            out[(c, r)] = LINE + NET_PIECE[mask(c, r, line)]
        elif ch == "b":
            out[(c, r)] = HOUSE
        elif ch == "B":
            out[(c, r)] = BLOCK_TALL
        elif ch == "#":
            out[(c, r)] = THREAD + NET_PIECE[mask(c, r, thread)]
        elif ch == "*":
            out[(c, r)] = POWER + NET_PIECE[mask(c, r, power)]
        elif ch == "x":
            out[(c, r)] = 0x45 if at(c - 1, r) in line or at(c + 1, r) in line else 0x46
        elif ch == "^":
            out[(c, r)] = 0x43 if at(c - 1, r) in line or at(c + 1, r) in line else 0x44
        elif ch == "r":
            out[(c, r)] = 0x47 if at(c - 1, r) in thread or at(c + 1, r) in thread else 0x48
        elif ch == "|":
            out[(c, r)] = 0x4A
        elif ch == "-":
            out[(c, r)] = 0x49
        elif ch in "+%&":
            a = axis(c, r)
            out[(c, r)] = {"+": (0x4B, 0x4C), "%": (0x4D, 0x4E), "&": (0x4F, 0x50)}[ch][a == "ns"]
        elif ch == "I":
            out[(c, r)] = 0x69
        elif ch == "R":
            #  (slab side, line side) -> id, as the shipped cities have them
            ids = {("c-", "r+"): 0x5D, ("r-", "c+"): 0x5D, ("c-", "r-"): 0x5E, ("r-", "c-"): 0x5E,
                   ("c+", "r-"): 0x5F, ("r+", "c-"): 0x5F, ("c+", "r+"): 0x60, ("r+", "c+"): 0x60}
            sides = {"r-": (0, -1), "r+": (0, 1), "c-": (-1, 0), "c+": (1, 0)}
            s = next((k for k, (dc, dr) in sides.items() if at(c + dc, r + dr) in band), None)
            perp = ("c-", "c+") if s in ("r-", "r+") else ("r-", "r+")
            #  The road the ramp joins, before another ramp beside it.
            l = next((k for k in perp if at(c + sides[k][0], r + sides[k][1]) in line - {"R"}), None)
            l = l or next((k for k in perp if at(c + sides[k][0], r + sides[k][1]) in line), None)
            if l is None:
                l = next((k for k in sides if at(c + sides[k][0], r + sides[k][1]) in line), perp[1])
                l = l if l in perp else perp[1]
            out[(c, r)] = ids.get((s, l), 0x5D)
    #  A thread on the flat beside the foot of its own slope takes the
    #  trestle end for that side ($1FB60), the first side found winning.
    for (c, r), ch in grid.items():
        if ch != "#" or ter.get((c, r), 0) in SLOPE_PIECE:
            continue
        for (dc, dr), slope, end in (((0, -1), 0x2E, 0x3B), ((0, 1), 0x30, 0x3D),
                                     ((-1, 0), 0x2F, 0x3C), ((1, 0), 0x31, 0x3E)):
            if out.get((c + dc, r + dr)) == slope:
                out[(c, r)] = end
                break
    #  The curve blocks: one id for the four cells, by the two sides a band
    #  leaves it by.
    done = set()
    for (c, r), ch in sorted(grid.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        if ch != "C" or (c, r) in done:
            continue
        cells = [(c, r), (c + 1, r), (c, r + 1), (c + 1, r + 1)]
        done.update(cells)
        up = any(at(c + i, r - 1) in band for i in (0, 1))
        dn = any(at(c + i, r + 2) in band for i in (0, 1))
        lf = any(at(c - 1, r + i) in band for i in (0, 1))
        rt = any(at(c + 2, r + i) in band for i in (0, 1))
        if dn and lf: b = 0x65
        elif dn and rt: b = 0x66
        elif up and rt: b = 0x67
        else: b = 0x68
        for p in cells:
            out[p] = b
    out.update(pins)
    return out


#  Each tile layer of a 1995 city: its name in a .arco chunk, and how many
#  tiles a side one of its cells covers (src/sim/arco.c, LAYERS).
LAYERS = {b"ALTM": ("altm", 1), b"XBLD": ("xbld", 1), b"XZON": ("xzon", 1), b"XTER": ("xter", 1),
          b"XUND": ("xund", 1), b"XBIT": ("xbit", 1), b"XTXT": ("xtxt", 1),
          b"XTRF": ("xtrf", 2), b"XPLT": ("xplt", 2), b"XVAL": ("xval", 2), b"XCRM": ("xcrm", 2),
          b"XPLC": ("xplc", 4), b"XFIR": ("xfir", 4), b"XPOP": ("xpop", 4), b"XROG": ("xrog", 4)}
#  A chunk's side in tiles, the world.json chunk_size.
CHUNK = 32


def chunk_layer(chunks, tag, cx, cy):
    """One chunk's cells of a 1995 layer, as a .arco chunk holds them:
    row-major and little-endian."""
    _, cell = LAYERS[tag]
    side, row = CHUNK // cell, N // cell
    raw = chunks[tag]
    if tag == b"ALTM":
        words = struct.unpack(">%dH" % (N * N), raw)
        return b"".join(struct.pack("<%dH" % side, *words[(cy * side + j) * row + cx * side:
                                                          (cy * side + j) * row + cx * side + side])
                        for j in range(side))
    return b"".join(raw[(cy * side + j) * row + cx * side:(cy * side + j) * row + cx * side + side]
                    for j in range(side))


def write(path, chunks, template):
    """The city as a .arco world: the template's own world (`arcology
    --convert` writes it), each tile layer and record replaced by the
    city's.  The owner and street layers are left out, so the reader
    works both out again from the tiles."""
    names = {v[0]: k for k, v in LAYERS.items()}
    with zipfile.ZipFile(template) as zin, zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zout:
        for info in zin.infolist():
            name, data = info.filename, None
            part = name.split("/")
            if part[0] == "chunks" and len(part) == 3:
                layer = part[2][:-len(".bin")]
                if layer in ("owner", "street"):
                    continue
                cx, cy = (int(v) for v in part[1].split("_"))
                data = chunk_layer(chunks, names[layer], cx, cy)
            elif part[0] == "cities" and len(part) == 3:
                rec = part[2][:-len(".bin")]
                if rec == "graphs":
                    g = chunks[b"XGRP"]
                    data = struct.pack("<%di" % (len(g) // 4), *struct.unpack(">%di" % (len(g) // 4), g))
                elif rec in ("xlab", "xmic", "xthg", "cnam"):
                    data = chunks[rec.upper().encode()]
            zout.writestr(name, zin.read(name) if data is None else data)


def terrain(heights):
    """Each cell's base altitude and terrain code from the levels of the
    cells, and the cells whose corners could not all be met (a cliff)."""
    level = lambda c, r: heights.get((c, r), 0)
    corner = {}
    for c in range(N + 1):
        for r in range(N + 1):
            corner[(c, r)] = max(level(c - dc, r - dr) for dc in (0, 1) for dr in (0, 1))
    base, ter = {}, {}
    for c in range(N):
        for r in range(N):
            own = level(c, r)
            hs = [corner[(c, r)], corner[(c, r + 1)], corner[(c + 1, r + 1)], corner[(c + 1, r)]]
            m = sum(1 << k for k, h in enumerate(hs) if h > own)
            if m == 15:
                own, m = own + 1, 0
            base[(c, r)] = own
            ter[(c, r)] = CODE.get(m, 13)
    return base, ter


def trim(b):
    """The block's own cells, the rows and columns with nothing on them cut off."""
    rows = list(b.rows) + ["." * len(b.rows[0])] * max(0, len(b.ground) - len(b.rows))
    ground = list(b.ground) + ["." * len(rows[0])] * (len(rows) - len(b.ground))
    used = lambda r, c: (c < len(rows[r]) and rows[r][c] != ".") or (c < len(ground[r]) and ground[r][c] not in ".0")
    w = max(len(x) for x in rows)
    rs = [r for r in range(len(rows)) if any(used(r, c) for c in range(w))]
    cs = [c for c in range(w) if any(used(r, c) for r in range(len(rows)))]
    #  The margins are cut two at a time, so a highway drawn on even cells
    #  of its block stays on even cells: the game lays a highway only on
    #  a 2x2 that starts at an even column and row.
    r0, r1, c0, c1 = rs[0] & ~1, rs[-1] + 1, cs[0] & ~1, cs[-1] + 1
    cut = lambda xs: [x.ljust(w, ".")[c0:c1] for x in xs[r0:r1]]
    pins = [(c - c0, r - r0, v) for c, r, v in b.pins]
    streets = [((ca - c0, ra - r0), (cb - c0, rb - r0), cls, way) for (ca, ra), (cb, rb), cls, way in b.streets]
    return Block(b.name, cut(rows), pins, cut(ground), streets)


def layout(blocks):
    """Blocks packed left to right in rows, GAP apart: [(block, col, row)],
    and the blocks that did not fit on one map."""
    placed, left, c, r, tall = [], [], EDGE, EDGE, 0
    for b in blocks:
        h, w = len(b.rows), len(b.rows[0])
        if c + w > N - EDGE:
            c, r, tall = EDGE, r + tall + GAP, 0
        if r + h > N - EDGE:
            left.append(b)
            continue
        #  The game lays a highway only on a 2x2 that starts at an even
        #  column and row, so a block stands where its highway's first
        #  column and first row land even.
        cells = [(x, y) for y, line in enumerate(b.rows) for x, ch in enumerate(line) if ch in "|-+%&CI"]
        if cells:
            c += (c + min(x for x, _ in cells)) & 1
            r += (r + min(y for _, y in cells)) & 1
        placed.append((b, c, r))
        c, tall = c + w + GAP, max(tall, h)
    return placed, left


def city(path, placed, template, template_arco):
    grid, pins, heights = {}, {}, {}
    for b, c0, r0 in placed:
        for r, line in enumerate(b.rows):
            for c, ch in enumerate(line):
                if ch != ".":
                    grid[(c0 + c, r0 + r)] = ch
        for r, line in enumerate(b.ground):
            for c, ch in enumerate(line):
                if ch.isdigit() and ch != "0":
                    heights[(c0 + c, r0 + r)] = int(ch)
        for c, r, v in b.pins:
            pins[(c0 + c, r0 + r)] = v
    base, ter = terrain(heights)
    ids = fit(grid, pins, ter)
    #  The game lays a highway only on a 2x2 that starts at an even column
    #  and row.
    hw = lambda v: 0x49 <= v <= 0x50 or 0x61 <= v <= 0x6B
    for (c, r), v in sorted(ids.items()):
        if hw(v) and not all(hw(ids.get(((c & ~1) + dc, (r & ~1) + dr), 0)) for dc in (0, 1) for dr in (0, 1)):
            print("  %s: the highway at %d,%d is not on a 2x2 that starts at an even column and row" %
                  (os.path.basename(path), c, r))
    across = sorted(p for p, ch in grid.items() if ch in "=#*x^r" and ter[p] not in (0, 1, 2, 3, 4))
    for c, r in across:
        print("  %s: the way at %d,%d stands on terrain %d, which no piece climbs" %
              (os.path.basename(path), c, r, ter[(c, r)]))

    chunks, _ = sc2.load(template)
    #  The floor is the template's commonest height of dry land.
    altm, xter = chunks[b"ALTM"], chunks[b"XTER"]
    count = {}
    for i in range(N * N):
        if xter[i] == 0:
            w = struct.unpack(">H", altm[2 * i:2 * i + 2])[0]
            count[w] = count.get(w, 0) + 1
    floor = max(count, key=count.get)
    alt, xt = bytearray(), bytearray(N * N)
    for r in range(N):
        for c in range(N):
            alt += struct.pack(">H", floor + base[(c, r)])
            xt[r * N + c] = ter[(c, r)]
    chunks[b"ALTM"] = bytes(alt)
    for t in (b"XZON", b"XUND", b"XTXT", b"XBIT", b"XTRF", b"XPLT", b"XCRM", b"XPLC", b"XFIR", b"XPOP",
              b"XROG", b"XGRP", b"XLAB", b"XMIC", b"XTHG"):
        chunks[t] = bytes(len(chunks[t]))
    chunks[b"XTER"] = bytes(xt)
    xbld, xzon = bytearray(N * N), bytearray(N * N)
    for (c, r), v in ids.items():
        xbld[r * N + c] = v
    #  A building is drawn from the corners XZON marks: all four bits on a
    #  house, one corner a cell on each 2x2 block.  The low nibble is the
    #  zone it grew in.
    seen = set()
    for (c, r) in sorted(ids, key=lambda p: (p[1], p[0])):
        v = ids[(c, r)]
        if v == HOUSE:
            xzon[r * N + c] = 0xF1
        elif v == BLOCK_TALL and (c, r) not in seen:
            for (dc, dr), bit in (((0, 0), 0x10), ((1, 0), 0x80), ((0, 1), 0x20), ((1, 1), 0x40)):
                seen.add((c + dc, r + dr))
                xzon[(r + dr) * N + c + dc] = bit | 0x02
        elif v in HIGHWAY_1X1:
            #  A highway piece of one tile: the placer marks all four
            #  corners on it, as on any single tile it places ($382E).
            xzon[r * N + c] = 0xF0
        elif v in HIGHWAY_2X2:
            #  A curve block or an interchange is one piece over a 2x2,
            #  and the game's placer marks its four corners as it marks a
            #  building's ($380A).  A highway's 2x2 starts at an even
            #  column and row, so a cell's corner is its place in it.
            xzon[r * N + c] = {(0, 0): 0x10, (1, 0): 0x80, (0, 1): 0x20, (1, 1): 0x40}[(c % 2, r % 2)]
    #  A ramp's byte names its two edges as the tiles round it ask, and
    #  XBIT bit 0x02 says which way round: set where the highway lies a
    #  row away, as the game's placer sets it.
    xbit = bytearray(chunks[b"XBIT"])
    band = set("|-+%&CI")
    for (c, r), v in ids.items():
        if 0x5D <= v <= 0x60 and (grid.get((c, r - 1)) in band or grid.get((c, r + 1)) in band):
            xbit[r * N + c] |= 0x02
    chunks[b"XBIT"] = bytes(xbit)
    chunks[b"XBLD"], chunks[b"XZON"] = bytes(xbld), bytes(xzon)
    if b"CNAM" in chunks:
        nm = os.path.splitext(os.path.basename(path))[0].encode()[:31]
        chunks[b"CNAM"] = bytes([len(nm)]) + nm + bytes(len(chunks[b"CNAM"]) - 1 - len(nm))
    write(path, chunks, template_arco)


def cities():
    """[(section, [(block, col, row)])], one city a section, a section
    that does not fit in one window going on in a second."""
    out = []
    for name, blocks in SECTIONS:
        rest, k = [trim(b) for b in blocks], 1
        while rest:
            placed, rest = layout(rest)
            out.append(("%s%s" % (name, "" if k == 1 else "-%d" % k), placed))
            k += 1
    return out


#  The test world's width in tiles, and the land between two cities.
WORLD_W, CITY_GAP = 384, 4


def extent(placed):
    """The tiles a city's blocks reach, from the window's corner, with the
    slopes around their ground."""
    w = max(c + len(b.rows[0]) for b, c, r in placed) + 1
    h = max(r + len(b.rows) for b, c, r in placed) + 1
    return w, h


def pack(sections):
    """Each city's window origin in the world: shelves left to right,
    each city as wide as what it holds, so the windows overlap where the
    cities do not.  An origin is a multiple of 32, a chunk's side."""
    up = lambda v: (v + 31) & ~31
    out, x, y, tall = [], 0, 0, 0
    for name, placed in sections:
        w, h = extent(placed)
        if x and x + w > WORLD_W:
            x, y, tall = 0, up(y + tall + CITY_GAP), 0
        out.append((name, placed, x, y))
        x, tall = up(x + w + CITY_GAP), max(tall, h)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--template", default=os.path.join(ROOT, "cities", "dullsville.sc2"))
    ap.add_argument("--out", default=os.path.join(ROOT, "cities", "test.arco"))
    ap.add_argument("--binary", default=os.path.join(ROOT, "build", "arcology"))
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    tmp = tempfile.mkdtemp()
    template = os.path.join(tmp, "template.arco")
    p = subprocess.run([a.binary, "--convert", a.template, template], capture_output=True, text=True)
    if p.returncode:
        shutil.rmtree(tmp)
        print(p.stdout.strip() or p.stderr.strip())
        return p.returncode
    args, edges = [], {}
    for name, placed, x0, y0 in pack(cities()):
        edges.update(street_edges(placed, x0, y0))
        path = os.path.join(tmp, "test-%s.arco" % name)
        city(path, placed, a.template, template)
        args.append("%s@%d,%d" % (path, x0, y0))
        print("city %-12s at %3d,%3d: %d blocks" % (name, x0, y0, len(placed)))
        if a.list:
            for b, c, r in placed:
                print("  %3d,%3d  %2dx%-2d  %s" % (x0 + c, y0 + r, len(b.rows[0]), len(b.rows), b.name))
    p = subprocess.run([a.binary, "--compose", a.out] + args, capture_output=True, text=True)
    shutil.rmtree(tmp)
    print(p.stdout.strip() or p.stderr.strip())
    if p.returncode == 0:
        put_streets(a.out, edges)
    return p.returncode


if __name__ == "__main__":
    sys.exit(main())
