function(addRapidgzipTest name)
    add_executable(${name})
    target_sources(${name} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/${name}.cpp)
    target_link_libraries(${name} PRIVATE librapidgzip)
    add_test(NAME ${name} COMMAND ${name})
    add_dependencies(all_tests ${name})
endfunction()


addRapidgzipTest(testGzipIndexFormat)
addRapidgzipTest(testHuffmanCoding)
addRapidgzipTest(testDeflate)
addRapidgzipTest(testGzip)
addRapidgzipTest(testParallelGzipReader)
addRapidgzipTest(testGzipChunkFetcher)
addRapidgzipTest(testGzipBlockFinder)
addRapidgzipTest(testPrecodeCheck)
addRapidgzipTest(testCompressedVector)
addRapidgzipTest(testCRC32)
addRapidgzipTest(testInflateWrapper)

addRapidgzipTest(testDecodedData)
target_compile_features(testDecodedData INTERFACE cxx_std_20)  # for aggregate initialization

addRapidgzipTest(testCLI)
target_include_directories(testCLI PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../tools)
target_link_libraries(testCLI PRIVATE librapidgzip cxxopts)
