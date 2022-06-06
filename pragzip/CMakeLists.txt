add_subdirectory(huffman)


add_library(libpragzip INTERFACE)
target_include_directories(libpragzip INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_sources(libpragzip INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/IndexFileFormat.hpp
)
target_link_libraries(libpragzip INTERFACE core)
target_compile_features(libpragzip INTERFACE cxx_std_17)
