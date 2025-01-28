#include <iostream>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include <common.hpp>
#include <IndexFileFormat.hpp>
#include <filereader/Standard.hpp>
#include <TestHelpers.hpp>


using namespace rapidgzip;


GzipIndex
testIndexRead( const std::filesystem::path& compressedPath,
               const std::filesystem::path& uncompressedPath,
               const std::filesystem::path& indexPath,
               const size_t                 expectedCheckpointSpacing,
               const size_t                 expectedCheckpointCount )
{
    auto index = readGzipIndex( std::make_unique<StandardFileReader>( indexPath.string() ),
                                /* This second argument is necessary when reading gztool indexes! */
                                std::make_unique<StandardFileReader>( compressedPath ) );

    REQUIRE_EQUAL( index.compressedSizeInBytes, fileSize( compressedPath ) );
    REQUIRE_EQUAL( index.uncompressedSizeInBytes, fileSize( uncompressedPath ) );

    REQUIRE_EQUAL( index.checkpointSpacing, expectedCheckpointSpacing );
    REQUIRE_EQUAL( index.checkpoints.size(), expectedCheckpointCount );

    REQUIRE( static_cast<bool>( index.windows ) );

    return index;
}


void
testIndexReadWrite( const std::filesystem::path& compressedPath,
                    const std::filesystem::path& uncompressedPath,
                    const std::filesystem::path& indexPath,
                    const size_t                 expectedCheckpointSpacing,
                    const size_t                 expectedCheckpointCount )
{
    const auto index = testIndexRead( compressedPath, uncompressedPath, indexPath,
                                      expectedCheckpointSpacing, expectedCheckpointCount );

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

            using namespace std::string_literals;
            if ( endsWith( indexPath.string(), ".gztool.index"s )
                 || endsWith( indexPath.string(), ".gztool.with-lines.index"s ) )
            {
                gztool::writeGzipIndex( index, checkedWrite );
            } else  {
                indexed_gzip::writeGzipIndex( index, checkedWrite );
            }
        }
        const auto rereadIndex = readGzipIndex( std::make_unique<StandardFileReader>( gzipIndexPath.string() ),
                                                /* This second argument is necessary when reading gztool indexes! */
                                                std::make_unique<StandardFileReader>( compressedPath.string() ) );

        REQUIRE_EQUAL( rereadIndex.compressedSizeInBytes, index.compressedSizeInBytes );
        REQUIRE_EQUAL( rereadIndex.uncompressedSizeInBytes, index.uncompressedSizeInBytes );
        REQUIRE_EQUAL( rereadIndex.checkpointSpacing, index.checkpointSpacing );
        REQUIRE_EQUAL( rereadIndex.windowSizeInBytes, index.windowSizeInBytes );
        REQUIRE_EQUAL( rereadIndex.hasLineOffsets, index.hasLineOffsets );
        REQUIRE_EQUAL( rereadIndex.newlineFormat, index.newlineFormat );

        REQUIRE( rereadIndex.checkpoints == index.checkpoints );

        REQUIRE_EQUAL( static_cast<bool>( rereadIndex.windows ), static_cast<bool>( index.windows ) );
        if ( rereadIndex.windows && index.windows ) {
            REQUIRE_EQUAL( rereadIndex.windows->size(), index.windows->size() );
            const auto& [_, windows] = index.windows->data();
            for ( const auto& [offset, window] : *windows ) {
                const auto rereadWindow = rereadIndex.windows->get( offset );
                if ( !rereadWindow ) {
                    std::cerr << "Failed to find offset " << offset << " in reread index!\n";
                    continue;
                }

                if ( static_cast<bool>( window ) != static_cast<bool>( rereadWindow ) ) {
                    std::stringstream message;
                    message << std::boolalpha << "Shared window has value: " << static_cast<bool>( window )
                            << " while reread shared window has value: " << static_cast<bool>( rereadWindow );
                    std::cerr << std::move( message ).str() << "\n";
                    continue;
                }

                if ( *window != *rereadWindow ) {
                    const auto a = window->decompress();
                    const auto b = rereadWindow->decompress();
                    if ( ( static_cast<bool>( a ) != static_cast<bool>( b ) )
                         || ( static_cast<bool>( a ) && static_cast<bool>( b ) && ( *a != *b ) ) )
                    {
                        std::cerr << "Window contents for offset " << offset << " differ!\n";
                        std::cerr << "Compressed window size: " << window->compressedSize()
                                  << " reread window: " << rereadWindow->compressedSize() << "\n";
                        if ( a && b ) {
                            std::cerr << "Decompressed window size: " << a->size()
                                      << " reread window: " << b->size() << "\n";
                        }
                    }
                }
            }
        }

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


int
main( int    argc,
      char** argv )
{
    if ( argc == 0 ) {
        std::cerr << "Expected at least the launch command as the first argument!\n";
        return 1;
    }

    const std::string binaryFilePath( argv[0] );
    auto binaryFolder = std::filesystem::path( binaryFilePath ).parent_path().string();
    if ( binaryFolder.empty() ) {
        binaryFolder = ".";
    }
    const auto rootFolder =
        static_cast<std::filesystem::path>(
            findParentFolderContaining( binaryFolder, "src/tests/data/base64-256KiB.bgz" )
        ) / "src" / "tests" / "data";

    using namespace std::string_literals;
    testIndexReadWrite( rootFolder / "base64-256KiB.gz",
                        rootFolder / "base64-256KiB",
                        rootFolder / "base64-256KiB.gz.index",
                        64_Ki, 5U );

    testIndexReadWrite( rootFolder / "base64-256KiB.gz",
                        rootFolder / "base64-256KiB",
                        rootFolder / "base64-256KiB.gz.gztool.index",
                        0, 5U );

    testIndexReadWrite( rootFolder / "base64-256KiB.gz",
                        rootFolder / "base64-256KiB",
                        rootFolder / "base64-256KiB.gz.gztool.with-lines.index",
                        0, 9U );

    testIndexRead( rootFolder / "base64-256KiB.bgz",
                   rootFolder / "base64-256KiB",
                   rootFolder / "base64-256KiB.bgz.gzi",
                   0, 4U );

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors == 0 ? 0 : 1;
}
