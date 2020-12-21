add_executable(bzcat)
target_sources(bzcat PRIVATE ${CMAKE_CURRENT_LIST_DIR}/bzcat.cpp)
target_link_libraries(bzcat PRIVATE indexed_bzip2)

add_executable(blockfinder)
target_sources(blockfinder PRIVATE ${CMAKE_CURRENT_LIST_DIR}/blockfinder.cpp)
target_link_libraries(blockfinder PRIVATE indexed_bzip2_parallel)

add_executable(bitpatternstats)
target_sources(bitpatternstats PRIVATE ${CMAKE_CURRENT_LIST_DIR}/bitpatternstats.cpp)
target_link_libraries(bitpatternstats PRIVATE cxxopts indexed_bzip2_parallel)
