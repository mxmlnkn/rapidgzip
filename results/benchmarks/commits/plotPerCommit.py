#!/usr/bin/env python3

import matplotlib.pyplot as plt
from matplotlib.ticker import ScalarFormatter
import numpy as np
import math
import os
import sys

data = np.array([
    [6099, 6283, 15, 6541],
    [6706, 6832, 10, 6971],
    [6227, 6418, 11, 6590],
    [6610, 6744,  7, 6865],
    [6182, 6382, 12, 6510],
    [6144, 6364, 11, 6541],
    [6353, 6678, 15, 6829],
    [6339, 6649, 12, 6786],
    [6531, 6675,  9, 6827],
    [6508, 6697, 10, 6833],
    [6320, 6559, 14, 6751],
    [6189, 6317,  9, 6472],
    [6560, 6682,  7, 6785],
    [6169, 6360,  9, 6469],
    [6480, 6677, 10, 6776],
    [6254, 6361,  7, 6482],
    [6465, 6697, 15, 6885],
])

data = np.array([
    [5783, 5911,  7, 6019],
    [5673, 5910, 10, 6016],
    [5496, 5722,  8, 5786],
    [5181, 5489, 22, 5946],
    [5572, 5695,  8, 5805],
    [5624, 5739,  6, 5822],
    [5693, 5903,  9, 6022],
    [5724, 5889, 11, 6051],
    [5782, 5888,  7, 6022],
    [5843, 5927,  5, 5997],
    [5606, 5814,  9, 5909],
    [5631, 5764,  8, 5877],
    [5723, 5885, 10, 5993],
    [5530, 5772,  9, 5833],
    [5699, 5938,  9, 6023],
    [5705, 5793,  5, 5854],
    [5856, 5938,  6, 6047],
])

labels = [
    "3ca62ea3",
    "da1fdb11",
    "12c0f19d",
    "bb547cc9",
    "adfa74f0",
    "3f522847",
    "3cbe3e6c",
    "3fa73118",
    "f34a599d",
    "2ca74763",
    "1a0f7d23",
    "f24191e8",
    "58ac926e",
    "0f3bc590",
    "8799e612",
    "ae8785bc",
    "21ba498b",
]


fig = plt.figure(figsize=(16, 8))
ax = fig.add_subplot(111, ylabel="Bandwidth in MB/s", xlabel="Commits from newest to oldest")
ax.fill_between(labels, data[:,0], data[:,3], color='tab:blue')
ax.errorbar(labels, data[:,1], yerr=data[:,2], capsize=4, marker='o', color='white')

fig.tight_layout()
fig.savefig("4GiB-base64-per-commit.png")

plt.show()
