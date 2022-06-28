add_library(libhuffman INTERFACE)
target_include_directories(libhuffman INTERFACE ${CMAKE_CURRENT_LIST_DIR})
# For definitions.hpp and Error.hpp. Maybe use to a "common" subfolder?
target_include_directories(libhuffman INTERFACE ${CMAKE_CURRENT_LIST_DIR}/..)
target_sources(libhuffman INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/HuffmanCodingBase.hpp
    ${CMAKE_CURRENT_LIST_DIR}/HuffmanCodingDoubleLiteralCached.hpp
    ${CMAKE_CURRENT_LIST_DIR}/HuffmanCodingLinearSearch.hpp
    ${CMAKE_CURRENT_LIST_DIR}/HuffmanCodingReversedBitsCached.hpp
    ${CMAKE_CURRENT_LIST_DIR}/HuffmanCodingReversedBitsCachedCompressed.hpp
    ${CMAKE_CURRENT_LIST_DIR}/HuffmanCodingReversedCodesPerLength.hpp
    ${CMAKE_CURRENT_LIST_DIR}/HuffmanCodingSymbolsPerLength.hpp
)
target_link_libraries(libhuffman INTERFACE core)
target_compile_features(libhuffman INTERFACE cxx_std_17)
