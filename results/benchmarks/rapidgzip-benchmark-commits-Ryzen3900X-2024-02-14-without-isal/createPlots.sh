#!/usr/bin/env bash

cd -- "$( dirname -- "$BASH_SOURCE[0]" )"
python3 ../../plotBenchmarksPerCommitGzip.py .
