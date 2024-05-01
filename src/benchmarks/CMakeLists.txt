add_custom_target(run-benchmarks)


add_executable(benchmarkHuffmanCoding)
target_sources(benchmarkHuffmanCoding PRIVATE ${CMAKE_CURRENT_LIST_DIR}/benchmarkHuffmanCoding.cpp)
target_link_libraries(benchmarkHuffmanCoding PRIVATE libhuffman libhuffmangzip core)
add_custom_target(run-benchmarkHuffmanCoding
    COMMAND ./benchmarkHuffmanCoding 2>&1 | tee benchmarkHuffmanCoding.log
    VERBATIM
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
add_dependencies(run-benchmarks run-benchmarkHuffmanCoding)


add_executable(benchmarkBitReader)
target_sources(benchmarkBitReader PRIVATE ${CMAKE_CURRENT_LIST_DIR}/benchmarkBitReader.cpp)
target_link_libraries(benchmarkBitReader PRIVATE core)
add_custom_target(run-benchmarkBitReader
    COMMAND ./benchmarkBitReader 2>&1 | tee benchmarkBitReader.log
    VERBATIM
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
add_dependencies(run-benchmarks run-benchmarkBitReader)


add_executable(benchmarkBitStringFinder)
target_sources(benchmarkBitStringFinder PRIVATE ${CMAKE_CURRENT_LIST_DIR}/benchmarkBitStringFinder.cpp)
target_link_libraries(benchmarkBitStringFinder PRIVATE indexed_bzip2_parallel)
# TODO Still requires an input file
#add_custom_target(run-benchmarkBitStringFinder
#    COMMAND ./benchmarkBitStringFinder 2>&1 | tee benchmarkBitStringFinder.log
#    VERBATIM
#    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
#)
#add_dependencies(run-benchmarks run-benchmarkBitStringFinder)


add_executable(benchmarkMarkerReplacement)
target_sources(benchmarkMarkerReplacement PRIVATE ${CMAKE_CURRENT_LIST_DIR}/benchmarkMarkerReplacement.cpp)
target_link_libraries(benchmarkMarkerReplacement PRIVATE core)
add_custom_target(run-benchmarkMarkerReplacement
    COMMAND ./benchmarkMarkerReplacement 2>&1 | tee benchmarkMarkerReplacement.log
    VERBATIM
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
add_dependencies(run-benchmarks run-benchmarkMarkerReplacement)


add_executable(benchmarkPigzBlockFinder)
target_sources(benchmarkPigzBlockFinder PRIVATE ${CMAKE_CURRENT_LIST_DIR}/benchmarkPigzBlockFinder.cpp)
target_link_libraries(benchmarkPigzBlockFinder PRIVATE librapidgzip libblockfinder)
add_custom_target(run-benchmarkPigzBlockFinder
    COMMAND ./benchmarkPigzBlockFinder 2>&1 | tee benchmarkPigzBlockFinder.log
    VERBATIM
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
add_dependencies(run-benchmarks run-benchmarkPigzBlockFinder)


if(TARGET LibArchive::LibArchive)

add_executable(benchmarkGzip)
target_sources(benchmarkGzip PRIVATE ${CMAKE_CURRENT_LIST_DIR}/benchmarkGzip.cpp)
target_link_libraries(benchmarkGzip PRIVATE zlibstatic LibArchive::LibArchive librapidgzip)
# TODO Still requires an input file
#add_custom_target(run-benchmarkGzip
#    COMMAND ./benchmarkGzip 2>&1 | tee benchmarkGzip.log
#    VERBATIM
#    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
#)
#add_dependencies(run-benchmarks run-benchmarkGzip)

endif()

add_executable(benchmarkInflate)
target_sources(benchmarkInflate PRIVATE ${CMAKE_CURRENT_LIST_DIR}/benchmarkInflate.cpp)
target_link_libraries(benchmarkInflate PRIVATE librapidgzip)

add_executable(benchmarkGzipBlockFinder)
target_sources(benchmarkGzipBlockFinder PRIVATE ${CMAKE_CURRENT_LIST_DIR}/benchmarkGzipBlockFinder.cpp)
target_link_libraries(benchmarkGzipBlockFinder PRIVATE cxxopts librapidgzip libblockfinder zlibstatic)
add_custom_target(run-benchmarkGzipBlockFinder
    COMMAND ./benchmarkGzipBlockFinder 2>&1 | tee benchmarkGzipBlockFinder.log
    VERBATIM
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
add_dependencies(run-benchmarks run-benchmarkGzipBlockFinder)


add_executable(benchmarkCalculations)
target_sources(benchmarkCalculations PRIVATE ${CMAKE_CURRENT_LIST_DIR}/benchmarkCalculations.cpp)
target_link_libraries(benchmarkCalculations PRIVATE librapidgzip)


add_executable(benchmarkIORead)
target_sources(benchmarkIORead PRIVATE ${CMAKE_CURRENT_LIST_DIR}/benchmarkIORead.cpp)
target_link_libraries(benchmarkIORead PRIVATE librapidgzip)


add_executable(benchmarkPipeRead)
target_sources(benchmarkPipeRead PRIVATE ${CMAKE_CURRENT_LIST_DIR}/benchmarkPipeRead.cpp)
target_link_libraries(benchmarkPipeRead PRIVATE librapidgzip)


add_executable(benchmarkIOWrite)
target_sources(benchmarkIOWrite PRIVATE ${CMAKE_CURRENT_LIST_DIR}/benchmarkIOWrite.cpp)
target_link_libraries(benchmarkIOWrite PRIVATE librapidgzip)


add_executable(empiricalFalsePositiveRate EXCLUDE_FROM_ALL)
target_sources(empiricalFalsePositiveRate PRIVATE ${CMAKE_CURRENT_LIST_DIR}/empiricalFalsePositiveRate.cpp)
target_link_libraries(empiricalFalsePositiveRate PRIVATE librapidgzip)
target_compile_features(empiricalFalsePositiveRate INTERFACE cxx_std_17)


add_executable(benchmarkSequential2023)
target_sources(benchmarkSequential2023 PRIVATE ${CMAKE_CURRENT_LIST_DIR}/benchmarkSequential2023.cpp)
target_link_libraries(benchmarkSequential2023 PRIVATE librapidgzip)


if(WITH_ISAL)
add_executable(benchmarkCRC32)
target_sources(benchmarkCRC32 PRIVATE ${CMAKE_CURRENT_LIST_DIR}/benchmarkCRC32.cpp)
target_link_libraries(benchmarkCRC32 PRIVATE librapidgzip isal_inflate)
endif()
