# Adapted from PS5LM (github.com/cobanov/PS5LM, GPL-3.0)
#
# CMake toolchain for PS5SD's native app: stable-diffusion.cpp and ggml-vulkan
# as static libraries for a title that links RADV (scripts/build-app.sh does
# the link). The compilers are PS5_Vulkan's title wrapper
# (tooling/prospero-clang18) over the PS5_PayloadSDK fork; PS5_PAYLOAD_SDK must
# name that SDK and PS5_VULKAN_ROOT the PS5_Vulkan checkout.

set(CMAKE_SYSTEM_NAME FreeBSD)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   "${CMAKE_CURRENT_LIST_DIR}/ps5cc")
set(CMAKE_CXX_COMPILER "${CMAKE_CURRENT_LIST_DIR}/ps5cxx")
set(CMAKE_AR     "$ENV{PS5_PAYLOAD_SDK}/bin/llvm-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "$ENV{PS5_PAYLOAD_SDK}/bin/llvm-ranlib" CACHE FILEPATH "")

# Only static libraries come out of this build; nothing is linked by CMake.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH "$ENV{PS5_PAYLOAD_SDK}/target")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
