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

set(SC2K_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/../../assets"
    CACHE PATH "Where sc2kpack.py wrote the atlases")
find_package(Python3 COMPONENTS Interpreter QUIET)

# ---- warnings ------------------------------------------------------------
#  Applied to our code only.  A silent integer conversion in a renderer is a
#  off-by-one on someone's screen, so the simulation's standard applies here.
add_library(sc2k_warnings INTERFACE)
target_compile_options(sc2k_warnings INTERFACE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall -Wextra -Wshadow -Wconversion -Wsign-compare -Wpointer-arith>
    $<$<C_COMPILER_ID:MSVC>:/W4>)
if(MSVC)
    target_compile_definitions(sc2k_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
endif()

# ---- vendored third party ------------------------------------------------
#  lodepng reads the indexed PNGs, including their palette and tRNS, which
#  stb_image cannot do -- it expands palettes and hands back RGBA.  jsmn is
#  header-only and is compiled into atlas.c.  Both are ISO C and both are
#  built with warnings off: they are not ours to fix.
#  The simulation comes first: it defines sc2k_vendor (lodepng), which
#  arco.c needs for deflate and the renderer needs for PNG.  One
#  definition, one copy in the link.
if(NOT TARGET sc2kcore)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../sim"
                     "${CMAKE_CURRENT_BINARY_DIR}/sim")
endif()

# ---- the renderer library ------------------------------------------------
#  Everything that is not a main: the atlas, the view, the sweep and the
#  generated tables.  The executable's own sources are listed further
#  down, with the SDL3 it needs.
add_library(sc2krender STATIC
    atlas/atlas.c     # the sprite atlas and its manifest
    city.c      # a City as the renderer wants to see it
    soft/soft.c        # the sweep: what the original draws, in what order
    soft/paint.c  # putting those ops down as pixels
    image.c       # the finished picture, and the PNG writers
    generated/tables.c  # GENERATED from the A5 image - do not edit
)
target_include_directories(sc2krender PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
                          "${CMAKE_CURRENT_SOURCE_DIR}/../util"
                          "${CMAKE_CURRENT_SOURCE_DIR}/../audio"
                          "${CMAKE_CURRENT_SOURCE_DIR}/../ui"
                          "${CMAKE_CURRENT_SOURCE_DIR}/../app"
                          "${CMAKE_CURRENT_SOURCE_DIR}/../script"
                          "${CMAKE_CURRENT_SOURCE_DIR}/generated")
target_link_libraries(sc2krender PUBLIC sc2k_vendor sc2kcore
                      PRIVATE sc2k_warnings)

#  The atlas dumper and the software rasteriser used to be executables of
#  their own.  They are `arcology --atlas` and `arcology --soft` now, so
#  their sources go into the one binary below.

# ---- the one executable: arcology -------------------------------------
#  Needs SDL3.  A system install is found first; failing that, the build
#  can fetch SDL3 from source (a few minutes the first time).  The
#  simulation is pulled in as the sc2kcore library from ../CMakeLists.txt.
option(SC2K_FETCH_SDL3 "Fetch and build SDL3 from source when none is installed" OFF)
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND AND SC2K_FETCH_SDL3)
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
#  SDL_GPU backends.  -DSC2K_IMGUI=OFF builds the game without a UI.
option(SC2K_IMGUI "Build the in-game UI with Dear ImGui" ON)
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
        mesh/mesh.c mesh/tile.c mesh/emit.c mesh/shapes.c mesh/check.c mesh/incr.c ../util/dump.c
        ../script/script.c ../script/lint.c  # the scripting layer, or its stubs where Lua is off
        mesh/shape.c net/walk.c net/table.c net/fit.c net/grade.c net/loft.c net/junction.c net/piece.c
        net/geo.c net/model.c net/road.c net/rail.c net/hiway.c net/power.c net/lane.c net/sidewalk.c net/walkway.c net/furniture.c net/marking.c net/traffic.c net/report.c
        atlas/dump.c soft/main.c)       # --atlas and --soft
    #  sc2kdev links sc2kcore PUBLIC, so naming both here would put the
    #  archive on the link line twice.
    target_link_libraries(arcology PRIVATE sc2krender sc2kdev
        SDL3::SDL3 spdlog::spdlog sc2k_warnings)
    #  Lua: the scripting layer.  The numbers the geometry is tuned with
    #  and the rules it is drawn by -- which of a junction's mouths carry
    #  a crossing, how its arms are controlled, how tight a kerb return
    #  turns -- are reachable from a script that the running program
    #  watches, so changing one is a file save rather than a compile.
    #  Upstream ships no CMake, so the library's own sources are named
    #  here; lua.c and luac.c carry mains of their own and are left out.
    #  Built without the project's warning flags, as the other vendored
    #  code is: they are other people's sources.
    option(SC2K_LUA "Build the scripting layer with Lua 5.4" ON)
    if(SC2K_LUA)
        include(FetchContent)
        FetchContent_Declare(lua
            GIT_REPOSITORY https://github.com/lua/lua.git
            GIT_TAG        v5.4.7
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(lua)
        set(SC2K_LUA_SRC lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem
            lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio
            lauxlib lbaselib lcorolib ldblib liolib lmathlib loadlib loslib
            lstrlib ltablib lutf8lib linit)
        list(TRANSFORM SC2K_LUA_SRC PREPEND "${lua_SOURCE_DIR}/")
        list(TRANSFORM SC2K_LUA_SRC APPEND ".c")
        add_library(sc2k_lua STATIC ${SC2K_LUA_SRC})
        target_include_directories(sc2k_lua SYSTEM PUBLIC ${lua_SOURCE_DIR})
        if(APPLE)
            target_compile_definitions(sc2k_lua PRIVATE LUA_USE_MACOSX)
        elseif(UNIX)
            target_compile_definitions(sc2k_lua PRIVATE LUA_USE_LINUX)
        endif()
        target_sources(arcology PRIVATE
            ../script/api_tune.c ../script/api_rules.c ../script/api_world.c ../script/api_put.c ../script/api_model.c ../script/api_object.c ../script/api_tile.c)
        target_link_libraries(arcology PRIVATE sc2k_lua)
        target_compile_definitions(arcology PRIVATE SC2K_LUA=1)
    endif()
    if(SC2K_IMGUI)
        include(FetchContent)
        FetchContent_Declare(imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG        v1.92.9b
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(imgui)
        add_library(sc2k_imgui STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp)
        target_include_directories(sc2k_imgui SYSTEM PUBLIC
            ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
        target_link_libraries(sc2k_imgui PUBLIC SDL3::SDL3)
        #  FreeType, when installed, rasterises the interface font monochrome
        #  with mono hinting: Chicago as the bitmap it was.  Without it the
        #  font is anti-aliased by ImGui's own rasteriser.
        find_package(Freetype QUIET)
        if(Freetype_FOUND)
            target_sources(sc2k_imgui PRIVATE ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp)
            target_include_directories(sc2k_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR}/misc/freetype)
            target_link_libraries(sc2k_imgui PUBLIC Freetype::Freetype)
            target_compile_definitions(sc2k_imgui PUBLIC IMGUI_ENABLE_FREETYPE)
            message(STATUS "FreeType found: the interface font is rasterised monochrome")
        else()
            message(STATUS "FreeType not found: the interface font is anti-aliased")
        endif()
        if(MSVC)
            target_compile_options(sc2k_imgui PRIVATE /w)
        else()
            target_compile_options(sc2k_imgui PRIVATE -w)
        endif()
        target_sources(arcology PRIVATE ../ui/ui.cpp)
        target_link_libraries(arcology PRIVATE sc2k_imgui)
        target_compile_definitions(arcology PRIVATE SC2K_IMGUI=1)
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
    target_link_libraries(arcology PRIVATE sc2krender sc2kdev sc2k_warnings)
    install(TARGETS arcology RUNTIME DESTINATION bin)
    message(STATUS "SDL3 not found: arcology is built without the game "
                   "(install SDL3, or configure with -DSC2K_FETCH_SDL3=ON)")
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
#  Splitting sim.c meant taking `static` off everything that turned out
#  to be shared.  This fails if one came off that did not have to.
add_test(NAME layers COMMAND ${Python3_EXECUTABLE}
         "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/layer_check.py")

if(EXISTS "${SC2K_ASSETS}/atlas.json")
    add_test(NAME atlas_loads COMMAND arcology --atlas "${SC2K_ASSETS}")
    #  No segment may be broken: a hard corner (the red node in show curves)
    #  or a segment without geometry.  A ratchet against tools/corner_baseline.json
    #  until the corpus is at zero.
    add_test(NAME no_broken_segments COMMAND ${Python3_EXECUTABLE}
             "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/corner_check.py" --binary $<TARGET_FILE:arcology>)
    #  Nothing of a road may be laid under its own footway: a junction's
    #  asphalt beneath the junction's own pavement, or a strip's
    #  carriageway beneath its own, means the two were never made to meet.
    #  A ratchet against tools/buried_baseline.json until the corpus is at
    #  zero.
    add_test(NAME no_buried_footway COMMAND ${Python3_EXECUTABLE}
             "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/buried_check.py" --binary $<TARGET_FILE:arcology>)
    #  A junction's outline must be a simple ring: no vertex where it
    #  doubles back on itself, no two edges crossing.  A spur's inward
    #  side points OUT of the junction, so the footway offset from it
    #  lands outside and the asphalt drawn inside the footway covers it.
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
    if(SC2K_LUA)
        add_test(NAME lua_lint COMMAND sh
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/lua_lint.sh" $<TARGET_FILE:arcology>)
    endif()
    #  The scripting layer answers, and only when asked: the reference
    #  script changes nothing, a rule changes the build, and the build
    #  with that rule still passes its own checks.
    if(SC2K_LUA AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/atlanta.sc2")
        add_test(NAME lua_rules COMMAND ${Python3_EXECUTABLE}
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/lua_check.py"
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
    if(SC2K_LUA AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/atlanta.sc2")
        add_test(NAME lua_compose COMMAND sh
                 "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/strip_check.sh" $<TARGET_FILE:arcology>)
    endif()
    #  A crosswalk is painted between two pavements.  Every crossing the
    #  network marks must therefore reach a footway at both of its ends,
    #  and the same run fails outright on one that does not.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/atlanta.sc2")
        add_test(NAME crossings_reach_a_pavement
                 COMMAND arcology "${CMAKE_CURRENT_SOURCE_DIR}/../../cities/atlanta.sc2" --mute --mesh-check)
        set_tests_properties(crossings_reach_a_pavement PROPERTIES
            PASS_REGULAR_EXPRESSION "0 reaching no pavement")
    endif()
    set_tests_properties(atlas_loads PROPERTIES
        PASS_REGULAR_EXPRESSION "1448 tiles across 3 levels")
endif()

#  The tests that matter compare against the GAME, not against another
#  implementation of ours.  There used to be a second renderer in Python
#  and a test that the two agreed; they did agree, exactly, while both were
#  wrong.  These need the emulator and the shipped cities, so they are
#  opt-in.
#
#    blit_check   every sprite through the original $18E96, both mirror
#                 states, every anchor
#    invariants   the renderer against the city data itself
#
#  tools/render_diff.py and tools/pixel_diff.py go further -- blit list and
#  pixels against the original renderer -- but they are slow enough to be
#  run by hand rather than from ctest.
set(SC2K_CITIES "" CACHE PATH "Folder holding the shipped city files")
if(Python3_FOUND AND EXISTS "${SC2K_ASSETS}/atlas.json")
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
#  above a road (mesh_check_roads).  SC2K_CITIES is the game folder,
#  whose cities are under Cities/, or the Cities folder itself.
if(TARGET arcology AND SC2K_CITIES AND EXISTS "${SC2K_ASSETS}/atlas.json")
    foreach(city "Bayview" "Cape Wells" "Egypt Falls" "Felsenschloss")
        set(city_file "${SC2K_CITIES}/Cities/${city}")
        if(NOT EXISTS "${city_file}")
            set(city_file "${SC2K_CITIES}/${city}")
        endif()
        if(EXISTS "${city_file}")
            string(REPLACE " " "_" city_id "${city}")
            add_test(NAME mesh_watertight_${city_id}
                     COMMAND arcology "${SC2K_ASSETS}" "${city_file}" --mesh-check --roads3d)
        endif()
    endforeach()
endif()

#  --atlas and --soft install with the one binary; nothing extra here.
