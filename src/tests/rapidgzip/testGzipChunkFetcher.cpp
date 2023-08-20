#include <algorithm>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#define TEST_DECODED_DATA

#include <common.hpp>
#include <ChunkData.hpp>
#include <definitions.hpp>
#include <filereader/Standard.hpp>
#include <FileUtils.hpp>
#include <GzipChunkFetcher.hpp>
#include <GzipReader.hpp>
#include <Prefetcher.hpp>
#include <TestHelpers.hpp>


using namespace rapidgzip;


[[nodiscard]] size_t
getBlockOffset( const std::string& filePath,
                size_t             blockIndex )
{
    GzipReader gzipReader( std::make_unique<StandardFileReader>( filePath ) );
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

    rapidgzip::BitReader bitReader(
        std::make_unique<SharedFileReader>(
            std::make_unique<StandardFileReader>( filePath ) ) );
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

        const auto& dataWithMarkers = result.getDataWithMarkers();
        std::vector<size_t> markerBlockSizesFound( dataWithMarkers.size() );
        std::transform( dataWithMarkers.begin(), dataWithMarkers.end(), markerBlockSizesFound.begin(),
                        [] ( const auto& buffer ) { return buffer.size(); } );

        const auto& data= result.getData();
        std::vector<size_t> blockSizesFound( data.size() );
        std::transform( data.begin(), data.end(), blockSizesFound.begin(),
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


std::ostream&
operator<<( std::ostream&                                    out,
            const std::vector<rapidgzip::ChunkData::Subblock>& chunks )
{
    out << "{";
    for ( const auto chunk : chunks ) {
        out << " (" << chunk.encodedOffset << ", " << chunk.encodedSize << ", " << chunk.decodedSize << ")";
    }
    out << " }";
    return out;
}


void
testBlockSplit()
{
    using DecodedDataView = rapidgzip::deflate::DecodedDataView;

    ChunkData chunk;
    chunk.encodedOffsetInBits = 0;
    chunk.maxEncodedOffsetInBits = 0;
    chunk.encodedSizeInBits = 0;

    using Subblock = rapidgzip::ChunkData::Subblock;
    using BlockBoundary = rapidgzip::ChunkData::BlockBoundary;
    chunk.finalize( 0 );
    REQUIRE( chunk.split( 1 ).empty() );

    /* Test split of data length == 1 and no block boundary. */
    {
        auto chunk2 = chunk;
        std::vector<uint8_t> data( 1, 0 );
        DecodedDataView toAppend;
        toAppend.data[0] = VectorView<uint8_t>( data.data(), data.size() );
        chunk2.append( toAppend );

        chunk2.finalize( 8 );
        const std::vector<Subblock> expected = { Subblock{ 0, 8, 1 } };
        REQUIRE( chunk2.split( 1 ) == expected );
        REQUIRE( chunk2.split( 2 ) == expected );
        REQUIRE( chunk2.split( 10 ) == expected );
    }

    /* Test split of data length == 1024 and 1 block boundary. */
    {
        std::vector<uint8_t> data( 1024, 0 );
        DecodedDataView toAppend;
        toAppend.data[0] = VectorView<uint8_t>( data.data(), data.size() );
        chunk.append( toAppend );

        chunk.blockBoundaries = { BlockBoundary{ 128, 1024 } };
        chunk.finalize( 128 );
        std::vector<Subblock> expected = { Subblock{ 0, 128, 1024 } };
        REQUIRE( chunk.split( 1 ) == expected );
        REQUIRE( chunk.split( 1024 ) == expected );
        REQUIRE( chunk.split( 10000 ) == expected );

        /* Test split of data length == 1024 and 2 block boundaries. */
        chunk.blockBoundaries = { BlockBoundary{ 30, 300 }, BlockBoundary{ 128, 1024 } };
        REQUIRE( chunk.split( 1024 ) == expected );
        REQUIRE( chunk.split( 10000 ) == expected );

        expected = { Subblock{ 0, 30, 300 }, Subblock{ 30, 128 - 30, 1024 - 300 } };
        REQUIRE( chunk.split( 400 ) == expected );
        REQUIRE( chunk.split( 512 ) == expected );
        REQUIRE( chunk.split( 600 ) == expected );
        REQUIRE( chunk.split( 1 ) == expected );
    }
}


void
testIsalBug()
{
    /**
     * m rapidgzip && src/tools/rapidgzip --import-index test-files/silesia/20xsilesia.tar.bgz.gzi -d -o /dev/null test-files/silesia/20xsilesia.tar.bgz
     * [2/2] Linking CXX executable src/tools/rapidgzip
     *   Block offset: 4727960325
     *   Until offset: 4731261455
     *   encoded size: 3301130
     *   decodedSize: 0
     *   alreadyDecoded: 4171815
     *   expected decodedSize: 4171816
     *   m_stream.read_in_length. 8
     * Caught exception: [ParallelGzipReader] Block does not contain the requested offset! Requested offset from
     * chunk fetcher: 1 GiB 687 MiB 62 KiB 495 B, decoded offset: 1 GiB 683 MiB 84 KiB 456 B, block data encoded offset:
     * 590995040 B 5 b, block data encoded size: 412641 B 2 b, block data size: 3 MiB 1002 KiB 39 B markers: 0
     * a2a926d84b8edc8baf88e50e7f690ca0  -
     */
    const std::string filePath{ "test-files/silesia/20xsilesia.tar.bgz" };
    const rapidgzip::BitReader bitReader(
        std::make_unique<SharedFileReader>(
            std::make_unique<StandardFileReader>( filePath ) ) );

    using ChunkFetcher = GzipChunkFetcher<FetchingStrategy::FetchMultiStream>;

    std::atomic<bool> cancel{ false };
    std::array<uint8_t, 32_Ki> window{};
    const auto blockOffset = 4727960325ULL;
    const auto untilOffset = 4731261455ULL;
    const auto result = ChunkFetcher::decodeBlock(
        bitReader,
        blockOffset,
        untilOffset,
        /* window */ ChunkFetcher::WindowView( window.data(), window.size() ),
        /* decodedSize */ 4171816,
        cancel,
        /* crc32Enabled */ false,
        /* maxDecompressedChunkSize */ 4_Mi,
        /* isBgzfFile */ true );
}


int
main( int    argc,
      char** argv )
{
    //testIsalBug();

    if ( argc == 0 ) {
        std::cerr << "Expected at least the launch command as the first argument!\n";
        return 1;
    }

    testBlockSplit();

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
