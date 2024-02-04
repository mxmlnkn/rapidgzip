#!/usr/bin/bash

# Use --export-index because it minimizes the I/O overhead while still ensuring that the windows
# have to be computed instead of being thrown away. Therefore, it is a worst case for benchmarking
# the window compression overhead. Same reasoning to reduce checksum overhead with --no-verify.
# Compressio with zlib will be slower than with ISA-L, therefore it is important to test with it.
# But it is also important to compare it to ISA-L in the first place.

set -euo pipefail

name="$( basename -- "$BASH_SOURCE" )"

# Benchmark
# - silesia because it is a standard benchmark with intermediate compression ratio.
# - 4GiB-base64.gz because it is Huffman-only compressed with one of the lowest compression
#   ratios right after non-compressible files. Compression of each window might take relative longer.
# - wikidata because it decompresses to 1.4 TB with 11 GB of windows, which create memory pressure.
files=(
    "test-files/silesia/20xsilesia.tar.gz"
    "4GiB-base64.gz"
    "test-files/fastq/10xSRR22403185_2.fastq.gz"
    "wikidata-20220103-all.json.gz"
)
commits=( 61a7f892 7d1cf8cd f378587d~ rapidgzip-v0.12.1 b42070f6 )
nRepetition=5


function getLogName()
{
    logFile="$name-$commit"
    if [[ "$withIsal" == "ON" ]]; then
        logFile="${logFile}-with-isal"
    else
        logFile="${logFile}-without-isal"
    fi
    logFile="${logFile}.log"
    echo "$logFile"
}


for withIsal in ON OFF; do
for commit in "${commits[@]}"; do
    if [[ "$withIsal" == "OFF" && "$commit" != "61a7f892" ]]; then
        continue;
    fi

    git checkout -f "$( git rev-parse "$commit" )"
    toolName=rapidgzip
    if [[ "$( find ../src/tools/ -name rapidgzip.cpp | wc -l )" -eq 0 ]]; then toolName=pragzip; fi

    cmake -DWITH_ISAL=$withIsal -DBUILD_TYPE=Release -WITH_RPMALLOC=ON ..
    ninja "$toolName"

    logFile=$( getLogName )
    true > "$logFile"
    for file in "${files[@]}"; do
        if [[ $( stat -L --format=%s -- "$file" ) -le $(( 16 * 1024 * 1024 * 1024 )) ]]; then
            cat "$file" | wc -c &>/dev/null  # try to cache file into memory
        fi

        NO_VERIFY=
        if 'grep' -q no-verify "../src/tools/$toolName.cpp"; then NO_VERIFY=--no-verify; fi
        for i in $( seq $nRepetition ); do
            /usr/bin/time -v "src/tools/$toolName" -v -P 24 -o /dev/null $NO_VERIFY --export-index /dev/null "$file"  2>&1 |
                tee -a "$logFile"
        done
    done
done
done


function refineLog()
{
    sed -nr '
    s/\t/    /g;
    s|file path for input: ||p;
    /Time spent compressing seek points/p;
    s/(Decompressed in total)/    \1/p;
    /Maximum resident set size/p;
    s|[[]ParallelGzipReader::exportIndex[]]|    exportIndex|p;
    ' -- "$1"
}


function refineFurther()
{
    sed -r '
    s|^    .*[Tt]ook ([0-9.]+) .*|\1|;
    s|^    .*: ([0-9.]+).*|\1|;
    s|^    .* in ([0-9.]+) s -> ([0-9.]+).*|\1 \2|;
    ' | sed -z 's|\n| |g' | sed -r 's| ([^ ]**[.]gz)|\n\1|g;'
}

'grep' 'model name' /proc/cpuinfo | uniq
# sudo lshw -short -C memory  # to get memory size and clock. Too bad it requires sudo

# Some commits also contain the time spent compressing windows!
for withIsal in ON OFF; do
for commit in "${commits[@]}"; do
    logFile=$( getLogName )
    #logFile="../results/benchmarks/commits/benchmark-2024-02-window-compression.sh$logFile"
    if [[ ! -f "$logFile" ]]; then continue; fi
    echo "== WITH_ISAL=$withIsal, commit=$commit =="
    if 'grep' -q 'Time spent compressing seek points' "$logFile"; then
        echo "exportIndex/s  compr./s  time/s  bw./(MB/s) Peak RSS/KiB"
    else
        echo "exportIndex/s  time/s    bw./(MB/s) Peak RSS/KiB"
    fi
    refineLog "$logFile" | refineFurther | column -t
    printf "\n\n"
done
done


# Condensend results as benchmarked on Ryzen 3900X
cat <<EOF >/dev/null
model name	: AMD Ryzen 9 3900X 12-Core Processor
16GiB DIMM DDR4 Synchronous Unbuffered (Unregistered) 3600 MHz (0.3 ns)
16GiB DIMM DDR4 Synchronous Unbuffered (Unregistered) 3600 MHz (0.3 ns)
32GiB DIMM DDR4 Synchronous Unbuffered (Unregistered) 3600 MHz (0.3 ns)
32GiB DIMM DDR4 Synchronous Unbuffered (Unregistered) 3600 MHz (0.3 ns)
768KiB L1 cache
6MiB L2 cache
64MiB L3 cache

== WITH_ISAL=OFF, commit=61a7f892 (compress subchunks on workers) ==
                                          exportIndex/s  compr./s   time/s bw./(MB/s) Peak RSS/KiB
test-files/silesia/20xsilesia.tar.gz        0.0575601    1.0492     1.40485  3017.51  1213848
test-files/silesia/20xsilesia.tar.gz        0.0710429    1.06828    1.43475  2954.64  1187220
test-files/silesia/20xsilesia.tar.gz        0.0962241    1.067      1.4621   2899.35  1193880
test-files/silesia/20xsilesia.tar.gz        0.0564812    1.06184    1.42426  2976.4   1223768
test-files/silesia/20xsilesia.tar.gz        0.0968331    1.05423    1.46033  2902.87  1194052
4GiB-base64.gz                              0.000977113  0.0344942  1.16811  3676.86  314768
4GiB-base64.gz                              0.00107349   0.034523   1.20404  3567.14  346104
4GiB-base64.gz                              0.00105904   0.0395838  1.21215  3543.27  337412
4GiB-base64.gz                              0.00104704   0.0330023  1.15369  3722.8   337440
4GiB-base64.gz                              0.000894533  0.0336919  1.1415   3762.56  283476
test-files/fastq/10xSRR22403185_2.fastq.gz  0.0555031    1.87886    1.50426  2405.27  1102556
test-files/fastq/10xSRR22403185_2.fastq.gz  0.0904413    2.12988    1.58271  2286.05  1385560
test-files/fastq/10xSRR22403185_2.fastq.gz  0.0835766    2.12717    1.55598  2325.31  1382852
test-files/fastq/10xSRR22403185_2.fastq.gz  0.090621     2.08981    1.56392  2313.52  1313248
test-files/fastq/10xSRR22403185_2.fastq.gz  0.0573762    2.18222    1.50687  2401.11  1347420
wikidata-20220103-all.json.gz               13.2121      282.263    484.156  2950.2   13362156
wikidata-20220103-all.json.gz               13.3117      241.225    436.547  3271.94  13334872
wikidata-20220103-all.json.gz               13.352       238.292    425.724  3355.11  13197832
wikidata-20220103-all.json.gz               13.3977      240.9      428.386  3334.27  13477540
wikidata-20220103-all.json.gz               13.9372      240.01     426.183  3351.51  13710640

 -> Interestingly, this is not much slower than ISA-L for wikidata and fastq but much faster
    for Silesia and 4GiB-base64. So, it is only faster for the cases without many back-references?

== WITH_ISAL=ON, commit=61a7f892 (compress subchunks on workers) ==
                                          exportIndex/s  compr./s   time/s  bw./(MB/s) Peak RSS/KiB
test-files/silesia/20xsilesia.tar.gz        0.0424686    0.194765   1.00796   4205.7   923676
test-files/silesia/20xsilesia.tar.gz        0.0524305    0.193163   1.01062   4194.59  894924
test-files/silesia/20xsilesia.tar.gz        0.0518695    0.206507   1.01293   4185.04  943772
test-files/silesia/20xsilesia.tar.gz        0.0281773    0.200813   0.976733  4340.14  931880
test-files/silesia/20xsilesia.tar.gz        0.0280553    0.191193   0.988842  4286.99  932792
4GiB-base64.gz                              0.000927893  0.0410805  0.716034  5998.27  297220
4GiB-base64.gz                              0.000926453  0.0360659  0.705579  6087.15  266428
4GiB-base64.gz                              0.000946714  0.0378603  0.714987  6007.06  357216
4GiB-base64.gz                              0.000930264  0.0372039  0.701603  6121.65  277624
4GiB-base64.gz                              0.000982743  0.0355897  0.70045   6131.72  272428
test-files/fastq/10xSRR22403185_2.fastq.gz  0.0306181    0.189944   1.39421   2595.13  1154628
test-files/fastq/10xSRR22403185_2.fastq.gz  0.0336294    0.185372   1.37034   2640.33  1051780
test-files/fastq/10xSRR22403185_2.fastq.gz  0.059531     0.187533   1.45608   2484.86  1345412
test-files/fastq/10xSRR22403185_2.fastq.gz  0.0305111    0.211915   1.43393   2523.25  1319636
test-files/fastq/10xSRR22403185_2.fastq.gz  0.0310488    0.200882   1.40647   2572.5   1023128
wikidata-20220103-all.json.gz               7.59723      57.3458    457.024   3125.34  11976032
wikidata-20220103-all.json.gz               7.52144      52.2111    412.626   3461.62  11976356
wikidata-20220103-all.json.gz               7.77253      51.0389    406.74    3511.72  12264284
wikidata-20220103-all.json.gz               7.23739      51.7217    407.464   3505.47  12067072
wikidata-20220103-all.json.gz               7.75798      52.0997    405.946   3518.58  11956500

 -> It actually seems to be equally fast (wikidata, Silesia, fastq), or even slightly slower
    (4GiB-base64: -5%) than the compression done on the main thread. This makes no sense to me!?
    Maybe it is a memory allocation bottleneck issue again?

== WITH_ISAL=ON, commit=7d1cf8cd (window compression on main thread) ==
                                          exportIndex/s  time/s  bw./(MB/s) Peak RSS/KiB
test-files/silesia/20xsilesia.tar.gz        0.0282607    1.01658   4170.01  959172
test-files/silesia/20xsilesia.tar.gz        0.0280218    1.00022   4238.22  980612
test-files/silesia/20xsilesia.tar.gz        0.0545226    1.05844   4005.1   940292
test-files/silesia/20xsilesia.tar.gz        0.0278027    1.00397   4222.41  951052
test-files/silesia/20xsilesia.tar.gz        0.0553309    1.02898   4119.75  891328
4GiB-base64.gz                              0.00103249   0.646531  6643.1   313780
4GiB-base64.gz                              0.000967594  0.68378   6281.22  338816
4GiB-base64.gz                              0.000961133  0.67978   6318.17  331596
4GiB-base64.gz                              0.000931233  0.651061  6596.87  324972
4GiB-base64.gz                              0.00103216   0.674173  6370.72  361144
test-files/fastq/10xSRR22403185_2.fastq.gz  0.0486245    1.4147    2557.54  1178884
test-files/fastq/10xSRR22403185_2.fastq.gz  0.0314655    1.36572   2649.27  1177880
test-files/fastq/10xSRR22403185_2.fastq.gz  0.0371635    1.38973   2603.5   1218148
test-files/fastq/10xSRR22403185_2.fastq.gz  0.0308705    1.34129   2697.51  1098456
test-files/fastq/10xSRR22403185_2.fastq.gz  0.052719     1.3846    2613.13  1059040
wikidata-20220103-all.json.gz               7.53908      469.281   3043.71  11883796
wikidata-20220103-all.json.gz               7.63906      421.459   3389.07  11865860
wikidata-20220103-all.json.gz               7.544        405.885   3519.11  11971896
wikidata-20220103-all.json.gz               7.86833      401.82    3554.71  11742336
wikidata-20220103-all.json.gz               7.52385      409.846   3485.1   11777412

== WITH_ISAL=ON, commit=f378587d~ (without window compression) ==
                                         exportIndex/s  time/s  bw./(MB/s) Peak RSS/KiB
test-files/silesia/20xsilesia.tar.gz        0.00314793  0.851391  4979.09  981264
test-files/silesia/20xsilesia.tar.gz        0.00370811  0.875483  4842.08  944476
test-files/silesia/20xsilesia.tar.gz        0.00310769  0.872798  4856.97  984796
test-files/silesia/20xsilesia.tar.gz        0.00306905  0.870194  4871.5   943420
test-files/silesia/20xsilesia.tar.gz        0.00355748  0.855597  4954.62  900716
4GiB-base64.gz                              0.00241429  0.714149  6014.11  370996
4GiB-base64.gz                              0.00255038  0.693719  6191.22  339988
4GiB-base64.gz                              0.00254871  0.677064  6343.52  253016
4GiB-base64.gz                              0.0025503   0.676729  6346.66  284612
4GiB-base64.gz                              0.00242761  0.677014  6343.99  256208
test-files/fastq/10xSRR22403185_2.fastq.gz  0.0025123   1.35362   2672.94  1120288
test-files/fastq/10xSRR22403185_2.fastq.gz  0.00256175  1.39088   2601.34  1433144
test-files/fastq/10xSRR22403185_2.fastq.gz  0.00267736  1.3759    2629.67  1188900
test-files/fastq/10xSRR22403185_2.fastq.gz  0.00300324  1.36464   2651.37  1020652
test-files/fastq/10xSRR22403185_2.fastq.gz  0.00257823  1.35909   2662.19  1240876
wikidata-20220103-all.json.gz               0.959876    431.334   3311.48  21428616
wikidata-20220103-all.json.gz               0.979507    434.36    3288.41  21550500
wikidata-20220103-all.json.gz               0.971522    383.444   3725.06  21578524
wikidata-20220103-all.json.gz               0.97717     375.808   3800.76  21749936
wikidata-20220103-all.json.gz               0.95518     426.115   3352.04  21327524

== WITH_ISAL=ON, commit=rapidgzip-v0.12.1 ==
                                          exportIndex/s  time/s  bw./(MB/s) Peak RSS/KiB
test-files/silesia/20xsilesia.tar.gz        0.00112329   0.864757  4902.14  920424
test-files/silesia/20xsilesia.tar.gz        0.00114416   0.828884  5114.29  907748
test-files/silesia/20xsilesia.tar.gz        0.00113348   0.847702  5000.76  937752
test-files/silesia/20xsilesia.tar.gz        0.00184506   0.83721   5063.43  924880
test-files/silesia/20xsilesia.tar.gz        0.00168765   0.859403  4932.68  901376
4GiB-base64.gz                              0.000981972  0.643674  6672.58  312880
4GiB-base64.gz                              0.000880793  0.624636  6875.95  256840
4GiB-base64.gz                              0.000868352  0.626235  6858.4   269384
4GiB-base64.gz                              0.000884973  0.643618  6673.17  289352
4GiB-base64.gz                              0.000876343  0.622911  6894.99  299132
test-files/fastq/10xSRR22403185_2.fastq.gz  0.0011419    1.17099   3089.82  1143184
test-files/fastq/10xSRR22403185_2.fastq.gz  0.00164939   1.12486   3216.55  1037968
test-files/fastq/10xSRR22403185_2.fastq.gz  0.00111925   1.16225   3113.07  981024
test-files/fastq/10xSRR22403185_2.fastq.gz  0.00105317   1.23571   2928     1227024
test-files/fastq/10xSRR22403185_2.fastq.gz  0.00102316   1.16259   3112.14  1174396
wikidata-20220103-all.json.gz               0.455286     470.765   3034.11  21270252
wikidata-20220103-all.json.gz               0.425916     386.331   3697.23  21556652
wikidata-20220103-all.json.gz               0.456763     435.764   3277.82  21326676
wikidata-20220103-all.json.gz               0.431739     383.912   3720.52  21451812
wikidata-20220103-all.json.gz               0.471779     429.979   3321.92  21179580

 -> FUCK!!! Where did I loose this much performance!? Lost ~8%
                              rapidgzip-v0.12.1 ->  f378587d~   -> 61a7f892 (ISA-L)
    20xsilesia.tar.gz              5.0  +- 0.1  -> 4.9  +- 0.1  -> 4.2  +- 0.1
    4GiB-base64.gz                 6.8  +- 0.1  -> 6.3  +- 0.1  -> 6.1  +- 0.1
    10xSRR22403185_2.fastq.gz      3.1  +- 0.1  -> 2.6  +- 0.1  -> 2.6  +- 0.1
    wikidata-20220103-all.json.gz  3.41 +- 0.26 -> 3.50 +- 0.22 -> 3.25 +- 0.15
 -> [ ] At this point, I probably also should compare even older versions!
 -> [ ] A good chunk of the performance for base64 and fastq was lost before the introduction of window compression!
        Find out where and why. Maybe because timings are now always on? Or maybe because of:
 -> [ ] Maybe do window compression in the WindowMap only after a threshold?
        Might introduce troublesome race-conditions again.
 -> [ ] Do not compress windows when they are cleared shortly after anyway (see setKeepIndex)!

== WITH_ISAL=ON, commit=b42070f6 ==
                                          exportIndex/s  compr./s   time/s  bw./(MB/s) Peak RSS/KiB
test-files/silesia/20xsilesia.tar.gz        0.0290612    0.153752   0.812715  5216.04  1101700
test-files/silesia/20xsilesia.tar.gz        0.0290139    0.15264    0.811221  5225.65  1029884
test-files/silesia/20xsilesia.tar.gz        0.0290846    0.153802   0.80851   5243.17  1060072
test-files/silesia/20xsilesia.tar.gz        0.0298313    0.155597   0.806753  5254.59  1048200
test-files/silesia/20xsilesia.tar.gz        0.062639     0.152765   0.858358  4938.68  1073116
4GiB-base64.gz                              0.000929233  0.0329478  0.699311  6141.71  272324
4GiB-base64.gz                              0.000955023  0.0340278  0.689155  6232.22  276628
4GiB-base64.gz                              0.000925323  0.0345943  0.687899  6243.6   257652
4GiB-base64.gz                              0.000945253  0.0326811  0.706905  6075.74  338948
4GiB-base64.gz                              0.000958493  0.0336863  0.694323  6185.83  264876
test-files/fastq/10xSRR22403185_2.fastq.gz  0.0323052    0.208903   1.33839   2703.37  1076856
test-files/fastq/10xSRR22403185_2.fastq.gz  0.034892     0.197627   1.3324    2715.51  1163040
test-files/fastq/10xSRR22403185_2.fastq.gz  0.0312948    0.189013   1.33486   2710.52  1047692
test-files/fastq/10xSRR22403185_2.fastq.gz  0.0304812    0.198447   1.35448   2671.25  1075796
test-files/fastq/10xSRR22403185_2.fastq.gz  0.0312463    0.248058   1.35164   2676.85  1336344
wikidata-20220103-all.json.gz               7.39346      67.0297    480.529   2972.46  10790348
wikidata-20220103-all.json.gz               7.30076      54.2636    396.451   3602.85  10785636
wikidata-20220103-all.json.gz               7.28537      53.8979    373.183   3827.49  10765896
wikidata-20220103-all.json.gz               7.50507      59.7104    429.43    3326.16  10764768
wikidata-20220103-all.json.gz               7.3101       58.0051    407.41    3505.93  10779648

 - For all but wikidata, the first run is always 10+ times slower because it is not
   yet cached into memory.
 - The bandwidths and runtimes of the most recent commit with compression on workers
   and with the compression ratio threshold for enabling compression, is only so
   much slower as it takes for exporting the index, which happens as a non-parallelized
   computation at the end and therefore adds pure overhead!


model name	: AMD Ryzen 7 PRO 4750U with Radeon Graphics
16GiB SODIMM DDR4 Synchronous Unbuffered (Unregistered) 3200 MHz (0.3 ns)
512KiB L1 cache
4MiB L2 cache
8MiB L3 cache
128KiB BIOS


files=( "SRR22401085_2.fastq.gz" )
commits=(
    61a7f892
    f378587d~
    rapidgzip-v0.12.1
    rapidgzip-v0.10.4
    rapidgzip-v0.9.0
    rapidgzip-v0.8.1
    rapidgzip-v0.7.1
    pragzip-v0.6.0
    pragzip-v0.5.0
)
for commit in "${commits[@]}"; do
    # This only works when we benchmark exactly one file and one commit per log file!
    printf '% 20s ' "$commit"; uncertainValue $( sed -nr 's|.* in [0-9.]+ s -> ([0-9.]+).*|\1|p;' benchmark-2024-02-window-compression.sh-$commit-with-isal.log )
done

            61a7f892  1233 | 1254 +- 4 | 1290
           f378587d~  1233 | 1262 +- 5 | 1303
   rapidgzip-v0.12.1  1250 | 1285 +- 4 | 1306
   rapidgzip-v0.10.4  1232 | 1270 +- 5 | 1293
    rapidgzip-v0.9.0  1235 | 1252 +- 3 | 1280
    rapidgzip-v0.8.1   772 |  839 +- 7 |  864
    rapidgzip-v0.7.1   811 |  836 +- 4 |  858
      pragzip-v0.6.0   798 |  834 +- 6 |  883
      pragzip-v0.5.0   830 |  863 +- 4 |  880

files=( "4GiB-base64.gz" )

            61a7f892 2578 | 2627 +- 10 | 2692
           f378587d~ 2341 | 2512 +- 22 | 2683
   rapidgzip-v0.12.1 2264 | 2447 +- 21 | 2550
   rapidgzip-v0.10.4 2343 | 2486 +- 19 | 2638
    rapidgzip-v0.9.0 2322 | 2419 +- 10 | 2474
    rapidgzip-v0.8.1 1248 | 1280 +-  3 | 1290
    rapidgzip-v0.7.1 1273 | 1296 +-  7 | 1364
      pragzip-v0.6.0 1287 | 1308 +-  6 | 1369
      pragzip-v0.5.0 1396 | 1415 +-  5 | 1463

 -> Cannot reproduce the slowdown between rapidgzip-v0.12.1 and 61a7f892 on my Thinkpad T14.
    Maybe it doesn't have enough copmuting power to saturate memory bandwidth, which might be the underlying problem?
 -> rapidgzip-0.9.0 did increase ISA-L usage in many locations, leading to the significant speedup.
    Therefore it should suffice to benchmark up to this version on my PC.


for commit in "${commits[@]}"; do
    # This only works when we benchmark exactly one file and one commit per log file!
    printf '% 20s ' "$commit"; uncertainValue $( sed -nr 's|.* in [0-9.]+ s -> ([0-9.]+).*|\1|p;' benchmark-2024-02-window-compression-20xsilesia/benchmark-2024-02-window-compression.sh-$commit-with-isal.log )
done


Tests on PC:

commits=(
    3ca62ea3  # [refactor] Introduce ChunkData::Configuration in order to forbid copying ChunkData
    da1fdb11  # [feature] Improve profiling output
    12c0f19d  # [performance] DecodedData: Avoid temporary allocations
    bb547cc9  # [performance] Use ISA-L CRC32 computation, which uses PCLMULQDQ if available
    adfa74f0  # [feature] Separately time CRC32 computation on workers and centralize timing of ChunkData::append
    3f522847  # [refactor] Split chunks on worker threads
    3cbe3e6c  # [refactor] Replace markers before inserting chunk metadata into the databases
    3fa73118  # [refactor] Always check the window map first even for BGZF files
    f34a599d  # [refactor] Move more statistics into ChunkData::Statistics
    2ca74763  # [API] Move ChunkData statistics into subclass
    1a0f7d23  # [API] Change template parameter ENABLE_STATISTICS to a member
    f24191e8  # [refactor] GzipChunkFetcher: Move statistics into subclass
    58ac926e  # [refactor] Make decodeBlock take ChunkData containing many of the configuration parameters as members
    0f3bc590  # [refactor] ChunkData: Do not make assumption on what guessed encodedOffsetInBits was used
    8799e612  # [fix] Take care that the optional decoded size is only used to optimize allocations and not as an implicit stop condition
    ae8785bc  # [refactor] WindowMap: Use shared_ptr for windows
    21ba498b  # [fix] Propagate EPIPE (broken pipe) to a normal return code instead of an exception
)
nRepetition=10

4GiB-base64

            3ca62ea3 6099 | 6283 +- 15 | 6541
            da1fdb11 6706 | 6832 +- 10 | 6971
            12c0f19d 6227 | 6418 +- 11 | 6590
            bb547cc9 6610 | 6744 +-  7 | 6865
            adfa74f0 6182 | 6382 +- 12 | 6510
            3f522847 6144 | 6364 +- 11 | 6541
            3cbe3e6c 6353 | 6678 +- 15 | 6829
            3fa73118 6339 | 6649 +- 12 | 6786
            f34a599d 6531 | 6675 +-  9 | 6827
            2ca74763 6508 | 6697 +- 10 | 6833
            1a0f7d23 6320 | 6559 +- 14 | 6751
            f24191e8 6189 | 6317 +-  9 | 6472
            58ac926e 6560 | 6682 +-  7 | 6785
            0f3bc590 6169 | 6360 +-  9 | 6469
            8799e612 6480 | 6677 +- 10 | 6776
            ae8785bc 6254 | 6361 +-  7 | 6482
            21ba498b 6465 | 6697 +- 15 | 6885


4GiB-base64 (while watching videos and with firefox open)

            3ca62ea3 5783 | 5911 +-  7 | 6019
            da1fdb11 5673 | 5910 +- 10 | 6016
            12c0f19d 5496 | 5722 +-  8 | 5786
            bb547cc9 5181 | 5489 +- 22 | 5946
            adfa74f0 5572 | 5695 +-  8 | 5805
            3f522847 5624 | 5739 +-  6 | 5822
            3cbe3e6c 5693 | 5903 +-  9 | 6022
            3fa73118 5724 | 5889 +- 11 | 6051
            f34a599d 5782 | 5888 +-  7 | 6022
            2ca74763 5843 | 5927 +-  5 | 5997
            1a0f7d23 5606 | 5814 +-  9 | 5909
            f24191e8 5631 | 5764 +-  8 | 5877
            58ac926e 5723 | 5885 +- 10 | 5993
            0f3bc590 5530 | 5772 +-  9 | 5833
            8799e612 5699 | 5938 +-  9 | 6023
            ae8785bc 5705 | 5793 +-  5 | 5854
            21ba498b 5856 | 5938 +-  6 | 6047

20xsilesia

            3ca62ea3 4207 | 4275 +-  4 | 4342
            da1fdb11 4141 | 4265 +-  7 | 4341
            12c0f19d 4194 | 4261 +-  4 | 4334
            bb547cc9 4192 | 4282 +-  4 | 4340
            adfa74f0 4104 | 4255 +-  7 | 4357
            3f522847 4124 | 4250 +-  6 | 4318
            3cbe3e6c 4120 | 4278 +-  7 | 4368
            3fa73118 4132 | 4263 +-  6 | 4342
            f34a599d 4183 | 4283 +-  5 | 4353
            2ca74763 4080 | 4251 +-  8 | 4369
            1a0f7d23 4153 | 4282 +-  5 | 4345
            f24191e8 4204 | 4295 +-  5 | 4351
            58ac926e 4168 | 4313 +-  5 | 4367
            0f3bc590 4136 | 4266 +-  7 | 4354
            8799e612 4262 | 4325 +-  5 | 4427
            ae8785bc 4216 | 4290 +-  5 | 4384
            21ba498b 3961 | 4279 +- 12 | 4397

10xSRR22403185_2.fastq

            3ca62ea3 2591.6 | 2651.5 +- 3   | 2696.3
            da1fdb11 2584   | 2660   +- 5   | 2739
            12c0f19d 2618.4 | 2653   +- 2.3 | 2691.8
            bb547cc9 2582   | 2635   +- 3   | 2686
            adfa74f0 2578   | 2632   +- 4   | 2698
            3f522847 2580   | 2644   +- 4   | 2702
            3cbe3e6c 2586.5 | 2642.5 +- 2.4 | 2682.3
            3fa73118 2521   | 2622   +- 5   | 2686
            f34a599d 2621.5 | 2660.6 +- 2.5 | 2698.2
            2ca74763 2507   | 2582   +- 6   | 2715
            1a0f7d23 2554   | 2656   +- 4   | 2709
            f24191e8 2229   | 2571   +- 17  | 2690
            58ac926e 2531   | 2649   +- 5   | 2713
            0f3bc590 2245   | 2551   +- 13  | 2658
            8799e612 2563   | 2664   +- 5   | 2743
            ae8785bc 2588   | 2648   +- 5   | 2755
            21ba498b 2611   | 2677   +- 4   | 2735

rm src/tools/CMakeFiles/rapidgzip.dir/rapidgzip.cpp.o
ninja -v
Add -S to get assembler output in the file specified with -o
Do for 8799e612 (10% faster) and 8799e612~ (slower)
for commit in 8799e612 8799e612~; do
    sed -r 's|([.]L[A-Z]*)[0-9]+|\1_123456|g' rapidgzip-$commit.cpp.S > rapidgzip-$commit.cpp.unified-labels.S
done
git add -f rapidgzip-8799e612~.cpp.unified-labels.S
git diff --no-index --color-moved rapidgzip-8799e612{~,}.cpp.unified-labels.S
Manually step through the paged output and look for changed non-moved lines:
 - Many moved function definitions, it seems: lines starting with .size, ...
   - [ ] Maybe one of these moves really is at fault? Maybe try link-time optimization to get reliable output?!
         https://blog.jetbrains.com/clion/2022/05/testing-3-approaches-performance-cpp_apps/
         Maybe also PGO? (why isn't LTO the default if it doesn't require any profile?!)
         https://stackoverflow.com/questions/16868235/corrupted-profile-info-with-g-and-cmake
            -fprofile-generate=profile-4GiB-base64 -fprofile-correction
            -fprofile-use=profile-4GiB-base64
          -> only seems to get worse
          -> with -flto + -fprofile-use, it seems to almost come back to the original speed
             -> but both commits seem to be equally slow now ... ...
 - Some renamed registers
 - Several additional .align 2 and .p2align 4 lines? Some also removed, so maybe only a problem with the move detection?

@@ -19716,9 +19716,9 @@ _ZNSt6vectorImSaImEE12emplace_backIJRmEEES3_DpOT_.isra.0:
        movq    8(%rdi), %rax
        cmpq    16(%rdi), %rax
        je      .L_123456
-       movq    (%rsi), %rdx
+       movl    (%rsi), %ecx
        addq    $8, %rax
-       movq    %rdx, -8(%rax)
+       movq    %rcx, -8(%rax)
        movq    %rax, 8(%rdi)
        addq    $24, %rsp
        .cfi_remember_state
@@ -19754,7 +19754,7 @@ _ZNSt6vectorImSaImEE12emplace_backIJRmEEES3_DpOT_.isra.0:
        xorl    %ebp, %ebp
        xorl    %edi, %edi
 .L_123456:
-       movq    (%rsi), %rax
+       movl    (%rsi), %eax
        movq    %rdi, %xmm0
        movq    %rax, (%rdi,%r12)
        leaq    8(%rdi,%r12), %rax
[...]
@@ -19848,9 +19848,9 @@ _ZNSt6vectorImSaImEE12emplace_backIJRjEEERmDpOT_.isra.0:
        movq    8(%rdi), %rax
        cmpq    16(%rdi), %rax
        je      .L_123456
-       movl    (%rsi), %ecx
+       movq    (%rsi), %rdx
        addq    $8, %rax
-       movq    %rcx, -8(%rax)
+       movq    %rdx, -8(%rax)
        movq    %rax, 8(%rdi)
        addq    $24, %rsp
        .cfi_remember_state
@@ -19886,7 +19886,7 @@ _ZNSt6vectorImSaImEE12emplace_backIJRjEEERmDpOT_.isra.0:
        xorl    %ebp, %ebp
        xorl    %edi, %edi
 .L_123456:
-       movl    (%rsi), %eax
+       movq    (%rsi), %rax
        movq    %rdi, %xmm0
        movq    %rax, (%rdi,%r12)
        leaq    8(%rdi,%r12), %rax
[...]
+       .section        .text._ZNSt13__future_base11_Task_stateIZN9rapidgzip16GzipChunkFetcherIN16FetchingStrategy16FetchMultiStreamENS1_16ChunkDataCounterELb1
EE26replaceMarkersInPrefetchedEvEUlvE0_SaIiEFvvEE6_M_runEv,"axG",@progbits,_ZNSt13__future_base11_Task_stateIZN9rapidgzip16GzipChunkFetcherIN16FetchingStrategy
16FetchMultiStreamENS1_16ChunkDataCounterELb1EE26replaceMarkersInPrefetchedEvEUlvE0_SaIiEFvvEE6_M_runEv,comdat
+       .align 2
+       .p2align 4
+       .weak   _ZNSt13__future_base11_Task_stateIZN9rapidgzip16GzipChunkFetcherIN16FetchingStrategy16FetchMultiStreamENS1_16ChunkDataCounterELb1EE26replaceMar
kersInPrefetchedEvEUlvE0_SaIiEFvvEE6_M_runEv
+       .type   _ZNSt13__future_base11_Task_stateIZN9rapidgzip16GzipChunkFetcherIN16FetchingStrategy16FetchMultiStreamENS1_16ChunkDataCounterELb1EE26replaceMar
kersInPrefetchedEvEUlvE0_SaIiEFvvEE6_M_runEv, @function
+_ZNSt13__future_base11_Task_stateIZN9rapidgzip16GzipChunkFetcherIN16FetchingStrategy16FetchMultiStreamENS1_16ChunkDataCounterELb1EE26replaceMarkersInPrefetche
dEvEUlvE0_SaIiEFvvEE6_M_runEv:
[...]
-       call    _ZNSt9basic_iosIcSt11char_traitsIcEE4initEPSt15basic_streambufIcS1_E@PLT
-.LEHE_123456:
-       movq    -24(%r12), %rax
-       leaq    688(%rsp), %r13
-       movq    %r12, 688(%rsp)
-       xorl    %esi, %esi
-       addq    %r13, %rax
-       movq    %rax, %rdi
-       movq    40+_ZTTNSt7__cxx1118basic_stringstreamIcSt11char_traitsIcESaIcEEE(%rip), %rax
-       movq    %rax, (%rdi)
-.LEHB_123456:
-       call    _ZNSt9basic_iosIcSt11char_traitsIcEE4initEPSt15basic_streambufIcS1_E@PLT
-.LEHE_123456:
-       movq    8+_ZTTNSt7__cxx1118basic_stringstreamIcSt11char_traitsIcESaIcEEE(%rip), %rax
-       movq    48+_ZTTNSt7__cxx1118basic_stringstreamIcSt11char_traitsIcESaIcEEE(%rip), %rcx
-       pxor    %xmm0, %xmm0
-       leaq    696(%rsp), %r15
-       movdqa  96(%rsp), %xmm3
-       movq    -24(%rax), %rax
-       movq    %rcx, 672(%rsp,%rax)
-       leaq    24+_ZTVNSt7__cxx1118basic_stringstreamIcSt11char_traitsIcESaIcEEE(%rip), %rax
-       movq    %rax, 672(%rsp)
-       addq    $80, %rax
-       movq    %rax, 800(%rsp)
-       leaq    752(%rsp), %rax
-       movq    %rax, %rdi
-       movq    %rax, 64(%rsp)
-       movaps  %xmm3, 688(%rsp)
-       movaps  %xmm0, 704(%rsp)
-       movaps  %xmm0, 720(%rsp)
-       movaps  %xmm0, 736(%rsp)
-       call    _ZNSt6localeC1Ev@PLT
-       leaq    16+_ZTVNSt7__cxx1115basic_stringbufIcSt11char_traitsIcESaIcEEE(%rip), %rax
-       movq    %r15, %rsi
-       movq    16(%rsp), %rdi
-       movq    %rax, 696(%rsp)
-       leaq    784(%rsp), %rax
-       movl    $24, 760(%rsp)
-       movq    %rax, 72(%rsp)
-       movq    %rax, 768(%rsp)
-       movq    $0, 776(%rsp)
-       movb    $0, 784(%rsp)
-.LEHB_123456:
-       call    _ZNSt9basic_iosIcSt11char_traitsIcEE4initEPSt15basic_streambufIcS1_E@PLT
+       call    _ZNSt7__cxx1118basic_stringstreamIcSt11char_traitsIcESaIcEEC1Ev@PLT

[...]

        .align 2
        .p2align 4
-       .weak   _ZNSt6vectorIS_It17RpmallocAllocatorItEESaIS2_EE17_M_realloc_insertIJEEEvN9__gnu_cxx17__normal_iteratorIPS2_S4_EEDpOT_
-       .type   _ZNSt6vectorIS_It17RpmallocAllocatorItEESaIS2_EE17_M_realloc_insertIJEEEvN9__gnu_cxx17__normal_iteratorIPS2_S4_EEDpOT_, @function
-_ZNSt6vectorIS_It17RpmallocAllocatorItEESaIS2_EE17_M_realloc_insertIJEEEvN9__gnu_cxx17__normal_iteratorIPS2_S4_EEDpOT_:
-.LFB_123456:
-       .cfi_startproc
-       endbr64
-       movabsq $-6148914691236517205, %rdx
-       pushq   %r15
-       .cfi_def_cfa_offset 16
-       .cfi_offset 15, -16
[...]
-       movq    %rcx, %rdx
-       subq    %rbp, %rdx
-       subq    $8, %rdx
-       cmpq    $32, %rdx
-       jbe     .L_123456
-       testq   %rsi, %rsi
-       je      .L_123456
-       leaq    1(%rsi), %r8
-       movq    %rcx, %rdx
-       movq    %r8, %rdi
-       shrq    %rdi
-       leaq    (%rdi,%rdi,2), %rdi
-       salq    $4, %rdi
-       addq    %rbp, %rdi
-       .p2align 4,,10
-       .p2align 3
-.L_123456:
-       movdqu  (%rax), %xmm6
-       movdqu  16(%rax), %xmm7
-       addq    $48, %rax
-       addq    $48, %rdx
-       movdqu  -16(%rax), %xmm0
-       movups  %xmm6, -48(%rdx)
-       movups  %xmm7, -32(%rdx)
-       movups  %xmm0, -16(%rdx)
-       cmpq    %rax, %rdi
-       jne     .L_123456
-       movq    %r8, %rdi
-       andq    $-2, %rdi
-       leaq    (%rdi,%rdi,2), %rax

 -> Note that the .je and .jne are the only lines here not colored as moved, i.e., it seems those jumps really
    did disappear from _ZNSt6vectorIS_It17RpmallocAllocatorItEESaIS2_EE17_M_realloc_insertIJEEEvN9__gnu_cxx17__normal_iteratorIPS2_S4_EEDpOT_!
    - [ ] Maybe try to benchmark the commits without rpmalloc!

 .L_123456:
-       addq    %r13, 216024(%rbp)
-       xorl    %edx, %edx
+       leal    1(%rbx), %eax
        jmp     .L_123456
-       .p2align 4,,10
-       .p2align 3
 .L_123456:
-       movq    %r12, %rsi
-       movq    %rbp, %rdi
-       movl    %r9d, 8(%rsp)
-       call    _ZNK9rapidgzip7deflate5BlockILb0EE11getDistanceER9BitReaderILb0EmE
-.LEHE_123456:
-       movl    8(%rsp), %r9d
-       movq    %rax, %rdx
-       sarq    $32, %rdx
-       testl   %edx, %edx
-       jne     .L_123456
-       cmpb    $0, 216040(%rbp)
-       movw    %r9w, 46(%rsp)
-       jne     .L_123456
-       movzwl  %ax, %r11d
-       movl    %r9d, %ecx
-.L_123456:
-       movq    216008(%rbp), %rdx
-       movzwl  %cx, %r8d
-       movl    %edx, %esi
-       subl    %r11d, %esi
-       movzwl  %si, %edi
-       leaq    (%rdx,%r8), %rsi
-       movq    %rdi, 8(%rsp)
-       cmpw    %cx, %ax
-       ja      .L_123456
-       cmpq    $65535, %rsi
-       ja      .L_123456
-       cmpw    %cx, %ax

 -> [ ] It seems like a call to getDistance got inlined (the diff color does not indicate a simple move)?! Try forceinline

@@ -125769,252 +125535,359 @@ _ZN9rapidgzip7deflate5BlockILb0EE22readInternalCompressedI9WeakArrayIhLm131072EE
        .p2align 3
 .L_123456:
        .cfi_restore_state
-       addl    %eax, 64(%rbp)
+       movl    %edx, %eax
+       movl    %edx, %edi
+       shrl    $26, %eax
+       andl    $33554431, %edi
+       andl    $3, %eax
+       movl    %eax, %r9d
+       je      .L_123456
 .L_123456:
+       movl    %edi, %edx
        cmpw    $255, %di
        jbe     .L_123456
-       cmpw    $256, %di
+       cmpl    $1, %r9d
        je      .L_123456
-       cmpw    $285, %di
-       ja      .L_123456
-       movzwl  %di, %edi
-       movq    %rbp, %rsi
-.LEHB_123456:
-       call    _ZN9rapidgzip7deflate5BlockILb0EE9getLengthEtR9BitReaderILb0EmE
-       movl    %eax, %ecx
-       testw   %ax, %ax
-       jne     .L_123456
 .L_123456:
-       cmpq    %r13, (%rsp)
+       movq    184(%rbp), %rcx
+       movq    216032(%rbp), %r8
+       leaq    1(%r15), %r13
+       movq    216008(%rbp), %rsi
+       leaq    1(%rcx), %rax
+       movq    %rax, 184(%rbp)
+       leaq    1(%r8), %rax
+       movq    %rax, 216032(%rbp)
+       movzbl  %dil, %eax
+       movw    %ax, (%rbx,%rsi,2)
+       addq    $1, %rsi
+       movzwl  %si, %eax
+       movl    %edi, %esi
+       movq    %rax, 216008(%rbp)
+       shrl    $8, %esi
+       subl    $1, %r9d
+       je      .L_123456
+       movl    %esi, %edx

 -> [ ] Some calls to getLength also seem to have gotten inlined?

@@ -126036,33 +125909,44 @@ _ZN9rapidgzip7deflate5BlockILb0EE22readInternalCompressedI9WeakArrayIhLm131072EE
        call    __cxa_throw@PLT
 .LEHE_123456:
 .L_123456:
-       endbr64
-.L_123456:
-       movq    %rax, %r12
-       jmp     .L_123456
+       movl    $16, %edi
+       call    __cxa_allocate_exception@PLT
+       leaq    .LC_123456(%rip), %rsi
+       movq    %rax, %rdi
+       movq    %rax, %rbp
+.LEHB_123456:
+       call    _ZNSt16invalid_argumentC1EPKc@PLT
+.LEHE_123456:
+       movq    _ZNSt16invalid_argumentD1Ev@GOTPCREL(%rip), %rdx
+       leaq    _ZTISt16invalid_argument(%rip), %rsi
+       movq    %rbp, %rdi
+.LEHB_123456:
+       call    __cxa_throw@PLT
+.LEHE_123456:
 .L_123456:
        subq    $1, %rax
        jne     .L_123456
        call    __cxa_begin_catch@PLT
-       movb    $0, 8(%rsp)
-       xorl    %eax, %eax
-.L_123456:
-       movzbl  8(%rsp), %esi
-       cmpb    (%r14), %sil
-       jnb     .L_123456
-       movl    64(%rbp), %ecx
-       addl    %eax, %eax
-       movw    %ax, 16(%rsp)
-       cmpl    $64, %ecx
+       movl    64(%r14), %esi
+       movq    56(%r14), %rdi
+       call    _ZNK9BitReaderILb0EmE13peekAvailableEv.isra.0
+       movq    %rax, %r13
+       testq   %rdx, %rdx
        jne     .L_123456
-       movl    $1, %esi
-       movq    %rbp, %rdi
+       movl    $8, %edi
+       call    __cxa_allocate_exception@PLT
+       leaq    _ZN9BitReaderILb0EmE16EndOfFileReachedD1Ev(%rip), %rdx
+       leaq    _ZTIN9BitReaderILb0EmE16EndOfFileReachedE(%rip), %rsi
+       movq    %rax, %rdi
+       leaq    16+_ZTVN9BitReaderILb0EmE16EndOfFileReachedE(%rip), %rax
+       movq    %rax, (%rdi)
 .LEHB_123456:
-       call    _ZN9BitReaderILb0EmE5read2Ej
+       call    __cxa_throw@PLT
 .LEHE_123456:
 .L_123456:
-       addb    $1, 8(%rsp)
-       orl     16(%rsp), %eax
+       endbr64
+.L_123456:
+       movq    %rax, %r12
        jmp     .L_123456
 .L_123456:
        endbr64

 -> Weird how that call to BitReader::read got changed to a throw. Some lines below it get reversed for, so probably only an unrecognized move:

  .LEHB_123456:
-       call    __cxa_throw@PLT
+       call    _ZN9BitReaderILb0EmE5read2Ej
 .LEHE_123456:


[...]

@@ -281960,9 +281965,9 @@ _Z18decompressParallelISt10unique_ptrIN9rapidgzip18ParallelGzipReaderINS1_16Chun
        testq   %r14, %r14
        je      .L_123456
        movq    %r14, %rdi
-.LEHB_123456:
-       call    _ZNSt5mutex4lockEv
-.LEHE_123456:
+       call    pthread_mutex_lock@PLT
+       testl   %eax, %eax
+       jne     .L_123456
        leaq    64(%rsp), %rax!
        movq    %r14, 48(%rsp)
        movq    %r13, %rsi


Trying to use perf record/report before and after and look for differences

    m rapidgzip && perf record --call-graph dwarf src/tools/rapidgzip -v -P 24 --no-verify --export-index /dev/null 4GiB-base64.gz
    perf report --no-children -T -i perf.data | c++filt > rapidgzip-export-index-8799e612.performance-with.log

 - [ ] Unrelated, but I'm seeing ~5.5% in __memset_avx2_unaligned_erms from finishDecodeBlockWithIsal!
       My age-old problem with unnecessary vector initialization on resize:
       https://stackoverflow.com/questions/5958572/how-can-i-avoid-stdvector-to-initialize-all-its-elements

The slowness can also be seen in the ecent counts:
    8799e612~  # Event count (approx.): 9096587440
    8799e612   # Event count (approx.): 7782659076

Also interesting and possibly avoidable? -> Probably not because it is inside ISA-L code
     2.67%  rapidgzip  libc.so.6             [.] __memmove_avx_unaligned_erms
            |
            ---__memcpy_avx_unaligned_erms (inlined)
               |
                --1.96%--isal_inflate
                          rapidgzip::IsalInflateWrapper::readStream
                          rapidgzip::GzipChunkFetcher<FetchingStrategy::FetchMultiStream, rapidgzip::ChunkData, true>::finishDecodeBlockWithIsal
                          0xffffffffffffffff

 -> Cannot see much difference except some 0.2% changes for some functions and a 2% change for "rapidgzip",
    which can be everything. Unfortunately, using ReleaseWithDebInfo slows down the program + perf too much (150 MB/s)
    to be useful

Try -n to show absolute event counts instead of percentages:
https://stackoverflow.com/questions/3630824/can-perf-display-raw-sample-counts



files=("4GiB-base64.gz")
commits=(
    ad10142e  # [wip] Bump version to 0.13.0
    4784317c  # [fix] Make CompressedVector work with Containers other than FasterVector
    bd08c676  # [performance] Add custom vector class that avoids the initialization during resize
)
for commit in "${commits[@]}"; do
    # This only works when we benchmark exactly one file and one commit per log file!
    printf '% 20s ' "$commit"; uncertainValue $( sed -nr 's|.* in [0-9.]+ s -> ([0-9.]+).*|\1|p;' benchmark-2024-02-window-compression.sh-$commit-with-isal.log )
done

    ad10142e 6191 | 6514 +- 14 | 6635
    4784317c 6760 | 6887 +-  8 | 6984
    bd08c676 6590 | 6933 +- 13 | 7066

 -> The performance gain between ad10142e and 4784317c is very likely the spurious compiler optimization again
    as already noted above.
 -> However, there is some measurably gain with bd08c676 and it is the first time that the 7 GB/s threshold
    has been broken!


commits=(
    ad10142e  # [wip] Bump version to 0.13.0
    4784317c  # [fix] Make CompressedVector work with Containers other than FasterVector
    bd08c676  # [performance] Add custom vector class that avoids the initialization during resize
    c868bd08  # [refactor] GzipChunkFetcher: Change interface to use SharedFileReader instead of BitReader
)

files=("test-files/fastq/10xSRR22403185_2.fastq.gz")
    ad10142e 2526 | 2588 +- 3 | 2639
    4784317c 2511 | 2579 +- 5 | 2680
    bd08c676 2569 | 2666 +- 7 | 2786
    c868bd08 2611 | 2662 +- 4 | 2712
files=("test-files/silesia/20xsilesia.tar.gz")
    ad10142e 4030 | 4122 +- 6 | 4211
    4784317c 4199 | 4279 +- 6 | 4408
    bd08c676 4129 | 4290 +- 9 | 4413
    c868bd08 4338 | 4459 +- 6 | 4540
EOF
