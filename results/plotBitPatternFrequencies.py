#!/usr/bin/env python3

import matplotlib.pyplot as plt
import numpy as np
import sys

file = sys.argv[1]  # "counts-2B.dat"

data = np.genfromtxt( file, dtype = 'int' ).transpose()

fig = plt.figure()
ax = fig.add_subplot( 111, yscale = 'log' )
ax.step( data[0], data[1] )
fig.tight_layout()
fig.savefig( file + ".pdf" )
fig.savefig( file + ".png" )

iSorted = np.argsort( data[1] )
sortedPatterns = data[0][iSorted]
sortedFrequencies = data[1][iSorted]

print( "Most and least common patterns:" )
for i in range( 1, min( 10, len( sortedPatterns ) / 2 ) ):
    print( f"0x{sortedPatterns[-i]:06x} -> {sortedFrequencies[-i]}" )
print( "..." )
for i in range( min( 10, len( sortedPatterns ) / 2 ) ):
    print( f"0x{sortedPatterns[i]:06x} -> {sortedFrequencies[i]}" )

#plt.show()
