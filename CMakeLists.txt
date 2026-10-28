cmake_minimum_required(VERSION 3.20)
# Our own code needs macOS 13 (SMAppService); `ninja dmg` raises the app's minimum to
# whatever the bundled Homebrew libraries were built for.
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "Minimum macOS version")
endif()
# The single version number: the Nix package reads it from here, and release tags must be v<VERSION>.
project(Muralop VERSION 3.0.0 LANGUAGES C CXX)
add_compile_definitions("MURALOP_VERSION=\"${PROJECT_VERSION}\"")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CTest)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    # The libmpv SDK for Windows only ships a MinGW import library (libmpv.dll.a).
    message(FATAL_ERROR "Build the Windows version with MinGW-w64 (see README); MSVC is not supported.")
endif()

find_package(Mpv REQUIRED)

# ---- shared core -------------------------------------------------------------
add_library(muralop_core STATIC
    src/core/AppControl.cpp
    src/core/Config.cpp
    src/core/Menu.cpp
    src/core/MpvPlayer.cpp
    src/core/Wallpaper.cpp
)
target_include_directories(muralop_core PUBLIC src)
target_link_libraries(muralop_core PUBLIC Mpv::Mpv)
target_compile_options(muralop_core PUBLIC -Wall -Wextra)
set_target_properties(muralop_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(MURALOP_BUILD_NODE_PLUGIN "Build the Electron Node-API playback plugin" OFF)

if(BUILD_TESTING AND NOT APPLE)
    add_executable(muralop_control_test
        tests/control_test.cpp tests/FakeMpv.cpp
        src/core/Config.cpp src/core/Wallpaper.cpp src/core/AppControl.cpp)
    target_include_directories(muralop_control_test PRIVATE src)
    target_compile_options(muralop_control_test PRIVATE -Wall -Wextra -UNDEBUG)
    if(WIN32)
        target_sources(muralop_control_test PRIVATE src/win/ControlPipe.cpp)
        target_compile_definitions(muralop_control_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_link_libraries(muralop_control_test PRIVATE user32 advapi32)
    endif()
    add_test(NAME controller COMMAND muralop_control_test)
endif()

if(BUILD_TESTING)
    add_executable(muralop_mpv_options_test tests/mpv_options_test.cpp)
    target_link_libraries(muralop_mpv_options_test PRIVATE muralop_core)
    target_compile_options(muralop_mpv_options_test PRIVATE -UNDEBUG)
    add_test(NAME mpv-options COMMAND muralop_mpv_options_test)
endif()

# ---- Windows: WorkerW + Shell_NotifyIcon ---------------------------------------
if(WIN32)
    add_executable(muralop WIN32
        src/win/main.cpp
        src/win/DesktopSurface.cpp
        src/win/ControlPipe.cpp
        src/win/Muralop.rc
    )
    # Version.h feeds the version resource in Muralop.rc.
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Version.h" CONTENT
"#define MURALOP_VERSION_RC  @PROJECT_VERSION_MAJOR@,@PROJECT_VERSION_MINOR@,@PROJECT_VERSION_PATCH@,0
#define MURALOP_VERSION_STR \"@PROJECT_VERSION@\"
" @ONLY)
    target_include_directories(muralop PRIVATE src/win "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(muralop PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(muralop PRIVATE muralop_core user32 gdi32 shell32 comdlg32 wtsapi32 ole32 advapi32)
    target_link_options(muralop PRIVATE -municode)   # wWinMain entry point

    # libmpv is a single DLL; put it next to the exe.
    if(Mpv_DLL)
        add_custom_command(TARGET muralop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${Mpv_DLL}" "$<TARGET_FILE_DIR:muralop>"
            COMMENT "Copying libmpv runtime")

        # Release packages: `ninja portable installer`.
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
            set(MURALOP_WIN_ARCH arm64)
        else()
            set(MURALOP_WIN_ARCH x64)
        endif()
        set(MURALOP_PACKAGE "${CMAKE_CURRENT_BINARY_DIR}/muralop-v${PROJECT_VERSION}-windows-${MURALOP_WIN_ARCH}")

        # A zip with one Muralop folder; the config.ini in it switches on portable mode.
        set(PORTABLE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/portable")
        add_custom_target(portable
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${PORTABLE_ROOT}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PORTABLE_ROOT}/Muralop"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:muralop>" "${Mpv_DLL}" "${PORTABLE_ROOT}/Muralop"
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/src/win/portable-config.ini"
                    "${PORTABLE_ROOT}/Muralop/config.ini"
            COMMAND ${CMAKE_COMMAND} -E chdir "${PORTABLE_ROOT}"
                    ${CMAKE_COMMAND} -E tar cf "${MURALOP_PACKAGE}-portable.zip" --format=zip Muralop
            DEPENDS muralop
            COMMENT "Packing ${MURALOP_PACKAGE}-portable.zip"
            VERBATIM)

        find_program(MAKENSIS makensis)
        if(MAKENSIS)
            add_custom_target(installer
                COMMAND "${MAKENSIS}" -V2 -WX -INPUTCHARSET UTF8
                        "-DVERSION=${PROJECT_VERSION}" "-DARCH=${MURALOP_WIN_ARCH}"
                        "-DEXE=$<TARGET_FILE:muralop>" "-DMPV_DLL=${Mpv_DLL}"
                        "-DOUTFILE=${MURALOP_PACKAGE}-setup.exe"
                        "${CMAKE_CURRENT_SOURCE_DIR}/src/win/installer.nsi"
                DEPENDS muralop
                COMMENT "Building ${MURALOP_PACKAGE}-setup.exe"
                VERBATIM)
        endif()
    endif()

# ---- macOS: desktop-level windows + NSStatusItem -------------------------------------
elseif(APPLE)
    enable_language(OBJCXX)
    add_executable(muralop MACOSX_BUNDLE
        src/mac/main.mm
        src/mac/DesktopSurface.mm
        src/mac/Muralop.icns
    )
    set_source_files_properties(src/mac/Muralop.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    configure_file(src/mac/Info.plist.in "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)
    set_target_properties(muralop PROPERTIES
        OUTPUT_NAME Muralop
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
    target_include_directories(muralop PRIVATE src/mac)
    target_compile_options(muralop PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_compile_definitions(muralop PRIVATE GL_SILENCE_DEPRECATION)
    target_link_libraries(muralop PRIVATE muralop_core
        "-framework Cocoa" "-framework QuartzCore" "-framework OpenGL"
        "-framework ServiceManagement" "-framework UniformTypeIdentifiers")

    # Release package: `ninja dmg` (needs dylibbundler, e.g. from Homebrew).
    get_filename_component(MURALOP_MPV_LIBDIR "${Mpv_LIBRARY}" DIRECTORY)
    set(MURALOP_DMG "${CMAKE_CURRENT_BINARY_DIR}/muralop-v${PROJECT_VERSION}-macos-${CMAKE_SYSTEM_PROCESSOR}.dmg")
    add_custom_target(dmg
        COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/src/mac/make-dmg.sh"
                "$<TARGET_BUNDLE_DIR:muralop>" "${MURALOP_DMG}" "${MURALOP_MPV_LIBDIR}"
        DEPENDS muralop
        COMMENT "Packing ${MURALOP_DMG}"
        VERBATIM)

# ---- Linux: Wayland layer-shell + StatusNotifierItem -------------------------------
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client wayland-egl egl glesv2)
    pkg_check_modules(SDBUS REQUIRED IMPORTED_TARGET sdbus-c++>=2.0)
    if(BUILD_TESTING)
        target_sources(muralop_control_test PRIVATE src/wayland/Session.cpp)
        target_link_libraries(muralop_control_test PRIVATE PkgConfig::SDBUS)
    endif()
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

    add_executable(muralop
        src/wayland/main.cpp
        src/wayland/EventLoop.cpp
        src/wayland/LayerSurface.cpp
        src/wayland/Mpris.cpp
        src/wayland/Session.cpp
        src/wayland/Tray.cpp
        src/wayland/WaylandDisplay.cpp
        ${PROTOCOL_SOURCES}
    )
    target_include_directories(muralop PRIVATE src/wayland "${PROTOCOL_OUT}")
    target_link_libraries(muralop PRIVATE muralop_core PkgConfig::WAYLAND PkgConfig::SDBUS)

    include(GNUInstallDirs)
    install(TARGETS muralop RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES data/muralop.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

    # `cpack -G DEB` / `cpack -G RPM` build the packages attached to releases; the
    # runtime dependencies come from dpkg-shlibdeps and rpmbuild's automatic requires.
    set(CPACK_PACKAGE_NAME muralop)
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Video desktop wallpaper for wlr-layer-shell Wayland compositors")
    set(CPACK_PACKAGE_DESCRIPTION "Plays a video as the desktop wallpaper on Wayland compositors with wlr-layer-shell (KDE Plasma, Sway, Hyprland, ...), with a tray icon to pick the video.")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/sdutwsl/Muralop")
    set(CPACK_PACKAGE_CONTACT "sdutwsl")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION x11)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_CONFLICTS videowallpaper)
    set(CPACK_DEBIAN_PACKAGE_REPLACES videowallpaper)
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")   # RPM would use CPack's generic text otherwise
    set(CPACK_RPM_PACKAGE_OBSOLETES "videowallpaper < ${PROJECT_VERSION}")
    set(CPACK_RPM_PACKAGE_RELEASE_DIST ON)
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION /usr/share/applications)
    include(CPack)
endif()

# ---- Electron playback plugin (stable Node-API, no V8/Electron ABI dependency) --
if(MURALOP_BUILD_NODE_PLUGIN)
    if(APPLE)
        message(FATAL_ERROR "The Electron playback plugin currently supports Linux/Windows only")
    endif()
    find_path(NODE_INCLUDE_DIR node_api.h PATH_SUFFIXES node REQUIRED)
    find_package(Threads REQUIRED)
    add_library(muralop_player MODULE src/node/PlayerAddon.cpp)
    target_include_directories(muralop_player PRIVATE "${NODE_INCLUDE_DIR}")
    target_compile_definitions(muralop_player PRIVATE NAPI_VERSION=8)
    target_link_libraries(muralop_player PRIVATE muralop_core Threads::Threads)
    set_target_properties(muralop_player PROPERTIES
        PREFIX "" SUFFIX ".node" OUTPUT_NAME "muralop"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/desktop"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/desktop")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/desktop/package.json" MURALOP_DESKTOP_METADATA)
    string(JSON MURALOP_DESKTOP_METADATA SET "${MURALOP_DESKTOP_METADATA}" version "\"${PROJECT_VERSION}\"")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/desktop")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/desktop/package.json" "${MURALOP_DESKTOP_METADATA}\n")
    if(WIN32)
        target_sources(muralop_player PRIVATE src/win/DesktopSurface.cpp)
        target_compile_definitions(muralop_player PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_link_libraries(muralop_player PRIVATE user32 gdi32 shell32 wtsapi32)
        target_link_options(muralop_player PRIVATE -static -static-libgcc -static-libstdc++)
        if(Mpv_DLL)
            add_custom_command(TARGET muralop_player POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${Mpv_DLL}" "$<TARGET_FILE_DIR:muralop_player>"
                COMMENT "Copying libmpv next to the Node playback plugin")
        endif()
    else()
        target_sources(muralop_player PRIVATE
            src/wayland/EventLoop.cpp src/wayland/LayerSurface.cpp
            src/wayland/Mpris.cpp src/wayland/Session.cpp src/wayland/WaylandDisplay.cpp ${PROTOCOL_SOURCES})
        target_include_directories(muralop_player PRIVATE src/wayland "${PROTOCOL_OUT}")
        target_link_libraries(muralop_player PRIVATE PkgConfig::WAYLAND PkgConfig::SDBUS)
        install(TARGETS muralop_player LIBRARY DESTINATION "${CMAKE_INSTALL_DATADIR}/muralop/desktop")
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/desktop/package.json"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/muralop/desktop")
    endif()
endif()
