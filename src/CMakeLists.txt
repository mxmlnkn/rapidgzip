add_library(cxxopts INTERFACE)
target_include_directories(cxxopts SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/external/cxxopts/include)
target_sources(cxxopts INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/external/cxxopts/include/cxxopts.hpp)


add_subdirectory(benchmarks)
add_subdirectory(core)
add_subdirectory(indexed_bzip2)
add_subdirectory(pragzip)
add_subdirectory(tools)
add_subdirectory(tests)
