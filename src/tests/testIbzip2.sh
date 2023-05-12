#!/usr/bin/env bash

# Should be run from build folder


IBZIP2='src/tools/ibzip2'
export IBZIP2


function echoerr() { echo "$@" 1>&2; }

function commandExists() {
    # http://stackoverflow.com/questions/592620/check-if-a-program-exists-from-a-bash-script
    command -v "$1" > /dev/null 2>&1;
}


function checkBz2Md5()
{
    local bz2File=$1

    # Randomized bit is deprecated and not supported in ibzip2, it throws an exception in that case.
    if [[ ${bz2File##*/} == 'rand.bz2' ]]; then return 0; fi

    local md5File="${bz2File%.bz2}.md5"
    if [[ ! -f "$md5File" ]]; then
        echoerr
        echoerr "Could not find md5 checksum file for: $bz2File"
        return 1
    fi

    printf .

    for tool in bzip2 lbzip2 pbzip2 "$IBZIP2 -P 1" "$IBZIP2 -P 2" "$IBZIP2 -P 8"; do
        if ! commandExists "$tool" && [[ ! -x "${tool%% *}" ]]; then
            echoerr "[Warning] Could not find '$tool', so will skip tests for it."
            continue
        fi

        if ! $tool --decompress --stdout "$bz2File" | md5sum --quiet --check "$md5File"; then
            echoerr -e "\n\e[31mTest with $tool on $bz2File failed.\e[0m\n"
            return 1
        fi
    done

    for parallelization in 1 2 8; do
        if ! python3 -c "import indexed_bzip2 as ibz2; import sys; sys.stdout.buffer.write( ibz2.IndexedBzip2File( sys.argv[1], parallelization=$parallelization ).read() )" "$bz2File" |
               md5sum --quiet --check "$md5File"
        then
            echoerr -e "\n\e[31mTest with indexed_bzip2 and parallelization=$parallelization on $bz2File failed.\e[0m\n"
            return 1
        fi
    done
}


tools=()

if commandExists bzip2; then tools+=( bzip2 ); fi
if commandExists lbzip2; then tools+=( lbzip2 ); fi
if commandExists pbzip2; then tools+=( pbzip2 ); fi

if [[ ! -x "$IBZIP2" ]]; then
    echoerr "Could not find $IBZIP2"'!'
fi


export -f echoerr commandExists checkBz2Md5

find ../src/external/bzip2-tests -type f -name '*.bz2' -print0 | xargs -0 -n 1 -I {} bash -c 'checkBz2Md5 "$@"' bash {}
# If any invocation of the command exits with a status of 255, xargs will stop immediately without reading any further input.
# An error message is issued on stderr when this happens.
# Exit status 123 if any invocation of the command exited with status 1-125
if [[ $? -eq 0 ]]; then
    echoerr -e '\n\e[32mAll tests successful!\e[0m'
else
    echoerr -e '\n\e[31mSome of the tests failed!\e[0m'
fi
