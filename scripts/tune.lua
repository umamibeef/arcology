--  tune.lua -- THE KNOBS: the numbers the tuning window moves live.
--
--  A knob is a number with a range, a value it starts at, and a name the
--  window puts on its slider.  That is the whole difference between a
--  knob and one of geo.lua's numbers, which a script sets and nothing
--  moves afterwards.
--
--  This file is the only list of them.  The window builds its sliders
--  from it, the renderer binds the ones it reads by name, and a family
--  names its width and its radii here (scripts/families).  So a knob is
--  added, ranged and labelled in one place.
--
--  `group` draws a rule across the window where it changes.  A knob with
--  no label gets no slider: some other control owns it.  `show_curves`
--  is the outline view and sits on the View menu.

local KNOBS = {
    {name = "line_w",       label = "road width",      group = "line", default = 0.50, lo = 0.15, hi = 1.00},
    {name = "thread_w",     label = "rail width",      group = "line", default = 0.62, lo = 0.15, hi = 1.00},
    {name = "line_rmin",    label = "road min radius", group = "line", default = 0.90, lo = 0.05, hi = 4.00},
    {name = "thread_rmin",  label = "rail min radius", group = "line", default = 3.00, lo = 0.05, hi = 8.00},
    {name = "line_rmax",    label = "road max sweep",  group = "line", default = 6.00, lo = 0.50, hi = 12.00},
    {name = "thread_rmax",  label = "rail max sweep",  group = "line", default = 8.00, lo = 0.50, hi = 16.00},
    {name = "approach",     label = "node approach",   group = "line", default = 0.80, lo = 0.00, hi = 2.00},
    {name = "margin",       label = "corridor margin", group = "line", default = 0.04, lo = 0.00, hi = 0.30},
    {name = "trim_cap",     label = "junction trim",   group = "line", default = 0.32, lo = 0.10, hi = 0.60},
    {name = "show_curves",                             group = "line", default = 0.00, lo = 0.00, hi = 1.00},

    {name = "band_w",       label = "band width",      group = "band", default = 0.60, lo = 0.30, hi = 1.00},
    {name = "band_rmin",    label = "band min radius", group = "band", default = 2.50, lo = 0.50, hi = 6.00},
    {name = "band_rmax",    label = "band max sweep",  group = "band", default = 8.00, lo = 1.00, hi = 16.00},
    {name = "band_reach",   label = "spur reach",      group = "band", default = 1.60, lo = 0.50, hi = 3.00},
    {name = "band_stair",   label = "staircase step",  group = "band", default = 2.00, lo = 0.00, hi = 6.00},
    {name = "corner_share", label = "corner share",    group = "band", default = 0.50, lo = 0.25, hi = 1.00},
    {name = "spur_merge",   label = "spur merge",      group = "band", default = 2.50, lo = 0.00, hi = 4.00},
    {name = "band_grade",   label = "deck grade",      group = "band", default = 0.10, lo = 0.02, hi = 0.50},
    {name = "band_stiff",   label = "deck stiffness",  group = "band", default = 2.00, lo = 0.00, hi = 8.00},
}

for _, k in ipairs(KNOBS) do
    arc.tune.declare(k)
end
