#!/usr/bin/env bash

# exit when any command fails
set -x -e

git tag -n99 --sort -refname -l  v* | sed -r 's|^v[^ ]+ +|\n# |; s|^    ||' > CHANGELOG.md

rm -rf build dist *.egg-info __pycache__

# generate bzip2.cpp from bzip2.pyx
python3 setup.py build_ext --inplace --cython
python3 setup.py sdist
srcPackage=$( find dist -name '*.tar.gz' -printf "%T@ %p\n" | sort -n | sed -n -r '1{ s|[0-9.]+ ||p; }' )
pip3 install --user "$srcPackage"

python3 testBz2.py

twine upload "$srcPackage"
