add_library(indexed_bzip2 INTERFACE)
target_include_directories(indexed_bzip2 INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_sources(indexed_bzip2 INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/BitReader.hpp
    ${CMAKE_CURRENT_LIST_DIR}/BZ2Reader.hpp
    ${CMAKE_CURRENT_LIST_DIR}/BZ2ReaderInterface.hpp
    ${CMAKE_CURRENT_LIST_DIR}/bzip2.hpp
    ${CMAKE_CURRENT_LIST_DIR}/common.hpp
    ${CMAKE_CURRENT_LIST_DIR}/FileReader.hpp
)
target_compile_features(indexed_bzip2 INTERFACE cxx_std_11)

find_package(Threads REQUIRED)


add_library(indexed_bzip2_parallel INTERFACE)
target_include_directories(indexed_bzip2_parallel INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_sources(indexed_bzip2_parallel INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/BitReader.hpp
    ${CMAKE_CURRENT_LIST_DIR}/BitStringFinder.hpp
    ${CMAKE_CURRENT_LIST_DIR}/BZ2Reader.hpp
    ${CMAKE_CURRENT_LIST_DIR}/BZ2ReaderInterface.hpp
    ${CMAKE_CURRENT_LIST_DIR}/bzip2.hpp
    ${CMAKE_CURRENT_LIST_DIR}/Cache.hpp
    ${CMAKE_CURRENT_LIST_DIR}/common.hpp
    ${CMAKE_CURRENT_LIST_DIR}/FileReader.hpp
    ${CMAKE_CURRENT_LIST_DIR}/JoiningThread.hpp
    ${CMAKE_CURRENT_LIST_DIR}/ParallelBitStringFinder.hpp
    ${CMAKE_CURRENT_LIST_DIR}/ParallelBZ2Reader.hpp
    ${CMAKE_CURRENT_LIST_DIR}/Prefetcher.hpp
    ${CMAKE_CURRENT_LIST_DIR}/ThreadPool.hpp
)
target_link_libraries(indexed_bzip2_parallel INTERFACE indexed_bzip2 Threads::Threads)
target_compile_features(indexed_bzip2_parallel INTERFACE cxx_std_17)
