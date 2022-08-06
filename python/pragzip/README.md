<p align="center"><a href=#><img src="https://raw.githubusercontent.com/mxmlnkn/indexed_bzip2/master/python/pragzip/pragzip.svg"/></a></p>

# Parallel Random Access Gzip (pragzip)

[![PyPI version](https://badge.fury.io/py/pragzip.svg)](https://badge.fury.io/py/pragzip)
[![Python Version](https://img.shields.io/pypi/pyversions/pragzip)](https://pypi.org/project/pragzip/)
[![PyPI Platforms](https://img.shields.io/badge/pypi-linux%20%7C%20macOS%20%7C%20Windows-brightgreen)](https://pypi.org/project/pragzip/)
[![Downloads](https://pepy.tech/badge/pragzip/month)](https://pepy.tech/project/pragzip)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](http://opensource.org/licenses/MIT)
[![Build Status](https://github.com/mxmlnkn/indexed_bzip2/workflows/tests/badge.svg)](https://github.com/mxmlnkn/pragzip/actions)
[![codecov](https://codecov.io/gh/mxmlnkn/indexed_bzip2/branch/master/graph/badge.svg?token=94ZD4UTZQW)](https://codecov.io/gh/mxmlnkn/pragzip)
![C++17](https://img.shields.io/badge/C++-17-blue.svg?style=flat-square)


This module provides a PragzipFile class, which can be used to seek inside gzip files without having to decompress them first.
Alternatively, you can use this simply as a **parallelized** gzip decoder as a replacement for Python's builtin `gzip` module in order to fully utilize all your cores.

The random seeking support is the same as provided by [indexed_gzip](https://github.com/pauldmccarthy/indexed_gzip) but further speedups are realized at the cost of higher memory usage thanks to a least-recently-used cache in combination with a parallelized prefetcher.


# Table of Contents

1. [Installation](#installation)
2. [Usage](#usage)
   1. [Command Line Tool](#command-line-tool)
   2. [Python Library](#python-library)
   3. [Via Ratarmount](#via-ratarmount)
   4. [C++ Library](#c-library)
3. [Performance comparison with gzip module](#performance-comparison-with-gzip-module)
4. [Internal Architecture](#internal-architecture)
5. [Tracing the Decoder](#tracing-the-decoder)


# Installation

You can simply install it from PyPI:

```
python3 -m pip install --upgrade pip  # Recommended for newer manylinux wheels
python3 -m pip install pragzip
```

The latest unreleased development version can be tested out with:

```bash
python3 -m pip install --force-reinstall 'git+https://github.com/mxmlnkn/indexed_bzip2.git@master#egginfo=pragzip&subdirectory=python/pragzip'
```

And to build locally, you can use `build` and install the wheel:

```bash
cd python/pragzip
rm -rf dist
python3 -m build .
python3 -m pip install --force-reinstall --user dist/*.whl
```


# Usage

## Command Line Tool

```bash
pragzip --help

# Parallel decoding: 1.7 s
time pragzip -d -c -P 0 sample.gz | wc -c

# Serial decoding: 22 s
time gzip -d -c sample.gz | wc -c
```


## Python Library

### Simple open, seek, read, and close

```python3
from pragzip import PragzipFile

file = PragzipFile( "example.gz", parallelization = os.cpu_count() )

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
import pragzip

with pragzip.open( "example.gz", parallelization = os.cpu_count() ) as file:
    file.seek( 123 )
    data = file.read( 100 )
```


### Storing and loading the block offset map

The creation of the list of gzip blocks can take a while because it has to decode the gzip file completely.
To avoid this setup when opening a gzip file, the block offset list can be exported and imported.


### Open a pure Python file-like object for indexed reading

```python3
import io
import os
import pragzip as pragzip

with open( "example.gz", 'rb' ) as file:
    in_memory_file = io.BytesIO( file.read() )

with pragzip.open( in_memory_file, parallelization = os.cpu_count() ) as file:
    file.seek( 123 )
    data = file.read( 100 )
```


## Via Ratarmount

Because `pragzip` can be used as a backend in ratarmount, you can use [ratarmount](https://github.com/mxmlnkn/ratarmount) to mount single gzip files easily.
Furthermore, since ratarmount 0.11.0, parallelization is the default and does not have to be specified explicitly with `-P`.

```bash
base64 /dev/urandom | head -c $(( 4 * 1024 * 1024 * 1024 )) | gzip > sample.gz
# Serial decoding: 23 s
time gzip -c -d sample.gz | wc -c

python3 -m pip install --user ratarmount
ratarmount sample.gz mounted

# Parallel decoding: 3.5 s
time cat mounted/sample | wc -c

# Random seeking to the middle of the file and reading 1 MiB: 0.287 s
time dd if=mounted/sample bs=$(( 1024 * 1024 )) \
       iflag=skip_bytes,count_bytes skip=$(( 2 * 1024 * 1024 * 1024 )) count=$(( 1024 * 1024 )) | wc -c
```


# C++ library

Because it is written in C++, it can of course also be used as a C++ library.
In order to make heavy use of templates and to simplify compiling with Python `setuptools`, it is mostly header-only so that integration it into another project should be easy.
The license is also permissive enough for most use cases.

I currently did not yet test integrating it into other projects other than simply manually copying the source in `src/core`, `src/pragzip`, and if integrated zlib is desired also `src/external/zlib`.
If you have suggestions and wishes like support with CMake or Conan, please open an issue.


# Performance comparison with gzip module when a gzip index exists

These are simple timing tests for reading all the contents of a gzip file sequentially.

```python3
import gzip
import time

with gzip.open( gzipFilePath ) as file:
    t0 = time.time()
    while file.read( 4*1024*1024 ):
        pass
    t1 = time.time()
    print( f"Decoded file in {t1-t0}s" )
```

The usage of pragzip is slightly different:

```python3
import indexed_gzip
import pragzip
import time

with indexed_gzip.IndexedGzipFile(gzipFilePath) as file:
    file.build_full_index()
    file.export_index(gzipFilePath + ".index")

# parallelization = 0 means that it is automatically using all available cores.
for parallelization in [0, 1, 2, 6, 12, 24, 32]:
    with pragzip.PragzipFile(gzipFilePath, parallelization = parallelization) as file:
        file.set_block_offsets(open(gzipFilePath + ".index", 'rb'))

        t0 = time.time()
        while file.read( 4*1024*1024 ):
            pass
        t1 = time.time()
        print( f"Decoded file in {t1-t0}s" )
```

Results for an AMD Ryzen 3900X 12-core (24 virtual cores) processor and with `gzipFilePath=4GB-base64.gz`, which is a 4 GiB gzip compressed file with base64 random data.

| Module                            | Runtime / s |
|-----------------------------------|-------------|
| gzip                              | 17.2        |
| pragzip with parallelization = 0  | 1.25        |
| pragzip with parallelization = 1  | 13.8        |
| pragzip with parallelization = 2  | 7.0         |
| pragzip with parallelization = 6  | 2.5         |
| pragzip with parallelization = 12 | 1.47        |
| pragzip with parallelization = 24 | 1.25        |
| pragzip with parallelization = 32 | 1.33        |

The speedup of `pragzip` over the `gzip` module with `parallelization = 0` is 17.2/1.25 = **14**.
When using only one core, `pragzip` is faster by (17.2-13.8)/17.2 = 20%.


# Internal Architecture

The main part of the [internal architecture](https://github.com/mxmlnkn/indexed_bzip2/tree/master/python/indexed_bzip2#internal-architecture) used for parallelizing is the same as used for [indexed_bzip2](https://github.com/mxmlnkn/indexed_bzip2).
