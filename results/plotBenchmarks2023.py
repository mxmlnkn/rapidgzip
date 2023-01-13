#!/usr/bin/env python3

import matplotlib.patches as mpatches
import matplotlib.pyplot as plt
import matplotlib.ticker
from matplotlib.lines import Line2D
from matplotlib.ticker import NullFormatter, ScalarFormatter, StrMethodFormatter
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
    filePath = os.path.join(folder, "result-bitreader-reads.dat")
    if not os.path.isfile(filePath):
        return None
    data = np.loadtxt(filePath)

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
    figs = []
    for pinning in ["no-pinning", "sequential-pinning", "recursive-pinning"]:
        fileName = f"result-read-file-parallel-{pinning}.dat"
        filePath = os.path.join(folder, fileName)
        if not os.path.isfile(filePath) or os.stat(filePath).st_size == 0:
            continue
        data = np.loadtxt(filePath)

        fig = plt.figure(figsize=(6, 3.5))
        ax = fig.add_subplot(111, xlabel="Number of Threads", ylabel="Bandwidth / (GB/s)", xscale = 'log')
        ax.grid(axis='both')
        threadCounts = list(np.unique(data[:, 0]))
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
        ax.set_xticks([int(x) for x in threadCounts[:8] + threadCounts[4::2]])
        ax.minorticks_off()
        ax.xaxis.set_major_formatter(ScalarFormatter())
        ax.yaxis.set_minor_formatter(ScalarFormatter())
        ax.yaxis.set_major_formatter(ScalarFormatter())

        fig.tight_layout()

        fig.savefig(f"filereader-bandwidths-number-of-threads-{pinning}.png")
        fig.savefig(f"filereader-bandwidths-number-of-threads-{pinning}.pdf")

        fig.suptitle(pinning);

        figs.append(fig)

    return figs


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
    fig = plt.figure(figsize=(12, 2.5))
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
        ("Pugz block finder" , "results-pugz-sync.dat"),
        ("DBF skip-LUT" , "result-find-dynamic-pragzip-skip-lut.dat"),
        ("DBF pragzip" , "result-find-dynamic.dat"),
        ("NBF" , "result-find-uncompressed.dat"),
        ("Marker replacement" , "result-apply-window.dat"),
        ("File writing" , "result-file-write.dat"),
        ("Count newlines" , "result-count-newlines.dat"),
    ]

    ticks = []
    for i, component in enumerate(components[::-1]):
        label, fname = component
        filePath = os.path.join(folder, fname)
        if not os.path.isfile(filePath):
            continue

        data = np.loadtxt(filePath, ndmin = 2)
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

    if not ticks:
        plt.close(fig)
        return

    ax.yaxis.set_ticks([x[0] for x in ticks])
    ax.yaxis.set_ticklabels([x[1] for x in ticks])
    ax.tick_params(axis='y', which='minor', bottom=False)
    ax.xaxis.set_major_formatter(ScalarFormatter())

    fig.tight_layout()

    fig.savefig("components-bandwidths.png")
    fig.savefig("components-bandwidths.pdf")
    return fig


def plotParallelDecompression(legacyPrefix, parallelPrefix, outputType='dev-null'):  # alternative: count-lines
    fig = plt.figure(figsize=(6, 3.5))
    ax = fig.add_subplot(111, xlabel="Number of Cores", ylabel="Bandwidth / (MB/s)", xscale = 'log', yscale = 'log')
    ax.grid(axis='both')

    tools = [
        ("pragzip", f"{parallelPrefix}-pragzip-{outputType}.dat", "tab:red"),
        ("pragzip (index)", f"{parallelPrefix}-pragzip-index-{outputType}.dat", "tab:orange"),
        ("pugz (sync)", f"{parallelPrefix}-pugz-sync-{outputType}.dat", "tab:cyan"),
        ("pugz", f"{parallelPrefix}-pugz-{outputType}.dat", "tab:blue"),
        ("pigz", f"{legacyPrefix}-pigz-{outputType}.dat", "tab:brown"),
        ("igzip", f"{legacyPrefix}-igzip-{outputType}.dat", "tab:purple"),
        ("gzip", f"{legacyPrefix}-gzip-{outputType}.dat", "tab:green"),
    ]

    symbols = []
    labels = []
    threadCountsTicks = []
    for tool, fileName, color in tools:
        filePath=os.path.join(folder, fileName)
        if not os.path.isfile(filePath):
            print("Skipping missing file:", filePath)
            continue
        data = np.loadtxt(filePath, ndmin = 2)

        positions = []
        bandwidths = []
        widths = []
        threadCounts = list(np.unique(data[:, 0]))
        if len(threadCounts) > len(threadCountsTicks):
            threadCountsTicks = threadCounts

        if 'igzip' in tool:
            threadCounts = [1]

        for threadCount in sorted(threadCounts):
            subdata = data[data[:, 0] == threadCount]
            bandwidths.append(subdata[:, 1] / subdata[:, 2] / 1e6)
            positions.append(threadCount)

        if tool.startswith('gzip'):
            print(f"Gzip speed: {np.median( bandwidths ):.2f} MB/s")

        if tool.startswith('igzip'):
            print(f"igzip speed: {np.median( bandwidths ):.2f} MB/s")

        if tool.startswith('igzip') or tool.startswith('gzip'):
            ax.axhline(np.median(bandwidths[0]), color = color, linestyle = ':', alpha = 0.75)

        if tool.startswith('pragzip') and not 'index' in tool:
            for i in range(len(bandwidths)):
                count = positions[i]
                bandwidth = bandwidths[i]
                print(f"Pragzip speed: {np.median( bandwidth ):.2f} MB/s for {count} cores")

        result = ax.violinplot(bandwidths, positions = positions, widths = np.array(positions) / 10.,
                               showextrema = False, showmedians = False)
        for body in result['bodies']:
            body.set_zorder(3)
            body.set_alpha(0.75)
            body.set_color(color)

        if tool.startswith('igzip') or tool.startswith('gzip'):
            symbols.append(Line2D([0], [0], color = color, alpha = 0.75, linestyle = ':'))
        else:
            symbols.append(mpatches.Patch(color = color, alpha = 0.75))
        labels.append(tool)

    if not labels:
        plt.close(fig)
        return

    # Add ideal scaling for comparison
    filePath = os.path.join(folder, f"{parallelPrefix}-pragzip-{outputType}.dat")
    if os.path.isfile(filePath):
        data = np.loadtxt(filePath, ndmin = 2)
        threadCount = 1
        subdata = data[data[:, 0] == threadCount]
        bandwidths = subdata[:, 1] / subdata[:, 2] / 1e6
        ax.plot(threadCountsTicks, np.median(bandwidths) * np.array(threadCountsTicks), linestyle = '--',
                label = "ideal linear scaling", color = "tab:red", alpha = 0.75)
    symbols.append(Line2D([0], [0], color = "tab:red", alpha = 0.75, linestyle = '--'))
    labels.append("linear scaling")

    ax.set_ylim((100, ax.get_ylim()[1]));
    ax.set_xticks([int(x) for x in threadCountsTicks])
    ax.minorticks_off()
    ax.xaxis.set_major_formatter(ScalarFormatter())
    ax.yaxis.set_major_locator(matplotlib.ticker.LogLocator(subs=(1.0, 0.5, 0.2)))
    ax.yaxis.set_minor_formatter(ScalarFormatter())
    ax.yaxis.set_major_formatter(ScalarFormatter())

    ax.legend(symbols, labels, loc="upper left")

    fig.tight_layout()

    fig.savefig(f"{parallelPrefix}-{outputType}-bandwidths-number-of-threads.png")
    fig.savefig(f"{parallelPrefix}-{outputType}-bandwidths-number-of-threads.pdf")
    return fig


def plotChunkSizes():
    fig = plt.figure(figsize=(6, 3.5))
    ax = fig.add_subplot(111, xlabel="Chunk Size / MiB", ylabel="Bandwidth / (MB/s)", xscale = 'log', yscale = 'log')
    ax.grid(axis='both')

    tools = [
        ("pragzip", f"result-chunk-size-pragzip-dev-null.dat", "tab:red"),
        ("pugz", f"result-chunk-size-pugz-dev-null.dat", "tab:blue"),
    ]

    symbols = []
    labels = []
    xTicks = []
    for tool, fileName, color in tools:
        filePath = os.path.join(folder, fileName)
        if not os.path.isfile(filePath):
            print("Ignore missing file:", filePath)
            continue
        data = np.loadtxt(filePath, ndmin = 2)
        if not data:
            print("Ignore empty file:", filePath)
            continue

        positions = []
        bandwidths = []
        widths = []
        chunkSizes = list(np.unique(data[:, 1]))
        print(chunkSizes)
        if len(chunkSizes) > len(xTicks):
            xTicks = np.array(chunkSizes) / 1024.**2

        for chunkSize in sorted(chunkSizes):
            subdata = data[data[:, 1] == chunkSize]
            bandwidths.append(subdata[:, 2] / subdata[:, 3] / 1e6)
            positions.append(chunkSize / 1024.**2)

        result = ax.violinplot(bandwidths, positions = positions, widths = np.array(positions) / 10.,
                               showextrema = False, showmedians = False)
        for body in result['bodies']:
            body.set_zorder(3)
            body.set_alpha(0.75)
            body.set_color(color)

        symbols.append(mpatches.Patch(color = color, alpha = 0.75))
        labels.append(tool)

    if not labels:
        plt.close(fig)
        return

    #ax.set_ylim((100, ax.get_ylim()[1]));
    print(xTicks)
    ax.set_ylim([900,3000])
    ax.set_xticks([int(x) if int(x) == x else x for x in xTicks])
    ax.minorticks_off()
    ax.xaxis.set_major_formatter(StrMethodFormatter('{x:g}'))
    ax.yaxis.set_major_locator(matplotlib.ticker.LogLocator(subs=(1.0, 0.5, 0.2, 0.15, 0.3)))
    ax.yaxis.set_minor_formatter(ScalarFormatter())
    ax.yaxis.set_major_formatter(ScalarFormatter())

    ax.legend(symbols, labels, loc="upper left")

    fig.tight_layout()

    fig.savefig(f"decompression-chunk-size-bandwidths-number-of-threads.png")
    fig.savefig(f"decompression-chunk-size-bandwidths-number-of-threads.pdf")
    return fig


# Old tests as to how to plot but the samples correctly but violing plots are sufficient
#plotBitReaderHistograms()
#plotBitReaderSelectedHistogram([24])

plotChunkSizes()
plotParallelDecompression("result-decompression-base64", "result-parallel-decompression-base64", "dev-null")
plotParallelDecompression("result-decompression-silesia", "result-parallel-decompression-silesia", "dev-null")
plotParallelReadingBandwidths()
plotBitReaderBandwidths()
plotComponentBandwidths()

plt.show()
