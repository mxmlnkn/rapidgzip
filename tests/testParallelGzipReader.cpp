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


void
testParallelDecoder( std::unique_ptr<FileReader> encoded,
                     std::unique_ptr<FileReader> decoded )
{
    /* Test a simple full read. */

    ParallelGzipReader reader( std::move( encoded ) );

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

    testParallelDecoder(
        std::make_unique<StandardFileReader>( ( rootFolder / "tests/data/base64-256KiB.bgz" ).string() ),
        std::make_unique<StandardFileReader>( ( rootFolder / "tests/data/base64-256KiB" ).string() ) );

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
