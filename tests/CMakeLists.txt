add_library(all_tests INTERFACE)

add_executable(testFileReader)
target_sources(testFileReader PRIVATE ${CMAKE_CURRENT_LIST_DIR}/testFileReader.cpp)
target_link_libraries(testFileReader PRIVATE indexed_bzip2_parallel)
add_test(NAME Test-File-Reader COMMAND testFileReader)
add_dependencies(all_tests testFileReader)

add_executable(testBitReader)
target_sources(testBitReader PRIVATE ${CMAKE_CURRENT_LIST_DIR}/testBitReader.cpp)
target_link_libraries(testBitReader PRIVATE indexed_bzip2_parallel)
add_test(NAME Test-Bit-Reader COMMAND testBitReader)
add_dependencies(all_tests testBitReader)

add_executable(testBitStringFinder)
target_sources(testBitStringFinder PRIVATE ${CMAKE_CURRENT_LIST_DIR}/testBitStringFinder.cpp)
target_link_libraries(testBitStringFinder PRIVATE indexed_bzip2_parallel)
add_test(NAME Test-Bit-String-Finder COMMAND testBitStringFinder)
add_dependencies(all_tests testBitStringFinder)

add_executable(testParallelBitStringFinder)
target_sources(testParallelBitStringFinder PRIVATE ${CMAKE_CURRENT_LIST_DIR}/testParallelBitStringFinder.cpp)
target_link_libraries(testParallelBitStringFinder PRIVATE indexed_bzip2_parallel)
add_test(NAME Test-Parallel-Bit-String-Finder COMMAND testParallelBitStringFinder)
add_dependencies(all_tests testParallelBitStringFinder)

add_executable(testThreadPool)
target_sources(testThreadPool PRIVATE ${CMAKE_CURRENT_LIST_DIR}/testThreadPool.cpp)
target_link_libraries(testThreadPool PRIVATE indexed_bzip2_parallel)
add_test(NAME Test-Thread-Pool COMMAND testThreadPool)
add_dependencies(all_tests testThreadPool)

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

add_executable(testPrefetcher)
target_sources(testPrefetcher PRIVATE ${CMAKE_CURRENT_LIST_DIR}/testPrefetcher.cpp)
target_link_libraries(testPrefetcher PRIVATE indexed_bzip2_parallel)
add_test(NAME Test-Prefetcher COMMAND testPrefetcher)
add_dependencies(all_tests testPrefetcher)

add_executable(testGzipIndexFormat)
target_sources(testGzipIndexFormat PRIVATE ${CMAKE_CURRENT_LIST_DIR}/testGzipIndexFormat.cpp)
target_link_libraries(testGzipIndexFormat PRIVATE libpragzip)
add_test(NAME Test-GzipIndexFormat COMMAND testGzipIndexFormat)
add_dependencies(all_tests testGzipIndexFormat)

add_executable(testHuffmanCoding)
target_sources(testHuffmanCoding PRIVATE ${CMAKE_CURRENT_LIST_DIR}/testHuffmanCoding.cpp)
target_link_libraries(testHuffmanCoding PRIVATE libhuffman)
add_test(NAME Test-HuffmanCoding COMMAND testHuffmanCoding)
add_dependencies(all_tests testHuffmanCoding)


add_executable(benchmarkHuffmanCoding)
target_sources(benchmarkHuffmanCoding PRIVATE ${CMAKE_CURRENT_LIST_DIR}/benchmarkHuffmanCoding.cpp)
target_link_libraries(benchmarkHuffmanCoding PRIVATE libhuffman core)

add_executable(benchmarkBitStringFinder)
target_sources(benchmarkBitStringFinder PRIVATE ${CMAKE_CURRENT_LIST_DIR}/benchmarkBitStringFinder.cpp)
target_link_libraries(benchmarkBitStringFinder PRIVATE indexed_bzip2_parallel)


add_executable(benchmarkMarkerReplacement)
target_sources(benchmarkMarkerReplacement PRIVATE ${CMAKE_CURRENT_LIST_DIR}/benchmarkMarkerReplacement.cpp)
target_link_libraries(benchmarkMarkerReplacement PRIVATE core)
