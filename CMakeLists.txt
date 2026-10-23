cmake_minimum_required(VERSION 3.20)
project(VideoWallpaper VERSION 2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(LibVLC REQUIRED)

# ---- shared core -------------------------------------------------------------
add_library(vw_core STATIC
    src/core/Config.cpp
    src/core/FrameQueue.cpp
    src/core/Menu.cpp
    src/core/VlcPlayer.cpp
    src/core/Wallpaper.cpp
)
target_include_directories(vw_core PUBLIC src)
target_link_libraries(vw_core PUBLIC LibVLC::LibVLC)
if(MSVC)
    target_compile_options(vw_core PUBLIC /utf-8 /W4)
else()
    target_compile_options(vw_core PUBLIC -Wall -Wextra)
endif()

# ---- Windows: WorkerW + Shell_NotifyIcon ---------------------------------------
if(WIN32)
    add_executable(videowallpaper WIN32
        src/win/main.cpp
        src/win/DesktopSurface.cpp
        src/win/Session.cpp
        src/win/VideoWallpaper.rc
    )
    target_include_directories(videowallpaper PRIVATE src/win)
    target_compile_definitions(videowallpaper PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(videowallpaper PRIVATE vw_core user32 gdi32 shell32 comdlg32 wtsapi32 ole32 dwmapi)
    if(MINGW)
        target_link_options(videowallpaper PRIVATE -municode)   # wWinMain entry point
    endif()

    # Put libvlc's runtime next to the exe when the SDK sits inside an unpacked VLC release.
    if(LIBVLC_SDK_DIR AND EXISTS "${LIBVLC_SDK_DIR}/../libvlc.dll")
        get_filename_component(VLC_RUNTIME_DIR "${LIBVLC_SDK_DIR}/.." ABSOLUTE)
        add_custom_command(TARGET videowallpaper POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VLC_RUNTIME_DIR}/libvlc.dll" "${VLC_RUNTIME_DIR}/libvlccore.dll" "$<TARGET_FILE_DIR:videowallpaper>"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${VLC_RUNTIME_DIR}/plugins" "$<TARGET_FILE_DIR:videowallpaper>/plugins"
            COMMENT "Copying libvlc runtime")
    endif()

# ---- Linux: Wayland layer-shell + StatusNotifierItem -------------------------------
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client wayland-egl egl glesv2)
    pkg_check_modules(SDBUS REQUIRED IMPORTED_TARGET sdbus-c++)
    pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
    if(NOT WAYLAND_SCANNER)
        find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
    endif()

    # Generate client code for the vendored protocol XMLs.
    set(PROTOCOL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/wayland/protocols")
    set(PROTOCOL_OUT "${CMAKE_CURRENT_BINARY_DIR}/protocols")
    file(MAKE_DIRECTORY "${PROTOCOL_OUT}")
    set(PROTOCOLS
        xdg-shell
        viewporter
        xdg-output-unstable-v1
        fractional-scale-v1
        wlr-layer-shell-unstable-v1
        wlr-foreign-toplevel-management-unstable-v1
    )
    set(PROTOCOL_SOURCES)
    foreach(proto IN LISTS PROTOCOLS)
        set(xml "${PROTOCOL_DIR}/${proto}.xml")
        set(header "${PROTOCOL_OUT}/${proto}-client-protocol.h")
        set(source "${PROTOCOL_OUT}/${proto}-protocol.c")
        add_custom_command(OUTPUT "${header}"
            COMMAND "${WAYLAND_SCANNER}" client-header "${xml}" "${header}"
            DEPENDS "${xml}" VERBATIM)
        add_custom_command(OUTPUT "${source}"
            COMMAND "${WAYLAND_SCANNER}" private-code "${xml}" "${source}"
            DEPENDS "${xml}" VERBATIM)
        list(APPEND PROTOCOL_SOURCES "${header}" "${source}")
    endforeach()

    # Embed the KWin helper script so the binary is self-contained.
    file(READ src/wayland/kwin/coverage.js KWIN_COVERAGE_JS)
    file(WRITE "${PROTOCOL_OUT}/kwin_coverage_script.h"
        "#pragma once\nstatic const char* KWIN_COVERAGE_SCRIPT = R\"JS(\n${KWIN_COVERAGE_JS})JS\";\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS src/wayland/kwin/coverage.js)

    add_executable(videowallpaper
        src/wayland/main.cpp
        src/wayland/KWinBridge.cpp
        src/wayland/EventLoop.cpp
        src/wayland/GlRenderer.cpp
        src/wayland/LayerSurface.cpp
        src/wayland/Session.cpp
        src/wayland/Tray.cpp
        src/wayland/WaylandDisplay.cpp
        ${PROTOCOL_SOURCES}
    )
    target_include_directories(videowallpaper PRIVATE src/wayland "${PROTOCOL_OUT}")
    target_link_libraries(videowallpaper PRIVATE vw_core PkgConfig::WAYLAND PkgConfig::SDBUS)

    include(GNUInstallDirs)
    install(TARGETS videowallpaper RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES data/videowallpaper.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
endif()
