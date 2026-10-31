"""strips.py -- made land: strips of tiles to try a street and a highway on.

A strip is a land like a city's, ROWS tiles deep: tile k runs from level
LANDS[k] at its near edge to LANDS[k + 1] at its far edge, which differ by at
most one, as the original's land does.  The highway runs on rows 0 and 1,
and the street and any ramp on row 2.  Both go through the same code as a
city's roads.
"""
from knobs import RAMP_CLIMB, RAMP_TOLERANCE

ROWS = 3

LANDS = {
    "rolling hills": [4, 4, 5, 5, 5, 6, 7, 7, 7, 6, 6, 5, 5, 5, 6, 6, 7, 8, 8, 8, 8,
                      7, 6, 6, 5, 5, 5, 5, 6, 6, 7, 7, 6, 6, 5, 5, 5, 4, 4, 4, 4],
    "a valley": [9, 9, 9, 9, 8, 7, 7, 6, 5, 5, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3,
                 3, 4, 4, 4, 5, 6, 6, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9],
    "a city on two levels": [4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 6, 7, 8, 8, 8, 8, 8, 8,
                             8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 6, 5, 4, 4, 4, 4, 4, 4, 4],
}


def land(levels):
    """The land of a strip, as land.load answers it: a tile rising toward
    its far edge lifts its NE and SE corners, one falling its NW and SW."""
    n = len(levels) - 1
    side = max(n, ROWS)
    alt = [[0] * side for _ in range(side)]
    lift = [[0] * side for _ in range(side)]
    for r in range(ROWS):
        for k in range(n):
            a, b = levels[k], levels[k + 1]
            alt[r][k] = min(a, b)
            lift[r][k] = 12 if b > a else 3 if a > b else 0
    return side, alt, lift, [[False] * side for _ in range(side)]


def at(prof, x):
    """A profile's height at s = x, between its samples."""
    for (x0, y0), (x1, y1) in zip(prof, prof[1:]):
        if x0 <= x <= x1:
            return y0 if x1 == x0 else y0 + (y1 - y0) * (x - x0) / (x1 - x0)
    return prof[0][1] if x < prof[0][0] else prof[-1][1]


def ramps(levels, deck):
    """Every legal ramp beside the highway: a flat tile of the street's row
    whose one end lies RAMP_CLIMB below the deck, within RAMP_TOLERANCE.  The
    ramp climbs from the tile's other end, on the ground, to the deck.  The
    strip's ends are cuts, not the deck's real ends, so a ramp within a unit
    of either is left out."""
    out = []
    a, b = deck[0][0] + 2, deck[-1][0] - 2
    for k in range(len(levels) - 1):
        g = levels[k]
        if levels[k + 1] != g:
            continue
        for top, foot in ((k + 1, k), (k, k + 1)):
            if a <= top <= b and abs(at(deck, top) - g - RAMP_CLIMB) <= RAMP_TOLERANCE:
                out.append(((float(foot), g), (float(top), g + RAMP_CLIMB)))
    return out
