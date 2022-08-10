#include <iostream>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include <common.hpp>
#include <IndexFileFormat.hpp>
#include <filereader/Standard.hpp>
#include <TestHelpers.hpp>


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

    const auto index = readGzipIndex(
        std::make_unique<StandardFileReader>( rootFolder / "base64-256KiB.gz.index" ) );

    REQUIRE( index.compressedSizeInBytes == fileSize( rootFolder / "base64-256KiB.gz" ) );
    REQUIRE( index.uncompressedSizeInBytes == fileSize( rootFolder / "base64-256KiB" ) );

    REQUIRE( index.checkpointSpacing == 64 * 1024 );
    REQUIRE( index.checkpoints.size() == 5 );

    try
    {
        const auto tmpFolder = createTemporaryDirectory( "pragzip.testGzipIndexFormat" );
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

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors;
}
