# Host-side tests for the parts of ps5/app that have no console code, built on
# the PC against stable-diffusion.cpp's library (CPU only):
#
#   cmake -S ps5/app/tests -B build/app-tests -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/app-tests && ctest --test-dir build/app-tests
#
# SD_DIR may point at a patched copy of stable-diffusion.cpp (its ggml
# submodule checked out); the default is the submodule.
cmake_minimum_required(VERSION 3.20)
project(ps5sd_app_tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/stable-diffusion.cpp" CACHE PATH
    "stable-diffusion.cpp source tree")
set(SD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SD_WEBP OFF CACHE BOOL "" FORCE)
set(SD_WEBM OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(GGML_METAL OFF CACHE BOOL "" FORCE)
set(GGML_BLAS OFF CACHE BOOL "" FORCE)
add_subdirectory("${SD_DIR}" sd EXCLUDE_FROM_ALL)

enable_testing()
add_executable(test_gen_args test_gen_args.cpp ../gen_args.cpp)
target_include_directories(test_gen_args PRIVATE ..)
target_link_libraries(test_gen_args PRIVATE stable-diffusion)
add_test(NAME gen_args COMMAND test_gen_args)
