#  Arcology -- the renderer, and the one executable.
#
#  Everything the project builds lands in a single binary here: the game,
#  the verification driver, the atlas dumper and the software rasteriser,
#  dispatched by a_main.c.  Build from the repository root:
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
#  C++ is used only to host Dear ImGui (r_ui.cpp); everything else is C99.
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
#  header-only and is compiled into r_atlas.c.  Both are ISO C and both are
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
    r_atlas.c     # the sprite atlas and its manifest
    r_city.c      # a City as the renderer wants to see it
    r_soft.c      # the sweep and the software rasteriser
    r_tables.c    # GENERATED from the A5 image - do not edit
)
target_include_directories(sc2krender PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
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
    #  r_log.h keeps a small C face over it so the C sources are plain.
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
        a_main.c                          # the dispatcher
        r_app.c r_gpu.c r_log.cpp r_adapt.c r_mesh.c r_shaders.c r_sound.c
        r_atlasdump.c r_softmain.c)       # --atlas and --soft
    #  sc2kdev links sc2kcore PUBLIC, so naming both here would put the
    #  archive on the link line twice.
    target_link_libraries(arcology PRIVATE sc2krender sc2kdev
        SDL3::SDL3 spdlog::spdlog sc2k_warnings)
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
        target_sources(arcology PRIVATE r_ui.cpp)
        target_link_libraries(arcology PRIVATE sc2k_imgui)
        target_compile_definitions(arcology PRIVATE SC2K_IMGUI=1)
    else()
        target_sources(arcology PRIVATE r_ui_none.c)
    endif()
    install(TARGETS arcology RUNTIME DESTINATION bin)
else()
    #  No SDL3, so no game -- but the developer modes must still be
    #  reachable, because that is what the checkers in tools/ drive and
    #  what ctest runs.  a_stub.c stands in for the game and says why.
    add_executable(arcology a_main.c a_stub.c r_atlasdump.c r_softmain.c)
    target_link_libraries(arcology PRIVATE sc2krender sc2kdev sc2k_warnings)
    install(TARGETS arcology RUNTIME DESTINATION bin)
    message(STATUS "SDL3 not found: arcology is built without the game "
                   "(install SDL3, or configure with -DSC2K_FETCH_SDL3=ON)")
endif()

#  r_shaders.c is generated from shaders/*.vert|frag and checked in, so a
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
if(EXISTS "${SC2K_ASSETS}/atlas.json")
    add_test(NAME atlas_loads COMMAND arcology --atlas "${SC2K_ASSETS}")
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
    add_test(NAME invariants
             COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_CURRENT_SOURCE_DIR}/tests/invariants.py")
endif()
#  The terrain mesh must be watertight: no edge of any triangle may be
#  free.  r_mesh_check counts them; the build fails the test on the first.
#  With --roads3d the same run also checks that no terrain face rises
#  above a road (r_mesh_check_roads).  SC2K_CITIES is the game folder,
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
