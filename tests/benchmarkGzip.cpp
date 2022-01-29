
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <archive.h>
#include <zlib.h>

#include <BitReader.hpp>
#include <common.hpp>
#include <FileUtils.hpp>
#include <MemoryFileReader.hpp>
#include <pragzip.hpp>
#include <StandardFileReader.hpp>
#include <Statistics.hpp>


class GzipWrapper
{
public:
    static constexpr int WINDOW_SIZE = 32*1024;

    enum class Format
    {
        AUTO,
        RAW,
        GZIP,
    };

public:
    explicit
    GzipWrapper( Format format = Format::AUTO ) :
        m_format( format )
    {
        init( format );
    }

private:
    void
    init( Format format )
    {
        m_stream = {};

        m_stream.zalloc = Z_NULL;     /* used to allocate the internal state */
        m_stream.zfree = Z_NULL;      /* used to free the internal state */
        m_stream.opaque = Z_NULL;     /* private data object passed to zalloc and zfree */

        m_stream.avail_in = 0;        /* number of bytes available at next_in */
        m_stream.next_in = Z_NULL;    /* next input byte */

        m_stream.avail_out = 0;       /* remaining free space at next_out */
        m_stream.next_out = Z_NULL;   /* next output byte will go here */

        m_stream.msg = nullptr;

        int windowBits = 15;  // maximum value corresponding to 32kiB;
        switch ( format )
        {
        case Format::AUTO:
            windowBits += 32;
            break;

        case Format::RAW:
            windowBits *= -1;
            break;

        case Format::GZIP:
            windowBits += 16;
            break;
        }

        auto ret = inflateInit2( &m_stream, windowBits );
        if ( ret != Z_OK ) {
            throw std::runtime_error( "inflateInit2 returned error code: " + std::to_string( ret ) );
        }
    }

public:
    ~GzipWrapper()
    {
        inflateEnd( &m_stream );
    }

    [[nodiscard]] size_t
    inflate( unsigned char const** compressedData,
             size_t*               compressedSize )
    {
        if ( *compressedSize == 0 ) {
            return 0;
        }

        m_stream.avail_in = *compressedSize;
        /* const_cast should be safe because zlib presumably only uses this in a const manner. */
        m_stream.next_in = const_cast<unsigned char*>( *compressedData );

        m_stream.avail_out = m_outputBuffer.size();
        m_stream.next_out = m_outputBuffer.data();

        /* When using Z_FINISH, it seems that avail_in and next_in are not updated!
         * Plus, the output buffer must be large enough to hold everything. Use Z_NO_FLUSH instead. */
        const auto errorCode = ::inflate( &m_stream, Z_NO_FLUSH );
        *compressedData = m_stream.next_in;
        *compressedSize = m_stream.avail_in;
        if ( ( errorCode != Z_OK ) && ( errorCode != Z_STREAM_END ) ) {
            return 0;
        }

        if ( m_stream.avail_out > m_outputBuffer.size() ) {
            throw std::logic_error( "Zlib returned and invalid value!" );
        }

        const auto nBytesDecoded = m_outputBuffer.size() - m_stream.avail_out;

        if ( errorCode == Z_STREAM_END ) {
            /* Reinitialize internal data at end position to support multi-stream input. */
            if ( *compressedSize > 0 ) {
                inflateEnd( &m_stream );
                init( m_format );
            }
        }

        return nBytesDecoded;
    }

private:
    const Format m_format;
    z_stream m_stream{};
    std::vector<unsigned char> m_window = std::vector<unsigned char>( 32UL * 1024UL, '\0' );
    std::vector<unsigned char> m_outputBuffer = std::vector<unsigned char>( 64UL * 1024UL * 1024UL );
};


template<typename Functor>
[[nodiscard]] std::pair<size_t, std::vector<double> >
benchmarkFunction( Functor functor )
{
    decltype(functor()) result{};
    std::vector<double> durations;
    for ( size_t i = 0; i < 3; ++i ) {
        const auto t0 = now();
        result = functor();
        const auto t1 = now();
        durations.push_back( duration( t0, t1 ) );
    }

    return { result, durations };
}


template<typename Functor,
         typename SetupFunctor>
[[nodiscard]] std::pair<size_t, std::vector<double> >
benchmarkFunction( SetupFunctor setup,
                   Functor      functor )
{
    decltype(setup()) setupResult;
    try {
        setupResult = setup();
    } catch ( const std::exception& e ) {
        std::cerr << "Failed to run setup with exception: " << e.what() << "\n";
        return {};
    }

    decltype(functor( setupResult )) result{};
    std::vector<double> durations;
    for ( size_t i = 0; i < 3; ++i ) {
        const auto t0 = now();
        result = functor( setupResult );
        const auto t1 = now();
        durations.push_back( duration( t0, t1 ) );
    }

    return { result, durations };
}


[[nodiscard]] std::vector<uint8_t>
readFile( const std::string& fileName )
{
    const auto file = throwingOpen( fileName, "rb" );
    const auto success = std::fseek( file.get(), 0, SEEK_END );
    if ( success != 0 ) {
        throw std::runtime_error( "Could not seek in given file!" );
    }

    const auto fileSize = std::ftell( file.get() );
    std::vector<uint8_t> contents( fileSize );
    std::fseek( file.get(), 0, SEEK_SET );
    const auto nBytesRead = std::fread( contents.data(), sizeof( contents[0] ), contents.size(), file.get() );

    if ( nBytesRead != contents.size() ) {
        throw std::logic_error( "Did read less bytes than file is large!" );
    }

    return contents;
}


[[nodiscard]] size_t
decompressWithZlib( const std::vector<uint8_t>& compressedData )
{
    GzipWrapper gzip;
    const auto* pCompressedData = compressedData.data();
    auto compressedSize = compressedData.size();
    size_t totalDecodedBytes = 0;

    while ( compressedSize > 0 )
    {
        const auto decodedBytes = gzip.inflate( &pCompressedData, &compressedSize );
        if ( decodedBytes == 0 ) {
            break;
        }
        totalDecodedBytes += decodedBytes;
    }

    return totalDecodedBytes;
}


[[nodiscard]] size_t
decompressWithLibArchive( const std::vector<uint8_t>& compressedData )
{
    auto* const pArchive = archive_read_new();
    archive_read_support_filter_gzip( pArchive );
    archive_read_support_format_raw( pArchive );
    auto errorCode = archive_read_open_memory( pArchive, compressedData.data(), compressedData.size() );
    if ( errorCode != ARCHIVE_OK ) {
        throw std::runtime_error( "Could not initialize libarchive!" );
    }

    struct archive_entry *entry{ nullptr };
    errorCode = archive_read_next_header( pArchive, &entry );
    if ( errorCode != ARCHIVE_OK ) {
        throw std::runtime_error( "Could not read header with libarchive!" );
    }

    std::vector<uint8_t> outputBuffer( 64UL * 1024UL * 1024UL );
    size_t totalDecodedBytes = 0;
    while ( true )
    {
        const auto nBytesDecoded = archive_read_data( pArchive, outputBuffer.data(), outputBuffer.size() );
        if ( nBytesDecoded < 0 ) {
            throw std::runtime_error( "Reading with libarchive failed!" );
        }
        if ( nBytesDecoded == 0 ) {
            break;
        }
        totalDecodedBytes += nBytesDecoded;
    }

    archive_read_free( pArchive );
    return totalDecodedBytes;
}


[[nodiscard]] size_t
decompressWithPragzip( const std::string& fileName )
{
    using namespace pragzip;

    size_t totalDecodedBytes = 0;
    size_t blockCount = 0;

    GzipReader gzipReader( std::make_unique<StandardFileReader>( fileName ) );
    std::vector<uint8_t> outputBuffer( 64UL * 1024UL * 1024UL );
    while ( true ) {
        const auto nBytesRead = gzipReader.read( -1,
                                                 reinterpret_cast<char*>( outputBuffer.data() ),
                                                 outputBuffer.size(),
                                                 GzipReader::StoppingPoint::END_OF_BLOCK_HEADER );
        if ( ( nBytesRead == 0 ) && gzipReader.eof() ) {
            break;
        }

        const auto currentPoint = gzipReader.currentPoint();
        if ( currentPoint == GzipReader::StoppingPoint::END_OF_BLOCK_HEADER ) {
            blockCount++;
        }
        totalDecodedBytes += nBytesRead;
    }

    std::cerr << "Decoded " << blockCount << " deflate blocks\n";

    return totalDecodedBytes;
}


[[nodiscard]] size_t
decompressWithPragzipParallel( const std::string& fileName )
{
    using namespace pragzip;

    try
    {
        BgzfBlockFinder( std::make_unique<StandardFileReader>( fileName ) );
    } catch ( const std::invalid_argument& ) {
        /* Not a bgz file. */
        return 0;
    }

    size_t totalDecodedBytes = 0;

    ParallelGzipReader gzipReader( std::make_unique<StandardFileReader>( fileName ) );
    std::vector<uint8_t> outputBuffer( 64UL * 1024UL * 1024UL );
    while ( true ) {
        const auto nBytesRead = gzipReader.read( -1,
                                                 reinterpret_cast<char*>( outputBuffer.data() ),
                                                 outputBuffer.size() );
        if ( ( nBytesRead == 0 ) && gzipReader.eof() ) {
            break;
        }

        totalDecodedBytes += nBytesRead;
    }

    return totalDecodedBytes;
}


[[nodiscard]] std::pair<std::string, GzipIndex>
createGzipIndex( const std::string& fileName )
{
    /* Create index using indexed_gzip */
    const auto indexFile = fileName + ".index";
    const auto command =
        R"(python3 -c 'import indexed_gzip as ig; f = ig.IndexedGzipFile( ")"
        + std::string( fileName )
        + R"(" ); f.build_full_index(); f.export_index( ")"
        + std::string( indexFile )
        + R"(" );')";
    const auto returnCode = std::system( command.c_str() );
    REQUIRE( returnCode == 0 );
    if ( returnCode != 0 ) {
        throw std::runtime_error( "Failed to create index using indexed_gzip Python module" );
    }

    auto index = readGzipIndex( std::make_unique<StandardFileReader>( indexFile ) );
    return { fileName, index };
}


[[nodiscard]] size_t
decompressWithPragzipParallelIndex( const std::pair<std::string, GzipIndex>& files )
{
    const auto& [fileName, index] = files;

    size_t totalDecodedBytes = 0;

    ParallelGzipReader gzipReader( std::make_unique<StandardFileReader>( fileName ) );
    gzipReader.setBlockOffsets( index );
    std::vector<uint8_t> outputBuffer( 64UL * 1024UL * 1024UL );
    while ( true ) {
        const auto nBytesRead = gzipReader.read( -1,
                                                 reinterpret_cast<char*>( outputBuffer.data() ),
                                                 outputBuffer.size() );
        if ( ( nBytesRead == 0 ) && gzipReader.eof() ) {
            break;
        }

        totalDecodedBytes += nBytesRead;
    }

    return totalDecodedBytes;
}


void
benchmarkDecompression( const std::string& fileName )
{
    const auto fileContents = readFile( fileName );

    const auto printStats =
        [] ( const std::vector<double>& values )
        {
            const Statistics<double> statisics( values );
            std::stringstream result;
            result << statisics.min << " <= " << statisics.average() << " +- "
                   << statisics.standardDeviation() << " <= " << statisics.max;
            return result.str();
        };

    const auto printDurations =
        [&printStats, nBytesEncoded = fileContents.size()]
        ( const std::vector<double>& durations,
          size_t                     nBytesDecoded )
        {
            std::cout << "    Runtime / s: " << printStats( durations ) << "\n";

            std::vector<double> encodedBandwidths( durations.size() );
            std::transform( durations.begin(), durations.end(), encodedBandwidths.begin(),
                            [nBytesEncoded] ( auto duration ) {
                                return static_cast<double>( nBytesEncoded ) / 1e6 / duration; } );
            std::cout << "    Bandwidth on Encoded Data / (MB/s): " << printStats( encodedBandwidths ) << "\n";

            std::vector<double> decodedBandwidths( durations.size() );
            std::transform( durations.begin(), durations.end(), decodedBandwidths.begin(),
                            [nBytesDecoded] ( auto duration ) {
                                return static_cast<double>( nBytesDecoded ) / 1e6 / duration; } );
            std::cout << "    Bandwidth on Decoded Data / (MB/s): " << printStats( decodedBandwidths ) << "\n";
        };

    const auto [sizeLibArchive, durationsLibArchive] = benchmarkFunction(
        [&fileContents] () { return decompressWithLibArchive( fileContents ); } );
    std::cout << "Decompressed " << fileContents.size() << " B to " << sizeLibArchive << " B with libarchive:\n";
    printDurations( durationsLibArchive, sizeLibArchive );

    const auto [sizeZlib, durationsZlib] = benchmarkFunction( [&fileContents] () {
        return decompressWithZlib( fileContents ); } );
    if ( sizeZlib == sizeLibArchive ) {
        std::cout << "Decompressed " << fileContents.size() << " B to " << sizeZlib << " B with zlib:\n";
        printDurations( durationsZlib, sizeZlib );
    } else {
        std::cerr << "Decompressing with zlib decoded a different amount than libarchive!\n";
    }

    const auto [sizePragzip, durationsPragzip] = benchmarkFunction(
        [&fileName] () { return decompressWithPragzip( fileName ); } );
    if ( sizePragzip == sizeLibArchive ) {
        std::cout << "Decompressed " << fileContents.size() << " B to " << sizePragzip << " B "
                  << "with pragzip (serial):\n";
        printDurations( durationsPragzip, sizePragzip );
    } else {
        std::cerr << "Decompressing with pragzip (serial) decoded a different amount than libarchive!\n";
    }

    const auto [sizePragzipParallel, durationsPragzipParallel] = benchmarkFunction(
        [&fileName] () { return decompressWithPragzipParallel( fileName ); } );
    if ( sizePragzipParallel == sizeLibArchive ) {
        std::cout << "Decompressed " << fileContents.size() << " B to " << sizePragzipParallel << " B "
                  << "with pragzip (parallel):\n";
        printDurations( durationsPragzipParallel, sizePragzipParallel );
    } else {
        std::cerr << "Decompressing with pragzip (parallel) decoded a different amount than libarchive!\n";
    }

    const auto [sizePragzipParallelIndex, durationsPragzipParallelIndex] = benchmarkFunction(
        [&fileName] () { return createGzipIndex( fileName ); }, decompressWithPragzipParallelIndex );
    if ( sizePragzipParallelIndex == sizeLibArchive ) {
        std::cout << "Decompressed " << fileContents.size() << " B to " << sizePragzipParallelIndex << " B "
                  << "with pragzip (parallel):\n";
        printDurations( durationsPragzipParallelIndex, sizePragzipParallelIndex );
    } else {
        std::cerr << "Decompressing with pragzip (parallel + index) decoded a different amount than libarchive!\n";
    }
}


int
main( int    argc,
      char** argv )
{
    if ( argc != 2 ) {
        std::cerr << "Please specify a gzip-compressed test file!\n";
        return 1;
    }

    if ( !fileExists( argv[1] ) ) {
        std::cerr << "Could not find specified file: " << argv[1] << "\n";
        return 2;
    }
    const std::string fileName = argv[1];

    benchmarkDecompression( fileName );

    return 0;
}


/**
base64 /dev/urandom | head -c $(( 512*1024*1024 )) > small
gzip -k small

make benchmarkGzip && taskset 0x01 tests/benchmarkGzip small.gz

    Decompressed 416689498 B to 536870912 B with libarchive:
        Runtime / s: 2.01913 <= 2.04098 +- 0.0218508 <= 2.06283
        Bandwidth on Encoded Data / (MB/s): 201.999 <= 204.177 +- 2.18607 <= 206.371
        Bandwidth on Decoded Data / (MB/s): 260.26 <= 263.065 +- 2.81657 <= 265.893
    Decompressed 416689498 B to 536870912 B with zlib:
        Runtime / s: 2.23081 <= 2.26465 +- 0.0408244 <= 2.30999
        Bandwidth on Encoded Data / (MB/s): 180.386 <= 184.037 +- 3.29478 <= 186.788
        Bandwidth on Decoded Data / (MB/s): 232.412 <= 237.117 +- 4.24505 <= 240.662
    Decompressed 416689498 B to 536870912 B with pragzip (serial):
        Runtime / s: 4.83257 <= 4.88146 +- 0.0543243 <= 4.93994
        Bandwidth on Encoded Data / (MB/s): 84.3511 <= 85.3688 +- 0.947384 <= 86.2252
        Bandwidth on Decoded Data / (MB/s): 108.68 <= 109.991 +- 1.22063 <= 111.094
    Decompressed 416689498 B to 536870912 B with pragzip (parallel + index):
        Runtime / s: 0.428104 <= 0.440346 +- 0.0157462 <= 0.458109
        Bandwidth on Encoded Data / (MB/s): 909.586 <= 947.073 +- 33.3243 <= 973.338
        Bandwidth on Decoded Data / (MB/s): 1171.93 <= 1220.23 +- 42.9356 <= 1254.07

      ->  pragzip is more than twice as slow as zlib :/

time gzip -d -k -c small.gz | wc -c
    real  0m3.542s
  -> pragzip is ~28% slower than gzip 1.10. Maybe slower than the above benchmarks because of I/O?

bgzip -c small > small.bgz
make benchmarkGzip && tests/benchmarkGzip small.bgz

    Decompressed 415096389 B to 536870912 B with libarchive:
        Runtime / s: 1.86041 <= 1.86558 +- 0.00716625 <= 1.87376
        Bandwidth on Encoded Data / (MB/s): 221.532 <= 222.505 +- 0.853011 <= 223.121
        Bandwidth on Decoded Data / (MB/s): 286.521 <= 287.78 +- 1.10325 <= 288.577
    Decompressed 415096389 B to 536870912 B with zlib:
        Runtime / s: 2.07877 <= 2.11722 +- 0.0342438 <= 2.14442
        Bandwidth on Encoded Data / (MB/s): 193.571 <= 196.091 +- 3.19447 <= 199.684
        Bandwidth on Decoded Data / (MB/s): 250.358 <= 253.618 +- 4.13162 <= 258.264
    Decoded 8226 deflate blocks
    Decoded 8226 deflate blocks
    Decoded 8226 deflate blocks
    Decompressed 415096389 B to 536870912 B with pragzip (serial):
        Runtime / s: 4.29972 <= 4.31889 +- 0.0172939 <= 4.33332
        Bandwidth on Encoded Data / (MB/s): 95.7918 <= 96.1128 +- 0.385448 <= 96.5403
        Bandwidth on Decoded Data / (MB/s): 123.894 <= 124.309 +- 0.498525 <= 124.862
    Decompressed 415096389 B to 536870912 B with pragzip (parallel):
        Runtime / s: 0.503374 <= 0.535733 +- 0.0325509 <= 0.568472
        Bandwidth on Encoded Data / (MB/s): 730.196 <= 776.731 +- 47.2307 <= 824.628
        Bandwidth on Decoded Data / (MB/s): 944.41 <= 1004.6 +- 61.0866 <= 1066.54
    Decompressed 415096389 B to 536870912 B with pragzip (parallel + index):
        Runtime / s: 0.477427 <= 0.493329 +- 0.0150873 <= 0.507442
        Bandwidth on Encoded Data / (MB/s): 818.018 <= 841.948 +- 25.8988 <= 869.445
        Bandwidth on Decoded Data / (MB/s): 1057.99 <= 1088.95 +- 33.4966 <= 1124.51

     -> ~1 GB/s for the decompressed bandwidth with the parallel bgz decoder and when decoding with an
        existing index is already quite nice!

time gzip -d -k -c small.bgz | wc -c
    real  0m3.248s
  -> Interestingly, this is reproducibly faster than the .gz compressed one. Maybe different compression setting?

time bgzip --threads $( nproc ) -d -c small.bgz | wc -c
    real  0m0.208s
  -> Twice as fast as parallel pragzip

ls -la small.*gz
    415096389 small.bgz
    416689498 small.gz
  -> The .bgz file is even smaller!


base64 /dev/urandom | head -c $(( 4*1024*1024*1024 )) > large
gzip -k large
bgzip -c large > large.bgz
make benchmarkGzip && tests/benchmarkGzip large.bgz
    Decompressed 3320779389 B to 4294967296 B with libarchive:
        Runtime / s: 14.6176 <= 14.7676 +- 0.154315 <= 14.9259
        Bandwidth on Encoded Data / (MB/s): 222.485 <= 224.886 +- 2.34813 <= 227.177
        Bandwidth on Decoded Data / (MB/s): 287.753 <= 290.859 +- 3.03698 <= 293.823
    Decompressed 3320779389 B to 4294967296 B with zlib:
        Runtime / s: 16.7697 <= 16.7799 +- 0.00927483 <= 16.7878
        Bandwidth on Encoded Data / (MB/s): 197.809 <= 197.902 +- 0.109409 <= 198.023
        Bandwidth on Decoded Data / (MB/s): 255.839 <= 255.959 +- 0.141505 <= 256.115
    Decoded 65795 deflate blocks
    Decoded 65795 deflate blocks
    Decoded 65795 deflate blocks
    Decompressed 3320779389 B to 4294967296 B with pragzip (serial):
        Runtime / s: 34.4183 <= 34.4518 +- 0.0457913 <= 34.504
        Bandwidth on Encoded Data / (MB/s): 96.2434 <= 96.3893 +- 0.128028 <= 96.483
        Bandwidth on Decoded Data / (MB/s): 124.478 <= 124.666 +- 0.165587 <= 124.787
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel):
        Runtime / s: 4.19626 <= 4.31237 +- 0.186016 <= 4.52692
        Bandwidth on Encoded Data / (MB/s): 733.563 <= 770.993 +- 32.4577 <= 791.366
        Bandwidth on Decoded Data / (MB/s): 948.762 <= 997.172 +- 41.9795 <= 1023.52
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel + index):
        Runtime / s: 3.9355 <= 4.00016 +- 0.0684622 <= 4.07188
        Bandwidth on Encoded Data / (MB/s): 815.541 <= 830.324 +- 14.1757 <= 843.802
        Bandwidth on Decoded Data / (MB/s): 1054.79 <= 1073.91 +- 18.3343 <= 1091.34

time bgzip --threads $( nproc ) -d -c large.bgz | wc -c
    real  0m2.155s
  -> Twice as fast as parallel pragzip
*/

/**
sudo apt install gzip tabix pigz libarchive-dev zlib1g-dev isal
python3 -m pip install --user pgzip indexed_gzip

base64 /dev/urandom | head -c $(( 512*1024*1024 )) > small
gzip -k small
tar -cf small.tar small
gzip small.tar

python3 -c 'import indexed_gzip as igz; import time; t0 = time.time(); igz.open("small.gz").read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Decompression took 4.594 s

python3 -c 'import gzip; import time; t0 = time.time(); gzip.open("small.gz").read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Decompression took 3.069 s

time python3 -m pgzip -o - small.gz | wc -c
    416755811

    real	0m3.289s
    user	0m10.282s
    sys	0m0.633s

time archivemount small.tar.gz mountpoint/
    real	0m1.657s
    user	0m1.563s
    sys	0m0.070s

time ( fuse-archive small.tar.gz mountpoint && stat mountpoint &>/dev/null; )
    real	0m1.697s
    user	0m0.002s
    sys	0m0.015s

time gzip -d -k -c small.gz | wc -c
    536870912

    real	0m3.510s
    user	0m3.395s
    sys	0m0.283s

time pigz -d -k -c small.gz | wc -c
    536870912

    real	0m2.225s
    user	0m2.797s
    sys	0m0.524s

time bgzip -d -c small.gz | wc -c
    536870912

    real	0m2.548s
    user	0m2.411s
    sys	0m0.253s

time igzip -d -c small.gz | wc -c
    536870912

    real	0m1.523s
    user	0m1.307s
    sys	0m0.344s

time pugz small.gz | wc -c
    536870912

    real	0m2.372s
    user	0m2.273s
    sys	0m0.262s

time pugz -t 4 small.gz | wc -c
    using 4 threads for decompression (experimental)
    536870912

    real	0m0.975s
    user	0m2.345s
    sys	0m0.318s

time pugz -t $( nproc ) small.gz | wc -c
    using 24 threads for decompression (experimental)
    536870912

    real	0m0.985s
    user	0m2.828s
    sys	0m6.894s

cd zlib-ng/ && mkdir build && cd $_ && cmake .. && cmake --build . --config Release
cp minigzip ~/bin/minigzip-zlib-ng
time minigzip-zlib-ng -d -k -c small.gz | wc -c
    536870912

    real	0m1.903s
    user	0m1.743s
    sys	0m0.283s

cd libdeflate && make && cp gunzio ~/bin/gunzip-libdeflate
time gunzip-libdeflate -d -k -c small.gz | wc -c
    536870912

    real	0m1.841s
    user	0m1.508s
    sys	0m0.460s

time crc32 small
    474e5ffd

    real	0m0.510s
    user	0m0.421s
    sys	0m0.069s
*/


/**
Repeat benchmarks with tarred and gzipped Silesia corpus.
http://sun.aei.polsl.pl/~sdeor/index.php?page=silesia

python3 -c 'import indexed_gzip as igz; import time; t0 = time.time(); igz.open("small.gz").read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Decompression took 1.249 s

python3 -c 'import gzip; import time; t0 = time.time(); gzip.open("small.gz").read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Decompression took 0.908 s

time python3 -m pgzip -o - small.gz | wc -c
    67997404

    real	0m1.837s
    user	0m1.719s
    sys	0m0.128s

time archivemount small.tar.gz mountpoint/
    real	0m1.663s
    user	0m1.580s
    sys	0m0.053s

time ( fuse-archive small.tar.gz mountpoint && stat mountpoint &>/dev/null; )
    real	0m1.629s
    user	0m0.007s
    sys	0m0.008s

time gzip -d -k -c small.gz | wc -c
    211957760

    real	0m1.000s
    user	0m0.958s
    sys	0m0.102s

time pigz -d -k -c small.gz | wc -c
    211957760

    real	0m0.622s
    user	0m0.860s
    sys	0m0.176s

time bgzip -d -c small.gz | wc -c
    211957760

    real	0m0.700s
    user	0m0.667s
    sys	0m0.068s

time igzip -d -c small.gz | wc -c
    211957760

    real	0m0.357s
    user	0m0.299s
    sys	0m0.109s

time pugz small.gz | wc -c
    terminate called after throwing an instance of 'gzip_error'
      what():  INVALID_LITERAL
    0

    real	0m0.078s
    user	0m0.005s
    sys	0m0.006s

time minigzip-zlib-ng -d -k -c small.gz | wc -c
    211957760

    real	0m0.443s
    user	0m0.405s
    sys	0m0.081s

time gunzip-libdeflate -d -k -c small.gz | wc -c
    211957760

    real	0m0.403s
    user	0m0.276s
    sys	0m0.184s

time crc32 silesia.tar
    78e42bf0

    real	0m0.236s
    user	0m0.174s
    sys	0m0.043s


Rebenchmark different versions and options of indexed_gzip

python3 -m pip install --user pgzip indexed_gzip
python3 -c 'import indexed_gzip as igz; import time; t0 = time.time(); igz.open("small.gz").read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Decompression took 4.666 s

python3 -c 'import indexed_gzip as igz; import time; t0 = time.time(); igz.IndexedGzipFile("small.gz", spacing=16*1024**2).read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Decompression took 6.403 s

python3 -c 'import indexed_gzip as igz; import time; t0 = time.time(); igz.IndexedGzipFile("small.gz", spacing=2**30, readbuf_size=2**30, buffer_size=2**30).read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Floating point exception

python3 -c 'import indexed_gzip as igz; import time; t0 = time.time(); igz.open("small.gz", spacing=int(1*1024**2), readbuf_size=int(1*1024**2), buffer_size=int(1*1024**2)).read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Decompression took 4.286 s

python3 -c 'import indexed_gzip as igz; import time; t0 = time.time(); igz.open("small.gz", spacing=int(32*1024**2), readbuf_size=int(1*1024**2), buffer_size=int(1*1024**2)).read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Decompression took 7.407 s

python3 -c 'import indexed_gzip as igz; import time; t0 = time.time(); igz.open("small.gz", spacing=int(128*1024**2), readbuf_size=int(1024**2), buffer_size=int(128*1024**2)).read(); print(f"Decompression took {time.time() - t0:.3f} s")'
    Decompression took 13.290 s
*/
