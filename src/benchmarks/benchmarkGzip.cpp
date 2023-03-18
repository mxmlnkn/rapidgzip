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
        Runtime / s: 1.597 <= 1.603 +- 0.006 <= 1.607
        Bandwidth on Encoded Data / (MB/s): 253.9 <= 254.5 +- 0.9 <= 255.5
        Bandwidth on Decoded Data / (MB/s): 334.1 <= 334.9 +- 1.2 <= 336.3
    Decompressed 407988639 B to 536870912 B with zlib:
        Runtime / s: 1.8927 <= 1.8956 +- 0.0027 <= 1.8979
        Bandwidth on Encoded Data / (MB/s): 215 <= 215.2 +- 0.3 <= 215.6
        Bandwidth on Decoded Data / (MB/s): 282.9 <= 283.2 +- 0.4 <= 283.6
    Decoded 15844 deflate blocks
    Decoded 15844 deflate blocks
    Decoded 15844 deflate blocks
    Decompressed 407988639 B to 536870912 B with pragzip (serial):
        Runtime / s: 3.536 <= 3.544 +- 0.011 <= 3.556
        Bandwidth on Encoded Data / (MB/s): 114.7 <= 115.1 +- 0.3 <= 115.4
        Bandwidth on Decoded Data / (MB/s): 151 <= 151.5 +- 0.5 <= 151.8
    Decompressed 407988639 B to 536870912 B with pragzip (parallel):
        Runtime / s: 0.331 <= 0.345 +- 0.014 <= 0.36
        Bandwidth on Encoded Data / (MB/s): 1130 <= 1180 +- 50 <= 1230
        Bandwidth on Decoded Data / (MB/s): 1490 <= 1560 +- 60 <= 1620
    Decompressed 407988639 B to 536870912 B with pragzip (parallel + index):
        Runtime / s: 0.217 <= 0.234 +- 0.015 <= 0.246
        Bandwidth on Encoded Data / (MB/s): 1660 <= 1750 +- 120 <= 1880
        Bandwidth on Decoded Data / (MB/s): 2180 <= 2300 +- 150 <= 2470

      -> pragzip is almost twice as slow as zlib :/
         -> Decoding with pragzip in parallel with an index is now much slower because of internal zlib usage!

time gzip -d -k -c small.gz | wc -c
    real  0m2.830s
  -> pragzip is ~28% slower than gzip 1.10. Maybe slower than the above benchmarks because of I/O?

pigz -c small > small.pigz
cmake --build . -- benchmarkGzip && src/benchmarks/benchmarkGzip small.pigz

    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=0):
        Runtime / s: 1.369 <= 1.395 +- 0.023 <= 1.412
        Bandwidth on Encoded Data / (MB/s): 289 <= 293 +- 5 <= 298
        Bandwidth on Decoded Data / (MB/s): 380 <= 385 +- 6 <= 392
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=1):
        Runtime / s: 0.843 <= 0.849 +- 0.006 <= 0.855
        Bandwidth on Encoded Data / (MB/s): 478 <= 481 +- 4 <= 485
        Bandwidth on Decoded Data / (MB/s): 628 <= 633 +- 5 <= 637
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=2):
        Runtime / s: 0.686 <= 0.695 +- 0.01 <= 0.705
        Bandwidth on Encoded Data / (MB/s): 580 <= 588 +- 8 <= 596
        Bandwidth on Decoded Data / (MB/s): 762 <= 773 +- 11 <= 783
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=4):
        Runtime / s: 0.551 <= 0.565 +- 0.014 <= 0.579
        Bandwidth on Encoded Data / (MB/s): 706 <= 723 +- 18 <= 741
        Bandwidth on Decoded Data / (MB/s): 928 <= 950 +- 23 <= 974
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=8):
        Runtime / s: 0.451 <= 0.458 +- 0.007 <= 0.465
        Bandwidth on Encoded Data / (MB/s): 878 <= 892 +- 14 <= 905
        Bandwidth on Decoded Data / (MB/s): 1154 <= 1173 +- 18 <= 1190
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=16):
        Runtime / s: 0.407 <= 0.411 +- 0.004 <= 0.415
        Bandwidth on Encoded Data / (MB/s): 985 <= 993 +- 9 <= 1003
        Bandwidth on Decoded Data / (MB/s): 1295 <= 1305 +- 12 <= 1319
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=24):
        Runtime / s: 0.394 <= 0.403 +- 0.014 <= 0.42
        Bandwidth on Encoded Data / (MB/s): 970 <= 1010 +- 30 <= 1040
        Bandwidth on Decoded Data / (MB/s): 1280 <= 1330 +- 50 <= 1360
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=32):
        Runtime / s: 0.377 <= 0.391 +- 0.016 <= 0.408
        Bandwidth on Encoded Data / (MB/s): 1000 <= 1050 +- 40 <= 1080
        Bandwidth on Decoded Data / (MB/s): 1320 <= 1370 +- 50 <= 1420
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=64):
        Runtime / s: 0.405 <= 0.415 +- 0.01 <= 0.424
        Bandwidth on Encoded Data / (MB/s): 964 <= 984 +- 23 <= 1009
        Bandwidth on Decoded Data / (MB/s): 1270 <= 1290 +- 30 <= 1330
    Decompressed 408430549 B to 536870912 B with pragzip (parallel, nBlocksToSkip=128):
        Runtime / s: 0.469 <= 0.475 +- 0.008 <= 0.485
        Bandwidth on Encoded Data / (MB/s): 842 <= 860 +- 15 <= 870
        Bandwidth on Decoded Data / (MB/s): 1107 <= 1130 +- 20 <= 1144

  -> 32 blocks as chunks are the fastest!, starting from 16, it seems to be somewhat saturated already.
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
        Runtime / s: 1.584 <= 1.587 +- 0.005 <= 1.593
        Bandwidth on Encoded Data / (MB/s): 256.3 <= 257.4 +- 0.9 <= 257.9
        Bandwidth on Decoded Data / (MB/s): 336.9 <= 338.3 +- 1.2 <= 339
    Decompressed 408430549 B to 536870912 B with zlib:
        Runtime / s: 1.86 <= 1.866 +- 0.007 <= 1.873
        Bandwidth on Encoded Data / (MB/s): 218 <= 218.8 +- 0.8 <= 219.5
        Bandwidth on Decoded Data / (MB/s): 286.6 <= 287.7 +- 1 <= 288.6
    Decoded 37796 deflate blocks
    Decoded 37796 deflate blocks
    Decoded 37796 deflate blocks
    Decompressed 408430549 B to 536870912 B with pragzip (serial):
        Runtime / s: 5.055 <= 5.075 +- 0.02 <= 5.096
        Bandwidth on Encoded Data / (MB/s): 80.1 <= 80.5 +- 0.3 <= 80.8
        Bandwidth on Decoded Data / (MB/s): 105.3 <= 105.8 +- 0.4 <= 106.2
    Decompressed 408430549 B to 536870912 B with pragzip (parallel):
        Runtime / s: 0.396 <= 0.399 +- 0.004 <= 0.404
        Bandwidth on Encoded Data / (MB/s): 1011 <= 1024 +- 11 <= 1032
        Bandwidth on Decoded Data / (MB/s): 1329 <= 1346 +- 15 <= 1357
    Decompressed 408430549 B to 536870912 B with pragzip (parallel + index):
        Runtime / s: 0.195 <= 0.2 +- 0.005 <= 0.205
        Bandwidth on Encoded Data / (MB/s): 1990 <= 2040 +- 50 <= 2090
        Bandwidth on Decoded Data / (MB/s): 2610 <= 2680 +- 70 <= 2750


bgzip -c small > small.bgz
cmake --build . -- benchmarkGzip && src/benchmarks/benchmarkGzip small.bgz

    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=0):
        Runtime / s: 0.46 <= 0.49 +- 0.04 <= 0.54
        Bandwidth on Encoded Data / (MB/s): 780 <= 860 +- 70 <= 900
        Bandwidth on Decoded Data / (MB/s): 1000 <= 1110 +- 90 <= 1160
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=1):
        Runtime / s: 0.3 <= 0.31 +- 0.009 <= 0.319
        Bandwidth on Encoded Data / (MB/s): 1300 <= 1340 +- 40 <= 1380
        Bandwidth on Decoded Data / (MB/s): 1690 <= 1730 +- 50 <= 1790
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=2):
        Runtime / s: 0.294 <= 0.303 +- 0.008 <= 0.309
        Bandwidth on Encoded Data / (MB/s): 1340 <= 1370 +- 30 <= 1410
        Bandwidth on Decoded Data / (MB/s): 1740 <= 1780 +- 50 <= 1820
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=4):
        Runtime / s: 0.286 <= 0.292 +- 0.006 <= 0.298
        Bandwidth on Encoded Data / (MB/s): 1392 <= 1423 +- 30 <= 1451
        Bandwidth on Decoded Data / (MB/s): 1800 <= 1840 +- 40 <= 1880
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=8):
        Runtime / s: 0.274 <= 0.281 +- 0.006 <= 0.285
        Bandwidth on Encoded Data / (MB/s): 1450 <= 1480 +- 30 <= 1510
        Bandwidth on Decoded Data / (MB/s): 1880 <= 1910 +- 40 <= 1960
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=16):
        Runtime / s: 0.249 <= 0.261 +- 0.01 <= 0.267
        Bandwidth on Encoded Data / (MB/s): 1550 <= 1590 +- 60 <= 1670
        Bandwidth on Decoded Data / (MB/s): 2010 <= 2060 +- 80 <= 2160
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=24):
        Runtime / s: 0.255 <= 0.259 +- 0.004 <= 0.263
        Bandwidth on Encoded Data / (MB/s): 1576 <= 1600 +- 25 <= 1625
        Bandwidth on Decoded Data / (MB/s): 2040 <= 2070 +- 30 <= 2100
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=32):
        Runtime / s: 0.253 <= 0.257 +- 0.004 <= 0.26
        Bandwidth on Encoded Data / (MB/s): 1600 <= 1618 +- 23 <= 1644
        Bandwidth on Decoded Data / (MB/s): 2069 <= 2093 +- 30 <= 2126
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=64):
        Runtime / s: 0.251 <= 0.261 +- 0.013 <= 0.275
        Bandwidth on Encoded Data / (MB/s): 1510 <= 1590 +- 80 <= 1660
        Bandwidth on Decoded Data / (MB/s): 1950 <= 2060 +- 100 <= 2140
    Decompressed 415096389 B to 536870912 B with pragzip (parallel, nBlocksToSkip=128):
        Runtime / s: 0.2841 <= 0.286 +- 0.0024 <= 0.2887
        Bandwidth on Encoded Data / (MB/s): 1438 <= 1451 +- 12 <= 1461
        Bandwidth on Decoded Data / (MB/s): 1859 <= 1877 +- 16 <= 1890

  -> 16 blocks for bgz would be the best in this case!
     -> pragzip --analyze small.bgz
        - There are 8226 blocks and gzip streams in total
        - Each block is ~ 50400 B encoded and 65280 B (63.75 KiB) decoded,
        - So, when skipping ~32 blocks (~33 chunked), we have ~1.54 MiB encoded and ~2 MiB decoded data
          -> This is only a factor 2 less than the optimum for pigz, so that we might use the same blocking criterium,
             or not.

    Decompressed 415096389 B to 536870912 B with libarchive:
        Runtime / s: 1.744 <= 1.747 +- 0.005 <= 1.752
        Bandwidth on Encoded Data / (MB/s): 236.9 <= 237.7 +- 0.7 <= 238.1
        Bandwidth on Decoded Data / (MB/s): 306.4 <= 307.4 +- 0.8 <= 307.9
    Decompressed 415096389 B to 536870912 B with zlib:
        Runtime / s: 1.9947 <= 1.9951 +- 0.0004 <= 1.9956
        Bandwidth on Encoded Data / (MB/s): 208.01 <= 208.06 +- 0.05 <= 208.1
        Bandwidth on Decoded Data / (MB/s): 269.03 <= 269.09 +- 0.06 <= 269.15
    Decoded 8226 deflate blocks
    Decoded 8226 deflate blocks
    Decoded 8226 deflate blocks
    Decompressed 415096389 B to 536870912 B with pragzip (serial):
        Runtime / s: 3.1003 <= 3.1015 +- 0.002 <= 3.1038
        Bandwidth on Encoded Data / (MB/s): 133.74 <= 133.84 +- 0.08 <= 133.89
        Bandwidth on Decoded Data / (MB/s): 172.97 <= 173.1 +- 0.11 <= 173.16
    Decompressed 415096389 B to 536870912 B with pragzip (parallel):
        Runtime / s: 0.247 <= 0.261 +- 0.013 <= 0.272
        Bandwidth on Encoded Data / (MB/s): 1520 <= 1590 +- 80 <= 1680
        Bandwidth on Decoded Data / (MB/s): 1970 <= 2060 +- 100 <= 2170
    Decompressed 415096389 B to 536870912 B with pragzip (parallel + index):
        Runtime / s: 0.262 <= 0.265 +- 0.003 <= 0.268
        Bandwidth on Encoded Data / (MB/s): 1549 <= 1565 +- 18 <= 1584
        Bandwidth on Decoded Data / (MB/s): 2003 <= 2024 +- 23 <= 2049

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
cmake --build . -- benchmarkGzip && src/benchmarks/benchmarkGzip large.bgz

    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=0):
        Runtime / s: 3.6 <= 4.9 +- 2.2 <= 7.5
        Bandwidth on Encoded Data / (MB/s): 440 <= 760 +- 270 <= 920
        Bandwidth on Decoded Data / (MB/s): 600 <= 1000 +- 400 <= 1200
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=1):
        Runtime / s: 2.42 <= 2.56 +- 0.15 <= 2.73
        Bandwidth on Encoded Data / (MB/s): 1220 <= 1300 +- 80 <= 1370
        Bandwidth on Decoded Data / (MB/s): 1570 <= 1680 +- 100 <= 1770
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=2):
        Runtime / s: 2.5 <= 2.9 +- 0.3 <= 3.2
        Bandwidth on Encoded Data / (MB/s): 1050 <= 1180 +- 140 <= 1330
        Bandwidth on Decoded Data / (MB/s): 1360 <= 1520 +- 190 <= 1720
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=4):
        Runtime / s: 2.26 <= 2.45 +- 0.2 <= 2.65
        Bandwidth on Encoded Data / (MB/s): 1250 <= 1360 +- 110 <= 1470
        Bandwidth on Decoded Data / (MB/s): 1620 <= 1760 +- 140 <= 1900
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=8):
        Runtime / s: 2.134 <= 2.155 +- 0.019 <= 2.17
        Bandwidth on Encoded Data / (MB/s): 1530 <= 1541 +- 14 <= 1556
        Bandwidth on Decoded Data / (MB/s): 1979 <= 1993 +- 18 <= 2013
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=16):
        Runtime / s: 1.95 <= 1.98 +- 0.04 <= 2.03
        Bandwidth on Encoded Data / (MB/s): 1640 <= 1680 +- 40 <= 1700
        Bandwidth on Decoded Data / (MB/s): 2120 <= 2170 +- 50 <= 2200
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=24):
        Runtime / s: 1.867 <= 1.891 +- 0.021 <= 1.904
        Bandwidth on Encoded Data / (MB/s): 1744 <= 1756 +- 19 <= 1779
        Bandwidth on Decoded Data / (MB/s): 2256 <= 2272 +- 25 <= 2300
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=32):
        Runtime / s: 1.85 <= 1.862 +- 0.02 <= 1.885
        Bandwidth on Encoded Data / (MB/s): 1762 <= 1783 +- 19 <= 1795
        Bandwidth on Decoded Data / (MB/s): 2279 <= 2306 +- 24 <= 2322
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=64):
        Runtime / s: 1.78 <= 1.83 +- 0.04 <= 1.86
        Bandwidth on Encoded Data / (MB/s): 1790 <= 1820 +- 40 <= 1870
        Bandwidth on Decoded Data / (MB/s): 2310 <= 2350 +- 50 <= 2410
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel, nBlocksToSkip=128):
        Runtime / s: 1.75 <= 1.79 +- 0.04 <= 1.83
        Bandwidth on Encoded Data / (MB/s): 1820 <= 1860 +- 40 <= 1890
        Bandwidth on Decoded Data / (MB/s): 2350 <= 2400 +- 50 <= 2450

  -> I don't see the peak yet! I guess the benchmark with the 512 MiB base64 file only topped out at 16 blocks
     per chunk because then the tail might produce stragglers!
     -> So we are fine using a chunking criterium shared with pigz of roughly 4 MiB of encoded data or maybe more.
        -> Note that for generic gzip files, we would need roughly 32 MiB of encoded data because of the much slower
           block finder amortizing slower!

    Decompressed 3320779389 B to 4294967296 B with libarchive:
        Runtime / s: 14.05 <= 14.08 +- 0.03 <= 14.11
        Bandwidth on Encoded Data / (MB/s): 235.3 <= 235.9 +- 0.5 <= 236.3
        Bandwidth on Decoded Data / (MB/s): 304.3 <= 305.1 +- 0.7 <= 305.6
    Decompressed 3320779389 B to 4294967296 B with zlib:
        Runtime / s: 15.99 <= 16.04 +- 0.04 <= 16.07
        Bandwidth on Encoded Data / (MB/s): 206.7 <= 207.1 +- 0.5 <= 207.6
        Bandwidth on Decoded Data / (MB/s): 267.3 <= 267.8 +- 0.7 <= 268.5
    Decoded 65795 deflate blocks
    Decoded 65795 deflate blocks
    Decoded 65795 deflate blocks
    Decompressed 3320779389 B to 4294967296 B with pragzip (serial):
        Runtime / s: 24.67 <= 24.88 +- 0.24 <= 25.15
        Bandwidth on Encoded Data / (MB/s): 132.1 <= 133.5 +- 1.3 <= 134.6
        Bandwidth on Decoded Data / (MB/s): 170.8 <= 172.7 +- 1.7 <= 174.1
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel):
        Runtime / s: 1.837 <= 1.845 +- 0.012 <= 1.859
        Bandwidth on Encoded Data / (MB/s): 1787 <= 1800 +- 11 <= 1808
        Bandwidth on Decoded Data / (MB/s): 2311 <= 2328 +- 15 <= 2338
    Decompressed 3320779389 B to 4294967296 B with pragzip (parallel + index):
        Runtime / s: 2.02 <= 2.028 +- 0.012 <= 2.041
        Bandwidth on Encoded Data / (MB/s): 1627 <= 1637 +- 9 <= 1644
        Bandwidth on Decoded Data / (MB/s): 2104 <= 2118 +- 12 <= 2127

time bgzip --threads $( nproc ) -d -c large.bgz | wc -c
    real  0m1.408s
  -> 30% faster than parallel pragzip thanks to internal usage of zlib


base64 /dev/urandom | head -c $(( 1024*1024*1024 )) > large
pigz -k -c large > large.pigz
cmake --build . -- benchmarkGzip && src/benchmarks/benchmarkGzip large.pigz

    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=0):
        Runtime / s: 2.74 <= 2.77 +- 0.03 <= 2.8
        Bandwidth on Encoded Data / (MB/s): 292 <= 295 +- 3 <= 299
        Bandwidth on Decoded Data / (MB/s): 384 <= 388 +- 4 <= 392
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=1):
        Runtime / s: 1.714 <= 1.731 +- 0.015 <= 1.742
        Bandwidth on Encoded Data / (MB/s): 469 <= 472 +- 4 <= 477
        Bandwidth on Decoded Data / (MB/s): 616 <= 620 +- 5 <= 626
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=2):
        Runtime / s: 1.376 <= 1.383 +- 0.008 <= 1.392
        Bandwidth on Encoded Data / (MB/s): 587 <= 591 +- 4 <= 594
        Bandwidth on Decoded Data / (MB/s): 771 <= 776 +- 5 <= 780
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=4):
        Runtime / s: 1.084 <= 1.094 +- 0.01 <= 1.104
        Bandwidth on Encoded Data / (MB/s): 740 <= 747 +- 7 <= 753
        Bandwidth on Decoded Data / (MB/s): 972 <= 981 +- 9 <= 990
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=8):
        Runtime / s: 0.895 <= 0.9 +- 0.008 <= 0.91
        Bandwidth on Encoded Data / (MB/s): 898 <= 907 +- 8 <= 913
        Bandwidth on Decoded Data / (MB/s): 1181 <= 1193 +- 11 <= 1199
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=16):
        Runtime / s: 0.767 <= 0.785 +- 0.016 <= 0.797
        Bandwidth on Encoded Data / (MB/s): 1025 <= 1041 +- 22 <= 1065
        Bandwidth on Decoded Data / (MB/s): 1348 <= 1368 +- 28 <= 1400
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=24):
        Runtime / s: 0.752 <= 0.768 +- 0.015 <= 0.782
        Bandwidth on Encoded Data / (MB/s): 1045 <= 1064 +- 21 <= 1086
        Bandwidth on Decoded Data / (MB/s): 1373 <= 1399 +- 27 <= 1427
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=32):
        Runtime / s: 0.7544 <= 0.7549 +- 0.0006 <= 0.7555
        Bandwidth on Encoded Data / (MB/s): 1081.2 <= 1082 +- 0.8 <= 1082.8
        Bandwidth on Decoded Data / (MB/s): 1421.2 <= 1422.3 +- 1.1 <= 1423.3
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=64):
        Runtime / s: 0.796 <= 0.8 +- 0.005 <= 0.806
        Bandwidth on Encoded Data / (MB/s): 1013 <= 1021 +- 6 <= 1026
        Bandwidth on Decoded Data / (MB/s): 1332 <= 1342 +- 8 <= 1348
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel, nBlocksToSkip=128):
        Runtime / s: 0.82 <= 0.839 +- 0.021 <= 0.862
        Bandwidth on Encoded Data / (MB/s): 948 <= 974 +- 25 <= 997
        Bandwidth on Decoded Data / (MB/s): 1250 <= 1280 +- 30 <= 1310

    Decompressed 816860634 B to 1073741824 B with libarchive:
        Runtime / s: 3.199 <= 3.202 +- 0.003 <= 3.206
        Bandwidth on Encoded Data / (MB/s): 254.82 <= 255.1 +- 0.26 <= 255.35
        Bandwidth on Decoded Data / (MB/s): 335 <= 335.3 +- 0.3 <= 335.6
    Decompressed 816860634 B to 1073741824 B with zlib:
        Runtime / s: 3.7798 <= 3.7817 +- 0.0017 <= 3.7831
        Bandwidth on Encoded Data / (MB/s): 215.92 <= 216.01 +- 0.1 <= 216.11
        Bandwidth on Decoded Data / (MB/s): 283.83 <= 283.93 +- 0.12 <= 284.07
    Decoded 75802 deflate blocks
    Decoded 75802 deflate blocks
    Decoded 75802 deflate blocks
    Decompressed 816860634 B to 1073741824 B with pragzip (serial):
        Runtime / s: 9.9 <= 10.7 +- 0.7 <= 11.1
        Bandwidth on Encoded Data / (MB/s): 74 <= 77 +- 5 <= 82
        Bandwidth on Decoded Data / (MB/s): 97 <= 101 +- 6 <= 108
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel):
        Runtime / s: 0.7585 <= 0.7602 +- 0.0028 <= 0.7635
        Bandwidth on Encoded Data / (MB/s): 1070 <= 1075 +- 4 <= 1077
        Bandwidth on Decoded Data / (MB/s): 1406 <= 1412 +- 5 <= 1416
    Decompressed 816860634 B to 1073741824 B with pragzip (parallel + index):
        Runtime / s: 0.376 <= 0.39 +- 0.018 <= 0.411
        Bandwidth on Encoded Data / (MB/s): 1990 <= 2100 +- 100 <= 2170
        Bandwidth on Decoded Data / (MB/s): 2610 <= 2750 +- 130 <= 2850

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
fname=4GB
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

fname=4GB
fileSize=$( stat -L --format=%s "$fname" )
for format in gz bgz pigz igz; do
    printf '\n| %6s | %13s | %10s | %18s |\n' Format Decoder 'Runtime / s' 'Bandwidth / (MB/s)'
    printf -- '|--------|---------------|-------------|--------------------|\n'
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
