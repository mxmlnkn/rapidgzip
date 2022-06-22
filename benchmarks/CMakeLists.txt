add_executable(benchmarkHuffmanCoding)
target_sources(benchmarkHuffmanCoding PRIVATE ${CMAKE_CURRENT_LIST_DIR}/HuffmanCoding.cpp)
target_link_libraries(benchmarkHuffmanCoding PRIVATE libhuffman core)

add_executable(benchmarkBitReader)
target_sources(benchmarkBitReader PRIVATE ${CMAKE_CURRENT_LIST_DIR}/BitReader.cpp)
target_link_libraries(benchmarkBitReader PRIVATE core)

add_executable(benchmarkBitStringFinder)
target_sources(benchmarkBitStringFinder PRIVATE ${CMAKE_CURRENT_LIST_DIR}/BitStringFinder.cpp)
target_link_libraries(benchmarkBitStringFinder PRIVATE indexed_bzip2_parallel)

add_executable(benchmarkMarkerReplacement)
target_sources(benchmarkMarkerReplacement PRIVATE ${CMAKE_CURRENT_LIST_DIR}/MarkerReplacement.cpp)
target_link_libraries(benchmarkMarkerReplacement PRIVATE core)

add_executable(benchmarkPigzBlockFinder)
target_sources(benchmarkPigzBlockFinder PRIVATE ${CMAKE_CURRENT_LIST_DIR}/PigzBlockFinder.cpp)
target_link_libraries(benchmarkPigzBlockFinder PRIVATE libpragzip libblockfinder)


if(LibArchive_FOUND AND ZLIB_FOUND)

add_executable(benchmarkGzip)
target_sources(benchmarkGzip PRIVATE ${CMAKE_CURRENT_LIST_DIR}/Gzip.cpp)
target_link_libraries(benchmarkGzip PRIVATE ZLIB::ZLIB LibArchive::LibArchive libpragzip)

add_executable(benchmarkGzipBlockFinder)
target_sources(benchmarkGzipBlockFinder PRIVATE ${CMAKE_CURRENT_LIST_DIR}/GzipBlockFinder.cpp)
target_link_libraries(benchmarkGzipBlockFinder PRIVATE cxxopts libpragzip libblockfinder ZLIB::ZLIB)

endif()
