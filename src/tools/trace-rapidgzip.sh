#!/usr/bin/env bash

set -e

export SCOREP_ENABLE_TRACING=true
export SCOREP_ENABLE_PROFILING=false
export SCOREP_TOTAL_MEMORY=1G
export SCOREP_EXPERIMENT_DIRECTORY=scorep-rapidgzip
export SCOREP_PROFILING_MAX_CALLPATH_DEPTH=48
export SCOREP_TIMER=clock_gettime
export SCOREP_MEMORY_RECORDING=true
#export SCOREP_FILTERING_FILE=custom.scorep.filter # better to use --instrument-filter but might be required when using --nocompiler, however only the compiler plugin can isntrument inline functions
# These require Score-P being built with libunwind-dev!
# Does not work for me ... When I activate it, I only get I/O functions traced none of my own ...
#export SCOREP_ENABLE_UNWINDING=true # add calling context like source code location!
#export SCOREP_SAMPLING_EVENTS= # deactivate sampling (only unwind events)
#export SCOREP_SAMPLING_EVENTS=perf_cycles@100000
#export SCOREP_SAMPLING_EVENTS=PAPI_TOT_CYC@100000

# xdg-open /opt/scorep-8.0/share/doc/scorep/pdf/scorep.pdf

# deflate::Block::appendToWindow is called for every byte!
# Have to fully exclude BitReader because it uses throw catch, which trips up Score-P enter/leave tracing :(

# We need to explicitly include almost everything to force inlined functions to be traced
cat <<EOF > custom.scorep.filter
SCOREP_REGION_NAMES_BEGIN
  INCLUDE *deflate*
  INCLUDE *Fetcher*
  INCLUDE *decodeBlock*
  INCLUDE *Finder*
  INCLUDE *BlockMap*
  INCLUDE *WindowMap*
  INCLUDE *Cache*
  INCLUDE *JoiningThread*
  INCLUDE *write*
  INCLUDE *splice*
  INCLUDE *SpliceVault*

  INCLUDE *Reader*
  EXCLUDE *BitReader*read*
  EXCLUDE *BitReader*peek*
  EXCLUDE *BitReader*peekUnsafe*
  EXCLUDE *BitReader*seekAfterPeek*
  EXCLUDE *BitReader*fillBitBuffer*
  EXCLUDE *BitReader*bitBufferSize*

  EXCLUDE *calculateLength*
  EXCLUDE *Block*appendToWindow*
  EXCLUDE *Block*getLength*
  EXCLUDE *Block*getDistance*
  EXCLUDE *Block*ShiftBackOnReturn*
  EXCLUDE *Block*resolveBackreference*
  INCLUDE *Block*readCompressed*

  EXCLUDE *HuffmanCoding*decode*
  EXCLUDE *MapMarkers*
  EXCLUDE *absdiff*
SCOREP_REGION_NAMES_END
EOF

function buildWithScoreP()
{
    scorep --instrument-filter="$PWD/custom.scorep.filter" --io=posix \
        g++ \
            -I../src/rapidgzip \
            -I../src/rapidgzip/huffman \
            -I../src/rapidgzip/huffman/.. \
            -I../src/core \
            -I../src/external/rpmalloc/rpmalloc \
            -I../src/external/isa-l/include \
            -isystem ../src/external/cxxopts/include \
            -march=native -O3 -DNDEBUG \
            -Wall -Wextra -Wshadow -Wunused -Werror=return-type -Wno-attributes -Wsuggest-override \
            -DWITH_RPMALLOC -DWITH_ISAL \
            -fconstexpr-ops-limit=99000100 \
            -o rapidgzip.cpp.scorep.o \
            -c ../src/tools/rapidgzip.cpp
    scorep --io=posix --thread=pthread \
        g++ -march=native -O3 -DNDEBUG \
        rapidgzip.cpp.scorep.o \
        -o src/tools/rapidgzip \
        src/librpmalloc.a \
        src/libzlibstatic.a \
        src/libisal_inflate.a
}

echo "Building code with Score-P..."
buildWithScoreP
echo "Built executable."

src/tools/rapidgzip -d -o /dev/null <( fcat 4GiB-base64.bgz )
