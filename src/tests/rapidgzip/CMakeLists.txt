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


addRapidgzipTest(testCompressedVector)
addRapidgzipTest(testCRC32)
addRapidgzipTest(testDeflate)
addRapidgzipTest(testGzipChunk)
addRapidgzipTest(testGzipIndexFormat)
addRapidgzipTest(testHuffmanCoding)
addRapidgzipTest(testInflateWrapper)

addRapidgzipTest(testGzipBlockFinder)
target_compile_options(testGzipBlockFinder PRIVATE
    # The default limit is ~33 M (1<<25) and 99 M seem to be enough currently to compile.
    # ../src/rapidgzip/blockfinder/DynamicHuffman.hpp:155:16: error: constexpr variable
    # 'NEXT_DYNAMIC_DEFLATE_CANDIDATE_LUT<'\x12'>' must be initialized by a constant expression
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fconstexpr-ops-limit=199000100>"
    "$<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang>:-fconstexpr-steps=199000100>"
    "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/constexpr:steps;199000100>"
)

addRapidgzipTest(testPrecodeCheck)
target_compile_options(testPrecodeCheck PRIVATE
    # The default limit is ~33 M (1<<25) and 99 M seem to be enough currently to compile.
    # ../src/rapidgzip/gzip/precode.hpp:86:16: error: constexpr variable 'VALID_HISTOGRAMS_COUNT'
    # must be initialized by a constant expression
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fconstexpr-ops-limit=199000100>"
    "$<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang>:-fconstexpr-steps=199000100>"
    "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/constexpr:steps;199000100>"
)


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
