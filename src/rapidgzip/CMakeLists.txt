add_subdirectory(huffman)
add_subdirectory(blockfinder)


add_library(librapidgzip INTERFACE)
target_include_directories(librapidgzip INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_sources(librapidgzip INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/crc32.hpp
    ${CMAKE_CURRENT_LIST_DIR}/ChunkData.hpp
    ${CMAKE_CURRENT_LIST_DIR}/CompressedVector.hpp
    ${CMAKE_CURRENT_LIST_DIR}/DecodedData.hpp
    ${CMAKE_CURRENT_LIST_DIR}/DecodedDataView.hpp
    ${CMAKE_CURRENT_LIST_DIR}/definitions.hpp
    ${CMAKE_CURRENT_LIST_DIR}/deflate.hpp
    ${CMAKE_CURRENT_LIST_DIR}/Error.hpp
    ${CMAKE_CURRENT_LIST_DIR}/gzip.hpp
    ${CMAKE_CURRENT_LIST_DIR}/GzipAnalyzer.hpp
    ${CMAKE_CURRENT_LIST_DIR}/GzipBlockFinder.hpp
    ${CMAKE_CURRENT_LIST_DIR}/GzipChunkFetcher.hpp
    ${CMAKE_CURRENT_LIST_DIR}/GzipReader.hpp
    ${CMAKE_CURRENT_LIST_DIR}/IndexFileFormat.hpp
    ${CMAKE_CURRENT_LIST_DIR}/InflateWrapper.hpp
    ${CMAKE_CURRENT_LIST_DIR}/MarkerReplacement.hpp
    ${CMAKE_CURRENT_LIST_DIR}/ParallelGzipReader.hpp
    ${CMAKE_CURRENT_LIST_DIR}/rapidgzip.hpp
    ${CMAKE_CURRENT_LIST_DIR}/precode.hpp
    ${CMAKE_CURRENT_LIST_DIR}/zlib.hpp
    ${CMAKE_CURRENT_LIST_DIR}/WindowMap.hpp
)
target_link_libraries(librapidgzip INTERFACE libhuffman core zlibstatic isal_inflate)
target_compile_features(librapidgzip INTERFACE cxx_std_17)
if(WITH_ISAL)
    target_compile_definitions(librapidgzip INTERFACE WITH_ISAL)
endif()
