/**
 * @file Executes benchmarks for varying gzip decompressors (zlib, libarchive, rapidgzip: sequential, parallel,
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


using namespace rapidgzip;


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
decompressWithRapidgzip( const std::string& fileName )
{
    using namespace rapidgzip;

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
decompressWithRapidgzipParallel( const std::string& fileName )
{
    size_t totalDecodedBytes = 0;

    rapidgzip::ParallelGzipReader gzipReader( std::make_unique<StandardFileReader>( fileName ) );
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
decompressWithRapidgzipParallelChunked( const std::string& fileName,
                                        const size_t       nBlocksToSkip )
{
    size_t totalDecodedBytes = 0;

    const auto spacing = ( nBlocksToSkip + 1 ) * 32_Ki;
    rapidgzip::ParallelGzipReader gzipReader( std::make_unique<StandardFileReader>( fileName ), 0, spacing );
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

    return { fileName, readGzipIndex( std::make_unique<StandardFileReader>( indexFile ) ) };
}


[[nodiscard]] size_t
decompressWithRapidgzipParallelIndex( const std::pair<std::string, GzipIndex>& files )
{
    const auto& [fileName, index] = files;

    size_t totalDecodedBytes = 0;

    rapidgzip::ParallelGzipReader gzipReader( std::make_unique<StandardFileReader>( fileName ) );
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
    const auto fileContents = readFile<std::vector<uint8_t> >( fileName );

    std::cout << "\n== Benchmarking with rapidgzip in parallel with different decoding chunk sizes ==\n\n";

    const auto [sizeLibArchive, durationsLibArchive] = benchmarkFunction<3>(
        [&fileContents] () { return decompressWithLibArchive( fileContents ); } );
    //std::cout << "Decompressed " << fileContents.size() << " B to " << sizeLibArchive << " B with libarchive:\n";
    //printBandwidths( durationsLibArchive, fileContents.size(), sizeLibArchive );

    const auto expectedSize = sizeLibArchive;

    for ( size_t nBlocksToSkip : { 0, 1, 2, 4, 8, 16, 24, 32, 64, 128 } ) {
        const auto [sizeRapidgzipParallel, durationsRapidgzipParallel] = benchmarkFunction<3>(
            [&fileName, nBlocksToSkip] () { return decompressWithRapidgzipParallelChunked( fileName, nBlocksToSkip ); } );
        if ( sizeRapidgzipParallel == expectedSize ) {
            std::cout << "Decompressed " << fileContents.size() << " B to " << sizeRapidgzipParallel << " B "
                      << "with rapidgzip (parallel, nBlocksToSkip=" << nBlocksToSkip << "):\n";
            printBandwidths( durationsRapidgzipParallel, fileContents.size(), sizeRapidgzipParallel );
        } else {
            std::cerr << "Decompressing with rapidgzip (parallel, nBlocksToSkip=" << nBlocksToSkip
                      << ") decoded a different amount (" << sizeRapidgzipParallel
                      << ") than libarchive (" << expectedSize << ")!\n";
        }
    }

    std::cout << "\n";
}


void
benchmarkDecompression( const std::string& fileName )
{
    const auto fileContents = readFile<std::vector<uint8_t> >( fileName );

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

    const auto [sizeRapidgzip, durationsRapidgzip] = benchmarkFunction<3>(
        [&fileName] () { return decompressWithRapidgzip( fileName ); } );
    if ( sizeRapidgzip == expectedSize ) {
        std::cout << "Decompressed " << fileContents.size() << " B to " << sizeRapidgzip << " B "
                  << "with rapidgzip (serial):\n";
        printBandwidths( durationsRapidgzip, fileContents.size(), sizeRapidgzip );
    } else {
        std::cerr << "Decompressing with rapidgzip (serial) decoded a different amount than libarchive!\n";
    }

    const auto [sizeRapidgzipParallel, durationsRapidgzipParallel] = benchmarkFunction<3>(
        [&fileName] () { return decompressWithRapidgzipParallel( fileName ); } );
    if ( sizeRapidgzipParallel == expectedSize ) {
        std::cout << "Decompressed " << fileContents.size() << " B to " << sizeRapidgzipParallel << " B "
                  << "with rapidgzip (parallel):\n";
        printBandwidths( durationsRapidgzipParallel, fileContents.size(), sizeRapidgzipParallel );
    } else {
        throw std::logic_error( "Decompressing with rapidgzip (parallel) decoded a different amount ("
                                + std::to_string( sizeRapidgzipParallel ) + ") than libarchive ("
                                + std::to_string( expectedSize ) + ")!" );
    }

    const auto [sizeRapidgzipParallelIndex, durationsRapidgzipParallelIndex] = benchmarkFunction<3>(
        [&fileName] () { return createGzipIndex( fileName ); }, decompressWithRapidgzipParallelIndex );
    if ( sizeRapidgzipParallelIndex == expectedSize ) {
        std::cout << "Decompressed " << fileContents.size() << " B to " << sizeRapidgzipParallelIndex << " B "
                  << "with rapidgzip (parallel + index):\n";
        printBandwidths( durationsRapidgzipParallelIndex, fileContents.size(), sizeRapidgzipParallelIndex );
    } else {
        throw std::logic_error( "Decompressing with rapidgzip (parallel + index) decoded a different amount than "
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
        Runtime / s: 1.501 <= 1.507 +- 0.007 <= 1.515
        Bandwidth on Encoded Data / (MB/s): 269.4 <= 270.8 +- 1.3 <= 271.8
        Bandwidth on Decoded Data / (MB/s): 354.5 <= 356.4 +- 1.7 <= 357.6
    Decompressed 407988639 B to 536870912 B with zlib:
        Runtime / s: 1.557 <= 1.565 +- 0.012 <= 1.579
        Bandwidth on Encoded Data / (MB/s): 258.3 <= 260.7 +- 2.0 <= 262.1
        Bandwidth on Decoded Data / (MB/s): 339.9 <= 343.0 +- 2.7 <= 344.9
    Decoded 15844 deflate blocks
    Decoded 15844 deflate blocks
    Decoded 15844 deflate blocks
    Decompressed 407988639 B to 536870912 B with rapidgzip (serial):
        Runtime / s: 2.238 <= 2.243 +- 0.008 <= 2.252
        Bandwidth on Encoded Data / (MB/s): 181.2 <= 181.9 +- 0.6 <= 182.3
        Bandwidth on Decoded Data / (MB/s): 238.4 <= 239.4 +- 0.8 <= 239.9
    Decompressed 407988639 B to 536870912 B with rapidgzip (parallel):
        Runtime / s: 0.193 <= 0.207 +- 0.018 <= 0.227
        Bandwidth on Encoded Data / (MB/s): 1800 <= 1980 +- 160 <= 2110
        Bandwidth on Decoded Data / (MB/s): 2370 <= 2610 +- 210 <= 2780
    Decompressed 407988639 B to 536870912 B with rapidgzip (parallel + index):
        Runtime / s: 0.178 <= 0.181 +- 0.005 <= 0.187
        Bandwidth on Encoded Data / (MB/s): 2190 <= 2250 +- 60 <= 2290
        Bandwidth on Decoded Data / (MB/s): 2880 <= 2960 +- 70 <= 3010

      -> rapidgzip is significantly slower than libarchive
         -> Decoding with rapidgzip in parallel with an index is now much faster because of internal zlib usage!

time gzip -d -k -c small.gz | wc -c
    real  0m2.830s
  -> rapidgzip is ~28% slower than gzip 1.10. Maybe slower than the above benchmarks because of I/O?

pigz -c small > small.pigz
cmake --build . -- benchmarkGzip && src/benchmarks/benchmarkGzip small.pigz

    Decompressed 408430549 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=0):
        Runtime / s: 0.99 <= 1.03 +- 0.08 <= 1.12
        Bandwidth on Encoded Data / (MB/s): 365 <= 397 +- 28 <= 415
        Bandwidth on Decoded Data / (MB/s): 480 <= 520 +- 40 <= 540
    Decompressed 408430549 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=1):
        Runtime / s: 0.561 <= 0.568 +- 0.006 <= 0.573
        Bandwidth on Encoded Data / (MB/s): 713 <= 719 +- 8 <= 728
        Bandwidth on Decoded Data / (MB/s): 938 <= 945 +- 10 <= 957
    Decompressed 408430549 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=2):
        Runtime / s: 0.471 <= 0.475 +- 0.004 <= 0.478
        Bandwidth on Encoded Data / (MB/s): 855 <= 859 +- 7 <= 867
        Bandwidth on Decoded Data / (MB/s): 1124 <= 1130 +- 9 <= 1140
    Decompressed 408430549 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=4):
        Runtime / s: 0.323 <= 0.336 +- 0.015 <= 0.352
        Bandwidth on Encoded Data / (MB/s): 1160 <= 1220 +- 50 <= 1260
        Bandwidth on Decoded Data / (MB/s): 1530 <= 1600 +- 70 <= 1660
    Decompressed 408430549 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=8):
        Runtime / s: 0.2337 <= 0.2357 +- 0.0019 <= 0.2375
        Bandwidth on Encoded Data / (MB/s): 1720 <= 1733 +- 14 <= 1748
        Bandwidth on Decoded Data / (MB/s): 2260 <= 2278 +- 18 <= 2297
    Decompressed 408430549 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=16):
        Runtime / s: 0.186 <= 0.197 +- 0.015 <= 0.215
        Bandwidth on Encoded Data / (MB/s): 1900 <= 2080 +- 150 <= 2190
        Bandwidth on Decoded Data / (MB/s): 2500 <= 2730 +- 200 <= 2880
    Decompressed 408430549 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=24):
        Runtime / s: 0.191 <= 0.195 +- 0.003 <= 0.197
        Bandwidth on Encoded Data / (MB/s): 2070 <= 2090 +- 40 <= 2140
        Bandwidth on Decoded Data / (MB/s): 2720 <= 2750 +- 50 <= 2810
    Decompressed 408430549 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=32):
        Runtime / s: 0.191 <= 0.194 +- 0.005 <= 0.199
        Bandwidth on Encoded Data / (MB/s): 2050 <= 2110 +- 50 <= 2140
        Bandwidth on Decoded Data / (MB/s): 2700 <= 2770 +- 70 <= 2820
    Decompressed 408430549 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=64):
        Runtime / s: 0.190 <= 0.199 +- 0.007 <= 0.205
        Bandwidth on Encoded Data / (MB/s): 2000 <= 2060 +- 80 <= 2140
        Bandwidth on Decoded Data / (MB/s): 2620 <= 2700 +- 100 <= 2820
    Decompressed 408430549 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=128):
        Runtime / s: 0.197 <= 0.200 +- 0.005 <= 0.205
        Bandwidth on Encoded Data / (MB/s): 1990 <= 2040 +- 50 <= 2070
        Bandwidth on Decoded Data / (MB/s): 2610 <= 2690 +- 60 <= 2720

  -> 64 blocks as chunks are the fastest!, starting from 24, it seems to be somewhat saturated already.
     -> rapidgzip --analyze small.pigz
        - There are 37796 blocks in total
        - Each block is ~ 12800 B encoded and ~16900 B decoded,
        - Flush marker blocks (byte-aligning uncompressed blocks of size 0) appear very irregularly:
          - 25, 61, 88, 97, 196, 277, 286, 304, 342, 361, 370, 379, 399, 408, 425, 434, 475, 484, 503, 522, 531, 548,
            557, 566, 575, 584, 593, 602, 641, 660, 680, 689, 723, 732, 752, 769, 778, 787, 826, 835, 844, 864, 873, ...
            So yeah, anywhere from 9 to 99 blocks! Really might be better if I skip based on encoded data size
          - rapidgzip --analyze small.pigz | grep ': Uncompressed' | wc -l
            In total there are 2114 flush markers, so on average, after 17.88 blocks, i.e.,
            17.88 * 12800 B * 16 (flush markers to skip) = 3.5 MiB of encoded data and 4.61 MiB of decoded data.

    Decompressed 408430549 B to 536870912 B with libarchive:
        Runtime / s: 1.6046 <= 1.6064 +- 0.0020 <= 1.6084
        Bandwidth on Encoded Data / (MB/s): 253.9 <= 254.3 +- 0.3 <= 254.5
        Bandwidth on Decoded Data / (MB/s): 333.8 <= 334.2 +- 0.4 <= 334.6
    Decompressed 408430549 B to 536870912 B with zlib:
        Runtime / s: 1.60 <= 1.64 +- 0.03 <= 1.67
        Bandwidth on Encoded Data / (MB/s): 245 <= 250 +- 5 <= 255
        Bandwidth on Decoded Data / (MB/s): 322 <= 328 +- 6 <= 335
    Decoded 37796 deflate blocks
    Decoded 37796 deflate blocks
    Decoded 37796 deflate blocks
    Decompressed 408430549 B to 536870912 B with rapidgzip (serial):
        Runtime / s: 2.350 <= 2.366 +- 0.023 <= 2.392
        Bandwidth on Encoded Data / (MB/s): 170.8 <= 172.7 +- 1.6 <= 173.8
        Bandwidth on Decoded Data / (MB/s): 224.5 <= 226.9 +- 2.2 <= 228.4
    Decompressed 408430549 B to 536870912 B with rapidgzip (parallel):
        Runtime / s: 0.201 <= 0.209 +- 0.012 <= 0.222
        Bandwidth on Encoded Data / (MB/s): 1840 <= 1960 +- 110 <= 2030
        Bandwidth on Decoded Data / (MB/s): 2420 <= 2580 +- 140 <= 2670
    Decompressed 408430549 B to 536870912 B with rapidgzip (parallel + index):
        Runtime / s: 0.1837 <= 0.1862 +- 0.0024 <= 0.1885
        Bandwidth on Encoded Data / (MB/s): 2167 <= 2194 +- 28 <= 2224
        Bandwidth on Decoded Data / (MB/s): 2850 <= 2880 +- 40 <= 2920


bgzip -c small > small.bgz
cmake --build . -- benchmarkGzip && src/benchmarks/benchmarkGzip small.bgz

    Decompressed 415096389 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=0):
        Runtime / s: 0.47 <= 0.50 +- 0.04 <= 0.54
        Bandwidth on Encoded Data / (MB/s): 760 <= 840 +- 60 <= 880
        Bandwidth on Decoded Data / (MB/s): 990 <= 1080 +- 80 <= 1140
    Decompressed 415096389 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=1):
        Runtime / s: 0.278 <= 0.287 +- 0.008 <= 0.295
        Bandwidth on Encoded Data / (MB/s): 1410 <= 1450 +- 40 <= 1490
        Bandwidth on Decoded Data / (MB/s): 1820 <= 1870 +- 50 <= 1930
    Decompressed 415096389 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=2):
        Runtime / s: 0.287 <= 0.291 +- 0.005 <= 0.296
        Bandwidth on Encoded Data / (MB/s): 1404 <= 1427 +- 22 <= 1448
        Bandwidth on Decoded Data / (MB/s): 1816 <= 1846 +- 29 <= 1873
    Decompressed 415096389 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=4):
        Runtime / s: 0.2062 <= 0.2081 +- 0.0019 <= 0.2099
        Bandwidth on Encoded Data / (MB/s): 1978 <= 1995 +- 18 <= 2013
        Bandwidth on Decoded Data / (MB/s): 2558 <= 2580 +- 23 <= 2604
    Decompressed 415096389 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=8):
        Runtime / s: 0.1839 <= 0.1846 +- 0.0007 <= 0.1851
        Bandwidth on Encoded Data / (MB/s): 2242 <= 2249 +- 8 <= 2258
        Bandwidth on Decoded Data / (MB/s): 2900 <= 2909 +- 10 <= 2920
    Decompressed 415096389 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=16):
        Runtime / s: 0.168 <= 0.171 +- 0.003 <= 0.174
        Bandwidth on Encoded Data / (MB/s): 2390 <= 2430 +- 40 <= 2470
        Bandwidth on Decoded Data / (MB/s): 3090 <= 3150 +- 60 <= 3190
    Decompressed 415096389 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=24):
        Runtime / s: 0.1649 <= 0.1681 +- 0.0028 <= 0.1702
        Bandwidth on Encoded Data / (MB/s): 2440 <= 2470 +- 40 <= 2520
        Bandwidth on Decoded Data / (MB/s): 3160 <= 3200 +- 50 <= 3260
    Decompressed 415096389 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=32):
        Runtime / s: 0.1592 <= 0.1616 +- 0.0022 <= 0.1635
        Bandwidth on Encoded Data / (MB/s): 2540 <= 2570 +- 40 <= 2610
        Bandwidth on Decoded Data / (MB/s): 3280 <= 3320 +- 50 <= 3370
    Decompressed 415096389 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=64):
        Runtime / s: 0.1610 <= 0.1627 +- 0.0019 <= 0.1648
        Bandwidth on Encoded Data / (MB/s): 2520 <= 2550 +- 30 <= 2580
        Bandwidth on Decoded Data / (MB/s): 3260 <= 3300 +- 40 <= 3330
    Decompressed 415096389 B to 536870912 B with rapidgzip (parallel, nBlocksToSkip=128):
        Runtime / s: 0.177 <= 0.185 +- 0.011 <= 0.197
        Bandwidth on Encoded Data / (MB/s): 2110 <= 2250 +- 130 <= 2350
        Bandwidth on Decoded Data / (MB/s): 2730 <= 2920 +- 170 <= 3040

  -> 32 blocks for bgz would be the best in this case!
     -> rapidgzip --analyze small.bgz
        - There are 8226 blocks and gzip streams in total
        - Each block is ~ 50400 B encoded and 65280 B (63.75 KiB) decoded,
        - So, when skipping ~32 blocks (~33 chunked), we have ~1.54 MiB encoded and ~2 MiB decoded data
          -> This is only a factor 2 less than the optimum for pigz, so that we might use the same blocking criterium,
             or not.

    Decompressed 415096389 B to 536870912 B with libarchive:
        Runtime / s: 1.6829 <= 1.6845 +- 0.0020 <= 1.6868
        Bandwidth on Encoded Data / (MB/s): 246.09 <= 246.42 +- 0.30 <= 246.66
        Bandwidth on Decoded Data / (MB/s): 318.3 <= 318.7 +- 0.4 <= 319.0
    Decompressed 415096389 B to 536870912 B with zlib:
        Runtime / s: 1.6962 <= 1.6977 +- 0.0017 <= 1.6996
        Bandwidth on Encoded Data / (MB/s): 244.23 <= 244.50 +- 0.25 <= 244.73
        Bandwidth on Decoded Data / (MB/s): 315.9 <= 316.2 +- 0.3 <= 316.5
    Decoded 8226 deflate blocks
    Decoded 8226 deflate blocks
    Decoded 8226 deflate blocks
    Decompressed 415096389 B to 536870912 B with rapidgzip (serial):
        Runtime / s: 2.739 <= 2.741 +- 0.004 <= 2.746
        Bandwidth on Encoded Data / (MB/s): 151.17 <= 151.42 +- 0.22 <= 151.57
        Bandwidth on Decoded Data / (MB/s): 195.52 <= 195.84 +- 0.28 <= 196.03
    Decompressed 415096389 B to 536870912 B with rapidgzip (parallel):
        Runtime / s: 0.174 <= 0.180 +- 0.007 <= 0.188
        Bandwidth on Encoded Data / (MB/s): 2210 <= 2320 +- 90 <= 2380
        Bandwidth on Decoded Data / (MB/s): 2850 <= 2990 +- 120 <= 3080
    Decompressed 415096389 B to 536870912 B with rapidgzip (parallel + index):
        Runtime / s: 0.162 <= 0.170 +- 0.007 <= 0.174
        Bandwidth on Encoded Data / (MB/s): 2390 <= 2450 +- 100 <= 2560
        Bandwidth on Decoded Data / (MB/s): 3090 <= 3170 +- 130 <= 3320

     -> ~2 GB/s for the decompressed bandwidth with the parallel bgz decoder and when decoding with an
        existing index is already quite nice!

time gzip -d -k -c small.bgz | wc -c
    real  0m3.048s
  -> Interestingly, this is reproducibly faster than the .gz compressed one. Maybe different compression setting?

time bgzip --threads $( nproc ) -d -c small.bgz | wc -c
    real  0m0.195s
  -> Only ~25% faster than parallel rapidgzip thanks to the internal usage of zlib

ls -la small.*gz
    415096389 small.bgz
    416689498 small.gz
  -> The .bgz file is even smaller!


base64 /dev/urandom | head -c $(( 4*1024*1024*1024 )) > 4GiB-base64
bgzip -c 4GiB-base64 > 4GiB-base64.bgz
cmake --build . -- benchmarkGzip && src/benchmarks/benchmarkGzip 4GiB-base64.bgz

    Decompressed 3246513262 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=0):
        Runtime / s: 3.75 <= 3.84 +- 0.15 <= 4.01
        Bandwidth on Encoded Data / (MB/s): 810 <= 850 +- 30 <= 870
        Bandwidth on Decoded Data / (MB/s): 1070 <= 1120 +- 40 <= 1140
    Decompressed 3246513262 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=1):
        Runtime / s: 2.279 <= 2.290 +- 0.012 <= 2.302
        Bandwidth on Encoded Data / (MB/s): 1410 <= 1418 +- 7 <= 1424
        Bandwidth on Decoded Data / (MB/s): 1866 <= 1876 +- 10 <= 1884
    Decompressed 3246513262 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=2):
        Runtime / s: 2.307 <= 2.313 +- 0.010 <= 2.324
        Bandwidth on Encoded Data / (MB/s): 1397 <= 1404 +- 6 <= 1407
        Bandwidth on Decoded Data / (MB/s): 1848 <= 1857 +- 8 <= 1862
    Decompressed 3246513262 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=4):
        Runtime / s: 1.550 <= 1.555 +- 0.005 <= 1.559
        Bandwidth on Encoded Data / (MB/s): 2082 <= 2087 +- 7 <= 2095
        Bandwidth on Decoded Data / (MB/s): 2755 <= 2761 +- 9 <= 2771
    Decompressed 3246513262 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=8):
        Runtime / s: 1.329 <= 1.342 +- 0.013 <= 1.354
        Bandwidth on Encoded Data / (MB/s): 2397 <= 2419 +- 23 <= 2443
        Bandwidth on Decoded Data / (MB/s): 3170 <= 3200 +- 30 <= 3230
    Decompressed 3246513262 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=16):
        Runtime / s: 1.149 <= 1.156 +- 0.006 <= 1.161
        Bandwidth on Encoded Data / (MB/s): 2797 <= 2809 +- 15 <= 2825
        Bandwidth on Decoded Data / (MB/s): 3700 <= 3716 +- 20 <= 3738
    Decompressed 3246513262 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=24):
        Runtime / s: 1.115 <= 1.128 +- 0.015 <= 1.144
        Bandwidth on Encoded Data / (MB/s): 2840 <= 2880 +- 40 <= 2910
        Bandwidth on Decoded Data / (MB/s): 3760 <= 3810 +- 50 <= 3850
    Decompressed 3246513262 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=32):
        Runtime / s: 1.084 <= 1.097 +- 0.011 <= 1.106
        Bandwidth on Encoded Data / (MB/s): 2940 <= 2960 +- 30 <= 2990
        Bandwidth on Decoded Data / (MB/s): 3880 <= 3920 +- 40 <= 3960
    Decompressed 3246513262 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=64):
        Runtime / s: 1.060 <= 1.072 +- 0.012 <= 1.083
        Bandwidth on Encoded Data / (MB/s): 3000 <= 3030 +- 30 <= 3060
        Bandwidth on Decoded Data / (MB/s): 3970 <= 4010 +- 40 <= 4050
    Decompressed 3246513262 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=128):
        Runtime / s: 1.063 <= 1.071 +- 0.007 <= 1.076
        Bandwidth on Encoded Data / (MB/s): 3017 <= 3032 +- 19 <= 3054
        Bandwidth on Decoded Data / (MB/s): 3991 <= 4011 +- 26 <= 4040

  -> The peak is at 64, close to the last.
     -> So we are fine using a chunking criterium shared with pigz of roughly 4 MiB of encoded data or maybe more.
        -> Note that for generic gzip files, we would need roughly 32 MiB of encoded data because of the much slower
           block finder amortizing slower!

    Decompressed 3246513262 B to 4294967296 B with libarchive:
        Runtime / s: 11.17 <= 11.29 +- 0.17 <= 11.48
        Bandwidth on Encoded Data / (MB/s): 283 <= 288 +- 4 <= 291
        Bandwidth on Decoded Data / (MB/s): 374 <= 380 +- 6 <= 384
    Decompressed 3246513262 B to 4294967296 B with zlib:
        Runtime / s: 11.52 <= 11.67 +- 0.21 <= 11.92
        Bandwidth on Encoded Data / (MB/s): 272 <= 278 +- 5 <= 282
        Bandwidth on Decoded Data / (MB/s): 360 <= 368 +- 7 <= 373
    Decoded 65795 deflate blocks
    Decoded 65795 deflate blocks
    Decoded 65795 deflate blocks
    Decompressed 3246513262 B to 4294967296 B with rapidgzip (serial):
        Runtime / s: 15.16 <= 15.22 +- 0.05 <= 15.26
        Bandwidth on Encoded Data / (MB/s): 212.8 <= 213.3 +- 0.7 <= 214.2
        Bandwidth on Decoded Data / (MB/s): 281.5 <= 282.2 +- 1.0 <= 283.3
    Decompressed 3246513262 B to 4294967296 B with rapidgzip (parallel):
        Runtime / s: 1.0691 <= 1.0710 +- 0.0018 <= 1.0727
        Bandwidth on Encoded Data / (MB/s): 3026 <= 3031 +- 5 <= 3037
        Bandwidth on Decoded Data / (MB/s): 4004 <= 4010 +- 7 <= 4017
    Decompressed 3246513262 B to 4294967296 B with rapidgzip (parallel + index):
        Runtime / s: 1.000 <= 1.005 +- 0.007 <= 1.013
        Bandwidth on Encoded Data / (MB/s): 3204 <= 3231 +- 24 <= 3247
        Bandwidth on Decoded Data / (MB/s): 4240 <= 4270 +- 30 <= 4300

      -> Why is the version with an index actually slower!?
         I almost would assume that zlib is less optimized than the rapidgzip-internal inflate for gzip header/footer
         reading and therefore is suboptimal for this one gzip stream per deflate block file format (BGZF).
         -> Has been circumvented now that ISA-L is used

time bgzip --threads $( nproc ) -d -c 4GiB-base64.bgz | wc -c
    real  0m1.327s
  -> Just as fast as rapidgzip (without index)!
time bgzip --threads $( nproc ) -d -c 4GiB-base64.bgz > /dev/null
    real  0m0.907s

time rapidgzip -d -c 4GiB-base64.bgz | wc -c
    real  0m1.338s
time rapidgzip -d -c 4GiB-base64.bgz > /dev/null
    real  0m0.620s
  -> 30% faster than bgzip thanks to internal usage of ISA-L


base64 /dev/urandom | head -c $(( 1024*1024*1024 )) | pigz > 4GiB-base64.pigz
cmake --build . -- benchmarkGzip && src/benchmarks/benchmarkGzip 4GiB-base64.pigz

    Decompressed 3267442534 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=0):
        Runtime / s: 7.9 <= 8.1 +- 0.3 <= 8.5
        Bandwidth on Encoded Data / (MB/s): 384 <= 402 +- 15 <= 412
        Bandwidth on Decoded Data / (MB/s): 505 <= 528 +- 20 <= 541
    Decompressed 3267442534 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=1):
        Runtime / s: 4.57 <= 4.62 +- 0.07 <= 4.70
        Bandwidth on Encoded Data / (MB/s): 695 <= 708 +- 11 <= 715
        Bandwidth on Decoded Data / (MB/s): 914 <= 931 +- 14 <= 939
    Decompressed 3267442534 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=2):
        Runtime / s: 3.754 <= 3.765 +- 0.009 <= 3.771
        Bandwidth on Encoded Data / (MB/s): 866.4 <= 867.9 +- 2.1 <= 870.3
        Bandwidth on Decoded Data / (MB/s): 1138.8 <= 1140.8 +- 2.8 <= 1144.0
    Decompressed 3267442534 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=4):
        Runtime / s: 2.460 <= 2.465 +- 0.007 <= 2.474
        Bandwidth on Encoded Data / (MB/s): 1321 <= 1325 +- 4 <= 1328
        Bandwidth on Decoded Data / (MB/s): 1736 <= 1742 +- 5 <= 1746
    Decompressed 3267442534 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=8):
        Runtime / s: 1.716 <= 1.724 +- 0.012 <= 1.737
        Bandwidth on Encoded Data / (MB/s): 1881 <= 1896 +- 13 <= 1904
        Bandwidth on Decoded Data / (MB/s): 2472 <= 2492 +- 17 <= 2503
    Decompressed 3267442534 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=16):
        Runtime / s: 1.367 <= 1.396 +- 0.026 <= 1.416
        Bandwidth on Encoded Data / (MB/s): 2310 <= 2340 +- 40 <= 2390
        Bandwidth on Decoded Data / (MB/s): 3030 <= 3080 +- 60 <= 3140
    Decompressed 3267442534 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=24):
        Runtime / s: 1.281 <= 1.285 +- 0.004 <= 1.289
        Bandwidth on Encoded Data / (MB/s): 2536 <= 2542 +- 8 <= 2550
        Bandwidth on Decoded Data / (MB/s): 3333 <= 3342 +- 10 <= 3352
    Decompressed 3267442534 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=32):
        Runtime / s: 1.219 <= 1.243 +- 0.022 <= 1.260
        Bandwidth on Encoded Data / (MB/s): 2590 <= 2630 +- 50 <= 2680
        Bandwidth on Decoded Data / (MB/s): 3410 <= 3460 +- 60 <= 3520
    Decompressed 3267442534 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=64):
        Runtime / s: 1.1526 <= 1.1553 +- 0.0029 <= 1.1583
        Bandwidth on Encoded Data / (MB/s): 2821 <= 2828 +- 7 <= 2835
        Bandwidth on Decoded Data / (MB/s): 3708 <= 3718 +- 9 <= 3726
    Decompressed 3267442534 B to 4294967296 B with rapidgzip (parallel, nBlocksToSkip=128):
        Runtime / s: 1.1046 <= 1.1071 +- 0.0022 <= 1.1084
        Bandwidth on Encoded Data / (MB/s): 2948 <= 2951 +- 6 <= 2958
        Bandwidth on Decoded Data / (MB/s): 3875 <= 3879 +- 8 <= 3888

    Decompressed 3267442534 B to 4294967296 B with libarchive:
        Runtime / s: 12.63 <= 12.67 +- 0.05 <= 12.73
        Bandwidth on Encoded Data / (MB/s): 256.7 <= 257.8 +- 1.0 <= 258.6
        Bandwidth on Decoded Data / (MB/s): 337.4 <= 338.9 +- 1.4 <= 339.9
    Decompressed 3267442534 B to 4294967296 B with zlib:
        Runtime / s: 12.90 <= 13.03 +- 0.11 <= 13.10
        Bandwidth on Encoded Data / (MB/s): 249.4 <= 250.8 +- 2.2 <= 253.3
        Bandwidth on Decoded Data / (MB/s): 327.8 <= 329.6 +- 2.9 <= 333.0
    Decoded 302998 deflate blocks
    Decoded 302998 deflate blocks
    Decoded 302998 deflate blocks
    Decompressed 3267442534 B to 4294967296 B with rapidgzip (serial):
        Runtime / s: 18.66 <= 18.72 +- 0.09 <= 18.82
        Bandwidth on Encoded Data / (MB/s): 173.6 <= 174.6 +- 0.8 <= 175.1
        Bandwidth on Decoded Data / (MB/s): 228.2 <= 229.5 +- 1.1 <= 230.2
    Decompressed 3267442534 B to 4294967296 B with rapidgzip (parallel):
        Runtime / s: 1.1057 <= 1.1077 +- 0.0017 <= 1.1088
        Bandwidth on Encoded Data / (MB/s): 2947 <= 2950 +- 5 <= 2955
        Bandwidth on Decoded Data / (MB/s): 3873 <= 3877 +- 6 <= 3884
    Decompressed 3267442534 B to 4294967296 B with rapidgzip (parallel + index):
        Runtime / s: 1.059 <= 1.066 +- 0.007 <= 1.073
        Bandwidth on Encoded Data / (MB/s): 3046 <= 3064 +- 20 <= 3087
        Bandwidth on Decoded Data / (MB/s): 4004 <= 4028 +- 27 <= 4057

  -> Rapidgzip has abysmal serial decoder speed, probably because pigz deflate blocks are only 16 KiB so that the
     upfront cost for the double-symbol cached Huffman-decoder becomes expensive.


for (( i=0; i<20; ++i )); do cat test-files/silesia/silesia.tar; done | gzip > silesia-20x.tar.gz
cmake --build . -- benchmarkGzip && src/benchmarks/benchmarkGzip silesia-20x.tar.gz

    Decompressed 1364776140 B to 4239155200 B with libarchive:
        Runtime / s: 10.09 <= 10.11 +- 0.04 <= 10.16
        Bandwidth on Encoded Data / (MB/s): 134.3 <= 134.9 +- 0.5 <= 135.3
        Bandwidth on Decoded Data / (MB/s): 417.3 <= 419.1 +- 1.6 <= 420.1
    Decompressed 1364776140 B to 4239155200 B with zlib:
        Runtime / s: 10.110 <= 10.123 +- 0.021 <= 10.148
        Bandwidth on Encoded Data / (MB/s): 134.49 <= 134.81 +- 0.28 <= 134.99
        Bandwidth on Decoded Data / (MB/s): 417.7 <= 418.8 +- 0.9 <= 419.3
    Decoded 66040 deflate blocks
    Decoded 66040 deflate blocks
    Decoded 66040 deflate blocks
    Decompressed 1364776140 B to 4239155200 B with rapidgzip (serial):
        Runtime / s: 11.71 <= 11.88 +- 0.16 <= 12.02
        Bandwidth on Encoded Data / (MB/s): 113.5 <= 114.9 +- 1.6 <= 116.6
        Bandwidth on Decoded Data / (MB/s): 353 <= 357 +- 5 <= 362
    Decompressed 1364776140 B to 4239155200 B with rapidgzip (parallel):
        Runtime / s: 1.38 <= 1.42 +- 0.05 <= 1.47
        Bandwidth on Encoded Data / (MB/s): 930 <= 960 +- 30 <= 990
        Bandwidth on Decoded Data / (MB/s): 2880 <= 2990 +- 100 <= 3070
    Decompressed 1364776140 B to 4239155200 B with rapidgzip (parallel + index):
        Runtime / s: 0.960 <= 0.966 +- 0.006 <= 0.972
        Bandwidth on Encoded Data / (MB/s): 1405 <= 1412 +- 9 <= 1422
        Bandwidth on Decoded Data / (MB/s): 4363 <= 4387 +- 27 <= 4416
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

python3 -c '
import indexed_gzip as igz;
import time; t0 = time.time();
with igz.open("10x.silesia.tar.gz") as file:
    while result := file.read( 16*1024*1024 ):
        pass
print(f"Decompression took {time.time() - t0:.3f} s")
'

    Chunk size | decompression time
    -----------+-------------------
         4 MiB | 14.3 s
        16 MiB | 11.3 s
        64 MiB | 11.3 s
       256 MiB | 10.9 s
      1024 MiB | 11.5 s
           inf | 13.1 s

 -> Note small.gz as used in the other benchmarks is only 398 MiB compressed, so that the "inf" case,
    which is used for the other benchmarks, should be almost optimal.

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
    printf '\n| %6s | %15s | %10s | %18s |\n' Format Decoder 'Runtime / s' 'Bandwidth / (MB/s)'
    printf -- '|--------|-----------------|-------------|--------------------|\n'
    crc32 "$fname.$format" &>/dev/null  # Make my QLC SSD cache the file into the SLC cache
    crc32 "$fname.$format" &>/dev/null  # Make my QLC SSD cache the file into the SLC cache

    for tool in gzip bgzip "bgzip -@ $( nproc )" pigz igzip "igzip -T $( nproc )" "src/tools/rapidgzip -P 1" "src/tools/rapidgzip -P $( nproc )"; do
        runtime=$( ( time $tool -d -c "$fname.$format" | wc -c ) 2>&1 | sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
        bandwidth=$( python3 -c "print( int( round( $fileSize / 1e6 / $runtime ) ) )" )
        printf '| %6s | %15s | %11s | %18s |\n' "$format" "${tool#src/tools/}" "$runtime" "$bandwidth"
    done
done

    | Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
    |--------|---------------|-------------|--------------------|
    |     gz |          gzip |      22.218 |                193 |
    |     gz |         bgzip |      16.202 |                265 |
    |     gz |   bgzip -@ 24 |      15.962 |                269 |
    |     gz |          pigz |      13.391 |                321 |
    |     gz |         igzip |       9.225 |                466 |
    |     gz |   igzip -T 24 |       9.295 |                462 |
    |     gz | rapidgzip -P 1 |       8.811 |                487 |
    |     gz | rapidgzip -P 24 |       1.320 |               **3254** |

    | Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
    |--------|---------------|-------------|--------------------|
    |    bgz |          gzip |      21.346 |                201 |
    |    bgz |         bgzip |      10.621 |                404 |
    |    bgz |   bgzip -@ 24 |       1.949 |               2204 |
    |    bgz |          pigz |      18.466 |                233 |
    |    bgz |         igzip |       7.321 |                587 |
    |    bgz |   igzip -T 24 |       7.377 |                582 |
    |    bgz | rapidgzip -P 1 |       7.520 |                571 |
    |    bgz | rapidgzip -P 24 |       1.125 |               **3818** |

    | Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
    |--------|---------------|-------------|--------------------|
    |   pigz |          gzip |      24.320 |                177 |
    |   pigz |         bgzip |      16.358 |                263 |
    |   pigz |   bgzip -@ 24 |      15.868 |                271 |
    |   pigz |          pigz |      13.404 |                320 |
    |   pigz |         igzip |       9.709 |                442 |
    |   pigz |   igzip -T 24 |       9.491 |                453 |
    |   pigz | rapidgzip -P 1 |       8.867 |                484 |
    |   pigz | rapidgzip -P 24 |       1.288 |               **3335** |

    | Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
    |--------|---------------|-------------|--------------------|
    |    igz |          gzip |      22.809 |                188 |
    |    igz |         bgzip |      15.302 |                281 |
    |    igz |   bgzip -@ 24 |      15.170 |                283 |
    |    igz |          pigz |      12.416 |                346 |
    |    igz |         igzip |       7.836 |                548 |
    |    igz |   igzip -T 24 |       7.797 |                551 |
    |    igz | rapidgzip -P 1 |       7.149 |                601 |
    |    igz | rapidgzip -P 24 |       1.293 |               **3322** |


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

    for tool in gzip bgzip "bgzip -@ $( nproc )" pigz igzip "igzip -T $( nproc )" "src/tools/rapidgzip -P 1" "src/tools/rapidgzip -P $( nproc )"; do
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
|     gz |  rapidgzip -P 1 |       1.244 |               3453 |
|     gz | rapidgzip -P 24 |       1.618 |               2654 |

| Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
|--------|---------------|-------------|--------------------|
|    bgz |          gzip |      20.119 |                213 |
|    bgz |         bgzip |       1.792 |               2397 |
|    bgz |   bgzip -@ 24 |       1.919 |               2238 |
|    bgz |          pigz |       9.072 |                473 |
|    bgz |         igzip |       1.775 |               2420 |
|    bgz |   igzip -T 24 |       1.733 |               2478 |
|    bgz |  rapidgzip -P 1 |       1.932 |               2223 |
|    bgz | rapidgzip -P 24 |       1.589 |               2703 |

| Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
|--------|---------------|-------------|--------------------|
|   pigz |          gzip |      19.810 |                217 |
|   pigz |         bgzip |       4.005 |               1072 |
|   pigz |   bgzip -@ 24 |       4.038 |               1064 |
|   pigz |          pigz |       4.062 |               1057 |
|   pigz |         igzip |       2.500 |               1718 |
|   pigz |   igzip -T 24 |       2.335 |               1839 |
|   pigz |  rapidgzip -P 1 |       1.027 |               4182 |
|   pigz | rapidgzip -P 24 |       1.618 |               2654 |

| Format |       Decoder | Runtime / s | Bandwidth / (MB/s) |
|--------|---------------|-------------|--------------------|
|    igz |          gzip |      26.927 |                160 |
|    igz |         bgzip |       5.601 |                767 |
|    igz |   bgzip -@ 24 |       5.446 |                789 |
|    igz |          pigz |       5.194 |                827 |
|    igz |         igzip |       3.839 |               1119 |
|    igz |   igzip -T 24 |       3.888 |               1105 |
|    igz |  rapidgzip -P 1 |       4.548 |                944 |
|    igz | rapidgzip -P 24 |       1.479 |               2904 |



# Benchmark rapidgzip scaling over threads

for fname in 4GiB-base64 4GiB-random; do
    fileSize=$( stat -L --format=%s -- "$fname" )
    format=gz
    printf '\n| %14s | %13s | %10s | %18s |\n' File Decoder 'Runtime / s' 'Bandwidth / (MB/s)'
    printf -- '|----------------|---------------|-------------|--------------------|\n'
    crc32 "$fname.$format" &>/dev/null  # Make my QLC SSD cache the file into the SLC cache

    for parallelization in 1 2 4 8 12 16 24 32; do
        tool="src/tools/rapidgzip -P $parallelization"
        visibleTool="rapidgzip -P $parallelization"
        runtime=$( ( time $tool -d -c "$fname.$format" | wc -c ) 2>&1 | sed -n -E 's|real[ \t]*0m||p' | sed 's|[ \ts]||' )
        bandwidth=$( python3 -c "print( int( round( $fileSize / 1e6 / $runtime ) ) )" )
        printf '| %14s | %13s | %11s | %18s |\n' "$fname.$format" "$visibleTool" "$runtime" "$bandwidth"
    done
done

    |           File |       Decoder | Runtime / s | Bandwidth / (MB/s) |
    |----------------|---------------|-------------|--------------------|
    | 4GiB-base64.gz |  rapidgzip -P 1 |      19.763 |                217 |
    | 4GiB-base64.gz |  rapidgzip -P 2 |       9.782 |                439 |
    | 4GiB-base64.gz |  rapidgzip -P 4 |       5.122 |                839 |
    | 4GiB-base64.gz |  rapidgzip -P 8 |       2.905 |               1478 |
    | 4GiB-base64.gz | rapidgzip -P 12 |       2.241 |               1917 |
    | 4GiB-base64.gz | rapidgzip -P 16 |       1.890 |               2272 |
    | 4GiB-base64.gz | rapidgzip -P 24 |       1.703 |               2522 |
    | 4GiB-base64.gz | rapidgzip -P 32 |       1.716 |               2503 |

    |           File |       Decoder | Runtime / s | Bandwidth / (MB/s) |
    |----------------|---------------|-------------|--------------------|
    | 4GiB-random.gz |  rapidgzip -P 1 |       1.289 |               3332 |
    | 4GiB-random.gz |  rapidgzip -P 2 |       1.961 |               2190 |
    | 4GiB-random.gz |  rapidgzip -P 4 |       1.431 |               3001 |
    | 4GiB-random.gz |  rapidgzip -P 8 |       1.276 |               3366 |
    | 4GiB-random.gz | rapidgzip -P 12 |       1.306 |               3289 |
    | 4GiB-random.gz | rapidgzip -P 16 |       1.439 |               2985 |
    | 4GiB-random.gz | rapidgzip -P 24 |       1.509 |               2846 |
    | 4GiB-random.gz | rapidgzip -P 32 |       1.530 |               2807 |
*/
