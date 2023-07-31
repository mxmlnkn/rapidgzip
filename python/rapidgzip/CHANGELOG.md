
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
   The performance benefits are less noticable for files larger than ~10 GB.
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
 - Actively avoiding cache-spilling was disfunctional.


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
