# Cross-compile the Windows x64 build from Linux with GCC mingw-w64 (`nix develop .#mingw`):
#   cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x86_64.cmake \
#         -DMPV_SDK_DIR=/path/to/mpv-dev-x86_64
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(triple x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${triple}-gcc)
set(CMAKE_CXX_COMPILER ${triple}-g++)
set(CMAKE_RC_COMPILER ${triple}-windres)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# Static runtime so the exe does not need libstdc++/libgcc DLLs next to it.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
