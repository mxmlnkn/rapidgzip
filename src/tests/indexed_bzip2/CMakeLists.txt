# Calling bzip2.exe via std::system does not work for some reason on the Windows Github CI runners.
# It quits with: bzip2: Can't open input file 'decoded': Invalid argument.
if(NOT WIN32)
add_executable(testParallelBZ2Reader)
target_sources(testParallelBZ2Reader PRIVATE ${CMAKE_CURRENT_LIST_DIR}/testParallelBZ2Reader.cpp)
target_link_libraries(testParallelBZ2Reader PRIVATE indexed_bzip2_parallel)
add_test(NAME Test-Parallel-BZ2-Reader COMMAND testParallelBZ2Reader)
add_dependencies(all_tests testParallelBZ2Reader)

add_executable(testBZ2Reader)
target_sources(testBZ2Reader PRIVATE ${CMAKE_CURRENT_LIST_DIR}/testBZ2Reader.cpp)
target_link_libraries(testBZ2Reader PRIVATE indexed_bzip2_parallel)
add_test(NAME Test-BZ2-Reader COMMAND testBZ2Reader)
add_dependencies(all_tests testBZ2Reader)
endif()

add_executable(testRunAB)
target_sources(testRunAB PRIVATE ${CMAKE_CURRENT_LIST_DIR}/testRunAB.cpp)
target_link_libraries(testRunAB PRIVATE core)
add_test(NAME Test-Run-AB COMMAND testRunAB)
add_dependencies(all_tests testRunAB)
