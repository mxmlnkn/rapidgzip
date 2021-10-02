# indexed_bzip2

[![PyPI version](https://badge.fury.io/py/indexed-bzip2.svg)](https://badge.fury.io/py/indexed-bzip2)
[![Python Version](https://img.shields.io/pypi/pyversions/indexed_bzip2)](https://pypi.org/project/indexed-bzip2/)
[![PyPI Platforms](https://img.shields.io/badge/pypi-linux%20%7C%20macOS%20%7C%20Windows-brightgreen)](https://pypi.org/project/indexed-bzip2/)
[![Conda Platforms](https://img.shields.io/conda/pn/mxmlnkn/indexed_bzip2?color=brightgreen&label=conda)](https://anaconda.org/mxmlnkn/indexed_bzip2)
[![Downloads](https://pepy.tech/badge/indexed-bzip2/month)](https://pepy.tech/project/indexed-bzip2)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](http://opensource.org/licenses/MIT)
[![Build Status](https://github.com/mxmlnkn/indexed_bzip2/workflows/tests/badge.svg)](https://github.com/mxmlnkn/indexed_bzip2/actions)
[![codecov](https://codecov.io/gh/mxmlnkn/indexed_bzip2/branch/master/graph/badge.svg?token=94ZD4UTZQW)](https://codecov.io/gh/mxmlnkn/indexed_bzip2)
![C++17](https://img.shields.io/badge/C++-17-blue.svg?style=flat-square)


This module provides an IndexedBzip2File class, which can be used to seek inside bzip2 files without having to decompress them first.
Alternatively, you can use this simply as a **parallelized** bzip2 decoder as a replacement for Python's builtin `bz2` module in order to fully utilize all your cores.

On a 12-core processor, this can lead to a speedup of **6** over Python's `bz2` module, see [this](#comparison-with-bz2-module) example.
Note that without parallelization, `indexed_bzip2` is unfortunately slower than Python's `bz2` module.
Therefore, it is not recommended when neither seeking nor parallelization is used!

The internals are based on an improved version of the bzip2 decoder [bzcat](https://github.com/landley/toybox/blob/c77b66455762f42bb824c1aa8cc60e7f4d44bdab/toys/other/bzcat.c) from [toybox](https://landley.net/code/toybox/), which was refactored and extended to be able to export and import bzip2 block offsets, seek to block offsets, and to add support for threaded parallel decoding of blocks.

Seeking inside a block is only emulated, so IndexedBzip2File will only speed up seeking when there are more than one block, which should almost always be the cause for archives larger than 1 MB.

Since version 1.2.0, parallel decoding of blocks is supported!
However, per default, the older serial implementation is used.
To use the parallel implementation you need to specify a `parallelization` argument other than 1 to `IndexedBzip2File`, see e.g. [this example](#comparison-with-bz2-module).


# Installation

You can simply install it from PyPI:

```
python3 -m pip install --upgrade pip  # Recommended for newer manylinux wheels
python3 -m pip install indexed_bzip2
```

# Usage Examples

## Simple open, seek, read, and close

```python3
from indexed_bzip2 import IndexedBzip2File

file = IndexedBzip2File( "example.bz2", parallelization = os.cpu_count() )

# You can now use it like a normal file
file.seek( 123 )
data = file.read( 100 )
file.close()
```

The first call to seek will ensure that the block offset list is complete and therefore might create them first.
Because of this the first call to seek might take a while.


## Use with context manager

```python3
import os
import indexed_bzip2 as ibz2

with ibz2.open( "example.bz2", parallelization = os.cpu_count() ) as file:
    file.seek( 123 )
    data = file.read( 100 )
```


## Storing and loading the block offset map

The creation of the list of bzip2 blocks can take a while because it has to decode the bzip2 file completely.
To avoid this setup when opening a bzip2 file, the block offset list can be exported and imported.

```python3
import indexed_bzip2 as ibz2
import pickle

# Calculate and save bzip2 block offsets
file = ibz2.open( "example.bz2", parallelization = os.cpu_count() )
block_offsets = file.block_offsets() # can take a while
# block_offsets is a simple dictionary where the keys are the bzip2 block offsets in bits(!)
# and the values are the corresponding offsets in the decoded data in bytes. E.g.:
# block_offsets = {32: 0, 14920: 4796}
with open( "offsets.dat", 'wb' ) as offsets_file:
    pickle.dump( block_offsets, offsets_file )
file.close()

# Load bzip2 block offsets for fast seeking
with open( "offsets.dat", 'rb' ) as offsets_file:
    block_offsets = pickle.load( offsets_file )
file2 = ibz2.open( "example.bz2", parallelization = os.cpu_count() )
file2.set_block_offsets( block_offsets ) # should be fast
file2.seek( 123 )
data = file2.read( 100 )
file2.close()
```


## Open a pure Python file-like object for indexed reading

```python3
import io
import os
import indexed_bzip2 as ibz2

with open( "example.bz2", 'rb' ) as file:
    in_memory_file = io.BytesIO( file.read() )

with ibz2.open( in_memory_file, parallelization = os.cpu_count() ) as file:
    file.seek( 123 )
    data = file.read( 100 )
```


## Comparison with bz2 module

These are simple timing tests for reading all the contents of a bzip2 file sequentially.

```python3
import bz2
import time

with bz2.open( bz2FilePath ) as file:
    t0 = time.time()
    while file.read( 4*1024*1024 ):
        pass
    t1 = time.time()
    print( f"Decoded file in {t1-t0}s" )
```

The usage of indexed_bzip2 is slightly different:

```python3
import indexed_bzip2
import time

# parallelization = 0 means that it is automatically using all available cores.
with indexed_bzip2.IndexedBzip2File( bz2FilePath, parallelization = 0 ) as file:
    t0 = time.time()
    while file.read( 4*1024*1024 ):
        pass
    t1 = time.time()
    print( f"Decoded file in {t1-t0}s" )
```

Results for an AMD Ryzen 3900X 12-core (24 virtual cores) processor and with `bz2FilePath=CTU-13-Dataset.tar.bz2`, which is a 2GB bz2 compressed archive.

| Module                                  | Runtime / s |
|-----------------------------------------|-------------|
| bz2                                     | 414         |
| indexed_bzip2 with parallelization = 0  | 69          |
| indexed_bzip2 with parallelization = 1  | 566         |
| indexed_bzip2 with parallelization = 2  | 315         |
| indexed_bzip2 with parallelization = 6  | 123         |
| indexed_bzip2 with parallelization = 12 | 79          |
| indexed_bzip2 with parallelization = 24 | 70          |
| indexed_bzip2 with parallelization = 32 | 69          |

The speedup between the `bz2` module and `indexed_bzip2` with `parallelization = 0` is 414/69 = **6**.
When using only one core, `indexed_bzip2` is slower by (566-414)/414 = 37%.

# Internal Architecture

The parallelization of the bzip2 decoder and adding support to read from Python file-like objects required a lot of work to design an architecture which works and can be reasoned about.
An earlier architecture was discarded because it became to monolithic.
That discarded one was able to even work with piped non-seekable input, with which the current parallel architecture does not work with yet.
The serial `BZ2Reader` still exists but is not shown in the class diagram because it is deprecated and will be removed some time in the future after the `ParallelBZ2Reader` has proven itself.
Click [here](results/design/ParallelBZ2Reader.png) or the image to get a larger image and [here](results/design/ParallelBZ2Reader.svg) to see an SVG version.

[![Class Diagram for ParallelBZ2Reader](results/design/ParallelBZ2Reader.png)](results/design/ParallelBZ2Reader.png)

# Tracing the decoder

Performance profiling and tracing is done with [Score-P](https://www.vi-hps.org/projects/score-p/) for instrumentation and [Vampir](https://vampir.eu/) for visualization.
This is one way, you could install Score-P with most of the functionalities on Debian 10.

```bash
# Install PAPI
wget http://icl.utk.edu/projects/papi/downloads/papi-5.7.0.tar.gz
tar -xf papi-5.7.0.tar.gz
cd papi-5.7.0/src
./configure
make -j
sudo make install

# Install Dependencies
sudo apt-get install libopenmpi-dev openmpi gcc-8-plugin-dev llvm-dev libclang-dev libunwind-dev libopen-trace-format-dev otf-trace

# Install Score-P (to /opt/scorep)
wget https://www.vi-hps.org/cms/upload/packages/scorep/scorep-6.0.tar.gz
tar -xf scorep-6.0.tar.gz
cd scorep-6.0
./configure --with-mpi=openmpi --enable-shared
make -j
make install

# Add /opt/scorep to your path variables on shell start
cat <<EOF >> ~/.bashrc
if test -d /opt/scorep; then
    export SCOREP_ROOT=/opt/scorep
    export PATH=$SCOREP_ROOT/bin:$PATH
    export LD_LIBRARY_PATH=$SCOREP_ROOT/lib:$LD_LIBRARY_PATH
fi
EOF

# Check whether it works
scorep --version
scorep-info config-summary

# Actually do the tracing
cd tools
bash trace.sh
```
