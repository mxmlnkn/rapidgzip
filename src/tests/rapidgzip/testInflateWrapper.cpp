#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <common.hpp>
#include <crc32.hpp>
#include <deflate.hpp>
#include <filereader/BufferView.hpp>
#include <filereader/Shared.hpp>
#include <filereader/Standard.hpp>
#include <GzipReader.hpp>
#include <TestHelpers.hpp>
#include <zlib.hpp>
#ifdef WITH_ISAL
    #include <isal.hpp>
#endif


using namespace std::literals;

using namespace rapidgzip;


[[nodiscard]] std::vector<std::byte>
createRandomData( uint64_t                      size,
                  const std::vector<std::byte>& allowedSymbols )
{
    std::mt19937_64 randomEngine;
    std::vector<std::byte> result( size );
    for ( auto& x : result ) {
        x = allowedSymbols[static_cast<size_t>( randomEngine() ) % allowedSymbols.size()];
    }
    return result;
}


template<typename InflateWrapper>
void
testGettingFooter()
{
    /* As there are 4 symbols, 2 bits per symbol should suffice and as the data is random, almost no backreferences
     * should be viable. This leads to a compression ratio of ~4, which is large enough for splitting and benign
     * enough to have multiple chunks with fairly little uncompressed data. */
    constexpr auto DNA_SYMBOLS = "ACGT"sv;
    std::vector<std::byte> allowedSymbols( DNA_SYMBOLS.size() );
    std::transform( DNA_SYMBOLS.begin(), DNA_SYMBOLS.end(), allowedSymbols.begin(),
                    [] ( const auto c ) { return static_cast<std::byte>( c ); } );

    const auto randomDNA = createRandomData( 16_Ki, allowedSymbols );
    const auto compressedRandomDNA = compressWithZlib( randomDNA, CompressionStrategy::HUFFMAN_ONLY );

    auto fileReader = std::make_unique<SharedFileReader>(
        std::make_unique<BufferViewFileReader>( compressedRandomDNA ) );
    gzip::BitReader bitReader( std::move( fileReader ) );
    bitReader.seek( 10 * CHAR_BIT );  // Deflate wrapper expects to start at deflate block
    InflateWrapper inflateWrapper( std::move( bitReader ) );

    std::vector<std::byte> decompressedResult( randomDNA.size() );
    auto [decompressedSize, footer] = inflateWrapper.readStream(
        reinterpret_cast<uint8_t*>( decompressedResult.data() ), decompressedResult.size() );
    REQUIRE_EQUAL( decompressedSize, randomDNA.size() );

    uint8_t dummy{ 0 };

    REQUIRE( footer.has_value() );
    REQUIRE_EQUAL( inflateWrapper.tellCompressed(), compressedRandomDNA.size() * BYTE_SIZE );
    std::tie( decompressedSize, footer ) = inflateWrapper.readStream( &dummy, 1 );
    REQUIRE( !footer.has_value() );

    REQUIRE_EQUAL( decompressedSize, 0U );
    REQUIRE_EQUAL( inflateWrapper.tellCompressed(), compressedRandomDNA.size() * BYTE_SIZE );
    if ( footer ) {
        REQUIRE_EQUAL( footer->gzipFooter.uncompressedSize, randomDNA.size() );
    }
}


void
testGzipHeaderSkip()
{
    const std::vector<std::byte> dataToCompress = { std::byte( 'A' ) };
    const auto compressedData = compressWithZlib( dataToCompress );
    std::vector<std::byte> decompressedResult( dataToCompress.size() );

    /* 2^15 = 32 KiB window buffer and minus signaling raw deflate stream to decode.
     * > The current implementation of inflateInit2() does not process any header information --
     * > that is deferred until inflate() is called.
     * Because of this, we don't have to ensure that enough data is available and/or calling it a
     * second time to read the rest of the header. */
    const auto windowFlags = /* decode gzip */ 16 + /* 2^15 buffer */ 15;

    z_stream stream{};
    {
        stream.zalloc = Z_NULL;     /* used to allocate the internal state */
        stream.zfree = Z_NULL;      /* used to free the internal state */
        stream.opaque = Z_NULL;     /* private data object passed to zalloc and zfree */

        stream.avail_in = 0;        /* number of bytes available at next_in */
        stream.next_in = Z_NULL;    /* next input byte */

        stream.avail_out = 0;       /* remaining free space at next_out */
        stream.next_out = Z_NULL;   /* next output byte will go here */
        stream.total_out = 0;       /* total amount of bytes read */

        stream.msg = nullptr;
    }

    stream.next_out = reinterpret_cast<Bytef*>( decompressedResult.data() );
    stream.avail_out = decompressedResult.size();

    /* Check that skipping over gzip header to first block works */
    {
        auto stream2 = stream;
        stream2.next_in = const_cast<Bytef*>( reinterpret_cast<const Bytef*>( compressedData.data() ) );
        stream2.avail_in = compressedData.size();

        if ( inflateInit2( &stream2, windowFlags ) != Z_OK ) {
            throw std::runtime_error( "Probably encountered invalid gzip header!" );
        }

        REQUIRE_EQUAL( stream2.avail_in, compressedData.size() );

        const auto errorCode = inflate( &stream2, Z_BLOCK );
        REQUIRE_EQUAL( errorCode, Z_OK );
        REQUIRE_EQUAL( stream2.avail_in, compressedData.size() - 10 );

        inflateEnd( &stream2 );
    }

    /* Check that skipping over gzip header to first block works with insufficient input data. */
    {
        auto stream2 = stream;
        stream2.next_in = const_cast<Bytef*>( reinterpret_cast<const Bytef*>( compressedData.data() ) );
        stream2.avail_in = 5;

        if ( inflateInit2( &stream2, windowFlags ) != Z_OK ) {
            throw std::runtime_error( "Probably encountered invalid gzip header!" );
        }

        REQUIRE_EQUAL( stream2.avail_in, 5U );

        const auto errorCode = inflate( &stream2, Z_BLOCK );
        /* This shows that insufficient input cannot be discerned from gzip header read like this. */
        REQUIRE_EQUAL( errorCode, Z_OK );

        /* In order to do the CRC32 computation ourselves, we have to skip over the gzip header
         * and call inflateInit2 with negative window flags again.
         * There is no way to assuredly read only over the gzip header and nothing more with "inflate" because:
         *  - inflate with z_BLOCk argument stops after reading the gzip header but it can also
         *    stop because there is insufficient input data and the result code is exactly the same.
         *    If avail_in > 0 after this read, then we can be sure that it skipped the gzip header
         *    but if avail_in = 0, then we cannot discern the case of insufficient input data from
         *    the case that the input data accidentally exactly equaled the gzip header size!
         *  - To remedy the above, one could try to ensure that we always call inflate with sufficient
         *    avail_in for any gzip header size but the gzip header can be arbitrarily large because
         *    strings like the file name are only zero-terminated as opposed to being specified by
         *    e.g. an 8-bit string length.
         * The solutions is to use inflateGetHeader in between inflateInit2 and inflate and
         */
        inflateEnd( &stream2 );
    }

    /* Same as above but use inflateGetHeader. */
    {
        auto stream2 = stream;
        stream2.next_in = const_cast<Bytef*>( reinterpret_cast<const Bytef*>( compressedData.data() ) );
        stream2.avail_in = 5;

        if ( inflateInit2( &stream2, windowFlags ) != Z_OK ) {
            throw std::runtime_error( "Probably encountered invalid gzip header!" );
        }
        gz_header gzipHeader{};
        /* -1 if it is a zlib stream, which has no gzip header
         * 1 if done and header CRC has been verified */
        gzipHeader.done = 2;
        const auto getHeaderSetupError = inflateGetHeader( &stream2, &gzipHeader );
        REQUIRE_EQUAL( getHeaderSetupError, Z_OK );
        REQUIRE_EQUAL( gzipHeader.done, 0 );

        REQUIRE_EQUAL( stream2.avail_in, 5U );

        const auto errorCode1 = inflate( &stream2, Z_BLOCK );
        REQUIRE_EQUAL( errorCode1, Z_OK );
        REQUIRE_EQUAL( stream2.avail_in, 0U );
        REQUIRE_EQUAL( gzipHeader.done, 0 );

        /* This tests reading the gzip header when the input contains exactly as much data as needed. */
        stream2.next_in = const_cast<Bytef*>( reinterpret_cast<const Bytef*>( compressedData.data() + 5 ) );
        stream2.avail_in = 5;
        const auto errorCode2 = inflate( &stream2, Z_BLOCK );
        REQUIRE_EQUAL( errorCode2, Z_OK );
        REQUIRE_EQUAL( stream2.avail_in, 0U );
        REQUIRE_EQUAL( gzipHeader.done, 1 );

        inflateEnd( &stream2 );
    }
}


template<typename InflateWrapper>
void
testMultiGzipStream()
{
    const std::vector<std::byte> dataToCompress = { std::byte( 'A' ) };
    auto compressedData = compressWithZlib( dataToCompress );

    /* Duplicate gzip stream. */
    const auto singleGzipStreamSize = compressedData.size();
    compressedData.resize( 2 * singleGzipStreamSize );
    std::copy( compressedData.begin(), compressedData.begin() + singleGzipStreamSize,
               compressedData.begin() + singleGzipStreamSize );
    const std::vector<std::byte> expectedResult = { std::byte( 'A' ), std::byte( 'A' ) };

    auto fileReader = std::make_unique<SharedFileReader>(
        std::make_unique<BufferViewFileReader>( compressedData ) );
    gzip::BitReader bitReader( std::move( fileReader ) );
    bitReader.seek( 10 * CHAR_BIT );  // Deflate wrapper expects to start at deflate block
    InflateWrapper inflateWrapper( std::move( bitReader ) );

    std::vector<std::byte> decompressedResult( expectedResult.size(), std::byte( 1 ) );

    /* Each read call only reads up to the first deflate stream end. */
    auto [decompressedSize, footer] = inflateWrapper.readStream(
        reinterpret_cast<uint8_t*>( decompressedResult.data() ), decompressedResult.size() );
    REQUIRE_EQUAL( decompressedSize, dataToCompress.size() );
    /* InflateWrapper reads the next gzip header right after encountering any footer! */
    constexpr auto GZIP_HEADER_SIZE = 10U;
    REQUIRE_EQUAL( inflateWrapper.tellCompressed(), ( compressedData.size() / 2 + GZIP_HEADER_SIZE ) * BYTE_SIZE );

    std::tie( decompressedSize, footer ) = inflateWrapper.readStream(
        reinterpret_cast<uint8_t*>( decompressedResult.data() + 1U ), dataToCompress.size() );
    REQUIRE_EQUAL( decompressedSize, dataToCompress.size() );

    REQUIRE( footer.has_value() );
    REQUIRE_EQUAL( inflateWrapper.tellCompressed(), compressedData.size() * BYTE_SIZE );
    REQUIRE( decompressedResult == expectedResult );
}


template<typename InflateWrapper>
void
testSmallReads( const std::filesystem::path& compressedFilePath,
                const std::filesystem::path& originalFilePath )
{
    /* Set up inflat wrapper on compressed file */
    gzip::BitReader bitReader(
        std::make_unique<SharedFileReader>(
            std::make_unique<StandardFileReader>( compressedFilePath ) ) );
    rapidgzip::gzip::readHeader( bitReader );
    InflateWrapper inflateWrapper( std::move( bitReader ) );

    /* Read original file data */
    const auto originalFileReader = std::make_unique<StandardFileReader>( originalFilePath );
    std::vector<uint8_t> originalData( originalFileReader->size().value() );
    const auto nBytesRead = originalFileReader->read( reinterpret_cast<char*>( originalData.data() ),
                                                      originalData.size() );
    REQUIRE_EQUAL( nBytesRead, originalData.size() );

    /* Decompress in steps of 1 B */
    std::vector<uint8_t> decompressedResult( originalData.size(), 3 );

    size_t decompressedSize{ 0 };
    std::optional<Footer> footer;
    for ( size_t i = 0; i < decompressedResult.size(); ++i ) {
        std::tie( decompressedSize, footer ) = inflateWrapper.readStream( decompressedResult.data() + i, 1 );
        /* While loop in case there are lots of empty gzip streams for some reason.
         * Pigz may insert empty streams when doing a full flush and BGZF has such an empty stream at
         * the file and as a kind of gzip-compatible magic bytes. */
        while ( footer && ( decompressedSize == 0 ) ) {
            const auto oldPosition = inflateWrapper.tellCompressed();
            std::tie( decompressedSize, footer ) = inflateWrapper.readStream(
                reinterpret_cast<uint8_t*>( decompressedResult.data() + i ), 1 );
            REQUIRE( oldPosition != inflateWrapper.tellCompressed() );
            if ( oldPosition == inflateWrapper.tellCompressed() ) {
                break;
            }
        }
        REQUIRE_EQUAL( decompressedSize, 1U );
    }

    REQUIRE( decompressedResult == originalData );
}


[[nodiscard]] std::vector<std::pair<size_t, size_t> >
getBlockOffsetsWithGzipReader( const std::filesystem::path& filePath )
{
    std::vector<std::pair<size_t, size_t> > blockOffsets;

    rapidgzip::GzipReader gzipReader{ std::make_unique<StandardFileReader>( filePath ) };
    while ( !gzipReader.eof() ) {
        gzipReader.read( -1, nullptr, std::numeric_limits<size_t>::max(),
                         static_cast<StoppingPoint>( StoppingPoint::END_OF_STREAM_HEADER
                                                     | StoppingPoint::END_OF_BLOCK ) );
        if ( gzipReader.currentDeflateBlock() && !gzipReader.currentDeflateBlock()->eos() ) {
            blockOffsets.emplace_back( gzipReader.tellCompressed(), gzipReader.tell() );
        }
    }
    blockOffsets.emplace_back( gzipReader.tellCompressed(), gzipReader.tell() );

    return blockOffsets;
}


[[nodiscard]] std::vector<std::pair<size_t, size_t> >
getBlockOffsets( const std::filesystem::path& filePath )
{
    using namespace rapidgzip;
    using Block = rapidgzip::deflate::Block</* Statistics */ true>;

    gzip::BitReader bitReader{ std::make_unique<StandardFileReader>( filePath ) };

    std::optional<gzip::Header> gzipHeader;
    Block block;

    size_t totalBytesRead = 0;
    size_t streamBytesRead = 0;

    CRC32Calculator crc32Calculator;

    std::vector<std::pair<size_t, size_t> > blockOffsets;

    while ( true ) {
        if ( !gzipHeader ) {
            const auto [header, error] = gzip::readHeader( bitReader );
            if ( error != Error::NONE ) {
                std::stringstream message;
                message << "Encountered error: " << toString( error ) << " while trying to read gzip header!";
                throw std::runtime_error( std::move( message ).str() );
            }

            streamBytesRead = 0;
            crc32Calculator.reset();
            gzipHeader = header;
            block.setInitialWindow();
        }

        const auto blockOffset = bitReader.tell();
        if ( const auto error = block.readHeader( bitReader ); error != Error::NONE ) {
            std::stringstream message;
            message << "Encountered error: " << toString( error ) << " while trying to read deflate header!";
            throw std::runtime_error( std::move( message ).str() );
        }

        const auto uncompressedBlockOffset = totalBytesRead;

        block.symbolTypes.literal = 0;
        block.symbolTypes.backreference = 0;

        while ( !block.eob() ) {
            const auto [buffers, error] = block.read( bitReader, std::numeric_limits<size_t>::max() );
            const auto nBytesRead = buffers.size();
            if ( error != Error::NONE ) {
                std::cerr << "Encountered error: " << toString( error ) << " while decompressing deflate block.\n";
            }
            totalBytesRead += nBytesRead;
            streamBytesRead += nBytesRead;

            for ( const auto& buffer : buffers.data ) {
                crc32Calculator.update( reinterpret_cast<const char*>( buffer.data() ), buffer.size() );
            }
        }

        /* Actual part we want. */
        blockOffsets.emplace_back( blockOffset, uncompressedBlockOffset );

        if ( block.isLastBlock() ) {
            const auto footer = gzip::readFooter( bitReader );

            if ( static_cast<uint32_t>( streamBytesRead ) != footer.uncompressedSize ) {
                std::stringstream message;
                message << "Mismatching size (" << static_cast<uint32_t>( streamBytesRead )
                        << " <-> footer: " << footer.uncompressedSize << ") for gzip stream!";
                throw std::runtime_error( std::move( message ).str() );
            }

            crc32Calculator.verify( footer.crc32 );
            gzipHeader = {};
        }

        if ( bitReader.eof() ) {
            blockOffsets.emplace_back( bitReader.tell(), totalBytesRead );
            break;
        }
    }

    return blockOffsets;
}


template<typename InflateWrapper>
void
testSmallReadsUntilOffset( const std::filesystem::path& compressedFilePath,
                           const std::filesystem::path& originalFilePath )
{
    /* Collect all deflate block offsets. */
    const auto blockOffsets = getBlockOffsets( compressedFilePath );

    gzip::BitReader compressedBitReader(
        std::make_unique<SharedFileReader>(
            std::make_unique<StandardFileReader>( compressedFilePath ) ) );

    /* Read original file data */
    const auto originalFileReader = std::make_unique<StandardFileReader>( originalFilePath );
    std::vector<uint8_t> originalData( originalFileReader->size().value() );
    const auto nBytesRead = originalFileReader->read( reinterpret_cast<char*>( originalData.data() ),
                                                      originalData.size() );
    REQUIRE_EQUAL( nBytesRead, originalData.size() );

    for ( size_t i = 0; i + 1 < blockOffsets.size(); ++i ) {
        /* Set up inflate wrapper on compressed file */
        auto bitReader = compressedBitReader;
        bitReader.seek( blockOffsets[i].first );
        InflateWrapper inflateWrapper( std::move( bitReader ), blockOffsets[i + 1].first );

        /* Initialize the window */
        const auto windowStart = blockOffsets[i].second >= deflate::MAX_WINDOW_SIZE
                                 ? deflate::MAX_WINDOW_SIZE - blockOffsets[i].second
                                 : 0;
        inflateWrapper.setWindow( VectorView<uint8_t>( originalData.data() + windowStart,
                                                       originalData.data() + blockOffsets[i].second ) );

        const std::vector<uint8_t> expectedResult( originalData.data() + blockOffsets[i].second,
                                                   originalData.data() + blockOffsets[i + 1].second );

        /* Decompress in steps of 1 B */
        std::vector<uint8_t> decompressedResult( expectedResult.size(), 3U );

        size_t decompressedSize{ 0 };
        std::optional<Footer> footer;
        for ( size_t j = 0; j < decompressedResult.size(); ++j ) {
            std::tie( decompressedSize, footer ) = inflateWrapper.readStream( decompressedResult.data() + j, 1U );
            /* While loop in case there are lots of empty gzip streams for some reason.
             * Pigz may insert empty streams when doing a full flush and BGZF has such an empty stream at
             * the file and as a kind of gzip-compatible magic bytes. */
            while ( footer && ( decompressedSize == 0 ) ) {
                const auto oldPosition = inflateWrapper.tellCompressed();
                std::tie( decompressedSize, footer ) = inflateWrapper.readStream(
                    reinterpret_cast<uint8_t*>( decompressedResult.data() + j ), 1U );
                REQUIRE( oldPosition != inflateWrapper.tellCompressed() );
                if ( oldPosition == inflateWrapper.tellCompressed() ) {
                    break;
                }
            }
            REQUIRE_EQUAL( decompressedSize, 1U );
            if ( decompressedSize != 1U ) {
                std::cerr << "  Tried reading the compressed range: [" << blockOffsets[i].first << ", "
                          << blockOffsets[i + 1].first << "), decompressed range: [" << blockOffsets[i].second << ", "
                          << blockOffsets[i + 1].second << "]. Already read " << j << " B.\n";
            }
        }

        REQUIRE( decompressedResult == expectedResult );
    }
}


void
compareBlockOffsets( const std::vector<std::pair<size_t, size_t> >& blockOffsets1,
                     const std::vector<std::pair<size_t, size_t> >& blockOffsets2 )
{
    REQUIRE_EQUAL( blockOffsets1.size(), blockOffsets2.size() );
    REQUIRE( blockOffsets1.size() > 0 );  // this is true even for an empty stream
    REQUIRE( blockOffsets1 == blockOffsets2 );
    if ( blockOffsets1 != blockOffsets2 ) {
        std::cerr << "Block offsets:\n";
        for ( size_t i = 0; i < std::max( blockOffsets1.size(), blockOffsets2.size() ); ++i ) {
            if ( i < blockOffsets1.size() ) {
                std::cerr << "    first  : " << blockOffsets1[i].first << " b -> " << blockOffsets1[i].second << " B\n";
            }
            if ( i < blockOffsets2.size() ) {
                std::cerr << "    second : " << blockOffsets2[i].first << " b -> " << blockOffsets2[i].second << " B\n";
            }
        }
    }
}


void
testGetBlockOffsets( const std::filesystem::path& compressedFilePath )
{
    const auto blockOffsets = getBlockOffsets( compressedFilePath );
    const auto blockOffsetsGzipReader = getBlockOffsetsWithGzipReader( compressedFilePath );
    compareBlockOffsets( blockOffsets, blockOffsetsGzipReader );
}


template<typename InflateWrapper>
void
testSmallBuffers()
{
    /* As there are 4 symbols, 2 bits per symbol should suffice and as the data is random, almost no backreferences
     * should be viable. This leads to a compression ratio of ~4, which is large enough for splitting and benign
     * enough to have multiple chunks with fairly little uncompressed data. */
    constexpr auto DNA_SYMBOLS = "ACGT"sv;
    std::vector<std::byte> allowedSymbols( DNA_SYMBOLS.size() );
    std::transform( DNA_SYMBOLS.begin(), DNA_SYMBOLS.end(), allowedSymbols.begin(),
                    [] ( const auto c ) { return static_cast<std::byte>( c ); } );

    const auto randomDNA = createRandomData( 16_Ki, allowedSymbols );
    const auto compressedRandomDNA = compressWithZlib( randomDNA, CompressionStrategy::HUFFMAN_ONLY );

    /* Decode 1 B per call. */
    {
        auto fileReader = std::make_unique<SharedFileReader>(
            std::make_unique<BufferViewFileReader>( compressedRandomDNA ) );
        gzip::BitReader bitReader( std::move( fileReader ) );
        bitReader.seek( 10 * CHAR_BIT );  // Deflate wrapper expects to start at deflate block
        InflateWrapper inflateWrapper( std::move( bitReader ) );

        std::vector<std::byte> decompressedResult( randomDNA.size() );
        for ( size_t i = 0; i < decompressedResult.size(); ++i ) {
            auto [decompressedSize, footer] = inflateWrapper.readStream(
                reinterpret_cast<uint8_t*>( decompressedResult.data() + i ), 1U );
            REQUIRE_EQUAL( decompressedSize, 1U );
        }

        REQUIRE( decompressedResult == randomDNA );
    }
}


void
testStoppingPoints()
{
#ifdef WITH_ISAL
    /* As there are 4 symbols, 2 bits per symbol should suffice and as the data is random, almost no backreferences
     * should be viable. This leads to a compression ratio of ~4, which is large enough for splitting and benign
     * enough to have multiple chunks with fairly little uncompressed data. */
    constexpr auto DNA_SYMBOLS = "ACGT"sv;
    std::vector<std::byte> allowedSymbols( DNA_SYMBOLS.size() );
    std::transform( DNA_SYMBOLS.begin(), DNA_SYMBOLS.end(), allowedSymbols.begin(),
                    [] ( const auto c ) { return static_cast<std::byte>( c ); } );

    const auto randomDNA = createRandomData( 128_Ki, allowedSymbols );
    const auto compressedRandomDNA = compressWithZlib( randomDNA, CompressionStrategy::HUFFMAN_ONLY );

    const auto stoppingPoints = static_cast<StoppingPoint>( StoppingPoint::END_OF_BLOCK |
                                                            StoppingPoint::END_OF_BLOCK_HEADER );
    std::unordered_map<StoppingPoint, std::vector<size_t> > offsetsWithGzipReader;
    std::unordered_map<StoppingPoint, std::vector<size_t> > offsetsWithIsalWrapper;

    /* Get offsets with GzipReader */
    {
        std::vector<char> decompressedResult( randomDNA.size() );
        rapidgzip::GzipReader gzipReader( std::make_unique<BufferViewFileReader>( compressedRandomDNA ) );
        size_t lastCompressedOffset{ 0 };
        while ( true ) {
            const auto nBytesRead = gzipReader.read( -1, decompressedResult.data(), decompressedResult.size(),
                                                     stoppingPoints );

            const auto offset = gzipReader.tellCompressed();
            if ( ( nBytesRead == 0 ) && ( offset <= lastCompressedOffset ) ) {
                break;
            }
            lastCompressedOffset = offset;

            if ( gzipReader.currentPoint() && testFlags( *gzipReader.currentPoint(), stoppingPoints ) ) {
                offsetsWithGzipReader[*gzipReader.currentPoint()].emplace_back( offset );
            }

            if ( gzipReader.currentPoint() ) {
                std::cerr << toString( *gzipReader.currentPoint() ) << " @ " << offset << "\n";
            } else {
                std::cerr << "? @ " << offset << "\n";
            }
        }
        std::cerr << "\n";
    }

    /* Decode 10 B per call. */
    {
        auto fileReader = std::make_unique<SharedFileReader>(
            std::make_unique<BufferViewFileReader>( compressedRandomDNA ) );
        gzip::BitReader bitReader( std::move( fileReader ) );
        bitReader.seek( 10 * CHAR_BIT );  // Deflate wrapper expects to start at deflate block
        IsalInflateWrapper inflateWrapper( std::move( bitReader ) );

        inflateWrapper.setStoppingPoints( stoppingPoints );

        std::vector<std::byte> decompressedResult( randomDNA.size() );
        for ( size_t i = 0; i < decompressedResult.size(); ) {
            const auto nBytesToDecompress = std::min<size_t>( 1000U, decompressedResult.size() - i );
            auto [decompressedSize, footer] = inflateWrapper.readStream(
                reinterpret_cast<uint8_t*>( decompressedResult.data() + i ),
                nBytesToDecompress );

            if ( inflateWrapper.stoppedAt() == StoppingPoint::NONE ) {
                REQUIRE_EQUAL( decompressedSize, nBytesToDecompress );
            } else {
                REQUIRE( decompressedSize <= nBytesToDecompress );
            }

            if ( inflateWrapper.stoppedAt() != StoppingPoint::NONE ) {
                offsetsWithIsalWrapper[inflateWrapper.stoppedAt()].emplace_back( inflateWrapper.tellCompressed() );
            }

            if ( inflateWrapper.stoppedAt() != StoppingPoint::NONE ) {
                std::cerr << toString( inflateWrapper.stoppedAt() ) << " @ " << inflateWrapper.tellCompressed();
                if ( const auto compressionType = inflateWrapper.compressionType(); compressionType ) {
                    std::cerr << " type: " << toString( *compressionType ) << "\n";
                } else {
                    std::cerr << "\n";
                }
            }

            i += decompressedSize;
        }

        if ( decompressedResult != randomDNA ) {
            std::cerr << std::string_view( reinterpret_cast<const char*>( decompressedResult.data() ),
                                           decompressedResult.size() );
            std::cerr << "\n\nshould be:\n\n";
            std::cerr << std::string_view( reinterpret_cast<const char*>( randomDNA.data() ), randomDNA.size() );
            std::cerr << "\n";

            for ( size_t i = 0; i < randomDNA.size(); ++i ) {
                std::cerr << static_cast<char>( decompressedResult[i] );
                if ( decompressedResult[i] != randomDNA[i] ) {
                    std::cerr << "[" << static_cast<char>( randomDNA[i] ) << "]";
                }
            }
            std::cerr << "\n";
        }

        REQUIRE( decompressedResult == randomDNA );
    }

    REQUIRE_EQUAL( offsetsWithGzipReader.size(), offsetsWithIsalWrapper.size() );
    REQUIRE( offsetsWithGzipReader == offsetsWithIsalWrapper );
#endif
}


template<typename InflateWrapper>
void
testInflateWrapper( const std::filesystem::path& rootFolder )
{
    static constexpr auto GZIP_FILE_NAMES = {
        "empty",
        "1B",
        "256B-extended-ASCII-table-in-utf8-dynamic-Huffman",
        "256B-extended-ASCII-table-uncompressed",
        "32A-fixed-Huffman",
        "base64-32KiB",
        "base64-256KiB",
        "dolorem-ipsum.txt",
        "numbers-10,65-90",
        "random-128KiB",
        "zeros",
    };

    for ( const auto& extension : { ".gz"s, ".bgz"s, ".igz"s, ".pigz"s } ) {
        for ( const auto* const fileName : GZIP_FILE_NAMES ) {
            std::cerr << "Testing with " << fileName + extension << "\n";
            const auto compressedFilePath = rootFolder / ( fileName + extension );
            testSmallReads<InflateWrapper>( compressedFilePath, rootFolder / fileName );
            testSmallReadsUntilOffset<InflateWrapper>( compressedFilePath, rootFolder / fileName );
            testGetBlockOffsets( compressedFilePath );
        }
    }

    testMultiGzipStream<InflateWrapper>();
    testGettingFooter<InflateWrapper>();
}


int
main( int    argc,
      char** argv )
{
    if ( argc == 0 ) {
        std::cerr << "Expected at least the launch command as the first argument!\n";
        return 1;
    }

    const std::string binaryFilePath( argv[0] );
    std::string binaryFolder = ".";
    if ( const auto lastSlash = binaryFilePath.find_last_of( '/' ); lastSlash != std::string::npos ) {
        binaryFolder = std::string( binaryFilePath.begin(),
                                    binaryFilePath.begin() + static_cast<std::string::difference_type>( lastSlash ) );
    }
    const auto rootFolder =
        static_cast<std::filesystem::path>(
            findParentFolderContaining( binaryFolder, "src/tests/data/base64-256KiB.bgz" )
        ) / "src" / "tests" / "data";

    testStoppingPoints();

#ifdef WITH_ISAL
    testSmallBuffers<IsalInflateWrapper>();
#endif
    testSmallBuffers<ZlibInflateWrapper>();

    testGzipHeaderSkip();

#ifdef WITH_ISAL
    testInflateWrapper<IsalInflateWrapper>( rootFolder );
#endif
    testInflateWrapper<ZlibInflateWrapper>( rootFolder );

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
