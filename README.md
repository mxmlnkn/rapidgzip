<div align="center">

# Parallel Random Access to bzip2 and gzip

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](http://opensource.org/licenses/MIT)
[![Build Status](https://github.com/mxmlnkn/indexed_bzip2/workflows/tests/badge.svg)](https://github.com/mxmlnkn/pragzip/actions)
[![codecov](https://codecov.io/gh/mxmlnkn/indexed_bzip2/branch/master/graph/badge.svg?token=94ZD4UTZQW)](https://codecov.io/gh/mxmlnkn/indexed_bzip2)
![C++17](https://img.shields.io/badge/C++-17-blue.svg)
[![Discord](https://img.shields.io/discord/783411320354766878?label=discord)](https://discord.gg/Wra6t6akh2)
[![Telegram](https://img.shields.io/badge/Chat-Telegram-%2330A3E6)](https://t.me/joinchat/FUdXxkXIv6c4Ib8bgaSxNg)

</div>

This repository contains the code for the [`indexed_bzip2`](python/indexed_bzip2) and [`pragzip`](python/pragzip) Python modules.
Both are built upon the same basic architecture to enable block-parallel decoding based on prefetching and caching.

<div align="center">

# pragzip (alternative name: rapidgzip)

[![PyPI version](https://badge.fury.io/py/pragzip.svg)](https://badge.fury.io/py/pragzip)
[![Python Version](https://img.shields.io/pypi/pyversions/pragzip)](https://pypi.org/project/pragzip/)
[![PyPI Platforms](https://img.shields.io/badge/pypi-linux%20%7C%20macOS%20%7C%20Windows-brightgreen)](https://pypi.org/project/pragzip/)
[![Downloads](https://pepy.tech/badge/pragzip/month)](https://pepy.tech/project/pragzip)

![](https://raw.githubusercontent.com/mxmlnkn/indexed_bzip2/master/results/asciinema/pragzip-comparison.gif)

</div>

This module provides: 
 - a `pragzip` command line tool for parallel decompression of gzip files with a similar command line interface to `gzip` so that it can be used as a replacement.
 - a `pragzip.open` Python method for reading and seeking inside gzip files using multiple threads for a speedup of **14** over the built-in gzip module using a 12-core processor.

The random seeking support is similar to the one provided by [indexed_gzip](https://github.com/pauldmccarthy/indexed_gzip) and the parallel capabilities are effectively a working version of [pugz](https://github.com/Piezoid/pugz), which is only a concept and only works with a limited subset of file contents, namely non-binary (ASCII characters 0 to 127) compressed files.

| Module                            | Runtime / s | Bandwidth / (MB/s) | Speedup |
|-----------------------------------|-------------|--------------------|---------|
| gzip                              |  17.5       |  190               | 1       |
| pragzip with parallelization = 1  |  18.2       |  180               | 1.0     |
| pragzip with parallelization = 2  |   9.3       |  350               | 1.9     |
| pragzip with parallelization = 12 |  1.82       | 1800               | 9.6     |
| pragzip with parallelization = 24 |  1.25       | 2620               | 14.0    |

[See here for the extended Readme.](python/pragzip)

There also exists a dedicated repository for pragzip [here](https://github.com/mxmlnkn/pragzip).
It was created for visibility reasons and in order to keep indexed_bzip2 and pragzip releases separate.
The main development will take place in [this](https://github.com/mxmlnkn/indexed_bzip2) repository while the pragzip repository will be updated at least for each release.
Issues regarding pragzip should be opened at [its repository](https://github.com/mxmlnkn/pragzip/issues).

A paper describing the implementation details and showing the scaling behavior with up to 128 cores has been submitted to and [accepted](https://www.hpdc.org/2023/program/technical-sessions/) in [ACM HPDC'23](https://www.hpdc.org/2023/), The 32nd International Symposium on High-Performance Parallel and Distributed Computing. If you use this software for your scientific publication, please cite it as stated [here](python/pragzip#citation).


<div align="center">

# indexed_bzip2

[![PyPI version](https://badge.fury.io/py/indexed-bzip2.svg)](https://badge.fury.io/py/indexed-bzip2)
[![Python Version](https://img.shields.io/pypi/pyversions/indexed_bzip2)](https://pypi.org/project/indexed-bzip2/)
[![PyPI Platforms](https://img.shields.io/badge/pypi-linux%20%7C%20macOS%20%7C%20Windows-brightgreen)](https://pypi.org/project/indexed-bzip2/)
[![Downloads](https://pepy.tech/badge/indexed-bzip2/month)](https://pepy.tech/project/indexed-bzip2)
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
| bz2                                     | 392         |  5.1               | 1       |
| indexed_bzip2 with parallelization = 1  | 559         |  3.6               | 0.7     |
| indexed_bzip2 with parallelization = 2  | 321         |  6.2               | 1.2     |
| indexed_bzip2 with parallelization = 12 |  72         | 27.8               | 5.4     |
| indexed_bzip2 with parallelization = 24 |  64         | 31.5               | 6.2     |

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
