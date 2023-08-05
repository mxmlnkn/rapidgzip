#!/usr/bin/env python3

import matplotlib.pyplot as plt
import numpy as np
import os, sys

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
            commits.append(tokens[0][0:9])
            minTimes.append(np.min(times))
            avgTimes.append(np.mean(times))
            maxTimes.append(np.max(times))

    return label, commits, np.array(minTimes), np.array(avgTimes), np.array(maxTimes)


fig = plt.figure(figsize=(10, 6))
ax = fig.add_subplot(111, ylabel="Runtime in seconds", xlabel="Commits from newest to oldest")
for logFile in benchmarkLogs:
    label, commits, minTimes, avgTimes, maxTimes = loadData(logFile)
    ax.errorbar(
        np.arange(len(commits)),
        avgTimes,
        yerr=(avgTimes - minTimes, maxTimes - avgTimes),
        # linestyle = '--' if 'clang' in label else '-',
        linestyle=':',
        marker='o' if 'clang' in label else 'v',
        capsize=4,
        label=label,
    )
ax.legend(loc='center right', bbox_to_anchor=(1.3, 0.5))
ax.set_xticklabels([])
# ax.legend(loc='best')
fig.tight_layout()

fig.savefig("benchmark-commits-per-compiler.pdf")
fig.savefig("benchmark-commits-per-compiler.png")

plt.show()
