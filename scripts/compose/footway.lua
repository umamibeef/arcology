--  footway.lua -- the pavement itself, and the crosswalk.
--
--  The network says where a band's stations are, what it joins and how
--  deep a crossing the arm could spare.  What is drawn over those
--  stations is here.
--
--  A cross-section runs to each pair of arguments, as a strip's own
--  quads are given: the outer and inner edge at one station, then at the
--  next.  The across then runs across the band -- 1 at the kerb line and
--  the footway's own share of it where the carriageway starts -- and the
--  height along it.  Handed the outer edge as one pair, the material
--  reads a kerb at every station and the band comes out barred.
--
--  A CROSSING is the carriageway marked, not a pavement: its
--  cross-sections run from one kerb to the other, so the across is the
--  road's own, and the along runs from nothing at the junction's mouth
--  to the band's full depth at its far end, where the material paints
--  the stop line the driver stops at before the bars.

arc.rules.footway = function (b)
    local d = b:info()
    local n = b:count()
    if n < 2 then return true end
    local fam = arc.rules.family({name = "road"})
    local fw = fam.footway
    if not fw then return true end
    local cross = d.band == "crossing"
    --  Written out rather than chained: a slot of zero is a real place
    --  in the stack, and `a and b or c` would read it as no answer.
    local share = fw.slot_junction
    if cross then share = fw.slot_cross
    elseif d.band == "side" then share = fw.slot_strip end
    local slot = d.order + share
    local ac0, ac1, al0, al1, mat = 1.0, fw.inner, -1.0, -1.0, arc.mat.walk
    if cross then
        ac0, al0, al1, mat = -fw.inner, 0.0, d.asked, arc.mat.zebra
    end
    for k = 1, n - 1 do
        local a0x, a0y, a1x, a1y, za = b:at(k)
        local b0x, b0y, b1x, b1y, zb = b:at(k + 1)
        --  A band that lies on the ground takes the drawn surface at
        --  each of its own corners, as the strip beside it does; a box's
        --  band is flat on its levelled pad.
        if d.drape then za, zb = -1.0, -1.0 end
        b:quad(a0x, a0y, a1x, a1y, b0x, b0y, b1x, b1y,
               za, zb, ac0, ac1, al0, al1, slot, mat)
    end
    return true
end
