#include <algorithm>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#define TEST_DECODED_DATA

#include <common.hpp>
#include <ChunkData.hpp>
#include <chunkdecoding/GzipChunk.hpp>
#include <definitions.hpp>
#include <filereader/Buffered.hpp>
#include <filereader/Standard.hpp>
#include <FileUtils.hpp>
#include <GzipReader.hpp>
#include <Prefetcher.hpp>
#include <TestHelpers.hpp>


using namespace rapidgzip;


[[nodiscard]] size_t
getBlockOffset( const std::filesystem::path& filePath,
                size_t                       blockIndex )
{
    GzipReader gzipReader( std::make_unique<StandardFileReader>( filePath.string() ) );
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

    auto sharedFileReader =
        std::make_unique<SharedFileReader>(
            std::make_unique<StandardFileReader>( filePath.string() ) );
    const auto blockOffset = getBlockOffset( filePath, blockIndex );
    try {
        std::atomic<bool> cancel{ false };

        ChunkData::Configuration chunkDataConfiguration;
        chunkDataConfiguration.crc32Enabled = false;
        chunkDataConfiguration.fileType = FileType::GZIP;

        const auto result = GzipChunk<ChunkData>::decodeChunk(
            sharedFileReader->clone(),
            blockOffset,
            /* untilOffset */ std::numeric_limits<size_t>::max(),
            /* window */ {},
            /* decodedSize */ std::nullopt,
            cancel,
            chunkDataConfiguration );

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


void
testAutomaticMarkerResolution( const std::filesystem::path& testFolder )
{
    const auto test =
        [&testFolder]
        ( const std::string&         fileName,
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
    /* When decodeChunk is able to delegate ISA-l, then the resulting chunks will be sized 128 KiB
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
}


using DecodedDataView = rapidgzip::deflate::DecodedDataView;
using Subchunk = rapidgzip::ChunkData::Subchunk;;


std::ostream&
operator<<( std::ostream&                out,
            const std::vector<Subchunk>& chunks )
{
    out << "{";
    for ( const auto& chunk : chunks ) {
        out << " (" << chunk.encodedOffset << ", " << chunk.encodedSize << ", " << chunk.decodedSize << ")";
    }
    out << " }";
    return out;
}


[[nodiscard]] std::vector<Subchunk>
splitChunk( const size_t                      dataSize,
            const std::vector<BlockBoundary>& blockBoundaries,
            const size_t                      encodedEndOffsetInBits,
            const size_t                      splitChunkSize )
{
    ChunkData chunk;
    chunk.encodedOffsetInBits = 0;
    chunk.maxEncodedOffsetInBits = 0;
    chunk.encodedSizeInBits = 0;

    std::vector<uint8_t> data( dataSize, 0 );
    DecodedDataView toAppend;
    toAppend.data[0] = VectorView<uint8_t>( data.data(), data.size() );
    chunk.append( toAppend );

    chunk.blockBoundaries = blockBoundaries;
    chunk.splitChunkSize = splitChunkSize;
    chunk.finalize( encodedEndOffsetInBits );
    return chunk.subchunks();
}


void
testBlockSplit()
{
    const auto split =
        [] ( ChunkData&   chunk,
             const size_t splitChunkSize )
        {
            chunk.splitChunkSize = splitChunkSize;
            chunk.finalize( chunk.encodedEndOffsetInBits );
            return chunk.subchunks();
        };

    /* Test split of empty chunk. */
    {
        ChunkData chunk;
        chunk.encodedOffsetInBits = 0;
        chunk.maxEncodedOffsetInBits = 0;
        chunk.encodedSizeInBits = 0;

        chunk.finalize( 0 );
        REQUIRE( split( chunk, 1 ).empty() );
    }

    /* Test split of data length == 1 and no block boundary. */
    {
        ChunkData chunk2;
        chunk2.encodedOffsetInBits = 0;
        chunk2.maxEncodedOffsetInBits = 0;
        chunk2.encodedSizeInBits = 0;

        std::vector<uint8_t> data( 1, 0 );
        DecodedDataView toAppend;
        toAppend.data[0] = VectorView<uint8_t>( data.data(), data.size() );
        chunk2.append( toAppend );

        chunk2.finalize( 8 );
        const std::vector<Subchunk> expected = { Subchunk{ 0, 0, 8, 1 } };
        REQUIRE( split( chunk2, 1 ) == expected );
        REQUIRE( split( chunk2, 2 ) == expected );
        REQUIRE( split( chunk2, 10 ) == expected );
    }

    /* Test split of data length == 1024 and 1 block boundary. */
    {
        const size_t encodedEndOffsetInBits = 128;
        const std::vector<BlockBoundary> blockBoundaries = { BlockBoundary{ encodedEndOffsetInBits, 1024 } };
        const std::vector<Subchunk> expected = { Subchunk{ 0, 0, encodedEndOffsetInBits, 1024 } };
        REQUIRE( splitChunk( 1024, blockBoundaries, encodedEndOffsetInBits, 1 ) == expected );
        REQUIRE( splitChunk( 1024, blockBoundaries, encodedEndOffsetInBits, 1024 ) == expected );
        REQUIRE( splitChunk( 1024, blockBoundaries, encodedEndOffsetInBits, 10000 ) == expected );
    }

    /* Test split of data length == 1024 and 2 block boundaries. */
    {
        const size_t encodedEndOffsetInBits = 128;
        const std::vector<BlockBoundary> blockBoundaries = {
            BlockBoundary{ 30, 300 }, BlockBoundary{ encodedEndOffsetInBits, 1024 }
        };
        {
            const std::vector<Subchunk> expected = { Subchunk{ 0, 0, encodedEndOffsetInBits, 1024 } };
            REQUIRE( splitChunk( 1024, blockBoundaries, encodedEndOffsetInBits, 1024 ) == expected );
            REQUIRE( splitChunk( 1024, blockBoundaries, encodedEndOffsetInBits, 10000 ) == expected );
        }

        const std::vector<Subchunk> expected = {
            Subchunk{ 0, 0, 30, 300 }, Subchunk{ 30, 300, encodedEndOffsetInBits - 30, 1024 - 300 }
        };
        REQUIRE( splitChunk( 1024, blockBoundaries, encodedEndOffsetInBits, 400 ) == expected );
        REQUIRE( splitChunk( 1024, blockBoundaries, encodedEndOffsetInBits, 512 ) == expected );
        REQUIRE( splitChunk( 1024, blockBoundaries, encodedEndOffsetInBits, 600 ) == expected );
        REQUIRE( splitChunk( 1024, blockBoundaries, encodedEndOffsetInBits, 1 ) == expected );
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
    auto sharedFileReader =
        std::make_unique<SharedFileReader>(
            std::make_unique<StandardFileReader>( filePath ) );

    ChunkData::Configuration chunkDataConfiguration;
    chunkDataConfiguration.crc32Enabled = false;
    chunkDataConfiguration.fileType = FileType::GZIP;

    std::atomic<bool> cancel{ false };
    std::vector<uint8_t> window( 32_Ki, 0 );
    const auto blockOffset = 4727960325ULL;
    const auto untilOffset = 4731261455ULL;
    const auto result = GzipChunk<ChunkData>::decodeChunk(
        std::move( sharedFileReader ),
        blockOffset,
        untilOffset,
        /* window */ std::make_shared<ChunkData::Window>( window, CompressionType::GZIP ),
        /* decodedSize */ 4171816,
        cancel,
        chunkDataConfiguration,
        /* maxDecompressedChunkSize */ 4_Mi,
        /* isBgzfFile */ true );
}


template<typename InflateWrapper>
void
testWikidataException( const std::filesystem::path& rootFolder )
{
    auto sharedFileReader =
        std::make_unique<SharedFileReader>(
            std::make_unique<StandardFileReader>(
                ( rootFolder / "wikidata-20220103-all.json.gz-379508635534b--379510732698b.deflate" ).string() ) );

    const auto startOffset = 0ULL;
    const auto exactUntilOffset = 2097164ULL;
    const auto decodedSize = 4'140'634ULL;
    std::vector<uint8_t> initialWindow( 32_Ki, 0 );

    ChunkData::Configuration chunkDataConfiguration;
    chunkDataConfiguration.crc32Enabled = true;
    chunkDataConfiguration.fileType = FileType::GZIP;
    chunkDataConfiguration.encodedOffsetInBits = startOffset;

    /* This did throw because it checks whether the exactUntilOffset has been reached. However, when a decoded size
     * is specified, it is used as a stop criterium. This means that for ISA-L the very last symbol, the end-of-block
     * symbol, might not be read from the input stream and, therefore, the exactUntilOffset was not reached.
     * This can be remedied by trying to read a single byte, which shouold reda nothing because the BitReader
     * is also given the exactUntilOffset and does not move more bits than that to the ISA-L input buffers. */
    const auto chunk =
        GzipChunk<ChunkData>::decodeChunkWithInflateWrapper<InflateWrapper>(
            std::move( sharedFileReader ), exactUntilOffset, initialWindow, decodedSize, chunkDataConfiguration );

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


[[nodiscard]] gzip::BitReader
initBitReaderAtDeflateStream( UniqueFileReader&& fileReader )
{
    gzip::BitReader bitReader( std::move( fileReader ) );
    rapidgzip::gzip::readHeader( bitReader );
    return bitReader;
}


[[nodiscard]] std::pair<size_t, std::unique_ptr<SharedFileReader> >
getDeflateStreamOffsetAndSharedFileReader( UniqueFileReader&& fileReader )
{
    auto sharedFileReader = ensureSharedFileReader( std::move( fileReader ) );
    gzip::BitReader bitReader( sharedFileReader->clone() );
    gzip::readHeader( bitReader );
    return { bitReader.tell(), std::move( sharedFileReader ) };
}


[[nodiscard]] ChunkData
decodeWithDecodeBlockWithRapidgzip( UniqueFileReader&& fileReader )
{
    auto bitReader = initBitReaderAtDeflateStream( std::move( fileReader ) );

    ChunkData::Configuration chunkDataConfiguration;
    chunkDataConfiguration.crc32Enabled = true;
    chunkDataConfiguration.fileType = FileType::GZIP;

    return GzipChunk<ChunkData>::decodeChunkWithRapidgzip(
        &bitReader,
        /* untilOffset */ std::numeric_limits<size_t>::max(),
        /* window */ std::nullopt,
        /* maxDecompressedChunkSize */ std::numeric_limits<size_t>::max(),
        chunkDataConfiguration );
}


[[nodiscard]] ChunkData
decodeWithDecodeBlock( UniqueFileReader&& fileReader )
{
    auto [streamOffset, sharedFileReader] = getDeflateStreamOffsetAndSharedFileReader( std::move( fileReader ) );
    std::atomic<bool> cancel{ false };

    ChunkData::Configuration chunkDataConfiguration;
    chunkDataConfiguration.crc32Enabled = false;
    chunkDataConfiguration.fileType = FileType::GZIP;

    return GzipChunk<ChunkData>::decodeChunk(
        std::move( sharedFileReader ),
        streamOffset,
        /* untilOffset */ std::numeric_limits<size_t>::max(),
        /* window */ {},
        /* decodedSize */ std::nullopt,
        cancel,
        chunkDataConfiguration );
}


template<typename InflateWrapper>
[[nodiscard]] ChunkData
decodeWithDecodeBlockWithInflateWrapper( UniqueFileReader&& fileReader )
{
    auto [streamOffset, sharedFileReader] = getDeflateStreamOffsetAndSharedFileReader( std::move( fileReader ) );

    ChunkData::Configuration chunkDataConfiguration;
    chunkDataConfiguration.crc32Enabled = true;
    chunkDataConfiguration.encodedOffsetInBits = streamOffset;
    chunkDataConfiguration.fileType = FileType::GZIP;

    const auto exactUntilOffset = sharedFileReader->size().value() * BYTE_SIZE;
    return GzipChunk<ChunkData>::decodeChunkWithInflateWrapper<InflateWrapper>(
        std::move( sharedFileReader ),
        exactUntilOffset,
        /* window */ {},
        /* decodedSize */ std::nullopt,
        chunkDataConfiguration );
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
            std::cerr << "Testing decodeChunkWithInflateWrapper with " << fileName + extension << "\n";
            const auto filePath = ( testFolder / ( fileName + extension ) ).string();
            testGettingBoundaries( std::make_unique<StandardFileReader>( filePath ) );
            testGettingFooters( std::make_unique<StandardFileReader>( filePath ) );
        }
    }

    testGettingBoundaries( createCompressedRandomDNA() );
}


void
compareBlockBoundaries( const std::vector<BlockBoundary>& blockBoundaries,
                        const std::vector<BlockBoundary>& expectedBlockBoundaries,
                        const std::string_view            name )
{
    REQUIRE_EQUAL( blockBoundaries.size(), expectedBlockBoundaries.size() );
    if ( blockBoundaries.size() != expectedBlockBoundaries.size() ) {
        std::cerr << "Differing block boundary counts for: " << name << "\n";
        return;
    }

    for ( size_t i = 0; i < std::min( blockBoundaries.size(),expectedBlockBoundaries.size() ); ++i ) {
        const auto a = blockBoundaries[i];
        const auto b = expectedBlockBoundaries[i];
        if ( ( a.encodedOffset != b.encodedOffset ) || ( a.decodedOffset != b.decodedOffset ) ) {
            std::cerr << "Boundary at index " << i << " differs!\n";
        }
        REQUIRE_EQUAL( a.encodedOffset, b.encodedOffset );
        REQUIRE_EQUAL( a.decodedOffset, b.decodedOffset );
    }
}


void
testBlockBoundaries( const std::filesystem::path&      filePath,
                     const std::vector<BlockBoundary>& blockBoundaries )
{
    std::cerr << "Test deflate block boundary collection with: " << filePath.filename() << "\n";

    auto sharedFileReader =
        std::make_unique<SharedFileReader>(
            std::make_unique<StandardFileReader>( filePath.string() ) );

    const auto chunkOffset = getBlockOffset( filePath, 0 );  /* This skips the gzip header. */

    try {
        ChunkData::Configuration chunkDataConfiguration;
        chunkDataConfiguration.crc32Enabled = false;
        chunkDataConfiguration.fileType = FileType::GZIP;
        chunkDataConfiguration.encodedOffsetInBits = chunkOffset;

        gzip::BitReader bitReader{ sharedFileReader->clone() };
        bitReader.seek( chunkOffset );
        /* decodeChunkWithInflateWrapper is not tested because it always returns 0 because chunk splitting and
         * such is not assumed to be necessary anymore for those decoding functions that are only called with a
         * window and an exact until offset. */
        const auto result = GzipChunk<ChunkData>::decodeChunkWithRapidgzip(
            &bitReader,
            /* untilOffset */ std::numeric_limits<size_t>::max(),
            /* initialWindow */ {},
            /* maxDecompressedChunkSize */ std::numeric_limits<size_t>::max(),
            chunkDataConfiguration );
        compareBlockBoundaries( result.blockBoundaries, blockBoundaries, "rapidgzip with " + filePath.string() );
    } catch ( const std::exception& exception ) {
        REQUIRE( false && "Exception thrown!" );
        std::cerr << "  Failed to get block boundaries:\n"
                  << "    exception    : " << exception.what() << "\n\n";
    }
}


void
testBlockBoundaries( const std::filesystem::path& testFolder )
{
    /* Data can e.g. be gathered with rapidgzip --analyze. The first deflate block offset is not stored as a
     * boundary because it is redundant. So if there is only one deflate block, the list of boundaries will be empty. */
    testBlockBoundaries( testFolder / "base64-32KiB.gz", {} );

    // *INDENT-OFF*
    /* BGZF has an empty gzip stream at the end. This results in the deflate boundary being at a decoded offset
     * equal to the decoded size. */
    testBlockBoundaries( testFolder / "base64-32KiB.bgz", { { 202024, 32768 } } );
    testBlockBoundaries( testFolder / "base64-32KiB.igz", {} );
    testBlockBoundaries( testFolder / "base64-32KiB.pigz", { { 102274, 16796 } } );

    testBlockBoundaries( testFolder / "random-128KiB.gz",
                         { { 32806 * 8, 32777 }, { 65604 * 8, 65570 }, { 98386 * 8, 98347 } } );
    testBlockBoundaries( testFolder / "random-128KiB.bgz",
                         { { 65329 * 8, 65280 }, { 130640 * 8, 130560 }, { 131183 * 8, 131072 } } );
    testBlockBoundaries( testFolder / "random-128KiB.igz", { { 65564 * 8, 65535 }, { 130793 * 8, 130759 } } );
    // *INDENT-ON*
    testBlockBoundaries( testFolder / "random-128KiB.pigz", {
        { 16416 * 8, 16387 },
        { 32810 * 8, 32776 },
        { 49210 * 8, 49171 },
        { 65612 * 8, 65568 },
        { 82006 * 8, 81957 },
        { 98398 * 8, 98344 },
        { 114796 * 8, 114737 },
    } );

    testBlockBoundaries( testFolder / "base64-256KiB.bgz", {
        { 50500 * 8, 65280 },
        { 100981 * 8, 130560 },
        { 151466 * 8, 195840 },
        { 201946 * 8, 261120 },
        { 202772 * 8, 262144 },
    } );

    testBlockBoundaries( testFolder / "base64-256KiB.igz", {
        { 98782 * 8, 130759 },
        { 197542 * 8 + 4, 261520 },
    } );

    testBlockBoundaries( testFolder / "base64-256KiB.gz", {
        { 25634 * 8 + 1, 33717 },
        { 51431 * 8 + 0, 67669 },
        { 77181 * 8 + 5, 101553 },
        { 102927 * 8 + 7, 135433 },
        { 128676 * 8 + 3, 169317 },
        { 154383 * 8 + 6, 203155 },
        { 180129 * 8 + 2, 237030 },
    } );

    testBlockBoundaries( testFolder / "base64-256KiB.pigz", {
        { 12798 * 8 + 3, 16813 },
        { 25655 * 8 + 4, 33716 },
        { 38598 * 8 + 1, 50737 },
        { 51472 * 8 + 5, 67667 },
        { 64353 * 8 + 7, 84600 },
        { 77248 * 8 + 7, 101550 },
        { 90165 * 8 + 2, 118532 },
        { 99713 * 8 + 1, 131072 },
        { 99718 * 8 + 0, 131072 },
        { 112607 * 8 + 5, 148016 },
        { 125471 * 8 + 4, 164930 },
        { 138370 * 8 + 4, 181888 },
        { 151239 * 8 + 0, 198808 },
        { 164100 * 8 + 6, 215721 },
        { 176991 * 8 + 3, 232664 },
        { 189857 * 8 + 5, 249581 },
    } );
}


[[nodiscard]] std::vector<std::byte>
getDecompressed( const ChunkData& chunkData,
                 const size_t     decodedOffset )
{
    std::vector<std::byte> result;
    for ( auto it = rapidgzip::deflate::DecodedData::Iterator( chunkData, decodedOffset );
          static_cast<bool>( it ); ++it )
    {
        const auto& [buffer, size] = *it;
        const auto* const byteBuffer = reinterpret_cast<const std::byte*>( buffer );
        result.insert( result.end(), byteBuffer, byteBuffer + size );
    }
    return result;
}


[[nodiscard]] deflate::DecodedVector
getSparseWindowByBruteForce( gzip::BitReader&              bitReader,
                             const deflate::DecodedVector& window )
{
    constexpr bool printUsage = false;
    std::cerr << "[getSparseWindowByBruteForce]\n";
    ChunkData::Configuration chunkDataConfiguration;
    chunkDataConfiguration.crc32Enabled = false;
    chunkDataConfiguration.fileType = FileType::GZIP;
    chunkDataConfiguration.encodedOffsetInBits = bitReader.tell();

    const auto chunkData = GzipChunk<ChunkData>::decodeChunkWithRapidgzip(
        &bitReader, /* untilOffset */ std::numeric_limits<size_t>::max(),
        window, /* maxDecompressedChunkSize */ std::numeric_limits<size_t>::max(),
        chunkDataConfiguration );
    const auto expected = getDecompressed( chunkData, 0 );

    deflate::DecodedVector sparseWindow( window.data(), window.data() + window.size() );
    for ( size_t i = 0; i < window.size(); ++i ) {
        sparseWindow[i] = 0;

        bitReader.seek( chunkDataConfiguration.encodedOffsetInBits );
        const auto sparseChunkData = GzipChunk<ChunkData>::decodeChunkWithRapidgzip(
            &bitReader, /* untilOffset */ std::numeric_limits<size_t>::max(),
            sparseWindow, /* maxDecompressedChunkSize */ std::numeric_limits<size_t>::max(),
            chunkDataConfiguration );

        const auto decoded = getDecompressed( sparseChunkData, 0 );
        if ( decoded.size() != expected.size() ) {
            throw std::logic_error(
                "Inequal size when decoding with sparse window (" + std::to_string( expected.size() )
                + ") vs. without (" + std::to_string( expected.size() ) + ")!" );
        }

        if ( printUsage ) {
            if ( i % 128 == 0 ) {
                std::cerr << "\n";
            }
            std::cerr << ( decoded == expected ? "_" : "1" );
        }

        if ( decoded != expected ) {
            sparseWindow[i] = window[i];
        }
    }

    if ( printUsage ) {
        std::cerr << "\n";
    }

    return sparseWindow;
}


template<typename Container,
         typename Predicate>
[[nodiscard]] std::vector<std::pair<size_t, size_t> >
findRanges( const Container& container,
            const Predicate& predicate )
{
    std::vector<std::pair<size_t, size_t> > ranges;

    std::optional<size_t> rangeBegin{};
    std::optional<size_t> rangeEnd{};
    for ( size_t i = 0; i < container.size(); ++i ) {
        if ( predicate( container[i] ) ) {
            if ( !rangeBegin ) {
                rangeBegin = i;
            }
            rangeEnd = i;
        } else {
            if ( rangeBegin && rangeEnd ) {
                ranges.emplace_back( *rangeBegin, *rangeEnd );
            }
            rangeBegin.reset();
            rangeEnd.reset();
        }
    }

    return ranges;
}

void
testUsedWindowSymbolsWithFile( const std::filesystem::path& filePath )
{
    std::cerr << "Test window symbol usage tracking with: " << filePath.filename() << "\n";

    auto sharedFileReader =
        std::make_unique<SharedFileReader>(
            std::make_unique<StandardFileReader>( filePath.string() ) );

    /* Collect all deflate block boundaries and windows for testing. */

    ChunkData::Configuration chunkDataConfiguration;
    chunkDataConfiguration.crc32Enabled = false;
    chunkDataConfiguration.fileType = FileType::GZIP;
    chunkDataConfiguration.encodedOffsetInBits = getBlockOffset( filePath, 0 );  /* This skips the gzip header. */

    gzip::BitReader bitReader{ sharedFileReader->clone() };
    bitReader.seek( chunkDataConfiguration.encodedOffsetInBits );
    /* decodeChunkWithInflateWrapper is not tested because it always returns 0 because chunk splitting and
     * such is not assumed to be necessary anymore for those decoding functions that are only called with a
     * window and an exact until offset. */
    const auto chunkData = GzipChunk<ChunkData>::decodeChunkWithRapidgzip(
        &bitReader,
        /* untilOffset */ std::numeric_limits<size_t>::max(),
        /* initialWindow */ {},
        /* maxDecompressedChunkSize */ std::numeric_limits<size_t>::max(),
        chunkDataConfiguration );

    /* Try decoding from each block boundary with full windows. */
    for ( const auto boundary : chunkData.blockBoundaries ) {
        chunkDataConfiguration.encodedOffsetInBits = boundary.encodedOffset;
        bitReader.seek( chunkDataConfiguration.encodedOffsetInBits );

        const auto window = chunkData.getWindowAt( {}, boundary.decodedOffset );
        const auto partialChunkData = GzipChunk<ChunkData>::decodeChunkWithRapidgzip(
            &bitReader, /* untilOffset */ std::numeric_limits<size_t>::max(),
            window, /* maxDecompressedChunkSize */ std::numeric_limits<size_t>::max(),
            chunkDataConfiguration );

        const auto expected = getDecompressed( chunkData, boundary.decodedOffset );
        const auto result = getDecompressed( partialChunkData, 0 );
        if ( expected != result ) {
            std::cerr << "    Test failure when decoding from decoded offset " << boundary.decodedOffset << "\n";
        }
        REQUIRE_EQUAL( expected.size(), result.size() );
    }

    /* Try decoding from each block boundary with sparse windows. */
    for ( const auto boundary : chunkData.blockBoundaries ) {
        chunkDataConfiguration.encodedOffsetInBits = boundary.encodedOffset;
        bitReader.seek( chunkDataConfiguration.encodedOffsetInBits );
        const auto window = chunkData.getWindowAt( {}, boundary.decodedOffset );
        const auto sparseWindow = deflate::getSparseWindow( bitReader, window );

        bitReader.seek( chunkDataConfiguration.encodedOffsetInBits );
        const auto partialChunkData = GzipChunk<ChunkData>::decodeChunkWithRapidgzip(
            &bitReader, /* untilOffset */ std::numeric_limits<size_t>::max(),
            sparseWindow, /* maxDecompressedChunkSize */ std::numeric_limits<size_t>::max(),
            chunkDataConfiguration );

        const auto expected = getDecompressed( chunkData, boundary.decodedOffset );
        const auto result = getDecompressed( partialChunkData, 0 );
        if ( expected != result ) {
            std::cerr << "    Test failure when decoding from decoded offset " << boundary.decodedOffset << "\n";
        }
        REQUIRE_EQUAL( expected.size(), result.size() );
        REQUIRE( expected == result );
    }

    /* Try decoding from each block boundary with sparse windows. */
    for ( const auto boundary : chunkData.blockBoundaries ) {
        std::cerr << "    Test sparse window at block offset " << boundary.encodedOffset << "\n";

        chunkDataConfiguration.encodedOffsetInBits = boundary.encodedOffset;
        auto window = chunkData.getWindowAt( {}, boundary.decodedOffset );

        bitReader.seek( chunkDataConfiguration.encodedOffsetInBits );
        const auto usedWindowSymbols = deflate::getUsedWindowSymbols( bitReader );

    #if 0
        /* This is really time-consuming. Therefore do not run it continuously. */

        bitReader.seek( chunkDataConfiguration.encodedOffsetInBits );
        const auto bruteSparseWindow = getSparseWindowByBruteForce( bitReader, window );

        const auto windowUsedRanges = findRanges( usedWindowSymbols, [] ( auto value ) { return value; } );
        const auto windowUsedRanges2 = findRanges( bruteSparseWindow, [] ( auto value ) { return value != 0; } );

        std::cerr << "Used window ranges:\n   ";
        for ( const auto& [begin, end] : windowUsedRanges ) {
            std::cerr << " " << begin << "-" << end;
        }
        std::cerr << "\n";

        std::cerr << "Used window ranges determined by brute-force:\n   ";
        for ( const auto& [begin, end] : windowUsedRanges2 ) {
            std::cerr << " " << begin << "-" << end;
        }
        std::cerr << "\n";

        REQUIRE_EQUAL( windowUsedRanges.size(), windowUsedRanges2.size() );
        if ( windowUsedRanges != windowUsedRanges2 ) {
            throw std::logic_error( "Used window symbol detection is inconsistent!" );
        }

        size_t zeroedSymbolCount{ 0 };
        if ( window.size() == usedWindowSymbols.size() ) {
            for ( size_t i = 0; i < window.size(); ++i ) {
                if ( !usedWindowSymbols[i] ) {
                    window[i] = 0;
                    ++zeroedSymbolCount;
                }
            }
        }
        std::stringstream message;
        message << "    zeroedSymbolCount: " << zeroedSymbolCount * 100.0 / window.size() << " %\n\n";
        std::cerr << message.str();
    #endif

        bitReader.seek( chunkDataConfiguration.encodedOffsetInBits );
        const auto partialChunkData = GzipChunk<ChunkData>::decodeChunkWithRapidgzip(
            &bitReader, /* untilOffset */ std::numeric_limits<size_t>::max(),
            window, /* maxDecompressedChunkSize */ std::numeric_limits<size_t>::max(),
            chunkDataConfiguration );

        const auto expected = getDecompressed( chunkData, boundary.decodedOffset );
        const auto result = getDecompressed( partialChunkData, 0 );
        if ( expected != result ) {
            std::cerr << "    Test failure when decoding from decoded offset " << boundary.decodedOffset << "\n";
            const auto ranges = findRanges( result, [] ( const auto value ) { return value == std::byte( 0 ); } );
            for ( const auto& [begin, end] : ranges ) {
                std::cerr << "Found ZERO at " << begin << "-" << end << " ("
                          << end - begin + 1 << ")\n";
            }
            throw 3;
        }
        REQUIRE_EQUAL( expected.size(), result.size() );
        REQUIRE( expected == result );
    }
}


void
testUsedWindowSymbols( const std::filesystem::path& testFolder )
{
    testUsedWindowSymbolsWithFile( testFolder / "base64-256KiB.gz" );
    testUsedWindowSymbolsWithFile( testFolder / "base64-256KiB.bgz" );
    testUsedWindowSymbolsWithFile( testFolder / "base64-256KiB.igz" );
    testUsedWindowSymbolsWithFile( testFolder / "base64-256KiB.pigz" );

    testUsedWindowSymbolsWithFile( testFolder / "random-128KiB.gz" );
    testUsedWindowSymbolsWithFile( testFolder / "random-128KiB.bgz" );
    testUsedWindowSymbolsWithFile( testFolder / "random-128KiB.igz" );
    testUsedWindowSymbolsWithFile( testFolder / "random-128KiB.pigz" );
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
    testAutomaticMarkerResolution( testFolder );
    testBlockBoundaries( testFolder );
    testUsedWindowSymbols( testFolder );

    /**
     * @todo Add more tests of combinations like random + base, base + random
     */

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
