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
#include <DataGenerators.hpp>
#include <ParallelGzipReader.hpp>
#include <rapidgzip.hpp>
#include <TestHelpers.hpp>
#include <zlib.hpp>


using namespace rapidgzip;


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
                     size_t                   nBlocksToSkip = 1,
                     bool                     readInChunks = false )
{
    /* Test a simple full read. */

    ParallelGzipReader reader( std::move( encoded ), /* parallelization */ 0, nBlocksToSkip * 32_Ki );
    reader.setCRC32Enabled( true );
    if ( index ) {
        reader.setBlockOffsets( std::move( *index ) );
        REQUIRE( reader.blockOffsetsComplete() );
    }

    const auto decodedSize = decoded->size().value();

    std::vector<char> result( decodedSize * 2 );
    size_t nBytesRead{ 0 };
    if ( readInChunks ) {
        static constexpr size_t CHUNK_SIZE = 4_Ki;
        while ( nBytesRead <= result.size() ) {
            const auto nBytesReadPerCall =
                reader.read( result.data() + nBytesRead,
                             std::clamp( result.size() - nBytesRead, size_t( 1 ), CHUNK_SIZE ) );
            if ( nBytesReadPerCall == 0 ) {
                break;
            }
            nBytesRead += nBytesReadPerCall;
        }
    } else {
        nBytesRead = reader.read( result.data(), std::max( size_t( 1 ), result.size() ) );
    }
    REQUIRE( nBytesRead == decodedSize );
    result.resize( nBytesRead );
    REQUIRE( reader.eof() );

    std::vector<char> decodedBuffer( decodedSize );
    const auto nDecodedBytesRead = decoded->read( decodedBuffer.data(), decodedBuffer.size() );
    REQUIRE( nDecodedBytesRead == decodedBuffer.size() );
    REQUIRE( result == decodedBuffer );

    if ( result != decodedBuffer ) {
        for ( size_t i = 0; i < result.size(); ++i ) {
            if ( result[i] != decodedBuffer[i] ) {
                std::cerr << "Decoded contents differ at position " << i << " B out of " << decodedSize << " B: "
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
    auto decodedFilePath = decoded;
    if ( decodedFilePath.empty() ) {
        decodedFilePath = encoded;
        decodedFilePath.replace_extension();
    }

    const std::vector<size_t> blocksToSkip = { 0, 1, 2, 4, 8, 16, 24, 32, 64, 128 };

    std::cerr << "Testing " << encoded.filename() << " without index ("
              << std::filesystem::file_size( encoded ) << " B)\n";
    for ( const size_t nBlocksToSkip : blocksToSkip ) {
        testParallelDecoder( std::make_unique<StandardFileReader>( encoded.string() ),
                             std::make_unique<StandardFileReader>( decodedFilePath.string() ),
                             std::nullopt,
                             nBlocksToSkip );
    }

    if ( std::filesystem::is_regular_file( index ) ) {
        std::cerr << "Testing " << encoded.filename() << " with given index ("
                  << std::filesystem::file_size( encoded ) << " B)\n";
        const auto givenIndexData = readGzipIndex( std::make_unique<StandardFileReader>( index.string() ),
                                                   std::make_unique<StandardFileReader>( encoded.string() ) );
        for ( const size_t nBlocksToSkip : blocksToSkip ) {
            testParallelDecoder( std::make_unique<StandardFileReader>( encoded.string() ),
                                 std::make_unique<StandardFileReader>( decodedFilePath.string() ),
                                 givenIndexData.clone(),
                                 nBlocksToSkip,
                                 /* readInChunks */ true );
        }
        for ( const size_t nBlocksToSkip : blocksToSkip ) {
            testParallelDecoder( std::make_unique<StandardFileReader>( encoded.string() ),
                                 std::make_unique<StandardFileReader>( decodedFilePath.string() ),
                                 givenIndexData.clone(),
                                 nBlocksToSkip );
        }
    }

    /* Create index if not given. */
    {
        std::cerr << "Testing " << encoded.filename() << " with generated index ("
                  << std::filesystem::file_size( encoded ) << " B)\n";
        const auto generatedIndex =
            [&] {
                ParallelGzipReader reader( std::make_unique<StandardFileReader>( encoded.string() ) );
                return reader.gzipIndex();
            } ();
        for ( const size_t nBlocksToSkip : blocksToSkip ) {
            testParallelDecoder( std::make_unique<StandardFileReader>( encoded.string() ),
                                 std::make_unique<StandardFileReader>( decodedFilePath.string() ),
                                 generatedIndex.clone(),
                                 nBlocksToSkip );
        }
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
    const auto decodedFile = ( tmpFolder.path() / "decoded" ).string();
    const auto encodedFile = ( tmpFolder.path() / "decoded.gz" ).string();
    const auto indexFile = ( tmpFolder.path() / "decoded.gz.index" ).string();
    createRandomTextFile( decodedFile, 64_Ki );

    {
        const auto command = "gzip -k " + decodedFile;
        const auto returnCode = std::system( command.c_str() );
        REQUIRE( returnCode == 0 );
        if ( returnCode != 0 ) {
            return;
        }
    }

    {
        const auto command =
            R"(python3 -c 'import indexed_gzip as ig; f = ig.IndexedGzipFile( ")"
            + encodedFile
            + R"(" ); f.build_full_index(); f.export_index( ")"
            + indexFile
            + R"(" );')";
        const auto returnCode = std::system( command.c_str() );
        REQUIRE( returnCode == 0 );
        if ( returnCode != 0 ) {
            return;
        }
    }

    std::cerr << "Test parallel decoder with larger gz file given an indexed_gzip index.\n";
    const auto realIndex = readGzipIndex( std::make_unique<StandardFileReader>( indexFile ) );
    for ( const size_t nBlocksToSkip : { 0, 1, 2, 4, 8, 16, 24, 32, 64, 128 } ) {
        testParallelDecoder( std::make_unique<StandardFileReader>( encodedFile ),
                             std::make_unique<StandardFileReader>( decodedFile ),
                             realIndex.clone(), nBlocksToSkip );
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

    if ( !realIndex.windows ) {
        throw std::logic_error( "Real index window map is not set!" );
    }
    if ( !reconstructedIndex.windows ) {
        throw std::logic_error( "Reconstructed index window map is not set!" );
    }

    if ( reconstructedIndex.checkpoints.size() == realIndex.checkpoints.size() ) {
        for ( size_t i = 0; i < reconstructedIndex.checkpoints.size(); ++i ) {
            const auto& reconstructed = reconstructedIndex.checkpoints[i];
            const auto& real = realIndex.checkpoints[i];
            REQUIRE_EQUAL( reconstructed.compressedOffsetInBits, real.compressedOffsetInBits );
            REQUIRE_EQUAL( reconstructed.uncompressedOffsetInBytes, real.uncompressedOffsetInBytes );

            const auto reconstructedWindow = reconstructedIndex.windows->get( reconstructed.compressedOffsetInBits );
            const auto realWindow = realIndex.windows->get( real.compressedOffsetInBits );
            REQUIRE( static_cast<bool>( reconstructedWindow ) );
            REQUIRE( static_cast<bool>( realWindow ) );
        }
    }
    REQUIRE( *reconstructedIndex.windows == *realIndex.windows );

    testParallelDecoder( std::make_unique<StandardFileReader>( encodedFile ),
                         std::make_unique<StandardFileReader>( decodedFile ),
                         reconstructedIndex.clone() );

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
        indexed_gzip::writeGzipIndex( realIndex, checkedWrite );
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
                         rewrittenIndex.clone() );
}


const std::vector<std::tuple<std::string, std::string, std::string, std::string > > TEST_ENCODERS = {
    /* [name, getVersion, command, extension] */
    { "gzip", "gzip --version", "gzip -k --force", "gzip" },
    { "pigz", "pigz --version", "pigz -k --force", "pigz" },
    { "pigz zlib", "pigz --version", "pigz -k --force --zlib", "zlib" },
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
    std::filesystem::copy( filePath, backupPath, std::filesystem::copy_options::overwrite_existing );

    const auto fullCommand = command + " " + filePath;
    const auto returnCode = std::system( fullCommand.c_str() );

    if ( !std::filesystem::exists( filePath ) ) {
        std::filesystem::rename( backupPath, filePath );
    }

    std::filesystem::current_path( oldCWD );

    if ( returnCode != 0 ) {
        throw std::runtime_error( "Failed to encode the temporary file with: " + fullCommand );
    }

    if ( std::filesystem::exists( filePath + ".gz" ) ) {
        return filePath + ".gz";
    }

    if ( ( command.find( " --zlib" ) != std::string::npos ) && std::filesystem::exists( filePath + ".zz" ) ) {
        return filePath + ".zz";
    }

    throw std::runtime_error( "Encoded file was not found!" );
}


void
testWithLargeFiles( const TemporaryDirectory& tmpFolder )
{
    std::vector<std::string> filePaths;

    filePaths.emplace_back( std::filesystem::absolute( tmpFolder.path() / "random-base64" ).string() );
    createRandomBase64( filePaths.back(), 8_Mi );

#ifndef SHORT_TESTS
    filePaths.emplace_back( std::filesystem::absolute( tmpFolder.path() / "random-numbers" ).string() );
    createRandomNumbers( filePaths.back(), 32_Mi );

    filePaths.emplace_back( std::filesystem::absolute( tmpFolder.path() / "random" ).string() );
    createRandom( filePaths.back(), 8_Mi );

    filePaths.emplace_back( std::filesystem::absolute( tmpFolder.path() / "zeros" ).string() );
    createZeros( filePaths.back(), 32_Mi );

    /* This test case triggers the exception thrown when trying to decode bgzip files with an index created
     * containing seek points inside gzip streams instead of at gzip stream boundaries. This happened because
     * the BGZF handling, as a special case, always assumed that no windows need to be known. Which is only
     * true if the seek points are always on stream boundaries, though.
     * > Decoding failed with error code -3 invalid distance too far back! Already decoded 0 B. */
    filePaths.emplace_back( std::filesystem::absolute( tmpFolder.path() / "random-words" ).string() );
    createRandomWords( filePaths.back(), 32_Mi );
#endif

    try {
        for ( const auto& [name, getVersion, command, extension] : TEST_ENCODERS ) {
            std::cout << "=== Get version for encoder: " << name << " ===\n\n";
            std::cout << "> " << getVersion << "\n";
            [[maybe_unused]] const auto versionReturnCode = std::system( ( getVersion + " > out" ).c_str() );
            std::cout << std::ifstream( "out" ).rdbuf();
            std::cout << "\n";
        }

        for ( const auto& fileName : filePaths ) {
            for ( const auto& [name, getVersion, command, extension] : TEST_ENCODERS ) {
                const auto encodedFilePath = encodeTestFile( fileName, tmpFolder, command );
                const auto newFileName = fileName + "." + extension;
                std::filesystem::rename( encodedFilePath, newFileName );

                std::cout << "=== Testing " << fileName << " with encoder: " << name << " ===\n\n";

                testParallelDecoder( newFileName );
            }
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
    rapidgzip::ParallelGzipReader<rapidgzip::ChunkData> reader(
        std::make_unique<StandardFileReader>( encodedFilePath ),
        parallelization );
    reader.setStatisticsEnabled( true );
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
              << ", parallelization: " << parallelization << "\n";

    if ( parallelization == 1 ) {
        REQUIRE_EQUAL( statistics.prefetchCount, 0U );
    } else {
        REQUIRE_EQUAL( statistics.onDemandFetchCount, 1U );
    }
    /* The block count can be larger if chunks were split. */
    REQUIRE( statistics.blockCount >= statistics.prefetchCount + statistics.onDemandFetchCount );
}


void
testPerformance( const TemporaryDirectory& tmpFolder )
{
    const auto fileName = std::filesystem::absolute( tmpFolder.path() / "random-base64" ).string();
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
    rapidgzip::ParallelGzipReader<rapidgzip::ChunkData> reader(
        std::make_unique<BufferViewFileReader>( compressed ), /* parallelization */ 2, /* chunk size */ 1_Mi );
    reader.setStatisticsEnabled( true );
    reader.setCRC32Enabled( true );

    /* Read everything. The data should contain sufficient chunks such that the first ones have been evicted. */
    std::vector<std::byte> decompressed( uncompressed.size() );
    /* In the bugged version, which did not calcualte the CRC32 for data cleaned inside cleanUnmarkedData,
     * this call would throw an exception because CRC32 verification failed. */
    reader.read( -1, reinterpret_cast<char*>( decompressed.data() ), std::numeric_limits<size_t>::max() );
    REQUIRE( decompressed == uncompressed );

    /* Test with export and load without CRC32 */

    rapidgzip::ParallelGzipReader<rapidgzip::ChunkData> reader2(
        std::make_unique<BufferViewFileReader>( compressed ), /* parallelization */ 2, /* chunk size */ 1_Mi );
    reader2.setStatisticsEnabled( true );
    reader2.setCRC32Enabled( false );
    reader2.setBlockOffsets( reader.gzipIndex() );

    std::fill( decompressed.begin(), decompressed.end(), std::byte( 0 ) );
    const auto nBytesRead = reader2.read( -1, reinterpret_cast<char*>( decompressed.data() ), decompressed.size() );

    REQUIRE_EQUAL( nBytesRead, decompressed.size() );
    REQUIRE( decompressed == uncompressed );

    /* Test with export and load with CRC32 */

    rapidgzip::ParallelGzipReader<rapidgzip::ChunkData> reader3(
        std::make_unique<BufferViewFileReader>( compressed ), /* parallelization */ 2, /* chunk size */ 1_Mi );
    reader3.setStatisticsEnabled( true );
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
    const auto compressedRandomDNA =
        compressWithZlib<std::vector<std::byte> >( randomDNA, CompressionStrategy::HUFFMAN_ONLY );
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

    constexpr auto INITIAL_RANDOM_SIZE = rapidgzip::deflate::MAX_WINDOW_SIZE;
    auto randomData = createRandomData( INITIAL_RANDOM_SIZE, DNA_SYMBOLS );
    randomData.resize( 10_Mi );

    for ( size_t i = INITIAL_RANDOM_SIZE; i < randomData.size(); ) {
        const auto distance = randomEngine() % INITIAL_RANDOM_SIZE;
        const auto remainingSize = randomData.size() - i;
        const auto length = std::min<size_t>( randomEngine() % 256, remainingSize );
        if ( ( length < 4 ) || ( length > distance ) ) {
            continue;
        }

        std::memcpy( randomData.data() + i, randomData.data() + ( i - distance ), length );
        i += length;
    }

    const auto creationDuration = duration( t0 );
    std::cout << "Created " << formatBytes( randomData.size() )
              << " data with random backreferences in " << creationDuration << " s\n";

    const auto compressed = compressWithZlib<std::vector<std::byte> >( randomData );

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
    rapidgzip::ParallelGzipReader<rapidgzip::ChunkData> reader(
        std::make_unique<BufferViewFileReader>( compressedZeros ), /* parallelization */ 8, /* chunk size */ 1_Mi );
    reader.setStatisticsEnabled( true );
    reader.setCRC32Enabled( true );
    reader.setMaxDecompressedChunkSize( 128_Mi );

    /* As there is only one chunk, this read call will cache it. */
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
    static constexpr size_t DATA_SIZE = 64_Mi;
    static constexpr size_t CHUNK_SIZE = 1_Mi;

    /* As there are 4 symbols, 2 bits per symbol should suffice and as the data is random, almost no backreferences
     * should be viable. This leads to a compression ratio of ~4, which is large enough for splitting and benign
     * enough to have multiple chunks with fairly little uncompressed data. */
    const auto compressedRandomDNA = compressWithZlib( createRandomData( DATA_SIZE, DNA_SYMBOLS ),
                                                       CompressionStrategy::HUFFMAN_ONLY );

    rapidgzip::ParallelGzipReader<rapidgzip::ChunkData> reader(
        std::make_unique<BufferViewFileReader>( compressedRandomDNA ), /* parallelization */ 2, CHUNK_SIZE );
    reader.setStatisticsEnabled( true );
    reader.setCRC32Enabled( true );

    /* Read everything. The data should contain sufficient chunks such that the first ones have been evicted. */
    reader.read( -1, nullptr, std::numeric_limits<size_t>::max() );
    REQUIRE_EQUAL( reader.statistics().onDemandFetchCount, 1U );
    REQUIRE_EQUAL( reader.tell(), 64_Mi );
    REQUIRE_EQUAL( reader.tellCompressed(), compressedRandomDNA.size() * BYTE_SIZE );
    REQUIRE( reader.blockOffsets().size() >= DATA_SIZE / CHUNK_SIZE );

    reader.seek( 0 );
    reader.read( -1, nullptr, std::numeric_limits<size_t>::max() );
    /* It might require two cache misses until the prefetcher recognizes it as a sequential access! */
    REQUIRE( reader.statistics().onDemandFetchCount <= 3U );

    /* Test with export and load */

    rapidgzip::ParallelGzipReader<rapidgzip::ChunkData> reader2(
        std::make_unique<BufferViewFileReader>( compressedRandomDNA ), /* parallelization */ 2, /* chunk size */ 1_Mi );
    reader2.setStatisticsEnabled( true );
    reader2.setCRC32Enabled( true );
    reader2.setBlockOffsets( reader.gzipIndex() );
    std::cerr << "File was split into " << reader.blockOffsets().size() - 1 << " chunks\n";  // 70, subject to change

    reader2.read( -1, nullptr, std::numeric_limits<size_t>::max() );
    REQUIRE_EQUAL( reader2.statistics().onDemandFetchCount, 1U );
}


void
testMultiThreadedUsage()
{
    static constexpr size_t DATA_SIZE = 64_Mi;

    /* As there are 4 symbols, 2 bits per symbol should suffice and as the data is random, almost no backreferences
     * should be viable. This leads to a compression ratio of ~4, which is large enough for splitting and benign
     * enough to have multiple chunks with fairly little uncompressed data. */
    const auto compressedRandomDNA = compressWithZlib( createRandomData( DATA_SIZE, DNA_SYMBOLS ),
                                                       CompressionStrategy::HUFFMAN_ONLY );

    auto reader = std::make_unique<rapidgzip::ParallelGzipReader<rapidgzip::ChunkData> >(
        std::make_unique<BufferViewFileReader>( compressedRandomDNA ),
        /* parallelization */ 6 );
    reader->setStatisticsEnabled( true );
    reader->setCRC32Enabled( true );

    std::vector<char> result;
    std::thread thread( [&result, gzipReader = std::move( reader )] () {
        std::vector<char> buffer( 1024ULL );
        while ( true ) {
            const auto nBytesRead = gzipReader->read( buffer.data(), buffer.size() );
            if ( nBytesRead == 0 ) {
                break;
            }
            result.insert( result.end(), buffer.begin(), buffer.begin() + nBytesRead );
        }
    } );

    thread.join();
    REQUIRE_EQUAL( result.size(), DATA_SIZE );
}


void
testIndexCreation( const std::filesystem::path&    encoded,
                   const std::map<size_t, size_t>& expectedBlockOffsets )
{
    std::cerr << "Testing index for " << encoded.filename() << "\n";
    ParallelGzipReader reader( std::make_unique<StandardFileReader>( encoded.string() ) );
    if ( reader.blockOffsets() != expectedBlockOffsets ) {
        std::cerr << "reader.blockOffsets: {";
        for ( const auto& [encodedOffset, decodedOffset] : reader.blockOffsets() ) {
            std::cerr << " {" << encodedOffset << "," << decodedOffset << "},";
        }
        std::cerr << "}\n";

        std::cerr << "expectedBlockOffsets: {";
        for ( const auto& [encodedOffset, decodedOffset] : expectedBlockOffsets ) {
            std::cerr << " {" << encodedOffset << "," << decodedOffset << "},";
        }
        std::cerr << "}\n";
    }
    REQUIRE( reader.blockOffsets() == expectedBlockOffsets );
}


template<typename Container>
[[nodiscard]] Container
duplicateContents( Container&& data,
                   size_t      count )
{
    const auto oldSize = data.size();
    data.resize( count * oldSize );
    for ( size_t i = 1; i < count; ++i ) {
        std::copy( data.begin(), data.begin() + oldSize,
                   data.begin() + i * oldSize );
    }
    return std::move( data );
}


void
testMultiStreamDecompression( const std::filesystem::path& encoded,
                              const std::filesystem::path& decoded )
{
    auto compressedData = readFile<std::vector<uint8_t> >( encoded.string() );
    auto decompressedData = readFile<std::vector<uint8_t> >( decoded.string() );

    /* Duplicate gzip stream. We need something larger than the chunk size at least. */
    const auto duplicationCount = ceilDiv( 32_Mi, compressedData.size() );
    compressedData = duplicateContents( std::move( compressedData ), duplicationCount );
    decompressedData = duplicateContents( std::move( decompressedData ), duplicationCount );

    std::cerr << "Test " << duplicationCount << " duplicated streams of " << encoded.filename() << " for a total of "
              << formatBytes( compressedData.size() ) << " decompressing to " << formatBytes( decompressedData.size() )
              << "\n";

    std::vector<uint8_t> decompressedResult( decompressedData.size() + 1, 3 );
    ParallelGzipReader reader( std::make_unique<BufferViewFileReader>( compressedData ) );
    const auto readSize = reader.read( reinterpret_cast<char*>( decompressedResult.data() ),
                                       decompressedResult.size() );
    REQUIRE_EQUAL( readSize, decompressedData.size() );
    decompressedResult.resize( decompressedData.size() );
    REQUIRE( decompressedResult == decompressedData );
}


void
testChecksummedMultiStreamDecompression( const std::filesystem::path& encoded,
                                         const std::filesystem::path& decoded )
{
    auto compressedData = readFile<std::vector<uint8_t> >( encoded );
    auto decompressedData = readFile<std::vector<uint8_t> >( decoded );

    const auto singleStreamSize = compressedData.size();
    CRC32Calculator checksummer;
    checksummer.update( decompressedData.data(), decompressedData.size() );

    /* Duplicate gzip stream. We need something larger than the chunk size at least. */
    const auto duplicationCount = ceilDiv( 32_Mi, compressedData.size() );
    compressedData = duplicateContents( std::move( compressedData ), duplicationCount );
    decompressedData = duplicateContents( std::move( decompressedData ), duplicationCount );

    std::cerr << "Test " << duplicationCount << " duplicated streams of " << encoded.filename() << " for a total of "
              << formatBytes( compressedData.size() ) << " decompressing to " << formatBytes( decompressedData.size() )
              << "\n";

    std::unordered_map<size_t, uint32_t> crc32s;
    crc32s.reserve( duplicationCount );
    for ( size_t i = 0; i < duplicationCount; ++i ) {
        crc32s.emplace( i * singleStreamSize, checksummer.crc32() );
    }

    std::unique_ptr<GzipIndex> index;
    /* Test without index. */
    {
        std::vector<uint8_t> decompressedResult( decompressedData.size() + 1, 3 );
        ParallelGzipReader reader( std::make_unique<BufferViewFileReader>( compressedData ) );
        reader.setCRC32Enabled( true );
        reader.setDeflateStreamCRC32s( std::move( crc32s ) );

        const auto readSize = reader.read( reinterpret_cast<char*>( decompressedResult.data() ),
                                           decompressedResult.size() );
        REQUIRE_EQUAL( readSize, decompressedData.size() );
        decompressedResult.resize( decompressedData.size() );
        REQUIRE( decompressedResult == decompressedData );

        index = std::make_unique<GzipIndex>( reader.gzipIndex().clone() );
    }

    /* Test with index. */
    {
        std::vector<uint8_t> decompressedResult( decompressedData.size() + 1, 3 );
        ParallelGzipReader reader( std::make_unique<BufferViewFileReader>( compressedData ) );
        reader.setCRC32Enabled( true );
        reader.setDeflateStreamCRC32s( std::move( crc32s ) );
        reader.setBlockOffsets( *index );

        const auto readSize = reader.read( reinterpret_cast<char*>( decompressedResult.data() ),
                                           decompressedResult.size() );
        REQUIRE_EQUAL( readSize, decompressedData.size() );
        decompressedResult.resize( decompressedData.size() );
        REQUIRE( decompressedResult == decompressedData );
    }
}


void
testWindowPruningSimpleBase64Compression( const TemporaryDirectory& tmpFolder,
                                          const std::string&        command )
{
    const auto filePath = std::filesystem::absolute( tmpFolder.path() / "random-base64" ).string();
    createRandomBase64( filePath, 1_Mi );
    const auto compressedFilePath = encodeTestFile( filePath, tmpFolder, command + " --force" );
    const auto compressedFileSize = fileSize( compressedFilePath );

    {
        ParallelGzipReader reader( std::make_unique<StandardFileReader>( compressedFilePath ), 0, 256_Ki );
        const auto index = reader.gzipIndex();

        REQUIRE( index.checkpoints.size() > 2 );
        REQUIRE( static_cast<bool>( index.windows ) );
        if ( index.windows ) {
            REQUIRE_EQUAL( index.windows->size(), index.checkpoints.size() );
            for ( const auto& checkpoint : index.checkpoints ) {
                const auto window = index.windows->get( checkpoint.compressedOffsetInBits );
                if ( ( checkpoint.compressedOffsetInBits < 64 * BYTE_SIZE /* guess for the gzip header size */ )
                     || ( checkpoint.compressedOffsetInBits == compressedFileSize * BYTE_SIZE )
                     || ( command == "bgzip" ) )
                {
                    REQUIRE( !window || window->empty() );
                } else {
                    REQUIRE( window && !window->empty() );
                }
            }
        }
    }

    std::filesystem::remove( filePath );
    std::filesystem::remove( compressedFilePath );
}


void
testWindowPruning( const TemporaryDirectory& tmpFolder )
{
    testWindowPruningSimpleBase64Compression( tmpFolder, "gzip" );
    testWindowPruningSimpleBase64Compression( tmpFolder, "bgzip" );
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

    testMultiThreadedUsage();
    testCRC32AndCleanUnmarkedData();
    testPrefetchingAfterSplit();
    testCachedChunkReuseAfterSplit();

    const auto tmpFolder = createTemporaryDirectory( "rapidgzip.testParallelGzipReader" );

    testWindowPruning( tmpFolder );

    testPerformance( tmpFolder );

    testParallelDecoderNano();

    /* The second and last encoded offset should always be at the end of the file, i.e., equal the file size in bits. */
    testIndexCreation( rootFolder / "1B.bgz", { { 18 * 8, 0 }, { 60 * 8, 1 } } );
    testIndexCreation( rootFolder / "1B.deflate", { { 0, 0 }, { 3 * 8, 1 } } );
    testIndexCreation( rootFolder / "1B.gz", { { 13 * 8, 0 }, { 24 * 8, 1 } } );
    testIndexCreation( rootFolder / "1B.igz", { { 13 * 8, 0 }, { 24 * 8, 1 } } );
    testIndexCreation( rootFolder / "1B.migz", { { 20 * 8, 0 }, { 31 * 8, 1 } } );
    testIndexCreation( rootFolder / "1B.pgzf", { { 32 * 8, 0 }, { 85 * 8, 1 } } );
    testIndexCreation( rootFolder / "1B.pigz", { { 13 * 8, 0 }, { 24 * 8, 1 } } );
    testIndexCreation( rootFolder / "1B.zlib", { { 2 * 8, 0 }, { 9 * 8, 1 } } );

    using namespace std::string_literals;

    testChecksummedMultiStreamDecompression( rootFolder / "base64-32KiB.deflate",
                                             rootFolder / "base64-32KiB" );

    for ( const auto& extension : { ".gz"s, ".bgz"s, ".igz"s, ".pigz"s, ".zlib"s, ".deflate"s } ) {
        testMultiStreamDecompression( rootFolder / ( "base64-32KiB" + extension ),
                                      rootFolder / "base64-32KiB" );
    }

    for ( const auto& extension : { ".gz"s, ".bgz"s, ".igz"s, ".pigz"s, ".zlib"s, ".deflate"s } ) {
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

    for ( const auto* const indexSuffix : { ".index", ".gztool.index", ".gztool.with-lines.index" } ) {
        testParallelDecoder( rootFolder / "base64-256KiB.gz",
                             rootFolder / "base64-256KiB",
                             rootFolder / ( "base64-256KiB.gz"s + indexSuffix ) );
    }

    testParallelDecoder( rootFolder / "base64-256KiB.bgz",
                         rootFolder / "base64-256KiB",
                         rootFolder / "base64-256KiB.bgz.gzi" );

    /**
     * @todo add test with false pigz positive, e.g., pigz marker inside comment, extra, or file name field.
     * @todo add test with valid empty pigz block. E.g., by concatenating empty.pigz. This might trip up
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
