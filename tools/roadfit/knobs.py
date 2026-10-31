"""knobs.py -- every number the road fit prototype is tuned by.

Distances are in tiles, heights in levels.  A level is LEVEL_H tiles tall.
Edit a number here and run `python3 tools/roadfit/run.py` again.
"""

LEVEL_H = 0.5  # a level's height in tiles, as the mesh has it (LEVEL_H)

# ---- plan: the line a road follows -----------------------------------------

#  A run of cells is a straight when one line passes through every cell's
#  square, half side STAB, and the run keeps a staircase's rhythm.
STAB = 0.5
#  The room to spare: a road's corridor is its cells widened by MARGIN.
MARGIN = 0.25
#  A corner takes at most HALF of each straight beside it (the tangent fit).
HALF = 0.5
#  Per network: the size of its unit in cells, its width, and its largest
#  corner radius.
NET = {
    "road": {"unit": 1, "width": 0.5, "radius": 1.5},
    "band": {"unit": 2, "width": 1.5, "radius": 4.0},
}

# ---- heights ----------------------------------------------------------------

#  How far a street may stray from its tiles' own heights.
STREET_HOLD = 1.0
#  How strongly a street keeps to its tiles' heights, and a deck to the
#  level it holds: the stiffness of each against bending.
STREET_PULL = 0.02
DECK_PULL = 0.01
#  A street never rises above the highest of its tiles' heights within
#  REACH, nor falls below the lowest, so it does not overshoot a level.
REACH = 2.0
#  A highway's deck stands CLEARANCE above the highest ground under each unit.
CLEARANCE = 1.0
#  It holds a level across a dip narrower than twice FLAT and no deeper than
#  EXTRA, on columns at most EXTRA taller.
FLAT = 4.0
EXTRA = 1.0
#  It may rise SMOOTH above a level it holds, to ease onto it.
SMOOTH = 0.25
#  It never climbs steeper than this angle, in degrees.
MAX_GRADE = 18.435  # one level for each tile and a half
#  A ramp is one cell climbing RAMP_CLIMB levels.  It is legal only where the
#  deck is RAMP_CLIMB above its cell, within RAMP_TOLERANCE.
RAMP_CLIMB = 1.0
RAMP_TOLERANCE = 0.05
