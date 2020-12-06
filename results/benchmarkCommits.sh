#!/usr/bin/env bash

gitRootDir=$( git rev-parse --show-toplevel )
cd -- "$gitRootDir"
git worktree add worktrees/benchmark-commits 2>/dev/null
cd worktrees/benchmark-commits
logFolder=../../results/benchmarks/
mkdir -p -- "$logFolder"

# This benchmark installs indexed_bzip2 using setuptools,
# so we can only start benchmarking when setup.py was introduced
firstSetupToolsCommit=$( git log --reverse --pretty=format:%h -- setup.py | head -1 )

# Create a file for benchmarking.
bz2File=$( mktemp --tmpdir=/dev/shm --suffix=.bz2 )
trap "'rm' -f -- '$bz2File'" EXIT
openssl enc -aes-256-ctr -pass pass:"nice seed used for reproducibility" -nosalt </dev/zero 2>/dev/null |
    head -c $(( 128 * 1024 * 1024 )) | bzip2 --compress --stdout > "$bz2File" # ~80ms for 128MB

# Go into a virtualenv in order to not pollute the host system
python3 -m virtualenv benchmark_venv
. benchmark_venv/bin/activate
python3 -m pip install cython pip numpy

# In general, all files relevant for the finished module and performance are in indexed_bzip2.
# setup.py is also possibly because it basically is the build system and contains the compiler flags.
commits=( $( git log --reverse --pretty=format:%h bccbedc~1..HEAD -- setup.py indexed_bzip2 ) )
for commit in "${commits[@]}"; do
    echo
    echo "[$( date +%Y-%m-%dT%H-%M )] Benchmarking $commit ..."

    # Because of CPython version changes, bzip2.cpp might change and keep us from successfully changing the commit!
    # It is safe to overwrite changes to it because it is auto-generated
    git checkout HEAD indexed_bzip2/indexed_bzip2.cpp &>/dev/null
    git checkout HEAD bzip2.cpp &>/dev/null

    python3 -m pip uninstall --yes indexed_bzip2

    git checkout "$commit" &>/dev/null &&
    # force rebuild by cleaning first else it won't work probably because the modification time of
    # checkout out files will always be in the past compared to the already built shared library!
    # Note that on 92827d1, compiling with the source generated with Cython 0.29.14 yields ~9.7s.
    # However when removing that source to force regeneration with Cython 0.29.21, I get ~8.3s!
    # Therefore, delete the cython generated source to force regenerating it with the current version
    # as I'm only interested in performance introduced by my own commits (for now).
    'rm' -f *.so indexed_bzip2/indexed_bzip2.cpp bzip2.cpp &&
    python3 setup.py clean --all &&
    python3 setup.py build_ext --inplace --cython &&
    python3 -m pip install --force-reinstall . &>/dev/null || continue

    cythonVersion=$( python3 -m cython --version 2>&1 | sed -nE 's|.* ([0-9]+\.[0-9]+\.[0-9]+).*|\1|p' )
    python3 -c '
import indexed_bzip2
from datetime import datetime
import sys, time
import numpy as np

version = "unknown"
try:
    version = indexed_bzip2.__version__
    print( "indexed_bzip2 version {}".format( indexed_bzip2.__version__ ), file = sys.stderr )
except AttributeError:
    pass

timings = []
for i in range( 5 ):
    f = indexed_bzip2.IndexedBzip2File( sys.argv[1] )
    t0 = time.time()
    readBytes = len( f.read( 512 * 1024 * 1024 ) ) # Not all versions had support for no argument at all
    t1 = time.time()
    print( f"    Decoding file with {readBytes}B fully for the first time took {t1-t0:.3f} s", file = sys.stderr )
    timings += [ t1 - t0 ]

dateString = datetime.now().strftime( "%Y-%m-%dT%H-%M" )
print( f"[{dateString}][{sys.argv[2]}] "
       f"{np.min( timings ):.3f} <= {np.mean( timings ):.3f} +- {np.std( timings ):.3f} <= {np.max( timings ):.3f} "
       f"at version {version}" )
' "$bz2File" "$commit" | tee -a "$logFolder/cython-$cythonVersion-full-first-read.log"

    bz2ReaderInclude=bzip2.h
    if [[ -f indexed_bzip2/BZ2Reader.hpp ]]; then bz2ReaderInclude=BZ2Reader.hpp; fi

    simpleBzcat=$( mktemp --suffix=.cpp )
    cat <<EOF > "$simpleBzcat"
#include <iostream>
#include <vector>
#include <$bz2ReaderInclude>

int main( int argc, char** argv )
{
    BZ2Reader reader( argv[1] );
    size_t nBytesWrittenTotal = 0;
    std::vector<char> buffer( 4 * 1024 * 1024, 0 );
    do {
        nBytesWrittenTotal += reader.read( -1, buffer.data(), buffer.size() );
    } while ( !reader.eof() );
    std::cerr << nBytesWrittenTotal << "\n";
    return 0;
}
EOF

    for compiler in g++ clang++ g++-9 g++-10 clang++-10; do
        if ! command -v "$compiler" &>/dev/null; then continue; fi
        compilerVersion=$( $compiler --version | sed -nE 's|.* ([0-9]+\.[0-9]+\.[0-9]+).*|\1|p' )

        $compiler -I . -I indexed_bzip2 -std=c++17 -Wall -Wextra -Wshadow -Wsuggest-override -O3 -DNDEBUG -pthread "$simpleBzcat" || continue
        times=()
        for (( i = 0; i < 5; ++i )); do
            times+=( $( { time -p ./a.out "$bz2File"; } 2>&1 | sed -n 's|real ||p' ) )
        done
        echo "[$( date +%Y-%m-%dT%H-%M )][$commit] ${times[*]}" | tee -a "$logFolder/$compiler-$compilerVersion-full-first-read.log"
    done

    'rm' "$simpleBzcat"
done

deactivate
