cmake_minimum_required(VERSION 3.16)

project("Indexed BZip2 Decoder" CXX)


option(CODE_COVERAGE "Enable coverage reporting" OFF)


if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Chile the build type: Debug, Release, RelWithDebInfo" FORCE)
endif()

add_compile_options(
    "$<$<CXX_COMPILER_ID:Clang>:-Wall;-Wextra;-Wshadow;-Werror=return-type>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wall;-Wextra;-Wshadow;-Wunused;-Wsuggest-override;-Werror=return-type>"
    # The default limit is ~33 M (1<<25) and 99 M seem to be enough currently to compile.
    "$<$<CXX_COMPILER_ID:GNU>:-fconstexpr-ops-limit=99000100>"
    "$<$<CXX_COMPILER_ID:Clang>:-fconstexpr-steps=99000100>"
    "$<$<AND:$<CXX_COMPILER_ID:Clang,GNU>,$<BOOL:${CODE_COVERAGE}>>:-O0;-g;--coverage>"
)

add_link_options(
    "$<$<AND:$<CXX_COMPILER_ID:Clang,GNU>,$<BOOL:${CODE_COVERAGE}>>:--coverage>"
)

if(CODE_COVERAGE)
    add_definitions(-DSHORT_TESTS)
endif()


# This command should be in the source directory root because ctest expects
# to find a test file in the build directory root.
include(CTest)


find_package(ZLIB)
find_package(LibArchive)


add_library(cxxopts INTERFACE)
target_include_directories(cxxopts SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/external/cxxopts/include)
target_sources(cxxopts INTERFACE ${CMAKE_SOURCE_DIR}/external/cxxopts/include/cxxopts.hpp)


find_package(Threads REQUIRED)


add_subdirectory(benchmarks)
add_subdirectory(core)
add_subdirectory(indexed_bzip2)
add_subdirectory(pragzip)
add_subdirectory(tools)
add_subdirectory(tests)


# Add convenience custom targets

add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --extra-verbose)

add_custom_target(check-memcheck
    COMMAND ${CMAKE_CTEST_COMMAND} --extra-verbose --force-new-ctest-process --test-action memcheck
    COMMAND cat "${CMAKE_BINARY_DIR}/Testing/Temporary/MemoryChecker.*.log"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_dependencies(check all_tests)
add_dependencies(check-memcheck all_tests)


# Add beautify target

find_package(Git QUIET)

if(GIT_FOUND)
    add_custom_target(beautify-all
        COMMAND git ls-tree -r --name-only HEAD > ${CMAKE_BINARY_DIR}/.beautify.lst
        COMMAND sed -i -E "/[.](h|c)(pp)?$/!d; /external\\//d; /indexed_bzip2.cpp/d;" ${CMAKE_BINARY_DIR}/.beautify.lst
        COMMAND uncrustify -c uncrustify.cfg -F ${CMAKE_BINARY_DIR}/.beautify.lst --no-backup
        VERBATIM
        # git ls-tree needs to be executed in git root
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()
