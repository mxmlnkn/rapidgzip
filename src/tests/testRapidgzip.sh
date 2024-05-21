#!/usr/bin/env bash

# Should be run from build folder


RAPIDGZIP='src/tools/rapidgzip'
export RAPIDGZIP

function echoerr() { echo "$@" 1>&2; }

returnError()
{
    local lineNumber message
    if [ $# -eq 2 ]; then
        lineNumber=:$1
        message=$2
    else
        message=$*
    fi

    echoerr -e "\e[37m${FUNCNAME[1]}$lineNumber <- ${FUNCNAME[*]:2}\e[0m"
    echoerr -e "\e[37m$message\e[0m"
    echoerr -e '\e[31mTEST FAILED!\e[0m'

    exit 1
}


# Tests with stdin are hard to do in Catch2, so do them here.
[[ "$( cat ../src/tests/data/base64-256KiB.gz | $RAPIDGZIP --count )" -eq 262144 ]] ||
    returnError "$LINENO" 'Stdin count test failed!'

echo -e '\e[32mAll tests ran successfully.\e[0m'
