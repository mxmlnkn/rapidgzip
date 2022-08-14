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
#include <type_traits>
#include <vector>

#include <zlib.h>

#include <BitReader.hpp>
#include <blockfinder/Bgzf.hpp>
#include <blockfinder/DynamicHuffman.hpp>
#include <blockfinder/PigzParallel.hpp>
#include <common.hpp>
#include <filereader/Buffered.hpp>
#include <pragzip.hpp>
#include <Statistics.hpp>
#include <TestHelpers.hpp>


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
    const auto file = throwingOpen( fileName, "rb" );

    static constexpr auto bufferSize = 4 * 1024 * 1024;
    std::vector<char> buffer( bufferSize, 0 );

    std::vector<size_t> streamOffsets;
    size_t totalBytesRead = 0;
    while ( true )
    {
        const auto bytesRead = fread( buffer.data(), sizeof( char ), buffer.size(), file.get() );
        if ( bytesRead == 0 ) {
            break;
        }

        for ( size_t i = 0; i + 8 < bytesRead; ++i ) {
            if ( ( buffer[i + 0] == (char)0x1F )
                 && ( buffer[i + 1] == (char)0x8B )
                 && ( buffer[i + 2] == (char)0x08 )
                 && ( buffer[i + 3] == (char)0x04 )
                 && ( buffer[i + 4] == (char)0x00 )  // this is assuming the mtime is zero, which obviously can differ!
                 && ( buffer[i + 5] == (char)0x00 )
                 && ( buffer[i + 6] == (char)0x00 )
                 && ( buffer[i + 7] == (char)0x00 )
                 && ( buffer[i + 8] == (char)0x00 ) ) {
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

    try {
        pragzip::blockfinder::Bgzf blockFinder( std::make_unique<StandardFileReader>( fileName ) );

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

    return streamOffsets;
}


/**
 * @see https://github.com/madler/zlib/blob/master/examples/zran.c
 */
[[nodiscard]] std::pair<std::vector<size_t>, std::vector<size_t> >
parseWithZlib(const std::string& fileName)
{
    const auto file = throwingOpen( fileName, "rb" );

    std::vector<size_t> streamOffsets;
    std::vector<size_t> blockOffsets;

    static constexpr int BUFFER_SIZE = 1024 * 1024;
    static constexpr int WINDOW_SIZE = 32 * 1024;

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
    std::array<unsigned char, BUFFER_SIZE> input{};
    std::array<unsigned char, WINDOW_SIZE> window{};

    /* initialize inflate */
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = 0;
    stream.next_in = Z_NULL;

    const auto throwCode = [] ( const auto code ) { throw std::domain_error( std::to_string( code ) ); };

    /* Second argument is window bits. log2 base of window size. Adding 32 to that (setting the 5-th bit),
     * means that automatic zlib or gzip decoding is detected. */
    auto ret = inflateInit2(&stream, 32 + 15);
    if ( ret != Z_OK ) {
        throwCode( ret );
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
        throwCode( ret );
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
        stream.avail_in = std::fread( input.data(), 1, input.size(), file.get() );
        if ( ( stream.avail_in == 0 ) && ( std::feof( file.get() ) != 0 ) ) {
            break;
        }
        if ( std::ferror( file.get() ) != 0 ) {
            throwCode( Z_ERRNO );
        }
        if ( stream.avail_in == 0 ) {
            throwCode( Z_DATA_ERROR );
        }
        stream.next_in = input.data();

        /* process all of that, or until end of stream */
        while ( stream.avail_in != 0 )
        {
            /* reset sliding window if necessary */
            if ( stream.avail_out == 0 ) {
                stream.avail_out = window.size();
                stream.next_out = window.data();
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
                throwCode( ret );
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
            const auto bits = static_cast<std::make_unsigned_t<decltype( stream.data_type )> >( stream.data_type );
            if ( ( ( bits & 128U ) != 0 ) && ( ( bits & 64U ) == 0 ) ) {
                blockOffsets.push_back( totin * 8U - ( bits & 7U ) );
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
    static constexpr int WINDOW_SIZE = 32 * 1024;

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

        m_stream.msg = nullptr;

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
            throw std::domain_error( std::to_string( ret ) );
        }
    }

    GzipWrapper( const GzipWrapper& ) = delete;
    GzipWrapper( GzipWrapper&& ) = delete;
    GzipWrapper& operator=( GzipWrapper&& ) = delete;
    GzipWrapper& operator=( GzipWrapper& ) = delete;

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
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
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

            auto errorCode = inflatePrime( &m_stream, static_cast<int>( 8U - bitsToSeek ),
                                           compressed[byteOffset] >> bitsToSeek );
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
        return nBytesDecoded >= outputPreviouslyAvailable;
    }

private:
    z_stream m_stream{};
    std::vector<unsigned char> m_window = std::vector<unsigned char>( 32ULL * 1024ULL, '\0' );
    std::vector<unsigned char> m_outputBuffer = std::vector<unsigned char>( 64ULL * 1024ULL * 1024ULL );
};


[[nodiscard]] std::vector<size_t>
findDeflateBlocksZlib( BufferedFileReader::AlignedBuffer buffer )
{
    std::vector<size_t> bitOffsets;
    GzipWrapper gzip( GzipWrapper::Format::RAW );

    for ( size_t offset = 0; offset <= ( buffer.size() - 1 ) * sizeof( buffer[0] ) * CHAR_BIT; ++offset ) {
        if ( gzip.tryInflate( reinterpret_cast<unsigned char*>( buffer.data() ),
                              buffer.size() * sizeof( buffer[0] ),
                              offset ) ) {
            bitOffsets.push_back( offset );
        }
    }
    return bitOffsets;
}


[[nodiscard]] std::vector<size_t>
findDeflateBlocksZlibOptimized( BufferedFileReader::AlignedBuffer buffer )
{
    pragzip::BitReader bitReader( std::make_unique<BufferedFileReader>( buffer ) );

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

    for ( size_t offset = 0; offset <= ( buffer.size() - 1 ) * sizeof( buffer[0] ) * CHAR_BIT; ++offset ) {
        nextThreeBits >>= 1U;
        nextThreeBits |= bitReader.read<1>() << 2U;

        /* Ignore final blocks and those with invalid compression. */
        /* Comment out to also find deflate blocks with bgz. But this alone reduces performance by factor 2!!!
         * Bgz will use another format anyway, so there should be no harm in skipping these. */
        if ( ( nextThreeBits & 0b001ULL ) != 0 ) {
            continue;
        }

        /* Filter out reserved block compression type. */
        if ( ( nextThreeBits & 0b110ULL ) == 0b110ULL ) {
            continue;
        }

        #if 1
        /* Check for uncompressed blocks. */
        if ( ( ( nextThreeBits >> 1U ) & 0b11ULL ) == 0b000ULL ) {
            /* Do not use CHAR_BIT because this is a deflate constant defining a byte as 8 bits. */
            const auto nextByteOffset = ceilDiv( offset + 3, 8U );
            const auto length = ( static_cast<uint16_t>( cBuffer[nextByteOffset + 1] )
                                  << static_cast<uint8_t>( CHAR_BIT ) )
                                + cBuffer[nextByteOffset];
            const auto negatedLength = static_cast<uint16_t>(
                ( static_cast<uint16_t>( cBuffer[nextByteOffset + 3] )
                  << static_cast<uint8_t>( CHAR_BIT ) )
                + cBuffer[nextByteOffset + 2] );
            if ( ( length != static_cast<uint16_t>( ~negatedLength ) ) || ( length < 8 * 1024 ) ) {
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
            if ( ( nextBlockOffset < buffer.size() * sizeof( buffer[0] ) )
                 && !gzip.tryInflate( cBuffer,
                                      buffer.size() * sizeof( buffer[0] ),
                                      ( nextByteOffset + 4 + length ) * 8U ) ) {
                continue;
            }

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
                              buffer.size() * sizeof( buffer[0] ),
                              offset ) ) {
            bitOffsets.push_back( offset );
        }
    }

    //const auto totalBitOffsets = ( buffer.size() - 1 ) * sizeof( buffer[0] ) * CHAR_BIT;
    //std::cout << "[findDeflateBlocksZlibOptimized] Needed to test with zlib " << zlibTestCount << " out of "
    //          << totalBitOffsets << " times\n";

    return bitOffsets;
}


[[nodiscard]] std::vector<size_t>
findDeflateBlocksPragzip( BufferedFileReader::AlignedBuffer buffer )
{
    using DeflateBlock = pragzip::deflate::Block</* CRC32 */ false>;

    const auto nBitsToTest = buffer.size() * CHAR_BIT;
    pragzip::BitReader bitReader( std::make_unique<BufferedFileReader>( std::move( buffer ) ) );

    std::vector<size_t> bitOffsets;

    pragzip::deflate::Block block;
    for ( size_t offset = 0; offset <= nBitsToTest; ++offset ) {
        bitReader.seek( static_cast<long long int>( offset ) );
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

            if ( block.compressionType() == DeflateBlock::CompressionType::UNCOMPRESSED ) {
                /* Ignore uncompressed blocks for comparability with the version using a LUT. */
                //std::cerr << "Uncompressed block candidate: " << offset << "\n";
                continue;
            }

            /* Testing decoding is not necessary because the Huffman canonical check is already very strong!
             * Decoding up to 8 kiB like in pugz only impedes performance and it is harder to reuse that already
             * decoded data if we do decide that it is a valid block. The number of checks during reading is also
             * pretty few because there almost are no wasted / invalid symbols. */
            bitOffsets.push_back( offset );
        } catch ( const pragzip::BitReader::EndOfFileReached& ) {
            break;
        }
    }
    return bitOffsets;
}


/**
 * Same as findDeflateBlocksPragzip but prefilters calling pragzip using a lookup table and even skips multiple bits.
 * Also, does not find uncompressed blocks nor fixed huffman blocks and as the others no final blocks!
 * The idea is that fixed huffman blocks should be very rare and uncompressed blocks can be found very fast in a
 * separate run over the data (to be implemented).
 */
template<uint8_t CACHED_BIT_COUNT>
[[nodiscard]] std::vector<size_t>
findDeflateBlocksPragzipLUT( BufferedFileReader::AlignedBuffer data )
{
    const size_t nBitsToTest = data.size() * CHAR_BIT;
    pragzip::BitReader bitReader( std::make_unique<BufferedFileReader>( std::move( data ) ) );

    std::vector<size_t> bitOffsets;

    using namespace pragzip::blockfinder;
    static constexpr auto nextDeflateCandidateLUT = createNextDeflateCandidateLUT<CACHED_BIT_COUNT>();

    pragzip::deflate::Block block;
    for ( size_t offset = 0; offset <= nBitsToTest; ) {
        bitReader.seek( static_cast<long long int>( offset ) );

        try
        {
            const auto peeked = bitReader.peek<CACHED_BIT_COUNT>();
            const auto nextPosition = nextDeflateCandidateLUT[peeked];  // ~8 MB/s
            //const auto nextPosition = nextDeflateCandidate<CACHED_BIT_COUNT>( peeked );  // ~6 MB/s (slower than LUT!)

            /* If we can skip forward, then that means that the new position only has been partially checked.
             * Therefore, rechecking the LUT for non-zero skips not only ensures that we aren't wasting time in
             * readHeader but it also ensures that we can avoid checking the first three bits again inside readHeader
             * and instead start reading and checking the dynamic Huffman code directly! */
            if ( nextPosition > 0 ) {
                bitReader.seekAfterPeek( nextPosition );
                offset += nextPosition;
                continue;
            }

            bitReader.seekAfterPeek( 3 );
            auto error = block.readDynamicHuffmanCoding( bitReader );
            if ( error != pragzip::Error::NONE ) {
                ++offset;
                continue;
            }

            /* Testing decoding is not necessary because the Huffman canonical check is already very strong!
             * Decoding up to 8 kiB like in pugz only impedes performance and it is harder to reuse that already
             * decoded data if we do decide that it is a valid block. The number of checks during reading is also
             * pretty few because there almost are no wasted / invalid symbols. */
            bitOffsets.push_back( offset );
            ++offset;
        } catch ( const pragzip::BitReader::EndOfFileReached& ) {
            /* This might happen when calling readDynamicHuffmanCoding quite some bytes before the end! */
            break;
        }
    }

    return bitOffsets;
}


/**
 * Same as findDeflateBlocksPragzipLUT but tries to improve pipelining by going over the data twice.
 * Once, doing simple Boyer-Moore-like string search tests and skips forward and the second time doing
 * extensive tests by loading and checking the dynamic Huffman trees, which might require seeking back.
 */
template<uint8_t CACHED_BIT_COUNT>
[[nodiscard]] std::vector<size_t>
findDeflateBlocksPragzipLUTTwoPass( BufferedFileReader::AlignedBuffer data )
{
    static_assert( CACHED_BIT_COUNT >= 13,
                   "The LUT must check at least 13-bits, i.e., up to including the distance "
                   "code length check, to avoid duplicate checks in the precode check!" );

    const size_t nBitsToTest = data.size() * CHAR_BIT;
    pragzip::BitReader bitReader( std::make_unique<BufferedFileReader>( std::move( data ) ) );

    std::vector<size_t> bitOffsetCandidates;

    using namespace pragzip::blockfinder;
    static constexpr auto nextDeflateCandidateLUT = createNextDeflateCandidateLUT<CACHED_BIT_COUNT>();

    for ( size_t offset = 0; offset <= nBitsToTest; ) {
        try {
            const auto nextPosition = nextDeflateCandidateLUT[bitReader.peek<CACHED_BIT_COUNT>()];
            if ( nextPosition == 0 ) {
                bitOffsetCandidates.push_back( offset );
                ++offset;
                bitReader.seekAfterPeek( 1 );
            } else {
                offset += nextPosition;
                bitReader.seekAfterPeek( nextPosition );
            }
        } catch ( const pragzip::BitReader::EndOfFileReached& ) {
            break;
        }
    }

    std::vector<size_t> bitOffsets;

    constexpr bool ENABLE_ANALYSIS{ false };
    pragzip::deflate::Block</* CRC32 */ false, ENABLE_ANALYSIS> block;
    std::unordered_map<pragzip::Error, uint64_t> errorCounts;

    const auto checkPrecode =
        [] ( pragzip::BitReader& bitReaderAtPrecode )
        {
            const auto codeLengthCount = 4 + bitReaderAtPrecode.read<4>();

            constexpr auto MAX_CL_SYMBOL_COUNT = 19U;
            constexpr auto CL_CODE_LENGTH_BIT_COUNT = 3U;
            constexpr auto MAX_CL_CODE_LENGTH = ( 1U << CL_CODE_LENGTH_BIT_COUNT ) - 1U;
            static constexpr std::array<uint8_t, MAX_CL_SYMBOL_COUNT> alphabetOrderC =
                { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            /* The index of this array is the symbol and the content is the code length. */
            std::array<uint8_t, MAX_CL_SYMBOL_COUNT> codeLengths = {};
            for ( size_t i = 0; i < codeLengthCount; ++i ) {
                codeLengths[alphabetOrderC[i]] = bitReaderAtPrecode.read<CL_CODE_LENGTH_BIT_COUNT>();
            }

            const auto maxCodeLength = getMax( codeLengths );
            const auto minCodeLength = getMinPositive( codeLengths );

            using HuffmanCode = uint8_t;
            std::array<HuffmanCode, MAX_CL_CODE_LENGTH + 1> bitLengthFrequencies = {};
            for ( const auto value : codeLengths ) {
                ++bitLengthFrequencies[value];
            }

            const auto nonZeroCount = codeLengths.size() - bitLengthFrequencies[0];
            HuffmanCode unusedSymbolCount = HuffmanCode( 1 ) << minCodeLength;
            for ( size_t bitLength = minCodeLength; bitLength <= maxCodeLength; ++bitLength ) {
                const auto frequency = bitLengthFrequencies[bitLength];
                if ( frequency > unusedSymbolCount ) {
                    return pragzip::Error::INVALID_CODE_LENGTHS;
                }
                unusedSymbolCount -= frequency;
                unusedSymbolCount *= 2;  /* Because we go down one more level for all unused tree nodes! */
            }

            if ( ( ( nonZeroCount == 1 ) && ( unusedSymbolCount >  1 ) ) ||
                 ( ( nonZeroCount >  1 ) && ( unusedSymbolCount != 0 ) ) ) {
                return pragzip::Error::BLOATING_HUFFMAN_CODING;
            }

            return pragzip::Error::NONE;
        };

    const auto checkOffset =
        [&] ( const auto offset )
        {
            /* Check the precode Huffman coding. We can skip a lot of the generic tests done in deflate::Block
             * because this is only called for offsets prefiltered by the LUT. But, this also means that the
             * LUT size must be at least 13-bit! */
            try {
                bitReader.seek( static_cast<long long int>( offset ) + 13 );
                const auto error = checkPrecode( bitReader );

                if ( error != pragzip::Error::NONE ) {
                    if constexpr ( ENABLE_ANALYSIS ) {
                        const auto [count, wasInserted] = errorCounts.try_emplace( error, 1 );
                        if ( !wasInserted ) {
                            count->second++;
                        }
                    }

                    return false;
                }
            } catch ( const pragzip::BitReader::EndOfFileReached& ) {}

            try {
                bitReader.seek( static_cast<long long int>( offset ) + 3 );
                const auto error = block.readDynamicHuffmanCoding( bitReader );

                if constexpr ( ENABLE_ANALYSIS ) {
                    const auto [count, wasInserted] = errorCounts.try_emplace( error, 1 );
                    if ( !wasInserted ) {
                        count->second++;
                    }
                }

                return error == pragzip::Error::NONE;
            } catch ( const pragzip::BitReader::EndOfFileReached& ) {}
            return false;
        };

    std::copy_if( bitOffsetCandidates.begin(), bitOffsetCandidates.end(),
                  std::back_inserter( bitOffsets ), checkOffset );

#if 0  // constexpr is not strong enough to avoid compile errors for the missing members if ENABLE_ANALYSIS = false :/
    if constexpr ( ENABLE_ANALYSIS ) {
        /* From 134'217'728 bits to test, found 14'220'922 candidates and reduced them down further to 652 */
        std::cerr << "From " << nBitsToTest << " bits to test, found " << bitOffsetCandidates.size()
                  << " candidates and reduced them down further to " << bitOffsets.size() << "\n";

        /**
         * @verbatim
         * Invalid Precode  HC: 10750095
         * Invalid Distance HC: 8171
         * Invalid Symbol   HC: 76
         * @endverbatim
         * This signifies a LOT of optimization potential! We might be able to handle precode checks faster!
         * Note that the maximum size of the precode coding can only be 3*19 bits = 57 bits!
         *  -> Note that BitReader::peek should be able to peek all of these on a 64-bit system even when only able to
         *     append full bytes to the 64-bit buffer because 64-57=7! I.e., 57 is the first case for which it wouldn't
         *     be able to further add to the bit buffer but anything smaller and it is able to insert a full byte!
         *     Using peek can avoid costly buffer-refilling seeks back!
         *     -> Unfortunately, we also have to seek back the 17 bits for the deflate block header and the three
         *        code lengths. So yeah, using peek probably will do nothing.
         */
        std::cerr << "Reading dynamic Huffman Code (HC) deflate block failed because the code lengths were invalid:\n"
                  << "    Invalid Precode  HC: " << block.failedPrecodeInit   << "\n"
                  << "    Invalid Distance HC: " << block.failedDistanceInit << "\n"
                  << "    Invalid Symbol   HC: " << block.failedLengthInit   << "\n";

        /**
         * @verbatim
         *  5 : 657612
         *  5 : 658795
         *  6 : 655429
         *  7 : 667649
         *  8 : 656510
         *  9 : 656660
         * 10 : 649638
         * 11 : 705195
         * 12 : 663376
         * 13 : 662214
         * 14 : 659557
         * 15 : 678195
         * 16 : 670389
         * 17 : 681204
         * 18 : 699319
         * 19 : 771474
         * @endverbatim
         * Because well compressed data is quasirandom, the distribution of the precode code lengths is also pretty even.
         * It is weird, that exactly the longest case appears much more often than the others, same for 7. This means
         * that runs of 1s seem to be more frequent than other things.
         * Unfortunately, this means that a catch-all LUT does not seem feasible.
         */
        std::cerr << "Precode CL count:\n";
        for ( size_t i = 0; i < block.precodeCLHistogram.size(); ++i ) {
            std::cerr << "    " << std::setw( 2 ) << 4 + i << " : " << block.precodeCLHistogram[i] << "\n";
        }

        /**
         * Encountered errors:
         *   Cannot copy last length because this is the first one! : 5403
         *   No error. : 494
         *   Constructing a Huffman coding from the given code length sequence failed! : 7114744
         *   Invalid number of literal/length codes! : 28976
         *   The Huffman coding is not optimal! : 3643598
         * -> 7M downright invalid Huffman codes but *also* ~4M non-optimal Huffman codes.
         *    The latter is kind of a strong criterium that I'm not even sure that all gzip encoders follow!
         */
        std::cerr << "Encountered errors:\n";
        for ( const auto& [error, count] : errorCounts ) {
            std::cerr << "    " << toString( error ) << " : " << count << "\n";
        }
    }
#endif

    return bitOffsets;
}


[[nodiscard]] std::vector<size_t>
findUncompressedDeflateBlocksNestedBranches( const BufferedFileReader::AlignedBuffer& buffer )
{
    std::vector<size_t> bitOffsets;

    for ( size_t i = 2; i + 2 < buffer.size(); ++i ) {
        if ( LIKELY( static_cast<uint8_t>( static_cast<uint8_t>( buffer[i] )
                                           ^ static_cast<uint8_t>( buffer[i+2] ) ) != 0xFFU ) ) [[likely]] {
            continue;
        }

        if ( LIKELY( static_cast<uint8_t>( static_cast<uint8_t>( buffer[i-1] )
                                           ^ static_cast<uint8_t>( buffer[i+1] ) ) != 0xFFU ) ) [[likely]] {
            continue;
        }

        if ( LIKELY( ( static_cast<uint8_t>( buffer[i-2] ) & 0b111U ) != 0 ) ) [[likely]] {
            continue;
        }

        if ( UNLIKELY( ( buffer[i] == 0 ) && ( buffer[i-1] == 0 ) ) ) [[unlikely]] {
            continue;
        }

        /* The size and negated size must be preceded by at least three zero bits, one indicating a non-final block
         * and two indicating a non-compressed block. This test assumes that the padding between the deflate block
         * header and the byte-aligned non-compressed data is zero!
         * @todo It is fine ignoring weird output with non-zero padding in the finder but the decoder should then
         *       know of this and not stop decoding thinking that the other thread has found that block!
         * @todo I might need an interface to determine what blocks could have been found and what not :/ */
        uint8_t trailingZeros = 3;
        for ( uint8_t j = trailingZeros + 1; j <= 8U; ++j ) {
            if ( ( static_cast<uint8_t>( buffer[i-1] ) & ( 1U << static_cast<uint8_t>( j - 1U ) ) ) == 0 ) {
                trailingZeros = j;
            }
        }
        bitOffsets.push_back( i * CHAR_BIT - trailingZeros );
    }

    return bitOffsets;
}


[[nodiscard]] std::vector<size_t>
findUncompressedDeflateBlocks( const BufferedFileReader::AlignedBuffer& buffer )
{
    std::vector<size_t> bitOffsets;

    for ( size_t i = 1; i + 2 < buffer.size(); ++i ) {
        const auto blockSize = loadUnaligned<uint16_t>( buffer.data() + i );
        const auto negatedBlockSize = loadUnaligned<uint16_t>( buffer.data() + i + 2 );
        if ( LIKELY( static_cast<uint16_t>( blockSize ^ negatedBlockSize ) != 0xFFFFU ) ) [[likely]] {
            continue;
        }

        if ( LIKELY( ( static_cast<uint8_t>( buffer[i-1] ) & 0b111U ) != 0 ) ) [[likely]] {
            continue;
        }

        if ( UNLIKELY( blockSize == 0 ) ) {
            continue;
        }

        uint8_t trailingZeros = 3;
        for ( uint8_t j = trailingZeros + 1; j <= 8; ++j ) {
            if ( ( static_cast<uint8_t>( buffer[i-1] ) & ( 1U << static_cast<uint8_t>( j - 1U ) ) ) == 0 ) {
                trailingZeros = j;
            }
        }

        bitOffsets.push_back( i * CHAR_BIT - trailingZeros );
    }

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


[[nodiscard]] BufferedFileReader::AlignedBuffer
bufferFile( const std::string& fileName,
            size_t             bytesToBuffer = std::numeric_limits<size_t>::max() )
{
    const auto file = throwingOpen( fileName, "rb" );
    BufferedFileReader::AlignedBuffer buffer( std::min( fileSize( fileName), bytesToBuffer ), 0 );
    const auto nElementsReadFromFile = std::fread( buffer.data(), sizeof( buffer[0] ), buffer.size(), file.get() );
    buffer.resize( nElementsReadFromFile );
    return buffer;
}


[[nodiscard]] std::string
formatBandwidth( const std::vector<double>& times,
                 size_t                     byteCount )
{
    std::vector<double> bandwidths( times.size() );
    std::transform( times.begin(), times.end(), bandwidths.begin(),
                    [byteCount] ( double time ) { return static_cast<double>( byteCount ) / time / 1e6; } );
    Statistics<double> bandwidthStats{ bandwidths };

    /* Motivation for showing min times and maximum bandwidths are because nothing can go faster than
     * physically possible but many noisy influences can slow things down, i.e., the minimum time is
     * the value closest to be free of noise. */
    std::stringstream result;
    result << "( " + bandwidthStats.formatAverageWithUncertainty( true ) << " ) MB/s";
    return result.str();
}


void
benchmarkGzip( const std::string& fileName )
{
    {
        const auto buffer = bufferFile( fileName, 128ULL * 1024ULL * 1024ULL );
        const auto [blockCandidates, durations] = benchmarkFunction<10>(
            [&buffer] () { return findUncompressedDeflateBlocks( buffer ); } );

        std::cout << "[findUncompressedDeflateBlocks] " << formatBandwidth( durations, buffer.size() ) << "\n";
        std::cout << "    Block candidates (" << blockCandidates.size() << "): "
                  << blockCandidates << "\n\n";
    }

    {
        const auto buffer = bufferFile( fileName, 128ULL * 1024ULL * 1024ULL );
        const auto [blockCandidates, durations] = benchmarkFunction<10>(
            [&buffer] () { return findUncompressedDeflateBlocksNestedBranches( buffer ); } );

        std::cout << "[findUncompressedDeflateBlocksNestedBranches] " << formatBandwidth( durations, buffer.size() ) << "\n";
        std::cout << "    Block candidates (" << blockCandidates.size() << "): "
                  << blockCandidates << "\n\n";
    }

    /* Ground truth offsets. */
    const auto [streamOffsets, blockOffsets] = parseWithZlib( fileName );
    std::cout << "Gzip streams (" << streamOffsets.size() << "): " << streamOffsets << "\n";
    std::cout << "Deflate blocks (" << blockOffsets.size() << "): " << blockOffsets << "\n\n";

    /* Print block size information */
    {
        std::vector<size_t> blockSizes( blockOffsets.size() );
        std::adjacent_difference( blockOffsets.begin(), blockOffsets.end(), blockSizes.begin() );
        assert( !blockSizes.empty() );
        blockSizes.erase( blockSizes.begin() );  /* adjacent_difference begins writing at output begin + 1! */

        const Histogram<size_t> sizeHistogram{ blockSizes, 6, "b" };

        std::cout << "Block size distribution: min: " << sizeHistogram.statistics().min / CHAR_BIT << " B"
                  << ", avg: " << sizeHistogram.statistics().average() / CHAR_BIT << " B"
                  << " +- " << sizeHistogram.statistics().standardDeviation() / CHAR_BIT << " B"
                  << ", max: " << sizeHistogram.statistics().max / CHAR_BIT << " B\n";

        std::cout << "Block Size Distribution (small to large):\n" << sizeHistogram.plot() << "\n\n";
    }

    /* In general, all solutions should return all blocks except for the final block, uncompressed blocks
     * and fixed Huffman encoded blocks. */
    const auto verifyCandidates =
        [&blockOffsets = blockOffsets]
        ( const std::vector<size_t>& blockCandidates,
          const size_t               nBytesToTest )
        {
            for ( size_t i = 0; i + 1 < blockOffsets.size(); ++i ) {
                /* Pigz produces a lot of very small fixed Huffman blocks, probably because of a "flush".
                 * But the block finder don't have to find fixed Huffman blocks */
                const auto size = blockOffsets[i + 1] - blockOffsets[i];
                if ( size < 1000 ) {
                    continue;
                }

                /* Especially for the naive zlib finder up to one deflate block might be missing,
                 * i.e., up to ~64 KiB! */
                const auto offset = blockOffsets[i];
                if ( offset >= nBytesToTest * CHAR_BIT - 128ULL * 1024ULL * CHAR_BIT ) {
                    break;
                }

                if ( !contains( blockCandidates, offset ) ) {
                    std::stringstream message;
                    message << "Block " << i << " at offset " << offset << " was not found!";
                    throw std::logic_error( std::move( message ).str() );
                }
            }

            if ( blockCandidates.size() > 2 * blockOffsets.size() + 10 ) {
                throw std::logic_error( "Too many false positives found!" );
            }
        };

    {
        const auto buffer = bufferFile( fileName, 256ULL * 1024ULL );
        const auto [blockCandidates, durations] = benchmarkFunction<10>(
            [&buffer] () { return findDeflateBlocksZlib( buffer ); } );

        std::cout << "[findDeflateBlocksZlib] " << formatBandwidth( durations, buffer.size() ) << "\n";
        std::cout << "    Block candidates (" << blockCandidates.size() << "): "
                  << blockCandidates << "\n\n";
        verifyCandidates( blockCandidates, buffer.size() );
    }

    /* Because final blocks are skipped, it won't find anything for BGZ files! */
    const auto isBgzfFile = pragzip::blockfinder::Bgzf::isBgzfFile( std::make_unique<StandardFileReader>( fileName ) );
    if ( !isBgzfFile ) {
        const auto buffer = bufferFile( fileName, 256ULL * 1024ULL );
        const auto [blockCandidates, durations] = benchmarkFunction<10>(
            [&buffer] () { return findDeflateBlocksZlibOptimized( buffer ); } );

        std::cout << "[findDeflateBlocksZlibOptimized] " << formatBandwidth( durations, buffer.size() ) << "\n";
        std::cout << "    Block candidates (" << blockCandidates.size() << "): "
                  << blockCandidates << "\n\n";
    }

    /* Benchmarks with own implementation (pragzip). */
    {
        const auto buffer = bufferFile( fileName, 16ULL * 1024ULL * 1024ULL );
        const auto [blockCandidates, durations] = benchmarkFunction<10>(
            [&buffer] () { return findDeflateBlocksPragzip( buffer ); } );

        std::cout << "[findDeflateBlocksPragzip] " << formatBandwidth( durations, buffer.size() ) << "\n";
        std::cout << "    Block candidates (" << blockCandidates.size() << "): "
                  << blockCandidates << "\n\n";

        /* Same as above but with a LUT that can skip bits similar to the Boyer–Moore string-search algorithm. */

        const auto [blockCandidatesLUT, durationsLUT] = benchmarkFunction<10>(
            /* As for choosing CACHED_BIT_COUNT == 13, see the output of the results at the end of the file.
             * 13 is the last for which it significantly improves over less bits and 14 bits produce reproducibly
             * slower bandwidths! 13 bits is the best configuration as far as I know. */
            [&buffer] () { return findDeflateBlocksPragzipLUT<13>( buffer ); } );

        std::cout << "[findDeflateBlocksPragzipLUT] " << formatBandwidth( durationsLUT, buffer.size() ) << "\n";
        std::cout << "    Block candidates (" << blockCandidatesLUT.size() << "): "
                  << blockCandidatesLUT << "\n\n";

        if ( blockCandidates != blockCandidatesLUT ) {
            throw std::logic_error( "Results with and without LUT differ!" );
        }

        /* Same as above but with a LUT and two-pass. */

        const auto [blockCandidatesLUT2P, durationsLUT2P] = benchmarkFunction<10>(
            /* As for choosing CACHED_BIT_COUNT == 13, see the output of the results at the end of the file.
             * 13 is the last for which it significantly improves over less bits and 14 bits produce reproducibly
             * slower bandwidths! 13 bits is the best configuration as far as I know. */
            [&buffer] () { return findDeflateBlocksPragzipLUTTwoPass<13>( buffer ); } );

        std::cout << "[findDeflateBlocksPragzipLUTTwoPass] " << formatBandwidth( durationsLUT2P, buffer.size() ) << "\n";
        std::cout << "    Block candidates (" << blockCandidatesLUT2P.size() << "): "
                  << blockCandidatesLUT2P << "\n\n";

        if ( blockCandidates != blockCandidatesLUT2P ) {
            throw std::logic_error( "Results with and without LUT + 2-pass differ!" );
        }
    }

    if ( isBgzfFile ) {
        const auto [blockCandidates, durations] =
            benchmarkFunction<10>( [fileName] () { return findBgzStreams( fileName ); } );

        std::cout << "[findBgzStreams] " << formatBandwidth( durations, fileSize( fileName ) ) << "\n";
        std::cout << "    Block candidates (" << blockCandidates.size() << "): "
                  << blockCandidates << "\n\n";
    }

    {
        const auto gzipStreams = findGzipStreams( fileName );
        if ( !gzipStreams.empty() ) {
            std::cout << "Found " << gzipStreams.size() << " gzip stream candidates!\n" << gzipStreams << "\n\n";
        }
    }

    std::cout << "\n";
}


template<uint8_t CACHED_BIT_COUNT>
std::vector<size_t>
benchmarkLUTSize( const BufferedFileReader::AlignedBuffer& buffer )
{
    std::optional<std::vector<size_t> > blockCandidatesWithLessBits;
    if constexpr ( CACHED_BIT_COUNT > 1 ) {
        blockCandidatesWithLessBits = benchmarkLUTSize<CACHED_BIT_COUNT - 1>( buffer );
    }

    const auto [blockCandidates, durations] = benchmarkFunction<10>(
        [&buffer] () { return findDeflateBlocksPragzipLUT<CACHED_BIT_COUNT>( buffer ); } );

    std::cout << "[findDeflateBlocksPragzipLUT with " << static_cast<int>( CACHED_BIT_COUNT ) << " bits] "
              << formatBandwidth( durations, buffer.size() ) << "\n";

    if ( blockCandidatesWithLessBits && ( *blockCandidatesWithLessBits != blockCandidates ) ) {
        throw std::logic_error( "Got inconsistent errors for pragzip blockfinder with different LUT table sizes!" );
    }

    return blockCandidates;
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

    const auto tmpFolder = createTemporaryDirectory( "pragzip.benchmarkGzipBlockFinder" );
    const std::string fileName = std::filesystem::absolute( tmpFolder.path() / "random-base64" );

    const std::vector<std::tuple<std::string, std::string, std::string, std::string > > testEncoders = {
        { "gzip", "gzip --version", "gzip -k --force", "gzip" },
        { "pigz", "pigz --version", "pigz -k --force", "pigz" },
        { "igzip", "igzip --version", "igzip -k --force", "igzip" },
        { "bgzip", "bgzip --version", "bgzip --force", "bgzip" },
        { "Python3 gzip", "python3 --version", "python3 -m gzip", "python3-gzip" },
        { "Python3 pgzip", "python3 -m pip show pgzip", "python3 -m pgzip", "python3-pgzip" },
    };

    try
    {
        for ( const auto& [name, getVersion, command, extension] : testEncoders ) {
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

            const auto newFileName = fileName + "." + extension;
            std::filesystem::rename( fileName + ".gz", newFileName );


            /* Benchmark Pragzip LUT version with different LUT sizes. */

            if ( name == "gzip" ) {
                std::cout << "=== Testing different Pragzip + LUT table sizes ===\n\n";
                /* CACHED_BIT_COUNT == 19 fails on GCC because it requires > 99 M constexpr steps.
                 * CACHED_BIT_COUNT == 18 fail on clang because it requires > 99 M constexpr steps. */
                benchmarkLUTSize<16>( bufferFile( newFileName ) );
                std::cout << "\n\n";
            }

            /* Benchmark all different blockfinder implementations with the current encoded file. */

            std::cout << "=== Testing with encoder: " << name << " ===\n\n";

            std::cout << "> " << getVersion << "\n";
            [[maybe_unused]] const auto versionReturnCode = std::system( ( getVersion + " > out" ).c_str() );
            std::cout << std::ifstream( "out" ).rdbuf();
            std::cout << "\n";

            benchmarkGzip( newFileName );
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
cmake --build . -- benchmarkGzipBlockFinder && benchmarks/benchmarkGzipBlockFinder random.gz

[findUncompressedDeflateBlocks] ( 2052 <= 2072 +- 18 <= 2084 ) MB/s
    Block candidates (4356):  80 262304 524560 786800 1049120 1311392 1573784 1836072 2098344 2360744 2623032 2791488 2885360 3147648 3409952 ...

[findUncompressedDeflateBlocksNestedBranches] ( 1969 <= 1974 +- 5 <= 1978 ) MB/s
    Block candidates (4356):  88 262312 524568 786808 1049128 1311400 1573792 1836080 2098352 2360752 2623040 2791501 2885368 3147656 3409960 ...


cmake --build . -- benchmarkGzipBlockFinder && benchmarks/benchmarkGzipBlockFinder


=== Testing different Pragzip + LUT table sizes ===

[findDeflateBlocksPragzipLUT with 1 bits] ( 2.018 <= 2.043 +- 0.022 <= 2.058 ) MB/s
[findDeflateBlocksPragzipLUT with 2 bits] ( 3.72 <= 3.75 +- 0.06 <= 3.83 ) MB/s
[findDeflateBlocksPragzipLUT with 3 bits] ( 7.113 <= 7.122 +- 0.013 <= 7.137 ) MB/s
[findDeflateBlocksPragzipLUT with 4 bits] ( 7.534 <= 7.545 +- 0.015 <= 7.562 ) MB/s
[findDeflateBlocksPragzipLUT with 5 bits] ( 7.45 <= 7.57 +- 0.17 <= 7.77 ) MB/s
[findDeflateBlocksPragzipLUT with 6 bits] ( 7.861 <= 7.875 +- 0.013 <= 7.885 ) MB/s
[findDeflateBlocksPragzipLUT with 7 bits] ( 8.12 <= 8.26 +- 0.13 <= 8.37 ) MB/s
[findDeflateBlocksPragzipLUT with 8 bits] ( 8.502 <= 8.521 +- 0.019 <= 8.54 ) MB/s
[findDeflateBlocksPragzipLUT with 9 bits] ( 8.31 <= 8.36 +- 0.05 <= 8.4 ) MB/s
[findDeflateBlocksPragzipLUT with 10 bits] ( 8.3 <= 8.38 +- 0.13 <= 8.53 ) MB/s
[findDeflateBlocksPragzipLUT with 11 bits] ( 8.49 <= 8.6 +- 0.11 <= 8.71 ) MB/s
[findDeflateBlocksPragzipLUT with 12 bits] ( 8.781 <= 8.805 +- 0.025 <= 8.831 ) MB/s
[findDeflateBlocksPragzipLUT with 13 bits] ( 8.866 <= 8.876 +- 0.009 <= 8.884 ) MB/s
[findDeflateBlocksPragzipLUT with 14 bits] ( 8.927 <= 8.936 +- 0.008 <= 8.942 ) MB/s
[findDeflateBlocksPragzipLUT with 15 bits] ( 8.96 <= 8.99 +- 0.04 <= 9.03 ) MB/s
[findDeflateBlocksPragzipLUT with 16 bits] ( 9.001 <= 9.015 +- 0.013 <= 9.028 ) MB/s


=== Testing with encoder: gzip ===

> gzip --version
gzip 1.10
Copyright (C) 2018 Free Software Foundation, Inc.
Copyright (C) 1993 Jean-loup Gailly.
This is free software.  You may redistribute copies of it under the terms of
the GNU General Public License <https://www.gnu.org/licenses/gpl.html>.
There is NO WARRANTY, to the extent permitted by law.

Written by Jean-loup Gailly.

[findUncompressedDeflateBlocks] ( 1366 <= 1368 +- 5 <= 1374 ) MB/s
    Block candidates (33):  1641849 14802352 16084952 18358560 19055164 23166984 28247616 32673280 32778929 34162050 37006712 37488520 38504569 43578016 43755016 ...

[findUncompressedDeflateBlocksNestedBranches] ( 1329.5 <= 1331.9 +- 2.2 <= 1333.8 ) MB/s
    Block candidates (33):  1641857 14802362 16084963 18358568 19055168 23166992 28247625 32673288 32778936 34162056 37006720 37488528 38504576 43578025 43755024 ...

Gzip streams (2):  0 12748064
Deflate blocks (495):  192 205414 411532 617749 824122 1029728 1236300 1442840 1649318 1855554 2061582 2267643 2473676 2679825 2886058 ...

Block size distribution: min: 0 B, avg: 25783.4 B +- 38.8132 B, max: 25888 B
Block Size Distribution (small to large):
     0 b |
         |
         |
         |
         |
207110 b |==================== (494)


[findDeflateBlocksZlib] ( 0.181 <= 0.184 +- 0.003 <= 0.187 ) MB/s
    Block candidates (20):  192 205414 411532 617749 824122 1028344 1028348 1028349 1029728 1236300 1442840 1572611 1572612 1641846 1641847 ...

[findDeflateBlocksZlibOptimized] ( 0.661 <= 0.667 +- 0.006 <= 0.673 ) MB/s
    Block candidates (11):  192 205414 411532 617749 824122 1029728 1236300 1442840 1649318 1855554 2094939

[findDeflateBlocksPragzip] ( 4.255 <= 4.27 +- 0.015 <= 4.284 ) MB/s
    Block candidates (494):  192 205414 411532 617749 824122 1029728 1236300 1442840 1649318 1855554 2061582 2267643 2473676 2679825 2886058 ...

[findDeflateBlocksPragzipLUT] ( 9.02 <= 9.1 +- 0.07 <= 9.15 ) MB/s
    Block candidates (494):  192 205414 411532 617749 824122 1029728 1236300 1442840 1649318 1855554 2061582 2267643 2473676 2679825 2886058 ...

[findDeflateBlocksPragzipLUTTwoPass] ( 9.12 <= 9.17 +- 0.05 <= 9.21 ) MB/s
    Block candidates (494):  192 205414 411532 617749 824122 1029728 1236300 1442840 1649318 1855554 2061582 2267643 2473676 2679825 2886058 ...


=== Testing with encoder: pigz ===

> pigz --version
pigz 2.6

[findUncompressedDeflateBlocks] ( 1386 <= 1390 +- 4 <= 1394 ) MB/s
    Block candidates (26):  9115355 15453880 17626256 20230960 20959049 32956072 33043921 36063152 36741288 38098897 42646161 44588288 47241200 51683920 53144346 ...

[findUncompressedDeflateBlocksNestedBranches] ( 1357.6 <= 1360.8 +- 2.9 <= 1363.2 ) MB/s
    Block candidates (26):  9115363 15453888 17626265 20230970 20959057 32956081 33043930 36063160 36741296 38098906 42646169 44588300 47241208 51683930 53144353 ...

Gzip streams (2):  0 12761091
Deflate blocks (1195):  192 102374 205527 308631 411790 515077 618182 721566 797442 797452 797462 797472 900531 1003441 1106502 ...

Block size distribution: min: 0 B, avg: 10679.8 B +- 4498.38 B, max: 12979 B
Block Size Distribution (small to large):
     0 b |===                  (171)
         |
         |
         |
         |==                   (127)
103838 b |==================== (896)


[findDeflateBlocksZlib] ( 0.1947 <= 0.1952 +- 0.0005 <= 0.1956 ) MB/s
    Block candidates (31):  192 102374 205527 234702 234703 234706 234707 308631 411790 515077 618182 721566 797472 900531 1003441 ...

[findDeflateBlocksZlibOptimized] ( 0.665 <= 0.669 +- 0.004 <= 0.672 ) MB/s
    Block candidates (22):  192 102374 205527 308631 411790 515077 618182 721566 797472 900531 1003441 1106502 1209841 1313251 1416637 ...

[findDeflateBlocksPragzip] ( 4.173 <= 4.195 +- 0.019 <= 4.206 ) MB/s
    Block candidates (1023):  192 102374 205527 308631 411790 515077 618182 721566 797472 900531 1003441 1106502 1209841 1313251 1416637 ...

[findDeflateBlocksPragzipLUT] ( 8.706 <= 8.738 +- 0.028 <= 8.758 ) MB/s
    Block candidates (1023):  192 102374 205527 308631 411790 515077 618182 721566 797472 900531 1003441 1106502 1209841 1313251 1416637 ...

[findDeflateBlocksPragzipLUTTwoPass] ( 8.66 <= 8.7 +- 0.06 <= 8.77 ) MB/s
    Block candidates (1023):  192 102374 205527 308631 411790 515077 618182 721566 797472 900531 1003441 1106502 1209841 1313251 1416637 ...


=== Testing with encoder: igzip ===

> igzip --version
igzip command line interface 2.30.0

[findUncompressedDeflateBlocks] ( 1369 <= 1372 +- 5 <= 1378 ) MB/s
    Block candidates (25):  9264928 9787856 11847658 25696321 26896376 30346740 32737457 32800465 34399866 36035720 44696189 48931825 49145872 52556250 53613251 ...

[findUncompressedDeflateBlocksNestedBranches] ( 1080 <= 1250 +- 140 <= 1330 ) MB/s
    Block candidates (25):  9264940 9787864 11847666 25696328 26896384 30346744 32737464 32800472 34399872 36035728 44696192 48931835 49145880 52556258 53613256 ...

Gzip streams (2):  0 12669134
Deflate blocks (129):  1136 790905 1580736 2370674 3160686 3950671 4740448 5530378 6321349 7112718 7903168 8692985 9482887 10274151 11065651 ...

Block size distribution: min: 0 B, avg: 98870.4 B +- 77.9492 B, max: 98950 B
Block Size Distribution (small to large):
     0 b |
         |
         |
         |
         |
791606 b |==================== (128)


[findDeflateBlocksZlib] ( 0.1884 <= 0.1891 +- 0.0006 <= 0.1894 ) MB/s
    Block candidates (8):  1136 790905 1139766 1173134 1580736 1702286 1702289 1702290

[findDeflateBlocksZlibOptimized] ( 0.6621 <= 0.6644 +- 0.0022 <= 0.6664 ) MB/s
    Block candidates (3):  1136 790905 1580736

[findDeflateBlocksPragzip] ( 4.228 <= 4.238 +- 0.01 <= 4.247 ) MB/s
    Block candidates (128):  1136 790905 1580736 2370674 3160686 3950671 4740448 5530378 6321349 7112718 7903168 8692985 9482887 10274151 11065651 ...

[findDeflateBlocksPragzipLUT] ( 9.12 <= 9.141 +- 0.023 <= 9.166 ) MB/s
    Block candidates (128):  1136 790905 1580736 2370674 3160686 3950671 4740448 5530378 6321349 7112718 7903168 8692985 9482887 10274151 11065651 ...

[findDeflateBlocksPragzipLUTTwoPass] ( 9.32 <= 9.41 +- 0.11 <= 9.54 ) MB/s
    Block candidates (128):  1136 790905 1580736 2370674 3160686 3950671 4740448 5530378 6321349 7112718 7903168 8692985 9482887 10274151 11065651 ...


=== Testing with encoder: bgzip ===

> bgzip --version
bgzip (htslib) 1.13+ds
Copyright (C) 2021 Genome Research Ltd.

[findUncompressedDeflateBlocks] ( 1351 <= 1368 +- 28 <= 1400 ) MB/s
    Block candidates (32):  2959288 3061001 3075096 6140777 11709712 12621274 12934273 13868528 22428648 27137769 30108368 30328864 34426556 35504328 43584170 ...

[findUncompressedDeflateBlocksNestedBranches] ( 1328 <= 1344 +- 14 <= 1356 ) MB/s
    Block candidates (32):  2959296 3061008 3075104 6140784 11709720 12621280 12934282 13868536 22428661 27137776 30108376 30328875 34426562 35504336 43584176 ...

Got 6 B of FEXTRA field!
Gzip streams (260):  0 49330 98651 147979 197311 246645 295981 345312 394654 443992 493330 542663 591980 641320 690645 ...
Deflate blocks (259):  144 394784 789352 1183976 1578632 1973304 2367992 2762640 3157376 3552080 3946784 4341448 4735984 5130704 5525304 ...

Block size distribution: min: 0 B, avg: 49140.7 B +- 3056.03 B, max: 49347 B
Block Size Distribution (small to large):
     0 b |                     (1)
         |
         |
         |
         |
394776 b |==================== (257)


[findDeflateBlocksZlib] ( 0.1913 <= 0.1927 +- 0.0016 <= 0.1944 ) MB/s
    Block candidates (6):  144 394784 789352 1183976 1578632 1973304

[findDeflateBlocksPragzip] ( 4.36 <= 4.371 +- 0.01 <= 4.379 ) MB/s
    Block candidates (0):

[findDeflateBlocksPragzipLUT] ( 9.42 <= 9.47 +- 0.07 <= 9.55 ) MB/s
    Block candidates (0):

[findDeflateBlocksPragzipLUTTwoPass] ( 9.33 <= 9.49 +- 0.14 <= 9.59 ) MB/s
    Block candidates (0):

[findBgzStreams] ( 20000 <= 31000 +- 10000 <= 37000 ) MB/s
    Block candidates (259):  144 394784 789352 1183976 1578632 1973304 2367992 2762640 3157376 3552080 3946784 4341448 4735984 5130704 5525304 ...

Found 259 gzip stream candidates!
 0 49330 98651 147979 197311 246645 295981 345312 394654 443992 493330 542663 591980 641320 690645 ...


=== Testing with encoder: Python3 gzip ===

> python3 --version
Python 3.10.4

[findUncompressedDeflateBlocks] ( 1389 <= 1401 +- 12 <= 1413 ) MB/s
    Block candidates (27):  194240 942224 2600937 4563939 7237456 9078080 14882897 16489920 23810345 29007536 30927412 33606996 47122649 49244473 49721738 ...

[findUncompressedDeflateBlocksNestedBranches] ( 1352 <= 1361 +- 12 <= 1374 ) MB/s
    Block candidates (27):  194248 942233 2600944 4563944 7237464 9078091 14882904 16489928 23810352 29007546 30927416 33607004 47122656 49244480 49721745 ...

Got 6 B of FEXTRA field!
Gzip streams (2):  0 12759547
Deflate blocks (989):  192 102672 205833 308639 411748 515132 618285 721612 824892 928415 1031456 1134888 1238197 1341253 1444122 ...

Block size distribution: min: 0 B, avg: 12903 B +- 27.2736 B, max: 12999 B
Block Size Distribution (small to large):
     0 b |
         |
         |
         |
         |
103999 b |==================== (988)


[findDeflateBlocksZlib] ( 0.1936 <= 0.1942 +- 0.0005 <= 0.1946 ) MB/s
    Block candidates (29):  192 102672 194239 194240 194241 194242 194245 205833 308639 411748 515132 618285 721612 824892 928415 ...

[findDeflateBlocksZlibOptimized] ( 0.6633 <= 0.6659 +- 0.0023 <= 0.6673 ) MB/s
    Block candidates (20):  192 102672 205833 308639 411748 515132 618285 721612 824892 928415 1031456 1134888 1238197 1341253 1444122 ...

[findDeflateBlocksPragzip] ( 4.173 <= 4.18 +- 0.007 <= 4.187 ) MB/s
    Block candidates (988):  192 102672 205833 308639 411748 515132 618285 721612 824892 928415 1031456 1134888 1238197 1341253 1444122 ...

[findDeflateBlocksPragzipLUT] ( 8.746 <= 8.763 +- 0.018 <= 8.783 ) MB/s
    Block candidates (988):  192 102672 205833 308639 411748 515132 618285 721612 824892 928415 1031456 1134888 1238197 1341253 1444122 ...

[findDeflateBlocksPragzipLUTTwoPass] ( 8.53 <= 8.76 +- 0.2 <= 8.88 ) MB/s
    Block candidates (988):  192 102672 205833 308639 411748 515132 618285 721612 824892 928415 1031456 1134888 1238197 1341253 1444122 ...


=== Testing with encoder: Python3 pgzip ===

> python3 -m pip show pgzip
Name: pgzip
Version: 0.3.1
Summary: A multi-threading implementation of Python gzip module
Home-page: https://github.com/pgzip/pgzip
Author: pgzip team
Author-email: pgzip@thegoldfish.org
License: MIT
Location: /home/hypatia/.local/lib/python3.10/site-packages
Requires:
Required-by:

[findUncompressedDeflateBlocks] ( 1180 <= 1290 +- 90 <= 1350 ) MB/s
    Block candidates (23):  2311842 13547514 17302848 21375988 29568384 38396442 40876281 45811441 46230384 50735041 50861242 51689824 52966512 56154593 62322161 ...

[findUncompressedDeflateBlocksNestedBranches] ( 1317 <= 1322 +- 5 <= 1327 ) MB/s
    Block candidates (23):  2311849 13547520 17302857 21375992 29568395 38396449 40876289 45811448 46230393 50735049 50861248 51689833 52966520 56154600 62322168 ...

Got 8 B of FEXTRA field!
Gzip streams (2):  0 12747800
Deflate blocks (495):  272 205800 411533 617885 824269 1030628 1237131 1442923 1649106 1855109 2061199 2267938 2473926 2680186 2886437 ...

Block size distribution: min: 0 B, avg: 25782.9 B +- 37.5362 B, max: 25890 B
Block Size Distribution (small to large):
     0 b |
         |
         |
         |
         |
207124 b |==================== (494)


[findDeflateBlocksZlib] ( 0.1825 <= 0.1831 +- 0.0007 <= 0.1838 ) MB/s
    Block candidates (12):  272 205800 411533 617885 824269 1030628 1164656 1237131 1442923 1649106 1771228 1855109

[findDeflateBlocksZlibOptimized] ( 0.633 <= 0.637 +- 0.004 <= 0.641 ) MB/s
    Block candidates (10):  272 205800 411533 617885 824269 1030628 1237131 1442923 1649106 1855109

[findDeflateBlocksPragzip] ( 4.098 <= 4.111 +- 0.011 <= 4.119 ) MB/s
    Block candidates (494):  272 205800 411533 617885 824269 1030628 1237131 1442923 1649106 1855109 2061199 2267938 2473926 2680186 2886437 ...

[findDeflateBlocksPragzipLUT] ( 8.725 <= 8.73 +- 0.005 <= 8.736 ) MB/s
    Block candidates (494):  272 205800 411533 617885 824269 1030628 1237131 1442923 1649106 1855109 2061199 2267938 2473926 2680186 2886437 ...

[findDeflateBlocksPragzipLUTTwoPass] ( 8.77 <= 8.85 +- 0.07 <= 8.9 ) MB/s
    Block candidates (494):  272 205800 411533 617885 824269 1030628 1237131 1442923 1649106 1855109 2061199 2267938 2473926 2680186 2886437 ...
*/
