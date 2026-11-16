cmake_minimum_required(VERSION 3.20)
project(Arachne VERSION 0.1.0 LANGUAGES C CXX)
# Arachne: pictures into vector art (SVG), and pictures from a prompt to trace. Part of Atelier.
# The tracer is Arachne's own (src/trace.cpp). The painting is stable-diffusion.cpp (MIT, ggml, CUDA), as in
# Firmament: built with -DSD_BUILD_SHARED_LIBS=ON, SDCPP pointing at its folder; stable-diffusion.dll is delay-loaded,
# so Arachne traces on any PC and only painting needs the NVIDIA card.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(SDCPP "D:/cc/_src/stable-diffusion.cpp" CACHE PATH "stable-diffusion.cpp's folder (with build-cuda/ built)")
set(SDCPP_BUILD "${SDCPP}/build-cuda" CACHE PATH "its build folder")

include(FetchContent)
# stb (public domain) and nanosvg (zlib): fetched once into external/
set(EXT ${CMAKE_SOURCE_DIR}/external)
foreach(f stb/stb_image.h stb/stb_image_write.h)
    if(NOT EXISTS ${EXT}/${f})
        get_filename_component(n ${f} NAME)
        file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/master/${n} ${EXT}/${f})
    endif()
endforeach()
foreach(n nanosvg.h nanosvgrast.h)
    if(NOT EXISTS ${EXT}/nanosvg/${n})
        file(DOWNLOAD https://raw.githubusercontent.com/memononen/nanosvg/master/src/${n} ${EXT}/nanosvg/${n})
    endif()
endforeach()

# ---- arachne-cli: the tracer from the command line
add_executable(arachne-cli src/cli.cpp src/trace.cpp src/stb.cpp)
target_include_directories(arachne-cli PRIVATE src ${EXT}/stb ${EXT}/nanosvg)
if(MSVC)
    target_compile_options(arachne-cli PRIVATE /W3 /utf-8 /O2 /D_CRT_SECURE_NO_WARNINGS)
endif()

# ---- Arachne.exe: the window
option(ARACHNE_GUI "Build the window (SDL2, Dear ImGui)" ON)
if(ARACHNE_GUI)
    if(EXISTS ${EXT}/imgui-1.90.4/imgui.h)
        set(FETCHCONTENT_SOURCE_DIR_IMGUI ${EXT}/imgui-1.90.4)
    endif()
    FetchContent_Declare(imgui URL https://github.com/ocornut/imgui/archive/refs/tags/v1.90.4.tar.gz)
    FetchContent_MakeAvailable(imgui)
    if(WIN32 AND NOT SDL2_DIR AND EXISTS ${EXT}/SDL2-2.30.9/cmake)
        set(SDL2_DIR ${EXT}/SDL2-2.30.9/cmake)
    endif()
    find_package(SDL2 REQUIRED)
    find_package(OpenGL REQUIRED)
    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL)
    set(SOURCES src/gui.cpp src/splash.cpp src/engine.cpp src/trace.cpp src/lang.cpp src/stb.cpp)
    if(WIN32)
        list(APPEND SOURCES src/app.rc)
    endif()
    add_executable(Arachne WIN32 ${SOURCES})
    target_include_directories(Arachne PRIVATE src ${EXT}/stb ${EXT}/nanosvg ${SDCPP}/include)
    target_link_libraries(Arachne PRIVATE imgui ${SDCPP_BUILD}/stable-diffusion.lib)
    target_compile_definitions(Arachne PRIVATE ARACHNE_VERSION="${PROJECT_VERSION}")
    if(WIN32)
        target_link_libraries(Arachne PRIVATE delayimp comdlg32 shell32 ole32)
        target_link_options(Arachne PRIVATE /DELAYLOAD:stable-diffusion.dll)
    endif()
    if(TARGET SDL2::SDL2main)
        target_link_libraries(Arachne PRIVATE SDL2::SDL2main)
    endif()
    if(MSVC)
        target_compile_options(Arachne PRIVATE /W3 /utf-8 /O2 /D_CRT_SECURE_NO_WARNINGS)
    endif()
    add_custom_command(TARGET Arachne POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:Arachne>/assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/lang $<TARGET_FILE_DIR:Arachne>/lang
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDCPP_BUILD}/bin/stable-diffusion.dll $<TARGET_FILE_DIR:Arachne>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:Arachne>)
endif()
