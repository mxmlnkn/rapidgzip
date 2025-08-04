import gzip
import os
import sys
import time

import indexed_gzip
import rapidgzip


gzipFilePath = sys.argv[1]
# Determine uncompressed file size
fileSize = 0
with gzip.open(gzipFilePath) as file:
    while result := file.read(4 * 1024 * 1024):
        fileSize += len(result)


# Benchmark default Python gzip implementation
print(f"\n== Benchmark decompression with Python's gzip module ==\n")
with gzip.open(gzipFilePath) as file:
    t0 = time.time()
    while file.read(512 * 1024):
        pass
    gzipDuration = time.time() - t0
    print(f"Decompression time: {gzipDuration:.2f}s, Bandwidth: {fileSize / gzipDuration / 1e6:.0f} MB/s")


# Create index
if not os.path.exists(gzipFilePath + ".index"):
    with indexed_gzip.IndexedGzipFile(gzipFilePath) as file:
        file.build_full_index()
        file.export_index(gzipFilePath + ".index")


# Benchmark decompression with rapidgzip with and without index
# parallelization = 0 means that it is automatically using all available cores.
for withIndex in [True, False]:
    print(f"\n== Benchmark decompression with rapidgzip {'with' if withIndex else 'without'} an existing index ==\n")
    for parallelization in [0, 1, 2, 6, 12, 24, 32]:
        with rapidgzip.RapidgzipFile(gzipFilePath, parallelization=parallelization) as file:
            if withIndex:
                file.import_index(open(gzipFilePath + ".index", 'rb'))

            t0 = time.time()
            # Unfortunately, the chunk size is very performance critical! It might depend on the cache size.
            while file.read(512 * 1024):
                pass
            rapidgzipDuration = time.time() - t0
            print(
                f"Parallelization: {parallelization}, Decompression time: {rapidgzipDuration:.2f}s"
                f", Bandwidth: {fileSize / rapidgzipDuration / 1e6:.0f} MB/s"
                f", Speedup: {gzipDuration / rapidgzipDuration:.1f}"
            )
