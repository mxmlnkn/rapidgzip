#!/usr/bin/env python3

import matplotlib.pyplot as plt
from matplotlib.ticker import ScalarFormatter
import numpy as np
import math
import os
import sys

if len(sys.argv) < 2:
    print("Please specify the folder containing the benchmark logs!")
folder = sys.argv[1]
suffix = '-commit-timings.dat'
benchmarkLogs = [os.path.join(folder, file) for file in os.listdir(folder) if file.endswith(suffix)]


# Return compilerName and lists of min, max, avg values per commit
def loadData(filePath):
    commits = []
    minTimes = []
    avgTimes = []
    maxTimes = []

    label = filePath.split('/')[-1]
    if label.endswith(suffix):
        label = label[: -len(suffix)]
    with open(filePath, 'rt') as file:
        for line in file:
            # Line: 9236b2d39c339991bf2f395636b53d98e0f227d9 1.723 1.737 1.765 1.744 1.757 1.678 1.705 1.737 1.738 1.692
            tokens = line.split(' ')
            if len(tokens) < 3:
                continue

            times = np.array([float(t) for t in tokens[1:]])
            if np.max(times) < 0.1:
                commits.append(tokens[0][0:9])
                minTimes.append(float('nan'))
                avgTimes.append(float('nan'))
                maxTimes.append(float('nan'))
                continue

            commits.append(tokens[0][0:9])
            minTimes.append(np.min(times))
            avgTimes.append(np.mean(times))
            maxTimes.append(np.max(times))

    return label, commits, np.array(minTimes), np.array(avgTimes), np.array(maxTimes)

yMin = float('+inf')
yMax = float('-inf')

fig = plt.figure(figsize=(16, 8))
ax = fig.add_subplot(111, ylabel="Runtime in seconds", xlabel="Commits from newest to oldest", yscale='log')
for logFile in benchmarkLogs:
    label, commits, minTimes, avgTimes, maxTimes = loadData(logFile)
    ax.errorbar(
        # Begin at 1 so that line numbersin .dat file, which begin at 1, corresspond to x value
        1 + np.arange(len(commits)),
        avgTimes,
        yerr=(avgTimes - minTimes, maxTimes - avgTimes),
        # linestyle = '--' if 'clang' in label else '-',
        linestyle=':',
        marker='o' if 'clang' in label else 'v',
        capsize=4,
        label=label,
    )

    yMin = min(yMin, np.nanmin(minTimes))
    yMax = max(yMax, np.nanmax(maxTimes))

ax.yaxis.set_major_formatter(ScalarFormatter())
ax.yaxis.set_minor_formatter(ScalarFormatter())
ax.set_ylim([math.floor(yMin), math.ceil(yMax)])
ax.set_xlim([0, len(commits) + 2])


# for tag in $( git tag -l | 'grep' gzip ); do
#    echo "$tag"
#    git log --oneline "$tag" | grep '[[]\(feature\|refactor\|fix\|performance\)[]]' | sed 's| .*||' | head -1
# done
lastFunctionCommitPerVersion = [
    ("1e41c7e3", '-', 0.700, "v0.8.1"),
    ("7ba84035", '-', 0.675, "v0.8.0"),
    ("820ef836", '-', 0.650, "v0.7.1"),
    ("1f4eac8e", '-', 0.625, "v0.7.0"),
    ("1f4eac8e", '-', 0.600, "v0.6.0"),
    ("3a67d3e9", '-', 0.850, "v0.5.0"),
    ("8a9877eb", '-', 0.850, "v0.4.0"),
    ("3d385061", '-', 0.850, "v0.3.0"),
    ("1001e4b0", '-', 0.850, "v0.2.2"),
    ("7df4c5e0", '-', 0.850, "v0.2.1"),
    ("0a6febe5", '-', 0.850, "v0.2.0"),

    # left-aligned labels
    ("60a40070", ':', 0.175, "Fix order of inserted fetch\nindexes during splitIndex call"),
    ("1e74b6b9", ':', 0.225, "Deflate: use std::memcpy when\npossible to resolve backreference"),
    # [ ] Performance degradation already fixed with 60a40070.
    #     However on Taurus, this actually improved performance from 1.6 to 1.5 but significantly
    #     and the fix in 60a40070 did restore the original / worsen the performance back to 1.6
    ("294b7d12", ':', 0.475, "Insert split chunk parts\nalso into block finder"),

    ("6cb4ab6b", ':', 0.675, "Parallelize marker replacement"),
    ("9506c2d1", ':', 0.725, "Split seek points based\non their decompressed size"),
    ("d4a66bc5", ':', 0.750, "Dump seek point statistics in verbose mode"),

    # right-aligned labels
    # HUGE performance gains on Taurus but only marginal ones on Ryzen and only for base64
    ("dd678c7c", ':', 0.075, "Use rpmalloc"),
    # [ ] This had significant improvements on Taurus for Silesia (7.0 s -> 5.0 s) and FASTQ (10.4 s -> 6.6 s).
    #     It might be a good idea to try and make this even faster.
    ("6dc244d4", ':', 0.175, "Improve marker replacement speed"),
    ("0aa83871", ':', 0.225, "Distance code counts must\nbe in 1-32 not 1-31!"),
    ("9e5961f7", ':', 0.250, "Uncompressed BlockFinder: Avoid full seeks"),

    # [ ] On Taurus, this worsened everything. I guess the shrink_to_fit resulted in a costly reallocation?
    #     Might not be an issue anymore with rpmalloc, which was introduced some time after this commit.
    ("02cabed2", ':', 0.325, "Avoid overallocation and reallocations"),
    ("49052699", ':', 0.375, "Actively avoiding cache-spilling\nwas disfunctional"),
    # On Taurus, this worsened base64
    ("87429392", ':', 0.425, "Decrease double-cached Huffman LUT\ncached bits to MAX_CODE_LENGTH"),
    ("62225149", ':', 0.475, "Only create Huffman LUT up to\nmax code length in block"),

    ("35852b74", ':', 0.425, "Use pread"),
    # This is a performance degradation only on Taurus and on Ryzen maybe with Silesia but not the others
    #   -> This has become moot with 70bb8917:
    #      "[performance] Avoid even more calls to BitReader::read and arithmetic operations: 345 -> 410 MB/s"
    ("8fff46b1", ':', 0.450, "Unroll loop over uncompressed data"),
    # [ ] These imply a better performance by increasing the current chunk size of 4 MiB to 8 MiB.
    #     Memory shouldn't be that much of an issue anymore thanks to chunk splitting.
    #     For CTU-13-Dataset.tar.gz it still is though. It takes roughly 10 GB for 24 threads with 4 MiB chunks
    #     and twice that with 8 MiB chunks. And it still only reaches ~1.5 GB/s :(
    #     - [ ] Parallelize decompression of such chunks with very large compression ratio.
    ("3d385061", ':', 0.500, "Decrease chunk size from 8->2 MiB\nto quarter memory usage"),
    ("68c3fec9", ':', 0.525, "Increase chunk size from 1->8 MiB"),
    ("f5865ffe", ':', 0.550, "Use new block finder in GzipBlockFetcher"),
    # [ ] These imply possible performance gains of maybe 5% by speeding up the uncompressed block finder,
    #     e.g., by exposing the BitReader byte buffer and look inside there to avoid costly read calls.
    #     Additionally these imply the same:
    #       24f2d8f2
    #       8fff46b1
    ("5aa05120", ':', 0.650, "Alternatingly look for dynamic and\nuncompressed deflate blocks in chunks"),
    ("1001e4b0", ':', 0.675, "Also look for uncompressed deflate blocks"),
]

transform = ax.get_xaxis_transform()


shortCommitLength = len(lastFunctionCommitPerVersion[0][0])
shortCommits = [c[:shortCommitLength] for c in commits]
for commit, linestyle, position, label in lastFunctionCommitPerVersion:
    try:
        x = 1 + shortCommits.index(commit)
        ax.axvline(x, color='k', linestyle=linestyle)
        ax.text(x, position, label, transform=transform, wrap=True, linespacing=0.9, verticalalignment='top',
                horizontalalignment='left' if x < shortCommits.index('dd678c7c') else 'right')

    except ValueError:
        pass

ax.legend(loc='upper left')
fig.tight_layout()

fig.savefig("benchmark-commits-per-compiler.pdf")
fig.savefig("benchmark-commits-per-compiler.png")

plt.show()
