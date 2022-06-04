#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include <BitStringFinder.hpp>
#include <common.hpp>
#include <BZ2Reader.hpp>


namespace
{
std::ios_base::seekdir
toSeekdir( int origin )
{
    switch ( origin )
    {
    case SEEK_SET: return std::ios_base::beg;
    case SEEK_CUR: return std::ios_base::cur;
    case SEEK_END: return std::ios_base::end;
    default: break;
    }

    throw std::invalid_argument( "Unknown origin" );
}
}


void
testSimpleOpenAndClose( const std::string& bz2File )
{
    const auto t0 = std::chrono::high_resolution_clock::now();
    {
        BZ2Reader encodedFile( bz2File );  // NOLINT
        const auto t1 = std::chrono::high_resolution_clock::now();
        const auto dt = std::chrono::duration_cast<std::chrono::duration<double> >( t1 - t0 ).count();
        REQUIRE( dt < 1 );
    }
    const auto t1 = std::chrono::high_resolution_clock::now();
    const auto dt = std::chrono::duration_cast<std::chrono::duration<double> >( t1 - t0 ).count();
    REQUIRE( dt < 1 );
}


void
testSeek( size_t                            decodedFileSize,
          std::ifstream&                    decodedFile,
          const std::unique_ptr<BZ2Reader>& encodedFile,
          long long int                     offset,
          int                               origin = SEEK_SET )
{
    std::cerr << "Seek to " << offset << "\n";

    /**
     * Clear fail bit in order to seek back.
     * When using read to read the number of bytes the file has, then no eof bit is set.
     * As soon as you request one more byte than the file contains, both, the failbit and eofbit are set
     * but only the eofbit will be cleared by seekg since C++11.
     * @see https://en.cppreference.com/w/cpp/io/basic_istream/seekg
     * > Before doing anything else, seekg clears eofbit. 	(since C++11)
     */
    if ( decodedFile.fail() ) {
        decodedFile.clear();
    }
    decodedFile.seekg( offset, toSeekdir( origin ) );
    const auto newSeekPosDecoded = static_cast<ssize_t>( decodedFile.tellg() );
    const auto newSeekPosEncoded = static_cast<ssize_t>( encodedFile->seek( offset, origin ) );

    /* Wanted differing behavior between std::ifstream and BZ2Reader. */
    REQUIRE_EQUAL( std::min<ssize_t>( newSeekPosDecoded, decodedFileSize ), newSeekPosEncoded );
    REQUIRE_EQUAL( std::min<ssize_t>( decodedFile.tellg(), decodedFileSize ),
                   static_cast<ssize_t>( encodedFile->tell() ) );

    /* Beware! eof behavior is different. std:ifstream requires to read more than the file contents
     * for EOF to be reached while BZ2Reader only required to read more than >or equal< the file
     * size of bytes. Furthermore, seeking beyond the file does not set EOF in std::ifstream but does
     * set EOF in BZ2Reader! */
    //REQUIRE_EQUAL( decodedFile.eof(), encodedFile->eof() );
}


void
testRead( std::ifstream&                    decodedFile,
          const std::unique_ptr<BZ2Reader>& encodedFile,
          size_t                            nBytesToRead )
{
    std::cerr << "Read " << nBytesToRead << "B from " << encodedFile->tell() << "\n";

    std::vector<char> decodedBuffer( nBytesToRead, 11 );
    std::vector<char> encodedBuffer( nBytesToRead, 22 );

    if ( !encodedFile->eof() ) {
        REQUIRE_EQUAL( static_cast<size_t>( decodedFile.tellg() ), encodedFile->tell() );
    }

    /* Why doesn't the ifstream has a similar return specifying the number of read bytes?! */
    decodedFile.read( decodedBuffer.data(), nBytesToRead );
    const auto nBytesReadDecoded = decodedFile.gcount();

    const auto nBytesReadEncoded = encodedFile->read( -1, encodedBuffer.data(), nBytesToRead );

    REQUIRE_EQUAL( static_cast<size_t>( nBytesReadDecoded ), nBytesReadEncoded );

    decodedBuffer.resize( nBytesReadDecoded );
    encodedBuffer.resize( nBytesReadEncoded );

    /* Encountering eof during read also sets fail bit meaning tellg will return -1! */
    if ( !decodedFile.eof() ) {
        REQUIRE_EQUAL( static_cast<ssize_t>( decodedFile.tellg() ),
                       static_cast<ssize_t>( encodedFile->tell() ) );
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
}


/**
 * Tests are in such a way that seeking and reading are mirrored on the BZ2Reader file and the decoded file.
 * Then all read results can be checked against each other. Same for the result of tell.
 */
void
testDecodingBz2ForFirstTime( const std::string& decodedTestFilePath,
                             const std::string& encodedTestFilePath )
{
    size_t decodedFileSize = fileSize( decodedTestFilePath );
    std::cerr << "Decoded file size: " << decodedFileSize << "\n";

    std::ifstream decodedFile( decodedTestFilePath );
    auto encodedFile = std::make_unique<BZ2Reader>( encodedTestFilePath );

    const auto seek =
        [&]( long long int offset,
             int           origin = SEEK_SET )
        { testSeek( decodedFileSize, decodedFile, encodedFile, offset, origin ); };

    const auto read = [&]( size_t nBytesToRead ){ testRead( decodedFile, encodedFile, nBytesToRead ); };

    /* Try some subsequent small reads. */
    read( 1 );
    read( 0 );
    read( 1 );
    read( 2 );
    read( 10 );
    read( 100 );
    read( 256 );

    /* Try some subsequent reads over bz2 block boundaries. */
    read( 5*1024*1024 );
    read( 7*1024*1024 );
    read( 1024 );

    /* Try reading over the end of the file. */
    read( 128*1024*1024 );

    /* Try out seeking. */
    seek( 0 );
    seek( 1 );
    seek( 2 );
    seek( 2 );
    seek( 4 );
    seek( 256 );
    seek( 3*1024*1024 );

    /* Seek after end of file */
    seek( static_cast<ssize_t>( decodedFileSize ) + 1000 );

    REQUIRE( encodedFile->blockOffsetsComplete() );
    REQUIRE_EQUAL( decodedFileSize, encodedFile->size() );

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
    read( 1024 );

    seek( 2*1024*1024 + 432 );
    read( 12345 );

    seek( 1*1024*1024 - 432 );
    read( 432 );

    /* Try reading 1B before the end of file */
    seek( static_cast<ssize_t>( decodedFileSize ) - 4 );
    for ( int i = 0; i < 5; ++i ) {
        read( 1 );
    }

    std::cerr << "Test block offset loading\n";
    const auto blockOffsets = encodedFile->blockOffsets();
    encodedFile->setBlockOffsets( blockOffsets );

    std::cerr << "Try reading 1B before the end of file\n";
    seek( static_cast<ssize_t>( decodedFileSize ) - 4 );
    read( decodedFileSize + 1000 );

    std::cerr << "Test block offset loading\n";
    decodedFile.clear();
    decodedFile.seekg( 0 );
    encodedFile = std::make_unique<BZ2Reader>( encodedTestFilePath );
    encodedFile->setBlockOffsets( blockOffsets );

    std::cerr << "Try reading 1B before the end of file\n";
    seek( static_cast<ssize_t>( decodedFileSize ) - 4 );
    for ( int i = 0; i < 5; ++i ) {
        read( 1 );
    }

    std::cerr << "Test block offset loading\n";
    decodedFile.clear();
    decodedFile.seekg( 0 );
    encodedFile = std::make_unique<BZ2Reader>( encodedTestFilePath );
    encodedFile->setBlockOffsets( blockOffsets );

    std::cerr << "Try reading 1B before the end of file\n";
    seek( static_cast<ssize_t>( decodedFileSize ) - 4 );
    read( decodedFileSize + 1000 );

    std::cerr << "Test block offset loading after partial reading\n";
    decodedFile.clear();
    decodedFile.seekg( 0 );
    encodedFile = std::make_unique<BZ2Reader>( encodedTestFilePath );
    read( 4 );
    encodedFile->setBlockOffsets( blockOffsets );

    std::cerr << "Try reading 1B before the end of file\n";
    seek( static_cast<ssize_t>( decodedFileSize ) - 4 );
    read( decodedFileSize + 1000 );
}


void
testSeekBeforeOffsetCompletion( const std::string& decodedTestFilePath,
                                const std::string& encodedTestFilePath )
{
    size_t decodedFileSize = fileSize( decodedTestFilePath );
    std::cerr << "Decoded file size: " << decodedFileSize << "\n";

    const auto blockOffsets = std::make_unique<BZ2Reader>( encodedTestFilePath )->blockOffsets();

    std::ifstream decodedFile( decodedTestFilePath );
    auto encodedFile = std::make_unique<BZ2Reader>( encodedTestFilePath );

    const auto seek =
        [&]( long long int offset,
             int           origin = SEEK_SET )
        { testSeek( decodedFileSize, decodedFile, encodedFile, offset, origin ); };

    const auto read = [&]( size_t nBytesToRead ){ testRead( decodedFile, encodedFile, nBytesToRead ); };

    /* Read a bit because having a non-zero decoded count is a prerequisite to trigger a possible bug. */
    REQUIRE( encodedFile->availableBlockOffsets().empty() );
    read( 50'000 );  /* Some value smaller than the first block. */

    std::cerr << "Current block offsets after seeking 50 KB:\n";
    for ( const auto& [encodedOffset, decodedOffset] : encodedFile->availableBlockOffsets() ) {
        std::cerr << "  " << encodedOffset << " b -> " << decodedOffset << " B\n";
    }
    const std::map<size_t, size_t> onlyFirstBlock = { { 32, 0 } };
    REQUIRE( encodedFile->availableBlockOffsets() == onlyFirstBlock );
    /* Seek back, which triggers redecoding parts leading to the internal variable being incremented. */
    seek( 0 );
    read( 20'000 );
    REQUIRE( encodedFile->tell() == 20'000 );

    REQUIRE( blockOffsets == encodedFile->blockOffsets() );
    REQUIRE( blockOffsets.size() > 1 &&
             "Cannot trigger the possible bug with only one real block! use a larger file." );
}


[[nodiscard]] TemporaryDirectory
createTemporaryDirectory()
{
    const std::filesystem::path tmpFolderName = "indexed_bzip2.testBZ2Reader." + std::to_string( unixTime() );
    std::filesystem::create_directory( tmpFolderName );
    return tmpFolderName;
}


int
main()
{
    const auto tmpFolder = createTemporaryDirectory();

    const auto decodedTestFilePath = tmpFolder.path() / "decoded";
    createRandomTextFile( decodedTestFilePath, 2 * 1024 * 1024 );

    const auto command = "bzip2 -k -- '" + std::string( decodedTestFilePath ) + "'";
    const auto returnCode = std::system( command.c_str() );
    if ( returnCode != 0 ) {
        std::cerr << "Failed to compress sample file\n";
        return 1;
    }
    const auto encodedTestFilePath = tmpFolder.path() / "encoded-sample.bz2";
    std::filesystem::rename( tmpFolder.path() / "decoded.bz2", encodedTestFilePath );

    try
    {
        testSimpleOpenAndClose( encodedTestFilePath );

        testDecodingBz2ForFirstTime( decodedTestFilePath, encodedTestFilePath );

        /* This test works because any seeking back triggers the completion of the block offset map! */
        testSeekBeforeOffsetCompletion( decodedTestFilePath, encodedTestFilePath );
    } catch ( const std::exception& exception ) {
        /* Note that the destructor for TemporaryDirectory might not be called for uncaught exceptions!
         * @see https://stackoverflow.com/questions/222175/why-destructor-is-not-called-on-exception */
        std::cerr << "Caught exception: " << exception.what() << "\n";
        gnTestErrors += 1;
    }

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors;
}
