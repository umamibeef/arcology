# Arcology -- the simulation, reconstructed from the 68k Macintosh binary.
#
# This builds LIBRARIES only.  The executable is `arcology`, defined in
# render/CMakeLists.txt, and the whole tree builds from the repository
# root:
#
#   cmake -B build && cmake --build build
#   ./build/arcology --verify "<game folder>" "<game folder>/Cities"
#
cmake_minimum_required(VERSION 3.16)
project(arcology_sim
        VERSION 0.1
        DESCRIPTION "The Arcology simulation, reconstructed from CODE 2"
        LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# tables.c is generated from the binary's own global image.  It is checked
# in so a plain build needs no Python, but regenerate it with
#   cmake --build build --target tables
find_package(Python3 COMPONENTS Interpreter)
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
add_library(sc2kcore STATIC
    city.c        # .SC2 reader/writer, RLE codec from $293EC
    sim.c         # the simulation passes
    rng.c         # the Toolbox LCG and the $20F30 LFSR
    ext80.c       # soft 80-bit extended, the SANE subset the game uses
    sane.c        # the _FP68K trap, reduced to the 21 opwords that occur
    economy.c     # economyPass $34D04, the national economy
    tables.c      # GENERATED - do not edit
    advisor.c
    arco.c        # .arco, the native world format
)
target_include_directories(sc2kcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# arco.c needs deflate and a CRC.  lodepng already carries both for the
# renderer's PNGs, so the save format borrows them rather than vendoring
# a second compressor.  The target is defined here, and render/ uses this
# one, so there is exactly one copy in any link.
if(NOT TARGET sc2k_vendor)
    add_library(sc2k_vendor STATIC ../vendor/lodepng.c)
    target_include_directories(sc2k_vendor SYSTEM PUBLIC
                               "${CMAKE_CURRENT_SOURCE_DIR}/../vendor")
    if(MSVC)
        target_compile_options(sc2k_vendor PRIVATE /w)
        target_compile_definitions(sc2k_vendor PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(sc2k_vendor PRIVATE -w)
    endif()
endif()
target_link_libraries(sc2kcore PUBLIC sc2k_vendor)

# main.c holds the developer modes -- --verify, --clock, --growth and the
# rest.  It is a library rather than an executable because a_main.c
# dispatches into it: one binary, `arcology`, answers to all of them.
add_library(sc2kdev STATIC main.c)
target_link_libraries(sc2kdev PUBLIC sc2kcore)

# The reconstruction is held to a strict standard on purpose: a silent
# integer conversion here is a behavioural difference from the original.
foreach(tgt sc2kcore sc2kdev)
    target_compile_options(${tgt} PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:
            -Wall -Wextra -Wshadow -Wconversion -Wsign-compare -Wpointer-arith>
        $<$<C_COMPILER_ID:MSVC>:/W4>)
endforeach()

find_library(M_LIB m)
if(M_LIB)
    target_link_libraries(sc2kcore PUBLIC ${M_LIB})
endif()

# ---- house style ----------------------------------------------------
# .clang-format at the repository root is the style.  `format` rewrites the
# sources in place; `format-check` fails if any of them is not already
# formatted, and runs as part of ctest so the style cannot drift.
find_program(CLANG_FORMAT NAMES clang-format
             HINTS /opt/homebrew/bin /usr/local/bin)
set(SC2K_SOURCES main.c city.c sim.c rng.c ext80.c sane.c tables.c arco.c
                 sc2k.h ext80.h arco.h)
if(CLANG_FORMAT)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${SC2K_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting the C sources"
        VERBATIM)
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --dry-run --Werror ${SC2K_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking the C sources against .clang-format"
        VERBATIM)
endif()

# ---- verification ---------------------------------------------------
# The game folder is not redistributable, so point at your own copy:
#   cmake -B build -DSC2K_CITIES="/path/to/SimCity 2000® Collection"
set(SC2K_CITIES "" CACHE PATH "Folder holding the shipped city files")
if(SC2K_CITIES OR CLANG_FORMAT)
    enable_testing()
endif()
if(CLANG_FORMAT)
    add_test(NAME format
             COMMAND ${CLANG_FORMAT} --dry-run --Werror ${SC2K_SOURCES}
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
#  The `verify` test needs the executable, which lives one level up, so
#  it is registered in the root CMakeLists.txt instead of here.
