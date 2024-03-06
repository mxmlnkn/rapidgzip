#!/usr/bin/env python3

import sys

import numpy as np
import matplotlib.pyplot as plt
import pandas as pd


def plotComparison(filePath):
    data = pd.read_csv(filePath, header=0, sep=';', comment='#')
    distributions = data['Code Length Distribution'].unique()

    fig = plt.figure(figsize=(9, 12))
    for iQuantity, quantity in enumerate(['Runtime', 'Construction Time', 'Decode Time']):
        for (iAxis, distribution) in enumerate(distributions):
            distributionData = data[data['Code Length Distribution'] == distribution]
            implementations = distributionData['Implementation'].unique()

            nColumns = 2
            iSubplot = 1 + iQuantity * nColumns + iAxis
            ax = fig.add_subplot(3, nColumns, iSubplot)
            ax.set_title(distribution)
            ax.set_xlabel('Maximum Length')
            ax.set_ylabel(quantity + "/ ms")

            for implementation in sorted(implementations):
                implementationData = distributionData[distributionData['Implementation'] == implementation]
                plt.errorbar(implementationData['Maximum Length'],
                             implementationData[quantity + ' Average'] * 1e3,
                             implementationData[quantity + ' StdDev'] * 1e3,
                             label=implementation, marker='.')

            if iSubplot == 1:
                plt.legend(loc='best')

    fig.tight_layout()
    fig.savefig(filePath.replace(".dat", "") + ".pdf")
    fig.savefig(filePath.replace(".dat", "") + ".png")

if __name__ == "__main__":
    plotComparison(sys.argv[1])
    plt.show()
