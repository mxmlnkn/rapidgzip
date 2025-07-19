
# Version 0.14.4 built on 2025-07-02

## Fixes

 - Some input files resulted in wrong null-bytes appearing in the decompressed stream when using `RapidgzipFile`
   for bzip2 files. `IndexedBzip2File` did not suffer from this bug.
 - On Python 3.11+, recursively stacking `RapdigzipFile` did segfault because of wrong usage of the GIL.


# Version 0.14.3 built on 2024-10-14

## Fixes

 - Some subchunks were post-processed on the main thread.
 - Only show warning about possibly slower decompression for parallelized usage.
 - Detect EOF when seeking forward to the end of the file.
 - Do not complain about index not being kept after importing one.
 - Apply `--quiet` to some non-fatal warnings.
 - Warn when specifying multiple output file paths.
 - Update dependencies: zlib: 1.3.0->1.3.1, rpmalloc: 1.4.5, cxxopts: 3.2.1.
 - Remove warning about useless index import followed by export because it can convert now.


# Version 0.14.2 built on 2024-05-22

## Fixes

 - Exporting an index for some files threw an exception.


# Version 0.14.1 built on 2024-05-21

## Fixes

 - Fix error when reading from stdin.


# Version 0.14.0 built on 2024-05-21

## Added

 - Add support for reading and writing indexes created with gztool.
 - Add `--ranges` option to output specified byte and line ranges.
 - Add command line options `--sparse-windows` and `--no-sparse-windows` to explicitly control window sparsity.

## Performance

 - Avoid unnecessary windows being created during bzip2 decompression to reduce memory usage vastly
   and also to increase decompression speed by ~10% from 344 MB/s to 388 MB/s.
 - Do not store windows for BGZF files as they are not needed. This reduces memory usage from 32 KiB
   down to ~16 B per seek point.
 - Optimize reading many very small (several bytes) gzip streams to be on par with igzip: 0.5 - 7.0 MB/s.
 - Use sparsity information of window to increase compression. This reduces memory size of the in-memory
   index and also exported gztool indexes. For wikidata, the index size is reduced 3x smaller.
 - Reduce memory footprint further by compressing the windows with in a zlib stream instead of gzip stream.
 - Also compress and apply sparseness for windows at chunk borders to further reduce the memory footprint.
 - Disable custom vector implementation that was meant to skip the overhead of initializing the contents
   that is to be overwritten anyway because it lead to higher memory usage (peak RSS) for yet unknown reasons.

## API

 - Add `FileReader::seekTo` method to reduce narrowing warnings for the offset.
 - Add option to configure the `BitReader` byte buffer size in the constructor.

## Fixes

 - Fix lots of style checker warnings and CI issues.
 - Detection for seeking inside the `BitReader` byte buffer did only work for the first 12.5% because
   of a missing byte to bit conversion.
 - Seeking inside the `BitReader` byte buffer after reading directly from the underlying file did result
   in a wrong seek position. This is very hard to trigger in earlier versions because of the above bug
   and because this call combination was rarely done.
 - Multiple exception were not actually thrown, only constructed. Found via clang-tidy after fixing all
   the false positives that hid these actual bugs.
 - Do not erroneously warn about useless index import when specifying an index export path and parallelization is 1.
 - Specifying an empty file path did show a seeking error instead of a helpful message.


# Version 0.13.3 built on 2024-04-27

## Fixes

 - Reading after seeking could fail in some cases because of a bug with the window compression.
 - `RapidgzipFile` `verbose` parameter did not have any effect.
 - Add newline after `--count` or `--count-lines` result.


# Version 0.13.2 built on 2024-04-22

## Fixes

 - Disable `vmsplice` usage on `-c`/`--stdout` because it can result in wrong output if rapidgzip quits before
   all of the splice output has been read from stdin of the piped to process.


# Version 0.13.1 built on 2024-03-25

## Fixes

 - The `verbose` argument was accidentally used to initialize the chunk size with 0 or 1,
   resulting in the minimum chunk size of 8 KiB being used, which can result in decompression
   errors for false positives and which slows down decompression to a crawl.


# Version 0.13.0 built on 2024-02-04

## Added

 - Use ISA-L CRC32 computation, which uses PCLMULQDQ if available
 - Improve profiling output on `--verbose`.
 - Add support for bzip2 decompression via the `ParallelGzipReader` architecture.
   This is one small step to a unified parallelized and seekable decoder for multiple formats.
 - Expose chunk size and I/O read method to Python interface.

## Performance

 - Compress windows for chunks with large compression ratios in memory to reduce the memory footprint.
   This reduces the memory usage for working with `wikidata-20220103-all.json.gz`
   from 20 GB down to 12 GB and can have even larger effects for larger files.
   The compression ratio threshold and the compression being done in parallel keeps the overhead
   for this memory optimization to a minimum.
 - Avoid temporary allocations for internal `SharedFileReader::getLock` calls.
 - Automatically adjust chunk size for "small" files and large parallelizations.
 - Use faster short-/long-LUT Huffman decoder if compiled without ISA-L.

## API

 - Change template parameter `ENABLE_STATISTICS` into a member.
 - Move `ChunkData` statistics into a subclass.

## Fixes

 - Return only an appropriate exit code instead of showing a Python stacktrace in case of a broken pipe signal.
 - Avoid segfault when exporting the index for an empty, invalid gzip file.
 - Use `isatty` instead of poll with 100ms timeout to determine whether rapidgzip is piped to.
 - Fix build error on macOS when no wheel are available.
 - Many smaller adjustments to the profiling output with `--verbose`.
 - Do not terminate with an error when trying to unlock the GIL during Python finalization


# Version 0.12.1 built on 2024-01-08

## Fixes

 - Fix segmentation fault from rpmalloc because `rpmalloc_thread_initialize` was not called because
   the `static thread_local` global variable was not initialized at all because it was never used.


# Version 0.12.0 built on 2024-01-07

## Added

 - Add support for zlib and raw deflate files.
 - Add `--oss-attributions-yaml` command line option to generate a Conda-compatible `THIRDPARTY.yml`.

## API

 - Add `rapidgzip::VERSION` and other preprocessor version macros.
 - Add `setDeflateStreamCRC32s` and `addDeflateStreamCRC32` for providing CRC32s for raw deflate streams.
 - Add `RapidgzipFile.file_type` getter and `determineFileType` to get the determined file type.
   Currently, it is one of the values `GZIP`, `BGZF`, `ZLIB`, `DEFLATE`, `None`.


# Version 0.11.2 built on 2024-01-06

## Fixes

 - Fix segfault with rpmalloc when creating a ParallelGzipReader object on one thread and using it into
   another thread created manually in Python.


# Version 0.11.1 built on 2023-12-22

## Fixes

 - Fix possible GIL deadlock when calling many `RapidgzipFile` methods in quick succession.


# Version 0.11.0 built on 2023-12-19

## Added

 - Make parallel decompression work from stdin and other non-seekable inputs.
 - The setup.py file now comes with fine-granular dependency control via the environment variables:
   `RAPIDGZIP_BUILD_CXXOPT`, `RAPIDGZIP_BUILD_ISAL`, `RAPIDGZIP_BUILD_RPMALLOC`, `RAPIDGZIP_BUILD_ZLIB`,
   which can be set to `enable`, `disable`, or `system`. Cxxopts and zlib may not be disabled.
 - Include `indexed_bzip2` classes and CLI method with the rapidgzip Python module. This only adds ~15%
   space overhead to the precompiled binaries. This is a step towards one Python module offering seekable
   access to many different file formats.
 - Add import/export timings with `--verbose`.
 - Enable checksum verification by default. This adds ~5 % overhead.
 - Show a message about mismatching CRC32 during `--analyze` but try to read further.
 - Track symbol usage in windows and show information with `--analyze`.
 - Reorganize output of `--help`.
 - Add `--io-read-method=...` option, which can be set to `pread`, `sequential`, or `locked-read`.
   `--io-read-method=sequential` is advisable when decompressing from files on slow I/O devices such as HDDs.
 - Add `RapidgzipFile.peek` method.

## Performance

 - Clear seek points / windows when they are not needed, e.g., for one-pass sequential decompression
   without `--export-index`. This reduces the memory usage for decompressing `wikidata-20220103-all.json.gz`
   from 20 GB down to 10 GB and can have even larger effects for larger files.
 - Avoid doubling memory usage during index import and export by streaming the data directly to the output file
   without an internal copy.

## Fixes

 - Show better error message when quitting via SIGINT during a long-running read loop over a RapidgzipFile
   object working on a Python file object without using Python context managers / the with-statement.
   This leads to the decompression threads being left running and trying to acquire a non-existing GIL
   while Python interpreter finalization has already started.
 - Fix compile error when compiling with Conda because it defines `__linux__` while not having `F_GETPIPE_SZ`.
 - Improve error messages on EOF, for ISA-L and Zlib wrappers, and when file seeking fails.

## API

 - Change `size_t FileReader::size()` to `std::optional<size_t> FileReader::size()`


# Version 0.10.5 built on 2024-02-22

## Fixes

 - Fix segfault with rpmalloc when creating a ParallelGzipReader object on one thread and using it into
   another thread created manually in Python.
 - Fix possible GIL deadlock when calling many `RapidgzipFile` methods in quick succession.
 - Fix many issues with the GIL acquirement code logic.
 - Avoid segfault when exporting the index for an empty, invalid gzip file.
 - Use `isatty` instead of poll with 100ms timeout to determine whether rapidgzip is piped to.
 - Fix build error on macOS when no wheel are available.


# Version 0.10.4 built on 2023-11-25

## Fixes

 - Do not warn about the output file when it is not used.
 - The encoded end offset check on the chunk fired erroneously when decoding with an imported index.
 - Do not needlessly decompress for these command line combinations:
   - `rapidgzip --import-index <index> --export-index <index>`, which currently can be used to check and
     copy an index and in the future can be used to convert index formats.
   - `rapidgzip --import-index <index> --count`, which can be used to quickly print the decompressed file size.
 - The combination `--export-index -d` did not work.
 - Size formatting with units did drop any parts larger than 1 TiB.
 - Avoid error for `--count --no-verify -P 1`.
 - Avoid error for `--count --no-verify` for some files with large compression ratios.
 - Only print the decompression duration and bandwidth with `-v`.
 - Fix some typos.
 - Fix "Python memory allocator called without holding the GIL" consistency check with `PYTHONDEVMODE=1`
   or `python3 -X dev`. No actual race condition has been observed. All Python callbacks were always serialized.
 - Add tests for many command line parameter combinations.


# Version 0.10.3 built on 2023-09-12

## Fixes

 - Do not build with ISA-L on aarch64 for now because it fails.


# Version 0.10.2 built on 2023-09-10

## Fixes

 - `fileno` did not work because of an inverted smart pointer check.


# Version 0.10.1 built on 2023-09-04

## Fixes

 - Avoid crash on some MSVC versions during building the project.
   This happened only for the Conda builds, not the Windows wheel builds.


# Version 0.10.0 built on 2023-09-03

## Performance

 - Several minuscule performance improvements in `BitReader` and `HuffmanCodingISAL`: +10 %

## Fixes

 - Make `export_index` work with non-seekable output Python file objects.
 - Add the `-mmacosx-version-min=10.14` compiler option on macOS to avoid build problems.
 - Avoid possible race condition when checking for markers in chunk data.


# Version 0.9.0 built on 2023-08-30

## Added

 - Support BGZI with `--import-index`. These can be created with `bgzip --reindex`.
 - Check against a wrong index being loaded.
 - Improve error messages when zlib or ISA-l wrappers are used.
 - New output with `--analyze`:
   - Show information about extra bytes written by pgzf, MiGz, QATzip, pgzip/mgzip, bgzip, dictzip.
   - Print stream size statistics.
   - Print out the position after the block header, i.e., the begin of the Huffman data.
 - New profiling output with `--verbose`:
   - Decompression durations split by ISA-l, zlib, rapidgzip internal
   - The number of block offsets found with a block finder, from which decoding failed inside a chunk

## Performance

 - Avoid allocations by replacing the markers in-place by reinterpreting the buffer: Silesia +11 %, FASTQ +36 %.
 - Allocate fixed 128 KiB chunks instead of one allocation per deflate block: FASTQ +3 %.
 - Use ISA-L, if available, with -P 1: +110%.
 - Use ISA-L when a window has become resolve inside a chunk: Silesia +12%, Random Base64 +70%.
 - Use ISA-l Huffman decoder for literal/length alphabet in internal decoder: +20-40%.

## Fixes

 - Only show informational message about internal chunk fetcher with `--verbose`.
 - Smaller fixes in the inflate wrappers.
 - Check against zero-length end-of-block symbol.
 - Reintroduce the error detection for distance code counts equal to 31 or 32, which was removed in 0.5.0.

## API

 - Add stopping points to inflate wrappers.
 - `SharedFileReader`: Decouple statistics recording from printing.
 - Add `VectorView` constructor taking a start and end pointer.
 - Remove unused `deflate::Block::window` method.
 - Make `SHOW_PROFILE` a simple bool member instead of template parameter.
 - Make `DecodedData::data` private and a vector of views to actual buffers.
 - Add `BufferViewFileReader(void*, size_t)` overload
 - Remove unused argument to `ParallelGzipReader::maxDecompressedChunkSize()` getter.


# Version 0.8.1 built on 2023-08-04

## Fixes

 - The CRC32 checksum was not correctly read from the footer when ISA-l was used.
 - Limit the minimum chunk size to 8 KiB, especially forbid a chunk size of 0, which lead to an infinite loop.


# Version 0.8.0 built on 2023-08-04

## Performance

 - Use ISA-l instead of zlib when decompressing with an existing index: 5.5 GB/s -> 8.0 GB/s.
   Probably even better speedups depending on the file and number of available cores.
   In single-threaded benchmarks, ISA-l often outperformed zlib by factor 2 to 3.
 - Use ISA-l for BGZF files (.bgz) even when an index is not available: 3.5 GB/s -> 8.0 GB/s.
   This obviates the need for indexes for BGZF files.


# Version 0.7.1 built on 2023-08-01

## Fixes

 - Add exception for some BGZF files when decompressing when with an imported index.
   Bug got introduced in 0.5.0 when adding additional seek points for one heuristically chosen large chunk.

## Performance

 - Avoid overallocations when decompressing BGZF files with an imported index: 3.5 GB/s -> 5.8 GB/s.

# Version 0.7.0 built on 2023-06-04

 - Rename pragzip to rapidgzip. See the About section in the Readme for some background.


# Version 0.6.0 built on 2023-05-30

## Added

 - Add `--verify` option to turn on CRC32 computation. The overhead is ~6% thanks to parallelizing the CRC32
   computation and thanks to using the the slice-by-n implementation where n is a template argument and n=16
   was found to be optimal. It achieves 4.5 GB/s on a single core and parallelizes almost trivially thanks to
   `crc32_combine`.
 - Add Open-Source Software attributions listing option with: `--oss-attributions`.
 - Profile the time spent allocating and copying when using the `--verbose` argument.
 - Show `SharedFileReader` statistics with `--verbose`.
 - `pragzip --analyze`:
   - Print code length histograms for the whole file for all three alphabets.
   - Print gzip footer information.

## Performance

 - Reduce the maximum memory usage by 50x by limiting the chunk size. This means that for compression ratios
   larger than 20x, the decompression gets effectively serialized. However, in cases of very large compression ratios,
   the optimizations below will reach much faster speeds to offset this slowdown.
   This puts the estimate for the maximum memory usage during decompression using `pragzip` at:
   - `chunk size * ( prefetch count + resident cache size ) * maximum compression ratio * parallelization`
   - `= 4 MiB * ( 2 * parallelization + 1 ) * 20 * parallelization`
   - `≈ 160 MiB * parallelization`
 - Inflate:
   - Use `memset` and `memcpy` to resolve backreferences. This speeds up performance for files consisting only
     of zeros from 360 MB/s -> 1940 MB/s. The performance on the Silesia dataset is: 201 MB/s -> 250 MB/s.
   - Check for rare output buffer wrap-around once per back-reference. Performance with Silesia: 182 -> 201 MB/s.
 - Block fetcher:
   - Avoid waiting for non-existing blocks.
   - Avoid prefetching the same failed offsets multiple times.
 - Stop looking for blocks after 512 KiB to avoid slower than single-core performance for BGZF-like files:
   100 -> 230 MB/s.
 - Use rpmalloc to increase multi-threaded malloc performance: 2.5 GB/s -> 3.1 GBs.
   The performance benefits are less noticeable for files larger than ~10 GB.
 - Add `verbose` option to the Python interfaces `open` and `PragzipFile`.
 - Spawn threads only when needed instead of all at once at startup.
 - Reduce memory usage by not storing the decompressed result when using only `--count`.

## Fixes

 - Quit on SIGINT (Ctrl+C) while using the CLI via Python with the `--analyze` argument.
 - Do not throw an EOF exception for an empty gzip-compressed file when decompression in parallel.
 - Show default chunk size in pragzip CLI help.
 - Do not open an output file when only `--export-index` but `-d` has not been specified.
 - Improve error message for non-existing or empty input file.
 - Fix `tellCompressed` to return the file size instead of 0 at the end of the file.
 - Profiling output displayed with `--verbose`:
   - Also count decode time of blocks that do not need marker replacement.
   - Seek back/forward statistics were wrong when using `pread`.
   - Fix cache misses metric.
 - Avoid `std::regex` initialization during `dlopen`.

## API

 - Remove `pragzip/blockfinder/Combined.hpp` and `pragzip/blockfinder/Skipping.hpp`
 - Move `ZlibDeflateWrapper` into `pragzip/zlib.hpp`.
 - Move `DecodedDataView` into `pragzip/DecodedDataView.hpp`.
 - Return smart pointer on `FileReader::clone`.
 - Add `BlockFinderInterface`, which all block finders derive from and implement.
 - Add `FileReaderStream`, which is a thin wrapper around a given `FileReader`, including
   `ParallelGzipReader`, and adapts the `std::istream` interface so that it can be used interchangibly
   to `std::ifstream` and others in C++ code.


# Version 0.5.0 built on 2023-01-29

## Added

 - Add `--count-lines` option to count lines faster by avoiding to pipe to `wc -l`.
 - Add a performance profile and index seek point statistics when using the `--verbose` option.
 - By default, start only as many threads as there are cores pragzip has affinity for.
 - The seek points are now chosen such that the decompressed size instead of the compressed size
   approximates the chunk size. This behavior is similar to `indexed_gzip` and improves decompression
   speed and memory usage when loading such an optimized index.
 - Add `--export-index` and `--import-index` options. Using an existing index is not only faster
   but also reduces the maximum memory consumption significantly.

## Performance

 - Uncompressed block finder
   - Avoid full seeks: 90 -> 380 MB/s.
   - Keep track of buffer outside of BitReader: 380 -> 430 MB/s.
 - Marker replacement:
   - Reduce branches: 800 -> 1400 MB/s (8-bit output bandwidth).
   - Parallelize marker replacement to scale parallel decompression for files with lots of
     LZSS backward pointers like the Silesia corpus to 16 cores and more.
 - File writing:
   - Use vectorized I/O output to avoid many small calls of 30 KiB: 2.3 GB/s -> 2.4 GB/s.
   - Reuse an existing output file to avoid costly fallocate: 2.4 GB/s -> 3.0 GB/s.

## Fixes

 - Quit when SIGINT has been sent while using the CLI via Python.
 - Also print errno as string when I/O write fails.
 - Fix erroneous error detection for distance code counts equal to 31 or 32.
   I was not able to find or reproduce a gzip file with those code counts but in principle they are possible.
 - Avoid crash when using `--analyze` to print alphabet information for uncompressed blocks.
 - Ensure sure that Huffman coding structures can be reused after `initializeFromLengths`.
 - Improve error message for truncated gzip files.
 - Remove special case handling for `/dev/null`.
 - Fix multi-stream gzip file support by reading over gzip footers when decoding with zlib.

## API

 - Renamings:
   - `GzipBlockFetcher` -> `GzipChunkFetcher`
   - `ThreadPool::submitTask` -> `ThreadPool::submit`
   - `FetchNext` -> `FetchNextFixed`
   - `FetchNextSmart` -> `FetchNextAdaptive`
   - `FetchNextMulti` -> `FetchMultiStream`
 - Call write callback with full results instead of once per contiguous memory chunk.
 - Make the `SharedFileReader` constructor with unclear ownership semantic private.
 - Add thread pool priorities.


# Version 0.4.0 built on 2022-11-10

## API

 - Move ParallelGzipReader into pragzip namespace.

## Added

 - Show compression type and ratio statistics with pragzip --analyze.

## Fixes

 - An empty precode alphabet was not detected as a faulty block and threw instead.
 - There was an erroneous exception for a block with a single symbol with code length 1.
 - Bit-reversing was bugged in rare if not impossible instance.
 - When piping the output, splicing might have resulted in invalid data in some circumstances.

## Performance

 - Decoding of uncompressed blocks:
   - Unaligned uncompressed blocks are now correctly found.
   - Speed up deflate loop by unrolling and avoid calls to std::fread and BitReader::read.
   - Fall back to memcpy for uncompressed blocks.
   - Use pread to reduce locks inside SharedFileReader for better uncompressed bandwidth: 2.7 -> 3.6 GB/s.
 - Block finder:
   - Make checkPrecode constexpr.
   - Keep 74 bits of buffer to avoid seek backs.
   - Use different Huffman Coding implementation for precode.
   - Replace uninteresting zero-count with non-zero-count in histogram.
   - Only do Huffman Coding checks and skip preparations for actual decoding.
   - Compile-time calculate all possible 1526 precode Huffman Codings.
 - Huffman decoding:
   - Only create Huffman LUT up to max code length in block: 140 -> 195 MB/s.
   - Decrease double-cached Huffman LUT cached bits: 165 -> 255 MB/s.
 - Reduce memory usage:
   - Remove previous blocks from the cache for the purely-sequential use case.
   - Avoid std::vector overallocations and reallocations.
 - Prefetching did not work when calling ParallelGzipReader::read multiple times with small values.
 - Uncompressed blocks directly before untilOffset were not found.
 - The very last block was never prefetched.
 - Avoid cache eviction when key already exists and value can be replaced.
 - Prefetch more blocks while waiting for non-completed block.
 - Actively avoiding cache-spilling was dysfunctional.


# Version 0.3.0 built on 2022-08-24

## Added

 - --count-lines option to integrate the functionality of wc -l and saving time piping the output.
 - --chunk-size option to adjust the work chunk size. The preset of 2 MiB should be good enough in
   most cases but it still might be desirable to reduce the memory usage by reducing the chunk size at
   the cost of speed.
 - Default argument (SEEK_SET) for "whence" in the "seek" method.

## Performance

 - Alternatingly look for uncompressed blocks and deflate blocks in small chunks.
   This fixes a performance degradation from 1.4 GB/s down to 1.0 GB/s introduced in 0.2.2.
 - Speed up the compressed block finder by more than 3x.
 - Double the work chunk size to increase speed by ~30%.
 - Increase speed when piping by ~10% by using vmsplice on Linux.

# Version 0.2.2 built on 2022-08-09

## Fixes

 - Specified output (-o) was ignored by pragzip.
 - An exception was thrown for very large runs of incompressible data because only 2147479552 B could be written.

## Performance

 - Improve performance for gzips with uncompressed deflate blocks.


# Version 0.2.1 built on 2022-08-07

## Fixes

 - An exception was thrown when decoding gzips with many consecutive uncompressed blocks in parallel.


# Version 0.2.0 built on 2022-08-05

## Added

 - Add support for parallel decompression of arbitrary gzip files even without a preexisting gzip index file.
 - Add the command line tool pragzip to be used as a replacement for decoding gzip files.
 - Add import_index method that actually works to Python interface.
 - Add export_index method to Python interface.

## Fixes

 - Fix segmentation fault when the IndexedBzip2File constructor fails.
 - Fix exception being thrown when gzip index is empty, which might happen for gzip smaller than approximately 64 KiB.
 - Fix race condition for fixed Huffmann coding deflate blocks.

## Performance

 - Speed up parallel decoding by reducing prefetcher cache pollution.
 - Speed up parallel decompression speed by 2x when the gzip index is known by using zlib in that case.


# Version 0.1.0 built on 2022-07-03

 - First prototype to be used with ratarmount for significant speedups over indexed_gzip.
