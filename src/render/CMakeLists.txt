#  Arcology -- the renderer, and the one executable.
#
#  Everything the project builds lands in a single binary here: the game,
#  the verification driver, the atlas dumper and the software rasteriser,
#  dispatched by app/main.c.  Build from the repository root:
#
#      cmake -B build && cmake --build build && ctest --test-dir build
#
cmake_minimum_required(VERSION 3.16)
project(arcology
        VERSION 0.1
        DESCRIPTION "Arcology -- assets, camera, rasteriser, and the game"
        LANGUAGES C CXX)

#  C99 without compiler extensions.  This is a portability control, not a
#  style preference: -std=c99 rather than -std=gnu99 means a POSIX-only call
#  such as strdup or an include of <unistd.h> fails here, on the developer's
#  machine, instead of on someone else's platform.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
#  C++ is used only to host Dear ImGui (ui.cpp); everything else is C99.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(ARC_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/../../assets"
    CACHE PATH "Where artpack.py wrote the atlases")
find_package(Python3 COMPONENTS Interpreter QUIET)

# ---- warnings ------------------------------------------------------------
#  Applied to our code only.  A silent integer conversion in a renderer is a
#  off-by-one on someone's screen, so the simulation's standard applies here.
add_library(arc_warnings INTERFACE)
target_compile_options(arc_warnings INTERFACE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall -Wextra -Wshadow -Wconversion -Wsign-compare -Wpointer-arith>
    $<$<C_COMPILER_ID:MSVC>:/W4>)
if(MSVC)
    target_compile_definitions(arc_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
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
    city.c      # a City as the renderer wants to see it
    soft/soft.c        # the sweep: what the original draws, in what order
    soft/paint.c  # putting those ops down as pixels
    image.c       # the finished picture, and the PNG writers
    generated/tables.c  # GENERATED from the A5 image - do not edit
    generated/materials.c  # GENERATED from mesh/materials.def - do not edit
)
target_include_directories(arc_render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
                          "${CMAKE_CURRENT_SOURCE_DIR}/../util"
                          "${CMAKE_CURRENT_SOURCE_DIR}/../audio"
                          "${CMAKE_CURRENT_SOURCE_DIR}/../ui"
                          "${CMAKE_CURRENT_SOURCE_DIR}/../app"
                          "${CMAKE_CURRENT_SOURCE_DIR}/../script"
                          "${CMAKE_CURRENT_SOURCE_DIR}/generated")
target_link_libraries(arc_render PUBLIC arc_vendor arc_core
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
if(SDL3_FOUND)
    #  Logging: spdlog does the parts that are tedious to get right --
    #  colour sinks that know about terminals and the Windows console,
    #  a stderr/stdout split, levels from the environment, and locking.
    #  log.h keeps a small C face over it so the C sources are plain.
    include(FetchContent)
    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.15.1
        GIT_SHALLOW    TRUE)
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
    set(SPDLOG_INSTALL       OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(spdlog)

    add_executable(arcology
        ../app/main.c                     # the dispatcher
        ../app/app.c ../app/options.cpp ../app/modes.cpp ../app/paths.c ../app/uiglue.c
        ../app/world.c ../app/clock.c ../app/area.c
        prefs.c camera.c gpu/gpu.c gpu/frame.c adapt.c generated/shaders.c
        ../util/log.cpp ../util/devopts.c ../util/fs.c ../audio/sound.c ../audio/music.c
        mesh/mesh.c mesh/tile.c mesh/emit.c mesh/shapes.c mesh/check.c incr.c ../util/dump.c
        ../script/script.c ../script/lint.c  # the scripting layer, or its stubs where Lua is off
        mesh/shape.c
        walk/walk.c walk/cursor.c walk/cell.c walk/walkway.c
        mesh/fit.c mesh/surface.c mesh/loft.c mesh/piece.c
        mesh/model.c
        net/table.c net/lane.c net/station.c net/box.c net/meet.c net/band.c net/band_walk.c net/network.c net/drive.c geo.c build.c net/family.c net/spur.c net/spur_build.c net/shelf.c net/margin.c net/node.c net/cut.c net/strip.c
        net/traffic.c net/report.c
        atlas/dump.c soft/main.c)       # --atlas and --soft
    #  arc_dev links arc_core PUBLIC, so naming both here would put the
    #  archive on the link line twice.
    target_link_libraries(arcology PRIVATE arc_render arc_dev
        SDL3::SDL3 spdlog::spdlog arc_warnings)
    #  Lua: the scripting layer.  The numbers the geometry is tuned with
    #  and the rules it is drawn by -- which of a junction's mouths carry
    #  a meet, how its arms are controlled, how tight a lip return
    #  turns -- are reachable from a script that the running program
    #  watches, so changing one is a file save rather than a compile.
    #  Upstream ships no CMake, so the library's own sources are named
    #  here; lua.c and luac.c carry mains of their own and are left out.
    #  Built without the project's warning flags, as the other vendored
    #  code is: they are other people's sources.
    #  There is no switch for it.  The geometry, the decisions, the
    #  families and the drive are all in the scripts, so a build without
    #  them would draw the terrain and nothing else -- not a smaller
    #  city, no city.
    include(FetchContent)
    FetchContent_Declare(lua
        GIT_REPOSITORY https://github.com/lua/lua.git
        GIT_TAG        v5.4.7
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(lua)
    set(ARC_LUA_SRC lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem
        lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio
        lauxlib lbaselib lcorolib ldblib liolib lmathlib loadlib loslib
        lstrlib ltablib lutf8lib linit)
    list(TRANSFORM ARC_LUA_SRC PREPEND "${lua_SOURCE_DIR}/")
    list(TRANSFORM ARC_LUA_SRC APPEND ".c")
    add_library(arc_lua STATIC ${ARC_LUA_SRC})
    target_include_directories(arc_lua SYSTEM PUBLIC ${lua_SOURCE_DIR})
    if(APPLE)
        target_compile_definitions(arc_lua PRIVATE LUA_USE_MACOSX)
    elseif(UNIX)
        target_compile_definitions(arc_lua PRIVATE LUA_USE_LINUX)
    endif()
    target_sources(arcology PRIVATE
        ../script/api_tune.c ../script/api_rules.c ../script/api_world.c ../script/api_put.c ../script/api_model.c ../script/api_object.c ../script/api_tile.c ../script/api_family.c ../script/api_data.c ../script/api_fit.c)
    target_link_libraries(arcology PRIVATE arc_lua)
    if(ARC_IMGUI)
        include(FetchContent)
        FetchContent_Declare(imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG        v1.92.9b
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(imgui)
        add_library(arc_imgui STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp)
        target_include_directories(arc_imgui SYSTEM PUBLIC
            ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
        target_link_libraries(arc_imgui PUBLIC SDL3::SDL3)
        #  FreeType, when installed, rasterises the interface font monochrome
        #  with mono hinting: Chicago as the bitmap it was.  Without it the
        #  font is anti-aliased by ImGui's own rasteriser.
        find_package(Freetype QUIET)
        if(Freetype_FOUND)
            target_sources(arc_imgui PRIVATE ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp)
            target_include_directories(arc_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR}/misc/freetype)
            target_link_libraries(arc_imgui PUBLIC Freetype::Freetype)
            target_compile_definitions(arc_imgui PUBLIC IMGUI_ENABLE_FREETYPE)
            message(STATUS "FreeType found: the interface font is rasterised monochrome")
        else()
            message(STATUS "FreeType not found: the interface font is anti-aliased")
        endif()
        if(MSVC)
            target_compile_options(arc_imgui PRIVATE /w)
        else()
            target_compile_options(arc_imgui PRIVATE -w)
        endif()
        target_sources(arcology PRIVATE ../ui/ui.cpp)
        target_link_libraries(arcology PRIVATE arc_imgui)
        target_compile_definitions(arcology PRIVATE ARC_IMGUI=1)
    else()
        target_sources(arcology PRIVATE ../ui/ui_none.c)
    endif()
    install(TARGETS arcology RUNTIME DESTINATION bin)
else()
    #  No SDL3, so no game -- but the developer modes must still be
    #  reachable, because that is what the checkers in tools/ drive and
    #  what ctest runs.  app/stub.c stands in for the game and says why.
    add_executable(arcology ../app/main.c ../app/modes.cpp ../app/stub.c atlas/dump.c
                        soft/main.c ../util/devopts.c)
    target_link_libraries(arcology PRIVATE arc_render arc_dev arc_warnings)
    install(TARGETS arcology RUNTIME DESTINATION bin)
    message(STATUS "SDL3 not found: arcology is built without the game "
                   "(install SDL3, or configure with -DARC_FETCH_SDL3=ON)")
endif()

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
enable_testing()
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
    #  No segment may be broken: a hard corner (the red node in show curves)
    #  or a segment without geometry.  A ratchet against tools/corner_baseline.json
    #  until the corpus is at zero.
    add_test(NAME no_broken_segments COMMAND ${Python3_EXECUTABLE}
             "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/corner_check.py" --binary $<TARGET_FILE:arcology>)
    #  Nothing of a line may be laid under its own margin: a junction's
    #  fill beneath the junction's own margin, or a strip's
    #  way beneath its own, means the two were never made to meet.
    #  A ratchet against tools/buried_baseline.json until the corpus is at
    #  zero.
    add_test(NAME no_buried_margin COMMAND ${Python3_EXECUTABLE}
             "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/buried_check.py" --binary $<TARGET_FILE:arcology>)
    #  A junction's outline must be a simple ring: no vertex where it
    #  doubles back on itself, no two edges meet.  A spur's inward
    #  side points OUT of the junction, so the margin offset from it
    #  lands outside and the fill drawn inside the margin covers it.
    #  Zero, on every city that ships with the repository.
    foreach(city "atlanta" "toronto" "manhattan")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/${city}.sc2")
            add_test(NAME no_junction_spurs_${city}
                     COMMAND arcology "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/${city}.sc2" --mute --mesh-check)
        endif()
    endforeach()
    #  Every script the repository ships reads clean.  The linter knows
    #  the vocabulary, so a setting or a rule that is not one -- which is
    #  silent at run time and costs an iteration to notice -- is a fault
    #  here.
    #  C is not to call into a script except for the drive: Lua composes
    #  and C serves.  A RATCHET -- it fails when the count rises -- so
    #  the old contract cannot creep back while the new one is being
    #  finished, and so "inverted" is a number rather than a judgement.
    add_test(NAME call_up COMMAND ${Python3_EXECUTABLE}
             "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/call_up.py")
    add_test(NAME lua_lint COMMAND sh
             "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/lua_lint.sh" $<TARGET_FILE:arcology>)
    #  An incremental rebuild draws what a full build draws.  Which
    #  chunks an edit touches is a CLOSURE -- a prediction made before
    #  the building pass runs -- and one that predicts too little leaves
    #  the last build's triangles standing in the chunks it skipped,
    #  with nothing in the build to say so.  Five edits, each built both
    #  ways and compared as a hash of the sorted tile dump.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/atlanta.sc2")
        add_test(NAME incremental_rebuild COMMAND ${Python3_EXECUTABLE}
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/incr_check.py"
                 --binary $<TARGET_FILE:arcology>)
    endif()
    #  The scripting layer answers, and only when asked: the reference
    #  script changes nothing, a rule changes the build, and the build
    #  with that rule still passes its own checks.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/atlanta.sc2")
        add_test(NAME lua_rules COMMAND ${Python3_EXECUTABLE}
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/lua_check.py"
                 --binary $<TARGET_FILE:arcology>)
    endif()
    #  The band's interchanges are joined by a script and by nothing
    #  else: which slab lane goes on to which -- another band's round a
    #  loop, the line's where the slab comes down, or the inner lane of
    #  its own band -- is arc.rules.links's.  Clearing it leaves every
    #  band end unjoined.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/tokyo.sc2")
        add_test(NAME interchange COMMAND ${Python3_EXECUTABLE}
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/interchange_check.py"
                 --binary $<TARGET_FILE:arcology>)
        set_tests_properties(interchange PROPERTIES TIMEOUT 900)
    endif()
    #  The moving world, hashed.  A headless frame does not draw the
    #  movers, so the pixel battery says nothing about the traffic at
    #  all: this advances the world and hashes what moved and what was
    #  drawn, against a recorded reference.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/atlanta.sc2")
        add_test(NAME moving_world COMMAND ${Python3_EXECUTABLE}
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/traffic_check.py"
                 --binary $<TARGET_FILE:arcology>)
        set_tests_properties(moving_world PROPERTIES TIMEOUT 900)
    endif()
    #  Each interpretation is swappable: the sweep of a line through its
    #  corridor, an on-spur's climb, the pattern an intersection draws
    #  and what a line carries, each replaced by a DIFFERENT ALGORITHM in
    #  a Lua file and nothing else.  The mesh is compared as a hash of
    #  its triangles, sorted, so a surface that moved without changing
    #  the count is caught too.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/atlanta.sc2")
        add_test(NAME swappable COMMAND ${Python3_EXECUTABLE}
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/swap_check.py"
                 --binary $<TARGET_FILE:arcology>)
        set_tests_properties(swappable PROPERTIES TIMEOUT 900)
    endif()
    #  The network is the SCRIPT'S: taking arc.rules.network away takes
    #  every line, railway and power line with it, a rule of the script's
    #  own making draws a different network, and every run the shipped
    #  rule hands over is checked against the planes it was given.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/atlanta.sc2")
        add_test(NAME network_discovery COMMAND ${Python3_EXECUTABLE}
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/network_check.py"
                 --binary $<TARGET_FILE:arcology>)
        set_tests_properties(network_discovery PROPERTIES TIMEOUT 600)
    endif()
    #  WHAT GEOMETRY COLLIDES, as a ratchet over the whole corpus: it
    #  carries the numbers as they stand and fails when one RISES,
    #  because it is not at zero and a check that fails on the state of
    #  the world is a check somebody turns off.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/atlanta.sc2")
        add_test(NAME no_collisions COMMAND ${Python3_EXECUTABLE}
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/collide_check.py"
                 --binary $<TARGET_FILE:arcology>)
        set_tests_properties(no_collisions PROPERTIES TIMEOUT 900)
    endif()
    #  The loft is a service: a path the SCRIPT invented, swept along a
    #  cross-section the script wrote out as numbers, read back out of
    #  the mesh -- the face count, the height, the shape and the ground
    #  it follows.  A build with no arc.rules.invent draws none of it.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/atlanta.sc2")
        add_test(NAME loft_service COMMAND ${Python3_EXECUTABLE}
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/loft_check.py"
                 --binary $<TARGET_FILE:arcology>)
        set_tests_properties(loft_service PROPERTIES TIMEOUT 600)
    endif()
    #  The fit is a service: a path the SCRIPT invented, fitted through
    #  arc.fit, and the pieces that come back checked in Python -- ends,
    #  chain, arc, tangency, radius and budget.  Not the fit compared
    #  against another copy of itself.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/atlanta.sc2")
        add_test(NAME fit_service COMMAND ${Python3_EXECUTABLE}
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/fit_check.py"
                 --binary $<TARGET_FILE:arcology>)
    endif()
    #  What a script reads off the map is what is in the city file.  The
    #  city is decoded a second time in Python, so this is not the
    #  renderer agreeing with itself: every layer, every cell, read three
    #  ways -- the whole plane, a call a cell, and the four neighbours.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/atlanta.sc2")
        add_test(NAME map_read COMMAND ${Python3_EXECUTABLE}
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/map_read.py"
                 --binary $<TARGET_FILE:arcology>)
    endif()
    #  Two checks the compiler cannot make.  The analyser walks paths
    #  through a function and reports what only a path can show -- a
    #  pointer that may be null where it is read, a value stored and
    #  never used, memory that leaks on one branch.  The leak check runs
    #  the program to its end and counts what it could no longer reach,
    #  discounting the window server's own, which are not ours.
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        add_test(NAME static_analysis COMMAND ${Python3_EXECUTABLE}
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/static_check.py")
        set_tests_properties(static_analysis PROPERTIES TIMEOUT 600)
    endif()
    if(Python3_FOUND AND APPLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/atlanta.sc2")
        add_test(NAME no_leaks COMMAND ${Python3_EXECUTABLE}
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/leak_check.py"
                 --binary $<TARGET_FILE:arcology>)
        set_tests_properties(no_leaks PROPERTIES TIMEOUT 600)
    endif()
    #  The strip's surface is composed by a script from the stations the
    #  fit and the grading produced.  The pipeline can still build one
    #  pair the way its own stages want it, so the two are compared pair
    #  for pair over a whole city: floats against doubles cannot agree to
    #  the bit, and what this proves is that nothing else differs.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/atlanta.sc2")
        add_test(NAME lua_compose COMMAND sh
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/strip_check.sh" $<TARGET_FILE:arcology>)
    endif()
    #  A crosswalk is painted between two margins.  Every meet the
    #  network marks must therefore reach a margin at both of its ends,
    #  and the same run fails outright on one that does not.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/atlanta.sc2")
        add_test(NAME meets_reach_a_margin
                 COMMAND arcology "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/atlanta.sc2" --mute --mesh-check)
        set_tests_properties(meets_reach_a_margin PROPERTIES
            PASS_REGULAR_EXPRESSION "0 reaching no margin")
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
#    invariants   the renderer against the city data itself
#
#  tools/render_diff.py and tools/pixel_diff.py go further -- blit list and
#  pixels against the original renderer -- but they are slow enough to be
#  run by hand rather than from ctest.
set(ARC_CITIES "" CACHE PATH "Folder holding the shipped city files")
if(Python3_FOUND AND EXISTS "${ARC_ASSETS}/atlas.json")
    add_test(NAME blit_matches_game
             COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/blit_check.py")
    #  the music player: the pitch rule as an octave, and the theme's length
    add_test(NAME music_check
             COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/music_check.py"
                     $<TARGET_FILE:arcology>)
    add_test(NAME invariants
             COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/invariants.py")
endif()
#  The terrain mesh must be watertight: no edge of any triangle may be
#  free.  mesh_check counts them; the build fails the test on the first.
#  With --roads3d the same run also checks that no terrain face rises
#  above a line (mesh_check_clip).  ARC_CITIES is the game folder,
#  whose cities are under Cities/, or the Cities folder itself.
if(TARGET arcology AND ARC_CITIES AND EXISTS "${ARC_ASSETS}/atlas.json")
    foreach(city "Bayview" "Cape Wells" "Egypt Falls" "Felsenschloss")
        set(city_file "${ARC_CITIES}/Cities/${city}")
        if(NOT EXISTS "${city_file}")
            set(city_file "${ARC_CITIES}/${city}")
        endif()
        if(EXISTS "${city_file}")
            string(REPLACE " " "_" city_id "${city}")
            add_test(NAME mesh_watertight_${city_id}
                     COMMAND arcology "${ARC_ASSETS}" "${city_file}" --mesh-check --roads3d)
        endif()
    endforeach()
endif()

#  --atlas and --soft install with the one binary; nothing extra here.
