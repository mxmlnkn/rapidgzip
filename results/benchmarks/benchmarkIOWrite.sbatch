#!/bin/bash
#SBATCH -A zihforschung
#SBATCH -p romeo
#SBATCH --exclusive
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --time=04:00:00
#SBATCH --mem-per-cpu=1972M
#SBATCH --cpu-freq=2000000-2000000

module load CMake Ninja Clang hwloc
module list

function m()
{
    if [[ -f Makefile ]]; then
        make -j $( nproc ) "$@"
    elif [[ -f CMakeCache.txt ]]; then
        cmake --build . --parallel $( nproc ) -- "$@"
    fi
}

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
    cmake --build . -- benchmarkIOWrite
)

# Benchmarks of components
timestamp=$( date +%Y-%m-%dT%H-%M-%S )

cd ~/indexed_bzip2/build
testFile="$BEEGFSWS/job-$SLURM_JOB_ID"

BEEGFSWS=$( ws_allocate --duration 1 --filesystem beegfs io-write-benchmark )
time src/benchmarks/benchmarkIOWrite "$testFile" | tee "benchmarkIOWrite-beegfs-default-$timestamp.md"

numTargets=36
beegfs-ctl --setpattern --chunksize=1m --numtargets="$numTargets" "$BEEGFSWS" --mount=/beegfs/global0
time src/benchmarks/benchmarkIOWrite "$testFile" | tee "benchmarkIOWrite-beegfs-$numTargets-targets-$timestamp.md"
beegfs-ctl --getentryinfo "$testFile" --mount=/beegfs/global0

testFile="/dev/shm/job-$SLURM_JOB_ID"
time src/benchmarks/benchmarkIOWrite "$testFile" | tee "benchmarkIOWrite-${SLURM_JOB_PARTITION}-dev-shm-$timestamp.md"

cd ~/indexed_bzip2/build
tar -cjf "~/benchmark-IO-write-results-$timestamp.tar.bz2" *"$timestamp.md" -C ~ "hwloc-$( hostname ).xml" "hwloc-$( hostname ).svg"

