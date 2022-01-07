add_subdirectory(huffman)
add_subdirectory(blockfinder)


add_library(libpragzip INTERFACE)
target_include_directories(libpragzip INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_sources(libpragzip INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/crc32.hpp
    ${CMAKE_CURRENT_LIST_DIR}/definitions.hpp
    ${CMAKE_CURRENT_LIST_DIR}/deflate.hpp
    ${CMAKE_CURRENT_LIST_DIR}/Error.hpp
    ${CMAKE_CURRENT_LIST_DIR}/gzip.hpp
    ${CMAKE_CURRENT_LIST_DIR}/GzipBlockFetcher.hpp
    ${CMAKE_CURRENT_LIST_DIR}/GzipReader.hpp
    ${CMAKE_CURRENT_LIST_DIR}/IndexFileFormat.hpp
    ${CMAKE_CURRENT_LIST_DIR}/ParallelGzipReader.hpp
    ${CMAKE_CURRENT_LIST_DIR}/pragzip.hpp
)
target_link_libraries(libpragzip INTERFACE libhuffman core)
target_compile_features(libpragzip INTERFACE cxx_std_17)
