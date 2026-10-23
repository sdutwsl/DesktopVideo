# Finds libmpv and provides the imported target Mpv::Mpv.
#
# Linux: pkg-config (libmpv-dev / mpv-devel / nixpkgs mpv-unwrapped).
# Windows (MinGW): point MPV_SDK_DIR at an unpacked libmpv dev archive
#   (https://sourceforge.net/projects/mpv-player-windows/files/libmpv/ -> mpv-dev-x86_64-*.7z,
#   containing include/, libmpv.dll.a and libmpv-2.dll), or set the MPV_SDK environment variable.

if(TARGET Mpv::Mpv)
    return()
endif()

set(MPV_SDK_DIR "$ENV{MPV_SDK}" CACHE PATH "libmpv SDK directory containing include/ and libmpv.dll.a")

# Only ask pkg-config for paths: mpv.pc lists dozens of Requires.private (libass, ffmpeg,
# libplacebo, ...) that --cflags/--libs insist on finding, but we link libmpv dynamically.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND NOT MPV_SDK_DIR)
    pkg_get_variable(PC_MPV_INCLUDEDIR mpv includedir)
    pkg_get_variable(PC_MPV_LIBDIR mpv libdir)
    execute_process(COMMAND "${PKG_CONFIG_EXECUTABLE}" --modversion mpv
        OUTPUT_VARIABLE PC_MPV_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()

find_path(Mpv_INCLUDE_DIR mpv/client.h
    HINTS ${PC_MPV_INCLUDEDIR} "${MPV_SDK_DIR}/include")
find_library(Mpv_LIBRARY NAMES mpv libmpv.dll.a
    HINTS ${PC_MPV_LIBDIR} "${MPV_SDK_DIR}")
if(MPV_SDK_DIR)
    find_file(Mpv_DLL NAMES libmpv-2.dll mpv-2.dll HINTS "${MPV_SDK_DIR}" NO_DEFAULT_PATH)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Mpv
    REQUIRED_VARS Mpv_LIBRARY Mpv_INCLUDE_DIR
    VERSION_VAR PC_MPV_VERSION)

if(Mpv_FOUND)
    add_library(Mpv::Mpv UNKNOWN IMPORTED)
    set_target_properties(Mpv::Mpv PROPERTIES
        IMPORTED_LOCATION "${Mpv_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Mpv_INCLUDE_DIR}")
endif()
