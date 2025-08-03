<div align="center">

# Parallel Random Access to bzip2 and gzip

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](http://opensource.org/licenses/MIT)
[![C++ Code Checks](https://github.com/mxmlnkn/indexed_bzip2/actions/workflows/test-cpp.yml/badge.svg)](https://github.com/mxmlnkn/indexed_bzip2/actions/workflows/test-cpp.yml)
[![codecov](https://codecov.io/gh/mxmlnkn/indexed_bzip2/branch/master/graph/badge.svg?token=94ZD4UTZQW)](https://codecov.io/gh/mxmlnkn/indexed_bzip2)
![C++17](https://img.shields.io/badge/C++-17-blue.svg)
[![Discord](https://img.shields.io/discord/783411320354766878?label=discord)](https://discord.gg/Wra6t6akh2)
[![Telegram](https://img.shields.io/badge/Chat-Telegram-%2330A3E6)](https://t.me/joinchat/FUdXxkXIv6c4Ib8bgaSxNg)

</div>

This repository contains the code for the [`indexed_bzip2`](python/indexed_bzip2) and [`rapidgzip`](python/rapidgzip) Python modules.
Both are built upon the same basic architecture to enable block-parallel decoding based on prefetching and caching.

<div align="center">

# rapidgzip

[![Changelog](https://img.shields.io/badge/Changelog-Markdown-blue)](https://github.com/mxmlnkn/indexed_bzip2/blob/master/python/rapidgzip/CHANGELOG.md)
[![PyPI version](https://badge.fury.io/py/rapidgzip.svg)](https://badge.fury.io/py/rapidgzip)
[![Python Version](https://img.shields.io/pypi/pyversions/rapidgzip)](https://pypi.org/project/rapidgzip/)
[![PyPI Platforms](https://img.shields.io/badge/pypi-linux%20%7C%20macOS%20%7C%20Windows-brightgreen)](https://pypi.org/project/rapidgzip/)
[![Downloads](https://static.pepy.tech/badge/rapidgzip/month)](https://pepy.tech/project/rapidgzip)

![](https://raw.githubusercontent.com/mxmlnkn/indexed_bzip2/master/results/asciinema/rapidgzip-comparison.gif)

</div>

This module provides: 
 - a `rapidgzip` command line tool for parallel decompression of gzip files with a similar command line interface to `gzip` so that it can be used as a replacement.
 - a `rapidgzip.open` Python method for reading and seeking inside gzip files using multiple threads for a speedup of **21** over the built-in gzip module using a 12-core processor.

The random seeking support is similar to the one provided by [indexed_gzip](https://github.com/pauldmccarthy/indexed_gzip), and the parallel capabilities are effectively a working version of [pugz](https://github.com/Piezoid/pugz), which is only a concept and only works with a limited subset of file contents, namely non-binary (ASCII characters 0 to 127) compressed files.

| Module                              | Bandwidth / (MB/s) | Speedup |
|-------------------------------------|--------------------|---------|
| gzip                                |  250               |  1      |
| rapidgzip with parallelization = 1  |  488               |  1.9    |
| rapidgzip with parallelization = 2  |  902               |  3.6    |
| rapidgzip with parallelization = 12 | 4463               | 17.7    |
| rapidgzip with parallelization = 24 | 5240               | 20.8    |

[See here for the extended Readme.](python/rapidgzip)

There also exists a dedicated repository for rapidgzip [here](https://github.com/mxmlnkn/rapidgzip).
It was created for visibility reasons and in order to keep indexed_bzip2 and rapidgzip releases separate.
The main development will take place in [this](https://github.com/mxmlnkn/indexed_bzip2) repository, while the rapidgzip repository will be updated at least for each release.
Issues regarding rapidgzip should be opened at [its repository](https://github.com/mxmlnkn/rapidgzip/issues).

A paper describing the implementation details and showing the scaling behavior with up to 128 cores has been submitted to and [accepted](https://www.hpdc.org/2023/program/technical-sessions/) in [ACM HPDC'23](https://www.hpdc.org/2023/), The 32nd International Symposium on High-Performance Parallel and Distributed Computing.
If you use this software for your scientific publication, please cite it as stated [here](python/rapidgzip#citation).
The author's version can be found [here](<results/paper/Knespel, Brunst - 2023 - Rapidgzip - Parallel Decompression and Seeking in Gzip Files Using Cache Prefetching.pdf>) and the accompanying presentation [here](results/Presentation-2023-06-22.pdf).


<div align="center">

# indexed_bzip2

[![Changelog](https://img.shields.io/badge/Changelog-Markdown-blue)](https://github.com/mxmlnkn/indexed_bzip2/blob/master/python/indexed_bzip2/CHANGELOG.md)
[![PyPI version](https://badge.fury.io/py/indexed-bzip2.svg)](https://badge.fury.io/py/indexed-bzip2)
[![Python Version](https://img.shields.io/pypi/pyversions/indexed_bzip2)](https://pypi.org/project/indexed-bzip2/)
[![PyPI Platforms](https://img.shields.io/badge/pypi-linux%20%7C%20macOS%20%7C%20Windows-brightgreen)](https://pypi.org/project/indexed-bzip2/)
[![Downloads](https://static.pepy.tech/badge/indexed-bzip2/month)](https://pepy.tech/project/indexed-bzip2)
<br>
[![Conda Platforms](https://img.shields.io/conda/v/conda-forge/indexed_bzip2?color=brightgreen)](https://anaconda.org/conda-forge/indexed_bzip2)
[![Conda Platforms](https://img.shields.io/conda/pn/conda-forge/indexed_bzip2?color=brightgreen)](https://anaconda.org/conda-forge/indexed_bzip2)

</div>

This module provides:
  - an `ibzip2` command line tool to decompress bzip2 files in parallel with a similar command line interface to `bzip2` so that it can be used as a replacement.
  - an `ibzip2.open` Python method for reading and seeking inside bzip2 files using multiple threads for a speedup of **6** over the built-in bzip2 module using a 12-core processor.

The parallel decompression capabilities are similar to [lbzip2](https://lbzip2.org/) but with a more permissive license and with support to be used as a library with random seeking capabilities similar to [seek-bzip2](https://github.com/galaxyproject/seek-bzip2).

| Module                                  | Runtime / s | Bandwidth / (MB/s) | Speedup |
|-----------------------------------------|-------------|--------------------|---------|
| bz2                                     | 386         |  5.2               | 1       |
| indexed_bzip2 with parallelization = 1  | 472         |  4.2               | 0.8     |
| indexed_bzip2 with parallelization = 2  | 265         |  7.6               | 1.5     |
| indexed_bzip2 with parallelization = 12 |  64         | 31.4               | 6.1     |
| indexed_bzip2 with parallelization = 24 |  63         | 31.8               | 6.1     |

[See here for the extended Readme.](python/indexed_bzip2)


# License

Licensed under either of

 * Apache License, Version 2.0, ([LICENSE-APACHE](LICENSE-APACHE) or http://www.apache.org/licenses/LICENSE-2.0)
 * MIT license ([LICENSE-MIT](LICENSE-MIT) or http://opensource.org/licenses/MIT)

at your option.

### Contribution

Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in the work by you, as defined in the Apache-2.0 license, shall be dual licensed as above, without any
additional terms or conditions.
