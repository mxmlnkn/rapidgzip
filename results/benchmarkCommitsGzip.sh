testFile='10xSRR22403185_2.fastq.gz'
> 'results.dat'
commits=( $( git log --oneline pragzip-7ce43a93~1..27b7ab8f51be077d4608aaf61d6e9ca65b144096 |
                 grep '[[]\(feature\|refactor\|fix\|performance\)[]]' | sed 's| .*||' ) )
for commit in "${commits[@]}"; do
    git checkout -f "$commit" || break
    git submodule update --init --recursive
    ( cd ../src/external/cxxopts/ && git checkout HEAD . )

    rm -f src/tools/{pragzip,rapidgzip}
    cmake --build . --parallel $( nproc ) -- pragzip
    cmake --build . --parallel $( nproc ) -- rapidgzip

    tool=
    if [[ -f src/tools/pragzip ]]; then tool=src/tools/pragzip; fi
    if [[ -f src/tools/rapidgzip ]]; then tool=src/tools/rapidgzip; fi
    if [[ -z "$tool" ]]; then continue; fi

    printf '%s' "$( git rev-parse HEAD )" >> 'results.dat'
    for (( i = 0; i < 10; ++i )); do
        runtime=$( ( time "$tool" -d -o /dev/null "$testFile" ) 2>&1 |sed -nr 's|real.*0m([0-9.]+)s|\1|p' )
        printf ' %s' "$runtime" >> 'results.dat'
    done
    echo >> 'results.dat'
done
