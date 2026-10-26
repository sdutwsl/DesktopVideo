# Cross-compile the Windows on ARM64 build from Linux with LLVM mingw-w64 (`nix develop .#mingw-arm64`):
#   cmake -S . -B build-win-arm64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-aarch64.cmake \
#         -DMPV_SDK_DIR=/path/to/mpv-dev-aarch64
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(triple aarch64-w64-mingw32)
set(CMAKE_C_COMPILER ${triple}-clang)
set(CMAKE_CXX_COMPILER ${triple}-clang++)
set(CMAKE_RC_COMPILER ${triple}-windres)
# llvm-windres runs a bare clang for #include <windows.h>; use the wrapped one, which knows
# where the mingw headers are. Naming a preprocessor replaces its default arguments too.
set(CMAKE_RC_FLAGS_INIT "--preprocessor=${triple}-clang --preprocessor-arg=-E --preprocessor-arg=-xc --preprocessor-arg=-DRC_INVOKED")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# Static runtime (libc++, compiler-rt, winpthreads) so only libmpv-2.dll ships next to the exe.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
