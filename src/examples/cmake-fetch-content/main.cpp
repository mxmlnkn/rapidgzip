#include <iostream>
#include <memory>

#include <rapidgzip.hpp>

using namespace rapidgzip;


int
main( int    argc,
      char** argv )
{
    /* Create a file reader for the gzipped file, decompress it with ParallelGzipReader,
     * read from it in chunks and write to std::cout until the file end has been reached. */
    if ( argc <= 1 ) {
        std::cerr << "Please specify a file to decompress.\n";
        return 1;
    }

    auto fileReader = std::make_unique<rapidgzip::StandardFileReader>( argv[1] );
    rapidgzip::ParallelGzipReader reader( std::move( fileReader ),
                                          0 /* parallelization (0 = auto, use all cores). */ );

    std::vector<char> buffer( 4_Mi );
    while ( true ) {
        size_t bytesRead = reader.read(buffer.data(), buffer.size());
        if ( bytesRead == 0 ) {
            break;
        }
        std::cout.write( buffer.data(), bytesRead );
    }

    return 0;
}
