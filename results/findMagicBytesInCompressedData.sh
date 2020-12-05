#/bin/bash

cd /dev/shm

sizePerTest=$(( 512*1024*1024 ))
folder='findBz2MagicBytes'
parallelism=$(( $( nproc ) - 4 ))
export PATH=/media/d/Myself/projects/ratarmount/indexed_bzip2/tools:$PATH
totalBytesSearched=0

mkdir -p -- "$folder" && cd -- "$folder"

while true; do
    echo "[$( date +%s )] Searched $(( totalBytesSearched / ( 1024 * 1024 * 1024 ) )) GiB"

    for i in $( seq $parallelism ); do
        file=$( date +%s.%N ).raw
        #head -c "$sizePerTest" /dev/urandom > "$file" # ~2.3s for 128MB
        seed=$( dd if=/dev/urandom bs=128 count=1 2>/dev/null | base64 )
        openssl enc -aes-256-ctr -pass pass:"$seed" -nosalt </dev/zero 2>/dev/null |
            head -c "$sizePerTest" > "$file" # ~80ms for 128MB
    done
    echo "[$( date +%s )] Created $(( sizePerTest * parallelism / ( 1024 * 1024 * 1024 ) )) GiB of random data"

    for file in *.raw; do lbzip2 "$file"; done
    echo "[$( date +%s )] Compressed random data"

    find . -iname '*.bz2' -print0 | xargs -0 -P $parallelism -I{} bash -c '
        bzcat "$0" "$(( 1024 * 1024 ))" 2>&1 >/dev/null |
            sed -nE "s|([0.9]+ B [0-9] b).*0x314159.*|\1|p" > "${0%.bz2}".decode.lst
    ' {}
    echo "[$( date +%s )] Found block offsets by decoding serially"

    for file in *.bz2; do
        (( totalBytesSearched += $( stat -c %s -- "$file" ) ))
        blockfinder "${file}" 2>&1 | sed -nE "s|([0.9]+ B [0-9] b).*0x314159.*|\1|p" > "${file%.bz2}.search.lst"
        if ! diff -q "${file%.bz2}.search.lst" "${file%.bz2}.decode.lst"; then
            echo -e "\e[31mFOUND FALSE POSITIVE IN $file"'!!!'"\e[0m"
            exit 1 # end goal !!!
        fi
        'rm' -- "$file"
    done
    echo "[$( date +%s )] Searched for magic bytes to find block offsets\n"
done

# [1606950071] Searched 0 GiB
# [1606950076] Created 0 GiB of random data
# [1606950136] Compressed random data
# [1606950195] Found block offsets by decoding serially
# [1606950203] Searched for magic bytes to find block offsets\n
# [1606950203] Searched 10 GiB
