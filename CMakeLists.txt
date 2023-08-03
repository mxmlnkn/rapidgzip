cmake_minimum_required(VERSION 3.17)
# CMake 3.17 added LibArchive::LibArchive target to FindLibArchive

project("Indexed BZip2/Gzip Decoder" CXX)


option(CODE_COVERAGE "Enable coverage reporting" OFF)
option(USE_SYSTEM_ZLIB "Use the system-installed zlib library instead of compiling it from source" OFF)
option(WITH_RPMALLOC "Compile with rpmalloc for faster memory allocations" ON)
option(WITH_ISAL "Compile with ISA-l for more than twice as fast decompression than zlib" ON)


if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Chile the build type: Debug, Release, RelWithDebInfo" FORCE)
endif()

add_compile_options(
    "$<$<OR:$<COMPILE_LANG_AND_ID:CXX,Clang>,$<COMPILE_LANG_AND_ID:C,Clang>>:-Wall;-Wextra;-Wshadow;-Werror=return-type;-Wno-unknown-attributes>"
    "$<$<OR:$<COMPILE_LANG_AND_ID:CXX,GNU>,$<COMPILE_LANG_AND_ID:C,GNU>>:-Wall;-Wextra;-Wshadow;-Wunused;-Werror=return-type;-Wno-attributes>"
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wsuggest-override>"
    # The default limit is ~33 M (1<<25) and 99 M seem to be enough currently to compile.
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fconstexpr-ops-limit=99000100>"
    "$<$<COMPILE_LANG_AND_ID:CXX,Clang>:-fconstexpr-steps=99000100>"
    "$<$<AND:$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>,$<BOOL:${CODE_COVERAGE}>>:-O0;-g;--coverage>"
)

add_link_options(
    "$<$<AND:$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>,$<BOOL:${CODE_COVERAGE}>>:--coverage>"
)

if(CODE_COVERAGE)
    add_definitions(-DSHORT_TESTS)
endif()


# This command should be in the source directory root because ctest expects
# to find a test file in the build directory root.
include(CTest)

if(USE_SYSTEM_ZLIB)
    set(OLD_CMAKE_FIND_LIBRARY_SUFFIXES "${CMAKE_FIND_LIBRARY_SUFFIXES}")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    find_package(ZLIB QUIET)
    set(CMAKE_FIND_LIBRARY_SUFFIXES "${OLD_CMAKE_FIND_LIBRARY_SUFFIXES}")
    add_library(zlibstatic ALIAS ZLIB::ZLIB)
endif()
find_package(LibArchive QUIET)
find_package(Threads REQUIRED)


add_subdirectory(src)


# Add convenience custom targets

include(ProcessorCount)
ProcessorCount(coreCount)

add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} -j ${coreCount} --extra-verbose)

add_custom_target(check-memcheck
    COMMAND ${CMAKE_CTEST_COMMAND} -j ${coreCount} --extra-verbose --force-new-ctest-process --test-action memcheck
    COMMAND cat "${CMAKE_BINARY_DIR}/Testing/Temporary/MemoryChecker.*.log"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_dependencies(check all_tests)
add_dependencies(check-memcheck all_tests)


# Add beautify target

find_package(Git QUIET)

if(GIT_FOUND)
    add_custom_target(beautify-all
        COMMAND ${GIT_EXECUTABLE} ls-tree -r --name-only HEAD > ${CMAKE_BINARY_DIR}/.beautify.lst
        COMMAND sed -i -E "/[.](h|c)(pp)?$/!d; /external\\//d; /indexed_bzip2.cpp/d;" ${CMAKE_BINARY_DIR}/.beautify.lst
        COMMAND uncrustify -c uncrustify.cfg -F ${CMAKE_BINARY_DIR}/.beautify.lst --no-backup
        VERBATIM
        # git ls-tree needs to be executed in git root
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()
