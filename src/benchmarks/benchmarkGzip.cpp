/**
 * @file Executes benchmarks for varying gzip decompressors (zlib, libarchive, pragzip: sequential, parallel,
 *       with index, with different chunk sizes) for a given compressed file.
 *       It is to be used as an integration benchmark especially for the parallel decompressor and for comparison
 *       how much it improves over preexisting solutions like zlib. Furthermore, it compares the result size
 *       of all implementations to check for errors.
 */

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <archive.h>
#include <zlib.h>

#include <common.hpp>
#include <filereader/Standard.hpp>
#include <FileUtils.hpp>
#include <GzipReader.hpp>
#include <ParallelGzipReader.hpp>
#include <Statistics.hpp>
#include <TestHelpers.hpp>


class GzipWrapper
{
public:
    static constexpr auto WINDOW_SIZE = 32_Ki;

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

    GzipWrapper&
    operator=( GzipWrapper&& ) = delete;

    GzipWrapper&
    operator=( GzipWrapper& ) = delete;

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
    std::vector<unsigned char> m_window = std::vector<unsigned char>( 32_Ki, '\0' );
    std::vector<unsigned char> m_outputBuffer = std::vector<unsigned char>( 64_Mi );
};



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

    std::vector<uint8_t> outputBuffer( 64_Mi );
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
    std::vector<uint8_t> outputBuffer( 64_Mi );
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

    pragzip::ParallelGzipReader gzipReader( std::make_unique<StandardFileReader>( fileName ) );
    std::vector<uint8_t> outputBuffer( 64_Mi );
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

    const auto spacing = ( nBlocksToSkip + 1 ) * 32_Ki;
    pragzip::ParallelGzipReader gzipReader( std::make_unique<StandardFileReader>( fileName ), 0, spacing );
    std::vector<uint8_t> outputBuffer( 64_Mi );
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

    pragzip::ParallelGzipReader gzipReader( std::make_unique<StandardFileReader>( fileName ) );
    gzipReader.setBlockOffsets( index );
    std::vector<uint8_t> outputBuffer( 64_Mi );
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
                        return static_cast<double>( nBytesEncoded ) / 1e6 / duration;
                    } );
    std::cout << "    Bandwidth on Encoded Data / (MB/s): "
              << Statistics<double>( encodedBandwidths ).formatAverageWithUncertainty( true ) << "\n";

    std::vector<double> decodedBandwidths( durations.size() );
    std::transform( durations.begin(), durations.end(), decodedBandwidths.begin(),
                    [nBytesDecoded] ( auto duration ) {
                        return static_cast<double>( nBytesDecoded ) / 1e6 / duration;
                    } );
    std::cout << "    Bandwidth on Decoded Data / (MB/s): "
              << Statistics<double>( decodedBandwidths ).formatAverageWithUncertainty( true ) << "\n";
};
void
benchmarkChunkedParallelDecompression( const std::string& fileName )
{
    const auto fileContents = readFile( fileName );

    std::cout << "\n== Benchmarking with pragzip in parallel with different decoding chunk sizes ==\n\n";

    const auto [sizeLibArchive, durationsLibArchive] = benchmarkFunction<3>(
        [&fileContents] () { return decompressWithLibArchive( fileContents ); } );
    //std::cout << "Decompressed " << fileContents.size() << " B to " << sizeLibArchive << " B with libarchive:\n";
    //printBandwidths( durationsLibArchive, fileContents.size(), sizeLibArchive );

    const auto expectedSize = sizeLibArchive;

    for ( size_t nBlocksToSkip : { 0, 1, 2, 4, 8, 16, 24, 32, 64, 128 } ) {
        const auto [sizePragzipParallel, durationsPragzipParallel] = benchmarkFunction<3>(
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

    const auto [sizeLibArchive, durationsLibArchive] = benchmarkFunction<3>(
        [&fileContents] () { return decompressWithLibArchive( fileContents ); } );
    std::cout << "Decompressed " << fileContents.size() << " B to " << sizeLibArchive << " B with libarchive:\n";
    printBandwidths( durationsLibArchive, fileContents.size(), sizeLibArchive );

    const auto expectedSize = sizeLibArchive;

    const auto [sizeZlib, durationsZlib] = benchmarkFunction<3>(
        [&fileContents] () { return decompressWithZlib( fileContents ); } );
    if ( sizeZlib == expectedSize ) {
        std::cout << "Decompressed " << fileContents.size() << " B to " << sizeZlib << " B with zlib:\n";
        printBandwidths( durationsZlib, fileContents.size(), sizeZlib );
    } else {
        std::cerr << "Decompressing with zlib decoded a different amount than libarchive!\n";
    }

    const auto [sizePragzip, durationsPragzip] = benchmarkFunction<3>(
        [&fileName] () { return decompressWithPragzip( fileName ); } );
    if ( sizePragzip == expectedSize ) {
        std::cout << "Decompressed " << fileContents.size() << " B to " << sizePragzip << " B "
                  << "with pragzip (serial):\n";
        printBandwidths( durationsPragzip, fileContents.size(), sizePragzip );
    } else {
        std::cerr << "Decompressing with pragzip (serial) decoded a different amount than libarchive!\n";
    }

    const auto [sizePragzipParallel, durationsPragzipParallel] = benchmarkFunction<3>(
        [&fileName] () { return decompressWithPragzipParallel( fileName ); } );
    if ( sizePragzipParallel == expectedSize ) {
        std::cout << "Decompressed " << fileContents.size() << " B to " << sizePragzipParallel << " B "
                  << "with pragzip (parallel):\n";
        printBandwidths( durationsPragzipParallel, fileContents.size(), sizePragzipParallel );
    } else {
        throw std::logic_error( "Decompressing with pragzip (parallel) decoded a different amount ("
                                + std::to_string( sizePragzipParallel ) + ") than libarchive ("
                                + std::to_string( expectedSize ) + ")!" );
    }

    const auto [sizePragzipParallelIndex, durationsPragzipParallelIndex] = benchmarkFunction<3>(
        [&fileName] () { return createGzipIndex( fileName ); }, decompressWithPragzipParallelIndex );
    if ( sizePragzipParallelIndex == expectedSize ) {
        std::cout << "Decompressed " << fileContents.size() << " B to " << sizePragzipParallelIndex << " B "
                  << "with pragzip (parallel + index):\n";
        printBandwidths( durationsPragzipParallelIndex, fileContents.size(), sizePragzipParallelIndex );
    } else {
        throw std::logic_error( "Decompressing with pragzip (parallel + index) decoded a different amount than "
                                "libarchive!" );
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

cmake --build . -- benchmarkGzip && src/benchmarks/benchmarkGzip small.gz

    Decompressed 407988639 B to 536870912 B with libarchive:
        Runtime / s: 1.574 <= 1.594 +- 0.029 <= 1.628
        Bandwidth on Encoded Data / (MB/s): 251 <= 256 +- 5 <= 259
        Bandwidth on Decoded Data / (MB/s): 330 <= 337 +- 6 <= 341
    Decompressed 407988639 B to 536870912 B with zlib:
        Runtime / s: 1.854 <= 1.867 +- 0.016 <= 1.884
        Bandwidth on Encoded Data / (MB/s): 216.5 <= 218.6 +- 1.8 <= 220.1
        Bandwidth on Decoded Data / (MB/s): 284.9 <= 287.6 +- 2.4 <= 289.6
    Decoded 15844 deflate blocks
    Decoded 15844 deflate blocks
    Decoded 15844 deflate blocks
    Decompressed 407988639 B to 536870912 B with pragzip (serial):
        Runtime / s: 2.260 <= 2.281 +- 0.024 <= 2.307
        Bandwidth on Encoded Data / (MB/s): 176.8 <= 178.9 +- 1.9 <= 180.5
        Bandwidth on Decoded Data / (MB/s): 232.7 <= 235.4 +- 2.5 <= 237.6
    Decompressed 407988639 B to 536870912 B with pragzip (parallel):
        Runtime / s: 0.211 <= 0.228 +- 0.018 <= 0.247
        Bandwidth on Encoded Data / (MB/s): 1650 <= 1800 +- 140 <= 1940
        Bandwidth on Decoded Data / (MB/s): 2170 <= 2370 +- 190 <= 2550
    Decompressed 407988639 B to 536870912 B with pragzip (parallel + index):
        Runtime / s: 0.185 <= 0.190 +- 0.005 <= 0.194
        Bandwidth on Encoded Data / (MB/s): 2100 <= 2150 +- 50 <= 2200
        Bandwidth on Decoded Data / (MB/s): 2770 <= 2830 +- 70 <= 2900

      -> pragzip is significantly slower than libarchive
         -> Decoding with pragzip in parallel with an index is now much faster because of internal zlib usage!

time gzip -d -k -c small.gz | wc -c
    real  0m2.830s
  -> pragzip is ~28% slower than gzip 1.10. Maybe slower than the above benchmarks because of I/O?

pigz -c small > small.pigz
cmake --build . -- benchmarkGzip && src/benchmarks/benchmarkGzip small.pigz

    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=0):
        Runtime / s: 1.11 <= 1.15 +- 0.05 <= 1.20
        Bandwidth on Encoded Data / (MB/s): 341 <= 356 +- 14 <= 368
        Bandwidth on Decoded Data / (MB/s): 448 <= 469 +- 18 <= 483
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=1):
        Runtime / s: 0.611 <= 0.618 +- 0.008 <= 0.627
        Bandwidth on Encoded Data / (MB/s): 652 <= 661 +- 9 <= 669
        Bandwidth on Decoded Data / (MB/s): 857 <= 868 +- 11 <= 879
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=2):
        Runtime / s: 0.475 <= 0.481 +- 0.006 <= 0.486
        Bandwidth on Encoded Data / (MB/s): 840 <= 849 +- 10 <= 860
        Bandwidth on Decoded Data / (MB/s): 1105 <= 1116 +- 13 <= 1131
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=4):
        Runtime / s: 0.3460 <= 0.3488 +- 0.0025 <= 0.3510
        Bandwidth on Encoded Data / (MB/s): 1164 <= 1171 +- 8 <= 1180
        Bandwidth on Decoded Data / (MB/s): 1530 <= 1539 +- 11 <= 1551
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=8):
        Runtime / s: 0.278 <= 0.290 +- 0.010 <= 0.297
        Bandwidth on Encoded Data / (MB/s): 1370 <= 1410 +- 50 <= 1470
        Bandwidth on Decoded Data / (MB/s): 1800 <= 1850 +- 70 <= 1930
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=16):
        Runtime / s: 0.242 <= 0.249 +- 0.006 <= 0.254
        Bandwidth on Encoded Data / (MB/s): 1610 <= 1640 +- 40 <= 1690
        Bandwidth on Decoded Data / (MB/s): 2110 <= 2160 +- 50 <= 2220
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=24):
        Runtime / s: 0.227 <= 0.231 +- 0.005 <= 0.237
        Bandwidth on Encoded Data / (MB/s): 1720 <= 1770 +- 40 <= 1800
        Bandwidth on Decoded Data / (MB/s): 2270 <= 2320 +- 50 <= 2360
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=32):
        Runtime / s: 0.2240 <= 0.2263 +- 0.0029 <= 0.2296
        Bandwidth on Encoded Data / (MB/s): 1779 <= 1805 +- 23 <= 1823
        Bandwidth on Decoded Data / (MB/s): 2339 <= 2372 +- 30 <= 2396
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=64):
        Runtime / s: 0.219 <= 0.237 +- 0.020 <= 0.259
        Bandwidth on Encoded Data / (MB/s): 1580 <= 1730 +- 150 <= 1860
        Bandwidth on Decoded Data / (MB/s): 2070 <= 2280 +- 190 <= 2450
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=128):
        Runtime / s: 0.231 <= 0.239 +- 0.010 <= 0.250
        Bandwidth on Encoded Data / (MB/s): 1630 <= 1710 +- 70 <= 1770
        Bandwidth on Decoded Data / (MB/s): 2150 <= 2250 +- 90 <= 2320

  -> 64 blocks as chunks are the fastest!, starting from 24, it seems to be somewhat saturated already.
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
        Runtime / s: 1.615 <= 1.637 +- 0.019 <= 1.650
        Bandwidth on Encoded Data / (MB/s): 247.6 <= 249.6 +- 2.9 <= 252.9
        Bandwidth on Decoded Data / (MB/s): 325 <= 328 +- 4 <= 332
    Decompressed 408430549 B to 536870912 B with zlib:
        Runtime / s: 1.906 <= 1.913 +- 0.006 <= 1.919
        Bandwidth on Encoded Data / (MB/s): 212.9 <= 213.5 +- 0.7 <= 214.3
        Bandwidth on Decoded Data / (MB/s): 279.8 <= 280.6 +- 0.9 <= 281.6
    Decoded 37796 deflate blocks
    Decoded 37796 deflate blocks
    Decoded 37796 deflate blocks
    Decompressed 408430549 B to 536870912 B with pragzip (serial):
        Runtime / s: 2.517 <= 2.550 +- 0.029 <= 2.574
        Bandwidth on Encoded Data / (MB/s): 158.7 <= 160.2 +- 1.9 <= 162.3
        Bandwidth on Decoded Data / (MB/s): 208.5 <= 210.6 +- 2.4 <= 213.3
    Decompressed 408430549 B to 536870912 B with pragzip (parallel):
        Runtime / s: 0.232 <= 0.241 +- 0.011 <= 0.253
        Bandwidth on Encoded Data / (MB/s): 1610 <= 1700 +- 80 <= 1760
        Bandwidth on Decoded Data / (MB/s): 2120 <= 2230 +- 100 <= 2320
    Decompressed 408430549 B to 536870912 B with pragzip (parallel + index):
        Runtime / s: 0.188 <= 0.191 +- 0.003 <= 0.195
        Bandwidth on Encoded Data / (MB/s): 2100 <= 2130 +- 40 <= 2170
        Bandwidth on Decoded Data / (MB/s): 2760 <= 2810 +- 50 <= 2860


bgzip -c small > small.bgz
cmake --build . -- benchmarkGzip && src/benchmarks/benchmarkGzip small.bgz

    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=0):
        Runtime / s: 0.559 <= 0.583 +- 0.029 <= 0.615
        Bandwidth on Encoded Data / (MB/s): 680 <= 710 +- 30 <= 740
        Bandwidth on Decoded Data / (MB/s): 870 <= 920 +- 40 <= 960
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=1):
        Runtime / s: 0.49 <= 0.55 +- 0.05 <= 0.58
        Bandwidth on Encoded Data / (MB/s): 710 <= 760 +- 70 <= 840
        Bandwidth on Decoded Data / (MB/s): 920 <= 980 +- 90 <= 1080
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=2):
        Runtime / s: 0.3370 <= 0.3387 +- 0.0026 <= 0.3417
        Bandwidth on Encoded Data / (MB/s): 1215 <= 1226 +- 9 <= 1232
        Bandwidth on Decoded Data / (MB/s): 1571 <= 1585 +- 12 <= 1593
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=4):
        Runtime / s: 0.469 <= 0.494 +- 0.024 <= 0.516
        Bandwidth on Encoded Data / (MB/s): 800 <= 840 +- 40 <= 890
        Bandwidth on Decoded Data / (MB/s): 1040 <= 1090 +- 50 <= 1150
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=8):
        Runtime / s: 0.2468 <= 0.2496 +- 0.0027 <= 0.2522
        Bandwidth on Encoded Data / (MB/s): 1646 <= 1663 +- 18 <= 1682
        Bandwidth on Decoded Data / (MB/s): 2128 <= 2151 +- 24 <= 2175
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=16):
        Runtime / s: 0.2304 <= 0.2313 +- 0.0008 <= 0.2319
        Bandwidth on Encoded Data / (MB/s): 1790 <= 1795 +- 6 <= 1802
        Bandwidth on Decoded Data / (MB/s): 2315 <= 2322 +- 8 <= 2331
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=24):
        Runtime / s: 0.220 <= 0.223 +- 0.005 <= 0.229
        Bandwidth on Encoded Data / (MB/s): 1810 <= 1860 +- 40 <= 1890
        Bandwidth on Decoded Data / (MB/s): 2340 <= 2400 +- 50 <= 2440
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=32):
        Runtime / s: 0.219 <= 0.222 +- 0.004 <= 0.226
        Bandwidth on Encoded Data / (MB/s): 1830 <= 1870 +- 30 <= 1890
        Bandwidth on Decoded Data / (MB/s): 2370 <= 2420 +- 40 <= 2450
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=64):
        Runtime / s: 0.221 <= 0.230 +- 0.009 <= 0.240
        Bandwidth on Encoded Data / (MB/s): 1730 <= 1810 +- 70 <= 1880
        Bandwidth on Decoded Data / (MB/s): 2240 <= 2340 +- 90 <= 2430
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=128):
        Runtime / s: 0.237 <= 0.239 +- 0.004 <= 0.243
        Bandwidth on Encoded Data / (MB/s): 1708 <= 1737 +- 25 <= 1753
        Bandwidth on Decoded Data / (MB/s): 2210 <= 2250 +- 30 <= 2270

  -> 32 blocks for bgz would be the best in this case!
     -> pragzip --analyze small.bgz
        - There are 8226 blocks and gzip streams in total
        - Each block is ~ 50400 B encoded and 65280 B (63.75 KiB) decoded,
        - So, when skipping ~32 blocks (~33 chunked), we have ~1.54 MiB encoded and ~2 MiB decoded data
          -> This is only a factor 2 less than the optimum for pigz, so that we might use the same blocking criterium,
             or not.

    Decompressed 415096389 B to 536870912 B with libarchive:
        Runtime / s: 1.8567 <= 1.8578 +- 0.0014 <= 1.8595
        Bandwidth on Encoded Data / (MB/s): 223.24 <= 223.43 +- 0.17 <= 223.57
        Bandwidth on Decoded Data / (MB/s): 288.73 <= 288.98 +- 0.22 <= 289.15
    Decompressed 415096389 B to 536870912 B with zlib:
        Runtime / s: 2.110 <= 2.118 +- 0.007 <= 2.122
        Bandwidth on Encoded Data / (MB/s): 195.6 <= 196.0 +- 0.7 <= 196.7
        Bandwidth on Decoded Data / (MB/s): 253.0 <= 253.5 +- 0.9 <= 254.5
    Decoded 8226 deflate blocks
    Decoded 8226 deflate blocks
    Decoded 8226 deflate blocks
    Decompressed 415096389 B to 536870912 B with pragzip (serial):
        Runtime / s: 3.331 <= 3.359 +- 0.024 <= 3.377
        Bandwidth on Encoded Data / (MB/s): 122.9 <= 123.6 +- 0.9 <= 124.6
        Bandwidth on Decoded Data / (MB/s): 159.0 <= 159.9 +- 1.2 <= 161.2
    Decompressed 415096389 B to 536870912 B with pragzip (parallel):
        Runtime / s: 0.236 <= 0.239 +- 0.004 <= 0.243
        Bandwidth on Encoded Data / (MB/s): 1711 <= 1740 +- 25 <= 1760
        Bandwidth on Decoded Data / (MB/s): 2210 <= 2250 +- 30 <= 2280
    Decompressed 415096389 B to 536870912 B with pragzip (parallel + index):
        Runtime / s: 0.247 <= 0.251 +- 0.003 <= 0.253
        Bandwidth on Encoded Data / (MB/s): 1639 <= 1654 +- 20 <= 1677
        Bandwidth on Decoded Data / (MB/s): 2119 <= 2140 +- 26 <= 2169

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


base64 /dev/urandom | head -c $(( 4*1024*1024*1024 )) > 4GiB-base64
bgzip -c 4GiB-base64 > 4GiB-base64.bgz
cmake --build . -- benchmarkGzip && src/benchmarks/benchmarkGzip 4GiB-base64.bgz

    Decompressed 3246513262 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=0):
        Runtime / s: 4.28 <= 4.44 +- 0.21 <= 4.68
        Bandwidth on Encoded Data / (MB/s): 690 <= 730 +- 30 <= 760
        Bandwidth on Decoded Data / (MB/s): 920 <= 970 +- 50 <= 1000
    Decompressed 3246513262 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=1):
        Runtime / s: 3.57 <= 3.62 +- 0.08 <= 3.71
        Bandwidth on Encoded Data / (MB/s): 876 <= 898 +- 19 <= 910
        Bandwidth on Decoded Data / (MB/s): 1158 <= 1188 +- 25 <= 1204
    Decompressed 3246513262 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=2):
        Runtime / s: 2.587 <= 2.600 +- 0.013 <= 2.613
        Bandwidth on Encoded Data / (MB/s): 1243 <= 1249 +- 6 <= 1255
        Bandwidth on Decoded Data / (MB/s): 1644 <= 1652 +- 8 <= 1660
    Decompressed 3246513262 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=4):
        Runtime / s: 3.03 <= 3.09 +- 0.05 <= 3.14
        Bandwidth on Encoded Data / (MB/s): 1035 <= 1050 +- 18 <= 1070
        Bandwidth on Decoded Data / (MB/s): 1369 <= 1390 +- 24 <= 1416
    Decompressed 3246513262 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=8):
        Runtime / s: 1.502 <= 1.516 +- 0.012 <= 1.523
        Bandwidth on Encoded Data / (MB/s): 2131 <= 2142 +- 17 <= 2161
        Bandwidth on Decoded Data / (MB/s): 2820 <= 2834 +- 22 <= 2859
    Decompressed 3246513262 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=16):
        Runtime / s: 1.325 <= 1.334 +- 0.008 <= 1.339
        Bandwidth on Encoded Data / (MB/s): 2424 <= 2434 +- 15 <= 2451
        Bandwidth on Decoded Data / (MB/s): 3207 <= 3220 +- 20 <= 3243
    Decompressed 3246513262 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=24):
        Runtime / s: 1.273 <= 1.280 +- 0.006 <= 1.286
        Bandwidth on Encoded Data / (MB/s): 2524 <= 2537 +- 13 <= 2549
        Bandwidth on Decoded Data / (MB/s): 3339 <= 3356 +- 17 <= 3373
    Decompressed 3246513262 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=32):
        Runtime / s: 1.243 <= 1.257 +- 0.013 <= 1.270
        Bandwidth on Encoded Data / (MB/s): 2557 <= 2584 +- 28 <= 2612
        Bandwidth on Decoded Data / (MB/s): 3380 <= 3420 +- 40 <= 3460
    Decompressed 3246513262 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=64):
        Runtime / s: 1.223 <= 1.232 +- 0.008 <= 1.238
        Bandwidth on Encoded Data / (MB/s): 2622 <= 2636 +- 17 <= 2655
        Bandwidth on Decoded Data / (MB/s): 3469 <= 3487 +- 23 <= 3513
    Decompressed 3246513262 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=128):
        Runtime / s: 1.251 <= 1.268 +- 0.026 <= 1.298
        Bandwidth on Encoded Data / (MB/s): 2500 <= 2560 +- 50 <= 2600
        Bandwidth on Decoded Data / (MB/s): 3310 <= 3390 +- 70 <= 3430

  -> The peak is at 64, close to the last.
     -> So we are fine using a chunking criterium shared with pigz of roughly 4 MiB of encoded data or maybe more.
        -> Note that for generic gzip files, we would need roughly 32 MiB of encoded data because of the much slower
           block finder amortizing slower!

    Decompressed 3246513262 B to 4294967296 B with libarchive:
        Runtime / s: 11.62 <= 11.75 +- 0.20 <= 11.98
        Bandwidth on Encoded Data / (MB/s): 271 <= 276 +- 5 <= 280
        Bandwidth on Decoded Data / (MB/s): 359 <= 366 +- 6 <= 370
    Decompressed 3246513262 B to 4294967296 B with zlib:
        Runtime / s: 13.93 <= 13.98 +- 0.06 <= 14.05
        Bandwidth on Encoded Data / (MB/s): 231.2 <= 232.3 +- 1.0 <= 233.1
        Bandwidth on Decoded Data / (MB/s): 305.8 <= 307.3 +- 1.3 <= 308.3
    Decoded 65795 deflate blocks
    Decoded 65795 deflate blocks
    Decoded 65795 deflate blocks
    Decompressed 3246513262 B to 4294967296 B with pragzip (serial):
        Runtime / s: 22.39 <= 22.57 +- 0.27 <= 22.89
        Bandwidth on Encoded Data / (MB/s): 141.9 <= 143.9 +- 1.7 <= 145.0
        Bandwidth on Decoded Data / (MB/s): 187.7 <= 190.3 +- 2.3 <= 191.8
    Decompressed 3246513262 B to 4294967296 B with pragzip (parallel):
        Runtime / s: 1.212 <= 1.224 +- 0.011 <= 1.235
        Bandwidth on Encoded Data / (MB/s): 2630 <= 2654 +- 24 <= 2678
        Bandwidth on Decoded Data / (MB/s): 3480 <= 3510 +- 30 <= 3540
    Decompressed 3246513262 B to 4294967296 B with pragzip (parallel + index):
        Runtime / s: 1.870 <= 1.884 +- 0.014 <= 1.899
        Bandwidth on Encoded Data / (MB/s): 1710 <= 1723 +- 13 <= 1736
        Bandwidth on Decoded Data / (MB/s): 2262 <= 2280 +- 17 <= 2297

      -> Why is the version with an index actually slower!?
         I almost would assume that zlib is less optimized than the pragzip-internal inflate for gzip header/footer
         reading and therefore is suboptimal for this one gzip stream per deflate block file format (BGZF).

time bgzip --threads $( nproc ) -d -c 4GiB-base64.bgz | wc -c
    real  0m1.327s
  -> Just as fast as pragzip (without index)!
time bgzip --threads $( nproc ) -d -c 4GiB-base64.bgz > /dev/null
    real  0m0.907s
  -> 30% faster than parallel pragzip thanks to internal usage of zlib


base64 /dev/urandom | head -c $(( 1024*1024*1024 )) | pigz > 4GiB-base64.pigz
cmake --build . -- benchmarkGzip && src/benchmarks/benchmarkGzip 4GiB-base64.pigz

    Info] Detected a performance problem. Decoding might take longer than necessary. Please consider opening a performance bug report with a reproducing compressed file. Detailed information:
    [Info] Found mismatching block. Need offset 1800044794 B 0 b. Look in partition offset: 1800044544 B 0 b. Found possible range: [1800044544 B 0 b, 1800044544 B 0 b]
    [Info] Detected a performance problem. Decoding might take longer than necessary. Please consider opening a performance bug report with a reproducing compressed file. Detailed information:
    [Info] Found mismatching block. Need offset 1800044794 B 0 b. Look in partition offset: 1800044544 B 0 b. Found possible range: [1800044544 B 0 b, 1800044544 B 0 b]
    [Info] Detected a performance problem. Decoding might take longer than necessary. Please consider opening a performance bug report with a reproducing compressed file. Detailed information:
    [Info] Found mismatching block. Need offset 1800044794 B 0 b. Look in partition offset: 1800044544 B 0 b. Found possible range: [1800044544 B 0 b, 1800044544 B 0 b]
    Decompressed 3267442522 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=0):
        Runtime / s: 8.99 <= 9.17 +- 0.26 <= 9.47
        Bandwidth on Encoded Data / (MB/s): 345 <= 356 +- 10 <= 364
        Bandwidth on Decoded Data / (MB/s): 453 <= 469 +- 13 <= 478
    Decompressed 3267442522 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=1):
        Runtime / s: 5.081 <= 5.090 +- 0.011 <= 5.102
        Bandwidth on Encoded Data / (MB/s): 640.4 <= 641.9 +- 1.4 <= 643.1
        Bandwidth on Decoded Data / (MB/s): 841.8 <= 843.8 +- 1.8 <= 845.4
    [Info] Detected a performance problem. Decoding might take longer than necessary. Please consider opening a performance bug report with a reproducing compressed file. Detailed information:
    [Info] Found mismatching block. Need offset 1800044794 B 0 b. Look in partition offset: 1800044544 B 0 b. Found possible range: [1800044544 B 0 b, 1800044544 B 0 b]
    [Info] Detected a performance problem. Decoding might take longer than necessary. Please consider opening a performance bug report with a reproducing compressed file. Detailed information:
    [Info] Found mismatching block. Need offset 1800044794 B 0 b. Look in partition offset: 1800044544 B 0 b. Found possible range: [1800044544 B 0 b, 1800044544 B 0 b]
    [Info] Detected a performance problem. Decoding might take longer than necessary. Please consider opening a performance bug report with a reproducing compressed file. Detailed information:
    [Info] Found mismatching block. Need offset 1800044794 B 0 b. Look in partition offset: 1800044544 B 0 b. Found possible range: [1800044544 B 0 b, 1800044544 B 0 b]
    Decompressed 3267442522 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=2):
        Runtime / s: 3.99 <= 4.03 +- 0.03 <= 4.06
        Bandwidth on Encoded Data / (MB/s): 804 <= 811 +- 7 <= 818
        Bandwidth on Decoded Data / (MB/s): 1057 <= 1066 +- 9 <= 1075
    Decompressed 3267442522 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=4):
        Runtime / s: 2.769 <= 2.786 +- 0.014 <= 2.795
        Bandwidth on Encoded Data / (MB/s): 1169 <= 1173 +- 6 <= 1180
        Bandwidth on Decoded Data / (MB/s): 1537 <= 1542 +- 8 <= 1551
    Decompressed 3267442522 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=8):
        Runtime / s: 2.16 <= 2.19 +- 0.03 <= 2.23
        Bandwidth on Encoded Data / (MB/s): 1468 <= 1494 +- 23 <= 1512
        Bandwidth on Decoded Data / (MB/s): 1930 <= 1960 +- 30 <= 1990
    Decompressed 3267442522 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=16):
        Runtime / s: 1.792 <= 1.794 +- 0.003 <= 1.797
        Bandwidth on Encoded Data / (MB/s): 1818 <= 1822 +- 3 <= 1823
        Bandwidth on Decoded Data / (MB/s): 2390 <= 2394 +- 4 <= 2397
    Decompressed 3267442522 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=24):
        Runtime / s: 1.664 <= 1.671 +- 0.009 <= 1.681
        Bandwidth on Encoded Data / (MB/s): 1944 <= 1956 +- 10 <= 1963
        Bandwidth on Decoded Data / (MB/s): 2555 <= 2571 +- 13 <= 2581
    Decompressed 3267442522 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=32):
        Runtime / s: 1.612 <= 1.623 +- 0.011 <= 1.634
        Bandwidth on Encoded Data / (MB/s): 1999 <= 2013 +- 14 <= 2026
        Bandwidth on Decoded Data / (MB/s): 2628 <= 2646 +- 18 <= 2664
    Decompressed 3267442522 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=64):
        Runtime / s: 1.539 <= 1.542 +- 0.003 <= 1.545
        Bandwidth on Encoded Data / (MB/s): 2114 <= 2120 +- 5 <= 2123
        Bandwidth on Decoded Data / (MB/s): 2779 <= 2786 +- 6 <= 2791
    Decompressed 3267442522 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=128):
        Runtime / s: 1.525 <= 1.542 +- 0.015 <= 1.554
        Bandwidth on Encoded Data / (MB/s): 2102 <= 2119 +- 21 <= 2142
        Bandwidth on Decoded Data / (MB/s): 2763 <= 2785 +- 28 <= 2816

    Decompressed 3267442522 B to 4294967296 B with libarchive:
        Runtime / s: 13.22 <= 13.38 +- 0.15 <= 13.52
        Bandwidth on Encoded Data / (MB/s): 241.6 <= 244.3 +- 2.7 <= 247.1
        Bandwidth on Decoded Data / (MB/s): 318 <= 321 +- 4 <= 325
    Decompressed 3267442522 B to 4294967296 B with zlib:
        Runtime / s: 15.42 <= 15.53 +- 0.09 <= 15.59
        Bandwidth on Encoded Data / (MB/s): 209.6 <= 210.4 +- 1.2 <= 211.9
        Bandwidth on Decoded Data / (MB/s): 275.5 <= 276.6 +- 1.6 <= 278.5
    Decoded 302998 deflate blocks
    Decoded 302998 deflate blocks
    Decoded 302998 deflate blocks
    Decompressed 3267442522 B to 4294967296 B with pragzip (serial):
        Runtime / s: 20.06 <= 20.16 +- 0.16 <= 20.34
        Bandwidth on Encoded Data / (MB/s): 160.6 <= 162.1 +- 1.3 <= 162.9
        Bandwidth on Decoded Data / (MB/s): 211.2 <= 213.1 +- 1.7 <= 214.1
    Decompressed 3267442522 B to 4294967296 B with pragzip (parallel):
        Runtime / s: 1.480 <= 1.499 +- 0.017 <= 1.514
        Bandwidth on Encoded Data / (MB/s): 2158 <= 2181 +- 25 <= 2208
        Bandwidth on Decoded Data / (MB/s): 2840 <= 2870 +- 30 <= 2900
    Decompressed 3267442522 B to 4294967296 B with pragzip (parallel + index):
        Runtime / s: 1.16 <= 1.19 +- 0.04 <= 1.23
        Bandwidth on Encoded Data / (MB/s): 2650 <= 2760 +- 100 <= 2820
        Bandwidth on Decoded Data / (MB/s): 3480 <= 3630 +- 120 <= 3700

     - [ ] The mismatching blocks above look like the stop condition read one more block than it should
           so that the next chunk was expected at the wrong offset. But further investigation is needed!
       Reproducible in (go back in steps of 40):
         b9769a95 (master) [test] ibzip2 integration tests were not running
         a5b97672 2023-03-17 [feature] Add command line option for toggling CRC32 computation
         42b5dfc0 2023-02-04 [style] Fix includes
         18b81385 2022-12-22 [refactor] Add (p)writeAllToFdVector
         85c396c1 2022-11-12 [build] Add "missing" inlines
         b54948f4 2022-09-07 [refactor] Make constexpr LUT-creation function a templated static constexpr variable instead
         a41058d4 2022-08-25 [performance] Fix performance degradation with pigz files because non-aligned uncompressed blocks were not found
       - WORKS:
         f2a35af3 2022-08-20 [performance] Use vmsplice to write to stdout pipe ~20% faster: 1.8 -> 2.0 GB/s
         f5865ffe 2022-08-17 [performance] Use new block finder in GzipBlockFetcher: 1.45 -> 1.45 GB/s
         7fd557e1 2022-08-20 [CI] Only start long-running tests on master and add manual trigger for other branches
        Bisection:

        BAD a41058d4 2022-08-25 mxmlnkn [performance] Fix performance degradation with pigz files because non-aligned uncompressed blocks were not found
          -> This commit introduces the debug output for mismatching blocks in the first place!
             - [ ] Therefore looks like the bug was not fully fixed in that commit. Maybe I can simply do the last 1% of the step!
        OK  bde61a91 2022-08-23 mxmlnkn [performance] Reduce instruction count for constexpr precode LUT creation
            e5a5a3dd 2022-08-26 mxmlnkn [feature] Print compression type and ratio statistics during pragzip --analyze
            bb109c79 2022-08-26 mxmlnkn [test] Add profiling output for the time spent in the block finder among others
        OK  9aa01733 2022-08-26 mxmlnkn [test] Output counter for locking in shared file
            4b89e069 2022-08-24 mxmlnkn (tag: pragzip-v0.3.0) [version] Bump pragzip version to 0.3.0
            3d385061 2022-08-23 mxmlnkn [performance] Reduce the chunk size from 8 MiB down to 2 MiB to quarter memory usage
            c1d55759 2022-08-23 mxmlnkn [feature] Make the parallelized work chunk size adjustable from the command line
            df5a9a6e 2022-08-23 mxmlnkn [feature] Integrate wc functionality like counting lines into pragzip to avoid pipe bottleneck: 2.0 -> 2.1 GB/s
            4609545b 2022-08-23 mxmlnkn [API] Add read overlead that takes a callback functor
        OK f2a35af3 2022-08-20 mxmlnkn [performance] Use vmsplice to write to stdout pipe ~20% faster: 1.8 -> 2.0 GB/s


       - [ ]  Add mismatchingChunkCount as extra metric that can be checked against in tests much more easily!

  -> Pragzip has abysmal serial decoder speed, probably because pigz deflate blocks are only 16 KiB so that the
     upfront cost for the double-symbol cached Huffman-decoder becomes expensive.


for (( i=0; i<20; ++i )); do cat test-files/silesia/silesia.tar; done | gzip > silesia-20x.tar.gz
cmake --build . -- benchmarkGzip && src/benchmarks/benchmarkGzip silesia-20x.tar.gz

    Decompressed 1364776140 B to 4239155200 B with libarchive:
        Runtime / s: 10.826 <= 10.837 +- 0.013 <= 10.851
        Bandwidth on Encoded Data / (MB/s): 125.78 <= 125.93 +- 0.15 <= 126.07
        Bandwidth on Decoded Data / (MB/s): 390.7 <= 391.2 +- 0.5 <= 391.6
    Decompressed 1364776140 B to 4239155200 B with zlib:
        Runtime / s: 12.36 <= 12.49 +- 0.22 <= 12.75
        Bandwidth on Encoded Data / (MB/s): 107.0 <= 109.3 +- 1.9 <= 110.4
        Bandwidth on Decoded Data / (MB/s): 332 <= 339 +- 6 <= 343
    Decoded 66040 deflate blocks
    Decoded 66040 deflate blocks
    Decoded 66040 deflate blocks
    Decompressed 1364776140 B to 4239155200 B with pragzip (serial):
        Runtime / s: 16.68 <= 16.75 +- 0.12 <= 16.88
        Bandwidth on Encoded Data / (MB/s): 80.9 <= 81.5 +- 0.6 <= 81.8
        Bandwidth on Decoded Data / (MB/s): 251.1 <= 253.1 +- 1.7 <= 254.2
    Decompressed 1364776140 B to 4239155200 B with pragzip (parallel):
        Runtime / s: 1.83 <= 1.93 +- 0.14 <= 2.09
        Bandwidth on Encoded Data / (MB/s): 650 <= 710 +- 50 <= 750
        Bandwidth on Decoded Data / (MB/s): 2030 <= 2210 +- 160 <= 2320
    Decompressed 1364776140 B to 4239155200 B with pragzip (parallel + index):
        Runtime / s: 1.028 <= 1.042 +- 0.012 <= 1.053
        Bandwidth on Encoded Data / (MB/s): 1297 <= 1310 +- 15 <= 1327
        Bandwidth on Decoded Data / (MB/s): 4030 <= 4070 +- 50 <= 4120
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
fname=4GiB-base64
base64 /dev/urandom | head -c $(( 4 * 1024 * 1024 * 1024 )) > "$fname"
pigz  -k -c "$fname" > "$fname.pigz"
bgzip    -c "$fname" > "$fname.bgz"
gzip  -k -c "$fname" > "$fname.gz"
igzip -k -c "$fname" > "$fname.igz"

printf '\n| %6s | %19s | %10s | %18s |\n' Format Decoder 'Runtime / s' 'Bandwidth / (MB/s)'
printf -- '|--------|---------------------|-------------|--------------------|\n'
for tool in cat fcat; do
    runtime=$( ( time $tool "$fname" | wc -c ) 2>&1 | sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
    bandwidth=$( python3 -c "print( int( round( $fileSize / 1e6 / $runtime ) ) )" )
    printf '| %6s | %19s | %11s | %18s |\n' "" "$tool" "$runtime" "$bandwidth"
done

fname=4GiB-base64
fileSize=$( stat -L --format=%s "$fname" )
for format in gz bgz pigz igz; do
    printf '\n| %6s | %13s | %10s | %18s |\n' Format Decoder 'Runtime / s' 'Bandwidth / (MB/s)'
    printf -- '|--------|---------------|-------------|--------------------|\n'
    crc32 "$fname.$format" &>/dev/null  # Make my QLC SSD cache the file into the SLC cache
    crc32 "$fname.$format" &>/dev/null  # Make my QLC SSD cache the file into the SLC cache

    for tool in gzip bgzip "bgzip -@ $( nproc )" pigz igzip "igzip -T $( nproc )" "src/tools/pragzip -P 1" "src/tools/pragzip -P $( nproc )"; do
        runtime=$( ( time $tool -d -c "$fname.$format" | wc -c ) 2>&1 | sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
        bandwidth=$( python3 -c "print( int( round( $fileSize / 1e6 / $runtime ) ) )" )
        printf '| %6s | %13s | %11s | %18s |\n' "$format" "${tool#src/tools/}" "$runtime" "$bandwidth"
    done
done

    | Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
    |--------|---------------|-------------|--------------------|
    |     gz |          gzip |      22.695 |                189 |
    |     gz |         bgzip |      15.821 |                271 |
    |     gz |   bgzip -@ 24 |      15.837 |                271 |
    |     gz |          pigz |      13.366 |                321 |
    |     gz |         igzip |       8.878 |                484 |
    |     gz |   igzip -T 24 |       9.123 |                471 |
    |     gz |  pragzip -P 1 |      19.180 |                224 |
    |     gz | pragzip -P 24 |       1.641 |               2617 |

    | Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
    |--------|---------------|-------------|--------------------|
    |    bgz |          gzip |      26.441 |                162 |
    |    bgz |         bgzip |      12.151 |                353 |
    |    bgz |   bgzip -@ 24 |       1.727 |               2487 |
    |    bgz |          pigz |      20.088 |                214 |
    |    bgz |         igzip |       9.451 |                454 |
    |    bgz |   igzip -T 24 |       9.498 |                452 |
    |    bgz |  pragzip -P 1 |      27.083 |                159 |
    |    bgz | pragzip -P 24 |       1.852 |               2319 |

    | Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
    |--------|---------------|-------------|--------------------|
    |   pigz |          gzip |      22.964 |                187 |
    |   pigz |         bgzip |      16.282 |                264 |
    |   pigz |   bgzip -@ 24 |      16.306 |                263 |
    |   pigz |          pigz |      13.509 |                318 |
    |   pigz |         igzip |       9.562 |                449 |
    |   pigz |   igzip -T 24 |       9.384 |                458 |
    |   pigz |  pragzip -P 1 |      22.193 |                194 |
    |   pigz | pragzip -P 24 |       1.839 |               2335 |

    | Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
    |--------|---------------|-------------|--------------------|
    |    igz |          gzip |      21.656 |                198 |
    |    igz |         bgzip |      15.076 |                285 |
    |    igz |   bgzip -@ 24 |      14.861 |                289 |
    |    igz |          pigz |      12.096 |                355 |
    |    igz |         igzip |       7.927 |                542 |
    |    igz |   igzip -T 24 |       7.887 |                545 |
    |    igz |  pragzip -P 1 |      15.463 |                278 |
    |    igz | pragzip -P 24 |       1.678 |               2560 |


# Create an incompressible random file
fileSize=$(( 4 * 1024 * 1024 * 1024 ))
fname=4GiB-random
head -c $fileSize /dev/urandom > $fname
pigz  -k -c "$fname" > "$fname.pigz"
bgzip    -c "$fname" > "$fname.bgz"
gzip  -k -c "$fname" > "$fname.gz"
igzip -k -c "$fname" > "$fname.igz"

printf '\n| %6s | %19s | %10s | %18s |\n' Format Decoder 'Runtime / s' 'Bandwidth / (MB/s)'
printf -- '|--------|---------------------|-------------|--------------------|\n'
for tool in cat fcat; do
    runtime=$( ( time $tool "$fname" | wc -c ) 2>&1 | sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
    bandwidth=$( python3 -c "print( int( round( $fileSize / 1e6 / $runtime ) ) )" )
    printf '| %6s | %19s | %11s | %18s |\n' "" "$tool" "$runtime" "$bandwidth"
done

fname=4GiB-random
fileSize=$( stat -L --format=%s -- "$fname" )

for format in gz bgz pigz igz; do
    printf '\n| %6s | %19s | %10s | %18s |\n' Format Decoder 'Runtime / s' 'Bandwidth / (MB/s)'
    printf -- '|--------|---------------------|-------------|--------------------|\n'
    crc32 "$fname.$format" &>/dev/null  # Make my QLC SSD cache the file into the SLC cache

    for tool in gzip bgzip "bgzip -@ $( nproc )" pigz igzip "igzip -T $( nproc )" "src/tools/pragzip -P 1" "src/tools/pragzip -P $( nproc )"; do
        runtime=$( ( time $tool -d -c "$fname.$format" | wc -c ) 2>&1 | sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
        bandwidth=$( python3 -c "print( int( round( $fileSize / 1e6 / $runtime ) ) )" )
        printf '| %6s | %19s | %11s | %18s |\n' "$format" "$tool" "$runtime" "$bandwidth"
    done
done

| Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
|--------|---------------|-------------|--------------------|
|        |           cat |       1.287 |               3337 |
|        |          fcat |       0.480 |               8948 |

| Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
|--------|---------------|-------------|--------------------|
|     gz |          gzip |      18.806 |                228 |
|     gz |         bgzip |       4.170 |               1030 |
|     gz |   bgzip -@ 24 |       4.087 |               1051 |
|     gz |          pigz |       4.391 |                978 |
|     gz |         igzip |       2.635 |               1630 |
|     gz |   igzip -T 24 |       2.573 |               1669 |
|     gz |  pragzip -P 1 |       1.244 |               3453 |
|     gz | pragzip -P 24 |       1.618 |               2654 |

| Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
|--------|---------------|-------------|--------------------|
|    bgz |          gzip |      20.119 |                213 |
|    bgz |         bgzip |       1.792 |               2397 |
|    bgz |   bgzip -@ 24 |       1.919 |               2238 |
|    bgz |          pigz |       9.072 |                473 |
|    bgz |         igzip |       1.775 |               2420 |
|    bgz |   igzip -T 24 |       1.733 |               2478 |
|    bgz |  pragzip -P 1 |       1.932 |               2223 |
|    bgz | pragzip -P 24 |       1.589 |               2703 |

| Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
|--------|---------------|-------------|--------------------|
|   pigz |          gzip |      19.810 |                217 |
|   pigz |         bgzip |       4.005 |               1072 |
|   pigz |   bgzip -@ 24 |       4.038 |               1064 |
|   pigz |          pigz |       4.062 |               1057 |
|   pigz |         igzip |       2.500 |               1718 |
|   pigz |   igzip -T 24 |       2.335 |               1839 |
|   pigz |  pragzip -P 1 |       1.027 |               4182 |
|   pigz | pragzip -P 24 |       1.618 |               2654 |

| Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
|--------|---------------|-------------|--------------------|
|    igz |          gzip |      26.927 |                160 |
|    igz |         bgzip |       5.601 |                767 |
|    igz |   bgzip -@ 24 |       5.446 |                789 |
|    igz |          pigz |       5.194 |                827 |
|    igz |         igzip |       3.839 |               1119 |
|    igz |   igzip -T 24 |       3.888 |               1105 |
|    igz |  pragzip -P 1 |       4.548 |                944 |
|    igz | pragzip -P 24 |       1.479 |               2904 |



# Benchmark pragzip scaling over threads

for fname in 4GiB-base64 4GiB-random; do
    fileSize=$( stat -L --format=%s -- "$fname" )
    format=gz
    printf '\n| %14s | %13s | %10s | %18s |\n' File Decoder 'Runtime / s' 'Bandwidth / (MB/s)'
    printf -- '|----------------|---------------|-------------|--------------------|\n'
    crc32 "$fname.$format" &>/dev/null  # Make my QLC SSD cache the file into the SLC cache

    for parallelization in 1 2 4 8 12 16 24 32; do
        tool="src/tools/pragzip -P $parallelization"
        visibleTool="pragzip -P $parallelization"
        runtime=$( ( time $tool -d -c "$fname.$format" | wc -c ) 2>&1 | sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
        bandwidth=$( python3 -c "print( int( round( $fileSize / 1e6 / $runtime ) ) )" )
        printf '| %14s | %13s | %11s | %18s |\n' "$fname.$format" "$visibleTool" "$runtime" "$bandwidth"
    done
done

    |           File |       Decoder | Runtime / s | Bandwidth / (MB/s) |
    |----------------|---------------|-------------|--------------------|
    | 4GiB-base64.gz |  pragzip -P 1 |      19.763 |                217 |
    | 4GiB-base64.gz |  pragzip -P 2 |       9.782 |                439 |
    | 4GiB-base64.gz |  pragzip -P 4 |       5.122 |                839 |
    | 4GiB-base64.gz |  pragzip -P 8 |       2.905 |               1478 |
    | 4GiB-base64.gz | pragzip -P 12 |       2.241 |               1917 |
    | 4GiB-base64.gz | pragzip -P 16 |       1.890 |               2272 |
    | 4GiB-base64.gz | pragzip -P 24 |       1.703 |               2522 |
    | 4GiB-base64.gz | pragzip -P 32 |       1.716 |               2503 |

    |           File |       Decoder | Runtime / s | Bandwidth / (MB/s) |
    |----------------|---------------|-------------|--------------------|
    | 4GiB-random.gz |  pragzip -P 1 |       1.289 |               3332 |
    | 4GiB-random.gz |  pragzip -P 2 |       1.961 |               2190 |
    | 4GiB-random.gz |  pragzip -P 4 |       1.431 |               3001 |
    | 4GiB-random.gz |  pragzip -P 8 |       1.276 |               3366 |
    | 4GiB-random.gz | pragzip -P 12 |       1.306 |               3289 |
    | 4GiB-random.gz | pragzip -P 16 |       1.439 |               2985 |
    | 4GiB-random.gz | pragzip -P 24 |       1.509 |               2846 |
    | 4GiB-random.gz | pragzip -P 32 |       1.530 |               2807 |
*/
