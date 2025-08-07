function(addRapidgzipTest name)
    add_executable(${name})
    target_sources(${name} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/${name}.cpp)
    target_link_libraries(${name} PRIVATE librapidgzip)
    target_compile_definitions(${name} PRIVATE RAPIDGZIP_FATAL_PERFORMANCE_WARNINGS)
    add_test(NAME ${name} COMMAND ${name})
    add_dependencies(all_tests ${name})

    # macos-13 ASAN is very slow!
    # 24/28 Test #19: testGzipChunk ....................   Passed  127.72 sec
    # 25/28 Test #21: testPrecodeCheck .................   Passed  460.81 sec
    # 26/28 Test #26: testGzip .........................   Passed  758.07 sec
    # 27/28 Test #27: testParallelGzipReader ...........***Timeout 1500.55 sec
    # 28/28 Test #28: testCLI ..........................   Passed  1434.14 sec
    set_tests_properties(${name} PROPERTIES TIMEOUT 3000)
endfunction()


addRapidgzipTest(testGzipIndexFormat)
addRapidgzipTest(testHuffmanCoding)
addRapidgzipTest(testDeflate)
addRapidgzipTest(testGzipChunk)
addRapidgzipTest(testGzipBlockFinder)
addRapidgzipTest(testPrecodeCheck)
addRapidgzipTest(testCompressedVector)
addRapidgzipTest(testCRC32)
addRapidgzipTest(testInflateWrapper)

addRapidgzipTest(testDecodedData)
target_compile_features(testDecodedData INTERFACE cxx_std_20)  # for aggregate initialization

if(NOT WIN32)
addRapidgzipTest(testGzip)
addRapidgzipTest(testParallelGzipReader)

addRapidgzipTest(testCLI)
target_include_directories(testCLI PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../tools)
target_compile_options(testCLI PRIVATE -Wno-braced-scalar-init)
target_link_libraries(testCLI PRIVATE librapidgzip cxxopts)
endif()
