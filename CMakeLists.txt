cmake_minimum_required(VERSION 3.17)
# CMake 3.17 added LibArchive::LibArchive target to FindLibArchive

project("Indexed BZip2/Gzip Decoder" CXX)

option(CODE_COVERAGE "Enable coverage reporting" OFF)
option(USE_SYSTEM_ZLIB "Use the system-installed zlib library instead of compiling it from source" OFF)
option(WITH_RPMALLOC "Compile with rpmalloc for faster memory allocations" ON)
option(WITH_ISAL "Compile with ISA-l for more than twice as fast decompression than zlib" ON)
option(RUN_CLANG_TIDY "Runs clang-tidy while building targets" OFF)

if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm" OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "aarch")
    set(IS_ARM ON)
    # ARM compilation not yet implemented!
    set(WITH_ISAL OFF)
else()
    set(IS_ARM OFF)
endif()

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)

# https://stackoverflow.com/questions/71797349/is-it-possible-to-ignore-a-header-with-clang-tidy
# Clang-tidy has no negative matching, so painstakingly list all except for the "external" folder.
# Not listing the "benchmark" and "test" folders also shouldn't hurt.
set(CLANG_TIDY_COMMAND "clang-tidy;--config-file;${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy;--header-filter=/src/\(core\|huffman\|indexed_bzip2\|rapidgzip\|tools\)")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Chile the build type: Debug, Release, RelWithDebInfo" FORCE)
endif()

add_compile_options(
    "$<$<OR:$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang>,$<COMPILE_LANG_AND_ID:C,AppleClang,Clang>>:-Wall;-Wextra;-Wshadow;-Werror=return-type;-Wno-unknown-attributes>"
    "$<$<OR:$<COMPILE_LANG_AND_ID:CXX,GNU>,$<COMPILE_LANG_AND_ID:C,GNU>>:-Wall;-Wextra;-Wshadow;-Wunused;-Werror=return-type;-Wno-attributes>"
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wsuggest-override>"
    # The default limit is ~33 M (1<<25) and 99 M seem to be enough currently to compile.
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fconstexpr-ops-limit=199000100>"
    "$<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang>:-fconstexpr-steps=199000100>"
    "$<$<AND:$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang,GNU>,$<BOOL:${CODE_COVERAGE}>>:-O0;-g;--coverage>"
    # Add some hardening. See e.g.:
    # https://www.phoronix.com/news/GCC-fhardened-Hardening-Option
    # https://developers.redhat.com/blog/2018/03/21/compiler-and-linker-flags-gcc
    # I have not observed any performance impact for these.
    "$<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-fpie;-fstack-protector-strong>"
    # -fstack-clash-protection crashes the MINGW compiler https://github.com/msys2/MINGW-packages/issues/5348
    "$<$<AND:$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>,$<NOT:$<PLATFORM_ID:Windows>>>:-fstack-clash-protection>"
    # Fix error with MINGW: Fatal error: can't write 94 bytes to section .text of testCLI.cpp.obj: 'file too big'
    "$<$<AND:$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>,$<PLATFORM_ID:Windows>>:-Wa,-mbig-obj>"
)

if(NOT ${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm" AND NOT ${CMAKE_SYSTEM_PROCESSOR} MATCHES "aarch")
    add_compile_options("$<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-fcf-protection=full>")
endif()

add_link_options(
    "$<$<AND:$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang,GNU>,$<BOOL:${CODE_COVERAGE}>>:--coverage>"
    # See the note about hardening inside add_compile_options.
    "$<$<AND:$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>,$<NOT:$<PLATFORM_ID:Windows>>>:-Wl,-pie,-z,relro,-z,now,-z,defs>"
)

if(CODE_COVERAGE)
    add_definitions(-DSHORT_TESTS -D_FORTIFY_SOURCE=2 -D_GLIBCXX_ASSERTIONS)
endif()


# This command should be in the source directory root because ctest expects
# to find a test file in the build directory root.
include(CTest)

if(USE_SYSTEM_ZLIB)
    set(OLD_CMAKE_FIND_LIBRARY_SUFFIXES "${CMAKE_FIND_LIBRARY_SUFFIXES}")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    find_package(ZLIB REQUIRED)
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
