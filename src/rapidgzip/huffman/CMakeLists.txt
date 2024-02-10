add_library(libhuffmangzip INTERFACE)
target_include_directories(libhuffmangzip INTERFACE ${CMAKE_CURRENT_LIST_DIR})
# For definitions.hpp and Error.hpp. Maybe use to a "common" subfolder?
target_include_directories(libhuffmangzip INTERFACE ${CMAKE_CURRENT_LIST_DIR}/..)
target_sources(libhuffmangzip INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/HuffmanCodingDoubleLiteralCached.hpp
    ${CMAKE_CURRENT_LIST_DIR}/HuffmanCodingReversedBitsCached.hpp
    ${CMAKE_CURRENT_LIST_DIR}/HuffmanCodingReversedBitsCachedCompressed.hpp
    ${CMAKE_CURRENT_LIST_DIR}/HuffmanCodingReversedCodesPerLength.hpp
)
target_link_libraries(libhuffmangzip INTERFACE core libhuffman)
target_compile_features(libhuffmangzip INTERFACE cxx_std_17)
