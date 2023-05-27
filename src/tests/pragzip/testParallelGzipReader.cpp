#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <common.hpp>
#include <filereader/Buffered.hpp>
#include <filereader/BufferView.hpp>
#include <filereader/Standard.hpp>
#include <ParallelGzipReader.hpp>
#include <pragzip.hpp>
#include <TestHelpers.hpp>
#include <zlib.hpp>


using namespace pragzip;


// *INDENT-OFF*
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
// *INDENT-ON*


const std::string_view NANO_SAMPLE_DECODED{
    "s3OZ93mdq4cnufOc5gurR0dQ7D/WVHBXsTgdA6z0fYzDGCXDgleL09xp/tc2S6VjJ31PoZyghBPl\n"
    "ZtdZO6p5xs7g9YNmsMBZ9s8kQq2BK2e5DhA3oJjbB3QRM7gh8k5"
};


const auto DNA_SYMBOLS =
    [] () {
        using namespace std::literals;
        constexpr auto DNA_SYMBOLS_STRING = "ACGT"sv;
        std::vector<std::byte> allowedSymbols( DNA_SYMBOLS_STRING.size() );
        std::transform( DNA_SYMBOLS_STRING.begin(), DNA_SYMBOLS_STRING.end(), allowedSymbols.begin(),
                        [] ( const auto c ) { return static_cast<std::byte>( c ); } );
        return allowedSymbols;
    }();

[[nodiscard]] std::pair<std::vector<char>, std::vector<char> >
duplicateNanoStream( size_t multiples )
{
    std::vector<char> encoded( NANO_SAMPLE_GZIP.size() * multiples );
    for ( size_t i = 0; i < multiples; ++i ) {
        std::copy( NANO_SAMPLE_GZIP.begin(), NANO_SAMPLE_GZIP.end(),
                   encoded.begin() + static_cast<std::ptrdiff_t>( i * NANO_SAMPLE_GZIP.size() ) );
    }

    std::vector<char> decoded( NANO_SAMPLE_DECODED.size() * multiples );
    for ( size_t i = 0; i < multiples; ++i ) {
        std::copy( NANO_SAMPLE_DECODED.begin(), NANO_SAMPLE_DECODED.end(),
                   decoded.begin() + static_cast<std::ptrdiff_t>( i * NANO_SAMPLE_DECODED.size() ) );
    }

    return { encoded, decoded };
}


void
testParallelDecoder( UniqueFileReader         encoded,
                     UniqueFileReader         decoded,
                     std::optional<GzipIndex> index = {},
                     size_t                   nBlocksToSkip = 31 )
{
    /* Test a simple full read. */

    ParallelGzipReader reader( std::move( encoded ), /* 32 KiB chunks to skip. */ nBlocksToSkip );
    reader.setCRC32Enabled( true );
    if ( index ) {
        reader.setBlockOffsets( *index );
        REQUIRE( reader.blockOffsetsComplete() );
    }

    std::vector<char> result( decoded->size() * 2 );
    const auto nBytesRead = reader.read( result.data(), std::max( size_t( 1 ), result.size() ) );
    REQUIRE( nBytesRead == decoded->size() );
    result.resize( nBytesRead );
    REQUIRE( reader.eof() );

    std::vector<char> decodedBuffer( decoded->size() );
    const auto nDecodedBytesRead = decoded->read( decodedBuffer.data(), decodedBuffer.size() );
    REQUIRE( nDecodedBytesRead == decodedBuffer.size() );
    REQUIRE( result == decodedBuffer );

    if ( result != decodedBuffer ) {
        for ( size_t i = 0; i < result.size(); ++i ) {
            if ( result[i] != decodedBuffer[i] ) {
                std::cerr << "Decoded contents differ at position " << i << " B out of " << decoded->size() << " B: "
                          << "Decoded != Truth: "
                          << result[i] << " != " << decodedBuffer[i] << " ("
                          << (int)result[i] << " != " << (int)decodedBuffer[i] << ")\n";
                break;
            }
        }
    }
}


void
testParallelDecoder( const std::filesystem::path& encoded,
                     const std::filesystem::path& decoded = {},
                     const std::filesystem::path& index = {} )
{
    std::cerr << "Testing " << encoded.filename() << ( index.empty() ? "" : " with indexed_gzip index" )
              << " (" << std::filesystem::file_size( encoded ) << " B)\n";

    auto decodedFilePath = decoded;
    if ( decodedFilePath.empty() ) {
        decodedFilePath = encoded;
        decodedFilePath.replace_extension();
    }

    auto indexData = index.empty()
                     ? std::nullopt
                     : std::make_optional( readGzipIndex( std::make_unique<StandardFileReader>( index.string() ) ) );
    for ( const size_t nBlocksToSkip : { 0, 1, 2, 4, 8, 16, 24, 32, 64, 128 } ) {
        testParallelDecoder( std::make_unique<StandardFileReader>( encoded.string() ),
                             std::make_unique<StandardFileReader>( decodedFilePath.string() ),
                             indexData,
                             nBlocksToSkip );
    }
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


void
testParallelDecodingWithIndex( const TemporaryDirectory& tmpFolder )
{
    const auto decodedFile = tmpFolder.path() / "decoded";
    const auto encodedFile = tmpFolder.path() / "decoded.gz";
    const auto indexFile = tmpFolder.path() / "decoded.gz.index";
    createRandomTextFile( decodedFile, 64_Ki );

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
    const auto realIndex = readGzipIndex( std::make_unique<StandardFileReader>( indexFile.string() ) );
    for ( const size_t nBlocksToSkip : { 0, 1, 2, 4, 8, 16, 24, 32, 64, 128 } ) {
        testParallelDecoder( std::make_unique<StandardFileReader>( encodedFile ),
                             std::make_unique<StandardFileReader>( decodedFile ),
                             realIndex, nBlocksToSkip );
    }

    std::cerr << "Test exporting and reimporting index.\n";
    ParallelGzipReader reader( std::make_unique<StandardFileReader>( encodedFile ) );
    reader.setCRC32Enabled( true );
    reader.setBlockOffsets( realIndex );

    const auto reconstructedIndex = reader.gzipIndex();
    REQUIRE_EQUAL( reconstructedIndex.compressedSizeInBytes, realIndex.compressedSizeInBytes );
    REQUIRE_EQUAL( reconstructedIndex.uncompressedSizeInBytes, realIndex.uncompressedSizeInBytes );
    REQUIRE_EQUAL( reconstructedIndex.windowSizeInBytes, 32_Ki );
    REQUIRE( reconstructedIndex.checkpointSpacing >= reconstructedIndex.windowSizeInBytes );
    REQUIRE_EQUAL( reconstructedIndex.checkpoints.size(), realIndex.checkpoints.size() );
    if ( reconstructedIndex.checkpoints.size() == realIndex.checkpoints.size() ) {
        for ( size_t i = 0; i < reconstructedIndex.checkpoints.size(); ++i ) {
            const auto& reconstructed = reconstructedIndex.checkpoints[i];
            const auto& real = realIndex.checkpoints[i];
            REQUIRE_EQUAL( reconstructed.compressedOffsetInBits, real.compressedOffsetInBits );
            REQUIRE_EQUAL( reconstructed.uncompressedOffsetInBytes, real.uncompressedOffsetInBytes );
            REQUIRE_EQUAL( reconstructed.window.size(), real.window.size() );
            REQUIRE( reconstructed.window == real.window );
        }
    }

    testParallelDecoder( std::make_unique<StandardFileReader>( encodedFile ),
                         std::make_unique<StandardFileReader>( decodedFile ),
                         reconstructedIndex );

    const auto writtenIndexFile = tmpFolder.path() / "decoded.gz.written-index";
    {
        const auto file = throwingOpen( writtenIndexFile.string(), "wb" );
        const auto checkedWrite =
            [&file] ( const void* buffer, size_t size )
            {
                if ( std::fwrite( buffer, 1, size, file.get() ) != size ) {
                    throw std::runtime_error( "Failed to write data to index!" );
                }
            };
        writeGzipIndex( realIndex, checkedWrite );
    }
    const auto rewrittenIndex = readGzipIndex( std::make_unique<StandardFileReader>( writtenIndexFile.string() ) );

    REQUIRE_EQUAL( rewrittenIndex.compressedSizeInBytes, realIndex.compressedSizeInBytes );
    REQUIRE_EQUAL( rewrittenIndex.uncompressedSizeInBytes, realIndex.uncompressedSizeInBytes );
    REQUIRE_EQUAL( rewrittenIndex.windowSizeInBytes, 32_Ki );
    REQUIRE( rewrittenIndex.checkpointSpacing >= rewrittenIndex.windowSizeInBytes );
    REQUIRE_EQUAL( rewrittenIndex.checkpoints.size(), realIndex.checkpoints.size() );
    REQUIRE( rewrittenIndex.checkpoints == realIndex.checkpoints );

    testParallelDecoder( std::make_unique<StandardFileReader>( encodedFile ),
                         std::make_unique<StandardFileReader>( decodedFile ),
                         rewrittenIndex );
}


const std::vector<std::tuple<std::string, std::string, std::string, std::string > > TEST_ENCODERS = {
    { "gzip", "gzip --version", "gzip -k --force", "gzip" },
    { "pigz", "pigz --version", "pigz -k --force", "pigz" },
    { "igzip", "igzip --version", "igzip -k --force", "igzip" },
    { "bgzip", "bgzip --version", "bgzip --force", "bgzip" },
    { "Python3 gzip", "python3 --version", "python3 -m gzip", "python3-gzip" },
    { "Python3 pgzip", "python3 -m pip show pgzip", "python3 -m pgzip", "python3-pgzip" },
};


[[nodiscard]] std::string
encodeTestFile( const std::string&           filePath,
                const std::filesystem::path& folder,
                const std::string&           command )
{
    /* Python3 module pgzip does not create the .gz file beside the input file but in the current directory,
     * so change current directory to the input file first. */
    const auto oldCWD = std::filesystem::current_path();
    std::filesystem::current_path( folder );

    /* Create backup of the uncompressed file because "bgzip" does not have a --keep option!
     * https://github.com/samtools/htslib/pull/1331 */
    const auto backupPath = std::filesystem::path( filePath ).filename().string() + ".bak";
    std::cerr << "Backup " << filePath << " -> " << backupPath << "\n";
    std::filesystem::copy( filePath, backupPath, std::filesystem::copy_options::overwrite_existing );

    const auto fullCommand = command + " " + filePath;
    const auto returnCode = std::system( fullCommand.c_str() );

    if ( !std::filesystem::exists( filePath ) ) {
        std::cerr << "Restore backup\n";
        std::filesystem::rename( backupPath, filePath );
    }

    std::filesystem::current_path( oldCWD );

    if ( returnCode != 0 ) {
        throw std::runtime_error( "Failed to encode the temporary file with: " + fullCommand );
    }

    if ( !std::filesystem::exists( filePath + ".gz" ) ) {
        throw std::runtime_error( "Encoded file was not found!" );
    }

    return filePath + ".gz";
}


void
createRandomBase64( const std::string& filePath,
                    const size_t       fileSize )
{
    constexpr std::string_view BASE64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz01234567890+/";
    std::ofstream file{ filePath };
    for ( size_t i = 0; i < fileSize; ++i ) {
        file << ( ( i + 1 == fileSize ) || ( ( i + 1 ) % 77 == 0 )
                  ? '\n' : BASE64[static_cast<size_t>( rand() ) % BASE64.size()] );
    }
}


void
testWithLargeFiles( const TemporaryDirectory& tmpFolder )
{
    const std::string fileName = std::filesystem::absolute( tmpFolder.path() / "random-base64" );
    createRandomBase64( fileName, 8_Mi );

    try {
        for ( const auto& [name, getVersion, command, extension] : TEST_ENCODERS ) {
            const auto encodedFilePath = encodeTestFile( fileName, tmpFolder, command );
            const auto newFileName = fileName + "." + extension;
            std::filesystem::rename( encodedFilePath, newFileName );

            std::cout << "=== Testing with encoder: " << name << " ===\n\n";

            std::cout << "> " << getVersion << "\n";
            [[maybe_unused]] const auto versionReturnCode = std::system( ( getVersion + " > out" ).c_str() );
            std::cout << std::ifstream( "out" ).rdbuf();
            std::cout << "\n";

            testParallelDecoder( newFileName );
        }
    } catch ( const std::exception& exception ) {
        /* Note that the destructor for TemporaryDirectory might not be called for uncaught exceptions!
         * @see https://stackoverflow.com/questions/222175/why-destructor-is-not-called-on-exception */
        std::cerr << "Caught exception: " << exception.what() << "\n";
        REQUIRE( false );
    }
}


void
testPerformance( const std::string& encodedFilePath,
                 const size_t       bufferSize,
                 const size_t       parallelization )
{
    pragzip::ParallelGzipReader<pragzip::ChunkData, /* ENABLE_STATISTICS */ true> reader(
        std::make_unique<StandardFileReader>( encodedFilePath ),
        parallelization );
    reader.setCRC32Enabled( true );

    std::vector<char> result( bufferSize );
    while ( true ) {
        const auto nBytesRead = reader.read( result.data(), result.size() );
        if ( nBytesRead == 0 ) {
            break;
        }
    }

    const auto statistics = reader.statistics();
    REQUIRE( statistics.blockCountFinalized );
    std::cerr << "statistics.blockCount:" << statistics.blockCount << ", statistics.prefetchCount:"
              << statistics.prefetchCount << ", statistics.onDemandFetchCount:" << statistics.onDemandFetchCount
              << "\n";
    REQUIRE_EQUAL( statistics.blockCount, statistics.prefetchCount + statistics.onDemandFetchCount );
}


void
testPerformance( const TemporaryDirectory& tmpFolder )
{
    const std::string fileName = std::filesystem::absolute( tmpFolder.path() / "random-base64" );
    createRandomBase64( fileName, 64_Mi );

    try {
        const auto& [name, getVersion, command, extension] = TEST_ENCODERS.front();
        const auto encodedFilePath = encodeTestFile( fileName, tmpFolder, command );

        for ( const auto parallelization : { 1, 2, 3, 4, 8 } ) {
            for ( const auto bufferSize : { 64_Mi, 4_Mi, 32_Ki, 1_Ki } ) {
                try {
                    testPerformance( encodedFilePath, bufferSize, parallelization );
                } catch ( const std::exception& exception ) {
                    std::cerr << "Caught exception: " << exception.what() << " while trying to test with a base64 "
                              << "example decompressed with " << parallelization << " threads and "
                              << formatBytes( bufferSize ) << " buffer.\n";
                    throw;
                }
            }
        }
    } catch ( const std::exception& exception ) {
        /* Note that the destructor for TemporaryDirectory might not be called for uncaught exceptions!
         * @see https://stackoverflow.com/questions/222175/why-destructor-is-not-called-on-exception */
        std::cerr << "Caught exception: " << exception.what() << "\n";
        REQUIRE( false );
    }
}


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
testParallelCRC32( const std::vector<std::byte>& uncompressed,
                   const std::vector<std::byte>& compressed )
{
    pragzip::ParallelGzipReader<pragzip::ChunkData, /* ENABLE_STATISTICS */ true> reader(
        std::make_unique<BufferViewFileReader>( compressed ), /* parallelization */ 2, /* chunk size */ 1_Mi );
    reader.setCRC32Enabled( true );

    /* Read everything. The data should contain sufficient chunks such that the first one have been evicted. */
    std::vector<std::byte> decompressed( uncompressed.size() );
    /* In the bugged version, which did not calcualte the CRC32 for data cleaned inside cleanUnmarkedData,
     * this call would throw an exception because CRC32 verification failed. */
    reader.read( -1, reinterpret_cast<char*>( decompressed.data() ), std::numeric_limits<size_t>::max() );
    REQUIRE( decompressed == uncompressed );

    /* Test with export and load without CRC32 */

    pragzip::ParallelGzipReader<pragzip::ChunkData, /* ENABLE_STATISTICS */ true> reader2(
        std::make_unique<BufferViewFileReader>( compressed ), /* parallelization */ 2, /* chunk size */ 1_Mi );
    reader2.setCRC32Enabled( false );
    reader2.setBlockOffsets( reader.gzipIndex() );

    std::fill( decompressed.begin(), decompressed.end(), std::byte( 0 ) );
    const auto nBytesRead = reader2.read( -1, reinterpret_cast<char*>( decompressed.data() ), decompressed.size() );

    REQUIRE_EQUAL( nBytesRead, decompressed.size() );
    REQUIRE( decompressed == uncompressed );
    std::cerr << "Decompressed correctly\n";

    /* Test with export and load */

    pragzip::ParallelGzipReader<pragzip::ChunkData, /* ENABLE_STATISTICS */ true> reader3(
        std::make_unique<BufferViewFileReader>( compressed ), /* parallelization */ 2, /* chunk size */ 1_Mi );
    reader3.setCRC32Enabled( true );
    reader3.setBlockOffsets( reader.gzipIndex() );

    reader3.read( -1, nullptr, std::numeric_limits<size_t>::max() );
}


void
testParallelCRC32MultiGzip( const std::vector<std::byte>& uncompressed,
                            const std::vector<std::byte>& compressed,
                            const size_t                  copyCount )
{
    std::vector<std::byte> multiStreamDataUncompressed;
    std::vector<std::byte> multiStreamDataCompressed;
    for ( size_t i = 0; i < copyCount; ++i ) {
        multiStreamDataUncompressed.insert( multiStreamDataUncompressed.end(),
                                            uncompressed.begin(), uncompressed.end() );
        multiStreamDataCompressed.insert( multiStreamDataCompressed.end(),
                                          compressed.begin(), compressed.end() );
    }
    testParallelCRC32( multiStreamDataUncompressed, multiStreamDataCompressed );
}


void
testCRC32AndCleanUnmarkedData( const std::vector<std::byte>& uncompressed,
                               const std::vector<std::byte>& compressed )
{
    testParallelCRC32( uncompressed, compressed );
    testParallelCRC32MultiGzip( uncompressed, compressed, 10 );
}


void
testCRC32AndCleanUnmarkedDataWithRandomDNA()
{
    /* As there are 4 symbols, 2 bits per symbol should suffice and as the data is random, almost no backreferences
     * should be viable. This leads to a compression ratio of ~4, which is large enough for splitting and benign
     * enough to have multiple chunks with fairly little uncompressed data. */
    constexpr auto UNCOMPRESSED_SIZE = 10_Mi;
    const auto randomDNA = createRandomData( UNCOMPRESSED_SIZE, DNA_SYMBOLS );
    const auto compressedRandomDNA = compressWithZlib( randomDNA, CompressionStrategy::HUFFMAN_ONLY );
    const auto compressionRatio = static_cast<double>( UNCOMPRESSED_SIZE )
                                  / static_cast<double>( compressedRandomDNA.size() );
    std::cerr << "Random DNA compression ratio: " << compressionRatio << "\n";  // 3.54874

    testCRC32AndCleanUnmarkedData( randomDNA, compressedRandomDNA );
}


void
testCRC32AndCleanUnmarkedDataWithRandomBackreferences()
{
    const auto t0 = now();

    std::mt19937_64 randomEngine;

    constexpr auto INITIAL_RANDOM_SIZE = pragzip::deflate::MAX_WINDOW_SIZE;
    auto randomData = createRandomData( INITIAL_RANDOM_SIZE, DNA_SYMBOLS );
    randomData.resize( 10_Mi );

    for ( size_t i = INITIAL_RANDOM_SIZE; i < randomData.size(); ) {
        const auto distance = randomEngine() % INITIAL_RANDOM_SIZE;
        const auto remainingSize = randomData.size() - i;
        const auto length = std::min( randomEngine() % 256, remainingSize );
        if ( ( length < 4 ) || ( length > distance ) ) {
            continue;
        }

        std::memcpy( randomData.data() + i, randomData.data() + ( i - distance ), length );
        i += length;
    }

    const auto creationDuration = duration( t0 );
    std::cout << "Created " << formatBytes( randomData.size() )
              << " data with random backreferences in " << creationDuration << " s\n";

    const auto compressed = compressWithZlib( randomData );

    testCRC32AndCleanUnmarkedData( randomData, compressed );
}


void
testCRC32AndCleanUnmarkedData()
{
    testCRC32AndCleanUnmarkedDataWithRandomDNA();
    testCRC32AndCleanUnmarkedDataWithRandomBackreferences();
}


void
testCachedChunkReuseAfterSplit()
{
    /* This compresses with a compression ratio of ~1028! I.e. even for 1 GiB, there will be only one chunk
     * even with a comparatively small chunk size of 1 MiB. */
    const auto compressedZeros = compressWithZlib( std::vector<std::byte>( 128_Mi, std::byte( 0 ) ) );
    pragzip::ParallelGzipReader<pragzip::ChunkData, /* ENABLE_STATISTICS */ true> reader(
        std::make_unique<BufferViewFileReader>( compressedZeros ), /* parallelization */ 8, /* chunk size */ 1_Mi );
    reader.setCRC32Enabled( true );

    /* As there is only one chunk, this read call will cache it.  */
    reader.read( -1, nullptr, 16_Mi );
    REQUIRE_EQUAL( reader.statistics().onDemandFetchCount, 1U );

    /* The chunk above will be split before inserting multiple smaller chunks into the BlockMap.
     * This tests whether the larger unsplit chunk, which still exists in the cache, is correctly reused
     * on the next access. */
    while ( true ) {
        const auto nBytesRead = reader.read( -1, nullptr, 1_Mi );
        REQUIRE_EQUAL( reader.statistics().onDemandFetchCount, 1U );
        if ( nBytesRead == 0 ) {
            break;
        }
    }
}


void
testPrefetchingAfterSplit()
{
    /* As there are 4 symbols, 2 bits per symbol should suffice and as the data is random, almost no backreferences
     * should be viable. This leads to a compression ratio of ~4, which is large enough for splitting and benign
     * enough to have multiple chunks with fairly little uncompressed data. */
    const auto compressedRandomDNA = compressWithZlib( createRandomData( 64_Mi, DNA_SYMBOLS ),
                                                       CompressionStrategy::HUFFMAN_ONLY );

    pragzip::ParallelGzipReader<pragzip::ChunkData, /* ENABLE_STATISTICS */ true> reader(
        std::make_unique<BufferViewFileReader>( compressedRandomDNA ), /* parallelization */ 2, /* chunk size */ 1_Mi );
    reader.setCRC32Enabled( true );

    /* Read everything. The data should contain sufficient chunks such that the first one have been evicted. */
    reader.read( -1, nullptr, std::numeric_limits<size_t>::max() );
    REQUIRE_EQUAL( reader.statistics().onDemandFetchCount, 1U );
    REQUIRE_EQUAL( reader.tell(), 64_Mi );
    REQUIRE_EQUAL( reader.tellCompressed(), compressedRandomDNA.size() * BYTE_SIZE );

    reader.seek( 0 );
    reader.read( -1, nullptr, std::numeric_limits<size_t>::max() );
    /* It might require two cache misses until the prefetcher recognizes it as a sequential access! */
    REQUIRE( reader.statistics().onDemandFetchCount <= 3U );

    /* Test with export and load */

    pragzip::ParallelGzipReader<pragzip::ChunkData, /* ENABLE_STATISTICS */ true> reader2(
        std::make_unique<BufferViewFileReader>( compressedRandomDNA ), /* parallelization */ 2, /* chunk size */ 1_Mi );
    reader2.setCRC32Enabled( true );
    reader2.setBlockOffsets( reader.gzipIndex() );
    std::cerr << "File was split into " << reader.blockOffsets().size() - 1 << " chunks\n";

    reader2.read( -1, nullptr, std::numeric_limits<size_t>::max() );
    REQUIRE_EQUAL( reader2.statistics().onDemandFetchCount, 1U );

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

    testCRC32AndCleanUnmarkedData();
    testPrefetchingAfterSplit();
    testCachedChunkReuseAfterSplit();

    const auto tmpFolder = createTemporaryDirectory( "pragzip.testParallelGzipReader" );

    testPerformance( tmpFolder );

    testParallelDecoderNano();

    using namespace std::string_literals;

    for ( const auto& extension : { ".gz"s, ".bgz"s, ".igz"s, ".pgz"s } ) {
        testParallelDecoder( rootFolder / ( "empty" + extension ) );
        testParallelDecoder( rootFolder / ( "1B" + extension ) );
        testParallelDecoder( rootFolder / ( "256B-extended-ASCII-table-in-utf8-dynamic-Huffman" + extension ) );
        testParallelDecoder( rootFolder / ( "256B-extended-ASCII-table-uncompressed" + extension ) );
        testParallelDecoder( rootFolder / ( "32A-fixed-Huffman" + extension ) );
        testParallelDecoder( rootFolder / ( "base64-32KiB" + extension ) );
        testParallelDecoder( rootFolder / ( "base64-256KiB" + extension ) );
        testParallelDecoder( rootFolder / ( "dolorem-ipsum.txt" + extension ) );
        testParallelDecoder( rootFolder / ( "numbers-10,65-90" + extension ) );
        testParallelDecoder( rootFolder / ( "random-128KiB" + extension ) );
        testParallelDecoder( rootFolder / ( "zeros" + extension ) );
    }

    testParallelDecoder( rootFolder / "base64-256KiB.gz",
                         rootFolder / "base64-256KiB",
                         rootFolder / "base64-256KiB.gz.index" );

    /**
     * @todo add test with false pigz positive, e.g., pigz marker inside comment, extra, or file name field.
     * @todo add test with valid empty pigz block. E.g., by concatenating empty.pgz. This might trip up
     *       ParallelGzipReader making it impossible to advance. Maybe use the EOS handling in the BlockFinder to filter
     *       these empty blocks? Maybe also skip empty deflate blocks inside PigzBlockFinder. BZ2 also never finds
     *       (empty) EOS blocks.
     * @todo Add test for bz2 with such an empty block! Will it lock up?!
     */

    try
    {
        testParallelDecodingWithIndex( tmpFolder );
    } catch ( const std::exception& exception ) {
        /* Note that the destructor for TemporaryDirectory might not be called for uncaught exceptions!
         * @see https://stackoverflow.com/questions/222175/why-destructor-is-not-called-on-exception */
        std::cerr << "Caught exception: " << exception.what() << "\n";
        REQUIRE( false );
    }

    testWithLargeFiles( tmpFolder );

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
