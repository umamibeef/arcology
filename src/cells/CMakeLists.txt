#  The city's cells, as the road network and the renderer read them: the
#  view type and the map's buffers, the adapter that fills the view from
#  the simulation, and what a terrain byte means.
target_sources(arcology PRIVATE
    city.c      # the view of the cells, and the map's buffers
    adapt.c     # the view, filled from the simulation's City
    terrain.c)  # what a tile's terrain byte means to the land
