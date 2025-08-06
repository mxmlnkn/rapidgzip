add_library(core INTERFACE)
target_include_directories(core INTERFACE ${CMAKE_CURRENT_LIST_DIR}/..)
target_sources(core INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/AffinityHelpers.hpp
    ${CMAKE_CURRENT_LIST_DIR}/AlignedAllocator.hpp
    ${CMAKE_CURRENT_LIST_DIR}/AtomicMutex.hpp
    ${CMAKE_CURRENT_LIST_DIR}/BitManipulation.hpp
    ${CMAKE_CURRENT_LIST_DIR}/BitStringFinder.hpp
    ${CMAKE_CURRENT_LIST_DIR}/BlockFetcher.hpp
    ${CMAKE_CURRENT_LIST_DIR}/BlockFinder.hpp
    ${CMAKE_CURRENT_LIST_DIR}/BlockFinderInterface.hpp
    ${CMAKE_CURRENT_LIST_DIR}/BlockMap.hpp
    ${CMAKE_CURRENT_LIST_DIR}/Cache.hpp
    ${CMAKE_CURRENT_LIST_DIR}/common.hpp
    ${CMAKE_CURRENT_LIST_DIR}/DataGenerators.hpp
    ${CMAKE_CURRENT_LIST_DIR}/Error.hpp
    ${CMAKE_CURRENT_LIST_DIR}/FasterVector.hpp
    ${CMAKE_CURRENT_LIST_DIR}/FileRanges.hpp
    ${CMAKE_CURRENT_LIST_DIR}/FileUtils.hpp
    ${CMAKE_CURRENT_LIST_DIR}/JoiningThread.hpp
    ${CMAKE_CURRENT_LIST_DIR}/ParallelBitStringFinder.hpp
    ${CMAKE_CURRENT_LIST_DIR}/Prefetcher.hpp
    ${CMAKE_CURRENT_LIST_DIR}/ScopedGIL.hpp
    ${CMAKE_CURRENT_LIST_DIR}/Statistics.hpp
    ${CMAKE_CURRENT_LIST_DIR}/StreamedResults.hpp
    ${CMAKE_CURRENT_LIST_DIR}/TestHelpers.hpp
    ${CMAKE_CURRENT_LIST_DIR}/ThreadPool.hpp
    ${CMAKE_CURRENT_LIST_DIR}/VectorView.hpp
)
target_link_libraries(core INTERFACE Threads::Threads filereader)
target_compile_features(core INTERFACE cxx_std_17)
target_compile_options(core INTERFACE
    # The default limit is ~33 M (1<<25) and 99 M seem to be enough currently to compile.
    # BitManipulation.hpp:243:56: note: in call to 'createReversedBitsLUT<unsigned short>()'
    #   243 | alignas( 8 ) static constexpr auto REVERSED_BITS_LUT = createReversedBitsLUT<T>();
    # TODO Compute the LUT once, write it out, and include it, maybe even compress it.
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fconstexpr-ops-limit=199000100>"
    "$<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang>:-fconstexpr-steps=199000100>"
)
