add_executable(ibzip2)
target_sources(ibzip2 PRIVATE ${CMAKE_CURRENT_LIST_DIR}/ibzip2.cpp)
target_link_libraries(ibzip2 PRIVATE cxxopts indexed_bzip2_parallel)

add_executable(bitpatternstats)
target_sources(bitpatternstats PRIVATE ${CMAKE_CURRENT_LIST_DIR}/bitpatternstats.cpp)
target_link_libraries(bitpatternstats PRIVATE cxxopts indexed_bzip2_parallel)

add_executable(pragzip)
target_sources(pragzip PRIVATE ${CMAKE_CURRENT_LIST_DIR}/pragzip.cpp)
target_link_libraries(pragzip PRIVATE cxxopts libpragzip rpmalloc)
target_compile_definitions(pragzip PRIVATE WITH_RPMALLOC)
