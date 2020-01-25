
# Version 1.1.3 built on 2020-10-xx

 - Provide readline, readlines, peek methods by inheriting from BufferedReader.
 - Add available_block_offsets, block_offsets_complete, and size method.
 - Make IndexedBzip2File work with non-seekable inputs like STDIN.
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
