
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
