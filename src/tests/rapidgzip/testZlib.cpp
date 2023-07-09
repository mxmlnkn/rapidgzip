#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <common.hpp>
#include <filereader/BufferView.hpp>
#include <filereader/Shared.hpp>
#include <TestHelpers.hpp>
#include <zlib.hpp>

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
    rapidgzip::BitReader bitReader( std::move( fileReader ) );
    bitReader.seek( 10 * CHAR_BIT );  // Deflate wrapper expects to start at deflate block
    ZlibInflateWrapper inflateWrapper( std::move( bitReader ) );

    std::vector<std::byte> decompressedResult( randomDNA.size() );
    auto [decompressedSize, footer] = inflateWrapper.readStream(
        reinterpret_cast<uint8_t*>( decompressedResult.data() ), decompressedResult.size() );
    REQUIRE_EQUAL( decompressedSize, randomDNA.size() );

    /* Unfortunately, because of the zlib API, we only know that we are at the end of a stream
     * AFTER trying trying to read more from it. */
    REQUIRE( !footer.has_value() );
    constexpr auto GZIP_FOOTER_SIZE = 8U;
    REQUIRE_EQUAL( inflateWrapper.tellEncoded(), ( compressedRandomDNA.size() - GZIP_FOOTER_SIZE ) * BYTE_SIZE );

    uint8_t dummy{ 0 };
    std::tie( decompressedSize, footer ) = inflateWrapper.readStream( &dummy, 1 );
    REQUIRE_EQUAL( decompressedSize, 0U );
    REQUIRE_EQUAL( inflateWrapper.tellEncoded(), compressedRandomDNA.size() * BYTE_SIZE );
    REQUIRE( footer.has_value() );
    if ( footer ) {
        REQUIRE_EQUAL( footer->uncompressedSize, randomDNA.size() );
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
    rapidgzip::BitReader bitReader( std::move( fileReader ) );
    bitReader.seek( 10 * CHAR_BIT );  // Deflate wrapper expects to start at deflate block
    ZlibInflateWrapper inflateWrapper( std::move( bitReader ) );

    std::vector<std::byte> decompressedResult( expectedResult.size(), std::byte( 1 ) );

    /* Each read call only reads up to the first deflate stream end. */
    auto [decompressedSize, footer] = inflateWrapper.readStream(
        reinterpret_cast<uint8_t*>( decompressedResult.data() ), decompressedResult.size() );
    REQUIRE_EQUAL( decompressedSize, dataToCompress.size() );
    /* ZlibInflateWrapper reads the next gzip header right after encountering any footer! */
    constexpr auto GZIP_HEADER_SIZE = 10U;
    REQUIRE_EQUAL( inflateWrapper.tellEncoded(), ( compressedData.size() / 2 + GZIP_HEADER_SIZE ) * BYTE_SIZE );

    std::tie( decompressedSize, footer ) = inflateWrapper.readStream(
        reinterpret_cast<uint8_t*>( decompressedResult.data() + 1U ), dataToCompress.size() );
    REQUIRE_EQUAL( decompressedSize, dataToCompress.size() );

    /* Unfortunately, because of the zlib API, we only know that we are at the end of a stream
     * AFTER trying trying to read more from it. */
    constexpr auto GZIP_FOOTER_SIZE = 8U;
    REQUIRE_EQUAL( inflateWrapper.tellEncoded(), ( compressedData.size() - GZIP_FOOTER_SIZE ) * BYTE_SIZE );

    REQUIRE( decompressedResult == expectedResult );
}


int
main()
{
    testGzipHeaderSkip();
    testMultiGzipStream();
    testGettingFooter();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
