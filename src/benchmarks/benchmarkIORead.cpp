#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <common.hpp>
#include <FileUtils.hpp>


using namespace rapidgzip;


/* Create a temporary file for benchmarking that is cleaned up with RAII. */
struct TemporaryFile
{
    explicit
    TemporaryFile( const size_t requestedSize ) :
        size( requestedSize )
    {
        std::ofstream file( path );
        std::vector<char> dataToWrite( 1_Mi );
        for ( auto& x : dataToWrite ) {
            x = static_cast<char>( rand() );
        }
        for ( size_t nBytesWritten = 0; nBytesWritten < size; nBytesWritten += dataToWrite.size() ) {
            file.write( dataToWrite.data(), static_cast<std::streamsize>( dataToWrite.size() ) );
        }
        file.close();
    }

    ~TemporaryFile()
    {
        std::filesystem::remove( path );
    }

    TemporaryFile( const TemporaryFile& ) = delete;

    TemporaryFile( TemporaryFile&& ) = delete;

    TemporaryFile&
    operator=( const TemporaryFile& ) = delete;

    TemporaryFile&
    operator=( TemporaryFile&& ) = delete;

    const std::string path{ "/dev/shm/rapidgzip-benchmark-random-file.dat" };
    const size_t size;
};


void
benchmarkSequentialReading( const std::string& filePath,
                            const size_t       chunkSize )
{
    const auto file = throwingOpen( filePath, "rb" );
    std::vector<char> buffer( chunkSize == 0 ? 4096 : chunkSize );

    const auto t0 = now();
    uint64_t totalBytesRead{ 0 };
    while ( true ) {
        const auto nBytesRead = std::fread( buffer.data(), /* element size */ 1, buffer.size(), file.get() );
        if ( nBytesRead <= 0 ) {
            break;
        }
        totalBytesRead += static_cast<uint64_t>( nBytesRead );
    }

    if ( chunkSize > 0 ) {
        const auto readTime = duration( t0 );
        const auto bandwidth = static_cast<double>( totalBytesRead ) / readTime;
        std::cout << "Read " << formatBytes( totalBytesRead ) << " using " << formatBytes( chunkSize ) << " chunks in "
                  << readTime << " s -> " << bandwidth / 1e9 << " GB/s\n";
    }
}


void
benchmarkReading( const std::string& filePath )
{
    /* Read the file once to trigger buffering it into RAM. */
    benchmarkSequentialReading( filePath, 0 );

    benchmarkSequentialReading( filePath, 4_Ki );
    benchmarkSequentialReading( filePath, 8_Ki );
    benchmarkSequentialReading( filePath, 16_Ki );
    benchmarkSequentialReading( filePath, 32_Ki );
    /* Without the 64 KiB case, the 128 KiB case reproducibly takes 1.13s instead of 0.8s.
     * Somehow it seems to train the kernel for better reading? */
    benchmarkSequentialReading( filePath, 64_Ki );
    benchmarkSequentialReading( filePath, 128_Ki );
    benchmarkSequentialReading( filePath, 1_Mi );
    benchmarkSequentialReading( filePath, 2_Mi );
    benchmarkSequentialReading( filePath, 4_Mi );
}


int
main( int    argc,
      char** argv )
{
    if ( argc > 1 ) {
        for ( int i = 1; i < argc; ++i ) {
            if ( std::filesystem::exists( argv[i] ) ) {
                benchmarkReading( argv[i] );
            }
        }
        return 0;
    }

    TemporaryFile temporaryFile( 8_Gi );

    benchmarkReading( temporaryFile.path );

    return 0;
}


/*
cmake --build . -- benchmarkIORead && src/benchmarks/benchmarkIORead

Read 8 GiB using 4 KiB chunks in 1.05269 s -> 8.16 GB/s
Read 8 GiB using 8 KiB chunks in 0.888408 s -> 9.6689 GB/s
Read 8 GiB using 16 KiB chunks in 0.860965 s -> 9.9771 GB/s
Read 8 GiB using 32 KiB chunks in 0.819003 s -> 10.4883 GB/s
Read 8 GiB using 64 KiB chunks in 0.806548 s -> 10.6503 GB/s
Read 8 GiB using 128 KiB chunks in 0.806915 s -> 10.6454 GB/s
Read 8 GiB using 1 MiB chunks in 0.813736 s -> 10.5562 GB/s
Read 8 GiB using 2 MiB chunks in 0.940564 s -> 9.13275 GB/s
Read 8 GiB using 4 MiB chunks in 0.983208 s -> 8.73664 GB/s
*/
