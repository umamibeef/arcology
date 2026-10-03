# ====================================================================
#  Arcology -- a reconstruction of the SimCity 2000 simulation engine.
#
#      cmake -B build
#      cmake --build build
#      ./build/arcology
#
#  One executable comes out: `arcology`.  It is the game, and it is also
#  the verification driver, the atlas dumper and the software rasteriser
#  -- see src/render/a_main.c for how it decides which.
#
#  To run the verification suite, point the build at your own copy of the
#  game (nothing in this repository can stand in for it):
#
#      cmake -B build -DARC_CITIES="/path/to/SimCity 2000 Collection"
#      ctest --test-dir build
# ====================================================================
cmake_minimum_required(VERSION 3.16)
#  ONE project(), here.  A sub-directory that declares its own starts a
#  project of its own: its version, its test scope and its packaging are
#  then separate, and which one a variable holds depends on where it is
#  read.  src/render and src/sim are directories of this one.
project(arcology
        VERSION 0.1.0
        DESCRIPTION "Arcology -- the SimCity 2000 simulation, reconstructed"
        LANGUAGES C CXX)

#  ---- what every directory below builds with --------------------------
#  C99 without compiler extensions.  This is a portability control, not a
#  style preference: -std=c99 rather than -std=gnu99 means a POSIX-only
#  call such as strdup, or an include of <unistd.h>, fails here on the
#  developer's machine instead of on someone else's platform.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
#  C++ hosts Dear ImGui (ui.cpp) and the log; everything else is C99.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

#  Put the binary at build/arcology rather than build/src/render/arcology.
#  Two dozen checkers in tools/ look for it there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

#  ---- what the whole tree shares --------------------------------------
#  The game folder is not redistributable, so a build that wants the
#  checks against the original points at your own copy.  ONE cache entry,
#  read by every directory below.
set(ARC_CITIES "" CACHE PATH "Folder holding the shipped city files")
#  The simulation's own checks run a 68k interpreter over a corpus of
#  cities and are slow.  Nothing in the renderer can move a simulation
#  layer, so they are off unless asked for.
option(ARC_SIM_TESTS "Run the simulation's checks against the original" OFF)
find_package(Python3 COMPONENTS Interpreter QUIET)

#  ONE enable_testing(), before the directories below register anything:
#  it enables testing for this directory and every one under it, and a
#  test added before it is not registered at all.
enable_testing()

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
if(Python3_FOUND AND ARC_CITIES)
    add_custom_target(showcase
        COMMAND ${CMAKE_COMMAND} -E env "ARC_CITIES=${ARC_CITIES}"
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_showcase.py
                --frames 16 --out ${CMAKE_CURRENT_SOURCE_DIR}/media/bayview.gif
        COMMAND ${CMAKE_COMMAND} -E env "ARC_CITIES=${ARC_CITIES}"
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_showcase.py
                --frames 12 --compare
                --out ${CMAKE_CURRENT_SOURCE_DIR}/media/bayview-renderers.gif
        DEPENDS arcology
        COMMENT "Rendering the README's animations from this build"
        VERBATIM)
endif()

# ---- verification ---------------------------------------------------
#  The format check lives in src/CMakeLists.txt because it is about the
#  sources; `verify` lives here because it needs the executable.
#  No printf in the renderer or the app: a message goes through the log
#  (R_ERR/R_WARN/R_NOTE/R_DBG) and a dump or a report line through dumpf,
#  which writes where --dump-to says.  This test needs nothing but the
#  sources, so it always runs.
add_test(NAME no_printf COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tools/no_printf.sh)
#  A comment states current fact: no history, no conversation, no
#  per-city example.  The same checker is the write hook.
add_test(NAME comment_lint COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/comment_lint.py
         src tools tests scripts CMakeLists.txt cmake docs README.md TODO.md CLAUDE.md .claude/CLAUDE.md
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
#  The documentation builds, and builds with no warning: a Sphinx warning
#  here is malformed markup that renders as visible garbage.  It needs
#  the docs' own virtual environment (docs/requirements.txt), so it is
#  registered only where that exists.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/.venv/bin/sphinx-build")
    add_test(NAME docs_build
             COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/docs/.venv/bin/sphinx-build" -b html -W -q
                     "${CMAKE_CURRENT_SOURCE_DIR}/docs" "${CMAKE_BINARY_DIR}/docs_check")
    set_tests_properties(docs_build PROPERTIES LABELS "gate;full" TIMEOUT 600)
endif()
#  The pipeline names no family: no road, railway, highway, ramp,
#  sidewalk or crossing in a filename, an identifier or a comment.
add_test(NAME family_words COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/family_words.py)
#  Goal 6: no routing in C.  The chain of points between two poses is a
#  script's, so no piece is filled in by hand outside the primitives.
add_test(NAME no_routing COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/no_routing.py)
#  The comments are Simplified Technical English: short sentences, the
#  active voice, no em dash, no semicolon, and one name for one thing.
add_test(NAME ste_comments COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/ste_lint.py)
add_test(NAME script_api COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/script_api.py)
#  The renderer's four levels: an include runs down them, or it is on
#  the list of exceptions with its reason.
add_test(NAME layering COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/layering.py)
add_test(NAME materials_fresh COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_materials.py --check)
#  The script API's reference page is generated from src/script, so it
#  cannot cite a method the program does not have.
add_test(NAME script_api_fresh COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_script_api.py --check)
#  The lint's world handle is generated from the pipeline's own WORLD[],
#  so it cannot answer a method the pipeline has not, and it cannot miss
#  one the pipeline has.
add_test(NAME lint_stub_fresh COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_lint_stub.py --check)
#  The shaders the GPU runs are compiled from the GLSL beside them and
#  checked in, so a plain build needs no shader tools.  One edited
#  without regenerating draws the last build's frame.
add_test(NAME shaders_fresh COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_shaders.py --check)

#  THE TABLES LIFTED OUT OF THE BINARY.  Both are checked in, so a plain
#  build needs no Python and no game folder; these say the checked-in
#  copy is still what the A5 image holds.  They read the game's own
#  files, so they run where those are.
if(ARC_CITIES)
    add_test(NAME sim_tables_fresh
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_tables.py --check)
    add_test(NAME render_tables_fresh
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_render_tables.py --check)
    foreach(t sim_tables_fresh render_tables_fresh)
        set_tests_properties(${t} PROPERTIES LABELS "oracle"
                             ENVIRONMENT "ARC_CITIES=${ARC_CITIES}")
    endforeach()
endif()

#  The simulation against the original, where the game folder is and the
#  simulation's checks were asked for (both are declared at the top).
if(ARC_CITIES AND ARC_SIM_TESTS)
    add_test(NAME verify
             COMMAND arcology --verify "${ARC_CITIES}" "${ARC_CITIES}/Cities")
    set_tests_properties(verify PROPERTIES
        PASS_REGULAR_EXPRESSION "codec round-trip, lossless[^\n]*100\\.00%")
endif()

#  .arco has to carry a city without losing anything.  This needs no game
#  folder -- the repository's own cities are the corpus.
if(Python3_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cities" AND ARC_SIM_TESTS)
    #  The year-end microsim pass, against $101AC.  Needs the oracle, so
    #  it only runs where the game folder is.
    if(ARC_CITIES)
        add_test(NAME microsim
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tools/micro_check.py)
        add_test(NAME allocmicro
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tools/micro_alloc_check.py)
        #  The rest of the oracle: each ported routine against the
        #  original's own, on the cities the brief quotes.  Bayview and
        #  Hawaii are the two with disasters on, so the trigger rolls.
        set(_oracle_cities "${ARC_CITIES}/Cities/Bayview" "${ARC_CITIES}/Cities/Hawaii")
        foreach(chk demolish footprint raise settile things)
            add_test(NAME oracle_${chk}
                     COMMAND ${Python3_EXECUTABLE}
                             ${CMAKE_CURRENT_SOURCE_DIR}/tools/${chk}_check.py ${_oracle_cities})
        endforeach()
        add_test(NAME oracle_growth
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tools/growth_check.py "${ARC_CITIES}/Cities")
        add_test(NAME oracle_cycle
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tools/cycle_check.py "${ARC_CITIES}/Cities" Bayview Hawaii)
        add_test(NAME oracle_multicycle
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tools/multicycle_check.py 3 ${_oracle_cities})
        #  The player's tools, against the original's own: each tile routine
        #  at its own tiles, then whole gestures through the tool's mouse
        #  loop.  These five carry every network between them, a tunnel,
        #  and trees by a raised tile.
        set(_tool_cities "")
        foreach(c atlanta flint bridgepo peter3 tomg-b3)
            list(APPEND _tool_cities "${CMAKE_CURRENT_SOURCE_DIR}/cities/${c}.sc2")
        endforeach()
        foreach(addr 8064 1F632 1FB60 1F2FE 1FFB8 20352 122E0)
            add_test(NAME oracle_tool_${addr}
                     COMMAND ${Python3_EXECUTABLE}
                             ${CMAKE_CURRENT_SOURCE_DIR}/tools/tool_check.py --call ${addr} ${_tool_cities})
            set_tests_properties(oracle_tool_${addr} PROPERTIES LABELS "oracle" TIMEOUT 3600)
        endforeach()
        add_test(NAME oracle_tool_gestures
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tools/tool_check.py --tool ${_tool_cities})
        foreach(t oracle_demolish oracle_footprint oracle_raise oracle_settile oracle_things oracle_growth oracle_cycle oracle_multicycle oracle_tool_gestures)
            set_tests_properties(${t} PROPERTIES LABELS "oracle" TIMEOUT 3600)
        endforeach()
    endif()
    add_test(NAME arco_roundtrip
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/arco_check.py)
endif()

#  The tiers (see src/render/CMakeLists.txt): these need no build.
foreach(t no_printf comment_lint family_words no_routing ste_comments script_api materials_fresh script_api_fresh lint_stub_fresh shaders_fresh format layering)
    if(TEST ${t})
        set_tests_properties(${t} PROPERTIES LABELS "lint;gate;full")
    endif()
endforeach()
foreach(t verify microsim allocmicro)
    if(TEST ${t})
        set_tests_properties(${t} PROPERTIES LABELS "oracle")
    endif()
endforeach()
if(TEST arco_roundtrip)
    set_tests_properties(arco_roundtrip PROPERTIES LABELS "full")
endif()
