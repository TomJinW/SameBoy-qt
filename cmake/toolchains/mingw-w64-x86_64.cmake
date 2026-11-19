# Cross-compile for Windows x64 with mingw-w64 (POSIX threads, SEH), matching Qt's MinGW kit.
# Debian names the POSIX-threads compilers *-posix; Homebrew's mingw-w64 is POSIX-threads under the plain names.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
find_program(mingw_gcc NAMES x86_64-w64-mingw32-gcc-posix x86_64-w64-mingw32-gcc REQUIRED)
find_program(mingw_gxx NAMES x86_64-w64-mingw32-g++-posix x86_64-w64-mingw32-g++ REQUIRED)
set(CMAKE_C_COMPILER ${mingw_gcc})
set(CMAKE_CXX_COMPILER ${mingw_gxx})
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
