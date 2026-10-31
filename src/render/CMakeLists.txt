#  Arcology -- the renderer, and the one executable.
#
#  Everything the project builds lands in a single binary here: the game,
#  the verification driver, the atlas dumper and the software rasteriser,
#  dispatched by app/main.c.  Build from the repository root:
#
#      cmake -B build && cmake --build build && ctest --test-dir build
#
#  This is a DIRECTORY of the one project the root declares, not a
#  project of its own: the version, the language standards, the build
#  type, ARC_CITIES, Python3 and enable_testing() are all settled there.

set(ARC_ASSETS "${CMAKE_SOURCE_DIR}/assets"
    CACHE PATH "Where artpack.py wrote the atlases")

# ---- warnings ------------------------------------------------------------
#  Applied to our code only.  A silent integer conversion in a renderer is a
#  off-by-one on someone's screen, so the simulation's standard applies here.
add_library(arc_warnings INTERFACE)
target_compile_options(arc_warnings INTERFACE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall -Wextra -Wshadow -Wconversion -Wsign-compare -Wpointer-arith
        -ffp-contract=off>
    $<$<C_COMPILER_ID:MSVC>:/W4 /fp:precise>)
#  No fused multiply-add.  With contraction on, a function inlined into
#  its caller rounds differently from the same function called across a
#  translation unit, so moving a function between files moves the mesh
#  by a few triangles.  A proof that a refactor is output-identical
#  means nothing while the compiler's inlining decides the bits.  Off, the arithmetic is the source's, wherever it is placed.
if(MSVC)
    target_compile_definitions(arc_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
endif()
#  A warning here is a defect: a silent integer conversion is an
#  off-by-one on somebody's screen.  Continuous integration says so by
#  building with this on.  It is OFF by default, because a newer compiler
#  turning a new warning on must not stop a person building the game.
option(ARC_WERROR "Treat compiler warnings as errors" OFF)
if(ARC_WERROR)
    target_compile_options(arc_warnings INTERFACE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Werror>
        $<$<C_COMPILER_ID:MSVC>:/WX>)
endif()

# ---- vendored third party ------------------------------------------------
#  lodepng reads the indexed PNGs, including their palette and tRNS, which
#  stb_image cannot do -- it expands palettes and hands back RGBA.  jsmn is
#  header-only and is compiled into atlas.c.  Both are ISO C and both are
#  built with warnings off: they are not ours to fix.
#  The simulation comes first: it defines arc_vendor (lodepng), which
#  arco.c needs for deflate and the renderer needs for PNG.  One
#  definition, one copy in the link.
if(NOT TARGET arc_core)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../sim"
                     "${CMAKE_CURRENT_BINARY_DIR}/sim")
endif()

# ---- the renderer library ------------------------------------------------
#  Everything that is not a main: the atlas, the view, the sweep and the
#  generated tables.  The executable's own sources are listed further
#  down, with the SDL3 it needs.
add_library(arc_render STATIC
    atlas/atlas.c     # the sprite atlas and its manifest
    soft/soft.c        # the sweep: what the original draws, in what order
    soft/paint.c  # putting those ops down as pixels
    soft/image.c  # the finished picture, and the PNG writers
    generated/tables.c  # GENERATED from the A5 image - do not edit
    generated/materials.c  # GENERATED from mesh/materials.def - do not edit
    mesh/lod.cpp       # clusterlod.h, compiled once, behind a C face
)
target_include_directories(arc_render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
                          "${CMAKE_CURRENT_SOURCE_DIR}/../cells"
                          "${CMAKE_CURRENT_SOURCE_DIR}/../util"
                          "${CMAKE_CURRENT_SOURCE_DIR}/../audio"
                          "${CMAKE_CURRENT_SOURCE_DIR}/../ui"
                          "${CMAKE_CURRENT_SOURCE_DIR}/../app"
                          "${CMAKE_CURRENT_SOURCE_DIR}/generated")
target_link_libraries(arc_render PUBLIC arc_vendor arc_core meshoptimizer
                      PRIVATE arc_warnings)

#  The atlas dumper and the software rasteriser are `arcology --atlas`
#  and `arcology --soft`, so their sources go into the one binary below.

# ---- the one executable: arcology -------------------------------------
#  Needs SDL3.  A system install is found first; failing that, the build
#  can fetch SDL3 from source (a few minutes the first time).  The
#  simulation is pulled in as the arc_core library from ../CMakeLists.txt.
option(ARC_FETCH_SDL3 "Fetch and build SDL3 from source when none is installed" OFF)
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND AND ARC_FETCH_SDL3)
    include(FetchContent)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.4.14
        GIT_SHALLOW    TRUE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
    set(SDL3_FOUND TRUE)
endif()
#  The in-game UI is Dear ImGui, fetched from its repository at configure
#  time (a small download the first time) and built here with its SDL3 and
#  SDL_GPU backends.  -DARC_IMGUI=OFF builds the game without a UI.
option(ARC_IMGUI "Build the in-game UI with Dear ImGui" ON)
#  Logging: spdlog does the parts that are tedious to get right --
#  colour sinks that know about terminals and the Windows console, a
#  stderr/stdout split, levels from the environment, and locking.  log.h
#  keeps a small C face over it so the C sources are plain.  It needs no
#  window, so it is here rather than behind SDL3.
include(FetchContent)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.1
    GIT_SHALLOW    TRUE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL       OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

#  Mesh work: meshoptimizer (indexing, simplification, vertex packing),
#  and the two companion projects distributed with it: clusterlod.h, the
#  single-header library for continuous level of detail by clustered
#  simplification, compiled once in mesh/lod.cpp; and gltfpack, the
#  command-line tool that optimizes the glTF the world is written out as.
FetchContent_Declare(meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG        v1.3
    GIT_SHALLOW    TRUE)
set(MESHOPT_BUILD_GLTFPACK ON  CACHE BOOL "" FORCE)
set(MESHOPT_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(meshoptimizer)
target_include_directories(arc_render SYSTEM PRIVATE "${meshoptimizer_SOURCE_DIR}/demo") # clusterlod.h, a vendor header: its warnings are not ours

#  ONE executable, built whatever is installed.  These are the sources
#  that need no window: the renderer's own here, and each other directory's
#  in its own CMakeLists.txt, added below.  The GAME is added where SDL3
#  was found.  There is no second list of these: a source named in one
#  branch and forgotten in the other is how a branch nobody builds stops
#  linking.
add_executable(arcology
    turn.c shore.c
    mesh/mesh.c mesh/list.c mesh/tile.c mesh/ground_bytes.c mesh/ground.c mesh/emit.c mesh/shapes.c mesh/check.c incr.c show.c
    mesh/shape.c
    prof.c
    mesh/surface.c mesh/poly.c mesh/poly.cpp mesh/slab.c mesh/window.c
    mesh/model.c
    geo.c build.c compose.c material.c trees.c draw/roads.c draw/rails.c draw/bands.c draw/lamps.c draw/laid.c draw/rail_shape.c
    draw/net_graph.c draw/net_overlay.c
    atlas/dump.c soft/main.c)       # --atlas and --soft
#  Clipper2, the polygon union and difference mesh/poly.cpp offers: vendored
#  unchanged (src/vendor/clipper2, Boost licence), so its warnings are not ours.
add_library(arc_clipper2 STATIC
    ../vendor/clipper2/src/clipper.engine.cpp
    ../vendor/clipper2/src/clipper.offset.cpp
    ../vendor/clipper2/src/clipper.rectclip.cpp)
target_include_directories(arc_clipper2 SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/clipper2/include")
#  arc_dev links arc_core PUBLIC, so naming both here would put the
#  archive on the link line twice.
target_link_libraries(arcology PRIVATE arc_render arc_dev spdlog::spdlog arc_warnings arc_clipper2)
install(TARGETS arcology RUNTIME DESTINATION bin)

# ---- the GAME, where SDL3 was found --------------------------------------
#  A window, a GPU device, sound and the in-game UI.  Everything above is
#  built either way; this is what a window adds.
if(SDL3_FOUND)
    target_sources(arcology PRIVATE gpu/gpu.c gpu/frame.c gpu/air.c gpu/clouds.c generated/shaders.c)
    target_link_libraries(arcology PRIVATE SDL3::SDL3)
endif()

# ---- every other directory's sources ------------------------------------
#  Each directory lists its own sources into the one executable.
foreach(d util cells network dev ui audio app)
    add_subdirectory(../${d} "${CMAKE_CURRENT_BINARY_DIR}/../${d}")
endforeach()

#  shaders.c is generated from gpu/shaders/*.vert|frag and checked in, so a
#  plain build needs no shader tools.  Regenerate with:
#     cmake --build build --target render-shaders
if(Python3_FOUND)
    add_custom_target(render-shaders
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/gen_shaders.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
        COMMENT "Compiling the GLSL to SPIR-V and MSL and embedding it"
        VERBATIM)
endif()

# ---- tests ---------------------------------------------------------------
#  Testing is enabled once, at the root.
#  These two need no assets and no built binary: they read the tree.
#  Registered outside the assets check so a clone without the game's art
#  still enforces the seam and the projection.
add_test(NAME seam COMMAND ${Python3_EXECUTABLE}
         "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/seam_check.py")
add_test(NAME projection COMMAND ${Python3_EXECUTABLE}
         "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/project_check.py")
#  Nothing in src/sim is global by accident: a routine loses its
#  `static` only where another part calls it.  This fails on any that
#  kept it without needing to.
add_test(NAME layers COMMAND ${Python3_EXECUTABLE}
         "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/layer_check.py")

if(EXISTS "${ARC_ASSETS}/atlas.json")
    add_test(NAME atlas_loads COMMAND arcology --atlas "${ARC_ASSETS}")
    #  Layer 0 of the road network, held to the fitters' own join tests
    #  over every byte.  It also counts the edges each byte claims over
    #  the cities that ship with the repository.
    file(GLOB _net_cell_cities "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/*.sc2")
    add_test(NAME net_cells COMMAND arcology --net-cells ${_net_cell_cities})
    #  Layers 1 and 2 of the road network, held to the lane check over
    #  the cities that ship: every lane goes on and is fed, and every
    #  sidewalk has a lane at each end.  No mesh is built.
    add_test(NAME net_check COMMAND ${Python3_EXECUTABLE}
             "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/net_check.py" $<TARGET_FILE:arcology>)
    #  A check the compiler cannot make.  The analyser walks paths
    #  through a function and reports what only a path can show -- a
    #  pointer that may be null where it is read, a value stored and
    #  never used, memory that leaks on one branch.
    if(Python3_FOUND)
        add_test(NAME static_analysis COMMAND ${Python3_EXECUTABLE}
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/static_check.py")
        set_tests_properties(static_analysis PROPERTIES TIMEOUT 600)
    endif()
    set_tests_properties(atlas_loads PROPERTIES
        PASS_REGULAR_EXPRESSION "1448 tiles across 3 levels")
endif()

#  The tests that matter compare against the GAME, not against another
#  implementation of ours.  Two renderers of ours agree exactly while
#  both are wrong, and the agreement says nothing.  These need the
#  emulator and the shipped cities, so they are opt-in.
#
#    blit_check   every sprite through the original $18E96, both mirror
#                 states, every anchor
#
#  tools/render_diff.py and tools/pixel_diff.py go further -- blit list and
#  pixels against the original renderer -- but they are slow enough to be
#  run by hand rather than from ctest.  ARC_CITIES is the root's.
if(Python3_FOUND AND EXISTS "${ARC_ASSETS}/atlas.json")
    add_test(NAME blit_matches_game
             COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/blit_check.py")
    #  the music player: the pitch rule as an octave, and the theme's length
    add_test(NAME music_check
             COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/music_check.py"
                     $<TARGET_FILE:arcology>)
endif()
#  --atlas and --soft install with the one binary; nothing extra here.

#  THE TIERS.  `ctest -L lint` needs no build and runs in a second;
#  `ctest -L gate` adds the atlas load, the music
#  check and the docs build to the lint tier, and builds no city; `ctest -L full` is everything that
#  needs only the repository; `ctest -L oracle` needs the game's own
#  files.  A test carries every tier it belongs to.
foreach(t seam projection layers)
    if(TEST ${t})
        set_tests_properties(${t} PROPERTIES LABELS "lint;gate;full")
    endif()
endforeach()
foreach(t atlas_loads net_cells net_check music_check)
    if(TEST ${t})
        set_tests_properties(${t} PROPERTIES LABELS "gate;full")
    endif()
endforeach()
foreach(t static_analysis)
    if(TEST ${t})
        set_tests_properties(${t} PROPERTIES LABELS "full")
    endif()
endforeach()
foreach(t blit_matches_game)
    if(TEST ${t})
        set_tests_properties(${t} PROPERTIES LABELS "oracle")
    endif()
endforeach()
