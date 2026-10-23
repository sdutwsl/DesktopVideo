# Finds libvlc and provides the imported target LibVLC::LibVLC.
#
# Linux: pkg-config (libvlc-dev / vlc-devel / nixpkgs libvlc).
# Windows: point LIBVLC_SDK_DIR at the "sdk" folder of a VLC release
#          (https://get.videolan.org/vlc/ -> vlc-<ver>-win64.7z -> sdk/),
#          or set the VLC_SDK environment variable.

if(TARGET LibVLC::LibVLC)
    return()
endif()

set(LIBVLC_SDK_DIR "$ENV{VLC_SDK}" CACHE PATH "VLC SDK directory containing include/ and lib/")

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND NOT LIBVLC_SDK_DIR)
    pkg_check_modules(PC_LIBVLC QUIET libvlc)
endif()

find_path(LibVLC_INCLUDE_DIR vlc/vlc.h
    HINTS ${PC_LIBVLC_INCLUDEDIR} ${PC_LIBVLC_INCLUDE_DIRS} "${LIBVLC_SDK_DIR}/include")
find_library(LibVLC_LIBRARY NAMES vlc libvlc
    HINTS ${PC_LIBVLC_LIBDIR} ${PC_LIBVLC_LIBRARY_DIRS} "${LIBVLC_SDK_DIR}/lib")
find_library(LibVLCCore_LIBRARY NAMES vlccore libvlccore
    HINTS ${PC_LIBVLC_LIBDIR} ${PC_LIBVLC_LIBRARY_DIRS} "${LIBVLC_SDK_DIR}/lib")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibVLC
    REQUIRED_VARS LibVLC_LIBRARY LibVLC_INCLUDE_DIR
    VERSION_VAR PC_LIBVLC_VERSION)

if(LibVLC_FOUND)
    add_library(LibVLC::LibVLC UNKNOWN IMPORTED)
    set_target_properties(LibVLC::LibVLC PROPERTIES
        IMPORTED_LOCATION "${LibVLC_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LibVLC_INCLUDE_DIR}")
    if(LibVLCCore_LIBRARY)
        set_property(TARGET LibVLC::LibVLC APPEND PROPERTY INTERFACE_LINK_LIBRARIES "${LibVLCCore_LIBRARY}")
    endif()
endif()
