
# Version 1.6.0 built on 2024-02-22

 - Version 1.5.0 was based on the same code base as rapidgzip 0.6.0.
   This update now uses the same code base as the planned rapidgzip 0.13.0 and shares many of the fixes.
 - Fix a segmentation fault that only happens with Python 3.12 when recursively mounting nested bzip2
   files with ratarmount.
 - Suppress `Python memory allocator called without holding the GIL` errors with `PYTHONDEVMODE=1`.
 - Reuse the cached Huffman decoder from rapidgzip for ~25% speedup with a single core and ~10% speedup
   with full parallelization over all virtual cores. The parallel version profits less because SMT
   already did a good job hiding the slowness of the decoder.

# Version 1.5.0 built on 2023-05-29

 - Fix wrong version number returned by `ibzip2 --version`.
 - Use the number of cores the process has affinity for as default parallelism.
 - Write verbose output enabled with the `-v` option to stderr instead of stdout.
 - Allow decompressing to `/dev/null` with ibzip2 for benchmarking purposes.
 - Add `--oss-attributions` to `ibzip2`.
 - Quit when SIGINT has been sent when called from Python.
 - Spawn threads only when needed instead of all at once at startup.
 - Avoid `std::regex` initialization during dlopen.

# Version 1.4.0 built on 2022-11-12

 - Add command line CLI entrypoint ibzip2 to be used as a standalone parallel bzip2 decoder as replacement for bzip2.
 - Add a `BZ2Reader::read` overload that takes a callback functor.
 - Add `io.SEEK_SET` as default value for the `seek` method's `whence` argument.
 - Fix segmentation fault when IndexedBzip2File failed to construct, e.g., because of a wrong argument type given.
 - Speed up magic bit string finder 9-fold.
 - Refactored a lot of code in order to build pragzip on top of the indexed_bzip2 backend.
 - Migrate to pyproject.toml installation method.

# Version 1.3.1 built on 2021-12-23

 - Fix `std::logic_error` exception for certain files when using the parallel decoder.

# Version 1.3.0 built on 2021-09-15

 - Add support and wheels for macOS and Windows additionally to Linux.
 - Build and upload Conda packages for Linux, Windows, and macOS.
 - Add `open` method for compatibility to `bzip2`, `gzip`, and other similar modules.
 - Add support for pure Python file-like objects without a fileno like BytesIO.

# Version 1.2.0 built on 2021-06-27

 - Fix corrupted data for concatenated bzip2 streams in one bzip2 file in serial BZ2Reader.
 - Add C++17 threaded parallel bzip2 block decoder which is used when constructing
   `IndexedBzip2File` with argument `parallelization` != 1.
 - Provide `readline`, `readlines`, `peek` methods by inheriting from `BufferedReader`.
 - Add `available_block_offsets`, `block_offsets_complete`, and `size` method.
 - Do not build index when seeking to current position.

# Version 1.1.2 built on 2020-07-04

 - Fix `tell` method.

# Version 1.1.1 built on 2020-04-12

 - Refactor code.
 - Fix endless loop caused by failing stream CRC check after using `set_block_offsets` and seeking to the end.

# Version 1.1.0 built on 2019-12-08

 - Fix broken `set_block_offsets, `block_offsets`.
 - Add forgotten `tell_compressed`.

# Version 1.0.0 built on 2019-12-08

 - First hopefully stable version.
