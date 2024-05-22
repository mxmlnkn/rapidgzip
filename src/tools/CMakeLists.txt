add_executable(ibzip2)
target_sources(ibzip2 PRIVATE ${CMAKE_CURRENT_LIST_DIR}/ibzip2.cpp)
target_link_libraries(ibzip2 PRIVATE cxxopts indexed_bzip2_parallel)

add_executable(bitpatternstats)
target_sources(bitpatternstats PRIVATE ${CMAKE_CURRENT_LIST_DIR}/bitpatternstats.cpp)
target_link_libraries(bitpatternstats PRIVATE cxxopts indexed_bzip2_parallel)

add_executable(rapidgzip)
target_sources(rapidgzip PRIVATE ${CMAKE_CURRENT_LIST_DIR}/rapidgzip.cpp)
target_link_libraries(rapidgzip PRIVATE cxxopts librapidgzip rpmalloc)
if(WITH_RPMALLOC)
    target_compile_definitions(rapidgzip PRIVATE WITH_RPMALLOC)
endif()

if(RUN_CLANG_TIDY)
    set_target_properties(ibzip2 PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")
    set_target_properties(rapidgzip PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")
endif()
