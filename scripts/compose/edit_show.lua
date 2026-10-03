--  edit_show.lua -- what an edit shows going and coming.
--
--  An edit rebuilds the pieces it reaches.  What was drawn on the edited
--  tiles GOES at once: it breaks into fragments that are thrown up and
--  fall away while the world is built again, as a wave out from those
--  tiles.  Once the world is built, the other pieces it changed LEAVE the
--  same way.  Nothing it left as it was moves.  The new world waits until
--  the last of them has gone, and then the pieces it made COME, their
--  fragments dropping into place from the far edge in, so the edit itself
--  is the last thing built.  The world on the screen until then is the
--  last build's, so what goes is what was there.
--
--  The pipeline hands this rule the candidates of each phase and moves
--  the ones it marks (src/render/show.h).  How they move is the numbers
--  below, from arc.geo.

arc.numbers("edit_show", {
    reach       = arc.geo.edit_show_reach,
    go_speed    = arc.geo.edit_show_go_speed,
    go_time     = arc.geo.edit_show_go_time,
    come_speed  = arc.geo.edit_show_come_speed,
    come_time   = arc.geo.edit_show_come_time,
    come_inward = arc.geo.edit_show_come_inward,
    piece       = arc.geo.edit_show_piece,
    jitter      = arc.geo.edit_show_jitter,
    scatter     = arc.geo.edit_show_scatter,
    dissolve    = arc.geo.edit_show_dissolve,
    hop         = arc.geo.edit_show_hop,
    depth       = arc.geo.edit_show_depth,
    drop        = arc.geo.edit_show_drop,
})

arc.rules.edit_show = function (e)
    local d = e:info()
    for i = 0, d.n - 1 do
        local dist, same, back, kept = e:at(i)
        --  What the edit was drawn on goes, and what the build changed
        --  near it leaves.  What comes is what went and is the same as it
        --  was, and what changed near the edit.  A change further out
        --  takes its place at once.
        if d.phase == "going"
           or (d.phase == "leaving" and dist <= arc.geo.edit_show_reach)
           or (d.phase == "coming" and (back or kept or (not same and dist <= arc.geo.edit_show_come))) then
            e:mark(i)
        end
    end
    return true
end
