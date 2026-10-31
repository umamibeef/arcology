#  The road network, derived from the cells (plans/network.md).
target_sources(arcology PRIVATE
    topology.c  # layer 1: which cells form a road and how they meet
    network.c   # layer 2: the roads, lane sections, lanes and junctions, and the lane check
    shape.c     # layer 3: each road's line and height
    bank.c      # the ground drawn beside each street
    store.c     # the network kept by place, chunk by chunk
    solved.c    # each interchange's search, kept by its place once solved
    street.c)   # the street layer
