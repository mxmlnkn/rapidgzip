add_library(libblockfinder INTERFACE)
target_include_directories(libblockfinder INTERFACE ${CMAKE_CURRENT_LIST_DIR}/..)
target_sources(libblockfinder INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/Bgzf.hpp
    ${CMAKE_CURRENT_LIST_DIR}/DynamicHuffman.hpp
    ${CMAKE_CURRENT_LIST_DIR}/Interface.hpp
    ${CMAKE_CURRENT_LIST_DIR}/PigzNaive.hpp
    ${CMAKE_CURRENT_LIST_DIR}/PigzParallel.hpp
    ${CMAKE_CURRENT_LIST_DIR}/PigzStringView.hpp
    ${CMAKE_CURRENT_LIST_DIR}/precodecheck/BruteForceLUT.hpp
    ${CMAKE_CURRENT_LIST_DIR}/precodecheck/SingleCompressedLUT.hpp
    ${CMAKE_CURRENT_LIST_DIR}/precodecheck/SingleLUT.hpp
    ${CMAKE_CURRENT_LIST_DIR}/precodecheck/WalkTreeCompressedLUT.hpp
    ${CMAKE_CURRENT_LIST_DIR}/precodecheck/WalkTreeLUT.hpp
    ${CMAKE_CURRENT_LIST_DIR}/precodecheck/WithoutLUT.hpp
)
target_link_libraries(libblockfinder INTERFACE core)
target_compile_features(libblockfinder INTERFACE cxx_std_17)
