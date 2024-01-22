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
#include <filereader/Buffered.hpp>
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

        ChunkData configuredChunkData;
        configuredChunkData.setCRC32Enabled( false );
        configuredChunkData.fileType = FileType::GZIP;

        const auto result = GzipChunkFetcher<FetchingStrategy::FetchMultiStream>::decodeBlock(
            bitReader,
            blockOffset,
            /* untilOffset */ std::numeric_limits<size_t>::max(),
            /* window */ {},
            /* decodedSize */ std::nullopt,
            cancel,
            configuredChunkData );

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
            const std::vector<rapidgzip::ChunkData::Subchunk>& chunks )
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

    const auto split =
        [] ( ChunkData    chunk,
             const size_t splitChunkSize )
        {
            chunk.splitChunkSize = splitChunkSize;
            chunk.finalize( chunk.encodedEndOffsetInBits );
            return chunk.subchunks;
        };

    ChunkData chunk;
    chunk.encodedOffsetInBits = 0;
    chunk.maxEncodedOffsetInBits = 0;
    chunk.encodedSizeInBits = 0;

    using Subchunk = rapidgzip::ChunkData::Subchunk;
    using BlockBoundary = rapidgzip::ChunkData::BlockBoundary;
    chunk.finalize( 0 );
    REQUIRE( split( chunk, 1 ).empty() );

    /* Test split of data length == 1 and no block boundary. */
    {
        auto chunk2 = chunk;
        std::vector<uint8_t> data( 1, 0 );
        DecodedDataView toAppend;
        toAppend.data[0] = VectorView<uint8_t>( data.data(), data.size() );
        chunk2.append( toAppend );

        chunk2.finalize( 8 );
        const std::vector<Subchunk> expected = { Subchunk{ 0, 8, 1 } };
        REQUIRE( split( chunk2, 1 ) == expected );
        REQUIRE( split( chunk2, 2 ) == expected );
        REQUIRE( split( chunk2, 10 ) == expected );
    }

    /* Test split of data length == 1024 and 1 block boundary. */
    {
        std::vector<uint8_t> data( 1024, 0 );
        DecodedDataView toAppend;
        toAppend.data[0] = VectorView<uint8_t>( data.data(), data.size() );
        chunk.append( toAppend );

        chunk.blockBoundaries = { BlockBoundary{ 128, 1024 } };
        chunk.finalize( 128 );
        std::vector<Subchunk> expected = { Subchunk{ 0, 128, 1024 } };
        REQUIRE( split( chunk, 1 ) == expected );
        REQUIRE( split( chunk, 1024 ) == expected );
        REQUIRE( split( chunk, 10000 ) == expected );

        /* Test split of data length == 1024 and 2 block boundaries. */
        chunk.blockBoundaries = { BlockBoundary{ 30, 300 }, BlockBoundary{ 128, 1024 } };
        REQUIRE( split( chunk, 1024 ) == expected );
        REQUIRE( split( chunk, 10000 ) == expected );

        expected = { Subchunk{ 0, 30, 300 }, Subchunk{ 30, 128 - 30, 1024 - 300 } };
        REQUIRE( split( chunk, 400 ) == expected );
        REQUIRE( split( chunk, 512 ) == expected );
        REQUIRE( split( chunk, 600 ) == expected );
        REQUIRE( split( chunk, 1 ) == expected );
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

    ChunkData configuredChunkData;
    configuredChunkData.setCRC32Enabled( false );
    configuredChunkData.fileType = FileType::GZIP;

    std::atomic<bool> cancel{ false };
    std::vector<uint8_t> window( 32_Ki, 0 );
    const auto blockOffset = 4727960325ULL;
    const auto untilOffset = 4731261455ULL;
    const auto result = ChunkFetcher::decodeBlock(
        bitReader,
        blockOffset,
        untilOffset,
        /* window */ std::make_shared<WindowMap::Window>( window ),
        /* decodedSize */ 4171816,
        cancel,
        configuredChunkData,
        /* maxDecompressedChunkSize */ 4_Mi,
        /* isBgzfFile */ true );
}


template<typename InflateWrapper>
void
testWikidataException( const std::filesystem::path& rootFolder )
{
    rapidgzip::BitReader bitReader(
        std::make_unique<SharedFileReader>(
            std::make_unique<StandardFileReader>(
                rootFolder / "wikidata-20220103-all.json.gz-379508635534b--379510732698b.deflate" ) ) );

    const auto startOffset = 0ULL;
    const auto exactUntilOffset = 2097164ULL;
    const auto decodedSize = 4'140'634ULL;
    std::vector<uint8_t> initialWindow( 32_Ki, 0 );

    ChunkData result;
    result.setCRC32Enabled( true );
    result.fileType = FileType::GZIP;
    result.encodedOffsetInBits = startOffset;

    /* This did throw because it checks whether the exactUntilOffset has been reached. However, when a decoded size
     * is specified, it is used as a stop criterium. This means that for ISA-L the very last symbol, the end-of-block
     * symbol, might not be read from the input stream and, therefore, the exactUntilOffset was not reached.
     * This can be remedied by trying to read a single byte, which shouold reda nothing because the BitReader
     * is also given the exactUntilOffset and does not move more bits than that to the ISA-L input buffers. */
    const auto chunk =
        rapidgzip::GzipChunkFetcher<FetchingStrategy::FetchMultiStream>::decodeBlockWithInflateWrapper<InflateWrapper>(
            bitReader, exactUntilOffset, initialWindow, decodedSize, std::move( result ) );

    REQUIRE_EQUAL( chunk.encodedSizeInBits, exactUntilOffset );
    REQUIRE_EQUAL( chunk.decodedSizeInBytes, decodedSize );
}


void
compareBlockOffsets( const std::vector<std::pair<size_t, size_t> >& blockOffsets1,
                     const std::vector<std::pair<size_t, size_t> >& blockOffsets2 )
{
    /* Note that block offsets might also be empty because the first deflate block is ignored because that
     * is implied by the chunk data offset. */
    REQUIRE_EQUAL( blockOffsets1.size(), blockOffsets2.size() );
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
getFooterOffsetsWithGzipReader( UniqueFileReader&& fileReader )
{
    std::vector<std::pair<size_t, size_t> > blockOffsets;

    rapidgzip::GzipReader gzipReader{ std::move( fileReader ) };
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
initBitReaderAtDeflateStream( UniqueFileReader&& fileReader )
{
    rapidgzip::BitReader bitReader( std::move( fileReader ) );
    rapidgzip::gzip::readHeader( bitReader );
    return bitReader;
}


[[nodiscard]] ChunkData
decodeWithDecodeBlockWithRapidgzip( UniqueFileReader&& fileReader )
{
    auto bitReader = initBitReaderAtDeflateStream( std::move( fileReader ) );

    ChunkData result;
    result.setCRC32Enabled( true );
    result.fileType = FileType::GZIP;

    return GzipChunkFetcher<FetchingStrategy::FetchMultiStream>::decodeBlockWithRapidgzip(
        &bitReader,
        /* untilOffset */ std::numeric_limits<size_t>::max(),
        /* window */ std::nullopt,
        /* maxDecompressedChunkSize */ std::numeric_limits<size_t>::max(),
        std::move( result ) );
}


[[nodiscard]] ChunkData
decodeWithDecodeBlock( UniqueFileReader&& fileReader )
{
    auto bitReader = initBitReaderAtDeflateStream( std::move( fileReader ) );
    std::atomic<bool> cancel{ false };

    ChunkData configuredChunkData;
    configuredChunkData.setCRC32Enabled( false );
    configuredChunkData.fileType = FileType::GZIP;

    return GzipChunkFetcher<FetchingStrategy::FetchMultiStream>::decodeBlock(
        bitReader,
        bitReader.tell(),
        /* untilOffset */ std::numeric_limits<size_t>::max(),
        /* window */ {},
        /* decodedSize */ std::nullopt,
        cancel,
        configuredChunkData );
}


template<typename InflateWrapper>
[[nodiscard]] ChunkData
decodeWithDecodeBlockWithInflateWrapper( UniqueFileReader&& fileReader )
{
    auto bitReader = initBitReaderAtDeflateStream( std::move( fileReader ) );
    using ChunkFetcher = GzipChunkFetcher<FetchingStrategy::FetchMultiStream>;

    ChunkData result;
    result.setCRC32Enabled( true );
    result.encodedOffsetInBits = bitReader.tell();
    result.fileType = FileType::GZIP;

    return ChunkFetcher::decodeBlockWithInflateWrapper<InflateWrapper>(
        bitReader,
        /* exactUntilOffset */ bitReader.size().value(),
        /* window */ {},
        /* decodedSize */ std::nullopt,
        std::move( result ) );
}


void
printOffsets( const std::vector<std::pair<size_t, size_t> >& blockOffsets )
{
    std::cerr << "Offsets: " << blockOffsets.size() << ", positions: ";
    if ( blockOffsets.size() < 10 ) {
        for ( const auto& [encodedOffset, decodedOffset] : blockOffsets ) {
            std::cerr << encodedOffset << "->" << decodedOffset << ", ";
        }
    } else {
        for ( const auto& [encodedOffset, decodedOffset] : blockOffsets ) {
            std::cerr << "\n    " << encodedOffset << "->" << decodedOffset;
        }
    }
    std::cerr << "\n";
}


void
testGettingFooters( UniqueFileReader&& fileReader )
{
    const auto sharedFileReader = std::make_unique<SharedFileReader>( std::move( fileReader ) );

    const auto footers = getFooterOffsetsWithGzipReader( sharedFileReader->clone() );
    compareBlockOffsets( footers, getFooterOffsets( decodeWithDecodeBlock( sharedFileReader->clone() ) ) );
    compareBlockOffsets( footers, getFooterOffsets( decodeWithDecodeBlockWithRapidgzip( sharedFileReader->clone() ) ) );
    const auto zlibChunk = decodeWithDecodeBlockWithInflateWrapper<ZlibInflateWrapper>( sharedFileReader->clone() );
    compareBlockOffsets( footers, getFooterOffsets( zlibChunk ) );
#ifdef WITH_ISAL
    const auto isalChunk = decodeWithDecodeBlockWithInflateWrapper<IsalInflateWrapper>( sharedFileReader->clone() );
    compareBlockOffsets( footers, getFooterOffsets( isalChunk ) );
#endif
}


[[nodiscard]] std::vector<std::pair<size_t, size_t> >
getBlockStartsWithGzipReader( UniqueFileReader&& fileReader )
{
    std::vector<std::pair<size_t, size_t> > blockOffsets;

    rapidgzip::GzipReader gzipReader{ std::move( fileReader ) };
    const auto stoppingPoints =  static_cast<StoppingPoint>( StoppingPoint::END_OF_STREAM_HEADER
                                                             | StoppingPoint::END_OF_BLOCK );
    bool ignoredFirstHeader{ false };
    while ( !gzipReader.eof() ) {
        const auto nBytesRead = gzipReader.read( -1, nullptr, std::numeric_limits<size_t>::max(), stoppingPoints );
        /* Not strictly necessary but without it, the last offset will be appended twice because EOF is
         * only set after trying to read past the end. */
        if ( ( nBytesRead == 0 ) && gzipReader.eof() ) {
            break;
        }

        if ( ( gzipReader.currentPoint() == StoppingPoint::END_OF_STREAM_HEADER )
             && blockOffsets.empty() && !ignoredFirstHeader )
        {
            ignoredFirstHeader = true;
            continue;
        }

        if ( ( gzipReader.currentPoint() == StoppingPoint::END_OF_STREAM_HEADER )
             || ( ( gzipReader.currentPoint() == StoppingPoint::END_OF_BLOCK )
                  && gzipReader.currentDeflateBlock()
                  && !gzipReader.currentDeflateBlock()->isLastBlock() ) )
        {
            blockOffsets.emplace_back( gzipReader.tellCompressed(), gzipReader.tell() );
        }
    }

    return blockOffsets;
}


[[nodiscard]] std::vector<std::pair<size_t, size_t> >
getOffsets( const ChunkData& chunkData )
{
    std::vector<std::pair<size_t, size_t> > blockOffsets( chunkData.blockBoundaries.size() );
    std::transform( chunkData.blockBoundaries.begin(), chunkData.blockBoundaries.end(), blockOffsets.begin(),
                    [] ( const auto& boundary ) {
                        return std::make_pair( boundary.encodedOffset, boundary.decodedOffset );
                    } );
    return blockOffsets;
}


void
testGettingBoundaries( UniqueFileReader&& fileReader )
{
    const auto sharedFileReader = std::make_unique<SharedFileReader>( std::move( fileReader ) );

    const auto boundaries = getBlockStartsWithGzipReader( sharedFileReader->clone() );

    compareBlockOffsets( boundaries, getOffsets( decodeWithDecodeBlock( sharedFileReader->clone() ) ) );
    compareBlockOffsets( boundaries, getOffsets( decodeWithDecodeBlockWithRapidgzip( sharedFileReader->clone() ) ) );

    /* decodeWithDecodeBlockWithInflateWrapper does not collect blockBoundaries
     * because it is used for when the index is already known. */
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


const auto DNA_SYMBOLS =
    [] () {
        using namespace std::literals;
        constexpr auto DNA_SYMBOLS_STRING = "ACGT"sv;
        std::vector<std::byte> allowedSymbols( DNA_SYMBOLS_STRING.size() );
        std::transform( DNA_SYMBOLS_STRING.begin(), DNA_SYMBOLS_STRING.end(), allowedSymbols.begin(),
                        [] ( const auto c ) { return static_cast<std::byte>( c ); } );
        return allowedSymbols;
    }();


[[nodiscard]] UniqueFileReader
createCompressedRandomDNA( const size_t chunkSize = 10_Mi,
                           const size_t chunkCount = 10 )
{
    /* As there are 4 symbols, 2 bits per symbol should suffice and as the data is random, almost no backreferences
     * should be viable. This leads to a compression ratio of ~4, which is large enough for splitting and benign
     * enough to have multiple chunks with fairly little uncompressed data. */
    const auto randomDNA = createRandomData( chunkSize, DNA_SYMBOLS );
    const auto compressed = compressWithZlib( randomDNA, CompressionStrategy::HUFFMAN_ONLY );

    std::vector<char> multiStreamDataCompressed;
    for ( size_t i = 0; i < chunkCount; ++i ) {
        multiStreamDataCompressed.insert( multiStreamDataCompressed.end(),
                                          reinterpret_cast<const char*>( compressed.data() ),
                                          reinterpret_cast<const char*>( compressed.data() + compressed.size() ) );
    }

    return std::make_unique<BufferedFileReader>( std::move( multiStreamDataCompressed ) );
}


void
testDecodeBlockWithInflateWrapperWithFiles( const std::filesystem::path& testFolder )
{
    using namespace std::literals::string_literals;
    for ( const auto& extension : { ".gz"s, ".bgz"s, ".igz"s, ".pigz"s } ) {
        for ( const auto* const fileName : GZIP_FILE_NAMES ) {
            std::cerr << "Testing decodeBlockWithInflateWrapper with " << fileName + extension << "\n";
            testGettingBoundaries( std::make_unique<StandardFileReader>( testFolder / ( fileName + extension ) ) );
            testGettingFooters( std::make_unique<StandardFileReader>( testFolder / ( fileName + extension ) ) );
        }
    }

    testGettingBoundaries( createCompressedRandomDNA() );
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

    testWikidataException<ZlibInflateWrapper>( testFolder );
#ifdef WITH_ISAL
    testWikidataException<IsalInflateWrapper>( testFolder );
#endif

    testDecodeBlockWithInflateWrapperWithFiles( testFolder );

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
    test( "base64-32KiB.pigz", 0, {}, { 16796, 15972 } );
    test( "base64-32KiB.pigz", 1, { 15793 }, { 179 } );

#ifdef WITH_ISAL
    /* When decodeBlock is able to delegate ISA-l, then the resulting chunks will be sized 128 KiB
     * to improve allocator behavior. All in all, testing the exact chunk sizes it not the most stable
     * unit test as it might be subject to further changes :/. For example, when decoding with rapidgzip
     * or replacing markers also tries to use chunk sizes of 128 KiB to reduce allocation fragmentation.
     * What should be important is the sum of the block sizes for markers and without. */
    test( "random-128KiB.gz" , 0, {}, { 32777, 98295 } );
    test( "random-128KiB.bgz", 0, {}, { 65280, 65280, 512 } );
    test( "random-128KiB.igz", 0, {}, { 65535, 65537 } );
    test( "random-128KiB.pigz", 0, {}, { 16387, 16389, 16395, 81901 } );

    test( "random-128KiB.gz" , 1, {}, { 32793, 65502 } );
    test( "random-128KiB.bgz", 1, {}, { 65280, 512 } );
    test( "random-128KiB.igz", 1, {}, { 65224, 313 } );
    test( "random-128KiB.pigz", 1, {}, { 16389, 16395, 16397, 65504 } );

    test( "random-128KiB.gz" , 2, {}, { 32777, 32725 } );
    test( "random-128KiB.bgz", 2, {}, { 512 } );
    test( "random-128KiB.igz", 2, {}, { 313 } );
    test( "random-128KiB.pigz", 2, {}, { 16395, 16397, 16389, 49115 } );
#else
    test( "random-128KiB.gz" , 0, {}, { 32777, 32793, 32777, 32725 } );
    test( "random-128KiB.bgz", 0, {}, { 65280, 65280, 512 } );
    test( "random-128KiB.igz", 0, {}, { 65535, 65224, 313 } );
    test( "random-128KiB.pigz", 0, {}, { 16387, 16389, 16395, 16397, 16389, 16387, 16393, 16335 } );

    test( "random-128KiB.gz" , 1, {}, { 32793, 32777, 32725 } );
    test( "random-128KiB.bgz", 1, {}, { 65280, 512 } );
    test( "random-128KiB.igz", 1, {}, { 65224, 313 } );
    test( "random-128KiB.pigz", 1, {}, { 16389, 16395, 16397, 16389, 16387, 16393, 16335 } );

    test( "random-128KiB.gz" , 2, {}, { 32777, 32725 } );
    test( "random-128KiB.bgz", 2, {}, { 512 } );
    test( "random-128KiB.igz", 2, {}, { 313 } );
    test( "random-128KiB.pigz", 2, {}, { 16395, 16397, 16389, 16387, 16393, 16335 } );
#endif
    // *INDENT-ON*

    /**
     * @todo Add more tests of combinations like random + base, base + random
     */

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
