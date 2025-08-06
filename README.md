<div align="center">

![](https://raw.githubusercontent.com/mxmlnkn/indexed_bzip2/master/results/librapidarchive.svg)

# Parallel Random Access to bzip2, gzip (and hopefully more in the future)

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

[**See here for the extended Readme.**](python/rapidgzip#readme)

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

[**See here for the extended Readme.**](python/indexed_bzip2#readme)


# Naming

The CMake options have been prefixed with `librapidarchive`.
This [difficult decision](https://github.com/mxmlnkn/indexed_bzip2/pull/22#discussion_r2249989703) came about because neither `RAPIDGZIP_` nor `IBZIP2_` would have made sense.
I needed an umbrella name for both, and possibly further compression formats such as LZ4 and ZIP in the future.
I aim for something akin to [libarchive](https://github.com/libarchive/libarchive), but with support for parallelized decompression and constant-time seeking instead of streaming extraction because it is to be used as a backend for [ratarmount](https://github.com/mxmlnkn/ratarmount).

The project started inside the [ratarmount](https://github.com/mxmlnkn/ratarmount) as a random-seekable bzip2 backend.
After troubles with compiling a Python C-extension and after noticing that this backend might also find usage on its own, I created the [`indexed_bzip2`](https://github.com/mxmlnkn/indexed_bzip2) repository, following the naming scheme of [`indexed_gzip`](https://github.com/pauldmccarthy/indexed_gzip) to make it easily discoverable, e.g., in the PyPI search.
After adding novel parallelized and seekable gzip decompression support and shortly before publishing the [paper](https://doi.org/10.1145/3588195.3592992), I split off yet another repository and project called [rapidgzip](https://github.com/mxmlnkn/rapidgzip), which became more well-known than `indexed_bzip2`.

Reasons for not including `rapidgzip` in `indexed_bzip2`:

 - **Much** more complicated build setup with rpmalloc, zlib, and ISA-L, which might fail to build on more systems than `indexed_bzip2` when there are no wheels available.
   On the other hand, `indexed_bzip2` only requires building its own C++ header-only sources.
   These dependencies are also the reason for [failing to get it merged into Conda](https://github.com/conda-forge/staged-recipes/pull/23901) while Conda `indexed_bzip2` [exists](https://anaconda.org/conda-forge/indexed_bzip2).
 - The rapidgzip Python module binary is also almost 10x larger because of large precomputed lookup tables and templating.
 - Releases, especially on Github. Many recent changes were only for rapidgzip, not `indexed_bzip2`. It makes sense to have different releases for these projects and also to keep them on different Github release pages.
 - More visibility:
   - Similar to how none would guess that `bsdtar` is able to extract archives other than TAR, it makes no sense to expect something called `indexed_bzip2` to also work for gzip, etc. `libarchive`, which provides `bsdtar`, makes much more sense as a name.
   - They have different ReadMe files with different usages and benchmarks.
     Showing these top-level in the specialized repositories is nice.
 - Note that the Python package `rapidgzip` does not even bundle `indexed_bzip2`. It can even natively open bzip2 with `RapidGzipFile`, but this uses a different algorithm, which is less specialized to bzip2 and therefore has more memory overhead and might be slightly slower.
   Until this does not have feature and performance parity, it makes sense to have two projects.

Downsides:

 - I am not sure how well the `rapidgzip` and `indexed_bzip2` Python modules work when loaded at the same time.
   There may be name collisions resulting in problems. It might be best to make the namespace, currently `rapidgzip::`, adjustable and use something else for each Python package.
   Currently, I am sidestepping this issue in ratarmount by including `indexed_bzip2` in the `rapidgzip` Python package because it is trivial and low-overhead to do so. So, if you need to use both, depend on `rapidgzip` for now.
 - Contributions and attention are split between all these projects, also resulting in confusion.
   I have mitigated it somewhat by adding a pull request template on the rapidgzip repository pointing to `indexed_bzip2`.

I think, in the future, I'll avoid starting new repositories and simply release specialized Packages from this one or even only alias Python packages, which point to / depend on `rapidgzip` or a hypothetical `librapidarchive`.


# License

Licensed under either of

 * Apache License, Version 2.0, ([LICENSE-APACHE](LICENSE-APACHE) or http://www.apache.org/licenses/LICENSE-2.0)
 * MIT license ([LICENSE-MIT](LICENSE-MIT) or http://opensource.org/licenses/MIT)

at your option.

### Contribution

Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in the work by you, as defined in the Apache-2.0 license, shall be dual licensed as above, without any
additional terms or conditions.
