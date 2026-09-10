--  invent.lua -- the world's own additions.
--
--  arc.rules.invent is the last thing the drive asks for, and it is the
--  door the pipeline's passes are not behind.  A script here holds the
--  world itself: it can invent a path from anything it can read of the
--  simulation, fit it with `arc.fit`, and sweep a cross-section of its
--  own writing along it with `w:loft` -- and nothing above knows or
--  needs to know what it drew.
--
--      local pieces = arc.fit({{x = 30, y = 40}, ...}, 3.0)
--      w:shape("viaduct", 30, 40)
--      w:loft(pieces, {{across = -1, up = 0, mat = arc.mat.line},
--                      {across =  1, up = 0, mat = arc.mat.line}},
--             {step = 0.25, step_arc = 0.1, lift = 1.0})
--
--  It draws NOTHING as it stands, and that is the right default: the
--  city this folder composes is SimCity 2000's, and every part of it
--  reaches the world through the passes above.  What goes here is
--  whatever a map wants that the game never had.

arc.rules.invent = function (w)
end
