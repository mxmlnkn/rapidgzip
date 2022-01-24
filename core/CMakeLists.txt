add_library(core INTERFACE)
target_include_directories(core INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_sources(core INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/BitManipulation.hpp
    ${CMAKE_CURRENT_LIST_DIR}/BitReader.hpp
    ${CMAKE_CURRENT_LIST_DIR}/BitStringFinder.hpp
    ${CMAKE_CURRENT_LIST_DIR}/BlockMap.hpp
    ${CMAKE_CURRENT_LIST_DIR}/Cache.hpp
    ${CMAKE_CURRENT_LIST_DIR}/common.hpp
    ${CMAKE_CURRENT_LIST_DIR}/FileReader.hpp
    ${CMAKE_CURRENT_LIST_DIR}/JoiningThread.hpp
    ${CMAKE_CURRENT_LIST_DIR}/ParallelBitStringFinder.hpp
    ${CMAKE_CURRENT_LIST_DIR}/Prefetcher.hpp
    ${CMAKE_CURRENT_LIST_DIR}/PythonFileReader.hpp
    ${CMAKE_CURRENT_LIST_DIR}/SharedFileReader.hpp
    ${CMAKE_CURRENT_LIST_DIR}/StandardFileReader.hpp
    ${CMAKE_CURRENT_LIST_DIR}/StreamedResults.hpp
    ${CMAKE_CURRENT_LIST_DIR}/ThreadPool.hpp
)
target_link_libraries(core INTERFACE Threads::Threads)
target_compile_features(core INTERFACE cxx_std_17)
