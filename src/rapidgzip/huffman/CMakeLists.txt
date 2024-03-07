add_library(libhuffmangzip INTERFACE)
add_library(rapidgzip::gzip::huffman ALIAS libhuffmangzip)
target_include_directories(libhuffmangzip INTERFACE ${CMAKE_CURRENT_LIST_DIR})
# For definitions.hpp. Maybe use yet another target?
target_include_directories(libhuffmangzip INTERFACE ${CMAKE_CURRENT_LIST_DIR}/../gzip)
# In order to make the decoders includable with <huffman/...hpp>
target_include_directories(libhuffmangzip INTERFACE ${CMAKE_CURRENT_LIST_DIR}/..)
target_sources(libhuffmangzip INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/HuffmanCodingDoubleLiteralCached.hpp
    ${CMAKE_CURRENT_LIST_DIR}/HuffmanCodingReversedBitsCached.hpp
    ${CMAKE_CURRENT_LIST_DIR}/HuffmanCodingReversedBitsCachedCompressed.hpp
    ${CMAKE_CURRENT_LIST_DIR}/HuffmanCodingReversedCodesPerLength.hpp
)
target_link_libraries(libhuffmangzip INTERFACE core libhuffman)
target_compile_features(libhuffmangzip INTERFACE cxx_std_17)
