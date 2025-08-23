function(addCoreTest name)
    add_executable(${name})
    target_sources(${name} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/${name}.cpp)
    target_link_libraries(${name} PRIVATE core)
    add_test(NAME ${name} COMMAND ${name})
    add_dependencies(all_tests ${name})
endfunction()


addCoreTest(testFileReader)
addCoreTest(testBitReader)
addCoreTest(testBitManipulation)
addCoreTest(testBitStringFinder)
addCoreTest(testCache)
addCoreTest(testCommon)
addCoreTest(testFasterVector)
addCoreTest(testFileRanges)
addCoreTest(testParallelBitStringFinder)
addCoreTest(testPrefetcher)
addCoreTest(testSimpleRunLengthEncoding)
target_compile_features(testSimpleRunLengthEncoding PRIVATE cxx_std_20)  # for constexpr std::ranges::equal

addCoreTest(testStreamAdapter)
addCoreTest(testThreadPool)
