
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

#define BENCHMARK_CHUNKING

#include <BitReader.hpp>
#include <common.hpp>
#include <filereader/Memory.hpp>
#include <filereader/Standard.hpp>
#include <FileUtils.hpp>
#include <pragzip.hpp>
#include <Statistics.hpp>


class GzipWrapper
{
public:
    static constexpr int WINDOW_SIZE = 32 * 1024;

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
                                                 StoppingPoint::END_OF_BLOCK_HEADER );
        if ( ( nBytesRead == 0 ) && gzipReader.eof() ) {
            break;
        }

        const auto currentPoint = gzipReader.currentPoint();
        if ( currentPoint == StoppingPoint::END_OF_BLOCK_HEADER ) {
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


[[nodiscard]] size_t
decompressWithPragzipParallelChunked( const std::string& fileName,
                                      const size_t       nBlocksToSkip )
{
    size_t totalDecodedBytes = 0;

    ParallelGzipReader gzipReader( std::make_unique<StandardFileReader>( fileName ), 0, nBlocksToSkip );
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
printBandwidths( const std::vector<double>& durations,
                 size_t                     nBytesEncoded,
                 size_t                     nBytesDecoded )
{
    std::cout << "    Runtime / s: " << Statistics<double>( durations ).formatAverageWithUncertainty( true ) << "\n";

    std::vector<double> encodedBandwidths( durations.size() );
    std::transform( durations.begin(), durations.end(), encodedBandwidths.begin(),
                    [nBytesEncoded] ( auto duration ) {
                        return static_cast<double>( nBytesEncoded ) / 1e6 / duration; } );
    std::cout << "    Bandwidth on Encoded Data / (MB/s): "
              << Statistics<double>( encodedBandwidths ).formatAverageWithUncertainty( true ) << "\n";

    std::vector<double> decodedBandwidths( durations.size() );
    std::transform( durations.begin(), durations.end(), decodedBandwidths.begin(),
                    [nBytesDecoded] ( auto duration ) {
                        return static_cast<double>( nBytesDecoded ) / 1e6 / duration; } );
    std::cout << "    Bandwidth on Decoded Data / (MB/s): "
              << Statistics<double>( decodedBandwidths ).formatAverageWithUncertainty( true ) << "\n";
};


void
benchmarkChunkedParallelDecompression( const std::string& fileName )
{
    const auto fileContents = readFile( fileName );

    std::cout << "\n== Benchmarking with pragzip in parallel with different decoding chunk sizes ==\n\n";

    const auto [sizeLibArchive, durationsLibArchive] = benchmarkFunction(
        [&fileContents] () { return decompressWithLibArchive( fileContents ); } );
    //std::cout << "Decompressed " << fileContents.size() << " B to " << sizeLibArchive << " B with libarchive:\n";
    //printBandwidths( durationsLibArchive, fileContents.size(), sizeLibArchive );

    const auto expectedSize = sizeLibArchive;

    for ( size_t nBlocksToSkip : { 0, 1, 2, 4, 8, 16, 24, 32, 64, 128 } ) {
        const auto [sizePragzipParallel, durationsPragzipParallel] = benchmarkFunction(
            [&fileName, nBlocksToSkip] () { return decompressWithPragzipParallelChunked( fileName, nBlocksToSkip ); } );
        if ( sizePragzipParallel == expectedSize ) {
            std::cout << "Decompressed " << fileContents.size() << " B to " << sizePragzipParallel << " B "
                      << "with pragzip (parallel, nBlocksToSkip=" << nBlocksToSkip << "):\n";
            printBandwidths( durationsPragzipParallel, fileContents.size(), sizePragzipParallel );
        } else {
            std::cerr << "Decompressing with pragzip (parallel, nBlocksToSkip=" << nBlocksToSkip
                      << ") decoded a different amount (" << sizePragzipParallel
                      << ") than libarchive (" << expectedSize << ")!\n";
        }
    }

    std::cout << "\n";
}


void
benchmarkDecompression( const std::string& fileName )
{
    const auto fileContents = readFile( fileName );

    const auto [sizeLibArchive, durationsLibArchive] = benchmarkFunction(
        [&fileContents] () { return decompressWithLibArchive( fileContents ); } );
    std::cout << "Decompressed " << fileContents.size() << " B to " << sizeLibArchive << " B with libarchive:\n";
    printBandwidths( durationsLibArchive, fileContents.size(), sizeLibArchive );

    const auto expectedSize = sizeLibArchive;

    const auto [sizeZlib, durationsZlib] = benchmarkFunction( [&fileContents] () {
        return decompressWithZlib( fileContents ); } );
    if ( sizeZlib == expectedSize ) {
        std::cout << "Decompressed " << fileContents.size() << " B to " << sizeZlib << " B with zlib:\n";
        printBandwidths( durationsZlib, fileContents.size(), sizeZlib );
    } else {
        std::cerr << "Decompressing with zlib decoded a different amount than libarchive!\n";
    }

    const auto [sizePragzip, durationsPragzip] = benchmarkFunction(
        [&fileName] () { return decompressWithPragzip( fileName ); } );
    if ( sizePragzip == expectedSize ) {
        std::cout << "Decompressed " << fileContents.size() << " B to " << sizePragzip << " B "
                  << "with pragzip (serial):\n";
        printBandwidths( durationsPragzip, fileContents.size(), sizePragzip );
    } else {
        std::cerr << "Decompressing with pragzip (serial) decoded a different amount than libarchive!\n";
    }

    const auto [sizePragzipParallel, durationsPragzipParallel] = benchmarkFunction(
        [&fileName] () { return decompressWithPragzipParallel( fileName ); } );
    if ( sizePragzipParallel == expectedSize ) {
        std::cout << "Decompressed " << fileContents.size() << " B to " << sizePragzipParallel << " B "
                  << "with pragzip (parallel):\n";
        printBandwidths( durationsPragzipParallel, fileContents.size(), sizePragzipParallel );
    } else {
        std::cerr << "Decompressing with pragzip (parallel) decoded a different amount (" << sizePragzipParallel
                  << ") than libarchive (" << expectedSize << ")!\n";
    }

    const auto [sizePragzipParallelIndex, durationsPragzipParallelIndex] = benchmarkFunction(
        [&fileName] () { return createGzipIndex( fileName ); }, decompressWithPragzipParallelIndex );
    if ( sizePragzipParallelIndex == expectedSize ) {
        std::cout << "Decompressed " << fileContents.size() << " B to " << sizePragzipParallelIndex << " B "
                  << "with pragzip (parallel + index):\n";
        printBandwidths( durationsPragzipParallelIndex, fileContents.size(), sizePragzipParallelIndex );
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

    if ( ( std::filesystem::path( fileName ).extension() == ".bgz" )
         || ( std::filesystem::path( fileName ).extension() == ".pigz" )
         || ( std::filesystem::path( fileName ).extension() == ".pgz" ) ) {
        benchmarkChunkedParallelDecompression( fileName );
    }

    benchmarkDecompression( fileName );

    return 0;
}


/**
base64 /dev/urandom | head -c $(( 512*1024*1024 )) > small
gzip -k small

make benchmarkGzip && benchmarks/benchmarkGzip small.gz

    Decompressed 416689498 B to 536870912 B with libarchive:
        Runtime / s: 1.988 <= 1.994 +- 0.005 <= 1.998
        Bandwidth on Encoded Data / (MB/s): 208.5 <= 209 +- 0.5 <= 209.6
        Bandwidth on Decoded Data / (MB/s): 268.7 <= 269.3 +- 0.7 <= 270
    Decompressed 416689498 B to 536870912 B with zlib:
        Runtime / s: 2.217 <= 2.22 +- 0.005 <= 2.227
        Bandwidth on Encoded Data / (MB/s): 187.1 <= 187.7 +- 0.5 <= 188
        Bandwidth on Decoded Data / (MB/s): 241.1 <= 241.8 +- 0.6 <= 242.2
    Decoded 13403 deflate blocks
    Decoded 13403 deflate blocks
    Decoded 13403 deflate blocks
    Decompressed 416689498 B to 536870912 B with pragzip (serial):
        Runtime / s: 3.65 <= 3.68 +- 0.03 <= 3.71
        Bandwidth on Encoded Data / (MB/s): 112.3 <= 113.1 +- 0.9 <= 114.2
        Bandwidth on Decoded Data / (MB/s): 144.7 <= 145.8 +- 1.2 <= 147.1
    Decompressed 416689498 B to 536870912 B with pragzip (parallel):
        Runtime / s: 3.98 <= 4.05 +- 0.08 <= 4.13
        Bandwidth on Encoded Data / (MB/s): 100.8 <= 102.8 +- 2 <= 104.7
        Bandwidth on Decoded Data / (MB/s): 129.9 <= 132.4 +- 2.5 <= 134.9
    Decompressed 416689498 B to 536870912 B with pragzip (parallel + index):
        Runtime / s: 0.31 <= 0.35 +- 0.05 <= 0.4
        Bandwidth on Encoded Data / (MB/s): 1040 <= 1200 +- 150 <= 1320
        Bandwidth on Decoded Data / (MB/s): 1340 <= 1550 +- 190 <= 1710

      ->  pragzip is almost twice as slow as zlib :/

time gzip -d -k -c small.gz | wc -c
    real  0m3.542s
  -> pragzip is ~28% slower than gzip 1.10. Maybe slower than the above benchmarks because of I/O?

pigz -c small > small.pigz
make benchmarkGzip && benchmarks/benchmarkGzip small.pigz

    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=0):
        Runtime / s: 0.8306 <= 0.8319 +- 0.0016 <= 0.8337
        Bandwidth on Encoded Data / (MB/s): 489.9 <= 491 +- 1 <= 491.7
        Bandwidth on Decoded Data / (MB/s): 643.9 <= 645.3 +- 1.3 <= 646.4
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=1):
        Runtime / s: 0.57 <= 0.61 +- 0.04 <= 0.65
        Bandwidth on Encoded Data / (MB/s): 630 <= 680 +- 50 <= 710
        Bandwidth on Decoded Data / (MB/s): 820 <= 890 +- 60 <= 940
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=2):
        Runtime / s: 0.526 <= 0.544 +- 0.017 <= 0.56
        Bandwidth on Encoded Data / (MB/s): 729 <= 751 +- 24 <= 776
        Bandwidth on Decoded Data / (MB/s): 960 <= 990 +- 30 <= 1020
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=4):
        Runtime / s: 0.462 <= 0.466 +- 0.006 <= 0.472
        Bandwidth on Encoded Data / (MB/s): 865 <= 877 +- 11 <= 884
        Bandwidth on Decoded Data / (MB/s): 1137 <= 1152 +- 14 <= 1162
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=8):
        Runtime / s: 0.479 <= 0.484 +- 0.006 <= 0.491
        Bandwidth on Encoded Data / (MB/s): 831 <= 844 +- 11 <= 853
        Bandwidth on Decoded Data / (MB/s): 1093 <= 1109 +- 15 <= 1122
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=16):
        Runtime / s: 0.43 <= 0.48 +- 0.06 <= 0.55
        Bandwidth on Encoded Data / (MB/s): 750 <= 860 +- 110 <= 960
        Bandwidth on Decoded Data / (MB/s): 980 <= 1130 +- 140 <= 1260
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=24):
        Runtime / s: 0.47 <= 0.479 +- 0.008 <= 0.486
        Bandwidth on Encoded Data / (MB/s): 840 <= 853 +- 15 <= 869
        Bandwidth on Decoded Data / (MB/s): 1104 <= 1121 +- 20 <= 1142
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=32):
        Runtime / s: 0.459 <= 0.478 +- 0.02 <= 0.499
        Bandwidth on Encoded Data / (MB/s): 820 <= 850 +- 40 <= 890
        Bandwidth on Decoded Data / (MB/s): 1080 <= 1120 +- 50 <= 1170
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=64):
        Runtime / s: 0.518 <= 0.528 +- 0.013 <= 0.543
        Bandwidth on Encoded Data / (MB/s): 752 <= 774 +- 19 <= 788
        Bandwidth on Decoded Data / (MB/s): 989 <= 1017 +- 25 <= 1036
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=128):
        Runtime / s: 0.581 <= 0.593 +- 0.01 <= 0.599
        Bandwidth on Encoded Data / (MB/s): 682 <= 689 +- 12 <= 702
        Bandwidth on Decoded Data / (MB/s): 896 <= 906 +- 15 <= 923

  -> 16 blocks as chunks are the fastest!, starting from 4, it seems to be somewhat saturated already.
     -> pragzip --analyze small.pigz
        - There are 37796 blocks in total
        - Each block is ~ 12800 B encoded and ~16900 B decoded,
        - Flush marker blocks (byte-aligning uncompressed blocks of size 0) appear very irregularly:
          - 25, 61, 88, 97, 196, 277, 286, 304, 342, 361, 370, 379, 399, 408, 425, 434, 475, 484, 503, 522, 531, 548,
            557, 566, 575, 584, 593, 602, 641, 660, 680, 689, 723, 732, 752, 769, 778, 787, 826, 835, 844, 864, 873, ...
            So yeah, anywhere from 9 to 99 blocks! Really might be better if I skip based on encoded data size
          - pragzip --analyze small.pigz | grep ': Uncompressed' | wc -l
            In total there are 2114 flush markers, so on average, after 17.88 blocks, i.e.,
            17.88 * 12800 B * 16 (flush markers to skip) = 3.5 MiB of encoded data and 4.61 MiB of decoded data.

Decompressed 408430549 B to 536870912 B with libarchive:
        Runtime / s: 1.656 <= 1.661 +- 0.005 <= 1.665
        Bandwidth on Encoded Data / (MB/s): 245.3 <= 245.8 +- 0.7 <= 246.6
        Bandwidth on Decoded Data / (MB/s): 322.4 <= 323.1 +- 0.9 <= 324.1
    Decompressed 408430549 B to 536870912 B with zlib:
        Runtime / s: 1.969 <= 1.981 +- 0.013 <= 1.995
        Bandwidth on Encoded Data / (MB/s): 204.7 <= 206.2 +- 1.4 <= 207.4
        Bandwidth on Decoded Data / (MB/s): 269.1 <= 271 +- 1.8 <= 272.6
    Decoded 37796 deflate blocks
    Decoded 37796 deflate blocks
    Decoded 37796 deflate blocks
    Decompressed 408430549 B to 536870912 B with pragzip (serial):
        Runtime / s: 5.197 <= 5.229 +- 0.029 <= 5.251
        Bandwidth on Encoded Data / (MB/s): 77.8 <= 78.1 +- 0.4 <= 78.6
        Bandwidth on Decoded Data / (MB/s): 102.2 <= 102.7 +- 0.6 <= 103.3
    Decompressed 408430549 B to 536870912 B with pragzip (parallel):
        Runtime / s: 0.4359 <= 0.4386 +- 0.0028 <= 0.4415
        Bandwidth on Encoded Data / (MB/s): 925 <= 931 +- 6 <= 937
        Bandwidth on Decoded Data / (MB/s): 1216 <= 1224 +- 8 <= 1232
    Decompressed 408430549 B to 536870912 B with pragzip (parallel + index):
        Runtime / s: 0.388 <= 0.397 +- 0.014 <= 0.413
        Bandwidth on Encoded Data / (MB/s): 990 <= 1030 +- 40 <= 1050
        Bandwidth on Decoded Data / (MB/s): 1300 <= 1350 +- 50 <= 1390


bgzip -c small > small.bgz
make benchmarkGzip && benchmarks/benchmarkGzip small.bgz

    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=0):
        Runtime / s: 0.494 <= 0.503 +- 0.014 <= 0.52
        Bandwidth on Encoded Data / (MB/s): 799 <= 826 +- 23 <= 841
        Bandwidth on Decoded Data / (MB/s): 1030 <= 1070 +- 30 <= 1090
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=1):
        Runtime / s: 0.371 <= 0.38 +- 0.008 <= 0.388
        Bandwidth on Encoded Data / (MB/s): 1070 <= 1093 +- 24 <= 1119
        Bandwidth on Decoded Data / (MB/s): 1380 <= 1410 +- 30 <= 1450
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=2):
        Runtime / s: 0.357 <= 0.369 +- 0.012 <= 0.382
        Bandwidth on Encoded Data / (MB/s): 1090 <= 1130 +- 40 <= 1160
        Bandwidth on Decoded Data / (MB/s): 1410 <= 1460 +- 50 <= 1500
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=4):
        Runtime / s: 0.316 <= 0.326 +- 0.016 <= 0.344
        Bandwidth on Encoded Data / (MB/s): 1210 <= 1280 +- 60 <= 1310
        Bandwidth on Decoded Data / (MB/s): 1560 <= 1650 +- 80 <= 1700
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=8):
        Runtime / s: 0.3184 <= 0.3195 +- 0.0018 <= 0.3215
        Bandwidth on Encoded Data / (MB/s): 1291 <= 1299 +- 7 <= 1304
        Bandwidth on Decoded Data / (MB/s): 1670 <= 1680 +- 9 <= 1686
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=16):
        Runtime / s: 0.291 <= 0.307 +- 0.019 <= 0.329
        Bandwidth on Encoded Data / (MB/s): 1260 <= 1360 +- 80 <= 1430
        Bandwidth on Decoded Data / (MB/s): 1630 <= 1760 +- 110 <= 1850
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=24):
        Runtime / s: 0.275 <= 0.285 +- 0.012 <= 0.298
        Bandwidth on Encoded Data / (MB/s): 1390 <= 1460 +- 60 <= 1510
        Bandwidth on Decoded Data / (MB/s): 1800 <= 1890 +- 80 <= 1950
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=32):
        Runtime / s: 0.277 <= 0.281 +- 0.004 <= 0.286
        Bandwidth on Encoded Data / (MB/s): 1454 <= 1477 +- 22 <= 1497
        Bandwidth on Decoded Data / (MB/s): 1880 <= 1910 +- 28 <= 1936
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=64):
        Runtime / s: 0.31 <= 0.325 +- 0.015 <= 0.341
        Bandwidth on Encoded Data / (MB/s): 1220 <= 1280 +- 60 <= 1340
        Bandwidth on Decoded Data / (MB/s): 1570 <= 1660 +- 80 <= 1730
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=128):
        Runtime / s: 0.305 <= 0.311 +- 0.01 <= 0.323
        Bandwidth on Encoded Data / (MB/s): 1280 <= 1330 +- 40 <= 1360
        Bandwidth on Decoded Data / (MB/s): 1660 <= 1730 +- 60 <= 1760

  -> 32 blocks for bgz would be the best in this case! But starting from 16, it feels already saturated.
     -> pragzip --analyze small.bgz
        - There are 8226 blocks and gzip streams in total
        - Each block is ~ 50400 B encoded and 65280 B (63.75 KiB) decoded,
        - So, when skipping ~32 blocks (~33 chunked), we have ~1.54 MiB encoded and ~2 MiB decoded data
          -> This is only a factor 2 less than the optimum for pigz, so that we might use the same blocking criterium,
             or not.

    Decompressed 415096389 B to 536870912 B with libarchive:
        Runtime / s: 1.874 <= 1.877 +- 0.004 <= 1.882
        Bandwidth on Encoded Data / (MB/s): 220.6 <= 221.1 +- 0.5 <= 221.5
        Bandwidth on Decoded Data / (MB/s): 285.3 <= 286 +- 0.7 <= 286.5
    Decompressed 415096389 B to 536870912 B with zlib:
        Runtime / s: 2.076 <= 2.087 +- 0.011 <= 2.098
        Bandwidth on Encoded Data / (MB/s): 197.9 <= 198.9 +- 1.1 <= 200
        Bandwidth on Decoded Data / (MB/s): 255.9 <= 257.3 +- 1.4 <= 258.6
    Decoded 8226 deflate blocks
    Decoded 8226 deflate blocks
    Decoded 8226 deflate blocks
    Decompressed 415096389 B to 536870912 B with pragzip (serial):
        Runtime / s: 3.185 <= 3.202 +- 0.015 <= 3.213
        Bandwidth on Encoded Data / (MB/s): 129.2 <= 129.6 +- 0.6 <= 130.3
        Bandwidth on Decoded Data / (MB/s): 167.1 <= 167.7 +- 0.8 <= 168.5
    Decompressed 415096389 B to 536870912 B with pragzip (parallel):
        Runtime / s: 0.281 <= 0.287 +- 0.008 <= 0.296
        Bandwidth on Encoded Data / (MB/s): 1400 <= 1450 +- 40 <= 1480
        Bandwidth on Decoded Data / (MB/s): 1810 <= 1870 +- 50 <= 1910
    Decompressed 415096389 B to 536870912 B with pragzip (parallel + index):
        Runtime / s: 0.42 <= 0.45 +- 0.04 <= 0.49
        Bandwidth on Encoded Data / (MB/s): 850 <= 920 +- 70 <= 990
        Bandwidth on Decoded Data / (MB/s): 1090 <= 1190 +- 90 <= 1280

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
make benchmarkGzip && benchmarks/benchmarkGzip large.bgz

    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=0):
        Runtime / s: 3.82 <= 3.92 +- 0.1 <= 4.03
        Bandwidth on Encoded Data / (MB/s): 825 <= 847 +- 23 <= 870
        Bandwidth on Decoded Data / (MB/s): 1067 <= 1095 +- 29 <= 1125
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=1):
        Runtime / s: 3.105 <= 3.122 +- 0.015 <= 3.134
        Bandwidth on Encoded Data / (MB/s): 1060 <= 1064 +- 5 <= 1070
        Bandwidth on Decoded Data / (MB/s): 1370 <= 1376 +- 7 <= 1383
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=2):
        Runtime / s: 2.84 <= 2.98 +- 0.21 <= 3.22
        Bandwidth on Encoded Data / (MB/s): 1030 <= 1120 +- 80 <= 1170
        Bandwidth on Decoded Data / (MB/s): 1330 <= 1450 +- 100 <= 1510
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=4):
        Runtime / s: 2.62 <= 2.66 +- 0.05 <= 2.71
        Bandwidth on Encoded Data / (MB/s): 1225 <= 1249 +- 22 <= 1270
        Bandwidth on Decoded Data / (MB/s): 1585 <= 1615 +- 29 <= 1642
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=8):
        Runtime / s: 2.313 <= 2.33 +- 0.019 <= 2.35
        Bandwidth on Encoded Data / (MB/s): 1413 <= 1425 +- 11 <= 1436
        Bandwidth on Decoded Data / (MB/s): 1828 <= 1843 +- 15 <= 1857
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=16):
        Runtime / s: 2.15 <= 2.27 +- 0.13 <= 2.4
        Bandwidth on Encoded Data / (MB/s): 1380 <= 1470 +- 80 <= 1550
        Bandwidth on Decoded Data / (MB/s): 1790 <= 1900 +- 110 <= 2000
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=24):
        Runtime / s: 2.02 <= 2.16 +- 0.16 <= 2.33
        Bandwidth on Encoded Data / (MB/s): 1420 <= 1550 +- 110 <= 1640
        Bandwidth on Decoded Data / (MB/s): 1840 <= 2000 +- 140 <= 2120
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=32):
        Runtime / s: 2 <= 2.07 +- 0.06 <= 2.11
        Bandwidth on Encoded Data / (MB/s): 1570 <= 1610 +- 50 <= 1660
        Bandwidth on Decoded Data / (MB/s): 2030 <= 2080 +- 70 <= 2150
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=64):
        Runtime / s: 2 <= 2.05 +- 0.05 <= 2.1
        Bandwidth on Encoded Data / (MB/s): 1580 <= 1620 +- 40 <= 1660
        Bandwidth on Decoded Data / (MB/s): 2040 <= 2100 +- 50 <= 2150
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=128):
        Runtime / s: 1.98 <= 2.04 +- 0.05 <= 2.08
        Bandwidth on Encoded Data / (MB/s): 1600 <= 1630 +- 40 <= 1670
        Bandwidth on Decoded Data / (MB/s): 2070 <= 2110 +- 50 <= 2160

  -> I don't see the peak yet! I guess the benchmark with the 512 MiB base64 file only topped out at 16 blocks
     per chunk because then the tail might produce stragglers!
     -> So we are fine using a chunking criterium shared with pigz of roughly 4 MiB of encoded data or maybe more.
        -> Note that for generic gzip files, we would need roughly 32 MiB of encoded data because of the much slower
           block finder amortizing slower!

    Decompressed 3320779389 B to 4294967296 B with libarchive:
        Runtime / s: 14.76 <= 14.81 +- 0.05 <= 14.86
        Bandwidth on Encoded Data / (MB/s): 223.5 <= 224.2 +- 0.7 <= 225
        Bandwidth on Decoded Data / (MB/s): 289.1 <= 289.9 +- 1 <= 291
    Decompressed 3320779389 B to 4294967296 B with zlib:
        Runtime / s: 16.89 <= 17.05 +- 0.14 <= 17.15
        Bandwidth on Encoded Data / (MB/s): 193.7 <= 194.8 +- 1.6 <= 196.6
        Bandwidth on Decoded Data / (MB/s): 250.5 <= 252 +- 2.1 <= 254.3
    Decoded 65795 deflate blocks
    Decoded 65795 deflate blocks
    Decoded 65795 deflate blocks
    Decompressed 3320779389 B to 4294967296 B with pragzip (serial):
        Runtime / s: 26.03 <= 26.2 +- 0.24 <= 26.47
        Bandwidth on Encoded Data / (MB/s): 125.4 <= 126.7 +- 1.1 <= 127.6
        Bandwidth on Decoded Data / (MB/s): 162.2 <= 163.9 +- 1.5 <= 165
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel):
        Runtime / s: 2.058 <= 2.082 +- 0.025 <= 2.107
        Bandwidth on Encoded Data / (MB/s): 1576 <= 1595 +- 19 <= 1613
        Bandwidth on Decoded Data / (MB/s): 2038 <= 2063 +- 24 <= 2087
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel + index):
        Runtime / s: 3.2 <= 3.29 +- 0.1 <= 3.39
        Bandwidth on Encoded Data / (MB/s): 980 <= 1010 +- 30 <= 1040
        Bandwidth on Decoded Data / (MB/s): 1270 <= 1300 +- 40 <= 1340

time bgzip --threads $( nproc ) -d -c large.bgz | wc -c
    real  0m2.155s
  -> Twice as fast as parallel pragzip


base64 /dev/urandom | head -c $(( 1024*1024*1024 )) > large
pigz -k -c large > large.pigz
make benchmarkGzip && benchmarks/benchmarkGzip large.pigz

    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=0):
        Runtime / s: 1.54 <= 1.59 +- 0.05 <= 1.64
        Bandwidth on Encoded Data / (MB/s): 498 <= 513 +- 16 <= 530
        Bandwidth on Decoded Data / (MB/s): 655 <= 674 +- 21 <= 696
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=1):
        Runtime / s: 1.084 <= 1.103 +- 0.022 <= 1.127
        Bandwidth on Encoded Data / (MB/s): 725 <= 740 +- 15 <= 753
        Bandwidth on Decoded Data / (MB/s): 953 <= 973 +- 19 <= 990
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=2):
        Runtime / s: 0.965 <= 0.979 +- 0.02 <= 1.002
        Bandwidth on Encoded Data / (MB/s): 815 <= 834 +- 17 <= 847
        Bandwidth on Decoded Data / (MB/s): 1071 <= 1097 +- 22 <= 1113
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=4):
        Runtime / s: 0.877 <= 0.895 +- 0.021 <= 0.917
        Bandwidth on Encoded Data / (MB/s): 890 <= 913 +- 21 <= 931
        Bandwidth on Decoded Data / (MB/s): 1170 <= 1200 +- 27 <= 1224
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=8):
        Runtime / s: 0.808 <= 0.822 +- 0.013 <= 0.835
        Bandwidth on Encoded Data / (MB/s): 978 <= 994 +- 16 <= 1011
        Bandwidth on Decoded Data / (MB/s): 1286 <= 1307 +- 21 <= 1328
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=16):
        Runtime / s: 0.772 <= 0.779 +- 0.008 <= 0.788
        Bandwidth on Encoded Data / (MB/s): 1037 <= 1048 +- 11 <= 1059
        Bandwidth on Decoded Data / (MB/s): 1363 <= 1378 +- 14 <= 1392
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=24):
        Runtime / s: 0.77 <= 0.778 +- 0.01 <= 0.79
        Bandwidth on Encoded Data / (MB/s): 1034 <= 1050 +- 14 <= 1061
        Bandwidth on Decoded Data / (MB/s): 1359 <= 1380 +- 18 <= 1395
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=32):
        Runtime / s: 0.775 <= 0.791 +- 0.017 <= 0.81
        Bandwidth on Encoded Data / (MB/s): 1009 <= 1033 +- 23 <= 1054
        Bandwidth on Decoded Data / (MB/s): 1326 <= 1358 +- 30 <= 1385
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=64):
        Runtime / s: 0.884 <= 0.904 +- 0.023 <= 0.929
        Bandwidth on Encoded Data / (MB/s): 879 <= 904 +- 23 <= 924
        Bandwidth on Decoded Data / (MB/s): 1156 <= 1188 +- 30 <= 1215
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=128):
        Runtime / s: 0.983 <= 1.006 +- 0.025 <= 1.033
        Bandwidth on Encoded Data / (MB/s): 791 <= 812 +- 20 <= 831
        Bandwidth on Decoded Data / (MB/s): 1039 <= 1068 +- 27 <= 1092

    Decompressed 816860634 B to 1073741824 B with libarchive:
        Runtime / s: 3.274 <= 3.281 +- 0.008 <= 3.289
        Bandwidth on Encoded Data / (MB/s): 248.4 <= 249 +- 0.6 <= 249.5
        Bandwidth on Decoded Data / (MB/s): 326.5 <= 327.2 +- 0.8 <= 328
    Decompressed 816860634 B to 1073741824 B with zlib:
        Runtime / s: 3.87 <= 3.92 +- 0.04 <= 3.95
        Bandwidth on Encoded Data / (MB/s): 206.8 <= 208.3 +- 2.2 <= 210.8
        Bandwidth on Decoded Data / (MB/s): 271.8 <= 273.8 +- 2.9 <= 277.2
    Decoded 75802 deflate blocks
    Decoded 75802 deflate blocks
    Decoded 75802 deflate blocks
    Decompressed 816860634 B to 1073741824 B with pragzip (serial):
        Runtime / s: 10.33 <= 10.4 +- 0.07 <= 10.48
        Bandwidth on Encoded Data / (MB/s): 78 <= 78.6 +- 0.6 <= 79.1
        Bandwidth on Decoded Data / (MB/s): 102.5 <= 103.3 +- 0.7 <= 103.9
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel):
        Runtime / s: 0.7809 <= 0.7829 +- 0.002 <= 0.7849
        Bandwidth on Encoded Data / (MB/s): 1040.7 <= 1043.4 +- 2.7 <= 1046.1
        Bandwidth on Decoded Data / (MB/s): 1368 <= 1372 +- 4 <= 1375
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel + index):
        Runtime / s: 0.708 <= 0.72 +- 0.016 <= 0.738
        Bandwidth on Encoded Data / (MB/s): 1107 <= 1135 +- 24 <= 1154
        Bandwidth on Decoded Data / (MB/s): 1460 <= 1490 +- 30 <= 1520

  -> Pragzip has abysmal serial decoder speed, probably because pigz deflate blocks are only 16 KiB so that the
     upfront cost for the double-symbol cached Huffman-decoder becomes expensive.
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


/*
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


/*
git clone https://github.com/mxmlnkn/indexed_bzip2
cd indexed_bzip2 && mkdir build && cd build && cmake ..
sudo apt install pigz isal tabix

# Create a compressible random file
base64 /dev/urandom | head -c $(( 512 * 1024 * 1024 )) > small
pigz  -k -c small > small.pigz
bgzip    -c small > small.bgz
gzip  -k -c small > small.gz
igzip -k -c small > small.igz

fileSize=$( stat --format=%s small )
for format in gz bgz pigz igz; do
    printf '\n| %6s | %19s | %10s | %18s |\n' Format Decoder 'Runtime / s' 'Bandwidth / (MB/s)'
    printf -- '|--------|---------------------|-------------|--------------------|\n'
    for tool in gzip bgzip "bgzip -@ $( nproc )" pigz igzip "igzip -T $( nproc )" tools/pragzip "tools/pragzip -P $( nproc )"; do
        runtime=$( ( time $tool -d -c "small.$format" | wc -c ) 2>&1 | sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
        bandwidth=$( python3 -c "print( int( round( $fileSize / 1e6 / $runtime ) ) )" )
        printf '| %6s | %19s | %11s | %18s |\n' "$format" "$tool" "$runtime" "$bandwidth"
    done
done


| Format |             Decoder | Runtime / s | Bandwidth / (MB/s) |
|--------|---------------------|-------------|--------------------|
|     gz |                gzip |       2.948 |                182 |
|     gz |               bgzip |       2.137 |                251 |
|     gz |         bgzip -@ 24 |       2.128 |                252 |
|     gz |                pigz |       1.737 |                309 |
|     gz |               igzip |       1.178 |                456 |
|     gz |         igzip -T 24 |       1.163 |                462 |
|     gz |       tools/pragzip |       3.598 |                149 |
|     gz | tools/pragzip -P 24 |       3.909 |                137 |

| Format |             Decoder | Runtime / s | Bandwidth / (MB/s) |
|--------|---------------------|-------------|--------------------|
|    bgz |                gzip |       3.303 |                163 |
|    bgz |               bgzip |       1.592 |                337 |
|    bgz |         bgzip -@ 24 |       0.193 |               2782 |
|    bgz |                pigz |       2.723 |                197 |
|    bgz |               igzip |       1.192 |                450 |
|    bgz |         igzip -T 24 |       1.216 |                442 |
|    bgz |       tools/pragzip |       3.296 |                163 |
|    bgz | tools/pragzip -P 24 |       0.375 |               1432 |

| Format |             Decoder | Runtime / s | Bandwidth / (MB/s) |
|--------|---------------------|-------------|--------------------|
|   pigz |                gzip |       3.514 |                153 |
|   pigz |               bgzip |       2.153 |                249 |
|   pigz |         bgzip -@ 24 |       2.148 |                250 |
|   pigz |                pigz |       1.773 |                303 |
|   pigz |               igzip |       1.247 |                431 |
|   pigz |         igzip -T 24 |       1.336 |                402 |
|   pigz |       tools/pragzip |       5.241 |                102 |
|   pigz | tools/pragzip -P 24 |       0.496 |               1082 |

| Format |             Decoder | Runtime / s | Bandwidth / (MB/s) |
|--------|---------------------|-------------|--------------------|
|    igz |                gzip |       2.793 |                192 |
|    igz |               bgzip |       1.958 |                274 |
|    igz |         bgzip -@ 24 |       2.004 |                268 |
|    igz |                pigz |       1.593 |                337 |
|    igz |               igzip |       1.019 |                527 |
|    igz |         igzip -T 24 |       1.065 |                504 |
|    igz |       tools/pragzip |       2.450 |                219 |
|    igz | tools/pragzip -P 24 |       2.626 |                204 |
*/
