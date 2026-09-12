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

--  WHAT EACH PASS DID, said once as the pass ends.
--
--  A build is not a frame: this is two lines a build and silent while
--  the world merely draws, so it is on always -- a load that takes
--  longer than it should says where it went without a switch having to
--  be found first.  Every entry is the wall time between one `step` and
--  the next, in milliseconds, in the order the drive ran them.
--  HOW MANY STEPS THIS BUILD HAS, so the bar knows what a step is worth.
--  A step announces itself only when it RUNS, so the count has to be
--  what this build will actually do.  Four steps always run.  The lines
--  add fourteen more.  The grading pass adds one to reconcile the
--  shelves, and the building pass does not.
--
--  A count that is too high leaves the bar short of the end for ever.
--  A bar that stops one step short reads as a build that stopped there,
--  and the last step is the one a person is already waiting on.
--
--  Beyond that it only has to be near enough: a step that takes longer
--  than its share moves the bar slower, which is what a bar is for.
local function steps_of(d, pass)
    local n = 4
    if d.lines and not d.underground then n = n + 14 end
    if pass == 1 then n = n + 1 end
    return n
end

local clock = os.clock
local step_name, step_at, said, n_said, step_w, n_step, n_steps

local function step(name)
    local now = clock()
    if step_name then
        n_said = n_said + 1
        said[n_said] = ("%s %.0f"):format(step_name, (now - step_at) * 1000.0)
    end
    step_name, step_at = name, now
    --  and told to whatever is showing a bar
    if name and step_w then
        n_step = n_step + 1
        step_w:progress(name, n_step, n_steps)
    end
end

--  THE BAR SPANS THE WHOLE BUILD, not one pass.  A pass numbered 1 is
--  the grading half of a two pass build, so its steps and the building
--  pass's are one total, and pass 2 goes on from where pass 1 stopped.
--  A bar that fills, empties and fills again shows two jobs where a
--  person is waiting for one.
local function step_first(w, d)
    said, n_said, step_name, step_at = {}, 0, nil, nil
    step_w = w
    --  Pass 2 goes on from pass 1 only where pass 1 ran.  Entered on its
    --  own, it counts from nought like any first pass.
    if d.pass ~= 2 or not n_steps then
        n_step  = 0
        n_steps = steps_of(d, d.pass)
        if d.pass == 1 then n_steps = n_steps + steps_of(d, 2) end
    end
end

local function step_said(pass, tally)
    step(nil)
    --  The last pass ends full.  What the pipeline does after the script
    --  returns, and the upload of the mesh it built, are outside every
    --  step here, and the bar holds its last reading through them.
    if pass ~= 1 and step_w and n_steps then
        step_w:progress("done", n_steps, n_steps)
    end
    if n_said > 0 then
        arc.log(("pass %d: %s ms%s"):format(pass, table.concat(said, ", ", 1, n_said),
                                            tally and (" | " .. tally) or ""))
    end
end

arc.rules.world = function (w)
    local d = w:info()
    local n = d.size
    local n_tile, n_junc, n_seg, n_band = 0, 0, 0, 0
    step_first(w, d)
    --  WHAT THE GROUND IS, before anything reads a tile: where every
    --  tile's top comes from.  Every later pass -- the ground faces, the
    --  walls, a strip's own samples of the surface under it -- reads the
    --  answer, so it is settled first and once.
    --
    --  Then the field, which cannot be built any sooner: a water tile's
    --  bed is clamped under the surface drawn over it, and that surface
    --  is the answer above.
    step("tops")
    local terrain, tf = arc.rules.terrain, w:terrain()
    if terrain and tf then terrain(tf) end
    w:field()

    step("ground")

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
                n_tile = n_tile + 1
            end
        end
    end
    w:shape() -- the last one: nothing after it nests inside the ground

    step("tints")

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

    step("network")

    --  The networks.  Underground there are none, and a build that draws
    --  no lines asks for none.  The lane model goes first: a junction
    --  beside a spur needs to know which slab lane that spur takes.
    --
    --  The measure fits every segment and settles where each meet's
    --  two paths actually run.  Only then can the meets and the
    --  lines be drawn -- asked for any sooner, a meet would have
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
        --  spur narrows, where the stations sit, what a slab stands on,
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

    if d.lines and not d.underground then
        --  Where a gate settles with no train anywhere near: one angle,
        --  asked for once here, and every gate the moving world starts
        --  begins there.
        local gate = arc.rules.gate
        w:gate_rest(gate and gate{angle = 0.0, near = 1e9, dt = 1000.0})

        --  THE NETWORK ITSELF, before anything reads it: which cells
        --  form a segment and where it runs.  Nothing in the pipeline
        --  walks the map for this -- arc.rules.network is handed the
        --  links, the art and the node kinds and hands back every run --
        --  and the class pass, the measure and the drawing all read the
        --  list it produced, in the order it produced it.
        local network = arc.rules.network
        for fk = 0, w:net_discover() - 1 do
            network(w:net_cells(fk))
            w:net_found()
        end

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

        --  And how high a strip has to stand to notch nothing, asked
        --  once for a plain strip and once for a structure, before any
        --  of the grading reads it.
        for i = 0, w:flies_runs() - 1 do
            local rule, structure = w:flies_run(i)
            if rule then
                local fn = arc.rules[rule]
                if fn then w:flies_run_is(i, fn(structure)) end
            end
        end



        --  And the band's own network: which cells form a band and
        --  where it runs.  Nothing in the pipeline walks the slab for
        --  this either -- arc.rules.bands is handed the pairs, the curve
        --  blocks and the spurs and hands back every band, asking
        --  arc.rules.band_start which way to leave each end it finds.
        --  A build that is replaying the bands a previous one fitted has
        --  nothing to discover and is handed no map.
        local bands = arc.rules.bands
        local hw = w:band_cells()
        if hw then bands(hw) end

        --  And how a spur's foot meets the line it lands on: the eight
        --  readings there are, settled before any spur is read.
        --  How a junction's lights are staggered and grouped: the eight
        --  staggers and the four edges, settled before a signal is drawn
        --  or a car held at one.
        local sig_phase, sig_group = arc.rules.signal_phase, arc.rules.signal_group
        for k = 0, 7 do w:signal_phase_is(k, sig_phase and sig_phase(k)) end
        for e = 0, 3 do w:signal_group_is(e, sig_group and sig_group(e)) end

        --  What class a tile's line is, for every traffic byte there
        --  is, settled before anything reads a class.
        local line_class = arc.rules.line_class
        for tv = 0, 255 do
            w:road_class_is(tv, line_class and line_class(tv))
        end

        --  How many cars a tile's traffic is worth, for every byte
        --  there is, settled before the traffic is built.
        local car_density = arc.rules.car_density
        for tv = 0, 255 do
            w:car_density_is(tv, car_density and car_density(tv))
        end

        --  Every on-spur tile on the map, found by the rule that walks
        --  it: which side of each the slab lies, which the line, and
        --  which way round it runs.
        local spurs, ro = arc.rules.spurs, w:spurs()
        if spurs and ro then spurs(ro) end

        --  One class for each whole segment, settled before any of them
        --  is fitted: how wide a line runs, where its lanes go and what
        --  is painted on it all follow from it.
        local seg_class = arc.rules.seg_class
        for i = 0, w:seg_classes() - 1 do
            local cnt = w:seg_class(i)
            if cnt and seg_class then w:seg_class_is(i, seg_class(cnt)) end
        end

        step("fit")

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

        step("cut")

        --  And every fitted path cut into the pieces a strip is lofted
        --  from.  The cut is arc.rules.pieces's, and the passes that
        --  need one queue the path rather than asking for themselves.
        --  A path queued as two POSES has its points built first: that
        --  is arc.rules.posed's (scripts/compose/chain.lua), and a chain
        --  it builds none for is cut into nothing.
        local pieces = arc.rules.pieces
        local posed  = arc.rules.posed
        local function cut()
            for i = 0, w:cuts() - 1 do
                local o = w:cut(i)
                if o then
                    if posed and o:info().posed then posed(o) end
                    if pieces then pieces(o) end
                end
                w:cut_done(i)
            end
        end
        cut()

        w:lanes()

        --  Two spurs whose tapers face each other share the tiles
        --  between them: the rule says how many each may take.
        local spur_share = arc.rules.spur_share
        for i = 0, w:spur_shares() - 1 do
            local r = w:spur_share(i)
            if r then w:spur_share_is(i, spur_share and spur_share(r)) end
        end

        w:networks()

        step("outlines")

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

        --  Every junction's MARGIN, asked for before the pass that
        --  reads it: how the margin sits on the ring, and off that
        --  which of the junction's mouths may carry a meet at all.
        --  Nothing in the margin asks either question for itself, so a
        --  build whose script answers neither has no margin and no
        --  meets anywhere.
        local band, lap_at = arc.rules.band, arc.rules.lap_at
        local function mouths(n)
            for k = 0, n - 1 do
                w:mouth_is(k, lap_at and lap_at(w:mouth(k)))
            end
        end
        for i = 0, w:junction_bands() - 1 do
            local b = w:junction_band(i)
            if b then
                if band then band(b) end
                mouths(w:junction_band_done())
            end
            w:junction_trim(i)
        end

        --  Stage three: the trim each junction hands its arms, off the
        --  rings just walked, and then how deep a band each mouth that
        --  asked for a crosswalk may take out of its line.
        local stripe = arc.rules.stripe
        for i = 0, w:trims() - 1 do
            w:xwalk_deep(i, stripe and stripe(w:xwalk(i)))
        end

        step("meets")

        --  The level meets.  Every measurement of one follows from
        --  the angle the line and the line cross at, so the ask hands
        --  that over, arc.rules.lap_frame settles the sizes, and
        --  the draw lays the panel, the record and the approaches.
        local frame, panel = arc.rules.lap_frame, arc.rules.panel
        local marks = arc.rules.lap_marks
        for row = 0, n - 1 do
            for col = 0, n - 1 do
                if w:wanted(col, row) then
                    local x = w:lap(col, row)
                    if x then
                        w:lap_frame(frame(x))
                        --  The two paths settle the panel's corners; what
                        --  is laid over them is this side's.
                        local p = w:lap_panel()
                        if p then panel(p) end
                        --  Each approach: what stands on it is this
                        --  side's, and the ask says what to go on.
                        for ap = 0, 1 do
                            local a = w:lap_approach(ap)
                            if a then
                                for _, mk in ipairs(marks(a)) do
                                    w:lap_mark(mk)
                                end
                            end
                        end
                        w:lap_approaches()
                    end
                end
            end
        end

        step("power")

        --  The power lines: a pylon with a wire out to each joined edge,
        --  and over a line or a railway the span alone.
        local pylon, span = arc.rules.power_tile, arc.rules.power_meet
        for row = 0, n - 1 do
            for col = 0, n - 1 do
                if w:wanted(col, row) then
                    --  The shape is opened by the ask, since only it
                    --  knows whether the tile is shared.
                    local at, meet = w:power(col, row)
                    if at then (meet and span or pylon)(at) end
                end
            end
        end
        w:emitted()
        w:shape()

        step("draw")

        --  The drawing pass, family by family: every junction first, so
        --  a leg knows whether it is signalled before it draws its
        --  crosswalk, then the segments each cut back to the outline its
        --  junctions gave it, the loops with no node at all, and the
        --  pieces standing on the map's own edge.
        w:networks_draw()
        local junction = arc.rules.junction
        local turns    = arc.rules.turns
        local node_threads = arc.rules.node_threads
        local cap      = arc.rules.cap
        local signs    = arc.rules.junction_signs
        for fk = 0, w:net_families() - 1 do
            w:junction_boxes(fk)
            while true do
                local j = w:junction_box()
                if j == nil then break end
                n_junc = n_junc + 1
                --  The pattern its lanes make, and then the
                --  connectors that pattern asked for, cut like every
                --  other path before the box is drawn on them.
                local t = w:junction_turns()
                if t and turns then turns(t) end
                --  And a thread junction's own threads, which are the whole
                --  of what it is.
                local tk = w:node_threads()
                if tk and node_threads then node_threads(tk) end
                cut()
                w:junction_lanes()
                --  And what the box itself is made of, once its lanes'
                --  connectors are taken: a thread junction's threads lofted
                --  from the pieces the cut answered, or a line's outline
                --  gathered for the fill to be laid on.
                w:rail_threads_done()
                w:box_paving()
                --  The ring read as a margin, before the fill is
                --  laid inside it: the box cannot know where its own
                --  edge is until the margin is answered.
                local b = w:box_band()
                if b then
                    if band then band(b) end
                    mouths(w:box_band_done())
                end
                j = w:box_fill()
                if j and junction then junction(j) end
                --  A box that lofts strips of its own -- a thread
                --  junction's threads -- draws none of them itself.
                for k = 0, w:box_lofts() - 1 do
                    w:box_loft(k)
                    lofted()
                end
                --  The fill taken back and the box handed to the
                --  signs, and then the box closed.
                w:box_paving_done()
                w:junction_box_done()
                --  And what stands at its arms, once the box has asked
                --  for them: the signals and stop signs, placed by the
                --  rule that decides what each arm carries.
                local sg = w:junction_signs()
                if sg then
                    if signs then signs(sg) end
                    w:junction_signs_done()
                end
            end
            w:segments(fk)
            while w:segment() do
                n_seg = n_seg + 1
                lofted()
                --  What its lanes do where it simply stops, and then the
                --  caps that answer asked for, cut like every other path.
                local cp = w:segment_caps()
                if cp and cap then cap(cp) end
                cut()
                w:segment_done()
            end
        end
        w:networks_drawn()

        step("bands")

        --  The bands, and then their bands' open ends carried across
        --  the meets into the lanes facing them before the bands are
        --  joined to the lines they become.
        w:bands()
        while w:band_next() do
            n_band = n_band + 1
            --  Which node each end of this band meets, for the fit's
            --  chain stage: it aims that end at the node.
            arc.band_fitting = arc.band_ends and arc.band_ends[n_band] or nil
            --  The points the fit is given, picked from the band the
            --  walk read: the straight cells', a lone block's corner,
            --  and nothing of a staircase.
            stage(w:band_chain())
            w:band_chained()

            --  A band the grading pass walked is fitted here, the same
            --  two ways a railway's segment is; one the building pass
            --  replayed was fitted already and asks for none.
            for k = 0, w:band_fits() - 1 do
                local p = w:band_fit(k)
                if p and path then path(p) end
                w:band_fit_done(k)
            end
            local c = w:band_fit_choice()
            if c then w:band_fit_choice_is(fit_choice and fit_choice(c)) end
            w:band_fitted()
            cut()
            w:band_cut()
            lofted()
            w:band_done()
        end
        arc.band_fitting = nil
        step("spur spans")

        --  And where each spur's descent runs along its slab, which
        --  follows from the station of the slab it stands nearest.
        local spur_span = arc.rules.spur_span
        for i = 0, w:spur_spans() - 1 do
            local r = w:spur_span(i)
            if r then w:spur_span_is(i, spur_span and spur_span(r)) end
        end

        step("spur joins")

        w:band_spurs()

        --  Each spur read up to the join the rule slides along the line's
        --  lane, and taken up again once it has answered.
        --  Each spur read up to the lanes its two ends may fasten to --
        --  the slab's at the top, the line's at the foot -- with the
        --  pick between them arc.rules.spur_lane's, and then the join
        --  slid along the line's lane by arc.rules.slide.
        local slide, spur_lane = arc.rules.slide, arc.rules.spur_lane
        local spur_target = arc.rules.spur_target
        local function pick()
            local p = w:spur_pick()
            if p and spur_lane then spur_lane(p) end
        end
        while w:spur_next() do
            pick()                   -- the slab lane the top meets
            w:spur_routed()
            cut()                    -- the descent from it
            --  Where its foot aims on the line, before the join is drawn.
            local tg = w:spur_target()
            if tg and spur_target then spur_target(tg) end
            w:spur_joined()
            pick()                   -- the line lane the foot meets
            local sl = w:spur_slide()
            if sl and slide then slide(sl) end
            w:spur_slid()
            cut()                    -- the legs the join falls back on
            w:spur_done()
        end
        step("spur lofts")

        for i = 0, w:spur_lofts() - 1 do
            w:spur_loft(i)
            lofted()
        end
        step("lane cross")

        local cross = arc.rules.cross
        local x = w:lane_cross()
        if x and cross then cross(x) end
        cut()                        -- the links it asked for
        w:lane_cross_done()

        step("band links")

        --  And the bands' own ends joined: which slab lane goes on to
        --  which -- the lane of another band round an interchange, the
        --  line's lane where the slab comes down, or the inner lane of
        --  its own band where it tapers out.  All of it is the script's;
        --  take arc.rules.links away and a slab's lanes end in mid-air.
        local links = arc.rules.links
        local lk = w:band_links()
        if lk and links then links(lk) end
        w:band_links_done()

        --  And the slab each 2x2 interchange is crossed on, swept from
        --  the map (scripts/compose/interchange.lua).
        arc.interchange_slabs(w)

        step("margins")

        --  The margins, over the network the segments and the junctions
        --  recorded.  In outline the bands stand aside with the rest of
        --  the line works and the network itself is drawn in their
        --  place, which is a different rule over the same paths.
        local paths, outline = w:margins()
        local band = outline and arc.rules.walk_curves or arc.rules.margin
        for i = 0, paths - 1 do
            local f = w:margin(i)
            if f then band(f) end
        end
        w:shape()

        step("wires")

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

    --  And whatever the script itself wants standing in the world.
    --
    --  This is the door the pipeline's own passes are not behind: a
    --  script invents a path, fits it with arc.fit and lofts it along a
    --  cross-section of its own through w:loft, and none of the passes
    --  above knows anything about it.  Nothing is here by default --
    --  arc.rules.invent is nil in the shipped scripts, and a build with
    --  no such rule draws exactly what it drew before.
    --
    --  The grading pass's geometry is thrown away, so it is asked for in
    --  the pass that survives and not in both.
    local invent = arc.rules.invent
    if invent and d.pass ~= 1 then
        invent(w)
        w:shape() -- whatever it left open
    end

    --  The grading pass leaves each corridor's shelf on its own tiles,
    --  and two corridors that share a corner leave two heights there.
    --  Reconciling them is the last thing the pass does: the world is
    --  built on the answer.
    if d.pass == 1 then
        step("shelf")
        local shelf, s = arc.rules.shelf, w:shelf()
        if shelf and s then shelf(s) end
    end

    step_said(d.pass, ("%d tiles, %d junctions, %d segments, %d bands"):format(
                          n_tile, n_junc, n_seg, n_band))
    return true
end
