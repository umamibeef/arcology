.. _appendix-road-spec:

Dynamic Road System Spec — North American Roads on a Square Grid
================================================================

.. container:: eyebrow

   Reference · the user's specification, brought in whole; updated 2 September 2026 with Part 7, the raised highways

.. container:: lede

   The specification below is the reference the road and rail renderers are built against (see the road section of :doc:`enhanced-renderer`). It is the user's document, reproduced verbatim. The feasibility of each part against what the original game and its save format provide is tabled on :doc:`future`.


Engine-agnostic. Assumes: square grid, each cell is a *tile*, roads are placed per tile, and everything visible (lines, curbs, sidewalks, signs, furniture) is derived from tile data rather than hand-placed. Units below are metres (NA standard values; scale to your tile size).

--------------

Part 1 — Inventory
------------------

1.1 Road classes (the master enum — almost every rule keys off this)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

============================= ============== ========== ======= ====================== ===================== ====================== ====================================================
Class                         Typical lanes  Lane width Speed   Sidewalk               Parking               Median                 Notes
============================= ============== ========== ======= ====================== ===================== ====================== ====================================================
``ALLEY``                     1 shared       3.0–3.6    15–20   none                   none                  none                   No markings, no curb, gravel/asphalt, garbage access
``LOCAL`` (residential)       2 (1 each way) 3.0–3.5    30–40   both sides, 1.5 m      parallel both sides   none                   Often *no centerline*
``COLLECTOR``                 2–3            3.3–3.5    40–50   both, 1.5–1.8 m        parallel, one/both    optional               Yellow centerline, occasional center turn lane
``ARTERIAL_MINOR``            4              3.5        50–60   both, 1.8–2.4          restricted / off-peak painted or raised      Turn lanes at intersections
``ARTERIAL_MAJOR``            4–6            3.5–3.7    60–70   both, 2.4+ with buffer none                  raised                 Dual left turn, right-turn pockets
``HIGHWAY`` (undivided/rural) 2–4            3.7        80–90   none; shoulders        none                  none or painted        Rumble strips, edge lines
``FREEWAY``                   4–8 divided    3.7        100–120 none                   none                  grass/concrete barrier Full access control, ramps only
``RAMP``                      1–2            4.0–4.5    40–60   none                   none                  —                      Gore, acceleration/deceleration taper
``ONEWAY`` (modifier)         —              —          —       —                      —                     —                      Flag on any class; changes centerline to lane lines
``PEDESTRIAN`` / ``MALL``     0              —          —       full width             none                  —                      Bollards, pavers, no vehicle markings
============================= ============== ========== ======= ====================== ===================== ====================== ====================================================

Derived widths (right-of-way): ``ROW = lanes × laneWidth + 2×(parking? 2.4 : 0) + medianWidth + 2×curbGutter + 2×sidewalk + 2×buffer``.

1.2 Cross-section elements (outside → inside)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Property line / setback**

**Sidewalk zone**

-  Sidewalk slab (concrete, scored every 1.5 m; expansion joints every ~6 m)
-  Frontage zone (0.3–0.6 m strip against buildings; café tables, sandwich boards)
-  Furnishing / boulevard / buffer strip (0.6–2.4 m): grass, street trees, tree grates, planters, utility poles, hydrants, benches, bins, lamp posts, bike racks, bus shelters, mailboxes, newspaper boxes, parking meters, utility cabinets/pedestals, transformer boxes
-  Curb ramps at every crossing (flared or returned; tactile warning domes — yellow (US), often red/yellow (CA))
-  Driveway aprons (residential 3–6 m, commercial 7–12 m) with depressed curb and flared wings

**Curb & gutter**

-  Barrier curb (150 mm vertical) — urban default
-  Mountable / rolled curb — residential subdivisions
-  No curb (rural/highway) — gravel shoulder + ditch
-  Gutter pan (0.3–0.6 m concrete strip, drains to catch basins)
-  Catch basins / storm inlets (curb inlet or grate) at low points, before every intersection curb return, max ~90 m spacing
-  Curb paint: red (no stopping), yellow (loading/no parking), blue (accessible), green (short-term), white (passenger loading)
-  Curb-face numbers/stencils, curb extensions (bulb-outs)

**Shoulder (rural/highway only)**

-  Paved shoulder 1.5–3.0 m, gravel shoulder, rumble strips (edge & centerline), ditch, guardrail (W-beam), cable barrier, concrete Jersey barrier, culverts, delineator posts

**Parking lane**

-  Parallel (2.4 m × 6.5–7 m stalls), angled (45°/60°), back-in angled, perpendicular (rare on-street)
-  Stall lines (white "T" or "L" tick marks, or full lines), meters / pay stations, loading zones, accessible stalls with symbol, EV stalls, taxi stands, fire hydrant no-parking gap (5 m each side)

**Bike facilities**

-  Painted bike lane (1.5–1.8 m, white line + bike symbol + arrow), buffered lane (hatched buffer 0.6–0.9 m), protected lane (curb/planter/bollard/parked-car separated), bike box at signals (green), sharrows on locals, green conflict-zone paint at intersections/driveways, bike signal heads

**Travel lanes**

-  Through lanes, turn lanes (left/right pockets, dual lefts), two-way left-turn lane (TWLTL, "suicide lane"), bus lane (red paint, "BUS ONLY"), HOV lane (diamond), reversible lane, passing lane, climbing lane, truck lane, acceleration/deceleration lane, weave lane

**Median / center**

-  None, painted (double yellow), flush hatched median, raised curbed median (concrete/grass/planted), median with left-turn cutouts, median nose/refuge island at crossings, concrete barrier, cable barrier, grass swale (freeway), median openings/U-turn cutouts, median lighting

**Pavement surface**

-  Asphalt (fresh black → grey with age), concrete (jointed panels, PCC), brick/paver, gravel, chip-seal, cobblestone (historic)
-  Crown (center high, 2% cross-slope), superelevation on curves
-  Wear: wheel ruts, patches, crack sealing, potholes, oil stains at stop lines, tire marks at intersections, utility trench scars, faded markings
-  Manholes (sanitary, storm, telecom, electrical) — usually in the lane, staggered
-  Water valve boxes, gas valve covers, survey monuments
-  Rumble strips, speed humps/tables/cushions, raised crossings, raised intersections
-  Rail: embedded streetcar/LRT tracks, railroad grade crossing (planks, X markings, gates, flashers, crossbuck)

1.3 Pavement markings (MUTCD / TAC conventions)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Longitudinal**

=============================== ======================================== ============================================ ==========================================================
Marking                         Colour                                   Pattern                                      Meaning
=============================== ======================================== ============================================ ==========================================================
Centerline, passing allowed     yellow                                   dashed 3 m line / 9 m gap (US: 10'/30')      two-way road
Centerline, no passing one side yellow                                   solid + dashed pair                          —
Centerline, no passing          yellow                                   double solid, 100–150 mm each, 100 mm gap    arterials, near intersections
Lane line                       white                                    dashed 3/9                                   same-direction lanes
Lane line, no change            white                                    solid                                        approaching intersections (last 30–60 m), tunnels, bridges
Edge line                       white (right) / yellow (left on divided) solid                                        highways, freeways, anywhere no curb
Wide dotted lane line           white                                    1 m / 1 m short dashes, wide                 lane drop, ramp, exit
Bike lane line                  white                                    solid 150 mm; dashed at merges/turn conflict —
Bus lane                        white solid + optional red fill          —                                            —
TWLTL boundaries                yellow                                   solid-outer / dashed-inner pair on each side —
Reversible lane                 yellow double dashed                     —                                            —
Parking lane line               white                                    solid                                        optional
Gore / channelizing             white (same dir) / yellow (opposite dir) wide solid with diagonal chevrons            ramps, medians
=============================== ======================================== ============================================ ==========================================================

**Transverse**

-  Stop line (white, 300–600 mm wide, 1.2 m before crosswalk or 4 m before centerline of cross street)
-  Yield line (white triangles "shark teeth")
-  Crosswalks: standard (two parallel lines), continental/zebra (600 mm bars, 600 mm gaps), ladder (both), diagonal-hatched, brick/textured, raised, scramble (diagonal across intersection), school (yellow in some jurisdictions)
-  Advance yield markings, speed hump markings (chevrons), rumble-strip markings, railroad "RXR" + X, "STOP AHEAD", "SCHOOL", "XING", "BUS STOP", "NO PARKING", "KEEP CLEAR" box (yellow cross-hatched box junction), speed limit stencils, "ONLY", lane-use arrows (through, left, right, left-through, left-right, U-turn, merge), bike symbols, sharrow chevrons, HOV diamond, accessible symbol, EV symbol,
   parking stall Ts

**Delineation hardware**

-  Raised pavement markers (RPMs) — white/yellow reflective, red on wrong-way side; snowplowable recessed variant (Canada, northern US)
-  Flexible delineator posts (orange/white), bollards, cat's eyes, tubular markers at lane splits

1.4 Intersection components
~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  Curb returns (rounded corners): radius by class — 3–5 m alley/local, 6–9 m collector, 9–15 m arterial, 15–25 m truck routes
-  Curb ramps ×2 per corner (perpendicular) or ×1 (diagonal — older), tactile plates
-  Crosswalks on 0–4 legs; stop bars on controlled legs
-  Corner islands / pork-chop islands for channelized right turns; splitter islands at roundabouts
-  Refuge islands in medians
-  Turn lane pockets (taper 30–60 m, storage 30–90 m)
-  Sight triangles (no parking within 6–10 m of corner: curb paint / signage)
-  Control hardware:

   -  Uncontrolled (locals × locals; yield-to-right)
   -  Yield sign
   -  2-way stop (minor street stops), 4-way / all-way stop with "ALL WAY" plaque
   -  Traffic signal: pedestal-mounted, mast arm (1 pole/corner, arms over approaches), span wire (older/rural), signal heads 3-section, 4/5-section with arrows, pedestrian signals (walk/hand with countdown), push buttons on poles, audible pedestrian signals, flashing yellow arrow, bike signals, signal controller cabinet (grey box on one corner), detector loops (sawcut squares in pavement),
      video/radar detectors on arms, "NO TURN ON RED", "LEFT ON GREEN ARROW ONLY" signs, street name blades on mast arm, red-light camera
   -  Roundabout: circulatory lane(s), central island (landscaped/truck apron), splitter islands, yield lines, chevron signs on central island, roundabout warning sign, no crosswalk across circulatory lane
   -  Flashing beacon (red/yellow), HAWK / RRFB pedestrian beacons at mid-block crossings

-  Corner furniture: street name signs (blade on pole at 1–2 diagonally opposite corners), fire hydrant, USPS/Canada Post box, utility pole with guy wires, streetlight, trash bin, newspaper boxes, signal cabinet, bench, bus stop (far-side placement default)

1.5 Signage (posts, mounting, contents)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  **Regulatory** (white/red/black): STOP, YIELD, speed limit (US: "SPEED LIMIT 35"; CA: "MAXIMUM 50"), ONE WAY, DO NOT ENTER, WRONG WAY, NO PARKING (time-limited variants), NO STOPPING, NO LEFT/RIGHT/U-TURN, KEEP RIGHT, ONE WAY arrow, lane-use control, TRUCK ROUTE, NO TRUCKS, BIKE LANE, BUS STOP, HOV, handicapped parking
-  **Warning** (yellow diamond): curve/turn/winding ahead with advisory speed plaque, intersection ahead (cross/T/side road), signal ahead, stop ahead, merge, lane ends, divided highway begins/ends, pedestrian crossing, school crossing (fluorescent yellow-green pentagon), playground, deer, bump, dip, slippery when wet, low clearance, narrow bridge, hill, dead end / no outlet, road narrows, two-way
   traffic, chevron alignment (on curves), object markers, RR advance warning (round yellow)
-  **Guide** (green/blue/brown): street name blades, route markers (interstate shield, US route, state/provincial highway, Trans-Canada), destination/distance, exit signs (with exit tabs), overhead gantry signs (freeway), rest area/services (blue), recreational (brown), bike route (green)
-  **Construction** (orange): work zone, flagger, detour, barrels, cones, barricades, arrow boards, temporary lane shift markings
-  **Mounting**: U-channel post, square tube (telespar), wood 4×4, breakaway bases, back-to-back mounting, mast arm mounted, overhead gantry, bridge-mounted, sign bridges, ground-mounted at 2.1 m clearance urban / 1.5 m rural

1.6 Lighting & overhead
~~~~~~~~~~~~~~~~~~~~~~~

-  Streetlights: cobra-head on davit arm (arterials, 9–12 m), post-top/decorative (locals, downtowns, 4–5 m), high-mast (interchanges, 30 m), pedestrian-scale twin-head, wall-mounted; spacing 30–50 m staggered or opposite; on medians for wide arterials
-  Utility poles (wood, 10–12 m) with primary/secondary lines, transformers, comm cables, guy wires, service drops to buildings; typical one side of the street; underground utilities on newer/arterial streets (then: pad-mount transformers, pedestals)
-  Overhead: span-wire signals, trolley/bus catenary, pedestrian bridges, sign gantries, banners, holiday lights

1.7 Transit
~~~~~~~~~~~

-  Bus stop: pole + sign only; bench; shelter (glass, ad panel); far-side/near-side/mid-block; bus bay/pull-out (arterials); bus bulb (curb extension); red bus lanes; "BUS STOP NO PARKING" zone 20–30 m; queue-jump lane; BRT median station
-  Streetcar/LRT: embedded track in street, platform, catenary poles, "trolley" markings, transit signal priority heads (white bar signals)

1.8 Structures & grade separation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  Bridges (girder, arch, truss), overpasses, underpasses, abutments, wingwalls, bridge rails, approach guardrail, expansion joints, pier columns in medians, clearance signs
-  Tunnels: portal, lighting, tile walls, jet fans, no-lane-change solid lines
-  Retaining walls (MSE panels, cast-in-place, gabion), noise walls (freeway), rock cuts, fill slopes
-  Interchanges: diamond, cloverleaf, partial cloverleaf (parclo), trumpet, stack, SPUI, DDI, roundabout interchange; loop ramps, directional ramps, collector-distributor roads, weaving sections, gore areas with crash cushions (yellow barrels / sand barrels / attenuators)
-  Toll plazas, gantry tolling, weigh stations, rest areas, truck runaway ramps, chain-up areas

1.9 Terminal & special geometry
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  Dead end (barricade + "DEAD END"/"NO EXIT" sign, end-of-road object marker)
-  Cul-de-sac (bulb radius 12–15 m; optional center island), hammerhead turnaround, eyebrow
-  Alley entrances (depressed curb, no crosswalk)
-  Driveways, parking lot entrances, gas station aprons
-  Private road signs, gated entries
-  Railroad grade crossings, drawbridges, ferry aprons
-  Diagonal streets meeting the grid (skew intersections 45°, five/six-way intersections, traffic circles — Washington DC, Detroit style)
-  Woonerf / shared street, festival street (removable bollards)
-  Traffic calming: speed hump, speed table, raised crossing, chicane, chokers, mini-roundabout, diverter, neckdown, textured pavement, "traffic calmed area" sign, 30 km/h zone

1.10 Climate / regional variants
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  **Snow (Canada, northern US)**: snowplowable RPMs or none, snow poles/stakes on edges, salt-stained pavement, snow windrows along curb, snowbanks at corners in winter, "snow route" signs, heated bus shelters, wider boulevards for snow storage, plow-damaged sod
-  **Southwest US**: concrete pavement common, no gutters/drainage swales instead, white-painted curbs, gravel medians
-  **Metric vs imperial**: CA speed signs "MAXIMUM 50", US "SPEED LIMIT 35"; CA uses more symbol signs; CA yellow centerlines identical
-  **Age of street**: old (brick underneath, narrow, overhead utilities, no ramps) vs new (wide, underground utilities, ramps, bike lanes)

--------------

Part 2 — Data model
-------------------

::

   RoadTile {
     class:        RoadClass
     flags:        { oneWay: dir?, bikeLane: L|R|both|none, parking: L|R|both|none,
                     transit: none|busLane|streetcar, median: none|painted|raised,
                     bridge: bool, tunnel: bool, elevation: int }
     connections:  bitmask over 8 neighbours (N, NE, E, SE, S, SW, W, NW)
     // derived — recomputed on any change within radius 2
     shape:        END | STRAIGHT | CORNER | TEE | CROSS | DIAG_STRAIGHT | DIAG_CORNER | DIAG_MERGE | ...
     role:         SEGMENT | INTERSECTION | APPROACH | TRANSITION
     intersection: ref → Intersection?      // shared object for multi-tile intersections
   }

   Intersection {
     tiles:        [RoadTile]                // 1 tile for local×local; 2×2 or 3×3 for arterials
     legs:         [{ dir, class, lanesIn, lanesOut, control }]
     control:      UNCONTROLLED | YIELD | STOP_2WAY | STOP_ALLWAY | SIGNAL | ROUNDABOUT
     cornerRadius: per corner
   }

   RoadSegment {  // chain of SEGMENT tiles between intersections
     tiles, class, length, centerline: polyline (smoothed), isDiagonal
   }

Key principle: **the grid stores topology; geometry is generated.** Every visual is a function of ``(tile, neighbours within radius 2, segment, intersection)``. Never store a sprite/mesh choice.

--------------

Part 3 — Dynamic rules
----------------------

3.1 Connectivity → shape classification
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Use an 8-bit neighbour mask, but evaluate orthogonal (N/E/S/W) and diagonal bits separately. A diagonal connection is only valid when *both* intervening orthogonal cells are not road (otherwise it's just an orthogonal corner).

=========== =========================== ==============================================================
Ortho count Diag count                  Shape
=========== =========================== ==============================================================
0           0                           ISOLATED (render as short stub, warn)
1           0                           END
2 opposite  0                           STRAIGHT
2 adjacent  0                           CORNER
3           0                           TEE
4           0                           CROSS
0           1                           DIAG_END
0           2 opposite (NE+SW or NW+SE) DIAG_STRAIGHT
0           2 adjacent                  DIAG_CORNER (90° in diagonal space)
1           1                           DIAG_MERGE (45° bend) — the orthogonal-to-diagonal transition
mixed, ≥3   —                           SKEW_INTERSECTION (route through Intersection object, 5/6-way)
=========== =========================== ==============================================================

Recompute the shape of a tile and all 8 neighbours whenever a tile is placed/removed/reclassed. Recompute intersections within radius 2 (multi-tile intersections extend that far).

3.2 Segment vs intersection
~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  A tile is an **INTERSECTION** if ortho+diag connection count ≥ 3, or if two connections meet at a corner and either neighbour class differs (a class change at a corner is a "junction" for signage).
-  Pure CORNER tiles with same class on both legs are **SEGMENT** (curved).
-  Tiles within ``approachLength(class)`` of an intersection along a segment are **APPROACH** tiles: they get solid lane lines, turn arrows, stop bars, no parking, crosswalk-adjacent curb ramps.

   -  ``approachLength``: LOCAL 1 tile, COLLECTOR 2, ARTERIAL 3–4, HIGHWAY 5.

-  Tiles where class changes mid-segment are **TRANSITION** tiles (taper markings, "LANE ENDS" sign, sidewalk width blends over 1 tile).

3.3 Intersection footprint
~~~~~~~~~~~~~~~~~~~~~~~~~~

Intersection size = f(max class among legs):

-  LOCAL/ALLEY/COLLECTOR: 1×1 tile
-  ARTERIAL_MINOR: 2×2 (if both crossing roads are arterial) else 1×2 along the arterial
-  ARTERIAL_MAJOR/HIGHWAY: 3×3 or 2×3
-  Freeway × anything: **never an at-grade intersection** — force interchange or reject placement.

Merge adjacent intersection tiles into one Intersection object; the whole footprint renders as one continuous pavement patch with no internal lines except: crosswalk-free interior, dotted extension lines guiding turning lanes across wide intersections, and the box junction if ``keepClear`` flag.

3.4 Intersection control selection
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Given legs sorted by class (major = highest class present):

============================================= ========================================================================================================================
Situation                                     Control
============================================= ========================================================================================================================
All legs ALLEY / LOCAL, 3–4 legs              UNCONTROLLED (2-way stop on the leg with lower "street order" if you want realism; many NA cities default to 2-way stop)
LOCAL × COLLECTOR                             STOP_2WAY on the local legs
COLLECTOR × COLLECTOR                         STOP_ALLWAY (or SIGNAL if traffic > threshold)
Anything × ARTERIAL, cross-street ≤ COLLECTOR STOP_2WAY on minor; SIGNAL if minor is COLLECTOR and traffic > threshold
ARTERIAL × ARTERIAL                           SIGNAL, mast arms
ARTERIAL_MAJOR × ARTERIAL_MAJOR               SIGNAL, protected left arrows, dual lefts
Any T-intersection where minor leg is LOCAL   STOP on the stem only
3-leg all same class ≤ COLLECTOR              STOP on stem
Player override                               ROUNDABOUT (needs ≥ 2×2 clear tiles + splitter approach), STOP_ALLWAY, SIGNAL
Alley meeting anything                        no control; alley gets a depressed curb, no crosswalk, sidewalk continues *through* the alley mouth
Driveway/parking-lot entrance                 not an intersection; curb cut only
============================================= ========================================================================================================================

Per leg hardware placement (right-hand side of the approaching driver, at the stop bar):

-  STOP sign: right corner, 1.5–2 m behind crosswalk.
-  Signal: mast arm on the **far-right corner** of each approach (arm extends over the approach it serves), plus a supplemental pole-mounted head near-side. Ped heads on all four poles. Controller cabinet: one corner, on the sidewalk furniture zone, opposite to hydrant.
-  Street name blades: diagonally opposite corners (NE & SW by convention); on mast arms for signalized.
-  Crosswalks: all legs where both sides have sidewalk; on signalized intersections use continental style; on local/uncontrolled use standard two-line or **no marking** (very common on NA locals — only mark at controlled legs and school zones).
-  Stop bar: only on controlled legs (stop or signal). Yield line on yield legs and roundabout entries.

3.5 Corner geometry (curb returns)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For each corner of an intersection:

::

   r = cornerRadius(max(classA, classB))   // ALLEY 3, LOCAL 4.5, COLLECTOR 7.5, ARTERIAL 10.5, TRUCK 15

-  Curb polyline: straight along leg A → circular arc of radius ``r`` centred at (A_edge + r, B_edge + r) → straight along leg B.
-  Sidewalk follows the curb offset by ``sidewalkWidth``; boulevard strip terminates before the arc (corners are paved to the curb).
-  Curb ramps: two per corner, each centred on the projection of its crosswalk; if ``r`` is small (< 5 m) use one diagonal ramp.
-  Catch basin: at the upstream start of each curb return (the tangent point, before the arc).
-  Bulb-outs (when ``parking`` present on both legs): extend the curb out into the parking lane for ``parkingWidth`` over the approach length, keeping the same ``r``.
-  Channelized right turn island: when max class ≥ ARTERIAL_MAJOR and ``r`` ≥ 15, replace the arc with a large-radius (25–30 m) slip lane + triangular island with its own ramp and crosswalk.

3.6 Lane assembly on approach tiles
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For each approach leg, from centerline outward:

1. Compute base lanes from class.
2. Add **left-turn pocket** if: class ≥ COLLECTOR and the intersection has a leg to the left and (median is painted/raised or lanes ≥ 4). Taper 1 tile, storage 1–2 tiles. Dual left if ARTERIAL_MAJOR × ARTERIAL_MAJOR.
3. Add **right-turn pocket** if class ≥ ARTERIAL_MINOR and right leg exists and parking is absent (parking lane converts into the right-turn lane over the approach length — "parking drops for turn lane").
4. Lane arrows: placed 15 m and 45 m before the stop bar in each lane. Through-lanes get no arrow unless a neighbour lane is turn-only (then through gets a straight arrow). Turn-only lanes also get "ONLY" stencil.
5. Lane lines become **solid** over the approach length; the last dashed segment ends exactly at a 3 m boundary.
6. Centerline becomes **double solid yellow** over the approach length (no passing near intersections) even on locals that otherwise have no centerline.
7. Bike lane: dashed over the last 15 m (merge zone), then bike box at signals (green), or drops into a green conflict strip through the right-turn pocket.

3.7 Straight segment rules
~~~~~~~~~~~~~~~~~~~~~~~~~~

**Markings by class** (longitudinal, in order from center):

-  ALLEY: nothing.
-  LOCAL: no centerline (< 6 m paved width) — but *add* dashed yellow if width ≥ 6 m or if street has bus route; no lane lines; optional parking Ts.
-  COLLECTOR: dashed yellow centerline; solid where sight distance low (hills, curves — see 3.10); edge line only if no curb.
-  ARTERIAL: double yellow (or TWLTL pair if ``median==painted``, or nothing in the median zone if ``median==raised``); dashed white between same-dir lanes; solid white bike lane line; parking lane line.
-  HIGHWAY: double yellow / dashed yellow with passing zones; solid white edge lines; rumble strips outside edge lines.
-  FREEWAY: dashed white lane lines; solid white right edge; solid **yellow** left edge (against the median); RPMs every 12 m on lane lines.
-  ONEWAY: no yellow at all — all lane lines white dashed; parking both sides on locals.

**Marking phase continuity**: dash pattern must be continuous across tiles. Compute along-segment arc-length ``s`` from the nearest intersection stop bar; dash is drawn where ``(s mod 12) < 3``. Never restart the pattern per tile.

**Furniture placement along a segment** (parameterised by arc-length so it works on diagonals/curves too):

-  Streetlights: every 35 m (LOCAL/COLLECTOR, one side, alternate sides every segment) / 30 m staggered both sides (ARTERIAL) / median 40 m (ARTERIAL_MAJOR). Never within 3 m of a driveway or hydrant.
-  Utility poles (if ``overheadUtilities`` on the segment): every 40 m, one side, same side as streetlights (streetlights mount on them). Underground on ARTERIAL_MAJOR+ and any tile with ``age == new``.
-  Hydrants: every ~120 m alternating sides; always one within 60 m of each intersection corner.
-  Street trees: every 8–10 m in the boulevard strip when ``sidewalkWidth + buffer ≥ 2.4 m``; none on ALLEY/HIGHWAY; skip 6 m either side of driveways and 10 m from corners (sight triangle).
-  Catch basins: every 90 m and at every corner tangent point; on the low side if elevation differs.
-  Manholes: every 90–120 m, alternating lanes, offset from centerline by 1 m.
-  Parking meters/pay stations: pay station every 8 stalls when ``parking && class ≥ COLLECTOR && zone == commercial``.
-  Bus stops: at far side of every 2nd–3rd intersection on routes; shelter if arterial or ridership > threshold; 25 m no-parking zone before the stop pole.
-  Signs mid-block: speed limit sign after every intersection on arterials (right side, first pole); "NO PARKING" every 60 m in no-parking zones; "DEAD END" at the start of any segment that ends in END.
-  Crown: pavement height +2% × halfWidth at centerline (use in normal map / vertex offsets).

**Driveways** (when adjacent tile is a building/lot): one curb cut per lot frontage centred on the lot, 3.5 m residential / 7 m commercial, breaks the boulevard strip and parking Ts, never within 3 m of a corner ramp.

3.8 Right-angle corners (single CORNER tile, same class both legs)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A 90° bend on the grid is a *curve*, not an intersection. Two acceptable renderings — pick per class:

-  **Tight fillet (LOCAL/ALLEY, 1 tile)**: inside curb arc radius ``r_in = 0.25 × tile``, outside curb radius ``r_out = r_in + ROW``. Centerline = arc with radius ``r_in + ROW/2``. Markings follow the centerline arc (double solid yellow through the curve). Chevron/curve sign on outside of bend for COLLECTOR+.
-  **Wide sweep (COLLECTOR+, uses 2×2 or 3×3 tiles)**: when the player draws an L with ≥ 2 tiles of clearance, or via a "smooth" tool, consume the inside corner tile(s) as road and render a large-radius curve: ``R = 1.5–2.5 tiles``. Superelevate 2–4%. "Curve ahead" warning sign + advisory speed plaque on both approaches for HIGHWAY. Inside of the curve gets no parking; outside gets guardrail on
   HIGHWAY.

Geometry: build the centerline as ``line → arc → line``, then **offset** it for every parallel feature (lane lines, curb, sidewalk edges). Use proper arc offsetting (same centre, radius ± d), not vertex offsetting, or inner lines will self-intersect at small radii. Discard any offset curve whose radius would go ≤ 0 (inner sidewalk edge on a tight bend) and replace with a point.

3.9 Diagonals
~~~~~~~~~~~~~

Support **true 45° roads**, not staircases. A run of diagonal-connected tiles forms one DIAG segment.

-  Tile footprint: a 45° road of width ``w`` passes through a tile as a band; the tile's pavement mesh is the intersection of that band with the tile square (a hexagon or rectangle). Neighbours along the diagonal are clipped identically so they seam perfectly. Corners of the tile outside the band are "leftover" land — assign to adjacent lots or render as sidewalk/boulevard/verge.
-  **The width problem.** A 45° band of full width ``w = tile`` needs ``tile × √2`` of grid extent, so it always overflows the diagonal chain of tiles by ~0.21 tile per side into the neighbours' corners. Conversely, the widest band that fits *inside* a one-tile staircase (see 3.10 step 4) is only ``tile / √2 ≈ 0.71 tile`` — a road that is 10 m on the straight would be 7 m on the diagonal. Diagonal
   width is therefore an explicit policy, keyed by class:

   -  ``FIT`` (default for ALLEY/LOCAL): pavement = inscribed band; sidewalks/boulevard absorb the sawtooth teeth. Works when ``pavementWidth ≤ 0.71 × tile``, which is normally true for a 2-lane local whose tile is the full ROW.
   -  ``ENCROACH`` (default for COLLECTOR and up): pavement keeps its orthogonal width; the band clips into neighbour tiles' corners. Those neighbours stay non-road but get a ``roadEncroached`` polygon that lots must subtract. Placement warns if a neighbour tile has an existing building in the encroached area.
   -  ``THICK`` (optional, or forced for ARTERIAL_MAJOR/HIGHWAY): require a two-tile-thick diagonal strip; inscribed band = 1.41 tile, so no encroachment. Reject or auto-widen the draw otherwise.
   -  Never let the pavement width silently shrink on a diagonal — a neck-down at the merge is the one thing players will always notice.

-  Length per tile along the diagonal = ``tile × √2`` — use this in arc-length for dash phase and furniture spacing.
-  **DIAG_MERGE tile (ortho ↔ diagonal, 45° bend)**: centerline = ``line → arc(45° sweep) → line``. Radius: ``R = min(1 tile, r_by_class)``. Tangent points land on the tile edge midpoint (orthogonal side) and the tile corner (diagonal side). Everything else is the standard offset-of-centerline machinery. Curve warning sign for COLLECTOR+.
-  **DIAG_CORNER (45°→ the other 45°, i.e., a 90° turn in diagonal space)**: same as 3.8 but rotated 45°; the arc is centred on the tile corner.
-  **Diagonal meets orthogonal grid road (skew intersection)**: force an Intersection object covering the tiles the diagonal band overlaps (typically 2–3). Acute corners (45°) get a **minimum radius** enforced (``r ≥ 6 m``) and the acute-side corner is often replaced with a pork-chop island. Obtuse corners get the standard radius. Crosswalks are drawn *perpendicular to the leg they cross*, not
   aligned to the grid. Signals prefer span-wire or a single-pole "box span" because mast-arm geometry gets ugly on skew.
-  **Two diagonals crossing** (X): a normal 4-leg intersection rotated 45°; footprint 2×2.
-  **Five/six-way (diagonal crossing an orthogonal cross)**: always SIGNAL or ROUNDABOUT; auto-suggest roundabout; generate a large single pavement patch, corner radii large, at least two legs get "NO LEFT TURN".
-  Sidewalks on diagonals: run parallel to the road, not along the grid; buildings on the leftover triangular lots face the diagonal (flatiron lots).

3.10 Curves, smoothing and straightening (the geometry pipeline)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1.  **Extract centerline polyline** for each RoadSegment from tile centres (ortho tiles contribute their centre; diagonal tiles their centre; intersection tiles contribute the intersection centroid as the segment endpoint).
2.  **Classify bends** at each polyline vertex by turn angle: 0° (straight), 45° (diag merge), 90° (corner), 135° (only on a staircase — see 4).
3.  **Fillet every bend** with radius ``R(class, angle)``; clamp so that consecutive fillets don't overlap (``R ≤ 0.5 × min(adjacent edge lengths) / tan(θ/2)``). If the clamp gives ``R`` below the class minimum, either (a) render as tight fillet and add a "sharp curve" sign, or (b) flag to the player ("road too tight for arterial").
4.  **Staircase straightening**: if the polyline alternates 90° turns every 1 tile (N, E, N, E…), replace the run with a single 45° diagonal segment (or a spline through the vertices for 2:1 / 3:1 slopes — a 26.6° road). Do this at generation time so the player can "draw" a rough line and get a clean road. Provide a toggle: ``snapDiagonals: 45only | anyAngle``. For ``anyAngle``, build a
    Catmull-Rom or clothoid-approximated spline through the tile centres and clip per tile as in 3.9.

    -  **Construction**: the staircase tiles fall on two parallel diagonals — one through the "outer corner" tiles, one through the "inner corner" tiles. The centerline is their midline (exact, not least-squares). The inscribed band's edges are the lines through the stair's inside corners; its perpendicular width is ``tile / √2`` for a 1:1 stair, ``tile / √5`` for 2:1, ``tile / √10`` for 3:1.
    -  **Ownership**: every tile the player drew stays a road tile and owns the segment. Pavement is the band; the sawtooth leftovers inside those tiles become sidewalk/boulevard/verge. Apply the width policy from 3.9 (``FIT`` / ``ENCROACH`` / ``THICK``) to decide whether the pavement is the inscribed band or overflows it.
    -  **Ends**: the first and last stair tiles are where the band exits at a tile edge rather than a corner. Treat them as DIAG_MERGE tiles: fillet from the orthogonal segment into the diagonal, and if the diagonal pavement is narrower than the orthogonal one (``FIT`` policy), taper the width linearly over that tile — a step change in curb offset is never acceptable.
    -  **Ambiguity**: a single N,E pair is a corner, not a staircase; require ≥ 2 full periods (N,E,N,E) before straightening, and keep a per-segment ``straightened: bool`` so the player can revert.

5.  **Superelevation / crown**: apply crown on straights, blend to superelevation over the fillet's tangent runout (one tile before the arc).
6.  **Offset the centerline** to produce every longitudinal feature: ``offset(d)`` for d in {lane edges, bike lane, parking line, curb face, curb back, sidewalk outer edge}. Handle joins with arc-offset on arcs and miter/round on straights. Cull degenerate inner offsets.
7.  **Arc-length parameterisation**: everything periodic (dash pattern, RPMs, lights, poles, trees, hydrants, manholes) is placed by walking ``s`` along the centerline and projecting to the offset curve. This is what makes diagonals and curves look identical to straights.
8.  **Cross-section sampling**: at every ``Δs`` (0.5–1 m) sample the centerline, build the cross-section profile (elevations for crown, curb heights, sidewalk), and loft a mesh strip. Split UVs at each feature boundary so textures don't smear.
9.  **Clip per tile**: intersect the lofted geometry with each tile's square so the tile remains the unit of load/save/edit; cache the result on the tile.
10. **Intersection patches**: for each Intersection, compute the union polygon of all leg pavements extended to the intersection centre, then union with the corner-return arcs (3.5). Triangulate (ear-clip). Curb/sidewalk follow the polygon boundary outward. Legs' longitudinal features terminate at the stop bar / crosswalk edge; markings inside the patch: none, except turn-guidance dotted lines
    (when the patch is > 25 m across) and optional box junction.
11. **End caps**: END tile → for LOCAL, render a cul-de-sac bulb if the tile's three non-road neighbours are free (``R = 12 m``), else a hammerhead; for others, a barricade + object marker; sidewalk wraps around the bulb.

3.11 Elevation, bridges, tunnels, grade
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  Elevation per tile is an integer level; road interpolates linearly between tile centres. Max grade by class: LOCAL 12%, COLLECTOR 10%, ARTERIAL 8%, HIGHWAY 6%, FREEWAY 4%. Reject or warn above.
-  Crest/sag vertical curves: fillet the *profile* polyline the same way as the plan polyline (R_v = 20–60 m by class). Crest curves with limited sight → centerline becomes **solid yellow** (no passing) for 2 tiles either side; "HILL" sign if grade ≥ 8%.
-  A road tile ≥ 1 level above terrain (or above another road) is a **bridge**: replace sidewalk boulevard with bridge rail; add abutments at the transition tiles, piers every 2–3 tiles in the span; lane lines solid on the bridge; "NARROW BRIDGE" or clearance sign. Road below gets a clearance sign and (for freeway) the piers land in the median or outside the shoulders.
-  A road tile ≥ 1 level below terrain covered by terrain/road above is a **tunnel**: portal tiles, interior lighting, solid lane lines, no parking, no sidewalks (or protected walkway), reduced speed.
-  **Ramps** (FREEWAY ↔ ARTERIAL): a RAMP-class diagonal or curved segment that starts as a lane-drop on the freeway (wide dotted line, gore chevrons, exit sign 1 tile before and at the gore), curves to meet the arterial at a signalized or stop-controlled T. Enforce merge/diverge taper length ≥ 3 tiles. Standard interchange templates (diamond = 4 ramps in a #-shape; parclo = 2 loops) can be
   stamped as a macro that emits tiles.
-  Railroad crossing (rail tile ∩ road tile): see 3.15.

3.12 Class transitions and one-way
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  Class increases along a segment: over 1–2 TRANSITION tiles taper the extra lane in with a ``taper = laneWidth × speed/...`` (just use 1 tile), "LANE ENDS MERGE LEFT" sign when decreasing, dotted extension through the taper.
-  Sidewalk width change: blend over the transition tile; boulevard strip appears/disappears at a driveway or corner, never mid-slab.
-  ONEWAY start: "ONE WAY" arrow signs on both corners facing the correct way, "DO NOT ENTER" + "WRONG WAY" on the far end facing the wrong way; parking allowed both sides on locals; all lines white.
-  Two-way ↔ one-way junction: treat as intersection legs with ``lanesIn = 0`` on the forbidden direction; signal phases drop that movement.

3.13 Rendering layer order (bottom → top)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1.  Terrain / lot fill
2.  Pavement base (asphalt/concrete) with crown normal map
3.  Pavement decals: patches, cracks, ruts, joints, manholes, valve covers, detector loops
4.  Longitudinal markings (yellow first, then white so white-over-yellow gore areas look right)
5.  Transverse markings, stencils, arrows, crosswalks, bike boxes, coloured lanes (red/green)
6.  Wear decals over markings (tire marks at stop bars, faded centers on old streets)
7.  RPMs
8.  Gutter, curb, curb ramps, tactile plates, driveway aprons
9.  Sidewalk, boulevard, tree pits/grates
10. Median (raised) + median plantings
11. Vertical furniture: poles, signs, lights, hydrants, meters, benches, bins, shelters, cabinets, bollards, guardrails, barriers
12. Overhead: mast arms, signal heads, span wires, utility lines, gantries, catenary
13. Trees (canopy)
14. Snow/weather overlay, puddles in ruts, wet reflection

3.14 Validation rules (reject or warn at placement)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  FREEWAY may not touch any non-freeway/ramp tile at grade.
-  No intersection spacing closer than 1 tile on ARTERIAL (drop to driveway/right-in-right-out).
-  Minimum curve radius by class; minimum segment length between reverse curves = 1 tile of tangent.
-  Grade limits (3.11). Bridges need ≥ 1 tile of abutment approach at each end.
-  A tile can't be both a signalized intersection footprint and a driveway.
-  Roundabout needs a clear 2×2 (LOCAL/COLLECTOR) or 3×3 (ARTERIAL) footprint and ≤ 6 legs.
-  Railway crossing rules per 3.15 (no freeway at-grade, min angle, spacing from intersections).

--------------

3.15 Railway crossings
~~~~~~~~~~~~~~~~~~~~~~

A crossing exists wherever a rail tile and a road tile coincide at the same elevation. It is its own object (``RailCrossing``), not an intersection: rail always has priority and the road is the one that gets controlled.

::

   RailCrossing {
     roadSegment, railSegment
     railKind:   MAINLINE | BRANCH | SPUR_INDUSTRIAL | LRT_SEPARATE | STREETCAR_INSTREET
     tracks:     int                  // parallel rail tiles merged into one crossing
     angle:      degrees (90 = square)
     control:    PASSIVE | FLASHERS | GATES | GATES_FULL | GRADE_SEPARATED
     interconnected: bool             // linked to a nearby traffic signal
   }

**Control selection** (highest of the two rules wins):

============================== ====================================================== ========================== ===========================================
Road class \\ Rail kind        SPUR_INDUSTRIAL                                        BRANCH                     MAINLINE
============================== ====================================================== ========================== ===========================================
ALLEY / driveway               PASSIVE (crossbuck)                                    PASSIVE + STOP sign        FLASHERS
LOCAL                          PASSIVE                                                FLASHERS                   GATES
COLLECTOR                      FLASHERS                                               GATES                      GATES
ARTERIAL_MINOR                 GATES                                                  GATES                      GATES (+ median gates if ``median`` raised)
ARTERIAL_MAJOR                 GATES + median                                         GATES_FULL (four-quadrant) GRADE_SEPARATED preferred; else GATES_FULL
HIGHWAY                        GATES                                                  GATES                      GRADE_SEPARATED
FREEWAY / RAMP                 GRADE_SEPARATED — at-grade crossing is rejected                                   
STREETCAR_INSTREET on any road not a crossing; handled as in-street track (see below)                            
============================== ====================================================== ========================== ===========================================

Upgrades: ``tracks ≥ 2`` → at least FLASHERS and a "2 TRACKS" plaque; angle < 60° → one control level higher; a signalized intersection within 60 m → ``interconnected = true`` (signal pre-emption) and a queue-cutter signal on the near side.

**Geometry**

-  Preferred angle 90°; allowed 60–90°; below 60° reject placement or auto-insert a 45° road jog so the road crosses square (the tool should offer "square up crossing"). Skew crossings extend the crossing surface so it covers the full road width at the skew.
-  Road profile across the rails: flat for 0.6 m either side of the outer rails, then grade ≤ 5% for 30 m (a hump crossing high-centres vehicles — the classic "LOW GROUND CLEARANCE" case gets a warning sign if terrain forces it).
-  Crossing surface: full road width plus sidewalks. Material by rail kind: rubber panels (mainline arterial), timber planks (branch/spur, local), concrete panels (LRT), embedded with flangeway groove (streetcar). Panel width = 2.4 m per track, extended to ``tracks × trackSpacing``.
-  Multiple parallel rail tiles ≤ 1 tile apart merge into one crossing with one set of hardware; beyond that they are separate crossings (each with its own signs).
-  No intersection, driveway, or bus stop within 1 tile of the crossing (queue storage). If the player forces one, emit "DO NOT STOP ON TRACKS" sign + hatched keep-clear box across the tracks.
-  Sight triangle for PASSIVE crossings: no buildings/trees in the quadrant triangle 30 m road × 100 m rail; otherwise auto-upgrade to FLASHERS.

**Pavement markings** (both approaches; none on ALLEY/driveway; none where a stop sign faces the crossing and speed < 40)

-  RXR stencil + large "X" at 15–30 m before the nearest rail, centred in each approach lane.
-  Solid centerline (double yellow, or solid white lane lines on one-way / multilane) from the RXR stencil to the stop line; no passing, no lane changes.
-  Stop line (white, 300–600 mm) 4.5 m from the nearest rail, perpendicular to the road. For PASSIVE crossings with a STOP sign only; for FLASHERS/GATES always.
-  Optional white "dynamic envelope" box lines 2 m outside each rail on multi-lane arterials.
-  No parking 15 m either side (curb paint / signs).

**Signage per approach** (right side, plus left side/median on multilane)

-  Crossbuck (W10-1, white X, "RAILROAD CROSSING" US / symbolic in CA), reflective post stripes, "n TRACKS" plaque if ``tracks ≥ 2``, YIELD or STOP plaque under crossbuck at PASSIVE crossings (STOP if sight triangle failed).
-  Advance warning (yellow circle W10-1 US / diamond symbol CA) at 1 tile before, plus advisory speed if approach grade or curve limits sight.
-  Emergency notification sign (blue, phone number + crossing ID) on the crossbuck post — small but ubiquitous in NA.
-  "DO NOT STOP ON TRACKS", "NO TURN ON RED" at interconnected signals, "EXEMPT" plaque on dead spurs.

**Active hardware** (FLASHERS and up)

-  Flasher mast on the right of each approach: two alternating red lamps + bell + crossbuck on the same post; cantilever arm with extra lamps over the roadway when lanes ≥ 3 or parking obstructs sight.
-  Gate arm (red/white stripes, three lamps) on the same mast, length = approach half-width (to the centerline). ``GATES_FULL``: exit gates on the far side too. Median gates when a raised median exists.
-  Pedestrian gates/swing gates and separate flashers at each sidewalk when sidewalk present; tactile plates 0.6 m before the crossing surface; "LOOK" stencil on the sidewalk.
-  Signal bungalow (silver box, ~1 tile away on the rail side, fenced) and a track-circuit "island" that visually is just a small cabinet; gate mechanism cabinets at each mast base.
-  Sequence: flashers on → 3 s bell → gates descend over 10 s → train → gates rise → clear. Road traffic sim treats it as a red signal on all approaches; pedestrians the same.

**Multi-track crossings**

-  Merge rule: parallel tracks whose centre spacing is ≤ 1 tile (and ≤ ~20 m in world units) form one ``RailCrossing`` with ``tracks = n``; beyond that they are separate crossings, each with its own stop lines and masts, plus "DO NOT STOP ON TRACKS" and a hatched keep-clear box between them.
-  Stop lines are measured from the nearest rail of the track nearest *each* approach, so they are asymmetric; RXR stencils, solid lines, no-parking zones and mast positions all slide with their stop line.
-  One continuous crossing surface spans all tracks — panels between tracks, headers only at the outer edges, never an asphalt gap a vehicle could stop in. Ballast band width outside the road = ``(tracks − 1) × trackSpacing + 4.5 m``.
-  Control floor: ``tracks ≥ 2`` → at least FLASHERS on any road class; "n TRACKS" plaque under every crossbuck including the pedestrian-gate crossbucks.
-  Second-train logic: gates stay down while *any* track's approach circuit is occupied; a train clearing one track does not raise the gates if another is approaching. Add a "second train" warning sign facing each sidewalk; at busy crossings an audible/LED "ANOTHER TRAIN COMING" annunciator. Expect longer average gate-down times and expose that in the sim so it reads as intended rather than as a
   stuck gate.
-  No pedestrian refuge or hardware between tracks; ped gates stay on the outer approach sides.
-  Arm lengths, cantilever logic and median flashers are keyed to lanes, not tracks — unchanged.
-  ``tracks ≥ 3`` on COLLECTOR or above: prefer GRADE_SEPARATED; warn on placement. A crossing may not include a station platform between the tracks.

**Rail kinds that are not "crossings"**

-  ``STREETCAR_INSTREET``: track runs in the road centre lanes. Rules: embedded rail with flangeways, no crossbucks or gates; "TROLLEY" or streetcar-symbol stencils; solid white line 0.3 m outside the dynamic envelope; transit signal priority heads (white bar) at intersections; platforms in the median or curb bulbs; bike lanes never cross the rails at < 60° (auto-jog the bike lane). Intersections
   with in-street track just add the white-bar heads.
-  ``LRT_SEPARATE`` (own right-of-way, in a median or alongside): crossings occur only where the road turns across it — treat as ``GATES`` with concrete panels, but flashers are the transit-style ones and the signal is interconnected by default.

**Grade separation**

-  Any crossing whose selection resolves to GRADE_SEPARATED is built as a bridge or underpass per 3.11: prefer road-over-rail if terrain allows (needs 2 tiles of approach each side at ≤ 6%); otherwise rail-over-road with a clearance sign and abutments. Reject placement rather than draw an at-grade FREEWAY crossing.

**Validation**

-  Reject: FREEWAY/RAMP at-grade; angle < 30°; crossing tile inside an intersection footprint; crossing on a bridge/tunnel tile.
-  Warn: angle 30–60°; intersection or driveway within 1 tile; approach grade > 5%; ``tracks ≥ 3`` on COLLECTOR+; another crossing on the same road within 2 tiles (should be one crossing).

3.16 Traffic signal operation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

3.4 places the hardware; this section defines what the signal *does*. Model every signalized intersection with the standard NEMA ring-and-barrier structure — it is what every NA controller actually runs, and it makes phasing, head assignment and timing all fall out of one table.

**Movements and phase numbers (NEMA 8-phase)**

===== ============================ ===== ============================
Phase Movement                     Phase Movement
===== ============================ ===== ============================
1     Southbound left              5     Northbound left
2     Northbound through (+ right) 6     Southbound through (+ right)
3     Westbound left               7     Eastbound left
4     Eastbound through (+ right)  8     Westbound through (+ right)
===== ============================ ===== ============================

-  Ring 1 = phases 1-2-3-4; Ring 2 = 5-6-7-8. A *barrier* separates {1,2,5,6} (main street) from {3,4,7,8} (side street). Phases in the same ring run sequentially; phases in different rings can run together only if they're on the same side of the barrier (so 1+5, 1+6, 2+5, 2+6 are legal pairs; 2+4 is never).
-  Ped phases: P2/P6 walk with 2/6, P4/P8 with 4/8. Overlap phases (A–D) drive right-turn arrows that run with the non-conflicting left (e.g. northbound right-turn arrow runs with phase 3).
-  Assign phases from the intersection's legs: main street = the higher-class pair of legs (``2/6`` axis); if legs are missing (T intersection) the absent phases are simply unused. A 5-leg skew gets a custom phase list — just allow up to 16 phases and let the barrier be user-defined.

**Left-turn treatment per approach** (decides head type and phase sequence)

========================================================= ======================================================================== =================================================================
Condition                                                 Treatment                                                                Head
========================================================= ======================================================================== =================================================================
No left-turn lane, or LOCAL/COLLECTOR cross street        Permissive (yield on circular green)                                     3-section ball
Left-turn lane, opposing lanes ≤ 2, moderate volume       Protected/permissive                                                     4-section flashing yellow arrow (FYA) — the modern NA default
Left-turn lane, opposing lanes ≥ 3 or speed ≥ 70          Protected/permissive with FYA, lead-lag allowed                          4-section FYA
Dual left-turn lanes                                      Protected only                                                           3-section arrows (R/Y/G arrows) + "LEFT ON GREEN ARROW ONLY" sign
Left across LRT/streetcar or raised median without cutout Protected only, or prohibited ("NO LEFT TURN")                           arrows or none
Opposing approach one-way toward you                      No opposing traffic → permissive doesn't conflict; run left with through ball
========================================================= ======================================================================== =================================================================

Lead vs lag: default leading lefts (both lefts first, then throughs). Use lag on the coordinated direction for progression; never lag a permissive-only left (yellow trap) — that's precisely what FYA exists to avoid, so FYA approaches may lag freely.

**Right turns**: default permissive on green + right-on-red (unless "NO TURN ON RED", which is set when ped volume is high, the intersection is skew, or a right-turn overlap with a leading ped interval exists). Channelized right-turn slip lanes are yield-controlled and unsignalized unless there's a dedicated right-turn arrow overlap.

**Signal head assignment per approach** (what to render on the mast arm)

-  One head per through lane minimum, two heads minimum per approach, at least one head over the lane it controls, positioned 12–55 m from the stop line (near-side pole head as supplemental).
-  Head per lane type: through → 3-section ball; shared through/left → ball only (never an arrow head over a shared lane); exclusive left → per table above; exclusive right → ball or, with an overlap, 3-section arrows; bike lane → bike signal head (3-section with bike symbols) where a bike box or protected lane exists; transit → white-bar head.
-  Backplates with yellow reflective border on ARTERIAL_MAJOR and highways; louvers/visors default; programmable-visibility heads on closely spaced signals.
-  Pedestrian heads: countdown "walk/hand" on every crosswalk pole; push buttons (with APS audible/vibrotactile) unless ``recall = ped`` (downtown: walks come up every cycle without a button).

**Detection** (determines actuated vs pretimed behaviour)

-  Sawcut inductive loops at the stop line (6×6 ft, one per lane) plus advance loops 60–100 m back on approaches ≥ 60 km/h; or video/radar detectors on the mast arm on newer intersections. Bike loops (diamond stencil) in bike lanes.
-  Downtown grids (``zone == CBD``, spacing ≤ 2 tiles): **pretimed**, no detection, everything on recall.
-  Elsewhere: **semi-actuated** — main street on recall (rests in green), side street and lefts called by detectors. **Fully actuated** on isolated ARTERIAL×ARTERIAL and highway intersections.
-  Preemption inputs: railway crossing within 60 m (3.15), fire station on an approach (emergency preemption with confirmation beacon on the mast arm), transit signal priority on bus/LRT routes.

**Timing rules** (per phase; defaults shown, all scaled if your sim time isn't 1:1)

-  Minimum green: 5–7 s through, 4–5 s left, 10–15 s if the ped phase is on recall.
-  Passage/gap time (actuated): 2–3 s; max green: 30–60 s side, 45–90 s main.
-  Yellow change: **speed_kmh / 16 + 1** clamped 3–6 s (40 → 3.5, 60 → 4.5, 80 → 5.5).
-  All-red clearance: ``(intersection width + vehicle length) / speed`` → 1–2 s normal, 2–3 s on wide arterials, longer on skew.
-  Pedestrian: WALK 7 s; flashing DON'T WALK = crossing distance / 1.0–1.2 m/s (a 4-lane 15 m crossing ≈ 13–15 s); solid DON'T WALK through yellow+all-red. Ped clearance often forces the minimum green for the parallel through phase.
-  Leading pedestrian interval (LPI): 3–7 s walk before the parallel green where ped volume is high or right-turn conflict is bad; pair with NO TURN ON RED.
-  Cycle length: 60–90 s locals/collectors, 90–120 s arterials, 120–150 s major arterials, ≤ 180 s hard cap. Pretimed CBD grids: 60–75 s.
-  Night/low volume: flash mode (yellow on main, red on side) — optional, common on collectors 23:00–06:00.

**Coordination (green wave)**

-  Any run of ≥ 3 signals on the same ARTERIAL segment within 300–800 m spacing is a coordinated corridor: common cycle length = max of the individual cycles; each signal gets an *offset* = distance from the upstream signal / progression speed (the posted speed) so a platoon arrives on green. Two-way progression on a uniform-spacing grid works when ``spacing = speed × cycle / 2``; otherwise favour
   the peak direction (inbound AM, outbound PM) with time-of-day plans.
-  Coordinated phases (2/6) are the ones that rest in green and take the leftover time; uncoordinated phases are max-limited.
-  Show it: on a coordinated corridor the "signal ahead" timing is deterministic, so a time-space diagram is derivable and the sim can render platoons.

**Alternative control types**

-  Roundabout: no signal; yield lines on entries; pedestrian crossings on splitter islands one car-length back from the yield line, RRFB optional.
-  Pedestrian signals mid-block: HAWK/PHB (dark → flashing yellow → solid yellow → solid red → alternating flashing red) or RRFB (uncontrolled, flashing amber beacons). Full mid-block signal where LRT platform access crosses an arterial.
-  Flashing beacon intersections (red on minor / yellow on major) — rural, replaces all-way stop before warranting a signal.
-  Signal warrants (when to auto-suggest upgrading STOP → SIGNAL): combined minor-street volume, ped volume, crash history, school crossing, or being on a coordinated corridor; ARTERIAL×COLLECTOR always qualifies.

**Simulation contract**: each intersection exposes ``phaseState(t) → {phase: GREEN|YELLOW|RED|FYA, ped: WALK|FLASH|DONT_WALK, per movement}``; vehicles look up their movement's indication, not the intersection's "colour". Preemption and TSP are just forced phase calls with a hold. This keeps the road, rail-crossing (3.15) and transit rules all speaking the same language.

--------------

Part 4 — Recommended implementation order
-----------------------------------------

1. Bitmask classification + 1×1 intersections + straight/corner/T/X for LOCAL and ARTERIAL (two classes is enough to force all the class-based branching).
2. Centerline → offset → loft pipeline with arc-length parameterisation (get dash phase right first; everything else reuses it).
3. Intersection patch union + corner returns + stop bars/crosswalks + control selection; signal phasing and timing per 3.16 once vehicles exist.
4. Diagonals (band clipping, DIAG_MERGE fillet, skew intersections).
5. Furniture placement pass (single arc-length walk per segment, one rule table).
6. Elevation/bridges/tunnels, then ramps as a macro; railway crossings (3.15) once rail tiles exist.
7. Rail (Part 5): reuse the centerline/offset/band pipeline; add the turnout node type, multi-tile arc claiming, and the signal-block pass.
8. Raised highways (Part 7): air/ground occupancy split first, then ramps as profiles with air claims, then the connector primitive (run–curve–run) and per-movement interchange assembly.
9. Wear, climate, age variants as decal/material swaps keyed off segment metadata.

--------------

Part 5 — Railways
-----------------

Same philosophy as roads: tiles store topology and class, geometry is generated from a centerline. The big difference is that rail **cannot bend sharply** — there is no rail equivalent of a one-tile corner — so the placement tool has to enforce minimum radii and generate curves that span several tiles, and branching happens only through turnouts, never through T-intersections.

5.1 Rail classes
~~~~~~~~~~~~~~~~

=================== ============= ======= ===================== ========================== ===================== ===========================================
Class               Tracks        Speed   Gauge/rail            Ballast                    Electrification       Notes
=================== ============= ======= ===================== ========================== ===================== ===========================================
``SPUR_INDUSTRIAL`` 1             15–25   light rail (jointed)  thin, dirty, weeds         none                  Serves factories/warehouses, ends at bumper
``YARD``            many parallel 10–15   light                 shared flat ballast        none                  Ladders, classification tracks, no signals
``BRANCH``          1             40–60   medium                standard                   none                  Passing sidings every 8–15 km
``MAINLINE``        1–2           80–160  heavy, CWR            full profile, clean        optional (rare in NA) Signalled, superelevated curves
``HIGH_SPEED``      2             200+    heavy, slab or CWR    slab track or deep ballast catenary              Fully fenced, grade-separated, huge radii
``COMMUTER``        2             100–130 heavy                 standard                   optional              Mainline rules + frequent platforms
``LRT``             2             50–80   girder or T-rail      ballast (own ROW) or slab  catenary 750 V        Median or separate ROW, low platforms
``STREETCAR``       1–2           30–50   girder rail, embedded none (in pavement)         catenary 600 V        Uses road tile; see 3.15
``SUBWAY``          2             60–100  heavy, third rail     slab/tunnel                third rail            Tunnel or elevated only
``HERITAGE``        1             20–40   light                 grassy                     none                  Tourist line, timber trestles
=================== ============= ======= ===================== ========================== ===================== ===========================================

Track centre spacing (multi-track): 4.0 m mainline, 4.3 m if ≥ 100 km/h, 5.0 m high speed, 3.6 m yard. Right-of-way width: single 15–20 m, double 25–30 m, fenced on MAINLINE/HIGH_SPEED/COMMUTER.

5.2 Inventory
~~~~~~~~~~~~~

**Track structure**

-  Subgrade/embankment (fill) or cutting; drainage ditches both sides; sub-ballast; ballast shoulder (crushed rock, 0.3 m beyond tie ends, 2:1 slope); ties (timber, creosote-dark on older lines; concrete on mainline/HSR/LRT; 0.5–0.6 m spacing); tie plates, spikes/clips (Pandrol e-clips); rail (jointed with fishplates every 12 m on older lines; continuous welded rail on mainline); guard rails on
   bridges and sharp curves; check rails; rail lubricators on curves; expansion joints; insulated joints at signal blocks; slab track (HSR, tunnels); embedded girder rail with flangeway (streetcar)
-  Turnout (switch): points, frog, guard rails, switch stand (hand-throw, target/lantern) or switch machine (powered, in a box beside the points), point heater (northern climates), derail device (at spurs joining a main), turnout numbers #8 (yard/spur, ~1:8 diverging) #10–12 (branch/siding), #15–20 (mainline high-speed), #24+ (HSR)
-  Crossover (two turnouts between parallel tracks), double crossover (scissors), single/double slip switches (rare, urban terminals), diamond crossing (two tracks crossing at grade), wye (triangular junction, for turning trains), gauntlet track (bridges/platform edges), catch points/trap points
-  Track ends: bumping post / buffer stop (steel, Hayes-style), earthen bumper, derail, end-of-track sign
-  Ballast regulator marks, tamped vs fouled ballast, weeds on spurs, rust vs shiny railhead (used tracks have polished heads)

**Signalling & control**

-  Wayside signals: searchlight (single lamp, old), colour-light 2/3-head on masts, signal bridges/gantries over multi-track, dwarf signals (yards, sidings), cantilever signals; aspects red/yellow/green plus flashing/lunar; number plates (permissive) vs absolute; distant/approach signals
-  Semaphores (heritage lines), interlocking towers (heritage/urban terminals), CTC — signals at block boundaries every 2–4 km on mainline, at each end of every passing siding, at every controlled turnout, at every diamond
-  Track circuits (insulated joints), axle counters, bonding wires, signal bungalows/equipment cabinets (silver aluminium huts on a concrete pad), relay cases, batteries, solar panels on remote cabinets, hot-box / dragging-equipment detectors (with a small hut and a sign "HBD"), defect detector radio antenna
-  Wayside signs: whistle post (W or ⁄ board, 400 m before crossings), mileposts (every mile/km), speed boards (permanent speed restriction: yellow/green boards; temporary: yellow/green flags), station name boards, yard limit signs, begin/end CTC, "STOP — private crossing", clearance point markers at turnouts, derail signs, catenary section markers, "END OF TRACK", flanger signs (northern lines:
   tells plow operators about crossings)
-  Cab signalling / PTC antennas and poles (modern mainline), radio towers at yards
-  Fencing: chain link on mainline in urban areas, post-and-wire rural, none on spurs; ROW markers (concrete posts), no-trespassing signs

**Electrification** (LRT, HSR, streetcar, some commuter)

-  Catenary masts (steel H-poles, every 50–65 m, staggered; portal/headspan across multiple tracks), contact wire + messenger, registration arms, tensioning weights every 1–1.5 km, section insulators, substations (fenced, every 2–5 km LRT, 20–50 km HSR), feeder cables, return-current bonds
-  Third rail (subway): cover boards, ramp ends, gaps at turnouts/crossings
-  Streetcar: trolley wire on span wires from poles both sides of the street or from building rosettes; frogs at wire junctions; no messenger

**Stations & stops**

-  Platform: low (LRT/streetcar/heritage, 0.3 m), high (commuter/subway/HSR, 1.1 m), length by train length (LRT 60–90 m, commuter 200–300 m, HSR 400 m); side platforms vs island platform; tactile edge strip (yellow), platform edge white line, mini-high platforms for accessibility on low-platform lines
-  Shelter/canopy, benches, ticket machines, fare gates (subway), signage totems, lighting, bike racks, park-and-ride lot, kiss-and-ride loop, bus bays, pedestrian overpass/underpass/at-grade crossing with gates between platforms, station building (heritage depot vs modern), elevators/stairs (subway/elevated)
-  Freight: team track, loading dock, transload area, grain elevator, intermodal crane, bulk terminal, engine house/shop, fuelling pad, wash rack, turntable/roundhouse (heritage)

**Yards & sidings**

-  Passing siding (mainline, 1.5–3 km, turnouts at both ends, signals at both ends), spur/industrial lead, ladder track (the diagonal track that turnouts fan off of), classification tracks, receiving/departure tracks, runaround track, storage tracks, car-repair track, hump yard (retarders, crest), yard office, yard lights (high-mast), crew paths, bad-order track
-  Yards are the one place rail can be dense: parallel tracks 3.6 m apart on a flat, shared ballast slab with no shoulders between tracks

**Structures**

-  Bridges: plate girder (most common), through truss, deck truss, timber trestle (heritage/spur), concrete arch/viaduct, bascule/swing (rivers), rail-over-road with clearance sign; abutments, piers, walkways with handrail, bridge guard rails on track
-  Tunnels: portal (concrete/stone), bore, ventilation shafts, refuge niches, tunnel signals
-  Culverts, retaining walls, snow sheds (mountain lines), rock-slide fences, avalanche sheds, snow fences, wind fences
-  Level crossings (3.15), pedestrian-only crossings (mazes/gates), farm/private crossings (planks, "STOP" sign only, no crossbuck sometimes)

**Rolling stock context** (for placement rules only): freight train 1–3 km long; LRT 60–90 m; commuter 200–300 m — this sets platform lengths, siding lengths, and how far back gates trigger.

5.3 Data model
~~~~~~~~~~~~~~

::

   RailTile {
     class:        RailClass
     tracks:       1 | 2 | n
     elevation:    int
     flags:        { electrified, fenced, bridge, tunnel, inStreet }
     connections:  8-bit mask, but with a constraint: every connection must be
                   continuable — see 5.4 (no 90° adjacent pairs)
     shape:        STRAIGHT | DIAG_STRAIGHT | CURVE_45 | CURVE_90_R{n} | TURNOUT_{L|R} | DIAMOND | CROSSOVER | END
     segment:      ref → RailSegment
     turnout?:     { mainDir, branchDir, number, powered, hand }
   }

   RailSegment {  // track between two "nodes" (turnouts, diamonds, ends, class changes)
     tiles, class, tracks, centerline: polyline → filleted with spirals
     speedLimit (derived from min radius and class)
     blocks: [signal positions], sidings: [...]
   }

5.4 Placement & connectivity rules
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  **No T or cross intersections.** A rail tile has at most 2 through connections unless it is a TURNOUT (3: main-in, main-out, branch-out, with branch within 45° of main-out) or a DIAMOND (4: two straight tracks crossing at 45–90°, no connection between them). Placing a third connection into a plain tile automatically converts it to a turnout if the geometry permits, otherwise rejects.
-  **Turn angle per tile is limited**: max 45° at any tile (so ortho→diag is a valid one-tile bend for yards/spurs only). A 90° bend must be built from two 45° bends with at least ``minTangent(class)`` tiles between them, *or* as a multi-tile arc (see 5.5).
-  **Minimum radius** (centerline, in tiles, assuming 1 tile ≈ 20 m — scale accordingly) — see table below.
-  **Grade limits**: yard 0%, mainline 1% (mountain 2.2%), branch 2%, LRT 6%, streetcar 8%, HSR 3.5%. Vertical curves R_v ≥ 500 m mainline (elevation change of 1 level must spread over ≥ 5 tiles). Never change grade inside a turnout, on a bridge deck end, or on a platform.
-  **Turnout rules**: the diverging track leaves at the turnout number's angle (#8 ≈ 7°, #12 ≈ 4.8°, #20 ≈ 2.9°) and then curves to the target direction; on a grid this means a turnout consumes ``2–4`` tiles along the main before the branch is one full track-spacing away (a *ladder* is a chain of these). Facing-point vs trailing-point is derived from traffic direction; mainline avoids facing-point
   turnouts for sidings (put a derail on trailing spurs). Turnouts never inside a curve of R < 2×R_min, never on a bridge, never within 1 tile of a level crossing, never on a platform.
-  **Parallel tracks**: adjacent rail tiles of the same class running parallel merge into one multi-track RailTile (``tracks = 2``) with the proper centre spacing rendered inside the tile; a crossover is placed by the player between them and becomes a CROSSOVER shape spanning 2–3 tiles.
-  **Class adjacency**: a SPUR may only branch off BRANCH/MAINLINE via a turnout with a derail; LRT/STREETCAR/SUBWAY never connect to freight classes; HSR connects only to HSR or COMMUTER via #24+ turnouts.
-  **Rail vs road**: same-elevation coincidence → 3.15 level crossing (if allowed) else reject; different elevation → bridge/tunnel per 3.11; STREETCAR class is placed *on* road tiles and sets ``road.flags.transit = streetcar``.
-  **Rail vs rail crossing**: same elevation, different segments → DIAMOND (allowed 45–90°; needs signals both directions on MAINLINE; speed restriction 40 km/h across it); different elevation → flyover.
-  **Ends**: an END tile gets a bumping post (yard/spur/platform track) or an "END OF TRACK" sign; a MAINLINE END is invalid unless it's a terminal station.

Minimum radius by class:

=========== ================================================================ =========
Class       R_min (tiles)                                                    Preferred
=========== ================================================================ =========
SPUR / YARD 1.5 (~90 m, 10 km/h)                                             3
STREETCAR   1 (~20 m)                                                        2
LRT         1.5                                                              4
BRANCH      4 (~180 m)                                                       8
MAINLINE    8 (~350 m)                                                       15
COMMUTER    8                                                                15
HIGH_SPEED  100+ (effectively straights and diagonals with long transitions) —
=========== ================================================================ =========

5.5 Curve geometry
~~~~~~~~~~~~~~~~~~

-  Build the centerline as with roads, but fillets are **circular arc + spiral (clothoid) transitions** on MAINLINE and above: transition length ``L_s = speed² / (R × k)`` — practically 1 tile of spiral each end on a grid. On SPUR/YARD/STREETCAR a plain circular fillet is fine.
-  A 90° turn at class radius R consumes an ``(R+1)×(R+1)`` tile block: the tool should draw the arc and *claim* the tiles it passes through (usually a staircase-shaped set), rendering in each the clipped ballast band exactly as the diagonal road band in 3.9. Tiles the arc passes through only in a corner still belong to rail (rail ROW is wide; the leftover becomes ditch/verge inside the fenced
   ROW).
-  Superelevation: outer rail raised up to 150 mm on mainline curves, none in yards; render as a tilt of the tie plane and a slightly taller ballast shoulder on the outside.
-  Widening: track gauge widens slightly on sharp curves, guard rail inside on R < 2×R_min.
-  Diagonal straights: same band-clipping as roads. The ballast band is wider than the rail spacing (≈ 4.5 m per track incl. shoulders) but the ROW is much wider than a road's, so encroachment into neighbours is the default (``ENCROACH``) and never a problem — rail neighbours are verge, not lots.
-  Speed limit of a segment = min over its curves of ``v(R)`` for the class; render a speed board at each restriction start and a resume board at the end.

5.6 Signalling rules (MAINLINE, COMMUTER, LRT-separate; none on YARD/SPUR/STREETCAR)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  Divide each segment into blocks of ``blockLength(class)`` (mainline 2–4 km ≈ 100–200 tiles; scale to your map — 8–12 tiles is playable). Place a colour-light signal at each block boundary on the right side of the direction of travel (NA rule: right-hand running, signals on the right) for each direction; on double track, one per track per direction.
-  Absolute signals (no number plate) at: both ends of every passing siding, every controlled turnout, every diamond, every junction of two segments. Intermediate block signals get number plates.
-  Signal placement offsets: ≥ 1 tile before a turnout's points (clearance point), never on a bridge, never on the crossing surface; a distant signal 1 block before every absolute signal on speeds ≥ 100.
-  A signal bungalow beside every absolute signal location and at every level crossing; a hot-box detector every ~30 km on mainline with its hut; mileposts every 1 mile/km on the right; whistle posts 400 m before each level crossing (each direction).
-  Dwarf signals in yards only at the ladder exit to the main.
-  Catenary masts every 3 tiles staggered on electrified segments, headspans over ≥ 3 tracks, a substation every 100–150 tiles (LRT) with a fenced pad; tensioning weights every 60 tiles; section insulators at station ends and turnouts.

5.7 Stations
~~~~~~~~~~~~

-  A station is a segment sub-range of length ``platformLength(class)`` with ``tracks ≥ 1``. Placement requires: straight track (or R ≥ 5×R_min), grade ≤ 0.5%, no turnout or crossing inside, ≥ 1 tile from any turnout.
-  Platform type by class: LRT/STREETCAR low side or median platform (streetcar: curb bulb "platform" in the road tile, or a median island with a crossing); COMMUTER/HSR high platform; BRANCH/HERITAGE low + depot building.
-  Layout: single track → one side platform; double track → two side platforms + grade crossing with gates between them (or overpass if MAINLINE freight also runs), or an island platform if the tracks can be spread (needs 2 extra tiles of width and crossovers each end to move the tracks apart — tool does this automatically as a "station widening").
-  Furniture per platform: shelter every 30 m, benches, lights every 15 m, totem sign at each end, tactile strip along the edge, ticket machine at entrances, bike racks, bins, PA speakers on light poles, clocks at HSR/commuter.
-  Ground side: a bus loop, park-and-ride lot and kiss-and-ride sized to class; road access must be a COLLECTOR or better within 1 tile.
-  Freight "stations" are spurs: a spur that ends adjacent to an industrial lot generates a loading dock/team track; a spur into a yard becomes a ladder.

5.8 Yards
~~~~~~~~~

-  A yard is a designated rectangular zone attached to a mainline via a lead track and a ladder. Inside: parallel tracks 3.6 m apart auto-generated to fill the width, ladder turnouts (#8) at one or both ends, a runaround, high-mast lights every 4 tiles, a yard office, fencing on the road side, no signals except at the lead. Grade 0% enforced; the tool levels terrain.
-  Engine facility optional: fuelling pad, sanding tower, shop building, wash; heritage: turntable + roundhouse.
-  Intermodal yard variant: two long tracks, paved between, gantry cranes, container stacks, truck gate.

5.9 Rendering layer order (rail)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1.  Subgrade/embankment or cutting (terrain modification)
2.  Ditches, culverts
3.  Ballast band (per-tile clipped), yard slab
4.  Ties, then rails (rails are the top-most track element; add rust vs polished head by class/traffic)
5.  Turnout hardware, frogs, guard rails, bumpers
6.  Platforms, fences, ROW markers
7.  Signals, masts, cabinets, signs, catenary poles
8.  Catenary wire (thin lines, above everything except bridges), third rail
9.  Bridges/tunnel portals
10. Weather: snow on ballast shoulders, snow fences

5.10 Validation (rail)
~~~~~~~~~~~~~~~~~~~~~~

-  Reject: 3-way non-turnout; turn > 45° in one tile; R < R_min; grade > limit; turnout on curve/bridge/crossing/platform; MAINLINE dead end without terminal; freight class connecting to LRT/streetcar/subway; diamond < 45°; FREEWAY level crossing.
-  Warn: R between R_min and preferred (speed restriction board added); facing-point spur turnout on mainline; siding shorter than train length; platform on grade > 0.5%; level crossing within 1 tile of turnout; more than 2 tracks in a level crossing; yard lead facing the wrong way for the dominant traffic direction.

--------------

Part 6 — Hardware modelling reference
-------------------------------------

Dimensions are typical North American values in metres unless noted. Colours are sRGB hex for the albedo; "retro" means retroreflective sheeting (render as high-albedo with a specular bump or an emissive-on-headlight trick). Every asset below is described as a **kit**: a base, a pole, and attachable parts with named sockets, so the placement rules in Parts 3 and 5 can assemble variants rather than
needing a unique mesh per combination.

6.0 Conventions
~~~~~~~~~~~~~~~

-  **Units & axes**: 1 unit = 1 m. Pivot at ground contact (bottom-centre of the base) with +Y up (or +Z if your engine prefers — be consistent). Forward = the direction the device *faces* (the face a driver sees), so a sign placed on the right side of a road with ``yaw = roadHeading + 180°`` faces oncoming traffic.
-  **Sockets**: each pole mesh exposes named attachment points with a transform: ``top``, ``arm_0..n`` (at fixed heights), ``sign_2.1``, ``sign_1.5``, ``pedhead``, ``button``. Attachments are separate meshes with their own pivot at their mounting point (back-centre for signs and signal heads, hinge for gate arms).
-  **LODs**: LOD0 full kit (< 30 m), LOD1 merged pole + attachments with baked normal map (30–120 m), LOD2 a single vertical quad + billboard for heads/signs (> 120 m). Wires and thin bars go to LOD2 as alpha-tested lines or vanish beyond 200 m.
-  **Materials** (share these across all hardware): ``galv_steel`` (#9aa0a3, rough 0.55, metal 1, slight streaking), ``painted_steel_black`` (#1c1c1c, rough 0.4), ``painted_steel_yellow`` (#f2b400 for signal heads/backplates in some cities; otherwise black), ``alu_extrusion`` (#b8bcbe, rough 0.35), ``wood_treated`` (#6b5a3e→ silvers with age #8f8a80), ``retro_white`` (#f4f4f2), ``retro_yellow``
   (#f5c400), ``retro_flyg`` (fluorescent yellow-green #c7f227), ``retro_red`` (#c8102e), ``retro_green`` (#00703c), ``retro_blue`` (#003f87), ``retro_orange`` (#ff7900), ``retro_brown`` (#5e3a1e), ``concrete`` (#b9b5aa), ``lens_red/yellow/green`` (#ff2a1a / #ffb400 / #16d95a emissive when lit, dark tinted glass when off), ``rubber_black`` (#1f1f1f rough 0.9).
-  **Aging channel**: expose a per-instance ``age 0–1`` that blends: paint chips at edges, rust bloom at welds and base, retro sheeting fade (drops saturation and albedo 20%), graffiti/sticker decals on the back of signs, sun-bleached poles. Rust is orange-brown #8a4a1c streaking downward.
-  **Snow/dirt channel** (top-down mask): snow caps on horizontal surfaces, salt-white splash up to 0.5 m on anything within 2 m of a driving lane.

6.1 Railway crossing hardware
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Crossbuck assembly (passive crossing)**

-  Post: 100 mm square galvanized steel tube or 3.5" round, 4.3 m above grade, set in a concrete footing 0.6 m × 0.6 m (flush, only a 50 mm collar shows). Retroreflective white vertical stripe 50 mm wide on the front face of the post for 2 m (a key visual cue).
-  Crossbuck sign: two blades 1220 mm × 230 mm crossing at 90°, retro white with black "RAILROAD" / "CROSSING" lettering (US) — Canada uses white blades with a red border and no lettering. Centre of X at 2.7 m (sign bottom ≥ 2.1 m above the sidewalk). Blades are 3 mm aluminium, mounted 20 mm proud of the post on a bracket.
-  "2 TRACKS" plaque: 690 × 460 mm, retro white, black text, mounted 0.15 m below the crossbuck centre.
-  YIELD (900 mm triangle) or STOP (750 mm octagon) plaque below that at passive crossings.
-  Emergency Notification (ENS) sign: blue 300 × 200 mm plaque with white text, at 1.5 m on the post, drivers' side. Also a small crossing ID stencil.
-  Optional: a second crossbuck on the back of the post facing the other way is *not* done — each approach has its own post.

**Flasher mast (active crossing)** — build as a kit on the same post family

-  Post: 4.5" or 5" galvanized steel pipe, 3.6–4.6 m tall, on a cast-iron or steel *base junction box* 0.45 m square × 0.6 m tall with a hinged door (this box is the visual signature at the foot of every crossing mast). Post is often white-painted rather than bare galvanized; older ones are silver.
-  Crossbuck at 2.7 m as above.
-  Flashing light unit: a horizontal bar 1.5 m wide (extruded aluminium, black or galvanized) at 2.3–2.4 m with a red 300 mm (12") LED/incandescent lamp at each end. Each lamp: cylindrical housing 300 mm dia × 250 mm deep, black, with a 150 mm hood/visor and a red lens; sometimes a target *backboard* (black square 400 mm) behind each lamp. Lamps alternate at 45–65 flashes per minute (each lamp 0.5
   s on / 0.5 s off, out of phase). A second pair of lamps facing *backwards* (rear lights for the far approach or the sidewalk) is common — model as the same bar with lamps on both faces.
-  Bell: 200–250 mm dome, cast iron or an electronic bell in a small black box, at the top of the post (4.3 m). Rings continuously while lamps flash.
-  Cantilever variant: the same mast becomes a 7–10 m horizontal truss/pipe arm at 5.5–6 m clearance with 1–2 extra lamp pairs over the far lanes and a crossbuck hung under the arm. Arm is a 200 mm round pipe with a knee brace, or a 300 mm deep triangular truss.
-  "NO TRAIN HORN" plaque, "2 TRACKS" plaque, ENS sign, all on the post below the crossbuck.

**Gate mechanism** — attaches to the flasher mast at 0.9–1.1 m

-  Mechanism case: cast-aluminium box ~0.5 × 0.4 × 0.5 m, silver/grey, hinged door, mounted on the *left* side of the post as seen from the approach (so the arm swings across the road). Contains the motor; nothing visible outside but the shaft.
-  Counterweight arms: two short steel arms (0.6–0.9 m) on the back of the shaft carrying cast-iron weights (rectangular, ~0.3 × 0.2 × 0.15 m, painted black or safety orange). They point *up* when the gate is down and *down/horizontal* when the gate is up — animate them as the negative of the arm angle.
-  Gate arm: fibreglass or aluminium, 100 × 100 mm hollow section at the root tapering to 60 × 60 mm at the tip, length = lanes covered × 3.5 m + 0.5 m (typ. 7.3 m for two lanes; max ~11.6 m before you need a second mast or a median gate). Painted white with retro red diagonal bands 400 mm wide at 45° (bands lean *toward* the tip on the top face — the diagonal points away from the post). Three red
   lamps along the top: tip (flashing), middle and root (steady when down). Tip lamp centred 0.3 m from the end. A breakaway coupling 1 m from the root (the arm can be knocked off and rehung — nice damage state).
-  Motion: from vertical (up, resting at ~88°) to horizontal takes 10–12 s with an ease-in near horizontal; rises in 6–8 s. Arm pivot is 0.9–1.1 m above grade at the mechanism shaft; a lowered arm's underside sits ~1.0 m above the pavement.
-  Median gate: an identical unit on a shorter (2.5 m) post on the median nose, with a short arm (3.5–4 m).
-  Exit gates (four-quadrant): mirror units on the far side of the crossing, arms of the same spec, lowered 2–4 s after the entrance gates.

**Pedestrian gate / swing gate**

-  Short post (2.4 m) near the sidewalk with a 1.2–1.8 m gate arm at 0.9 m hinge height, same red/white bands, one tip lamp; or a hand-operated swing gate with a "STOP" disc on a spring closer. A pair of small 200 mm flashers at 2.1 m facing the sidewalk. "LOOK" pavement stencil in front.
-  Tactile plate: 0.6 × sidewalk-width panel of truncated domes, yellow (US) or red/yellow/grey (CA), cast iron or polymer; domes 0.9 mm high on a 60 mm grid — bake as normal map.

**Signal bungalow / equipment**

-  Bungalow: prefabricated aluminium or fibreglass house 2.4 × 3.0 × 2.6 m (or 1.8 × 2.4 for small crossings), silver or light grey, low-pitch roof, one steel door with a padlock hasp, louvred vent, an eyebolt lifting lug at each top corner, on a 0.3 m concrete pad or piers. Often a small chain-link fence (1.8 m, 3 strands barbed wire) 1 m off the walls, and a gravel apron. A 10 m radio
   mast/antenna beside it for newer installs, plus a conduit riser coming up the wall.
-  Track-side "wayside" boxes: small grey cases 0.6 × 0.4 × 0.5 m on a post (relay case, battery box), and a *bootleg* riser (small dark box 0.3 m tall at tie-end) where wires meet the rail.
-  Gate cabinet at each mast is the base junction box described above; sometimes an extra "cake box" case beside it.

**Crossing surface**

-  Rubber panels: 2.4 m long modules along the rail (i.e., panel length runs *with* the track), 0.9 m wide gauge panel between the rails and 0.6 m wide field panels outside, black rubber #1f1f1f with a raised diamond tread; a 50–65 mm flangeway groove inside each rail. Ends are bevelled steel or rubber.
-  Concrete panels (LRT, some mainline): same layout, precast grey concrete with an anti-skid broom finish, steel edge angle at the flangeway.
-  Timber planks (spur/local): 150 × 200 mm treated timbers laid parallel to the rail, dark brown to grey, gaps of 10 mm, often with a steel angle at the flange side; asphalt fills the field side.
-  Asphalt-only (rural spur): rails bedded in asphalt with a formed flangeway; looks like potholes waiting to happen.
-  Header: a full-depth concrete or asphalt curb-height header at the panel edges so the pavement doesn't ravel — visible as a 0.3 m band on each side.

**Track pieces at the crossing**

-  Rail: 115–136 lb/yd (US) profile — head 70 mm wide × 45 mm, web, base 150 mm, overall 172 mm tall. Rust brown #5b3a22 on the web and base, polished steel #c9c9c9 on the head of any track that sees traffic. Gauge 1435 mm between inner head faces.
-  Ties: timber 2.6 m × 230 mm × 180 mm at 500–600 mm spacing (19–21 per 10 m), creosote dark (#3d2f24) fading to grey; concrete ties 2.6 m × 280 mm (wider in the middle) × 230 mm, pale grey #b4b1a8, at 600 mm.
-  Tie plates (150 × 200 mm, rust), spikes or Pandrol e-clips (a distinctive curl ~ 70 mm high, rust), rail anchors on the underside.
-  Ballast: crushed granite/limestone 30–60 mm, greys; top of ballast level with the top of the tie in the crib, shoulder 300 mm beyond the tie ends sloped 2:1; fouled/muddy in the crossing approach; weeds on spurs.
-  Insulated joints 30–60 m either side of the crossing (a 12 mm fibre gap in the rail with bonded bars) and the bootleg wires — small details that read as "signalled crossing" up close.
-  Whistle post: white post 1.5 m with a black "W" (US) or a large black "⁄" board (CA), 400 m before the crossing; at LOD2 just the post.

6.2 Traffic signal hardware
~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Signal head (vehicle)**

-  Section: 12" (300 mm) lens in a 340 × 340 × 280 mm housing (8" = 200 mm sections still exist on locals and as the "ball" sections of some arrow heads). 3-section head: 340 wide × 1070 tall; 4-section (FYA) 1400; 5-section (doghouse: R over Y/G ball + Y/G arrow side by side) 700 wide × 1070 tall.
-  Housing: die-cast aluminium or polycarbonate, black (most of NA), dark green (older Ontario/Quebec/some US cities), yellow (many Canadian cities, some Texas/Florida — yellow housings are a strong regional cue). Doors have a hinge on one side and a latch on the other; visors 250 mm tunnel or cutaway, black, on every section (some cities use "tunnel" on all, others "cap" visors — pick one per
   city).
-  Backplate: 130 mm rim around the head, black polycarbonate with a 25 mm retro yellow border on newer arterial installs, louvered slots; no backplate on locals.
-  Lenses: red top, yellow middle, green bottom; arrows are a white/black arrow on the lens (red arrow, yellow arrow, green arrow); FYA head is red-arrow / solid-yellow-arrow / flashing-yellow-arrow / green-arrow. LED lenses show a faint dot matrix pattern; incandescent show a smooth glow with a Fresnel ring.
-  Mounting: rigid pipe-arm bracket from the top (mast-arm mount, head hangs 300 mm below the arm), or *span-wire* hangers with the head swinging on a clamp (older/rural) with a tether wire at the bottom. Pedestal/pole-mounted heads use a side bracket at 2.4–3.0 m to the bottom of the head.
-  Mounting height: bottom of head 4.6–5.5 m over the road on arms; over 5.2 m is typical for truck clearance.

**Pedestrian signal**

-  Housing 400 × 460 × 200 mm, one-section "combo" LED (orange hand + white walking man + countdown digits), or two-section older (WALK white / DON'T WALK orange text). Black housing with a lattice eggcrate or louvered visor. Mounted 2.1–3.0 m to the bottom, on the pole, facing the far end of its crosswalk.
-  Push button station: 100 × 120 mm plate with a 50 mm button, an arrow decal, and a sign plaque (230 × 300) above; APS versions have a larger 50 mm raised button, a speaker, and a locator tone. Mounted at 1.07 m on the pole, on the side facing the crosswalk.

**Poles and arms**

-  Mast arm pole: tapered steel shaft 300–400 mm at base to 200 mm at top, 6–8 m tall, galvanized or powder-coated (black, dark green, bronze, brown — pick per city), on a 0.6 m square anchor-bolt base plate with 4 nuts and a 100 mm rim of grout; a hand-hole with a cover 0.3 m up; a decorative base cover (fluted or plain cylinder 0.6 m tall) on downtown poles.
-  Arm: tapered monotube 200→100 mm, 6–20 m long, cantilevered from the pole at 5.5–6.5 m with a 0–3° upsweep, straight or with a truss for > 15 m. Signal heads hang below at lane centres; the street name sign (internally lit box 2.4 × 0.5 m or a flat blade 2 × 0.4 m) hangs near the pole; a luminaire arm (2–3 m) can extend up from the top of the same pole with a cobra head at 9–12 m.
-  Span wire: 10 mm messenger between two 9–11 m wooden or steel poles at the corners, heads hung by hangers, a lower tether wire, and diagonal guy wires to anchors — this is the "rural/older" look.
-  Pedestal: a 4" pole 3 m tall on a 0.5 m cast-iron base for corner ped heads and near-side supplemental heads.

**Controller cabinet**

-  NEMA/Caltrans-style cabinet: unpainted aluminium, 1.4–1.7 m tall × 0.75 × 0.5 m (Type 332 is 1.7 tall × 0.6 × 0.76), on a 100 mm concrete pad or pole-mounted (smaller 0.9 m size), a single front door with a 3-point handle and a police panel door, louvred vent at top, a fan grille, sometimes a small satellite/radio antenna on top. Pick a corner: usually the one with the mast-arm pole, 1–2 m back
   from the curb on the furnishing strip. Often has a "sticker layer" (graffiti removal ghosts, city asset tag).
-  Meter pedestal / service point: a 0.6 m grey box on a post nearby.
-  Detector loops: 6-ft (1.8 m) squares or 6 × 40 ft rectangles sawcut into the pavement with a sealant line 6 mm wide, dark; a "home run" cut to the curb. Video detection: a 150 mm bullet camera on the arm tip, or a radar unit (small white box) on the arm.
-  Emergency preemption receiver: a small white cylinder on the arm, and a confirmation strobe (blue/white) on the arm for fire trucks.

**Mid-block pedestrian devices**

-  RRFB: a pair of 200 × 80 mm amber LED bars under a ped-crossing sign on each side of the road (and on the median), solar panel on top of the post, button on the post.
-  HAWK/PHB: a 3-lamp head (two reds side by side over one yellow) on a mast arm over each direction, dark when idle; ped heads and signs "CROSSWALK — STOP ON RED".

6.3 Signs and posts
~~~~~~~~~~~~~~~~~~~

-  Sheet: 2–3 mm aluminium; sizes — STOP 750 (locals) / 900 (arterials) / 1200 mm (freeway) octagon; YIELD 900 triangle; speed limit 600 × 750 (US) / 600 × 750 (CA "MAXIMUM"); warning diamond 750 (local) / 900 (arterial) / 1200 (freeway); street-name blade 150–300 mm tall × 600–1200 long, double-sided, green with white letters (US default; blue/white, brown, or black in some cities, Canada often
   green or blue), mounted on a 90° bracket at the top of the post or on the signal arm; regulatory plates 300 × 450 (NO PARKING); ped-crossing 750 diamond in fluorescent yellow-green with a 600 × 300 arrow plaque.
-  Back of sign: dull grey aluminium with a stencilled install date and city logo; often a second sign back-to-back.
-  Posts: 2" square perforated steel tube (telespar) with a slip-base sleeve, galvanized — the NA default; 3-lb U-channel green-painted steel (older, rural); 100 × 100 mm treated wood (rural, park); breakaway 75 mm round steel. Height so the bottom of the lowest sign is 2.1 m (urban, over sidewalk) or 1.5 m (rural shoulder) and the post extends 100 mm above the top sign. Mounting via 8 mm bolts
   with nylon washers; 2 per sign.
-  Bases: driven post with no visible base, or an anchor sleeve with a 75 mm stub, or a concrete footing collar in sidewalk (with a 25 mm expansion gap ring in the concrete).
-  Sign lighting: none on locals; freeway guide signs on gantries are externally lit by small LED fixtures on arms below the sign or internally lit (older).

6.4 Lighting and utility poles
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  Cobra-head luminaire: 0.7–0.9 m long, 0.35 wide, grey aluminium, LED (flat panel with a "chip" grid) or HPS (curved acrylic drop lens, orange light 2000 K); on a 2.4–4.6 m davit or truss arm, 9–12 m mounting height, one per pole. Photocell nub on top (50 mm).
-  Poles: tapered galvanized or aluminium round, 10–12 m, on an anchor-bolt base with a base cover; concrete poles (prestressed, hexagonal, grey, common in Florida and BC); wood 12 m class 4 with a rebar-wrapped top for luminaire arms.
-  Decorative/pedestrian: 4–5 m fluted or smooth black/green pole, acorn or teardrop luminaire (0.5 m dia globe), or a "shoebox" post-top; banner arms (1 m) with vinyl banners; plant hanger hooks; GFCI outlet box at 3 m for holiday lighting; often paired on a single pole with a mid-height ped lamp.
-  High-mast: 30 m galvanized pole with a ring of 6–8 floodlights on a lowering carriage, on a 1.5 m base, at interchanges and yards.
-  Wood utility pole: 10–12 m, 300 mm butt tapering to 200 mm, treated brown/grey, 3 primary conductors on a 2.4 m crossarm with insulators (grey porcelain or polymer), a 2-tank or 3-tank transformer bank (grey cylinders 0.6 × 0.9 m) on the pole below the primaries on every 3rd–5th pole, secondary triplex below that, telecom cables (thick black) and their splice cases at 5–6 m, a guy wire with a
   yellow guard, pole number tag, a streetlight arm on many. Down-guy anchors, riser conduits for underground connections, and an "H-frame" for corners. Slack sag on conductors between poles: sag ≈ 1–2% of span.
-  Pad-mount transformer (underground areas): 1.0 × 0.9 × 1.1 m green steel box on a concrete pad, warning stickers; telecom pedestals 0.3 × 0.3 × 0.9 m green/grey posts; traffic and utility hand-holes with 0.6 × 0.9 m composite lids in the sidewalk.

6.5 Curb, gutter, sidewalk, ramps
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  Barrier curb: 150 mm face, 12–25 mm batter, 150 mm wide top, with a 300–600 mm gutter pan at 8% cross-slope towards the curb; a 12 mm tooled joint every 3 m and expansion joint every 30 m; a rounded 25 mm radius on the curb top edge. Rolled curb: a 250 mm arc profile. Depressed curb at driveways: 25–50 mm face with a 1:10 transition wing 0.9–1.5 m.
-  Sidewalk: 100–150 mm concrete, 1.5–2.4 m, scored every 1.5 m (tooled 6 mm grooves), expansion joint every 6 m with a black asphalt-impregnated strip; a 2% cross-slope to the curb; broom finish perpendicular to travel. Age: settled panels with a 20 mm lip (trip hazard), root heave, grinder marks, patch panels of different colour.
-  Curb ramp: 1.2–1.5 m wide, 1:12 (8.3%) max slope, 100 mm lip removed to flush, flared sides at 1:10, detectable warning surface 0.6 m deep × ramp width at the bottom in a contrasting colour (yellow, red, brick-red, grey polymer), with a 25 mm return curb on the sides for "parallel" ramps. Two per corner on a perpendicular layout; one diagonal on old corners (fed by a 1.2 m landing).
-  Catch basin: 0.6 × 0.9 m cast-iron curb inlet with a horizontal throat 150 mm tall under a curb-face frame, or a 0.6 × 0.6 m grate in the gutter; "DUMP NO WASTE — DRAINS TO CREEK" medallion cast in.
-  Manhole: 0.6 m cast-iron round lid with a diamond or waffle pattern and the utility name cast, on a 0.7 m frame, in a 0.9 m square asphalt patch of different tone; storm ones are often larger (0.7 m).

6.6 Street furniture (quick dimension sheet)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  Hydrant: 0.7–0.9 m tall, bonnet + two 65 mm hose nozzles + one 100 mm steamer nozzle, 5-sided operating nut on top, colour by city (red, yellow, silver, blue bonnet by flow class), a breakaway flange at ground, on a concrete collar; a blue reflective RPM in the road opposite it; a 1.2 m white/red stake in snow country.
-  Bench: 1.8 m, cast-iron ends + wood slats (park style) or 1.5 m steel mesh with centre armrest (anti-lie-down style), back to the road.
-  Litter bin: 0.9–1.1 m tall, steel mesh or slatted with a lid, or a 1.1 m Big Belly (green/black compactor with a solar lid); paired with recycling in newer cities.
-  Bus stop: 3 m post with a 0.3 × 0.45 m flag sign at 2.4 m and a route ID plaque; shelter 4 × 1.5 × 2.5 m, tempered glass on a steel or aluminium frame, flat or barrel roof, one ad panel 1.2 × 1.8 backlit, a bench inside, an 0.6 m diameter map cylinder; a 1.5 × 2.4 m concrete pad extension at the door.
-  Parking meter: single-space 1.2 m post with a 0.3 m head; pay station 1.5 m tall × 0.4 × 0.3 m, solar panel on top, blue/grey.
-  Bike rack: inverted U 0.9 × 0.9 m, 50 mm galvanized tube, 1 m spacing; post-and-ring in Toronto style.
-  Bollard: 150–200 mm steel pipe 0.9–1.1 m, concrete-filled, black or yellow, domed cap, on a 0.3 m spacing at pedestrian malls; removable sleeve version has a padlock hasp.
-  Newspaper box: 1.2 × 0.5 × 0.4 m plastic, garish per-paper colours; disappearing in newer cities.
-  Mailbox: US blue relay box 1.2 × 0.5 × 0.6 with a pull-down hopper; Canada Post red street letter box 1.3 × 0.4 × 0.5 with a slot.
-  Tree pit: 1.2–1.8 m square cast-iron grate (concentric rings or a grid), or a 1.5 × 3 m planter bed with a 150 mm curb; tree guard 1.2 m tall vertical bars; young trees staked with 2 stakes and rubber ties.
-  Planter: 1.2 m concrete cube or 0.9 m dia, seasonal flowers.
-  Utility cabinet (telecom/cable): 1.2 × 0.9 × 0.4 m grey/green metal on a pad, usually plastered with stickers.

6.7 Animation & state hooks (so the sim can drive assets)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

================= ================================================================================== ===============================================================================
Asset             States                                                                             Notes
================= ================================================================================== ===============================================================================
Crossing flashers off / flashing (0.5 s alternate)                                                   Left lamp leads; bell audio loop while flashing
Gate arm          up (88°) / lowering (10–12 s) / down (0°) / rising (6–8 s) / broken (arm detached) Counterweights rotate opposite; tip lamp flashes; root/mid steady when down
Ped gate          up / down                                                                          Follows vehicle gate with 1 s lag
Signal head       R / Y / G / FYA / arrow variants / dark / flash-yellow / flash-red                 Per section emissive; LED "flicker" is not needed; incandescent has 0.15 s fade
Ped head          walk / flashing-hand (with countdown) / hand / dark                                Countdown digits 0–99
Push button       idle / pressed (locator tone + "WAIT" LED)                                         APS: speaker plays "wait"/"walk sign is on"
Streetlight       off / on / flicker (failing)                                                       Photocell: on at dusk, HPS warms up over 2 min (pinkish → orange)
Traffic sign      static; ``age`` blend; ``snowCap``                                                 Retro sheeting reacts to headlights
Catenary/wires    static, wind sway ± 2 cm at LOD0                                                   Skip at LOD1+
Rail              polished-head strength from traffic count                                          Emissive-free; just a roughness/albedo swap on the head strip
Ballast           clean / fouled / weedy by class and age                                            Vertex colour mask
================= ================================================================================== ===============================================================================

6.8 Suggested asset list (minimum viable kit)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  Poles: ``pole_sign_telespar``, ``pole_sign_uchannel``, ``pole_sign_wood``, ``pole_pedestal_4in``, ``pole_mastarm_{6,8}m`` + ``arm_mastarm_{8,12,16}m``, ``pole_span_wood_11m``, ``pole_light_10m`` + ``arm_davit_3m``, ``pole_light_ped_4m``, ``pole_utility_wood_12m`` (+ crossarm, transformer×1/2/3, guy), ``pole_highmast_30m``, ``pole_catenary_H``
-  Heads: ``sig_3sec``, ``sig_4sec_fya``, ``sig_5sec_doghouse``, ``sig_3sec_arrows``, ``sig_bike``, ``sig_transit_bar``, ``ped_head_combo``, ``ped_button_aps``, ``rrfb_pair``, ``hawk_head``
-  Crossing: ``rr_post_crossbuck``, ``rr_mast_flasher``, ``rr_mast_flasher_cantilever``, ``rr_gate_mech`` + ``rr_gate_arm_{4,8,11}m``, ``rr_median_mast``, ``rr_ped_gate``, ``rr_bungalow_{small,large}``, ``rr_relay_case``, ``rr_panel_rubber_{gauge,field}``, ``rr_panel_concrete``, ``rr_plank_timber``, ``rr_whistle_post``, ``rr_tactile_plate``
-  Signs: ``sign_stop_{750,900}``, ``sign_yield``, ``sign_speed_{us,ca}``, ``sign_warn_diamond_*``, ``sign_streetname_blade``, ``sign_streetname_box``, ``sign_reg_plate_300x450``, ``sign_crossbuck_{us,ca}``, ``sign_advance_rr_{us,ca}``, ``sign_ens``, ``sign_oneway``, ``sign_dne``
-  Furniture: ``hydrant``, ``bench_{park,steel}``, ``bin_{mesh,bigbelly}``, ``busstop_post``, ``bus_shelter_4m``, ``paystation``, ``meter_single``, ``bikerack_u``, ``bollard_{fixed,removable}``, ``mailbox_{usps,cpc}``, ``tree_grate_1.5``, ``tree_guard``, ``cabinet_controller_{nema,332}``, ``cabinet_telecom``, ``padmount_transformer``, ``catchbasin_{curb,grate}``, ``manhole_{600,700}``
-  Curb kit (procedural, not meshes): profiles for barrier, rolled, depressed; ramp profiles; joint decals.

--------------

Part 7 — Raised highways (elevated freeways)
--------------------------------------------

Scope: a grade-separated, two-direction freeway carried on a viaduct, built from **2×2 tile segments**. Ramps connect it to surface roads; interchanges connect it to other raised highways at X and T junctions. Nominal tile = 15 m for the arithmetic below; if your tile is different, the lane bake and the ramp lengths (in tiles) scale with it.

7.1 The baked cross-section
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Deck width = 2 tiles = 30 m. That fits the common urban-viaduct section, which is what we bake:

====================================================================== ========
Element (outer → centre, one direction)                                Width
====================================================================== ========
Outer parapet / barrier (F-shape concrete, 0.81 m tall)                0.5
Outer shoulder                                                         2.4
Lane 3 (outer / merge lane)                                            3.7
Lane 2                                                                 3.7
Lane 1 (inner)                                                         3.7
Inner shoulder                                                         0.6
Half of median barrier (single-slope, 1.07 m tall, light poles on top) 0.3
**Half deck**                                                          **14.9**
====================================================================== ========

Two directions → 29.8 m, leaving 0.1 m each side inside the 30 m footprint. **3 + 3 through lanes is the bake.** Everything else in Part 7 derives from this: lane count, shoulder space for a merge taper, where the lights go.

If your tile is smaller than ~13.5 m, drop to 2 + 2 lanes with full shoulders (2+2+shoulders = 12.6 m per direction) rather than squeezing lane widths — a 3.0 m freeway lane looks wrong at speed.

Fixed elements on the deck (rendered per 2×2 segment, arc-length parameterised like Part 3):

-  Lane lines: dashed white 3/9 (``4.5/13.5`` if you scale with the 15 m tile), solid white right edge line, **solid yellow left edge line** against the median barrier — this yellow-left / white-right pair is the most recognisable freeway marking cue.
-  RPMs on the lane lines every 12 m, white; red-backed on the wrong-way face at ramp junctions.
-  Median barrier: continuous single-slope concrete with light poles every 45 m (twin-arm cobra heads, 12 m), glare screen optional, drainage slots at the base.
-  Parapets: continuous, with a 0.3 m curb lip and steel rail on top on older viaducts (the "T-rail" look), plain concrete on new ones; scuppers every 30 m draining to a downspout that runs down a pier.
-  Expansion joints every 3–4 segments (finger joints across the whole deck, a dark line with steel teeth).
-  Overhead sign gantries: full-span truss on both parapets, at least at 2 segments before every exit and at every lane drop; cantilever gantries for single-lane exits.
-  Deck lighting on the median only; no lighting from the parapets except at interchanges.

7.2 Structure
~~~~~~~~~~~~~

-  **Deck**: post-tensioned concrete box girder (single cell, 2.4 m deep, sloped webs, the modern default) or steel plate girders (4–6 girders, 2.0 m deep, older/grey-green paint) on a 0.25 m slab. Deck underside is flat for the box girder, ribbed for plate girders. Bottom of deck = ``elevation``; road surface = ``elevation + deckDepth + 0.25``.
-  **Clearance**: 5.0 m minimum from any surface road crown to the bottom of the deck (5.3 for truck routes; 7.0 over rail); so a raised highway sits at ``elevation ≥ 5.0 + deckDepth`` above the highest thing below it — practically **7.5–8 m to the road surface** for a box girder. This number drives every ramp length in 7.3.
-  **Piers**: one per segment boundary (span = 2 tiles = 30 m; box girders can do 2 segments = 60 m if you want fewer columns). Choose the pier type from what's underneath:

   -  Nothing / lot / verge: single hammerhead pier (1.8 m octagonal column, 1.5 m deep cap the full deck width) in the deck centreline.
   -  Surface road running parallel under the deck: two-column bent placed in the road's median or outside its curbs; never in a lane.
   -  Surface road crossing under the deck: straddle bent (portal frame) with the columns outside the road's curbs, or a span long enough to clear the road with the hammerhead landing in a corner lot.
   -  Rail under: straddle bent only, 7 m clearance.
   -  Water: 2-column bents on pile caps; add a fender.

-  **Ground transition**: the highway leaves grade over an MSE wall approach (panelled retaining walls, 1 segment per ~2.5 m of rise at 4%: 4 segments to reach 8 m — or 3 segments at 6% for a game-scale compromise), then the first abutment. Alternatively depressed-to-raised via a bridge over a cross street.
-  **Noise walls**: on the outer parapet wherever residential lots are within 2 tiles (2.4–4.0 m tall panels, concrete or absorptive, on the parapet top).
-  **Under-deck zone**: the ground tiles beneath a raised highway keep their ground use, but with ``underDeck = true``: lots there may hold only parking, storage, low industrial, or a surface road; building height capped at ``elevation − 1``; chain-link fence around pier bases; permanent shadow decal; drainage grates; graffiti aging on piers.

7.3 Ramps — 1×1 in data, rendered with whatever air is free
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Hard constraint.** A ramp is one 1×1 tile adjacent to a deck edge, connected to a surface road on its far side. The data does not grow. An interchange (or another ramp, or the map edge) may sit on the very next tile along the deck. So the renderer never *requires* extra tiles; it *opportunistically* uses free air beside the deck, and degrades gracefully to a form that fits in the ramp tile
alone.

**The arithmetic the renderer is fighting.** Deck surface ≈ 7.5 m above the surface road. A straight descent inside one 15 m tile is a 50% grade — not drawable. Path length is the only lever: 6% needs ~125 m, 8% ~95 m, 12% ~65 m, 16% ~47 m. Every form below is just a way of packing path length into the tiles available.

**Occupancy layers.** Every tile has ``ground`` (what's there) and ``air`` (a height band that's claimed). The ramp tile claims ground. Anything else the ramp uses is an air claim on a neighbour — legal on any tile whose tallest ground object is below the ramp's ``zMin`` there, and *always* legal on tiles that are only lots, roads, rail, or verge. Air claims by an interchange template count as
occupied.

**Step 1 — orient.** The ramp tile touches the deck on one side; that side's deck direction (EB/WB) is the one the ramp serves (the lane it is adjacent to). ``ON`` ramps join in the direction of travel; ``OFF`` ramps leave in it. Upstream = against that direction.

**Step 2 — scan for free air along the deck on the ramp's side.**

-  ``D`` = number of consecutive free-air tiles immediately **downstream** of the ramp tile, along the deck edge, capped at 8.
-  ``U`` = same, **upstream**, capped at 2.
-  A tile is "free" if it has no air claim in ``[0, zDeck+1]`` and no ground object taller than the ramp would be there (the ramp is lowest far from the deck, so this is usually satisfied). Interchange templates, other ramps' outriggers and the map edge count as occupied.
-  ON ramps use ``D`` for the climb and the merge (vehicles must arrive at the deck heading with traffic) and ``U`` only for the hairpin. OFF ramps are the exact mirror: swap upstream/downstream.

**Step 3 — pick the ramp form.** (ON ramp described; OFF is mirrored.)

======================= ================================================================================================================================================================================================= ===================== ======= ========================================================================
``D``                   Form                                                                                                                                                                                              Climb path            Grade   Merge
======================= ================================================================================================================================================================================================= ===================== ======= ========================================================================
≥ 6                     **Parallel** — climb over the first ``min(D − 2, 6)`` downstream tiles, 1-tile offset from the parapet, turn in at the last one                                                                   4–6 tiles + ramp tile 6–8 %   Outrigger over the next 2 tiles + 1-tile taper (or aux lane, see Step 4)
4–5                     **Short parallel** — climb over ``D − 2`` tiles, compressed                                                                                                                                       2–3 tiles + ramp tile 12–15 % Outrigger over the remaining 2 tiles, no taper if an interchange follows
2–3 and ``U ≥ 1``       **Hairpin** — climb west over one upstream tile, 180° turn (R = 5.5 m), climb back east across the top of the ramp tile, join the deck at the ramp tile                                           ~50 m                 ~15 %   Outrigger over the ``D`` downstream tiles
2–3 and ``U = 0``, or 1 **Helix + turn-in** (the "one extra tile in air" case) — 1.25-turn spiral in the ramp tile (R = 6 m centreline; see *Helix geometry* below), climb-out and turn-in over the first downstream tile 47 + 15 ≈ 62 m        ~12 %   Outrigger over what's left of ``D``; aux lane or shoulder if none
0                       **Pure helix** — 1.25 turns in the ramp tile, joining the deck edge at the ramp tile itself                                                                                                       ~47 m                 ~16 %   Shoulder merge, or the downstream interchange's aux lane (Step 4)
======================= ================================================================================================================================================================================================= ===================== ======= ========================================================================

**Helix geometry (surface ramps only — a game compromise; interchange connectors in 7.4 never use it).** A helix is a circular ramp of centreline radius ``R`` that gains ``rise`` metres while turning ``n`` full turns plus a net heading change ``θ``.

-  **Turn count is set by vertical clearance, not grade.** Where the spiral passes over itself, the two deck levels need ≥ 5.0 m clear (4.5 m vehicle + 0.5 m deck). So ``n = floor(rise / 5.0)`` full turns at most: for ``rise ≈ 7.4–7.5 m`` that is exactly **one** full turn. With the 90° net turn a deck-to-deck or surface-to-deck helix is therefore **1.25 turns**, no more and no less (0.25 turns
   would be a 9 m path at 80 %).
-  **Path and grade**: ``L = 2πR × 1.25``; at ``R = 6 m``, ``L ≈ 47 m``, grade ≈ 16 % for 7.5 m. Larger ``R`` helps linearly but the tile bounds it: the outer parapet must stay inside the tile, so ``R + 2.5 (lane half-width + parapet) + c ≤ tile``, where ``c`` is the centre offset below.
-  **The ramp is the outer lane.** (This rule also governs every interchange connector in 7.4.) A diverge is a *split of the deck's outer lane*, not a structure beside the deck: at the gore the outer lane becomes an option lane (through or exit, marked with a split arrow), the ramp lane peels away across the outer shoulder and out through an opening in the parapet, and the through deck keeps its 3
   lanes. A merge is the reverse: the ramp lane arrives alongside the outer shoulder and yields into the outer lane. Every helix, loop and direct connector is constructed from the **outer lane centreline** — 4.75 m inside the parapet line (0.5 parapet + 2.4 shoulder + 1.85 half-lane) on the baked deck.
-  **Tangent construction** (this is what places the helix in its tile): the entry tangent is the lower deck's outer-lane centreline; the exit tangent is the line the ramp must reach at the top (for a surface ramp: 2.5 m outside the deck's parapet, from where it merges). The circle is tangent to both: centre at ``R`` from the outer-lane centreline, i.e. ``R − 4.75`` m **outside** the lower deck's
   parapet (1.25 m at R = 6), and ``2.5 + R`` = 8.5 m from the upper deck's parapet. The first quarter-turn therefore lies over the lower deck's own outer lane and shoulder — it *is* the gore: deck-level pavement, the parapet interrupted, the shoulder ending at a hatched wedge and a crash cushion. The exit straight runs ``tile − 1.25`` ≈ 13.7 m along the upper deck's outside, 2.5 m off its
   parapet, at the upper level.
-  **Clearance checks that bound the circle**: (a) no part of the spiral below ``upperLevel`` may lie within the upper deck's footprint (its underside is only 5.0 m above the lower deck), so the circle's near edge stays ≥ 2.5 m outside the upper deck's parapet — that is the 8.5 m; (b) the spiral's second pass over its own gore, one full turn later, is at ≈ 5.9 m and needs ≥ 5.0 m clear over the
   deck surface: 5.9 − 0.5 (ramp deck) = 5.4 m, OK — this is the only place it is permitted over the lower deck; (c) the outer parapet of the spiral stays inside the tile: ``(R − 4.75) + R + 2.5 ≤ tile`` on the lower-deck axis and ``8.5 + R + 2.5 ≤ tile`` on the upper-deck axis — at a 15 m tile, ``R ≤ 6.0``. So ``R = 6`` is the maximum, not a choice.
-  **Merge at the top.** The exit straight ends at the block edge beside the upper deck's arm; the ramp lane runs along the outside of the parapet and merges into the outer lane through an opening, using the 2.4 m shoulder over the next tile(s) as the acceleration lane: a YIELD-controlled merge if no free air exists for an outrigger (7.3 Step 4), a proper outrigger merge lane if it does. Lane
   balance on the receiving deck is preserved either way: the ramp joins the outer lane, it doesn't add one.
-  **Lane balance at the gore.** With the fixed deck there is no added exit lane, so the outer lane is an option lane and the through count stays 3. Where free air exists upstream of the gore, the standard "lanes out = lanes in + 1" form applies instead: an outrigger adds a dedicated exit lane over 2 tiles (wide-dotted line, "EXIT ONLY" arrow), and the outer lane stays through-only.
-  **Turn direction** is that of the movement: right turns spiral clockwise in plan (right-hand traffic), so the entry tangent is on the approach's right side and the exit tangent on the receiving deck's right side. The single quadrant/tile satisfying both is the one the movement uses.
-  **Profile**: entry straight flat, then a constant grade around the spiral, then flat exit; 3 m vertical curves at each change. Superelevate 6 % toward the centre drum.
-  **Look**: a 4 m concrete drum in the middle carries the inner edge; the outer edge is a parapet with a light every 90°; chevron signs at the entry; 15–20 km/h advisory.

Rules that make the forms look right:

-  Helix/hairpin loops always turn *toward* the deck (the top of the spiral faces the deck edge) so the last quarter-turn becomes the turn-in.
-  Inside of a helix: a circular retaining wall/column drum (concrete, 4 m diameter) to hide the tight geometry; outside: parapet with lighting on the outer face every 90°.
-  Vertical curve of 5 m at both ends of any ramp profile so the surface junction and the turn-in aren't creased.
-  A ramp at 15 % gets a 20 km/h advisory sign, one at 11 % a 30 km/h; the ramp meter (urban) sits at the bottom of the helix on the ground tile.
-  OFF ramps prefer the parallel forms more strongly (a car leaving at 100 km/h needs deceleration length): if fewer than 3 upstream tiles are free, the deceleration lane is provided on the deck by Step 4, and the helix does the rest.

**Step 4 — the merge/diverge lane lives on the deck, never in the ramp tile.** The deck is 2 tiles wide with a 2.4 m outer shoulder; an acceleration lane needs 3.7 m. So the deck grows a one-lane **outrigger** (air claim on the tile beside the deck) for the merge:

-  Downstream of an ON ramp: outrigger for the tiles left over after the climb (up to 4) then a 1-tile taper. If none are left, the shoulder itself is striped as a very short merge (the honest-but-ugly fallback) and a "MERGE" sign is placed on the deck 1 tile upstream.
-  **When the next thing downstream is an interchange or an OFF ramp** (the "interchange right after" case — *interchange* here and throughout 7.3 means a 7.4 highway-to-highway T or X template): do *not* taper. Every 7.4 template begins each approach with a **right-side diverge** — the right-turn ramp, or the flyover/loop connector for the left turn, which also leaves to the right — so there is
   always a diverge for the aux lane to feed. The outrigger continues as an **auxiliary lane** straight into the interchange's own diverge/outrigger — the ON-ramp lane becomes the "EXIT ONLY" lane for the next exit. This is standard freeway design for close spacing and needs no extra tiles, because the interchange already has that air claimed. The only additions are a wide-dotted lane line for the
   whole aux lane and an "EXIT ONLY" panel on the interchange's advance gantry.
-  Upstream of an OFF ramp: the mirror — outrigger deceleration lane over the tiles left over after the descent; if an ON ramp or interchange merge is immediately upstream, it's the same aux lane seen from the other end.
-  Outrigger tiles are air claims only; they never fail on ground content. They can fail on a competing air claim — then fall back to the shoulder-merge and warn.

**Step 5 — surface end.** The ground tile's far edge meets the surface road as a T (yield/stop/signal per 3.4), with the "FREEWAY ENTRANCE" / shield sign, and "DO NOT ENTER — WRONG WAY" facing the road on OFF ramps. If the surface road is a LOCAL, warn (real ramps land on collectors and up) but render.

**What the renderer stores.** Nothing new in the ramp data: the form, air claims, and outrigger are all derived on each edit from ``D``/``U`` and cached on the deck segments. Deleting the interchange next door will silently upgrade the ramp from helix to parallel on the next recompute — that's the intended behaviour, not a bug.

**Surface interchange types** (pairs of ramps at one cross street) are unchanged in kind — diamond, SPUI, parclo, folded diamond, frontage-road slips — but each ramp of the pair is rendered independently by the steps above, so a diamond whose ramps have 6 free tiles gets parallel ramps and one squeezed against an interchange gets a helix on that corner. Constraints kept: gore spacing ≥ 3 segments;
ON→OFF within 4 segments = aux lane; no touchdown within 1 tile of a rail crossing.

7.4 Highway-to-highway interchanges (X and T)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Reference forms follow AASHTO's *Policy on Geometric Design of Highways and Streets* (the set summarised at transportgeography.org: trumpet, three-way/directional T, single-quadrant, diamond, SPUI, cloverleaf, partial cloverleaf, stack). Diamond, SPUI and parclo are *service* interchanges (freeway ↔ surface road) and are handled by ramp pairs in 7.3; the single-quadrant type needs surface signals
and is not generated between two raised highways. This section is about *system* interchanges between two raised 2×2 highways: trumpet and directional T at a T; cloverleaf, partial cloverleaf, turbine and stack at an X.

**7.4.1 The connector primitive — every ramp in every form is this**

A connector carries one turning movement from the outer lane of deck A to the outer lane of deck B. It is always built from three pieces, and *the length lives in the runs, not in the curve*:

::

   connector = run_A  +  curve  +  run_B

-  ``run_A``: leaves deck A's outer lane at a gore (option lane split, or a dedicated exit lane on an outrigger where air permits), and runs **parallel to deck A**, 1 tile outboard of A's parapet, over the tiles beside A's arm. This is where it changes elevation.
-  ``curve``: a circular arc (R by type, below) in the quadrant between A and B. Superelevated, mostly constant grade or flat.
-  ``run_B``: runs parallel to deck B, 1 tile outboard, finishes the elevation change, and merges into B's outer lane (outrigger acceleration lane if air permits, otherwise a yield merge over B's shoulder).

Elevation change per connector: between a lower and an upper deck it is one level, **Δ = 7.4 m**; a flyover that crosses *above* the upper deck needs two levels from the lower deck (14.8 m) or one from the upper. Length needed at the design grade: ``L = Δ / g`` — 7.4 m at 6 % = 125 m ≈ 8 tiles of run; at 8 % ≈ 6 tiles. A loop or a large-radius curve contributes its arc length to ``L``.

Connector types and radii (15 m tiles):

================================ ======================== ================================================================================== ===== ===== ======================================================================
Type                             Movement                 Curve                                                                              Speed Lanes Tiles of run needed (``run_A + run_B``, at 8 %)
================================ ======================== ================================================================================== ===== ===== ======================================================================
**Outer (right-turn) connector** right                    quarter-circle, R = 2–4 tiles (30–60 m)                                            40–60 1     6 − (arc length ÷ 15), min 2
**Loop**                         left, via 270°           R = 2–3 tiles (30–45 m) centreline                                                 30–40 1     0–2 — a 270° loop at R = 2 tiles is 94 m, nearly the whole Δ by itself
**Semi-directional flyover**     left                     leaves right, curves ~90° at R = 4–6 tiles (60–90 m), crosses over the interchange 60–80 1–2   10–12 (two levels: 14.8 m at 8 %)
**Directional flyover**          left, leaves on the left R = 6+ tiles                                                                       80    2     10–12
================================ ======================== ================================================================================== ===== ===== ======================================================================

**Air needed, in tiles.** For each arm of the crossing define ``L_side`` = number of consecutive free-air tiles in the 1-tile strip outboard of that arm, on the side in question, counted from the 4×4 block outward (the 7.3 free-air test; cap 12). For each quadrant define ``Q`` = side of the largest free square anchored at the quadrant's corner tile (cap 8). A connector for a movement A→B via
quadrant q is buildable iff:

::

   Q(q) ≥ Q_min(type)   and   L_A(right side) + L_B(right side) ≥ runs_needed(type, Q)

with ``Q_min`` = 2 for an outer connector (a 30 m quadrant holds a R = 2-tile quarter-circle), 4 for a loop (R = 2 tiles + width), 5 for a semi-directional flyover. Larger ``Q`` lets the curve absorb more of ``L``, reducing the runs needed; the renderer picks the largest R that fits.

**7.4.2 The reference forms, in tiles**

================================= ======== =========================== ===================================================================== ===================================================================================== ========================================================================================== ==================================================================================
Form                              Junction Levels / overpasses         Left turns by                                                         Footprint (land + air)                                                                Weaving                                                                                    When selected
================================= ======== =========================== ===================================================================== ===================================================================================== ========================================================================================== ==================================================================================
**Trumpet**                       T        2 levels, 1 overpass        one loop (the stem's or the through's) + one semi-directional flyover loop quadrant 5×5, flyover across the through deck, runs of 6–8 tiles along both legs none on mainline                                                                           T with ``Q ≥ 5`` in one stem-side quadrant
**Directional T (three-way)**     T        3 levels, 3 overpasses      two semi-directional flyovers                                         6×6 land + 10–12 tile runs each leg                                                   none                                                                                       T with ``Q ≥ 5`` nowhere but long ``L`` on all legs
**Cloverleaf**                    X        2 levels, 1 overpass        four loops                                                            4 quadrants of ``Q ≥ 5`` → ~12×12; +1 tile each side for C-D roads                    between adjacent loops: only the crossing width (~2 segments) — add C-D roads if ``L ≥ 4`` all four ``Q ≥ 5``
**Partial cloverleaf (parclo)**   X        2 levels                    loops in 2 quadrants, others unbuilt or via outer connectors          2 quadrants ``Q ≥ 5``                                                                 in the loop pair                                                                           two adjacent or two opposite quadrants ``Q ≥ 5``
**Turbine**                       X        3 levels                    four semi-directional flyovers spiralling around the centre           ~10×10 land, runs 10–12 tiles                                                         none                                                                                       four ``Q ≥ 5`` and long ``L`` on all arms; preferred over cloverleaf when both fit
**Stack**                         X        4 levels, ramps up to ~28 m four semi-directional flyovers crossing at the centre                 ~8×8 land but runs of 12–16 tiles per leg                                             none                                                                                       ``Q ≥ 3`` and ``L ≥ 12`` on every arm; the landmark option
**Compact in-band stack (7.4.6)** X or T   up to 4 levels              flyovers inside the bands                                             the 4×4 block + 14–20 tiles of each leg's own band                                    none                                                                                       **default** — needs no outboard air
**Grade separation only**         X or T   2 levels, 1 overpass        none                                                                  the 4×4 block                                                                         —                                                                                          the floor: legs too short for 7.4.6
================================= ======== =========================== ===================================================================== ===================================================================================== ========================================================================================== ==================================================================================

Mixed forms are normal: the renderer starts from the in-band stack of 7.4.6 and upgrades each of the 8 (X) or 6 (T) movements independently by 7.4.1 where outboard air exists, and *names* the result afterwards (four loops → cloverleaf; two loops + two flyovers → parclo-plus, etc.). A movement that cannot be built is simply absent: the sim removes it from routing and the advance gantry gets a "NO
ACCESS TO " panel. Through movements are never affected.

**7.4.3 Which quadrant, which side.** With right-hand traffic every connector leaves deck A on its right and joins deck B on its right. The quadrant for a right turn A→B is the one on A's right *before* the crossing (which is also on B's right after it): with an E–W lower deck and N–S upper, SW = EB→SB, SE = NB→EB, NE = WB→NB, NW = SB→WB. A loop for the left turn A→B sits in the quadrant on A's
right *after* the crossing (EB→NB loops in SE). A semi-directional flyover for A→B leaves in A's right-turn quadrant, curves over the crossing, and lands in B's right-side strip. Left-hand exits are not generated.

**7.4.4 Levels and the mainline profile.** Lower deck L1 ≈ 7.5 m over ground, upper L2 = L1 + 7.4, flyover level L3 = L2 + 7.4, stack L4 = L3 + 7.4. The upper deck climbs to L2 on its own approach segments at ≤ 4 % (3 segments) or ≤ 6 % (2 segments); if a neighbouring interchange leaves fewer, warn and steepen to 8 %. Mainline through lanes stay 3+3 at every level; the only lane arithmetic is at
gores and merges: with free air, "lanes out = lanes in + 1" via a 2-tile outrigger exit lane; with none, the outer lane is an option lane.

**7.4.5 Selection procedure (per interchange, on every edit)**

1. Compute ``Q`` for each quadrant and ``L`` for each arm side.
2. For each turning movement, in priority order (right turns first, then left turns), pick the cheapest connector type that satisfies 7.4.1 with the air available, allocate its tiles (air claims), and mark them used. Right turns take the quadrant curve first because loops and flyovers need the outer connector's position free at the gore.
3. If both a loop and a flyover fit for a left turn, prefer the loop when ``Q ≥ 5`` and the arm's ``L < 10``, else the flyover (avoids the cloverleaf weave when the runs exist).
4. Where two loops would be adjacent on the same deck (EB's loop entry followed by WB's… i.e. a cloverleaf pair) and ``L ≥ 4`` on that side, build a collector–distributor road: a 1-tile ramp parallel to the mainline connecting the two loop terminals, with the weave moved onto it.
5. Name the form from the movements built; emit gantries (two advance per exit, exit-direction at the gore, pull-through for the mainline), crash cushions at gores, barriers continuous through the runs.
6. Movements not built: sign them as unavailable and drop them from routing. If none are built, the interchange is a grade separation only — one overpass and nothing else.

**Right-side exits, always.** Every turning movement in every form leaves the mainline on the right — the outer connector directly, the loop or semi-directional flyover by first peeling off right. This is what lets an on-ramp's aux lane (7.3, Step 4) always find a diverge to run into.

**Shared rules for every interchange**

-  Gore spacing on the mainline: exit gores ≥ 3 segments apart; an interchange's last entrance and the next interchange's first exit ≥ 6 segments apart (else insert a C-D road).
-  Lane balance: at a diverge, lanes out = lanes in + 1 where an outrigger exists; at a merge, lanes in = lanes out + 1. Lane drops happen only on the outer lane, after a merge, with a 2-segment taper and a "LANE ENDS" gantry sign.
-  Signing: 2 gantries before each exit (½ and ¼ mile equivalent: 4 and 2 segments), the exit-direction gantry at the gore, a pull-through sign for the mainline on the same gantry, route shields and cardinal directions ("NORTH 99 / Vancouver"), then a confirmation "shield + direction" 1 segment after each merge.
-  Piers under a crossing: the L2 highway's piers land in the L1 highway's median (a single tall column between the two barriers) or outside its parapets — never over a lane. Flyover piers sit in ramp gores or in the L0 verge.
-  Barriers are continuous; at a diverge the gore gets a crash cushion (yellow sand barrels on old, a steel attenuator on new); at a merge the two parapets join with a short flare.
-  Under-deck zones inside an interchange become a single "interchange land" lot: grass, stormwater pond, maintenance access, no buildings.
-  Curve rules from 3.10 apply with ``R_min``: mainline 3 tiles (45 m — already generous), outer connector 2 tiles, loop 2 tiles centreline, 2-lane connector 3 tiles. Superelevate everything at 4–6 %.

**7.4.6 The tight-constraint form: the compact in-band stack (default)**

7.4.1–7.4.5 describe what to build when there is free air outboard of the decks. The data guarantees none of that. What it *does* guarantee is: the 4×4 block, each leg's own 2-wide band for as many segments as it runs, and the air column above those tiles (and below the upper deck's). The one interchange family that lives entirely inside that is a **stack whose connectors run within their own
highway's band** — the outer lane at deck level, then a viaduct directly above the outer-lane strip — and cross the 4×4 as flyovers over the crossing. It is the default; the outboard forms of 7.4.2 are upgrades applied per movement when outboard air happens to exist.

*Slots.* Inside each 15 m half-deck the outer lane + outer shoulder + parapet form a 6.6 m **outer strip**. The strip has three usable levels: S0 = the deck surface (the outer lane itself), S1 = +7.4 m above it, S2 = +14.8 m. Under the upper deck's strip there is a further slot at its −7.4 m (= the lower deck's level), 5.0 m clear beneath the box girder. Connectors on S1/S2 are 3.0 m lanes with
0.4 m barriers on a 3.8 m viaduct; two fit side by side in the strip (7.6 m > 6.6 m only by borrowing 1 m of lane 2's line — accept, and mark the through lanes 3.5 m for that stretch). Piers land on the parapet line.

*Lane arithmetic (the honest cost).* On every approach the outer lane becomes an **exit-only lane** (split arrow, wide-dotted line) and the through deck is **2 lanes** from there to the far side of the interchange, where the entering connector becomes the new outer lane and through returns to 3. This is exactly what a cloverleaf or stack does with a 3-lane deck and no auxiliary lanes; nothing is
invented.

*Approach leg (tiles counted back from the 4×4 edge, 8 % grades, Δ = 7.4 m per level):*

1. ``−13``: split arrow in the outer lane; through drops to 2 lanes.
2. ``−13 → −12``: the exit lane widens to two 3.0 m connector lanes over the outer strip at deck level — inboard = right-turn connector, outboard = left-turn connector. Gore with a nose and a crash cushion at ``−12``.
3. ``−12 → 0``: the **left connector** (outboard) climbs 14.8 m to S2 = the flyover level. It is above 5.0 m from ``−8`` on.
4. ``−6 → 0``: the **right connector** (inboard) climbs 7.4 m to S1, which is the *upper deck's* level when the leg is the lower deck. (When the leg is the upper deck, the right connector instead **descends** 7.4 m in the slot under its own strip, to the lower deck's level.)
5. Beneath the climbing connectors the strip is dead width (no shoulder); "NO SHOULDER" signing, piers every tile.

*In the 4×4 block:*

-  **Right turns** arrive at the receiving deck's level, on the inboard slot line (4.75 m inside the parapet), and turn 90° through the **corner tile**: a quarter-circle tangent to the two outer-lane lines, centre at (4.75 + R) inside each parapet line measured toward the quadrant — with a 15 m corner tile, ``R = 15 m`` centreline, 30 km/h, 1 lane. It exits onto the receiving deck's outer strip at
   that deck's level — and since that deck's own outer lane has already left as *its* right-turn exit, the arriving connector simply *is* the new outer lane from the block edge onward (merge over 2 tiles, wide-dotted, then solid).
-  **Left turns** cross the block as **quarter-circle flyovers** from the approach's outboard slot to the receiving leg's outboard slot, tangent to both outboard-strip centrelines (13.4 m from the crossing centre: the 15 m half-deck less 1.65 m). The midpoint of such an arc lies ``13.4 − 0.293 R`` from the centre, on the arc's own side — so at R ≈ 45 m the arc passes through the centre, and two
   arcs of a point-symmetric pair at the same level would collide there. **The pair radius is therefore fixed at R = 60 m**: each arc passes 4.2 m *beyond* the centre onto the far side, the two arcs of a pair are 8.4 m apart at the middle and ≥ 17 m apart near the corner tiles, and the tangent points fall 17 m (1.1 tiles) outside the block on each leg's own outboard strip — still inside the band.
   Smaller radii (≤ 32 m) also avoid the centre but bring the pair within 2.5 m beside the corner tiles, so they are not used. Speed 55 km/h, 1 lane. Assignment: the two lower-deck legs' left connectors (EB→NB, WB→SB) share L3; the two upper-deck legs' (NB→WB, SB→EB) share L4; the pairs cross each other 7.4 m apart. The block therefore carries L1, L2, L3, L4 — a four-level stack in a 60 m square,
   the flyovers overhanging the block edges by a tile on each leg.
-  **Departure leg**: a left flyover lands on the outboard slot of the receiving leg and descends to that deck's level over 6 tiles (from one level up) or 18 tiles (from two levels up: an upper-leg flyover at L4 landing on a lower-deck leg); the right connector is already at deck level. At ``+2`` the two entering lanes merge (left connector yields to right) into one, which is the deck's new outer
   lane; through is 3 lanes again from ``+3``. Gantries: "EXIT ONLY" over the outer lane at ``−13`` and ``−9``; exit-direction gantry at ``−12`` with a two-way split panel; pull-through for the mainline.

*What each leg needs.* ``N_leg`` = number of tiles of the leg's own band available before the next node. Right turn only: ``N ≥ 8`` (6 climb + 2 gore). Right + left: ``N ≥ 14``. Departure with a 2-level descent: ``N ≥ 20``. When ``N`` is short, degrade in this order, per leg: (1) steepen to 10 % with a warning; (2) drop the left-turn movements on that leg; (3) drop the right turns; (4) grade
separation only for that leg's movements. The other legs keep whatever they can build — mixed results are normal.

*Right-turn note.* R = 15 m at 30 km/h is tight for a system interchange; it is what a 15 m corner tile allows and is comparable to urban loop ramps. If the receiving leg's outboard air happens to be free (7.3 free-air test), the renderer widens the curve into it (R = 30 m, Q-style) — an upgrade, never a requirement.

*Structure.* Connector viaducts are 3.8 m wide single-cell boxes on single columns at the parapet line, 1 per tile; the L3/L4 flyovers over the block sit on 2-column bents planted in the lower deck's median (between barriers) and in the corner tiles. Barriers continuous; light poles on the outer barrier of the highest flyover; the under-slot connector beneath the upper deck has its own parapet and
lighting and reads as a tunnel-like gallery — the recognisable look of an urban stacked junction.

*Why this and not a helix or a corner-tile ramp.* A 7.4 m level change needs ~95 m of path at 8 %; the band provides it along the leg, which is where real connectors put it. Corner tiles handle only the flat 90° turn, which is all a 15 m tile can honestly do.

**7.4.7 Ramps inside an interchange's runs — one strip, one occupant**

The 2-tile band has a single outer strip per side, and an interchange's connectors occupy it — at deck level and in the air above — from ``−13`` on the approach to ``+3`` (right-turn side) or ``+18`` (left-turn side) on the departure. A 1×1 ON or OFF ramp tile may sit adjacent to any tile of that range; the data doesn't prevent it. The strip cannot serve both, so: **the interchange owns the strip;
the ramp adapts.** Precedence, per ramp, on every recompute:

1. **Slide the merge out of the run** (needs outboard air). The ramp's landing tile stays where it is, but its parallel run (7.3 form) continues alongside the deck — *outside* the parapet, in outboard air — until it reaches a tile where lane 3 exists: upstream of ``−14`` for an ON ramp, downstream of ``+3`` for an OFF ramp. There it merges/diverges normally and its traffic can go anywhere. This is
   the real-world answer (ramps are kept out of the interchange's influence area) and it is used whenever the 7.3 free-air scan finds the outboard tiles free from the ramp tile to that point.
2. **Ramp-to-ramp** (no outboard air). The ramp joins the connector system instead of the mainline. It arrives from outside the parapet, so the lane it meets first is the **outboard slot = the left-turn connector**:

   -  ON ramp at tile ``t`` on the approach: it merges into the left connector at the connector's elevation there, ``h(t) = 14.8 m × (t + 12) / 12`` for ``−12 ≤ t ≤ 0`` (0 for ``t < −12``). The ramp's own form is computed by 7.3 with ``rise = zDeck + h(t)``; the helix turn count follows the 5 m-per-turn rule (a 20 m rise is a 4.25-turn helix — legal, ugly, and honestly what the data asked for).
      Its traffic can use **only the left-turn movement** of that leg — and, if ``t ≤ −12`` where both connector lanes are still at deck level, the right turn as well via a lane change. Sign at the ramp entrance: "TO ONLY — NO ACCESS TO ". The sim removes the through movement for that ramp.
   -  OFF ramp at tile ``t`` on the departure: mirror — it can only leave from the descending left connector, so only vehicles that arrived by that left turn can use it, until ``+3`` where lane 3 is restored and a normal exit is possible. Sign on the connector: "EXIT to ".
   -  A ramp tile at ``−5 … −1`` meets the left connector at 8–14 m and the right connector at 0–7 m directly beneath it; only the left is reachable from outside. A ramp tile at ``−12 … −6`` meets both lanes at deck level (weave permitted, 2-tile minimum weaving length or a "NO LANE CHANGE" solid line).

3. **Infeasible** (rise beyond what any 7.3 form can build, or the ramp tile is inside the 4×4 block itself): the ramp renders as a stub with an "END" barricade and is removed from routing; validation warns "ramp inside interchange".

*What the 2×2 segment renders.* Nothing changes in data. The segment adjacent to the ramp tile renders the ramp's merge piece; the connectors are rendered as per-segment pieces along the leg (each 2×2 segment holds the slice of every connector crossing it, with continuity of profile and edge across segment boundaries), so a ramp joining a connector mid-leg is just an extra gore on that segment's
slice. Priority 1's outboard run is an air claim on the tiles beside the segments it passes, exactly as in 7.3.

*Through-lane count.* Priority 2 never adds a lane to the deck: lane 3 is already gone from ``−13``, and the ramp-to-ramp merge happens on the connector, so the 2-lane through section is unaffected. Priority 1 merges into lane 3 upstream of the split, where 3 lanes exist.

7.5 Placement and data model
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

   HighwaySegment {                 // one 2×2 block
     elevation, deckType: BOX | PLATE
     connections: 4-bit mask over the 4 neighbouring 2×2 blocks (N/E/S/W) +
                  diagonal blocks for 45° runs (same band-clipping as 3.9,
                  band width = 2 tiles)
     shape:  STRAIGHT | CURVE_R{n} | DIAG | RAMP_OUTRIGGER_{L|R} | LANE_DROP | INTERCHANGE
     outriggers: [{ side, from, to, kind: MERGE | DIVERGE | AUX }]
     gantries: [...]                // derived
   }

   RampTile {                       // 1×1
     kind: ON | OFF | CONNECTOR
     lanes: 1 | 2
     zStart, zEnd                   // per tile, from the profile
     airClaims: [{ tile, zMin, zMax }]   // tiles this tile's deck passes over
     groundClaim: bool              // touchdown or pier tile
   }

   Interchange {
     kind: DIAMOND | SPUI | PARCLO | TRUMPET | DIRECTIONAL_T | TURBINE | CLOVERLEAF | STACK
     origin, rotation, mirror
     template → emits HighwaySegments, RampTiles, air claims, piers, gantries
   }

Placement flow: the player draws highway blocks (2×2 snapping) and ramps as 1×1 paths from a deck edge to a surface road. On each edit: (1) classify blocks like road tiles (the corner/curve rules at 2-tile band width; a 90° corner needs a 3-block arc, so an L-shaped draw auto-expands); (2) for each ramp path, build the profile from ``zDeck`` to 0 at ≤ max grade — if the path is too short, **extend
it in air** along the same heading and show the extension ghosted until the player confirms or redraws; (3) compute outriggers on the adjacent highway blocks; (4) compute air claims and reject if a claim intersects an existing building above ``zMin``; (5) place piers, gantries, signs.

7.6 Validation (raised highways)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  Reject: ramp grade > 16% (nothing fits, not even a helix — only possible if the ramp tile isn't adjacent to a deck edge); ramp with no reachable deck edge; ramp touching down on a LOCAL or ALLEY; two mainline gores < 2 segments apart; a pier landing in a lane, a rail track, or a crossing; interchange template overlapping another interchange's air claims; highway block adjacent to a highway
   block at a different elevation without a transition segment.
-  Warn: ramp form is hairpin or helix (grade > 10%); shoulder-merge fallback used; weave < 4 segments without an aux lane; exit without 2 advance gantries (map edge); noise wall missing next to residential; loop ramp on a route flagged for trucks.

7.7 Rendering notes
~~~~~~~~~~~~~~~~~~~

-  Shadows: bake a soft deck shadow decal onto the ground tiles under every air claim (offset by sun angle if you have one); it's the cheapest cue that something is raised.
-  Underside detail: box-girder soffit is flat with drain pipes and utility conduits; plate-girder soffit shows the girder lines — a single normal map per deck type.
-  Ramps use the same parapet/barrier material as the deck but with a smaller profile; ramp lighting is pole-on-parapet every 40 m on the outer side only.
-  Interchanges are the one place a 2×2 grid shows — hide it with the curve rules (nothing on an interchange is orthogonal) and by letting air claims be arbitrary polygons rather than tile squares.
