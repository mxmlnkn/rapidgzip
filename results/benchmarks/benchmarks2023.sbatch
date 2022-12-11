#!/bin/bash
#SBATCH -A zihforschung
#SBATCH -p romeo
#SBATCH --exclusive
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=128
#SBATCH --time=04:00:00
#SBATCH --mem-per-cpu=1972M
#SBATCH --cpu-freq=2000000-2000000

module load CMake Ninja Clang NASM hwloc
module list

function m()
{
    if [[ -f Makefile ]]; then
        make -j $( nproc ) "$@"
    elif [[ -f CMakeCache.txt ]]; then
        cmake --build . --parallel $( nproc ) -- "$@"
    fi
}

# Install pigz
(
    cd ~
    git clone 'https://github.com/madler/pigz.git'
    git checkout master
    cd pigz
    m -B
)

# Install pugz
(
    cd ~
    git clone 'https://github.com/mxmlnkn/pugz'
    cd pugz
    git fetch origin
    git reset --hard origin/benchmarks
    git pull
    m -B
    'mv' gunzip pugz

    # Benchmark of pugz blockfinder
    size=$(( 2 * 1024 * 1024 ))
    fname='random-2MiB'
    head -c $size /dev/urandom > "$fname"
    g++ -lrt -march=native --std=c++17 -Wall -O3 -DNDEBUG -o blockfinder -I . -I common programs/blockfinder.cpp &&
    ./blockfinder "$fname" 200 2>&1 | tee pugz.log &&
    sed -n -E 's|.* in ([0-9.]+) s.*|'"$size "'\1|p' pugz.log > 'results-pugz-sync.dat'
    'rm' -f "$fname"
)

# Install igzip
(
    cd ~
    git clone 'https://github.com/intel/isa-l'
    cd isa-l
    ./autogen.sh
    ./configure
    m -B
)

# Generic system information
(
    cd ~
    echo -e "\n=== hwloc-info ==="
    hwloc-info
    echo -e "\n=== hwloc-ls ==="
    hwloc-ls
    lstopo -f --of xml "$HOME/hwloc-$( hostname ).xml"
    lstopo -f --of svg "$HOME/hwloc-$( hostname ).svg"
)

# Install pragzip
(
    cd ~
    git clone 'https://github.com/mxmlnkn/indexed_bzip2'
    cd indexed_bzip2
    git fetch origin
    git reset --hard origin/develop
    mkdir -p build
    cd build
    cmake -GNinja ..
    ninja pragzip benchmarkSequential2023
)

export PATH=$HOME/pigz:$PATH
export PATH=$HOME/pugz:$PATH
export PATH=$HOME/isa-l/programs:$PATH
export PATH=$HOME/indexed_bzip2/build/src/tools:$PATH

# Benchmarks of components
cd ~/indexed_bzip2/build
time src/benchmarks/benchmarkSequential2023


nRepetitions=20
set -o pipefail

# /dev/urandom reading is 60 MB/s slow!
# That's why we pregenrate 512 MiB of random data and then simply repeat that as needed
base64File512MiB="/dev/shm/base64-512MiB"
base64 /dev/urandom | head -c $(( 512 * 1024 * 1024 )) > "$base64File512MiB"
base64File128MiB="/dev/shm/base64-128MiB"
base64 /dev/urandom | head -c $(( 128 * 1024 * 1024 )) > "$base64File128MiB"

# Decompression bandwidth over thread count (this takes ~80 min)
echo '# parallelization dataSize/B runtime/s' > 'result-parallel-pragzip-dev-null.dat'
echo '# parallelization dataSize/B runtime/s' > 'result-parallel-pragzip-index-dev-null.dat'
echo '# parallelization dataSize/B runtime/s' > 'result-parallel-pugz-dev-null.dat'
echo '# parallelization dataSize/B runtime/s' > 'result-parallel-pugz-dev-null-sync.dat'
#for parallelization in 1 2 3 4 6 8 12 16 24 32 48 64 96 128; do
for parallelization in 128 96 64 48 32 24 16 12 8 6 4 3 2 1; do
    fileSize=$(( parallelization * 128 * 1024 * 1024 ))
    filePath="/dev/shm/base64.gz"
    # Increase the block size because many smaller blocks trigger some kind of bug in pugz:
    # https://github.com/Piezoid/pugz/issues/13
    for (( i=0; i<parallelization; ++i )); do cat "$base64File128MiB"; done |
        pigz --blocksize $(( parallelization * 4 * 1024 )) > "$filePath"

    # pugz synchronized output
    for (( i = 0; i < nRepetitions; ++i )); do
        tool="pugz -t $parallelization"
        runtime=$( ( time timeout 30s taskset --cpu-list 0-$(( parallelization - 1 )) $tool "$filePath" > /dev/null ) 2>&1 |
                       sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
        errorCode=$?
        if [[ $errorCode -eq 0 ]]; then
            echo "$parallelization $fileSize $runtime" | tee -a 'result-parallel-pugz-dev-null-sync.dat'
        else
            echo "Encountered error code $errorCode when executing $tool"
        fi
    done

    fileSize=$(( parallelization * 512 * 1024 * 1024 ))
    filePath="/dev/shm/base64.gz"
    # Increase the block size because many smaller blocks trigger some kind of bug in pugz:
    # https://github.com/Piezoid/pugz/issues/13
    for (( i=0; i<parallelization; ++i )); do cat "$base64File512MiB"; done |
        pigz --blocksize $(( parallelization * 4 * 1024 )) > "$filePath"

    # pugz unsynchronized output
    for (( i = 0; i < nRepetitions; ++i )); do
        tool="pugz -t $parallelization -u"
        runtime=$( ( time timeout 20s taskset --cpu-list 0-$(( parallelization - 1 )) $tool "$filePath" > /dev/null ) 2>&1 |
                       sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
        errorCode=$?
        if [[ $errorCode -eq 0 ]]; then
            echo "$parallelization $fileSize $runtime" | tee -a 'result-parallel-pugz-dev-null.dat'
        else
            echo "Encountered error code $errorCode when executing $tool"
        fi
    done

    # pragzip
    for (( i = 0; i < nRepetitions; ++i )); do
        tool="pragzip -P $parallelization -o /dev/null"
        runtime=$( ( time taskset --cpu-list 0-$(( parallelization - 1 )) $tool "$filePath" ) 2>&1 |
                       sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
        errorCode=$?
        if [[ $errorCode -eq 0 ]]; then
            echo "$parallelization $fileSize $runtime" | tee -a 'result-parallel-pragzip-dev-null.dat'
        else
            echo "Encountered error code $errorCode when executing $tool"
        fi
    done

    # pragzip with index
    pragzip -P $( nproc ) --count-lines --export-index "${filePath}.gzindex" "$filePath"
    for (( i = 0; i < nRepetitions; ++i )); do
        tool="pragzip -P $parallelization -o /dev/null --import-index ${filePath}.gzindex"
        runtime=$( ( time taskset --cpu-list 0-$(( parallelization - 1 )) $tool "$filePath" ) 2>&1 |
                       sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
        errorCode=$?
        if [[ $errorCode -eq 0 ]]; then
            echo "$parallelization $fileSize $runtime" | tee -a 'result-parallel-pragzip-index-dev-null.dat'
        else
            echo "Encountered error code $errorCode when executing $tool"
        fi
    done
done


# Decompression bandwidth over thread count for legacy tools
echo '# parallelization dataSize/B runtime/s' > 'result-decompression-gzip-count-bytes.dat'
echo '# parallelization dataSize/B runtime/s' > 'result-decompression-igzip-count-bytes.dat'
echo '# parallelization dataSize/B runtime/s' > 'result-decompression-pigz-count-bytes.dat'
fileSize=$(( 2 * 512 * 1024 * 1024 ))
filePath="/dev/shm/base64.gz"
for (( i=0; i<2; ++i )); do cat "$base64File512MiB"; done | pigz > "$filePath"
for parallelization in 1 2 3 4 6 8 12 16 24 32 48 64 96 128; do
    # gzip
    if [[ $parallelization -eq 1 ]]; then
        for (( i = 0; i < nRepetitions; ++i )); do
            tool="gzip -c -d"
            runtime=$( ( time taskset --cpu-list 0-$(( parallelization - 1 )) $tool "$filePath" | wc -c ) 2>&1 |
                           sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
            echo "$parallelization $fileSize $runtime" | tee -a 'result-decompression-gzip-count-bytes.dat'
        done
    fi

    # igzip
    if [[ $parallelization -le 2 ]]; then
        for (( i = 0; i < nRepetitions; ++i )); do
            tool="igzip -c -d -T $parallelization"
            runtime=$( ( time taskset --cpu-list 0-$(( parallelization - 1 )) $tool "$filePath" | wc -c ) 2>&1 |
                           sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
            echo "$parallelization $fileSize $runtime" | tee -a 'result-decompression-igzip-count-bytes.dat'
        done
    fi

    # pigz
    for (( i = 0; i < nRepetitions; ++i )); do
        tool="pigz -c -d -p $parallelization"
        runtime=$( ( time taskset --cpu-list 0-$(( parallelization - 1 )) $tool "$filePath" | wc -c ) 2>&1 |
                       sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
        echo "$parallelization $fileSize $runtime" | tee -a 'result-decompression-pigz-count-bytes.dat'
    done
done


# Benchmark over chunk size for one chosen parallelization
parallelization=16

fileSize=$(( parallelization * 512 * 1024 * 1024 ))
filePath="/dev/shm/base64.gz"
# Increase the block size because many smaller blocks trigger some kind of bug in pugz:
# https://github.com/Piezoid/pugz/issues/13
for (( i=0; i<parallelization; ++i )); do cat "$base64File512MiB"; done | gzip > "$filePath"

echo '# parallelization chunkSize/B dataSize/B runtime/s' > 'result-chunk-size-pragzip-dev-null.dat'
for chunkSize in 128 256 512 $(( 1*1024 )) $(( 2*1024 )) $(( 4*1024 )) $(( 8*1024 )) $(( 16*1024 )) $(( 32*1024 )) \
                 $(( 64*1024 )) $(( 128*1024 )) $(( 256*1024 )) $(( 512*1024 )) $(( 1024*1024 )); do
    # pragzip
    for (( i = 0; i < nRepetitions; ++i )); do
        tool="pragzip -P $parallelization -o /dev/null --chunk-size $chunkSize"
        runtime=$( ( time taskset --cpu-list 0-$(( parallelization - 1 )) $tool "$filePath" ) 2>&1 |
                       sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
        errorCode=$?
        if [[ $errorCode -eq 0 ]]; then
            echo "$parallelization $(( chunkSize * 1024 )) $fileSize $runtime" | tee -a 'result-chunk-size-pragzip-dev-null.dat'
        else
            echo "Encountered error code $errorCode when executing $tool"
        fi
    done
done
echo '# parallelization chunkSize/B dataSize/B runtime/s' > 'result-chunk-size-pugz-dev-null.dat'
# Smaller chunk sizes lead to errors and/or take too long to feasibly benchmark
for chunkSize in $(( 4*1024 )) $(( 8*1024 )) $(( 16*1024 )) $(( 32*1024 )) $(( 64*1024 )) $(( 128*1024 )) \
                 $(( 256*1024 )) $(( 512*1024 )) $(( 1024*1024 )); do
    # pugz
    for (( i = 0; i < nRepetitions; ++i )); do
        tool="pugz -t $parallelization -l -s $chunkSize -u"
        runtime=$( ( time timeout 120s taskset --cpu-list 0-$(( parallelization - 1 )) $tool "$filePath" > /dev/null ) 2>&1 |
                       sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
        errorCode=$?
        if [[ $errorCode -eq 0 ]]; then
            echo "$parallelization $(( chunkSize * 1024 )) $fileSize $runtime" | tee -a 'result-chunk-size-pugz-dev-null.dat'
        else
            echo "Encountered error code $errorCode when executing $tool"
        fi
    done
done



cd ~/indexed_bzip2/build
tar -cjf ~/pragzip-benchmark-results-$( date +%Y-%m-%dT%H-%M-%S ).tar.bz2 *.dat -C ~/pugz results-pugz-sync.dat -C ~ "hwloc-$( hostname ).xml" "hwloc-$( hostname ).svg"

