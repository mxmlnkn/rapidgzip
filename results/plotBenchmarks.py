#!/usr/bin/env python3

import matplotlib.pyplot as plt
import numpy as np
import os, sys

if len( sys.argv ) < 2:
    print( "Please specify the folder containing the benchmark logs!" )
folder = sys.argv[1]
suffix = '-full-first-read.log'
benchmarkLogs = [ os.path.join( folder, file ) for file in os.listdir( folder ) if file.endswith( suffix ) ]

# Return compilerName and lists of min, max, avg values per commit
def loadData( filePath ):
    commits = []
    minTimes = []
    avgTimes = []
    maxTimes = []

    label = filePath.split( '/' )[-1]
    if label.endswith( suffix ):
        label = label[:-len( suffix )]
    with open( filePath, 'rt' ) as file:
        for line in file:
            tokens = line.split( ' ' )
            if len( tokens ) < 3:
                continue

            commit = tokens[0][20:-1]
            if commit in commits:
                continue
            commits += [ commit ]
            if '+-' in tokens:
                # version 1: [2020-12-06T21-36][bccbedc] 10.164 <= 10.294 +- 0.105 <= 10.475 at version unknown
                minTimes += [ float( tokens[1] ) ]
                avgTimes += [ float( tokens[3] ) ]
                maxTimes += [ float( tokens[7] ) ]
            else:
                # version 2: [2020-12-06T23-01][9ca572f] 8.42 8.34 8.49 8.67 8.58
                times = np.array( [ float( t ) for t in tokens[1:] ] )
                minTimes += [ np.min( times ) ]
                avgTimes += [ np.mean( times ) ]
                maxTimes += [ np.max( times ) ]

    return label, commits, np.array( minTimes ), np.array( avgTimes ), np.array( maxTimes )

fig = plt.figure( figsize = ( 10,6 ) )
ax = fig.add_subplot( 111, ylabel = "Runtime in seconds", xlabel = "Commits from oldest to newest" )
ax.set_title( "Decoding 128MiB of random data compressed to BZ2" )
for logFile in benchmarkLogs:
    label, commits, minTimes, avgTimes, maxTimes = loadData( logFile )
    ax.errorbar( np.arange( len( commits ) ), avgTimes, yerr = ( avgTimes - minTimes, maxTimes - avgTimes ),
                 #linestyle = '--' if 'clang' in label else '-',
                 linestyle = ':',
                 marker = 'o' if 'clang' in label else 'v', capsize = 4, label = label )
ax.legend( loc = 'center right', bbox_to_anchor = ( 1.3, 0.5 ) )
ax.set_xticklabels( [] )
#ax.legend( loc = 'best' )
fig.tight_layout()

fig.savefig( "benchmark-commits-per-compiler.pdf" )
fig.savefig( "benchmark-commits-per-compiler.png" )

plt.show()
