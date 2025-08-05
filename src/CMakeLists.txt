
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/cxxopts/include/cxxopts.hpp OR
   NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/zlib/inflate.h OR
   NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/rpmalloc/rpmalloc.h)
    find_package(Git REQUIRED)

    execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update --recursive --init
        # git submodule needs to be executed in git root
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()

add_library(cxxopts INTERFACE)
target_include_directories(cxxopts SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/external/cxxopts/include)
target_sources(cxxopts INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/external/cxxopts/include/cxxopts.hpp)


# rpmalloc

project(rpmalloc C)

add_library(rpmalloc STATIC)
set(RPMALLOC_HOME "${CMAKE_CURRENT_SOURCE_DIR}/external/rpmalloc/rpmalloc")
target_include_directories(rpmalloc SYSTEM INTERFACE ${RPMALLOC_HOME})
target_sources(rpmalloc PRIVATE
    ${RPMALLOC_HOME}/rpmalloc.c
    ${RPMALLOC_HOME}/rpmalloc.h
    # Do not use! Overriding operator new/delete is too bug-prone.
    #${RPMALLOC_HOME}/rpnew.h
)
set_target_properties(rpmalloc PROPERTIES C_STANDARD 11)
set_target_properties(rpmalloc PROPERTIES LINKER_LANGUAGE C)
# ENABLE_OVERRIDE=1 results in a segfault from _dl_start_user
target_compile_definitions(rpmalloc PUBLIC ENABLE_OVERRIDE=0 ENABLE_ASSERTS=0)

option(ENABLE_RPMALLOC_STATISTICS "Enables statistics for rpmalloc" OFF)
if (ENABLE_RPMALLOC_STATISTICS)
	target_compile_definitions(rpmalloc PRIVATE -DENABLE_STATISTICS=1)
endif()

#add_subdirectory(external/zlib)

if(NOT USE_SYSTEM_ZLIB)
    project(zlibstatic C)
    add_library(zlibstatic STATIC)
    set(ZLIB_HOME "${CMAKE_CURRENT_SOURCE_DIR}/external/zlib")
    target_include_directories(zlibstatic SYSTEM INTERFACE ${ZLIB_HOME})
    target_sources(zlibstatic PRIVATE
        ${ZLIB_HOME}/inflate.c
        ${ZLIB_HOME}/inflate.h
        ${ZLIB_HOME}/adler32.c  # zutil.h is the corresponding header
        ${ZLIB_HOME}/crc32.c
        ${ZLIB_HOME}/crc32.h
        ${ZLIB_HOME}/inffixed.h
        ${ZLIB_HOME}/inffast.c
        ${ZLIB_HOME}/inffast.h
        ${ZLIB_HOME}/inftrees.c
        ${ZLIB_HOME}/inftrees.h
        ${ZLIB_HOME}/gzguts.h
        ${ZLIB_HOME}/zutil.c
        ${ZLIB_HOME}/zutil.h
        ${ZLIB_HOME}/zlib.h
        # Only required for compression, e.g., used for the tests and benchmarks
        ${ZLIB_HOME}/deflate.c
        ${ZLIB_HOME}/deflate.h
        ${ZLIB_HOME}/trees.c
        ${ZLIB_HOME}/trees.h
    )
    set_target_properties(zlibstatic PROPERTIES LINKER_LANGUAGE C)
endif()


# ISA-l

if(WITH_ISAL)
    include(FetchContent)

    FetchContent_Declare(isal_project SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/isa-l)
    # https://discourse.cmake.org/t/fetchcontent-declare-with-cmake-args-does-not-work/11227
    # https://discourse.cmake.org/t/what-is-the-correct-way-to-set-options-of-a-project-before-fetch-content/268
    set(ISAL_BUILD_TESTS OFF CACHE BOOL "Disable ISA-L tests" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static ISA-L" FORCE)
    FetchContent_MakeAvailable(isal_project)

    # I am not using the guarded <isa-l/igzip_lib.h> includes because it would not work in the current setup.py :(.
    # The guarded includes are also not necessary or possible with FetchContent, only with ExternalProject_Add!
    add_library(ISAL::isal ALIAS isal)
endif(WITH_ISAL)


add_subdirectory(benchmarks)
add_subdirectory(core)
add_subdirectory(huffman)
add_subdirectory(indexed_bzip2)
add_subdirectory(rapidgzip)
add_subdirectory(tools)
add_subdirectory(tests)
