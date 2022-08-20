
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/cxxopts/include/cxxopts.hpp OR
   NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/zlib/inflate.h)
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


add_subdirectory(benchmarks)
add_subdirectory(core)
add_subdirectory(indexed_bzip2)
add_subdirectory(pragzip)
add_subdirectory(tools)
add_subdirectory(tests)


#add_subdirectory(external/zlib)

project(zlibstatic C)

if(NOT USE_SYSTEM_ZLIB)
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
    )
    set_target_properties(zlibstatic PROPERTIES LINKER_LANGUAGE C)
endif()
