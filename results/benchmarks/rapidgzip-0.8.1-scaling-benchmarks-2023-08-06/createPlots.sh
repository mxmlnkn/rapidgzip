#!/usr/bin/env bash

cd -- "$( dirname -- "$BASH_SOURCE[0]" )/plots"
DISPLAY= python3 ../../../plotBenchmarks2023.py ../data/scaling-base64
DISPLAY= python3 ../../../plotBenchmarks2023.py ../data/scaling-fastq
DISPLAY= python3 ../../../plotBenchmarks2023.py ../data/scaling-silesia
