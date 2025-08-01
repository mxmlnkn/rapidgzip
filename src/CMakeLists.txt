
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


add_subdirectory(benchmarks)
add_subdirectory(core)
add_subdirectory(huffman)
add_subdirectory(indexed_bzip2)
add_subdirectory(rapidgzip)
add_subdirectory(tools)
add_subdirectory(tests)


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
    project(isal_inflate LANGUAGES C)

    set(ISAL_HOME "${CMAKE_CURRENT_SOURCE_DIR}/external/isa-l")

    # I think I tried to build with a subset of all files in order to package it easier for PyPI.
    # But the MANIFEST.in globs anything by extension anyway. So I am not sure anymore. I also simply do not need
    # much of the stuff, e.g., for compression, which could reduce binary size.
    add_library(isal_inflate STATIC
        # Files suffixed with _test, _perf, _example can be ignored!
        ${ISAL_HOME}/include/igzip_lib.h
        ${ISAL_HOME}/include/unaligned.h
        ${ISAL_HOME}/igzip/igzip_inflate.c
        ${ISAL_HOME}/igzip/igzip.c
        ${ISAL_HOME}/igzip/hufftables_c.c
        ${ISAL_HOME}/igzip/igzip_checksums.h
        ${ISAL_HOME}/igzip/igzip_wrapper.h
        ${ISAL_HOME}/igzip/static_inflate.h

        # Compression
        ${ISAL_HOME}/igzip/igzip_base_aliases.c
        ${ISAL_HOME}/igzip/encode_df.c
        ${ISAL_HOME}/igzip/igzip_icf_base.c
        ${ISAL_HOME}/igzip/igzip_icf_body.c
        ${ISAL_HOME}/igzip/igzip_base.c

        ${ISAL_HOME}/igzip/adler32_base.c
        ${ISAL_HOME}/igzip/flatten_ll.c
        #${ISAL_HOME}/igzip/generate_custom_hufftables.c
        #${ISAL_HOME}/igzip/generate_static_inflate.c
        ${ISAL_HOME}/igzip/huff_codes.c
        ${ISAL_HOME}/igzip/proc_heap_base.c

        ${ISAL_HOME}/crc/crc_base.c
        #${ISAL_HOME}/crc/crc_base_aliases.c
    )

    if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "aarch64" OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm64")
        enable_language("ASM")
        # On aarch64, use the C compiler for assembly instead of NASM.
        set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})
        set(CMAKE_ASM_FLAGS "-D__ASSEMBLY__")

        target_sources(isal_inflate PRIVATE
            ${ISAL_HOME}/include/aarch64_multibinary.h
            ${ISAL_HOME}/include/aarch64_label.h
            ${ISAL_HOME}/crc/aarch64/crc_aarch64_dispatcher.c
            ${ISAL_HOME}/igzip/aarch64/igzip_multibinary_aarch64_dispatcher.c
        )

        set(CMAKE_ASM_NASM_SOURCE_FILE_EXTENSIONS "asm;nasm;S")

        target_sources(isal_inflate PRIVATE
            ${ISAL_HOME}/igzip/aarch64/bitbuf2_aarch64.h
            ${ISAL_HOME}/igzip/aarch64/data_struct_aarch64.h
            ${ISAL_HOME}/igzip/aarch64/encode_df.S
            ${ISAL_HOME}/igzip/aarch64/gen_icf_map.S
            ${ISAL_HOME}/igzip/aarch64/huffman_aarch64.h
            ${ISAL_HOME}/igzip/aarch64/igzip_decode_huffman_code_block_aarch64.S
            ${ISAL_HOME}/igzip/aarch64/igzip_deflate_body_aarch64.S
            ${ISAL_HOME}/igzip/aarch64/igzip_deflate_finish_aarch64.S
            ${ISAL_HOME}/igzip/aarch64/igzip_deflate_hash_aarch64.S
            ${ISAL_HOME}/igzip/aarch64/igzip_inflate_multibinary_arm64.S
            ${ISAL_HOME}/igzip/aarch64/igzip_isal_adler32_neon.S
            ${ISAL_HOME}/igzip/aarch64/igzip_multibinary_aarch64_dispatcher.c
            ${ISAL_HOME}/igzip/aarch64/igzip_multibinary_arm64.S
            ${ISAL_HOME}/igzip/aarch64/igzip_set_long_icf_fg.S
            ${ISAL_HOME}/igzip/aarch64/isal_deflate_icf_body_hash_hist.S
            ${ISAL_HOME}/igzip/aarch64/isal_deflate_icf_finish_hash_hist.S
            ${ISAL_HOME}/igzip/aarch64/isal_update_histogram.S
            ${ISAL_HOME}/igzip/aarch64/lz0a_const_aarch64.h
            ${ISAL_HOME}/igzip/aarch64/options_aarch64.h
            ${ISAL_HOME}/igzip/aarch64/stdmac_aarch64.h

            ${ISAL_HOME}/crc/aarch64/crc_multibinary_arm.S
            #${ISAL_HOME}/crc/aarch64/crc32_aarch64_common.h
            #${ISAL_HOME}/crc/aarch64/crc32_common_crc_ext_cortex_a72.S
            #${ISAL_HOME}/crc/aarch64/crc32_common_mix_neoverse_n1.S
            ${ISAL_HOME}/crc/aarch64/crc32_gzip_refl_3crc_fold.S
            ${ISAL_HOME}/crc/aarch64/crc32_gzip_refl_crc_ext.S
            ${ISAL_HOME}/crc/aarch64/crc32_gzip_refl_pmull.S
            ${ISAL_HOME}/crc/aarch64/crc32_gzip_refl_pmull.h
            #${ISAL_HOME}/crc/aarch64/crc32_mix_default.S
            #${ISAL_HOME}/crc/aarch64/crc32_mix_default_common.S
            #${ISAL_HOME}/crc/aarch64/crc32_mix_neoverse_n1.S
            #${ISAL_HOME}/crc/aarch64/crc32_norm_common_pmull.h
            #${ISAL_HOME}/crc/aarch64/crc32_refl_common_pmull.h
            #${ISAL_HOME}/crc/aarch64/crc32c_mix_default.S
            #${ISAL_HOME}/crc/aarch64/crc32c_mix_neoverse_n1.S
        )
    else()
        enable_language("ASM_NASM")
        target_sources(isal_inflate PRIVATE
            # Files suffixed with _test, _perf, _example can be ignored!
            ${ISAL_HOME}/include/reg_sizes.asm
            ${ISAL_HOME}/include/multibinary.asm

            ${ISAL_HOME}/igzip/igzip_inflate_multibinary.asm
            ${ISAL_HOME}/igzip/igzip_decode_block_stateless_01.asm
            ${ISAL_HOME}/igzip/igzip_decode_block_stateless_04.asm
            #${ISAL_HOME}/igzip/igzip_decode_block_stateless.asm
            ${ISAL_HOME}/igzip/rfc1951_lookup.asm
            ${ISAL_HOME}/igzip/stdmac.asm

            # Compression
            ${ISAL_HOME}/igzip/igzip_deflate_hash.asm
            ${ISAL_HOME}/igzip/igzip_body.asm
            ${ISAL_HOME}/igzip/igzip_multibinary.asm
            ${ISAL_HOME}/igzip/igzip_update_histogram_01.asm
            ${ISAL_HOME}/igzip/igzip_update_histogram_04.asm
            #${ISAL_HOME}/igzip/igzip_update_histogram.asm

            #${ISAL_HOME}/igzip/bitbuf2.asm
            #${ISAL_HOME}/igzip/data_struct2.asm
            ${ISAL_HOME}/igzip/encode_df_04.asm
            ${ISAL_HOME}/igzip/encode_df_06.asm
            #${ISAL_HOME}/igzip/heap_macros.asm
            #${ISAL_HOME}/igzip/huffman.asm
            #${ISAL_HOME}/igzip/igzip_compare_types.asm
            ${ISAL_HOME}/igzip/igzip_finish.asm
            ${ISAL_HOME}/igzip/igzip_gen_icf_map_lh1_04.asm
            ${ISAL_HOME}/igzip/igzip_gen_icf_map_lh1_06.asm
            ${ISAL_HOME}/igzip/igzip_icf_body_h1_gr_bt.asm
            ${ISAL_HOME}/igzip/igzip_icf_finish.asm
            ${ISAL_HOME}/igzip/igzip_set_long_icf_fg_04.asm
            ${ISAL_HOME}/igzip/igzip_set_long_icf_fg_06.asm
            #${ISAL_HOME}/igzip/inflate_data_structs.asm
            #${ISAL_HOME}/igzip/lz0a_const.asm
            #${ISAL_HOME}/igzip/options.asm
            ${ISAL_HOME}/igzip/proc_heap.asm

            ${ISAL_HOME}/igzip/adler32_avx2_4.asm
            ${ISAL_HOME}/igzip/adler32_sse.asm

            ${ISAL_HOME}/crc/crc_multibinary.asm
            ${ISAL_HOME}/crc/crc32_gzip_refl_by16_10.asm
            ${ISAL_HOME}/crc/crc32_gzip_refl_by8_02.asm
            ${ISAL_HOME}/crc/crc32_gzip_refl_by8.asm
        )
    endif()

    target_compile_options(isal_inflate PRIVATE
        "$<$<OR:$<COMPILE_LANG_AND_ID:CXX,Clang>,$<COMPILE_LANG_AND_ID:C,Clang>>:-Wno-sign-compare;-Wno-unused-parameter;-Wno-shadow;-Wno-incompatible-pointer-types-discards-qualifiers;-Wno-implicit-fallthrough>"
        "$<$<OR:$<COMPILE_LANG_AND_ID:CXX,GNU>,$<COMPILE_LANG_AND_ID:C,GNU>>:-Wno-sign-compare;-Wno-unused-parameter;-Wno-shadow;-Wno-incompatible-pointer-types-discards-qualifiers;-Wno-implicit-fallthrough>"
    )
    target_include_directories(isal_inflate
        PUBLIC
            ${ISAL_HOME}/include
        PRIVATE
            ${ISAL_HOME}
            ${ISAL_HOME}/igzip
    )
endif(WITH_ISAL)
