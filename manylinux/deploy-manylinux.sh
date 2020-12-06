#!/usr/bin/env bash
# This small wrapper script calls build-wheels.sh using docker.

set -e -x

# https://docs.travis-ci.com/user/environment-variables/#default-environment-variables
# This script is to be called from travis with a fitting job name!
[[ "$TRAVIS_JOB_NAME" =~ ^(build[\ -])?manylinux[0-9]*_.*$ ]] || exit 1

# Get target platform from travis job name
PLATFORM=${TRAVIS_JOB_NAME#build[ -]}  # E.g., manylinux2010_x86_64, manylinux1_x86_64, ...
echo "Build for platform $PLATFORM on $( uname --machine )"

docker run --rm -v "$( pwd ):/project" "quay.io/pypa/$PLATFORM" bash /project/manylinux/build-wheels.sh "$PLATFORM"
twine check dist/*

[[ "$TRAVIS_BUILD_STAGE_NAME" == Deploy ]] && twine upload --skip-existing -u __token__ dist/*
