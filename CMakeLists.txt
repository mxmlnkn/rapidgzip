cmake_minimum_required(VERSION 3.10)

project("Indexed BZip2 Decoder" CXX)


add_compile_options(
    "$<$<CXX_COMPILER_ID:Clang>:-Wall;-Wextra;-Wshadow>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wall;-Wextra;-Wshadow;-Wsuggest-override>"
)


# This command should be in the source directory root because ctest expects
# to find a test file in the build directory root.
include(CTest)

add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --verbose)

add_custom_target(check-memcheck
    COMMAND ${CMAKE_CTEST_COMMAND} --force-new-ctest-process --test-action memcheck
    COMMAND cat "${CMAKE_BINARY_DIR}/Testing/Temporary/MemoryChecker.*.log"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_library(cxxopts INTERFACE)
target_include_directories(cxxopts INTERFACE ${CMAKE_SOURCE_DIR}/external/cxxopts/include)
target_sources(cxxopts INTERFACE ${CMAKE_SOURCE_DIR}/external/cxxopts/include/cxxopts.hpp)

add_subdirectory(indexed_bzip2)
add_subdirectory(tools)
add_subdirectory(tests)
