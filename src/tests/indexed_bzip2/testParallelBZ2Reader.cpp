#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <BitStringFinder.hpp>
#include <common.hpp>
#include <DataGenerators.hpp>
#include <filereader/Standard.hpp>
#include <FileUtils.hpp>
#include <ParallelBZ2Reader.hpp>
#include <TestHelpers.hpp>


using namespace rapidgzip;
using namespace indexed_bzip2;


void
testSimpleOpenAndClose( const std::string& bz2File )
{
    const auto t0 = std::chrono::high_resolution_clock::now();
    {
        ParallelBZ2Reader encodedFile( std::make_unique<StandardFileReader>( bz2File ) );
        const auto t1 = std::chrono::high_resolution_clock::now();
        const auto dt = std::chrono::duration_cast<std::chrono::duration<double> >( t1 - t0 ).count();
        REQUIRE( dt < 1 );
    }
    const auto t1 = std::chrono::high_resolution_clock::now();
    const auto dt = std::chrono::duration_cast<std::chrono::duration<double> >( t1 - t0 ).count();
    REQUIRE( dt < 1 );
}


/**
 * Tests are in such a way that seeking and reading are mirrored on the ParallelBZ2Reader file and the decoded file.
 * Then all read results can be checked against each other. Same for the result of tell.
 */
void
testDecodingBz2ForFirstTime( const std::string& decodedTestFilePath,
                             const std::string& encodedTestFilePath )
{
    size_t decodedFileSize = 0;
    {
        std::ifstream file( decodedTestFilePath );
        file.seekg( 0, std::ios::end );
        decodedFileSize = file.tellg();
    }
    std::cerr << "Decoded file size: " << decodedFileSize << "\n";

    std::ifstream decodedFile( decodedTestFilePath );
    auto encodedFile =
        std::make_unique<ParallelBZ2Reader>( std::make_unique<StandardFileReader>( encodedTestFilePath ) );

    const auto seek =
        [&] ( long long int offset,
              int           origin = SEEK_SET )
        {
            std::cerr << "Seek to " << offset << "\n";

            /**
             * Clear fail bit in order to seek back.
             * When using read to read the number of bytes the file has, then no eof bit is set.
             * As soon as you request one more byte than the file contains, both, the failbit and eofbit are set
             * but only the eofbit will be cleared by seekg since C++11.
             * @see https://en.cppreference.com/w/cpp/io/basic_istream/seekg
             * > Before doing anything else, seekg clears eofbit. (since C++11)
             */
            if ( decodedFile.fail() ) {
                decodedFile.clear();
            }
            decodedFile.seekg( offset, toSeekdir( origin ) );
            const auto newSeekPosDecoded = static_cast<long long int>( decodedFile.tellg() );
            const auto newSeekPosEncoded = static_cast<long long int>( encodedFile->seek( offset, origin ) );

            /* Wanted differing behavior between std::ifstream and BZ2Reader. */
            REQUIRE_EQUAL( std::min<long long int>( newSeekPosDecoded, decodedFileSize ), newSeekPosEncoded );
            REQUIRE_EQUAL( std::min<long long int>( decodedFile.tellg(), decodedFileSize ),
                           static_cast<long long int>( encodedFile->tell() ) );

            /* Beware! eof behavior is different. std:ifstream requires to read more than the file contents
             * for EOF to be reached while BZ2Reader only required to read more than >or equal< the file
             * size of bytes. Furthermore, seeking beyond the file does not set EOF in std::ifstream but does
             * set EOF in BZ2Reader! */
            //REQUIRE_EQUAL( decodedFile.eof(), encodedFile->eof() );
        };

    const auto read =
        [&] ( size_t nBytesToRead )
        {
            std::cerr << "Read " << nBytesToRead << "B\n";

            std::vector<char> decodedBuffer( nBytesToRead, 11 );
            std::vector<char> encodedBuffer( nBytesToRead, 22 );

            if ( !encodedFile->eof() ) {
                REQUIRE_EQUAL( static_cast<size_t>( decodedFile.tellg() ), encodedFile->tell() );
            }

            /* Why doesn't the ifstream has a similar return specifying the number of read bytes?! */
            decodedFile.read( decodedBuffer.data(), static_cast<std::streamsize>( nBytesToRead ) );
            const auto nBytesReadDecoded = decodedFile.gcount();

            const auto nBytesReadEncoded = encodedFile->read( -1, encodedBuffer.data(), nBytesToRead );

            REQUIRE_EQUAL( static_cast<size_t>( nBytesReadDecoded ), nBytesReadEncoded );

            decodedBuffer.resize( nBytesReadDecoded );
            encodedBuffer.resize( nBytesReadEncoded );

            /* Encountering eof during read also sets fail bit meaning tellg will return -1! */
            if ( !decodedFile.eof() ) {
                REQUIRE_EQUAL( static_cast<long long int>( decodedFile.tellg() ),
                               static_cast<long long int>( encodedFile->tell() ) );
            }
            REQUIRE_EQUAL( decodedFile.eof(), encodedFile->eof() );

            /* Avoid REQUIRE_EQUAL in order to avoid printing huge binary buffers out. */
            size_t equalElements = 0;
            size_t firstInequal = std::numeric_limits<long int>::max();
            for ( size_t i = 0; i < std::min( decodedBuffer.size(), encodedBuffer.size() ); ++i ) {
                if ( decodedBuffer[i] == encodedBuffer[i] ) {
                    ++equalElements;
                } else {
                    firstInequal = std::min( firstInequal, i );
                }
            }
            REQUIRE_EQUAL( equalElements, std::min( decodedBuffer.size(), encodedBuffer.size() ) );

            if ( equalElements != std::min( decodedBuffer.size(), encodedBuffer.size() ) ) {
                std::cerr << "First inequal element at " << firstInequal << "\n";
            }
        };

    /* Try some subsequent small reads. */
    read( 1 );
    read( 0 );
    read( 1 );
    read( 2 );
    read( 10 );
    read( 100 );
    read( 256 );

    /* Try some subsequent reads over bz2 block boundaries. */
    read( 5_Mi );
    read( 7_Mi );
    read( 1_Ki );

    /* Try reading over the end of the file. */
    read( decodedFileSize + 1000 );

    /* Try out seeking. */
    seek( 0 );
    seek( 1 );
    seek( 2 );
    seek( 2 );
    seek( 4 );
    seek( 256 );
    seek( 3_Mi );

    /* Seek after end of file */
    seek( 1_Gi );

    REQUIRE( encodedFile->blockOffsetsComplete() );
    REQUIRE_EQUAL( decodedFileSize, encodedFile->size().value() );

    /* Seek back and forth */
    seek( 10'000 );
    seek( 50'000 );
    seek( 10'000 );
    seek( 40'000 );

    /* Seek and read */
    seek( 0 );
    read( 1 );

    seek( 1 );
    read( 1 );

    seek( 2 );
    read( 2 );

    seek( 256 );
    read( 2 );

    seek( 256 );
    read( 1_Ki );

    seek( 2_Mi + 432 );
    read( 12345 );

    seek( 1_Mi - 432 );
    read( 432 );

    /* Try reading 1B before the end of file */
    seek( static_cast<long long int>( decodedFileSize ) - 4 );
    for ( int i = 0; i < 5; ++i ) {
        read( 1 );
    }

    /* Test thread joining feature intended for FUSE */
    seek( 333 );
    encodedFile->joinThreads();
    read( 100 );  /* Direct read after current position even after closing threads. */
    seek( 222 );
    read( 1_Mi );
    read( decodedFileSize + 1000 );

    encodedFile->joinThreads();
    seek( 1 );  /* Direct read after current position even after closing threads. */
    read( 100 );

    std::cerr << "Test block offset loading\n";
    const auto blockOffsets = encodedFile->blockOffsets();
    encodedFile->setBlockOffsets( blockOffsets );

    std::cerr << "Try reading 1B before the end of file\n";
    seek( static_cast<long long int>( decodedFileSize ) - 4 );
    read( decodedFileSize + 1000 );

    std::cerr << "Test block offset loading\n";
    decodedFile.clear();
    decodedFile.seekg( 0 );
    encodedFile = std::make_unique<ParallelBZ2Reader>( std::make_unique<StandardFileReader>( encodedTestFilePath ) );
    encodedFile->setBlockOffsets( blockOffsets );

    std::cerr << "Try reading 1B before the end of file\n";
    seek( static_cast<long long int>( decodedFileSize ) - 4 );
    for ( int i = 0; i < 5; ++i ) {
        read( 1 );
    }

    std::cerr << "Test block offset loading\n";
    decodedFile.clear();
    decodedFile.seekg( 0 );
    encodedFile = std::make_unique<ParallelBZ2Reader>( std::make_unique<StandardFileReader>( encodedTestFilePath ) );
    encodedFile->setBlockOffsets( blockOffsets );

    std::cerr << "Try reading 1B before the end of file\n";
    seek( static_cast<long long int>( decodedFileSize ) - 4 );
    read( decodedFileSize + 1000 );

    std::cerr << "Test block offset loading after partial reading\n";
    decodedFile.clear();
    decodedFile.seekg( 0 );
    encodedFile = std::make_unique<ParallelBZ2Reader>( std::make_unique<StandardFileReader>( encodedTestFilePath ) );
    read( 4 );
    encodedFile->setBlockOffsets( blockOffsets );

    std::cerr << "Try reading 1B before the end of file\n";
    seek( static_cast<long long int>( decodedFileSize ) - 4 );
    read( decodedFileSize + 1000 );
}


int
main()
{
    const auto tmpFolder = createTemporaryDirectory( "indexed_bzip2.testParallelBZ2Reader" );

    const auto decodedTestFilePath = tmpFolder.path() / "decoded";
    createRandomTextFile( decodedTestFilePath.string(), 2_Mi );

    const auto command = "bzip2 -k -- '" + decodedTestFilePath.string() + "'";
    const auto returnCode = std::system( command.c_str() );
    if ( returnCode != 0 ) {
        std::cerr << "Failed to compress sample file\n";
        return 1;
    }
    const auto encodedTestFilePath = tmpFolder.path() / "encoded-sample.bz2";
    std::filesystem::rename( tmpFolder.path() / "decoded.bz2", encodedTestFilePath );

    try
    {
        testSimpleOpenAndClose( encodedTestFilePath.string() );
        testDecodingBz2ForFirstTime( decodedTestFilePath.string(), encodedTestFilePath.string() );
    } catch ( const std::exception& exception ) {
        /* Note that the destructor for TemporaryDirectory might not be called for uncaught exceptions!
         * @see https://stackoverflow.com/questions/222175/why-destructor-is-not-called-on-exception */
        std::cerr << "Caught exception: " << exception.what() << "\n";
        REQUIRE( false );
    }

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
