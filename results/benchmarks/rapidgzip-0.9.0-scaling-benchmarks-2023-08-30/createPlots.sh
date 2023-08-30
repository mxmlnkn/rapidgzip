#!/usr/bin/env bash

mkdir -p -- "$( dirname -- "$BASH_SOURCE[0]" )/plots" && cd -- "$_"
for type in base64 fastq silesia; do (
    mkdir -p -- "scaling-$type" &&
    cd -- "$_" &&
    DISPLAY= python3 ../../../../plotBenchmarks2023.py "../../data/scaling-$type"
); done
