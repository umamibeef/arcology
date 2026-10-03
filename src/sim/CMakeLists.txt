# Arcology -- the simulation, reconstructed from the 68k Macintosh binary.
#
# This builds LIBRARIES only.  The executable is `arcology`, defined in
# render/CMakeLists.txt, and the whole tree builds from the repository
# root:
#
#   cmake -B build && cmake --build build
#   ./build/arcology --verify "<game folder>" "<game folder>/Cities"
#
# This is a DIRECTORY of the one project the root declares, not a project
# of its own: the version, the language standard, the build type,
# ARC_CITIES, Python3 and enable_testing() are all settled there.

# tables.c is generated from the binary's own global image.  It is checked
# in so a plain build needs no Python, but regenerate it with
#   cmake --build build --target tables
if(Python3_FOUND)
    add_custom_target(tables
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/gen_tables.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
        COMMENT "Regenerating tables.c from the DATA 0 global image"
        VERBATIM)
endif()

# The simulation is a library so the game and the verification driver run
# exactly the same code.
add_library(arc_core STATIC
    city.c          # .SC2 reader/writer, RLE codec from $293EC

    #  sim.c was 8,438 lines and every phase of the game.  The phases now
    #  sit beside the ones they belong with; sim_int.h names the handful
    #  of routines that still cross between them.
    sim.c           # the clock and the money: sim_tick, the budget, the poll
    sim_scan.c      # the passes that read the map and write a plane
    sim_growth.c    # whether a zone grows, and into what
    sim_map.c       # changing what is on a tile: the bulldozer and the stamp
    sim_place.c     # putting something down: zones, specials, auto stations
    sim_thing.c     # the moving things: trains, boats, ships, aircraft
    sim_disaster.c  # the nineteen disasters, and the roll that picks one
    sim_tool.c      # the player's tools: the work each one does at a tile
    sim_road.c      # each network fits its piece to its neighbors
    sim_rail.c
    sim_power.c
    sim_pipe.c
    sim_subway.c
    sim_micro.c     # the XMIC records, aged a year at a time
    rotate.c        # the map turned a quarter, as the original rewrites it
    sim_queue.c     # the 512-entry ring those passes walk the map with

    rng.c           # the Toolbox LCG and the $20F30 LFSR
    ext80.c         # soft 80-bit extended, the SANE subset the game uses
    sane.c          # the _FP68K trap, reduced to the 21 opwords that occur
    economy.c       # economyPass $34D04, the national economy
    generated/tables.c  # GENERATED - do not edit
    arco.c          # .arco, the native world format
)
target_include_directories(arc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# arco.c needs deflate and a CRC.  lodepng already carries both for the
# renderer's PNGs, so the save format borrows them rather than vendoring
# a second compressor.  The target is defined here, and render/ uses this
# one, so there is exactly one copy in any link.
if(NOT TARGET arc_vendor)
    add_library(arc_vendor STATIC ../vendor/lodepng.c)
    target_include_directories(arc_vendor SYSTEM PUBLIC
                               "${CMAKE_CURRENT_SOURCE_DIR}/../vendor")
    if(MSVC)
        target_compile_options(arc_vendor PRIVATE /w)
        target_compile_definitions(arc_vendor PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(arc_vendor PRIVATE -w)
    endif()
endif()
target_link_libraries(arc_core PUBLIC arc_vendor)

# dev.c holds the developer modes -- --verify, --clock, --growth and the
# The developer modes that drive it are TOOLING, not reconstruction, and
# are built in src/dev.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../dev" "${CMAKE_CURRENT_BINARY_DIR}/../dev")

# The reconstruction is held to a strict standard on purpose: a silent
# integer conversion here is a behavioural difference from the original.
target_compile_options(arc_core PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall -Wextra -Wshadow -Wconversion -Wsign-compare -Wpointer-arith>
    $<$<C_COMPILER_ID:MSVC>:/W4>)

find_library(M_LIB m)
if(M_LIB)
    target_link_libraries(arc_core PUBLIC ${M_LIB})
endif()

# ---- house style ----------------------------------------------------
# .clang-format at the repository root is the style.  `format` rewrites the
# sources in place; `format-check` fails if any of them is not already
# formatted, and runs as part of ctest so the style cannot drift.
find_program(CLANG_FORMAT NAMES clang-format
             HINTS /opt/homebrew/bin /usr/local/bin)
#  The hand-written sources only.  generated/tables.c is machine output --
#  its layout is gen_tables.py's business, and holding it to a style meant
#  for people just made the generator and the checker argue.
set(ARC_SOURCES city.c sim.c sim_thing.c sim_disaster.c sim_queue.c sim_scan.c sim_map.c sim_place.c
                sim_growth.c sim_micro.c rng.c ext80.c sane.c arco.c economy.c rotate.c
                sim.h sim_int.h ext80.h arco.h
                ../dev/dev.c ../dev/testcity.c ../dev/advisor.c ../dev/advisor.h)
if(CLANG_FORMAT)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${ARC_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting the C sources"
        VERBATIM)
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --dry-run --Werror ${ARC_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking the C sources against .clang-format"
        VERBATIM)
endif()

# ---- verification ---------------------------------------------------
# Testing is enabled once, at the root, and ARC_CITIES is declared there.
if(CLANG_FORMAT)
    add_test(NAME format
             COMMAND ${CLANG_FORMAT} --dry-run --Werror ${ARC_SOURCES}
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(format PROPERTIES LABELS "lint;gate;full")
endif()
#  The `verify` test needs the executable, which lives one level up, so
#  it is registered in the root CMakeLists.txt instead of here.
