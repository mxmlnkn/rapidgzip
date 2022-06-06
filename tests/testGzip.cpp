#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <BufferedFileReader.hpp>
#include <common.hpp>
#include <pragzip.hpp>
#include <StandardFileReader.hpp>


using namespace pragzip;


const std::vector<uint8_t> NANO_SAMPLE_GZIP = {
    /*          ID1   ID2   CM    FLG  [       MTIME        ]     XFL   OS   [      FNAME = "nano"      ]  <Deflate */
    /* 0x00 */ 0x1F, 0x8B, 0x08, 0x08, 0xF5, 0x04, 0xDB, 0x61,   0x02, 0x03, 0x6E, 0x61, 0x6E, 0x6F, 0x00, 0x05,
    /* 0x10 */ 0xC1, 0xDD, 0x0E, 0x82, 0x20, 0x18, 0x00, 0xD0,   0xFB, 0x5E, 0x46, 0x92, 0x50, 0xB9, 0x94, 0xD8,
    /* 0x20 */ 0x6A, 0x96, 0x21, 0xD6, 0x4C, 0xB9, 0x54, 0xF4,   0x63, 0xFE, 0xA4, 0x86, 0x6E, 0xE6, 0xD3, 0x77,
    /* 0x30 */ 0x8E, 0xC5, 0x42, 0x51, 0x3C, 0xE8, 0xF9, 0x54,   0x7D, 0xD6, 0x46, 0x54, 0x04, 0xD6, 0x6F, 0x8A,
    /* 0x40 */ 0xB4, 0xF4, 0xB9, 0xF3, 0xCE, 0xAE, 0x2C, 0xB7,   0x2F, 0xD0, 0xA1, 0xB7, 0xA3, 0xA6, 0xD8, 0xF9,
    /* 0x50 */ 0xE5, 0x9C, 0x73, 0xE8, 0xEB, 0x3B, 0xA2, 0xDB,   0xE4, 0x2C, 0x95, 0xFB, 0xF4, 0xB2, 0x36, 0xC2,
    /* 0x60 */ 0xC7, 0x64, 0x54, 0x3F, 0x30, 0x2C, 0xE9, 0x0F,   0x6A, 0xD1, 0x4A, 0x78, 0x13, 0xD9, 0xAC, 0x0F,
    /* 0x70 */ 0xB4, 0x78, 0x0C, 0x36, 0x66, 0x8A, 0xDA, 0xA0,   0x93, 0xB3, 0xCB, 0x6E, 0x6E, 0x4D, 0xB8, 0x09,
    /* 0x80 */ 0xF1, 0x18, 0xB5, 0x25, 0xC3, 0x32, 0x8D, 0x7D,   0x30, 0x41, 0x47, 0xFE, 0x36, 0xC3, 0xC5, 0x28,
    /* 0x90 */ 0x80, 0x00, 0x00, 0x00
};


const std::string_view NANO_SAMPLE_DECODED{
    "s3OZ93mdq4cnufOc5gurR0dQ7D/WVHBXsTgdA6z0fYzDGCXDgleL09xp/tc2S6VjJ31PoZyghBPl\n"
    "ZtdZO6p5xs7g9YNmsMBZ9s8kQq2BK2e5DhA3oJjbB3QRM7gh8k5"
};


[[nodiscard]] std::pair<std::vector<char>, std::vector<char> >
duplicateNanoStream( size_t multiples )
{
    std::vector<char> encoded( NANO_SAMPLE_GZIP.size() * multiples );
    for ( size_t i = 0; i < multiples; ++i ) {
        std::copy( NANO_SAMPLE_GZIP.begin(), NANO_SAMPLE_GZIP.end(),
                   encoded.begin() + i * NANO_SAMPLE_GZIP.size() );
    }

    std::vector<char> decoded( NANO_SAMPLE_DECODED.size() * multiples );
    for ( size_t i = 0; i < multiples; ++i ) {
        std::copy( NANO_SAMPLE_DECODED.begin(), NANO_SAMPLE_DECODED.end(),
                   decoded.begin() + i * NANO_SAMPLE_DECODED.size() );
    }

    return { encoded, decoded };
}


void
testSerialDecoderNanoSample()
{
    const std::vector<char> signedData( NANO_SAMPLE_GZIP.begin(), NANO_SAMPLE_GZIP.end() );
    GzipReader gzipReader( std::make_unique<BufferedFileReader>( signedData ) );

    std::vector<char> result( NANO_SAMPLE_DECODED.size() + 10, 0 );
    const auto nBytesDecoded = gzipReader.read( -1, result.data(), result.size() );
    REQUIRE( nBytesDecoded == NANO_SAMPLE_DECODED.size() );
    REQUIRE( std::equal( NANO_SAMPLE_DECODED.begin(), NANO_SAMPLE_DECODED.end(), result.begin() ) );
}


void
testSerialDecoderNanoSample( size_t multiples,
                             size_t bufferSize )
{
    const auto [encoded, decoded] = duplicateNanoStream( multiples );

    GzipReader gzipReader( std::make_unique<BufferedFileReader>( encoded ) );

    std::vector<char> result( bufferSize, 0 );
    size_t totalBytesDecoded = 0;
    while ( !gzipReader.eof() ) {
        const auto nBytesDecoded = gzipReader.read( -1, result.data(), result.size() );
        if ( nBytesDecoded < result.size() ) {
            REQUIRE( nBytesDecoded == ( decoded.size() % bufferSize ) );
        }
        REQUIRE( std::equal( result.begin(), result.begin() + nBytesDecoded, decoded.begin() + totalBytesDecoded ) );
        totalBytesDecoded += nBytesDecoded;
    }
}


void
testSerialDecoderNanoSampleStoppingPoints()
{
    const auto multiples = 2;
    const auto [encoded, decoded] = duplicateNanoStream( multiples );

    const auto collectStoppingPoints =
        [&encoded = encoded, &decoded = decoded] ( GzipReader::StoppingPoint stoppingPoint )
        {
            std::vector<size_t> offsets;
            std::vector<size_t> compressedOffsets;

            GzipReader gzipReader( std::make_unique<BufferedFileReader>( encoded ) );

            std::vector<char> result( decoded.size(), 0 );
            size_t totalBytesDecoded = 0;
            while ( !gzipReader.eof() ) {
                const auto nBytesDecoded = gzipReader.read( -1, result.data(), result.size(), stoppingPoint );
                REQUIRE( std::equal( result.begin(), result.begin() + nBytesDecoded,
                                     decoded.begin() + totalBytesDecoded ) );
                totalBytesDecoded += nBytesDecoded;

                offsets.emplace_back( gzipReader.tell() );
                compressedOffsets.emplace_back( gzipReader.tellCompressed() );
            }

            return std::make_pair( offsets, compressedOffsets );
        };

    {
        const auto [offsets, compressedOffsets] = collectStoppingPoints( GzipReader::StoppingPoint::NONE );
        REQUIRE( offsets == std::vector<size_t>( { decoded.size() } ) );
        REQUIRE( compressedOffsets == std::vector<size_t>( { encoded.size() * 8 } ) );
    }

    {
        const auto [offsets, compressedOffsets] = collectStoppingPoints( GzipReader::StoppingPoint::END_OF_STREAM );
        REQUIRE( offsets == std::vector<size_t>( { NANO_SAMPLE_DECODED.size(), decoded.size() } ) );
        REQUIRE( compressedOffsets == std::vector<size_t>( { NANO_SAMPLE_GZIP.size() * 8, encoded.size() * 8 } ) );
    }

    {
        const auto [offsets, compressedOffsets] =
            collectStoppingPoints( GzipReader::StoppingPoint::END_OF_STREAM_HEADER );
        REQUIRE( offsets == std::vector<size_t>( { 0, NANO_SAMPLE_DECODED.size(), decoded.size() } ) );
        REQUIRE( compressedOffsets == std::vector<size_t>( { 15 * 8, ( NANO_SAMPLE_GZIP.size() + 15 ) * 8,
                                                             encoded.size() * 8 } ) );
    }

    {
        const auto [offsets, compressedOffsets] =
            collectStoppingPoints( GzipReader::StoppingPoint::END_OF_BLOCK_HEADER );
        REQUIRE( offsets == std::vector<size_t>( { 0, NANO_SAMPLE_DECODED.size(), decoded.size() } ) );
        REQUIRE( compressedOffsets == std::vector<size_t>( { 15 * 8 + 270, ( NANO_SAMPLE_GZIP.size() + 15 ) * 8 + 270,
                                                             encoded.size() * 8 } ) );
    }

    {
        const auto [offsets, compressedOffsets] =
            collectStoppingPoints( GzipReader::StoppingPoint::END_OF_BLOCK );
        REQUIRE( offsets == std::vector<size_t>( { NANO_SAMPLE_DECODED.size(), decoded.size(), decoded.size() } ) );
        constexpr auto FOOTER_SIZE = 8;
        REQUIRE( compressedOffsets == std::vector<size_t>( { ( NANO_SAMPLE_GZIP.size() - FOOTER_SIZE ) * 8 ,
                                                             ( encoded.size() - FOOTER_SIZE ) * 8,
                                                             encoded.size() * 8 } ) );
    }
}


void
testSerialDecoder( std::filesystem::path const& decodedFilePath,
                   std::filesystem::path const& encodedFilePath,
                   size_t                const  bufferSize )
{
    std::vector<char> decodedBuffer( bufferSize );
    std::vector<char> buffer( bufferSize );

    std::ifstream decodedFile( decodedFilePath );
    GzipReader gzipReader( std::make_unique<StandardFileReader>( encodedFilePath.string().c_str() ) );

    size_t totalBytesDecoded{ 0 };
    while ( !gzipReader.eof() ) {
        const auto nBytesRead = gzipReader.read( -1, buffer.data(), buffer.size() );
        buffer.resize( nBytesRead );
        if ( nBytesRead == 0 ) {
            REQUIRE( gzipReader.eof() );
            break;
        }

        /* Compare with ground truth. */
        decodedBuffer.resize( buffer.size() );
        decodedFile.read( decodedBuffer.data(), decodedBuffer.size() );
        REQUIRE( !decodedFile.fail() );
        REQUIRE( static_cast<size_t>( decodedFile.gcount() ) == buffer.size() );

        REQUIRE( std::equal( buffer.begin(), buffer.end(), decodedBuffer.begin() ) );
        if ( !std::equal( buffer.begin(), buffer.end(), decodedBuffer.begin() ) ) {
            for ( size_t i = 0; i < buffer.size(); ++i ) {
                if ( buffer[i] != decodedBuffer[i] ) {
                    std::cerr << "Decoded contents differ at position " << i << " B: "
                              << buffer[i] << " != " << decodedBuffer[i] << " ("
                              << (int)buffer[i] << " != " << (int)decodedBuffer[i] << ")\n";
                    break;
                }
            }
        }

        totalBytesDecoded += buffer.size();
    }

    REQUIRE( totalBytesDecoded == fileSize( decodedFilePath ) );
    std::cerr << "Decoded " << decodedFilePath.filename() << " with buffer size " << bufferSize << "\n";
}


int
main( int    argc,
      char** argv )
{
    if ( argc == 0 ) {
        std::cerr << "Expected at least the launch command as the first argument!\n";
        return 1;
    }

    testSerialDecoderNanoSampleStoppingPoints();
    testSerialDecoderNanoSample();
    for ( const auto multiples : { 1, 2, 3, 10 } ) {
        for ( const auto bufferSize : { 1, 2, 3, 4, 12, 32, 300, 1024, 1024 * 1024 } ) {
            std::cerr << "Try to decode " << multiples << " nano samples with buffer size: " << bufferSize << "\n";
            testSerialDecoderNanoSample( multiples, bufferSize );
        }
    }

    const std::string binaryFilePath( argv[0] );
    std::string binaryFolder = ".";
    if ( const auto lastSlash = binaryFilePath.find_last_of( '/' ); lastSlash != std::string::npos ) {
        binaryFolder = std::string( binaryFilePath.begin(), binaryFilePath.begin() + lastSlash );
    }
    const std::filesystem::path rootFolder = findParentFolderContaining( binaryFolder, "tests/data/base64-256KiB.gz" );

    for ( auto const& entry : std::filesystem::directory_iterator( rootFolder / "tests/data" ) ) {
        if ( !entry.is_regular_file() || !entry.path().has_extension() ) {
            continue;
        }

        const auto extension = entry.path().extension();
        const std::unordered_set<std::string> validExtensions = { ".gz", ".bgz", ".pgz" };
        if ( validExtensions.find( extension ) == validExtensions.end() ) {
            continue;
        }

        const auto& encodedFilePath = entry.path();
        auto decodedFilePath = entry.path();
        decodedFilePath.replace_extension( "" );
        if ( !std::filesystem::exists( decodedFilePath ) ) {
            continue;
        }

        try
        {
            for ( const auto bufferSize : { 1, 2, 12, 32, 1000, 1024, 128 * 1024, 1024 * 1024, 64 * 1024 * 1024 } ) {
                testSerialDecoder( decodedFilePath.string(), encodedFilePath.string(), bufferSize );
            }
        }
        catch ( const std::exception& e )
        {
            std::cerr << "Exception was thrown: " << e.what() << "\n";
            gnTestErrors++;
        }
    }

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
