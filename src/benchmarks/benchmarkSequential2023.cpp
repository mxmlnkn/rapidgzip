/**
 * @file While the other benchmarks test varying situations and parameters for single components,
 *       this file is a collection of benchmarks for selected (best) versions for each component
 *       to get an overview of the current state of pragzip.
 * @todo Write full statistics to file for plotting.
 */

#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include <vector>

#include <BitManipulation.hpp>
#include <BitReader.hpp>
#include <blockfinder/DynamicHuffman.hpp>
#include <blockfinder/Uncompressed.hpp>
#include <common.hpp>
#include <DecodedData.hpp>
#include <filereader/BufferView.hpp>
#include <pragzip.hpp>
#include <Statistics.hpp>


constexpr size_t REPEAT_COUNT{ 200 };


[[nodiscard]] size_t
getOpenFileHandleCount()
{
    std::filesystem::directory_iterator entries( "/proc/self/fd" );
    return std::distance( begin( entries ), end( entries ) );
}


[[nodiscard]] std::string
formatBandwidth( const std::vector<double>& times,
                 const size_t               nBytes )
{
    std::vector<double> bandwidths( times.size() );
    std::transform( times.begin(), times.end(), bandwidths.begin(),
                    [nBytes] ( double time ) { return static_cast<double>( nBytes ) / time / 1e6; } );
    Statistics<double> bandwidthStats{ bandwidths };

    /* Motivation for showing min times and maximum bandwidths are because nothing can go faster than
     * physically possible but many noisy influences can slow things down, i.e., the minimum time is
     * the value closest to be free of noise. */
    std::stringstream result;
    result << "( min: " << bandwidthStats.min << ", " << bandwidthStats.formatAverageWithUncertainty()
           << ", max: " << bandwidthStats.max << " ) MB/s";
    return result.str();
}


[[nodiscard]] std::vector<double>
repeatBenchmarks( const std::function<std::pair</* duration */ double, /* checksum */ uint64_t>()>& toMeasure )
{
    std::cout << "Repeating benchmarks " << REPEAT_COUNT << " times ... ";
    const auto tStart = now();

    std::optional<uint64_t> checksum;
    std::vector<double> times( REPEAT_COUNT );
    for ( auto& time : times ) {
        const auto [measuredTime, calculatedChecksum] = toMeasure();
        time = measuredTime;

        if ( !checksum ) {
            checksum = calculatedChecksum;
        } else if ( *checksum != calculatedChecksum ) {
            throw std::runtime_error( "Indeterministic or wrong result observed!" );
        }
    }

    std::cout << "Done (" << duration( tStart ) << " s)\n";
    return times;
}


[[nodiscard]] std::pair<double, uint64_t>
benchmarkBitReader( const std::vector<char>& data,
                    const uint8_t            nBits )
{
    using pragzip::BitReader;
    BitReader bitReader( std::make_unique<BufferViewFileReader>( data ) );

    const auto t0 = now();

    uint64_t sum = 0;
    try {
        /* Without unrolling ~1.4 GB/s and with unrolling ~1.8 GB/s! */
        #pragma GCC unroll 4
        while ( true ) {
            sum += bitReader.read( nBits );
        }
    } catch ( const typename BitReader::EndOfFileReached& ) {
        /* Ignore EOF exception. Checking for it in each loop is expensive! */
    }

    return { duration( t0 ), sum };
}


void
benchmarkBitReaderBitReads( const std::vector<uint8_t>& nBitsToTest )
{
    std::ofstream dataFile( "result-bitreader-reads.dat" );
    dataFile << "# 64-bit buffer LSB (gzip) order\n";
    dataFile << "# bitsPerReadCall dataSize/B runtime/s\n";

    for ( const auto nBits : nBitsToTest ) {
        /* Scale benchmark size with bits to get roughly equally long-running benchmarks and therefore also
         * roughly equally good error estimates. */
        std::vector<char> data( 2_Mi * nBits );
        for ( auto& x : data ) {
            x = static_cast<char>( rand() );
        }

        std::cout << "\n== Benchmarking by reading " << static_cast<int>( nBits ) << " bits ==\n";
        const auto times = repeatBenchmarks( [&] () { return benchmarkBitReader( data, nBits ); } );
        std::cout << "[BitReader::read loop] Decoded with " << formatBandwidth( times, data.size() ) << "\n";

        for ( const auto time : times ) {
            dataFile << static_cast<int>( nBits ) << " " << data.size() << " " << time << "\n";
        }
    }
}


[[nodiscard]] std::pair<double, uint64_t>
benchmarkUncompressedBlockFinder( const std::vector<char>& data )
{
    const auto t0 = now();

    using pragzip::BitReader;
    BitReader bitReader( std::make_unique<BufferViewFileReader>( data ) );

    uint64_t count{ 0 };
    while ( true ) {
        const auto& [min, max] = pragzip::blockfinder::seekToNonFinalUncompressedDeflateBlock( bitReader );
        if ( min == std::numeric_limits<size_t>::max() ) {
            break;
        }
        ++count;
        bitReader.seek( max + 1 );
    }

    return { duration( t0 ), count };
}


void
benchmarkFindUncompressedBlocks()
{
    const auto t0 = now();
    std::cout << "Initializing random data for benchmark... ";
    std::vector<char> data( 32_Mi );
    for ( auto& x : data ) {
        x = static_cast<char>( rand() );
    }
    std::cout << "Done (" << duration( t0 ) << " s)\n";

    std::ofstream dataFile( "result-find-uncompressed.dat" );
    dataFile << "# dataSize/B runtime/s\n";
    const auto times = repeatBenchmarks( [&] () { return benchmarkUncompressedBlockFinder( data ); } );
    for ( const auto time : times ) {
        dataFile << data.size() << " " << time << "\n";
    }

    std::cout << "[Uncompressed block finder] " << formatBandwidth( times, data.size() ) << "\n";
}


[[nodiscard]] std::pair<double, uint64_t>
benchmarkDynamicBlockFinder( const std::vector<char>& data )
{
    const auto t0 = now();

    using pragzip::BitReader;
    BitReader bitReader( std::make_unique<BufferViewFileReader>( data ) );

    uint64_t count{ 0 };
    while ( true ) {
        const auto offset = pragzip::blockfinder::seekToNonFinalDynamicDeflateBlock( bitReader );
        if ( offset == std::numeric_limits<size_t>::max() ) {
            break;
        }
        ++count;
        bitReader.seek( offset + 1 );
    }

    return { duration( t0 ), count };
}


void
benchmarkDynamicBlockFinder()
{
    const auto t0 = now();
    std::cout << "Initializing random data for benchmark... ";
    std::vector<char> data( 4_Mi );
    for ( auto& x : data ) {
        x = static_cast<char>( rand() );
    }
    std::cout << "Done (" << duration( t0 ) << " s)\n";

    const auto times = repeatBenchmarks( [&data] () { return benchmarkDynamicBlockFinder( data ); } );

    std::ofstream dataFile( "result-find-dynamic.dat" );
    dataFile << "# dataSize/B runtime/s\n";
    for ( const auto time : times ) {
        dataFile << data.size() << " " << time << "\n";
    }

    std::cout << "[Dynamic block finder] " << formatBandwidth( times, data.size() ) << "\n";
}


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
    GzipWrapper( Format format = Format::AUTO )
    {
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
            throw std::domain_error( std::to_string( ret ) );
        }
    }

    GzipWrapper( const GzipWrapper& ) = delete;
    GzipWrapper( GzipWrapper&& ) = delete;
    GzipWrapper& operator=( GzipWrapper&& ) = delete;
    GzipWrapper& operator=( GzipWrapper& ) = delete;

    ~GzipWrapper()
    {
        inflateEnd( &m_stream );
    }

    bool
    tryInflate( unsigned char const* compressed,
                size_t               compressedSize,
                size_t               bitOffset = 0 )
    {
        if ( inflateReset( &m_stream ) != Z_OK ) {
            return false;
        }

        if ( ceilDiv( bitOffset, CHAR_BIT ) >= compressedSize ) {
            return false;
        }

        const auto bitsToSeek = bitOffset % CHAR_BIT;
        const auto byteOffset = bitOffset / CHAR_BIT;
        m_stream.avail_in = compressedSize - byteOffset;
        /* const_cast should be safe because zlib presumably only uses this in a const manner.
         * I'll probably have to roll out my own deflate decoder anyway so I might be able
         * to change this bothersome interface. */
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        m_stream.next_in = const_cast<unsigned char*>( compressed ) + byteOffset;

        const auto outputPreviouslyAvailable = std::min( 8_Ki, m_outputBuffer.size() );
        m_stream.avail_out = outputPreviouslyAvailable;
        m_stream.next_out = m_outputBuffer.data();

        /* Using std::fill leads to 10x slowdown -.-!!! Memset probably better.
         * Well, or not necessary at all because we are not interested in the specific output values anyway.
         * std::memset only incurs a 30% slowdown. */
        //std::fill( m_window.begin(), m_window.end(), '\0' );
        //std::memset( m_window.data(), 0, m_window.size() );
        if ( bitsToSeek > 0 ) {
            m_stream.next_in += 1;
            m_stream.avail_in -= 1;

            auto errorCode = inflatePrime( &m_stream, static_cast<int>( 8U - bitsToSeek ),
                                           compressed[byteOffset] >> bitsToSeek );
            if ( errorCode != Z_OK ) {
                return false;
            }
        }

        auto errorCode = inflateSetDictionary( &m_stream, m_window.data(), m_window.size() );
        if ( errorCode != Z_OK ) {}

        errorCode = inflate( &m_stream, Z_BLOCK );
        if ( ( errorCode != Z_OK ) && ( errorCode != Z_STREAM_END ) ) {
            return false;
        }

        if ( errorCode == Z_STREAM_END ) {
            /* We are not interested in blocks close to the stream end.
             * Because either this is close to the end and no parallelization is necessary,
             * or this means the gzip file is compromised of many gzip streams, which are a tad
             * easier to search for than raw deflate streams! */
            return false;
        }
        const auto nBytesDecoded = outputPreviouslyAvailable - m_stream.avail_out;
        return nBytesDecoded >= outputPreviouslyAvailable;
    }

private:
    z_stream m_stream{};
    std::vector<unsigned char> m_window = std::vector<unsigned char>( 32_Ki, '\0' );
    std::vector<unsigned char> m_outputBuffer = std::vector<unsigned char>( 64_Mi );
};


[[nodiscard]] std::pair<double, uint64_t>
findDeflateBlocksZlib( const std::vector<char>& buffer )
{
    const auto t0 = now();

    uint64_t count{ 0 };
    GzipWrapper gzip( GzipWrapper::Format::RAW );

    for ( size_t offset = 0; offset <= ( buffer.size() - 1 ) * sizeof( buffer[0] ) * CHAR_BIT; ++offset ) {
        if ( gzip.tryInflate( reinterpret_cast<unsigned char const*>( buffer.data() ),
                              buffer.size() * sizeof( buffer[0] ),
                              offset ) ) {
            ++count;
        }
    }

    return { duration( t0 ), count };
}


void
benchmarkDynamicBlockFinderZlib()
{
    const auto t0 = now();
    std::cout << "Initializing random data for benchmark... ";
    std::vector<char> data( 32_Ki );
    for ( auto& x : data ) {
        x = static_cast<char>( rand() );
    }
    std::cout << "Done (" << duration( t0 ) << " s)\n";

    const auto times = repeatBenchmarks( [&data] () { return findDeflateBlocksZlib( data ); } );

    std::ofstream dataFile( "result-find-dynamic-zlib.dat" );
    dataFile << "# dataSize/B runtime/s\n";
    for ( const auto time : times ) {
        dataFile << data.size() << " " << time << "\n";
    }

    std::cout << "[Dynamic block finder using zlib] " << formatBandwidth( times, data.size() ) << "\n";
}


[[nodiscard]] std::pair<double, uint64_t>
findDeflateBlocksPragzip( const std::vector<char>& buffer )
{
    using DeflateBlock = pragzip::deflate::Block</* CRC32 */ false>;

    const auto nBitsToTest = buffer.size() * CHAR_BIT;
    pragzip::BitReader bitReader( std::make_unique<BufferViewFileReader>( buffer ) );

    const auto t0 = now();

    uint64_t count{ 0 };

    pragzip::deflate::Block block;
    for ( size_t offset = 0; offset <= nBitsToTest; ++offset ) {
        bitReader.seek( static_cast<long long int>( offset ) );
        try
        {
            auto error = block.readHeader</* count last block as error */ true>( bitReader );
            if ( ( error != pragzip::Error::NONE )
                 || ( block.compressionType() == DeflateBlock::CompressionType::DYNAMIC_HUFFMAN ) ) {
                continue;
            }

            ++count;
        } catch ( const pragzip::BitReader::EndOfFileReached& ) {
            break;
        }
    }
    return { duration( t0 ), count };
}


void
benchmarkDynamicBlockFinderPragzip()
{
    const auto t0 = now();
    std::cout << "Initializing random data for benchmark... ";
    std::vector<char> data( 512_Ki );
    for ( auto& x : data ) {
        x = static_cast<char>( rand() );
    }
    std::cout << "Done (" << duration( t0 ) << " s)\n";

    const auto times = repeatBenchmarks( [&data] () { return findDeflateBlocksPragzip( data ); } );

    std::ofstream dataFile( "result-find-dynamic-pragzip.dat" );
    dataFile << "# dataSize/B runtime/s\n";
    for ( const auto time : times ) {
        dataFile << data.size() << " " << time << "\n";
    }

    std::cout << "[Dynamic block finder using pragzip] " << formatBandwidth( times, data.size() ) << "\n";
}


[[nodiscard]] std::pair<double, uint64_t>
findDeflateBlocksPragzipLUT( const std::vector<char>& buffer )
{
    using DeflateBlock = pragzip::deflate::Block</* CRC32 */ false>;
    constexpr auto CACHED_BIT_COUNT = pragzip::blockfinder::OPTIMAL_NEXT_DEFLATE_LUT_SIZE;

    /* Testing a dozen positions less should not make a difference but avoid EOF exceptions. */
    const auto nBitsToTest = buffer.size() * CHAR_BIT - CACHED_BIT_COUNT;
    pragzip::BitReader bitReader( std::make_unique<BufferViewFileReader>( buffer ) );

    const auto t0 = now();

    uint64_t count{ 0 };

    auto bitBufferForLUT = bitReader.peek<CACHED_BIT_COUNT>();

    pragzip::deflate::Block block;
    for ( size_t offset = 0; offset <= nBitsToTest; ) {
        const auto nextPosition =
            pragzip::blockfinder::NEXT_DYNAMIC_DEFLATE_CANDIDATE_LUT<CACHED_BIT_COUNT>[bitBufferForLUT];

        const auto bitsToLoad = std::max( static_cast<std::decay_t<decltype( nextPosition )> >( 1 ), nextPosition );
        bitBufferForLUT >>= bitsToLoad;
        bitBufferForLUT |= bitReader.read( bitsToLoad )
                           << static_cast<uint8_t>( CACHED_BIT_COUNT - bitsToLoad );
        if ( nextPosition > 0 ) {
            offset += nextPosition;
            continue;
        }

        bitReader.seek( static_cast<long long int>( offset ) );
        try
        {
            auto error = block.readHeader</* count last block as error */ true>( bitReader );
            if ( ( error != pragzip::Error::NONE )
                 || ( block.compressionType() == DeflateBlock::CompressionType::DYNAMIC_HUFFMAN ) ) {
                continue;
            }

            ++count;
        } catch ( const pragzip::BitReader::EndOfFileReached& ) {
            break;
        }

        ++offset;
    }
    return { duration( t0 ), count };
}


void
benchmarkDynamicBlockFinderPragzipLUT()
{
    const auto t0 = now();
    std::cout << "Initializing random data for benchmark... ";
    std::vector<char> data( 2_Mi );
    for ( auto& x : data ) {
        x = static_cast<char>( rand() );
    }
    std::cout << "Done (" << duration( t0 ) << " s)\n";

    const auto times = repeatBenchmarks( [&data] () { return findDeflateBlocksPragzipLUT( data ); } );

    std::ofstream dataFile( "result-find-dynamic-pragzip-skip-lut.dat" );
    dataFile << "# dataSize/B runtime/s\n";
    for ( const auto time : times ) {
        dataFile << data.size() << " " << time << "\n";
    }

    std::cout << "[Dynamic block finder using skip LUT and pragzip] " << formatBandwidth( times, data.size() ) << "\n";
}


[[nodiscard]] std::pair<double, uint64_t>
benchmarkFileReader( const std::string& path,
                     const size_t       chunkSize )
{
    const auto t0 = now();

    StandardFileReader fileReader( path );

    std::vector<char> buffer( chunkSize );
    uint64_t checksum{ 0 };
    while ( true ) {
        const auto nBytesRead = fileReader.read( buffer.data(), buffer.size() );
        checksum += nBytesRead + buffer[buffer.size() / 2];
        if ( nBytesRead == 0 ) {
            break;
        }
    }

    return { duration( t0 ), checksum };
}


/* Create a temporary file for benchmarking that is cleaned up with RAII. */
struct TemporaryFile
{
    TemporaryFile( const size_t requestedSize ) :
        size( requestedSize )
    {
        std::ofstream file( path );
        std::vector<char> dataToWrite( 1_Mi );
        for ( auto& x : dataToWrite ) {
            x = static_cast<char>( rand() );
        }
        for ( size_t nBytesWritten = 0; nBytesWritten < size; nBytesWritten += dataToWrite.size() ) {
            file.write( dataToWrite.data(), dataToWrite.size() );
        }
        file.close();
    }

    ~TemporaryFile()
    {
        std::filesystem::remove( path );
    }

    const std::string path{ "/dev/shm/pragzip-benchmark-random-file.dat" };
    const size_t size;
};


void
benchmarkFileReader()
{
    TemporaryFile temporaryFile( 1_Gi );

    const auto times = repeatBenchmarks( [&] () { return benchmarkFileReader( temporaryFile.path,
                                                                              pragzip::BitReader::IOBUF_SIZE ); } );

    std::ofstream dataFile( "result-read-file.dat" );
    dataFile << "# dataSize/B chunkSize/B runtime/s\n";
    for ( const auto time : times ) {
        dataFile << temporaryFile.size << " " << pragzip::BitReader::IOBUF_SIZE << " " << time << "\n";
    }

    std::cout << "[File Reading] " << formatBandwidth( times, temporaryFile.size ) << "\n";
}


[[nodiscard]] std::pair<double, uint64_t>
benchmarkFileReaderParallel( ThreadPool&        threadPool,
                             const std::string& path,
                             const size_t       chunkSize )
{
    const auto t0 = now();

    const auto shareableFileReader = std::make_unique<SharedFileReader>( std::make_unique<StandardFileReader>( path ) );

    const auto readStrided =
        [chunkSize]
        ( const std::unique_ptr<FileReader>& fileReader,
          const size_t                       offset,
          const size_t                       stride ) -> uint64_t
        {
            std::vector<char> buffer( chunkSize );

            uint64_t checksum{ 0 };
            const auto fileSize = fileReader->size();
            for ( auto currentOffset = offset; currentOffset < fileSize; currentOffset += stride ) {
                fileReader->seek( currentOffset );
                const auto nBytesRead = fileReader->read( buffer.data(), buffer.size() );
                checksum += nBytesRead + buffer[buffer.size() / 2];
                if ( nBytesRead == 0 ) {
                    break;
                }
            }
            return checksum;
        };

    std::vector<std::future<uint64_t> > results;
    const auto parallelism = threadPool.size();
    for ( size_t i = 0; i < parallelism; ++i ) {
        auto sharedFileReader = std::unique_ptr<FileReader>( shareableFileReader->clone() );
        results.emplace_back( threadPool.submitTask(
            [chunkSize, i, parallelism, fileReader = std::move( sharedFileReader ), &readStrided] () mutable {
                return readStrided( fileReader, i * chunkSize, parallelism * chunkSize );
            }
        ) );
    }

    uint64_t checksum{ 0 };
    for ( auto& result : results ) {
        checksum += result.get();
    }

    return { duration( t0 ), checksum };
}


[[nodiscard]] std::vector<double>
benchmarkFileReaderParallelRepeatedly( const size_t fileSize,
                                       const size_t threadCount )
{
    TemporaryFile temporaryFile( fileSize );

    ThreadPool threadPool( threadCount );

    const auto times = repeatBenchmarks( [&] () {
        return benchmarkFileReaderParallel( threadPool, temporaryFile.path, pragzip::BitReader::IOBUF_SIZE );
    } );

    std::cout << "[Parallel File Reading] Using " << threadCount << " threads: "
              << formatBandwidth( times, temporaryFile.size ) << "\n";

    return times;
}


void
benchmarkFileReaderParallel()
{
    constexpr size_t fileSize{ 1_Gi };

    std::ofstream dataFile( "result-read-file-parallel.dat" );
    dataFile << "# threadCount dataSize/B chunkSize/B runtime/s\n";

    const std::vector<size_t> threadCounts = {
        1, 2, 3, 4,
        5, 6, 7, 8,
        10, 12, 14, 16,      // delta = 2
        20, 24, 28, 32,      // delta = 4
        40, 48, 56, 64,      // delta = 8
        80, 96, 112, 128,    // delta = 16
        160, 192, 224, 256,  // delta = 32
    };
    for ( const auto threadCount : threadCounts ) {
        if ( threadCount > std::thread::hardware_concurrency() ) {
            continue;
        }

        const auto times = benchmarkFileReaderParallelRepeatedly( fileSize, threadCount );
        for ( const auto time : times ) {
            dataFile << threadCount << " " << fileSize << " " << pragzip::BitReader::IOBUF_SIZE << " " << time << "\n";
        }

        std::cout << "[Parallel File Reading] " << formatBandwidth( times, fileSize ) << "\n";
        std::cerr << "Open file handles: " << getOpenFileHandleCount() << "\n";
    }
}


[[nodiscard]] std::pair<double, uint64_t>
benchmarkCountNewlines( const std::vector<char>& data )
{
    const auto t0 = now();

    const auto count = countNewlines( { data.data(), data.size() } );

    return { duration( t0 ), count };
}


void
benchmarkCountNewlines()
{
    const auto t0 = now();
    std::cout << "Initializing random data for benchmark... ";
    std::vector<char> data( 1_Gi );
    for ( auto& x : data ) {
        x = static_cast<char>( rand() );
    }
    const auto t1 = now();
    std::cout << "Done (" << duration( t0, t1 ) << " s)\n";

    const auto times = repeatBenchmarks( [&] () { return benchmarkCountNewlines( data ); } );

    std::ofstream dataFile( "result-count-newlines.dat" );
    dataFile << "# dataSize/B runtime/s\n";
    for ( const auto time : times ) {
        dataFile << data.size() << " " << time << "\n";
    }

    std::cout << "[Count newlines] " << formatBandwidth( times, data.size() ) << "\n";
}


[[nodiscard]] std::pair<double, uint64_t>
benchmarkApplyWindow( std::vector<uint16_t>       data,
                      const std::vector<uint8_t>& window )
{
    pragzip::deflate::DecodedData decoded;
    decoded.dataWithMarkers.emplace_back( std::move( data ) );

    const auto t0 = now();
    decoded.applyWindow( { window.data(), window.size() } );
    const auto checksum = decoded.data.front().at( decoded.data.front().size() / 2 );

    return { duration( t0 ), checksum };
}


void
benchmarkApplyWindow()
{
    const auto t0 = now();
    std::cout << "Initializing random data for benchmark... ";
    std::vector<uint16_t> data( 32_Mi );
    for ( auto& x : data ) {
        do {
            x = static_cast<uint16_t>( rand() );
        } while ( ( x > std::numeric_limits<uint8_t>::max() ) && ( x < pragzip::deflate::MAX_WINDOW_SIZE ) );
    }
    std::vector<uint8_t> window( 32_Ki );
    for ( auto& x : window ) {
        x = static_cast<uint8_t>( rand() );
    }
    std::cout << "Done (" << duration( t0 ) << " s)\n";

    const auto times = repeatBenchmarks( [&] () { return benchmarkApplyWindow( data, window ); } );

    std::ofstream dataFile( "result-apply-window.dat" );
    dataFile << "# dataSize/B runtime/s\n";
    for ( const auto time : times ) {
        dataFile << data.size() << " " << time << "\n";
    }

    std::cout << "[Apply window] Output(!) bandwidth of 8-bit symbols (input is 16-bit symbols): "
              << formatBandwidth( times, data.size() ) << "\n";
}


int
main()
{
    benchmarkDynamicBlockFinderPragzipLUT();
    benchmarkDynamicBlockFinderPragzip();
    benchmarkDynamicBlockFinderZlib();
    benchmarkDynamicBlockFinder();
    benchmarkFindUncompressedBlocks();

    benchmarkApplyWindow();

    benchmarkCountNewlines();

    benchmarkFileReaderParallel();
    //benchmarkFileReader();

    /* This is nice for testing. Probably should add this to the tests or maybe run this benchmark also as a test? */
    //benchmarkBitReaderBitReads( { 1, 2, 8, 15, 16 } );
    std::vector<uint8_t> nBitsToTest( 32 );
    std::iota( nBitsToTest.begin(), nBitsToTest.end(), 1 );
    benchmarkBitReaderBitReads( nBitsToTest );

    return 0;
}


/*
cmake --build . -- benchmarkSequential2023 && src/benchmarks/benchmarkSequential2023 2>&1 | grep benchmarks2023.log
sed -r '/[.]{3}/d; /Open file handles/d' benchmarks2023.log

[Dynamic block finder using skip LUT and pragzip] ( min: 18.9116, 20.1 +- 0.4, max: 20.8573 ) MB/s
[Dynamic block finder using pragzip] ( min: 3.29371, 3.72 +- 0.10, max: 3.87226 ) MB/s
[Dynamic block finder using zlib] ( min: 0.157237, 0.176 +- 0.003, max: 0.180353 ) MB/s
[Dynamic block finder] ( min: 55.817, 67.3 +- 2.1, max: 69.4555 ) MB/s
[Uncompressed block finder] ( min: 389.345, 430 +- 7, max: 444.314 ) MB/s
[Apply window] Output(!) bandwidth of 8-bit symbols (input is 16-bit symbols): ( min: 680.418, 791 +- 19, max: 810.93 ) MB/s
[Count newlines] ( min: 7525.09, 12400 +- 800, max: 12901.9 ) MB/s
[Parallel File Reading] Using 1 threads: ( min: 7083.82, 9480 +- 280, max: 9886.19 ) MB/s
[Parallel File Reading] ( min: 7083.82, 9480 +- 280, max: 9886.19 ) MB/s
[Parallel File Reading] Using 2 threads: ( min: 10804.6, 16700 +- 800, max: 17494.9 ) MB/s
[Parallel File Reading] ( min: 10804.6, 16700 +- 800, max: 17494.9 ) MB/s
[Parallel File Reading] Using 3 threads: ( min: 14343.9, 21000 +- 800, max: 22484.9 ) MB/s
[Parallel File Reading] ( min: 14343.9, 21000 +- 800, max: 22484.9 ) MB/s
[Parallel File Reading] Using 4 threads: ( min: 16590.9, 23700 +- 1100, max: 26073.6 ) MB/s
[Parallel File Reading] ( min: 16590.9, 23700 +- 1100, max: 26073.6 ) MB/s
[Parallel File Reading] Using 5 threads: ( min: 17327, 25100 +- 1600, max: 26901.9 ) MB/s
[Parallel File Reading] ( min: 17327, 25100 +- 1600, max: 26901.9 ) MB/s
[Parallel File Reading] Using 6 threads: ( min: 19075.5, 25700 +- 1200, max: 27137.2 ) MB/s
[Parallel File Reading] ( min: 19075.5, 25700 +- 1200, max: 27137.2 ) MB/s
[Parallel File Reading] Using 7 threads: ( min: 18933, 25600 +- 1100, max: 27392.8 ) MB/s
[Parallel File Reading] ( min: 18933, 25600 +- 1100, max: 27392.8 ) MB/s
[Parallel File Reading] Using 8 threads: ( min: 19842.3, 26200 +- 1300, max: 27854.6 ) MB/s
[Parallel File Reading] ( min: 19842.3, 26200 +- 1300, max: 27854.6 ) MB/s
[Parallel File Reading] Using 10 threads: ( min: 22016.6, 26700 +- 1100, max: 28596.4 ) MB/s
[Parallel File Reading] ( min: 22016.6, 26700 +- 1100, max: 28596.4 ) MB/s
[Parallel File Reading] Using 12 threads: ( min: 21673, 27200 +- 1300, max: 28914.1 ) MB/s
[Parallel File Reading] ( min: 21673, 27200 +- 1300, max: 28914.1 ) MB/s
[Parallel File Reading] Using 14 threads: ( min: 21706.7, 27100 +- 1100, max: 28362.9 ) MB/s
[Parallel File Reading] ( min: 21706.7, 27100 +- 1100, max: 28362.9 ) MB/s
[Parallel File Reading] Using 16 threads: ( min: 20218.1, 26300 +- 1300, max: 28079.3 ) MB/s
[Parallel File Reading] ( min: 20218.1, 26300 +- 1300, max: 28079.3 ) MB/s
[Parallel File Reading] Using 20 threads: ( min: 21475.5, 26700 +- 1000, max: 28022.7 ) MB/s
[Parallel File Reading] ( min: 21475.5, 26700 +- 1000, max: 28022.7 ) MB/s
[Parallel File Reading] Using 24 threads: ( min: 19052.8, 26500 +- 1000, max: 27849.2 ) MB/s
[Parallel File Reading] ( min: 19052.8, 26500 +- 1000, max: 27849.2 ) MB/s

== Benchmarking by reading 1 bits ==
[BitReader::read loop] Decoded with ( min: 122.498, 161 +- 5, max: 164.376 ) MB/s

== Benchmarking by reading 2 bits ==
[BitReader::read loop] Decoded with ( min: 266.72, 309 +- 6, max: 321.063 ) MB/s

== Benchmarking by reading 3 bits ==
[BitReader::read loop] Decoded with ( min: 425.604, 451 +- 3, max: 456.371 ) MB/s

== Benchmarking by reading 4 bits ==
[BitReader::read loop] Decoded with ( min: 520.838, 606 +- 11, max: 624.394 ) MB/s

== Benchmarking by reading 5 bits ==
[BitReader::read loop] Decoded with ( min: 534.35, 636 +- 12, max: 648.005 ) MB/s

== Benchmarking by reading 6 bits ==
[BitReader::read loop] Decoded with ( min: 636.798, 695 +- 9, max: 708.326 ) MB/s

== Benchmarking by reading 7 bits ==
[BitReader::read loop] Decoded with ( min: 873.463, 937 +- 10, max: 949.673 ) MB/s

== Benchmarking by reading 8 bits ==
[BitReader::read loop] Decoded with ( min: 1065.14, 1109 +- 9, max: 1122.96 ) MB/s

== Benchmarking by reading 9 bits ==
[BitReader::read loop] Decoded with ( min: 1035.21, 1125 +- 13, max: 1140.21 ) MB/s

== Benchmarking by reading 10 bits ==
[BitReader::read loop] Decoded with ( min: 946.785, 1229 +- 26, max: 1244.2 ) MB/s

== Benchmarking by reading 11 bits ==
[BitReader::read loop] Decoded with ( min: 1254.49, 1300 +- 10, max: 1315.62 ) MB/s

== Benchmarking by reading 12 bits ==
[BitReader::read loop] Decoded with ( min: 1266.81, 1393 +- 14, max: 1409.23 ) MB/s

== Benchmarking by reading 13 bits ==
[BitReader::read loop] Decoded with ( min: 1079.85, 1351 +- 27, max: 1403.08 ) MB/s

== Benchmarking by reading 14 bits ==
[BitReader::read loop] Decoded with ( min: 1504.5, 1597 +- 23, max: 1624.09 ) MB/s

== Benchmarking by reading 15 bits ==
[BitReader::read loop] Decoded with ( min: 1451.06, 1661 +- 23, max: 1690.45 ) MB/s

== Benchmarking by reading 16 bits ==
[BitReader::read loop] Decoded with ( min: 1358.66, 1920 +- 60, max: 1961.59 ) MB/s

== Benchmarking by reading 17 bits ==
[BitReader::read loop] Decoded with ( min: 1697.41, 1826 +- 25, max: 1858.6 ) MB/s

== Benchmarking by reading 18 bits ==
[BitReader::read loop] Decoded with ( min: 1407.06, 1830 +- 70, max: 1922.55 ) MB/s

== Benchmarking by reading 19 bits ==
[BitReader::read loop] Decoded with ( min: 1407.94, 1890 +- 90, max: 1967.88 ) MB/s

== Benchmarking by reading 20 bits ==
[BitReader::read loop] Decoded with ( min: 1540.93, 2020 +- 70, max: 2073.51 ) MB/s

== Benchmarking by reading 21 bits ==
[BitReader::read loop] Decoded with ( min: 1556.3, 2040 +- 80, max: 2095.13 ) MB/s

== Benchmarking by reading 22 bits ==
[BitReader::read loop] Decoded with ( min: 1410.89, 1850 +- 50, max: 1913.31 ) MB/s

== Benchmarking by reading 23 bits ==
[BitReader::read loop] Decoded with ( min: 1925.43, 2140 +- 40, max: 2216.76 ) MB/s

== Benchmarking by reading 24 bits ==
[BitReader::read loop] Decoded with ( min: 1567.21, 2320 +- 60, max: 2366.29 ) MB/s

== Benchmarking by reading 25 bits ==
[BitReader::read loop] Decoded with ( min: 1833.37, 2240 +- 50, max: 2293.54 ) MB/s

== Benchmarking by reading 26 bits ==
[BitReader::read loop] Decoded with ( min: 1904.12, 2310 +- 50, max: 2388.26 ) MB/s

== Benchmarking by reading 27 bits ==
[BitReader::read loop] Decoded with ( min: 1793.45, 2380 +- 60, max: 2424.04 ) MB/s

== Benchmarking by reading 28 bits ==
[BitReader::read loop] Decoded with ( min: 1845.9, 2440 +- 100, max: 2514.3 ) MB/s

== Benchmarking by reading 29 bits ==
[BitReader::read loop] Decoded with ( min: 1968.16, 2440 +- 60, max: 2487.26 ) MB/s

== Benchmarking by reading 30 bits ==
[BitReader::read loop] Decoded with ( min: 2355.82, 2486 +- 25, max: 2562.91 ) MB/s

== Benchmarking by reading 31 bits ==
[BitReader::read loop] Decoded with ( min: 1978.01, 2540 +- 70, max: 2610.21 ) MB/s

== Benchmarking by reading 32 bits ==
[BitReader::read loop] Decoded with ( min: 2282.07, 3030 +- 120, max: 3124.6 ) MB/s
*/
