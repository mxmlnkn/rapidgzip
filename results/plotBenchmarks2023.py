#!/usr/bin/env python3

import matplotlib.pyplot as plt
from matplotlib.ticker import NullFormatter, ScalarFormatter
import numpy as np
import os, sys


folder = "." if len(sys.argv) < 2 else sys.argv[1]


def plotBitReaderHistograms():
    data = np.loadtxt(os.path.join(folder, "result-bitreader-reads.dat"))

    fig = plt.figure(figsize=(6, 4))
    ax = fig.add_subplot(111, xlabel="Bandwidth / (MB/s)", ylabel="Frequency", xscale='log')
    for nBits in [1, 2, 8, 16]:
        subdata = data[data[:, 0] == nBits]
        bandwidths = subdata[:, 1] / subdata[:, 2] / 1e6
        ax.hist(bandwidths, bins=20, label=f"{nBits} bits per read")
    ax.legend(loc="best")
    fig.tight_layout()

    fig.savefig("bitreader-bandwidth-multiple-histograms.png")
    fig.savefig("bitreader-bandwidth-multiple-histograms.pdf")
    return fig


def plotBitReaderSelectedHistogram(nBitsToPlot):
    data = np.loadtxt(os.path.join(folder, "result-bitreader-reads.dat"))

    fig = plt.figure(figsize=(12, 4))
    ax = fig.add_subplot(111, xlabel="Bandwidth / (MB/s)", ylabel="Frequency", xscale='log')
    for nBits in nBitsToPlot:
        subdata = data[data[:, 0] == nBits]
        bandwidths = subdata[:, 1] / subdata[:, 2] / 1e6
        ax.hist(bandwidths, bins=100, label=f"{nBits} bits per read")
    ax.legend(loc="best")
    fig.tight_layout()

    fig.savefig("bitreader-bandwidth-selected-histogram.png")
    fig.savefig("bitreader-bandwidth-selected-histogram.pdf")
    return fig


def plotBitReaderBandwidths():
    data = np.loadtxt(os.path.join(folder, "result-bitreader-reads.dat"))

    fig = plt.figure(figsize=(6, 3.5))
    ax = fig.add_subplot(111, xlabel="Bits Per Read Call", ylabel="Bandwidth / (MB/s)")
    ax.grid(axis='both')
    nBitsTested = np.unique(data[:, 0])
    for nBits in sorted(nBitsTested):
        subdata = data[data[:, 0] == nBits]
        bandwidths = subdata[:, 1] / subdata[:, 2] / 1e6
        #ax.boxplot(runtimes, positions = [nBits], showfliers=False)
        result = ax.violinplot(bandwidths, positions = [nBits], widths = 1, showextrema = False, showmedians = True)
        for body in result['bodies']:
            body.set_zorder(3)
            body.set_alpha(0.75)
            body.set_color('b')
        if body := result['cmedians']:
            body.set(zorder=3, color='0.75')

    ax.set_xlim([min(nBitsTested) - 1, max(nBitsTested) + 1])
    ax.set_ylim([0, ax.get_ylim()[1]])
    #ax.set_xticks(nBitsTested)
    ax.yaxis.set_minor_formatter(ScalarFormatter())
    ax.yaxis.set_major_formatter(ScalarFormatter())

    fig.tight_layout()

    fig.savefig("bitreader-bandwidths-over-bits-per-read.png")
    fig.savefig("bitreader-bandwidths-over-bits-per-read.pdf")
    return fig


def plotParallelReadingBandwidths():
    data = np.loadtxt(os.path.join(folder, "result-read-file-parallel.dat"))

    fig = plt.figure(figsize=(6, 3.5))
    ax = fig.add_subplot(111, xlabel="Number of Threads", ylabel="Bandwidth / (GB/s)", xscale = 'log')
    ax.grid(axis='both')
    threadCounts = np.unique(data[:, 0])
    for threadCount in sorted(threadCounts):
        subdata = data[data[:, 0] == threadCount]
        bandwidths = subdata[:, 1] / subdata[:, 3] / 1e9
        widths = threadCount / 10.
        result = ax.violinplot(bandwidths, positions = [threadCount], widths = widths,
                               showextrema = False, showmedians = True)
        for body in result['bodies']:
            body.set_zorder(3)
            body.set_alpha(0.75)
            body.set_color('b')
        if body := result['cmedians']:
            body.set(zorder=3, color='0.75')

    ax.set_ylim([0, ax.get_ylim()[1]])
    ax.set_xticks([int(x) for x in threadCounts])
    ax.minorticks_off()
    ax.xaxis.set_major_formatter(ScalarFormatter())
    ax.yaxis.set_minor_formatter(ScalarFormatter())
    ax.yaxis.set_major_formatter(ScalarFormatter())

    fig.tight_layout()

    fig.savefig("filereader-bandwidths-number-of-threads.png")
    fig.savefig("filereader-bandwidths-number-of-threads.pdf")
    return fig


def formatBytes(nBytes):
    if nBytes > 1e9:
        return f"{nBytes/1e9:.1f} GB"
    if nBytes > 1e8:
        return f"{nBytes/1e6:.0f} MB"
    if nBytes > 1e6:
        return f"{nBytes/1e6:.1f} MB"
    if nBytes > 1e5:
        return f"{nBytes/1e3:.0f} kB"
    if nBytes > 1e3:
        return f"{nBytes/1e3:.1f} kB"
    return f"{nBytes:.1f} B"


def plotComponentBandwidths():
    fig = plt.figure(figsize=(12, 2))
    ax = fig.add_subplot(111, xlabel="Bandwidth / (MB/s)", xscale = 'log')
    ax.grid(axis='both')

    #components = [
    #    ("DBF trial and error (TAE) with zlib" , "result-find-dynamic-zlib.dat"),
    #    ("DBF TAE with custom deflate" , "result-find-dynamic-pragzip.dat"),
    #    ("DBF TAE with custom deflate and skip-LUT" , "result-find-dynamic-pragzip-skip-lut.dat"),
    #    ("Dynamic block finder (DBF)" , "result-find-dynamic.dat"),
    #    ("Uncompressed block finder" , "result-find-uncompressed.dat"),
    #    ("Marker replacement" , "result-apply-window.dat"),
    #    ("Count newlines" , "result-count-newlines.dat"),
    #]

    components = [
        ("DBF zlib" , "result-find-dynamic-zlib.dat"),
        ("DBF custom deflate" , "result-find-dynamic-pragzip.dat"),
        ("DBF pugz" , "results-pugz-sync.dat"),
        ("DBF skip-LUT" , "result-find-dynamic-pragzip-skip-lut.dat"),
        ("DBF pragzip" , "result-find-dynamic.dat"),
        ("NBF" , "result-find-uncompressed.dat"),
        ("Marker replacement" , "result-apply-window.dat"),
        ("Count newlines" , "result-count-newlines.dat"),
    ]

    ticks = []
    for i, component in enumerate(components[::-1]):
        label, fname = component

        data = np.loadtxt(os.path.join(folder, fname))
        bandwidths = data[:, 0] / data[:, 1]

        labelWithMedian = f"{label} ({formatBytes( np.median( bandwidths ) )}/s)"
        ticks.append( (i, labelWithMedian) )

        # Do not show medians because the "violin" is almost as flat as the median line
        result = ax.violinplot(bandwidths / 1e6, positions = [i], vert = False, widths = [1],
                               showextrema = False, showmedians = False)
        for body in result['bodies']:
            body.set_zorder(3)
            body.set_alpha(0.75)
            body.set_color('b')

    ax.yaxis.set_ticks([x[0] for x in ticks])
    ax.yaxis.set_ticklabels([x[1] for x in ticks])
    ax.tick_params(axis='y', which='minor', bottom=False)
    ax.xaxis.set_major_formatter(ScalarFormatter())

    fig.tight_layout()

    fig.savefig("components-bandwidths.png")
    fig.savefig("components-bandwidths.pdf")
    return fig

# Old tests as to how to plot but the samples correctly but violing plots are sufficient
#plotBitReaderHistograms()
#plotBitReaderSelectedHistogram([24])


plotParallelReadingBandwidths()
plotBitReaderBandwidths()
plotComponentBandwidths()

plt.show()
