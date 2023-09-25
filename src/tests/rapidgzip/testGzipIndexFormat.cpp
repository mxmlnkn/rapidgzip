#include <iostream>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include <common.hpp>
#include <IndexFileFormat.hpp>
#include <filereader/Standard.hpp>
#include <TestHelpers.hpp>


GzipIndex
testIndexRead( const std::filesystem::path& compressedPath,
               const std::filesystem::path& uncompressedPath,
               const std::filesystem::path& indexPath )
{
    auto index = readGzipIndex( std::make_unique<StandardFileReader>( indexPath ) );

    REQUIRE_EQUAL( index.compressedSizeInBytes, fileSize( compressedPath ) );
    REQUIRE_EQUAL( index.uncompressedSizeInBytes, fileSize( uncompressedPath ) );

    REQUIRE_EQUAL( index.checkpointSpacing, 64_Ki );
    REQUIRE_EQUAL( index.checkpoints.size(), 5U );

    REQUIRE( static_cast<bool>( index.windows ) );

    return index;
}


void
testIndexReadWrite( const std::filesystem::path& compressedPath,
                    const std::filesystem::path& uncompressedPath,
                    const std::filesystem::path& indexPath )
{
    const auto index = testIndexRead( compressedPath, uncompressedPath, indexPath );

    try
    {
        const auto tmpFolder = createTemporaryDirectory( "rapidgzip.testGzipIndexFormat" );
        const auto gzipIndexPath = tmpFolder.path() / "gzipindex";

        {
            const auto file = throwingOpen( gzipIndexPath, "wb" );
            const auto checkedWrite =
                [&file] ( const void* buffer, size_t size )
                {
                    if ( std::fwrite( buffer, 1, size, file.get() ) != size ) {
                        throw std::runtime_error( "Failed to write data to index!" );
                    }
                };
            writeGzipIndex( index, checkedWrite );
        }
        const auto rereadIndex = readGzipIndex( std::make_unique<StandardFileReader>( gzipIndexPath ) );
        REQUIRE( rereadIndex == index );
    }
    catch ( const std::exception& exception )
    {
        /* Note that the destructor for TemporaryDirectory might not be called for uncaught exceptions!
         * @see https://stackoverflow.com/questions/222175/why-destructor-is-not-called-on-exception */
        std::cerr << "Caught exception: " << exception.what() << "\n";
        REQUIRE( false );
    }
}


GzipIndex
testBzipIndexRead( const std::filesystem::path& compressedPath,
                   const std::filesystem::path& uncompressedPath,
                   const std::filesystem::path& indexPath )
{
    auto index = readGzipIndex( std::make_unique<StandardFileReader>( indexPath ),
                                /* This second argument is only necessary when reading bgzip indexes! */
                                std::make_unique<StandardFileReader>( compressedPath ) );

    REQUIRE_EQUAL( index.compressedSizeInBytes, fileSize( compressedPath ) );
    REQUIRE_EQUAL( index.uncompressedSizeInBytes, fileSize( uncompressedPath ) );

    /* checkpointSpacing is not available for bgzip indexes. */
    REQUIRE_EQUAL( index.checkpointSpacing, 0U );
    REQUIRE_EQUAL( index.checkpoints.size(), 4U );

    REQUIRE( static_cast<bool>( index.windows ) );

    return index;
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
    std::string binaryFolder = std::filesystem::path( binaryFilePath ).parent_path();
    if ( binaryFolder.empty() ) {
        binaryFolder = ".";
    }
    const auto rootFolder =
        static_cast<std::filesystem::path>(
            findParentFolderContaining( binaryFolder, "src/tests/data/base64-256KiB.bgz" )
        ) / "src" / "tests" / "data";

    testIndexReadWrite( rootFolder / "base64-256KiB.gz",
                        rootFolder / "base64-256KiB",
                        rootFolder / "base64-256KiB.gz.index" );
    testBzipIndexRead( rootFolder / "base64-256KiB.bgz",
                       rootFolder / "base64-256KiB",
                       rootFolder / "base64-256KiB.bgz.gzi" );

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors;
}
