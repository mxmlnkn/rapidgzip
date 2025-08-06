add_library(libblockfinder INTERFACE)
add_library(rapidgzip::libblockfinder ALIAS libblockfinder)
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
target_compile_options(libblockfinder INTERFACE
    # The default limit is ~33 M (1<<25) and 99 M seem to be enough currently to compile.
    # DynamicHuffman.hpp:148 has egregious compile-time overhead.
    # TODO Compute the LUT once, write it out, and include it, maybe even compress it.
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fconstexpr-ops-limit=199000100>"
    "$<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang>:-fconstexpr-steps=199000100>"
)
