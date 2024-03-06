#!/usr/bin/env python3

import re
import sys

import numpy as np
import matplotlib.pyplot as plt
import pandas as pd


def plotLengths(ax, lengths, data):
    ax.set_xlabel('Length')
    ax.set_xlim([np.min(lengths), np.max(lengths)])
    ax.set_ylim([np.min(data), np.max(data)])

    prop_cycle = plt.rcParams['axes.prop_cycle']
    colors = prop_cycle.by_key()['color']

    thresholds = [25, 50, 75, 95]
    threshold = 0
    sum = np.sum(data)
    cumsum = 0
    for i in range(len(lengths)):
        if threshold >= len(thresholds):
            break
        cumsum += data[i]
        if cumsum >= thresholds[threshold] * sum / 100:
            ax.axvline(
                lengths[i], label=f"{thresholds[threshold]}th percentile at {lengths[i]}",
                color=colors[threshold + 1], linestyle=':',
            )
            threshold += 1

    ax.plot(lengths, data, marker='.')

    ax.legend(loc='best')


def plotComparison(fig, filePath, rows, columns, column):
    data = np.genfromtxt(filePath)
    lengths = np.arange(len(data))

    ax = fig.add_subplot(rows, columns, 1 + column)
    ax.set_ylabel('Frequency')
    ax.set_title(re.match(".*/(.*)[.]length-frequencies.dat", filePath).group(1))
    plotLengths(ax, lengths, data)

    ax = fig.add_subplot(rows, columns, 1 + columns + column)
    ax.set_ylabel('Total Bytes')
    plotLengths(ax, lengths, data * lengths)


def plotComparisons(filePaths):
    fig = plt.figure(figsize=(16, 8))

    columns = len(filePaths)
    rows = 2
    for column, filePath in enumerate(filePaths):
        plotComparison(fig, filePath, rows, columns, column)

    fig.tight_layout()
    fig.savefig("length-frequencies.pdf")
    fig.savefig("length-frequencies.png")


if __name__ == "__main__":
    plotComparisons(sys.argv[1:])
    plt.show()
