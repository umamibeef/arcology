--  band.lua -- where a band's edge runs, in one place.
--
--  A strip's way and the margin beside it share a line: the
--  way stops at the margin's inner edge and the margin starts
--  there.  Two expressions for one line agree only by luck, and where
--  they disagree by a hair the line is drawn a hair under its own
--  margin.  So there is one expression, and both use it.
--
--  It is worked out in the MESH'S own precision.  Vertices are floats
--  there, and a line computed to more places than the mesh can keep
--  lands a few millionths from where its neighbour thinks it is: the two
--  then overlap by that hair instead of meeting along it.
--
--  `sgn` is which hand of the centreline: 1 the right looking along it,
--  -1 the left.  `w` is that side's own share of the half width, which a
--  slab narrows where a spur has taken its outer lane.  `h` is the half
--  width already scaled by whatever share of the band this edge is.
local f32 = arc.put.f32

function arc.f32(x)
    return f32(x)
end

--  The half width a band reaches at one station: the strip's own, less
--  what the fit takes off a diagonal, less whatever share of the band
--  this edge is.
function arc.band_half(half, factor, share)
    return f32(f32(half * factor) * share)
end

function arc.band_edge(px, py, dx, dy, h, w, sgn)
    return f32(px + f32(f32(dy * h) * w) * sgn),
           f32(py - f32(f32(dx * h) * w) * sgn)
end

--  The angle a boundary turns through where two arms meet, from their
--  two headings: 0 where they leave together and pi where they leave
--  opposite ways.
function arc.corner_angle(ax, ay, bx, by)
    local cs = f32(f32(ax * bx) + f32(ay * by))
    return arc.put.acos(math.max(-1, math.min(1, cs)))
end

--  How far apart two points on the map are.
function arc.dist(ax, ay, bx, by)
    local dx, dy = f32(ax - bx), f32(ay - by)
    return arc.put.sqrt(f32(f32(dx * dx) + f32(dy * dy)))
end
