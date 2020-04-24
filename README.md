# indexed_bzip2

[![PyPI version](https://badge.fury.io/py/indexed-bzip2.svg)](https://badge.fury.io/py/indexed-bzip2)
[![Downloads](https://pepy.tech/badge/indexed-bzip2/month)](https://pepy.tech/project/indexed-bzip2/month)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](http://opensource.org/licenses/MIT)
[![Build Status](https://travis-ci.org/mxmlnkn/indexed_bzip2.svg?branch=master)](https://travis-ci.com/mxmlnkn/indexed_bzip2)

This module provides an IndexedBzip2File class, which can be used to seek inside bzip2 files without having to decompress them first.
It's based on an improved version of the bzip2 decoder [bzcat](https://github.com/landley/toybox/blob/c77b66455762f42bb824c1aa8cc60e7f4d44bdab/toys/other/bzcat.c) from [toybox](https://landley.net/code/toybox/), which was refactored and extended to be able to export and import bzip2 block offsets and seek to them.
Seeking inside a block is only emulated, so IndexedBzip2File will only speed up seeking when there are more than one blocks, which should almost always be the cause for archives larger than 1 MB.


# Installation

You can simply install it from PyPI:
```
pip install indexed_bzip2
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
blockOffsets = file.block_offsets() # can take a while
file.close()

file2 = IndexedBzip2File( "example.bz2" )
blockOffsets = file2.set_block_offsets( blockOffsets ) # should be fast
file2.seek( 123 )
data = file2.read( 100 )
file2.close()
```


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
