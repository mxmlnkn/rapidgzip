#include <array>
#include <iostream>

#include <common.hpp>
#include <FileUtils.hpp>
#include <filereader/Shared.hpp>
#include <filereader/Standard.hpp>


using namespace rapidgzip;


size_t
benchmarkPipeRead( const UniqueFileReader& inputFile,
                   const bool              readIntoBuffer )
{
    if ( !inputFile ) {
        throw std::invalid_argument( "Input file must be valid!" );
    }

    size_t nBytesRead{ 0 };
    std::vector<char> buffer;
    if ( readIntoBuffer ) {
        /**
         * Buffer size optimized with unwrapped StandardFileReader:
         *   4 KiB : 5665 5553 5956 5362 5888
         * 128 KiB : 6469 6341 6326 6070 5910
         *   1 MiB : 6553 6289 6245 6457 6108
         *   4 MiB : 6268 5850 5570 6164 6001
         *  64 MiB : 4273 4242 4356 4181 4128
         * 512 MiB : 3730 3751 3720 3779 3673
         * -> Seems like nothing can be done. The reading from the pipe simply becomes slow for some reason,
         *    when the buffer becomes too large. It's positively surprising that rapidgzip reaches ~4 GB/s
         *    even with its ~400 MB buffer(s).
         * I see why read performance with nullptr target destination would be faster with ~8.2 GB/s but I
         * don't understand why the buffer size can lead to an almost further 50% slowdown. Cache sizes would
         * be one obvious point but I don't understand why because this data is written once and then forgotten,
         * so it should be streamed out of the caches into the RAM fastly.
         * I'd hazard a guess that even if it is streamed into RAM, it means that the RAM read streaming by fcat
         * reduces bandwidth further! Basically, we have two std::memcpys running but only if the buffer does not
         * fit into the cache.
         */
        buffer.resize( 4_Mi );
    }

    auto* const sharedFileReader = dynamic_cast<SharedFileReader*>( inputFile.get() );
    auto* const singlePassFileReader = dynamic_cast<SinglePassFileReader*>( inputFile.get() );

    while ( true ) {
        const auto nBytesReadPerCall = readIntoBuffer
                                       ? inputFile->read( buffer.data(), buffer.size() )
                                       : inputFile->read( nullptr, std::numeric_limits<size_t>::max() );
        if ( nBytesReadPerCall == 0 ) {
            break;
        }
        nBytesRead += nBytesReadPerCall;

        if ( sharedFileReader != nullptr ) {
            const auto& [lock, file] = sharedFileReader->underlyingFile();
            auto* const fileReader = dynamic_cast<SinglePassFileReader*>( file );
            if ( fileReader != nullptr ) {
                fileReader->releaseUpTo( nBytesRead );
            }
        } else if ( singlePassFileReader != nullptr ) {
            singlePassFileReader->releaseUpTo( nBytesRead );
        }
    }

    return nBytesRead;
}


enum class FileWrapper
{
    NONE,
    SINGLE_PASS,
    SHARED,
};


[[nodiscard]] UniqueFileReader
wrapFileReader( UniqueFileReader inputFile,
                FileWrapper      fileWrapper )
{
    switch ( fileWrapper )
    {
    case FileWrapper::NONE:
        return inputFile;
    case FileWrapper::SINGLE_PASS:
        return std::make_unique<SinglePassFileReader>( std::move( inputFile ) );
    case FileWrapper::SHARED:
        return std::make_unique<SharedFileReader>(
            std::make_unique<SinglePassFileReader>( std::move( inputFile ) ) );
    }
    return {};
}


size_t
benchmarkPipeRead( UniqueFileReader inputFile,
                   const bool       readIntoBuffer,
                   FileWrapper      fileWrapper )
{
    return benchmarkPipeRead( wrapFileReader( std::move( inputFile ), fileWrapper ), readIntoBuffer );
}


size_t
benchmarkThreadedPipeRead( UniqueFileReader inputFile,
                           const bool       readIntoBuffer,
                           FileWrapper      fileWrapper )
{
    const auto wrappedFile = wrapFileReader( std::move( inputFile ), fileWrapper );
    auto result = std::async( std::launch::async,
                              [&] () { return benchmarkPipeRead( wrappedFile, readIntoBuffer ); } );
    return result.get();
}


int
main( int    argc,
      char** argv )
{
    auto inputFile = openFileOrStdin( argc > 1 ? argv[1] : "" );
    if ( !inputFile ) {
        std::cerr << "Failed to open pipe\n";
        return 1;
    }

    const auto t0 = now();

    /* 8373 8144 7653 8718 8105 MB/s */
    //const auto nBytesRead = benchmarkPipeRead( std::move( inputFile ), /* buffer */ false, FileWrapper::NONE );
    /* 6183 6283 5985 5977 5970 MB/s */
    //const auto nBytesRead = benchmarkPipeRead( std::move( inputFile ), /* buffer */ true, FileWrapper::NONE );

    /* 2322 1969 2086 2137 2040 MB/s
     * This is slow because no memory gets released in the single call to SinglePassReader::read! */
    //const auto nBytesRead = benchmarkPipeRead( std::move( inputFile ), /* buffer */ false, FileWrapper::SINGLE_PASS );
    /* 2938 2962 2782 3080 3073 MB/s */
    //const auto nBytesRead = benchmarkPipeRead( std::move( inputFile ), /* buffer */ true, FileWrapper::SINGLE_PASS );

    /* 2080 2175 2088 2055 2092 MB/s */
    //const auto nBytesRead = benchmarkPipeRead( std::move( inputFile ), /* buffer */ false, FileWrapper::SHARED );
    /* 3156 2839 2747 3146 2880 MB/s */
    const auto nBytesRead = benchmarkPipeRead( std::move( inputFile ), /* buffer */ true, FileWrapper::SHARED );

    const auto dt = duration( t0 );
    std::stringstream message;
    message << "Read " << formatBytes( nBytesRead ) << " from pipe with in " << dt << " s -> "
            << std::round( static_cast<double>( nBytesRead ) / dt / 1e6 ) << " MB/s\n";
    std::cerr << std::move( message ).str();

    return 0;
}


/*
cmake --build . -- benchmarkPipeRead && src/benchmarks/benchmarkPipeRead <( fcat 4GiB-base64.gz )

FileWrapper::SHARED, readIntoBuffer = true:

    Finished buffering the whole file: 3 GiB 40 MiB 720 KiB 411 B!
    Read 3 GiB 44 MiB from pipe with in 1.14994 s -> 2841 MB/s

time wc -l <( fcat 4GiB-base64.gz )

    real 0.602s 0.610s 0.609s

time wc -c <( fcat 4GiB-base64.gz )

    real 0.435s 0.398s 0.396s

 -> Seems to me like wc -c is just faster because it doesn't actually have to "copy" the stream to RAM.

rapidgzip && src/tools/rapidgzip -P 0 -d -o /dev/null <( fcat 4GiB-base64.gz )

    Decompressed in total 4294967296 B in 1.16497 s -> 3686.75 MB/s
    Decompressed in total 4294967296 B in 1.17416 s -> 3657.9 MB/s
    Decompressed in total 4294967296 B in 1.14751 s -> 3742.87 MB/s

  -> We are completely bounded by the pipe reading speed!
*/
