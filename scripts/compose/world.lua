--  world.lua -- the ORDER the world is composed in.
--
--  This is the drive.  The renderer runs a pass and hands it here; what
--  happens in that pass -- which tiles are composed, in what order, and
--  which of the network passes run at all -- is decided below.  There is
--  no loop in C behind it: take this rule away and nothing is drawn, and
--  the build says so rather than reporting an empty city.
--
--  Two passes reach here.  The FIRST lays the networks out and records
--  the surface their corridors want; the SECOND builds the world with
--  those corridors notched into it.  `d.pass` says which.
--
--  The ground first, then the tints over it, then the networks over
--  both: the painter's order the mesh is drawn in.  The tints are a flat
--  quad a hair over the ground, so they follow it; the networks read the
--  shelves the ground was cut to, so they follow both.

arc.rules.world = function (w)
    local d = w:info()
    local n = d.size

    --  The ground.  `w:tile` gathers one tile and hands it over -- or
    --  answers nothing, where an edit's build is keeping the chunk it
    --  lies in.  Composing it is this side's: the shape it is drawn
    --  into is opened here, and arc.rules.tile lays the faces.
    local ground = arc.rules.tile
    for row = 0, n - 1 do
        for col = 0, n - 1 do
            local t = w:tile(col, row)
            if t then
                w:shape("ground", col, row)
                ground(t)
            end
        end
    end
    w:shape() -- the last one: nothing after it nests inside the ground

    --  The zone tints, for the map view.
    local tint = arc.rules.zone_tint
    for row = 0, n - 1 do
        for col = 0, n - 1 do
            local t = w:zone(col, row)
            if t then
                w:shape("zone tint", col, row)
                tint(t)
            end
        end
    end
    w:shape()

    --  The networks.  Underground there are none, and a build that draws
    --  no roads asks for none.  The lane model goes first: a junction
    --  beside a ramp needs to know which deck lane that ramp takes.
    --
    --  The measure fits every segment and settles where each crossing's
    --  two paths actually run.  Only then can the crossings and the
    --  lines be drawn -- asked for any sooner, a crossing would have
    --  only the tile's axes to go on -- and only after those does the
    --  drawing pass lay the junctions and the strips.
    --  Every strip the pipeline lofts is composed HERE, wherever it was
    --  lofted: the slab itself, and over it the fitted line the tuning
    --  window shows.  The loft leaves the strip standing until the shape
    --  is closed, which is the last thing this does.
    local strip, curves = arc.rules.strip, arc.rules.curves
    local function answer(name, a, b)
        local fn = name and arc.rules[name]
        return fn and fn(a, b)
    end
    local function stage(name, o)
        answer(name, o)
    end
    local function lofted()
        --  The loft's own stages, in the order it asks for them: where a
        --  ramp narrows, where the stations sit, what a deck stands on,
        --  and what the strip leaves for the passes that read it.
        stage(w:loft_taper())
        stage(w:loft_profile())
        stage(w:loft_dropped())
        stage(w:loft_works())
        w:loft_record_is(answer(w:loft_record()))
        w:loft_furniture_is(answer(w:loft_furniture()))
        w:loft_recorded()
        local o = w:curves()
        if o and curves then curves(o) end
        o = w:strip()
        if o and strip then strip(o) end
        w:strip_done()
    end

    if d.roads and not d.underground then
        --  Where every family's lanes run, settled before the lane model
        --  is built: the connectors, the paint and the traffic all read
        --  the same answer, so the three cannot part company.
        local lanes = arc.rules.lanes
        for i = 0, w:lane_runs() - 1 do
            local fam, cls = w:lane_run(i)
            w:lane_run_is(i, lanes and lanes(fam, cls))
        end

        --  And where the traffic runs on a family whose traffic stage is
        --  a rule rather than a primitive of the pipeline's own.
        for i = 0, w:traffic_runs() - 1 do
            local rule, cls = w:traffic_run(i)
            if rule then
                local fn = arc.rules[rule]
                if fn then w:traffic_run_is(i, fn(cls)) end
            end
        end



        --  Which way a band is walked from a cell that could start one:
        --  the four readings there are, settled before the walk begins.
        local band_start = arc.rules.band_start
        for back = 0, 1 do
            for on = 0, 1 do
                w:band_start_is(back, on, band_start and band_start{back = back == 1, on = on == 1})
            end
        end

        --  And which way each ramp's taper lies, which follows from the
        --  orientation just settled.
        local ramp_side = arc.rules.ramp_side
        for i = 0, w:ramp_sides() - 1 do
            local r = w:ramp_side(i)
            if r then w:ramp_side_is(i, ramp_side and ramp_side(r)) end
        end

        --  And how a ramp's foot meets the road it lands on: the eight
        --  readings there are, settled before any ramp is read.
        local ramp_fork = arc.rules.ramp_fork
        for straight = 0, 1 do
            for along = 0, 1 do
                for against = 0, 1 do
                    w:ramp_fork_is(straight, along, against,
                                   ramp_fork and ramp_fork{straight = straight == 1,
                                                           along = along == 1,
                                                           against = against == 1})
                end
            end
        end

        --  Every on-ramp tile read once: which side of it the deck lies,
        --  which the road, and which way round it runs.
        local orient = arc.rules.orient
        for i = 0, w:orients() - 1 do
            local o = w:orient(i)
            if o and orient then orient(o) end
        end

        --  One class for each whole segment, settled before any of them
        --  is fitted: how wide a road runs, where its lanes go and what
        --  is painted on it all follow from it.
        local seg_class = arc.rules.seg_class
        for i = 0, w:seg_classes() - 1 do
            local cnt = w:seg_class(i)
            if cnt and seg_class then w:seg_class_is(i, seg_class(cnt)) end
        end

        --  And the path of every one of them, fitted through its own
        --  corridor.  A family whose runs may leave its cells is fitted
        --  twice and the better of the two kept.
        local path, fit_choice = arc.rules.path, arc.rules.fit_choice
        for i = 0, w:fits() - 1 do
            local p = w:fit(i)
            if p and path then path(p) end
            local c = w:fit_done(i)
            if c then w:fit_choice_is(fit_choice and fit_choice(c)) end
        end

        w:lanes()

        --  Two ramps whose tapers face each other share the tiles
        --  between them: the rule says how many each may take.
        local ramp_share = arc.rules.ramp_share
        for i = 0, w:ramp_shares() - 1 do
            local r = w:ramp_share(i)
            if r then w:ramp_share_is(i, ramp_share and ramp_share(r)) end
        end

        w:networks()

        --  Every junction's outline, walked from the arms the measure
        --  filled.  Both the trims below and the box drawn later read
        --  it, so it is asked for once here and looked up after.
        local outline = arc.rules.outline
        for i = 0, w:junctions() - 1 do
            local o = w:junction(i)
            if o then
                outline(o)
                w:junction_ring()
            end
        end

        --  Every junction's control, settled before anything that turns
        --  on it: the reading is the family's own, and the rule that
        --  answers it is the one the family names.
        for i = 0, w:controls() - 1 do
            local rule, at = w:control(i)
            local fn = arc.rules[rule]
            w:control_is(i, fn and fn(at))
        end

        --  Stage three: the trim each junction hands its arms, off the
        --  rings just walked, and then how deep a band each mouth that
        --  asked for a crosswalk may take out of its road.
        local crossing = arc.rules.crossing
        for i = 0, w:trims() - 1 do
            w:xwalk_deep(i, crossing and crossing(w:xwalk(i)))
        end

        --  The level crossings.  Every measurement of one follows from
        --  the angle the road and the line cross at, so the ask hands
        --  that over, arc.rules.crossing_frame settles the sizes, and
        --  the draw lays the panel, the record and the approaches.
        local frame, panel = arc.rules.crossing_frame, arc.rules.panel
        local marks = arc.rules.crossing_marks
        for row = 0, n - 1 do
            for col = 0, n - 1 do
                if w:wanted(col, row) then
                    local x = w:crossing(col, row)
                    if x then
                        w:crossing_frame(frame(x))
                        --  The two paths settle the panel's corners; what
                        --  is laid over them is this side's.
                        local p = w:crossing_panel()
                        if p then panel(p) end
                        --  Each approach: what stands on it is this
                        --  side's, and the ask says what to go on.
                        for ap = 0, 1 do
                            local a = w:crossing_approach(ap)
                            if a then
                                for _, mk in ipairs(marks(a)) do
                                    w:crossing_mark(mk)
                                end
                            end
                        end
                        w:crossing_approaches()
                    end
                end
            end
        end

        --  The power lines: a pylon with a wire out to each joined edge,
        --  and over a road or a railway the span alone.
        local pylon, span = arc.rules.power_tile, arc.rules.power_crossing
        for row = 0, n - 1 do
            for col = 0, n - 1 do
                if w:wanted(col, row) then
                    --  The shape is opened by the ask, since only it
                    --  knows whether the tile is shared.
                    local at, crossing = w:power(col, row)
                    if at then (crossing and span or pylon)(at) end
                end
            end
        end
        w:emitted()
        w:shape()

        --  The drawing pass, family by family: every junction first, so
        --  a leg knows whether it is signalled before it draws its
        --  crosswalk, then the segments each cut back to the outline its
        --  junctions gave it, the loops with no node at all, and the
        --  pieces standing on the map's own edge.
        w:networks_draw()
        local junction = arc.rules.junction
        for fk = 0, w:net_families() - 1 do
            w:junction_boxes(fk)
            while true do
                local j = w:junction_box()
                if j == nil then break end
                if j and junction then junction(j) end
                --  A box that lofts strips of its own -- a rail
                --  junction's tracks -- draws none of them itself.
                for k = 0, w:box_lofts() - 1 do
                    w:box_loft(k)
                    lofted()
                end
                w:junction_box_done()
            end
            w:segments(fk)
            while w:segment() do
                lofted()
                w:segment_done()
            end
        end
        w:networks_drawn()

        --  The highways, and then their bands' open ends carried across
        --  the crossings into the lanes facing them before the bands are
        --  joined to the roads they become.
        w:highways()
        while w:hiway_band() do
            --  The points the fit is given, picked from the band the
            --  walk read: the straight cells', a lone block's corner,
            --  and nothing of a staircase.
            stage(w:hiway_chain())
            w:hiway_band_chained()

            --  A band the grading pass walked is fitted here, the same
            --  two ways a railway's segment is; one the building pass
            --  replayed was fitted already and asks for none.
            for k = 0, w:hw_fits() - 1 do
                local p = w:hw_fit(k)
                if p and path then path(p) end
                w:hw_fit_done(k)
            end
            local c = w:hw_fit_choice()
            if c then w:hw_fit_choice_is(fit_choice and fit_choice(c)) end
            w:hiway_band_fitted()
            lofted()
            w:hiway_band_done()
        end
        --  And where each ramp's descent runs along its deck, which
        --  follows from the station of the deck it stands nearest.
        local ramp_span = arc.rules.ramp_span
        for i = 0, w:ramp_spans() - 1 do
            local r = w:ramp_span(i)
            if r then w:ramp_span_is(i, ramp_span and ramp_span(r)) end
        end

        w:highway_ramps()

        --  Each ramp read up to the join the rule slides along the road's
        --  lane, and taken up again once it has answered.
        local slide = arc.rules.slide
        while w:ramp_next() do
            local sl = w:ramp_slide()
            if sl and slide then slide(sl) end
            w:ramp_done()
        end
        for i = 0, w:ramp_lofts() - 1 do
            w:ramp_loft(i)
            lofted()
        end
        local cross = arc.rules.cross
        local x = w:lane_cross()
        if x and cross then cross(x) end
        w:highway_links()

        --  The footways, over the network the segments and the junctions
        --  recorded.  In outline the bands stand aside with the rest of
        --  the road works and the network itself is drawn in their
        --  place, which is a different rule over the same paths.
        local paths, outline = w:footways()
        local band = outline and arc.rules.walk_curves or arc.rules.footway
        for i = 0, paths - 1 do
            local f = w:footway(i)
            if f then band(f) end
        end
        w:shape()

        --  The lane overlay's wires, laid last: every pass gathered what
        --  it wanted drawn, and each is entered again under the shape it
        --  belongs to, so the inspector still names the lane it runs
        --  over.  Nothing is gathered unless the curves are shown.
        local lane = arc.rules.lane
        for i = 0, w:wires() - 1 do
            local l = w:wire(i)
            if l and lane then lane(l) end
            w:wire_done(i)
        end
    end

    --  The grading pass leaves each corridor's shelf on its own tiles,
    --  and two corridors that share a corner leave two heights there.
    --  Reconciling them is the last thing the pass does: the world is
    --  built on the answer.
    if d.pass == 1 then
        local shelf, s = arc.rules.shelf, w:shelf()
        if shelf and s then shelf(s) end
    end

    return true
end
