
# Version 0.3.0 built on 2022-08-24

 - Add --count-lines option to integrate the functionality of wc -l and saving time piping the output.
 - Add --chunk-size option to adjust the work chunk size. The preset of 2 MiB should be good enough in
   most cases but it still might be desirable to reduce the memory usage by reducing the chunk size at
   the cost of speed.
 - Add default argument (SEEK_SET) for "whence" in the "seek" method.
 - Alternatingly look for uncompressed blocks and deflate blocks in small chunks.
   This fixes a performance degradation from 1.4 GB/s down to 1.0 GB/s introduced in 0.2.2.
 - Speed up the compressed block finder by more than 3x.
 - Double the work chunk size to increase speed by ~30%.
 - Increase speed when piping by ~10% by using vmsplice on Linux.

# Version 0.2.2 built on 2022-08-09

 - Fix specified output (-o) was ignored by pragzip.
 - Fix exception thrown for very large runs of incompressible data because only 2147479552 B could be written.
 - Improve performance for gzips with uncompressed deflate blocks.

# Version 0.2.1 built on 2022-08-07

 - Fix exception when decoding gzips with many consecutive uncompressed blocks in parallel.

# Version 0.2.0 built on 2022-08-05

 - Add support for parallel decompression of arbitrary gzip files even without a preexisting gzip index file.
 - Add the command line tool pragzip to be used as a replacement for decoding gzip files.
 - Add import_index method that actually works.
 - Add export_index method
 - Speed up parallel decoding by reducing prefetcher cache pollution.
 - Speed up parallel decompression speed by 2x when the gzip index is known by using zlib in that case.
 - Fix segmentation fault when the IndexedBzip2File constructor fails.
 - Fix exception being thrown when gzip index is empty, which might happen for gzip smaller than approximately 64 KiB.
 - Fix race condition for fixed Huffmann coding deflate blocks.

# Version 0.1.0 built on 2022-07-03

 - First prototype to be used with ratarmount for significant speedups over indexed_gzip.
