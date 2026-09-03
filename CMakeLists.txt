# ====================================================================
#  Arcology -- a reconstruction of the SimCity 2000 simulation engine.
#
#      cmake -B build
#      cmake --build build
#      ./build/arcology
#
#  One executable comes out: `arcology`.  It is the game, and it is also
#  the verification driver, the atlas dumper and the software rasteriser
#  -- see sc2k/render/a_main.c for how it decides which.
#
#  To run the verification suite, point the build at your own copy of the
#  game (nothing in this repository can stand in for it):
#
#      cmake -B build -DSC2K_CITIES="/path/to/SimCity 2000 Collection"
#      ctest --test-dir build
# ====================================================================
cmake_minimum_required(VERSION 3.16)
project(arcology_all
        VERSION 0.1.0
        DESCRIPTION "Arcology -- the SimCity 2000 simulation, reconstructed"
        LANGUAGES C CXX)

#  Put the binary at build/arcology rather than build/sc2k/render/arcology.
#  Two dozen checkers in tools/ look for it there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

#  src/render pulls in ../sim itself, so adding it is enough to build
#  the simulation, the renderer and the executable.
add_subdirectory(src/render)

# ---- the version --------------------------------------------------------
#  Semver is the VERSION above, and that is the only place it is typed.
#  The commit, whether the tree was clean and the commit's date come from
#  git on EVERY build, through cmake/version.cmake, into a generated
#  header: build/generated/arc_version.h.  The script rewrites the file
#  only when its content changes, so nothing recompiles for nothing.
#  The banner and --version read it; a_main.c and r_app.c include it.
set(ARC_VERSION_H "${CMAKE_BINARY_DIR}/generated/arc_version.h")
add_custom_target(arc_version ALL
    COMMAND ${CMAKE_COMMAND}
            -DSRC=${CMAKE_SOURCE_DIR}
            -DOUT=${ARC_VERSION_H}
            -DVERSION=${PROJECT_VERSION}
            -P ${CMAKE_SOURCE_DIR}/cmake/version.cmake
    BYPRODUCTS ${ARC_VERSION_H}
    COMMENT "the version header"
    VERBATIM)
if(TARGET arcology)
    add_dependencies(arcology arc_version)
    target_include_directories(arcology PRIVATE "${CMAKE_BINARY_DIR}/generated")
endif()

# ---- the README's animations ----------------------------------------
#  The GIFs at the top of README.md are real frames of this build, so
#  they go stale the moment the renderer or the simulation changes.
#  There is no CI that can regenerate them -- rendering needs the game's
#  own art, which cannot be published -- so it is a target you run.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND AND SC2K_CITIES)
    add_custom_target(showcase
        COMMAND ${CMAKE_COMMAND} -E env "SC2K_CITIES=${SC2K_CITIES}"
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_showcase.py
                --frames 16 --out ${CMAKE_CURRENT_SOURCE_DIR}/media/bayview.gif
        COMMAND ${CMAKE_COMMAND} -E env "SC2K_CITIES=${SC2K_CITIES}"
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_showcase.py
                --frames 12 --compare
                --out ${CMAKE_CURRENT_SOURCE_DIR}/media/bayview-renderers.gif
        DEPENDS arcology
        COMMENT "Rendering the README's animations from this build"
        VERBATIM)
endif()

# ---- verification ---------------------------------------------------
#  The format check lives in sc2k/CMakeLists.txt because it is about the
#  sources; `verify` lives here because it needs the executable.
set(SC2K_CITIES "" CACHE PATH "Folder holding the shipped city files")
if(SC2K_CITIES)
    enable_testing()
    add_test(NAME verify
             COMMAND arcology --verify "${SC2K_CITIES}" "${SC2K_CITIES}/Cities")
endif()

#  .arco has to carry a city without losing anything.  This needs no game
#  folder -- the repository's own cities are the corpus.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cities")
    enable_testing()
    #  The year-end microsim pass, against $101AC.  Needs the oracle, so
    #  it only runs where the game folder is.
    if(SC2K_CITIES)
        add_test(NAME microsim
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tools/micro_check.py)
        add_test(NAME allocmicro
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tools/micro_alloc_check.py)
    endif()
    add_test(NAME arco_roundtrip
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/arco_check.py)

    set_tests_properties(verify PROPERTIES
        PASS_REGULAR_EXPRESSION "codec round-trip, lossless[^\n]*100\\.00%")
endif()
