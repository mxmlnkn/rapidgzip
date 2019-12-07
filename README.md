# indexed_bzip2

This module provides an IndexedBzip2File class, which can be used to seek inside bzip2 files without having to decompress them first.
It's based on an improved version of the bzip2 decoder [bzcat](https://github.com/landley/toybox/blob/c77b66455762f42bb824c1aa8cc60e7f4d44bdab/toys/other/bzcat.c) from [toybox](https://landley.net/code/toybox/), which was refactored and extended to be able to export an import bzip2 block offsets and seek to them.
Seeking inside a block is only emulated, so IndexedBzip2File will only speed up seeking when there are more than one blocks, which should almost always be the cause for archives larger than 1 MB.


# Installation

You can simply install it from PyPI:
```
pip install indexed_bzip3
```

# Usage

## Example 1

```python3
from indexed_bzip2 import IndexedBzip2File

file = IndexedBzip2File( "example.bz2" )

# You can now use it like a normal file
file.seek( 123 )
data = file.read( 100 )
```

The first call to seek will ensure that the block offset list is complete and therefore might create them first.
Because of this the first call to seek might take a while.

## Example 2

The creation of the list of bzip2 blocks can take a while because it has to decode the bzip2 file completely.
To avoid this setup when opening a bzip2 file, the block offset list can be exported and imported.

```python3
from indexed_bzip2 import IndexedBzip2File

file = IndexedBzip2File( "example.bz2" )
blockOffsets = file.blockOffsets() # can take a while
file.close()

file2 = IndexedBzip2File( "example.bz2" )
blockOffsets = file2.setBlockOffsets( blockOffsets ) # should be fast
file2.seek( 123 )
data = file2.read( 100 )
file2.close()
```
