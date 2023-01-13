#!/bin/bash
#SBATCH -A zihforschung
#SBATCH -p romeo
#SBATCH --exclusive
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=128
#SBATCH --time=05:00:00
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
    g++ -lrt -march=native --std=c++17 -Wall -O3 -DNDEBUG -o blockfinder -I . -I common programs/blockfinder.cpp
    > pugz.log
    ./blockfinder "$fname" 100 2>&1 | tee -a pugz.log
    for (( i=0; i<100; ++i )); do
        ./blockfinder "$fname" 2>&1 | tee -a pugz.log
    done
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


function benchmarkPugzSyncParallelDecompressionScaling()
{
    filePrefix=$1     # e.g., result-parallel-decompression-base64 or result-parallel-decompression-silesia
    inputFilePath=$2  # e.g., /dev/shm/base64-128MiB

    echo '# parallelization dataSize/B runtime/s compressedDataSize/B' > "$filePrefix-pugz-sync-dev-null.dat"

    for parallelization in 128 96 64 48 32 24 16 12 8 6 4 3 2 1; do
        fileSize=$(( parallelization * $( stat --format=%s -- "$inputFilePath" ) ))
        filePath="/dev/shm/decompression-benchmark.gz"
        # Increase the block size because many smaller blocks trigger some kind of bug in pugz:
        # https://github.com/Piezoid/pugz/issues/13
        for (( i=0; i<parallelization; ++i )); do cat "$inputFilePath"; done |
            pigz --blocksize $(( parallelization * 4 * 1024 )) > "$filePath"
        compressedFileSize=$( stat --format=%s -- "$filePath" )

        # pugz synchronized output
        for (( i = 0; i < nRepetitions; ++i )); do
            tool="pugz -t $parallelization"
            runtime=$( ( time timeout 30s taskset --cpu-list 0-$(( parallelization - 1 )) $tool "$filePath" > /dev/null ) 2>&1 |
                           sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
            errorCode=$?
            if [[ $errorCode -eq 0 ]]; then
                echo "$parallelization $fileSize $runtime $compressedFileSize" |
                    tee -a "$filePrefix-pugz-sync-dev-null.dat"
            else
                echo "Encountered error code $errorCode when executing $tool"
            fi
        done
    done

    'rm' -- "$filePath"
}


function benchmarkParallelDecompressionScaling()
{
    filePrefix=$1  # e.g. result-parallel-decompression-base64 or result-parallel-decompression-silesia
    inputFilePath=$2  # e.g., /dev/shm/base64-512MiB

    echo '# parallelization dataSize/B runtime/s compressedDataSize/B' > "$filePrefix-pragzip-dev-null.dat"
    echo '# parallelization dataSize/B runtime/s compressedDataSize/B' > "$filePrefix-pragzip-index-dev-null.dat"
    if [[ ! "$filePrefix" =~ silesia ]]; then
        echo '# parallelization dataSize/B runtime/s compressedDataSize/B' > "$filePrefix-pugz-dev-null.dat"
    fi

    for parallelization in 128 96 64 48 32 24 16 12 8 6 4 3 2 1; do
        if [[ $parallelization -gt $( nproc ) ]]; then
            continue
        fi

        fileSize=$(( parallelization * $( stat --format=%s -- "$inputFilePath" ) ))
        filePath="/dev/shm/decompression-benchmark.gz"
        # Increase the block size because many smaller blocks trigger some kind of bug in pugz:
        # https://github.com/Piezoid/pugz/issues/13
        for (( i=0; i<parallelization; ++i )); do cat "$inputFilePath"; done |
            pigz --blocksize $(( parallelization * 4 * 1024 )) > "$filePath"
        compressedFileSize=$( stat --format=%s -- "$filePath" )

        # pragzip with index
        echo "Creating gzip index file using pragzip..."
        pragzip -P $( nproc ) -o '/dev/null' -f --export-index "${filePath}.gzindex" "$filePath"
        for (( i = 0; i < nRepetitions; ++i )); do
            tool="pragzip -P $parallelization -o /dev/null --import-index ${filePath}.gzindex"
            ( time taskset --cpu-list 0-$(( parallelization - 1 )) $tool "$filePath" ) > output.log 2>&1
            errorCode=$?
            runtime=$( sed -n -E 's|real[ \t]*0m||p' output.log | sed 's|[ \ts]||' )
            if [[ -n "$runtime" && $errorCode -eq 0 ]]; then
                echo "$parallelization $fileSize $runtime $compressedFileSize" |
                    tee -a "$filePrefix-pragzip-index-dev-null.dat"
            else
                echo "Encountered error code $errorCode when executing $tool, runtime: $runtime"
                cat output.log
            fi
        done

        # pragzip
        for (( i = 0; i < nRepetitions; ++i )); do
            tool="pragzip -P $parallelization -o /dev/null"
            ( time taskset --cpu-list 0-$(( parallelization - 1 )) $tool "$filePath" ) > output.log 2>&1
            errorCode=$?
            runtime=$( sed -n -E 's|real[ \t]*0m||p' output.log | sed 's|[ \ts]||' )
            if [[ -n "$runtime" && $errorCode -eq 0 ]]; then
                echo "$parallelization $fileSize $runtime $compressedFileSize" |
                    tee -a "$filePrefix-pragzip-dev-null.dat"
            else
                echo "Encountered error code $errorCode when executing $tool, runtime: $runtime"
            fi
        done

        # pugz unsynchronized output
        if [[ ! "$filePrefix" =~ silesia ]]; then
            for (( i = 0; i < nRepetitions; ++i )); do
                tool="pugz -t $parallelization -u"
                runtime=$( ( time timeout 20s taskset --cpu-list 0-$(( parallelization - 1 )) $tool "$filePath" > /dev/null ) 2>&1 |
                               sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
                errorCode=$?
                if [[ $errorCode -eq 0 ]]; then
                    echo "$parallelization $fileSize $runtime $compressedFileSize" |
                        tee -a "$filePrefix-pugz-dev-null.dat"
                else
                    echo "Encountered error code $errorCode when executing $tool"
                fi
            done
        fi
    done

    'rm' -- "$filePath"
}


function benchmarkLegacyGzipTools()
{
    filePrefix=$1  # e.g. result-decompression-base64 or result-decompression-silesia
    filePath=$2    # e.g. /dev/shm/base64.gz

    compressedFileSize=$( stat --format=%s -- "$filePath" )
    fileSize=$( igzip -c -d "$filePath" | wc -c )

    # Decompression bandwidth over thread count for legacy tools
    echo '# parallelization dataSize/B runtime/s compressedDataSize/B' > "$filePrefix-gzip-dev-null.dat"
    echo '# parallelization dataSize/B runtime/s compressedDataSize/B' > "$filePrefix-igzip-dev-null.dat"
    echo '# parallelization dataSize/B runtime/s compressedDataSize/B' > "$filePrefix-pigz-dev-null.dat"

    for parallelization in 1 2 3 4 6 8 12 16 24 32 48 64 96 128; do
        if [[ $parallelization -gt $( nproc ) ]]; then
            continue
        fi

        echo "Benchmark legacy gzip tools with parallelization: $parallelization"

        # gzip
        if [[ $parallelization -eq 1 ]]; then
            for (( i = 0; i < nRepetitions; ++i )); do
                tool="gzip -c -d"
                runtime=$( ( time taskset --cpu-list 0-$(( parallelization - 1 )) $tool "$filePath" > /dev/null ) 2>&1 |
                               sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
                echo "$parallelization $fileSize $runtime" | tee -a "$filePrefix-gzip-dev-null.dat"
            done
        fi

        # igzip
        if [[ $parallelization -le 2 ]]; then
            for (( i = 0; i < nRepetitions; ++i )); do
                tool="igzip -c -d -T $parallelization"
                runtime=$( ( time taskset --cpu-list 0-$(( parallelization - 1 )) $tool "$filePath" > /dev/null ) 2>&1 |
                               sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
                echo "$parallelization $fileSize $runtime" | tee -a "$filePrefix-igzip-dev-null.dat"
            done
        fi

        # pigz
        for (( i = 0; i < nRepetitions; ++i )); do
            tool="pigz -c -d -p $parallelization"
            runtime=$( ( time taskset --cpu-list 0-$(( parallelization - 1 )) $tool "$filePath" > /dev/null ) 2>&1 |
                           sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
            echo "$parallelization $fileSize $runtime" | tee -a "$filePrefix-pigz-dev-null.dat"
        done
    done
}

function benchmarkBase64()
{
    # /dev/urandom reading is 60 MB/s slow!
    # That's why we pregenerate 512 MiB of random data and then simply repeat that as needed.
    base64File512MiB="/dev/shm/base64-512MiB"
    base64 /dev/urandom | head -c $(( 512 * 1024 * 1024 )) > "$base64File512MiB"
    base64File128MiB="/dev/shm/base64-128MiB"
    base64 /dev/urandom | head -c $(( 128 * 1024 * 1024 )) > "$base64File128MiB"

    time benchmarkParallelDecompressionScaling 'result-parallel-decompression-base64' "$base64File512MiB"  # 60 min
    # 20 min
    time benchmarkPugzSyncParallelDecompressionScaling 'result-parallel-decompression-base64' "$base64File128MiB"

    filePath='/dev/shm/base64.gz'
    for (( i=0; i<2; ++i )); do cat "$base64File512MiB"; done | pigz > "$filePath"
    time benchmarkLegacyGzipTools 'result-decompression-base64' "$filePath"  # 25 min
    'rm' -- "$filePath" "$base64File512MiB" "$base64File128MiB"
}


function benchmarkSilesia()
{
    if [[ ! -f 'silesia.zip' || "$( md5sum silesia.zip | sed 's| .*||' )" != 'c240c17d6805fb8a0bde763f1b94cd99' ]]; then
        wget -O 'silesia.zip' 'https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip'
    fi

    rm -rf silesia/
    mkdir -p silesia && ( cd silesia && unzip ../silesia.zip )
    tar -cf silesia.tar silesia/  # 211957760 B -> 212 MB, 203 MiB, gzip 66 MiB -> compression factor: 3.08

    silesia2x="/dev/shm/silesia-2x-203MiB"
    cat silesia.tar silesia.tar > "$silesia2x"

    # Decompression bandwidth over thread count (this takes ~80 min)
    time benchmarkParallelDecompressionScaling 'result-parallel-decompression-silesia' "$silesia2x"  # 35 min
    'rm' "$silesia2x"

    silesia5x='/dev/shm/base64.gz'
    for (( i=0; i<5; ++i )); do cat 'silesia.tar'; done | pigz > "$silesia5x"
    time benchmarkLegacyGzipTools 'result-decompression-silesia' "$silesia5x"  # 20 min
    'rm' "$silesia5x"
}


cd ~/indexed_bzip2/build
rm -f *.dat


nRepetitions=20
set -o pipefail


time benchmarkBase64   # 105 min
time benchmarkSilesia  # 55 min
time src/benchmarks/benchmarkSequential2023  # Benchmarks of components (this takes ~70 min)


# Benchmark over chunk size for one chosen parallelization
parallelization=16

fileSize=$(( parallelization * 512 * 1024 * 1024 ))
filePath="/dev/shm/base64.gz"
# Increase the block size because many smaller blocks trigger some kind of bug in pugz:
# https://github.com/Piezoid/pugz/issues/13
base64File512MiB="/dev/shm/base64-512MiB"
base64 /dev/urandom | head -c $(( 512 * 1024 * 1024 )) > "$base64File512MiB"
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
tar -cjf ~/pragzip-benchmark-results-$( date +%Y-%m-%dT%H-%M-%S ).tar.bz2 *.dat -C ~/pugz results-pugz-sync.dat -C ~ "hwloc-$( hostname ).xml" "hwloc-$( hostname ).svg" &&
rm *.dat
