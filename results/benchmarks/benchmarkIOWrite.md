# Test on HomePC

```bash
cmake --build . -- benchmarkIOWrite && src/benchmarks/benchmarkIOWrite /dev/shm/pragzip-write-test
```

## File Creation

    posix_fallocate file sized 512 MiB: ( min: 5.84402, 6.11 +- 0.13, max: 6.25867 ) GB/s
    posix_fallocate file sized 1 GiB: ( min: 6.21112, 6.31 +- 0.09, max: 6.50644 ) GB/s
    posix_fallocate file sized 2 GiB: ( min: 6.38622, 6.49 +- 0.06, max: 6.57573 ) GB/s
    posix_fallocate file sized 4 GiB: ( min: 5.56835, 5.79 +- 0.11, max: 5.90624 ) GB/s

    fallocate file sized 512 MiB: ( min: 4.60836, 4.82 +- 0.15, max: 5.0095 ) GB/s
    fallocate file sized 1 GiB: ( min: 4.15629, 4.61 +- 0.25, max: 4.92278 ) GB/s
    fallocate file sized 2 GiB: ( min: 4.89434, 5.08 +- 0.08, max: 5.14254 ) GB/s
    fallocate file sized 4 GiB: ( min: 5.59217, 5.64 +- 0.05, max: 5.72309 ) GB/s

    ftruncate file sized 512 MiB: ( min: 19745.2, 200000 +- 70000, max: 240749 ) GB/s
    ftruncate file sized 1 GiB: ( min: 328361, 400000 +- 60000, max: 497103 ) GB/s
    ftruncate file sized 2 GiB: ( min: 673192, 810000 +- 100000, max: 909951 ) GB/s
    ftruncate file sized 4 GiB: ( min: 1.14532e+06, 1460000 +- 190000, max: 1.8199e+06 ) GB/s

## Mmap Write

    ftruncate + mmap write 1 GiB: ( min: 1.78614, 1.829 +- 0.022, max: 1.86556 ) GB/s
    ftruncate + mmap write 1 GiB using  1 threads and maps: ( min: 1.62298, 1.652 +- 0.024, max: 1.68956 ) GB/s
    ftruncate + mmap write 1 GiB using  2 threads and maps: ( min: 2.26482, 2.36 +- 0.07, max: 2.51335 ) GB/s
    ftruncate + mmap write 1 GiB using  4 threads and maps: ( min: 3.45573, 3.61 +- 0.12, max: 3.82636 ) GB/s
    ftruncate + mmap write 1 GiB using  8 threads and maps: ( min: 4.5674, 4.71 +- 0.14, max: 4.95821 ) GB/s
    ftruncate + mmap write 1 GiB using 16 threads and maps: ( min: 4.43107, 4.70 +- 0.21, max: 5.10833 ) GB/s

    ftruncate + mmap write 1 GiB using  1 threads and maps and fds: ( min: 1.67507, 1.709 +- 0.021, max: 1.74593 ) GB/s
    ftruncate + mmap write 1 GiB using  2 threads and maps and fds: ( min: 2.31731, 2.44 +- 0.12, max: 2.70069 ) GB/s
    ftruncate + mmap write 1 GiB using  4 threads and maps and fds: ( min: 3.51626, 3.57 +- 0.05, max: 3.65022 ) GB/s
    ftruncate + mmap write 1 GiB using  8 threads and maps and fds: ( min: 4.47624, 4.72 +- 0.19, max: 5.00709 ) GB/s
    ftruncate + mmap write 1 GiB using 16 threads and maps and fds: ( min: 4.37408, 4.71 +- 0.22, max: 5.09138 ) GB/s

    ftruncate + mmap write 1 GiB using  1 threads: ( min: 1.67221, 1.702 +- 0.023, max: 1.74172 ) GB/s
    ftruncate + mmap write 1 GiB using  2 threads: ( min: 2.23156, 2.29 +- 0.04, max: 2.33281 ) GB/s
    ftruncate + mmap write 1 GiB using  4 threads: ( min: 2.97806, 3.09 +- 0.06, max: 3.19617 ) GB/s
    ftruncate + mmap write 1 GiB using  8 threads: ( min: 3.39216, 3.57 +- 0.08, max: 3.63343 ) GB/s
    ftruncate + mmap write 1 GiB using 16 threads: ( min: 3.53481, 3.59 +- 0.04, max: 3.63589 ) GB/s

## Write into an emptied file

### Vectorized Writing

    writev 1 GiB into an emptied file in  1 KiB chunks (x128): ( min: 2.17462, 2.203 +- 0.018, max: 2.23422 ) GB/s
    writev 1 GiB into an emptied file in  4 KiB chunks (x128): ( min: 3.11778, 3.18 +- 0.04, max: 3.22551 ) GB/s
    writev 1 GiB into an emptied file in 16 KiB chunks (x128): ( min: 3.12581, 3.21 +- 0.05, max: 3.28576 ) GB/s
    writev 1 GiB into an emptied file in 64 KiB chunks (x128): ( min: 2.94272, 3.17 +- 0.10, max: 3.26525 ) GB/s
    writev 1 GiB into an emptied file in  1 MiB chunks (x128): ( min: 3.17186, 3.22 +- 0.04, max: 3.27871 ) GB/s
    writev 1 GiB into an emptied file in  8 MiB chunks (x128): ( min: 3.15734, 3.25 +- 0.05, max: 3.34077 ) GB/s

    pwritev 1 GiB into an emptied file in  1 KiB chunks (x128): ( min: 2.13577, 2.24 +- 0.05, max: 2.30555 ) GB/s
    pwritev 1 GiB into an emptied file in  4 KiB chunks (x128): ( min: 3.16202, 3.25 +- 0.04, max: 3.28277 ) GB/s
    pwritev 1 GiB into an emptied file in 16 KiB chunks (x128): ( min: 3.22056, 3.29 +- 0.04, max: 3.34698 ) GB/s
    pwritev 1 GiB into an emptied file in 64 KiB chunks (x128): ( min: 3.16224, 3.30 +- 0.06, max: 3.39591 ) GB/s
    pwritev 1 GiB into an emptied file in  1 MiB chunks (x128): ( min: 3.14943, 3.29 +- 0.06, max: 3.3547 ) GB/s
    pwritev 1 GiB into an emptied file in  8 MiB chunks (x128): ( min: 3.08709, 3.36 +- 0.10, max: 3.44127 ) GB/s

### Parallel Writing

    Use pwrite to write 1 GiB into an emptied file using  1 threads: ( min: 3.65501, 3.78 +- 0.08, max: 3.91278 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  2 threads: ( min: 3.58177, 3.73 +- 0.10, max: 3.85989 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  4 threads: ( min: 3.46004, 3.67 +- 0.11, max: 3.75483 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  8 threads: ( min: 3.2711, 3.64 +- 0.14, max: 3.74627 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using 16 threads: ( min: 3.06714, 3.39 +- 0.18, max: 3.59429 ) GB/s

    Write 1 GiB into one file per thread using  1 threads: ( min: 3.60361, 3.94 +- 0.17, max: 4.11549 ) GB/s
    Write 1 GiB into one file per thread using  2 threads: ( min: 6.03597, 6.18 +- 0.11, max: 6.34634 ) GB/s
    Write 1 GiB into one file per thread using  4 threads: ( min: 7.75313, 8.06 +- 0.16, max: 8.22231 ) GB/s
    Write 1 GiB into one file per thread using  8 threads: ( min: 7.80952, 8.19 +- 0.17, max: 8.40012 ) GB/s
    Write 1 GiB into one file per thread using 16 threads: ( min: 7.78106, 8.04 +- 0.20, max: 8.33951 ) GB/s

### Simple Writing

    fwrite 1 GiB into an emptied file in   1 KiB chunks: ( min: 2.50099, 2.54 +- 0.03, max: 2.59498 ) GB/s
    fwrite 1 GiB into an emptied file in   4 KiB chunks: ( min: 2.61004, 2.74 +- 0.07, max: 2.85374 ) GB/s
    fwrite 1 GiB into an emptied file in   8 KiB chunks: ( min: 2.62658, 2.75 +- 0.05, max: 2.82849 ) GB/s
    fwrite 1 GiB into an emptied file in  16 KiB chunks: ( min: 2.88293, 2.98 +- 0.06, max: 3.08301 ) GB/s
    fwrite 1 GiB into an emptied file in  64 KiB chunks: ( min: 3.09199, 3.22 +- 0.06, max: 3.27249 ) GB/s
    fwrite 1 GiB into an emptied file in   1 MiB chunks: ( min: 3.24771, 3.30 +- 0.05, max: 3.44274 ) GB/s
    fwrite 1 GiB into an emptied file in  16 MiB chunks: ( min: 3.24038, 3.31 +- 0.05, max: 3.40273 ) GB/s
    fwrite 1 GiB into an emptied file in  64 MiB chunks: ( min: 3.08652, 3.35 +- 0.10, max: 3.44806 ) GB/s
    fwrite 1 GiB into an emptied file in 512 MiB chunks: ( min: 3.11291, 3.31 +- 0.10, max: 3.39963 ) GB/s
    fwrite 1 GiB into an emptied file in   1 GiB chunks: ( min: 2.90378, 3.29 +- 0.17, max: 3.46138 ) GB/s

    write 1 GiB into an emptied file in   1 KiB chunks: ( min: 1.60368, 1.69 +- 0.05, max: 1.76948 ) GB/s
    write 1 GiB into an emptied file in   4 KiB chunks: ( min: 2.59264, 2.70 +- 0.06, max: 2.79149 ) GB/s
    write 1 GiB into an emptied file in   8 KiB chunks: ( min: 2.71762, 2.95 +- 0.15, max: 3.1496 ) GB/s
    write 1 GiB into an emptied file in  16 KiB chunks: ( min: 3.1505, 3.27 +- 0.08, max: 3.40037 ) GB/s
    write 1 GiB into an emptied file in  64 KiB chunks: ( min: 3.02575, 3.29 +- 0.12, max: 3.43195 ) GB/s
    write 1 GiB into an emptied file in   1 MiB chunks: ( min: 3.20915, 3.36 +- 0.07, max: 3.4726 ) GB/s
    write 1 GiB into an emptied file in  16 MiB chunks: ( min: 3.13768, 3.33 +- 0.09, max: 3.42021 ) GB/s
    write 1 GiB into an emptied file in  64 MiB chunks: ( min: 3.28928, 3.40 +- 0.07, max: 3.49561 ) GB/s
    write 1 GiB into an emptied file in 512 MiB chunks: ( min: 3.26785, 3.38 +- 0.08, max: 3.50762 ) GB/s
    write 1 GiB into an emptied file in   1 GiB chunks: ( min: 3.26904, 3.35 +- 0.05, max: 3.42906 ) GB/s

## Write into a sparsely allocated file

### Vectorized Writing

    writev 1 GiB into a sparsely allocated file in  1 KiB chunks (x128): ( min: 2.25287, 2.277 +- 0.022, max: 2.32698 ) GB/s
    writev 1 GiB into a sparsely allocated file in  4 KiB chunks (x128): ( min: 3.29108, 3.36 +- 0.05, max: 3.46689 ) GB/s
    writev 1 GiB into a sparsely allocated file in 16 KiB chunks (x128): ( min: 3.14811, 3.37 +- 0.10, max: 3.46988 ) GB/s
    writev 1 GiB into a sparsely allocated file in 64 KiB chunks (x128): ( min: 3.40432, 3.45 +- 0.04, max: 3.52344 ) GB/s
    writev 1 GiB into a sparsely allocated file in  1 MiB chunks (x128): ( min: 3.36995, 3.427 +- 0.030, max: 3.45976 ) GB/s
    writev 1 GiB into a sparsely allocated file in  8 MiB chunks (x128): ( min: 3.33848, 3.40 +- 0.04, max: 3.43727 ) GB/s

    pwritev 1 GiB into a sparsely allocated file in  1 KiB chunks (x128): ( min: 2.28025, 2.321 +- 0.030, max: 2.36948 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in  4 KiB chunks (x128): ( min: 3.36119, 3.41 +- 0.03, max: 3.46142 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in 16 KiB chunks (x128): ( min: 3.3202, 3.44 +- 0.07, max: 3.54471 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in 64 KiB chunks (x128): ( min: 3.34498, 3.43 +- 0.06, max: 3.51855 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in  1 MiB chunks (x128): ( min: 3.20374, 3.34 +- 0.08, max: 3.48548 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in  8 MiB chunks (x128): ( min: 3.313, 3.39 +- 0.05, max: 3.45536 ) GB/s

### Parallel Writing

    Use pwrite to write 1 GiB into a sparsely allocated file using  1 threads: ( min: 3.57206, 3.69 +- 0.05, max: 3.75006 ) GB/s
    Use pwrite to write 1 GiB into a sparsely allocated file using  2 threads: ( min: 3.54508, 3.63 +- 0.05, max: 3.70139 ) GB/s
    Use pwrite to write 1 GiB into a sparsely allocated file using  4 threads: ( min: 3.33329, 3.53 +- 0.13, max: 3.67708 ) GB/s
    Use pwrite to write 1 GiB into a sparsely allocated file using  8 threads: ( min: 3.03255, 3.42 +- 0.19, max: 3.7229 ) GB/s
    Use pwrite to write 1 GiB into a sparsely allocated file using 16 threads: ( min: 2.99007, 3.32 +- 0.21, max: 3.53632 ) GB/s

    Write 1 GiB into one file per thread using  1 threads: ( min: 3.65317, 3.79 +- 0.08, max: 3.95443 ) GB/s
    Write 1 GiB into one file per thread using  2 threads: ( min: 5.65583, 5.89 +- 0.14, max: 6.05127 ) GB/s
    Write 1 GiB into one file per thread using  4 threads: ( min: 7.63074, 7.91 +- 0.14, max: 8.09442 ) GB/s
    Write 1 GiB into one file per thread using  8 threads: ( min: 8.0458, 8.30 +- 0.13, max: 8.47936 ) GB/s
    Write 1 GiB into one file per thread using 16 threads: ( min: 7.80375, 8.13 +- 0.15, max: 8.30999 ) GB/s

### Simple Writing

    fwrite 1 GiB into a sparsely allocated file in   1 KiB chunks: ( min: 2.4474, 2.50 +- 0.03, max: 2.52849 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in   4 KiB chunks: ( min: 2.59826, 2.66 +- 0.05, max: 2.73212 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in   8 KiB chunks: ( min: 2.5873, 2.64 +- 0.05, max: 2.72506 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in  16 KiB chunks: ( min: 2.8395, 2.91 +- 0.05, max: 2.97198 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in  64 KiB chunks: ( min: 3.00593, 3.13 +- 0.06, max: 3.21214 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in   1 MiB chunks: ( min: 3.19368, 3.29 +- 0.06, max: 3.35557 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in  16 MiB chunks: ( min: 3.17769, 3.30 +- 0.05, max: 3.34997 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in  64 MiB chunks: ( min: 3.22617, 3.29 +- 0.05, max: 3.38289 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in 512 MiB chunks: ( min: 3.19531, 3.27 +- 0.05, max: 3.34133 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in   1 GiB chunks: ( min: 3.17465, 3.26 +- 0.05, max: 3.34126 ) GB/s

    write 1 GiB into a sparsely allocated file in   1 KiB chunks: ( min: 1.7458, 1.769 +- 0.018, max: 1.81043 ) GB/s
    write 1 GiB into a sparsely allocated file in   4 KiB chunks: ( min: 2.78764, 2.84 +- 0.03, max: 2.88225 ) GB/s
    write 1 GiB into a sparsely allocated file in   8 KiB chunks: ( min: 2.84975, 3.14 +- 0.12, max: 3.24239 ) GB/s
    write 1 GiB into a sparsely allocated file in  16 KiB chunks: ( min: 3.10793, 3.25 +- 0.07, max: 3.33236 ) GB/s
    write 1 GiB into a sparsely allocated file in  64 KiB chunks: ( min: 3.24308, 3.38 +- 0.08, max: 3.45539 ) GB/s
    write 1 GiB into a sparsely allocated file in   1 MiB chunks: ( min: 3.2138, 3.38 +- 0.08, max: 3.478 ) GB/s
    write 1 GiB into a sparsely allocated file in  16 MiB chunks: ( min: 3.37297, 3.42 +- 0.03, max: 3.46468 ) GB/s
    write 1 GiB into a sparsely allocated file in  64 MiB chunks: ( min: 3.30847, 3.43 +- 0.06, max: 3.49755 ) GB/s
    write 1 GiB into a sparsely allocated file in 512 MiB chunks: ( min: 3.27971, 3.40 +- 0.06, max: 3.48201 ) GB/s
    write 1 GiB into a sparsely allocated file in   1 GiB chunks: ( min: 3.35924, 3.44 +- 0.06, max: 3.51459 ) GB/s

## Write into a preallocated file

### Vectorized Writing

    writev 1 GiB into a preallocated file in  1 KiB chunks (x128): ( min: 2.94674, 3.02 +- 0.04, max: 3.08552 ) GB/s
    writev 1 GiB into a preallocated file in  4 KiB chunks (x128): ( min: 4.62991, 4.78 +- 0.07, max: 4.86475 ) GB/s
    writev 1 GiB into a preallocated file in 16 KiB chunks (x128): ( min: 4.70169, 4.81 +- 0.07, max: 4.91284 ) GB/s
    writev 1 GiB into a preallocated file in 64 KiB chunks (x128): ( min: 4.80261, 4.86 +- 0.04, max: 4.90534 ) GB/s
    writev 1 GiB into a preallocated file in  1 MiB chunks (x128): ( min: 4.77039, 4.85 +- 0.06, max: 4.96816 ) GB/s
    writev 1 GiB into a preallocated file in  8 MiB chunks (x128): ( min: 4.74988, 4.86 +- 0.07, max: 4.91802 ) GB/s

    pwritev 1 GiB into a preallocated file in  1 KiB chunks (x128): ( min: 2.78118, 3.00 +- 0.09, max: 3.10199 ) GB/s
    pwritev 1 GiB into a preallocated file in  4 KiB chunks (x128): ( min: 4.3786, 4.71 +- 0.13, max: 4.82644 ) GB/s
    pwritev 1 GiB into a preallocated file in 16 KiB chunks (x128): ( min: 4.6528, 4.72 +- 0.05, max: 4.80239 ) GB/s
    pwritev 1 GiB into a preallocated file in 64 KiB chunks (x128): ( min: 4.68111, 4.81 +- 0.08, max: 4.93608 ) GB/s
    pwritev 1 GiB into a preallocated file in  1 MiB chunks (x128): ( min: 4.70144, 4.80 +- 0.05, max: 4.85725 ) GB/s
    pwritev 1 GiB into a preallocated file in  8 MiB chunks (x128): ( min: 4.70745, 4.80 +- 0.05, max: 4.87174 ) GB/s

### Parallel Writing

    Use pwrite to write 1 GiB into a preallocated file using  1 threads: ( min: 4.84367, 5.06 +- 0.09, max: 5.13053 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  2 threads: ( min: 4.45075, 4.93 +- 0.18, max: 5.0597 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  4 threads: ( min: 4.7898, 4.99 +- 0.09, max: 5.09553 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  8 threads: ( min: 4.63462, 4.80 +- 0.09, max: 4.91695 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using 16 threads: ( min: 4.32757, 4.49 +- 0.10, max: 4.64215 ) GB/s

    Write 1 GiB into one file per thread using  1 threads: ( min: 5.1609, 5.40 +- 0.13, max: 5.55782 ) GB/s
    Write 1 GiB into one file per thread using  2 threads: ( min: 5.62225, 6.4 +- 0.3, max: 6.64507 ) GB/s
    Write 1 GiB into one file per thread using  4 threads: ( min: 7.58734, 8.00 +- 0.24, max: 8.33539 ) GB/s
    Write 1 GiB into one file per thread using  8 threads: ( min: 7.68697, 8.32 +- 0.27, max: 8.59974 ) GB/s
    Write 1 GiB into one file per thread using 16 threads: ( min: 7.95709, 8.19 +- 0.21, max: 8.56123 ) GB/s

### Simple Writing

    fwrite 1 GiB into a preallocated file in   1 KiB chunks: ( min: 3.08301, 3.15 +- 0.05, max: 3.23255 ) GB/s
    fwrite 1 GiB into a preallocated file in   4 KiB chunks: ( min: 3.29352, 3.40 +- 0.06, max: 3.47562 ) GB/s
    fwrite 1 GiB into a preallocated file in   8 KiB chunks: ( min: 3.30081, 3.46 +- 0.06, max: 3.51299 ) GB/s
    fwrite 1 GiB into a preallocated file in  16 KiB chunks: ( min: 3.7797, 3.85 +- 0.04, max: 3.90994 ) GB/s
    fwrite 1 GiB into a preallocated file in  64 KiB chunks: ( min: 4.12693, 4.20 +- 0.04, max: 4.24252 ) GB/s
    fwrite 1 GiB into a preallocated file in   1 MiB chunks: ( min: 4.30822, 4.46 +- 0.10, max: 4.5734 ) GB/s
    fwrite 1 GiB into a preallocated file in  16 MiB chunks: ( min: 4.40965, 4.49 +- 0.05, max: 4.55869 ) GB/s
    fwrite 1 GiB into a preallocated file in  64 MiB chunks: ( min: 4.33898, 4.44 +- 0.07, max: 4.54101 ) GB/s
    fwrite 1 GiB into a preallocated file in 512 MiB chunks: ( min: 4.26882, 4.38 +- 0.06, max: 4.46536 ) GB/s
    fwrite 1 GiB into a preallocated file in   1 GiB chunks: ( min: 4.2959, 4.44 +- 0.07, max: 4.53681 ) GB/s

    write 1 GiB into a preallocated file in   1 KiB chunks: ( min: 1.91138, 1.948 +- 0.030, max: 1.99361 ) GB/s
    write 1 GiB into a preallocated file in   4 KiB chunks: ( min: 3.78296, 3.86 +- 0.04, max: 3.90672 ) GB/s
    write 1 GiB into a preallocated file in   8 KiB chunks: ( min: 3.94493, 4.08 +- 0.07, max: 4.15168 ) GB/s
    write 1 GiB into a preallocated file in  16 KiB chunks: ( min: 4.08327, 4.21 +- 0.06, max: 4.29188 ) GB/s
    write 1 GiB into a preallocated file in  64 KiB chunks: ( min: 4.33495, 4.361 +- 0.023, max: 4.41339 ) GB/s
    write 1 GiB into a preallocated file in   1 MiB chunks: ( min: 4.3345, 4.44 +- 0.05, max: 4.50124 ) GB/s
    write 1 GiB into a preallocated file in  16 MiB chunks: ( min: 4.40057, 4.46 +- 0.05, max: 4.54828 ) GB/s
    write 1 GiB into a preallocated file in  64 MiB chunks: ( min: 4.43119, 4.49 +- 0.05, max: 4.5498 ) GB/s
    write 1 GiB into a preallocated file in 512 MiB chunks: ( min: 4.38619, 4.48 +- 0.06, max: 4.5548 ) GB/s
    write 1 GiB into a preallocated file in   1 GiB chunks: ( min: 4.38991, 4.47 +- 0.03, max: 4.50879 ) GB/s

## Observations

 1. All 5 write methods (p)write(v) and fwrite reach similar maximum bandwidths.
    Writing into an emptied file           : 3.2-3.4 GB/s (the vectorized variants tend to be slightly faster)
    Writing into a sparsely allocated file : 3.4 GB/s
    Writing into a truncated file          : 4.7 GB/s
    Pwrite is slightly (10%) faster but that might be because of the differing (multithreaded) benchmark setup
    or because it only executes a single pwrite call per thread, i.e., the chunk size is as large as the file size
    divided by the thread count.
 2. pwritev and writev require at least a chunk size of 4 KiB but from there are relatively stable.
    Note that the total data, considering the chunk count, in ALL cases is much larger than the chunk size
    used for pwrite and write.
 3. fwrite needs at least write sizes >= 64 KiB, while write needs >= 16 KiB to reach the maximum speed.
 4. Using more threads for pwrite does NOT speed things up!
    And while the mean stays somewhat stable, the minimum tends lower for more threads, probably because of stragglers.
 5. ftruncate + mmap DOES profit from more threads, reaching somehwat of a maximum at 8 threads,
    but it is still SLOWER than a simple single-core pwrite.
 6. Writing to 4 files from 4 threads doubles the write speed to ~8.7 GB/s!
    However, how can I make use of this when I want one result file?!


# Test with ext4 formatted (veracrypt encrypted) NVME drive

```bash
cmake --build . -- benchmarkIOWrite && src/benchmarks/benchmarkIOWrite /media/e/pragzip-write-test
```

## File Creation

    posix_fallocate file sized 512 MiB: ( min: 977.124, 1340 +- 200, max: 1647.09 ) GB/s
    posix_fallocate file sized 1 GiB: ( min: 1624.39, 1970 +- 180, max: 2221.55 ) GB/s
    posix_fallocate file sized 2 GiB: ( min: 1865.93, 2140 +- 190, max: 2416.43 ) GB/s
    posix_fallocate file sized 4 GiB: ( min: 1930.38, 2340 +- 250, max: 2593.5 ) GB/s

    fallocate file sized 512 MiB: ( min: 843.182, 1700 +- 300, max: 1943.63 ) GB/s
    fallocate file sized 1 GiB: ( min: 1914.45, 2150 +- 140, max: 2361.89 ) GB/s
    fallocate file sized 2 GiB: ( min: 2100.74, 2340 +- 120, max: 2575.38 ) GB/s
    fallocate file sized 4 GiB: ( min: 2066.64, 2330 +- 120, max: 2493.52 ) GB/s

    ftruncate file sized 512 MiB: ( min: 18273.3, 24000 +- 2700, max: 27962 ) GB/s
    ftruncate file sized 1 GiB: ( min: 29959.3, 46000 +- 7000, max: 54587.8 ) GB/s
    ftruncate file sized 2 GiB: ( min: 83624.8, 104000 +- 10000, max: 112729 ) GB/s
    ftruncate file sized 4 GiB: ( min: 174167, 197000 +- 16000, max: 218129 ) GB/s

## Mmap Write

    ftruncate + mmap write 1 GiB: ( min: 1.85634, 1.899 +- 0.024, max: 1.93904 ) GB/s
    ftruncate + mmap write 1 GiB using  1 threads and maps: ( min: 1.77462, 1.84 +- 0.03, max: 1.88777 ) GB/s
    ftruncate + mmap write 1 GiB using  2 threads and maps: ( min: 3.33214, 3.7 +- 0.4, max: 4.34656 ) GB/s
    ftruncate + mmap write 1 GiB using  4 threads and maps: ( min: 4.62412, 4.87 +- 0.20, max: 5.36133 ) GB/s
    ftruncate + mmap write 1 GiB using  8 threads and maps: ( min: 5.51416, 5.71 +- 0.11, max: 5.8641 ) GB/s
    ftruncate + mmap write 1 GiB using 16 threads and maps: ( min: 5.42304, 5.64 +- 0.16, max: 5.8633 ) GB/s

    ftruncate + mmap write 1 GiB using  1 threads and maps and fds: ( min: 1.64845, 1.78 +- 0.08, max: 1.90084 ) GB/s
    ftruncate + mmap write 1 GiB using  2 threads and maps and fds: ( min: 3.32949, 3.6 +- 0.3, max: 4.34866 ) GB/s
    ftruncate + mmap write 1 GiB using  4 threads and maps and fds: ( min: 4.4478, 5.02 +- 0.26, max: 5.35514 ) GB/s
    ftruncate + mmap write 1 GiB using  8 threads and maps and fds: ( min: 4.57158, 5.6 +- 0.5, max: 6.13068 ) GB/s
    ftruncate + mmap write 1 GiB using 16 threads and maps and fds: ( min: 5.83478, 5.98 +- 0.13, max: 6.15088 ) GB/s

    ftruncate + mmap write 1 GiB using  1 threads: ( min: 1.86077, 1.90 +- 0.03, max: 1.95486 ) GB/s
    ftruncate + mmap write 1 GiB using  2 threads: ( min: 2.70346, 2.80 +- 0.06, max: 2.90704 ) GB/s
    ftruncate + mmap write 1 GiB using  4 threads: ( min: 3.40022, 3.62 +- 0.13, max: 3.78363 ) GB/s
    ftruncate + mmap write 1 GiB using  8 threads: ( min: 3.9524, 4.07 +- 0.07, max: 4.15057 ) GB/s
    ftruncate + mmap write 1 GiB using 16 threads: ( min: 4.1095, 4.20 +- 0.05, max: 4.26065 ) GB/s

## Write into an emptied file

### Vectorized Writing

    writev 256 MiB into an emptied file in  1 KiB chunks (x128): ( min: 1.07451, 1.22 +- 0.06, max: 1.2943 ) GB/s
    writev 256 MiB into an emptied file in  4 KiB chunks (x128): ( min: 1.4016, 1.52 +- 0.06, max: 1.57724 ) GB/s
    writev 256 MiB into an emptied file in 16 KiB chunks (x128): ( min: 1.51493, 1.554 +- 0.025, max: 1.58881 ) GB/s
    writev 256 MiB into an emptied file in 64 KiB chunks (x128): ( min: 1.47585, 1.58 +- 0.08, max: 1.71412 ) GB/s
    writev 256 MiB into an emptied file in  1 MiB chunks (x128): ( min: 1.43551, 1.57 +- 0.11, max: 1.71955 ) GB/s
    writev 256 MiB into an emptied file in  8 MiB chunks (x128): ( min: 1.46746, 1.60 +- 0.08, max: 1.67778 ) GB/s

    pwritev 256 MiB into an emptied file in  1 KiB chunks (x128): ( min: 1.13431, 1.26 +- 0.07, max: 1.35153 ) GB/s
    pwritev 256 MiB into an emptied file in  4 KiB chunks (x128): ( min: 1.49189, 1.541 +- 0.028, max: 1.57937 ) GB/s
    pwritev 256 MiB into an emptied file in 16 KiB chunks (x128): ( min: 1.09356, 1.50 +- 0.15, max: 1.61449 ) GB/s
    pwritev 256 MiB into an emptied file in 64 KiB chunks (x128): ( min: 1.50648, 1.59 +- 0.06, max: 1.70947 ) GB/s
    pwritev 256 MiB into an emptied file in  1 MiB chunks (x128): ( min: 1.45513, 1.57 +- 0.10, max: 1.70656 ) GB/s
    pwritev 256 MiB into an emptied file in  8 MiB chunks (x128): ( min: 1.26941, 1.45 +- 0.09, max: 1.52028 ) GB/s

### Parallel Writing

    Use pwrite to write 256 MiB into an emptied file using  1 threads: ( min: 1.46057, 1.53 +- 0.05, max: 1.64133 ) GB/s
    Use pwrite to write 256 MiB into an emptied file using  2 threads: ( min: 1.28303, 1.45 +- 0.11, max: 1.60699 ) GB/s
    Use pwrite to write 256 MiB into an emptied file using  4 threads: ( min: 1.29384, 1.47 +- 0.09, max: 1.56679 ) GB/s
    Use pwrite to write 256 MiB into an emptied file using  8 threads: ( min: 1.29009, 1.48 +- 0.11, max: 1.64004 ) GB/s
    Use pwrite to write 256 MiB into an emptied file using 16 threads: ( min: 1.24062, 1.38 +- 0.07, max: 1.45139 ) GB/s

### Simple Writing

    fwrite 256 MiB into an emptied file in   1 KiB chunks: ( min: 0.828859, 0.90 +- 0.04, max: 0.955089 ) GB/s
    fwrite 256 MiB into an emptied file in   4 KiB chunks: ( min: 0.879336, 0.94 +- 0.04, max: 0.996811 ) GB/s
    fwrite 256 MiB into an emptied file in   8 KiB chunks: ( min: 0.878949, 0.93 +- 0.03, max: 0.974824 ) GB/s
    fwrite 256 MiB into an emptied file in  16 KiB chunks: ( min: 1.06734, 1.16 +- 0.04, max: 1.20787 ) GB/s
    fwrite 256 MiB into an emptied file in  64 KiB chunks: ( min: 1.35715, 1.41 +- 0.03, max: 1.44964 ) GB/s
    fwrite 256 MiB into an emptied file in   1 MiB chunks: ( min: 1.41604, 1.53 +- 0.08, max: 1.63505 ) GB/s
    fwrite 256 MiB into an emptied file in  16 MiB chunks: ( min: 1.42118, 1.53 +- 0.07, max: 1.63903 ) GB/s
    fwrite 256 MiB into an emptied file in  64 MiB chunks: ( min: 1.50546, 1.59 +- 0.08, max: 1.72756 ) GB/s
    fwrite 256 MiB into an emptied file in 512 MiB chunks: ( min: 1.38175, 1.53 +- 0.10, max: 1.66113 ) GB/s
    fwrite 256 MiB into an emptied file in   1 GiB chunks: ( min: 1.53476, 1.62 +- 0.07, max: 1.73483 ) GB/s

    write 256 MiB into an emptied file in   1 KiB chunks: ( min: 0.388361, 0.404 +- 0.009, max: 0.416311 ) GB/s
    write 256 MiB into an emptied file in   4 KiB chunks: ( min: 0.954036, 1.008 +- 0.025, max: 1.04164 ) GB/s
    write 256 MiB into an emptied file in   8 KiB chunks: ( min: 0.994721, 1.14 +- 0.08, max: 1.3012 ) GB/s
    write 256 MiB into an emptied file in  16 KiB chunks: ( min: 1.08694, 1.22 +- 0.07, max: 1.30796 ) GB/s
    write 256 MiB into an emptied file in  64 KiB chunks: ( min: 1.22618, 1.39 +- 0.09, max: 1.48892 ) GB/s
    write 256 MiB into an emptied file in   1 MiB chunks: ( min: 1.36946, 1.49 +- 0.07, max: 1.56345 ) GB/s
    write 256 MiB into an emptied file in  16 MiB chunks: ( min: 1.45024, 1.492 +- 0.026, max: 1.53012 ) GB/s
    write 256 MiB into an emptied file in  64 MiB chunks: ( min: 1.35178, 1.46 +- 0.04, max: 1.49596 ) GB/s
    write 256 MiB into an emptied file in 512 MiB chunks: ( min: 1.33108, 1.43 +- 0.06, max: 1.4886 ) GB/s
    write 256 MiB into an emptied file in   1 GiB chunks: ( min: 1.34253, 1.47 +- 0.07, max: 1.55927 ) GB/s

## Write into a preallocated file

### Vectorized Writing

    writev 256 MiB into a preallocated file in  1 KiB chunks (x128): ( min: 2.67706, 2.77 +- 0.05, max: 2.81494 ) GB/s
    writev 256 MiB into a preallocated file in  4 KiB chunks (x128): ( min: 4.16628, 4.30 +- 0.06, max: 4.35493 ) GB/s
    writev 256 MiB into a preallocated file in 16 KiB chunks (x128): ( min: 3.41461, 4.2 +- 0.3, max: 4.38802 ) GB/s
    writev 256 MiB into a preallocated file in 64 KiB chunks (x128): ( min: 4.10231, 4.35 +- 0.09, max: 4.4091 ) GB/s
    writev 256 MiB into a preallocated file in  1 MiB chunks (x128): ( min: 4.21597, 4.39 +- 0.08, max: 4.45905 ) GB/s
    writev 256 MiB into a preallocated file in  8 MiB chunks (x128): ( min: 4.23267, 4.39 +- 0.09, max: 4.48143 ) GB/s

    pwritev 256 MiB into a preallocated file in  1 KiB chunks (x128): ( min: 2.57955, 2.76 +- 0.07, max: 2.80544 ) GB/s
    pwritev 256 MiB into a preallocated file in  4 KiB chunks (x128): ( min: 4.12264, 4.30 +- 0.10, max: 4.3836 ) GB/s
    pwritev 256 MiB into a preallocated file in 16 KiB chunks (x128): ( min: 4.38419, 4.414 +- 0.016, max: 4.44388 ) GB/s
    pwritev 256 MiB into a preallocated file in 64 KiB chunks (x128): ( min: 0.0829603, 3.9 +- 1.3, max: 4.40409 ) GB/s
    pwritev 256 MiB into a preallocated file in  1 MiB chunks (x128): ( min: 4.19947, 4.36 +- 0.08, max: 4.43472 ) GB/s
    pwritev 256 MiB into a preallocated file in  8 MiB chunks (x128): ( min: 4.36719, 4.408 +- 0.024, max: 4.4433 ) GB/s

### Parallel Writing

    Use pwrite to write 256 MiB into a preallocated file using  1 threads: ( min: 4.35057, 4.52 +- 0.08, max: 4.62083 ) GB/s
    Use pwrite to write 256 MiB into a preallocated file using  2 threads: ( min: 4.3863, 4.50 +- 0.06, max: 4.59059 ) GB/s
    Use pwrite to write 256 MiB into a preallocated file using  4 threads: ( min: 4.16243, 4.37 +- 0.13, max: 4.57046 ) GB/s
    Use pwrite to write 256 MiB into a preallocated file using  8 threads: ( min: 4.1548, 4.37 +- 0.13, max: 4.55125 ) GB/s
    Use pwrite to write 256 MiB into a preallocated file using 16 threads: ( min: 4.02281, 4.16 +- 0.11, max: 4.3615 ) GB/s

### Simple Writing

    fwrite 256 MiB into a preallocated file in   1 KiB chunks: ( min: 1.57586, 1.595 +- 0.011, max: 1.61369 ) GB/s
    fwrite 256 MiB into a preallocated file in   4 KiB chunks: ( min: 1.65299, 1.705 +- 0.027, max: 1.72818 ) GB/s
    fwrite 256 MiB into a preallocated file in   8 KiB chunks: ( min: 1.53443, 1.68 +- 0.06, max: 1.72699 ) GB/s
    fwrite 256 MiB into a preallocated file in  16 KiB chunks: ( min: 2.27822, 2.38 +- 0.05, max: 2.41384 ) GB/s
    fwrite 256 MiB into a preallocated file in  64 KiB chunks: ( min: 3.62871, 3.639 +- 0.008, max: 3.65172 ) GB/s
    fwrite 256 MiB into a preallocated file in   1 MiB chunks: ( min: 4.13951, 4.27 +- 0.07, max: 4.33378 ) GB/s
    fwrite 256 MiB into a preallocated file in  16 MiB chunks: ( min: 4.17276, 4.31 +- 0.08, max: 4.45234 ) GB/s
    fwrite 256 MiB into a preallocated file in  64 MiB chunks: ( min: 4.39733, 4.425 +- 0.013, max: 4.45199 ) GB/s
    fwrite 256 MiB into a preallocated file in 512 MiB chunks: ( min: 4.32063, 4.41 +- 0.06, max: 4.48257 ) GB/s
    fwrite 256 MiB into a preallocated file in   1 GiB chunks: ( min: 4.30517, 4.40 +- 0.04, max: 4.45547 ) GB/s

    write 256 MiB into a preallocated file in   1 KiB chunks: ( min: 0.584793, 0.607 +- 0.014, max: 0.633744 ) GB/s
    write 256 MiB into a preallocated file in   4 KiB chunks: ( min: 1.64147, 1.78 +- 0.05, max: 1.82773 ) GB/s
    write 256 MiB into a preallocated file in   8 KiB chunks: ( min: 2.38419, 2.49 +- 0.06, max: 2.543 ) GB/s
    write 256 MiB into a preallocated file in  16 KiB chunks: ( min: 3.11368, 3.18 +- 0.04, max: 3.22404 ) GB/s
    write 256 MiB into a preallocated file in  64 KiB chunks: ( min: 3.78954, 4.01 +- 0.12, max: 4.10093 ) GB/s
    write 256 MiB into a preallocated file in   1 MiB chunks: ( min: 4.09723, 4.36 +- 0.10, max: 4.45412 ) GB/s
    write 256 MiB into a preallocated file in  16 MiB chunks: ( min: 4.233, 4.36 +- 0.07, max: 4.43779 ) GB/s
    write 256 MiB into a preallocated file in  64 MiB chunks: ( min: 4.16476, 4.37 +- 0.08, max: 4.44346 ) GB/s
    write 256 MiB into a preallocated file in 512 MiB chunks: ( min: 3.84873, 4.29 +- 0.19, max: 4.41402 ) GB/s
    write 256 MiB into a preallocated file in   1 GiB chunks: ( min: 4.23838, 4.35 +- 0.05, max: 4.40482 ) GB/s

## Observations

 1. Similarly to /dev/shm, all 5 write methods (p)write(v) and fwrite reach similar maximum bandwidths.
    Writing into an emptied file           : 2.5 GB/s
    Writing into a sparsely allocated file : 4.4 GB/s
    Writing into a truncated file          : 4.4 GB/s


# Repeat Test on ext4-formatted NVME on different day

## File Creation

    posix_fallocate file sized 512 MiB: ( min: 1221.24, 2000 +- 300, max: 2382.27 ) GB/s
    posix_fallocate file sized 1 GiB: ( min: 1919.62, 2190 +- 210, max: 2589.26 ) GB/s
    posix_fallocate file sized 2 GiB: ( min: 1714.35, 2100 +- 220, max: 2357.43 ) GB/s
    posix_fallocate file sized 4 GiB: ( min: 1987.82, 2250 +- 130, max: 2445.93 ) GB/s

    fallocate file sized 512 MiB: ( min: 1498.59, 1720 +- 160, max: 1899.76 ) GB/s
    fallocate file sized 1 GiB: ( min: 1146.9, 2000 +- 300, max: 2248.29 ) GB/s
    fallocate file sized 2 GiB: ( min: 1010.25, 2000 +- 400, max: 2247.61 ) GB/s
    fallocate file sized 4 GiB: ( min: 1868.82, 2080 +- 90, max: 2175.66 ) GB/s

    ftruncate file sized 512 MiB: ( min: 16503.9, 28000 +- 8000, max: 36275.1 ) GB/s
    ftruncate file sized 1 GiB: ( min: 59224.6, 65000 +- 5000, max: 72014.9 ) GB/s
    ftruncate file sized 2 GiB: ( min: 101488, 123000 +- 20000, max: 158720 ) GB/s
    ftruncate file sized 4 GiB: ( min: 124060, 168000 +- 24000, max: 193720 ) GB/s

## Mmap Write

    ftruncate + mmap write 1 GiB: ( min: 1.93064, 1.967 +- 0.021, max: 1.99308 ) GB/s
    ftruncate + mmap write 1 GiB using  1 threads: ( min: 1.90799, 1.94 +- 0.03, max: 1.98611 ) GB/s
    ftruncate + mmap write 1 GiB using  2 threads: ( min: 2.70552, 2.80 +- 0.06, max: 2.86871 ) GB/s
    ftruncate + mmap write 1 GiB using  4 threads: ( min: 3.56554, 3.68 +- 0.09, max: 3.80599 ) GB/s
    ftruncate + mmap write 1 GiB using  8 threads: ( min: 4.07374, 4.16 +- 0.06, max: 4.26736 ) GB/s
    ftruncate + mmap write 1 GiB using 16 threads: ( min: 4.0208, 4.21 +- 0.09, max: 4.28702 ) GB/s

## Write into an emptied file

### Vectorized Writing

    writev 1 GiB into an emptied file in  1 KiB chunks (x128): ( min: 1.57072, 1.604 +- 0.026, max: 1.65762 ) GB/s
    writev 1 GiB into an emptied file in  4 KiB chunks (x128): ( min: 2.05412, 2.10 +- 0.03, max: 2.15785 ) GB/s
    writev 1 GiB into an emptied file in 16 KiB chunks (x128): ( min: 2.07417, 2.13 +- 0.04, max: 2.1926 ) GB/s
    writev 1 GiB into an emptied file in 64 KiB chunks (x128): ( min: 2.07555, 2.115 +- 0.024, max: 2.17209 ) GB/s
    writev 1 GiB into an emptied file in  1 MiB chunks (x128): ( min: 2.08699, 2.126 +- 0.027, max: 2.16954 ) GB/s
    writev 1 GiB into an emptied file in  8 MiB chunks (x128): ( min: 1.76009, 2.06 +- 0.11, max: 2.1465 ) GB/s

    pwritev 1 GiB into an emptied file in  1 KiB chunks (x128): ( min: 1.57519, 1.590 +- 0.012, max: 1.61231 ) GB/s
    pwritev 1 GiB into an emptied file in  4 KiB chunks (x128): ( min: 1.97682, 2.07 +- 0.04, max: 2.12264 ) GB/s
    pwritev 1 GiB into an emptied file in 16 KiB chunks (x128): ( min: 2.07165, 2.088 +- 0.019, max: 2.13678 ) GB/s
    pwritev 1 GiB into an emptied file in 64 KiB chunks (x128): ( min: 2.06901, 2.099 +- 0.030, max: 2.14604 ) GB/s
    pwritev 1 GiB into an emptied file in  1 MiB chunks (x128): ( min: 1.95569, 2.05 +- 0.07, max: 2.15613 ) GB/s
    pwritev 1 GiB into an emptied file in  8 MiB chunks (x128): ( min: 2.086, 2.12 +- 0.03, max: 2.1929 ) GB/s

### Parallel Writing

    Use pwrite to write 1 GiB into an emptied file using  1 threads: ( min: 2.22622, 2.29 +- 0.04, max: 2.34972 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  2 threads: ( min: 2.12756, 2.20 +- 0.06, max: 2.29274 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  4 threads: ( min: 2.19959, 2.31 +- 0.08, max: 2.44431 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  8 threads: ( min: 2.03493, 2.30 +- 0.20, max: 2.70725 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using 16 threads: ( min: 1.51394, 2.08 +- 0.29, max: 2.4002 ) GB/s

    Write 1 GiB into one file per thread using  1 threads: ( min: 2.31113, 2.38 +- 0.03, max: 2.42936 ) GB/s
    Write 1 GiB into one file per thread using  2 threads: ( min: 3.52143, 3.64 +- 0.14, max: 3.96902 ) GB/s
    Write 1 GiB into one file per thread using  4 threads: ( min: 5.43039, 5.96 +- 0.25, max: 6.2095 ) GB/s
    Write 1 GiB into one file per thread using  8 threads: ( min: 4.0735, 7.1 +- 1.1, max: 7.73654 ) GB/s
    Write 1 GiB into one file per thread using 16 threads: ( min: 7.25262, 7.71 +- 0.25, max: 7.9585 ) GB/s

### Simple Writing

    fwrite 1 GiB into an emptied file in   1 KiB chunks: ( min: 0.988926, 1.05 +- 0.03, max: 1.09359 ) GB/s
    fwrite 1 GiB into an emptied file in   4 KiB chunks: ( min: 1.01448, 1.08 +- 0.04, max: 1.12775 ) GB/s
    fwrite 1 GiB into an emptied file in   8 KiB chunks: ( min: 1.02823, 1.10 +- 0.03, max: 1.12973 ) GB/s
    fwrite 1 GiB into an emptied file in  16 KiB chunks: ( min: 1.3339, 1.394 +- 0.029, max: 1.45197 ) GB/s
    fwrite 1 GiB into an emptied file in  64 KiB chunks: ( min: 1.58209, 1.74 +- 0.11, max: 1.91934 ) GB/s
    fwrite 1 GiB into an emptied file in   1 MiB chunks: ( min: 1.95393, 2.05 +- 0.04, max: 2.10197 ) GB/s
    fwrite 1 GiB into an emptied file in  16 MiB chunks: ( min: 1.80125, 2.02 +- 0.10, max: 2.12123 ) GB/s
    fwrite 1 GiB into an emptied file in  64 MiB chunks: ( min: 1.99933, 2.06 +- 0.04, max: 2.12895 ) GB/s
    fwrite 1 GiB into an emptied file in 512 MiB chunks: ( min: 1.8685, 1.98 +- 0.07, max: 2.07511 ) GB/s
    fwrite 1 GiB into an emptied file in   1 GiB chunks: ( min: 1.90751, 1.99 +- 0.05, max: 2.07037 ) GB/s

    write 1 GiB into an emptied file in   1 KiB chunks: ( min: 0.404608, 0.415 +- 0.008, max: 0.429767 ) GB/s
    write 1 GiB into an emptied file in   4 KiB chunks: ( min: 1.1173, 1.127 +- 0.008, max: 1.14182 ) GB/s
    write 1 GiB into an emptied file in   8 KiB chunks: ( min: 1.28066, 1.37 +- 0.05, max: 1.44188 ) GB/s
    write 1 GiB into an emptied file in  16 KiB chunks: ( min: 1.44517, 1.56 +- 0.08, max: 1.66704 ) GB/s
    write 1 GiB into an emptied file in  64 KiB chunks: ( min: 1.75012, 1.86 +- 0.06, max: 1.92874 ) GB/s
    write 1 GiB into an emptied file in   1 MiB chunks: ( min: 1.84151, 1.94 +- 0.05, max: 1.99937 ) GB/s
    write 1 GiB into an emptied file in  16 MiB chunks: ( min: 1.91911, 2.00 +- 0.05, max: 2.06275 ) GB/s
    write 1 GiB into an emptied file in  64 MiB chunks: ( min: 1.97101, 2.017 +- 0.025, max: 2.05353 ) GB/s
    write 1 GiB into an emptied file in 512 MiB chunks: ( min: 1.99803, 2.05 +- 0.04, max: 2.11593 ) GB/s
    write 1 GiB into an emptied file in   1 GiB chunks: ( min: 1.9008, 2.00 +- 0.05, max: 2.08222 ) GB/s

## Write into a sparsely allocated file

### Vectorized Writing

    writev 1 GiB into a sparsely allocated file in  1 KiB chunks (x128): ( min: 1.79836, 1.85 +- 0.04, max: 1.9373 ) GB/s
    writev 1 GiB into a sparsely allocated file in  4 KiB chunks (x128): ( min: 2.43984, 2.56 +- 0.06, max: 2.63251 ) GB/s
    writev 1 GiB into a sparsely allocated file in 16 KiB chunks (x128): ( min: 2.34638, 2.50 +- 0.07, max: 2.57537 ) GB/s
    writev 1 GiB into a sparsely allocated file in 64 KiB chunks (x128): ( min: 2.50768, 2.57 +- 0.05, max: 2.67745 ) GB/s
    writev 1 GiB into a sparsely allocated file in  1 MiB chunks (x128): ( min: 2.42516, 2.53 +- 0.06, max: 2.63443 ) GB/s
    writev 1 GiB into a sparsely allocated file in  8 MiB chunks (x128): ( min: 2.36061, 2.49 +- 0.06, max: 2.55686 ) GB/s

    pwritev 1 GiB into a sparsely allocated file in  1 KiB chunks (x128): ( min: 1.76786, 1.817 +- 0.024, max: 1.85585 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in  4 KiB chunks (x128): ( min: 2.37722, 2.51 +- 0.07, max: 2.58644 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in 16 KiB chunks (x128): ( min: 2.42008, 2.52 +- 0.05, max: 2.61795 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in 64 KiB chunks (x128): ( min: 2.17827, 2.36 +- 0.12, max: 2.5602 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in  1 MiB chunks (x128): ( min: 2.41166, 2.53 +- 0.07, max: 2.6132 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in  8 MiB chunks (x128): ( min: 2.41499, 2.56 +- 0.08, max: 2.68519 ) GB/s

### Parallel Writing

    Use pwrite to write 1 GiB into a sparsely allocated file using  1 threads: ( min: 2.66272, 2.76 +- 0.06, max: 2.84479 ) GB/s
    Use pwrite to write 1 GiB into a sparsely allocated file using  2 threads: ( min: 2.5659, 2.72 +- 0.09, max: 2.87545 ) GB/s
    Use pwrite to write 1 GiB into a sparsely allocated file using  4 threads: ( min: 2.56812, 2.75 +- 0.09, max: 2.85749 ) GB/s
    Use pwrite to write 1 GiB into a sparsely allocated file using  8 threads: ( min: 2.14858, 2.58 +- 0.18, max: 2.78234 ) GB/s
    Use pwrite to write 1 GiB into a sparsely allocated file using 16 threads: ( min: 2.09691, 2.39 +- 0.17, max: 2.6279 ) GB/s

    Write 1 GiB into one file per thread using  1 threads: ( min: 3.07973, 3.17 +- 0.06, max: 3.26674 ) GB/s
    Write 1 GiB into one file per thread using  2 threads: ( min: 2.36916, 3.8 +- 0.9, max: 4.49209 ) GB/s
    Write 1 GiB into one file per thread using  4 threads: ( min: 3.19072, 5.5 +- 1.2, max: 6.37856 ) GB/s
    Write 1 GiB into one file per thread using  8 threads: ( min: 7.00052, 7.61 +- 0.24, max: 7.87061 ) GB/s
    Write 1 GiB into one file per thread using 16 threads: ( min: 3.47816, 6.6 +- 2.0, max: 8.03352 ) GB/s

### Simple Writing

    fwrite 1 GiB into a sparsely allocated file in   1 KiB chunks: ( min: 1.17369, 1.201 +- 0.018, max: 1.22841 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in   4 KiB chunks: ( min: 1.20799, 1.267 +- 0.024, max: 1.29363 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in   8 KiB chunks: ( min: 1.26762, 1.292 +- 0.013, max: 1.30429 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in  16 KiB chunks: ( min: 1.64825, 1.690 +- 0.026, max: 1.73862 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in  64 KiB chunks: ( min: 2.24396, 2.32 +- 0.04, max: 2.36079 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in   1 MiB chunks: ( min: 2.49248, 2.62 +- 0.05, max: 2.67662 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in  16 MiB chunks: ( min: 2.53908, 2.59 +- 0.05, max: 2.68438 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in  64 MiB chunks: ( min: 2.43794, 2.59 +- 0.08, max: 2.68373 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in 512 MiB chunks: ( min: 2.50203, 2.58 +- 0.05, max: 2.65129 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in   1 GiB chunks: ( min: 2.54357, 2.60 +- 0.03, max: 2.65128 ) GB/s

    write 1 GiB into a sparsely allocated file in   1 KiB chunks: ( min: 0.510725, 0.527 +- 0.009, max: 0.537128 ) GB/s
    write 1 GiB into a sparsely allocated file in   4 KiB chunks: ( min: 1.31094, 1.336 +- 0.016, max: 1.35949 ) GB/s
    write 1 GiB into a sparsely allocated file in   8 KiB chunks: ( min: 1.69936, 1.741 +- 0.025, max: 1.78256 ) GB/s
    write 1 GiB into a sparsely allocated file in  16 KiB chunks: ( min: 1.92471, 2.08 +- 0.06, max: 2.13579 ) GB/s
    write 1 GiB into a sparsely allocated file in  64 KiB chunks: ( min: 2.43554, 2.51 +- 0.04, max: 2.571 ) GB/s
    write 1 GiB into a sparsely allocated file in   1 MiB chunks: ( min: 2.5017, 2.62 +- 0.06, max: 2.67647 ) GB/s
    write 1 GiB into a sparsely allocated file in  16 MiB chunks: ( min: 2.49555, 2.63 +- 0.08, max: 2.76115 ) GB/s
    write 1 GiB into a sparsely allocated file in  64 MiB chunks: ( min: 2.52701, 2.62 +- 0.06, max: 2.71749 ) GB/s
    write 1 GiB into a sparsely allocated file in 512 MiB chunks: ( min: 2.3879, 2.59 +- 0.09, max: 2.6628 ) GB/s
    write 1 GiB into a sparsely allocated file in   1 GiB chunks: ( min: 2.25958, 2.60 +- 0.13, max: 2.71713 ) GB/s

## Write into a preallocated file

### Vectorized Writing

    writev 1 GiB into a preallocated file in  1 KiB chunks (x128): ( min: 1.89351, 1.95 +- 0.04, max: 2.01283 ) GB/s
    writev 1 GiB into a preallocated file in  4 KiB chunks (x128): ( min: 2.42279, 2.69 +- 0.11, max: 2.8089 ) GB/s
    writev 1 GiB into a preallocated file in 16 KiB chunks (x128): ( min: 2.7177, 2.80 +- 0.05, max: 2.8669 ) GB/s
    writev 1 GiB into a preallocated file in 64 KiB chunks (x128): ( min: 2.52513, 2.68 +- 0.11, max: 2.82374 ) GB/s
    writev 1 GiB into a preallocated file in  1 MiB chunks (x128): ( min: 2.68268, 2.81 +- 0.07, max: 2.88682 ) GB/s
    writev 1 GiB into a preallocated file in  8 MiB chunks (x128): ( min: 2.60381, 2.79 +- 0.09, max: 2.92279 ) GB/s

    pwritev 1 GiB into a preallocated file in  1 KiB chunks (x128): ( min: 1.89167, 1.936 +- 0.025, max: 1.97695 ) GB/s
    pwritev 1 GiB into a preallocated file in  4 KiB chunks (x128): ( min: 2.19067, 2.60 +- 0.19, max: 2.85783 ) GB/s
    pwritev 1 GiB into a preallocated file in 16 KiB chunks (x128): ( min: 2.37349, 2.65 +- 0.14, max: 2.86841 ) GB/s
    pwritev 1 GiB into a preallocated file in 64 KiB chunks (x128): ( min: 2.5474, 2.71 +- 0.08, max: 2.8225 ) GB/s
    pwritev 1 GiB into a preallocated file in  1 MiB chunks (x128): ( min: 2.44974, 2.65 +- 0.13, max: 2.83242 ) GB/s
    pwritev 1 GiB into a preallocated file in  8 MiB chunks (x128): ( min: 2.69649, 2.77 +- 0.05, max: 2.8391 ) GB/s

### Parallel Writing

    Use pwrite to write 1 GiB into a preallocated file using  1 threads: ( min: 2.87486, 3.02 +- 0.09, max: 3.17131 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  2 threads: ( min: 2.82708, 2.98 +- 0.09, max: 3.12767 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  4 threads: ( min: 2.53856, 2.85 +- 0.19, max: 3.08872 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  8 threads: ( min: 2.53763, 2.88 +- 0.19, max: 3.11305 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using 16 threads: ( min: 2.19272, 2.61 +- 0.23, max: 3.02451 ) GB/s

    Write 1 GiB into one file per thread using  1 threads: ( min: 3.2524, 3.40 +- 0.09, max: 3.50795 ) GB/s
    Write 1 GiB into one file per thread using  2 threads: ( min: 2.47751, 4.1 +- 0.6, max: 4.40293 ) GB/s
    Write 1 GiB into one file per thread using  4 threads: ( min: 3.37848, 5.9 +- 0.9, max: 6.42595 ) GB/s
    Write 1 GiB into one file per thread using  8 threads: ( min: 3.57102, 6.2 +- 2.1, max: 8.01132 ) GB/s
    Write 1 GiB into one file per thread using 16 threads: ( min: 4.65272, 7.2 +- 1.5, max: 8.14042 ) GB/s

### Simple Writing

    fwrite 1 GiB into a preallocated file in   1 KiB chunks: ( min: 1.2107, 1.258 +- 0.029, max: 1.29675 ) GB/s
    fwrite 1 GiB into a preallocated file in   4 KiB chunks: ( min: 1.23118, 1.292 +- 0.029, max: 1.31998 ) GB/s
    fwrite 1 GiB into a preallocated file in   8 KiB chunks: ( min: 1.29509, 1.331 +- 0.019, max: 1.3538 ) GB/s
    fwrite 1 GiB into a preallocated file in  16 KiB chunks: ( min: 1.71083, 1.77 +- 0.04, max: 1.81762 ) GB/s
    fwrite 1 GiB into a preallocated file in  64 KiB chunks: ( min: 2.32603, 2.40 +- 0.04, max: 2.43892 ) GB/s
    fwrite 1 GiB into a preallocated file in   1 MiB chunks: ( min: 2.61129, 2.74 +- 0.07, max: 2.81556 ) GB/s
    fwrite 1 GiB into a preallocated file in  16 MiB chunks: ( min: 2.61609, 2.73 +- 0.07, max: 2.84358 ) GB/s
    fwrite 1 GiB into a preallocated file in  64 MiB chunks: ( min: 2.56222, 2.71 +- 0.11, max: 2.86376 ) GB/s
    fwrite 1 GiB into a preallocated file in 512 MiB chunks: ( min: 2.64408, 2.76 +- 0.08, max: 2.89074 ) GB/s
    fwrite 1 GiB into a preallocated file in   1 GiB chunks: ( min: 2.71599, 2.78 +- 0.05, max: 2.89785 ) GB/s

    write 1 GiB into a preallocated file in   1 KiB chunks: ( min: 0.523481, 0.533 +- 0.006, max: 0.542113 ) GB/s
    write 1 GiB into a preallocated file in   4 KiB chunks: ( min: 1.30774, 1.350 +- 0.029, max: 1.40093 ) GB/s
    write 1 GiB into a preallocated file in   8 KiB chunks: ( min: 1.70201, 1.79 +- 0.04, max: 1.8362 ) GB/s
    write 1 GiB into a preallocated file in  16 KiB chunks: ( min: 2.10776, 2.15 +- 0.03, max: 2.21645 ) GB/s
    write 1 GiB into a preallocated file in  64 KiB chunks: ( min: 2.56745, 2.66 +- 0.04, max: 2.70374 ) GB/s
    write 1 GiB into a preallocated file in   1 MiB chunks: ( min: 2.74514, 2.84 +- 0.05, max: 2.91267 ) GB/s
    write 1 GiB into a preallocated file in  16 MiB chunks: ( min: 2.66165, 2.80 +- 0.06, max: 2.86093 ) GB/s
    write 1 GiB into a preallocated file in  64 MiB chunks: ( min: 2.71294, 2.82 +- 0.07, max: 2.9116 ) GB/s
    write 1 GiB into a preallocated file in 512 MiB chunks: ( min: 2.69801, 2.82 +- 0.05, max: 2.88598 ) GB/s
    write 1 GiB into a preallocated file in   1 GiB chunks: ( min: 2.72747, 2.85 +- 0.05, max: 2.91572 ) GB/s

## Observations

 1. Similarly to /dev/shm, all 5 write methods (p)write(v) and fwrite reach similar maximum bandwidths.
    Writing into an emptied file           : 2.0 GB/s
    Writing into a sparsely allocated file : 2.6 GB/s
    Writing into a truncated file          : 2.8 GB/s
 2. Similarly to /dev/shm, pwritev and writev require at least a chunk size of 4 KiB but from there are relatively
    stable. Note that the total data, considering the chunk count, in ALL cases is much larger than the chunk size
    used for pwrite and write.
 3. In contrast to /dev/shm, write and fwrite require at least write sizes >= 1 MiB KiB (instead of 64 KiB)
    to reach the maximum speed.
 4. Similarly to /dev/shm, using more threads for pwrite does NOT speed things up!
    And while the mean stays somewhat stable, the minimum tends lower for more threads, probably because of stragglers.
 5. Similarly to /dev/shm, ftruncate + mmap DOES profit from more threads, reaching somehwat of a maximum at 8 threads,
    but it is still SLOWER than a simple single-core ftruncate + write. Note that mmap already is preceded by ftruncate.
 6. Similarly to /dev/shm, Writing to 8 files from 8 threads more than doubles the write speed to ~7.7 GB/s!
 7. Similary to /dev/shm, fwrite and write are equally fast except for 1 KiB chunks, for which write is SLOWER
    than fwrite maybe because fwrite is buffered?
 8. O_DIRECT slows preallocated pwrite done a lot on my SSD.
 9. Similary to /dev/shm, fwrite and write are equally fast except for 1 KiB chunks, for which write is SLOWER
    than fwrite maybe because fwrite is buffered?
10. Allocation is 100x faster than /dev/shm !? But only sometimes. Mabye ext4 has a buffer of reusable sectors?


# Test on HomePC with BTRFS striped into two files residing on /dev/shm

```bash
fallocate --length 1GiB /dev/shm/foo1
fallocate --length 1GiB /dev/shm/foo2
mkfs.btrfs -L test -m raid1 -d raid0 /dev/shm/foo{1,2}

    btrfs-progs v5.16.2
    See http://btrfs.wiki.kernel.org for more information.

    NOTE: several default settings have changed in version 5.15, please make sure
          this does not affect your deployments:
          - DUP for metadata (-m dup)
          - enabled no-holes (-O no-holes)
          - enabled free-space-tree (-R free-space-tree)

    Label:              test
    UUID:               6eecae99-33cd-4557-809b-0d8e6d432694
    Node size:          16384
    Sector size:        4096
    Filesystem size:    2.00GiB
    Block group profiles:
      Data:             single            8.00MiB
      Metadata:         RAID1           102.38MiB
      System:           RAID1             8.00MiB
    SSD detected:       no
    Zoned device:       no
    Incompat features:  extref, skinny-metadata, no-holes
    Runtime features:   free-space-tree
    Checksum:           crc32c
    Number of devices:  2
    Devices:
       ID        SIZE  PATH
        1     1.00GiB  /dev/shm/foo1
        2     1.00GiB  /dev/shm/foo2

sudo losetup /dev/loop1 /dev/shm/foo1
sudo losetup /dev/loop2 /dev/shm/foo2
mkdir /media/btrfs
sudo mount /dev/loop1 /media/btrfs

df -h /media/btrfs

    Filesystem      Size  Used Avail Use% Mounted on
    /dev/loop1      2.0G  3.6M  1.8G   1% /media/btrfs

cmake --build . -- benchmarkIOWrite && src/benchmarks/benchmarkIOWrite /media/btrfs/pragzip-write-test

sudo umount /media/btrfs
sudo losetup --detach /dev/loop1
sudo losetup --detach /dev/loop2
'rm' /dev/shm/foo{1,2}
```

## File Creation

    posix_fallocate file sized 512 MiB: ( min: 1194.58, 6000 +- 5000, max: 13664.3 ) GB/s
    posix_fallocate file sized 1 GiB: ( min: 3377.81, 5200 +- 2600, max: 12443.4 ) GB/s
    posix_fallocate file sized 2 GiB: ( min: 11435, 12900 +- 800, max: 13850.3 ) GB/s
    posix_fallocate file sized 4 GiB: ( min: 24110.1, 26900 +- 1400, max: 28314.1 ) GB/s

    fallocate file sized 512 MiB: ( min: 1377.19, 6000 +- 3000, max: 10148.8 ) GB/s
    fallocate file sized 1 GiB: ( min: 4181.87, 5200 +- 2600, max: 12693.3 ) GB/s
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    fallocate file sized 2 GiB: ( min: 9965.58, 11600 +- 900, max: 12949.1 ) GB/s
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    fallocate file sized 4 GiB: ( min: 20937.8, 23500 +- 1400, max: 25177.1 ) GB/s

    ftruncate file sized 512 MiB: ( min: 17016.5, 27000 +- 5000, max: 30766.2 ) GB/s
    ftruncate file sized 1 GiB: ( min: 38623.8, 58000 +- 7000, max: 67234.9 ) GB/s
    ftruncate file sized 2 GiB: ( min: 69791.5, 112000 +- 20000, max: 128901 ) GB/s
    ftruncate file sized 4 GiB: ( min: 209296, 236000 +- 18000, max: 259829 ) GB/s

## Mmap Write

    ftruncate + mmap write 1 GiB: ( min: 1.47878, 1.61 +- 0.05, max: 1.65373 ) GB/s
    ftruncate + mmap write 1 GiB using  1 threads: ( min: 1.68728, 1.703 +- 0.012, max: 1.72495 ) GB/s
    ftruncate + mmap write 1 GiB using  2 threads: ( min: 2.10933, 2.48 +- 0.15, max: 2.65696 ) GB/s
    ftruncate + mmap write 1 GiB using  4 threads: ( min: 2.29741, 2.64 +- 0.22, max: 2.91162 ) GB/s
    ftruncate + mmap write 1 GiB using  8 threads: ( min: 2.38579, 2.75 +- 0.16, max: 2.94162 ) GB/s
    ftruncate + mmap write 1 GiB using 16 threads: ( min: 2.38526, 2.44 +- 0.04, max: 2.49241 ) GB/s

## Write into an emptied file

### Vectorized Writing

    writev 1 GiB into an emptied file in  1 KiB chunks (x128): ( min: 2.19923, 2.26 +- 0.04, max: 2.30389 ) GB/s
    writev 1 GiB into an emptied file in  4 KiB chunks (x128): ( min: 1.50032, 3.0 +- 0.5, max: 3.30542 ) GB/s
    writev 1 GiB into an emptied file in 16 KiB chunks (x128): ( min: 3.2025, 3.29 +- 0.07, max: 3.37774 ) GB/s
    writev 1 GiB into an emptied file in 64 KiB chunks (x128): ( min: 3.23658, 3.31 +- 0.04, max: 3.38589 ) GB/s
    writev 1 GiB into an emptied file in  1 MiB chunks (x128): ( min: 3.23819, 3.31 +- 0.05, max: 3.38312 ) GB/s
    writev 1 GiB into an emptied file in  8 MiB chunks (x128): ( min: 3.29882, 3.38 +- 0.05, max: 3.43668 ) GB/s

    pwritev 1 GiB into an emptied file in  1 KiB chunks (x128): ( min: 2.22261, 2.28 +- 0.04, max: 2.32223 ) GB/s
    pwritev 1 GiB into an emptied file in  4 KiB chunks (x128): ( min: 2.50591, 3.20 +- 0.25, max: 3.33147 ) GB/s
    pwritev 1 GiB into an emptied file in 16 KiB chunks (x128): ( min: 2.11319, 3.2 +- 0.4, max: 3.45544 ) GB/s
    pwritev 1 GiB into an emptied file in 64 KiB chunks (x128): ( min: 3.01684, 3.33 +- 0.13, max: 3.44025 ) GB/s
    pwritev 1 GiB into an emptied file in  1 MiB chunks (x128): ( min: 3.29044, 3.36 +- 0.05, max: 3.44673 ) GB/s
    pwritev 1 GiB into an emptied file in  8 MiB chunks (x128): ( min: 2.81554, 3.11 +- 0.17, max: 3.37963 ) GB/s

### Parallel Writing

    Use pwrite to write 1 GiB into an emptied file using  1 threads: ( min: 3.09821, 3.29 +- 0.15, max: 3.5006 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  2 threads: ( min: 2.88391, 3.36 +- 0.18, max: 3.49537 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  4 threads: ( min: 2.76745, 3.11 +- 0.19, max: 3.40337 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  8 threads: ( min: 1.51969, 2.9 +- 0.5, max: 3.25973 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using 16 threads: ( min: 2.6655, 3.01 +- 0.20, max: 3.23938 ) GB/s

### Simple Writing

    fwrite 1 GiB into an emptied file in   1 KiB chunks: ( min: 1.26326, 1.281 +- 0.014, max: 1.31053 ) GB/s
    fwrite 1 GiB into an emptied file in   4 KiB chunks: ( min: 1.31278, 1.333 +- 0.010, max: 1.35381 ) GB/s
    fwrite 1 GiB into an emptied file in   8 KiB chunks: ( min: 0.967796, 1.29 +- 0.12, max: 1.34949 ) GB/s
    fwrite 1 GiB into an emptied file in  16 KiB chunks: ( min: 1.82769, 1.88 +- 0.04, max: 1.92523 ) GB/s
    fwrite 1 GiB into an emptied file in  64 KiB chunks: ( min: 2.6548, 2.71 +- 0.04, max: 2.77784 ) GB/s
    fwrite 1 GiB into an emptied file in   1 MiB chunks: ( min: 2.89066, 3.09 +- 0.08, max: 3.18177 ) GB/s
    fwrite 1 GiB into an emptied file in  16 MiB chunks: ( min: 2.62358, 2.96 +- 0.16, max: 3.1441 ) GB/s
    fwrite 1 GiB into an emptied file in  64 MiB chunks: ( min: 2.75154, 2.94 +- 0.10, max: 3.03047 ) GB/s
    fwrite 1 GiB into an emptied file in 512 MiB chunks: ( min: 2.87739, 3.01 +- 0.08, max: 3.13778 ) GB/s
    fwrite 1 GiB into an emptied file in   1 GiB chunks: ( min: 2.84353, 3.02 +- 0.09, max: 3.11223 ) GB/s

    write 1 GiB into an emptied file in   1 KiB chunks: ( min: 0.354614, 0.389 +- 0.013, max: 0.399672 ) GB/s
    write 1 GiB into an emptied file in   4 KiB chunks: ( min: 0.927434, 1.30 +- 0.13, max: 1.38354 ) GB/s
    write 1 GiB into an emptied file in   8 KiB chunks: ( min: 1.8427, 1.90 +- 0.04, max: 1.97674 ) GB/s
    write 1 GiB into an emptied file in  16 KiB chunks: ( min: 2.33626, 2.368 +- 0.022, max: 2.41521 ) GB/s
    write 1 GiB into an emptied file in  64 KiB chunks: ( min: 2.83564, 2.91 +- 0.06, max: 3.02564 ) GB/s
    write 1 GiB into an emptied file in   1 MiB chunks: ( min: 2.97612, 3.06 +- 0.04, max: 3.11131 ) GB/s
    write 1 GiB into an emptied file in  16 MiB chunks: ( min: 3.0002, 3.10 +- 0.05, max: 3.16848 ) GB/s
    write 1 GiB into an emptied file in  64 MiB chunks: ( min: 2.60867, 3.04 +- 0.20, max: 3.22144 ) GB/s
    write 1 GiB into an emptied file in 512 MiB chunks: ( min: 2.98629, 3.14 +- 0.08, max: 3.23885 ) GB/s
    write 1 GiB into an emptied file in   1 GiB chunks: ( min: 3.05313, 3.14 +- 0.07, max: 3.22438 ) GB/s

## Write into a sparsely allocated file

### Vectorized Writing

    writev 1 GiB into a sparsely allocated file in  1 KiB chunks (x128): ( min: 2.10825, 2.164 +- 0.030, max: 2.21053 ) GB/s
    writev 1 GiB into a sparsely allocated file in  4 KiB chunks (x128): ( min: 2.91324, 3.01 +- 0.06, max: 3.09544 ) GB/s
    writev 1 GiB into a sparsely allocated file in 16 KiB chunks (x128): ( min: 1.49594, 2.9 +- 0.5, max: 3.25325 ) GB/s
    writev 1 GiB into a sparsely allocated file in 64 KiB chunks (x128): ( min: 3.02037, 3.10 +- 0.05, max: 3.16892 ) GB/s
    writev 1 GiB into a sparsely allocated file in  1 MiB chunks (x128): ( min: 2.93309, 3.08 +- 0.07, max: 3.15045 ) GB/s
    writev 1 GiB into a sparsely allocated file in  8 MiB chunks (x128): ( min: 3.04546, 3.15 +- 0.06, max: 3.26833 ) GB/s

    pwritev 1 GiB into a sparsely allocated file in  1 KiB chunks (x128): ( min: 2.07598, 2.14 +- 0.04, max: 2.19062 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in  4 KiB chunks (x128): ( min: 2.81409, 2.97 +- 0.07, max: 3.10257 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in 16 KiB chunks (x128): ( min: 2.96888, 3.05 +- 0.05, max: 3.11657 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in 64 KiB chunks (x128): ( min: 2.99915, 3.08 +- 0.04, max: 3.1294 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in  1 MiB chunks (x128): ( min: 1.65381, 2.9 +- 0.5, max: 3.18539 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in  8 MiB chunks (x128): ( min: 2.94195, 3.07 +- 0.07, max: 3.14129 ) GB/s

### Parallel Writing

    Use pwrite to write 1 GiB into a sparsely allocated file using  1 threads: ( min: 3.12368, 3.19 +- 0.05, max: 3.26824 ) GB/s
    Use pwrite to write 1 GiB into a sparsely allocated file using  2 threads: ( min: 2.93466, 3.15 +- 0.09, max: 3.2299 ) GB/s
    Use pwrite to write 1 GiB into a sparsely allocated file using  4 threads: ( min: 3.04005, 3.15 +- 0.07, max: 3.22996 ) GB/s
    Use pwrite to write 1 GiB into a sparsely allocated file using  8 threads: ( min: 2.88158, 3.09 +- 0.10, max: 3.16277 ) GB/s
    Use pwrite to write 1 GiB into a sparsely allocated file using 16 threads: ( min: 2.62079, 2.93 +- 0.16, max: 3.10447 ) GB/s

### Simple Writing

    fwrite 1 GiB into a sparsely allocated file in   1 KiB chunks: ( min: 0.865213, 1.12 +- 0.09, max: 1.18113 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in   4 KiB chunks: ( min: 1.1071, 1.17 +- 0.03, max: 1.20544 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in   8 KiB chunks: ( min: 1.18349, 1.202 +- 0.013, max: 1.22403 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in  16 KiB chunks: ( min: 1.15025, 1.36 +- 0.23, max: 1.72123 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in  64 KiB chunks: ( min: 1.64812, 1.77 +- 0.06, max: 1.86379 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in   1 MiB chunks: ( min: 1.69848, 1.87 +- 0.12, max: 2.06963 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in  16 MiB chunks: ( min: 1.76459, 1.91 +- 0.09, max: 2.05086 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in  64 MiB chunks: ( min: 1.83727, 2.06 +- 0.29, max: 2.8594 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in 512 MiB chunks: ( min: 2.93825, 3.08 +- 0.07, max: 3.17762 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in   1 GiB chunks: ( min: 2.77491, 3.07 +- 0.13, max: 3.18735 ) GB/s

    write 1 GiB into a sparsely allocated file in   1 KiB chunks: ( min: 0.364659, 0.391 +- 0.017, max: 0.408974 ) GB/s
    write 1 GiB into a sparsely allocated file in   4 KiB chunks: ( min: 0.908704, 0.980 +- 0.029, max: 1.00868 ) GB/s
    write 1 GiB into a sparsely allocated file in   8 KiB chunks: ( min: 1.24908, 1.284 +- 0.030, max: 1.35173 ) GB/s
    write 1 GiB into a sparsely allocated file in  16 KiB chunks: ( min: 1.45958, 1.52 +- 0.04, max: 1.59901 ) GB/s
    write 1 GiB into a sparsely allocated file in  64 KiB chunks: ( min: 1.72044, 1.79 +- 0.06, max: 1.86585 ) GB/s
    write 1 GiB into a sparsely allocated file in   1 MiB chunks: ( min: 1.69859, 1.86 +- 0.11, max: 2.01391 ) GB/s
    write 1 GiB into a sparsely allocated file in  16 MiB chunks: ( min: 1.76572, 1.90 +- 0.06, max: 1.96319 ) GB/s
    write 1 GiB into a sparsely allocated file in  64 MiB chunks: ( min: 1.87306, 1.95 +- 0.06, max: 2.05155 ) GB/s
    write 1 GiB into a sparsely allocated file in 512 MiB chunks: ( min: 1.76801, 1.90 +- 0.08, max: 2.00376 ) GB/s
    write 1 GiB into a sparsely allocated file in   1 GiB chunks: ( min: 1.79256, 1.92 +- 0.06, max: 2.01518 ) GB/s

## Write into a preallocated file

### Vectorized Writing

    writev 1 GiB into a preallocated file in  1 KiB chunks (x128): ( min: 1.62274, 1.76 +- 0.09, max: 1.89896 ) GB/s
    writev 1 GiB into a preallocated file in  4 KiB chunks (x128): ( min: 2.24839, 2.42 +- 0.11, max: 2.62909 ) GB/s
    writev 1 GiB into a preallocated file in 16 KiB chunks (x128): ( min: 2.04882, 2.34 +- 0.16, max: 2.58314 ) GB/s
    writev 1 GiB into a preallocated file in 64 KiB chunks (x128): ( min: 2.14051, 2.36 +- 0.11, max: 2.47833 ) GB/s
    writev 1 GiB into a preallocated file in  1 MiB chunks (x128): ( min: 2.30104, 2.38 +- 0.05, max: 2.46119 ) GB/s
    writev 1 GiB into a preallocated file in  8 MiB chunks (x128): ( min: 2.1575, 2.41 +- 0.16, max: 2.65168 ) GB/s

    pwritev 1 GiB into a preallocated file in  1 KiB chunks (x128): ( min: 1.65748, 1.74 +- 0.04, max: 1.784 ) GB/s
    pwritev 1 GiB into a preallocated file in  4 KiB chunks (x128): ( min: 2.16071, 2.29 +- 0.10, max: 2.46631 ) GB/s
    pwritev 1 GiB into a preallocated file in 16 KiB chunks (x128): ( min: 2.2413, 2.42 +- 0.11, max: 2.60139 ) GB/s
    pwritev 1 GiB into a preallocated file in 64 KiB chunks (x128): ( min: 2.12604, 2.33 +- 0.12, max: 2.53877 ) GB/s
    pwritev 1 GiB into a preallocated file in  1 MiB chunks (x128): ( min: 2.25109, 2.34 +- 0.10, max: 2.57351 ) GB/s
    pwritev 1 GiB into a preallocated file in  8 MiB chunks (x128): ( min: 2.31454, 2.46 +- 0.11, max: 2.61466 ) GB/s

### Parallel Writing

    Use pwrite to write 1 GiB into a preallocated file using  1 threads: ( min: 2.30656, 2.46 +- 0.08, max: 2.57269 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  2 threads: ( min: 2.19661, 2.42 +- 0.14, max: 2.61658 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  4 threads: ( min: 2.24306, 2.40 +- 0.11, max: 2.59202 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  8 threads: ( min: 2.12149, 2.36 +- 0.14, max: 2.5357 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using 16 threads: ( min: 2.06347, 2.23 +- 0.13, max: 2.41649 ) GB/s

### Simple Writing

    fwrite 1 GiB into a preallocated file in   1 KiB chunks: ( min: 0.996756, 1.017 +- 0.015, max: 1.05049 ) GB/s
    fwrite 1 GiB into a preallocated file in   4 KiB chunks: ( min: 1.00683, 1.049 +- 0.026, max: 1.0769 ) GB/s
    fwrite 1 GiB into a preallocated file in   8 KiB chunks: ( min: 0.983908, 1.04 +- 0.04, max: 1.09199 ) GB/s
    fwrite 1 GiB into a preallocated file in  16 KiB chunks: ( min: 1.32516, 1.38 +- 0.03, max: 1.42 ) GB/s
    fwrite 1 GiB into a preallocated file in  64 KiB chunks: ( min: 1.79565, 1.98 +- 0.09, max: 2.11399 ) GB/s
    fwrite 1 GiB into a preallocated file in   1 MiB chunks: ( min: 2.13068, 2.34 +- 0.16, max: 2.65869 ) GB/s
    fwrite 1 GiB into a preallocated file in  16 MiB chunks: ( min: 1.96863, 2.33 +- 0.17, max: 2.58965 ) GB/s
    fwrite 1 GiB into a preallocated file in  64 MiB chunks: ( min: 2.08723, 2.27 +- 0.11, max: 2.42334 ) GB/s
    fwrite 1 GiB into a preallocated file in 512 MiB chunks: ( min: 2.17759, 2.33 +- 0.11, max: 2.55789 ) GB/s
    fwrite 1 GiB into a preallocated file in   1 GiB chunks: ( min: 2.18983, 2.38 +- 0.13, max: 2.54735 ) GB/s

    write 1 GiB into a preallocated file in   1 KiB chunks: ( min: 0.368942, 0.382 +- 0.007, max: 0.389395 ) GB/s
    write 1 GiB into a preallocated file in   4 KiB chunks: ( min: 1.03256, 1.059 +- 0.012, max: 1.07341 ) GB/s
    write 1 GiB into a preallocated file in   8 KiB chunks: ( min: 1.35772, 1.45 +- 0.05, max: 1.52244 ) GB/s
    write 1 GiB into a preallocated file in  16 KiB chunks: ( min: 1.76232, 1.90 +- 0.05, max: 1.96042 ) GB/s
    write 1 GiB into a preallocated file in  64 KiB chunks: ( min: 2.13685, 2.32 +- 0.13, max: 2.52444 ) GB/s
    write 1 GiB into a preallocated file in   1 MiB chunks: ( min: 2.35638, 2.50 +- 0.12, max: 2.69043 ) GB/s
    write 1 GiB into a preallocated file in  16 MiB chunks: ( min: 2.26399, 2.53 +- 0.11, max: 2.671 ) GB/s
    write 1 GiB into a preallocated file in  64 MiB chunks: ( min: 2.25126, 2.44 +- 0.10, max: 2.63851 ) GB/s
    write 1 GiB into a preallocated file in 512 MiB chunks: ( min: 2.18099, 2.42 +- 0.15, max: 2.72754 ) GB/s
    write 1 GiB into a preallocated file in   1 GiB chunks: ( min: 2.11635, 2.29 +- 0.11, max: 2.44166 ) GB/s

## Observations

 1. No speedup over directly using /dev/shm, even a little bit slower especially for the preallocated case.


# Test on HomePC with EXT4 on a RAID-0 with 512K chunks striped into 4 files residing on /dev/shm

```bash
for i in 1 2 3 4; do
    fallocate --length 1GiB /dev/shm/foo$i
    sudo losetup /dev/loop$i /dev/shm/foo$i
done
sudo mdadm --create --verbose /dev/md/memory-raid-0 --level=0 --raid-devices=4 /dev/loop[1-4]

    mdadm: chunk size defaults to 512K
    mdadm: Defaulting to version 1.2 metadata
    mdadm: array /dev/md/memory-raid-0 started.

mkfs.ext4 -m 0 /dev/md/memory-raid-0

    mke2fs 1.46.5 (30-Dec-2021)
    Discarding device blocks: done
    Creating filesystem with 1046528 4k blocks and 261632 inodes
    Filesystem UUID: b5ae540a-73ed-483e-a948-f7c881cbcea4
    Superblock backups stored on blocks:
        32768, 98304, 163840, 229376, 294912, 819200, 884736

    Allocating group tables: done
    Writing inode tables: done
    Creating journal (16384 blocks): done
    Writing superblocks and filesystem accounting information: done

mkdir /media/memory-raid-0
sudo mount /dev/md/memory-raid-0 /media/memory-raid-0
sudo chmod o+w /media/memory-raid-0
df -h /media/memory-raid-0

    Filesystem      Size  Used Avail Use% Mounted on
    /dev/md127      3.9G   24K  3.9G   1% /media/memory-raid-0

cmake --build . -- benchmarkIOWrite && src/benchmarks/benchmarkIOWrite /media/memory-raid-0/pragzip-write-test

sudo umount /dev/md/memory-raid-0
for i in 1 2 3 4; do
    sudo losetup --detach /dev/loop$i
    'rm' /dev/shm/foo$i
done
```

# File Creation

    posix_fallocate file sized 512 MiB: ( min: 530.74, 1700 +- 400, max: 1976.92 ) GB/s
    posix_fallocate file sized 1 GiB: ( min: 865.326, 1900 +- 600, max: 2760.26 ) GB/s
    posix_fallocate file sized 2 GiB: ( min: 1798.4, 1890 +- 60, max: 1980.07 ) GB/s
    posix_fallocate file sized 4 GiB: ( min: 2002.76, 2190 +- 80, max: 2259.88 ) GB/s

    fallocate file sized 512 MiB: ( min: 697.597, 2200 +- 800, max: 2706.67 ) GB/s
    fallocate file sized 1 GiB: ( min: 1192.81, 2300 +- 800, max: 2949.11 ) GB/s
    fallocate file sized 2 GiB: ( min: 1460.06, 1710 +- 160, max: 1923.32 ) GB/s
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    Encountered error while calling fallocate on file: No space left on device (28)
    fallocate file sized 4 GiB: ( min: 1774.81, 1970 +- 160, max: 2244.1 ) GB/s

    ftruncate file sized 512 MiB: ( min: 26188.8, 42000 +- 8000, max: 53208.2 ) GB/s
    ftruncate file sized 1 GiB: ( min: 56246.3, 77000 +- 13000, max: 99605 ) GB/s
    ftruncate file sized 2 GiB: ( min: 119971, 160000 +- 30000, max: 204717 ) GB/s
    ftruncate file sized 4 GiB: ( min: 263333, 330000 +- 40000, max: 415374 ) GB/s

# Mmap Write

    ftruncate + mmap write 1 GiB: ( min: 1.89576, 1.943 +- 0.026, max: 1.97251 ) GB/s
    ftruncate + mmap write 1 GiB using  1 threads: ( min: 1.91181, 1.938 +- 0.017, max: 1.97241 ) GB/s
    ftruncate + mmap write 1 GiB using  2 threads: ( min: 2.73886, 2.81 +- 0.05, max: 2.89571 ) GB/s
    ftruncate + mmap write 1 GiB using  4 threads: ( min: 3.40213, 3.55 +- 0.09, max: 3.66022 ) GB/s
    ftruncate + mmap write 1 GiB using  8 threads: ( min: 3.36117, 3.54 +- 0.11, max: 3.6977 ) GB/s
    ftruncate + mmap write 1 GiB using 16 threads: ( min: 2.93512, 3.08 +- 0.11, max: 3.22082 ) GB/s

# Write into an emptied file

## Vectorized Writing

    writev 1 GiB into an emptied file in  1 KiB chunks (x128): ( min: 1.56918, 1.596 +- 0.018, max: 1.62632 ) GB/s
    writev 1 GiB into an emptied file in  4 KiB chunks (x128): ( min: 2.05898, 2.091 +- 0.023, max: 2.11951 ) GB/s
    writev 1 GiB into an emptied file in 16 KiB chunks (x128): ( min: 2.04449, 2.10 +- 0.03, max: 2.14047 ) GB/s
    writev 1 GiB into an emptied file in 64 KiB chunks (x128): ( min: 2.08091, 2.112 +- 0.024, max: 2.16522 ) GB/s
    writev 1 GiB into an emptied file in  1 MiB chunks (x128): ( min: 1.6799, 2.08 +- 0.14, max: 2.18105 ) GB/s
    writev 1 GiB into an emptied file in  8 MiB chunks (x128): ( min: 2.06099, 2.089 +- 0.023, max: 2.14051 ) GB/s

    pwritev 1 GiB into an emptied file in  1 KiB chunks (x128): ( min: 1.56861, 1.599 +- 0.030, max: 1.6519 ) GB/s
    pwritev 1 GiB into an emptied file in  4 KiB chunks (x128): ( min: 1.95718, 2.05 +- 0.05, max: 2.10899 ) GB/s
    pwritev 1 GiB into an emptied file in 16 KiB chunks (x128): ( min: 2.05989, 2.087 +- 0.016, max: 2.11258 ) GB/s
    pwritev 1 GiB into an emptied file in 64 KiB chunks (x128): ( min: 2.00609, 2.09 +- 0.04, max: 2.13972 ) GB/s
    pwritev 1 GiB into an emptied file in  1 MiB chunks (x128): ( min: 2.02536, 2.10 +- 0.03, max: 2.15874 ) GB/s
    pwritev 1 GiB into an emptied file in  8 MiB chunks (x128): ( min: 2.04048, 2.11 +- 0.04, max: 2.15783 ) GB/s

## Parallel Writing

    Use pwrite to write 1 GiB into an emptied file using  1 threads: ( min: 2.15861, 2.24 +- 0.04, max: 2.29646 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  2 threads: ( min: 2.12819, 2.28 +- 0.12, max: 2.54807 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  4 threads: ( min: 2.05487, 2.32 +- 0.14, max: 2.56311 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  8 threads: ( min: 1.90896, 2.30 +- 0.16, max: 2.47218 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using 16 threads: ( min: 1.94859, 2.15 +- 0.13, max: 2.45999 ) GB/s

    Write 1 GiB into one file per thread using  1 threads: ( min: 1.4462, 2.27 +- 0.29, max: 2.40633 ) GB/s
    Write 1 GiB into one file per thread using  2 threads: ( min: 3.25562, 3.55 +- 0.12, max: 3.67789 ) GB/s
    Write 1 GiB into one file per thread using  4 threads: ( min: 3.42008, 5.6 +- 0.8, max: 6.02345 ) GB/s
    Write 1 GiB into one file per thread using  8 threads: ( min: 3.60285, 7.0 +- 1.7, max: 7.84321 ) GB/s
    Write 1 GiB into one file per thread using 16 threads: ( min: 3.87997, 7.4 +- 1.3, max: 8.02459 ) GB/s

## Simple Writing

    fwrite 1 GiB into an emptied file in   1 KiB chunks: ( min: 1.10324, 1.141 +- 0.017, max: 1.16022 ) GB/s
    fwrite 1 GiB into an emptied file in   4 KiB chunks: ( min: 1.13706, 1.186 +- 0.020, max: 1.20883 ) GB/s
    fwrite 1 GiB into an emptied file in   8 KiB chunks: ( min: 1.09486, 1.19 +- 0.04, max: 1.22726 ) GB/s
    fwrite 1 GiB into an emptied file in  16 KiB chunks: ( min: 1.45485, 1.514 +- 0.027, max: 1.53974 ) GB/s
    fwrite 1 GiB into an emptied file in  64 KiB chunks: ( min: 1.85953, 1.93 +- 0.04, max: 1.97049 ) GB/s
    fwrite 1 GiB into an emptied file in   1 MiB chunks: ( min: 1.97094, 2.04 +- 0.04, max: 2.09446 ) GB/s
    fwrite 1 GiB into an emptied file in  16 MiB chunks: ( min: 2.03563, 2.081 +- 0.027, max: 2.13791 ) GB/s
    fwrite 1 GiB into an emptied file in  64 MiB chunks: ( min: 2.07385, 2.12 +- 0.03, max: 2.17155 ) GB/s
    fwrite 1 GiB into an emptied file in 512 MiB chunks: ( min: 2.03554, 2.14 +- 0.05, max: 2.19338 ) GB/s
    fwrite 1 GiB into an emptied file in   1 GiB chunks: ( min: 2.09423, 2.15 +- 0.03, max: 2.20468 ) GB/s

    write 1 GiB into an emptied file in   1 KiB chunks: ( min: 0.450983, 0.462 +- 0.007, max: 0.4752 ) GB/s
    write 1 GiB into an emptied file in   4 KiB chunks: ( min: 1.08378, 1.18 +- 0.05, max: 1.22467 ) GB/s
    write 1 GiB into an emptied file in   8 KiB chunks: ( min: 1.49194, 1.519 +- 0.020, max: 1.56327 ) GB/s
    write 1 GiB into an emptied file in  16 KiB chunks: ( min: 1.66853, 1.75 +- 0.05, max: 1.83646 ) GB/s
    write 1 GiB into an emptied file in  64 KiB chunks: ( min: 1.92428, 1.99 +- 0.04, max: 2.04185 ) GB/s
    write 1 GiB into an emptied file in   1 MiB chunks: ( min: 1.95089, 2.05 +- 0.05, max: 2.10728 ) GB/s
    write 1 GiB into an emptied file in  16 MiB chunks: ( min: 2.00942, 2.08 +- 0.04, max: 2.13782 ) GB/s
    write 1 GiB into an emptied file in  64 MiB chunks: ( min: 2.01368, 2.063 +- 0.030, max: 2.11409 ) GB/s
    write 1 GiB into an emptied file in 512 MiB chunks: ( min: 1.90792, 2.06 +- 0.06, max: 2.13236 ) GB/s
    write 1 GiB into an emptied file in   1 GiB chunks: ( min: 1.94934, 2.00 +- 0.04, max: 2.06174 ) GB/s

# Write into a sparsely allocated file

## Vectorized Writing

    writev 1 GiB into a sparsely allocated file in  1 KiB chunks (x128): ( min: 1.81781, 1.88 +- 0.04, max: 1.92909 ) GB/s
    writev 1 GiB into a sparsely allocated file in  4 KiB chunks (x128): ( min: 2.50127, 2.57 +- 0.04, max: 2.64558 ) GB/s
    writev 1 GiB into a sparsely allocated file in 16 KiB chunks (x128): ( min: 2.34833, 2.52 +- 0.09, max: 2.63045 ) GB/s
    writev 1 GiB into a sparsely allocated file in 64 KiB chunks (x128): ( min: 2.48923, 2.56 +- 0.05, max: 2.6194 ) GB/s
    writev 1 GiB into a sparsely allocated file in  1 MiB chunks (x128): ( min: 2.48843, 2.63 +- 0.08, max: 2.71061 ) GB/s
    writev 1 GiB into a sparsely allocated file in  8 MiB chunks (x128): ( min: 2.49376, 2.60 +- 0.05, max: 2.66297 ) GB/s

    pwritev 1 GiB into a sparsely allocated file in  1 KiB chunks (x128): ( min: 1.73039, 1.80 +- 0.04, max: 1.84807 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in  4 KiB chunks (x128): ( min: 2.28099, 2.49 +- 0.10, max: 2.58677 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in 16 KiB chunks (x128): ( min: 2.41144, 2.53 +- 0.07, max: 2.62265 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in 64 KiB chunks (x128): ( min: 2.39013, 2.56 +- 0.10, max: 2.70821 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in  1 MiB chunks (x128): ( min: 2.4978, 2.539 +- 0.029, max: 2.59897 ) GB/s
    pwritev 1 GiB into a sparsely allocated file in  8 MiB chunks (x128): ( min: 2.51332, 2.59 +- 0.05, max: 2.65336 ) GB/s

## Parallel Writing

    Use pwrite to write 1 GiB into a sparsely allocated file using  1 threads: ( min: 2.68573, 2.81 +- 0.06, max: 2.91638 ) GB/s
    Use pwrite to write 1 GiB into a sparsely allocated file using  2 threads: ( min: 2.6069, 2.71 +- 0.05, max: 2.75817 ) GB/s
    Use pwrite to write 1 GiB into a sparsely allocated file using  4 threads: ( min: 2.53404, 2.70 +- 0.09, max: 2.83444 ) GB/s
    Use pwrite to write 1 GiB into a sparsely allocated file using  8 threads: ( min: 2.34037, 2.62 +- 0.15, max: 2.78236 ) GB/s
    Use pwrite to write 1 GiB into a sparsely allocated file using 16 threads: ( min: 2.1, 2.36 +- 0.21, max: 2.78997 ) GB/s

    Write 1 GiB into one file per thread using  1 threads: ( min: 2.68351, 2.87 +- 0.14, max: 3.04955 ) GB/s
    Write 1 GiB into one file per thread using  2 threads: ( min: 3.41838, 4.02 +- 0.22, max: 4.17873 ) GB/s
    Write 1 GiB into one file per thread using  4 threads: ( min: 5.14838, 5.54 +- 0.26, max: 5.9319 ) GB/s
    Write 1 GiB into one file per thread using  8 threads: ( min: 6.68145, 7.08 +- 0.18, max: 7.27802 ) GB/s
    Write 1 GiB into one file per thread using 16 threads: ( min: 4.02533, 6.1 +- 1.6, max: 7.43174 ) GB/s

## Simple Writing

    fwrite 1 GiB into a sparsely allocated file in   1 KiB chunks: ( min: 1.2664, 1.287 +- 0.013, max: 1.30903 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in   4 KiB chunks: ( min: 1.28689, 1.320 +- 0.027, max: 1.35486 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in   8 KiB chunks: ( min: 1.27084, 1.317 +- 0.026, max: 1.36242 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in  16 KiB chunks: ( min: 1.69945, 1.736 +- 0.027, max: 1.77217 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in  64 KiB chunks: ( min: 2.1925, 2.35 +- 0.07, max: 2.43756 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in   1 MiB chunks: ( min: 2.51079, 2.56 +- 0.03, max: 2.6258 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in  16 MiB chunks: ( min: 2.56858, 2.63 +- 0.04, max: 2.68211 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in  64 MiB chunks: ( min: 2.55299, 2.62 +- 0.07, max: 2.73179 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in 512 MiB chunks: ( min: 2.54356, 2.64 +- 0.05, max: 2.73167 ) GB/s
    fwrite 1 GiB into a sparsely allocated file in   1 GiB chunks: ( min: 2.56557, 2.63 +- 0.04, max: 2.68307 ) GB/s

    write 1 GiB into a sparsely allocated file in   1 KiB chunks: ( min: 0.546227, 0.571 +- 0.012, max: 0.588535 ) GB/s
    write 1 GiB into a sparsely allocated file in   4 KiB chunks: ( min: 1.3257, 1.362 +- 0.020, max: 1.39962 ) GB/s
    write 1 GiB into a sparsely allocated file in   8 KiB chunks: ( min: 1.71908, 1.76 +- 0.03, max: 1.80638 ) GB/s
    write 1 GiB into a sparsely allocated file in  16 KiB chunks: ( min: 2.04658, 2.09 +- 0.03, max: 2.1501 ) GB/s
    write 1 GiB into a sparsely allocated file in  64 KiB chunks: ( min: 2.46134, 2.501 +- 0.029, max: 2.53541 ) GB/s
    write 1 GiB into a sparsely allocated file in   1 MiB chunks: ( min: 2.53794, 2.60 +- 0.05, max: 2.67999 ) GB/s
    write 1 GiB into a sparsely allocated file in  16 MiB chunks: ( min: 2.52126, 2.61 +- 0.06, max: 2.68587 ) GB/s
    write 1 GiB into a sparsely allocated file in  64 MiB chunks: ( min: 2.54728, 2.59 +- 0.04, max: 2.68069 ) GB/s
    write 1 GiB into a sparsely allocated file in 512 MiB chunks: ( min: 2.50794, 2.59 +- 0.06, max: 2.66418 ) GB/s
    write 1 GiB into a sparsely allocated file in   1 GiB chunks: ( min: 2.56873, 2.63 +- 0.05, max: 2.71947 ) GB/s

# Write into a preallocated file

## Vectorized Writing

    writev 1 GiB into a preallocated file in  1 KiB chunks (x128): ( min: 1.88145, 1.93 +- 0.04, max: 1.99706 ) GB/s
    writev 1 GiB into a preallocated file in  4 KiB chunks (x128): ( min: 2.65725, 2.72 +- 0.05, max: 2.79333 ) GB/s
    writev 1 GiB into a preallocated file in 16 KiB chunks (x128): ( min: 2.66487, 2.72 +- 0.05, max: 2.83919 ) GB/s
    writev 1 GiB into a preallocated file in 64 KiB chunks (x128): ( min: 2.68086, 2.73 +- 0.04, max: 2.81069 ) GB/s
    writev 1 GiB into a preallocated file in  1 MiB chunks (x128): ( min: 2.63922, 2.72 +- 0.05, max: 2.8103 ) GB/s
    writev 1 GiB into a preallocated file in  8 MiB chunks (x128): ( min: 2.66629, 2.77 +- 0.07, max: 2.89423 ) GB/s

    pwritev 1 GiB into a preallocated file in  1 KiB chunks (x128): ( min: 1.85791, 1.901 +- 0.029, max: 1.95824 ) GB/s
    pwritev 1 GiB into a preallocated file in  4 KiB chunks (x128): ( min: 2.56639, 2.65 +- 0.04, max: 2.72738 ) GB/s
    pwritev 1 GiB into a preallocated file in 16 KiB chunks (x128): ( min: 2.68357, 2.717 +- 0.028, max: 2.75869 ) GB/s
    pwritev 1 GiB into a preallocated file in 64 KiB chunks (x128): ( min: 2.56843, 2.71 +- 0.06, max: 2.76094 ) GB/s
    pwritev 1 GiB into a preallocated file in  1 MiB chunks (x128): ( min: 2.71006, 2.80 +- 0.04, max: 2.85031 ) GB/s
    pwritev 1 GiB into a preallocated file in  8 MiB chunks (x128): ( min: 2.60974, 2.73 +- 0.07, max: 2.82219 ) GB/s

## Parallel Writing

    Use pwrite to write 1 GiB into a preallocated file using  1 threads: ( min: 2.85471, 3.04 +- 0.08, max: 3.12548 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  2 threads: ( min: 2.88861, 2.97 +- 0.05, max: 3.05895 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  4 threads: ( min: 2.77604, 2.91 +- 0.08, max: 3.04366 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  8 threads: ( min: 2.36131, 2.73 +- 0.20, max: 2.94817 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using 16 threads: ( min: 2.26655, 2.51 +- 0.17, max: 2.77191 ) GB/s

    Write 1 GiB into one file per thread using  1 threads: ( min: 2.84498, 3.10 +- 0.18, max: 3.32806 ) GB/s
    Write 1 GiB into one file per thread using  2 threads: ( min: 3.70944, 3.95 +- 0.12, max: 4.13063 ) GB/s
    Write 1 GiB into one file per thread using  4 threads: ( min: 4.3075, 5.3 +- 0.4, max: 5.71149 ) GB/s
    Write 1 GiB into one file per thread using  8 threads: ( min: 6.11861, 6.9 +- 0.4, max: 7.28392 ) GB/s
    Write 1 GiB into one file per thread using 16 threads: ( min: 2.77009, 2.98 +- 0.12, max: 3.18324 ) GB/s

## Simple Writing

    fwrite 1 GiB into a preallocated file in   1 KiB chunks: ( min: 1.22472, 1.26 +- 0.03, max: 1.32421 ) GB/s
    fwrite 1 GiB into a preallocated file in   4 KiB chunks: ( min: 1.2856, 1.324 +- 0.024, max: 1.35112 ) GB/s
    fwrite 1 GiB into a preallocated file in   8 KiB chunks: ( min: 1.28816, 1.321 +- 0.024, max: 1.35387 ) GB/s
    fwrite 1 GiB into a preallocated file in  16 KiB chunks: ( min: 1.5554, 1.73 +- 0.07, max: 1.78044 ) GB/s
    fwrite 1 GiB into a preallocated file in  64 KiB chunks: ( min: 2.0791, 2.29 +- 0.10, max: 2.41611 ) GB/s
    fwrite 1 GiB into a preallocated file in   1 MiB chunks: ( min: 2.38495, 2.49 +- 0.09, max: 2.66576 ) GB/s
    fwrite 1 GiB into a preallocated file in  16 MiB chunks: ( min: 2.53022, 2.565 +- 0.027, max: 2.61345 ) GB/s
    fwrite 1 GiB into a preallocated file in  64 MiB chunks: ( min: 2.4814, 2.56 +- 0.04, max: 2.6147 ) GB/s
    fwrite 1 GiB into a preallocated file in 512 MiB chunks: ( min: 2.46017, 2.57 +- 0.05, max: 2.65444 ) GB/s
    fwrite 1 GiB into a preallocated file in   1 GiB chunks: ( min: 2.28881, 2.53 +- 0.12, max: 2.69895 ) GB/s

    write 1 GiB into a preallocated file in   1 KiB chunks: ( min: 0.541408, 0.577 +- 0.015, max: 0.588528 ) GB/s
    write 1 GiB into a preallocated file in   4 KiB chunks: ( min: 1.27866, 1.41 +- 0.05, max: 1.45068 ) GB/s
    write 1 GiB into a preallocated file in   8 KiB chunks: ( min: 1.69199, 1.78 +- 0.04, max: 1.84028 ) GB/s
    write 1 GiB into a preallocated file in  16 KiB chunks: ( min: 2.01717, 2.13 +- 0.06, max: 2.20091 ) GB/s
    write 1 GiB into a preallocated file in  64 KiB chunks: ( min: 2.401, 2.50 +- 0.06, max: 2.60915 ) GB/s
    write 1 GiB into a preallocated file in   1 MiB chunks: ( min: 2.6119, 2.66 +- 0.03, max: 2.71 ) GB/s
    write 1 GiB into a preallocated file in  16 MiB chunks: ( min: 2.58532, 2.66 +- 0.04, max: 2.72007 ) GB/s
    write 1 GiB into a preallocated file in  64 MiB chunks: ( min: 2.50852, 2.64 +- 0.08, max: 2.75877 ) GB/s
    write 1 GiB into a preallocated file in 512 MiB chunks: ( min: 2.53332, 2.63 +- 0.05, max: 2.69524 ) GB/s
    write 1 GiB into a preallocated file in   1 GiB chunks: ( min: 2.57959, 2.67 +- 0.05, max: 2.73804 ) GB/s

## Observations

 1. No speedup over directly using /dev/shm, even a little bit slower especially for the preallocated case.

# Test on HomePC with FUSE ramdisk

```bash
git clone https://github.com/thegreyd/FuseFilesystem.git
cd FuseFilesystem
make
mkdir /media/fuse-ramdisk
./ramdisk /media/fuse-ramdisk $(( 5*1024 ))
```

Had to do the benchmark with an 8 MiB file because it is so excruciatingly slow!

posix_fallocate seems to indicate an O(size^2) complexity for file creation!


## File Creation

    posix_fallocate file sized 1 MiB: ( min: 17.8726, 19.5 +- 1.2, max: 21.318 ) MB/s
    posix_fallocate file sized 4 MiB: ( min: 14.8422, 15.4 +- 0.6, max: 16.3736 ) MB/s
    posix_fallocate file sized 8 MiB: ( min: 10.094, 10.7 +- 0.3, max: 11.0294 ) MB/s

    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    fallocate file sized 512 MiB: ( min: 25.3301, 26.4 +- 0.9, max: 28.0807 ) GB/s
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    fallocate file sized 1 GiB: ( min: 47.3621, 49.1 +- 1.9, max: 52.9534 ) GB/s
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    fallocate file sized 2 GiB: ( min: 95.3893, 102 +- 4, max: 108.402 ) GB/s
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    fallocate file sized 4 GiB: ( min: 200.583, 212 +- 9, max: 222.018 ) GB/s

    ftruncate file sized 1 MiB: ( min: 46.6442, 49.0 +- 1.4, max: 51.7978 ) MB/s
    ftruncate file sized 4 MiB: ( min: 189.704, 196 +- 5, max: 203.289 ) MB/s
    ftruncate file sized 8 MiB: ( min: 381.715, 409 +- 15, max: 421.898 ) MB/s
    ftruncate file sized 512 MiB: ( min: 24.389, 25.6 +- 1.1, max: 27.9059 ) GB/s
    ftruncate file sized 1 GiB: ( min: 50.7639, 52.8 +- 1.7, max: 55.5697 ) GB/s
    ftruncate file sized 2 GiB: ( min: 96.4096, 99.4 +- 2.7, max: 104.393 ) GB/s
    ftruncate file sized 4 GiB: ( min: 189.401, 199 +- 4, max: 203.192 ) GB/s

## Mmap Write

Bus error

## Write into an emptied file

### Vectorized Writing

    writev 8 MiB into an emptied file in  1 KiB chunks (x128): ( min: 12.6677, 14.4 +- 1.2, max: 16.557 ) MB/s
    writev 8 MiB into an emptied file in  4 KiB chunks (x128): ( min: 13.1902, 14.4 +- 0.8, max: 15.5523 ) MB/s
    writev 8 MiB into an emptied file in 16 KiB chunks (x128): ( min: 13.0973, 14.0 +- 1.0, max: 16.1181 ) MB/s
    writev 8 MiB into an emptied file in 64 KiB chunks (x128): ( min: 13.3963, 14.3 +- 0.6, max: 15.2199 ) MB/s
    writev 8 MiB into an emptied file in  1 MiB chunks (x128): ( min: 12.7146, 13.9 +- 0.9, max: 15.6852 ) MB/s
    writev 8 MiB into an emptied file in  8 MiB chunks (x128): ( min: 13.1792, 14.2 +- 0.7, max: 15.7967 ) MB/s

    pwritev 8 MiB into an emptied file in  1 KiB chunks (x128): ( min: 12.8628, 14.1 +- 1.3, max: 17.5208 ) MB/s
    pwritev 8 MiB into an emptied file in  4 KiB chunks (x128): ( min: 12.5943, 13.7 +- 0.9, max: 15.2522 ) MB/s
    pwritev 8 MiB into an emptied file in 16 KiB chunks (x128): ( min: 13.1092, 14.0 +- 0.6, max: 14.9534 ) MB/s
    pwritev 8 MiB into an emptied file in 64 KiB chunks (x128): ( min: 13.6511, 14.7 +- 1.0, max: 16.6158 ) MB/s
    pwritev 8 MiB into an emptied file in  1 MiB chunks (x128): ( min: 13.0949, 14.4 +- 0.9, max: 15.9416 ) MB/s
    pwritev 8 MiB into an emptied file in  8 MiB chunks (x128): ( min: 12.9403, 13.8 +- 0.8, max: 15.0388 ) MB/s

### Parallel Writing

    Use pwrite to write 8 MiB into an emptied file using  1 threads: ( min: 13.3933, 14.3 +- 0.8, max: 15.2329 ) MB/s
    Use pwrite to write 8 MiB into an emptied file using  2 threads: ( min: 13.2425, 14.7 +- 0.9, max: 16.275 ) MB/s
    Use pwrite to write 8 MiB into an emptied file using  4 threads: ( min: 35.4458, 42 +- 9, max: 66.7752 ) MB/s
    Use pwrite to write 8 MiB into an emptied file using  8 threads: ( min: 14.5187, 34 +- 10, max: 45.4536 ) MB/s
    Use pwrite to write 8 MiB into an emptied file using 16 threads: ( min: 34.9735, 54 +- 12, max: 73.3856 ) MB/s

    Write 8 MiB into one file per thread using  1 threads: ( min: 8.00452, 8.9 +- 0.5, max: 9.85372 ) MB/s
    Write 8 MiB into one file per thread using  2 threads: ( min: 20.9214, 36 +- 16, max: 60.4318 ) MB/s
    Write 8 MiB into one file per thread using  4 threads: ( min: 52.2176, 76 +- 25, max: 133.66 ) MB/s
    Write 8 MiB into one file per thread using  8 threads: ( min: 133.13, 164 +- 21, max: 195.354 ) MB/s
    Write 8 MiB into one file per thread using 16 threads: ( min: 429.453, 436 +- 5, max: 446.01 ) MB/s

### Simple Writing

    fwrite 8 MiB into an emptied file in   1 KiB chunks: ( min: 12.6644, 13.9 +- 1.0, max: 16.3616 ) MB/s
    fwrite 8 MiB into an emptied file in   4 KiB chunks: ( min: 12.624, 14.4 +- 1.7, max: 17.3052 ) MB/s
    fwrite 8 MiB into an emptied file in   8 KiB chunks: ( min: 12.6739, 14.2 +- 0.8, max: 15.1773 ) MB/s
    fwrite 8 MiB into an emptied file in  16 KiB chunks: ( min: 13.0152, 14.2 +- 1.0, max: 15.9444 ) MB/s
    fwrite 8 MiB into an emptied file in  64 KiB chunks: ( min: 12.9416, 14.0 +- 1.4, max: 17.5491 ) MB/s
    fwrite 8 MiB into an emptied file in   1 MiB chunks: ( min: 12.613, 13.9 +- 1.1, max: 15.7377 ) MB/s
    fwrite 8 MiB into an emptied file in  16 MiB chunks: ( min: 12.7611, 14.3 +- 1.3, max: 15.8564 ) MB/s
    fwrite 8 MiB into an emptied file in  64 MiB chunks: ( min: 13.0322, 14.1 +- 0.9, max: 15.3809 ) MB/s
    fwrite 8 MiB into an emptied file in 512 MiB chunks: ( min: 12.6473, 15 +- 3, max: 23.7991 ) MB/s
    fwrite 8 MiB into an emptied file in   1 GiB chunks: ( min: 14.979, 18.0 +- 2.1, max: 20.7383 ) MB/s

    write 8 MiB into an emptied file in   1 KiB chunks: ( min: 8.31012, 9.1 +- 0.6, max: 9.85775 ) MB/s
    write 8 MiB into an emptied file in   4 KiB chunks: ( min: 13.0138, 14.3 +- 1.0, max: 16.1853 ) MB/s
    write 8 MiB into an emptied file in   8 KiB chunks: ( min: 13.3879, 14.7 +- 1.0, max: 16.5362 ) MB/s
    write 8 MiB into an emptied file in  16 KiB chunks: ( min: 12.3954, 13.9 +- 1.0, max: 15.8126 ) MB/s
    write 8 MiB into an emptied file in  64 KiB chunks: ( min: 12.8168, 14.5 +- 1.1, max: 16.3037 ) MB/s
    write 8 MiB into an emptied file in   1 MiB chunks: ( min: 13.9885, 16.2 +- 2.0, max: 20.95 ) MB/s
    write 8 MiB into an emptied file in  16 MiB chunks: ( min: 14.205, 16.2 +- 1.7, max: 18.7723 ) MB/s
    write 8 MiB into an emptied file in  64 MiB chunks: ( min: 13.0722, 15.1 +- 1.2, max: 17.0455 ) MB/s
    write 8 MiB into an emptied file in 512 MiB chunks: ( min: 13.6873, 14.7 +- 0.8, max: 15.8247 ) MB/s
    write 8 MiB into an emptied file in   1 GiB chunks: ( min: 12.8965, 15.5 +- 1.7, max: 18.7664 ) MB/s

## Write into a sparsely allocated file

### Vectorized Writing

    writev 8 MiB into a sparsely allocated file in  1 KiB chunks (x128): ( min: 13.2436, 14.4 +- 0.8, max: 15.8168 ) MB/s
    writev 8 MiB into a sparsely allocated file in  4 KiB chunks (x128): ( min: 13.6935, 14.5 +- 1.4, max: 18.2746 ) MB/s
    writev 8 MiB into a sparsely allocated file in 16 KiB chunks (x128): ( min: 13.8364, 15.5 +- 1.1, max: 17.1991 ) MB/s
    writev 8 MiB into a sparsely allocated file in 64 KiB chunks (x128): ( min: 14.2881, 15.8 +- 1.3, max: 18.5714 ) MB/s
    writev 8 MiB into a sparsely allocated file in  1 MiB chunks (x128): ( min: 13.2122, 15.4 +- 1.2, max: 17.4655 ) MB/s
    writev 8 MiB into a sparsely allocated file in  8 MiB chunks (x128): ( min: 14.034, 16.2 +- 1.6, max: 19.3718 ) MB/s

    pwritev 8 MiB into a sparsely allocated file in  1 KiB chunks (x128): ( min: 13.4295, 15.2 +- 1.7, max: 19.0183 ) MB/s
    pwritev 8 MiB into a sparsely allocated file in  4 KiB chunks (x128): ( min: 13.6187, 15.2 +- 1.0, max: 16.7872 ) MB/s

    [...]


# Test on HomePC with fusepy ramdisk

```bash
git clone https://github.com/fusepy/fusepy.git
python3 -m pip install --user fusepy
sed -i 's|allow_other=True|allow_other=False|; s|foreground=True|foreground=False|; s|logging.basicConfig|#logging.basicConfig|' ./examples/memory.py
python3 ./examples/memory.py /media/fuse-ramdisk/
sed -i ' ./examples/memory.py
```

Even slower than the perl ramdisk ... 200 kB/s or so allocation bandwidth ...


# Test on Taurus Romeo /dev/shm

```bash
srun -p romeo --ntasks=1 --cpus-per-task=16 --cpu-freq=2000000-2000000 --time=01:00:00 --mem-per-cpu=1972M --pty --x11 bash
module load CMake Ninja Clang NASM hwloc
cmake --build . -- benchmarkIOWrite && src/benchmarks/benchmarkIOWrite /dev/shm/mimi
```

## File Creation

    posix_fallocate file sized 512 MiB: ( min: 9.90548, 11.1 +- 0.4, max: 11.2968 ) GB/s
    posix_fallocate file sized 1 GiB: ( min: 10.5637, 10.70 +- 0.18, max: 11.1899 ) GB/s
    posix_fallocate file sized 2 GiB: ( min: 10.7414, 10.84 +- 0.05, max: 10.9226 ) GB/s
    posix_fallocate file sized 4 GiB: ( min: 10.9741, 11.04 +- 0.06, max: 11.1706 ) GB/s

    fallocate file sized 512 MiB: ( min: 10.2082, 11.1 +- 0.3, max: 11.3262 ) GB/s
    fallocate file sized 1 GiB: ( min: 10.5157, 10.67 +- 0.16, max: 11.093 ) GB/s
    fallocate file sized 2 GiB: ( min: 10.805, 10.86 +- 0.05, max: 10.9495 ) GB/s
    fallocate file sized 4 GiB: ( min: 10.913, 11.04 +- 0.07, max: 11.1715 ) GB/s

    ftruncate file sized 512 MiB: ( min: 5097.57, 200000 +- 70000, max: 259483 ) GB/s
    ftruncate file sized 1 GiB: ( min: 271147, 460000 +- 80000, max: 531555 ) GB/s
    ftruncate file sized 2 GiB: ( min: 660764, 960000 +- 120000, max: 1.0684e+06 ) GB/s
    ftruncate file sized 4 GiB: ( min: 1.45592e+06, 1880000 +- 240000, max: 2.1368e+06 ) GB/s

## Mmap Write

    ftruncate + mmap write 1 GiB: ( min: 2.00815, 2.017 +- 0.006, max: 2.02408 ) GB/s
    ftruncate + mmap write 1 GiB using  1 threads: ( min: 1.66444, 1.70 +- 0.05, max: 1.8259 ) GB/s
    ftruncate + mmap write 1 GiB using  2 threads: ( min: 2.35177, 2.44 +- 0.11, max: 2.74224 ) GB/s
    ftruncate + mmap write 1 GiB using  4 threads: ( min: 3.56526, 3.74 +- 0.13, max: 4.02589 ) GB/s
    ftruncate + mmap write 1 GiB using  8 threads: ( min: 4.55801, 4.62 +- 0.06, max: 4.74242 ) GB/s
    ftruncate + mmap write 1 GiB using 16 threads: ( min: 4.62584, 4.73 +- 0.04, max: 4.78146 ) GB/s

## Write into an emptied file

### Vectorized Writing

    writev 1 GiB into an emptied file in  1 KiB chunks (x128): ( min: 3.17901, 3.187 +- 0.005, max: 3.19561 ) GB/s
    writev 1 GiB into an emptied file in  4 KiB chunks (x128): ( min: 3.33004, 3.343 +- 0.008, max: 3.3549 ) GB/s
    writev 1 GiB into an emptied file in 16 KiB chunks (x128): ( min: 3.35692, 3.364 +- 0.005, max: 3.37168 ) GB/s
    writev 1 GiB into an emptied file in 64 KiB chunks (x128): ( min: 3.36411, 3.372 +- 0.005, max: 3.37935 ) GB/s
    writev 1 GiB into an emptied file in  1 MiB chunks (x128): ( min: 3.35992, 3.369 +- 0.007, max: 3.38061 ) GB/s
    writev 1 GiB into an emptied file in  8 MiB chunks (x128): ( min: 3.35646, 3.368 +- 0.006, max: 3.3731 ) GB/s

    pwritev 1 GiB into an emptied file in  1 KiB chunks (x128): ( min: 3.16432, 3.171 +- 0.005, max: 3.18162 ) GB/s
    pwritev 1 GiB into an emptied file in  4 KiB chunks (x128): ( min: 3.32543, 3.332 +- 0.004, max: 3.33991 ) GB/s
    pwritev 1 GiB into an emptied file in 16 KiB chunks (x128): ( min: 3.33858, 3.348 +- 0.008, max: 3.35988 ) GB/s
    pwritev 1 GiB into an emptied file in 64 KiB chunks (x128): ( min: 3.32999, 3.339 +- 0.006, max: 3.3476 ) GB/s
    pwritev 1 GiB into an emptied file in  1 MiB chunks (x128): ( min: 3.33035, 3.340 +- 0.005, max: 3.34733 ) GB/s
    pwritev 1 GiB into an emptied file in  8 MiB chunks (x128): ( min: 3.32206, 3.334 +- 0.009, max: 3.34334 ) GB/s

### Parallel Writing

    Use pwrite to write 1 GiB into an emptied file using  1 threads: ( min: 3.47043, 3.476 +- 0.006, max: 3.48874 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  2 threads: ( min: 3.45981, 3.468 +- 0.007, max: 3.4777 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  4 threads: ( min: 3.45484, 3.467 +- 0.007, max: 3.47744 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  8 threads: ( min: 3.43814, 3.449 +- 0.008, max: 3.46341 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using 16 threads: ( min: 3.42642, 3.440 +- 0.009, max: 3.4493 ) GB/s

### Simple Writing

    fwrite 1 GiB into an emptied file in   1 KiB chunks: ( min: 2.58974, 2.608 +- 0.008, max: 2.61819 ) GB/s
    fwrite 1 GiB into an emptied file in   4 KiB chunks: ( min: 2.72864, 2.741 +- 0.006, max: 2.74646 ) GB/s
    fwrite 1 GiB into an emptied file in   8 KiB chunks: ( min: 2.73311, 2.737 +- 0.004, max: 2.74432 ) GB/s
    fwrite 1 GiB into an emptied file in  16 KiB chunks: ( min: 2.96453, 2.976 +- 0.019, max: 3.01567 ) GB/s
    fwrite 1 GiB into an emptied file in  64 KiB chunks: ( min: 3.17096, 3.199 +- 0.013, max: 3.21091 ) GB/s
    fwrite 1 GiB into an emptied file in   1 MiB chunks: ( min: 3.28325, 3.290 +- 0.004, max: 3.29653 ) GB/s
    fwrite 1 GiB into an emptied file in  16 MiB chunks: ( min: 3.27094, 3.285 +- 0.008, max: 3.29363 ) GB/s
    fwrite 1 GiB into an emptied file in  64 MiB chunks: ( min: 1.87029, 3.2 +- 0.5, max: 3.34322 ) GB/s
    fwrite 1 GiB into an emptied file in 512 MiB chunks: ( min: 3.33542, 3.3400 +- 0.0023, max: 3.34386 ) GB/s
    fwrite 1 GiB into an emptied file in   1 GiB chunks: ( min: 3.33516, 3.3410 +- 0.0030, max: 3.34679 ) GB/s

    write 1 GiB into an emptied file in   1 KiB chunks: ( min: 1.50553, 1.5083 +- 0.0021, max: 1.51163 ) GB/s
    write 1 GiB into an emptied file in   4 KiB chunks: ( min: 2.79801, 2.806 +- 0.006, max: 2.81607 ) GB/s
    write 1 GiB into an emptied file in   8 KiB chunks: ( min: 3.00874, 3.021 +- 0.008, max: 3.03071 ) GB/s
    write 1 GiB into an emptied file in  16 KiB chunks: ( min: 3.1674, 3.1708 +- 0.0022, max: 3.17548 ) GB/s
    write 1 GiB into an emptied file in  64 KiB chunks: ( min: 3.28842, 3.295 +- 0.004, max: 3.30153 ) GB/s
    write 1 GiB into an emptied file in   1 MiB chunks: ( min: 3.32575, 3.3284 +- 0.0028, max: 3.33528 ) GB/s
    write 1 GiB into an emptied file in  16 MiB chunks: ( min: 3.31714, 3.324 +- 0.005, max: 3.33392 ) GB/s
    write 1 GiB into an emptied file in  64 MiB chunks: ( min: 3.3219, 3.3267 +- 0.0025, max: 3.33001 ) GB/s
    write 1 GiB into an emptied file in 512 MiB chunks: ( min: 3.32246, 3.327 +- 0.004, max: 3.33612 ) GB/s
    write 1 GiB into an emptied file in   1 GiB chunks: ( min: 3.32104, 3.326 +- 0.005, max: 3.33929 ) GB/s

## Write into a preallocated file

### Vectorized Writing

    writev 1 GiB into a preallocated file in  1 KiB chunks (x128): ( min: 4.11131, 4.149 +- 0.020, max: 4.17035 ) GB/s
    writev 1 GiB into a preallocated file in  4 KiB chunks (x128): ( min: 4.483, 4.501 +- 0.012, max: 4.51993 ) GB/s
    writev 1 GiB into a preallocated file in 16 KiB chunks (x128): ( min: 4.53156, 4.540 +- 0.008, max: 4.55453 ) GB/s
    writev 1 GiB into a preallocated file in 64 KiB chunks (x128): ( min: 4.46958, 4.535 +- 0.025, max: 4.55792 ) GB/s
    writev 1 GiB into a preallocated file in  1 MiB chunks (x128): ( min: 4.45243, 4.495 +- 0.019, max: 4.51383 ) GB/s
    writev 1 GiB into a preallocated file in  8 MiB chunks (x128): ( min: 4.51031, 4.523 +- 0.009, max: 4.53804 ) GB/s

    pwritev 1 GiB into a preallocated file in  1 KiB chunks (x128): ( min: 4.14255, 4.151 +- 0.005, max: 4.1565 ) GB/s
    pwritev 1 GiB into a preallocated file in  4 KiB chunks (x128): ( min: 4.48011, 4.493 +- 0.007, max: 4.50359 ) GB/s
    pwritev 1 GiB into a preallocated file in 16 KiB chunks (x128): ( min: 4.53064, 4.541 +- 0.006, max: 4.55076 ) GB/s
    pwritev 1 GiB into a preallocated file in 64 KiB chunks (x128): ( min: 4.54346, 4.548 +- 0.006, max: 4.56238 ) GB/s
    pwritev 1 GiB into a preallocated file in  1 MiB chunks (x128): ( min: 4.4491, 4.54 +- 0.03, max: 4.55927 ) GB/s
    pwritev 1 GiB into a preallocated file in  8 MiB chunks (x128): ( min: 4.52511, 4.556 +- 0.013, max: 4.5705 ) GB/s

### Parallel Writing

    Use pwrite to write 1 GiB into a preallocated file using  1 threads: ( min: 4.78466, 4.824 +- 0.018, max: 4.84607 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  2 threads: ( min: 4.77028, 4.799 +- 0.015, max: 4.81773 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  4 threads: ( min: 4.72353, 4.759 +- 0.018, max: 4.78184 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  8 threads: ( min: 4.72563, 4.748 +- 0.016, max: 4.77519 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using 16 threads: ( min: 4.6901, 4.709 +- 0.016, max: 4.7366 ) GB/s

### Simple Writing

    fwrite 1 GiB into a preallocated file in   1 KiB chunks: ( min: 3.24207, 3.270 +- 0.012, max: 3.28091 ) GB/s
    fwrite 1 GiB into a preallocated file in   4 KiB chunks: ( min: 3.50177, 3.540 +- 0.016, max: 3.5556 ) GB/s
    fwrite 1 GiB into a preallocated file in   8 KiB chunks: ( min: 3.5317, 3.539 +- 0.004, max: 3.54576 ) GB/s
    fwrite 1 GiB into a preallocated file in  16 KiB chunks: ( min: 3.94118, 3.986 +- 0.017, max: 3.99967 ) GB/s
    fwrite 1 GiB into a preallocated file in  64 KiB chunks: ( min: 4.36013, 4.398 +- 0.016, max: 4.40899 ) GB/s
    fwrite 1 GiB into a preallocated file in   1 MiB chunks: ( min: 4.54959, 4.564 +- 0.010, max: 4.57787 ) GB/s
    fwrite 1 GiB into a preallocated file in  16 MiB chunks: ( min: 4.513, 4.530 +- 0.012, max: 4.54714 ) GB/s
    fwrite 1 GiB into a preallocated file in  64 MiB chunks: ( min: 4.53441, 4.554 +- 0.012, max: 4.56944 ) GB/s
    fwrite 1 GiB into a preallocated file in 512 MiB chunks: ( min: 4.54045, 4.557 +- 0.008, max: 4.56657 ) GB/s
    fwrite 1 GiB into a preallocated file in   1 GiB chunks: ( min: 4.55372, 4.566 +- 0.009, max: 4.57778 ) GB/s

    write 1 GiB into a preallocated file in   1 KiB chunks: ( min: 1.85652, 1.874 +- 0.010, max: 1.88269 ) GB/s
    write 1 GiB into a preallocated file in   4 KiB chunks: ( min: 3.5975, 3.668 +- 0.026, max: 3.68509 ) GB/s
    write 1 GiB into a preallocated file in   8 KiB chunks: ( min: 4.07922, 4.099 +- 0.011, max: 4.1135 ) GB/s
    write 1 GiB into a preallocated file in  16 KiB chunks: ( min: 4.21818, 4.240 +- 0.015, max: 4.25567 ) GB/s
    write 1 GiB into a preallocated file in  64 KiB chunks: ( min: 4.42673, 4.477 +- 0.023, max: 4.50116 ) GB/s
    write 1 GiB into a preallocated file in   1 MiB chunks: ( min: 4.46033, 4.519 +- 0.023, max: 4.54031 ) GB/s
    write 1 GiB into a preallocated file in  16 MiB chunks: ( min: 4.50367, 4.529 +- 0.010, max: 4.53975 ) GB/s
    write 1 GiB into a preallocated file in  64 MiB chunks: ( min: 4.50704, 4.526 +- 0.012, max: 4.53946 ) GB/s
    write 1 GiB into a preallocated file in 512 MiB chunks: ( min: 4.51166, 4.535 +- 0.010, max: 4.55107 ) GB/s
    write 1 GiB into a preallocated file in   1 GiB chunks: ( min: 4.53889, 4.548 +- 0.004, max: 4.55163 ) GB/s


# Test on Taurus Romeo BeeGFS

    srun -p romeo --ntasks=1 --cpus-per-task=16 --cpu-freq=2000000-2000000 --time=01:00:00 --mem-per-cpu=1972M --pty --x11 bash
    module load CMake Ninja Clang NASM hwloc
    ws_allocate --duration 1 --filesystem beegfs io-write-benchmark
    cmake --build . -- benchmarkIOWrite && src/benchmarks/benchmarkIOWrite "$BEEGFSWS/mimi"


## File Creation

    posix_fallocate file sized 512 MiB: ( min: 0.0821209, 0.098 +- 0.006, max: 0.104755 ) GB/s
    posix_fallocate file sized 1 GiB: ( min: 0.0900766, 0.097 +- 0.006, max: 0.10741 ) GB/s
    posix_fallocate file sized 2 GiB: ( min: 0.0860204, 0.094 +- 0.008, max: 0.106395 ) GB/s
    posix_fallocate file sized 4 GiB: ( min: 0.0880936, 0.093 +- 0.004, max: 0.0993601 ) GB/s

    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    fallocate file sized 512 MiB: ( min: 672.678, 1300 +- 400, max: 1811.45 ) GB/s
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    fallocate file sized 1 GiB: ( min: 2242.69, 3200 +- 500, max: 3842.26 ) GB/s
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    fallocate file sized 2 GiB: ( min: 5117.02, 6700 +- 700, max: 7257.56 ) GB/s
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    fallocate file sized 4 GiB: ( min: 9212.22, 12700 +- 1900, max: 15580.8 ) GB/s

    ftruncate file sized 512 MiB: ( min: 294.407, 430 +- 80, max: 586.86 ) GB/s
    ftruncate file sized 1 GiB: ( min: 656.113, 850 +- 70, max: 917.339 ) GB/s
    ftruncate file sized 2 GiB: ( min: 591.297, 1600 +- 400, max: 1892.03 ) GB/s
    ftruncate file sized 4 GiB: ( min: 577.606, 2400 +- 1200, max: 3551.48 ) GB/s

## Mmap Write

    ftruncate + mmap write 1 GiB: ( min: 0.68387, 0.757 +- 0.028, max: 0.789559 ) GB/s
    ftruncate + mmap write 1 GiB using  1 threads: ( min: 0.692692, 0.740 +- 0.028, max: 0.770258 ) GB/s
    ftruncate + mmap write 1 GiB using  2 threads: ( min: 1.34176, 1.40 +- 0.05, max: 1.46161 ) GB/s
    ftruncate + mmap write 1 GiB using  4 threads: ( min: 2.3137, 2.44 +- 0.09, max: 2.56388 ) GB/s
    ftruncate + mmap write 1 GiB using  8 threads: ( min: 2.63801, 3.3 +- 0.4, max: 3.70552 ) GB/s
    ftruncate + mmap write 1 GiB using 16 threads: ( min: 3.30606, 3.7 +- 0.3, max: 4.40742 ) GB/s

## Write into an emptied file

### Vectorized Writing

    writev 1 GiB into an emptied file in  1 KiB chunks (x128): ( min: 0.862277, 0.95 +- 0.03, max: 0.971602 ) GB/s
    writev 1 GiB into an emptied file in  4 KiB chunks (x128): ( min: 0.96226, 0.982 +- 0.012, max: 0.999512 ) GB/s
    writev 1 GiB into an emptied file in 16 KiB chunks (x128): ( min: 1.00514, 1.017 +- 0.007, max: 1.02882 ) GB/s
    writev 1 GiB into an emptied file in 64 KiB chunks (x128): ( min: 0.995384, 1.007 +- 0.008, max: 1.02088 ) GB/s
    writev 1 GiB into an emptied file in  1 MiB chunks (x128): ( min: 1.52526, 1.536 +- 0.007, max: 1.54326 ) GB/s
    writev 1 GiB into an emptied file in  8 MiB chunks (x128): ( min: 3.17459, 3.190 +- 0.009, max: 3.202 ) GB/s

    pwritev 1 GiB into an emptied file in  1 KiB chunks (x128): ( min: 0.987491, 1.007 +- 0.016, max: 1.03676 ) GB/s
    pwritev 1 GiB into an emptied file in  4 KiB chunks (x128): ( min: 0.979955, 0.991 +- 0.008, max: 1.0031 ) GB/s
    pwritev 1 GiB into an emptied file in 16 KiB chunks (x128): ( min: 0.693792, 0.98 +- 0.10, max: 1.02183 ) GB/s
    pwritev 1 GiB into an emptied file in 64 KiB chunks (x128): ( min: 0.995241, 1.003 +- 0.006, max: 1.01188 ) GB/s
    pwritev 1 GiB into an emptied file in  1 MiB chunks (x128): ( min: 1.52202, 1.534 +- 0.008, max: 1.54637 ) GB/s
    pwritev 1 GiB into an emptied file in  8 MiB chunks (x128): ( min: 3.1528, 3.178 +- 0.018, max: 3.20324 ) GB/s

### Parallel Writing

    Use pwrite to write 1 GiB into an emptied file using  1 threads: ( min: 3.16267, 3.20 +- 0.03, max: 3.2424 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  2 threads: ( min: 3.05424, 3.17 +- 0.06, max: 3.23712 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  4 threads: ( min: 2.84718, 3.13 +- 0.14, max: 3.2283 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  8 threads: ( min: 2.854, 3.00 +- 0.09, max: 3.18987 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using 16 threads: ( min: 2.84625, 2.98 +- 0.07, max: 3.08657 ) GB/s

### Simple Writing

    fwrite 1 GiB into an emptied file in   1 KiB chunks: ( min: 0.953338, 0.968 +- 0.009, max: 0.978514 ) GB/s
    fwrite 1 GiB into an emptied file in   4 KiB chunks: ( min: 0.962424, 0.976 +- 0.008, max: 0.988676 ) GB/s
    fwrite 1 GiB into an emptied file in   8 KiB chunks: ( min: 0.984835, 0.993 +- 0.005, max: 0.999316 ) GB/s
    fwrite 1 GiB into an emptied file in  16 KiB chunks: ( min: 0.986457, 0.993 +- 0.005, max: 1.00141 ) GB/s
    fwrite 1 GiB into an emptied file in  64 KiB chunks: ( min: 0.981478, 0.992 +- 0.008, max: 1.00513 ) GB/s
    fwrite 1 GiB into an emptied file in   1 MiB chunks: ( min: 1.38065, 1.421 +- 0.023, max: 1.45235 ) GB/s
    fwrite 1 GiB into an emptied file in  16 MiB chunks: ( min: 3.13779, 3.162 +- 0.015, max: 3.18301 ) GB/s
    fwrite 1 GiB into an emptied file in  64 MiB chunks: ( min: 3.16605, 3.196 +- 0.014, max: 3.21528 ) GB/s
    fwrite 1 GiB into an emptied file in 512 MiB chunks: ( min: 3.14083, 3.19 +- 0.03, max: 3.2446 ) GB/s
    fwrite 1 GiB into an emptied file in   1 GiB chunks: ( min: 3.11903, 3.167 +- 0.028, max: 3.19909 ) GB/s

    write 1 GiB into an emptied file in   1 KiB chunks: ( min: 0.859209, 0.882 +- 0.013, max: 0.89704 ) GB/s
    write 1 GiB into an emptied file in   4 KiB chunks: ( min: 0.950591, 0.961 +- 0.007, max: 0.974691 ) GB/s
    write 1 GiB into an emptied file in   8 KiB chunks: ( min: 0.98024, 0.991 +- 0.008, max: 1.00496 ) GB/s
    write 1 GiB into an emptied file in  16 KiB chunks: ( min: 0.99765, 1.006 +- 0.006, max: 1.01389 ) GB/s
    write 1 GiB into an emptied file in  64 KiB chunks: ( min: 0.984066, 0.996 +- 0.007, max: 1.00589 ) GB/s
    write 1 GiB into an emptied file in   1 MiB chunks: ( min: 1.50833, 1.545 +- 0.018, max: 1.56533 ) GB/s
    write 1 GiB into an emptied file in  16 MiB chunks: ( min: 3.16342, 3.185 +- 0.014, max: 3.20858 ) GB/s
    write 1 GiB into an emptied file in  64 MiB chunks: ( min: 3.08856, 3.19 +- 0.04, max: 3.24178 ) GB/s
    write 1 GiB into an emptied file in 512 MiB chunks: ( min: 3.15452, 3.188 +- 0.017, max: 3.21638 ) GB/s
    write 1 GiB into an emptied file in   1 GiB chunks: ( min: 3.09668, 3.17 +- 0.04, max: 3.20522 ) GB/s

## Write into a preallocated file

### Vectorized Writing

    writev 1 GiB into a preallocated file in  1 KiB chunks (x128): ( min: 0.98567, 1.15 +- 0.07, max: 1.22399 ) GB/s
    writev 1 GiB into a preallocated file in  4 KiB chunks (x128): ( min: 1.15234, 1.25 +- 0.09, max: 1.3647 ) GB/s
    writev 1 GiB into a preallocated file in 16 KiB chunks (x128): ( min: 1.21977, 1.30 +- 0.08, max: 1.4482 ) GB/s
    writev 1 GiB into a preallocated file in 64 KiB chunks (x128): ( min: 1.2313, 1.30 +- 0.06, max: 1.40352 ) GB/s
    writev 1 GiB into a preallocated file in  1 MiB chunks (x128): ( min: 2.07989, 2.102 +- 0.015, max: 2.12648 ) GB/s
    writev 1 GiB into a preallocated file in  8 MiB chunks (x128): ( min: 4.36935, 4.43 +- 0.04, max: 4.49219 ) GB/s

    pwritev 1 GiB into a preallocated file in  1 KiB chunks (x128): ( min: 1.03225, 1.12 +- 0.06, max: 1.22163 ) GB/s
    pwritev 1 GiB into a preallocated file in  4 KiB chunks (x128): ( min: 1.18269, 1.24 +- 0.05, max: 1.3135 ) GB/s
    pwritev 1 GiB into a preallocated file in 16 KiB chunks (x128): ( min: 1.22951, 1.29 +- 0.06, max: 1.41325 ) GB/s
    pwritev 1 GiB into a preallocated file in 64 KiB chunks (x128): ( min: 1.21417, 1.29 +- 0.08, max: 1.42046 ) GB/s
    pwritev 1 GiB into a preallocated file in  1 MiB chunks (x128): ( min: 2.05628, 2.104 +- 0.029, max: 2.13421 ) GB/s
    pwritev 1 GiB into a preallocated file in  8 MiB chunks (x128): ( min: 4.4053, 4.455 +- 0.027, max: 4.4889 ) GB/s

### Parallel Writing

    Use pwrite to write 1 GiB into a preallocated file using  1 threads: ( min: 3.92743, 4.39 +- 0.21, max: 4.54088 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  2 threads: ( min: 4.4648, 4.55 +- 0.05, max: 4.60185 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  4 threads: ( min: 4.44014, 4.489 +- 0.027, max: 4.53465 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  8 threads: ( min: 4.42279, 4.48 +- 0.05, max: 4.5543 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using 16 threads: ( min: 4.33694, 4.41 +- 0.05, max: 4.47872 ) GB/s

### Simple Writing

    fwrite 1 GiB into a preallocated file in   1 KiB chunks: ( min: 0.99721, 1.045 +- 0.028, max: 1.07739 ) GB/s
    fwrite 1 GiB into a preallocated file in   4 KiB chunks: ( min: 1.0768, 1.16 +- 0.06, max: 1.24687 ) GB/s
    fwrite 1 GiB into a preallocated file in   8 KiB chunks: ( min: 1.17215, 1.21 +- 0.04, max: 1.27838 ) GB/s
    fwrite 1 GiB into a preallocated file in  16 KiB chunks: ( min: 1.13493, 1.22 +- 0.05, max: 1.2965 ) GB/s
    fwrite 1 GiB into a preallocated file in  64 KiB chunks: ( min: 1.17874, 1.25 +- 0.06, max: 1.35519 ) GB/s
    fwrite 1 GiB into a preallocated file in   1 MiB chunks: ( min: 1.88209, 1.914 +- 0.021, max: 1.93899 ) GB/s
    fwrite 1 GiB into a preallocated file in  16 MiB chunks: ( min: 4.25032, 4.31 +- 0.04, max: 4.36116 ) GB/s
    fwrite 1 GiB into a preallocated file in  64 MiB chunks: ( min: 3.95681, 4.22 +- 0.15, max: 4.33556 ) GB/s
    fwrite 1 GiB into a preallocated file in 512 MiB chunks: ( min: 2.30427, 4.2 +- 0.7, max: 4.43517 ) GB/s
    fwrite 1 GiB into a preallocated file in   1 GiB chunks: ( min: 4.22413, 4.30 +- 0.04, max: 4.34379 ) GB/s

    write 1 GiB into a preallocated file in   1 KiB chunks: ( min: 0.854546, 0.92 +- 0.05, max: 0.985307 ) GB/s
    write 1 GiB into a preallocated file in   4 KiB chunks: ( min: 1.04968, 1.081 +- 0.019, max: 1.11493 ) GB/s
    write 1 GiB into a preallocated file in   8 KiB chunks: ( min: 1.13547, 1.159 +- 0.014, max: 1.18101 ) GB/s
    write 1 GiB into a preallocated file in  16 KiB chunks: ( min: 1.13301, 1.159 +- 0.011, max: 1.17557 ) GB/s
    write 1 GiB into a preallocated file in  64 KiB chunks: ( min: 1.13055, 1.149 +- 0.014, max: 1.16997 ) GB/s
    write 1 GiB into a preallocated file in   1 MiB chunks: ( min: 1.99341, 2.08 +- 0.03, max: 2.11972 ) GB/s
    write 1 GiB into a preallocated file in  16 MiB chunks: ( min: 4.27261, 4.316 +- 0.030, max: 4.36314 ) GB/s
    write 1 GiB into a preallocated file in  64 MiB chunks: ( min: 4.12652, 4.24 +- 0.06, max: 4.30356 ) GB/s
    write 1 GiB into a preallocated file in 512 MiB chunks: ( min: 4.2631, 4.32 +- 0.03, max: 4.37141 ) GB/s
    write 1 GiB into a preallocated file in   1 GiB chunks: ( min: 4.25994, 4.318 +- 0.028, max: 4.35635 ) GB/s


# Test on Taurus Romeo BeeGFS with 1 MiB striping and 64 targets

    beegfs-ctl --getentryinfo "$BEEGFSWS/mimi" --mount=/beegfs/global0

        Entry type: file
        EntryID: 1-63AADBC5-1
        Metadata node: taurusnvme2-0 [ID: 1]
        Stripe pattern details:
        + Type: RAID0
        + Chunksize: 2M
        + Number of storage targets: desired: 2; actual: 2
        + Storage targets:
          + 5 @ taurusnvme10-1 [ID: 20]
          + 4 @ taurusnvme10-0 [ID: 19]

    beegfs-ctl --listtargets --nodetype=storage --spaceinfo --longnodes --state --mount=/beegfs/global0

        TargetID     Reachability  Consistency        Total         Free    %      ITotal       IFree    %   NodeID
        ========     ============  ===========        =====         ====    =      ======       =====    =   ======
               1           Online         Good    2979.4GiB     286.6GiB  10%      156.3M      154.7M  99%   beegfs-storage taurusnvme10-0 [ID: 19]
               2           Online         Good    2979.4GiB      20.0GiB   1%       22.9M       21.7M  95%   beegfs-storage taurusnvme10-0 [ID: 19]
               3           Online         Good    2979.4GiB      15.2GiB   1%       18.0M       16.8M  94%   beegfs-storage taurusnvme10-0 [ID: 19]
        [...]
              92           Online         Good    2979.4GiB     915.1GiB  31%      312.6M      311.6M 100%   beegfs-storage taurusnvme23-1 [ID: 24]
              93           Online         Good    2979.4GiB     925.3GiB  31%      312.6M      311.6M 100%   beegfs-storage taurusnvme23-1 [ID: 24]
              94           Online         Good    2979.4GiB      19.5GiB   1%       41.4M       40.9M  99%   beegfs-storage taurusnvme23-1 [ID: 24]

    beegfs-ctl --setpattern --chunksize=1m --numtargets=64 "$BEEGFSWS" --mount=/beegfs/global0


# File Creation

    posix_fallocate file sized 512 MiB: ( min: 0.070798, 0.0746 +- 0.0026, max: 0.0780938 ) GB/s
    posix_fallocate file sized 1 GiB: ( min: 0.0672955, 0.0693 +- 0.0014, max: 0.071785 ) GB/s
    ^C

    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    fallocate file sized 512 MiB: ( min: 283.452, 900 +- 300, max: 1237.81 ) GB/s
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    fallocate file sized 1 GiB: ( min: 2313.13, 2600 +- 160, max: 2846.26 ) GB/s
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    fallocate file sized 2 GiB: ( min: 4731.24, 5240 +- 270, max: 5569.99 ) GB/s
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    Encountered error while calling fallocate on file: Operation not supported (95)
    fallocate file sized 4 GiB: ( min: 9006.88, 10100 +- 500, max: 10928.5 ) GB/s

    ftruncate file sized 512 MiB: ( min: 213.876, 235 +- 14, max: 258.228 ) GB/s
    ftruncate file sized 1 GiB: ( min: 469.331, 495 +- 22, max: 535.373 ) GB/s
    ftruncate file sized 2 GiB: ( min: 982.01, 1030 +- 30, max: 1093.64 ) GB/s
    ftruncate file sized 4 GiB: ( min: 1926.17, 2080 +- 80, max: 2250.99 ) GB/s

# Mmap Write

    ftruncate + mmap write 1 GiB: ( min: 0.546038, 0.555 +- 0.007, max: 0.567933 ) GB/s
    ftruncate + mmap write 1 GiB using  1 threads: ( min: 0.535991, 0.544 +- 0.005, max: 0.551373 ) GB/s
    ftruncate + mmap write 1 GiB using  2 threads: ( min: 0.833444, 0.97 +- 0.05, max: 1.01174 ) GB/s
    ftruncate + mmap write 1 GiB using  4 threads: ( min: 1.36527, 1.67 +- 0.15, max: 1.81139 ) GB/s
    ftruncate + mmap write 1 GiB using  8 threads: ( min: 2.36004, 2.67 +- 0.21, max: 2.95807 ) GB/s
    ftruncate + mmap write 1 GiB using 16 threads: ( min: 2.86923, 3.27 +- 0.27, max: 3.67242 ) GB/s

# Write into an emptied file

## Vectorized Writing

    writev 1 GiB into an emptied file in  1 KiB chunks (x128): ( min: 0.741576, 0.752 +- 0.005, max: 0.758639 ) GB/s
    writev 1 GiB into an emptied file in  4 KiB chunks (x128): ( min: 0.773663, 0.790 +- 0.014, max: 0.825966 ) GB/s
    writev 1 GiB into an emptied file in 16 KiB chunks (x128): ( min: 0.80828, 0.84 +- 0.03, max: 0.903099 ) GB/s
    writev 1 GiB into an emptied file in 64 KiB chunks (x128): ( min: 0.842118, 0.857 +- 0.019, max: 0.902332 ) GB/s
    writev 1 GiB into an emptied file in  1 MiB chunks (x128): ( min: 1.0676, 1.093 +- 0.022, max: 1.13279 ) GB/s
    writev 1 GiB into an emptied file in  8 MiB chunks (x128): ( min: 2.99817, 3.07 +- 0.06, max: 3.1699 ) GB/s

    pwritev 1 GiB into an emptied file in  1 KiB chunks (x128): ( min: 0.780956, 0.794 +- 0.010, max: 0.814196 ) GB/s
    pwritev 1 GiB into an emptied file in  4 KiB chunks (x128): ( min: 0.830693, 0.847 +- 0.011, max: 0.86012 ) GB/s
    pwritev 1 GiB into an emptied file in 16 KiB chunks (x128): ( min: 0.847757, 0.868 +- 0.014, max: 0.890096 ) GB/s
    pwritev 1 GiB into an emptied file in 64 KiB chunks (x128): ( min: 0.855784, 0.870 +- 0.011, max: 0.886999 ) GB/s
    pwritev 1 GiB into an emptied file in  1 MiB chunks (x128): ( min: 1.06918, 1.093 +- 0.026, max: 1.159 ) GB/s
    pwritev 1 GiB into an emptied file in  8 MiB chunks (x128): ( min: 3.08915, 3.30 +- 0.11, max: 3.41964 ) GB/s

## Parallel Writing

    Use pwrite to write 1 GiB into an emptied file using  1 threads: ( min: 3.05305, 3.46 +- 0.18, max: 3.57315 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  2 threads: ( min: 3.058, 3.41 +- 0.19, max: 3.54627 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  4 threads: ( min: 3.04597, 3.38 +- 0.16, max: 3.52173 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using  8 threads: ( min: 3.00675, 3.32 +- 0.15, max: 3.43962 ) GB/s
    Use pwrite to write 1 GiB into an emptied file using 16 threads: ( min: 2.97259, 3.29 +- 0.17, max: 3.40323 ) GB/s

## Simple Writing

    fwrite 1 GiB into an emptied file in   1 KiB chunks: ( min: 0.758541, 0.782 +- 0.021, max: 0.821806 ) GB/s
    fwrite 1 GiB into an emptied file in   4 KiB chunks: ( min: 0.793491, 0.816 +- 0.018, max: 0.840451 ) GB/s
    fwrite 1 GiB into an emptied file in   8 KiB chunks: ( min: 0.804017, 0.816 +- 0.009, max: 0.825414 ) GB/s
    fwrite 1 GiB into an emptied file in  16 KiB chunks: ( min: 0.805736, 0.823 +- 0.015, max: 0.861495 ) GB/s
    fwrite 1 GiB into an emptied file in  64 KiB chunks: ( min: 0.822271, 0.845 +- 0.013, max: 0.85884 ) GB/s
    fwrite 1 GiB into an emptied file in   1 MiB chunks: ( min: 0.955136, 0.978 +- 0.019, max: 1.01374 ) GB/s
    fwrite 1 GiB into an emptied file in  16 MiB chunks: ( min: 2.73464, 3.00 +- 0.24, max: 3.29799 ) GB/s
    fwrite 1 GiB into an emptied file in  64 MiB chunks: ( min: 2.68745, 2.93 +- 0.22, max: 3.38457 ) GB/s
    fwrite 1 GiB into an emptied file in 512 MiB chunks: ( min: 2.65781, 2.95 +- 0.26, max: 3.38493 ) GB/s
    fwrite 1 GiB into an emptied file in   1 GiB chunks: ( min: 2.71047, 3.01 +- 0.28, max: 3.38674 ) GB/s

    write 1 GiB into an emptied file in   1 KiB chunks: ( min: 0.677804, 0.691 +- 0.013, max: 0.723798 ) GB/s
    write 1 GiB into an emptied file in   4 KiB chunks: ( min: 0.769238, 0.787 +- 0.013, max: 0.813792 ) GB/s
    write 1 GiB into an emptied file in   8 KiB chunks: ( min: 0.706594, 0.81 +- 0.04, max: 0.859818 ) GB/s
    write 1 GiB into an emptied file in  16 KiB chunks: ( min: 0.789176, 0.815 +- 0.018, max: 0.846279 ) GB/s
    write 1 GiB into an emptied file in  64 KiB chunks: ( min: 0.792962, 0.805 +- 0.010, max: 0.819659 ) GB/s
    write 1 GiB into an emptied file in   1 MiB chunks: ( min: 1.06367, 1.093 +- 0.015, max: 1.10866 ) GB/s
    write 1 GiB into an emptied file in  16 MiB chunks: ( min: 2.53606, 2.571 +- 0.026, max: 2.61336 ) GB/s
    write 1 GiB into an emptied file in  64 MiB chunks: ( min: 2.44298, 2.481 +- 0.021, max: 2.50628 ) GB/s
    write 1 GiB into an emptied file in 512 MiB chunks: ( min: 2.44544, 2.479 +- 0.019, max: 2.5117 ) GB/s
    write 1 GiB into an emptied file in   1 GiB chunks: ( min: 2.46455, 2.486 +- 0.021, max: 2.53055 ) GB/s

# Write into a preallocated file

## Vectorized Writing

    writev 1 GiB into a preallocated file in  1 KiB chunks (x128): ( min: 0.906679, 0.930 +- 0.017, max: 0.964037 ) GB/s
    writev 1 GiB into a preallocated file in  4 KiB chunks (x128): ( min: 0.942697, 0.966 +- 0.018, max: 0.995736 ) GB/s
    writev 1 GiB into a preallocated file in 16 KiB chunks (x128): ( min: 0.980393, 1.005 +- 0.017, max: 1.03667 ) GB/s
    writev 1 GiB into a preallocated file in 64 KiB chunks (x128): ( min: 0.983517, 1.000 +- 0.014, max: 1.02639 ) GB/s
    writev 1 GiB into a preallocated file in  1 MiB chunks (x128): ( min: 1.32614, 1.372 +- 0.029, max: 1.42394 ) GB/s
    writev 1 GiB into a preallocated file in  8 MiB chunks (x128): ( min: 2.96452, 3.26 +- 0.25, max: 3.58874 ) GB/s

    pwritev 1 GiB into a preallocated file in  1 KiB chunks (x128): ( min: 0.918678, 0.936 +- 0.010, max: 0.947565 ) GB/s
    pwritev 1 GiB into a preallocated file in  4 KiB chunks (x128): ( min: 0.950842, 0.972 +- 0.021, max: 1.00676 ) GB/s
    pwritev 1 GiB into a preallocated file in 16 KiB chunks (x128): ( min: 0.987989, 1.015 +- 0.022, max: 1.04804 ) GB/s
    pwritev 1 GiB into a preallocated file in 64 KiB chunks (x128): ( min: 0.977708, 1.013 +- 0.020, max: 1.03797 ) GB/s
    pwritev 1 GiB into a preallocated file in  1 MiB chunks (x128): ( min: 1.32854, 1.349 +- 0.013, max: 1.37262 ) GB/s
    pwritev 1 GiB into a preallocated file in  8 MiB chunks (x128): ( min: 3.04783, 3.18 +- 0.14, max: 3.49314 ) GB/s

## Parallel Writing

    Use pwrite to write 1 GiB into a preallocated file using  1 threads: ( min: 2.83056, 3.07 +- 0.28, max: 3.60487 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  2 threads: ( min: 2.53705, 3.1 +- 0.4, max: 3.54489 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  4 threads: ( min: 2.4922, 2.56 +- 0.06, max: 2.69231 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using  8 threads: ( min: 2.53869, 2.57 +- 0.03, max: 2.65272 ) GB/s
    Use pwrite to write 1 GiB into a preallocated file using 16 threads: ( min: 2.54429, 2.569 +- 0.021, max: 2.61292 ) GB/s

## Simple Writing

    fwrite 1 GiB into a preallocated file in   1 KiB chunks: ( min: 0.888493, 0.908 +- 0.015, max: 0.933492 ) GB/s
    fwrite 1 GiB into a preallocated file in   4 KiB chunks: ( min: 0.893643, 0.912 +- 0.015, max: 0.940932 ) GB/s
    fwrite 1 GiB into a preallocated file in   8 KiB chunks: ( min: 0.903871, 0.919 +- 0.014, max: 0.946394 ) GB/s
    fwrite 1 GiB into a preallocated file in  16 KiB chunks: ( min: 0.909451, 0.919 +- 0.008, max: 0.939767 ) GB/s
    fwrite 1 GiB into a preallocated file in  64 KiB chunks: ( min: 0.937498, 0.950 +- 0.013, max: 0.972367 ) GB/s
    fwrite 1 GiB into a preallocated file in   1 MiB chunks: ( min: 1.14851, 1.167 +- 0.016, max: 1.19358 ) GB/s
    fwrite 1 GiB into a preallocated file in  16 MiB chunks: ( min: 2.55985, 2.582 +- 0.027, max: 2.65059 ) GB/s
    fwrite 1 GiB into a preallocated file in  64 MiB chunks: ( min: 2.47065, 2.494 +- 0.019, max: 2.5239 ) GB/s
    fwrite 1 GiB into a preallocated file in 512 MiB chunks: ( min: 2.46581, 2.500 +- 0.024, max: 2.54285 ) GB/s
    fwrite 1 GiB into a preallocated file in   1 GiB chunks: ( min: 2.47628, 2.504 +- 0.021, max: 2.53725 ) GB/s

    write 1 GiB into a preallocated file in   1 KiB chunks: ( min: 0.775414, 0.790 +- 0.010, max: 0.809114 ) GB/s
    write 1 GiB into a preallocated file in   4 KiB chunks: ( min: 0.756211, 0.87 +- 0.04, max: 0.89023 ) GB/s
    write 1 GiB into a preallocated file in   8 KiB chunks: ( min: 0.908212, 0.917 +- 0.008, max: 0.932833 ) GB/s
    write 1 GiB into a preallocated file in  16 KiB chunks: ( min: 0.920943, 0.933 +- 0.006, max: 0.940773 ) GB/s
    write 1 GiB into a preallocated file in  64 KiB chunks: ( min: 0.932192, 0.940 +- 0.008, max: 0.959429 ) GB/s
    write 1 GiB into a preallocated file in   1 MiB chunks: ( min: 1.31291, 1.35 +- 0.03, max: 1.40333 ) GB/s
    write 1 GiB into a preallocated file in  16 MiB chunks: ( min: 2.60759, 2.644 +- 0.022, max: 2.66762 ) GB/s
    write 1 GiB into a preallocated file in  64 MiB chunks: ( min: 2.43503, 2.51 +- 0.04, max: 2.55861 ) GB/s
    write 1 GiB into a preallocated file in 512 MiB chunks: ( min: 2.48915, 2.506 +- 0.019, max: 2.55414 ) GB/s
    write 1 GiB into a preallocated file in   1 GiB chunks: ( min: 2.48632, 2.504 +- 0.012, max: 2.51832 ) GB/s
