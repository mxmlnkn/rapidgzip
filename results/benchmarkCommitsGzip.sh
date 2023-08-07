testFile='10xSRR22403185_2.fastq.gz'
testFile='test-files/silesia/20xsilesia.tar.gz'
testFile='4GiB-base64.gz'
resultFile='Ryzen3900X-20xsilesia.tar.gz-commit-timings.dat'
resultFile='Ryzen3900X-4GiB-base64.gz-commit-timings.dat'
> "$resultFile"
commits=( $( git log --oneline 7ce43a93~1..rapidgzip-v0.8.1 |
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

    printf '%s' "$( git rev-parse HEAD )" >> "$resultFile"
    for (( i = 0; i < 10; ++i )); do
        runtime=$( ( time "$tool" -d -o /dev/null "$testFile" ) 2>&1 |sed -nr 's|real.*0m([0-9.]+)s|\1|p' )
        printf ' %s' "$runtime" >> "$resultFile"
    done
    echo >> "$resultFile"
done
