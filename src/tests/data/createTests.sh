# pgzf can be installed with sudo apt install wtdbg2
# mzip (migz) can simply be downloaded as a binary from https://github.com/linkedin/migz/releases

function rawDeflate()
{
    python3 -c '
import sys, zlib
o = zlib.compressobj( wbits = -15 )
sys.stdout.buffer.write( o.compress( open( sys.argv[1], "rb" ).read() ) )
sys.stdout.buffer.write( o.flush() )
' "$1"
}

function allgzip()
{
    local file=$1
    test -f "${file}.bz2" || bzip2 -c -- "$file" > "${file}.bz2"
    test -f "${file}.lz4" || lz4 -c -- "$file" > "${file}.lz4"
    test -f "${file}.gz" || gzip -c -- "$file" > "${file}.gz"
    test -f "${file}.igz" || igzip -c -- "$file" > "${file}.igz"
    test -f "${file}.pigz" || pigz -c -- "$file" > "${file}.pigz"
    test -f "${file}.zlib" || pigz -c --zlib -- "$file" > "${file}.zlib"
    test -f "${file}.bgz" || bgzip -c -- "$file" > "${file}.bgz"
    test -f "${file}.pgzf" || pgzf -o "${file}.pgzf" "$file"
    test -f "${file}.migz" || cat -- "$file" | mzip > "${file}.migz"
    test -f "${file}.deflate" || rawDeflate "$file" > "${file}.deflate"
}

touch empty && allgzip empty
fname='1B'; printf 'A' > "$fname" && allgzip "$fname"
fname='32A-fixed-Huffman'; printf 's' '' | tr ' ' 'A' > "$fname" && allgzip "$fname"

# TODO: This file leads to read errors when opening the pgz version with rapidgzip!
# Encountered error: All code lengths are zero! while trying to read deflate header!
fname='256B-extended-ASCII-table-in-utf8-dynamic-Huffman'
python3 -c 'print("".join(chr(x) for x in range(256)))' > "$fname"
allgzip "$fname"

fname='256B-extended-ASCII-table-uncompressed'
python3 -c 'import sys; sys.stdout.buffer.write(bytes(range(256)))' > "$fname"
allgzip "$fname"

python3 -c 'import sys; sys.stdout.buffer.write(bytes(range(256)))' | pigz > 0CL.pigz

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
pigz -c --blocksize 32 -- "$fname" > "${fname}.pigz"


function createRandomWordsFile()
{
    # Create a worst case for marker replacement for which the backreference window can never be fully resolved
    # and is propagated through the whole file because the gzip compression consists almost exclusively out
    # of backreferences. The file contents are random words drawn from a set of words that fits into the window.
    # Create base dictionary consisting of 128 words of length 16 (the newline is counted as belonging to the word).
    wordLength=16
    wordCount=128
    chunkSize=$(( 1024 * 1024 ))  # 1 MiB
    fileSizeInChunk=$(( 2 * 1024 ))  # 2 GiB
    fileName="random-words-2-GiB.dat"
    cat /dev/urandom | base64 -w $(( wordLength - 1 )) | head -n "$wordCount" > 'random-words.dat'

    > 'random-words-chunk.dat'  # Create or empty file
    for (( i=0; i < chunkSize; i += wordLength * wordCount )); do
        cat 'random-words.dat' >> 'random-words-chunk.dat'
    done

    > "$fileName"  # Create or empty file
    for (( i=0; i < fileSizeInChunk; ++i )); do
        shuf 'random-words-chunk.dat' >> "$fileName"
    done

    gzip -k "$fileName"

    # > rapidgzip --analyze "$fileName".gz | head -100
    # [First block]
    #     Symbol Types:
    #        Literal         : 1966 (5.99994 %)
    #        Back-References : 30801 (94.0001 %)
    # [All subsequent blocks]
    #     Symbol Types:
    #         Literal         : 0 (0 %)
    #         Back-References : 32767 (100 %)
}
