#  The in-game interface: Dear ImGui behind a C face (ui.h).  ImGui is
#  fetched from its repository at configure time, a small download the
#  first time, and built here with its SDL3 and SDL_GPU backends.
#  -DARC_IMGUI=OFF builds the game with the no-op face, ui_none.c.  With
#  no SDL3 there is no game, and no interface.
if(NOT SDL3_FOUND)
    return()
endif()
target_sources(arcology PRIVATE names.c messages.c commands.c)
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
    target_sources(arcology PRIVATE ui.cpp frame.cpp palette.cpp menubar.cpp windows.cpp inspect.cpp)
    target_link_libraries(arcology PRIVATE arc_imgui)
    target_compile_definitions(arcology PRIVATE ARC_IMGUI=1)
else()
    target_sources(arcology PRIVATE ui_none.c)
endif()
