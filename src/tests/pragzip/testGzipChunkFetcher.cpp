#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <common.hpp>
#include <filereader/Buffered.hpp>
#include <filereader/Standard.hpp>
#include <ParallelGzipReader.hpp>
#include <pragzip.hpp>
#include <TestHelpers.hpp>


using namespace pragzip;


[[nodiscard]] size_t
getBlockOffset( const std::string& filePath,
                size_t             blockIndex )
{
    GzipReader</* CRC32 */ false> gzipReader( std::make_unique<StandardFileReader>( filePath ) );
    for ( size_t i = 0; ( i <= blockIndex ) && !gzipReader.eof(); ++i ) {
        [[maybe_unused]] const auto nBytesRead =
            gzipReader.read( -1, nullptr, std::numeric_limits<size_t>::max(),
                             static_cast<StoppingPoint>( StoppingPoint::END_OF_STREAM_HEADER
                                                         | StoppingPoint::END_OF_BLOCK ) );
        if ( gzipReader.currentDeflateBlock()->eos() ) {
            --i;
        }
    }
    return gzipReader.tellCompressed();
}


void
testAutomaticMarkerResolution( const std::filesystem::path& filePath,
                               const size_t                 blockIndex,
                               const std::vector<size_t>&   markerBlockSizes,
                               const std::vector<size_t>&   blockSizes )
{
    std::cerr << "Test Automatic Marker Resolution with: " << filePath.filename()
              << " starting from block " << blockIndex << "\n";

    pragzip::BitReader bitReader( std::make_unique<StandardFileReader>( filePath ) );
    const auto blockOffset = getBlockOffset( filePath, blockIndex );
    try {
        std::atomic<bool> cancel{ false };
        const auto result = GzipChunkFetcher<FetchingStrategy::FetchMultiStream>::decodeBlock(
            bitReader,
            blockOffset,
            /* untilOffset */ std::numeric_limits<size_t>::max(),
            /* window */ std::nullopt,
            /* decodedSize */ std::nullopt,
            cancel );

        std::vector<size_t> markerBlockSizesFound( result.dataWithMarkers.size() );
        std::transform( result.dataWithMarkers.begin(), result.dataWithMarkers.end(), markerBlockSizesFound.begin(),
                        [] ( const auto& buffer ) { return buffer.size(); } );

        std::vector<size_t> blockSizesFound( result.data.size() );
        std::transform( result.data.begin(), result.data.end(), blockSizesFound.begin(),
                        [] ( const auto& buffer ) { return buffer.size(); } );

        if ( ( markerBlockSizesFound != markerBlockSizes ) || ( blockSizesFound != blockSizes ) ) {
            std::cerr << "  block index  : " << blockIndex << "\n"
                      << "  block offset : " << blockOffset << "\n";

            const auto* const markerSizesDiffer = markerBlockSizesFound == markerBlockSizes ? "" : " differ";
            std::cerr << "  Sizes of deflate blocks with markers" << markerSizesDiffer << ":\n"
                      << "    Found    : " << markerBlockSizesFound << "\n"
                      << "    Expected : " << markerBlockSizes << "\n";

            const auto* const sizesDiffer = blockSizesFound == blockSizes ? "" : " differ";
            std::cerr << "  Sizes of fully-decoded deflate blocks" << sizesDiffer << ":\n"
                      << "    Found    : " << blockSizesFound << "\n"
                      << "    Expected : " << blockSizes << "\n\n";
        }

        REQUIRE_EQUAL( markerBlockSizesFound, markerBlockSizes );
        REQUIRE_EQUAL( blockSizesFound, blockSizes );
    } catch ( const std::exception& exception ) {
        REQUIRE( false && "Exception thrown!" );

        std::cerr << "  Failed to get block sizes:\n"
                  << "    exception    : " << exception.what() << "\n"
                  << "    block offset : " << blockOffset << "\n\n";
    }
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
    const auto testFolder =
        static_cast<std::filesystem::path>(
            findParentFolderContaining( binaryFolder, "src/tests/data/base64-256KiB.bgz" )
        ) / "src" / "tests" / "data";

    const auto test =
        [&] ( const std::string&         fileName,
              const size_t               blockIndex,
              const std::vector<size_t>& markerSizes,
              const std::vector<size_t>& sizes )
        {
            testAutomaticMarkerResolution( testFolder / fileName, blockIndex, markerSizes, sizes );
        };

    // *INDENT-OFF*
    test( "base64-32KiB.gz" , 0, {}, { 32768 } );
    test( "base64-32KiB.bgz", 0, {}, { 32768 } );
    test( "base64-32KiB.igz", 0, {}, { 32768 } );
    test( "base64-32KiB.pgz", 0, {}, { 16796, 15972 } );
    test( "base64-32KiB.pgz", 1, { 15793 }, { 179 } );

    test( "random-128KiB.gz" , 0, {}, { 32777, 32793, 32777, 32725 } );
    test( "random-128KiB.bgz", 0, {}, { 65280, 65280, 512 } );
    test( "random-128KiB.igz", 0, {}, { 65535, 65224, 313 } );
    test( "random-128KiB.pgz", 0, {}, { 16387, 16389, 16395, 16397, 16389, 16387, 16393, 16335 } );

    test( "random-128KiB.gz" , 1, {}, { 32793, 32777, 32725 } );
    test( "random-128KiB.bgz", 1, {}, { 65280, 512 } );
    test( "random-128KiB.igz", 1, {}, { 65224, 313 } );
    test( "random-128KiB.pgz", 1, {}, { 16389, 16395, 16397, 16389, 16387, 16393, 16335 } );

    test( "random-128KiB.gz" , 2, {}, { 32777, 32725 } );
    test( "random-128KiB.bgz", 2, {}, { 512 } );
    test( "random-128KiB.igz", 2, {}, { 313 } );
    test( "random-128KiB.pgz", 2, {}, { 16395, 16397, 16389, 16387, 16393, 16335 } );
    // *INDENT-ON*

    /**
     * @todo Add more tests of combinations like random + base, base + random
     */

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
