--  chain.lua -- THE CHAIN BETWEEN TWO POSES, and the arithmetic of it.
--
--  Every link this layer lays is a chain of points the script builds
--  itself and hands to arc.fit.  No router is asked where a way runs.
--
--  The arithmetic is in the world's own precision.  The lane model holds
--  its poses as floats, and a comparison made to more places than it
--  keeps answers differently at a boundary: a link that should be laid
--  would not be.

local M = {}

local f32 = arc.put.f32
local sqrt = arc.put.sqrt

local function sub(a, b) return f32(a - b) end
local function dot(ax, ay, bx, by) return f32(f32(ax * bx) + f32(ay * by)) end
local function dist(ax, ay, bx, by)
    local dx, dy = sub(bx, ax), sub(by, ay)
    return sqrt(f32(f32(dx * dx) + f32(dy * dy)))
end

--  THE CHAIN BETWEEN TWO POSES is the script's, never a router's: the
--  first pose, a straight lead along it, a straight lead back along the
--  second pose, the second pose.  The lead is the node's, cut down for
--  a short link so the two leads never meet.  Every corner may sweep
--  the node's turn radius and spend half the shorter leg beside it.
--  arc.fit cuts it into pieces, and a chain it can cut into none is a
--  link that cannot be laid, so nothing is answered for one.  The spur's
--  slide builds its placings with this too (scripts/compose/slide.lua).
--  A chain that FOLDS is no chain.  Where the far pose lies behind the
--  near one along its own line, the chord between the two leads runs
--  back against a lead, the corner there is a turn of nearly half a
--  circle, and the fit answers it with a fillet of no radius and a
--  piece of enormous length that nothing downstream can walk to the
--  end of.  So a corner at either lead sharper than FOLD is refused.
local FOLD = -0.94
function arc.chain_between(ax, ay, adx, ady, bx, by, bdx, bdy)
    local far  = dist(ax, ay, bx, by)
    local lead = math.min(arc.geo.interchange_lead, 0.3 * far)
    local q = {{x = ax, y = ay}}
    if lead > 1e-3 then
        local ax2, ay2 = ax + adx * lead, ay + ady * lead
        local bx2, by2 = bx - bdx * lead, by - bdy * lead
        local cx, cy = bx2 - ax2, by2 - ay2
        local cl = math.sqrt(cx * cx + cy * cy)
        if cl < 1e-6 then return nil end
        cx, cy = cx / cl, cy / cl
        if cx * adx + cy * ady < FOLD or cx * bdx + cy * bdy < FOLD then return nil end
        q[#q + 1] = {x = ax2, y = ay2}
        q[#q + 1] = {x = bx2, y = by2}
    elseif far > 1e-6 and ((bx - ax) * adx + (by - ay) * ady) / far < FOLD then
        return nil
    end
    q[#q + 1] = {x = bx, y = by}
    local rad, tlim = {}, {}
    for i = 1, #q do
        rad[i] = arc.geo.interchange_radius
        local d1 = i > 1 and dist(q[i - 1].x, q[i - 1].y, q[i].x, q[i].y) or 1e9
        local d2 = i < #q and dist(q[i].x, q[i].y, q[i + 1].x, q[i + 1].y) or 1e9
        tlim[i] = 0.5 * math.min(d1, d2)
    end
    if #arc.fit(q, rad, tlim) < 1 then return nil end
    return q, rad, tlim
end

M.f32  = f32
M.sqrt = sqrt
M.sub  = sub
M.dot  = dot
M.dist = dist
return M
