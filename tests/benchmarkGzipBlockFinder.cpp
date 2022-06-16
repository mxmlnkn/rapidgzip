/*
DEFLATE Compressed Data Format Specification version 1.3
https://www.rfc-editor.org/rfc/rfc1951.txt

GZIP file format specification version 4.3
https://www.ietf.org/rfc/rfc1952.txt
*/


#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <zlib.h>

#include <BitReader.hpp>
#include <blockfinder/Bgzf.hpp>
#include <blockfinder/PigzParallel.hpp>
#include <BufferedFileReader.hpp>
#include <common.hpp>
#include <pragzip.hpp>
#include <Statistics.hpp>


std::ostream&
operator<<( std::ostream& out, std::vector<size_t> vector )
{
    constexpr size_t MAX_VALUES_TO_PRINT = 15;
    for ( size_t i = 0; i < std::min( vector.size(), MAX_VALUES_TO_PRINT ); ++i ) {
        out << " " << vector[i];
    }
    if ( vector.size() > MAX_VALUES_TO_PRINT ) {
        out << " ...";
    }
    return out;
}


[[nodiscard]] std::vector<size_t>
findGzipStreams( const std::string& fileName )
{
    const auto file = throwingOpen( fileName.c_str(), "rb" );

    static constexpr auto bufferSize = 4*1024*1024;
    std::vector<char> buffer( bufferSize, 0 );

    std::vector<size_t> streamOffsets;
    size_t totalBytesRead = 0;
    while ( true )
    {
        const auto bytesRead = fread( buffer.data(), sizeof(char), buffer.size(), file.get() );
        if ( bytesRead == 0 ) {
            break;
        }

        for ( size_t i = 0; i + 8 < bytesRead; ++i ) {
            if ( ( buffer[i+0] == (char)0x1F )
                 && ( buffer[i+1] == (char)0x8B )
                 && ( buffer[i+2] == (char)0x08 )
                 && ( buffer[i+3] == (char)0x04 )
                 && ( buffer[i+4] == (char)0x00 )  // this is assuming the mtime is zero, which obviously can differ!
                 && ( buffer[i+5] == (char)0x00 )
                 && ( buffer[i+6] == (char)0x00 )
                 && ( buffer[i+7] == (char)0x00 )
                 && ( buffer[i+8] == (char)0x00 ) ) {
                //std::cerr << "Found possible candidate for a gzip stream at offset: " << totalBytesRead + i << " B\n";
                streamOffsets.push_back(totalBytesRead + i);
            }
        }

        totalBytesRead += bytesRead;
    }

    return streamOffsets;
}


[[nodiscard]] std::vector<size_t>
findBgzStreams( const std::string& fileName )
{
    std::vector<size_t> streamOffsets;

    const auto t0 = now();

    try {
        BgzfBlockFinder blockFinder( std::make_unique<StandardFileReader>( fileName ) );

        while ( true ) {
            const auto offset = blockFinder.find();
            if ( offset == std::numeric_limits<size_t>::max() ) {
                break;
            }
            streamOffsets.push_back( offset );
        }
    }
    catch ( const std::invalid_argument& ) {
        return {};
    }

    const auto t1 = now();
    const auto totalBytesRead = streamOffsets.back() / 8;

    std::cout << "[findBgzStreams] Trying to find block bit offsets in " << totalBytesRead / 1024 / 1024
              << " MiB of data took " << duration(t0, t1) << " s => "
              << ( totalBytesRead / 1e6 / duration(t0, t1) ) << " MB/s" << "\n";

    return streamOffsets;
}


/**
 * @see https://github.com/madler/zlib/blob/master/examples/zran.c
 */
[[nodiscard]] std::pair<std::vector<size_t>, std::vector<size_t> >
parseWithZlib(const std::string& fileName)
{
    const auto file = throwingOpen( fileName.c_str(), "rb" );

    std::vector<size_t> streamOffsets;
    std::vector<size_t> blockOffsets;

    static constexpr int BUFFER_SIZE = 1024*1024;
    static constexpr int WINDOW_SIZE = 32*1024;

    /**
     * Make one entire pass through the compressed stream and build an index, with
     * access points about every span bytes of uncompressed output -- span is
     * chosen to balance the speed of random access against the memory requirements
     * of the list, about 32K bytes per access point.  Note that data after the end
     * of the first zlib or gzip stream in the file is ignored.  build_index()
     * returns the number of access points on success (>= 1), Z_MEM_ERROR for out
     * of memory, Z_DATA_ERROR for an error in the input file, or Z_ERRNO for a
     * file read error.  On success, *built points to the resulting index.
     */
    unsigned char input[BUFFER_SIZE];
    unsigned char window[WINDOW_SIZE];

    /* initialize inflate */
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = 0;
    stream.next_in = Z_NULL;

    /* Second argument is window bits. log2 base of window size. Adding 32 to that (setting the 5-th bit),
     * means that automatic zlib or gzip decoding is detected. */
    auto ret = inflateInit2(&stream, 32 + 15);
    if ( ret != Z_OK ) {
        throw ret;
    }

    std::vector<unsigned char> extraBuffer( 1024 );

    gz_header header;
    header.extra = extraBuffer.data();
    header.extra_max = extraBuffer.size();
    header.name = Z_NULL;
    header.comment = Z_NULL;
    header.done = 0;

    bool readHeader = true;
    ret = inflateGetHeader(&stream, &header);
    if ( ret != Z_OK ) {
        throw ret;
    }
    streamOffsets.push_back( 0 );

    /* Counters to avoid 4GB limit */
    off_t totin = 0;
    stream.avail_out = 0;

    /* inflate the input, maintain a sliding window, and build an index -- this
       also validates the integrity of the compressed data using the check
       information at the end of the gzip or zlib stream */
    while( true )
    {
        /* get some compressed data from input file */
        stream.avail_in = std::fread( input, 1, BUFFER_SIZE, file.get() );
        if ( stream.avail_in == 0 && std::feof( file.get() ) ) {
            break;
        }
        if ( std::ferror( file.get() ) ) {
            throw Z_ERRNO;
        }
        if ( stream.avail_in == 0 ) {
            throw Z_DATA_ERROR;
        }
        stream.next_in = input;

        /* process all of that, or until end of stream */
        while ( stream.avail_in != 0 )
        {
            /* reset sliding window if necessary */
            if ( stream.avail_out == 0 ) {
                stream.avail_out = WINDOW_SIZE;
                stream.next_out = window;
            }

            /* inflate until out of input, output, or at end of block --
               update the total input and output counters */
            totin  += stream.avail_in;
            ret     = inflate( &stream, Z_BLOCK );  /* return at end of block */
            totin  -= stream.avail_in;
            if ( ret == Z_NEED_DICT ) {
                ret = Z_DATA_ERROR;
            }
            if ( ( ret == Z_MEM_ERROR ) || ( ret == Z_DATA_ERROR ) ) {
                throw ret;
            }

            if ( readHeader && ( header.done == 1 ) && ( header.extra_len > 0 ) ) {
                readHeader = false;
                /* retry if extra did not fit? */
                extraBuffer.resize( std::min( header.extra_len, static_cast<unsigned int>( extraBuffer.size() ) ) );
                std::cout << "Got " << extraBuffer.size() << " B of FEXTRA field!\n";
            }

            if ( ret == Z_STREAM_END ) {
                ret = inflateReset( &stream );
                if ( ret == Z_OK ) {
                    streamOffsets.push_back( totin );
                }
                continue;
            }

            /**
             * > The Z_BLOCK option assists in appending to or combining deflate streams.
             * > To assist in this, on return inflate() always sets strm->data_type to the
             * > number of unused bits in the last byte taken from strm->next_in, plus 64 if
             * > inflate() is currently decoding the last block in the deflate stream, plus
             * > 128 if inflate() returned immediately after decoding an end-of-block code or
             * > decoding the complete header up to just before the first byte of the deflate
             * > stream.  The end-of-block will not be indicated until all of the uncompressed
             * > data from that block has been written to strm->next_out.  The number of
             * > unused bits may in general be greater than seven, except when bit 7 of
             * > data_type is set, in which case the number of unused bits will be less than
             * > eight.  data_type is set as noted here every time inflate() returns for all
             * > flush options, and so can be used to determine the amount of currently
             * > consumed input in bits.
             * -> bit 7 corresponds to 128 -> if set, then number of unused bits is less than 8 -> therefore &7!
             *    as zlib stops AFTER the block, we are not interested in the offset for the last block,
             *    i.e., we check against the 6-th bit, which corresponds to ( x & 64 ) == 0 for all but last block.
             */
            if ( ( ( stream.data_type & 128 ) != 0 ) && ( ( stream.data_type & 64 ) == 0 ) ) {
                blockOffsets.push_back( totin * 8 - ( stream.data_type & 7 ) );
            }

        }
    }

    /* clean up and return index (release unused entries in list) */
    (void) inflateEnd( &stream );
    return { streamOffsets, blockOffsets };
}


class GzipWrapper
{
public:
    static constexpr int WINDOW_SIZE = 32*1024;

    enum class Format
    {
        AUTO,
        RAW,
        GZIP,
    };

public:
    explicit
    GzipWrapper( Format format = Format::AUTO )
    {
        m_stream.zalloc = Z_NULL;     /* used to allocate the internal state */
        m_stream.zfree = Z_NULL;      /* used to free the internal state */
        m_stream.opaque = Z_NULL;     /* private data object passed to zalloc and zfree */

        m_stream.avail_in = 0;        /* number of bytes available at next_in */
        m_stream.next_in = Z_NULL;    /* next input byte */

        m_stream.avail_out = 0;       /* remaining free space at next_out */
        m_stream.next_out = Z_NULL;   /* next output byte will go here */

        m_stream.msg = NULL;

        int windowBits = 15;  // maximum value corresponding to 32kiB;
        switch ( format )
        {
        case Format::AUTO:
            windowBits += 32;
            break;

        case Format::RAW:
            windowBits *= -1;
            break;

        case Format::GZIP:
            windowBits += 16;
            break;
        }

        auto ret = inflateInit2( &m_stream, windowBits );
        if ( ret != Z_OK ) {
            throw ret;
        }
    }

    ~GzipWrapper()
    {
        inflateEnd( &m_stream );
    }

    bool
    tryInflate( unsigned char const* compressed,
                size_t               compressedSize,
                size_t               bitOffset = 0 )
    {
        if ( inflateReset( &m_stream ) != Z_OK ) {
            return false;
        }

        if ( ceilDiv( bitOffset, CHAR_BIT ) >= compressedSize ) {
            return false;
        }

        const auto bitsToSeek = bitOffset % CHAR_BIT;
        const auto byteOffset = bitOffset / CHAR_BIT;
        m_stream.avail_in = compressedSize - byteOffset;
        /* const_cast should be safe because zlib presumably only uses this in a const manner.
         * I'll probably have to roll out my own deflate decoder anyway so I might be able
         * to change this bothersome interface. */
        m_stream.next_in = const_cast<unsigned char*>( compressed ) + byteOffset;

        const auto outputPreviouslyAvailable = std::min( size_t( 8 * 1024 ), m_outputBuffer.size() );
        m_stream.avail_out = outputPreviouslyAvailable;
        m_stream.next_out = m_outputBuffer.data();

        /* Using std::fill leads to 10x slowdown -.-!!! Memset probably better.
         * Well, or not necessary at all because we are not interested in the specific output values anyway.
         * std::memset only incurs a 30% slowdown. */
        //std::fill( m_window.begin(), m_window.end(), '\0' );
        //std::memset( m_window.data(), 0, m_window.size() );
        if ( bitsToSeek > 0 ) {
            m_stream.next_in += 1;
            m_stream.avail_in -= 1;

            auto errorCode = inflatePrime( &m_stream, 8 - bitsToSeek, compressed[byteOffset] >> bitsToSeek );
            if ( errorCode != Z_OK ) {
                return false;
            }
        }

        auto errorCode = inflateSetDictionary( &m_stream, m_window.data(), m_window.size() );
        if ( errorCode != Z_OK ) {
        }

        errorCode = inflate( &m_stream, Z_BLOCK );
        if ( ( errorCode != Z_OK ) && ( errorCode != Z_STREAM_END ) ) {
            return false;
        }

        if ( errorCode == Z_STREAM_END ) {
            /* We are not interested in blocks close to the stream end.
             * Because either this is close to the end and no parallelization is necessary,
             * or this means the gzip file is compromised of many gzip streams, which are a tad
             * easier to search for than raw deflate streams! */
            return false;
        }
        const auto nBytesDecoded = outputPreviouslyAvailable - m_stream.avail_out;
        if ( nBytesDecoded < outputPreviouslyAvailable ) {
            return false;
        }

        return true;
    }

private:
    z_stream m_stream;
    std::vector<unsigned char> m_window = std::vector<unsigned char>( 32 * 1024, '\0' );
    std::vector<unsigned char> m_outputBuffer = std::vector<unsigned char>( 64 * 1024 * 1024 );
};


[[nodiscard]] std::vector<size_t>
findDeflateBlocksZlib( const std::string& fileName )
{
    const auto file = throwingOpen( fileName.c_str(), "rb" );
    /* Use uint32_t only for alignment. */
    std::vector<uint32_t> buffer( 1024*1024 / sizeof( uint32_t ), 0 );
    const auto nElementsRead = std::fread( buffer.data(), sizeof(buffer[0]), buffer.size(), file.get() );
    buffer.resize( nElementsRead );
    pragzip::BitReader bitReader( fileName );

    std::vector<size_t> bitOffsets;
    GzipWrapper gzip( GzipWrapper::Format::RAW );

    [[maybe_unused]] uint32_t nextThreeBits = bitReader.read<2>();

    const auto t0 = now();
    for ( size_t offset = 0; offset <= ( buffer.size() - 1 ) * sizeof( buffer[0]) * CHAR_BIT; ++offset ) {
        nextThreeBits >>= 1;
        nextThreeBits |= bitReader.read<1>() << 2;

        if ( gzip.tryInflate( reinterpret_cast<unsigned char*>( buffer.data() ),
                              buffer.size() * sizeof(buffer[0]),
                              offset ) ) {
            bitOffsets.push_back( offset );
        }
    }
    const auto t1 = now();
    const auto nBytesToTest = buffer.size() * sizeof(buffer[0]);
    std::cout << "[findDeflateBlocksZlib] Trying to find block bit offsets in " << nBytesToTest
              << " B of data took " << duration(t0, t1) << " s => "
              << ( nBytesToTest / 1e6 / duration(t0, t1) ) << " MB/s" << "\n";

    return bitOffsets;
}


[[nodiscard]] std::vector<size_t>
findDeflateBlocksZlibOptimized( const std::string& fileName )
{
    const auto file = throwingOpen( fileName.c_str(), "rb" );
    /* Use uint32_t only for alignment. */
    std::vector<uint32_t> buffer( 1024*1024 / sizeof( uint32_t ), 0 );
    const auto nElementsRead = std::fread( buffer.data(), sizeof(buffer[0]), buffer.size(), file.get() );
    buffer.resize( nElementsRead );
    pragzip::BitReader bitReader( fileName );

    /**
     * Deflate Block:
     *
     *   Each block of compressed data begins with 3 header bits
     *   containing the following data:
     *
     *      first bit       BFINAL
     *      next 2 bits     BTYPE
     *
     *   Note that the header bits do not necessarily begin on a byte
     *   boundary, since a block does not necessarily occupy an integral
     *   number of bytes.
     *
     *   BFINAL is set if and only if this is the last block of the data
     *   set.
     *
     *   BTYPE specifies how the data are compressed, as follows:
     *
     *      00 - no compression
     *      01 - compressed with fixed Huffman codes
     *      10 - compressed with dynamic Huffman codes
     *      11 - reserved (error)
     *
     * => For a perfect compression, we wouldn't be able to find the blocks in any way because all input data
     *    would be valid data. Therefore, in order to find blocks we are trying to find and make use of any
     *    kind of redundancy / invalid values, which might appear.
     * -> We can reduce the number of bit offsets to try by assuming BFINAL = 0,
     *    which should not matter for performance anyway. This is a kind of redundancy, which could have been
     *    compressed further by saving the number of expected blocks at the beginning. This number would amortize
     *    after 64 blocks for a 64-bit number. And it could even be stored more compactly like done in UTF-8.
     */

     /**
      * @verbatim
      *         GZM CMP FLG   MTIME    XFL OS      FNAME
      *        <---> <> <> <--------->  <> <> <----------------
      * @0x00  1f 8b 08 08 bb 97 d7 61  02 03 74 69 6e 79 62 36  |.......a..tinyb6|
      *
      *        FNAME Blocks starting at 18 B
      *        <---> <----
      * @0x10  34 00 14 9d b7 7a 9c 50  10 46 7b bd 0a 05 2c 79  |4....z.P.F{...,y|
      * @0x20  4b 72 5a 72 a6 23 e7 9c  79 7a e3 c6 85 3e 5b da  |KrZr.#..yz...>[.|
      *        <--------------------->
      *               uint64_t
      * @endverbatim
      */

    auto* const cBuffer = reinterpret_cast<unsigned char*>( buffer.data() );

    std::vector<size_t> bitOffsets;
    GzipWrapper gzip( GzipWrapper::Format::RAW );
    size_t zlibTestCount = 0;

    uint32_t nextThreeBits = bitReader.read<2>();

    const auto t0 = now();
    for ( size_t offset = 0; offset <= ( buffer.size() - 1 ) * sizeof( buffer[0]) * CHAR_BIT; ++offset ) {
        nextThreeBits >>= 1;
        nextThreeBits |= bitReader.read<1>() << 2;

        /* Ignore final blocks and those with invalid compression. */
        /* Comment out to also find deflate blocks with bgz. But this alone reduces performance by factor 2!!!
         * Bgz will use another format anyway, so there should be no harm in skipping these. */
        #if 1
        if ( ( nextThreeBits & 0b001ULL ) != 0 ) {
            //std::cerr << "Filter " << offset << " because not final block (might happen often for bgz format!)\n";
            continue;
        }
        #endif

        if ( ( nextThreeBits & 0b110ULL ) == 0b110ULL ) {
            continue;
        }

        #if 1
        if ( ( ( nextThreeBits >> 1 ) & 0b11ULL ) == 0b000ULL ) {
            /* Do not use CHAR_BIT because this is a deflate constant defining a byte as 8 bits. */
            const auto nextByteOffset = ceilDiv( offset + 3, 8U );
            const auto length = ( static_cast<uint16_t>( cBuffer[nextByteOffset + 1] ) << CHAR_BIT )
                                + cBuffer[nextByteOffset];
            const auto negatedLength = ( static_cast<uint16_t>( cBuffer[nextByteOffset + 3] ) << CHAR_BIT )
                                       + cBuffer[nextByteOffset + 2];
            if ( ( length != static_cast<uint16_t>( ~negatedLength ) ) || ( length < 8*1024 ) ) {
                continue;
            }

            /** @todo check if padded bits are zero and if so, then mark all of them belonging to the same block
             *        as bit offset candidates. */
            /* Note that calling zlib on this, will do not much at all, except unnecessarily copy the bytes
             * and check the size. We can check the size ourselves. Instead, we should call zlib to try and
             * decompress the next block because uncompressed block headers have comparably fewer redundancy
             * to check against! */
            const auto nextBlockOffset = nextByteOffset + 4 + length;
            /**
             * If we can't check the next block, then for now simply do not filter it.
             * @todo keep a sliding window which can keep enough buffers, i.e., ~2 * 32kiB
             *       (32kiB is largest uncompressed block length)
             */
            if ( ( nextBlockOffset < buffer.size() * sizeof(buffer[0]) )
                 && !gzip.tryInflate( cBuffer,
                                      buffer.size() * sizeof(buffer[0]),
                                      ( nextByteOffset + 4 + length ) * 8U ) ) {
                continue;
            }

            //std::cerr << "Found uncompressed block of length " << length << " candidate at offset " << offset << " b\n";
            bitOffsets.push_back( offset );
            continue;
        }
        #endif

        /**
         * Note that stored blocks begin with 0b000 and furthermore the next value is padded to byte areas.
         * This means that we can't say for certain at which bit offset the block begins because multiple
         * can be valid because of the padding. This becomes important when matching the previous block's
         * end to this block's beginning. It would require a min,max possible range (<8)!
         */
        ++zlibTestCount;
        if ( gzip.tryInflate( reinterpret_cast<unsigned char*>( buffer.data() ),
                              buffer.size() * sizeof(buffer[0]),
                              offset ) ) {
            bitOffsets.push_back( offset );
        }
    }
    const auto t1 = now();
    const auto nBytesToTest = buffer.size() * sizeof(buffer[0]);
    std::cout << "[findDeflateBlocksZlibOptimized] Trying to find block bit offsets in " << nBytesToTest
              << " B of data took " << duration(t0, t1) << " s => "
              << ( nBytesToTest / 1e6 / duration(t0, t1) ) << " MB/s" << "\n";

    const auto totalBitOffsets = ( buffer.size() - 1 ) * sizeof( buffer[0]) * CHAR_BIT;
    std::cout << "  Needed to test with zlib " << zlibTestCount << " out of " << totalBitOffsets << " times\n";

    return bitOffsets;
}


/**
 * Same as findDeflateBlocks but uses the extracted custom gzip decoder classes.
 */
[[nodiscard]] std::vector<size_t>
findDeflateBlocksPragzip( const std::string& fileName )
{
    using DeflateBlock = pragzip::deflate::Block</* CRC32 */ false>;

    constexpr auto nBytesToTest = 1024*1024;
    const auto file = throwingOpen( fileName.c_str(), "rb" );
    BufferedFileReader::AlignedBuffer buffer( nBytesToTest + 4096, 0 );
    const auto nElementsReadFromFile = std::fread( buffer.data(), sizeof( buffer[0] ), buffer.size(), file.get() );
    buffer.resize( nElementsReadFromFile );
    pragzip::BitReader bitReader( std::make_unique<BufferedFileReader>( std::move( buffer ) ) );

    std::vector<size_t> bitOffsets;
    GzipWrapper gzip( GzipWrapper::Format::RAW );

    pragzip::deflate::Block block;
    const auto t0 = now();
    for ( size_t offset = 0; offset <= nBytesToTest * CHAR_BIT; ++offset ) {
        bitReader.seek( offset );
        try
        {
        #if 0
            /* Unfortunately, this version with peek is ~5% slower */
            const auto isLastBlock = bitReader.peek<1>() != 0;
            if ( isLastBlock ) {
                bitReader.seekAfterPeek( 1 );
                continue;
            }
            auto error = block.readHeader( bitReader );
        #else
            auto error = block.readHeader</* count last block as error */ true>( bitReader );
        #endif
            if ( error != pragzip::Error::NONE ) {
                continue;
            }

            /* Ignoring fixed Huffman compressed blocks speeds up finding blocks by more than 3x!
             * This is probably because there is very few metadata to check in this case and it begins
             * decoding immediately, which has a much rarer error rate on random data. Fixed Huffman
             * is used by GNU gzip for highly compressible (all zeros) or very short data.
             * However, because of this reason, this compression type should be rather rare!
             * Because such blocks are also often only several dozens of bytes large. So, for all of the
             * blocks in 10MiB of data to use fixed Huffman coding, the encoder is either not finished yet
             * and simply can't encode dynamic Huffman blocks or we have a LOT of highly compressible data,
             * to be specific 10 GiB of uncompressed data because of the maximum compression ratio of 1032.
             * @see https://stackoverflow.com/questions/16792189/gzip-compression-ratio-for-zeros/16794960#16794960 */
            if ( block.compressionType() == DeflateBlock::CompressionType::FIXED_HUFFMAN ) {
                continue;
            }

            /* Testing decoding is not necessary because the Huffman canonical check is already very strong!
             * Decoding up to 8 kiB like in pugz only impedes performance and it is harder to reuse that already
             * decoded data if we do decide that it is a valid block. The number of checks during reading is also
             * pretty few because there almost are no wasted / invalid symbols. */
            bitOffsets.push_back( offset );
        } catch ( const std::exception& exception ) {
            /* Should only happen when we reach the end of the file! */
            std::cerr << "Caught exception: " << exception.what() << "\n";
        }
    }
    const auto t1 = now();
    std::cout << "[findDeflateBlocksPragzip] Trying to find block bit offsets in " << nBytesToTest
              << " B of data took " << duration(t0, t1) << " s => "
              << ( nBytesToTest / 1e6 / duration(t0, t1) ) << " MB/s" << "\n";

    return bitOffsets;
}


void
createRandomBase64( const std::string& filePath,
                    const size_t       fileSize )
{
    constexpr std::string_view BASE64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz01234567890+/";
    std::ofstream file{ filePath };
    for ( size_t i = 0; i < fileSize; ++i ) {
        file << ( ( i + 1 == fileSize ) || ( ( i + 1 ) % 77 == 0 )
                  ? '\n' : BASE64[static_cast<size_t>( rand() ) % BASE64.size()] );
    }
}


[[nodiscard]] TemporaryDirectory
createTemporaryDirectory()
{
    const std::filesystem::path tmpFolderName = "pragzip.benchmarkGzipBlockFinder." + std::to_string( unixTime() );
    std::filesystem::create_directory( tmpFolderName );
    return tmpFolderName;
}


void
benchmarkGzip( const std::string& fileName )
{
    /* Ground truth offsets. */
    const auto [streamOffsets, blockOffsets] = parseWithZlib( fileName );
    std::cout << "Gzip streams (" << streamOffsets.size() << "): " << streamOffsets << "\n";
    std::cout << "Deflate blocks (" << blockOffsets.size() << "): " << blockOffsets << "\n\n";

    {
        const auto blockCandidateOffsets = findDeflateBlocksZlib( fileName );
        std::cout << "  Block candidates using naive zlib (" << blockCandidateOffsets.size() << "): "
                  << blockCandidateOffsets << "\n\n";
    }

    {
        const auto blockCandidateOffsets = findDeflateBlocksZlibOptimized( fileName );
        std::cout << "  Block candidates using zlib with shortcuts (" << blockCandidateOffsets.size() << "): "
                  << blockCandidateOffsets << "\n\n";
    }

    const auto blockCandidateOffsetsPragzip = findDeflateBlocksPragzip( fileName );
    std::cout << "  Block candidates pragzip (" << blockCandidateOffsetsPragzip.size() << "): "
              << blockCandidateOffsetsPragzip << "\n\n";

    std::vector<size_t> blockSizes( blockOffsets.size() );
    std::adjacent_difference( blockOffsets.begin(), blockOffsets.end(), blockSizes.begin() );
    assert( !blockSizes.empty() );
    blockSizes.erase( blockSizes.begin() );  /* adjacent_difference begins writing at output begin + 1! */

    const Histogram<size_t> sizeHistogram{ blockSizes, 10, "b" };

    std::cout << "Block size distribution: min: " << sizeHistogram.statistics().min / CHAR_BIT << " B"
              << ", avg: " << sizeHistogram.statistics().average() / CHAR_BIT << " B"
              << " +- " << sizeHistogram.statistics().standardDeviation() / CHAR_BIT << " B"
              << ", max: " << sizeHistogram.statistics().max / CHAR_BIT << " B\n";

    std::cout << "Block Size Distribution (small to large):\n" << sizeHistogram.plot() << "\n";

    const auto bgzOffsets = findBgzStreams( fileName );
    if ( !bgzOffsets.empty() ) {
        std::cout << "Found " << bgzOffsets.size() << " bgz streams!\n" << bgzOffsets << "\n\n";
    }

    const auto gzipStreams = findGzipStreams( fileName );
    if ( !gzipStreams.empty() ) {
        std::cout << "Found " << gzipStreams.size() << " gzip stream candidates!\n" << gzipStreams << "\n\n";
    }

    std::cout << "\n";
}


int
main( int    argc,
      char** argv )
{
    if ( argc > 1 ) {
        for ( int i = 1; i < argc; ++i ) {
            if ( std::filesystem::exists( argv[i] ) ) {
                benchmarkGzip( argv[i] );
            }
        }
    }

    const auto tmpFolder = createTemporaryDirectory();
    const std::string fileName = std::filesystem::absolute( tmpFolder.path() / "random-base64" );

    const std::vector<std::tuple<std::string, std::string, std::string, std::string > > testEncoders = {
        { "bgzip", "bgzip --version", "bgzip --force", "bgzip" },
        { "gzip", "gzip --version", "gzip -k --force", "gzip" },
        { "pigz", "pigz --version", "pigz -k --force", "pigz" },
        { "igzip", "igzip --version", "igzip -k --force", "igzip" },
        { "Python3 gzip", "python3 --version", "python3 -m gzip", "python3-gzip" },
        { "Python3 pgzip", "python3 -m pip show pgzip", "python3 -m pgzip", "python3-pgzip" },
    };

    try
    {
        for ( const auto& [name, getVersion, command, extension] : testEncoders ) {

            std::cout << "=== Testing with encoder: " << name << " ===\n\n";

            std::cout << "> " << getVersion << "\n";
            [[maybe_unused]] const auto versionReturnCode = std::system( ( getVersion + " > out" ).c_str() );
            std::cout << std::ifstream( "out" ).rdbuf();
            std::cout << "\n";

            /* Check for the uncompressed file inside the loop because "bgzip" does not have a --keep option!
             * https://github.com/samtools/htslib/pull/1331 */
            if ( !std::filesystem::exists( fileName ) ) {
                createRandomBase64( fileName, 16UL * 1024UL * 1024UL );
            }

            /* Python3 module pgzip does not create the .gz file beside the input file but in the current directory,
             * so change current directory to the input file first. */
            const auto oldCWD = std::filesystem::current_path();
            std::filesystem::current_path( tmpFolder );

            const auto fullCommand = command + " " + fileName;
            const auto returnCode = std::system( fullCommand.c_str() );

            std::filesystem::current_path( oldCWD );

            if ( returnCode != 0 ) {
                std::cerr << "Failed to encode the temporary file with: " << fullCommand << "\n";
                continue;
            }

            if ( !std::filesystem::exists( fileName + ".gz" ) ) {
                std::cerr << "Encoded file was not found!\n";
                continue;
            }

            std::filesystem::rename( fileName + ".gz", fileName + "." + extension );
            benchmarkGzip( fileName + "." + extension );
        }
    } catch ( const std::exception& exception ) {
        /* Note that the destructor for TemporaryDirectory might not be called for uncaught exceptions!
         * @see https://stackoverflow.com/questions/222175/why-destructor-is-not-called-on-exception */
        std::cerr << "Caught exception: " << exception.what() << "\n";
        return 1;
    }

    return 0;
}


/*
cmake --build . -- benchmarkGzipBlockFinder && tests/benchmarkGzipBlockFinder

=== Testing with encoder: gzip ===

> gzip --version
gzip 1.10
Copyright (C) 2018 Free Software Foundation, Inc.
Copyright (C) 1993 Jean-loup Gailly.
This is free software.  You may redistribute copies of it under the terms of
the GNU General Public License <https://www.gnu.org/licenses/gpl.html>.
There is NO WARRANTY, to the extent permitted by law.

Written by Jean-loup Gailly.

Gzip streams (2):  0 12748064
Deflate blocks (495):  192 205414 411532 617749 824122 1029728 1236300 1442840 1649318 1855554 2061582 2267643 2473676 2679825 2886058 ...

[findDeflateBlocksZlib] Trying to find block bit offsets in 1048576 B of data took 5.52473 s => 0.189797 MB/s
  Block candidates using naive zlib (71):  192 205414 411532 617749 824122 1028344 1028348 1028349 1029728 1236300 1442840 1572611 1572612 1641846 1641847 ...

[findDeflateBlocksZlibOptimized] Trying to find block bit offsets in 1048576 B of data took 1.62282 s => 0.646144 MB/s
  Needed to test with zlib 2052965 out of 8388576 times
  Block candidates using zlib with shortcuts (41):  192 205414 411532 617749 824122 1029728 1236300 1442840 1649318 1855554 2061582 2267643 2473676 2679825 2886058 ...

[findDeflateBlocksPragzip] Trying to find block bit offsets in 1048576 B of data took 0.250924 s => 4.17886 MB/s
  Block candidates pragzip (44):  192 205414 411532 617749 824122 1028349 1029728 1236300 1442840 1649318 1855554 2061582 2267643 2473676 2679825 ...

Block size distribution: min: 0 B, avg: 25783.4 B +- 1161.87 B, max: 25888 B
Block Size Distribution (small to large):
     0 b |
         |
         |
         |
         |
         |
         |
         |
         |
207110 b |==================== (494)


=== Testing with encoder: pigz ===

> pigz --version
pigz 2.6

Gzip streams (2):  0 12761091
Deflate blocks (1195):  192 102374 205527 308631 411790 515077 618182 721566 797442 797452 797462 797472 900531 1003441 1106502 ...

[findDeflateBlocksZlib] Trying to find block bit offsets in 1048576 B of data took 5.516 s => 0.190097 MB/s
  Block candidates using naive zlib (104):  192 102374 205527 234702 234703 234706 234707 308631 411790 515077 618182 721566 797472 900531 1003441 ...

[findDeflateBlocksZlibOptimized] Trying to find block bit offsets in 1048576 B of data took 1.61422 s => 0.649585 MB/s
  Needed to test with zlib 2053986 out of 8388576 times
  Block candidates using zlib with shortcuts (87):  192 102374 205527 308631 411790 515077 618182 721566 797472 900531 1003441 1106502 1209841 1313251 1416637 ...

[findDeflateBlocksPragzip] Trying to find block bit offsets in 1048576 B of data took 0.253108 s => 4.1428 MB/s
  Block candidates pragzip (111):  192 102374 205527 308631 411790 515077 618182 721566 797472 900531 1003441 1106502 1209841 1313251 1416637 ...

Block size distribution: min: 0 B, avg: 10679.8 B +- 4509 B, max: 12979 B
Block Size Distribution (small to large):
     0 b |===                  (171)
         |
         |
         |
         |
         |
         |
         |==                   (127)
         |
103838 b |==================== (896)


=== Testing with encoder: igzip ===

> igzip --version
igzip command line interface 2.30.0

Gzip streams (2):  0 12669134
Deflate blocks (129):  1136 790905 1580736 2370674 3160686 3950671 4740448 5530378 6321349 7112718 7903168 8692985 9482887 10274151 11065651 ...

[findDeflateBlocksZlib] Trying to find block bit offsets in 1048576 B of data took 5.47061 s => 0.191675 MB/s
  Block candidates using naive zlib (19):  1136 790905 1139766 1173134 1580736 1702286 1702289 1702290 2370674 3160686 3950671 4740448 5530378 6321349 7112718 ...

[findDeflateBlocksZlibOptimized] Trying to find block bit offsets in 1048576 B of data took 1.59908 s => 0.655736 MB/s
  Needed to test with zlib 2048097 out of 8388576 times
  Block candidates using zlib with shortcuts (12):  1136 790905 1580736 2370674 3160686 3950671 4740448 5530378 6321349 7112718 7903168 8069446

[findDeflateBlocksPragzip] Trying to find block bit offsets in 1048576 B of data took 0.252561 s => 4.15178 MB/s
  Block candidates pragzip (11):  1136 790905 1580736 2370674 3160686 3950671 4740448 5530378 6321349 7112718 7903168

Block size distribution: min: 0 B, avg: 98870.4 B +- 8773.68 B, max: 98950 B
Block Size Distribution (small to large):
     0 b |
         |
         |
         |
         |
         |
         |
         |
         |
791606 b |==================== (128)


=== Testing with encoder: bgzip ===

> bgzip --version
bgzip (htslib) 1.13+ds
Copyright (C) 2021 Genome Research Ltd.

Got 6 B of FEXTRA field!
Gzip streams (260):  0 50481 100948 151434 201908 252370 302849 353305 403788 454267 504746 555197 605656 656134 706610 ...
Deflate blocks (259):  144 403992 807728 1211616 1615408 2019104 2422936 2826584 3230448 3634280 4038112 4441720 4845392 5249216 5653024 ...

[findDeflateBlocksZlib] Trying to find block bit offsets in 1048576 B of data took 5.74974 s => 0.182369 MB/s
  Block candidates using naive zlib (35):  144 403992 807728 1211616 1615408 2019104 2422936 2826584 3230448 3634280 4038112 4431917 4441720 4675542 4675545 ...

[findDeflateBlocksZlibOptimized] Trying to find block bit offsets in 1048576 B of data took 1.64014 s => 0.639321 MB/s
  Needed to test with zlib 2021401 out of 8388576 times
  Block candidates using zlib with shortcuts (0):

[findDeflateBlocksPragzip] Trying to find block bit offsets in 1048576 B of data took 0.240549 s => 4.35909 MB/s
  Block candidates pragzip (0):

Block size distribution: min: 0 B, avg: 50276.8 B +- 4428.77 B, max: 50494 B
Block Size Distribution (small to large):
     0 b |                     (1)
         |
         |
         |
         |
         |
         |
         |
         |
403952 b |==================== (257)

[findBgzStreams] Trying to find block bit offsets in 12 MiB of data took 0.0006563 s => 19764.5 MB/s
Found 259 bgz streams!
 144 403992 807728 1211616 1615408 2019104 2422936 2826584 3230448 3634280 4038112 4441720 4845392 5249216 5653024 ...

Found 259 gzip stream candidates!
 0 50481 100948 151434 201908 252370 302849 353305 403788 454267 504746 555197 605656 656134 706610 ...


=== Testing with encoder: Python3 gzip ===

> python3 --version
Python 3.9.7

Gzip streams (2):  0 12759547
Deflate blocks (989):  192 102672 205833 308639 411748 515132 618285 721612 824892 928415 1031456 1134888 1238197 1341253 1444122 ...

[findDeflateBlocksZlib] Trying to find block bit offsets in 1048576 B of data took 5.52907 s => 0.189648 MB/s
  Block candidates using naive zlib (114):  192 102672 194239 194240 194241 194242 194245 205833 308639 411748 515132 618285 721612 824892 928415 ...

[findDeflateBlocksZlibOptimized] Trying to find block bit offsets in 1048576 B of data took 1.57387 s => 0.666239 MB/s
  Needed to test with zlib 2053335 out of 8388576 times
  Block candidates using zlib with shortcuts (81):  192 102672 205833 308639 411748 515132 618285 721612 824892 928415 1031456 1134888 1238197 1341253 1444122 ...

[findDeflateBlocksPragzip] Trying to find block bit offsets in 1048576 B of data took 0.255064 s => 4.11102 MB/s
  Block candidates pragzip (84):  192 102672 205833 308639 411748 515132 618285 721612 824892 928415 1031456 1134888 1238197 1341253 1444122 ...

Block size distribution: min: 0 B, avg: 12903 B +- 411.611 B, max: 12999 B
Block Size Distribution (small to large):
     0 b |
         |
         |
         |
         |
         |
         |
         |
         |
103999 b |==================== (988)


=== Testing with encoder: Python3 pgzip ===

> python3 -m pip show pgzip
Name: pgzip
Version: 0.3.1
Summary: A multi-threading implementation of Python gzip module
Home-page: https://github.com/pgzip/pgzip
Author: pgzip team
Author-email: pgzip@thegoldfish.org
License: MIT
Location: /home/hypatia/.local/lib/python3.9/site-packages
Requires:
Required-by:

Got 8 B of FEXTRA field!
Gzip streams (2):  0 12747800
Deflate blocks (495):  272 205800 411533 617885 824269 1030628 1237131 1442923 1649106 1855109 2061199 2267938 2473926 2680186 2886437 ...

[findDeflateBlocksZlib] Trying to find block bit offsets in 1048576 B of data took 5.42939 s => 0.19313 MB/s
  Block candidates using naive zlib (60):  272 205800 411533 617885 824269 1030628 1164656 1237131 1442923 1649106 1771228 1855109 2061199 2267938 2311838 ...

[findDeflateBlocksZlibOptimized] Trying to find block bit offsets in 1048576 B of data took 1.56031 s => 0.672031 MB/s
  Needed to test with zlib 2049711 out of 8388576 times
  Block candidates using zlib with shortcuts (41):  272 205800 411533 617885 824269 1030628 1237131 1442923 1649106 1855109 2061199 2267938 2473926 2680186 2886437 ...

[findDeflateBlocksPragzip] Trying to find block bit offsets in 1048576 B of data took 0.252826 s => 4.14743 MB/s
  Block candidates pragzip (44):  272 205800 411533 617885 824269 1030628 1237131 1442923 1649106 1855109 2061199 2267938 2347916 2347917 2473926 ...

Block size distribution: min: 0 B, avg: 25782.9 B +- 1161.81 B, max: 25890 B
Block Size Distribution (small to large):
     0 b |
         |
         |
         |
         |
         |
         |
         |
         |
207124 b |==================== (494)
*/
