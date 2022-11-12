
# Version 1.4.0 built on 2022-11-12

 - Add command line CLI entrypoint ibzip2 to be used as a standalone parallel bzip2 decoder as replacement for bzip2.
 - Add a BZ2Reader::read overload that takes a callback functor.
 - Add io.SEEK_SET as default value for the "seek" method's "whence" argument.
 - Fix segmentation fault when IndexedBzip2File failed to construct, e.g., because of a wrong argument type given.
 - Speed up magic bit string finder 9-fold.
 - Refactored a lot of code in order to build pragzip on top of the indexed_bzip2 backend.
 - Migrate to pyproject.toml installation method.

# Version 1.3.1 built on 2021-12-23

 - Fix logic_error exception for certain files when using the parallel decoder.

# Version 1.3.0 built on 2021-09-15

 - Add support and wheels for macOS and Windows additionally to Linux.
 - Build and upload Conda packages for Linux, Windows, and macOS.
 - Add 'open' method for compatibility to bzip2, gzip, and other similar modules.
 - Add support for pure Python file-like objects without a fileno like BytesIO.

# Version 1.2.0 built on 2021-06-27

 - Fix corrupted data for concatenated bzip2 streams in one bzip2 file in serial BZ2Reader.
 - Add C++17 threaded parallel bzip2 block decoder which is used when constructing
   'IndexedBzip2File' with argument 'parallelization' != 1.
 - Provide readline, readlines, peek methods by inheriting from BufferedReader.
 - Add available_block_offsets, block_offsets_complete, and size method.
 - Do not build index when seeking to current position.

# Version 1.1.2 built on 2020-07-04

 - Fix tell() method

# Version 1.1.1 built on 2020-04-12

 - Refactor code
 - Fix endless loop caused by failing stream CRC check after using set_block_offsets and seeking to the end

# Version 1.1.0 built on 2019-12-08

 - Fix broken set_block_offsets, block_offsets
 - Add forgotten tell_compressed

# Version 1.0.0 built on 2019-12-08

 - First hopefully stable version
