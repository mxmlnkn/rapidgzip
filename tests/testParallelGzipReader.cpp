#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <BufferedFileReader.hpp>
#include <ParallelGzipReader.hpp>
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
                   encoded.begin() + static_cast<ssize_t>( i * NANO_SAMPLE_GZIP.size() ) );
    }

    std::vector<char> decoded( NANO_SAMPLE_DECODED.size() * multiples );
    for ( size_t i = 0; i < multiples; ++i ) {
        std::copy( NANO_SAMPLE_DECODED.begin(), NANO_SAMPLE_DECODED.end(),
                   decoded.begin() + static_cast<ssize_t>( i * NANO_SAMPLE_DECODED.size() ) );
    }

    return { encoded, decoded };
}


void
testParallelDecoder( std::unique_ptr<FileReader> encoded,
                     std::unique_ptr<FileReader> decoded,
                     std::optional<GzipIndex>    index = {} )
{
    /* Test a simple full read. */

    ParallelGzipReader reader( std::move( encoded ) );
    if ( index ) {
        reader.setBlockOffsets( *index );
    }

    std::vector<char> result( decoded->size() * 2 );
    const auto nBytesRead = reader.read( result.data(), result.size() );
    REQUIRE( nBytesRead == decoded->size() );
    result.resize( nBytesRead );
    REQUIRE( reader.eof() );

    std::vector<char> decodedBuffer( decoded->size() );
    const auto nDecodedBytesRead = decoded->read( decodedBuffer.data(), decodedBuffer.size() );
    REQUIRE( nDecodedBytesRead == decodedBuffer.size() );
    REQUIRE( result == decodedBuffer );
}


void
testParallelDecoderNano()
{
    for ( int nCopies = 1; nCopies < 16; ++nCopies ) {
        std::cerr << "Testing parallel decoder with " << nCopies << " blocks\n";
        const auto [encoded, decoded] = duplicateNanoStream( nCopies );
        testParallelDecoder( std::make_unique<BufferedFileReader>( encoded ),
                             std::make_unique<BufferedFileReader>( decoded ) );
    }
}


[[nodiscard]] TemporaryDirectory
createTemporaryDirectory()
{
    const std::filesystem::path tmpFolderName = "pragzip.testParallelGzipReader." + std::to_string( unixTime() );
    std::filesystem::create_directory( tmpFolderName );
    return tmpFolderName;
}


void
testParallelDecodingWithIndex()
{
    const auto tmpFolder = createTemporaryDirectory();

    const auto decodedFile = tmpFolder.path() / "decoded";
    const auto encodedFile = tmpFolder.path() / "decoded.gz";
    const auto indexFile = tmpFolder.path() / "decoded.gz.index";
    createRandomTextFile( decodedFile, 64 * 1024 );

    {
        const auto command = "gzip -k " + std::string( decodedFile );
        const auto returnCode = std::system( command.c_str() );
        REQUIRE( returnCode == 0 );
        if ( returnCode != 0 ) {
            return;
        }
    }

    {
        const auto command =
            R"(python3 -c 'import indexed_gzip as ig; f = ig.IndexedGzipFile( ")"
            + std::string( encodedFile )
            + R"(" ); f.build_full_index(); f.export_index( ")"
            + std::string( indexFile )
            + R"(" );')";
        const auto returnCode = std::system( command.c_str() );
        REQUIRE( returnCode == 0 );
        if ( returnCode != 0 ) {
            return;
        }
    }

    std::cerr << "Test parallel decoder with larger gz file given an indexed_gzip index.\n";
    testParallelDecoder( std::make_unique<StandardFileReader>( encodedFile ),
                         std::make_unique<StandardFileReader>( decodedFile ),
                         readGzipIndex( std::make_unique<StandardFileReader>( indexFile.string() ) ) );
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
                                    binaryFilePath.begin() + static_cast<ssize_t>( lastSlash ) );
    }
    const std::filesystem::path rootFolder = findParentFolderContaining( binaryFolder, "tests/data/base64-256KiB.bgz" );

    testParallelDecoderNano();

    testParallelDecoder(
        std::make_unique<StandardFileReader>( ( rootFolder / "tests/data/base64-256KiB.bgz" ).string() ),
        std::make_unique<StandardFileReader>( ( rootFolder / "tests/data/base64-256KiB" ).string() ) );

    testParallelDecoder(
        std::make_unique<StandardFileReader>( ( rootFolder / "tests/data/base64-256KiB.gz" ).string() ),
        std::make_unique<StandardFileReader>( ( rootFolder / "tests/data/base64-256KiB" ).string() ),
        readGzipIndex(
            std::make_unique<StandardFileReader>( ( rootFolder / "tests/data/base64-256KiB.gz.index" ).string() ) )
    );

    try
    {
        testParallelDecodingWithIndex();
    } catch ( const std::exception& exception ) {
        /* Note that the destructor for TemporaryDirectory might not be called for uncaught exceptions!
         * @see https://stackoverflow.com/questions/222175/why-destructor-is-not-called-on-exception */
        std::cerr << "Caught exception: " << exception.what() << "\n";
        REQUIRE( false );
    }

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
