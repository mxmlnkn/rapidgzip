#!/bin/bash
#SBATCH -A zihforschung
#SBATCH -p romeo
#SBATCH --exclusive
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=128
#SBATCH --time=08:00:00
#SBATCH --mem-per-cpu=1972M
#SBATCH --cpu-freq=2000000-2000000

module purge
module load CMake Ninja Clang NASM hwloc bzip2
# because module load bzip2 automatically switches to 11.3.0 while the previous benchmarks have been made with 12.2.0
module load GCCcore/12.2.0
module list

echo "[$( date --iso-8601=seconds )] Begin installing tools to benchmark..."

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
        git clone 'https://github.com/madler/pigz.git'
        cd pigz
        git checkout master
        m -B
    )
    export PATH=$installPath/pigz:$PATH

    # Install rapidgzip
    (
        git clone 'https://github.com/mxmlnkn/indexed_bzip2'
        cd indexed_bzip2
        git fetch origin
        # rapidgzip-v0.8.1 cannot be used because cxxopts is pinned on a commit that only exists in my fork.
        git checkout -f ed5c77f7
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
for tool in gzip gcc clang rapidgzip; do
    $tool --version 2>&1 | tee "toolVersions/$tool"
done


echo "[$( date --iso-8601=seconds )] Compile Silesia test data..."  # 13:15:31 -> 13:22:59 = ~ 7 min 30 s

nRepetitions=5
set -o pipefail


function getSilesiaTar()
{
    if [[ ! -f 'silesia.zip' || "$( md5sum silesia.zip | sed 's| .*||' )" != 'c240c17d6805fb8a0bde763f1b94cd99' ]]; then
        wget -O 'silesia.zip' 'https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip'
    fi

    rm -rf silesia/
    mkdir -p silesia && ( cd silesia && unzip ../silesia.zip )
    tar -cf silesia.tar silesia/
}


function benchmarkCommits()
{
    cd -- "$installPath/indexed_bzip2/build"

    # Create or empty result file
    for testFile in "${testFiles[@]}"; do
        resultsFile="$workdir/${testFile##*/}-commit-timings.dat"
        > "$resultsFile"
    done

    commits=( $( git log --oneline 7ce43a93~1..HEAD |
                     grep '[[]\(feature\|refactor\|fix\|performance\)[]]' | sed 's| .*||' ) )
    for commit in "${commits[@]}"; do
        git checkout -f "$commit" || break
        git submodule update --init --recursive
        ( cd ../src/external/cxxopts/ && git checkout HEAD . )

        rm -f src/tools/{pragzip,rapidgzip}
        cmake --build . --parallel $( nproc ) -- pragzip
        cmake --build . --parallel $( nproc ) -- rapidgzip

        tool=
        if [[ -f src/tools/pragzip ]]; then tool=src/tools/pragzip; fi
        if [[ -f src/tools/rapidgzip ]]; then tool=src/tools/rapidgzip; fi
        if [[ -z "$tool" ]]; then continue; fi

        # Benchmark for different test files and write results to respective result files
        for testFile in "${testFiles[@]}"; do
            resultsFile="$workdir/${testFile##*/}-commit-timings.dat"
            printf '%s' "$( git rev-parse HEAD )" >> "$resultsFile"
            for (( i = 0; i < nRepetitions; ++i )); do
                runtime=$( ( time "$tool" -d -o /dev/null "$testFile" ) 2>&1 |sed -nr 's|real.*0m([0-9.]+)s|\1|p' )
                printf ' %s' "$runtime" >> "$resultsFile"
            done
            echo >> "$resultsFile"
        done
    done
}


# 92 MiB extracts to 345 MiB (~3.75x)
url='http://ftp.sra.ebi.ac.uk/vol1/fastq/SRR224/085/SRR22403185/SRR22403185_2.fastq.gz'
fileName="${url##*/}"
wget -O "$fileName" "$url"
fileGrowFactor=8
fastqTestFile="/dev/shm/${fileGrowFactor}x$fileName"
for (( i = 0; i < fileGrowFactor; ++i )); do gzip -d -c "$fileName"; done |
    pigz --blocksize $(( 128 * 4 * 1024 )) > "$fastqTestFile"

# 211957760 B -> 212 MB, 203 MiB, gzip 66 MiB -> compression factor: 3.08
getSilesiaTar
fileName='silesia.tar'
fileGrowFactor=16
silesiaTestFile="/dev/shm/${fileGrowFactor}x$fileName.gz"
for (( i = 0; i < fileGrowFactor; ++i )); do cat "$fileName"; done |
    pigz --blocksize $(( 128 * 4 * 1024 )) > "$silesiaTestFile"


# /dev/urandom reading is 60 MB/s slow!
# That's why we pregenerate 512 MiB of random data and then simply repeat that as needed.
fileName='/dev/shm/base64-512MiB'
base64 /dev/urandom | head -c $(( 512 * 1024 * 1024 )) > "$fileName"
fileGrowFactor=16
base64TestFile="/dev/shm/base64-8GiB.gz"
for (( i = 0; i < fileGrowFactor; ++i )); do cat "$fileName"; done |
    pigz --blocksize $(( 128 * 4 * 1024 )) > "$base64TestFile"


testFiles=( "$fastqTestFile" "$silesiaTestFile" "$base64TestFile" )
time benchmarkCommits


# Back all data up into a TAR archive

cd "$workdir"
echo "[$( date --iso-8601=seconds )] Bundle benchmark results into a TAR..."


# Output log file might bot be flushed but better than nothing. Should be copied manually anyway!
'cp' "$( scontrol show job "$SLURM_JOBID" | sed -n -E 's|.*StdErr=(.*)|\1|p' )" ./
'cp' "$( scontrol show job "$SLURM_JOBID" | sed -n -E 's|.*StdOut=(.*)|\1|p' )" ./
sacct --format=jobid,jobidraw,jobname,partition,maxvmsize,maxvmsizenode,maxvmsizetask,avevmsize,maxrss,maxrssnode,maxrsstask,averss,maxpages,maxpagesnode,maxpagestask,avepages,mincpu,mincpunode,mincputask,avecpu,ntasks,alloccpus,elapsed,state,exitcode,avecpufreq,reqcpufreqmin,reqcpufreqmax,reqcpufreqgov,reqmem,consumedenergy,maxdiskread,maxdiskreadnode,maxdiskreadtask,avediskread,maxdiskwrite,maxdiskwritenode,maxdiskwritetask,avediskwrite,reqtres,alloctres,tresusageinave,tresusageinmax,tresusageinmaxn,tresusageinmaxt,tresusageinmin,tresusageinminn,tresusageinmint,tresusageintot,tresusageoutmax,tresusageoutmaxn,tresusageoutmaxt,tresusageoutave,tresusageouttot,admincomment,allocnodes,associd,blockid,cluster,comment,constraints,consumedenergy,consumedenergyraw,cputime,cputimeraw,dbindex,derivedexitcode,elapsedraw,eligible,end,flags,gid,layout,maxpagestask,mcslabel,ncpus,nnodes,nodelist,priority,qos,qosraw,reason,reqcpufreq,reqcpus,reqnodes,reservation,reservationid,reserved,resvcpu,resvcpuraw,start,submit,suspended,systemcpu,systemcomment,timelimit,timelimitraw,totalcpu,tresusageinmaxnode,tresusageinmaxtask,tresusageinminnode,tresusageinmintask,tresusageoutmaxnode,tresusageoutmaxtask,tresusageoutmin,tresusageoutminnode,tresusageoutmintask,uid,usercpu,wckey,wckeyid -p -j "$SLURM_JOB_ID" > "sacct-$SLURM_JOB_ID.log"


( cd -- "$workdir" && 'rm' -rf silesia.zip silesia silesia.tar; )
tar -cj --transform="s|${HOME#/}/||" -f ~/rapidgzip-comparison-benchmarks-$( date +%Y-%m-%dT%H-%M-%S ).tar.bz2 "$workdir"

ws_release --filesystem beegfs decompression-benchmarks

echo "[$( date --iso-8601=seconds )] Done."
