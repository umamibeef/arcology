--  meet.lua -- a level meet's panel.
--
--  The panel covers the whole of the line it interrupts: the line's full
--  width across, and along the line as far as the thread bed reaches,
--  which where the two cross at an angle is further than the thread is
--  wide.  Where its four corners fall is settled by the two paths' own
--  lines; what is laid over them is here.
--
--  A hair over the fill it replaces, and over the ballast it
--  interrupts: on the ground itself the panel is the one surface here
--  that the clip check can catch dipping under it.  Its height comes
--  from the HIGHER of each end's two corners, so on a tile that tilts
--  across the line it stands on the ground and not under it.

arc.rules.panel = function (x)
    local d = x:info()
    local _, _, g0 = x:at(0)
    local _, _, g1 = x:at(1)
    local _, _, g2 = x:at(2)
    local _, _, g3 = x:at(3)
    x:quad(math.max(g0, g1) + d.lift, math.max(g2, g3) + d.lift, d.order + d.slot)
    return true
end
