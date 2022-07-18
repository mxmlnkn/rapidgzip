
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

    /* These are implicitly deleted anyway because of the const member but clang-tidy keeps complaining without this. */
    GzipWrapper( const GzipWrapper& ) = delete;
    GzipWrapper( GzipWrapper&& ) = delete;
    GzipWrapper& operator=( GzipWrapper&& ) = delete;
    GzipWrapper& operator=( GzipWrapper& ) = delete;

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
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
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
    std::vector<uint8_t> contents( std::filesystem::file_size( fileName ) );
    const auto file = throwingOpen( fileName, "rb" );
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

make benchmarkGzip && src/benchmarks/benchmarkGzip small.gz

    Decompressed 407988639 B to 536870912 B with libarchive:
        Runtime / s: 1.58 <= 1.6 +- 0.03 <= 1.64
        Bandwidth on Encoded Data / (MB/s): 248 <= 255 +- 5 <= 258
        Bandwidth on Decoded Data / (MB/s): 327 <= 335 +- 7 <= 339
    Decompressed 407988639 B to 536870912 B with zlib:
        Runtime / s: 1.8735 <= 1.8748 +- 0.0021 <= 1.8771
        Bandwidth on Encoded Data / (MB/s): 217.35 <= 217.62 +- 0.24 <= 217.76
        Bandwidth on Decoded Data / (MB/s): 286 <= 286.4 +- 0.3 <= 286.6
    Decoded 15844 deflate blocks
    Decoded 15844 deflate blocks
    Decoded 15844 deflate blocks
    Decompressed 407988639 B to 536870912 B with pragzip (serial):
        Runtime / s: 3.4 <= 3.42 +- 0.03 <= 3.46
        Bandwidth on Encoded Data / (MB/s): 118.1 <= 119.3 +- 1.1 <= 120.1
        Bandwidth on Decoded Data / (MB/s): 155.4 <= 157 +- 1.4 <= 158.1
    Decompressed 407988639 B to 536870912 B with pragzip (parallel):
        Runtime / s: 3.57 <= 3.64 +- 0.06 <= 3.68
        Bandwidth on Encoded Data / (MB/s): 110.8 <= 112.2 +- 1.8 <= 114.2
        Bandwidth on Decoded Data / (MB/s): 145.8 <= 147.6 +- 2.3 <= 150.2
    Decompressed 407988639 B to 536870912 B with pragzip (parallel + index):
        Runtime / s: 0.211 <= 0.216 +- 0.004 <= 0.219
        Bandwidth on Encoded Data / (MB/s): 1860 <= 1890 +- 40 <= 1930
        Bandwidth on Decoded Data / (MB/s): 2450 <= 2490 +- 50 <= 2540

      -> pragzip is almost twice as slow as zlib :/
         -> Decoding with pragzip in parallel with an index is now much slower because of internal zlib usage!

time gzip -d -k -c small.gz | wc -c
    real  0m2.830s
  -> pragzip is ~28% slower than gzip 1.10. Maybe slower than the above benchmarks because of I/O?

pigz -c small > small.pigz
make benchmarkGzip && src/benchmarks/benchmarkGzip small.pigz

    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=0):
        Runtime / s: 0.77 <= 0.81 +- 0.04 <= 0.83
        Bandwidth on Encoded Data / (MB/s): 491 <= 505 +- 24 <= 533
        Bandwidth on Decoded Data / (MB/s): 640 <= 660 +- 30 <= 700
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=1):
        Runtime / s: 0.566 <= 0.573 +- 0.01 <= 0.585
        Bandwidth on Encoded Data / (MB/s): 698 <= 713 +- 13 <= 721
        Bandwidth on Decoded Data / (MB/s): 918 <= 937 +- 17 <= 948
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=2):
        Runtime / s: 0.529 <= 0.541 +- 0.01 <= 0.549
        Bandwidth on Encoded Data / (MB/s): 744 <= 755 +- 15 <= 772
        Bandwidth on Decoded Data / (MB/s): 978 <= 993 +- 19 <= 1015
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=4):
        Runtime / s: 0.454 <= 0.463 +- 0.008 <= 0.469
        Bandwidth on Encoded Data / (MB/s): 872 <= 882 +- 15 <= 899
        Bandwidth on Decoded Data / (MB/s): 1146 <= 1159 +- 19 <= 1181
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=8):
        Runtime / s: 0.445 <= 0.466 +- 0.02 <= 0.485
        Bandwidth on Encoded Data / (MB/s): 840 <= 880 +- 40 <= 920
        Bandwidth on Decoded Data / (MB/s): 1110 <= 1150 +- 50 <= 1210
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=16):
        Runtime / s: 0.478 <= 0.482 +- 0.004 <= 0.486
        Bandwidth on Encoded Data / (MB/s): 840 <= 847 +- 7 <= 854
        Bandwidth on Decoded Data / (MB/s): 1104 <= 1113 +- 9 <= 1123
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=24):
        Runtime / s: 0.49 <= 0.53 +- 0.03 <= 0.55
        Bandwidth on Encoded Data / (MB/s): 740 <= 770 +- 50 <= 830
        Bandwidth on Decoded Data / (MB/s): 970 <= 1010 +- 70 <= 1090
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=32):
        Runtime / s: 0.527 <= 0.537 +- 0.009 <= 0.545
        Bandwidth on Encoded Data / (MB/s): 749 <= 761 +- 13 <= 775
        Bandwidth on Decoded Data / (MB/s): 984 <= 1000 +- 17 <= 1019
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=64):
        Runtime / s: 0.643 <= 0.659 +- 0.015 <= 0.672
        Bandwidth on Encoded Data / (MB/s): 608 <= 620 +- 14 <= 635
        Bandwidth on Decoded Data / (MB/s): 799 <= 815 +- 18 <= 835
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=128):
        Runtime / s: 0.85 <= 0.87 +- 0.04 <= 0.91
        Bandwidth on Encoded Data / (MB/s): 448 <= 469 +- 19 <= 480
        Bandwidth on Decoded Data / (MB/s): 589 <= 617 +- 24 <= 631

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
        Runtime / s: 1.605 <= 1.611 +- 0.006 <= 1.617
        Bandwidth on Encoded Data / (MB/s): 252.6 <= 253.5 +- 1 <= 254.5
        Bandwidth on Decoded Data / (MB/s): 332 <= 333.2 +- 1.3 <= 334.6
    Decompressed 408430549 B to 536870912 B with zlib:
        Runtime / s: 1.901 <= 1.906 +- 0.009 <= 1.916
        Bandwidth on Encoded Data / (MB/s): 213.2 <= 214.3 +- 1 <= 214.9
        Bandwidth on Decoded Data / (MB/s): 280.2 <= 281.6 +- 1.3 <= 282.5
    Decoded 37796 deflate blocks
    Decoded 37796 deflate blocks
    Decoded 37796 deflate blocks
    Decompressed 408430549 B to 536870912 B with pragzip (serial):
        Runtime / s: 4.94 <= 4.99 +- 0.05 <= 5.04
        Bandwidth on Encoded Data / (MB/s): 81 <= 81.9 +- 0.9 <= 82.8
        Bandwidth on Decoded Data / (MB/s): 106.5 <= 107.6 +- 1.2 <= 108.8
    Decompressed 408430549 B to 536870912 B with pragzip (parallel):
        Runtime / s: 0.46 <= 0.467 +- 0.006 <= 0.471
        Bandwidth on Encoded Data / (MB/s): 866 <= 875 +- 11 <= 888
        Bandwidth on Decoded Data / (MB/s): 1139 <= 1151 +- 15 <= 1167
    Decompressed 408430549 B to 536870912 B with pragzip (parallel + index):
        Runtime / s: 0.207 <= 0.21 +- 0.005 <= 0.215
        Bandwidth on Encoded Data / (MB/s): 1900 <= 1950 +- 40 <= 1970
        Bandwidth on Decoded Data / (MB/s): 2490 <= 2560 +- 60 <= 2590


bgzip -c small > small.bgz
make benchmarkGzip && src/benchmarks/benchmarkGzip small.bgz

    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=0):
        Runtime / s: 0.434 <= 0.452 +- 0.017 <= 0.466
        Bandwidth on Encoded Data / (MB/s): 890 <= 920 +- 30 <= 960
        Bandwidth on Decoded Data / (MB/s): 1150 <= 1190 +- 40 <= 1240
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=1):
        Runtime / s: 0.3275 <= 0.3286 +- 0.001 <= 0.3294
        Bandwidth on Encoded Data / (MB/s): 1260 <= 1263 +- 4 <= 1268
        Bandwidth on Decoded Data / (MB/s): 1630 <= 1634 +- 5 <= 1639
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=2):
        Runtime / s: 0.322 <= 0.332 +- 0.013 <= 0.347
        Bandwidth on Encoded Data / (MB/s): 1200 <= 1250 +- 50 <= 1290
        Bandwidth on Decoded Data / (MB/s): 1550 <= 1620 +- 60 <= 1670
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=4):
        Runtime / s: 0.299 <= 0.31 +- 0.011 <= 0.32
        Bandwidth on Encoded Data / (MB/s): 1300 <= 1340 +- 50 <= 1390
        Bandwidth on Decoded Data / (MB/s): 1680 <= 1730 +- 60 <= 1800
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=8):
        Runtime / s: 0.292 <= 0.296 +- 0.004 <= 0.3
        Bandwidth on Encoded Data / (MB/s): 1386 <= 1405 +- 17 <= 1419
        Bandwidth on Decoded Data / (MB/s): 1792 <= 1817 +- 22 <= 1836
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=16):
        Runtime / s: 0.265 <= 0.274 +- 0.014 <= 0.29
        Bandwidth on Encoded Data / (MB/s): 1430 <= 1520 +- 80 <= 1570
        Bandwidth on Decoded Data / (MB/s): 1850 <= 1970 +- 100 <= 2030
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=24):
        Runtime / s: 0.259 <= 0.264 +- 0.006 <= 0.271
        Bandwidth on Encoded Data / (MB/s): 1530 <= 1570 +- 40 <= 1600
        Bandwidth on Decoded Data / (MB/s): 1980 <= 2030 +- 50 <= 2070
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=32):
        Runtime / s: 0.262 <= 0.264 +- 0.004 <= 0.269
        Bandwidth on Encoded Data / (MB/s): 1546 <= 1570 +- 21 <= 1585
        Bandwidth on Decoded Data / (MB/s): 1999 <= 2030 +- 27 <= 2051
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=64):
        Runtime / s: 0.2768 <= 0.2786 +- 0.0017 <= 0.2802
        Bandwidth on Encoded Data / (MB/s): 1482 <= 1490 +- 9 <= 1500
        Bandwidth on Decoded Data / (MB/s): 1916 <= 1927 +- 12 <= 1940
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=128):
        Runtime / s: 0.33 <= 0.334 +- 0.004 <= 0.338
        Bandwidth on Encoded Data / (MB/s): 1228 <= 1243 +- 15 <= 1257
        Bandwidth on Decoded Data / (MB/s): 1588 <= 1607 +- 19 <= 1626

  -> 32 blocks for bgz would be the best in this case! But starting from 16, it feels already saturated.
     -> pragzip --analyze small.bgz
        - There are 8226 blocks and gzip streams in total
        - Each block is ~ 50400 B encoded and 65280 B (63.75 KiB) decoded,
        - So, when skipping ~32 blocks (~33 chunked), we have ~1.54 MiB encoded and ~2 MiB decoded data
          -> This is only a factor 2 less than the optimum for pigz, so that we might use the same blocking criterium,
             or not.

    Decompressed 415096389 B to 536870912 B with libarchive:
        Runtime / s: 1.8292 <= 1.8308 +- 0.0017 <= 1.8326
        Bandwidth on Encoded Data / (MB/s): 226.51 <= 226.72 +- 0.21 <= 226.93
        Bandwidth on Decoded Data / (MB/s): 292.96 <= 293.24 +- 0.27 <= 293.5
    Decompressed 415096389 B to 536870912 B with zlib:
        Runtime / s: 2.064 <= 2.076 +- 0.017 <= 2.095
        Bandwidth on Encoded Data / (MB/s): 198.1 <= 200 +- 1.6 <= 201.1
        Bandwidth on Decoded Data / (MB/s): 256.2 <= 258.6 +- 2.1 <= 260.1
    Decoded 8226 deflate blocks
    Decoded 8226 deflate blocks
    Decoded 8226 deflate blocks
    Decompressed 415096389 B to 536870912 B with pragzip (serial):
        Runtime / s: 3.063 <= 3.073 +- 0.01 <= 3.082
        Bandwidth on Encoded Data / (MB/s): 134.7 <= 135.1 +- 0.4 <= 135.5
        Bandwidth on Decoded Data / (MB/s): 174.2 <= 174.7 +- 0.6 <= 175.3
    Decompressed 415096389 B to 536870912 B with pragzip (parallel):
        Runtime / s: 0.272 <= 0.291 +- 0.026 <= 0.32
        Bandwidth on Encoded Data / (MB/s): 1300 <= 1430 +- 120 <= 1530
        Bandwidth on Decoded Data / (MB/s): 1680 <= 1850 +- 160 <= 1980
    Decompressed 415096389 B to 536870912 B with pragzip (parallel + index):
        Runtime / s: 0.259 <= 0.263 +- 0.004 <= 0.267
        Bandwidth on Encoded Data / (MB/s): 1553 <= 1578 +- 25 <= 1604
        Bandwidth on Decoded Data / (MB/s): 2010 <= 2040 +- 30 <= 2070

     -> ~2 GB/s for the decompressed bandwidth with the parallel bgz decoder and when decoding with an
        existing index is already quite nice!

time gzip -d -k -c small.bgz | wc -c
    real  0m3.048s
  -> Interestingly, this is reproducibly faster than the .gz compressed one. Maybe different compression setting?

time bgzip --threads $( nproc ) -d -c small.bgz | wc -c
    real  0m0.195s
  -> Only ~25% faster than parallel pragzip thanks to the internal usage of zlib

ls -la small.*gz
    415096389 small.bgz
    416689498 small.gz
  -> The .bgz file is even smaller!


base64 /dev/urandom | head -c $(( 4*1024*1024*1024 )) > large
gzip -k large
bgzip -c large > large.bgz
make benchmarkGzip && src/benchmarks/benchmarkGzip large.bgz

    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=0):
        Runtime / s: 3.47 <= 3.59 +- 0.14 <= 3.74
        Bandwidth on Encoded Data / (MB/s): 890 <= 930 +- 40 <= 960
        Bandwidth on Decoded Data / (MB/s): 1150 <= 1200 +- 50 <= 1240
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=1):
        Runtime / s: 2.606 <= 2.612 +- 0.007 <= 2.62
        Bandwidth on Encoded Data / (MB/s): 1268 <= 1271 +- 3 <= 1274
        Bandwidth on Decoded Data / (MB/s): 1639 <= 1645 +- 4 <= 1648
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=2):
        Runtime / s: 2.55 <= 2.6 +- 0.06 <= 2.67
        Bandwidth on Encoded Data / (MB/s): 1240 <= 1280 +- 30 <= 1300
        Bandwidth on Decoded Data / (MB/s): 1610 <= 1650 +- 40 <= 1690
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=4):
        Runtime / s: 2.267 <= 2.286 +- 0.017 <= 2.3
        Bandwidth on Encoded Data / (MB/s): 1444 <= 1453 +- 11 <= 1465
        Bandwidth on Decoded Data / (MB/s): 1868 <= 1879 +- 14 <= 1894
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=8):
        Runtime / s: 2.111 <= 2.129 +- 0.015 <= 2.138
        Bandwidth on Encoded Data / (MB/s): 1553 <= 1560 +- 11 <= 1573
        Bandwidth on Decoded Data / (MB/s): 2009 <= 2018 +- 14 <= 2034
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=16):
        Runtime / s: 1.936 <= 1.953 +- 0.017 <= 1.969
        Bandwidth on Encoded Data / (MB/s): 1687 <= 1701 +- 14 <= 1716
        Bandwidth on Decoded Data / (MB/s): 2181 <= 2200 +- 19 <= 2219
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=24):
        Runtime / s: 1.881 <= 1.891 +- 0.011 <= 1.903
        Bandwidth on Encoded Data / (MB/s): 1745 <= 1756 +- 10 <= 1766
        Bandwidth on Decoded Data / (MB/s): 2257 <= 2272 +- 13 <= 2284
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=32):
        Runtime / s: 1.825 <= 1.834 +- 0.008 <= 1.842
        Bandwidth on Encoded Data / (MB/s): 1802 <= 1811 +- 8 <= 1819
        Bandwidth on Decoded Data / (MB/s): 2331 <= 2342 +- 11 <= 2353
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=64):
        Runtime / s: 1.814 <= 1.823 +- 0.007 <= 1.828
        Bandwidth on Encoded Data / (MB/s): 1816 <= 1822 +- 7 <= 1830
        Bandwidth on Decoded Data / (MB/s): 2349 <= 2357 +- 10 <= 2367
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=128):
        Runtime / s: 1.82 <= 1.84 +- 0.03 <= 1.88
        Bandwidth on Encoded Data / (MB/s): 1770 <= 1800 +- 30 <= 1830
        Bandwidth on Decoded Data / (MB/s): 2290 <= 2330 +- 40 <= 2360

  -> I don't see the peak yet! I guess the benchmark with the 512 MiB base64 file only topped out at 16 blocks
     per chunk because then the tail might produce stragglers!
     -> So we are fine using a chunking criterium shared with pigz of roughly 4 MiB of encoded data or maybe more.
        -> Note that for generic gzip files, we would need roughly 32 MiB of encoded data because of the much slower
           block finder amortizing slower!

    Decompressed 3320779389 B to 4294967296 B with libarchive:
        Runtime / s: 14.5 <= 14.64 +- 0.17 <= 14.83
        Bandwidth on Encoded Data / (MB/s): 223.9 <= 226.9 +- 2.7 <= 229
        Bandwidth on Decoded Data / (MB/s): 290 <= 293 +- 3 <= 296
    Decompressed 3320779389 B to 4294967296 B with zlib:
        Runtime / s: 16.766 <= 16.774 +- 0.013 <= 16.789
        Bandwidth on Encoded Data / (MB/s): 197.79 <= 197.97 +- 0.15 <= 198.07
        Bandwidth on Decoded Data / (MB/s): 255.82 <= 256.04 +- 0.2 <= 256.17
    Decoded 65795 deflate blocks
    Decoded 65795 deflate blocks
    Decoded 65795 deflate blocks
    Decompressed 3320779389 B to 4294967296 B with pragzip (serial):
        Runtime / s: 25.15 <= 25.24 +- 0.14 <= 25.4
        Bandwidth on Encoded Data / (MB/s): 130.7 <= 131.6 +- 0.7 <= 132.1
        Bandwidth on Decoded Data / (MB/s): 169.1 <= 170.1 +- 0.9 <= 170.8
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel):
        Runtime / s: 1.944 <= 1.951 +- 0.006 <= 1.956
        Bandwidth on Encoded Data / (MB/s): 1698 <= 1702 +- 5 <= 1708
        Bandwidth on Decoded Data / (MB/s): 2196 <= 2201 +- 7 <= 2209
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel + index):
        Runtime / s: 2.049 <= 2.061 +- 0.015 <= 2.077
        Bandwidth on Encoded Data / (MB/s): 1599 <= 1612 +- 12 <= 1620
        Bandwidth on Decoded Data / (MB/s): 2067 <= 2084 +- 15 <= 2096

time bgzip --threads $( nproc ) -d -c large.bgz | wc -c
    real  0m1.408s
  -> 30% faster than parallel pragzip thanks to internal usage of zlib


base64 /dev/urandom | head -c $(( 1024*1024*1024 )) > large
pigz -k -c large > large.pigz
make benchmarkGzip && src/benchmarks/benchmarkGzip large.pigz

    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=0):
        Runtime / s: 1.54 <= 1.6 +- 0.06 <= 1.64
        Bandwidth on Encoded Data / (MB/s): 497 <= 510 +- 18 <= 531
        Bandwidth on Decoded Data / (MB/s): 654 <= 671 +- 24 <= 698
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=1):
        Runtime / s: 1.049 <= 1.059 +- 0.009 <= 1.065
        Bandwidth on Encoded Data / (MB/s): 767 <= 771 +- 6 <= 779
        Bandwidth on Decoded Data / (MB/s): 1008 <= 1014 +- 8 <= 1023
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=2):
        Runtime / s: 0.954 <= 0.958 +- 0.004 <= 0.963
        Bandwidth on Encoded Data / (MB/s): 849 <= 853 +- 4 <= 856
        Bandwidth on Decoded Data / (MB/s): 1115 <= 1121 +- 5 <= 1126
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=4):
        Runtime / s: 0.833 <= 0.846 +- 0.016 <= 0.864
        Bandwidth on Encoded Data / (MB/s): 945 <= 965 +- 18 <= 980
        Bandwidth on Decoded Data / (MB/s): 1243 <= 1269 +- 24 <= 1289
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=8):
        Runtime / s: 0.799 <= 0.815 +- 0.02 <= 0.838
        Bandwidth on Encoded Data / (MB/s): 975 <= 1003 +- 25 <= 1023
        Bandwidth on Decoded Data / (MB/s): 1280 <= 1320 +- 30 <= 1340
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=16):
        Runtime / s: 0.776 <= 0.788 +- 0.017 <= 0.807
        Bandwidth on Encoded Data / (MB/s): 1012 <= 1037 +- 22 <= 1053
        Bandwidth on Decoded Data / (MB/s): 1331 <= 1363 +- 29 <= 1384
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=24):
        Runtime / s: 0.786 <= 0.803 +- 0.016 <= 0.817
        Bandwidth on Encoded Data / (MB/s): 1000 <= 1018 +- 20 <= 1039
        Bandwidth on Decoded Data / (MB/s): 1314 <= 1338 +- 26 <= 1366
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=32):
        Runtime / s: 0.797 <= 0.821 +- 0.021 <= 0.838
        Bandwidth on Encoded Data / (MB/s): 974 <= 996 +- 26 <= 1025
        Bandwidth on Decoded Data / (MB/s): 1280 <= 1310 +- 30 <= 1350
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=64):
        Runtime / s: 0.966 <= 0.98 +- 0.012 <= 0.987
        Bandwidth on Encoded Data / (MB/s): 827 <= 834 +- 11 <= 846
        Bandwidth on Decoded Data / (MB/s): 1088 <= 1096 +- 14 <= 1112
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=128):
        Runtime / s: 1.252 <= 1.275 +- 0.024 <= 1.3
        Bandwidth on Encoded Data / (MB/s): 628 <= 641 +- 12 <= 653
        Bandwidth on Decoded Data / (MB/s): 826 <= 842 +- 16 <= 858

    Decompressed 816860634 B to 1073741824 B with libarchive:
        Runtime / s: 3.3022 <= 3.303 +- 0.0007 <= 3.3036
        Bandwidth on Encoded Data / (MB/s): 247.26 <= 247.31 +- 0.05 <= 247.37
        Bandwidth on Decoded Data / (MB/s): 325.02 <= 325.08 +- 0.07 <= 325.16
    Decompressed 816860634 B to 1073741824 B with zlib:
        Runtime / s: 3.892 <= 3.897 +- 0.005 <= 3.903
        Bandwidth on Encoded Data / (MB/s): 209.31 <= 209.61 +- 0.29 <= 209.9
        Bandwidth on Decoded Data / (MB/s): 275.1 <= 275.5 +- 0.4 <= 275.9
    Decoded 75802 deflate blocks
    Decoded 75802 deflate blocks
    Decoded 75802 deflate blocks
    Decompressed 816860634 B to 1073741824 B with pragzip (serial):
        Runtime / s: 9.85 <= 9.96 +- 0.15 <= 10.13
        Bandwidth on Encoded Data / (MB/s): 80.6 <= 82 +- 1.2 <= 83
        Bandwidth on Decoded Data / (MB/s): 106 <= 107.8 +- 1.6 <= 109.1
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel):
        Runtime / s: 0.806 <= 0.812 +- 0.006 <= 0.816
        Bandwidth on Encoded Data / (MB/s): 1000 <= 1006 +- 7 <= 1014
        Bandwidth on Decoded Data / (MB/s): 1315 <= 1322 +- 9 <= 1333
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel + index):
        Runtime / s: 0.396 <= 0.403 +- 0.006 <= 0.406
        Bandwidth on Encoded Data / (MB/s): 2012 <= 2029 +- 29 <= 2063
        Bandwidth on Decoded Data / (MB/s): 2640 <= 2670 +- 40 <= 2710

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
base64 /dev/urandom | head -c $(( 4 * 1024 * 1024 * 1024 )) > 4GB
pigz  -k -c 4GB > 4GB.pigz
bgzip    -c 4GB > 4GB.bgz
gzip  -k -c 4GB > 4GB.gz
igzip -k -c 4GB > 4GB.igz

fileSize=$( stat --format=%s 4GB )
for format in gz bgz pigz igz; do
    printf '\n| %6s | %13s | %10s | %18s |\n' Format Decoder 'Runtime / s' 'Bandwidth / (MB/s)'
    printf -- '|--------|---------------|-------------|--------------------|\n'
    for tool in gzip bgzip "bgzip -@ $( nproc )" pigz igzip "igzip -T $( nproc )" src/tools/pragzip "src/tools/pragzip -P $( nproc )"; do
        runtime=$( ( time $tool -d -c "4GB.$format" | wc -c ) 2>&1 | sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
        bandwidth=$( python3 -c "print( int( round( $fileSize / 1e6 / $runtime ) ) )" )
        printf '| %6s | %13s | %11s | %18s |\n' "$format" "${tool//*\//}" "$runtime" "$bandwidth"
    done
done


| Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
|--------|---------------|-------------|--------------------|
|     gz |          gzip |      25.510 |                168 |
|     gz |         bgzip |      16.237 |                265 |
|     gz |   bgzip -@ 24 |      15.934 |                270 |
|     gz |          pigz |      13.171 |                326 |
|     gz |         igzip |       8.802 |                488 |
|     gz |   igzip -T 24 |       8.779 |                489 |
|     gz |       pragzip |      30.736 |                140 |
|     gz | pragzip -P 24 |      29.580 |                145 |

| Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
|--------|---------------|-------------|--------------------|
|    bgz |          gzip |      27.574 |                156 |
|    bgz |         bgzip |      12.087 |                355 |
|    bgz |   bgzip -@ 24 |       1.878 |               2287 |
|    bgz |          pigz |      20.327 |                211 |
|    bgz |         igzip |       9.286 |                463 |
|    bgz |   igzip -T 24 |       9.239 |                465 |
|    bgz |       pragzip |      25.679 |                167 |
|    bgz | pragzip -P 24 |       2.555 |               1681 |

| Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
|--------|---------------|-------------|--------------------|
|   pigz |          gzip |      25.741 |                167 |
|   pigz |         bgzip |      17.095 |                251 |
|   pigz |   bgzip -@ 24 |      16.326 |                263 |
|   pigz |          pigz |      13.732 |                313 |
|   pigz |         igzip |       9.972 |                431 |
|   pigz |   igzip -T 24 |       9.736 |                441 |
|   pigz |       pragzip |      41.204 |                104 |
|   pigz | pragzip -P 24 |       3.224 |               1332 |

| Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
|--------|---------------|-------------|--------------------|
|    igz |          gzip |      23.278 |                185 |
|    igz |         bgzip |      14.966 |                287 |
|    igz |   bgzip -@ 24 |      15.267 |                281 |
|    igz |          pigz |      12.123 |                354 |
|    igz |         igzip |       7.815 |                550 |
|    igz |   igzip -T 24 |       7.776 |                552 |
|    igz |       pragzip |      18.244 |                235 |
|    igz | pragzip -P 24 |      20.372 |                211 |
*/
