#!/usr/bin/env bash
set -e -x

PLATFORM=$1

# Compile wheels
for PYBIN in /opt/python/*3*/bin; do
(
    # use clean directory to fix the cryptic "Can't repair" error message
    # of auditwheel seemingly caused by some leftover files
    # auditwheel: error: cannot repair "wheelhouse/indexed_bzip2-1.0.0-cp37-cp37m-linux_x86_64.whl"
    #             to "manylinux1_x86_64" ABI because of the presence of too-recent versioned symbols.
    #             You'll need to compile the wheel on an older toolchain.
    buildFolder=$( mktemp -d )
    cd /project
    git worktree add "$buildFolder"
    cd -- "$buildFolder"

    "${PYBIN}/pip" wheel .
    for wheel in *.whl; do
        # Bundle external shared libraries into the wheels
        auditwheel repair "$wheel" --plat $PLATFORM -w /project/dist/
    done

    cd -
    git worktree remove --force "$buildFolder"
    rm -rf "$buildFolder"
)
done

