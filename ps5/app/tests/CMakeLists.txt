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

add_executable(test_models test_models.cpp ../models.cpp ../net_family.cpp)
target_include_directories(test_models PRIVATE .. "${SD_DIR}/thirdparty")
add_test(NAME models COMMAND test_models)

# The network a model or LoRA is for, from its tensors (net_family.hpp).
add_executable(test_net_family test_net_family.cpp ../net_family.cpp)
target_include_directories(test_net_family PRIVATE .. "${SD_DIR}/thirdparty")
add_test(NAME net_family COMMAND test_net_family)

# Will a run fit in memory (mem_fit.hpp).
add_executable(test_mem_fit test_mem_fit.cpp ../mem_fit.cpp)
target_include_directories(test_mem_fit PRIVATE .. "${SD_DIR}/thirdparty")
add_test(NAME mem_fit COMMAND test_mem_fit)

# An exception in a thread's body is caught there (thread_guard.hpp).
find_package(Threads REQUIRED)
add_executable(test_thread_guard test_thread_guard.cpp)
target_include_directories(test_thread_guard PRIVATE ..)
target_link_libraries(test_thread_guard PRIVATE Threads::Threads)
add_test(NAME thread_guard COMMAND test_thread_guard)

# The fan payload's modes and steps (ps5/fan/fan_policy.h).
add_executable(test_fan_policy test_fan_policy.cpp)
target_include_directories(test_fan_policy PRIVATE ../../fan)
add_test(NAME fan_policy COMMAND test_fan_policy)

add_executable(test_sd_plan test_sd_plan.cpp ../sd_plan.cpp ../gen_args.cpp)
target_include_directories(test_sd_plan PRIVATE ..)
target_link_libraries(test_sd_plan PRIVATE stable-diffusion)
add_test(NAME sd_plan COMMAND test_sd_plan)

# AppStatus alone: no kit, no GL, no sd.cpp (what the no-display build compiles).
add_executable(test_app_status test_app_status.cpp ../app_status.cpp)
target_include_directories(test_app_status PRIVATE ..)
add_test(NAME app_status COMMAND test_app_status)

# The console's readings: ps5-exporter's metrics (a fixture from the console), the history, its JSON.
add_executable(test_console_stats test_console_stats.cpp ../console_stats.cpp ../app_status.cpp)
target_include_directories(test_console_stats PRIVATE .. "${SD_DIR}/thirdparty")
target_compile_definitions(test_console_stats PRIVATE PS5SD_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME console_stats COMMAND test_console_stats)

# The downloader's pure part: URLs, destinations, the catalog, the queue file.
add_executable(test_downloads test_downloads.cpp ../download_core.cpp)
target_include_directories(test_downloads PRIVATE .. "${SD_DIR}/thirdparty")
add_test(NAME downloads COMMAND test_downloads)

# The downloader against the real Hugging Face and Civitai (network; not run by
# ctest): -DPS5SD_NET_SMOKE=ON -DMBEDTLS_ROOT=<a host install of mbedTLS 3.6>.
option(PS5SD_NET_SMOKE "build smoke_downloader" OFF)
if(PS5SD_NET_SMOKE)
    set(MBEDTLS_ROOT "" CACHE PATH "mbedTLS 3.6 install prefix")
    find_package(Threads REQUIRED)
    add_executable(smoke_downloader smoke_downloader.cpp ../downloader.cpp ../https_client.cpp
                   ../download_core.cpp ../models.cpp ../net_family.cpp ../ftp_writer.cpp)
    target_include_directories(smoke_downloader PRIVATE .. "${SD_DIR}/thirdparty" "${MBEDTLS_ROOT}/include")
    target_link_libraries(smoke_downloader PRIVATE "${MBEDTLS_ROOT}/lib/libmbedtls.a"
                          "${MBEDTLS_ROOT}/lib/libmbedx509.a" "${MBEDTLS_ROOT}/lib/libmbedcrypto.a" Threads::Threads)
endif()

# The gallery's names and tags, and the images that are not kept (gallery_labels.hpp).
add_executable(test_gallery_labels test_gallery_labels.cpp ../gallery_labels.cpp)
target_include_directories(test_gallery_labels PRIVATE .. "${SD_DIR}/thirdparty")
add_test(NAME gallery_labels COMMAND test_gallery_labels)

# Tag completion from the user's tag files (prompt_tags.hpp).
add_executable(test_prompt_tags test_prompt_tags.cpp ../prompt_tags.cpp)
target_include_directories(test_prompt_tags PRIVATE .. "${SD_DIR}/thirdparty")
add_test(NAME prompt_tags COMMAND test_prompt_tags)
