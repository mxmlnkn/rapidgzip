#!/usr/bin/env bash


function commandExists() {
    command -v "$1" > /dev/null 2>&1;
}

function build()
{
    if [[ -f CMakeCache.txt ]]; then
        cmake --build .
    elif [[ -f build.ninja ]]; then
        ninja
    elif [[ -f Makefile ]]; then
        make -j $( nproc )
    fi
}

function buildRepository()
(
    if [[ -f CMakeLists.txt ]]; then
        mkdir -p build
        cd build
        if commandExists ninja; then
            cmake -GNinja -DCMAKE_BUILD_TYPE=Release ..
        else
            cmake ..
        fi
    elif [[ -x configure ]]; then
        ./configure
    elif [[ -x autogen.sh ]]; then
        ./autogen.sh
        ./configure
    elif [[ -x configure.py ]]; then
        python3 configure.py
    fi

    build
)


buildFolder='benchmark-malloc-replacements'
cd "$buildFolder"

distributions=(
    'https://github.com/microsoft/mimalloc.git'    'v2.0.7'          'build/libmimalloc.so'
    'https://github.com/mjansson/rpmalloc.git'     '1.4.4'           'bin/linux/release/x86-64/librpmallocwrap.so'
    'https://github.com/gperftools/gperftools.git' 'gperftools-2.10' 'build/libtcmalloc_minimal.so'
    'https://github.com/oneapi-src/oneTBB.git'     'v2021.7.0'       'libtbbmalloc_proxy.so'
    'https://github.com/microsoft/snmalloc.git'    '0a5eb403a'       'build/libsnmallocshim.so'
    'https://github.com/jemalloc/jemalloc.git'     '5.3.0'           'lib/libjemalloc.so'
)

wrappers=( '' )

for (( i = 0; i < ${#distributions[@]}; i += 3 )); do
    url=${distributions[i+0]}
    tag=${distributions[i+1]}
    wrapper=${distributions[i+2]}

    folder="${url##*/}"
    folder="${folder%.git}"

    if [[ ! -d "$folder" ]]; then
        git clone --depth 1 --shallow-submodules --recursive --branch "$tag" "$url"
    fi

    wrapperPath="$folder/$wrapper"
    if [[ ! -f "$wrapperPath" && "${wrapper%/*}" == "$wrapper" ]]; then
        candidate=$( find "$folder" -name "$wrapper" )
        if [[ -n "$candidate" ]]; then
            wrapperPath="$candidate";
        fi
    fi

    if [[ ! -f "$wrapperPath" ]]; then (
        cd "$folder"
        buildRepository
    ); fi

    if [[ ! -f "$wrapperPath" ]]; then
        echo -e "\e[31mCould not find '$wrapper'. Build might have failed.\e[0m" 1>&2
        continue
    fi
    echo -e "\e[32mSuccessfully built '$wrapper'.\e[0m" 1>&2
    wrappers+=( "$PWD/$wrapperPath" )
done


cd ..

for wrapper in "${wrappers[@]}"; do
    echo "Check with: $wrapper"
    for i in $( seq 5 ); do
        time LD_PRELOAD="$wrapper" src/tools/pragzip -P 0 -d -c 4GiB-base64.gz 2>pragzip.log | wc -l
        grep "Decompressed in total" pragzip.log
    done
done

for wrapper in "${wrappers[@]}"; do
    echo "Check with: $wrapper"
    for i in $( seq 5 ); do
        time LD_PRELOAD="$wrapper" src/tools/pragzip -P 0 --count-lines 4GiB-base64.gz 2>pragzip.log
        grep "Decompressed in total" pragzip.log
    done
done
