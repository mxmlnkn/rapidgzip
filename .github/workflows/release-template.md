# Binaries

The offered binaries are statically built and therefore should run on most systems released in the last 10 years without additional dependencies.
The package binaries can simply be downloaded, extracted, and executed.
The binaries contain a command line interface similar to `gzip`. See, e.g., `rapidgzip --help`

Alternatively, installation helpers such as [eget](https://github.com/zyedidia/eget) and [ubi](https://github.com/houseabsolute/ubi) can be used.

 - `eget --to="$HOME/.local/bin/" mxmlnkn/rapidgzip` or `eget --pre-release mxmlnkn/rapidgzip`
 - `ubi -i "$HOME/.local/bin/" -p mxmlnkn/rapidgzip` (seems to not work with pre-releases)
