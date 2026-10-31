#  The program: the dispatcher, the modes of their own, and the record
#  under a picked shape, which the headless --pick reads as the game does.
#  Where SDL3 was found, the game itself: options, the loop, the clock, the
#  world and the glue to the interface.  Where it was not, stub.c stands in
#  for the game and says why.  The developer modes are still reachable,
#  because that is what the checkers in tools/ drive and what ctest runs.
target_sources(arcology PRIVATE
    main.c      # the dispatcher
    modes.cpp
    net_pick.c)
if(SDL3_FOUND)
    target_sources(arcology PRIVATE
        app.c frame.c headless.c input.c options.cpp paths.c uiglue.c inspect.c
        world.c land_settle.c clock.c area.c
        prefs.c camera.c)
else()
    target_sources(arcology PRIVATE stub.c)
    message(STATUS "SDL3 not found: arcology is built without the game "
                   "(install SDL3, or configure with -DARC_FETCH_SDL3=ON)")
endif()
