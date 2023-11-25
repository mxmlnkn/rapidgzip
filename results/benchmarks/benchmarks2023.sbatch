#!/bin/bash
#SBATCH -A zihforschung
#SBATCH -p romeo
#SBATCH --exclusive
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=128
#SBATCH --time=06:00:00
#SBATCH --mem-per-cpu=1972M
#SBATCH --cpu-freq=2000000-2000000

module purge
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


workdir="$HOME/job-$SLURM_JOB_ID"
mkdir -- "$workdir" && cd -- "$workdir"
installPath="$workdir/tools"


if [[ -d "$installPath" ]]; then
    export PATH="$installPath/pigz:$installPath/isa-l/programs:$installPath/zstd:$installPath/zstd/contrib/pzstd:$installPath/lz4:$installPath/htslib:$installPath/lbzip2/src:$installPath/pixz/src:$installPath/indexed_bzip2/build/src/tools:$PATH"
else
    mkdir -p -- "$installPath"
    cd -- "$installPath"

    # Install pigz
    (
        git clone 'https://github.com/madler/pigz.git'
        cd pigz
        git checkout master
        m -B
    )
    export PATH=$installPath/pigz:$PATH

    # Install pugz
    (
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
        sed -n -E 's|.* in ([0-9.]+) s.*|'"$size "'\1|p' pugz.log > "$workdir/results-pugz-sync.dat"
        'rm' -f "$fname"
    )
    export PATH=$installPath/pugz:$PATH

    # Install igzip
    (
        git clone --depth 1 'https://github.com/intel/isa-l'
        cd isa-l
        ./autogen.sh
        ./configure
        m
    )
    export PATH=$installPath/isa-l/programs:$PATH

    # Install rapidgzip
    (
        git clone 'https://github.com/mxmlnkn/indexed_bzip2'
        cd indexed_bzip2
        git checkout -f rapidgzip-v0.9.0
        mkdir -p build
        cd build
        cmake -GNinja ..
        ninja rapidgzip benchmarkSequential2023
    )
    export PATH=$installPath/indexed_bzip2/build/src/tools:$PATH
fi

cd -- "$workdir"

# Generic system information
(
    echo -e "\n=== hwloc-info ==="
    hwloc-info
    echo -e "\n=== hwloc-ls ==="
    hwloc-ls
    lstopo -f --of xml "hwloc-$( hostname ).xml"
    lstopo -f --of svg "hwloc-$( hostname ).svg"
)


mkdir -p toolVersions
for tool in gzip pigz igzip gcc clang rapidgzip; do
    $tool --version 2>&1 | tee "toolVersions/$tool"
done


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

    #chunkSizes=( $(( 1 * 1024 )) $(( 2 * 1024 )) $(( 4 * 1024 )) $(( 8 * 1024 )) )
    chunkSizes=( $(( 4 * 1024 )) )
    for chunkSize in "${chunkSizes[@]}"; do
        echo '# parallelization dataSize/B runtime/s compressedDataSize/B' > "$filePrefix-pragzip-$(( chunkSize / 1024 ))-MiB-chunks-dev-null.dat"
    done
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

        # rapidgzip with index
        echo "Creating gzip index file using rapidgzip..."
        rapidgzip -P $( nproc ) -o '/dev/null' -f --export-index "${filePath}.gzindex" "$filePath"
        for (( i = 0; i < nRepetitions; ++i )); do
            tool="rapidgzip -d -P $parallelization -o /dev/null --import-index ${filePath}.gzindex"
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

        # rapidgzip
        for chunkSize in "${chunkSizes[@]}"; do
            echo "Benchmark rapidgzip with chunk size: $(( chunkSize / 1024 )) MiB"

            for (( i = 0; i < nRepetitions; ++i )); do
                tool="rapidgzip -d -P $parallelization -o /dev/null --chunk-size $chunkSize"
                ( time taskset --cpu-list 0-$(( parallelization - 1 )) $tool "$filePath" ) > output.log 2>&1
                errorCode=$?
                runtime=$( sed -n -E 's|real[ \t]*0m||p' output.log | sed 's|[ \ts]||' )
                if [[ -n "$runtime" && $errorCode -eq 0 ]]; then
                    echo "$parallelization $fileSize $runtime $compressedFileSize" |
                        tee -a "$filePrefix-pragzip-$(( chunkSize / 1024 ))-MiB-chunks-dev-null.dat"
                else
                    echo "Encountered error code $errorCode when executing $tool, runtime: $runtime"
                fi
            done
        done

        # pugz unsynchronized output
        if [[ ! "$filePrefix" =~ silesia ]]; then
            echo "Benchmark pugz with unsynchronized output..."

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

    'rm' -- "$filePath" 'output.log'
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


function benchmarkFastq()
{
    # 92 MiB extracts to 345 MiB (~3.75x)
    url='http://ftp.sra.ebi.ac.uk/vol1/fastq/SRR224/085/SRR22403185/SRR22403185_2.fastq.gz'

    # 252 MiB extracts to 1300 MiB (~5.16x)
    # Too large, this benchmark not even counting Silesia and base64 take only just under 6h!
    #url='http://ftp.sra.ebi.ac.uk/vol1/fastq/SRR224/085/SRR22401085/SRR22401085_2.fastq.gz'

    fileName="${url##*/}"
    wget -O "$fileName" "$url"

    uncompressedFile='/dev/shm/fastq'
    igzip -c -d "$fileName" > "$uncompressedFile"

    fastq2xFilePath='/dev/shm/fastq2x'
    for (( i = 0; i < 2; ++i )); do cat "$uncompressedFile"; done > "$fastq2xFilePath"
    time benchmarkParallelDecompressionScaling 'result-parallel-decompression-fastq' "$fastq2xFilePath"  # ? min

    # ? min
    time benchmarkPugzSyncParallelDecompressionScaling 'result-parallel-decompression-fastq' "$uncompressedFile"

    for (( i=0; i<5; ++i )); do cat "$uncompressedFile"; done | pigz > "$fastq2xFilePath.pigz"
    time benchmarkLegacyGzipTools 'result-decompression-fastq' "$fastq2xFilePath.pigz"  # 25 min
    'rm' -- "$uncompressedFile" "$fastq2xFilePath" "$fastq2xFilePath.pigz"
}


function benchmarkChunks()
{
    # Benchmark over chunk size for one chosen parallelization
    parallelization=128


    echo "Benchmark chunk sizes for rapidgzip with Silesia..."

    if [[ ! -f 'silesia.zip' || "$( md5sum silesia.zip | sed 's| .*||' )" != 'c240c17d6805fb8a0bde763f1b94cd99' ]]; then
        wget -O 'silesia.zip' 'https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip'
    fi

    rm -rf silesia/
    mkdir -p silesia && ( cd silesia && unzip ../silesia.zip )
    tar -cf silesia.tar silesia/  # 211957760 B -> 212 MB, 203 MiB, gzip 66 MiB -> compression factor: 3.08
    inputFilePath='silesia.tar'

    fileSize=$(( 2 * parallelization * $( stat --format=%s -- "$inputFilePath" ) ))
    filePath="/dev/shm/silesia.tar.gz"

    # Increase the block size because many smaller blocks trigger some kind of bug in pugz:
    # https://github.com/Piezoid/pugz/issues/13
    for (( i=0; i<parallelization * 2; ++i )); do cat "$inputFilePath"; done |
        pigz --blocksize $(( parallelization * 4 * 1024 )) > "$filePath"

    echo "Benchmark chunk sizes for rapidgzip..."
    echo '# parallelization chunkSize/B dataSize/B runtime/s' > 'result-chunk-size-pragzip-dev-null.dat'
    for chunkSize in 128 256 512 $(( 1*1024 )) $(( 2*1024 )) $(( 4*1024 )) $(( 8*1024 )) $(( 16*1024 )) $(( 32*1024 )) \
                     $(( 64*1024 )) $(( 128*1024 )) $(( 256*1024 )) $(( 512*1024 )) $(( 1024*1024 )); do
        # rapidgzip
        for (( i = 0; i < nRepetitions; ++i )); do
            tool="rapidgzip -d -P $parallelization -o /dev/null --chunk-size $chunkSize"
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

    'rm' -- "$filePath"



    fileSize=$(( parallelization * 512 * 1024 * 1024 ))
    filePath="/dev/shm/base64.gz"
    # Increase the block size because many smaller blocks trigger some kind of bug in pugz:
    # https://github.com/Piezoid/pugz/issues/13
    base64File512MiB="/dev/shm/base64-512MiB"
    base64 /dev/urandom | head -c $(( 512 * 1024 * 1024 )) > "$base64File512MiB"

    # Increase the block size because many smaller blocks trigger some kind of bug in pugz:
    # https://github.com/Piezoid/pugz/issues/13
    for (( i=0; i<parallelization; ++i )); do cat "$inputFilePath"; done |
        pigz --blocksize $(( parallelization * 4 * 1024 )) > "$filePath"

    echo "Benchmark chunk sizes for rapidgzip..."
    echo '# parallelization chunkSize/B dataSize/B runtime/s' > 'result-chunk-size-pragzip-dev-null.dat'
    for chunkSize in 128 256 512 $(( 1*1024 )) $(( 2*1024 )) $(( 4*1024 )) $(( 8*1024 )) $(( 16*1024 )) $(( 32*1024 )) \
                     $(( 64*1024 )) $(( 128*1024 )) $(( 256*1024 )) $(( 512*1024 )) $(( 1024*1024 )); do
        # rapidgzip
        for (( i = 0; i < nRepetitions; ++i )); do
            tool="rapidgzip -d -P $parallelization -o /dev/null --chunk-size $chunkSize"
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

    echo "Benchmark chunk sizes for pugz..."
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

    'rm' -- "$filePath"
}



nRepetitions=20
set -o pipefail


#time benchmarkSilesia  # 55 min
time benchmarkBase64   # 105 min (longer because pugz is also benchmarked)
#time benchmarkFastq     # probably ~105 min (longer because pugz is also benchmarked)
#time src/benchmarks/benchmarkSequential2023  # Benchmarks of components (this takes ~70 min)

benchmarkChunks


# Back all data up into a TAR archive

echo "[$( date --iso-8601=seconds )] Bundle benchmark results into a TAR..."


# Output log file might bot be flushed but better than nothing. Should be copied manually anyway!
'cp' "$( scontrol show job "$SLURM_JOBID" | sed -n -E 's|.*StdErr=(.*)|\1|p' )" ./
'cp' "$( scontrol show job "$SLURM_JOBID" | sed -n -E 's|.*StdOut=(.*)|\1|p' )" ./
sacct --format=jobid,jobidraw,jobname,partition,maxvmsize,maxvmsizenode,maxvmsizetask,avevmsize,maxrss,maxrssnode,maxrsstask,averss,maxpages,maxpagesnode,maxpagestask,avepages,mincpu,mincpunode,mincputask,avecpu,ntasks,alloccpus,elapsed,state,exitcode,avecpufreq,reqcpufreqmin,reqcpufreqmax,reqcpufreqgov,reqmem,consumedenergy,maxdiskread,maxdiskreadnode,maxdiskreadtask,avediskread,maxdiskwrite,maxdiskwritenode,maxdiskwritetask,avediskwrite,reqtres,alloctres,tresusageinave,tresusageinmax,tresusageinmaxn,tresusageinmaxt,tresusageinmin,tresusageinminn,tresusageinmint,tresusageintot,tresusageoutmax,tresusageoutmaxn,tresusageoutmaxt,tresusageoutave,tresusageouttot,admincomment,allocnodes,associd,blockid,cluster,comment,constraints,consumedenergy,consumedenergyraw,cputime,cputimeraw,dbindex,derivedexitcode,elapsedraw,eligible,end,flags,gid,layout,maxpagestask,mcslabel,ncpus,nnodes,nodelist,priority,qos,qosraw,reason,reqcpufreq,reqcpus,reqnodes,reservation,reservationid,reserved,resvcpu,resvcpuraw,start,submit,suspended,systemcpu,systemcomment,timelimit,timelimitraw,totalcpu,tresusageinmaxnode,tresusageinmaxtask,tresusageinminnode,tresusageinmintask,tresusageoutmaxnode,tresusageoutmaxtask,tresusageoutmin,tresusageoutminnode,tresusageoutmintask,uid,usercpu,wckey,wckeyid -p -j "$SLURM_JOB_ID" > "sacct-$SLURM_JOB_ID.log"


tar -cj --transform="s|${HOME#/}/||" -f ~/rapidgzip-comparison-benchmarks-$( date +%Y-%m-%dT%H-%M-%S ).tar.bz2 "$workdir"

echo "[$( date --iso-8601=seconds )] Done."
