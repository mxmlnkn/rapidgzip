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


void
compareBlockOffsets( const std::vector<std::pair<size_t, size_t> >& blockOffsets1,
                     const std::vector<std::pair<size_t, size_t> >& blockOffsets2 )
{
    REQUIRE_EQUAL( blockOffsets1.size(), blockOffsets2.size() );
    REQUIRE( blockOffsets1.size() > 0 );  // this is true even for an empty stream
    REQUIRE( blockOffsets1 == blockOffsets2 );
    if ( blockOffsets1 != blockOffsets2 ) {
        std::cerr << "Block offset sizes:\n"
                  << "    first  : " << blockOffsets1.size() << "\n"
                  << "    second : " << blockOffsets2.size() << "\n";
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


[[nodiscard]] std::vector<std::pair<size_t, size_t> >
getFooterOffsetsWithGzipReader( const std::filesystem::path& filePath )
{
    std::vector<std::pair<size_t, size_t> > blockOffsets;

    rapidgzip::GzipReader gzipReader{ std::make_unique<StandardFileReader>( filePath ) };
    while ( !gzipReader.eof() ) {
        const auto nBytesRead = gzipReader.read( -1, nullptr, std::numeric_limits<size_t>::max(),
                                                 StoppingPoint::END_OF_STREAM );
        /* Not strictly necessary but without it, the last offset will be appended twice because EOF is
         * only set after trying to read past the end. */
        if ( ( nBytesRead == 0 ) && gzipReader.eof() ) {
            break;
        }
        blockOffsets.emplace_back( gzipReader.tellCompressed(), gzipReader.tell() );
    }
    if ( blockOffsets.empty() || ( gzipReader.tellCompressed() != blockOffsets.back().first ) ) {
        blockOffsets.emplace_back( gzipReader.tellCompressed(), gzipReader.tell() );
    }

    return blockOffsets;
}


[[nodiscard]] std::vector<std::pair<size_t, size_t> >
getFooterOffsets( const ChunkData& chunkData )
{
    std::vector<std::pair<size_t, size_t> > blockOffsets( chunkData.footers.size() );
    std::transform( chunkData.footers.begin(), chunkData.footers.end(), blockOffsets.begin(),
                    [] ( const auto& footer ) {
                        return std::make_pair( footer.blockBoundary.encodedOffset,
                                               footer.blockBoundary.decodedOffset );
                    } );
    return blockOffsets;
}


[[nodiscard]] rapidgzip::BitReader
initBitReaderAtDeflateStream( const std::filesystem::path& filePath )
{
    rapidgzip::BitReader bitReader(
        std::make_unique<SharedFileReader>(
            std::make_unique<StandardFileReader>( filePath ) ) );
    rapidgzip::gzip::readHeader( bitReader );
    return bitReader;
}


[[nodiscard]] ChunkData
decodeWithDecodeBlockWithRapidgzip( const std::filesystem::path& filePath )
{
    auto bitReader = initBitReaderAtDeflateStream( filePath );
    const auto chunkData = GzipChunkFetcher<FetchingStrategy::FetchMultiStream>::decodeBlockWithRapidgzip(
        &bitReader,
        /* untilOffset */ std::numeric_limits<size_t>::max(),
        /* window */ std::nullopt,
        /* crc32Enabled */ true,
        /* maxDecompressedChunkSize */ std::numeric_limits<size_t>::max() );
    return chunkData;
}


[[nodiscard]] ChunkData
decodeWithDecodeBlock( const std::filesystem::path& filePath )
{
    auto bitReader = initBitReaderAtDeflateStream( filePath );
    std::atomic<bool> cancel{ false };
    const auto chunkData = GzipChunkFetcher<FetchingStrategy::FetchMultiStream>::decodeBlock(
        bitReader,
        bitReader.tell(),
        /* untilOffset */ std::numeric_limits<size_t>::max(),
        /* window */ std::nullopt,
        /* decodedSize */ std::nullopt,
        cancel );
    return chunkData;
}


template<typename InflateWrapper>
[[nodiscard]] ChunkData
decodeWithDecodeBlockWithInflateWrapper( const std::filesystem::path& filePath )
{
    auto bitReader = initBitReaderAtDeflateStream( filePath );
    using ChunkFetcher = GzipChunkFetcher<FetchingStrategy::FetchMultiStream>;
    const auto chunkData = ChunkFetcher::decodeBlockWithInflateWrapper<IsalInflateWrapper>(
        bitReader,
        bitReader.tell(),
        /* exactUntilOffset */ bitReader.size(),
        /* window */ {},
        /* decodedSize */ std::nullopt,
        /* crc32Enabled */ true );
    return chunkData;
}


void
printFooters( const std::vector<std::pair<size_t, size_t> >& blockOffsets )
{
    std::cerr << "Footers: " << blockOffsets.size() << ", positions: ";
    for ( const auto& [encodedOffset, decodedOffset] : blockOffsets ) {
        std::cerr << encodedOffset << "->" << decodedOffset << ", ";
    }
    std::cerr << "\n";
}


void
testGettingFooters( const std::filesystem::path& filePath )
{
    const auto footers = getFooterOffsetsWithGzipReader( filePath );
    compareBlockOffsets( footers, getFooterOffsets( decodeWithDecodeBlock( filePath ) ) );
    compareBlockOffsets( footers, getFooterOffsets( decodeWithDecodeBlockWithRapidgzip( filePath ) ) );
    const auto zlibChunk = decodeWithDecodeBlockWithInflateWrapper<ZlibInflateWrapper>( filePath );
    compareBlockOffsets( footers, getFooterOffsets( zlibChunk ) );
#ifdef WITH_ISAL
    const auto isalChunk = decodeWithDecodeBlockWithInflateWrapper<IsalInflateWrapper>( filePath );
    compareBlockOffsets( footers, getFooterOffsets( isalChunk ) );
#endif
}


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


void
testDecodeBlockWithInflateWrapperWithFiles( const std::filesystem::path& testFolder )
{
    using namespace std::literals::string_literals;
    for ( const auto& extension : { ".gz"s, ".bgz"s, ".igz"s, ".pgz"s } ) {
        for ( const auto* const fileName : GZIP_FILE_NAMES ) {
            std::cerr << "Testing decodeBlockWithInflateWrapper with " << fileName + extension << "\n";
            testGettingFooters( testFolder / ( fileName + extension ) );
        }
    }
}


int
main( int    argc,
      char** argv )
{
    /* Disable this because it requires 20xsilesia.tar.gz, which is not in the repo because of its size. */
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


    testDecodeBlockWithInflateWrapperWithFiles( testFolder );

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;


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

#ifdef WITH_ISAL
    /* When decodeBlock is able to delegate ISA-l, then the resulting chunks will be sized 128 KiB
     * to improve allocator behavior. All in all, testing the exact chunk sizes it not the most stable
     * unit test as it might be subject to further changes :/. For example, when decoding with rapidgzip
     * or replacing markers also tries to use chunk sizes of 128 KiB to reduce allocation fragmentation.
     * What should be important is the sum of the block sizes for markers and without. */
    test( "random-128KiB.gz" , 0, {}, { 32777, 98295 } );
    test( "random-128KiB.bgz", 0, {}, { 65280, 65280, 512 } );
    test( "random-128KiB.igz", 0, {}, { 65535, 65537 } );
    test( "random-128KiB.pgz", 0, {}, { 16387, 16389, 16395, 81901 } );

    test( "random-128KiB.gz" , 1, {}, { 32793, 65502 } );
    test( "random-128KiB.bgz", 1, {}, { 65280, 512 } );
    test( "random-128KiB.igz", 1, {}, { 65224, 313 } );
    test( "random-128KiB.pgz", 1, {}, { 16389, 16395, 16397, 65504 } );

    test( "random-128KiB.gz" , 2, {}, { 32777, 32725 } );
    test( "random-128KiB.bgz", 2, {}, { 512 } );
    test( "random-128KiB.igz", 2, {}, { 313 } );
    test( "random-128KiB.pgz", 2, {}, { 16395, 16397, 16389, 49115 } );
#else
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
#endif
    // *INDENT-ON*

    /**
     * @todo Add more tests of combinations like random + base, base + random
     */

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
