cmake_minimum_required(VERSION 3.16)

project("Indexed BZip2 Decoder" CXX)


option(CODE_COVERAGE "Enable coverage reporting" OFF)

add_compile_options(
    "$<$<CXX_COMPILER_ID:Clang>:-Wall;-Wextra;-Wshadow;-Werror=return-type>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wall;-Wextra;-Wshadow;-Wunused;-Wsuggest-override;-Werror=return-type>"
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

add_library(cxxopts INTERFACE)
target_include_directories(cxxopts SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/external/cxxopts/include)
target_sources(cxxopts INTERFACE ${CMAKE_SOURCE_DIR}/external/cxxopts/include/cxxopts.hpp)


find_package(Threads REQUIRED)


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
