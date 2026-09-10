--  line_class.lua -- what class a tile's line is.
--
--  A line is drawn as a line, an avenue or a boulevard, and which of
--  those it is follows from the TRAFFIC on it: the busier the tile, the
--  wider the line the city drew there.
--
--  Two steps, and where they fall is the whole of it.  A different pair
--  of numbers gives a city of boulevards or a city of lanes; a curve
--  with more steps in it would need the classes to go with it, which is
--  arc.rules.lanes's side of the same question.
--
--  Every reader comes through this one answer -- the junction's control,
--  the class pass, the gate's arm, and arc.city.line_class -- so none of
--  them can hold a different idea of what an avenue is.  The pipeline
--  asks for all two hundred and fifty-six before it builds anything.

arc.rules.line_class = function (tv)
    if tv >= arc.geo.class_boulevard then return 2 end
    if tv >= arc.geo.class_avenue then return 1 end
    return 0
end
