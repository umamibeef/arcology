# Arcology — the drawing: the design

How the shaped road network is laid as surfaces.  This page holds the design and nothing about where the
build stands.  A difference between the build and this page is a breach, written on the status page,
never here as a fact.

## What it is for

The DRAWING lays what the network holds (`plans/network.md`) as closed solids the camera sees: the streets
and their sidewalks, the junctions, the railways with their gravel and track, the crossings between them,
and the ground graded to meet them.  It reads the network and decides nothing the network holds.

* **It reads the store, never a plan.**  A road's line and height are layer 3's.  A lane's width and its
  place across its road, `t`, are layer 2's.  A junction's outline is layer 3's.  Where the drawing needs a
  fact the store does not answer, that is a finding about the store, and it is answered there.
* **It places nothing across a road by a number of its own.**  Every border across a road, an edge, a
  sidewalk's, a median's, a track's, is a lane's border at that `s`.  The drawing's own numbers shape only
  what lies inside a lane's span or stands on it: the gravel's bed and its height, a sleeper, a rail, a
  buffer stop, how far a surface sinks into the ground.  They are named in `geo_defaults`.
* **It changes nothing in the network.**  It adds no road, no lane and no link, and it moves no line.

## Lanes are markings, not cuts

A lane is where traffic runs and where a marking goes.  It is not how a surface is built.  So a road's
surface is cut only where its MATERIAL changes across it: a sidewalk's concrete, a lane's asphalt, a
median, a railway's gravel.  The border between two lanes of one material is a marking, laid on the
surface as a layer of its own, never a seam through it.

## A road

* **One road, one slab.**  A road is one closed solid along its line: a top, a bottom under it, its two
  sides and its two ends.  Across its top it is cut into STRIPS where its material changes, each strip a
  lane's span or the span of lanes of one material side by side.
* **Its edges follow its lanes.**  At each `s` a strip runs from one lane border to another, so where a
  road's lanes ease to a new width, its surface eases with them, with nothing of its own.
* **A street across a level meet** is the meet's connecting roads that run it: the lanes of each way and
  the sidewalk of each side, read together across the street's line.

## A junction

* **A junction's surface is its outline**, layer 3's boundary, filled whole.  Its paved area is its own, as
  OpenDRIVE's junction area is: its connecting roads' lanes are the paths traffic takes across it and the
  markings laid on it, not the pieces it is made of.
* **Its corners are its corner sidewalks**, laid where their lanes lie, along the outline.  A corner between
  two arms that both carry sidewalks has one.  A corner beside an arm with no sidewalk, a ramp's road or a
  highway's, has none: there the asphalt runs to the outline, so the arm's whole mouth and the corner's
  rounding are open to turn through, and the street's sidewalk ends at its own mouth.
* **Each arm meets the junction edge to edge**, at its mouth, from the same lane borders.

## A level crossing

* **Each family goes straight on across it**, drawn whole as its own connecting roads.
* **A railway has priority over a street.**  The railway is drawn whole, gravel and track.  The street gives
  way where the railway's gravel crosses it: it ends on the gravel's foot, the line where the gravel meets
  the ground, whatever the railway's curve.
* **A meet where a family turns inside it**, more than two of its roads meeting there, is a junction of
  that family, not a crossing.

## A railway

* **Its gravel** is a pile across its lanes.  Its foot is the outer border of its `border` lanes, and its
  bed, the flat top the sleepers lie on, lies inside them.  Under each track the gravel is darker.
* **Its two tracks** each lie halfway across a `rail` lane: sleepers across it, two rails along it, the
  rails a gauge apart.
* **A turnout's gravel** is one surface, the union of the gravel of every way through it.  Its tracks are
  laid straightest first, and a track laid later gives way to the rails and sleepers already there.

## A highway

* **One highway road, one deck.**  A highway road of the band, a bundle or a strand of an interchange
  among them, is one closed slab along its line at its height: a top, a bottom `deck_thick` under it, its
  two sides and its two ends.  Across its top it is cut where its material changes: a `shoulder` lane's
  concrete, the traffic lanes' asphalt, a `median` lane's concrete.
* **Its edges follow its lanes**, as a street's do, so a lane that eases between nothing and its width at a
  split or a join eases the deck with it.
* **A deck's sides and underside are concrete**, lighter than its top, so a raised deck reads as a structure.
  Its top is asphalt where traffic runs, its shoulders grey and its median pale.
* **A highway stands on COLUMNS**: one every `pier_step` along every drawn highway road, an interchange's
  strands among them, where the deck stands `pier_least` over the ground.  A road with a median has one
  under the middle of each way's traffic, and any other road one under its middle.  A column stands on
  the ground and its top meets the deck's underside where that is lowest over it.  It keeps clear of every
  road it would meet below its top, its own included where the road comes back under itself: it moves
  along its road a tenth of a tile at a time, half a tile at most, and is left out where none is clear.
* **A lane is drawn the same wherever it is.**  Every drawn road is swept across its lanes' borders, and each
  lane's top is its type's material: asphalt for a lane traffic drives, grey for a shoulder, pale for a
  median, concrete for a sidewalk.  One asphalt serves every road.  A road whose line stands `pier_least`
  or more over the ground anywhere is a deck, whatever network it is of: `deck_thick` deep, its sides
  and underside concrete, on columns.  Every other road is a street's thin slab.  So a ramp's lane
  looks the same from its street to the highway.
* **A tile's art is not drawn where everything on it is drawn**: a highway unit whose roads all have their
  lines, and a street, a railway or a wire under it that is drawn on its own.  A ramp's tile is the
  highway's: its art goes where the highway's unit it joins is drawn, as the highway's strand or its
  gore's lane runs over it.  A tile of an interchange
  that is a placeholder keeps its art, and the art is the placeholder's model.

## A dead end

* **A road's CAP** runs straight on from the end of its line, along its heading there, to the far side of
  its dead end's outline.
* **A street's cap** is a closed end.  A street of one tile joined to nothing is a stub across its tile,
  along the edges its piece claims, closed at both ends.
* **A highway's cap** is its deck run on, level at the height of its end, each lane and shoulder as it is
  there, and closed by the deck's end.  Its columns stand under it as under the rest of the deck, and the
  dead end's tiles are the highway's: their art is not drawn.  A dead end that only a road of no units
  reaches has no line to run on from.  Its cap is laid from the edge it shares across its unit to the far
  side, straight, with the lanes of that road of no units, at the dead end's height.
* **A cap under a highway** is drawn as any other, and the highway's drawing takes the tile's art away.
* **A railway's cap** carries its gravel and its tracks on to the end, and each track stops at a BUFFER
  STOP: a concrete block across the track, standing on the bed.

## The ground

* **The ground meets every surface**, graded down to it from the land, and lowered under it by the
  surface's sink.  A tile a surface lies over is drawn finely enough to follow it.
* **The ground is never raised to hide a surface**, and a surface never sinks to hide the ground.

## What the drawing keeps

These hold everywhere the drawing lays anything.

* **A surface is drawn in LAYERS, each a step nearer the camera than the one under it, and each painting
  only its own pixels in one colour whatever piece it is of.**  The base surface first (asphalt, ballast, a
  slab's deck), then its markings, then for a railway its ties and then its rails.  Two pieces lying over
  each other then show both of their layers on one bed, rather than one covering the other.  A mark laid as
  a piece of its own, a stop line on a strip, shows whole for the same reason.
* **A depth or height nudge that picks a winner between two things that should both show is a fault.**  It
  hides the fight and breaks the picture.  Where two surfaces overlap, either they are layers of one surface
  (above), or they should not overlap at all and the network is wrong.
* **Two surfaces that are not layers meet edge to edge.**  A margin, a lane drop or a junction fill shares
  the edge of what it meets, from the same numbers, never a sliver of overlap to hide a gap.
* **Paint lies clear of other paint.**  A stop line starts outside the centre line.  Markings that cross are
  one layer and one colour.
* **What is translucent is drawn after everything it lies over**, the ground's own art included (the zone
  fill), or the art paints over it.
* **Something raised is not in the way of what is under it.**  Only a building can block what is raised,
  never a road, a railway or a wire beneath it.  A power line never shapes the ground.
* **The picture is judged by looking at it**, close, at the place a person would look.  A view that matches
  a reference can be broken in both, and a reference made from a broken build records the fault.
* **Everything is named by where it is**, and every piece is bounded.

## The witnesses

* **Every lane link meets in the world** (`--net-check`, `shape_lane_check`): where lanes meet, their
  borders meet, so a surface laid from them has no step.  It holds the store the drawing reads.
* **The mesh is closed and nothing passes through anything** (`--mesh-check`): no free edge, no face
  through another, no two coplanar faces sharing a painter's slot.
