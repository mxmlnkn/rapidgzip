cmake_minimum_required(VERSION 3.17)
# FetchContent was added in CMake 3.11.
# FetchContent_MakeAvailable was added in CMake 3.14.
# librapidarchive / rapidgzip requires CMake 3.17.

project("decompress" CXX)

include(FetchContent)

FetchContent_Declare(
    librapidarchive
    # Either use the git repository or a local path, which could be a git submodule.
    # GIT_REPOSITORY "https://github.com/mxmlnkn/indexed_bzip2"
    # GIT_TAG        "master"
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

set(LIBRAPIDARCHIVE_BUILD_TESTS OFF)
set(LIBRAPIDARCHIVE_BUILD_TOOLS OFF)

FetchContent_MakeAvailable(librapidarchive)

get_property(allTargets DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY BUILDSYSTEM_TARGETS)
message(STATUS "Available targets in current directory: ${allTargets}")

get_property(allTargets GLOBAL PROPERTY TARGETS)
message(STATUS "All available global targets: ${allTargets}")


add_executable(decompress main.cpp)
target_link_libraries(decompress PRIVATE rapidgzip::rapidgzip)
