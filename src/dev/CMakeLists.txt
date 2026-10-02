#  Arcology -- the developer modes.
#
#  This is TOOLING, not reconstruction.  `arcology --verify`, `--check`,
#  `--clock`, `--advisor` and the rest drive the simulation and print
#  what it did; none of them is a routine the original has.  They are
#  kept apart from arc_core for that reason: a file here may print, may
#  hold state of its own and may invent a format, and none of those is
#  allowed in the reconstruction beside it.
#
#  It is a library rather than an executable because app/main.c
#  dispatches into it: one binary, `arcology`, answers to all of them.
add_library(arc_dev STATIC
    dev.c       # the modes themselves
    testcity.c  # a city built to order, for a test that needs one
    advisor.c   # the board of advisors: a joke layer over the ranking
)
target_include_directories(arc_dev PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(arc_dev PUBLIC arc_core)

#  The same standard the reconstruction is held to: these drive it, and a
#  silent conversion in a checker reports a difference that is not there.
target_compile_options(arc_dev PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall -Wextra -Wshadow -Wconversion -Wsign-compare -Wpointer-arith>
    $<$<C_COMPILER_ID:MSVC>:/W4>)
