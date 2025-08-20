#!/usr/bin/env python3

import sys
from pathlib import Path

import numpy as np
import matplotlib.pyplot as plt


data = np.array([int(x) for x in Path(sys.argv[1]).read_text().replace(' ', '').replace('\n', '').split(',') if x])

for row_length in [32, 64, 128, 256]:
    image = data.reshape((len(data) // row_length, row_length))

    fig = plt.figure(figsize=(6,9))
    ax = fig.add_subplot(111)
    ax.imshow(image, cmap='viridis', interpolation='nearest')
    im = ax.imshow(image, cmap='viridis', interpolation='nearest')
    fig.colorbar(im, ax=ax, label="Value")
    ax.set_title(f"Array Visualization ({row_length} columns per row)")
    fig.tight_layout()

plt.show()
