#!/bin/bash
#SBATCH -A zihforschung
#SBATCH -p romeo
#SBATCH --exclusive
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=128
#SBATCH --time=04:00:00
#SBATCH --mem-per-cpu=1972M
#SBATCH --cpu-freq=2000000-2000000

module purge
module load CMake Ninja Clang NASM hwloc bzip2
# because module load bzip2 automatically switches to 11.3.0 while the previous benchmarks have been made with 12.2.0
module load GCCcore/12.2.0
module list

echo "[$( date --iso-8601=seconds )] Begin installing tools to benchmark..."  # 10:14:25 -> 10:17:43 = ~3 min

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

'cp' "$( scontrol show job "$SLURM_JOBID" | sed -n -E 's|.*Command=(.*)|\1|p' )" ./

if [[ -d "$installPath" ]]; then
    export PATH="$installPath/pigz:$installPath/isa-l/programs:$installPath/zstd:$installPath/zstd/contrib/pzstd:$installPath/lz4:$installPath/htslib:$installPath/lbzip2/src:$installPath/pixz/src:$installPath/indexed_bzip2/build/src/tools:$PATH"
else
    mkdir -p -- "$installPath"
    cd -- "$installPath"

    # Install pigz
    (
        version=2.7
        wget -O- "https://github.com/madler/pigz/archive/refs/tags/v$version.tar.gz" | tar -xz
        mv "pigz-$version" "pigz"
        cd "pigz"
        m -B
    )
    export PATH=$installPath/pigz:$PATH

    # Install igzip
    (
        git clone --depth 1 'https://github.com/intel/isa-l'
        cd isa-l
        ./autogen.sh
        ./configure
        m
    )
    export PATH=$installPath/isa-l/programs:$PATH

    # Install zstd
    (
        version=1.5.4
        wget -O- "https://github.com/facebook/zstd/releases/download/v$version/zstd-$version.tar.gz" | tar -xz
        mv "zstd-$version" "zstd"
        cd "zstd"
        m -B
        cd "contrib/pzstd"
        m -B
    )
    export PATH=$installPath/zstd:$PATH
    export PATH=$installPath/zstd/contrib/pzstd:$PATH

    # Install lz4
    (
        version=1.9.4
        wget -O- "https://github.com/lz4/lz4/archive/refs/tags/v$version.tar.gz" | tar -xz
        mv "lz4-$version" "lz4"
        cd "lz4"
        m
        m -B
    )
    export PATH=$installPath/lz4:$PATH

    # Install bgzip
    (
        version=1.17
        wget -O- "https://github.com/samtools/htslib/releases/download/$version/htslib-$version.tar.bz2" | tar -xj
        mv "htslib-$version" "htslib"
        cd "htslib"
        ./configure
        m bgzip
    )
    export PATH=$installPath/htslib:$PATH

    # Install lbzip2
    (
        version=2.5
        wget -O- "http://archive.ubuntu.com/ubuntu/pool/universe/l/lbzip2/lbzip2_$version.orig.tar.bz2" | tar -xj
        mv "lbzip2-$version" "lbzip2"
        cd "lbzip2"
        ./configure
        m
    )
    export PATH=$installPath/lbzip2/src:$PATH

    # Install pixz
    (
        version=1.0.7
        wget -O- "https://github.com/vasi/pixz/releases/download/v$version/pixz-$version.tar.bz2" | tar -xj
        mv "pixz-$version" "pixz"
        cd "pixz"
        ./configure
        m
    )
    export PATH=$installPath/pixz/src:$PATH

    # Install rapidgzip
    (
        git clone 'https://github.com/mxmlnkn/indexed_bzip2'
        cd indexed_bzip2
        git fetch origin
        git checkout -f rapidgzip-v0.10.1
        mkdir -p build
        cd build
        CXX=clang++ CC=clang cmake -GNinja ..
        m rapidgzip
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
for tool in gzip lz4 pigz igzip zstd pzstd bzip2 lbzip2 bgzip pixz gcc clang rapidgzip; do
    $tool --version 2>&1 | tee "toolVersions/$tool"
done


echo "[$( date --iso-8601=seconds )] Compile Silesia test data..."  # 13:15:31 -> 13:22:59 = ~ 7 min 30 s

nRepetitions=20
set -o pipefail


BEEGFSWS=$( ws_allocate --duration 1 --filesystem beegfs decompression-benchmarks )


function getSilesiaTar()
{
    if [[ ! -f 'silesia.zip' || "$( md5sum silesia.zip | sed 's| .*||' )" != 'c240c17d6805fb8a0bde763f1b94cd99' ]]; then
        wget -O 'silesia.zip' 'https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip'
    fi

    rm -rf silesia/
    mkdir -p silesia && ( cd silesia && unzip ../silesia.zip )
    tar -cf silesia.tar silesia/  # 211957760 B -> 212 MB, 203 MiB, gzip 66 MiB -> compression factor: 3.08
}


function compareCompressionLevels()
{
    # benchmarks to show wide applicability of rapidgzip no matter the compressor

    compressors=(
        'igzip -0'                # ~1s for 1 Silesia compression, ~7 s for parallel decompression
        'igzip -1'                # ~1s for 1 Silesia compression, ~7 s for parallel decompression
        # -2 is the default https://github.com/intel/isa-l/blob/master/programs/igzip_cli.c#L78
        'igzip -2'                # ~1s for 1 Silesia compression, ~7 s for parallel decompression
        'igzip -3'                # ~3s for 1 Silesia compression, ~7 s for parallel decompression

        'bgzip -l -1'  # default  # ~6 s for parallel decompression -> ~2 min for 20 repetitions
        'bgzip -l 0'              # ~3 s for parallel decompression
        'bgzip -l 3'              # ~6 s for parallel decompression
        'bgzip -l 6'              # ~6 s for parallel decompression
        'bgzip -l 9'              # ~6 s for parallel decompression

        'gzip -1'  # --fast       # ~ 5s for 1 Silesia compression, ~7 s for parallel decompression
        'gzip -3'                 # ~ 5s for 1 Silesia compression, ~7 s for parallel decompression
        'gzip -6'  # default      # ~10s for 1 Silesia compression, ~7 s for parallel decompression
        'gzip -9'  # --best       # ~25s for 1 Silesia compression, ~7 s for parallel decompression

        'pigz -1'  # --fast
        'pigz -3'
        'pigz -6'  # default
        'pigz -9'  # --best
    )
    # Including all levels would result in 34 variants instead of the 17 selected here!


    # Gather information to derive the deflate block counts from for comparison
    mkdir -p compressionInfo
    for compressor in "${compressors[@]}"; do
        compressedFile="/dev/shm/tmp-single-silesia.gz"
        $compressor -c silesia.tar > "$compressedFile"
        rapidgzip --analyze "$compressedFile" &> "compressionInfo/silesia.$compressor.log"
    done



    parallelization=128  # Cannot be changed easily because of the time-optimized compression code!

    echo "Will use $parallelization parallelization."

    pathToCompress="$BEEGFSWS/silesia-$(( 2 * parallelization ))x.tar"
    for (( i = 0; i < 2 * parallelization; ++i )); do cat silesia.tar; done > "$pathToCompress"

    find "$BEEGFSWS" -size 0 -delete
    for compressor in "${compressors[@]}"; do
        if [[ -f "$pathToCompress.$compressor" ]]; then continue; fi

        if [[ "$compressor" == 'igzip -0' ]]; then
            # igzip -0 compressed files cannot be decompressed by rapidgzip in parallel.
            # Therefore, compress only 2 tarball concatenations.
            for (( i = 0; i < 8; ++i )); do cat silesia.tar; done | $compressor -c > "$pathToCompress.$compressor" &
        elif [[ "$compressor" == 'gzip -6' || "$compressor" == 'gzip -9' ]]; then
            # The single-core compressors are too slow to compress 60 GB of data and we don't even have
            # sufficient parallelism, with only 8 different single-core versions but 128 cores.
            # It would be a waste, so compress only once and simply concatenate the gzips.
            # This is possible because we don't want to benchmark pugz, which does not support multi-stream gzips!
            # However, especially for igzip -0 we cannot concatenate (too much) because that would add detectable
            # barriers for rapidgzip to use and therefore falsify benchmarks!
            # Compression takes ~6 min anyway, so compress as much as possible in that time even with single-core gzip.
            # This would be 600 s / 25 s = 24 for gzip -9. "Round" down to 16. Full single-core compression
            echo "[$( date --iso-8601=seconds )] Compress Silesia with $compressor" \
                 "by concatenating $(( 2 * parallelization / 16 )) parts..."
            for (( i = 0; i < 16; ++i )); do cat silesia.tar; done | $compressor -c > "$pathToCompress.$compressor.part" &
        elif [[ "$compressor" =~ ^gzip.*$ || "$compressor" == 'igzip -3' ]]; then
            echo "[$( date --iso-8601=seconds )] Compress Silesia with $compressor" \
                 "by concatenating $(( 2 * parallelization / 64 )) parts..."
            for (( i = 0; i < 64; ++i )); do cat silesia.tar; done | $compressor -c > "$pathToCompress.$compressor.part" &
        elif [[ "$compressor" =~ ^bgzip.*$ ]]; then
            # Note that bgzip and pigz are also run in background and therefore in parallel because piping to
            # stdout limits parallel compression. Unfortunately, neither bgzip nor pigz can specify an arbitrary
            # output file!
            # Note that bgzip by default only uses 1 core!
            $compressor -c --threads $( nproc ) "$pathToCompress" > "$pathToCompress.$compressor" &
        else
            $compressor -c "$pathToCompress" > "$pathToCompress.$compressor" &
        fi
    done
    wait  # Wait for single-core compressions to finish

    echo "[$( date --iso-8601=seconds )] Concatenate single-core compressed parts to larger data streams..."
    for compressor in "${compressors[@]}"; do
        if [[ -f "$pathToCompress.$compressor" ]]; then continue; fi
        if [[ "$compressor" == 'igzip -0' ]]; then continue; fi

        if [[ "$compressor" == 'gzip -6' || "$compressor" == 'gzip -9' ]]; then
            for (( i = 0; i < 2 * parallelization / 16; ++i )); do
                cat "$pathToCompress.$compressor.part"
            done > "$pathToCompress.$compressor" &
        elif [[ "$compressor" =~ ^gzip.*$ || "$compressor" == 'igzip -3' ]]; then
            for (( i = 0; i < 2 * parallelization / 64; ++i )); do
                cat "$pathToCompress.$compressor.part"
            done > "$pathToCompress.$compressor" &
        fi
    done
    wait


    # 11:07:24 -> 12:48:00 for bgzip -l -1, 0, 3, 6, 9, gzip -1, -3, -6. I.e., 8 compressors in 100min
    #   -> ~12min per compressor :/
    echo "[$( date --iso-8601=seconds )] Benchmark rapidgzip with differently compressed gzip files..."

    dataFile='compression-levels-pragzip-dev-null.dat'
    echo '# compressor parallelization dataSize/B runtime/s compressedDataSize/B' | tee "$dataFile"
    for compressor in "${compressors[@]}"; do
        if [[ "$compressor" == 'igzip -0' ]]; then
            fileSize=$(( 8 * $( stat -L --format=%s silesia.tar ) ))
        else
            fileSize=$( stat -L --format=%s "$pathToCompress" )
        fi

        compressedPath="$pathToCompress.$compressor"
        compressedFileSize=$( stat -L --format=%s "$compressedPath" )

        pathToDecompress="/dev/shm/tmp-compressed-silesia"
        'cp' -- "$compressedPath" "$pathToDecompress"
        tool='rapidgzip'

        for (( i = 0; i < nRepetitions; ++i )); do
            runtime=$( ( time taskset --cpu-list 0-$(( parallelization - 1 )) \
                         $tool -P $parallelization -d -o /dev/null "$pathToDecompress" | wc -c ) 2>&1 |
                           sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
            bandwidth=$( python3 -c "print( int( round( $fileSize / 1e6 / $runtime ) ) )" )

            printf '[%s] | %13s | %9s | %7s |\n' "$( date --iso-8601=seconds )" "$compressor" "$runtime" "$bandwidth"
            echo "$compressor;$parallelization;$fileSize;$runtime;$compressedFileSize" | tee -a "$dataFile"
        done

        'rm' -- "$pathToDecompress"
    done
}

# Compare rapidgzip to other compression formats
function compareCompressionFormats()
{
    # Table: compressor | compression ratio | parallelization | decompression bandwidth
    # One page has ~60 lines. We really have to fit this table into one column!
    # I think I can skip pixz because it is so much slower >.>
    # Things I want to show:
    # - Maximum performance at 128 cores
    # - That pzstd stops scaling at ~16 cores
    # - That pzstd and bgzip require a suitably compressed file
    # - That lz4 is pretty fast
    # - That igzip is faster than rapidgzip and therefore shows optimization potential -> Already shown in Figure 10. Don't
    # - That rapidgzip is faster for bgzip files -> Already shown in above benchmarks. Don't!
    # Note that all of these support -c for writing to stdout and -d for forcing decompression
    benchmarks=(
        # compressor            # Decompressor
        # Single-core
        'lz4'                   'lz4'                                         # ~ 12 s
        'bzip2'                 'lbzip2 -k -c -n 1'                           # ~ 3 min
        #'gzip'                  'igzip'
        'gzip'                  'rapidgzip -o /dev/null -P 1'                 # ~ 1 min
        'gzip'                  'rapidgzip -o /dev/null -P 1 --import-index'  # ~ 1 min
        'gzip'                  'bgzip -k -c --threads 1'                     # ~ 30 s
        'bgzip'                 'bgzip -k -c --threads 1'                     # ~ 30 s
        'gzip'                  'igzip -k -f -o /dev/null'                    # ~ 30 s
        #'bgzip'                 'rapidgzip -P 1'
        #'xz'                    'xz'
        #'xz'                    'pixz -p 1'
        #'pixz'                  'pixz -p 1'
        'zstd'                  'zstd -o /dev/null'                           # ~ 15 s
        'zstd'                  'pzstd -o /dev/null -p 1'                     # ~ 15 s
        'pzstd'                 'pzstd -o /dev/null -p 1'                     # ~ 15 s
        'pzstd'                 'pzstd -o /dev/null -p 1 --no-check'          # ~ 15 s

        # Compare all multi-core capable tools at 16 cores
        'bzip2'                 'lbzip2 -k -c -n 16'                          # ~ 4 min
        'gzip'                  'rapidgzip -o /dev/null -P 16'                # ~ 2 min
        'gzip'                  'rapidgzip -o /dev/null -P 16 --import-index' # ~ 1 min
        'gzip'                  'bgzip -k -c --threads 16'                    # ~ 14 min
        'bgzip'                 'bgzip -k -c --threads 16'                    # ~ 90 s
        #'bgzip'                 'rapidgzip -P 16'
        #'xz'                    'pixz -p 16'
        #'pixz'                  'pixz -p 16
        'zstd'                  'pzstd -o /dev/null -p 16'                    # ~ 3 min
        'pzstd'                 'pzstd -o /dev/null -p 16'                    # ~ 1 min
        'pzstd'                 'pzstd -o /dev/null -p 16 --no-check'         # ~ ? min

        # Compare all at 128 cores
        'bzip2'                 'lbzip2 -k -c -n 128'                         # ~ 10 min
        'gzip'                  'rapidgzip -o /dev/null -P 128'                # ~ 8 min
        'gzip'                  'rapidgzip -o /dev/null -P 128 --import-index' # ~ 7 min ?! WHY? Should be twice as fast!
        'bgzip'                 'bgzip -k -c --threads 128'                   # ~ 9 min
        #'pixz'                  'pixz -p 128'
        #'zstd'                  'pzstd -o /dev/null -p 128'
        'pzstd'                 'pzstd -o /dev/null -p 128'                   # ~ 8 min
        'pzstd'                 'pzstd -o /dev/null -p 128 --no-check'        # ~ ? min
    )


    echo "[$( date --iso-8601=seconds )] Compile Silesia test data for benchmarks with other compression formats..."

    # Compressing bz2 is excruciatingly slow, simply concatenate the bzip2 compressed files,
    # which the bz2 file format allows to do.

    bzip2 -c silesia.tar > /dev/shm/silesia.tar.bz2
    for parallelization in 1 16 128; do
        pathToCompress="$BEEGFSWS/silesia-$(( 2 * parallelization ))x.tar"
        for (( i = 0; i < 2 * parallelization; ++i )); do cat /dev/shm/silesia.tar.bz2; done > "$pathToCompress.bzip2"
    done
    'rm' /dev/shm/silesia.tar.bz2


    for parallelization in 1 16 128; do
        pathToCompress="$BEEGFSWS/silesia-$(( 2 * parallelization ))x.tar"
        for (( i = 0; i < 2 * parallelization; ++i )); do cat silesia.tar; done > "$pathToCompress"
    done
    find "$BEEGFSWS" -size 0 -delete
    for (( i = 0; i < ${#benchmarks[@]}; i += 2 )); do
        compressor=${benchmarks[i]}
        decompressor=${benchmarks[i+1]}
        parallelization=$( printf %s "$decompressor" | sed -nE 's|.* ([0-9]+).*|\1|p' )
        if [[ -z "$parallelization" ]]; then parallelization=1; fi

        pathToCompress="$BEEGFSWS/silesia-$(( 2 * parallelization ))x.tar"
        compressedPath="$pathToCompress.$compressor"
        if [[ -f "$compressedPath" ]]; then continue; fi

        # Skip and instead create it through concatenation because compression with gzip is too slow
        if [[ "$compressedPath" =~ ^.*silesia-256x.tar.gzip$ ]]; then continue; fi

        if [[ "$compressor" == 'bgzip' ]]; then
            $compressor --threads $( nproc ) -c "$pathToCompress" > "$compressedPath" &
        else
            $compressor -c "$pathToCompress" > "$compressedPath" &
        fi
    done
    wait

    for (( i = 0; i < 256 / 32; ++i )); do cat "$BEEGFSWS/silesia-32x.tar.gzip"; done > "$BEEGFSWS/silesia-256x.tar.gzip"

    # Create rapidgzip indexes
    for (( i = 0; i < ${#benchmarks[@]}; i += 2 )); do
        compressor=${benchmarks[i]}
        decompressor=${benchmarks[i+1]}
        parallelization=$( printf %s "$decompressor" | sed -nE 's|.* ([0-9]+).*|\1|p' )
        if [[ -z "$parallelization" ]]; then parallelization=1; fi

        pathToCompress="$BEEGFSWS/silesia-$(( 2 * parallelization ))x.tar"
        compressedPath="$pathToCompress.$compressor"
        pathToIndex="$compressedPath.gzindex"
        if [[ ! -f "$pathToIndex" && "$decompressor" =~ ^rapidgzip.*$ ]]; then
            echo "Creating gzip index file using rapidgzip..."
            rapidgzip -P $( nproc ) -o '/dev/null' -f --export-index "$pathToIndex" "$compressedPath"
        fi
    done
    wait


    echo "[$( date --iso-8601=seconds )] Benchmarks other compression formats..."

    dataFile='compression-formats-dev-null.dat'
    echo '# compressor decompressor parallelization dataSize/B runtime/s compressedDataSize/B' | tee "$dataFile"
    for (( i = 0; i < ${#benchmarks[@]}; i += 2 )); do
        compressor=${benchmarks[i]}
        decompressor=${benchmarks[i+1]}
        parallelization=$( printf %s "$decompressor" | sed -nE 's|.* ([0-9]+).*|\1|p' )
        if [[ -z "$parallelization" ]]; then parallelization=1; fi

        pathToCompress="$BEEGFSWS/silesia-$(( 2 * parallelization ))x.tar"
        fileSize=$( stat -L --format=%s "$pathToCompress" )

        compressedPath="$pathToCompress.$compressor"
        compressedFileSize=$( stat -L --format=%s "$compressedPath" )

        pathToDecompress="/dev/shm/tmp-compressed-silesia"
        'cp' -- "$compressedPath" "$pathToDecompress"

        if [[ $decompressor =~ ^.*import-index$ ]]; then
            echo "Creating gzip index file using rapidgzip..."
            rapidgzip -P $( nproc ) -o '/dev/null' -f --export-index "$pathToDecompress.gzindex" "$compressedPath"
        fi

        for (( j = 0; j < nRepetitions; ++j )); do
            # LZ4 has no -o option but the output file can be specified as the last argument.
            if [[ $decompressor == 'lz4' ]]; then
                ( time taskset --cpu-list 0-$(( parallelization - 1 )) \
                  $decompressor -d "$pathToDecompress" /dev/null > /dev/null ) > output.log 2>&1
            elif [[ $decompressor =~ ^.*import-index$ ]]; then
                ( time taskset --cpu-list 0-$(( parallelization - 1 )) \
                  $decompressor "$pathToDecompress.gzindex" -d "$pathToDecompress" > /dev/null ) > output.log 2>&1
            else
                ( time taskset --cpu-list 0-$(( parallelization - 1 )) \
                  $decompressor -d "$pathToDecompress" > /dev/null ) > output.log 2>&1
            fi

            errorCode=$?
            runtime=$( sed -n -E 's|real[ \t]*0m||p' output.log | sed 's|[ \ts]||' )
            bandwidth=$( python3 -c "print( int( round( $fileSize / 1e6 / $runtime ) ) )" )

            printf '[%s] | %8s | %13s | %9s | %7s |\n' \
                "$( date --iso-8601=seconds )" "$compressor" "$decompressor" "$runtime" "$bandwidth"
            echo "$compressor;$decompressor;$parallelization;$fileSize;$runtime;$compressedFileSize" |
                tee -a "$dataFile"

        done

        'rm' -- "$pathToDecompress" 'output.log'
    done
}


time getSilesiaTar               # ~25 s
time compareCompressionLevels    # ~80 min
time compareCompressionFormats   # ~40 min


# Back all data up into a TAR archive

echo "[$( date --iso-8601=seconds )] Bundle benchmark results into a TAR..."


# Output log file might bot be flushed but better than nothing. Should be copied manually anyway!
'cp' "$( scontrol show job "$SLURM_JOBID" | sed -n -E 's|.*StdErr=(.*)|\1|p' )" ./
'cp' "$( scontrol show job "$SLURM_JOBID" | sed -n -E 's|.*StdOut=(.*)|\1|p' )" ./
sacct --format=jobid,jobidraw,jobname,partition,maxvmsize,maxvmsizenode,maxvmsizetask,avevmsize,maxrss,maxrssnode,maxrsstask,averss,maxpages,maxpagesnode,maxpagestask,avepages,mincpu,mincpunode,mincputask,avecpu,ntasks,alloccpus,elapsed,state,exitcode,avecpufreq,reqcpufreqmin,reqcpufreqmax,reqcpufreqgov,reqmem,consumedenergy,maxdiskread,maxdiskreadnode,maxdiskreadtask,avediskread,maxdiskwrite,maxdiskwritenode,maxdiskwritetask,avediskwrite,reqtres,alloctres,tresusageinave,tresusageinmax,tresusageinmaxn,tresusageinmaxt,tresusageinmin,tresusageinminn,tresusageinmint,tresusageintot,tresusageoutmax,tresusageoutmaxn,tresusageoutmaxt,tresusageoutave,tresusageouttot,admincomment,allocnodes,associd,blockid,cluster,comment,constraints,consumedenergy,consumedenergyraw,cputime,cputimeraw,dbindex,derivedexitcode,elapsedraw,eligible,end,flags,gid,layout,maxpagestask,mcslabel,ncpus,nnodes,nodelist,priority,qos,qosraw,reason,reqcpufreq,reqcpus,reqnodes,reservation,reservationid,reserved,resvcpu,resvcpuraw,start,submit,suspended,systemcpu,systemcomment,timelimit,timelimitraw,totalcpu,tresusageinmaxnode,tresusageinmaxtask,tresusageinminnode,tresusageinmintask,tresusageoutmaxnode,tresusageoutmaxtask,tresusageoutmin,tresusageoutminnode,tresusageoutmintask,uid,usercpu,wckey,wckeyid -p -j "$SLURM_JOB_ID" > "sacct-$SLURM_JOB_ID.log"


( cd -- "$workdir" && 'rm' -rf silesia.zip silesia silesia.tar; )
tar -cj --transform="s|${HOME#/}/||" -f ~/rapidgzip-comparison-benchmarks-$( date +%Y-%m-%dT%H-%M-%S ).tar.bz2 "$workdir"

ws_release --filesystem beegfs decompression-benchmarks

echo "[$( date --iso-8601=seconds )] Done."
