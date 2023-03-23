<div align="center">

# indexed_bzip2

[![PyPI version](https://badge.fury.io/py/indexed-bzip2.svg)](https://badge.fury.io/py/indexed-bzip2)
[![Python Version](https://img.shields.io/pypi/pyversions/indexed_bzip2)](https://pypi.org/project/indexed-bzip2/)
[![PyPI Platforms](https://img.shields.io/badge/pypi-linux%20%7C%20macOS%20%7C%20Windows-brightgreen)](https://pypi.org/project/indexed-bzip2/)
[![Downloads](https://pepy.tech/badge/indexed-bzip2/month)](https://pepy.tech/project/indexed-bzip2)
<br>
[![Conda Platforms](https://img.shields.io/conda/v/conda-forge/indexed_bzip2?color=brightgreen)](https://anaconda.org/conda-forge/indexed_bzip2)
[![Conda Platforms](https://img.shields.io/conda/pn/conda-forge/indexed_bzip2?color=brightgreen)](https://anaconda.org/conda-forge/indexed_bzip2)
<br>
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](http://opensource.org/licenses/MIT)
[![Build Status](https://github.com/mxmlnkn/indexed_bzip2/workflows/tests/badge.svg)](https://github.com/mxmlnkn/indexed_bzip2/actions)
[![codecov](https://codecov.io/gh/mxmlnkn/indexed_bzip2/branch/master/graph/badge.svg?token=94ZD4UTZQW)](https://codecov.io/gh/mxmlnkn/indexed_bzip2)
![C++17](https://img.shields.io/badge/C++-17-blue.svg)

</div>

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


# Table of Contents

1. [Performance comparison with bz2 module](#performance-comparison-with-bz2-module)
2. [Installation](#installation)
3. [Usage](#usage)
   1. [Python Library](#python-library)
   2. [Via Ratarmount](#via-ratarmount)
   3. [Command Line Tool](#command-line-tool)
   4. [C++ Library](#c-library)
4. [Internal Architecture](#internal-architecture)
5. [Tracing the Decoder](#tracing-the-decoder)


# Performance comparison with bz2 module

Results for an AMD Ryzen 3900X 12-core (24 virtual cores) processor and with `bz2FilePath='CTU-13-Dataset.tar.bz2'`, which is a 2GB bz2 compressed archive.

| Module                                  | Runtime / s | Bandwidth / (MB/s) | Speedup |
|-----------------------------------------|-------------|--------------------|---------|
| bz2                                     | 392         |  5.1               | 1       |
| indexed_bzip2 with parallelization = 0  |  62         | 32.3               | 6.3     |
| indexed_bzip2 with parallelization = 1  | 559         |  3.6               | 0.7     |
| indexed_bzip2 with parallelization = 2  | 321         |  6.2               | 1.2     |
| indexed_bzip2 with parallelization = 6  | 116         | 17.2               | 3.4     |
| indexed_bzip2 with parallelization = 12 |  72         | 27.8               | 5.4     |
| indexed_bzip2 with parallelization = 24 |  64         | 31.5               | 6.2     |
| indexed_bzip2 with parallelization = 32 |  66         | 30.1               | 5.9     |

The speedup of `indexed_bzip2` over the `bz2` module with `parallelization = 0` is **6**.
When using only one core, `indexed_bzip2` is unfortunately slower by (559-392)/392 = 42%.

These are simple timing tests for reading all the contents of a bzip2 file sequentially.

<details>

```python3
import bz2
import os
import time

fileSize = os.stat(bz2FilePath).st_size

with bz2.open(bz2FilePath) as file:
    t0 = time.time()
    while file.read(512*1024):
        pass
    bz2Duration = time.time() - t0
    print(f"Decoded file in {bz2Duration:.0f}s, bandwidth: {fileSize / bz2Duration / 1e6:.1f} MB/s")
```

The usage of indexed_bzip2 is slightly different:

```python3
import indexed_bzip2
import time

# parallelization = 0 means that it is automatically using all available cores.
for parallelization in [0, 1, 2, 6, 12, 24, 32]:
    with indexed_bzip2.IndexedBzip2File(bz2FilePath, parallelization = parallelization) as file:
        t0 = time.time()
        # Unfortunately, the chunk size is very performance critical! It might depend on the cache size.
        while file.read(512*1024):
            pass
        ibz2Duration = time.time() - t0
        print(f"Decoded file in {ibz2Duration:.0f}s"
              f", bandwidth: {fileSize / ibz2Duration / 1e6:.1f} MB/s"
              f", speedup: {bz2Duration/ibz2Duration:.1f}")
```

</details>


# Installation

You can simply install it from PyPI:

```bash
python3 -m pip install --upgrade pip  # Recommended for newer manylinux wheels
python3 -m pip install indexed_bzip2
```

To install with conda:

```bash
conda install -c conda-forge indexed_bzip2
```

The latest unreleased development version can be tested out with:

```bash
python3 -m pip install --force-reinstall 'git+https://github.com/mxmlnkn/indexed_bzip2.git@master#egginfo=indexed_bzip2&subdirectory=python/indexed_bzip2'
```

And to build locally, you can use `build` and install the wheel:

```bash
cd python/indexed_bzip2
rm -rf dist
python3 -m build .
python3 -m pip install --force-reinstall --user dist/*.whl
```


# Usage

## Python Library

### Simple open, seek, read, and close

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


### Use with context manager

```python3
import os
import indexed_bzip2 as ibz2

with ibz2.open( "example.bz2", parallelization = os.cpu_count() ) as file:
    file.seek( 123 )
    data = file.read( 100 )
```


### Storing and loading the block offset map

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


### Open a pure Python file-like object for indexed reading

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


## Via Ratarmount

Because `indexed_bzip2` is used by default as a backend in ratarmount, you can use [ratarmount](https://github.com/mxmlnkn/ratarmount) to mount single bzip2 files easily.
Furthermore, since ratarmount 0.11.0, parallelization is the default and does not have to be specified explicitly with `-P`.

```bash
base64 /dev/urandom | head -c $(( 512 * 1024 * 1024 )) | bzip2 > sample.bz2
# Serial decoding: 23 s
time bzip2 -c -d sample.bz2 | wc -c

python3 -m pip install --user ratarmount
ratarmount sample.bz2 mounted

# Parallel decoding: 2 s
time cat mounted/sample | wc -c

# Random seeking to the middle of the file and reading 1 MiB: 0.132 s
time dd if=mounted/sample bs=$(( 1024 * 1024 )) \
       iflag=skip_bytes,count_bytes skip=$(( 256 * 1024 * 1024 )) count=$(( 1024 * 1024 )) | wc -c
```


## Command Line Tool


A rudimentary command line tool exists but is not yet shipped with the Python module and instead has to be built from source.

```c++
git clone https://github.com/mxmlnkn/indexed_bzip2.git
cd indexed_bzip2
mkdir build
cd build
cmake ..
cmake --build . -- ibzip2
```

The finished `ibzip2` binary is created in the `tools` subfolder.
To install it, it can be copied, e.g., to `/usr/local/bin` or anywhere else as long as it is available in your `PATH` variable.
The command line options are similar to those of the existing `bzip2` tool.

```bash
src/tools/ibzip2 --help

# Parallel decoding: 1.7 s
time src/tools/ibzip2 -d -c -P 0 sample.bz2 | wc -c

# Serial decoding: 22 s
time bzip2 -d -c sample.bz2 | wc -c
```


# C++ library

Because it is written in C++, it can of course also be used as a C++ library.
In order to make heavy use of templates and to simplify compiling with Python `setuptools`, it is completely header-only so that integration it into another project should be easy.
The license is also permissive enough for most use cases.

I currently did not yet test integrating it into other projects other than simply manually copying the source in `core` and `indexed_bzip2`.
If you have suggestions and wishes like support with CMake or Conan, please open an issue.


# Internal Architecture

The parallelization of the bzip2 decoder and adding support to read from Python file-like objects required a lot of work to design an architecture which works and can be reasoned about.
An earlier architecture was discarded because it became to monolithic.
That discarded one was able to even work with piped non-seekable input, with which the current parallel architecture does not work with yet.
The serial `BZ2Reader` still exists but is not shown in the class diagram because it is deprecated and will be removed some time in the future after the `ParallelBZ2Reader` has proven itself.
Click [here](https://raw.githubusercontent.com/mxmlnkn/indexed_bzip2/master/results/design/ParallelBZ2Reader.png) or the image to get a larger image and [here](https://raw.githubusercontent.com/mxmlnkn/indexed_bzip2/master/results/design/ParallelBZ2Reader.svg) to see an SVG version.

[![Class Diagram for ParallelBZ2Reader](https://raw.githubusercontent.com/mxmlnkn/indexed_bzip2/master/results/design/ParallelBZ2Reader.png)](https://raw.githubusercontent.com/mxmlnkn/indexed_bzip2/master/results/design/ParallelBZ2Reader.png)

# Tracing the Decoder

Performance profiling and tracing is done with [Score-P](https://www.vi-hps.org/projects/score-p/) for instrumentation and [Vampir](https://vampir.eu/) for visualization.
This is one way, you could install Score-P with most of the functionalities on Ubuntu 22.04.

```bash
sudo apt-get install libopenmpi-dev openmpi-bin gcc-11-plugin-dev llvm-dev libclang-dev libunwind-dev \
                     libopen-trace-format-dev otf-trace libpapi-dev

# Install Score-P (to /opt/scorep)
SCOREP_VERSION=8.0
wget "https://perftools.pages.jsc.fz-juelich.de/cicd/scorep/tags/scorep-${SCOREP_VERSION}/scorep-${SCOREP_VERSION}.tar.gz"
tar -xf "scorep-${SCOREP_VERSION}.tar.gz"
cd "scorep-${SCOREP_VERSION}"
./configure --with-mpi=openmpi --enable-shared --without-llvm --without-shmem --without-cubelib --prefix="/opt/scorep-${SCOREP_VERSION}"
make -j $( nproc )
make install

# Add /opt/scorep to your path variables on shell start
cat <<EOF >> ~/.bashrc
if test -d /opt/scorep; then
    export SCOREP_ROOT=/opt/scorep
    export PATH=$SCOREP_ROOT/bin:$PATH
    export LD_LIBRARY_PATH=$SCOREP_ROOT/lib:$LD_LIBRARY_PATH
fi
EOF

echo -1 | sudo tee /proc/sys/kernel/perf_event_paranoid

# Check whether it works
scorep --version
scorep-info config-summary

# Actually do the tracing
cd tools
bash trace.sh
```
