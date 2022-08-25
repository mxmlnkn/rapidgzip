function allgzip()
{
    local file=$1
    test -f "${file}.gz" || gzip -c -- "$file" > "${file}.gz"
    test -f "${file}.igz" || igzip -c -- "$file" > "${file}.igz"
    test -f "${file}.pgz" || pigz -c -- "$file" > "${file}.pgz"
    test -f "${file}.bgz" || bgzip -c -- "$file" > "${file}.bgz"
}

touch empty && allgzip empty
fname='1B'; printf 'A' > "$fname" && allgzip "$fname"
fname='32A-fixed-Huffman'; printf 's' '' | tr ' ' 'A' > "$fname" && allgzip "$fname"

# TODO: This file leads to read errors when opening the pgz version with pragzip!
# Encountered error: All code lengths are zero! while trying to read deflate header!
fname='256B-extended-ASCII-table-in-utf8-dynamic-Huffman'
python3 -c 'print("".join(chr(x) for x in range(256)))' > "$fname"
allgzip "$fname"

fname='256B-extended-ASCII-table-uncompressed'
python3 -c 'import sys; sys.stdout.buffer.write(bytes(range(256)))' > "$fname"
allgzip "$fname"

python3 -c 'import sys; sys.stdout.buffer.write(bytes(range(256)))' | pigz > 0CL.pgz

fname='base64-256KiB'; base64 /dev/urandom | head -c $(( 256*1024 )) > "$fname" && allgzip "$fname"
python3 -c 'import sys; import indexed_gzip as igz; f = igz.IndexedGzipFile(sys.argv[1], spacing=64*1024); f.build_full_index(); f.export_index(sys.argv[1] + ".index")' "${fname}.gz"

# For triggering a kind of internal buffer overflow bug leading to skipped decoded data, we need to
# produce uncompressed blocks > 32KiB. gzip seems to limit them to 32 KiB, which avoids that bug!
# pigz even limits it to 16 KiB! igzip finally produces blocks sized 64 KiB!
fname='random-128KiB'; head -c $(( 128*1024 )) /dev/urandom > "$fname"; allgzip "$fname"

# Test with exactly one deflate window size of data
fname='base64-32KiB'; base64 /dev/urandom | head -c $(( 32*1024 )) > "$fname" && allgzip "$fname"

# Create a compressed file with one pigz flush block (uncompressed block of size 0).
# 32KiB between flush blocks is the minimum.
fname='base64-64KiB'; base64 /dev/urandom | head -c $(( 64*1024 )) > "$fname" &&
pigz -c --blocksize 32 -- "$fname" > "${fname}.pgz"
