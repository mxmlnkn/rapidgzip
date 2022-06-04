function allgzip()
{
    local file=$1
    gzip -c -- "$file" > "${file}.gz"
    pigz -c -- "$file" > "${file}.pgz"
    bgzip -c -- "$file" > "${file}.bgz"
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
